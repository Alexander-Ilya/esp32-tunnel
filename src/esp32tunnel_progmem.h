/*
 * esp32tunnel_progmem.h — serve PROGMEM (flash) assets in handler mode. Optional module.
 *
 *   #include <esp32tunnel.h>
 *   #include <esp32tunnel_progmem.h>
 *
 *   const char INDEX_HTML[] PROGMEM = "<h1>Hello</h1>";
 *   const char APP_JS[]     PROGMEM = "console.log('hi')";
 *
 *   String handler(const String &method, const String &path) {
 *     if (path == "/")       return tunnelProgmem(INDEX_HTML, "text/html");
 *     if (path == "/app.js") return tunnelProgmem(APP_JS, "application/javascript");
 *     return "";   // 404
 *   }
 *
 * Proxy mode doesn't need this — ESPAsyncWebServer already does r->send(200, type, INDEX_HTML).
 * Limit: text assets only, ≤TUN_MAX_BODY per response.
 */

#ifndef ESP32TUNNEL_PROGMEM_H
#define ESP32TUNNEL_PROGMEM_H

#include "esp32tunnel.h"

inline String tunnelProgmem(const char *data, const char *type) {
  tunnelContentType(type);
  return String(FPSTR(data));
}

#endif
