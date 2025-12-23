/**
 * EpicWeatherBox Firmware - Weather API Interface
 *
 * Fetches weather data from Open-Meteo API (free, no API key required)
 * Supports 7-day forecast and multiple location weather display (up to 5).
 */

#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// =============================================================================
// WEATHER CONFIGURATION
// =============================================================================

// Open-Meteo API endpoint (free, no API key needed!)
// Using HTTP instead of HTTPS for ESP8266 compatibility (saves ~20KB RAM)
#define WEATHER_API_URL "http://api.open-meteo.com/v1/forecast"

// Update interval (milliseconds) - 20 minutes default
#define WEATHER_UPDATE_INTERVAL_MS (20 * 60 * 1000)

// Maximum forecast days supported
#define WEATHER_FORECAST_DAYS 7

// Maximum number of locations supported
#define MAX_WEATHER_LOCATIONS 5

// =============================================================================
// WEATHER CODE MAPPING (WMO Weather interpretation codes)
// =============================================================================
// https://open-meteo.com/en/docs#weathervariables
// 0 = Clear sky
// 1, 2, 3 = Mainly clear, partly cloudy, overcast
// 45, 48 = Fog
// 51, 53, 55 = Drizzle
// 56, 57 = Freezing drizzle
// 61, 63, 65 = Rain
// 66, 67 = Freezing rain
// 71, 73, 75 = Snow
// 77 = Snow grains
// 80, 81, 82 = Rain showers
// 85, 86 = Snow showers
// 95 = Thunderstorm
// 96, 99 = Thunderstorm with hail

// Weather condition categories (simplified for display)
enum WeatherCondition {
    WEATHER_CLEAR = 0,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_CLOUDY,
    WEATHER_FOG,
    WEATHER_DRIZZLE,
    WEATHER_RAIN,
    WEATHER_FREEZING_RAIN,
    WEATHER_SNOW,
    WEATHER_THUNDERSTORM,
    WEATHER_UNKNOWN
};

// =============================================================================
// DATA STRUCTURES
// =============================================================================

/**
 * Current weather conditions
 */
struct CurrentWeather {
    float temperature;          // Current temperature
    float apparentTemperature;  // "Feels like" temperature
    float windSpeed;            // Wind speed
    float windDirection;        // Wind direction in degrees
    float precipitation;        // Precipitation amount
    int weatherCode;            // WMO weather code
    WeatherCondition condition; // Simplified condition category
    bool isDay;                 // Day/night indicator
    unsigned long timestamp;    // When this data was fetched
};

/**
 * Single day forecast
 */
struct ForecastDay {
    float tempMax;              // Maximum temperature
    float tempMin;              // Minimum temperature
    float precipitationSum;     // Total precipitation
    float precipitationProb;    // Precipitation probability (%)
    float windSpeedMax;         // Maximum wind speed
    int weatherCode;            // WMO weather code
    WeatherCondition condition; // Simplified condition category
    char dayName[4];            // Short day name (Mon, Tue, etc.)
};

/**
 * Complete weather data for a location
 */
struct WeatherData {
    // Location info
    char locationName[32];      // City/location name
    float latitude;
    float longitude;
    char timezone[32];          // Timezone string

    // Current conditions
    CurrentWeather current;

    // 7-day forecast
    ForecastDay forecast[WEATHER_FORECAST_DAYS];
    int forecastDays;           // Number of valid forecast days

    // Status
    bool valid;                 // Is this data valid?
    unsigned long lastUpdate;   // Last successful update time
    int errorCount;             // Consecutive error count
    char lastError[64];         // Last error message
};

/**
 * Location configuration
 */
struct WeatherLocation {
    char name[32];              // Display name
    float latitude;
    float longitude;
    bool enabled;               // Is this location active?
};

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * Initialize weather system
 * Call once in setup()
 */
void initWeather();

/**
 * Update weather data if needed (checks interval)
 * Call in loop() - handles timing internally
 * Returns true if an update was performed
 */
bool updateWeather();

/**
 * Force immediate weather update for all locations
 * Returns true if update was successful
 */
bool forceWeatherUpdate();

/**
 * Fetch weather for a specific location
 * @param lat Latitude
 * @param lon Longitude
 * @param data Output weather data structure
 * @return true on success
 */
bool fetchWeather(float lat, float lon, WeatherData& data);

// =============================================================================
// MULTI-LOCATION API (NEW)
// =============================================================================

/**
 * Get the number of configured locations
 */
int getLocationCount();

/**
 * Get weather data by index (0 to getLocationCount()-1)
 */
const WeatherData& getWeather(int index);

/**
 * Get location config by index
 */
const WeatherLocation& getLocation(int index);

/**
 * Add a new location
 * @return true if added, false if at max capacity
 */
bool addLocation(const char* name, float lat, float lon);

/**
 * Remove a location by index
 * @return true if removed, false if can't remove (last location or invalid index)
 */
bool removeLocation(int index);

/**
 * Update an existing location
 * @return true if updated, false if invalid index
 */
bool updateLocation(int index, const char* name, float lat, float lon);

/**
 * Clear all locations and reset to default
 */
void clearLocations();

// =============================================================================
// LEGACY API (BACKWARD COMPATIBILITY)
// =============================================================================

/**
 * Get weather data for primary location (index 0)
 * @deprecated Use getWeather(0) instead
 */
const WeatherData& getPrimaryWeather();

/**
 * Get weather data for secondary location (index 1)
 * @deprecated Use getWeather(1) instead
 */
const WeatherData& getSecondaryWeather();

/**
 * Set primary location (index 0)
 * @deprecated Use updateLocation(0, ...) instead
 */
void setPrimaryLocation(const char* name, float lat, float lon);

/**
 * Set secondary location (index 1)
 * @deprecated Use addLocation() or updateLocation(1, ...) instead
 */
void setSecondaryLocation(const char* name, float lat, float lon);

/**
 * Enable/disable secondary location
 * @deprecated Use addLocation/removeLocation instead
 */
void setSecondaryLocationEnabled(bool enabled);

/**
 * Check if secondary location is enabled
 * @deprecated Use getLocationCount() > 1 instead
 */
bool isSecondaryLocationEnabled();

/**
 * Get time until next weather update (milliseconds)
 */
unsigned long getNextUpdateIn();

/**
 * Set temperature unit (true = Celsius, false = Fahrenheit)
 */
void setUseCelsius(bool celsius);

/**
 * Get temperature unit setting
 */
bool getUseCelsius();

/**
 * Convert WMO weather code to simplified condition
 */
WeatherCondition weatherCodeToCondition(int code);

/**
 * Get human-readable condition string
 */
const char* conditionToString(WeatherCondition condition);

/**
 * Get short condition string (for small displays)
 */
const char* conditionToShortString(WeatherCondition condition);

/**
 * Get weather icon character (for icon fonts or emoji)
 */
const char* conditionToIcon(WeatherCondition condition, bool isDay = true);

/**
 * Save weather locations to LittleFS
 */
bool saveWeatherConfig();

/**
 * Load weather locations from LittleFS
 */
bool loadWeatherConfig();

/**
 * Get weather data as JSON for API response
 */
void weatherToJson(const WeatherData& data, JsonDocument& doc);

// =============================================================================
// DISPLAY SETTINGS
// =============================================================================

/**
 * Get/Set display brightness (0-100)
 */
int getBrightness();
void setBrightness(int brightness);

/**
 * Night mode settings
 * When enabled, automatically dims display and uses dark theme during night hours
 */
bool getNightModeEnabled();
void setNightModeEnabled(bool enabled);
int getNightModeStartHour();  // Hour to start night mode (0-23)
void setNightModeStartHour(int hour);
int getNightModeEndHour();    // Hour to end night mode (0-23)
void setNightModeEndHour(int hour);
int getNightModeBrightness(); // Brightness during night mode (0-100)
void setNightModeBrightness(int brightness);

/**
 * Check if currently in night mode based on time
 */
bool isNightModeActive(int currentHour);

/**
 * Show forecast screens in rotation
 * true = show forecast screens (current + forecast days)
 * false = show only current weather screens (still cycles between locations)
 */
bool getShowForecast();
void setShowForecast(bool show);

/**
 * Screen cycle time in seconds (5-60)
 * How long to display each screen before switching
 */
int getScreenCycleTime();
void setScreenCycleTime(int seconds);

/**
 * GIF screen settings
 * Controls whether the animated GIF screen appears in rotation
 */
bool getGifScreenEnabled();
void setGifScreenEnabled(bool enabled);

/**
 * Theme mode
 * 0 = auto (dark at night, light during day based on night mode hours)
 * 1 = always dark
 * 2 = always light
 */
int getThemeMode();
void setThemeMode(int mode);

#endif // WEATHER_H
