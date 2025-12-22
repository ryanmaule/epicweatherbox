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

    // Weather API endpoint
    server.on("/api/weather", HTTP_GET, []() {
        JsonDocument doc;

        // Primary location weather
        JsonObject primary = doc["primary"].to<JsonObject>();
        JsonDocument primaryDoc;
        weatherToJson(getPrimaryWeather(), primaryDoc);
        primary.set(primaryDoc.as<JsonObject>());

        // Secondary location (if enabled)
        if (isSecondaryLocationEnabled()) {
            JsonObject secondary = doc["secondary"].to<JsonObject>();
            JsonDocument secondaryDoc;
            weatherToJson(getSecondaryWeather(), secondaryDoc);
            secondary.set(secondaryDoc.as<JsonObject>());
        }

        // Add metadata
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

        // Primary location
        const WeatherData& primary = getPrimaryWeather();
        JsonObject p = doc["primary"].to<JsonObject>();
        p["name"] = primary.locationName;
        p["lat"] = primary.latitude;
        p["lon"] = primary.longitude;

        // Secondary location
        if (isSecondaryLocationEnabled()) {
            const WeatherData& secondary = getSecondaryWeather();
            JsonObject s = doc["secondary"].to<JsonObject>();
            s["enabled"] = true;
            s["name"] = secondary.locationName;
            s["lat"] = secondary.latitude;
            s["lon"] = secondary.longitude;
        } else {
            JsonObject s = doc["secondary"].to<JsonObject>();
            s["enabled"] = false;
        }

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

        // Update primary location
        JsonObject primary = doc["primary"];
        if (primary) {
            const char* name = primary["name"];
            float lat = primary["lat"] | 0.0f;
            float lon = primary["lon"] | 0.0f;
            if (name && lat != 0 && lon != 0) {
                setPrimaryLocation(name, lat, lon);
            }
        }

        // Update secondary location
        JsonObject secondary = doc["secondary"];
        if (secondary) {
            bool enabled = secondary["enabled"] | false;
            setSecondaryLocationEnabled(enabled);
            if (enabled) {
                const char* name = secondary["name"];
                float lat = secondary["lat"] | 0.0f;
                float lon = secondary["lon"] | 0.0f;
                if (name && lat != 0 && lon != 0) {
                    setSecondaryLocation(name, lat, lon);
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

    // Version endpoint (original firmware compatibility)
    server.on("/v.json", HTTP_GET, []() {
        JsonDocument doc;
        doc["v"] = FIRMWARE_VERSION;

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
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
        "<li>Phase C: Display Driver - Pending</li>"
        "</ul></div>");

    html += F("</div></body></html>");

    server.send(200, "text/html", html);
}

/**
 * Handle admin page - minimal location config
 */
void handleAdmin() {
    const WeatherData& w = getPrimaryWeather();
    bool celsius = getUseCelsius();
    const char* unit = celsius ? "C" : "F";

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
        "a{color:#00d4ff}.hint{font-size:0.8em;color:#666;margin-top:5px}</style></head><body><div class='c'><h1>EpicWeatherBox</h1>");

    // Current weather status
    html += F("<div class='card'><h3>Current Weather</h3>");
    if (w.valid) {
        html += String(w.locationName) + F(": ");
        html += String((int)w.current.temperature) + "°" + unit + ", ";
        html += conditionToString(w.current.condition);
    } else {
        html += F("No data - configure location below");
    }
    html += F("</div>");

    // Location config form
    html += F("<div class='card'><h3>Location</h3>"
        "<form id='f'><label>Display Name</label>"
        "<input type='text' id='name' value='");
    html += w.locationName;
    html += F("'><p class='hint'>Name shown on display (e.g. \"Aurora\" or \"Home\")</p>"
        "<div class='row'><div><label>Latitude</label>"
        "<input type='number' id='lat' step='0.0001' value='");
    html += String(w.latitude, 4);
    html += F("'></div><div><label>Longitude</label>"
        "<input type='number' id='lon' step='0.0001' value='");
    html += String(w.longitude, 4);
    html += F("'></div></div><p class='hint'>Find coordinates at latlong.net or use button below</p>"
        "<label>Temperature</label><select id='unit'>"
        "<option value='f'");
    html += celsius ? "" : " selected";
    html += F(">Fahrenheit</option><option value='c'");
    html += celsius ? " selected" : "";
    html += F(">Celsius</option></select>"
        "<button type='submit'>Save & Refresh</button>"
        "<button type='button' onclick='geo()' style='background:#444;margin-left:10px'>Use My Location</button>"
        "<div id='st' class='status'></div></form></div>");

    // Links
    html += F("<div class='card' style='text-align:center'>"
        "<a href='/'>Home</a> | <a href='/api/weather'>Weather API</a> | "
        "<a href='/update'>Firmware</a> | <a href='/reboot'>Reboot</a></div>");

    // JavaScript
    html += F("<script>"
        "document.getElementById('f').onsubmit=async e=>{"
        "e.preventDefault();const s=document.getElementById('st');"
        "s.style.display='block';s.className='status';s.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({primary:{name:document.getElementById('name').value,"
        "lat:parseFloat(document.getElementById('lat').value),"
        "lon:parseFloat(document.getElementById('lon').value)},"
        "useCelsius:document.getElementById('unit').value==='c'})});"
        "const d=await r.json();s.className='status '+(d.success?'ok':'err');"
        "s.textContent=d.message;if(d.success)setTimeout(()=>location.reload(),2000);"
        "}catch(e){s.className='status err';s.textContent='Error';}};"
        "function geo(){if(!navigator.geolocation)return alert('Not supported');"
        "navigator.geolocation.getCurrentPosition(p=>{"
        "document.getElementById('lat').value=p.coords.latitude.toFixed(4);"
        "document.getElementById('lon').value=p.coords.longitude.toFixed(4);"
        "},()=>alert('Could not get location'));}</script>");

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
