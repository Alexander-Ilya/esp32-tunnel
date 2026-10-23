/*
 * OpenPort — free permanent public port from openport.pw, no account, no relay of your own.
 *
 * On first boot the ESP32 registers with openport.pw and keeps its id + token in
 * flash, so it gets the SAME port after every restart:
 *   http://openport.pw:<port>      (printed to Serial)
 *
 * Every visitor connection is passed through byte for byte to your web server, so
 * pages of any size work (no TUN_MAX_BODY limit). Plain http, no access key —
 * anyone with the link can open it. openport.pw allows 3 new registrations per
 * IP per hour, so don't erase flash in a loop.
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

  tunnelSetup(OPENPORT);
}

void loop() {
  static bool shown = false;
  if (!shown && tunnelReady()) {
    shown = true;
    Serial.printf("Public link: %s\n", tunnelURL().c_str());
  }
  delay(100);
}
