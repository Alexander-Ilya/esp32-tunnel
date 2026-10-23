/*
 * DualCore — keep doing your own work in loop() while the tunnel serves it.
 *
 * On ESP32 the tunnel runs in its own FreeRTOS task, so loop() is free for
 * sensors, displays, motors... This sketch "reads a sensor" in loop() and
 * the web page shows the latest value through the tunnel.
 *
 * Sharing data between loop() and web requests: they run in different tasks,
 * so keep shared values small and simple (a volatile int/float is fine).
 *
 * Task tuning, if needed (build flags or #define before the include):
 *   TUN_TASK_CORE  (default 1)   TUN_TASK_PRIO  (default 2, keep it above 1)
 *   TUN_TASK_STACK (default 8192)
 *
 * Board: ESP32
 */

#include <ESPAsyncWebServer.h>
#include <esp32tunnel.h>

// ── Change these ─────────────────────────────────────────────
const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASS = "YOUR_PASS";
const char *TUNNEL_SERVER = "https://esp32-tunnel-waa0.onrender.com/my-device";
// ─────────────────────────────────────────────────────────────

AsyncWebServer server(80);

volatile float temperature = 0;   // written by loop(), read by the web server
volatile unsigned long readings = 0;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "text/html",
      "<h1>Sensor</h1><p>" + String(temperature, 1) + " &deg;C after " + String(readings) +
      " readings</p><p><a href=\"status\">status</a></p>");
  });
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(200, "application/json",
      "{\"temperature\":" + String(temperature, 1) + ",\"readings\":" + String(readings) + "}");
  });
  server.begin();

  tunnelSetup(SELFHOST, TUNNEL_SERVER);   // from here on the tunnel runs by itself
}

void loop() {
  // Your application. It never has to call anything for the tunnel.
  temperature = 20.0 + 5.0 * sin(millis() / 10000.0);   // stand-in for a real sensor
  readings = readings + 1;

  static bool shown = false;
  if (!shown && tunnelReady()) {
    shown = true;
    String key = tunnelKey();
    Serial.printf("Public link: %s/%s\n", tunnelURL().c_str(), key.length() ? ("?key=" + key).c_str() : "");
  }
  delay(1000);
}
