/**
 * EpicWeatherBox Firmware - Weather API Implementation
 *
 * Fetches weather data from Open-Meteo API (free, no API key required)
 * Supports 7-day forecast and dual location weather display.
 */

#include "weather.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <LittleFS.h>

// =============================================================================
// STATIC DATA
// =============================================================================

// Weather data storage
static WeatherData primaryWeather;
static WeatherData secondaryWeather;

// Location configuration
static WeatherLocation primaryLocation = {"Seattle", 47.6062, -122.3321, true};
static WeatherLocation secondaryLocation = {"Portland", 45.5152, -122.6784, false};

// Display settings
static bool useCelsius = false;  // false = Fahrenheit, true = Celsius

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

    // Clear weather data
    memset(&primaryWeather, 0, sizeof(primaryWeather));
    memset(&secondaryWeather, 0, sizeof(secondaryWeather));

    // Load saved configuration
    loadWeatherConfig();

    // Copy location names to weather data
    strncpy(primaryWeather.locationName, primaryLocation.name, sizeof(primaryWeather.locationName));
    strncpy(secondaryWeather.locationName, secondaryLocation.name, sizeof(secondaryWeather.locationName));

    initialized = true;
    Serial.println(F("[WEATHER] Initialized"));
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
    Serial.println(F("[WEATHER] Updating weather data..."));

    bool success = true;

    // Update primary location
    if (primaryLocation.enabled) {
        strncpy(primaryWeather.locationName, primaryLocation.name, sizeof(primaryWeather.locationName));
        if (!fetchWeather(primaryLocation.latitude, primaryLocation.longitude, primaryWeather)) {
            success = false;
        }
    }

    // Update secondary location
    if (secondaryLocation.enabled) {
        strncpy(secondaryWeather.locationName, secondaryLocation.name, sizeof(secondaryWeather.locationName));
        if (!fetchWeather(secondaryLocation.latitude, secondaryLocation.longitude, secondaryWeather)) {
            success = false;
        }
    }

    lastUpdateTime = millis();
    return success;
}

/**
 * Get primary weather data
 */
const WeatherData& getPrimaryWeather() {
    return primaryWeather;
}

/**
 * Get secondary weather data
 */
const WeatherData& getSecondaryWeather() {
    return secondaryWeather;
}

/**
 * Set primary location
 */
void setPrimaryLocation(const char* name, float lat, float lon) {
    strncpy(primaryLocation.name, name, sizeof(primaryLocation.name) - 1);
    primaryLocation.latitude = lat;
    primaryLocation.longitude = lon;
    primaryLocation.enabled = true;

    // Clear cached data to force refresh
    primaryWeather.valid = false;
}

/**
 * Set secondary location
 */
void setSecondaryLocation(const char* name, float lat, float lon) {
    strncpy(secondaryLocation.name, name, sizeof(secondaryLocation.name) - 1);
    secondaryLocation.latitude = lat;
    secondaryLocation.longitude = lon;

    // Clear cached data to force refresh
    secondaryWeather.valid = false;
}

/**
 * Enable/disable secondary location
 */
void setSecondaryLocationEnabled(bool enabled) {
    secondaryLocation.enabled = enabled;
}

/**
 * Check if secondary location is enabled
 */
bool isSecondaryLocationEnabled() {
    return secondaryLocation.enabled;
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
// CONFIGURATION PERSISTENCE
// =============================================================================

/**
 * Save weather configuration to LittleFS
 */
bool saveWeatherConfig() {
    JsonDocument doc;

    // Primary location
    JsonObject primary = doc["primary"].to<JsonObject>();
    primary["name"] = primaryLocation.name;
    primary["lat"] = primaryLocation.latitude;
    primary["lon"] = primaryLocation.longitude;
    primary["enabled"] = primaryLocation.enabled;

    // Secondary location
    JsonObject secondary = doc["secondary"].to<JsonObject>();
    secondary["name"] = secondaryLocation.name;
    secondary["lat"] = secondaryLocation.latitude;
    secondary["lon"] = secondaryLocation.longitude;
    secondary["enabled"] = secondaryLocation.enabled;

    // Display settings
    doc["useCelsius"] = useCelsius;

    File file = LittleFS.open(WEATHER_CONFIG_FILE, "w");
    if (!file) {
        Serial.println(F("[WEATHER] Failed to open config file for writing"));
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.println(F("[WEATHER] Configuration saved"));
    return true;
}

/**
 * Load weather configuration from LittleFS
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

    // Load primary location
    JsonObject primary = doc["primary"];
    if (primary) {
        const char* name = primary["name"];
        if (name) strncpy(primaryLocation.name, name, sizeof(primaryLocation.name) - 1);
        primaryLocation.latitude = primary["lat"] | primaryLocation.latitude;
        primaryLocation.longitude = primary["lon"] | primaryLocation.longitude;
        primaryLocation.enabled = primary["enabled"] | true;
    }

    // Load secondary location
    JsonObject secondary = doc["secondary"];
    if (secondary) {
        const char* name = secondary["name"];
        if (name) strncpy(secondaryLocation.name, name, sizeof(secondaryLocation.name) - 1);
        secondaryLocation.latitude = secondary["lat"] | secondaryLocation.latitude;
        secondaryLocation.longitude = secondary["lon"] | secondaryLocation.longitude;
        secondaryLocation.enabled = secondary["enabled"] | false;
    }

    // Load display settings
    useCelsius = doc["useCelsius"] | false;

    Serial.printf("[WEATHER] Config loaded: %s (%.2f, %.2f) %s\n",
                  primaryLocation.name, primaryLocation.latitude, primaryLocation.longitude,
                  useCelsius ? "Celsius" : "Fahrenheit");

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
