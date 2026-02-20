/**
 * EpicWeatherBox - Platform Abstraction Layer
 *
 * Wraps platform-specific APIs behind a common interface.
 * All source files should include this instead of platform-specific headers.
 *
 * Supports: ESP8266 (SmallTV-Ultra), ESP32 (CYD)
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <Arduino.h>

// =============================================================================
// PLATFORM DETECTION
// =============================================================================

#if defined(ESP8266)
    #define PLATFORM_ESP8266 1
#elif defined(ESP32)
    #define PLATFORM_ESP32 1
#else
    #error "Unsupported platform — expected ESP8266 or ESP32"
#endif

// =============================================================================
// NETWORK INCLUDES
// =============================================================================

#if PLATFORM_ESP8266
    #include <ESP8266WiFi.h>
    #include <ESP8266WebServer.h>
    #include <ESP8266HTTPClient.h>
    #include <ESP8266HTTPUpdateServer.h>
    #include <ESP8266mDNS.h>
    extern "C" {
        #include <user_interface.h>
    }
#elif PLATFORM_ESP32
    #include <WiFi.h>
    #include <WebServer.h>
    #include <HTTPClient.h>
    #include <HTTPUpdateServer.h>
    #include <ESPmDNS.h>
#endif

// =============================================================================
// TYPE ALIASES
// =============================================================================

#if PLATFORM_ESP8266
    using PlatformWebServer        = ESP8266WebServer;
    using PlatformHTTPUpdateServer = ESP8266HTTPUpdateServer;
#elif PLATFORM_ESP32
    using PlatformWebServer        = WebServer;
    using PlatformHTTPUpdateServer = HTTPUpdateServer;
#endif

// =============================================================================
// WATCHDOG API
// =============================================================================

#if PLATFORM_ESP8266

/** Disable software WDT, enable hardware WDT (8 s timeout). */
inline void platformWdtSetup() {
    ESP.wdtDisable();
    // Hardware watchdog with 8-second timeout
    ESP.wdtEnable(WDTO_8S);
}

/** Feed the watchdog timer — call during long-running loops. */
inline void platformWdtFeed() {
    ESP.wdtFeed();
}

#elif PLATFORM_ESP32

#include <esp_task_wdt.h>

/** Configure task watchdog timer (8 s timeout). */
inline void platformWdtSetup() {
    esp_task_wdt_init(8, true);
    esp_task_wdt_add(NULL);  // Add current task
}

/** Feed the watchdog timer — call during long-running loops. */
inline void platformWdtFeed() {
    esp_task_wdt_reset();
}

#endif

// =============================================================================
// PWM API
// =============================================================================

#if PLATFORM_ESP8266

/** Set the PWM output range (e.g. 100 for 0-100%). */
inline void platformPwmSetRange(uint32_t range) {
    analogWriteRange(range);
}

/** Set the PWM frequency in Hz. */
inline void platformPwmSetFreq(uint32_t freq) {
    analogWriteFreq(freq);
}

/** Write a PWM value to a pin. */
inline void platformPwmWrite(uint8_t pin, uint32_t value) {
    analogWrite(pin, value);
}

#elif PLATFORM_ESP32

// ESP32 uses LEDC peripheral for PWM. We use channel 0, 8-bit resolution.
// platformPwmSetRange/SetFreq are no-ops; LEDC is configured on first write.

static bool _ledcInitialized = false;
static uint8_t _ledcPin = 0;

inline void platformPwmSetRange(uint32_t range) {
    // No-op on ESP32 — LEDC uses fixed 8-bit resolution (0-255)
    (void)range;
}

inline void platformPwmSetFreq(uint32_t freq) {
    // No-op on ESP32 — frequency set during LEDC setup
    (void)freq;
}

/** Write a PWM value to a pin. Lazy-inits LEDC channel 0 on first call. */
inline void platformPwmWrite(uint8_t pin, uint32_t value) {
    if (!_ledcInitialized || _ledcPin != pin) {
        ledcSetup(0, 1000, 8);    // Channel 0, 1kHz, 8-bit
        ledcAttachPin(pin, 0);
        _ledcPin = pin;
        _ledcInitialized = true;
    }
    // Scale 0-100 input to 0-255 for 8-bit LEDC
    uint32_t scaled = (value * 255) / 100;
    ledcWrite(0, scaled);
}

#endif

// =============================================================================
// SYSTEM INFO
// =============================================================================

#if PLATFORM_ESP8266

inline uint32_t platformGetFreeHeap() {
    return ESP.getFreeHeap();
}

inline uint32_t platformGetChipId() {
    return ESP.getChipId();
}

inline uint32_t platformGetFlashSize() {
    return ESP.getFlashChipRealSize();
}

inline uint32_t platformGetSketchSize() {
    return ESP.getSketchSize();
}

inline uint32_t platformGetFreeSketchSpace() {
    return ESP.getFreeSketchSpace();
}

#elif PLATFORM_ESP32

inline uint32_t platformGetFreeHeap() {
    return ESP.getFreeHeap();
}

inline uint32_t platformGetChipId() {
    // Derive a 32-bit ID from the lower bytes of the MAC address
    uint64_t mac = ESP.getEfuseMac();
    return (uint32_t)(mac >> 24);
}

inline uint32_t platformGetFlashSize() {
    return ESP.getFlashChipSize();
}

inline uint32_t platformGetSketchSize() {
    return ESP.getSketchSize();
}

inline uint32_t platformGetFreeSketchSpace() {
    return ESP.getFreeSketchSpace();
}

#endif

// =============================================================================
// SSL BUFFER SIZING
// =============================================================================

#if PLATFORM_ESP8266
#include <WiFiClientSecure.h>

/**
 * Set SSL/TLS buffer sizes for memory-constrained environments.
 * On ESP8266, BearSSL defaults to 16KB per direction — this lets us shrink it.
 */
inline void platformSetSSLBufferSizes(WiFiClientSecure& client,
                                       uint16_t recv, uint16_t send) {
    client.setBufferSizes(recv, send);
}

#elif PLATFORM_ESP32
#include <WiFiClientSecure.h>

/**
 * No-op on ESP32 — mbedTLS manages buffer sizes automatically.
 */
inline void platformSetSSLBufferSizes(WiFiClientSecure& client,
                                       uint16_t recv, uint16_t send) {
    (void)client; (void)recv; (void)send;
}

#endif

// =============================================================================
// FILESYSTEM INFO
// =============================================================================

#if PLATFORM_ESP8266
#include <LittleFS.h>

/** Return bytes used on LittleFS. */
inline size_t platformGetFsUsed() {
    FSInfo info;
    LittleFS.info(info);
    return info.usedBytes;
}

/** Return total bytes available on LittleFS. */
inline size_t platformGetFsTotal() {
    FSInfo info;
    LittleFS.info(info);
    return info.totalBytes;
}

#elif PLATFORM_ESP32
#include <LittleFS.h>

/** Return bytes used on LittleFS. */
inline size_t platformGetFsUsed() {
    return LittleFS.usedBytes();
}

/** Return total bytes available on LittleFS. */
inline size_t platformGetFsTotal() {
    return LittleFS.totalBytes();
}

#endif

// =============================================================================
// SYSTEM CONTROL
// =============================================================================

#if PLATFORM_ESP8266

/** Restart the device. */
inline void platformRestart() {
    ESP.restart();
}

#elif PLATFORM_ESP32

/** Restart the device. */
inline void platformRestart() {
    ESP.restart();
}

#endif

#endif // PLATFORM_H
