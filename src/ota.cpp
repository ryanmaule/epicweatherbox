/**
 * EpicWeatherBox Firmware - OTA Update Handler Implementation
 *
 * Implements both ArduinoOTA (for PlatformIO/Arduino IDE) and
 * web-based OTA (for browser-based updates).
 */

#include "ota.h"
#include <ESP8266mDNS.h>

// State tracking
static bool otaInProgress = false;
static ESP8266HTTPUpdateServer httpUpdateServer;

// Store HTML in PROGMEM to save RAM
static const char OTA_UPDATE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>EpicWeatherBox Firmware Update</title>
    <style>
        * { box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #eee;
            margin: 0;
            padding: 20px;
            min-height: 100vh;
        }
        .container {
            max-width: 500px;
            margin: 0 auto;
        }
        h1 {
            color: #00d4ff;
            text-align: center;
            margin-bottom: 30px;
        }
        .card {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 12px;
            padding: 25px;
            margin-bottom: 20px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        .warning {
            background: rgba(255, 193, 7, 0.15);
            border-color: rgba(255, 193, 7, 0.3);
            color: #ffc107;
        }
        .warning h3 {
            margin-top: 0;
            color: #ffc107;
        }
        form {
            display: flex;
            flex-direction: column;
            gap: 15px;
        }
        input[type="file"] {
            background: rgba(255, 255, 255, 0.1);
            border: 2px dashed rgba(255, 255, 255, 0.3);
            border-radius: 8px;
            padding: 20px;
            color: #eee;
            cursor: pointer;
        }
        input[type="file"]:hover {
            border-color: #00d4ff;
        }
        input[type="submit"] {
            background: #00d4ff;
            color: #1a1a2e;
            border: none;
            padding: 15px 30px;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
        }
        input[type="submit"]:hover {
            background: #00a8cc;
            transform: translateY(-2px);
        }
        input[type="submit"]:disabled {
            background: #666;
            cursor: not-allowed;
            transform: none;
        }
        .progress-container {
            display: none;
            margin-top: 20px;
        }
        .progress-bar {
            width: 100%;
            height: 30px;
            background: rgba(255, 255, 255, 0.1);
            border-radius: 15px;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #00d4ff, #00a8cc);
            width: 0%;
            transition: width 0.3s ease;
            border-radius: 15px;
        }
        .progress-text {
            text-align: center;
            margin-top: 10px;
            font-size: 14px;
        }
        .back-link {
            display: block;
            text-align: center;
            color: #00d4ff;
            text-decoration: none;
            margin-top: 20px;
        }
        .back-link:hover {
            text-decoration: underline;
        }
        ul {
            margin: 0;
            padding-left: 20px;
        }
        li {
            margin-bottom: 8px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Firmware Update</h1>

        <div class="card warning">
            <h3>Important</h3>
            <ul>
                <li>Do NOT disconnect power during update</li>
                <li>Update takes about 30-60 seconds</li>
                <li>Device will reboot automatically when complete</li>
                <li>Upload a <code>.bin</code> firmware file only</li>
            </ul>
        </div>

        <div class="card">
            <form method="POST" action="/update" enctype="multipart/form-data" id="upload_form">
                <input type="file" name="update" id="file" accept=".bin" required>
                <input type="submit" value="Upload Firmware" id="submit_btn">
            </form>

            <div class="progress-container" id="progress">
                <div class="progress-bar">
                    <div class="progress-fill" id="progress-fill"></div>
                </div>
                <div class="progress-text" id="progress-text">Uploading... 0%</div>
            </div>
        </div>

        <a href="/" class="back-link">Back to Home</a>
    </div>

    <script>
        const form = document.getElementById('upload_form');
        const progress = document.getElementById('progress');
        const progressFill = document.getElementById('progress-fill');
        const progressText = document.getElementById('progress-text');
        const submitBtn = document.getElementById('submit_btn');
        const fileInput = document.getElementById('file');

        form.addEventListener('submit', function(e) {
            e.preventDefault();

            const file = fileInput.files[0];
            if (!file) {
                alert('Please select a firmware file');
                return;
            }

            if (!file.name.endsWith('.bin')) {
                alert('Please select a .bin firmware file');
                return;
            }

            const formData = new FormData();
            formData.append('update', file);

            const xhr = new XMLHttpRequest();

            xhr.upload.addEventListener('progress', function(e) {
                if (e.lengthComputable) {
                    const percent = Math.round((e.loaded / e.total) * 100);
                    progressFill.style.width = percent + '%';
                    progressText.textContent = 'Uploading... ' + percent + '%';
                }
            });

            xhr.addEventListener('load', function() {
                if (xhr.status === 200) {
                    progressFill.style.width = '100%';
                    progressText.textContent = 'Update complete! Rebooting...';
                    setTimeout(function() {
                        progressText.textContent = 'Reconnecting in 10 seconds...';
                        setTimeout(function() {
                            window.location.href = '/';
                        }, 10000);
                    }, 2000);
                } else {
                    progressText.textContent = 'Update failed: ' + xhr.responseText;
                    progressFill.style.background = '#dc3545';
                    submitBtn.disabled = false;
                }
            });

            xhr.addEventListener('error', function() {
                progressText.textContent = 'Upload failed. Please try again.';
                progressFill.style.background = '#dc3545';
                submitBtn.disabled = false;
            });

            xhr.open('POST', '/update');
            xhr.send(formData);

            progress.style.display = 'block';
            submitBtn.disabled = true;
            progressText.textContent = 'Starting upload...';
        });
    </script>
</body>
</html>
)rawliteral";

/**
 * Initialize ArduinoOTA for wireless uploads from PlatformIO/Arduino IDE
 */
void initArduinoOTA(const char* hostname) {
    // Set hostname for mDNS
    ArduinoOTA.setHostname(hostname);

    // Set port
    ArduinoOTA.setPort(OTA_PORT);

    // Set password if defined and not empty
    #ifdef OTA_PASSWORD
    if (strlen(OTA_PASSWORD) > 0) {
        ArduinoOTA.setPassword(OTA_PASSWORD);
    }
    #endif

    // Callbacks for progress/status reporting
    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "firmware";
        } else {
            type = "filesystem";
        }
        Serial.println("\n[OTA] Starting " + type + " update...");
    });

    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
        Serial.println("\n[OTA] Update complete! Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static int lastPercent = -1;
        int percent = (progress / (total / 100));
        if (percent != lastPercent && percent % 10 == 0) {
            Serial.printf("[OTA] Progress: %u%%\n", percent);
            lastPercent = percent;
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
        Serial.printf("[OTA] Error[%u]: ", error);
        switch (error) {
            case OTA_AUTH_ERROR:
                Serial.println("Auth Failed");
                break;
            case OTA_BEGIN_ERROR:
                Serial.println("Begin Failed");
                break;
            case OTA_CONNECT_ERROR:
                Serial.println("Connect Failed");
                break;
            case OTA_RECEIVE_ERROR:
                Serial.println("Receive Failed");
                break;
            case OTA_END_ERROR:
                Serial.println("End Failed");
                break;
        }
    });

    // Start OTA service
    ArduinoOTA.begin();

    Serial.printf("[OTA] ArduinoOTA ready on port %d\n", OTA_PORT);
    Serial.printf("[OTA] Hostname: %s.local\n", hostname);
}

/**
 * Initialize web-based OTA update server
 */
void initWebOTA(ESP8266WebServer* server) {
    // Serve the update page
    server->on(OTA_UPDATE_PATH, HTTP_GET, [server]() {
        server->send(200, "text/html", FPSTR(OTA_UPDATE_HTML));
    });

    // Initialize the HTTP update server
    // This handles POST to /update with the firmware file
    #ifdef OTA_UPDATE_USERNAME
    if (strlen(OTA_UPDATE_USERNAME) > 0) {
        httpUpdateServer.setup(server, OTA_UPDATE_PATH, OTA_UPDATE_USERNAME, OTA_UPDATE_PASSWORD);
    } else {
        httpUpdateServer.setup(server, OTA_UPDATE_PATH);
    }
    #else
    httpUpdateServer.setup(server, OTA_UPDATE_PATH);
    #endif

    Serial.printf("[OTA] Web update available at http://%s%s\n",
                  WiFi.localIP().toString().c_str(), OTA_UPDATE_PATH);
}

/**
 * Handle OTA in main loop
 */
void handleOTA() {
    ArduinoOTA.handle();
}

/**
 * Check if OTA update is in progress
 */
bool isOTAInProgress() {
    return otaInProgress;
}

/**
 * Get the HTML page for web OTA updates
 */
String getOTAUpdatePage() {
    return FPSTR(OTA_UPDATE_HTML);
}
