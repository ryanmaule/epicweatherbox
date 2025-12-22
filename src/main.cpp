/**
 * EpicWeatherBox Firmware
 *
 * Custom firmware for SmallTV-Ultra hardware
 *
 * Features:
 * - WiFi setup via captive portal
 * - Web-based configuration
 * - 7-day weather forecast (vs original 3-day)
 * - Dual location weather support
 * - Time display with NTP sync
 * - OTA firmware updates (ArduinoOTA + Web)
 *
 * CRITICAL: This firmware includes OTA update capability.
 * The USB-C port on SmallTV-Ultra is power-only (no data),
 * so OTA is the ONLY way to update firmware after initial flash.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Enable hardware watchdog
extern "C" {
    #include <user_interface.h>
}

// Local includes
#include "config.h"
#include "ota.h"
#include "weather.h"
// #include "display.h"  // TODO: Phase C

// Note: FIRMWARE_VERSION and DEVICE_NAME are defined in config.h

// Objects
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// Watchdog timeout (8 seconds is max for ESP8266)
#define WDT_TIMEOUT_SECONDS 8

// Forward declarations
void setupWiFi();
void setupWebServer();
void setupWatchdog();
void handleRoot();
void handleAdmin();
void handleDisplayPreview();
void handleNotFound();
void feedWatchdog();

void setup() {
    // Initialize serial first for debugging
    Serial.begin(115200);
    delay(100);  // Let serial stabilize

    Serial.println();
    Serial.println(F("================================================"));
    Serial.printf_P(PSTR("%s Custom Firmware v%s\n"), DEVICE_NAME, FIRMWARE_VERSION);
    Serial.println(F("================================================"));
    Serial.println(F("[BOOT] Starting initialization..."));

    // Initialize hardware watchdog
    setupWatchdog();
    Serial.println(F("[BOOT] Watchdog timer enabled"));

    // Initialize LittleFS (SPIFFS is deprecated)
    Serial.print(F("[BOOT] Mounting LittleFS... "));
    if (!LittleFS.begin()) {
        Serial.println(F("FAILED!"));
        // Continue anyway - we can still work without filesystem
    } else {
        Serial.println(F("OK"));
        FSInfo fs_info;
        LittleFS.info(fs_info);
        Serial.printf_P(PSTR("[BOOT] LittleFS: %u/%u bytes used\n"),
                       fs_info.usedBytes, fs_info.totalBytes);
    }

    feedWatchdog();

    // Initialize display
    // TODO: Phase 2 - Display driver
    // setupDisplay();
    Serial.println(F("[BOOT] Display: TODO (Phase 2)"));

    // Initialize WiFi (this can take a while)
    Serial.println(F("[BOOT] Starting WiFi..."));
    setupWiFi();

    feedWatchdog();

    // Only proceed if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        // Initialize OTA - CRITICAL for future updates!
        Serial.println(F("[BOOT] Initializing OTA..."));
        initArduinoOTA(OTA_HOSTNAME);

        // Initialize NTP
        Serial.println(F("[BOOT] Starting NTP client..."));
        timeClient.begin();
        timeClient.update();  // Force initial update

        // Initialize web server (includes OTA web interface)
        Serial.println(F("[BOOT] Starting web server..."));
        setupWebServer();

        // Initialize web OTA (add /update endpoint)
        initWebOTA(&server);

        // Initialize weather system
        Serial.println(F("[BOOT] Initializing weather..."));
        initWeather();

        // Fetch initial weather data
        Serial.println(F("[BOOT] Fetching initial weather..."));
        forceWeatherUpdate();
    }

    feedWatchdog();

    // Print startup summary
    Serial.println(F("================================================"));
    Serial.println(F("[BOOT] Initialization complete!"));
    Serial.printf_P(PSTR("[BOOT] Free heap: %u bytes\n"), ESP.getFreeHeap());
    Serial.printf_P(PSTR("[BOOT] Chip ID: %08X\n"), ESP.getChipId());
    Serial.printf_P(PSTR("[BOOT] Flash size: %u bytes\n"), ESP.getFlashChipRealSize());

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf_P(PSTR("[BOOT] IP Address: %s\n"), WiFi.localIP().toString().c_str());
        Serial.printf_P(PSTR("[BOOT] Web UI: http://%s/\n"), WiFi.localIP().toString().c_str());
        Serial.printf_P(PSTR("[BOOT] OTA Update: http://%s/update\n"), WiFi.localIP().toString().c_str());
    }
    Serial.println(F("================================================"));
}

void loop() {
    // Feed watchdog at start of loop
    feedWatchdog();

    // Handle OTA updates - CRITICAL, must be called frequently
    handleOTA();

    // Skip other processing during OTA to ensure stability
    if (isOTAInProgress()) {
        return;
    }

    // Handle web server
    server.handleClient();

    // Update NTP (library handles update interval internally)
    timeClient.update();

    // Update weather data (checks interval internally)
    updateWeather();

    // TODO: Phase C - Update display
    // updateDisplay();

    // Small yield to prevent watchdog issues
    yield();
}

/**
 * Setup hardware watchdog timer
 * This will reset the ESP if it hangs for too long
 */
void setupWatchdog() {
    // Disable software watchdog (we're using hardware)
    ESP.wdtDisable();

    // Enable hardware watchdog with 8-second timeout
    ESP.wdtEnable(WDTO_8S);
}

/**
 * Feed the watchdog timer
 * Call this regularly in loop() to prevent reset
 */
void feedWatchdog() {
    ESP.wdtFeed();
    yield();  // Also yield to system tasks
}

/**
 * Setup WiFi using WiFiManager
 * Creates AP for configuration if no saved credentials
 */
void setupWiFi() {
    WiFiManager wifiManager;

    // Reset saved settings for testing (uncomment if needed)
    // wifiManager.resetSettings();

    // Set AP name
    String apName = String(DEVICE_NAME);

    // Set timeout for config portal (5 minutes)
    wifiManager.setConfigPortalTimeout(300);

    // Set minimum signal quality to show networks (%)
    wifiManager.setMinimumSignalQuality(15);

    // Set static IP if desired (optional)
    // wifiManager.setSTAStaticIPConfig(IPAddress(192,168,1,99),
    //                                   IPAddress(192,168,1,1),
    //                                   IPAddress(255,255,255,0));

    // Custom parameters could be added here for API keys, locations, etc.
    // WiFiManagerParameter custom_api_key("apikey", "Weather API Key", "", 40);
    // wifiManager.addParameter(&custom_api_key);

    Serial.println(F("[WIFI] Starting WiFi Manager..."));
    Serial.printf_P(PSTR("[WIFI] AP Name: %s\n"), apName.c_str());

    // Feed watchdog before potentially long operation
    feedWatchdog();

    // Try to connect, or start config portal
    // autoConnect will block until connected or timeout
    if (!wifiManager.autoConnect(apName.c_str())) {
        Serial.println(F("[WIFI] Failed to connect and hit timeout"));
        Serial.println(F("[WIFI] Restarting in 3 seconds..."));
        delay(3000);
        ESP.restart();
    }

    Serial.println(F("[WIFI] Connected successfully!"));
    Serial.printf_P(PSTR("[WIFI] SSID: %s\n"), WiFi.SSID().c_str());
    Serial.printf_P(PSTR("[WIFI] IP: %s\n"), WiFi.localIP().toString().c_str());
    Serial.printf_P(PSTR("[WIFI] RSSI: %d dBm\n"), WiFi.RSSI());
    Serial.printf_P(PSTR("[WIFI] MAC: %s\n"), WiFi.macAddress().c_str());
}

/**
 * Setup web server routes
 */
void setupWebServer() {
    // Main page
    server.on("/", HTTP_GET, handleRoot);

    // API endpoints
    server.on("/api/status", HTTP_GET, []() {
        JsonDocument doc;
        doc["version"] = FIRMWARE_VERSION;
        doc["device"] = DEVICE_NAME;
        doc["heap"] = ESP.getFreeHeap();
        doc["uptime"] = millis() / 1000;
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["ssid"] = WiFi.SSID();
        doc["mac"] = WiFi.macAddress();
        doc["chipId"] = String(ESP.getChipId(), HEX);
        doc["flashSize"] = ESP.getFlashChipRealSize();
        doc["sketchSize"] = ESP.getSketchSize();
        doc["freeSketchSpace"] = ESP.getFreeSketchSpace();

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    server.on("/api/time", HTTP_GET, []() {
        JsonDocument doc;
        doc["epoch"] = timeClient.getEpochTime();
        doc["formatted"] = timeClient.getFormattedTime();
        doc["day"] = timeClient.getDay();

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Weather API endpoint - returns all locations
    server.on("/api/weather", HTTP_GET, []() {
        JsonDocument doc;

        // Return all locations as array
        JsonArray locations = doc["locations"].to<JsonArray>();
        for (int i = 0; i < getLocationCount(); i++) {
            JsonObject loc = locations.add<JsonObject>();
            JsonDocument locDoc;
            weatherToJson(getWeather(i), locDoc);
            loc.set(locDoc.as<JsonObject>());
        }

        // Add metadata
        doc["locationCount"] = getLocationCount();
        doc["maxLocations"] = MAX_WEATHER_LOCATIONS;
        doc["nextUpdateIn"] = getNextUpdateIn() / 1000;  // seconds
        doc["updateInterval"] = WEATHER_UPDATE_INTERVAL_MS / 1000;  // seconds

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Force weather refresh endpoint
    server.on("/api/weather/refresh", HTTP_GET, []() {
        bool success = forceWeatherUpdate();

        JsonDocument doc;
        doc["success"] = success;
        doc["message"] = success ? "Weather updated" : "Update failed";

        String response;
        serializeJson(doc, response);
        server.send(success ? 200 : 500, "application/json", response);
    });

    // Config API - GET returns location settings, POST saves them
    server.on("/api/config", HTTP_GET, []() {
        JsonDocument doc;

        // Return all locations as array
        JsonArray locArray = doc["locations"].to<JsonArray>();
        for (int i = 0; i < getLocationCount(); i++) {
            const WeatherLocation& loc = getLocation(i);
            JsonObject l = locArray.add<JsonObject>();
            l["name"] = loc.name;
            l["lat"] = loc.latitude;
            l["lon"] = loc.longitude;
            l["enabled"] = loc.enabled;
        }

        // Metadata
        doc["locationCount"] = getLocationCount();
        doc["maxLocations"] = MAX_WEATHER_LOCATIONS;

        // Display settings
        doc["useCelsius"] = getUseCelsius();

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    server.on("/api/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"No data\"}");
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }

        // Check if using new array format
        if (doc["locations"].is<JsonArray>()) {
            JsonArray locArray = doc["locations"].as<JsonArray>();

            // Validate
            if (locArray.size() == 0) {
                server.send(400, "application/json", "{\"success\":false,\"message\":\"At least one location required\"}");
                return;
            }
            if (locArray.size() > MAX_WEATHER_LOCATIONS) {
                server.send(400, "application/json", "{\"success\":false,\"message\":\"Max 5 locations\"}");
                return;
            }

            // Clear existing locations and add new ones
            clearLocations();

            bool first = true;
            for (JsonObject loc : locArray) {
                const char* name = loc["name"];
                float lat = loc["lat"] | 0.0f;
                float lon = loc["lon"] | 0.0f;

                if (name && strlen(name) > 0 && (lat != 0 || lon != 0)) {
                    if (first) {
                        // Update first location (can't remove it)
                        updateLocation(0, name, lat, lon);
                        first = false;
                    } else {
                        addLocation(name, lat, lon);
                    }
                }
            }
        }
        // Fall back to old format for backward compatibility
        else if (doc["primary"].is<JsonObject>()) {
            JsonObject primary = doc["primary"];
            const char* name = primary["name"];
            float lat = primary["lat"] | 0.0f;
            float lon = primary["lon"] | 0.0f;
            if (name && (lat != 0 || lon != 0)) {
                updateLocation(0, name, lat, lon);
            }

            // Handle secondary if present
            if (doc["secondary"].is<JsonObject>()) {
                JsonObject secondary = doc["secondary"];
                bool enabled = secondary["enabled"] | false;
                if (enabled) {
                    const char* secName = secondary["name"];
                    float secLat = secondary["lat"] | 0.0f;
                    float secLon = secondary["lon"] | 0.0f;
                    if (secName && (secLat != 0 || secLon != 0)) {
                        if (getLocationCount() < 2) {
                            addLocation(secName, secLat, secLon);
                        } else {
                            updateLocation(1, secName, secLat, secLon);
                        }
                    }
                } else if (getLocationCount() > 1) {
                    // Remove secondary if disabled
                    removeLocation(1);
                }
            }
        }

        // Update display settings
        if (doc.containsKey("useCelsius")) {
            setUseCelsius(doc["useCelsius"] | false);
        }

        // Save and refresh weather
        saveWeatherConfig();
        forceWeatherUpdate();

        server.send(200, "application/json", "{\"success\":true,\"message\":\"Config saved\"}");
    });

    // Admin page - minimal location config
    server.on("/admin", HTTP_GET, handleAdmin);

    // Display preview page - simulates the TFT display in browser
    server.on("/preview", HTTP_GET, handleDisplayPreview);

    // Version endpoint (original firmware compatibility)
    server.on("/v.json", HTTP_GET, []() {
        JsonDocument doc;
        doc["v"] = FIRMWARE_VERSION;

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Geocoding API - search for city by name
    server.on("/api/geocode", HTTP_GET, []() {
        if (!server.hasArg("q")) {
            server.send(400, "application/json", "{\"error\":\"Missing query parameter 'q'\"}");
            return;
        }

        String query = server.arg("q");
        if (query.length() < 2) {
            server.send(400, "application/json", "{\"error\":\"Query too short\"}");
            return;
        }

        // URL encode the query
        String encodedQuery = "";
        for (size_t i = 0; i < query.length(); i++) {
            char c = query.charAt(i);
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encodedQuery += c;
            } else if (c == ' ') {
                encodedQuery += "%20";
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
                encodedQuery += buf;
            }
        }

        // Build Open-Meteo geocoding URL
        // Request 20 results from API to include international cities (Canada, etc.)
        String url = "http://geocoding-api.open-meteo.com/v1/search?name=";
        url += encodedQuery;
        url += "&count=20&language=en&format=json";

        Serial.printf("[GEOCODE] Searching: %s\n", query.c_str());

        WiFiClient client;
        HTTPClient http;
        http.setTimeout(10000);

        if (!http.begin(client, url)) {
            server.send(500, "application/json", "{\"error\":\"HTTP begin failed\"}");
            return;
        }

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            http.end();
            server.send(500, "application/json", "{\"error\":\"Geocoding request failed\"}");
            return;
        }

        String payload = http.getString();
        http.end();

        // Parse and simplify the response
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            server.send(500, "application/json", "{\"error\":\"JSON parse failed\"}");
            return;
        }

        // Build simplified response
        JsonDocument response;
        JsonArray results = response["results"].to<JsonArray>();

        JsonArray apiResults = doc["results"];
        if (apiResults) {
            for (JsonObject r : apiResults) {
                JsonObject item = results.add<JsonObject>();
                item["name"] = r["name"];
                item["lat"] = r["latitude"];
                item["lon"] = r["longitude"];

                // Build display string: "City, State, Country"
                String display = r["name"].as<String>();
                if (r.containsKey("admin1") && !r["admin1"].isNull()) {
                    display += ", ";
                    display += r["admin1"].as<String>();
                }
                if (r.containsKey("country") && !r["country"].isNull()) {
                    display += ", ";
                    display += r["country"].as<String>();
                }
                item["display"] = display;
            }
        }

        response["count"] = results.size();

        String responseStr;
        serializeJson(response, responseStr);
        server.send(200, "application/json", responseStr);
    });

    // Reboot endpoint
    server.on("/reboot", HTTP_GET, []() {
        server.send(200, "text/html",
            F("<!DOCTYPE html><html><head>"
              "<meta name='viewport' content='width=device-width, initial-scale=1'>"
              "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
              "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
              ".box{text-align:center;}</style></head><body><div class='box'>"
              "<h1>Rebooting...</h1><p>Please wait, redirecting in 10 seconds.</p>"
              "<script>setTimeout(function(){location.href='/';},10000);</script>"
              "</div></body></html>"));
        delay(500);
        ESP.restart();
    });

    // Reset WiFi settings endpoint
    server.on("/reset", HTTP_GET, []() {
        server.send(200, "text/html",
            F("<!DOCTYPE html><html><head>"
              "<meta name='viewport' content='width=device-width, initial-scale=1'>"
              "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
              "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
              ".box{text-align:center;}</style></head><body><div class='box'>"
              "<h1>Factory Reset</h1><p>WiFi settings cleared. Rebooting...</p>"
              "<p>Connect to EpicWeatherBox AP to reconfigure.</p>"
              "</div></body></html>"));
        delay(500);

        // Clear WiFi credentials
        WiFi.disconnect(true);
        delay(1000);
        ESP.restart();
    });

    // Not found handler
    server.onNotFound(handleNotFound);

    // Start server
    server.begin();
    Serial.println(F("[WEB] HTTP server started on port 80"));
}

/**
 * Handle root page
 */
void handleRoot() {
    // Using F() macro to store strings in flash
    String html = F("<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>EpicWeatherBox</title>"
        "<style>"
        "*{box-sizing:border-box;}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "margin:0;padding:20px;background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);"
        "color:#eee;min-height:100vh;}"
        ".container{max-width:600px;margin:0 auto;}"
        "h1{color:#00d4ff;text-align:center;margin-bottom:30px;}"
        ".card{background:rgba(255,255,255,0.05);border-radius:12px;padding:20px;"
        "margin-bottom:20px;border:1px solid rgba(255,255,255,0.1);}"
        ".card h3{margin-top:0;color:#00d4ff;}"
        ".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}"
        ".info-item{padding:10px;background:rgba(0,0,0,0.2);border-radius:8px;}"
        ".info-label{font-size:12px;color:#888;margin-bottom:4px;}"
        ".info-value{font-size:16px;font-weight:500;}"
        "a{color:#00d4ff;text-decoration:none;}"
        "a:hover{text-decoration:underline;}"
        ".links{display:flex;flex-wrap:wrap;gap:10px;}"
        ".link-btn{display:inline-block;padding:12px 20px;background:#00d4ff;color:#1a1a2e;"
        "border-radius:8px;font-weight:600;transition:all 0.3s;}"
        ".link-btn:hover{background:#00a8cc;text-decoration:none;transform:translateY(-2px);}"
        ".link-btn.warning{background:#ffc107;}"
        ".link-btn.danger{background:#dc3545;color:#fff;}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>EpicWeatherBox</h1>");

    html += F("<div class='card'><h3>Device Status</h3><div class='info-grid'>");
    html += F("<div class='info-item'><div class='info-label'>Firmware</div><div class='info-value'>");
    html += FIRMWARE_VERSION;
    html += F("</div></div>");
    html += F("<div class='info-item'><div class='info-label'>IP Address</div><div class='info-value'>");
    html += WiFi.localIP().toString();
    html += F("</div></div>");
    html += F("<div class='info-item'><div class='info-label'>Free Memory</div><div class='info-value'>");
    html += String(ESP.getFreeHeap());
    html += F(" bytes</div></div>");
    html += F("<div class='info-item'><div class='info-label'>Uptime</div><div class='info-value'>");

    // Format uptime nicely
    unsigned long uptime = millis() / 1000;
    if (uptime < 60) {
        html += String(uptime) + "s";
    } else if (uptime < 3600) {
        html += String(uptime / 60) + "m " + String(uptime % 60) + "s";
    } else {
        html += String(uptime / 3600) + "h " + String((uptime % 3600) / 60) + "m";
    }

    html += F("</div></div>");
    html += F("<div class='info-item'><div class='info-label'>WiFi Signal</div><div class='info-value'>");
    html += String(WiFi.RSSI());
    html += F(" dBm</div></div>");
    html += F("<div class='info-item'><div class='info-label'>Time</div><div class='info-value'>");
    html += timeClient.getFormattedTime();
    html += F("</div></div></div></div>");

    html += F("<div class='card'><h3>Quick Links</h3><div class='links'>"
        "<a href='/admin' class='link-btn'>Admin Panel</a>"
        "<a href='/preview' class='link-btn'>Display Preview</a>"
        "<a href='/update' class='link-btn'>Firmware Update</a>"
        "<a href='/reboot' class='link-btn warning'>Reboot</a>"
        "<a href='/reset' class='link-btn danger'>Factory Reset</a>"
        "</div></div>");

    html += F("<div class='card'><h3>API Endpoints</h3>"
        "<p><a href='/api/weather'>/api/weather</a> - Weather data</p>"
        "<p><a href='/api/config'>/api/config</a> - Location config</p>"
        "<p><a href='/api/status'>/api/status</a> - Device status</p>"
        "</div>");

    html += F("<div class='card'><h3>Development Status</h3>"
        "<p>Custom weather station firmware for SmallTV hardware.</p>"
        "<ul>"
        "<li>Phase 1: OTA Updates - <strong style='color:#00ff88'>Complete</strong></li>"
        "<li>Phase A: Weather API - <strong style='color:#00ff88'>Complete</strong></li>"
        "<li>Phase B: Admin Panel - <strong style='color:#00ff88'>Complete</strong></li>"
        "<li>Phase C: Display Preview - <strong style='color:#00ff88'>Complete</strong></li>"
        "<li>Phase D: TFT Display Driver - <strong style='color:#ffc107'>In Progress</strong></li>"
        "</ul></div>");

    html += F("</div></body></html>");

    server.send(200, "text/html", html);
}

/**
 * Handle admin page - multi-location config with city search and add/remove
 */
void handleAdmin() {
    bool celsius = getUseCelsius();
    const char* unit = celsius ? "C" : "F";
    int locCount = getLocationCount();

    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Admin</title><style>"
        "*{box-sizing:border-box}body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px}"
        ".c{max-width:500px;margin:0 auto}h1{color:#00d4ff;text-align:center}"
        ".card{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin-bottom:15px}"
        ".card h3{color:#00d4ff;margin-top:0}label{display:block;margin:10px 0 5px;color:#aaa;font-size:0.9em}"
        "input,select{width:100%;padding:10px;border:1px solid #333;border-radius:6px;background:#2a2a4e;color:#eee}"
        ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
        "button{background:#00d4ff;color:#1a1a2e;border:none;padding:12px 20px;border-radius:6px;cursor:pointer;margin-top:15px}"
        "button:hover{background:#00a8cc}.status{padding:10px;border-radius:6px;margin-top:10px;display:none}"
        ".ok{background:rgba(0,200,100,0.2);color:#0c6}.err{background:rgba(200,50,50,0.2);color:#f66}"
        "a{color:#00d4ff}.hint{font-size:0.8em;color:#666;margin-top:5px}"
        ".search-box{display:flex;gap:10px}.search-box input{flex:1}.search-box button{margin-top:0}"
        ".results{max-height:200px;overflow-y:auto;margin-top:10px}"
        ".result{padding:10px;background:rgba(0,0,0,0.3);margin:5px 0;border-radius:6px;cursor:pointer}"
        ".result:hover{background:rgba(0,212,255,0.2)}.result small{color:#888}"
        ".loc-item{background:rgba(0,0,0,0.2);border-radius:8px;padding:12px;margin:8px 0;position:relative}"
        ".loc-item .name{font-weight:bold;font-size:1.1em}.loc-item .coords{color:#888;font-size:0.85em}"
        ".loc-item .remove{position:absolute;right:10px;top:50%;transform:translateY(-50%);background:#dc3545;"
        "color:#fff;border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:0.85em}"
        ".loc-item .remove:hover{background:#c82333}"
        ".add-btn{background:#28a745;width:100%}.add-btn:hover{background:#218838}"
        ".pending{border:2px dashed #00d4ff;background:rgba(0,212,255,0.1)}"
        "</style></head><body><div class='c'><h1>EpicWeatherBox</h1>");

    // Current weather for all locations
    html += F("<div class='card'><h3>Current Weather</h3>");
    for (int i = 0; i < locCount; i++) {
        const WeatherData& w = getWeather(i);
        if (i > 0) html += F("<br>");
        if (w.valid) {
            html += String(w.locationName) + F(": ");
            html += String((int)w.current.temperature) + "°" + unit + ", ";
            html += conditionToString(w.current.condition);
        } else {
            html += String(w.locationName) + F(": No data");
        }
    }
    if (locCount == 0) {
        html += F("No locations configured");
    }
    html += F("</div>");

    // City search section
    html += F("<div class='card'><h3>Find Location</h3>"
        "<div class='search-box'><input type='text' id='search' placeholder='Type city name (e.g. Aurora)'>"
        "<button type='button' onclick='searchCity()'>Search</button></div>"
        "<div id='results' class='results'></div>"
        "<p class='hint'>Search by city name only. Click a result to select it.</p>"
        "<div id='pending' class='loc-item pending' style='display:none'>"
        "<div class='name' id='pendingName'>-</div>"
        "<div class='coords' id='pendingCoords'>-</div>"
        "<button class='add-btn' onclick='addPending()'>+ Add This Location</button></div></div>");

    // Configured locations
    html += F("<div class='card'><h3>Locations</h3><div id='locations'>");

    // Render current locations from server side
    for (int i = 0; i < locCount; i++) {
        const WeatherLocation& loc = getLocation(i);
        html += F("<div class='loc-item' data-idx='");
        html += String(i);
        html += F("'><div class='name'>");
        html += String(i + 1) + ". " + String(loc.name);
        html += F("</div><div class='coords'>");
        html += String(loc.latitude, 4) + ", " + String(loc.longitude, 4);
        html += F("</div>");
        // Only show remove button if more than 1 location
        if (locCount > 1) {
            html += F("<button class='remove' onclick='removeLoc(");
            html += String(i);
            html += F(")'>Remove</button>");
        }
        html += F("</div>");
    }

    html += F("</div><p class='hint'>Max ");
    html += String(MAX_WEATHER_LOCATIONS);
    html += F(" locations. Current: ");
    html += String(locCount);
    html += F("</p></div>");

    // Settings
    html += F("<div class='card'><h3>Settings</h3>"
        "<label>Temperature Unit</label><select id='unit'>"
        "<option value='f'");
    html += celsius ? "" : " selected";
    html += F(">Fahrenheit</option><option value='c'");
    html += celsius ? " selected" : "";
    html += F(">Celsius</option></select>"
        "<button onclick='saveSettings()'>Save Settings</button>"
        "<div id='st' class='status'></div></div>");

    // Links
    html += F("<div class='card' style='text-align:center'>"
        "<a href='/'>Home</a> | <a href='/preview'>Display Preview</a> | <a href='/api/weather'>Weather API</a> | "
        "<a href='/update'>Firmware</a> | <a href='/reboot'>Reboot</a></div>");

    // JavaScript - more complex now for multi-location management
    html += F("<script>"
        "let locations=[];let pendingLoc=null;const MAX=");
    html += String(MAX_WEATHER_LOCATIONS);
    html += F(";"
        // Load current locations from API
        "async function loadLocations(){"
        "try{const r=await fetch('/api/config');const d=await r.json();"
        "locations=d.locations||[];renderLocations();"
        "}catch(e){console.error('Load failed',e);}}"

        // Render locations list
        "function renderLocations(){"
        "const el=document.getElementById('locations');el.innerHTML='';"
        "locations.forEach((loc,i)=>{"
        "const div=document.createElement('div');div.className='loc-item';"
        "div.innerHTML='<div class=\"name\">'+(i+1)+'. '+loc.name+'</div>'"
        "+'<div class=\"coords\">'+loc.lat.toFixed(4)+', '+loc.lon.toFixed(4)+'</div>';"
        "if(locations.length>1){"
        "const btn=document.createElement('button');btn.className='remove';btn.textContent='Remove';"
        "btn.onclick=()=>removeLoc(i);div.appendChild(btn);}"
        "el.appendChild(div);});}"

        // Search city
        "async function searchCity(){"
        "const q=document.getElementById('search').value.trim();"
        "if(q.length<2){alert('Enter at least 2 characters');return;}"
        "const res=document.getElementById('results');res.innerHTML='Searching...';"
        "try{const r=await fetch('/api/geocode?q='+encodeURIComponent(q));"
        "const d=await r.json();if(d.error){res.innerHTML='<p>'+d.error+'</p>';return;}"
        "if(!d.results||d.results.length===0){res.innerHTML='<p>No results found</p>';return;}"
        "res.innerHTML='';d.results.forEach(loc=>{"
        "const div=document.createElement('div');div.className='result';"
        "div.innerHTML=loc.display+'<br><small>'+loc.lat.toFixed(4)+', '+loc.lon.toFixed(4)+'</small>';"
        "div.onclick=()=>selectLocation(loc);res.appendChild(div);});"
        "}catch(e){res.innerHTML='<p>Search failed</p>';}}"

        // Select location from search
        "function selectLocation(loc){"
        "pendingLoc=loc;"
        "document.getElementById('pending').style.display='block';"
        "document.getElementById('pendingName').textContent=loc.display||loc.name;"
        "document.getElementById('pendingCoords').textContent=loc.lat.toFixed(4)+', '+loc.lon.toFixed(4);"
        "document.getElementById('results').innerHTML='<p style=\"color:#0c6\">Selected - click Add to confirm</p>';}"

        // Add pending location
        "async function addPending(){"
        "if(!pendingLoc){alert('Select a location first');return;}"
        "if(locations.length>=MAX){alert('Max '+MAX+' locations');return;}"
        "locations.push({name:pendingLoc.name,lat:pendingLoc.lat,lon:pendingLoc.lon,enabled:true});"
        "await saveLocations();pendingLoc=null;"
        "document.getElementById('pending').style.display='none';}"

        // Remove location
        "async function removeLoc(idx){"
        "if(locations.length<=1){alert('Must have at least 1 location');return;}"
        "if(!confirm('Remove '+locations[idx].name+'?'))return;"
        "locations.splice(idx,1);await saveLocations();}"

        // Save locations to server
        "async function saveLocations(){"
        "const st=document.getElementById('st');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({locations:locations,useCelsius:document.getElementById('unit').value==='c'})});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.message;if(d.success){setTimeout(()=>location.reload(),2000);}"
        "}catch(e){st.className='status err';st.textContent='Error';}}"

        // Save settings only
        "async function saveSettings(){"
        "const st=document.getElementById('st');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({locations:locations,useCelsius:document.getElementById('unit').value==='c'})});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.message;if(d.success){setTimeout(()=>location.reload(),2000);}"
        "}catch(e){st.className='status err';st.textContent='Error';}}"

        // Event listeners
        "document.getElementById('search').onkeypress=e=>{if(e.key==='Enter'){e.preventDefault();searchCity();}};"
        "loadLocations();"
        "</script>");

    html += F("</div></body></html>");
    server.send(200, "text/html", html);
}

/**
 * Handle display preview page - HTML5 Canvas simulation of the TFT display
 * Shows what will be rendered on the actual 240x240 display
 */
void handleDisplayPreview() {
    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Display Preview - EpicWeatherBox</title><style>"
        "*{box-sizing:border-box}body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px}"
        ".c{max-width:800px;margin:0 auto}h1{color:#00d4ff;text-align:center}"
        ".preview-container{display:flex;justify-content:center;margin:20px 0}"
        "#display{border:8px solid #333;border-radius:12px;background:#000;image-rendering:pixelated}"
        ".controls{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin:15px 0;text-align:center}"
        "button{background:#00d4ff;color:#1a1a2e;border:none;padding:10px 20px;border-radius:6px;cursor:pointer;margin:5px}"
        "button:hover{background:#00a8cc}button.active{background:#00ff88}"
        ".info{margin-top:10px;color:#888;font-size:0.9em}"
        ".screen-label{color:#00d4ff;font-size:1.2em;margin:10px 0}"
        "a{color:#00d4ff}.card{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin:15px 0}"
        "</style></head><body><div class='c'><h1>Display Preview</h1>"
        "<div class='preview-container'><canvas id='display' width='240' height='240'></canvas></div>"
        "<div class='controls'>"
        "<div class='screen-label' id='screenLabel'>Current Weather</div>"
        "<button onclick='prevScreen()'>◀ Prev</button>"
        "<button onclick='nextScreen()'>Next ▶</button>"
        "<button onclick='toggleAuto()' id='autoBtn'>Auto: ON</button>"
        "<button onclick='refreshWeather()'>Refresh Weather</button>"
        "<div class='info'>Screen updates every 10 seconds when Auto is ON</div></div>"
        "<div class='card'><strong>Location:</strong> <span id='locName'>-</span> "
        "(<span id='locIdx'>1</span>/<span id='locTotal'>1</span>)</div>"
        "<div class='card' style='text-align:center'>"
        "<a href='/admin'>Admin Panel</a> | <a href='/'>Home</a></div>");

    // JavaScript for canvas rendering
    html += F("<script>"
        "const canvas=document.getElementById('display');"
        "const ctx=canvas.getContext('2d');"
        "ctx.imageSmoothingEnabled=false;"  // Pixel art style
        "let weatherData=null;let currentLoc=0;let currentScreen=0;let autoPlay=true;let autoTimer=null;"

        // Color constants matching display.h
        "const COLORS={"
        "BG_DAY:'#5DDF',BG_NIGHT:'#1926',"
        "TEXT_WHITE:'#FFFFFF',TEXT_LIGHT:'#DEDEDE',TEXT_YELLOW:'#FFE000',TEXT_ORANGE:'#FD2000',"
        "TEXT_BLUE:'#5D9F',TEXT_CYAN:'#00FFFF',CARD_BG:'#212040',SUN:'#FFE000',MOON:'#C0C0C0',"
        "CLOUD:'#DEDEDE',RAIN:'#5D9FFF',SNOW:'#FFFFFF',THUNDER:'#FFE000'};"

        // Weather icons as pixel art (32x32 simplified)
        "const ICONS={"
        "sun:["
        "'                                ',"
        "'              ##                ',"
        "'              ##                ',"
        "'    #         ##         #      ',"
        "'     #                  #       ',"
        "'       ##  ########  ##         ',"
        "'         ############           ',"
        "'        ##          ##          ',"
        "'       ##            ##         ',"
        "'      ##              ##        ',"
        "'      #                #        ',"
        "'     ##                ##       ',"
        "'     ##                ##       ',"
        "'######                  ######  ',"
        "'######                  ######  ',"
        "'     ##                ##       ',"
        "'     ##                ##       ',"
        "'      #                #        ',"
        "'      ##              ##        ',"
        "'       ##            ##         ',"
        "'        ##          ##          ',"
        "'         ############           ',"
        "'       ##  ########  ##         ',"
        "'     #                  #       ',"
        "'    #         ##         #      ',"
        "'              ##                ',"
        "'              ##                ',"
        "'              ##                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                '],"

        "moon:["
        "'                                ',"
        "'          ######                ',"
        "'        ##########              ',"
        "'       ############             ',"
        "'      ##############            ',"
        "'      #############             ',"
        "'     #############              ',"
        "'     ############               ',"
        "'     ###########                ',"
        "'    ###########                 ',"
        "'    ##########                  ',"
        "'    ##########                  ',"
        "'    ##########                  ',"
        "'    ##########                  ',"
        "'    ##########                  ',"
        "'    ##########                  ',"
        "'    ##########                  ',"
        "'    ##########                  ',"
        "'    ###########                 ',"
        "'     ############               ',"
        "'     #############              ',"
        "'      ##############            ',"
        "'      ###############           ',"
        "'       ###############          ',"
        "'        ################        ',"
        "'          ##############        ',"
        "'            ############        ',"
        "'              ########          ',"
        "'                ####            ',"
        "'                                ',"
        "'                                ',"
        "'                                '],"

        "cloud:["
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'           ######               ',"
        "'         ##########             ',"
        "'        ##        ##            ',"
        "'       ##          ##           ',"
        "'       #            #           ',"
        "'      ##            #           ',"
        "'     ##              #          ',"
        "'    ##               ##         ',"
        "'   ##                 ###       ',"
        "'  ##                   ###      ',"
        "' ##                     ##      ',"
        "'##                       #      ',"
        "'#                        #      ',"
        "'#                        #      ',"
        "'#                        #      ',"
        "'##                      ##      ',"
        "'######################  #       ',"
        "'#######################         ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                '],"

        "rain:["
        "'                                ',"
        "'           ######               ',"
        "'         ##########             ',"
        "'        ##        ##            ',"
        "'       ##          ##           ',"
        "'       #            #           ',"
        "'      ##            #           ',"
        "'     ##              #          ',"
        "'    ##               ##         ',"
        "'   ##                 ###       ',"
        "'  ##                   ###      ',"
        "' ##                     ##      ',"
        "'##                       #      ',"
        "'#                        #      ',"
        "'##                      ##      ',"
        "'######################  #       ',"
        "'#######################         ',"
        "'                                ',"
        "'      #   #    #                ',"
        "'       #   #    #               ',"
        "'        #   #    #              ',"
        "'   #     #   #                  ',"
        "'    #     #   #    #            ',"
        "'      #       #    #            ',"
        "'       #   #        #           ',"
        "'        #   #    #              ',"
        "'   #         #    #             ',"
        "'    #    #        #             ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                '],"

        "snow:["
        "'                                ',"
        "'           ######               ',"
        "'         ##########             ',"
        "'        ##        ##            ',"
        "'       ##          ##           ',"
        "'       #            #           ',"
        "'      ##            #           ',"
        "'     ##              #          ',"
        "'    ##               ##         ',"
        "'   ##                 ###       ',"
        "'  ##                   ###      ',"
        "' ##                     ##      ',"
        "'##                       #      ',"
        "'#                        #      ',"
        "'##                      ##      ',"
        "'######################  #       ',"
        "'#######################         ',"
        "'                                ',"
        "'       #      #      #          ',"
        "'      ###    ###    ###         ',"
        "'       #      #      #          ',"
        "'                                ',"
        "'    #      #      #      #      ',"
        "'   ###    ###    ###    ###     ',"
        "'    #      #      #      #      ',"
        "'                                ',"
        "'       #      #      #          ',"
        "'      ###    ###    ###         ',"
        "'       #      #      #          ',"
        "'                                ',"
        "'                                ',"
        "'                                '],"

        "thunder:["
        "'                                ',"
        "'           ######               ',"
        "'         ##########             ',"
        "'        ##        ##            ',"
        "'       ##          ##           ',"
        "'       #            #           ',"
        "'      ##            #           ',"
        "'     ##              #          ',"
        "'    ##               ##         ',"
        "'   ##                 ###       ',"
        "'  ##                   ###      ',"
        "' ##                     ##      ',"
        "'##                       #      ',"
        "'#                        #      ',"
        "'##                      ##      ',"
        "'######################  #       ',"
        "'#######################         ',"
        "'          ####                  ',"
        "'         ####                   ',"
        "'        ####                    ',"
        "'       ####                     ',"
        "'      ########                  ',"
        "'        ####                    ',"
        "'       ####                     ',"
        "'      ####                      ',"
        "'     ####                       ',"
        "'      ##                        ',"
        "'       #                        ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                '],"

        "fog:["
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'##############################  ',"
        "'##############################  ',"
        "'                                ',"
        "'                                ',"
        "'  ##########################    ',"
        "'  ##########################    ',"
        "'                                ',"
        "'                                ',"
        "'##############################  ',"
        "'##############################  ',"
        "'                                ',"
        "'                                ',"
        "'  ##########################    ',"
        "'  ##########################    ',"
        "'                                ',"
        "'                                ',"
        "'##############################  ',"
        "'##############################  ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                '],"

        "unknown:["
        "'                                ',"
        "'        ##########              ',"
        "'      ##############            ',"
        "'     ####        ####           ',"
        "'    ###            ###          ',"
        "'   ###              ###         ',"
        "'   ##                ##         ',"
        "'   ##                ##         ',"
        "'   ##                ##         ',"
        "'                     ##         ',"
        "'                    ##          ',"
        "'                   ##           ',"
        "'                 ###            ',"
        "'               ###              ',"
        "'              ##                ',"
        "'             ##                 ',"
        "'             ##                 ',"
        "'             ##                 ',"
        "'                                ',"
        "'                                ',"
        "'             ##                 ',"
        "'            ####                ',"
        "'            ####                ',"
        "'             ##                 ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ',"
        "'                                ']};"

        // Get icon for condition
        "function getIcon(condition,isDay){"
        "const c=condition||9;"  // 9 = unknown
        "if(c===0)return isDay?ICONS.sun:ICONS.moon;"  // Clear
        "if(c===1)return ICONS.cloud;"  // Partly cloudy - use cloud
        "if(c===2)return ICONS.cloud;"  // Cloudy
        "if(c===3)return ICONS.fog;"  // Fog
        "if(c===4)return ICONS.rain;"  // Drizzle
        "if(c===5||c===6)return ICONS.rain;"  // Rain/Freezing rain
        "if(c===7)return ICONS.snow;"  // Snow
        "if(c===8)return ICONS.thunder;"  // Thunder
        "return ICONS.unknown;}"

        // Get icon color
        "function getIconColor(condition,isDay){"
        "const c=condition||9;"
        "if(c===0)return isDay?COLORS.SUN:COLORS.MOON;"
        "if(c<=3)return COLORS.CLOUD;"
        "if(c<=6)return COLORS.RAIN;"
        "if(c===7)return COLORS.SNOW;"
        "if(c===8)return COLORS.THUNDER;"
        "return COLORS.TEXT_WHITE;}"

        // Draw pixel icon
        "function drawIcon(icon,x,y,size,color){"
        "const scale=size/32;"
        "ctx.fillStyle=color;"
        "for(let row=0;row<32;row++){"
        "for(let col=0;col<32;col++){"
        "if(icon[row]&&icon[row][col]==='#'){"
        "ctx.fillRect(x+col*scale,y+row*scale,scale,scale);}}}}"

        // Temperature color
        "function getTempColor(temp){"
        "if(temp<0)return COLORS.TEXT_BLUE;"
        "if(temp<10)return COLORS.TEXT_CYAN;"
        "if(temp<20)return COLORS.TEXT_WHITE;"
        "return COLORS.TEXT_ORANGE;}"

        // Format temp
        "function formatTemp(temp,useCelsius){"
        "if(!useCelsius)temp=temp*9/5+32;"
        "return Math.round(temp)+(useCelsius?'°C':'°F');}"

        // Draw current weather screen
        "function drawCurrentWeather(){"
        "if(!weatherData||!weatherData.locations||weatherData.locations.length===0){"
        "ctx.fillStyle='#333';ctx.fillRect(0,0,240,240);"
        "ctx.fillStyle='#FFF';ctx.font='16px sans-serif';ctx.textAlign='center';"
        "ctx.fillText('No weather data',120,120);return;}"
        "const loc=weatherData.locations[currentLoc]||weatherData.locations[0];"
        "const w=loc.current||{};"
        "const isDay=w.isDay!==false;"
        "const useCelsius=true;"  // TODO: get from config
        // Background
        "ctx.fillStyle=isDay?'#5DDFFF':'#192640';ctx.fillRect(0,0,240,240);"
        // Time
        "const now=new Date();const h=now.getHours().toString().padStart(2,'0');"
        "const m=now.getMinutes().toString().padStart(2,'0');"
        "ctx.fillStyle=COLORS.TEXT_WHITE;ctx.font='bold 48px sans-serif';ctx.textAlign='center';"
        "ctx.fillText(h+':'+m,120,55);"
        // Location name
        "ctx.fillStyle=COLORS.TEXT_LIGHT;ctx.font='18px sans-serif';"
        "ctx.fillText(loc.locationName||'Unknown',120,85);"
        // Weather icon (centered, large)
        "const icon=getIcon(w.condition,isDay);"
        "const iconColor=getIconColor(w.condition,isDay);"
        "drawIcon(icon,88,100,64,iconColor);"
        // Temperature
        "const temp=w.temperature||0;"
        "ctx.fillStyle=getTempColor(temp);ctx.font='bold 36px sans-serif';"
        "ctx.fillText(formatTemp(temp,useCelsius),120,195);"
        // Condition text
        "ctx.fillStyle=COLORS.TEXT_LIGHT;ctx.font='14px sans-serif';"
        "const condNames=['Clear','Partly Cloudy','Cloudy','Fog','Drizzle','Rain','Freezing Rain','Snow','Thunderstorm','Unknown'];"
        "ctx.fillText(condNames[w.condition||9],120,218);"
        // Location dots
        "const numLocs=weatherData.locations.length;"
        "if(numLocs>1){"
        "const startX=120-(numLocs-1)*6;"
        "for(let i=0;i<numLocs;i++){"
        "ctx.fillStyle=i===currentLoc?COLORS.TEXT_WHITE:COLORS.TEXT_LIGHT;"
        "ctx.beginPath();ctx.arc(startX+i*12,232,3,0,Math.PI*2);ctx.fill();}}}"

        // Draw forecast screen
        "function drawForecast(startDay){"
        "if(!weatherData||!weatherData.locations)return;"
        "const loc=weatherData.locations[currentLoc]||weatherData.locations[0];"
        "const forecast=loc.forecast||[];"
        "const isDay=loc.current?.isDay!==false;"
        "const useCelsius=true;"
        // Background
        "ctx.fillStyle=isDay?'#5DDFFF':'#192640';ctx.fillRect(0,0,240,240);"
        // Title
        "ctx.fillStyle=COLORS.TEXT_WHITE;ctx.font='14px sans-serif';ctx.textAlign='center';"
        "const title=(loc.locationName||'?')+' - Days '+(startDay+1)+'-'+(startDay+3);"
        "ctx.fillText(title,120,22);"
        // Draw 3 cards
        "const cardW=70,cardH=160,cardSpace=8,startX=(240-3*cardW-2*cardSpace)/2;"
        "for(let i=0;i<3;i++){"
        "const dayIdx=startDay+i;"
        "if(dayIdx>=forecast.length)continue;"
        "const day=forecast[dayIdx];"
        "const cardX=startX+i*(cardW+cardSpace);"
        // Card background
        "ctx.fillStyle=COLORS.CARD_BG;"
        "ctx.beginPath();ctx.roundRect(cardX,45,cardW,cardH,4);ctx.fill();"
        // Day name
        "ctx.fillStyle=COLORS.TEXT_WHITE;ctx.font='12px sans-serif';"
        "ctx.fillText(day.dayName||'?',cardX+cardW/2,62);"
        // Icon
        "const icon=getIcon(day.condition,true);"
        "const iconColor=getIconColor(day.condition,true);"
        "drawIcon(icon,cardX+(cardW-32)/2,70,32,iconColor);"
        // High temp
        "ctx.fillStyle=getTempColor(day.tempMax||0);ctx.font='14px sans-serif';"
        "ctx.fillText(formatTemp(day.tempMax||0,useCelsius),cardX+cardW/2,120);"
        // Low temp
        "ctx.fillStyle=getTempColor(day.tempMin||0);"
        "ctx.fillText(formatTemp(day.tempMin||0,useCelsius),cardX+cardW/2,140);"
        // Precip %
        "if((day.precipitationProb||0)>0){"
        "ctx.fillStyle=COLORS.RAIN;ctx.font='11px sans-serif';"
        "ctx.fillText(Math.round(day.precipitationProb)+'%',cardX+cardW/2,160);}}"
        // Location dots
        "const numLocs=weatherData.locations.length;"
        "if(numLocs>1){"
        "const dotsX=120-(numLocs-1)*6;"
        "for(let i=0;i<numLocs;i++){"
        "ctx.fillStyle=i===currentLoc?COLORS.TEXT_WHITE:COLORS.TEXT_LIGHT;"
        "ctx.beginPath();ctx.arc(dotsX+i*12,232,3,0,Math.PI*2);ctx.fill();}}}"

        // Main render function
        "function render(){"
        "ctx.clearRect(0,0,240,240);"
        "const screenNames=['Current Weather','Forecast Days 1-3','Forecast Days 4-6'];"
        "document.getElementById('screenLabel').textContent=screenNames[currentScreen];"
        "if(weatherData&&weatherData.locations){"
        "const loc=weatherData.locations[currentLoc];"
        "document.getElementById('locName').textContent=loc?.locationName||'Unknown';"
        "document.getElementById('locIdx').textContent=currentLoc+1;"
        "document.getElementById('locTotal').textContent=weatherData.locations.length;}"
        "switch(currentScreen){"
        "case 0:drawCurrentWeather();break;"
        "case 1:drawForecast(0);break;"
        "case 2:drawForecast(3);break;}}"

        // Navigation
        "function nextScreen(){"
        "currentScreen=(currentScreen+1)%3;"
        "if(currentScreen===0&&weatherData&&weatherData.locations){"
        "currentLoc=(currentLoc+1)%weatherData.locations.length;}"
        "render();}"

        "function prevScreen(){"
        "currentScreen=(currentScreen+2)%3;"
        "if(currentScreen===2&&weatherData&&weatherData.locations){"
        "currentLoc=(currentLoc+weatherData.locations.length-1)%weatherData.locations.length;}"
        "render();}"

        "function toggleAuto(){"
        "autoPlay=!autoPlay;"
        "document.getElementById('autoBtn').textContent='Auto: '+(autoPlay?'ON':'OFF');"
        "document.getElementById('autoBtn').className=autoPlay?'active':'';"
        "if(autoPlay)startAuto();else stopAuto();}"

        "function startAuto(){"
        "stopAuto();"
        "autoTimer=setInterval(nextScreen,10000);}"

        "function stopAuto(){"
        "if(autoTimer){clearInterval(autoTimer);autoTimer=null;}}"

        // Fetch weather data
        "async function fetchWeather(){"
        "try{const r=await fetch('/api/weather');weatherData=await r.json();render();"
        "}catch(e){console.error('Failed to fetch weather',e);}}"

        "function refreshWeather(){"
        "fetch('/api/weather/refresh').then(()=>setTimeout(fetchWeather,2000));}"

        // Init
        "fetchWeather();"
        "if(autoPlay)startAuto();"
        "setInterval(fetchWeather,60000);"  // Refresh data every minute
        "</script>");

    html += F("</div></body></html>");
    server.send(200, "text/html", html);
}

/**
 * Handle 404
 */
void handleNotFound() {
    String message = F("<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
        "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
        ".box{text-align:center;}a{color:#00d4ff;}</style></head><body>"
        "<div class='box'><h1>404 - Not Found</h1>"
        "<p>The requested URL was not found.</p>"
        "<p><a href='/'>Go to Home</a></p></div></body></html>");

    server.send(404, "text/html", message);
}
