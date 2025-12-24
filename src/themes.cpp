/**
 * EpicWeatherBox Firmware - Theme System Implementation
 *
 * Manages display themes with preset and user-customizable color schemes.
 */

#include "themes.h"
#include "weather.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

// =============================================================================
// CONFIGURATION
// =============================================================================

static const char* THEMES_CONFIG_FILE = "/themes.json";

// =============================================================================
// BUILT-IN THEME DEFINITIONS (stored in PROGMEM)
// =============================================================================

// Classic theme - the original EpicWeatherBox colors
static const ThemeColors CLASSIC_DARK PROGMEM = {
    0x0841,  // bg: Very dark blue-gray
    0x2104,  // card: Dark card background
    0xFFFF,  // text: White
    0x07FF,  // cyan: Bright cyan
    0xFD20,  // orange: Bright orange
    0x5D9F,  // blue: Medium blue
    0x8410   // gray: Medium gray
};

static const ThemeColors CLASSIC_LIGHT PROGMEM = {
    0xC618,  // bg: Medium gray background
    0xEF7D,  // card: Light gray cards
    0x2104,  // text: Dark text
    0x4416,  // cyan: Steel blue (#4682B4)
    0xC280,  // orange: Darker orange
    0x4B0D,  // blue: Darker blue
    0x4208   // gray: Darker gray
};

// Sunset theme - warm orange/purple palette
// Colors designed by TFT Designer for warm, inviting sunset vibes
// All colors are RGB565 format (R in high bits 11-15, B in low bits 0-4)
static const ThemeColors SUNSET_DARK PROGMEM = {
    0x28C5,  // bg: Deep burgundy/purple (#2D1B2E)
    0x3947,  // card: Slightly lighter burgundy (#3E2B3F)
    0xFFFF,  // text: White
    0xFCE8,  // cyan: Warm coral (#FF8C42) - accent for time/headers
    0xFB46,  // orange: Bright orange (#FF6B35) - high temps
    0xFDB6,  // blue: Soft pink (#FFB7B2) - low temps
    0x9EF3   // gray: Muted mauve (#9E8E9F)
};

static const ThemeColors SUNSET_LIGHT PROGMEM = {
    0xFFBC,  // bg: Warm cream (#FFF5E6)
    0xFFFF,  // card: White
    0x3942,  // text: Deep brown (#3D2914)
    0xD325,  // cyan: Burnt orange (#D4652F) - accent for time/headers
    0xD325,  // orange: Burnt orange (#D4652F) - high temps
    0xCA69,  // blue: Coral red (#C94C4C) - low temps
    0x6A88   // gray: Warm brown (#6B5344)
};

// Built-in theme definitions
static const ThemeDefinition BUILTIN_THEMES[] PROGMEM = {
    { "Classic", CLASSIC_DARK, CLASSIC_LIGHT },
    { "Sunset", SUNSET_DARK, SUNSET_LIGHT }
};

// =============================================================================
// RUNTIME STATE
// =============================================================================

// Active theme index (0=Classic, 1=Sunset, 2=Custom)
static int activeTheme = THEME_CLASSIC;

// Theme mode (0=auto, 1=dark, 2=light)
static int themeMode = THEME_MODE_AUTO;

// User custom theme (editable, stored in LittleFS)
static ThemeColors customThemeDark;
static ThemeColors customThemeLight;

// Cached theme pointers for fast access
static const ThemeColors* currentDark = &CLASSIC_DARK;
static const ThemeColors* currentLight = &CLASSIC_LIGHT;

// Icon colors (constant, not theme-dependent currently)
// Dark mode icons
static const uint16_t ICON_CLOUD_DARK_MODE = 0xFFFF;       // White cloud
static const uint16_t ICON_CLOUD_STORM_DARK = 0xC618;      // Gray storm cloud
static const uint16_t ICON_SNOW_DARK_MODE = 0xFFFF;        // White snow
static const uint16_t ICON_RAIN_DARK_MODE = 0xFD00;        // Light blue rain

// Light mode icons
static const uint16_t ICON_CLOUD_LIGHT_MODE = 0x6B4D;      // Dark gray cloud
static const uint16_t ICON_CLOUD_STORM_LIGHT = 0x4208;     // Very dark storm cloud
static const uint16_t ICON_SNOW_LIGHT_MODE = 0x4208;       // Dark gray snow
static const uint16_t ICON_RAIN_LIGHT_MODE = 0x4B0D;       // Dark blue rain

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

// Copy PROGMEM theme colors to RAM
static void copyThemeColors(ThemeColors& dest, const ThemeColors& src) {
    memcpy_P(&dest, &src, sizeof(ThemeColors));
}

// Update cached theme pointers based on activeTheme
static void updateCachedTheme() {
    switch (activeTheme) {
        case THEME_CLASSIC:
            currentDark = &CLASSIC_DARK;
            currentLight = &CLASSIC_LIGHT;
            break;
        case THEME_SUNSET:
            currentDark = &SUNSET_DARK;
            currentLight = &SUNSET_LIGHT;
            break;
        case THEME_CUSTOM:
            currentDark = &customThemeDark;
            currentLight = &customThemeLight;
            break;
        default:
            currentDark = &CLASSIC_DARK;
            currentLight = &CLASSIC_LIGHT;
            break;
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void initThemes() {
    // Initialize custom theme to Classic defaults
    copyThemeColors(customThemeDark, CLASSIC_DARK);
    copyThemeColors(customThemeLight, CLASSIC_LIGHT);

    // Load saved settings from LittleFS
    loadThemeConfig();

    // Update cached theme pointers
    updateCachedTheme();
}

// =============================================================================
// THEME SELECTION
// =============================================================================

int getActiveTheme() {
    return activeTheme;
}

void setActiveTheme(int index) {
    if (index >= 0 && index < TOTAL_THEMES) {
        activeTheme = index;
        updateCachedTheme();
        saveThemeConfig();
    }
}

int getThemeMode() {
    return themeMode;
}

void setThemeMode(int mode) {
    if (mode >= THEME_MODE_AUTO && mode <= THEME_MODE_LIGHT) {
        themeMode = mode;
        saveThemeConfig();
    }
}

bool shouldUseDarkTheme() {
    if (themeMode == THEME_MODE_DARK) return true;
    if (themeMode == THEME_MODE_LIGHT) return false;

    // Auto mode: use isDay from weather API
    const WeatherData& weather = getWeather(0);
    return !weather.current.isDay;
}

// =============================================================================
// COLOR GETTERS
// =============================================================================

// Helper to get current colors based on dark/light mode
static const ThemeColors& getCurrentColors() {
    if (shouldUseDarkTheme()) {
        if (activeTheme == THEME_CUSTOM) {
            return customThemeDark;
        }
        // For built-in themes, we need to copy from PROGMEM
        static ThemeColors tempDark;
        copyThemeColors(tempDark, *currentDark);
        return tempDark;
    } else {
        if (activeTheme == THEME_CUSTOM) {
            return customThemeLight;
        }
        static ThemeColors tempLight;
        copyThemeColors(tempLight, *currentLight);
        return tempLight;
    }
}

uint16_t getThemeBg() {
    const ThemeColors& colors = getCurrentColors();
    return colors.bg;
}

uint16_t getThemeCard() {
    const ThemeColors& colors = getCurrentColors();
    return colors.card;
}

uint16_t getThemeText() {
    const ThemeColors& colors = getCurrentColors();
    return colors.text;
}

uint16_t getThemeCyan() {
    const ThemeColors& colors = getCurrentColors();
    return colors.cyan;
}

uint16_t getThemeOrange() {
    const ThemeColors& colors = getCurrentColors();
    return colors.orange;
}

uint16_t getThemeBlue() {
    const ThemeColors& colors = getCurrentColors();
    return colors.blue;
}

uint16_t getThemeGray() {
    const ThemeColors& colors = getCurrentColors();
    return colors.gray;
}

// =============================================================================
// ICON COLORS
// =============================================================================

uint16_t getIconCloud() {
    return shouldUseDarkTheme() ? ICON_CLOUD_DARK_MODE : ICON_CLOUD_LIGHT_MODE;
}

uint16_t getIconCloudDark() {
    return shouldUseDarkTheme() ? ICON_CLOUD_STORM_DARK : ICON_CLOUD_STORM_LIGHT;
}

uint16_t getIconSnow() {
    return shouldUseDarkTheme() ? ICON_SNOW_DARK_MODE : ICON_SNOW_LIGHT_MODE;
}

uint16_t getIconRain() {
    return shouldUseDarkTheme() ? ICON_RAIN_DARK_MODE : ICON_RAIN_LIGHT_MODE;
}

// =============================================================================
// THEME DATA ACCESS
// =============================================================================

// Static theme definitions for getThemeDefinition
static ThemeDefinition themeDefs[TOTAL_THEMES];
static bool themeDefsInitialized = false;

static void initThemeDefs() {
    if (themeDefsInitialized) return;

    // Classic
    themeDefs[THEME_CLASSIC].name = "Classic";
    copyThemeColors(const_cast<ThemeColors&>(themeDefs[THEME_CLASSIC].dark), CLASSIC_DARK);
    copyThemeColors(const_cast<ThemeColors&>(themeDefs[THEME_CLASSIC].light), CLASSIC_LIGHT);

    // Sunset
    themeDefs[THEME_SUNSET].name = "Sunset";
    copyThemeColors(const_cast<ThemeColors&>(themeDefs[THEME_SUNSET].dark), SUNSET_DARK);
    copyThemeColors(const_cast<ThemeColors&>(themeDefs[THEME_SUNSET].light), SUNSET_LIGHT);

    // Custom
    themeDefs[THEME_CUSTOM].name = "Custom";
    // Colors will be copied dynamically

    themeDefsInitialized = true;
}

const ThemeDefinition* getThemeDefinition(int index) {
    if (index < 0 || index >= TOTAL_THEMES) return nullptr;

    initThemeDefs();

    // For custom theme, update colors from current state
    if (index == THEME_CUSTOM) {
        themeDefs[THEME_CUSTOM].dark = customThemeDark;
        themeDefs[THEME_CUSTOM].light = customThemeLight;
    }

    return &themeDefs[index];
}

const char* getThemeName(int index) {
    switch (index) {
        case THEME_CLASSIC: return "Classic";
        case THEME_SUNSET: return "Sunset";
        case THEME_CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

bool isThemeBuiltIn(int index) {
    return index == THEME_CLASSIC || index == THEME_SUNSET;
}

// =============================================================================
// USER THEME CUSTOMIZATION
// =============================================================================

bool updateCustomTheme(const ThemeColors& dark, const ThemeColors& light) {
    customThemeDark = dark;
    customThemeLight = light;

    // If custom theme is active, update cache
    if (activeTheme == THEME_CUSTOM) {
        updateCachedTheme();
    }

    return saveThemeConfig();
}

bool resetCustomTheme() {
    copyThemeColors(customThemeDark, CLASSIC_DARK);
    copyThemeColors(customThemeLight, CLASSIC_LIGHT);

    if (activeTheme == THEME_CUSTOM) {
        updateCachedTheme();
    }

    return saveThemeConfig();
}

const ThemeColors& getCustomThemeDark() {
    return customThemeDark;
}

const ThemeColors& getCustomThemeLight() {
    return customThemeLight;
}

// =============================================================================
// PERSISTENCE
// =============================================================================

bool saveThemeConfig() {
    JsonDocument doc;

    doc["activeTheme"] = activeTheme;
    doc["themeMode"] = themeMode;

    // Save custom theme colors
    JsonObject custom = doc["custom"].to<JsonObject>();

    JsonObject dark = custom["dark"].to<JsonObject>();
    dark["bg"] = customThemeDark.bg;
    dark["card"] = customThemeDark.card;
    dark["text"] = customThemeDark.text;
    dark["cyan"] = customThemeDark.cyan;
    dark["orange"] = customThemeDark.orange;
    dark["blue"] = customThemeDark.blue;
    dark["gray"] = customThemeDark.gray;

    JsonObject light = custom["light"].to<JsonObject>();
    light["bg"] = customThemeLight.bg;
    light["card"] = customThemeLight.card;
    light["text"] = customThemeLight.text;
    light["cyan"] = customThemeLight.cyan;
    light["orange"] = customThemeLight.orange;
    light["blue"] = customThemeLight.blue;
    light["gray"] = customThemeLight.gray;

    File f = LittleFS.open(THEMES_CONFIG_FILE, "w");
    if (!f) {
        Serial.println(F("[Themes] Failed to open themes.json for writing"));
        return false;
    }

    serializeJson(doc, f);
    f.close();

    Serial.println(F("[Themes] Theme config saved"));
    return true;
}

bool loadThemeConfig() {
    if (!LittleFS.exists(THEMES_CONFIG_FILE)) {
        Serial.println(F("[Themes] No themes.json found, using defaults"));

        // Initialize custom theme to Classic
        copyThemeColors(customThemeDark, CLASSIC_DARK);
        copyThemeColors(customThemeLight, CLASSIC_LIGHT);

        // Save the new config file
        saveThemeConfig();
        return true;
    }

    File f = LittleFS.open(THEMES_CONFIG_FILE, "r");
    if (!f) {
        Serial.println(F("[Themes] Failed to open themes.json"));
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, f);
    f.close();

    if (error) {
        Serial.printf("[Themes] Failed to parse themes.json: %s\n", error.c_str());
        return false;
    }

    // Load settings
    activeTheme = doc["activeTheme"] | THEME_CLASSIC;
    themeMode = doc["themeMode"] | THEME_MODE_AUTO;

    // Validate activeTheme
    if (activeTheme < 0 || activeTheme >= TOTAL_THEMES) {
        activeTheme = THEME_CLASSIC;
    }

    // Load custom theme colors
    if (doc["custom"].is<JsonObject>()) {
        JsonObject custom = doc["custom"];

        if (custom["dark"].is<JsonObject>()) {
            JsonObject dark = custom["dark"];
            customThemeDark.bg = dark["bg"] | CLASSIC_DARK.bg;
            customThemeDark.card = dark["card"] | CLASSIC_DARK.card;
            customThemeDark.text = dark["text"] | CLASSIC_DARK.text;
            customThemeDark.cyan = dark["cyan"] | CLASSIC_DARK.cyan;
            customThemeDark.orange = dark["orange"] | CLASSIC_DARK.orange;
            customThemeDark.blue = dark["blue"] | CLASSIC_DARK.blue;
            customThemeDark.gray = dark["gray"] | CLASSIC_DARK.gray;
        }

        if (custom["light"].is<JsonObject>()) {
            JsonObject light = custom["light"];
            customThemeLight.bg = light["bg"] | CLASSIC_LIGHT.bg;
            customThemeLight.card = light["card"] | CLASSIC_LIGHT.card;
            customThemeLight.text = light["text"] | CLASSIC_LIGHT.text;
            customThemeLight.cyan = light["cyan"] | CLASSIC_LIGHT.cyan;
            customThemeLight.orange = light["orange"] | CLASSIC_LIGHT.orange;
            customThemeLight.blue = light["blue"] | CLASSIC_LIGHT.blue;
            customThemeLight.gray = light["gray"] | CLASSIC_LIGHT.gray;
        }
    } else {
        // No custom colors saved, use Classic defaults
        copyThemeColors(customThemeDark, CLASSIC_DARK);
        copyThemeColors(customThemeLight, CLASSIC_LIGHT);
    }

    Serial.printf("[Themes] Loaded: theme=%d, mode=%d\n", activeTheme, themeMode);
    return true;
}
