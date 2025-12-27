/**
 * EpicWeatherBox Recovery Firmware
 *
 * Minimal firmware for recovering bricked devices.
 * Features:
 * - WiFi AP mode with captive portal (SSID: EpicWeatherBox-Recovery)
 * - Option to join an existing WiFi network
 * - Web-based OTA update at /update
 * - Clean UI matching EpicWeatherBox style
 *
 * Use this to flash the full firmware when the stock firmware
 * doesn't have enough space for a direct OTA update.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <DNSServer.h>
#include <LittleFS.h>

// Configuration
#define AP_SSID "EpicWeatherBox-Recovery"
#define AP_PASS ""  // Open network for easy access
#define DNS_PORT 53

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
DNSServer dnsServer;

bool isConnectedToNetwork = false;
String connectedSSID = "";
String deviceIP = "192.168.4.1";

// Scan for available networks
String scanNetworks() {
    String options = "";
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        String strength = (rssi > -50) ? "●●●●" : (rssi > -60) ? "●●●○" : (rssi > -70) ? "●●○○" : "●○○○";
        options += "<option value=\"" + ssid + "\">" + ssid + " (" + strength + ")</option>";
    }
    return options;
}

// Main HTML page
String getIndexPage() {
    String networkOptions = scanNetworks();
    String statusText = isConnectedToNetwork
        ? "<div class='status success'>Connected to: " + connectedSSID + "<br>Device IP: " + deviceIP + "</div>"
        : "<div class='status'>Not connected to any network. Using AP mode.</div>";

    return R"(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EpicWeatherBox Recovery</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;padding:20px}
.container{max-width:400px;margin:0 auto}
h1{color:#00d4ff;text-align:center;margin-bottom:20px;font-size:1.5em}
.card{background:rgba(255,255,255,0.05);border-radius:12px;padding:20px;margin-bottom:15px;border:1px solid rgba(255,255,255,0.1)}
.card h2{color:#00d4ff;margin-bottom:15px;font-size:1.1em}
.btn{display:block;width:100%;background:#00d4ff;color:#1a1a2e;padding:14px;border:none;border-radius:8px;font-size:1em;cursor:pointer;text-decoration:none;text-align:center;font-weight:bold}
.btn:hover{background:#00a8cc}
.btn-secondary{background:rgba(255,255,255,0.1);color:#eee}
.btn-secondary:hover{background:rgba(255,255,255,0.2)}
.form-group{margin-bottom:12px}
.form-group label{display:block;margin-bottom:4px;font-size:0.85em;color:#aaa}
.form-group select,.form-group input{width:100%;padding:10px;border:1px solid #333;border-radius:6px;background:#2a2a4e;color:#eee;font-size:1em}
.form-group select:focus,.form-group input:focus{outline:none;border-color:#00d4ff}
.status{padding:12px;border-radius:8px;margin-bottom:15px;text-align:center;font-size:0.9em;background:rgba(255,255,255,0.05);color:#888}
.status.success{background:rgba(0,200,100,0.15);color:#0c6}
.status.error{background:rgba(200,50,50,0.15);color:#f66}
.divider{text-align:center;color:#666;margin:20px 0;font-size:0.85em}
.info{font-size:0.8em;color:#666;margin-top:15px;text-align:center}
.logo{text-align:center;margin-bottom:10px;font-size:2em}
</style>
</head>
<body>
<div class="container">
<div class="logo">☀️</div>
<h1>EpicWeatherBox Recovery</h1>

)" + statusText + R"(

<div class="card">
<h2>📦 Upload Firmware</h2>
<p style="color:#aaa;font-size:0.85em;margin-bottom:15px">Upload the EpicWeatherBox firmware.bin file to flash your device.</p>
<a href="/update" class="btn">Upload Firmware</a>
</div>

<div class="divider">— or connect to WiFi first —</div>

<div class="card">
<h2>📶 Join Network</h2>
<p style="color:#aaa;font-size:0.85em;margin-bottom:15px">Connect to your home WiFi so you can flash from your regular network.</p>
<form action="/wifi" method="POST">
<div class="form-group">
<label>WiFi Network</label>
<select name="ssid" required>
<option value="">Select network...</option>
)" + networkOptions + R"(
</select>
</div>
<div class="form-group">
<label>Password</label>
<input type="password" name="pass" placeholder="Enter password">
</div>
<button type="submit" class="btn btn-secondary">Connect</button>
</form>
</div>

<div class="info">
After flashing, the device will reboot and you can configure it via the EpicWeatherBox admin panel.
</div>
</div>
</body>
</html>)";
}

// WiFi connect result page
String getWifiResultPage(bool success, const String& message) {
    String statusClass = success ? "success" : "error";
    String ipInfo = success ? "<p style='margin-top:10px'>Device IP: <strong>" + deviceIP + "</strong></p>" : "";

    return R"(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Connection</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,sans-serif;background:#1a1a2e;color:#eee;min-height:100vh;padding:20px}
.container{max-width:400px;margin:0 auto;text-align:center}
h1{color:#00d4ff;margin-bottom:20px;font-size:1.5em}
.status{padding:20px;border-radius:12px;margin-bottom:20px}
.status.success{background:rgba(0,200,100,0.15);color:#0c6}
.status.error{background:rgba(200,50,50,0.15);color:#f66}
.btn{display:inline-block;background:#00d4ff;color:#1a1a2e;padding:14px 30px;border-radius:8px;text-decoration:none;font-weight:bold;margin:10px}
.btn:hover{background:#00a8cc}
.info{font-size:0.85em;color:#888;margin-top:20px}
</style>
</head>
<body>
<div class="container">
<h1>☀️ EpicWeatherBox</h1>
<div class="status )" + statusClass + R"(">
)" + message + ipInfo + R"(
</div>
<a href="/" class="btn">Back to Recovery</a>
<a href="/update" class="btn">Upload Firmware</a>
)" + (success ? "<div class='info'>You can now access the device at http://" + deviceIP + "/ from your network.</div>" : "") + R"(
</div>
</body>
</html>)";
}

void handleRoot() {
    server.send(200, "text/html", getIndexPage());
}

void handleWifiConnect() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    if (ssid.length() == 0) {
        server.send(200, "text/html", getWifiResultPage(false, "Please select a network."));
        return;
    }

    Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());

    // Enable AP+STA mode to maintain AP while connecting
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Wait for connection (with timeout)
    int timeout = 30;  // 15 seconds
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        Serial.print(".");
        timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        isConnectedToNetwork = true;
        connectedSSID = ssid;
        deviceIP = WiFi.localIP().toString();
        Serial.printf("\nConnected! IP: %s\n", deviceIP.c_str());
        server.send(200, "text/html", getWifiResultPage(true, "Successfully connected to <strong>" + ssid + "</strong>!"));
    } else {
        Serial.println("\nConnection failed!");
        WiFi.mode(WIFI_AP);  // Fall back to AP only
        server.send(200, "text/html", getWifiResultPage(false, "Failed to connect to <strong>" + ssid + "</strong>. Check password and try again."));
    }
}

// Captive portal detection endpoints
void handleCaptivePortal() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

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
    deviceIP = ip.toString();
    Serial.printf("AP Started: %s\n", AP_SSID);
    Serial.printf("IP Address: %s\n", deviceIP.c_str());

    // Start DNS server for captive portal
    dnsServer.start(DNS_PORT, "*", ip);

    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/wifi", HTTP_POST, handleWifiConnect);

    // Captive portal detection endpoints
    server.on("/generate_204", handleCaptivePortal);  // Android
    server.on("/hotspot-detect.html", handleCaptivePortal);  // iOS
    server.on("/connecttest.txt", handleCaptivePortal);  // Windows
    server.on("/ncsi.txt", handleCaptivePortal);  // Windows
    server.on("/fwlink", handleCaptivePortal);  // Windows
    server.onNotFound(handleCaptivePortal);

    // Setup OTA update handler
    httpUpdater.setup(&server, "/update");

    server.begin();
    Serial.println("Web server started on port 80");
    Serial.println("Captive portal active - connect to EpicWeatherBox-Recovery WiFi");
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    yield();
}
