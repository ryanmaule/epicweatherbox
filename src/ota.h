/**
 * EpicWeatherBox Firmware - OTA Update Handler
 *
 * Provides both ArduinoOTA and web-based firmware updates.
 * This is CRITICAL functionality - without working OTA, the device
 * cannot be updated (USB-C is power only, no data).
 */

#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <ESP8266HTTPUpdateServer.h>

// OTA Configuration
#define OTA_HOSTNAME "epicweatherbox"
#define OTA_PORT 8266
// Leave password empty for easier development, set in production
#define OTA_PASSWORD ""

// Update paths
#define OTA_UPDATE_PATH "/update"
#define OTA_UPDATE_USERNAME ""  // Empty = no auth required
#define OTA_UPDATE_PASSWORD ""  // Empty = no auth required

/**
 * Initialize ArduinoOTA
 * Call this in setup() after WiFi is connected
 *
 * @param hostname Device hostname for mDNS
 */
void initArduinoOTA(const char* hostname = OTA_HOSTNAME);

/**
 * Initialize web-based OTA update server
 * Adds /update endpoint to the provided web server
 *
 * @param server Pointer to ESP8266WebServer instance
 */
void initWebOTA(ESP8266WebServer* server);

/**
 * Handle OTA in main loop
 * Call this in loop() to process OTA requests
 */
void handleOTA();

/**
 * Check if OTA update is in progress
 * Use this to pause other activities during update
 *
 * @return true if update is in progress
 */
bool isOTAInProgress();

/**
 * Get the HTML page for web OTA updates
 *
 * @return HTML string for the update page
 */
String getOTAUpdatePage();

#endif // OTA_H
