// localtunnel (loca.lt) provider — internal, included by esp32tunnel.h.

#ifndef ESP32TUNNEL_LOCALTUNNEL_H
#define ESP32TUNNEL_LOCALTUNNEL_H

// MARK: State

static struct : _TunBase {
  TunnelHandler handler;
  TunnelMode mode;
  String option, host, id;
  String ltHost;
  int ltPort;
  unsigned long ltRealloc;
} _lt;

static WiFiClient   _pool[TUN_POOL];
static unsigned long _poolAge[TUN_POOL];
static unsigned long _poolAliveCheck[TUN_POOL];
static WiFiClient    _ltLocal;

// TLS only for the loca.lt allocation API

static bool _tlsConnect(WiFiClientSecure &tls, const char *host, uint16_t port) {
  tls.setInsecure();
#ifdef ESP32
  return tls.connect(host, port, _CONNECT_MS);
#else
  tls.setTimeout(_CONNECT_MS);
  return tls.connect(host, port);
#endif
}

// MARK: Connection pool

// getsockopt at most every 5 s per slot
static bool _ltIsAlive(int i) {
  if (!_pool[i].connected()) return false;
  if (millis() - _poolAliveCheck[i] < 5000) return true;
  _poolAliveCheck[i] = millis();
#ifdef ESP32
  int fd = _pool[i].fd(); if (fd < 0) return false;
  int err = 0; socklen_t len = sizeof(err);
  getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
  return err == 0;
#else
  return true;
#endif
}

static bool _ltOpenSlot(int i, const String &host, int port) {
  if (!_tcpConnectHost(_pool[i], host.c_str(), port)) return false;
  _setKeepAlive(_pool[i]);
  _poolAge[i] = millis();
  _poolAliveCheck[i] = millis();
  return true;
}

static void _ltFillPool() {
  for (int i = 0; i < TUN_POOL; i++) {
    if (_lt.stop || _pool[i].connected()) continue;
    _ltOpenSlot(i, _lt.ltHost, _lt.ltPort);
    _DELAY(100);
  }
}

static void _ltStopAll() {
  for (int i = 0; i < TUN_POOL; i++) _pool[i].stop();
}

// MARK: HTTP handling

static bool _ltReadRequest(WiFiClient &c, String &method, String &path,
                           String &xff, String &ct, String &body) {
  char line[512];
  int n = _readLineBuf(c, line, sizeof(line), _HDR_TIMEOUT);
  char *s1 = strchr(line, ' ');
  if (!s1) return false;
  char *s2 = strchr(s1 + 1, ' ');
  if (!s2) return false;
  *s1 = 0; *s2 = 0;
  method = line;
  path   = s1 + 1;

  int contentLen = 0, hdrBytes = 0;
  while (c.connected() && hdrBytes < _MAX_HDR_SIZE) {
    if (!c.available()) { _DELAY(1); continue; }
    n = _readLineBuf(c, line, sizeof(line), _DRAIN_TIMEOUT);
    hdrBytes += n;
    if (n == 0) break;
    if (_hdrMatch(line, "x-forwarded-for:"))    { xff = String(line + 16); xff.trim(); }
    else if (_hdrMatch(line, "content-type:"))   { ct = String(line + 13); ct.trim(); }
    else if (_hdrMatch(line, "content-length:")) contentLen = atoi(line + 15);
  }

  if (contentLen > 0) {
    int toRead = min(contentLen, _MAX_BODY_SIZE);
    uint8_t buf[256]; int got = 0;
    unsigned long bt = millis();
    while (got < toRead && c.connected() && millis() - bt < _LOCAL_TIMEOUT) {
      if (c.available()) {
        int chunk = min(c.available(), min((int)sizeof(buf), toRead - got));
        int r = c.read(buf, chunk);
        if (r > 0) { body.concat((const char*)buf, r); got += r; bt = millis(); }
      } else _DELAY(1);
    }
  } else {
    int d = 0; while (c.available() && d < _MAX_BODY_SIZE) { c.read(); d++; }
  }
  return true;
}

static void _ltSendError(WiFiClient &c, int code, const char *msg) {
  c.printf("HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nConnection: close\r\n"
           "Content-Length: %d\r\n\r\n%s", code, _httpStatus(code), (int)strlen(msg), msg);
  c.flush(); c.stop();
}

static void _ltProxyHTTP(WiFiClient &remote) {
  String method, path, xff, ct, body;
  if (!_ltReadRequest(remote, method, path, xff, ct, body)) { remote.stop(); return; }
  String ip = xff.length() ? xff : remote.remoteIP().toString();
  _lt.lastIP = ip;

  if (_ltLocal.connected()) _ltLocal.stop();
  if (!_openLocal(_ltLocal, method, path, ip, body, ct)) { _ltSendError(remote, 502, "Local server unreachable"); return; }
  if (!_waitLocal(_ltLocal)) { _ltLocal.stop(); _ltSendError(remote, 504, "Local timeout"); return; }

  int code = 200;
  uint8_t buf[512]; bool first = true;
  while (_ltLocal.connected() || _ltLocal.available()) {
    if (_ltLocal.available()) {
      int n = _ltLocal.read(buf, sizeof(buf));
      if (n > 0) {
        if (first) { first = false; char *sp = (char*)memchr(buf, ' ', n); if (sp && sp - (char*)buf + 4 <= n) code = atoi(sp + 1); }
        remote.write(buf, n);
      }
    } else { if (!_ltLocal.connected()) break; _DELAY(1); }
  }
  _ltLocal.stop(); remote.flush(); remote.stop();
  _tunLog(ip, method, path, code);
}

static void _ltHandlerHTTP(WiFiClient &remote) {
  String method, path, xff, ct, body;
  if (!_ltReadRequest(remote, method, path, xff, ct, body)) { remote.stop(); return; }
  String ip = xff.length() ? xff : remote.remoteIP().toString();
  _lt.lastIP = ip;

  _tunType = _TUN_DEFAULT_TYPE;
  String respBody = _lt.handler(method, path);
  int code = respBody.length() ? 200 : 404;
  if (!respBody.length()) respBody = "Not Found";
  remote.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                "Connection: close\r\nContent-Length: %d\r\n\r\n",
                code, _httpStatus(code), _tunType.c_str(), (int)respBody.length());
  remote.print(respBody);
  remote.flush(); remote.stop();
  _tunLog(ip, method, path, code);
}

// MARK: Allocation + init + serve

static bool _ltAllocate() {
  WiFiClientSecure tls;
  if (!_tlsConnect(tls, "loca.lt", 443)) return false;

  String sub = _lt.option.length() ? _lt.option : ("esp32-" + _chipID());
  tls.printf("GET /api/tunnels/%s/0 HTTP/1.1\r\nHost: loca.lt\r\nConnection: close\r\n\r\n", sub.c_str());

  unsigned long t0 = millis();
  while (!tls.available() && millis() - t0 < _CONNECT_MS) _DELAY(10);
  if (!tls.available()) { tls.stop(); return false; }

  char line[256];
  _readLineBuf(tls, line, sizeof(line), _CONNECT_MS);
  bool ok = strstr(line, "200") != nullptr;
  while (tls.connected()) {
    int n = _readLineBuf(tls, line, sizeof(line), _DRAIN_TIMEOUT);
    if (n == 0) break;
  }
  String respBody; respBody.reserve(256);
  uint8_t rb[128]; while (tls.available()) {
    int n = tls.read(rb, sizeof(rb));
    if (n > 0) respBody.concat((const char*)rb, n);
  }
  tls.stop();
  if (!ok) return false;

  _lt.ltPort = _jInt(respBody, "port", 0);
  String url = _jStr(respBody, "url");
  if (_lt.ltPort <= 0 || !url.length()) return false;

  const char *proto = strstr(url.c_str(), "://");
  const char *hostStart = proto ? proto + 3 : url.c_str();
  const char *slash = strchr(hostStart, '/');
  _lt.ltHost = slash ? url.substring(hostStart - url.c_str(), slash - url.c_str()) : String(hostStart);

  _lt.url  = url;
  _lt.host = _lt.ltHost;
  _lt.id   = sub;
  return true;
}

static bool _ltInit() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (!_ltAllocate()) return false;
  _lt.ready = true;
  _lt.ltRealloc = millis();
  _ltFillPool();
  return true;
}

static bool _ltServe() {
  for (int i = 0; i < TUN_POOL; i++) {
    if (!_pool[i].connected()) continue;
    if (!_ltIsAlive(i) || millis() - _poolAge[i] > TUN_STALE) {
      _pool[i].stop(); _ltOpenSlot(i, _lt.ltHost, _lt.ltPort); continue;
    }
    if (!_pool[i].available()) continue;
    if (_lt.handler) _ltHandlerHTTP(_pool[i]); else _ltProxyHTTP(_pool[i]);
    _DELAY(1);
    _ltOpenSlot(i, _lt.ltHost, _lt.ltPort);
  }
  if (TUN_REALLOC > 0 && millis() - _lt.ltRealloc > (unsigned long)TUN_REALLOC * 3600000UL) return false;
  return true;
}

// MARK: Internal API (dispatched from esp32tunnel.h)

static void _ltBegin(TunnelHandler handler, const char *subdomain, TunnelMode mode) {
  if (_lt.started) return;
  _lt.handler = handler;
  _lt.mode    = mode;
  _lt.option  = subdomain ? subdomain : "";
  _tunBegin(_lt);
}

static void _ltLoop() {
  // STRICT waits longer for a taken subdomain to free up
  unsigned long cap = (_lt.mode == TUN_STRICT && _lt.option.length()) ? 300000UL : 60000UL;
  _tunStep(_lt, 2000, cap, _ltInit, _ltServe, _ltStopAll);
}

static void _ltStop() {
  _ltStopAll();
  _tunEnd(_lt);
}

#endif
