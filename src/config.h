/**
 * EpicWeatherBox Firmware Configuration
 *
 * Custom firmware for SmallTV-Ultra hardware
 * Defines hardware pins, default settings, and constants
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// FIRMWARE INFO
// =============================================================================
#define FIRMWARE_VERSION "1.7.0"
#define DEVICE_NAME "EpicWeatherBox"
#define DEVICE_MODEL "EpicWeatherBox"

// =============================================================================
// DISPLAY CONFIGURATION
// =============================================================================
// Display resolution (based on image size requirements from original firmware)
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 240

// Display type - ST7789T3 (1.54" 240x240 IPS TFT)
#define DISPLAY_ST7789

// Display pins - Confirmed from hardware reverse engineering
// Source: https://www.elektroda.com/news/news4113933.html
#define TFT_CS   4   // Chip select (GPIO4)
#define TFT_DC   0   // Data/Command (GPIO0)
#define TFT_RST  2   // Reset (GPIO2)
#define TFT_BL   5   // Backlight PWM (GPIO5)
#define TFT_MOSI 13  // SPI MOSI (GPIO13)
#define TFT_SCLK 14  // SPI Clock (GPIO14)
// Note: MISO not used

// ST7789 requires SPI Mode 3
#define TFT_SPI_MODE SPI_MODE3

// =============================================================================
// WIFI CONFIGURATION
// =============================================================================
#define WIFI_AP_SSID "EpicWeatherBox"   // Fun name for setup portal
#define WIFI_AP_PASSWORD ""           // Open network for setup
#define WIFI_CONFIG_PORTAL_TIMEOUT 300 // 5 minutes

// =============================================================================
// WEB SERVER CONFIGURATION
// =============================================================================
#define WEB_SERVER_PORT 80
#define WEBSOCKET_PORT 81

// =============================================================================
// WEATHER API CONFIGURATION
// =============================================================================

// OpenWeatherMap (current weather)
#define OWM_API_HOST "api.openweathermap.org"
#define OWM_API_PORT 80
#define OWM_CURRENT_WEATHER_PATH "/data/2.5/weather"

// WeatherAPI.com (forecast) - Note: Free tier limited to 3 days
#define WEATHERAPI_HOST "api.weatherapi.com"
#define WEATHERAPI_PORT 80
#define WEATHERAPI_FORECAST_PATH "/v1/forecast.json"

// Open-Meteo (alternative - free 7-day forecast)
#define OPENMETEO_HOST "api.open-meteo.com"
#define OPENMETEO_PORT 443  // HTTPS
#define OPENMETEO_FORECAST_PATH "/v1/forecast"

// Default update intervals (minutes)
#define WEATHER_UPDATE_INTERVAL_DEFAULT 20
#define WEATHER_UPDATE_INTERVAL_MIN 5
#define WEATHER_UPDATE_INTERVAL_MAX 60

// YouTube API update interval (30 minutes to conserve API quota)
#define YOUTUBE_UPDATE_INTERVAL_MS (30 * 60 * 1000)

// Forecast days
#define FORECAST_DAYS_ORIGINAL 3
#define FORECAST_DAYS_EXTENDED 7

// =============================================================================
// TIME CONFIGURATION
// =============================================================================
#define NTP_SERVER_DEFAULT "pool.ntp.org"
#define NTP_UPDATE_INTERVAL 3600000  // 1 hour in milliseconds
#define TIME_API_HOST "worldtimeapi.org"
#define TIME_API_PATH "/api/timezone/UTC"

// =============================================================================
// DISPLAY THEMES
// =============================================================================
#define THEME_WEATHER_CLOCK_TODAY 1
#define THEME_WEATHER_FORECAST 2
#define THEME_PHOTO_ALBUM 3
#define THEME_TIME_STYLE_1 4
#define THEME_TIME_STYLE_2 5
#define THEME_TIME_STYLE_3 6
#define THEME_SIMPLE_WEATHER_CLOCK 7
#define THEME_COUNT 7

// =============================================================================
// BRIGHTNESS SETTINGS
// =============================================================================
#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 100
#define BRIGHTNESS_DEFAULT 50
#define NIGHT_MODE_START_DEFAULT 22  // 10 PM
#define NIGHT_MODE_END_DEFAULT 7     // 7 AM
#define NIGHT_MODE_BRIGHTNESS_DEFAULT 20

// =============================================================================
// FILE SYSTEM PATHS
// =============================================================================
// Configuration files
#define CONFIG_FILE "/config.json"
#define WIFI_FILE "/wifi.json"
#define CITY_FILE "/city.json"
#define KEY_FILE "/key.json"
#define FKEY_FILE "/fkey.json"
#define TIMEBRT_FILE "/timebrt.json"
#define THEME_LIST_FILE "/theme_list.json"
#define VERSION_FILE "/v.json"
#define SPACE_FILE "/space.json"

// Directories
#define GIF_DIR "/gif"
#define IMAGE_DIR "/image"

// Asset sizes
#define GIF_SIZE 80         // 80x80 pixels for weather screen GIF
#define IMAGE_SIZE 240      // 240x240 pixels for photo album

// =============================================================================
// MEMORY OPTIMIZATION
// =============================================================================
// Maximum sizes for JSON documents
#define JSON_DOC_SIZE_SMALL 128
#define JSON_DOC_SIZE_MEDIUM 512
#define JSON_DOC_SIZE_LARGE 2048
#define JSON_DOC_SIZE_WEATHER 4096

// HTTP buffer sizes
#define HTTP_BUFFER_SIZE 1024

// =============================================================================
// FEATURE FLAGS
// =============================================================================
// Enable/disable features to save memory
#define FEATURE_WEATHER_FORECAST 1
#define FEATURE_DUAL_LOCATION 1       // New feature: dual location weather
#define FEATURE_EXTENDED_FORECAST 1   // New feature: 7-day forecast
#define FEATURE_PHOTO_ALBUM 1
#define FEATURE_GIF_ANIMATION 0       // Disabled - ESP8266 memory constraints
#define FEATURE_NIGHT_MODE 1
#define FEATURE_OTA_UPDATE 1

// Disabled features (not needed for EpicWeatherBox)
#define FEATURE_STOCK_TICKER 0        // Disabled - not needed
#define FEATURE_BILIBILI 0            // Disabled - not needed
#define FEATURE_YOUTUBE_STATS 1       // YouTube channel stats screen
// Note: Countdown events are now part of the carousel system (weather.h)

// =============================================================================
// DEBUG SETTINGS
// =============================================================================
#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

#endif // CONFIG_H
