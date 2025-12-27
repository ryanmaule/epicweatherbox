/**
 * EpicWeatherBox Recovery Firmware
 *
 * Minimal firmware for recovering bricked devices.
 * Features:
 * - WiFi AP mode (SSID: EpicWeatherBox-Recovery)
 * - Web-based OTA update at /update
 * - No display, no weather, no fancy features
 *
 * Use this to flash the full firmware when the stock firmware
 * doesn't have enough space for a direct OTA update.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <LittleFS.h>

// Configuration
#define AP_SSID "EpicWeatherBox-Recovery"
#define AP_PASS ""  // Open network for easy access

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Simple HTML page
const char* indexPage = R"(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Recovery Mode</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#fff;text-align:center;padding:20px}
h1{color:#00d4ff}
.btn{display:inline-block;background:#00d4ff;color:#1a1a2e;padding:15px 30px;text-decoration:none;border-radius:8px;font-size:1.2em;margin:10px}
.btn:hover{background:#00a8cc}
p{color:#888}
</style>
</head>
<body>
<h1>EpicWeatherBox Recovery</h1>
<p>This device is in recovery mode.</p>
<a class="btn" href="/update">Upload Firmware</a>
<p style="margin-top:40px">After uploading, the device will reboot automatically.</p>
</body>
</html>
)";

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== EpicWeatherBox Recovery Mode ===");

    // Initialize LittleFS (format if needed to clear space)
    if (!LittleFS.begin()) {
        Serial.println("LittleFS mount failed, formatting...");
        LittleFS.format();
        LittleFS.begin();
    }

    // Start WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("AP Started: %s\n", AP_SSID);
    Serial.printf("IP Address: %s\n", ip.toString().c_str());

    // Setup web server
    server.on("/", []() {
        server.send(200, "text/html", indexPage);
    });

    // Setup OTA update handler
    httpUpdater.setup(&server, "/update");

    server.begin();
    Serial.println("Web server started on port 80");
    Serial.println("Navigate to http://192.168.4.1/update to flash firmware");
}

void loop() {
    server.handleClient();
    yield();
}
