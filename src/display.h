/**
 * EpicWeatherBox Firmware - Display Driver
 *
 * Handles the ST7789 240x240 TFT display with pixel art weather icons.
 * Implements 3 screen types that cycle through each location.
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "weather.h"

// =============================================================================
// DISPLAY CONFIGURATION
// =============================================================================

// Display dimensions (from config.h, but redefined for clarity)
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 240

// Backlight pin (PWM capable)
#define TFT_BL_PIN 5

// =============================================================================
// SCREEN TYPES
// =============================================================================

enum ScreenType {
    SCREEN_CURRENT_WEATHER = 0,  // Current time + weather (temp, conditions)
    SCREEN_FORECAST_1_3,         // Days 1-3 forecast
    SCREEN_FORECAST_4_6,         // Days 4-6 forecast
    SCREEN_TYPE_COUNT
};

// =============================================================================
// TIMING CONFIGURATION
// =============================================================================

// Time per screen in milliseconds (10 seconds)
#define SCREEN_DISPLAY_TIME_MS 10000

// Animation frame rate
#define DISPLAY_FPS 30
#define DISPLAY_FRAME_TIME_MS (1000 / DISPLAY_FPS)

// =============================================================================
// COLOR PALETTE (Pixel art friendly, 16-bit RGB565)
// =============================================================================

// Background colors
#define COLOR_BG_DAY       0x5DDF  // Light blue sky
#define COLOR_BG_NIGHT     0x1926  // Dark blue night
#define COLOR_BG_DARK      0x0841  // Very dark (for cards)

// Text colors
#define COLOR_TEXT_WHITE   0xFFFF
#define COLOR_TEXT_LIGHT   0xDEFB  // Light gray
#define COLOR_TEXT_YELLOW  0xFFE0  // Yellow (sun)
#define COLOR_TEXT_ORANGE  0xFD20  // Orange (warm)
#define COLOR_TEXT_BLUE    0x5D9F  // Blue (cold)
#define COLOR_TEXT_CYAN    0x07FF  // Cyan (rain)

// Weather condition colors
#define COLOR_SUN          0xFFE0  // Yellow
#define COLOR_MOON         0xC618  // Light gray
#define COLOR_CLOUD        0xDEFB  // White-gray
#define COLOR_RAIN         0x5D9F  // Blue
#define COLOR_SNOW         0xFFFF  // White
#define COLOR_THUNDER      0xFFE0  // Yellow

// Card background
#define COLOR_CARD_BG      0x2104  // Dark gray with hint of blue

// =============================================================================
// ICON SIZE
// =============================================================================

#define ICON_SIZE_LARGE    64  // Main current weather icon
#define ICON_SIZE_SMALL    32  // Forecast icons

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * Initialize the display
 * Call once in setup()
 */
void initDisplay();

/**
 * Update display (call in loop)
 * Handles screen cycling and animations
 */
void updateDisplay();

/**
 * Force redraw of current screen
 */
void refreshDisplay();

/**
 * Set display brightness (0-100)
 */
void setDisplayBrightness(int brightness);

/**
 * Get current brightness
 */
int getDisplayBrightness();

/**
 * Turn display on/off
 */
void setDisplayOn(bool on);

/**
 * Check if display is on
 */
bool isDisplayOn();

// =============================================================================
// SCREEN DRAWING FUNCTIONS
// =============================================================================

/**
 * Draw the current weather screen
 * Shows: Time, Location name, Current temp, Weather icon, Conditions
 */
void drawCurrentWeatherScreen(int locationIndex);

/**
 * Draw the 3-day forecast screen
 * @param locationIndex Which location to show
 * @param startDay 0 for days 1-3, 3 for days 4-6
 */
void drawForecastScreen(int locationIndex, int startDay);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/**
 * Draw a weather icon at position
 * @param condition Weather condition enum
 * @param x, y Position (top-left corner)
 * @param size Icon size (32 or 64)
 * @param isDay Day or night variant
 */
void drawWeatherIcon(WeatherCondition condition, int x, int y, int size, bool isDay);

/**
 * Draw temperature with appropriate color
 * @param temp Temperature value
 * @param x, y Position
 * @param size Font size
 */
void drawTemperature(float temp, int x, int y, int size);

/**
 * Get temperature color based on value
 */
uint16_t getTempColor(float temp);

/**
 * Format temperature string (handles C/F)
 */
String formatTemp(float temp);

/**
 * Draw a rounded rectangle card
 */
void drawCard(int x, int y, int w, int h, uint16_t color);

#endif // DISPLAY_H
