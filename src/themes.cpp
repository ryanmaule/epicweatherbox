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

// Classic theme - refined neutral palette with improved contrast
// Dark: Dark bg with light cards - same colors work on both
// Light: Light bg with white cards - same colors work on both
static const ThemeColors CLASSIC_DARK PROGMEM = {
    0x1083,  // bg: True neutral dark (#101018)
    0x18E4,  // card: Slightly elevated gray (#1C1C21)
    0xFFFF,  // text: Pure white (on bg)
    0xFFFF,  // textOnCard: Pure white (on card)
    0x5DDE,  // cyan: Softer sky blue (#5CB8F0)
    0x5DDE,  // cyanOnCard: Same
    0xFC60,  // orange: Warm amber (#FF8C00)
    0xFC60,  // orangeOnCard: Same
    0x4C1F,  // blue: Clean blue (#4C80FF)
    0x4C1F,  // blueOnCard: Same
    0x9CF3,  // gray: Brighter neutral gray (#9C9C9C)
    0x9CF3   // grayOnCard: Same
};

static const ThemeColors CLASSIC_LIGHT PROGMEM = {
    0xF79E,  // bg: Warm off-white (#F0F0F0)
    0xFFFF,  // card: Pure white for clarity
    0x2104,  // text: Dark text (on bg)
    0x2104,  // textOnCard: Dark text (on card)
    0x2B52,  // cyan: Deep teal (#2A6890)
    0x2B52,  // cyanOnCard: Same
    0xD340,  // orange: Rich burnt orange (#D46800)
    0xD340,  // orangeOnCard: Same
    0x2B1D,  // blue: True blue (#2960E8)
    0x2B1D,  // blueOnCard: Same
    0x528A,  // gray: Neutral gray (#525252)
    0x528A   // grayOnCard: Same
};

// Minecraft theme - blocky earthy palette inspired by the game
// Dark mode: Dark bg, dark grass cards - bright accents work on both
// Light mode: Light sand bg, gray stone cards - need different accents!
static const ThemeColors MINECRAFT_DARK PROGMEM = {
    0x0862,  // bg: Night sky (#0C0C14)
    0x1B22,  // card: Dark grass block (#1A6410)
    0xF79D,  // text: Warm white (#F0F0E8)
    0xF79D,  // textOnCard: Same warm white
    0x4F7B,  // cyan: Diamond ore (#4AEDD9)
    0x4F7B,  // cyanOnCard: Same diamond
    0xFC84,  // orange: Lava glow (#FF9020)
    0xFC84,  // orangeOnCard: Same lava
    0x3399,  // blue: Night water (#3070C8)
    0x3399,  // blueOnCard: Same water
    0x8410,  // gray: Stone (#808080)
    0x8410   // grayOnCard: Same stone
};

// Light mode: Sand bg (#EBE8BA) needs dark colors, Stone cards (#808080) need bright colors
static const ThemeColors MINECRAFT_LIGHT PROGMEM = {
    0xEF5D,  // bg: Light sand/dirt (#EBE8BA)
    0x8410,  // card: Cobblestone gray (#808080)
    0x2903,  // text: Dark oak (#2C2018) - for light bg
    0xFFFF,  // textOnCard: White - for gray stone cards
    0x1AC2,  // cyan: Dark grass (#1A5A10) - for light bg
    0x5FE9,  // cyanOnCard: Bright grass (#5BFC48) - for stone cards
    0xCC00,  // orange: Deep gold (#C88000) - for light bg
    0xFE00,  // orangeOnCard: Bright gold (#FFCC00) - for stone cards
    0x1A94,  // blue: Deep water (#1850A0) - for light bg
    0x5DDF,  // blueOnCard: Bright water (#5BBCFF) - for stone cards
    0x4A49,  // gray: Dark stone (#4A4A4A) - for light bg
    0xC618   // grayOnCard: Light gray (#C6C6C6) - for stone cards
};

// Built-in theme definitions
static const ThemeDefinition BUILTIN_THEMES[] PROGMEM = {
    { "Classic", CLASSIC_DARK, CLASSIC_LIGHT },
    { "Minecraft", MINECRAFT_DARK, MINECRAFT_LIGHT }
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
        case THEME_MINECRAFT:
            currentDark = &MINECRAFT_DARK;
            currentLight = &MINECRAFT_LIGHT;
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

uint16_t getThemeCyanOnCard() {
    const ThemeColors& colors = getCurrentColors();
    return colors.cyanOnCard;
}

uint16_t getThemeOrange() {
    const ThemeColors& colors = getCurrentColors();
    return colors.orange;
}

uint16_t getThemeOrangeOnCard() {
    const ThemeColors& colors = getCurrentColors();
    return colors.orangeOnCard;
}

uint16_t getThemeBlue() {
    const ThemeColors& colors = getCurrentColors();
    return colors.blue;
}

uint16_t getThemeBlueOnCard() {
    const ThemeColors& colors = getCurrentColors();
    return colors.blueOnCard;
}

uint16_t getThemeGray() {
    const ThemeColors& colors = getCurrentColors();
    return colors.gray;
}

uint16_t getThemeGrayOnCard() {
    const ThemeColors& colors = getCurrentColors();
    return colors.grayOnCard;
}

uint16_t getThemeTextOnCard() {
    const ThemeColors& colors = getCurrentColors();
    return colors.textOnCard;
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

    // Minecraft
    themeDefs[THEME_MINECRAFT].name = "Minecraft";
    copyThemeColors(const_cast<ThemeColors&>(themeDefs[THEME_MINECRAFT].dark), MINECRAFT_DARK);
    copyThemeColors(const_cast<ThemeColors&>(themeDefs[THEME_MINECRAFT].light), MINECRAFT_LIGHT);

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
        case THEME_MINECRAFT: return "Minecraft";
        case THEME_CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

bool isThemeBuiltIn(int index) {
    return index == THEME_CLASSIC || index == THEME_MINECRAFT;
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
