/**
 * EpicWeatherBox Firmware - Weather API Implementation
 *
 * Fetches weather data from Open-Meteo API (free, no API key required)
 * Supports 7-day forecast and multiple location weather display (up to 5).
 */

#include "weather.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>

// =============================================================================
// STATIC DATA
// =============================================================================

// Weather data storage (array for multiple locations)
static WeatherData weatherData[MAX_WEATHER_LOCATIONS];

// Location configuration (array for multiple locations)
static WeatherLocation locations[MAX_WEATHER_LOCATIONS] = {
    {"Seattle", 47.6062, -122.3321, true},  // Default first location
    {"", 0, 0, false},
    {"", 0, 0, false},
    {"", 0, 0, false},
    {"", 0, 0, false}
};

// Number of configured locations (at least 1 required)
static int locationCount = 1;

// Display settings
static bool useCelsius = false;  // false = Fahrenheit, true = Celsius
static int brightness = 50;      // 0-100
static bool nightModeEnabled = true;
static int nightModeStartHour = 22;  // 10 PM
static int nightModeEndHour = 7;     // 7 AM
static int nightModeBrightness = 20;
static bool showForecast = true;  // true = show forecast screens, false = current weather only
static int screenCycleTime = 10;  // seconds between screen changes (5-60)
// themeMode moved to themes.cpp
static bool gifScreenEnabled = false;  // Show GIF screen in rotation
static int uiNudgeY = 0;  // UI vertical offset in pixels (positive=up, negative=down)

// Custom screen settings (legacy - single screen, kept for backward compatibility)
static bool customScreenEnabled = false;
static char customScreenHeader[17] = "";   // 16 chars + null
static char customScreenBody[161] = "";    // 160 chars + null (legacy size)
static char customScreenFooter[31] = "";   // 30 chars + null

// =============================================================================
// CAROUSEL SYSTEM DATA
// =============================================================================

// Carousel items (ordered list of screens to display)
static CarouselItem carousel[MAX_CAROUSEL_ITEMS];
static uint8_t carouselCount = 0;

// Countdown events
static CountdownEvent countdowns[MAX_COUNTDOWN_EVENTS] = {
    {COUNTDOWN_BIRTHDAY, 1, 1, ""},
    {COUNTDOWN_BIRTHDAY, 1, 1, ""},
    {COUNTDOWN_BIRTHDAY, 1, 1, ""}
};
static uint8_t countdownCount = 0;

// Custom text screens (multiple, replaces single customScreen for carousel)
static CustomScreenConfig customScreens[MAX_CUSTOM_SCREENS] = {
    {"", "", ""},
    {"", "", ""},
    {"", "", ""}
};
static uint8_t customScreenCount = 0;

// YouTube stats
static YouTubeConfig youtubeConfig = {"", "", false};
static YouTubeData youtubeData = {"", "", "", 0, 0, 0, false, 0, ""};
static unsigned long youtubeLastUpdateTime = 0;
static bool youtubeInitialized = false;
static const char* YOUTUBE_CONFIG_FILE = "/youtube_config.json";

// Timing
static unsigned long lastUpdateTime = 0;
static bool initialized = false;

// Config file path
static const char* WEATHER_CONFIG_FILE = "/weather_config.json";

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Convert WMO weather code to simplified condition
 */
WeatherCondition weatherCodeToCondition(int code) {
    if (code == 0) return WEATHER_CLEAR;
    if (code >= 1 && code <= 2) return WEATHER_PARTLY_CLOUDY;
    if (code == 3) return WEATHER_CLOUDY;
    if (code >= 45 && code <= 48) return WEATHER_FOG;
    if (code >= 51 && code <= 55) return WEATHER_DRIZZLE;
    if (code >= 56 && code <= 57) return WEATHER_FREEZING_RAIN;
    if (code >= 61 && code <= 65) return WEATHER_RAIN;
    if (code >= 66 && code <= 67) return WEATHER_FREEZING_RAIN;
    if (code >= 71 && code <= 77) return WEATHER_SNOW;
    if (code >= 80 && code <= 82) return WEATHER_RAIN;
    if (code >= 85 && code <= 86) return WEATHER_SNOW;
    if (code >= 95 && code <= 99) return WEATHER_THUNDERSTORM;
    return WEATHER_UNKNOWN;
}

/**
 * Get human-readable condition string
 */
const char* conditionToString(WeatherCondition condition) {
    switch (condition) {
        case WEATHER_CLEAR: return "Clear";
        case WEATHER_PARTLY_CLOUDY: return "Partly Cloudy";
        case WEATHER_CLOUDY: return "Cloudy";
        case WEATHER_FOG: return "Fog";
        case WEATHER_DRIZZLE: return "Drizzle";
        case WEATHER_RAIN: return "Rain";
        case WEATHER_FREEZING_RAIN: return "Freezing Rain";
        case WEATHER_SNOW: return "Snow";
        case WEATHER_THUNDERSTORM: return "Thunderstorm";
        default: return "Unknown";
    }
}

/**
 * Get short condition string
 */
const char* conditionToShortString(WeatherCondition condition) {
    switch (condition) {
        case WEATHER_CLEAR: return "Clear";
        case WEATHER_PARTLY_CLOUDY: return "P.Cloudy";
        case WEATHER_CLOUDY: return "Cloudy";
        case WEATHER_FOG: return "Fog";
        case WEATHER_DRIZZLE: return "Drizzle";
        case WEATHER_RAIN: return "Rain";
        case WEATHER_FREEZING_RAIN: return "F.Rain";
        case WEATHER_SNOW: return "Snow";
        case WEATHER_THUNDERSTORM: return "T.Storm";
        default: return "???";
    }
}

/**
 * Normalize UTF-8 string to ASCII for TFT display
 * Converts common Latin diacritics to their base characters
 * e.g., "Cancún" -> "Cancun", "São Paulo" -> "Sao Paulo"
 */
void normalizeToAscii(char* dest, const char* src, size_t maxLen) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < maxLen - 1; ) {
        unsigned char c = (unsigned char)src[i];

        // ASCII characters pass through
        if (c < 0x80) {
            dest[j++] = src[i++];
            continue;
        }

        // UTF-8 two-byte sequences (0xC0-0xDF followed by 0x80-0xBF)
        if ((c & 0xE0) == 0xC0 && (src[i+1] & 0xC0) == 0x80) {
            uint16_t codepoint = ((c & 0x1F) << 6) | (src[i+1] & 0x3F);
            char replacement = '?';

            // Common Latin diacritics
            switch (codepoint) {
                // A variants
                case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
                    replacement = 'A'; break;
                case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
                    replacement = 'a'; break;
                // C variants
                case 0x00C7: replacement = 'C'; break;
                case 0x00E7: replacement = 'c'; break;
                // E variants
                case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
                    replacement = 'E'; break;
                case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
                    replacement = 'e'; break;
                // I variants
                case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
                    replacement = 'I'; break;
                case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
                    replacement = 'i'; break;
                // N variants
                case 0x00D1: replacement = 'N'; break;
                case 0x00F1: replacement = 'n'; break;
                // O variants
                case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
                    replacement = 'O'; break;
                case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
                    replacement = 'o'; break;
                // U variants
                case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
                    replacement = 'U'; break;
                case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
                    replacement = 'u'; break;
                // Y variants
                case 0x00DD: replacement = 'Y'; break;
                case 0x00FD: case 0x00FF: replacement = 'y'; break;
                // German sharp s
                case 0x00DF: replacement = 's'; break;
                // AE ligature
                case 0x00C6: replacement = 'A'; break;  // Æ -> A
                case 0x00E6: replacement = 'a'; break;  // æ -> a
                default: replacement = '?'; break;
            }
            dest[j++] = replacement;
            i += 2;
            continue;
        }

        // UTF-8 three-byte sequences - skip and replace with ?
        if ((c & 0xF0) == 0xE0) {
            dest[j++] = '?';
            i += 3;
            continue;
        }

        // UTF-8 four-byte sequences - skip and replace with ?
        if ((c & 0xF8) == 0xF0) {
            dest[j++] = '?';
            i += 4;
            continue;
        }

        // Invalid UTF-8, skip byte
        i++;
    }
    dest[j] = '\0';
}

/**
 * Get weather icon (emoji-style for web, can adapt for icon fonts)
 */
const char* conditionToIcon(WeatherCondition condition, bool isDay) {
    switch (condition) {
        case WEATHER_CLEAR:
            return isDay ? "☀️" : "🌙";
        case WEATHER_PARTLY_CLOUDY:
            return isDay ? "⛅" : "☁️";
        case WEATHER_CLOUDY:
            return "☁️";
        case WEATHER_FOG:
            return "🌫️";
        case WEATHER_DRIZZLE:
            return "🌦️";
        case WEATHER_RAIN:
            return "🌧️";
        case WEATHER_FREEZING_RAIN:
            return "🌨️";
        case WEATHER_SNOW:
            return "❄️";
        case WEATHER_THUNDERSTORM:
            return "⛈️";
        default:
            return "❓";
    }
}

/**
 * Get day name from day of week (0 = Sunday)
 */
static void getDayName(int dayOfWeek, char* buffer) {
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    strcpy(buffer, days[dayOfWeek % 7]);
}

// =============================================================================
// API FETCH
// =============================================================================

/**
 * Build Open-Meteo API URL
 */
static String buildApiUrl(float lat, float lon) {
    String url = WEATHER_API_URL;
    url += "?latitude=" + String(lat, 4);
    url += "&longitude=" + String(lon, 4);
    url += "&current_weather=true";
    url += "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,weathercode,windspeed_10m_max,sunrise,sunset";
    url += useCelsius ? "&temperature_unit=celsius" : "&temperature_unit=fahrenheit";
    url += "&windspeed_unit=mph";
    url += "&precipitation_unit=inch";
    url += "&timezone=auto";
    url += "&forecast_days=" + String(WEATHER_FORECAST_DAYS);
    return url;
}

/**
 * Fetch weather for a specific location
 */
bool fetchWeather(float lat, float lon, WeatherData& data) {
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(data.lastError, "WiFi not connected", sizeof(data.lastError));
        data.errorCount++;
        return false;
    }

    String url = buildApiUrl(lat, lon);
    Serial.printf("[WEATHER] Fetching: %s\n", url.c_str());

    // Use regular WiFiClient for HTTP (saves RAM vs BearSSL)
    WiFiClient client;

    HTTPClient http;
    http.setTimeout(10000);  // 10 second timeout

    if (!http.begin(client, url)) {
        strncpy(data.lastError, "HTTP begin failed", sizeof(data.lastError));
        data.errorCount++;
        Serial.println(F("[WEATHER] HTTP begin failed"));
        return false;
    }

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        snprintf(data.lastError, sizeof(data.lastError), "HTTP error: %d", httpCode);
        data.errorCount++;
        Serial.printf("[WEATHER] HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[WEATHER] Response size: %d bytes\n", payload.length());

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        snprintf(data.lastError, sizeof(data.lastError), "JSON error: %s", error.c_str());
        data.errorCount++;
        Serial.printf("[WEATHER] JSON parse error: %s\n", error.c_str());
        return false;
    }

    // Store location info
    data.latitude = doc["latitude"] | lat;
    data.longitude = doc["longitude"] | lon;

    const char* tz = doc["timezone"];
    if (tz) {
        strncpy(data.timezone, tz, sizeof(data.timezone) - 1);
    }

    // Get UTC offset for time display
    data.utcOffsetSeconds = doc["utc_offset_seconds"] | 0;

    // Parse current weather
    JsonObject current = doc["current_weather"];
    if (current) {
        data.current.temperature = current["temperature"] | 0.0f;
        data.current.windSpeed = current["windspeed"] | 0.0f;
        data.current.windDirection = current["winddirection"] | 0.0f;
        data.current.weatherCode = current["weathercode"] | 0;
        data.current.isDay = current["is_day"] | 1;
        data.current.condition = weatherCodeToCondition(data.current.weatherCode);
        data.current.timestamp = millis();
    }

    // Parse daily forecast
    JsonObject daily = doc["daily"];
    if (daily) {
        JsonArray tempMax = daily["temperature_2m_max"];
        JsonArray tempMin = daily["temperature_2m_min"];
        JsonArray precip = daily["precipitation_sum"];
        JsonArray precipProb = daily["precipitation_probability_max"];
        JsonArray codes = daily["weathercode"];
        JsonArray wind = daily["windspeed_10m_max"];
        JsonArray times = daily["time"];

        data.forecastDays = min((int)tempMax.size(), WEATHER_FORECAST_DAYS);

        for (int i = 0; i < data.forecastDays; i++) {
            data.forecast[i].tempMax = tempMax[i] | 0.0f;
            data.forecast[i].tempMin = tempMin[i] | 0.0f;
            data.forecast[i].precipitationSum = precip[i] | 0.0f;
            data.forecast[i].precipitationProb = precipProb[i] | 0.0f;
            data.forecast[i].windSpeedMax = wind[i] | 0.0f;
            data.forecast[i].weatherCode = codes[i] | 0;
            data.forecast[i].condition = weatherCodeToCondition(data.forecast[i].weatherCode);

            // Parse date to get day name
            const char* dateStr = times[i];
            if (dateStr && strlen(dateStr) >= 10) {
                // Date format: YYYY-MM-DD
                // Simple calculation for day of week
                int year, month, day;
                sscanf(dateStr, "%d-%d-%d", &year, &month, &day);

                // Zeller's formula for day of week
                if (month < 3) {
                    month += 12;
                    year--;
                }
                int dow = (day + 13*(month+1)/5 + year + year/4 - year/100 + year/400) % 7;
                // Convert from Zeller (0=Sat) to standard (0=Sun)
                dow = (dow + 6) % 7;
                getDayName(dow, data.forecast[i].dayName);
            } else {
                strcpy(data.forecast[i].dayName, "???");
            }
        }

        // Parse sunrise/sunset for today (index 0) - format: "2024-01-01T07:23"
        const char* sunriseStr = daily["sunrise"][0];
        const char* sunsetStr = daily["sunset"][0];
        if (sunriseStr && strlen(sunriseStr) >= 13) {
            int hour = 0;
            sscanf(sunriseStr + 11, "%d", &hour);  // Skip "YYYY-MM-DDTHH"
            data.sunriseHour = (uint8_t)hour;
        } else {
            data.sunriseHour = 6;  // Default 6 AM
        }
        if (sunsetStr && strlen(sunsetStr) >= 13) {
            int hour = 0;
            sscanf(sunsetStr + 11, "%d", &hour);
            data.sunsetHour = (uint8_t)hour;
        } else {
            data.sunsetHour = 18;  // Default 6 PM
        }
        Serial.printf("[WEATHER] Sunrise: %d:00, Sunset: %d:00\n", data.sunriseHour, data.sunsetHour);
    }

    // Success!
    data.valid = true;
    data.lastUpdate = millis();
    data.errorCount = 0;
    data.lastError[0] = '\0';

    Serial.printf("[WEATHER] Success! Temp: %.1f°F, Condition: %s\n",
                  data.current.temperature,
                  conditionToString(data.current.condition));

    return true;
}

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * Initialize weather system
 */
void initWeather() {
    if (initialized) return;

    Serial.println(F("[WEATHER] Initializing..."));

    // Clear all weather data
    for (int i = 0; i < MAX_WEATHER_LOCATIONS; i++) {
        memset(&weatherData[i], 0, sizeof(WeatherData));
    }

    // Load saved configuration
    loadWeatherConfig();

    // Copy location names to weather data
    for (int i = 0; i < locationCount; i++) {
        strncpy(weatherData[i].locationName, locations[i].name, sizeof(weatherData[i].locationName));
    }

    initialized = true;
    Serial.printf("[WEATHER] Initialized with %d location(s)\n", locationCount);
}

/**
 * Update weather if interval has elapsed
 */
bool updateWeather() {
    if (!initialized) {
        initWeather();
    }

    unsigned long now = millis();

    // Check if update is needed
    if (lastUpdateTime > 0 && (now - lastUpdateTime) < WEATHER_UPDATE_INTERVAL_MS) {
        return false;  // Not time yet
    }

    return forceWeatherUpdate();
}

/**
 * Force immediate weather update
 */
bool forceWeatherUpdate() {
    Serial.printf("[WEATHER] Updating weather for %d location(s)...\n", locationCount);

    bool success = true;

    // Update all enabled locations
    for (int i = 0; i < locationCount; i++) {
        if (locations[i].enabled) {
            strncpy(weatherData[i].locationName, locations[i].name, sizeof(weatherData[i].locationName));
            Serial.printf("[WEATHER] Fetching location %d: %s\n", i, locations[i].name);
            if (!fetchWeather(locations[i].latitude, locations[i].longitude, weatherData[i])) {
                success = false;
            }
        }
    }

    lastUpdateTime = millis();
    return success;
}

// =============================================================================
// MULTI-LOCATION API (NEW)
// =============================================================================

/**
 * Get the number of configured locations
 */
int getLocationCount() {
    return locationCount;
}

/**
 * Get weather data by index
 */
const WeatherData& getWeather(int index) {
    if (index < 0 || index >= MAX_WEATHER_LOCATIONS) {
        index = 0;  // Return first location as fallback
    }
    return weatherData[index];
}

/**
 * Get location config by index
 */
const WeatherLocation& getLocation(int index) {
    if (index < 0 || index >= MAX_WEATHER_LOCATIONS) {
        index = 0;  // Return first location as fallback
    }
    return locations[index];
}

/**
 * Add a new location
 * Returns true if added, false if at max capacity
 */
bool addLocation(const char* name, float lat, float lon) {
    if (locationCount >= MAX_WEATHER_LOCATIONS) {
        Serial.println(F("[WEATHER] Cannot add location - at max capacity"));
        return false;
    }

    int idx = locationCount;
    // Normalize name to ASCII for TFT display (e.g., "Cancún" -> "Cancun")
    normalizeToAscii(locations[idx].name, name, sizeof(locations[idx].name));
    locations[idx].latitude = lat;
    locations[idx].longitude = lon;
    locations[idx].enabled = true;

    // Clear weather data for new location
    memset(&weatherData[idx], 0, sizeof(WeatherData));
    normalizeToAscii(weatherData[idx].locationName, name, sizeof(weatherData[idx].locationName));

    locationCount++;
    Serial.printf("[WEATHER] Added location %d: %s (%.4f, %.4f)\n", idx, locations[idx].name, lat, lon);
    return true;
}

/**
 * Remove a location by index
 * Returns true if removed, false if can't remove (last location or invalid index)
 */
bool removeLocation(int index) {
    // Can't remove if it's the last location or invalid index
    if (locationCount <= 1 || index < 0 || index >= locationCount) {
        Serial.println(F("[WEATHER] Cannot remove location"));
        return false;
    }

    Serial.printf("[WEATHER] Removing location %d: %s\n", index, locations[index].name);

    // Shift all locations after this one down
    for (int i = index; i < locationCount - 1; i++) {
        locations[i] = locations[i + 1];
        weatherData[i] = weatherData[i + 1];
    }

    // Clear the last slot
    locationCount--;
    memset(&locations[locationCount], 0, sizeof(WeatherLocation));
    memset(&weatherData[locationCount], 0, sizeof(WeatherData));

    Serial.printf("[WEATHER] Now have %d location(s)\n", locationCount);
    return true;
}

/**
 * Update an existing location
 * Returns true if updated, false if invalid index
 */
bool updateLocation(int index, const char* name, float lat, float lon) {
    if (index < 0 || index >= locationCount) {
        return false;
    }

    // Normalize name to ASCII for TFT display (e.g., "Cancún" -> "Cancun")
    normalizeToAscii(locations[index].name, name, sizeof(locations[index].name));
    locations[index].latitude = lat;
    locations[index].longitude = lon;
    locations[index].enabled = true;

    // Update weather data location name and invalidate cache
    normalizeToAscii(weatherData[index].locationName, name, sizeof(weatherData[index].locationName));
    weatherData[index].valid = false;

    Serial.printf("[WEATHER] Updated location %d: %s (%.4f, %.4f)\n", index, locations[index].name, lat, lon);
    return true;
}

/**
 * Clear all locations and reset to default
 */
void clearLocations() {
    // Reset to single default location
    strcpy(locations[0].name, "Seattle");
    locations[0].latitude = 47.6062;
    locations[0].longitude = -122.3321;
    locations[0].enabled = true;

    // Clear remaining slots
    for (int i = 1; i < MAX_WEATHER_LOCATIONS; i++) {
        memset(&locations[i], 0, sizeof(WeatherLocation));
        memset(&weatherData[i], 0, sizeof(WeatherData));
    }

    locationCount = 1;
    memset(&weatherData[0], 0, sizeof(WeatherData));
    strncpy(weatherData[0].locationName, locations[0].name, sizeof(weatherData[0].locationName));

    Serial.println(F("[WEATHER] Locations cleared, reset to default"));
}

// =============================================================================
// LEGACY API (BACKWARD COMPATIBILITY)
// =============================================================================

/**
 * Get primary weather data (index 0)
 * @deprecated Use getWeather(0) instead
 */
const WeatherData& getPrimaryWeather() {
    return getWeather(0);
}

/**
 * Get secondary weather data (index 1)
 * @deprecated Use getWeather(1) instead
 */
const WeatherData& getSecondaryWeather() {
    return getWeather(1);
}

/**
 * Set primary location (index 0)
 * @deprecated Use updateLocation(0, ...) instead
 */
void setPrimaryLocation(const char* name, float lat, float lon) {
    updateLocation(0, name, lat, lon);
}

/**
 * Set secondary location (index 1)
 * @deprecated Use addLocation() or updateLocation(1, ...) instead
 */
void setSecondaryLocation(const char* name, float lat, float lon) {
    if (locationCount < 2) {
        addLocation(name, lat, lon);
    } else {
        updateLocation(1, name, lat, lon);
    }
}

/**
 * Enable/disable secondary location
 * @deprecated Use addLocation/removeLocation instead
 */
void setSecondaryLocationEnabled(bool enabled) {
    if (enabled && locationCount < 2) {
        // Add a default secondary location if none exists
        addLocation("Portland", 45.5152, -122.6784);
    } else if (!enabled && locationCount >= 2) {
        // Remove secondary location
        removeLocation(1);
    }
}

/**
 * Check if secondary location is enabled
 * @deprecated Use getLocationCount() > 1 instead
 */
bool isSecondaryLocationEnabled() {
    return locationCount > 1;
}

/**
 * Get time until next update
 */
unsigned long getNextUpdateIn() {
    if (lastUpdateTime == 0) return 0;

    unsigned long elapsed = millis() - lastUpdateTime;
    if (elapsed >= WEATHER_UPDATE_INTERVAL_MS) return 0;

    return WEATHER_UPDATE_INTERVAL_MS - elapsed;
}

/**
 * Set temperature unit
 */
void setUseCelsius(bool celsius) {
    useCelsius = celsius;
}

/**
 * Get temperature unit setting
 */
bool getUseCelsius() {
    return useCelsius;
}

// =============================================================================
// DISPLAY SETTINGS
// =============================================================================

int getBrightness() { return brightness; }
void setBrightness(int b) { brightness = constrain(b, 0, 100); }

bool getNightModeEnabled() { return nightModeEnabled; }
void setNightModeEnabled(bool enabled) { nightModeEnabled = enabled; }

int getNightModeStartHour() { return nightModeStartHour; }
void setNightModeStartHour(int hour) {
    // -1 = sunset, -2 = sunrise, 0-23 = specific hour
    nightModeStartHour = (hour == -1 || hour == -2) ? hour : constrain(hour, 0, 23);
}

int getNightModeEndHour() { return nightModeEndHour; }
void setNightModeEndHour(int hour) {
    // -1 = sunset, -2 = sunrise, 0-23 = specific hour
    nightModeEndHour = (hour == -1 || hour == -2) ? hour : constrain(hour, 0, 23);
}

int getNightModeBrightness() { return nightModeBrightness; }
void setNightModeBrightness(int b) { nightModeBrightness = constrain(b, 0, 100); }

bool getShowForecast() { return showForecast; }
void setShowForecast(bool show) { showForecast = show; }

int getScreenCycleTime() { return screenCycleTime; }
void setScreenCycleTime(int seconds) { screenCycleTime = constrain(seconds, 5, 60); }

// getThemeMode() and setThemeMode() moved to themes.cpp

bool getGifScreenEnabled() { return gifScreenEnabled; }
void setGifScreenEnabled(bool enabled) { gifScreenEnabled = enabled; }

int getUiNudgeY() { return uiNudgeY; }
void setUiNudgeY(int nudge) { uiNudgeY = constrain(nudge, -20, 20); }

// Custom screen getters/setters
bool getCustomScreenEnabled() { return customScreenEnabled; }
void setCustomScreenEnabled(bool enabled) { customScreenEnabled = enabled; }

const char* getCustomScreenHeader() { return customScreenHeader; }
void setCustomScreenHeader(const char* text) {
    if (text) {
        strncpy(customScreenHeader, text, sizeof(customScreenHeader) - 1);
        customScreenHeader[sizeof(customScreenHeader) - 1] = '\0';
    } else {
        customScreenHeader[0] = '\0';
    }
}

const char* getCustomScreenBody() { return customScreenBody; }
void setCustomScreenBody(const char* text) {
    if (text) {
        strncpy(customScreenBody, text, sizeof(customScreenBody) - 1);
        customScreenBody[sizeof(customScreenBody) - 1] = '\0';
    } else {
        customScreenBody[0] = '\0';
    }
}

const char* getCustomScreenFooter() { return customScreenFooter; }
void setCustomScreenFooter(const char* text) {
    if (text) {
        strncpy(customScreenFooter, text, sizeof(customScreenFooter) - 1);
        customScreenFooter[sizeof(customScreenFooter) - 1] = '\0';
    } else {
        customScreenFooter[0] = '\0';
    }
}

// =============================================================================
// CAROUSEL SYSTEM
// =============================================================================

uint8_t getCarouselCount() {
    return carouselCount;
}

static CarouselItem emptyCarouselItem = {CAROUSEL_LOCATION, 0};

const CarouselItem& getCarouselItem(uint8_t index) {
    if (index >= carouselCount) {
        return emptyCarouselItem;
    }
    return carousel[index];
}

void setCarousel(const CarouselItem* items, uint8_t count) {
    carouselCount = min(count, (uint8_t)MAX_CAROUSEL_ITEMS);
    for (uint8_t i = 0; i < carouselCount; i++) {
        carousel[i] = items[i];
    }
    Serial.printf("[CAROUSEL] Set %d items\n", carouselCount);
}

bool addCarouselItem(uint8_t type, uint8_t dataIndex) {
    if (carouselCount >= MAX_CAROUSEL_ITEMS) {
        Serial.println(F("[CAROUSEL] Cannot add - at max capacity"));
        return false;
    }
    carousel[carouselCount].type = type;
    carousel[carouselCount].dataIndex = dataIndex;
    carouselCount++;
    Serial.printf("[CAROUSEL] Added item type=%d, index=%d\n", type, dataIndex);
    return true;
}

bool removeCarouselItem(uint8_t index) {
    if (index >= carouselCount) {
        return false;
    }
    // Shift items down
    for (uint8_t i = index; i < carouselCount - 1; i++) {
        carousel[i] = carousel[i + 1];
    }
    carouselCount--;
    Serial.printf("[CAROUSEL] Removed item at index %d, now %d items\n", index, carouselCount);
    return true;
}

bool moveCarouselItem(uint8_t fromIndex, uint8_t toIndex) {
    if (fromIndex >= carouselCount || toIndex >= carouselCount || fromIndex == toIndex) {
        return false;
    }
    CarouselItem temp = carousel[fromIndex];
    if (fromIndex < toIndex) {
        // Shift items up
        for (uint8_t i = fromIndex; i < toIndex; i++) {
            carousel[i] = carousel[i + 1];
        }
    } else {
        // Shift items down
        for (uint8_t i = fromIndex; i > toIndex; i--) {
            carousel[i] = carousel[i - 1];
        }
    }
    carousel[toIndex] = temp;
    Serial.printf("[CAROUSEL] Moved item from %d to %d\n", fromIndex, toIndex);
    return true;
}

// =============================================================================
// COUNTDOWN EVENTS
// =============================================================================

uint8_t getCountdownCount() {
    return countdownCount;
}

static CountdownEvent emptyCountdown = {COUNTDOWN_BIRTHDAY, 1, 1, ""};

const CountdownEvent& getCountdown(uint8_t index) {
    if (index >= countdownCount) {
        return emptyCountdown;
    }
    return countdowns[index];
}

int addCountdown(uint8_t type, uint8_t month, uint8_t day, const char* title) {
    if (countdownCount >= MAX_COUNTDOWN_EVENTS) {
        Serial.println(F("[COUNTDOWN] Cannot add - at max capacity"));
        return -1;
    }
    int idx = countdownCount;
    countdowns[idx].type = type;
    countdowns[idx].month = constrain(month, 1, 12);
    countdowns[idx].day = constrain(day, 1, 31);
    if (title) {
        strncpy(countdowns[idx].title, title, sizeof(countdowns[idx].title) - 1);
        countdowns[idx].title[sizeof(countdowns[idx].title) - 1] = '\0';
    } else {
        countdowns[idx].title[0] = '\0';
    }
    countdownCount++;
    Serial.printf("[COUNTDOWN] Added event type=%d, %d/%d, title=%s\n", type, month, day, countdowns[idx].title);
    return idx;
}

bool updateCountdown(uint8_t index, uint8_t type, uint8_t month, uint8_t day, const char* title) {
    if (index >= countdownCount) {
        return false;
    }
    countdowns[index].type = type;
    countdowns[index].month = constrain(month, 1, 12);
    countdowns[index].day = constrain(day, 1, 31);
    if (title) {
        strncpy(countdowns[index].title, title, sizeof(countdowns[index].title) - 1);
        countdowns[index].title[sizeof(countdowns[index].title) - 1] = '\0';
    }
    Serial.printf("[COUNTDOWN] Updated event %d\n", index);
    return true;
}

bool removeCountdown(uint8_t index) {
    if (index >= countdownCount) {
        return false;
    }
    // Shift items down
    for (uint8_t i = index; i < countdownCount - 1; i++) {
        countdowns[i] = countdowns[i + 1];
    }
    countdownCount--;
    // Clear the last slot
    countdowns[countdownCount].type = COUNTDOWN_BIRTHDAY;
    countdowns[countdownCount].month = 1;
    countdowns[countdownCount].day = 1;
    countdowns[countdownCount].title[0] = '\0';
    Serial.printf("[COUNTDOWN] Removed event at index %d, now %d events\n", index, countdownCount);
    return true;
}

// =============================================================================
// CUSTOM SCREENS (Multiple)
// =============================================================================

uint8_t getCustomScreenCount() {
    return customScreenCount;
}

static CustomScreenConfig emptyCustomScreen = {"", "", ""};

const CustomScreenConfig& getCustomScreenConfig(uint8_t index) {
    if (index >= customScreenCount) {
        return emptyCustomScreen;
    }
    return customScreens[index];
}

int addCustomScreenConfig(const char* header, const char* body, const char* footer) {
    if (customScreenCount >= MAX_CUSTOM_SCREENS) {
        Serial.println(F("[CUSTOM] Cannot add - at max capacity"));
        return -1;
    }
    int idx = customScreenCount;
    if (header) {
        strncpy(customScreens[idx].header, header, sizeof(customScreens[idx].header) - 1);
        customScreens[idx].header[sizeof(customScreens[idx].header) - 1] = '\0';
    }
    if (body) {
        strncpy(customScreens[idx].body, body, sizeof(customScreens[idx].body) - 1);
        customScreens[idx].body[sizeof(customScreens[idx].body) - 1] = '\0';
    }
    if (footer) {
        strncpy(customScreens[idx].footer, footer, sizeof(customScreens[idx].footer) - 1);
        customScreens[idx].footer[sizeof(customScreens[idx].footer) - 1] = '\0';
    }
    customScreenCount++;
    Serial.printf("[CUSTOM] Added screen %d\n", idx);
    return idx;
}

bool updateCustomScreenConfig(uint8_t index, const char* header, const char* body, const char* footer) {
    if (index >= customScreenCount) {
        return false;
    }
    if (header) {
        strncpy(customScreens[index].header, header, sizeof(customScreens[index].header) - 1);
        customScreens[index].header[sizeof(customScreens[index].header) - 1] = '\0';
    }
    if (body) {
        strncpy(customScreens[index].body, body, sizeof(customScreens[index].body) - 1);
        customScreens[index].body[sizeof(customScreens[index].body) - 1] = '\0';
    }
    if (footer) {
        strncpy(customScreens[index].footer, footer, sizeof(customScreens[index].footer) - 1);
        customScreens[index].footer[sizeof(customScreens[index].footer) - 1] = '\0';
    }
    Serial.printf("[CUSTOM] Updated screen %d\n", index);
    return true;
}

bool removeCustomScreenConfig(uint8_t index) {
    if (index >= customScreenCount) {
        return false;
    }
    // Shift items down
    for (uint8_t i = index; i < customScreenCount - 1; i++) {
        customScreens[i] = customScreens[i + 1];
    }
    customScreenCount--;
    // Clear the last slot
    customScreens[customScreenCount].header[0] = '\0';
    customScreens[customScreenCount].body[0] = '\0';
    customScreens[customScreenCount].footer[0] = '\0';
    Serial.printf("[CUSTOM] Removed screen at index %d, now %d screens\n", index, customScreenCount);
    return true;
}

/**
 * Check if currently in night mode based on hour
 * Supports special values: -1 = sunset, -2 = sunrise (from weather data)
 */
bool isNightModeActive(int currentHour) {
    if (!nightModeEnabled) return false;

    // Resolve start hour: -1 = sunset, -2 = sunrise, else use configured hour
    int startHour = nightModeStartHour;
    if (startHour == -1) {
        // Use sunset from first location's weather data
        if (locationCount > 0 && weatherData[0].valid) {
            startHour = weatherData[0].sunsetHour;
        } else {
            startHour = 18;  // Default 6 PM if no weather data
        }
    } else if (startHour == -2) {
        // Use sunrise from first location's weather data
        if (locationCount > 0 && weatherData[0].valid) {
            startHour = weatherData[0].sunriseHour;
        } else {
            startHour = 6;  // Default 6 AM if no weather data
        }
    }

    // Resolve end hour: -1 = sunset, -2 = sunrise, else use configured hour
    int endHour = nightModeEndHour;
    if (endHour == -1) {
        if (locationCount > 0 && weatherData[0].valid) {
            endHour = weatherData[0].sunsetHour;
        } else {
            endHour = 18;
        }
    } else if (endHour == -2) {
        if (locationCount > 0 && weatherData[0].valid) {
            endHour = weatherData[0].sunriseHour;
        } else {
            endHour = 6;
        }
    }

    // Handle overnight range (e.g., 22:00 to 7:00)
    if (startHour > endHour) {
        return currentHour >= startHour || currentHour < endHour;
    }
    // Handle same-day range (e.g., 1:00 to 5:00)
    return currentHour >= startHour && currentHour < endHour;
}

// =============================================================================
// CONFIGURATION PERSISTENCE
// =============================================================================

/**
 * Save weather configuration to LittleFS
 */
bool saveWeatherConfig() {
    JsonDocument doc;

    // Save locations as array
    JsonArray locArray = doc["locations"].to<JsonArray>();
    for (int i = 0; i < locationCount; i++) {
        JsonObject loc = locArray.add<JsonObject>();
        loc["name"] = locations[i].name;
        loc["lat"] = locations[i].latitude;
        loc["lon"] = locations[i].longitude;
        loc["enabled"] = locations[i].enabled;
    }

    // Display settings
    doc["useCelsius"] = useCelsius;
    doc["brightness"] = brightness;
    doc["nightModeEnabled"] = nightModeEnabled;
    doc["nightModeStartHour"] = nightModeStartHour;
    doc["nightModeEndHour"] = nightModeEndHour;
    doc["nightModeBrightness"] = nightModeBrightness;
    doc["showForecast"] = showForecast;
    doc["screenCycleTime"] = screenCycleTime;
    // themeMode now saved separately in themes.json
    doc["gifScreenEnabled"] = gifScreenEnabled;
    doc["uiNudgeY"] = uiNudgeY;

    // Custom screen settings (legacy single screen)
    doc["customScreenEnabled"] = customScreenEnabled;
    doc["customScreenHeader"] = customScreenHeader;
    doc["customScreenBody"] = customScreenBody;
    doc["customScreenFooter"] = customScreenFooter;

    // Carousel items
    JsonArray carouselArray = doc["carousel"].to<JsonArray>();
    for (uint8_t i = 0; i < carouselCount; i++) {
        JsonObject item = carouselArray.add<JsonObject>();
        item["type"] = carousel[i].type;
        item["dataIndex"] = carousel[i].dataIndex;
    }

    // Countdown events
    JsonArray countdownArray = doc["countdowns"].to<JsonArray>();
    for (uint8_t i = 0; i < countdownCount; i++) {
        JsonObject event = countdownArray.add<JsonObject>();
        event["type"] = countdowns[i].type;
        event["month"] = countdowns[i].month;
        event["day"] = countdowns[i].day;
        event["title"] = countdowns[i].title;
    }

    // Custom screens (multiple)
    JsonArray customArray = doc["customScreens"].to<JsonArray>();
    for (uint8_t i = 0; i < customScreenCount; i++) {
        JsonObject screen = customArray.add<JsonObject>();
        screen["header"] = customScreens[i].header;
        screen["body"] = customScreens[i].body;
        screen["footer"] = customScreens[i].footer;
    }

    File file = LittleFS.open(WEATHER_CONFIG_FILE, "w");
    if (!file) {
        Serial.println(F("[WEATHER] Failed to open config file for writing"));
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.printf("[WEATHER] Configuration saved (%d locations)\n", locationCount);
    return true;
}

/**
 * Load weather configuration from LittleFS
 * Supports both old format (primary/secondary) and new format (locations array)
 */
bool loadWeatherConfig() {
    if (!LittleFS.exists(WEATHER_CONFIG_FILE)) {
        Serial.println(F("[WEATHER] No config file, using defaults"));
        return false;
    }

    File file = LittleFS.open(WEATHER_CONFIG_FILE, "r");
    if (!file) {
        Serial.println(F("[WEATHER] Failed to open config file"));
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[WEATHER] Config parse error: %s\n", error.c_str());
        return false;
    }

    // Check for new array format first
    if (doc["locations"].is<JsonArray>()) {
        JsonArray locArray = doc["locations"].as<JsonArray>();
        locationCount = 0;

        for (JsonObject loc : locArray) {
            if (locationCount >= MAX_WEATHER_LOCATIONS) break;

            const char* name = loc["name"];
            if (name && strlen(name) > 0) {
                // Normalize name to ASCII for TFT display
                normalizeToAscii(locations[locationCount].name, name, sizeof(locations[locationCount].name));
                locations[locationCount].latitude = loc["lat"] | 0.0f;
                locations[locationCount].longitude = loc["lon"] | 0.0f;
                locations[locationCount].enabled = loc["enabled"] | true;
                locationCount++;
            }
        }

        // Ensure at least one location
        if (locationCount == 0) {
            strcpy(locations[0].name, "Seattle");
            locations[0].latitude = 47.6062;
            locations[0].longitude = -122.3321;
            locations[0].enabled = true;
            locationCount = 1;
        }

        Serial.printf("[WEATHER] Loaded %d location(s) from array format\n", locationCount);
    }
    // Fall back to old format for migration
    else if (doc["primary"].is<JsonObject>()) {
        Serial.println(F("[WEATHER] Migrating from old config format..."));

        locationCount = 0;

        // Load primary location
        JsonObject primary = doc["primary"];
        const char* name = primary["name"];
        if (name && strlen(name) > 0) {
            // Normalize name to ASCII for TFT display
            normalizeToAscii(locations[0].name, name, sizeof(locations[0].name));
            locations[0].latitude = primary["lat"] | 47.6062;
            locations[0].longitude = primary["lon"] | -122.3321;
            locations[0].enabled = primary["enabled"] | true;
            locationCount = 1;
        }

        // Load secondary location if enabled
        JsonObject secondary = doc["secondary"];
        if (secondary) {
            bool secondaryEnabled = secondary["enabled"] | false;
            const char* secName = secondary["name"];
            if (secondaryEnabled && secName && strlen(secName) > 0) {
                // Normalize name to ASCII for TFT display
                normalizeToAscii(locations[1].name, secName, sizeof(locations[1].name));
                locations[1].latitude = secondary["lat"] | 0.0f;
                locations[1].longitude = secondary["lon"] | 0.0f;
                locations[1].enabled = true;
                locationCount = 2;
            }
        }

        // Ensure at least one location
        if (locationCount == 0) {
            strcpy(locations[0].name, "Seattle");
            locations[0].latitude = 47.6062;
            locations[0].longitude = -122.3321;
            locations[0].enabled = true;
            locationCount = 1;
        }

        // Save in new format for next time
        Serial.println(F("[WEATHER] Saving config in new format..."));
    }

    // Load display settings
    useCelsius = doc["useCelsius"] | false;
    brightness = doc["brightness"] | 50;
    nightModeEnabled = doc["nightModeEnabled"] | true;
    nightModeStartHour = doc["nightModeStartHour"] | 22;
    nightModeEndHour = doc["nightModeEndHour"] | 7;
    nightModeBrightness = doc["nightModeBrightness"] | 20;
    showForecast = doc["showForecast"] | true;
    screenCycleTime = doc["screenCycleTime"] | 10;
    // themeMode now loaded from themes.json by themes.cpp
    gifScreenEnabled = doc["gifScreenEnabled"] | false;
    uiNudgeY = doc["uiNudgeY"] | 0;

    // Custom screen settings
    customScreenEnabled = doc["customScreenEnabled"] | false;

    const char* header = doc["customScreenHeader"];
    if (header) {
        strncpy(customScreenHeader, header, sizeof(customScreenHeader) - 1);
        customScreenHeader[sizeof(customScreenHeader) - 1] = '\0';
    }

    const char* body = doc["customScreenBody"];
    if (body) {
        strncpy(customScreenBody, body, sizeof(customScreenBody) - 1);
        customScreenBody[sizeof(customScreenBody) - 1] = '\0';
    }

    const char* footer = doc["customScreenFooter"];
    if (footer) {
        strncpy(customScreenFooter, footer, sizeof(customScreenFooter) - 1);
        customScreenFooter[sizeof(customScreenFooter) - 1] = '\0';
    }

    // Load carousel items
    bool carouselLoaded = false;
    if (doc["carousel"].is<JsonArray>()) {
        JsonArray carouselArray = doc["carousel"].as<JsonArray>();
        carouselCount = 0;
        for (JsonObject item : carouselArray) {
            if (carouselCount >= MAX_CAROUSEL_ITEMS) break;
            carousel[carouselCount].type = item["type"] | 0;
            carousel[carouselCount].dataIndex = item["dataIndex"] | 0;
            carouselCount++;
        }
        if (carouselCount > 0) {
            Serial.printf("[WEATHER] Loaded %d carousel items\n", carouselCount);
            carouselLoaded = true;
        }
    }

    // If no carousel items loaded, initialize from locations
    if (!carouselLoaded) {
        carouselCount = 0;
        for (int i = 0; i < locationCount && carouselCount < MAX_CAROUSEL_ITEMS; i++) {
            carousel[carouselCount].type = CAROUSEL_LOCATION;
            carousel[carouselCount].dataIndex = i;
            carouselCount++;
        }
        Serial.printf("[WEATHER] Initialized default carousel with %d locations\n", carouselCount);
    }

    // Load countdown events
    if (doc["countdowns"].is<JsonArray>()) {
        JsonArray countdownArray = doc["countdowns"].as<JsonArray>();
        countdownCount = 0;
        for (JsonObject event : countdownArray) {
            if (countdownCount >= MAX_COUNTDOWN_EVENTS) break;
            countdowns[countdownCount].type = event["type"] | 0;
            countdowns[countdownCount].month = event["month"] | 1;
            countdowns[countdownCount].day = event["day"] | 1;
            const char* title = event["title"];
            if (title) {
                strncpy(countdowns[countdownCount].title, title, sizeof(countdowns[countdownCount].title) - 1);
                countdowns[countdownCount].title[sizeof(countdowns[countdownCount].title) - 1] = '\0';
            }
            countdownCount++;
        }
        Serial.printf("[WEATHER] Loaded %d countdown events\n", countdownCount);
    }

    // Load custom screens (multiple)
    if (doc["customScreens"].is<JsonArray>()) {
        JsonArray customArray = doc["customScreens"].as<JsonArray>();
        customScreenCount = 0;
        for (JsonObject screen : customArray) {
            if (customScreenCount >= MAX_CUSTOM_SCREENS) break;
            const char* h = screen["header"];
            const char* b = screen["body"];
            const char* f = screen["footer"];
            if (h) {
                strncpy(customScreens[customScreenCount].header, h, sizeof(customScreens[customScreenCount].header) - 1);
                customScreens[customScreenCount].header[sizeof(customScreens[customScreenCount].header) - 1] = '\0';
            }
            if (b) {
                strncpy(customScreens[customScreenCount].body, b, sizeof(customScreens[customScreenCount].body) - 1);
                customScreens[customScreenCount].body[sizeof(customScreens[customScreenCount].body) - 1] = '\0';
            }
            if (f) {
                strncpy(customScreens[customScreenCount].footer, f, sizeof(customScreens[customScreenCount].footer) - 1);
                customScreens[customScreenCount].footer[sizeof(customScreens[customScreenCount].footer) - 1] = '\0';
            }
            customScreenCount++;
        }
        Serial.printf("[WEATHER] Loaded %d custom screens\n", customScreenCount);
    }

    // Log loaded locations
    for (int i = 0; i < locationCount; i++) {
        Serial.printf("[WEATHER] Location %d: %s (%.4f, %.4f)\n",
                      i, locations[i].name, locations[i].latitude, locations[i].longitude);
    }
    Serial.printf("[WEATHER] Temperature unit: %s\n", useCelsius ? "Celsius" : "Fahrenheit");
    Serial.printf("[WEATHER] Brightness: %d%%, Night mode: %s\n", brightness, nightModeEnabled ? "on" : "off");

    return true;
}

// =============================================================================
// JSON OUTPUT
// =============================================================================

/**
 * Convert weather data to JSON for API response
 */
void weatherToJson(const WeatherData& data, JsonDocument& doc) {
    doc["location"] = data.locationName;
    doc["latitude"] = data.latitude;
    doc["longitude"] = data.longitude;
    doc["timezone"] = data.timezone;
    doc["valid"] = data.valid;
    doc["lastUpdate"] = data.lastUpdate;

    if (!data.valid) {
        doc["error"] = data.lastError;
        return;
    }

    // Current weather
    JsonObject current = doc["current"].to<JsonObject>();
    current["temperature"] = data.current.temperature;
    current["windSpeed"] = data.current.windSpeed;
    current["windDirection"] = data.current.windDirection;
    current["weatherCode"] = data.current.weatherCode;
    current["condition"] = conditionToString(data.current.condition);
    current["conditionShort"] = conditionToShortString(data.current.condition);
    current["icon"] = conditionToIcon(data.current.condition, data.current.isDay);
    current["isDay"] = data.current.isDay;

    // Forecast
    JsonArray forecast = doc["forecast"].to<JsonArray>();
    for (int i = 0; i < data.forecastDays; i++) {
        JsonObject day = forecast.add<JsonObject>();
        day["day"] = data.forecast[i].dayName;
        day["tempMax"] = data.forecast[i].tempMax;
        day["tempMin"] = data.forecast[i].tempMin;
        day["precipitation"] = data.forecast[i].precipitationSum;
        day["precipProbability"] = data.forecast[i].precipitationProb;
        day["windSpeedMax"] = data.forecast[i].windSpeedMax;
        day["weatherCode"] = data.forecast[i].weatherCode;
        day["condition"] = conditionToString(data.forecast[i].condition);
        day["icon"] = conditionToIcon(data.forecast[i].condition, true);
    }
}

// =============================================================================
// YOUTUBE STATS
// =============================================================================

/**
 * Fetch YouTube channel stats from API
 */
static bool fetchYouTubeStats() {
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(youtubeData.lastError, "WiFi not connected", sizeof(youtubeData.lastError));
        return false;
    }

    if (strlen(youtubeConfig.apiKey) == 0 || strlen(youtubeConfig.channelHandle) == 0) {
        strncpy(youtubeData.lastError, "API key or channel not configured", sizeof(youtubeData.lastError));
        return false;
    }

    // Check free heap before attempting HTTPS (BearSSL needs ~15-20KB)
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[YOUTUBE] Free heap before HTTPS: %u bytes\n", freeHeap);

    if (freeHeap < 20000) {
        strncpy(youtubeData.lastError, "Insufficient memory for HTTPS", sizeof(youtubeData.lastError));
        Serial.println(F("[YOUTUBE] Not enough memory for HTTPS connection"));
        return false;
    }

    // Build YouTube API URL
    String url = "https://www.googleapis.com/youtube/v3/channels";
    url += "?part=statistics,snippet";
    url += "&forHandle=" + String(youtubeConfig.channelHandle);
    url += "&key=" + String(youtubeConfig.apiKey);

    Serial.printf("[YOUTUBE] Fetching: %s\n", url.c_str());

    // Use WiFiClientSecure for HTTPS
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation (OK for non-sensitive API calls)
    client.setBufferSizes(512, 512);  // Reduce buffer sizes to save memory (default is 16KB each!)

    HTTPClient http;
    http.setTimeout(20000);  // 20 second timeout (HTTPS on ESP8266 is slow)
    http.setReuse(false);    // Don't keep connection open

    // Give the system some time to free up memory
    yield();

    if (!http.begin(client, url)) {
        strncpy(youtubeData.lastError, "HTTP begin failed", sizeof(youtubeData.lastError));
        Serial.println(F("[YOUTUBE] HTTP begin failed"));
        return false;
    }

    Serial.printf("[YOUTUBE] Free heap during request: %u bytes\n", ESP.getFreeHeap());

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        snprintf(youtubeData.lastError, sizeof(youtubeData.lastError), "HTTP error: %d", httpCode);
        Serial.printf("[YOUTUBE] HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[YOUTUBE] Response size: %d bytes\n", payload.length());

    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        snprintf(youtubeData.lastError, sizeof(youtubeData.lastError), "JSON error: %s", error.c_str());
        Serial.printf("[YOUTUBE] JSON parse error: %s\n", error.c_str());
        return false;
    }

    // Check if items exist
    JsonArray items = doc["items"];
    if (items.size() == 0) {
        strncpy(youtubeData.lastError, "Channel not found", sizeof(youtubeData.lastError));
        Serial.println(F("[YOUTUBE] Channel not found"));
        return false;
    }

    JsonObject channel = items[0];

    // Get snippet (channel info)
    JsonObject snippet = channel["snippet"];
    if (snippet) {
        const char* title = snippet["title"];
        if (title) {
            strncpy(youtubeData.channelName, title, sizeof(youtubeData.channelName) - 1);
            youtubeData.channelName[sizeof(youtubeData.channelName) - 1] = '\0';
        }
    }

    // Get channel ID
    const char* channelId = channel["id"];
    if (channelId) {
        strncpy(youtubeData.channelId, channelId, sizeof(youtubeData.channelId) - 1);
        youtubeData.channelId[sizeof(youtubeData.channelId) - 1] = '\0';
    }

    // Get statistics
    JsonObject stats = channel["statistics"];
    if (stats) {
        // YouTube API returns these as strings
        const char* subs = stats["subscriberCount"];
        const char* views = stats["viewCount"];
        const char* videos = stats["videoCount"];

        youtubeData.subscribers = subs ? strtoul(subs, NULL, 10) : 0;
        youtubeData.views = views ? strtoul(views, NULL, 10) : 0;
        youtubeData.videos = videos ? strtoul(videos, NULL, 10) : 0;
    }

    // Copy channel handle
    strncpy(youtubeData.channelHandle, youtubeConfig.channelHandle, sizeof(youtubeData.channelHandle) - 1);
    youtubeData.channelHandle[sizeof(youtubeData.channelHandle) - 1] = '\0';

    // Success!
    youtubeData.valid = true;
    youtubeData.lastUpdate = millis();
    youtubeData.lastError[0] = '\0';

    Serial.printf("[YOUTUBE] Success! %s: %u subs, %u views, %u videos\n",
                  youtubeData.channelName,
                  youtubeData.subscribers,
                  youtubeData.views,
                  youtubeData.videos);

    return true;
}

/**
 * Initialize YouTube system
 */
void initYouTube() {
    if (youtubeInitialized) return;

    Serial.println(F("[YOUTUBE] Initializing..."));

    // Clear data
    memset(&youtubeData, 0, sizeof(YouTubeData));

    // Load saved configuration
    loadYouTubeConfig();

    youtubeInitialized = true;
    Serial.printf("[YOUTUBE] Initialized, enabled=%d\n", youtubeConfig.enabled);
}

/**
 * Update YouTube stats if interval has elapsed
 */
bool updateYouTube() {
    if (!youtubeInitialized) {
        initYouTube();
    }

    // Don't update if not enabled or not configured
    if (!youtubeConfig.enabled || !isYouTubeConfigured()) {
        return false;
    }

    unsigned long now = millis();

    // Check if update is needed (30 minute interval)
    if (youtubeLastUpdateTime > 0 && (now - youtubeLastUpdateTime) < YOUTUBE_UPDATE_INTERVAL_MS) {
        return false;  // Not time yet
    }

    return forceYouTubeUpdate();
}

/**
 * Force immediate YouTube stats update
 */
bool forceYouTubeUpdate() {
    if (!isYouTubeConfigured()) {
        Serial.println(F("[YOUTUBE] Cannot update - not configured"));
        return false;
    }

    Serial.println(F("[YOUTUBE] Updating stats..."));
    bool success = fetchYouTubeStats();
    youtubeLastUpdateTime = millis();
    return success;
}

/**
 * Get YouTube configuration
 */
const YouTubeConfig& getYouTubeConfig() {
    return youtubeConfig;
}

/**
 * Get YouTube stats data
 */
const YouTubeData& getYouTubeData() {
    return youtubeData;
}

/**
 * Set YouTube API key
 */
void setYouTubeApiKey(const char* key) {
    if (key) {
        strncpy(youtubeConfig.apiKey, key, sizeof(youtubeConfig.apiKey) - 1);
        youtubeConfig.apiKey[sizeof(youtubeConfig.apiKey) - 1] = '\0';
    } else {
        youtubeConfig.apiKey[0] = '\0';
    }
    // Invalidate cached data when key changes
    youtubeData.valid = false;
}

/**
 * Set YouTube channel handle
 */
void setYouTubeChannelHandle(const char* handle) {
    if (handle) {
        // Remove @ prefix if present
        const char* h = (handle[0] == '@') ? handle + 1 : handle;
        strncpy(youtubeConfig.channelHandle, h, sizeof(youtubeConfig.channelHandle) - 1);
        youtubeConfig.channelHandle[sizeof(youtubeConfig.channelHandle) - 1] = '\0';
    } else {
        youtubeConfig.channelHandle[0] = '\0';
    }
    // Invalidate cached data when channel changes
    youtubeData.valid = false;
}

/**
 * Enable/disable YouTube screen
 */
void setYouTubeEnabled(bool enabled) {
    youtubeConfig.enabled = enabled;
}

/**
 * Check if YouTube is properly configured
 */
bool isYouTubeConfigured() {
    return strlen(youtubeConfig.apiKey) > 0 && strlen(youtubeConfig.channelHandle) > 0;
}

/**
 * Save YouTube config to LittleFS
 */
bool saveYouTubeConfig() {
    JsonDocument doc;

    doc["apiKey"] = youtubeConfig.apiKey;
    doc["channelHandle"] = youtubeConfig.channelHandle;
    doc["enabled"] = youtubeConfig.enabled;

    File file = LittleFS.open(YOUTUBE_CONFIG_FILE, "w");
    if (!file) {
        Serial.println(F("[YOUTUBE] Failed to open config file for writing"));
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.printf("[YOUTUBE] Configuration saved (enabled=%d, channel=%s)\n",
                  youtubeConfig.enabled, youtubeConfig.channelHandle);
    return true;
}

/**
 * Load YouTube config from LittleFS
 */
bool loadYouTubeConfig() {
    if (!LittleFS.exists(YOUTUBE_CONFIG_FILE)) {
        Serial.println(F("[YOUTUBE] No config file, using defaults"));
        return false;
    }

    File file = LittleFS.open(YOUTUBE_CONFIG_FILE, "r");
    if (!file) {
        Serial.println(F("[YOUTUBE] Failed to open config file"));
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[YOUTUBE] Config parse error: %s\n", error.c_str());
        return false;
    }

    const char* apiKey = doc["apiKey"];
    if (apiKey) {
        strncpy(youtubeConfig.apiKey, apiKey, sizeof(youtubeConfig.apiKey) - 1);
        youtubeConfig.apiKey[sizeof(youtubeConfig.apiKey) - 1] = '\0';
    }

    const char* handle = doc["channelHandle"];
    if (handle) {
        strncpy(youtubeConfig.channelHandle, handle, sizeof(youtubeConfig.channelHandle) - 1);
        youtubeConfig.channelHandle[sizeof(youtubeConfig.channelHandle) - 1] = '\0';
    }

    youtubeConfig.enabled = doc["enabled"] | false;

    Serial.printf("[YOUTUBE] Config loaded (enabled=%d, channel=%s)\n",
                  youtubeConfig.enabled, youtubeConfig.channelHandle);
    return true;
}
