// bore TCP tunnel provider — internal, included by esp32tunnel.h.
// Protocol (bore v0.5+): NUL-delimited JSON on control port 7835.
//   {"Hello":0} -> {"Hello":PORT};  {"Connection":UUID} -> new TCP, send {"Accept":UUID}, proxy raw.

#ifndef ESP32TUNNEL_BORE_H
#define ESP32TUNNEL_BORE_H

#ifndef BORE_CONTROL_PORT
#define BORE_CONTROL_PORT 7835
#endif
#ifndef BORE_MAX_PROXY
#define BORE_MAX_PROXY 4
#endif

static struct : _TunBase {
  String host;
  uint16_t remotePort;
  uint16_t localPort;
} _bore;

static WiFiClient _boreCtrl;
static WiFiClient _boreProxy[BORE_MAX_PROXY];
static WiFiClient _boreLocal[BORE_MAX_PROXY];

// MARK: NUL-delimited JSON

static String _boreRecvMsg(WiFiClient &c, int timeoutMs = 5000) {
  String msg;
  for (unsigned long t = millis(); millis() - t < (unsigned long)timeoutMs;) {
    if (!c.connected()) return "";
    if (!c.available()) { _DELAY(1); continue; }
    char ch = c.read();
    if (ch == '\0') return msg;
    msg += ch;
    if (msg.length() > 256) return "";
  }
  return "";
}

static void _boreSendMsg(WiFiClient &c, const String &msg) {
  c.print(msg);
  c.write((uint8_t)0);
}

// MARK: Proxy

static void _boreAccept(const String &uuid, int slot) {
  WiFiClient &proxy = _boreProxy[slot], &local = _boreLocal[slot];
  if (!_tcpConnectHost(proxy, _bore.host.c_str(), BORE_CONTROL_PORT)) return;
  _boreSendMsg(proxy, "{\"Accept\":\"" + uuid + "\"}");
  if (!_tcpConnect(local, IPAddress(127, 0, 0, 1), _bore.localPort)) { proxy.stop(); return; }
  local.setNoDelay(true);
  _tunPipe(proxy, local);
}

// MARK: Init + serve

static bool _boreInit() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (_boreCtrl.connected()) _boreCtrl.stop();
  if (!_tcpConnectHost(_boreCtrl, _bore.host.c_str(), BORE_CONTROL_PORT)) return false;
  _setKeepAlive(_boreCtrl);
  _boreSendMsg(_boreCtrl, "{\"Hello\":0}");          // 0 = any free port
  String resp = _boreRecvMsg(_boreCtrl, 10000);
  int port = _jInt(resp, "Hello", -1);
  if (port <= 0 || _jStr(resp, "Error").length()) { _boreCtrl.stop(); return false; }
  _bore.remotePort = port;
  _bore.url = "http://" + _bore.host + ":" + String(port);
  _bore.ready = true;
  return true;
}

static bool _boreServe() {
  if (!_boreCtrl.connected()) return false;
  if (!_boreCtrl.available()) return true;
  String msg = _boreRecvMsg(_boreCtrl, 3000);
  if (!msg.length()) return _boreCtrl.connected();
  String uuid = _jStr(msg, "Connection");             // anything else (Heartbeat) is ignored
  if (!uuid.length()) return true;
  for (int i = 0; i < BORE_MAX_PROXY; i++)
    if (!_boreProxy[i].connected() && !_boreLocal[i].connected()) { _boreAccept(uuid, i); break; }
  return true;                                        // all slots busy: drop it
}

// MARK: Internal API (dispatched from esp32tunnel.h)

static void _boreBegin(const char *host, uint16_t localPort) {
  if (_bore.started) return;
  _bore.host      = (host && strlen(host) > 0) ? host : "bore.pub";
  _bore.localPort = localPort > 0 ? localPort : TUN_PORT;
  _tunBegin(_bore);
}

static void _boreDrop() { _boreCtrl.stop(); }

static void _boreLoop() { _tunStep(_bore, 2000, 60000, _boreInit, _boreServe, _boreDrop); }

static void _boreStop() {
  _boreCtrl.stop();
  for (int i = 0; i < BORE_MAX_PROXY; i++) { _boreProxy[i].stop(); _boreLocal[i].stop(); }
  _tunEnd(_bore);
}

#endif
