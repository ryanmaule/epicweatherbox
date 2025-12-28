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

// Maximum carousel items (3 locations + 3 countdowns + 3 custom + 1 youtube + 3 images = 13)
#define MAX_CAROUSEL_ITEMS 13
#define MAX_COUNTDOWN_EVENTS 3
#define MAX_CUSTOM_SCREENS 3
#define MAX_YOUTUBE_CHANNELS 1
#define MAX_IMAGE_SCREENS 3
#define MAX_IMAGE_FILE_SIZE 102400  // 100KB max per image

// =============================================================================
// CAROUSEL & COUNTDOWN TYPES
// =============================================================================

/**
 * Carousel item types - what kind of screen to display
 */
enum CarouselItemType {
    CAROUSEL_LOCATION = 0,   // Weather location (shows 3 screens: current + 2 forecast)
    CAROUSEL_COUNTDOWN = 1,  // Countdown event (single screen)
    CAROUSEL_CUSTOM = 2,     // Custom text screen (single screen)
    CAROUSEL_YOUTUBE = 3,    // YouTube channel stats (single screen)
    CAROUSEL_IMAGE = 4       // Custom image screen (single screen)
};

/**
 * Countdown event types - preset and custom events
 */
enum CountdownEventType {
    COUNTDOWN_BIRTHDAY = 0,   // Custom date, recurring yearly
    COUNTDOWN_EASTER = 1,     // Calculated each year (Computus algorithm)
    COUNTDOWN_HALLOWEEN = 2,  // Oct 31
    COUNTDOWN_VALENTINE = 3,  // Feb 14
    COUNTDOWN_CHRISTMAS = 4,  // Dec 25
    COUNTDOWN_CUSTOM = 5      // Custom date and title
};

/**
 * Countdown event configuration
 */
struct CountdownEvent {
    uint8_t type;           // CountdownEventType
    uint8_t month;          // 1-12 (for Birthday and Custom types)
    uint8_t day;            // 1-31
    char title[32];         // Display title (used for Birthday and Custom)
};

/**
 * Custom text screen configuration
 */
struct CustomScreenConfig {
    char header[17];        // Top-right text (16 chars + null)
    char body[81];          // Center text (80 chars + null) - e.g., "My Weather Clock is AWESOME!"
    char footer[31];        // Bottom bar text (30 chars + null)
};

/**
 * YouTube channel configuration
 */
struct YouTubeChannel {
    char channelHandle[32]; // Channel handle without @ (e.g., "sterlings.funtube")
};

/**
 * YouTube channel stats (fetched from API)
 */
struct YouTubeData {
    char channelName[48];   // Display name from API
    char channelHandle[32]; // Channel handle
    char channelId[32];     // Channel ID (UCxxxx)
    uint32_t subscribers;   // Subscriber count
    uint32_t views;         // Total view count
    uint32_t videos;        // Video count
    bool valid;             // Is this data valid?
    unsigned long lastUpdate; // Last successful update time
    char lastError[64];     // Last error message
};

/**
 * YouTube configuration
 */
struct YouTubeConfig {
    char apiKey[48];        // YouTube Data API v3 key
    char channelHandle[32]; // Channel to display
    bool enabled;           // Is YouTube screen enabled?
};

/**
 * Image screen configuration
 */
struct ImageScreenConfig {
    char filename[24];      // e.g., "/images/image_0.jpg"
    bool valid;             // Is image file present?
};

/**
 * Single carousel item - references data by type and index
 */
struct CarouselItem {
    uint8_t type;           // CarouselItemType
    uint8_t dataIndex;      // Index into respective data array
};

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
    int utcOffsetSeconds;       // UTC offset in seconds (for NTP)

    // Current conditions
    CurrentWeather current;

    // 7-day forecast
    ForecastDay forecast[WEATHER_FORECAST_DAYS];
    int forecastDays;           // Number of valid forecast days

    // Sunrise/sunset times (minutes since midnight for precise night mode)
    uint16_t sunriseMinutes;    // Minutes since midnight (0-1439)
    uint16_t sunsetMinutes;     // Minutes since midnight (0-1439)

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
 * @param currentMinutes Minutes since midnight in local time (0-1439)
 */
bool isNightModeActive(int currentMinutes);

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

// Theme mode functions moved to themes.h
// int getThemeMode();
// void setThemeMode(int mode);

/**
 * UI vertical nudge in pixels
 * Positive values move UI up, negative values move UI down
 * Range: -20 to +20 pixels
 */
int getUiNudgeY();
void setUiNudgeY(int nudge);

// =============================================================================
// CUSTOM SCREEN SETTINGS
// =============================================================================

/**
 * Custom text screen - appears after weather screens in rotation
 * Global setting (same content shown after every location)
 */
bool getCustomScreenEnabled();
void setCustomScreenEnabled(bool enabled);

/**
 * Custom screen header text (max 16 chars)
 * Displayed in top-right corner next to time
 */
const char* getCustomScreenHeader();
void setCustomScreenHeader(const char* text);

/**
 * Custom screen body text (max 160 chars)
 * Displayed centered with dynamic font sizing based on length
 */
const char* getCustomScreenBody();
void setCustomScreenBody(const char* text);

/**
 * Custom screen footer text (max 30 chars)
 * Displayed in rounded bar at bottom
 */
const char* getCustomScreenFooter();
void setCustomScreenFooter(const char* text);

// =============================================================================
// CAROUSEL SYSTEM
// =============================================================================

/**
 * Get number of items in carousel
 */
uint8_t getCarouselCount();

/**
 * Get carousel item by index
 */
const CarouselItem& getCarouselItem(uint8_t index);

/**
 * Set entire carousel (items array and count)
 */
void setCarousel(const CarouselItem* items, uint8_t count);

/**
 * Add item to carousel
 * @return true if added, false if at max capacity
 */
bool addCarouselItem(uint8_t type, uint8_t dataIndex);

/**
 * Remove carousel item by index
 */
bool removeCarouselItem(uint8_t index);

/**
 * Move carousel item from one position to another
 */
bool moveCarouselItem(uint8_t fromIndex, uint8_t toIndex);

// =============================================================================
// COUNTDOWN EVENTS
// =============================================================================

/**
 * Get number of countdown events
 */
uint8_t getCountdownCount();

/**
 * Get countdown event by index
 */
const CountdownEvent& getCountdown(uint8_t index);

/**
 * Add countdown event
 * @return index of new event, or -1 if at max capacity
 */
int addCountdown(uint8_t type, uint8_t month, uint8_t day, const char* title);

/**
 * Update countdown event
 */
bool updateCountdown(uint8_t index, uint8_t type, uint8_t month, uint8_t day, const char* title);

/**
 * Remove countdown event by index
 */
bool removeCountdown(uint8_t index);

// =============================================================================
// CUSTOM SCREENS (Multiple)
// =============================================================================

/**
 * Get number of custom screens
 */
uint8_t getCustomScreenCount();

/**
 * Get custom screen config by index
 */
const CustomScreenConfig& getCustomScreenConfig(uint8_t index);

/**
 * Add custom screen
 * @return index of new screen, or -1 if at max capacity
 */
int addCustomScreenConfig(const char* header, const char* body, const char* footer);

/**
 * Update custom screen
 */
bool updateCustomScreenConfig(uint8_t index, const char* header, const char* body, const char* footer);

/**
 * Remove custom screen by index
 */
bool removeCustomScreenConfig(uint8_t index);

// =============================================================================
// YOUTUBE STATS
// =============================================================================

/**
 * Initialize YouTube system
 */
void initYouTube();

/**
 * Update YouTube stats if needed (checks interval)
 * Call in loop() - handles timing internally
 * Returns true if an update was performed
 */
bool updateYouTube();

/**
 * Force immediate YouTube stats update
 * Returns true if update was successful
 */
bool forceYouTubeUpdate();

/**
 * Get YouTube configuration
 */
const YouTubeConfig& getYouTubeConfig();

/**
 * Get YouTube stats data
 */
const YouTubeData& getYouTubeData();

/**
 * Set YouTube API key
 */
void setYouTubeApiKey(const char* key);

/**
 * Set YouTube channel handle
 */
void setYouTubeChannelHandle(const char* handle);

/**
 * Enable/disable YouTube screen
 */
void setYouTubeEnabled(bool enabled);

/**
 * Check if YouTube is properly configured (has API key and channel)
 */
bool isYouTubeConfigured();

/**
 * Save YouTube config to LittleFS
 */
bool saveYouTubeConfig();

/**
 * Load YouTube config from LittleFS
 */
bool loadYouTubeConfig();

// =============================================================================
// IMAGE SCREENS
// =============================================================================

/**
 * Get number of image screens
 */
uint8_t getImageScreenCount();

/**
 * Get image screen config by index
 */
const ImageScreenConfig& getImageScreenConfig(uint8_t index);

/**
 * Add image screen (when file uploaded)
 * @return index of new screen, or -1 if at max capacity
 */
int addImageScreenConfig(const char* filename);

/**
 * Remove image screen by index (also deletes file)
 */
bool removeImageScreenConfig(uint8_t index);

/**
 * Validate image file (check JPG header, size)
 */
bool validateImageFile(const char* filename);

#endif // WEATHER_H
