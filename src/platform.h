/**
 * EpicWeatherBox - Platform Abstraction Layer
 *
 * Wraps platform-specific APIs behind a common interface.
 * All source files should include this instead of ESP8266-specific headers.
 *
 * Currently supports: ESP8266
 * Future: ESP32 (CYD)
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
#endif

// =============================================================================
// TYPE ALIASES
// =============================================================================

#if PLATFORM_ESP8266
    using PlatformWebServer        = ESP8266WebServer;
    using PlatformHTTPUpdateServer = ESP8266HTTPUpdateServer;
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

#endif // PLATFORM_ESP8266

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

#endif // PLATFORM_ESP8266

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

#endif // PLATFORM_ESP8266

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

#endif // PLATFORM_ESP8266

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

#endif // PLATFORM_ESP8266

// =============================================================================
// SYSTEM CONTROL
// =============================================================================

#if PLATFORM_ESP8266

/** Restart the device. */
inline void platformRestart() {
    ESP.restart();
}

#endif // PLATFORM_ESP8266

#endif // PLATFORM_H
