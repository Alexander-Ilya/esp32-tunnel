/*
 * esp32tunnel_littlefs.h — serve files from LittleFS (or any fs::FS) in handler mode. Optional module.
 *
 *   #include <esp32tunnel.h>
 *   #include <esp32tunnel_littlefs.h>
 *
 *   String handler(const String &method, const String &path) {
 *     if (path == "/api/temp") return String(readTemp());   // your routes first
 *     return tunnelFile(path);                              // then /data files; "" = 404
 *   }
 *   void setup() { LittleFS.begin(); ... tunnelSetup(SELFHOST, handler, "host/id"); }
 *
 * "/" and "/dir/" serve index.html. Upload files with the LittleFS upload tool (data/ folder).
 * Proxy mode doesn't need this — use server.serveStatic("/", LittleFS, "/").
 * Limit: text files only (html/css/js/json/svg/txt), ≤TUN_MAX_BODY each — binary would break
 * the JSON text frame, so other extensions return 404.
 */

#ifndef ESP32TUNNEL_LITTLEFS_H
#define ESP32TUNNEL_LITTLEFS_H

#include "esp32tunnel.h"
#include <LittleFS.h>

// Content type for text assets; nullptr = not servable through the tunnel.
inline const char *tunnelMime(const String &path) {
  static const char *const types[][2] = {
    {".html", "text/html; charset=utf-8"}, {".htm", "text/html; charset=utf-8"},
    {".css", "text/css"}, {".js", "application/javascript"}, {".json", "application/json"},
    {".svg", "image/svg+xml"}, {".txt", "text/plain; charset=utf-8"},
  };
  for (auto &t : types) if (path.endsWith(t[0])) return t[1];
  return nullptr;
}

inline String tunnelFile(const String &path, fs::FS &fs = LittleFS) {
  String p = path;
  int q = p.indexOf('?');
  if (q >= 0) p.remove(q);
  if (p.endsWith("/")) p += "index.html";
  const char *type = tunnelMime(p);
  if (!type) return "";
  File f = fs.open(p, "r");
  if (!f || f.isDirectory() || f.size() > (size_t)_MAX_BODY_SIZE) return "";
  tunnelContentType(type);
  return f.readString();
}

#endif
