/*
 * HandlerMode — answer requests in a function, no web server library.
 *
 * Instead of running ESPAsyncWebServer, give tunnelSetup() a handler:
 *   String handler(const String &method, const String &path)
 * It gets every request's method and path (with ?query), and returns the
 * response body. Return "" for 404. Lighter: no web server, ~70 KB less flash.
 *
 * Content type defaults to text/html; call tunnelContentType() inside the
 * handler to change it for that reply.
 *
 * Serving files? See <esp32tunnel_progmem.h> and <esp32tunnel_littlefs.h>.
 *
 * Board: ESP32
 */

#include <esp32tunnel.h>

// ── Change these ─────────────────────────────────────────────
const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASS = "YOUR_PASS";
const char *TUNNEL_SERVER = "https://esp32-tunnel-waa0.onrender.com/my-device";
// ─────────────────────────────────────────────────────────────

// Called for every request that comes through the tunnel.
String handleRequest(const String &method, const String &path) {
  if (path == "/") {
    // Relative link ("status", not "/status") so it works under /<device-id>/
    return "<h1>Hello from ESP32</h1><p><a href=\"status\">status</a></p>";
  }
  if (path == "/status") {
    tunnelContentType("application/json");
    return "{\"heap\":" + String(ESP.getFreeHeap()) + ",\"uptime\":" + String(millis() / 1000) + "}";
  }
  return "";   // anything else: 404 Not Found
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);   // join the strongest AP for this SSID
  WiFi.setSleep(false);                        // much faster replies
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());

  tunnelLog();
  tunnelSetup(SELFHOST, handleRequest, TUNNEL_SERVER);   // handler instead of a web server
}

void loop() {
  static bool shown = false;
  if (!shown && tunnelReady()) {
    shown = true;
    String key = tunnelKey();
    Serial.printf("Public link: %s/%s\n", tunnelURL().c_str(), key.length() ? ("?key=" + key).c_str() : "");
  }

  delay(100);
}
