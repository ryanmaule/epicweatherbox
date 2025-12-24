/**
 * EpicWeatherBox Firmware - Theme System
 *
 * Manages display themes with preset and user-customizable color schemes.
 * Supports dark/light mode variants for each theme.
 */

#ifndef THEMES_H
#define THEMES_H

#include <Arduino.h>

// =============================================================================
// THEME CONFIGURATION
// =============================================================================

// Maximum user-customizable themes (stored in LittleFS)
#define MAX_USER_THEMES 1

// Total themes: 2 built-in (Classic, Minecraft) + 1 user = 3
#define TOTAL_THEMES 3

// Theme indices
#define THEME_CLASSIC   0
#define THEME_MINECRAFT 1
#define THEME_CUSTOM    2

// Theme mode constants
#define THEME_MODE_AUTO   0   // Dark at night, light during day
#define THEME_MODE_DARK   1   // Always dark
#define THEME_MODE_LIGHT  2   // Always light

// =============================================================================
// DATA STRUCTURES
// =============================================================================

/**
 * Color palette for one theme variant (dark or light mode)
 * All colors are in RGB565 format (16-bit)
 *
 * Colors are split into two groups:
 * - On-background: For text/icons drawn on the main background
 * - On-card: For text/icons drawn inside card containers
 */
struct ThemeColors {
    uint16_t bg;            // Main background color
    uint16_t card;          // Card/container background
    uint16_t text;          // Primary text on background
    uint16_t textOnCard;    // Primary text on cards
    uint16_t cyan;          // Accent on background (time, headers)
    uint16_t cyanOnCard;    // Accent on cards (precipitation, etc)
    uint16_t orange;        // High temp on background
    uint16_t orangeOnCard;  // High temp on cards
    uint16_t blue;          // Low temp on background
    uint16_t blueOnCard;    // Low temp on cards
    uint16_t gray;          // Secondary text on background
    uint16_t grayOnCard;    // Secondary text on cards
};

/**
 * Complete theme definition with dark and light variants
 */
struct ThemeDefinition {
    const char* name;       // Theme display name
    ThemeColors dark;       // Dark mode colors
    ThemeColors light;      // Light mode colors
};

// =============================================================================
// INITIALIZATION
// =============================================================================

/**
 * Initialize theme system
 * Call once in setup() - loads user theme from LittleFS
 */
void initThemes();

// =============================================================================
// THEME SELECTION
// =============================================================================

/**
 * Get active theme index (0=Classic, 1=Minecraft, 2=Custom)
 */
int getActiveTheme();

/**
 * Set active theme by index
 * @param index Theme index (0-2)
 */
void setActiveTheme(int index);

/**
 * Get theme mode (0=auto, 1=dark, 2=light)
 */
int getThemeMode();

/**
 * Set theme mode
 * @param mode 0=auto, 1=always dark, 2=always light
 */
void setThemeMode(int mode);

/**
 * Check if dark theme should be used based on mode and time
 */
bool shouldUseDarkTheme();

// =============================================================================
// COLOR GETTERS (use active theme and mode)
// =============================================================================

/**
 * Get current background color
 */
uint16_t getThemeBg();

/**
 * Get current card/container background color
 */
uint16_t getThemeCard();

/**
 * Get current primary text color
 */
uint16_t getThemeText();

/**
 * Get current cyan/accent color (time, headers) - for use on background
 */
uint16_t getThemeCyan();

/**
 * Get current cyan/accent color - for use on cards
 */
uint16_t getThemeCyanOnCard();

/**
 * Get current orange/accent color (high temp) - for use on background
 */
uint16_t getThemeOrange();

/**
 * Get current orange/accent color - for use on cards
 */
uint16_t getThemeOrangeOnCard();

/**
 * Get current blue/accent color (low temp) - for use on background
 */
uint16_t getThemeBlue();

/**
 * Get current blue/accent color - for use on cards
 */
uint16_t getThemeBlueOnCard();

/**
 * Get current gray/secondary color - for use on background
 */
uint16_t getThemeGray();

/**
 * Get current gray/secondary color - for use on cards
 */
uint16_t getThemeGrayOnCard();

/**
 * Get text color for use on cards
 */
uint16_t getThemeTextOnCard();

// =============================================================================
// ICON COLORS
// =============================================================================

/**
 * Get cloud icon color
 */
uint16_t getIconCloud();

/**
 * Get dark cloud icon color (for stormy weather)
 */
uint16_t getIconCloudDark();

/**
 * Get snow icon color
 */
uint16_t getIconSnow();

/**
 * Get rain icon color
 */
uint16_t getIconRain();

// =============================================================================
// THEME DATA ACCESS
// =============================================================================

/**
 * Get theme definition by index
 * @param index Theme index (0=Classic, 1=Minecraft, 2=Custom)
 * @return Pointer to theme definition, or nullptr if invalid index
 */
const ThemeDefinition* getThemeDefinition(int index);

/**
 * Get theme name by index
 */
const char* getThemeName(int index);

/**
 * Check if theme is built-in (not editable)
 */
bool isThemeBuiltIn(int index);

// =============================================================================
// USER THEME CUSTOMIZATION
// =============================================================================

/**
 * Update the user custom theme colors
 * @param dark Dark mode colors
 * @param light Light mode colors
 * @return true if saved successfully
 */
bool updateCustomTheme(const ThemeColors& dark, const ThemeColors& light);

/**
 * Reset custom theme to Classic defaults
 * @return true if saved successfully
 */
bool resetCustomTheme();

/**
 * Get custom theme dark colors (for editing UI)
 */
const ThemeColors& getCustomThemeDark();

/**
 * Get custom theme light colors (for editing UI)
 */
const ThemeColors& getCustomThemeLight();

// =============================================================================
// PERSISTENCE
// =============================================================================

/**
 * Save theme settings to LittleFS
 * Saves: activeTheme, themeMode, custom theme colors
 */
bool saveThemeConfig();

/**
 * Load theme settings from LittleFS
 */
bool loadThemeConfig();

#endif // THEMES_H
