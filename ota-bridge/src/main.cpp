/**
 * OTA Bridge Firmware
 *
 * Minimal firmware that provides:
 * - WiFi connection (via WiFiManager)
 * - Web-based OTA update capability
 * - ArduinoOTA for PlatformIO uploads
 *
 * This is designed to be small enough to fit in the OTA partition
 * of devices with limited space (like original GeekMagic firmware).
 *
 * After flashing this, you can then flash the full EpicWeatherBox
 * firmware via the /update web endpoint.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

// Web server on port 80
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Device info
const char* DEVICE_NAME = "OTA-Bridge";
const char* VERSION = "1.0.0";

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println();
    Serial.println(F("================================"));
    Serial.println(F("OTA Bridge Firmware v1.0.0"));
    Serial.println(F("================================"));

    // Initialize WiFi with captive portal
    WiFiManager wifiManager;
    wifiManager.setConfigPortalTimeout(180);  // 3 minute timeout

    Serial.println(F("Connecting to WiFi..."));
    if (!wifiManager.autoConnect("OTA-Bridge-Setup")) {
        Serial.println(F("Failed to connect. Restarting..."));
        delay(3000);
        ESP.restart();
    }

    Serial.print(F("Connected! IP: "));
    Serial.println(WiFi.localIP());

    // Setup ArduinoOTA
    ArduinoOTA.setHostname("ota-bridge");
    ArduinoOTA.onStart([]() {
        Serial.println(F("OTA Start"));
    });
    ArduinoOTA.onEnd([]() {
        Serial.println(F("\nOTA End"));
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
        else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
    });
    ArduinoOTA.begin();
    Serial.println(F("ArduinoOTA ready"));

    // Setup web server
    server.on("/", []() {
        String html = F("<!DOCTYPE html><html><head>");
        html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
        html += F("<title>OTA Bridge</title>");
        html += F("<style>");
        html += F("body{font-family:sans-serif;background:#1a1a2e;color:#fff;margin:0;padding:20px;text-align:center}");
        html += F("h1{color:#00D4FF}");
        html += F(".box{background:#141428;padding:20px;border-radius:10px;max-width:400px;margin:20px auto}");
        html += F("a{color:#00D4FF;display:block;margin:10px;padding:15px;background:#1a1a2e;border-radius:5px;text-decoration:none}");
        html += F("a:hover{background:#252540}");
        html += F(".info{color:#888;font-size:0.9em}");
        html += F("</style></head><body>");
        html += F("<h1>OTA Bridge</h1>");
        html += F("<div class='box'>");
        html += F("<p>This is a minimal firmware for OTA updates.</p>");
        html += F("<a href='/update'>Upload New Firmware</a>");
        html += F("<p class='info'>IP: ");
        html += WiFi.localIP().toString();
        html += F("<br>Free heap: ");
        html += String(ESP.getFreeHeap());
        html += F(" bytes<br>Version: ");
        html += VERSION;
        html += F("</p></div></body></html>");
        server.send(200, "text/html", html);
    });

    // Setup HTTP updater (web-based OTA)
    httpUpdater.setup(&server);

    server.begin();
    Serial.println(F("Web server started"));
    Serial.println(F(""));
    Serial.println(F("Ready for firmware upload!"));
    Serial.print(F("Open http://"));
    Serial.print(WiFi.localIP());
    Serial.println(F("/update"));
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
    yield();
}
