/*
 * Tunnelmole — free HTTPS link from tunnelmole.com, no account, no relay of your own.
 *
 * The ESP32 opens a secure WebSocket to tunnelmole and gets a random link:
 *   https://<random>.tunnelmole.net      (printed to Serial)
 *
 * Works with a web server (like here) or a handler function:
 *   tunnelSetup(TUNNELMOLE, handler, nullptr);
 * Each reply is limited to TUN_MAX_BODY (16 KB by default). No access key —
 * anyone with the link can open it. The link changes when the device reconnects.
 *
 * Board: ESP32
 */

#include <ESPAsyncWebServer.h>
#include <esp32tunnel.h>
#include <esp32tunnel_testpage.h>

// ── Change these ─────────────────────────────────────────────
const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASS = "YOUR_PASS";
// ─────────────────────────────────────────────────────────────

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html", TUN_TEST_HTML);
  });
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "application/json", "{\"pong\":true}");
  });
  server.begin();

  tunnelLog();
  tunnelSetup(TUNNELMOLE);
}

void loop() {
  static bool shown = false;
  if (!shown && tunnelReady()) {
    shown = true;
    Serial.printf("Public link: %s\n", tunnelURL().c_str());
  }
  delay(100);
}
