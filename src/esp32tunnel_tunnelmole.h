// tunnelmole.com provider — internal, included by esp32tunnel.h. ESP32 only.
// Free random https://<x>.tunnelmole.net link, no account. Rides the self-hosted WebSocket
// session (_sh: same socket loop), with tunnelmole's JSON messages:
//   -> {"type":"initialise","clientId"}       <- {"type":"hostnameAssigned","hostname"}
//   <- {"type":"forwardedRequest","requestId","url","method","headers","body":base64}
//   -> {"type":"forwardedResponse","requestId","url","statusCode","headers","body":base64}

#ifndef ESP32TUNNEL_TUNNELMOLE_H
#define ESP32TUNNEL_TUNNELMOLE_H
#ifdef ESP32

#include <mbedtls/base64.h>

static String _tmB64Decode(const String &in) {
  String out;
  size_t cap = in.length() / 4 * 3 + 3, n = 0;
  uint8_t *buf = (uint8_t *)malloc(cap);
  if (buf && mbedtls_base64_decode(buf, cap, &n, (const uint8_t *)in.c_str(), in.length()) == 0)
    out.concat((const char *)buf, n);
  free(buf);
  return out;
}

// Base64 straight onto the socket in 768-byte bites (-> 1024 chars), no encoded copy in RAM.
static bool _tmSendB64(const String &s) {
  static uint8_t out[1028];
  for (size_t i = 0; i < s.length(); i += 768) {
    size_t n = 0;
    mbedtls_base64_encode(out, sizeof out, &n, (const uint8_t *)s.c_str() + i, min((size_t)768, s.length() - i));
    if (!_shSend(out, n)) return false;
  }
  return true;
}

static void _tmRespond(const String &rid, const String &url, int code, const String &body, const String &type) {
  String pre = "{\"type\":\"forwardedResponse\",\"requestId\":\"" + rid + "\",\"statusCode\":" + String(code) + ",\"url\":\"";
  static const char mid[] = "\",\"headers\":{\"content-type\":\"", mid2[] = "\"},\"body\":\"";
  size_t len = pre.length() + _escLen(url) + strlen(mid) + _escLen(type) + strlen(mid2) +
               (body.length() + 2) / 3 * 4 + 2;
  _shFrame(0x1, len) && _shSend((const uint8_t *)pre.c_str(), pre.length()) && _shSendEscaped(url) &&
    _shSend((const uint8_t *)mid, strlen(mid)) && _shSendEscaped(type) &&
    _shSend((const uint8_t *)mid2, strlen(mid2)) && _tmSendB64(body) && _shSend((const uint8_t *)"\"}", 2);
}

static void _tmMessage(const String &msg) {
  String kind = _jStr(msg, "type");
  if (kind == "hostnameAssigned") {
    _sh.url = "https://" + _jStr(msg, "hostname");
    _sh.ready = true;
    return;
  }
  if (kind != "forwardedRequest") return;
  String rid = _jStr(msg, "requestId"), method = _jStr(msg, "method"), path = _jStr(msg, "url");
  if (!rid.length() || !method.length()) return;
  if (!path.length()) path = "/";
  int code; String body, type;
  if (path.indexOf("..") >= 0) { code = 400; body = "Bad path"; type = "text/plain"; }
  else {
    String ct = _jStr(msg, "Content-Type");
    if (!ct.length()) ct = _jStr(msg, "content-type");
    _sh.lastIP = _jStr(msg, "X-Forwarded-For");
    _tunFetch(method, path, _sh.lastIP, _tmB64Decode(_jStr(msg, "body")), ct, msg, code, body, type);
  }
  _tmRespond(rid, path, code, body, type);
}

static bool _tmInit() {
  if (WiFi.status() != WL_CONNECTED || !_shConnect("/")) return false;
  String hello = "{\"type\":\"initialise\",\"clientId\":\"" + _tunNvsRandom("tm_id") + "\"}";
  return _shFrame(0x1, hello.length()) && _shSend((const uint8_t *)hello.c_str(), hello.length());
}   // ready once hostnameAssigned arrives

static bool _tmServe() { return _wsServe(_tmMessage); }

// MARK: Internal API (dispatched from esp32tunnel.h)

static void _tmBegin(TunnelHandler handler) {
  if (_sh.started) return;
  _sh.handler = handler;
  _sh.ws = &_shWsPlain;
  _tunBegin(_sh);
  _sh.useTLS = true;
  _sh.host = "service.tunnelmole.com";
  _sh.shPort = 8083;
}

static void _tmLoop() { _tunStep(_sh, 2000, 60000, _tmInit, _tmServe, _shDrop); }

#endif
#endif
