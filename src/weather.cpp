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
static int themeMode = 0;  // 0=auto, 1=dark, 2=light
static bool gifScreenEnabled = false;  // Show GIF screen in rotation

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
    url += "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,precipitation_probability_max,weathercode,windspeed_10m_max";
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
    strncpy(locations[idx].name, name, sizeof(locations[idx].name) - 1);
    locations[idx].name[sizeof(locations[idx].name) - 1] = '\0';
    locations[idx].latitude = lat;
    locations[idx].longitude = lon;
    locations[idx].enabled = true;

    // Clear weather data for new location
    memset(&weatherData[idx], 0, sizeof(WeatherData));
    strncpy(weatherData[idx].locationName, name, sizeof(weatherData[idx].locationName) - 1);

    locationCount++;
    Serial.printf("[WEATHER] Added location %d: %s (%.4f, %.4f)\n", idx, name, lat, lon);
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

    strncpy(locations[index].name, name, sizeof(locations[index].name) - 1);
    locations[index].name[sizeof(locations[index].name) - 1] = '\0';
    locations[index].latitude = lat;
    locations[index].longitude = lon;
    locations[index].enabled = true;

    // Update weather data location name and invalidate cache
    strncpy(weatherData[index].locationName, name, sizeof(weatherData[index].locationName) - 1);
    weatherData[index].valid = false;

    Serial.printf("[WEATHER] Updated location %d: %s (%.4f, %.4f)\n", index, name, lat, lon);
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
void setNightModeStartHour(int hour) { nightModeStartHour = constrain(hour, 0, 23); }

int getNightModeEndHour() { return nightModeEndHour; }
void setNightModeEndHour(int hour) { nightModeEndHour = constrain(hour, 0, 23); }

int getNightModeBrightness() { return nightModeBrightness; }
void setNightModeBrightness(int b) { nightModeBrightness = constrain(b, 0, 100); }

bool getShowForecast() { return showForecast; }
void setShowForecast(bool show) { showForecast = show; }

int getScreenCycleTime() { return screenCycleTime; }
void setScreenCycleTime(int seconds) { screenCycleTime = constrain(seconds, 5, 60); }

int getThemeMode() { return themeMode; }
void setThemeMode(int mode) { themeMode = constrain(mode, 0, 2); }

bool getGifScreenEnabled() { return gifScreenEnabled; }
void setGifScreenEnabled(bool enabled) { gifScreenEnabled = enabled; }

/**
 * Check if currently in night mode based on hour
 */
bool isNightModeActive(int currentHour) {
    if (!nightModeEnabled) return false;

    // Handle overnight range (e.g., 22:00 to 7:00)
    if (nightModeStartHour > nightModeEndHour) {
        return currentHour >= nightModeStartHour || currentHour < nightModeEndHour;
    }
    // Handle same-day range (e.g., 1:00 to 5:00)
    return currentHour >= nightModeStartHour && currentHour < nightModeEndHour;
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
    doc["themeMode"] = themeMode;
    doc["gifScreenEnabled"] = gifScreenEnabled;

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
                strncpy(locations[locationCount].name, name, sizeof(locations[locationCount].name) - 1);
                locations[locationCount].name[sizeof(locations[locationCount].name) - 1] = '\0';
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
            strncpy(locations[0].name, name, sizeof(locations[0].name) - 1);
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
                strncpy(locations[1].name, secName, sizeof(locations[1].name) - 1);
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
    themeMode = doc["themeMode"] | 0;
    gifScreenEnabled = doc["gifScreenEnabled"] | false;

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
