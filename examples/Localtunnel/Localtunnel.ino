/*
 * Localtunnel — free HTTPS link from loca.lt, no relay server of your own.
 *
 * The ESP32 asks loca.lt for a subdomain and receives visitors' requests
 * over plain TCP connections it keeps open:
 *   https://my-esp32.loca.lt
 *
 * If the subdomain is taken, loca.lt hands out a random one instead —
 * the link actually used is printed to Serial.
 *
 * Note: loca.lt may show first-time browser visitors its own reminder page.
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
const char *SUBDOMAIN = "my-esp32";          // https://my-esp32.loca.lt
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
  tunnelSetup(LOCALTUNNEL, SUBDOMAIN);
}

void loop() {
  static bool shown = false;
  if (!shown && tunnelReady()) {
    shown = true;
    Serial.printf("Public link: %s\n", tunnelURL().c_str());
  }

  delay(100);
}
