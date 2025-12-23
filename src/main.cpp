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
        doc["brightness"] = getBrightness();
        doc["nightModeEnabled"] = getNightModeEnabled();
        doc["nightModeStartHour"] = getNightModeStartHour();
        doc["nightModeEndHour"] = getNightModeEndHour();
        doc["nightModeBrightness"] = getNightModeBrightness();
        doc["mainScreenOnly"] = getMainScreenOnly();
        doc["themeMode"] = getThemeMode();
        doc["gifScreenEnabled"] = getGifScreenEnabled();

        // GIF status
        doc["bootGifExists"] = LittleFS.exists("/boot.gif");
        doc["screenGifExists"] = LittleFS.exists("/screen.gif");

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
        if (doc["useCelsius"].is<bool>()) {
            setUseCelsius(doc["useCelsius"] | false);
        }
        if (doc["brightness"].is<int>()) {
            setBrightness(doc["brightness"] | 50);
        }
        if (doc["nightModeEnabled"].is<bool>()) {
            setNightModeEnabled(doc["nightModeEnabled"] | true);
        }
        if (doc["nightModeStartHour"].is<int>()) {
            setNightModeStartHour(doc["nightModeStartHour"] | 22);
        }
        if (doc["nightModeEndHour"].is<int>()) {
            setNightModeEndHour(doc["nightModeEndHour"] | 7);
        }
        if (doc["nightModeBrightness"].is<int>()) {
            setNightModeBrightness(doc["nightModeBrightness"] | 20);
        }
        if (doc["mainScreenOnly"].is<bool>()) {
            setMainScreenOnly(doc["mainScreenOnly"] | false);
        }
        if (doc["themeMode"].is<int>()) {
            setThemeMode(doc["themeMode"] | 0);
        }
        if (doc["gifScreenEnabled"].is<bool>()) {
            setGifScreenEnabled(doc["gifScreenEnabled"] | false);
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

    // GIF file upload endpoints
    // Boot GIF upload
    server.on("/api/upload/bootgif", HTTP_POST, []() {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Boot GIF uploaded\"}");
    }, []() {
        HTTPUpload& upload = server.upload();
        static File uploadFile;

        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[UPLOAD] Boot GIF: %s\n", upload.filename.c_str());
            // Check file size limit (50KB max)
            uploadFile = LittleFS.open("/boot.gif", "w");
            if (!uploadFile) {
                Serial.println(F("[UPLOAD] Failed to open file for writing"));
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile) {
                // Check total size doesn't exceed 50KB
                if (uploadFile.size() + upload.currentSize > 51200) {
                    Serial.println(F("[UPLOAD] File too large (max 50KB)"));
                    uploadFile.close();
                    LittleFS.remove("/boot.gif");
                    return;
                }
                uploadFile.write(upload.buf, upload.currentSize);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) {
                uploadFile.close();
                Serial.printf("[UPLOAD] Boot GIF uploaded: %u bytes\n", upload.totalSize);
            }
        }
    });

    // Screen GIF upload
    server.on("/api/upload/screengif", HTTP_POST, []() {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Screen GIF uploaded\"}");
    }, []() {
        HTTPUpload& upload = server.upload();
        static File uploadFile;

        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[UPLOAD] Screen GIF: %s\n", upload.filename.c_str());
            uploadFile = LittleFS.open("/screen.gif", "w");
            if (!uploadFile) {
                Serial.println(F("[UPLOAD] Failed to open file for writing"));
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (uploadFile) {
                // Check total size doesn't exceed 100KB
                if (uploadFile.size() + upload.currentSize > 102400) {
                    Serial.println(F("[UPLOAD] File too large (max 100KB)"));
                    uploadFile.close();
                    LittleFS.remove("/screen.gif");
                    return;
                }
                uploadFile.write(upload.buf, upload.currentSize);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (uploadFile) {
                uploadFile.close();
                Serial.printf("[UPLOAD] Screen GIF uploaded: %u bytes\n", upload.totalSize);
            }
        }
    });

    // Delete boot GIF
    server.on("/api/delete/bootgif", HTTP_DELETE, []() {
        if (LittleFS.exists("/boot.gif")) {
            LittleFS.remove("/boot.gif");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Boot GIF deleted\"}");
        } else {
            server.send(404, "application/json", "{\"success\":false,\"message\":\"No boot GIF found\"}");
        }
    });

    // Delete screen GIF
    server.on("/api/delete/screengif", HTTP_DELETE, []() {
        if (LittleFS.exists("/screen.gif")) {
            LittleFS.remove("/screen.gif");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Screen GIF deleted\"}");
        } else {
            server.send(404, "application/json", "{\"success\":false,\"message\":\"No screen GIF found\"}");
        }
    });

    // GIF status endpoint
    server.on("/api/gif/status", HTTP_GET, []() {
        JsonDocument doc;
        doc["bootGifExists"] = LittleFS.exists("/boot.gif");
        doc["screenGifExists"] = LittleFS.exists("/screen.gif");
        doc["gifScreenEnabled"] = getGifScreenEnabled();

        // Get file sizes
        if (LittleFS.exists("/boot.gif")) {
            File f = LittleFS.open("/boot.gif", "r");
            doc["bootGifSize"] = f.size();
            f.close();
        }
        if (LittleFS.exists("/screen.gif")) {
            File f = LittleFS.open("/screen.gif", "r");
            doc["screenGifSize"] = f.size();
            f.close();
        }

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Serve boot GIF file
    server.on("/api/gif/boot", HTTP_GET, []() {
        if (LittleFS.exists("/boot.gif")) {
            File f = LittleFS.open("/boot.gif", "r");
            server.streamFile(f, "image/gif");
            f.close();
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });

    // Serve screen GIF file
    server.on("/api/gif/screen", HTTP_GET, []() {
        if (LittleFS.exists("/screen.gif")) {
            File f = LittleFS.open("/screen.gif", "r");
            server.streamFile(f, "image/gif");
            f.close();
        } else {
            server.send(404, "text/plain", "Not found");
        }
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

    // Settings - Temperature
    html += F("<div class='card'><h3>Settings</h3>"
        "<label>Temperature Unit</label><select id='unit'>"
        "<option value='f'");
    html += celsius ? "" : " selected";
    html += F(">Fahrenheit</option><option value='c'");
    html += celsius ? " selected" : "";
    html += F(">Celsius</option></select>");

    // Display Mode
    html += F("<label>Display Mode</label><select id='screenMode'>"
        "<option value='0'");
    html += getMainScreenOnly() ? "" : " selected";
    html += F(">Cycle All Screens</option><option value='1'");
    html += getMainScreenOnly() ? " selected" : "";
    html += F(">Main Screen Only</option></select>"
        "<p class='hint'>Cycle through weather + forecast screens, or show only main weather</p>");

    // Theme Mode
    html += F("<label>Theme</label><select id='themeMode'>"
        "<option value='0'");
    html += (getThemeMode() == 0) ? " selected" : "";
    html += F(">Auto (dark at night)</option><option value='1'");
    html += (getThemeMode() == 1) ? " selected" : "";
    html += F(">Always Dark</option><option value='2'");
    html += (getThemeMode() == 2) ? " selected" : "";
    html += F(">Always Light</option></select>");

    // Brightness
    html += F("<label>Brightness: <span id='brtVal'>");
    html += String(getBrightness());
    html += F("</span>%</label><input type='range' id='brightness' min='10' max='100' value='");
    html += String(getBrightness());
    html += F("' oninput='document.getElementById(\"brtVal\").textContent=this.value'>");

    // Night Mode
    html += F("<label><input type='checkbox' id='nightMode'");
    html += getNightModeEnabled() ? " checked" : "";
    html += F("> Enable Night Mode</label>"
        "<p class='hint'>Automatically dims display and uses dark theme during night hours</p>"
        "<div class='row'><div><label>Night Start</label><select id='nightStart'>");
    for (int h = 0; h < 24; h++) {
        html += F("<option value='");
        html += String(h);
        html += "'";
        if (h == getNightModeStartHour()) html += F(" selected");
        html += ">";
        html += (h == 0) ? "12 AM" : (h < 12) ? String(h) + " AM" : (h == 12) ? "12 PM" : String(h - 12) + " PM";
        html += F("</option>");
    }
    html += F("</select></div><div><label>Night End</label><select id='nightEnd'>");
    for (int h = 0; h < 24; h++) {
        html += F("<option value='");
        html += String(h);
        html += "'";
        if (h == getNightModeEndHour()) html += F(" selected");
        html += ">";
        html += (h == 0) ? "12 AM" : (h < 12) ? String(h) + " AM" : (h == 12) ? "12 PM" : String(h - 12) + " PM";
        html += F("</option>");
    }
    html += F("</select></div></div>"
        "<label>Night Brightness: <span id='nightBrtVal'>");
    html += String(getNightModeBrightness());
    html += F("</span>%</label><input type='range' id='nightBrightness' min='5' max='50' value='");
    html += String(getNightModeBrightness());
    html += F("' oninput='document.getElementById(\"nightBrtVal\").textContent=this.value'>");

    html += F("<button onclick='saveSettings()'>Save Settings</button>"
        "<div id='st' class='status'></div></div>");

    // GIF Settings section
    html += F("<div class='card'><h3>Animated GIFs</h3>"
        "<p class='hint'>Upload GIF images for boot animation and screen rotation.</p>"

        // Boot GIF
        "<label>Boot Animation (max 50KB)</label>"
        "<div id='bootGifStatus' style='margin-bottom:10px;color:#888'>Checking...</div>"
        "<input type='file' id='bootGifFile' accept='.gif' style='display:none' onchange='uploadGif(\"boot\")'>"
        "<button type='button' onclick='document.getElementById(\"bootGifFile\").click()' style='margin-top:0'>Upload Boot GIF</button>"
        "<button type='button' onclick='deleteGif(\"boot\")' style='background:#dc3545;margin-left:10px'>Delete</button>"

        // Screen GIF
        "<label style='margin-top:20px'>Screen GIF (max 100KB)</label>"
        "<div id='screenGifStatus' style='margin-bottom:10px;color:#888'>Checking...</div>"
        "<input type='file' id='screenGifFile' accept='.gif' style='display:none' onchange='uploadGif(\"screen\")'>"
        "<button type='button' onclick='document.getElementById(\"screenGifFile\").click()' style='margin-top:0'>Upload Screen GIF</button>"
        "<button type='button' onclick='deleteGif(\"screen\")' style='background:#dc3545;margin-left:10px'>Delete</button>"

        // GIF screen toggle
        "<label style='margin-top:20px'><input type='checkbox' id='gifScreenEnabled'");
    html += getGifScreenEnabled() ? " checked" : "";
    html += F("> Show GIF screen in rotation</label>"
        "<p class='hint'>When enabled and a screen GIF is uploaded, it will appear in the display rotation.</p>"
        "<button type='button' onclick='saveGifSettings()' style='margin-top:10px'>Save GIF Settings</button>"
        "<div id='gifSt' class='status'></div></div>");

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

        // Get all settings as object
        "function getSettings(){"
        "return{locations:locations,"
        "useCelsius:document.getElementById('unit').value==='c',"
        "brightness:parseInt(document.getElementById('brightness').value),"
        "nightModeEnabled:document.getElementById('nightMode').checked,"
        "nightModeStartHour:parseInt(document.getElementById('nightStart').value),"
        "nightModeEndHour:parseInt(document.getElementById('nightEnd').value),"
        "nightModeBrightness:parseInt(document.getElementById('nightBrightness').value),"
        "mainScreenOnly:document.getElementById('screenMode').value==='1',"
        "themeMode:parseInt(document.getElementById('themeMode').value),"
        "gifScreenEnabled:document.getElementById('gifScreenEnabled').checked};}"

        // Save locations to server
        "async function saveLocations(){"
        "const st=document.getElementById('st');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(getSettings())});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.message;if(d.success){setTimeout(()=>location.reload(),2000);}"
        "}catch(e){st.className='status err';st.textContent='Error';}}"

        // Save settings only
        "async function saveSettings(){"
        "const st=document.getElementById('st');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(getSettings())});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.message;if(d.success){setTimeout(()=>location.reload(),2000);}"
        "}catch(e){st.className='status err';st.textContent='Error';}}"

        // GIF upload function
        "async function uploadGif(type){"
        "const file=document.getElementById(type+'GifFile').files[0];"
        "if(!file){return;}"
        "const maxSize=type==='boot'?51200:102400;"
        "if(file.size>maxSize){alert('File too large. Max '+(maxSize/1024)+'KB');return;}"
        "const st=document.getElementById('gifSt');st.style.display='block';st.className='status';"
        "st.textContent='Uploading...';"
        "const form=new FormData();form.append('file',file);"
        "try{const r=await fetch('/api/upload/'+type+'gif',{method:'POST',body:form});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');st.textContent=d.message;"
        "if(d.success){setTimeout(updateGifStatus,1000);}"
        "}catch(e){st.className='status err';st.textContent='Upload failed';}}"

        // GIF delete function
        "async function deleteGif(type){"
        "if(!confirm('Delete '+type+' GIF?'))return;"
        "const st=document.getElementById('gifSt');st.style.display='block';st.className='status';"
        "st.textContent='Deleting...';"
        "try{const r=await fetch('/api/delete/'+type+'gif',{method:'DELETE'});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');st.textContent=d.message;"
        "updateGifStatus();"
        "}catch(e){st.className='status err';st.textContent='Delete failed';}}"

        // Save GIF settings only
        "async function saveGifSettings(){"
        "const st=document.getElementById('gifSt');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(getSettings())});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.success?'GIF settings saved!':d.message;"
        "}catch(e){st.className='status err';st.textContent='Error saving';}}"

        // Update GIF status display
        "async function updateGifStatus(){"
        "try{const r=await fetch('/api/gif/status');const d=await r.json();"
        "document.getElementById('bootGifStatus').textContent=d.bootGifExists?"
        "'Uploaded ('+(d.bootGifSize/1024).toFixed(1)+'KB)':'No file uploaded';"
        "document.getElementById('screenGifStatus').textContent=d.screenGifExists?"
        "'Uploaded ('+(d.screenGifSize/1024).toFixed(1)+'KB)':'No file uploaded';"
        "}catch(e){console.error('GIF status check failed');}}"

        // Event listeners
        "document.getElementById('search').onkeypress=e=>{if(e.key==='Enter'){e.preventDefault();searchCity();}};"
        "loadLocations();updateGifStatus();"
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
        "*{box-sizing:border-box}body{font-family:sans-serif;background:#0d0d1a;color:#eee;margin:0;padding:20px}"
        ".c{max-width:800px;margin:0 auto}h1{color:#00d4ff;text-align:center}"
        ".preview-container{display:flex;justify-content:center;margin:20px 0}"
        "#display{border:8px solid #333;border-radius:12px;background:#000;image-rendering:pixelated}"
        ".controls{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin:15px 0;text-align:center}"
        "button{background:#00d4ff;color:#1a1a2e;border:none;padding:10px 20px;border-radius:6px;cursor:pointer;margin:5px}"
        "button:hover{background:#00a8cc}button.active{background:#00ff88}"
        ".info{margin-top:10px;color:#888;font-size:0.9em}"
        ".screen-label{color:#00d4ff;font-size:1.2em;margin:10px 0}"
        "a{color:#00d4ff}.card{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin:15px 0}"
        ".dots{display:flex;justify-content:center;gap:8px;margin:10px 0}"
        ".dot{width:10px;height:10px;border-radius:50%;background:#444}"
        ".dot.active{background:#00d4ff}"
        "</style></head><body><div class='c'><h1>Display Preview</h1>"
        "<div class='preview-container'><canvas id='display' width='240' height='240'></canvas></div>"
        "<div class='controls'>"
        "<div class='screen-label' id='screenLabel'>Current Weather</div>"
        "<div class='dots' id='screenDots'></div>"
        "<button onclick='prevScreen()'>◀ Prev</button>"
        "<button onclick='nextScreen()'>Next ▶</button>"
        "<button onclick='toggleAuto()' id='autoBtn'>Auto: ON</button>"
        "<button onclick='toggleTheme()' id='themeBtn'>Theme: Dark</button>"
        "<button onclick='showBoot()' id='bootBtn'>Boot</button>"
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
        "ctx.imageSmoothingEnabled=false;"
        "let weatherData=null,config=null,currentLoc=0,currentScreen=0,autoPlay=true,autoTimer=null;"
        "let mainScreenOnly=false,darkMode=true,gifScreenEnabled=false,showingBoot=false;"
        "let bootGifExists=false,screenGifExists=false,bootGifImg=null,screenGifImg=null;"
        "const SCREENS_PER_LOC=4;"

        // Colors - dark and light themes
        "const DARK={BG:'#0a0a14',CARD:'#141428',WHITE:'#FFFFFF',GRAY:'#888888',CYAN:'#00D4FF',ORANGE:'#FF6B35',BLUE:'#4DA8DA',YELLOW:'#FFE000',GREEN:'#00FF88'};"
        "const LIGHT={BG:'#E8F4FC',CARD:'#FFFFFF',WHITE:'#1a1a2e',GRAY:'#555555',CYAN:'#0088AA',ORANGE:'#E85520',BLUE:'#2980B9',YELLOW:'#D4A800',GREEN:'#00AA55'};"
        "let C=DARK;"

        // Condition string to number mapping
        "function condToNum(s){"
        "if(!s)return 9;"
        "const m={'Clear':0,'Partly Cloudy':1,'Cloudy':2,'Overcast':2,'Fog':3,'Mist':3,"
        "'Drizzle':4,'Light Rain':4,'Rain':5,'Heavy Rain':5,'Freezing Rain':6,"
        "'Snow':7,'Light Snow':7,'Heavy Snow':7,'Sleet':7,"
        "'Thunderstorm':8,'Thunder':8};"
        "return m[s]!==undefined?m[s]:9;}"

        // Simple pixel icons as 16x16 (smaller, cleaner)
        "const ICO={"
        "sun:'0000011000000000000001100000000000000110000000000000011000000000011111111111000001100110011000000110000001100000011000000110000001100000011000000110000001100000011000000110000001111111111100000001100110011000000000011000000000000001100000000000000110000000000000011000000000',"
        "moon:'0000111100000000001111111000000001111111100000001111111110000001111111110000001111111100000001111111100000001111111000000001111111000000001111111000000001111111100000001111111110000001111111111000000111111111100000001111111100000000011111000000000000110000000000',"
        "cloud:'0000011110000000001111111100000011000000110000110000000011001100000000001111000000000011110000000001111111111111111111111111111111111111111111000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000',"
        "rain:'0000011110000000001111111100000011000000110000110000000011001111111111111111111111111111110000000000000000100100100100000010010010010000001001001001000000010010010000000001001001000000000100100100000000010010010000000000000000000000000000000000000000000000',"
        "snow:'0000011110000000001111111100000011000000110000110000000011001111111111111111111111111111110000000000000000010001000100000001000100010000001110111011100000010001000100000001000100010000000000000000000000000000000000000000000000000000000000000000000000000000',"
        "thunder:'0000011110000000001111111100000011000000110000111111111111111111111111111111110000000000000000001111000000000000111000000000000011100000000001111110000000000011100000000000001110000000000000111000000000000001000000000000000000000000000000000000000000000000',"
        "fog:'0000000000000000111111111111111111111111111111110000000000000000000000000000000011111111111111111111111111111111000000000000000000000000000000001111111111111111111111111111111100000000000000000000000000000000111111111111111100000000000000000000000000000000',"
        "unknown:'0001111111100000011111111111000011100000011100001100000000110000110000000011000000000000011000000000000011000000000000110000000000011000000000001100000000000110000000000011000000000000110000000000000000000000000000000000000000110000000000001111000000000000110000000000'};"

        // Draw 16x16 icon
        "function drawIco(name,x,y,sz,col){"
        "const d=ICO[name]||ICO.unknown,sc=sz/16;"
        "ctx.fillStyle=col;"
        "for(let r=0;r<16;r++)for(let c=0;c<16;c++)"
        "if(d[r*16+c]==='1')ctx.fillRect(x+c*sc,y+r*sc,sc,sc);}"

        // Get icon name and color from condition
        "function getIco(cond,isDay){"
        "const c=typeof cond==='string'?condToNum(cond):cond;"
        "const icons=['sun','cloud','cloud','fog','rain','rain','rain','snow','thunder','unknown'];"
        "const colors=[isDay?C.YELLOW:C.GRAY,C.GRAY,C.GRAY,C.GRAY,C.BLUE,C.BLUE,C.BLUE,C.WHITE,C.YELLOW,C.GRAY];"
        "if(c===0&&!isDay)return{ico:'moon',col:C.GRAY};"
        "return{ico:icons[c]||'unknown',col:colors[c]||C.GRAY};}"

        // Format time with AM/PM
        "function fmtTime(){"
        "const n=new Date(),h=n.getHours(),m=n.getMinutes();"
        "const h12=h%12||12,ap=h<12?'AM':'PM';"
        "return{time:h12+':'+(m<10?'0':'')+m,ampm:ap};}"

        // Format date
        "function fmtDate(){"
        "const n=new Date(),mo=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];"
        "return mo[n.getMonth()]+' '+n.getDate();}"

        // Get tomorrow's date + offset
        "function getTomorrow(offset){"
        "const d=new Date();d.setDate(d.getDate()+1+offset);"
        "const days=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];"
        "return{day:days[d.getDay()],date:(d.getMonth()+1)+'/'+d.getDate()};}"

        // Temperature color
        "function tempCol(t){return t<0?C.BLUE:t<10?C.CYAN:t<20?C.WHITE:C.ORANGE;}"

        // Format temp
        "function fmtTemp(t,c){if(!c)t=t*9/5+32;return Math.round(t)+'°';}"

        // Draw current weather screen - 2 column layout (vertically centered)
        "function drawCurrent(){"
        "const locs=weatherData?.locations||[];"
        "if(!locs.length){ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"
        "ctx.fillStyle=C.WHITE;ctx.font='16px sans-serif';ctx.textAlign='center';"
        "ctx.fillText('No weather data',120,120);return;}"
        "const loc=locs[currentLoc]||locs[0],w=loc.current||{};"
        "const isDay=w.isDay!==false,useC=weatherData.useCelsius!==false;"
        "ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"
        // Time + AM/PM
        "const t=fmtTime();"
        "ctx.fillStyle=C.CYAN;ctx.font='bold 48px sans-serif';ctx.textAlign='center';"
        "ctx.fillText(t.time,100,52);"
        "ctx.font='18px sans-serif';ctx.fillText(t.ampm,178,52);"
        // Date + Location row
        "ctx.fillStyle=C.GRAY;ctx.font='14px sans-serif';"
        "ctx.fillText(fmtDate()+' • '+(loc.location||'Unknown'),120,76);"
        // 2-column layout: left=icon+condition, right=temp+hi/lo+precip
        // Row 2 starts at y=105 (more space from header)
        // Left column: icon (80x80) + condition directly below
        "const ico=getIco(w.condition,isDay);"
        "drawIco(ico.ico,15,105,80,ico.col);"
        // Condition text tight under icon (just a few px gap)
        "ctx.fillStyle=C.WHITE;ctx.font='14px sans-serif';ctx.textAlign='center';"
        "const cond=w.condition||'Unknown';"
        "ctx.fillText(cond.length>12?cond.substring(0,12):cond,55,195);"
        // Right column: temp centered, hi/lo below, precip at bottom
        "const temp=w.temperature||0;"
        "ctx.fillStyle=tempCol(temp);ctx.font='bold 56px sans-serif';"
        "ctx.fillText(fmtTemp(temp,useC),170,155);"
        // Hi/Lo centered below temp
        "const fc=loc.forecast||[];if(fc.length>0){"
        "const today=fc[0];"
        "ctx.font='14px sans-serif';"
        "ctx.fillStyle=C.ORANGE;ctx.fillText('↑'+fmtTemp(today.tempMax||0,useC),145,183);"
        "ctx.fillStyle=C.BLUE;ctx.fillText('↓'+fmtTemp(today.tempMin||0,useC),195,183);"
        // Precipitation below hi/lo
        "const pp=today.precipProbability||today.precipitationProb||0;"
        "ctx.fillStyle=pp>0?C.BLUE:C.GRAY;ctx.font='12px sans-serif';"
        "ctx.fillText((pp>0?'💧':'')+Math.round(pp)+'%',170,203);}"
        // Screen dots
        "drawDots();}"

        // Draw forecast screen (3 days starting from startIdx in forecast array)
        "function drawForecast(startIdx){"
        "const locs=weatherData?.locations||[];"
        "if(!locs.length)return;"
        "const loc=locs[currentLoc]||locs[0],fc=loc.forecast||[];"
        "const useC=weatherData.useCelsius!==false;"
        "ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"
        // Compact header with time and location
        "const t=fmtTime();"
        "ctx.fillStyle=C.CYAN;ctx.font='bold 20px sans-serif';ctx.textAlign='center';"
        "ctx.fillText(t.time+' '+t.ampm+' • '+(loc.location||'?'),120,20);"
        // 3 forecast cards (skip day 0 which is today, so startIdx+1)
        "const cw=76,ch=195,sp=4,sx=(240-3*cw-2*sp)/2;"
        "for(let i=0;i<3;i++){"
        "const fi=startIdx+i+1;"  // +1 to skip today
        "if(fi>=fc.length)continue;"
        "const day=fc[fi],cx=sx+i*(cw+sp);"
        "ctx.fillStyle=C.CARD;"
        "ctx.beginPath();ctx.roundRect(cx,30,cw,ch,6);ctx.fill();"
        // Day name + date (compact at top)
        "const tm=getTomorrow(startIdx+i);"
        "ctx.fillStyle=C.CYAN;ctx.font='bold 14px sans-serif';"
        "ctx.fillText(day.day||tm.day,cx+cw/2,46);"
        "ctx.fillStyle=C.GRAY;ctx.font='12px sans-serif';"
        "ctx.fillText(tm.date,cx+cw/2,62);"
        // Icon - 48x48 with more space from date
        "const ico=getIco(day.condition,true);"
        "drawIco(ico.ico,cx+(cw-48)/2,78,48,ico.col);"
        // High/Low temps (moved down to match icon shift)
        "ctx.fillStyle=C.ORANGE;ctx.font='bold 16px sans-serif';"
        "ctx.fillText('↑'+fmtTemp(day.tempMax||0,useC),cx+cw/2,148);"
        "ctx.fillStyle=C.BLUE;ctx.font='14px sans-serif';"
        "ctx.fillText('↓'+fmtTemp(day.tempMin||0,useC),cx+cw/2,168);"
        // Precip
        "const pp=day.precipProbability||day.precipitationProb||0;"
        "ctx.fillStyle=pp>0?C.BLUE:C.GRAY;ctx.font='12px sans-serif';"
        "ctx.fillText((pp>0?'💧':'')+Math.round(pp)+'%',cx+cw/2,188);"
        // Condition
        "ctx.fillStyle=C.GRAY;ctx.font='11px sans-serif';"
        "const cond=day.condition||'?';"
        "ctx.fillText(cond.length>9?cond.substring(0,9):cond,cx+cw/2,206);}"
        // Screen dots
        "drawDots();}"

        // Draw boot screen (special screen, not in rotation)
        "function drawBootScreen(){"
        "ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"
        // Title and version at top
        "ctx.fillStyle=C.CYAN;ctx.font='bold 28px sans-serif';ctx.textAlign='center';"
        "ctx.fillText('EpicWeatherBox',120,45);"
        "ctx.fillStyle=C.GRAY;ctx.font='16px sans-serif';"
        "ctx.fillText('v0.2.0-dev',120,75);"
        // Show boot GIF below title if exists (scaled to fit)
        "if(bootGifImg&&bootGifImg.complete){"
        "const w=bootGifImg.width,h=bootGifImg.height;"
        "const maxW=160,maxH=130;"
        "const sc=Math.min(maxW/w,maxH/h,1);"
        "const dw=w*sc,dh=h*sc;"
        "ctx.drawImage(bootGifImg,(240-dw)/2,95+(maxH-dh)/2,dw,dh);"
        "}else if(bootGifExists){"
        "ctx.font='12px sans-serif';ctx.fillStyle='#666';"
        "ctx.fillText('Loading...',120,160);}"
        // B indicator at bottom instead of dots
        "ctx.fillStyle=C.CYAN;ctx.font='bold 14px sans-serif';"
        "ctx.fillText('B',120,232);}"

        // Draw GIF animation screen
        "function drawGifScreen(){"
        "ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"
        // Time header
        "const t=fmtTime();"
        "ctx.fillStyle=C.WHITE;ctx.font='bold 24px sans-serif';ctx.textAlign='center';"
        "ctx.fillText(t.time+' '+t.ampm,120,35);"
        // Separator line
        "ctx.strokeStyle=C.GRAY;ctx.beginPath();ctx.moveTo(20,55);ctx.lineTo(220,55);ctx.stroke();"
        // Show screen GIF if exists
        "if(screenGifImg&&screenGifImg.complete){"
        "const w=screenGifImg.width,h=screenGifImg.height;"
        "const maxW=200,maxH=170;"
        "const sc=Math.min(maxW/w,maxH/h,1);"
        "const dw=w*sc,dh=h*sc;"
        "ctx.drawImage(screenGifImg,(240-dw)/2,60+(maxH-dh)/2,dw,dh);"
        "}else{"
        // GIF placeholder area
        "ctx.strokeStyle=C.CYAN;ctx.lineWidth=2;ctx.setLineDash([5,5]);"
        "ctx.strokeRect(40,70,160,140);ctx.setLineDash([]);"
        "ctx.fillStyle=C.GRAY;ctx.font='16px sans-serif';"
        "ctx.fillText(screenGifExists?'Loading GIF...':'[No GIF Uploaded]',120,145);"
        "ctx.font='12px sans-serif';ctx.fillStyle='#666';"
        "ctx.fillText('Upload via Admin panel',120,170);}"
        // Screen dots
        "drawDots();}"

        // Draw screen indicator dots
        "function drawDots(){"
        "const locs=weatherData?.locations||[],nLocs=locs.length||1;"
        "const screens=gifScreenEnabled?SCREENS_PER_LOC:SCREENS_PER_LOC-1;"
        "const total=nLocs*screens,cur=currentLoc*screens+Math.min(currentScreen,screens-1);"
        "const dotR=3,gap=10,sx=120-(total-1)*gap/2;"
        "for(let i=0;i<total;i++){"
        "ctx.fillStyle=i===cur?C.CYAN:C.GRAY;"
        "ctx.beginPath();ctx.arc(sx+i*gap,232,dotR,0,Math.PI*2);ctx.fill();}}"

        // Update HTML dots
        "function updateHtmlDots(){"
        "const el=document.getElementById('screenDots');el.innerHTML='';"
        // Show B indicator for boot screen
        "if(showingBoot){"
        "const b=document.createElement('div');b.className='dot active';b.textContent='B';"
        "b.style.cssText='width:auto;height:auto;border-radius:3px;padding:2px 6px;font-size:10px;font-weight:bold';el.appendChild(b);return;}"
        "const locs=weatherData?.locations||[],nLocs=locs.length||1;"
        "const screens=gifScreenEnabled?SCREENS_PER_LOC:SCREENS_PER_LOC-1;"
        "const total=nLocs*screens,cur=currentLoc*screens+Math.min(currentScreen,screens-1);"
        "for(let i=0;i<total;i++){const d=document.createElement('div');"
        "d.className='dot'+(i===cur?' active':'');el.appendChild(d);}}"

        // Main render
        "function render(){"
        "ctx.clearRect(0,0,240,240);"
        // Handle boot screen separately (not in rotation)
        "if(showingBoot){drawBootScreen();"
        "document.getElementById('screenLabel').textContent='Boot Screen';"
        "updateHtmlDots();return;}"
        "const names=['Current Weather','Forecast Days 1-3','Forecast Days 4-6','GIF Animation'];"
        "document.getElementById('screenLabel').textContent=names[currentScreen]+(currentScreen===3&&!gifScreenEnabled?' (disabled)':'');"
        "if(weatherData?.locations){"
        "const loc=weatherData.locations[currentLoc];"
        "document.getElementById('locName').textContent=loc?.location||'Unknown';"
        "document.getElementById('locIdx').textContent=currentLoc+1;"
        "document.getElementById('locTotal').textContent=weatherData.locations.length;}"
        "updateHtmlDots();"
        "switch(currentScreen){"
        "case 0:drawCurrent();break;"
        "case 1:drawForecast(0);break;"
        "case 2:drawForecast(3);break;"
        "case 3:drawGifScreen();break;}}"

        // Navigation
        "function nextScreen(){"
        "if(showingBoot){showingBoot=false;currentScreen=0;render();return;}"
        "const nLocs=weatherData?.locations?.length||1;"
        "const maxScreens=gifScreenEnabled?SCREENS_PER_LOC:SCREENS_PER_LOC-1;"
        "if(mainScreenOnly){"
        "currentLoc=(currentLoc+1)%nLocs;currentScreen=0;}"
        "else{"
        "currentScreen++;"
        "if(currentScreen>=maxScreens){currentScreen=0;currentLoc=(currentLoc+1)%nLocs;}}"
        "render();}"

        "function prevScreen(){"
        "if(showingBoot){showingBoot=false;currentScreen=0;render();return;}"
        "const nLocs=weatherData?.locations?.length||1;"
        "const maxScreens=gifScreenEnabled?SCREENS_PER_LOC:SCREENS_PER_LOC-1;"
        "if(mainScreenOnly){"
        "currentLoc=(currentLoc+nLocs-1)%nLocs;currentScreen=0;}"
        "else{"
        "currentScreen--;"
        "if(currentScreen<0){currentScreen=maxScreens-1;currentLoc=(currentLoc+nLocs-1)%nLocs;}}"
        "render();}"

        // Toggle boot screen view
        "function showBoot(){showingBoot=true;render();}"

        "function toggleAuto(){"
        "autoPlay=!autoPlay;"
        "document.getElementById('autoBtn').textContent='Auto: '+(autoPlay?'ON':'OFF');"
        "document.getElementById('autoBtn').className=autoPlay?'active':'';"
        "if(autoPlay)startAuto();else stopAuto();}"

        "function toggleTheme(){"
        "darkMode=!darkMode;C=darkMode?DARK:LIGHT;"
        "document.getElementById('themeBtn').textContent='Theme: '+(darkMode?'Dark':'Light');"
        "document.getElementById('themeBtn').className=darkMode?'':'active';"
        "document.body.style.background=darkMode?'#0d0d1a':'#f0f5fa';"
        "render();}"

        "function startAuto(){stopAuto();autoTimer=setInterval(nextScreen,10000);}"
        "function stopAuto(){if(autoTimer){clearInterval(autoTimer);autoTimer=null;}}"

        // Fetch config
        "async function fetchConfig(){"
        "try{const r=await fetch('/api/config');config=await r.json();"
        "mainScreenOnly=config.mainScreenOnly||false;"
        "gifScreenEnabled=config.gifScreenEnabled||false;"
        "console.log('Config:',config);"
        "}catch(e){console.error('Config fetch failed',e);}}"

        // Fetch GIF status and load images
        "async function fetchGifStatus(){"
        "try{const r=await fetch('/api/gif/status');const d=await r.json();"
        "bootGifExists=d.bootGifExists||false;"
        "screenGifExists=d.screenGifExists||false;"
        "console.log('GIF status:',d);"
        // Load boot GIF if exists
        "if(bootGifExists){"
        "bootGifImg=new Image();bootGifImg.onload=()=>render();"
        "bootGifImg.src='/api/gif/boot?t='+Date.now();}"
        // Load screen GIF if exists
        "if(screenGifExists){"
        "screenGifImg=new Image();screenGifImg.onload=()=>render();"
        "screenGifImg.src='/api/gif/screen?t='+Date.now();}"
        "}catch(e){console.error('GIF status fetch failed',e);}}"

        // Fetch weather
        "async function fetchWeather(){"
        "try{const r=await fetch('/api/weather');weatherData=await r.json();"
        "console.log('Weather data:',weatherData);render();"
        "}catch(e){console.error('Fetch failed',e);}}"

        "function refreshWeather(){fetch('/api/weather/refresh').then(()=>setTimeout(fetchWeather,2000));}"

        // Init
        "fetchConfig().then(()=>{fetchGifStatus();fetchWeather();});if(autoPlay)startAuto();setInterval(fetchWeather,60000);"
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
