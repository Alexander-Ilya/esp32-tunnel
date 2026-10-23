/*
 * SelfHosted — put your ESP32 web server on the internet through a relay.
 *
 * How it works:
 *   1. The ESP32 runs a normal web server (ESPAsyncWebServer) on port 80.
 *   2. tunnelSetup() opens a WebSocket to the relay and keeps it alive.
 *   3. Visitors open https://<relay>/<device-id>/ and the relay forwards
 *      each request to your web server, then sends the reply back.
 *
 * Open it from:
 *   Anywhere:  https://esp32-tunnel-waa0.onrender.com/my-device/?key=<key>
 *   Same WiFi: http://my-device.local/   (direct, faster, no key)
 * Both links are printed to Serial at boot.
 *
 * Board: ESP32
 */

#include <ESPAsyncWebServer.h>
#include <esp32tunnel.h>
#include <esp32tunnel_testpage.h>   // TUN_TEST_HTML: a tiny page that pings /ping

// ── Change these ─────────────────────────────────────────────
const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASS = "YOUR_PASS";

// Relay URL + your device id. Pick an id nobody else uses.
const char *TUNNEL_SERVER = "https://esp32-tunnel-waa0.onrender.com/my-device";
// ─────────────────────────────────────────────────────────────

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  // WiFi. Two settings that matter for a tunnel:
  //   - scan all channels so a mesh/repeater SSID joins the strongest AP
  //   - no modem sleep: replies get much faster (1 KB: 11 s -> 64 ms, measured)
  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());

  // Your web server — exactly as you'd write it without a tunnel.
  // Use relative links in your HTML (href="ping", not "/ping") so pages
  // also work under /<device-id>/ on the relay.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", TUN_TEST_HTML);
  });
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "application/json", "{\"pong\":true}");
  });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r) {   // the relay dashboard checks this
    r->send(200, "application/json",
      "{\"heap\":" + String(ESP.getFreeHeap()) + ",\"uptime\":" + String(millis() / 1000) + "}");
  });
  server.begin();

  // Start the tunnel. By default the ESP32 makes a random access key once and
  // keeps it in flash; visitors must add ?key=<key> (it's in the printed link).
  tunnelLog();                                           // print every request
  // tunnelPublic();                                     // ...or no key at all
  tunnelSetup(SELFHOST, TUNNEL_SERVER);
  // tunnelSetup(SELFHOST, TUNNEL_SERVER, "my-secret");  // ...or your own key
}

void loop() {
  // The tunnel runs in its own FreeRTOS task — loop() is yours.
  static bool shown = false;
  if (!shown && tunnelReady()) {
    shown = true;
    String key = tunnelKey();
    Serial.printf("Public link: %s/%s\n", tunnelURL().c_str(), key.length() ? ("?key=" + key).c_str() : "");
    Serial.printf("Local link:  %s/\n", tunnelLocalURL().c_str());
  }

  delay(100);
}
