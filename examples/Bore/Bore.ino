/*
 * Bore — free public TCP tunnel through bore.pub, no account, no login.
 *
 * The ESP32 asks bore.pub for a public port and forwards every connection
 * on it to your local web server, byte for byte:
 *   http://bore.pub:<port>      (port is random, printed to Serial)
 *
 * Plain http and a new port on every restart — fine for quick tests.
 * Own server? Run `bore server` on it and use tunnelSetup(BORE, "your-server.com").
 * No access key here — anyone with the link can open it.
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

  tunnelSetup(BORE);                        // bore.pub, random port
  // tunnelSetup(BORE, "your-server.com");  // your own bore server
}

void loop() {
  static bool shown = false;
  if (!shown && tunnelReady()) {
    shown = true;
    Serial.printf("Public link: %s\n", tunnelURL().c_str());
  }

  delay(100);
}
