/**
 * EpicWeatherBox Firmware
 *
 * Custom firmware for SmallTV-Ultra hardware
 *
 * Features:
 * - WiFi setup via captive portal
 * - Web-based configuration
 * - 7-day weather forecast (vs original 3-day)
 * - Dual location weather support
 * - Time display with NTP sync
 * - OTA firmware updates (ArduinoOTA + Web)
 *
 * CRITICAL: This firmware includes OTA update capability.
 * The USB-C port on SmallTV-Ultra is power-only (no data),
 * so OTA is the ONLY way to update firmware after initial flash.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Enable hardware watchdog
extern "C" {
    #include <user_interface.h>
}

// Local includes
#include "config.h"
#include "ota.h"
#include "weather.h"

// ============================================================================
// TFT DISPLAY - MINIMAL SAFE TEST
// ============================================================================
// This is a minimal, safe TFT test that avoids the full display.cpp
// to prevent crashes. We'll enable features incrementally.
#define ENABLE_TFT_TEST 1  // Set to 0 to disable TFT completely

#if ENABLE_TFT_TEST
#include <TFT_eSPI.h>
#include <NTPClient.h>
// AnimatedGIF disabled - uses too much RAM (~25KB) for ESP8266 with only 80KB total
// The ESP8266 runs out of heap when GIF decoder is loaded alongside web server
// TODO: Consider ESP32 upgrade or simpler static image support
// #include <AnimatedGIF.h>
#define GIF_SUPPORT_DISABLED 1

// FreeSans smooth fonts - already defined by TFT_eSPI when LOAD_GFXFF=1
// Just need extern declarations to reference them
extern const GFXfont FreeSans9pt7b;
extern const GFXfont FreeSans12pt7b;
extern const GFXfont FreeSans18pt7b;
extern const GFXfont FreeSans24pt7b;
extern const GFXfont FreeSansBold9pt7b;
extern const GFXfont FreeSansBold12pt7b;
extern const GFXfont FreeSansBold18pt7b;
extern const GFXfont FreeSansBold24pt7b;

// Font aliases for convenience
#define FSS9  &FreeSans9pt7b
#define FSS12 &FreeSans12pt7b
#define FSS18 &FreeSans18pt7b
#define FSS24 &FreeSans24pt7b
#define FSSB9  &FreeSansBold9pt7b
#define FSSB12 &FreeSansBold12pt7b
#define FSSB18 &FreeSansBold18pt7b
#define FSSB24 &FreeSansBold24pt7b
#define GFXFF 1  // GFX Free Font render mode

static TFT_eSPI tft = TFT_eSPI();
#define TFT_BL_PIN 5  // Backlight PWM pin

// GIF support disabled due to memory constraints
// static AnimatedGIF gif;
// static File gifFile;
static bool gifPlaying = false;
// static unsigned long gifStartTime = 0;
// static int gifX = 0, gifY = 0;

// Forward declare timeClient (defined below)
extern NTPClient timeClient;

// Display state
static unsigned long lastDisplayUpdate = 0;
static int currentDisplayScreen = 0;
static int currentDisplayLocation = 0;

// Emergency safe mode - stops normal operation to allow recovery
static bool emergencySafeMode = false;

// Colors (dark theme)
// Dark theme (night)
#define COLOR_BG_DARK      0x0841  // Very dark blue-gray
#define COLOR_CARD_DARK    0x2104  // Dark card background

// Light theme (day)
#define COLOR_BG_LIGHT     0xC618  // Medium gray background (much darker than white)
#define COLOR_CARD_LIGHT   0xEF7D  // Light gray cards (visible contrast from bg)

// Current active colors (set by getThemeBg/getThemeCard)
#define COLOR_BG       0x0841  // Default: dark (overridden by functions below)
#define COLOR_CARD     0x2104  // Default: dark
#define COLOR_CYAN     0x07FF  // Bright cyan
#define COLOR_WHITE    0xFFFF
#define COLOR_GRAY     0x8410
#define COLOR_ORANGE   0xFD20
#define COLOR_BLUE     0x5D9F

// Text colors for light theme
#define COLOR_TEXT_DARK    0x2104  // Dark text for light backgrounds

// Light theme accent colors (darker for contrast)
#define COLOR_CYAN_LIGHT   0x0555  // Darker cyan for light bg
#define COLOR_ORANGE_LIGHT 0xC280  // Darker orange for light bg
#define COLOR_BLUE_LIGHT   0x4B0D  // Darker blue for light bg
#define COLOR_GRAY_LIGHT   0x4208  // Darker gray for light bg

// Icon colors (pixel art style - BGR565 format)
// BGR565: BBBBB GGGGGG RRRRR (5-6-5 bits)
#define ICON_SUN       0x07FF  // Yellow (cyan in RGB, yellow in BGR display)
#define ICON_CLOUD     0xFFFF  // White cloud (for dark mode)
#define ICON_CLOUD_DARK 0xC618 // Gray cloud (for stormy weather)
#define ICON_RAIN      0xFD00  // Light blue rain drops
#define ICON_SNOW      0xFFFF  // White snow (dark mode)
#define ICON_LIGHTNING 0x07FF  // Yellow lightning bolt

// Light mode icon colors (darker for visibility)
#define ICON_CLOUD_LIGHT     0x6B4D  // Dark gray cloud for light mode
#define ICON_CLOUD_STORM_LIGHT 0x4208 // Very dark cloud for storms in light mode
#define ICON_SNOW_LIGHT      0x4208  // Dark gray snow for light mode
#define ICON_RAIN_LIGHT      0x4B0D  // Dark blue rain for light mode

// ============================================================================
// PROCEDURAL PIXEL-ART WEATHER ICONS
// ============================================================================
// Based on reference: weather-line-icons-pixel-art-set
// Each icon is drawn at specified x,y with given size (default 32x32)

// Helper: Draw a pixel block (scaled pixel)
inline void drawPixel(int x, int y, int px, int py, int scale, uint16_t color) {
    tft.fillRect(x + px * scale, y + py * scale, scale, scale, color);
}

// Draw sun icon (golden circle with rays)
void drawIconSun(int x, int y, int size = 32) {
    int s = size / 16;  // Scale factor (2 for 32px icon)
    uint16_t c = ICON_SUN;

    // Center circle (4x4 at center)
    for (int py = 6; py < 10; py++) {
        for (int px = 6; px < 10; px++) {
            drawPixel(x, y, px, py, s, c);
        }
    }

    // Rays (extending outward)
    // Top ray
    drawPixel(x, y, 7, 2, s, c); drawPixel(x, y, 8, 2, s, c);
    drawPixel(x, y, 7, 3, s, c); drawPixel(x, y, 8, 3, s, c);
    // Bottom ray
    drawPixel(x, y, 7, 12, s, c); drawPixel(x, y, 8, 12, s, c);
    drawPixel(x, y, 7, 13, s, c); drawPixel(x, y, 8, 13, s, c);
    // Left ray
    drawPixel(x, y, 2, 7, s, c); drawPixel(x, y, 2, 8, s, c);
    drawPixel(x, y, 3, 7, s, c); drawPixel(x, y, 3, 8, s, c);
    // Right ray
    drawPixel(x, y, 12, 7, s, c); drawPixel(x, y, 12, 8, s, c);
    drawPixel(x, y, 13, 7, s, c); drawPixel(x, y, 13, 8, s, c);
    // Diagonal rays (smaller)
    drawPixel(x, y, 4, 4, s, c); drawPixel(x, y, 11, 4, s, c);
    drawPixel(x, y, 4, 11, s, c); drawPixel(x, y, 11, 11, s, c);
}

// Draw cloud icon (fluffy white cloud)
void drawIconCloud(int x, int y, int size = 32, uint16_t color = ICON_CLOUD) {
    int s = size / 16;

    // Main cloud body - rounded shape
    // Top bumps
    for (int px = 5; px < 9; px++) drawPixel(x, y, px, 4, s, color);
    for (int px = 9; px < 13; px++) drawPixel(x, y, px, 5, s, color);
    // Middle section
    for (int py = 5; py < 10; py++) {
        for (int px = 3; px < 14; px++) {
            drawPixel(x, y, px, py, s, color);
        }
    }
    // Bottom flat
    for (int px = 2; px < 14; px++) {
        drawPixel(x, y, px, 10, s, color);
        drawPixel(x, y, px, 11, s, color);
    }
}

// Draw rain drops below a position
void drawRainDrops(int x, int y, int size = 32, uint16_t color = ICON_RAIN) {
    int s = size / 16;

    // 3 rain drops in a row
    drawPixel(x, y, 4, 12, s, color); drawPixel(x, y, 4, 13, s, color);
    drawPixel(x, y, 8, 13, s, color); drawPixel(x, y, 8, 14, s, color);
    drawPixel(x, y, 12, 12, s, color); drawPixel(x, y, 12, 13, s, color);
}

// Draw snow flakes
void drawSnowFlakes(int x, int y, int size = 32, uint16_t color = ICON_SNOW) {
    int s = size / 16;

    // Small dots for snow
    drawPixel(x, y, 4, 12, s, color);
    drawPixel(x, y, 7, 14, s, color);
    drawPixel(x, y, 11, 12, s, color);
    drawPixel(x, y, 9, 13, s, color);
    drawPixel(x, y, 5, 14, s, color);
}

// Draw lightning bolt
void drawLightning(int x, int y, int size = 32) {
    int s = size / 16;
    uint16_t c = ICON_LIGHTNING;

    // Zigzag bolt shape
    drawPixel(x, y, 8, 8, s, c);
    drawPixel(x, y, 7, 9, s, c);
    drawPixel(x, y, 8, 9, s, c);
    drawPixel(x, y, 6, 10, s, c);
    drawPixel(x, y, 7, 10, s, c);
    drawPixel(x, y, 8, 10, s, c);
    drawPixel(x, y, 9, 10, s, c);
    drawPixel(x, y, 7, 11, s, c);
    drawPixel(x, y, 8, 11, s, c);
    drawPixel(x, y, 6, 12, s, c);
    drawPixel(x, y, 7, 12, s, c);
    drawPixel(x, y, 5, 13, s, c);
    drawPixel(x, y, 6, 13, s, c);
}

// Draw moon (crescent)
void drawIconMoon(int x, int y, int size = 32) {
    int s = size / 16;
    uint16_t c = ICON_SUN;  // Yellow moon

    // Crescent shape
    for (int py = 4; py < 12; py++) {
        for (int px = 5; px < 11; px++) {
            // Full circle minus inner offset circle
            int dx = px - 8;
            int dy = py - 8;
            int dx2 = px - 6;  // Offset for crescent cutout
            if (dx*dx + dy*dy <= 16 && dx2*dx2 + dy*dy > 9) {
                drawPixel(x, y, px, py, s, c);
            }
        }
    }
}

// Draw fog lines
void drawIconFog(int x, int y, int size = 32) {
    int s = size / 16;
    uint16_t c = COLOR_GRAY;

    // Horizontal wavy lines
    for (int px = 3; px < 13; px++) {
        drawPixel(x, y, px, 6, s, c);
        drawPixel(x, y, px, 9, s, c);
        drawPixel(x, y, px, 12, s, c);
    }
}

// Indicator icons for forecast cards (taller arrows to match font height)

// Up arrow for high temp (10px wide, 16px tall)
void drawArrowUp(int x, int y, uint16_t color) {
    tft.fillTriangle(x+5, y, x, y+6, x+10, y+6, color);  // Arrow head
    tft.fillRect(x+2, y+6, 7, 10, color);                 // Tall stem
}

// Down arrow for low temp (10px wide, 16px tall)
void drawArrowDown(int x, int y, uint16_t color) {
    tft.fillRect(x+2, y, 7, 10, color);                   // Tall stem
    tft.fillTriangle(x+5, y+16, x, y+10, x+10, y+10, color);  // Arrow head
}

// Small raindrop for precipitation
void drawRaindrop(int x, int y, uint16_t color) {
    tft.fillTriangle(x+4, y, x+1, y+5, x+7, y+5, color);
    tft.fillCircle(x+4, y+6, 3, color);
}

// Small percent symbol (8x10 pixels) - drawn as two circles with diagonal line
void drawPercent(int x, int y, uint16_t color) {
    tft.fillCircle(x+2, y+2, 2, color);      // Top-left circle
    tft.fillCircle(x+8, y+8, 2, color);      // Bottom-right circle
    // Diagonal line (draw as small rectangles for thickness)
    for (int i = 0; i < 10; i++) {
        tft.fillRect(x + 8 - i, y + i, 2, 1, color);
    }
}

// Small globe icon (12x12 pixels) for location
void drawGlobe(int x, int y, uint16_t color) {
    tft.drawCircle(x+6, y+6, 5, color);       // Outer circle
    tft.drawFastHLine(x+1, y+6, 10, color);   // Horizontal line (equator)
    tft.drawFastVLine(x+6, y+1, 10, color);   // Vertical line (meridian)
    // Curved lines for globe effect
    tft.drawPixel(x+3, y+3, color);
    tft.drawPixel(x+9, y+3, color);
    tft.drawPixel(x+3, y+9, color);
    tft.drawPixel(x+9, y+9, color);
}

// Small calendar icon (12x12 pixels) for date
void drawCalendar(int x, int y, uint16_t color) {
    // Calendar body
    tft.drawRect(x, y+2, 12, 10, color);
    // Top bar (header)
    tft.fillRect(x, y+2, 12, 3, color);
    // Calendar hooks
    tft.fillRect(x+2, y, 2, 3, color);
    tft.fillRect(x+8, y, 2, 3, color);
    // Date dots (grid)
    tft.fillRect(x+2, y+7, 2, 2, color);
    tft.fillRect(x+5, y+7, 2, 2, color);
    tft.fillRect(x+8, y+7, 2, 2, color);
}

// =============================================================================
// LARGE CUSTOM NUMBERS (scalable, smooth rounded segments)
// =============================================================================

// Draw a single large digit (0-9) or minus sign
// Returns the width of the drawn character
int drawLargeDigit(int x, int y, char digit, int height, uint16_t color) {
    // Proportions based on height
    int w = height * 3 / 5;      // Width is 60% of height
    int t = height / 10;         // Segment thickness
    if (t < 2) t = 2;
    int gap = t / 2;             // Gap between segments
    int midY = y + height / 2 - t / 2;

    // Segment positions
    int top = y;
    int mid = midY;
    int bot = y + height - t;
    int left = x;
    int right = x + w - t;

    // Segment definitions for each digit (top, topL, topR, mid, botL, botR, bot)
    // Using rounded rectangles for smooth look
    switch (digit) {
        case '0':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(left, top + gap, t, height/2 - gap, t/2, color); // topL
            tft.fillRoundRect(right, top + gap, t, height/2 - gap, t/2, color);// topR
            tft.fillRoundRect(left, mid + gap, t, height/2 - gap, t/2, color); // botL
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            tft.fillRoundRect(left + gap, bot, w - 2*gap, t, t/2, color);      // bot
            break;
        case '1':
            // Draw '1' at left of its bounding box (not at 'right' position)
            tft.fillRoundRect(left, top + gap, t, height/2 - gap, t/2, color);
            tft.fillRoundRect(left, mid + gap, t, height/2 - gap, t/2, color);
            return t + gap;  // Narrow width for 1
        case '2':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(right, top + gap, t, height/2 - gap, t/2, color);// topR
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid
            tft.fillRoundRect(left, mid + gap, t, height/2 - gap, t/2, color); // botL
            tft.fillRoundRect(left + gap, bot, w - 2*gap, t, t/2, color);      // bot
            break;
        case '3':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(right, top + gap, t, height/2 - gap, t/2, color);// topR
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            tft.fillRoundRect(left + gap, bot, w - 2*gap, t, t/2, color);      // bot
            break;
        case '4':
            tft.fillRoundRect(left, top + gap, t, height/2 - gap, t/2, color); // topL
            tft.fillRoundRect(right, top + gap, t, height/2 - gap, t/2, color);// topR
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            break;
        case '5':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(left, top + gap, t, height/2 - gap, t/2, color); // topL
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            tft.fillRoundRect(left + gap, bot, w - 2*gap, t, t/2, color);      // bot
            break;
        case '6':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(left, top + gap, t, height/2 - gap, t/2, color); // topL
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid
            tft.fillRoundRect(left, mid + gap, t, height/2 - gap, t/2, color); // botL
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            tft.fillRoundRect(left + gap, bot, w - 2*gap, t, t/2, color);      // bot
            break;
        case '7':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(right, top + gap, t, height/2 - gap, t/2, color);// topR
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            break;
        case '8':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(left, top + gap, t, height/2 - gap, t/2, color); // topL
            tft.fillRoundRect(right, top + gap, t, height/2 - gap, t/2, color);// topR
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid
            tft.fillRoundRect(left, mid + gap, t, height/2 - gap, t/2, color); // botL
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            tft.fillRoundRect(left + gap, bot, w - 2*gap, t, t/2, color);      // bot
            break;
        case '9':
            tft.fillRoundRect(left + gap, top, w - 2*gap, t, t/2, color);      // top
            tft.fillRoundRect(left, top + gap, t, height/2 - gap, t/2, color); // topL
            tft.fillRoundRect(right, top + gap, t, height/2 - gap, t/2, color);// topR
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid
            tft.fillRoundRect(right, mid + gap, t, height/2 - gap, t/2, color);// botR
            tft.fillRoundRect(left + gap, bot, w - 2*gap, t, t/2, color);      // bot
            break;
        case '-':
            tft.fillRoundRect(left + gap, mid, w - 2*gap, t, t/2, color);      // mid only
            break;
        default:
            break;
    }
    return w;
}

// Draw a number string with large custom digits
// Returns total width drawn
int drawLargeNumber(int x, int y, const char* numStr, int height, uint16_t color) {
    int curX = x;
    int spacing = height / 8;  // Space between digits
    if (spacing < 2) spacing = 2;

    for (int i = 0; numStr[i] != '\0'; i++) {
        int charW = drawLargeDigit(curX, y, numStr[i], height, color);
        curX += charW + spacing;
    }
    return curX - x - spacing;  // Total width (minus last spacing)
}

// Calculate width of a number string at given height (without drawing)
int getLargeNumberWidth(const char* numStr, int height) {
    int w = height * 3 / 5;      // Standard digit width
    int t = height / 10;
    if (t < 2) t = 2;
    int gap = t / 2;
    int spacing = height / 8;
    if (spacing < 2) spacing = 2;

    int total = 0;
    for (int i = 0; numStr[i] != '\0'; i++) {
        if (numStr[i] == '1') {
            total += t + gap;  // Matches drawLargeDigit return for '1'
        } else {
            total += w;
        }
        total += spacing;
    }
    return total - spacing;  // Remove last spacing
}

// Forward declarations for theme-aware functions (defined later)
bool shouldUseDarkTheme();
uint16_t getThemeBg();
uint16_t getThemeCard();
uint16_t getThemeCyan();
uint16_t getThemeOrange();
uint16_t getThemeBlue();
uint16_t getThemeGray();
uint16_t getIconCloud();
uint16_t getIconCloudDark();
uint16_t getIconSnow();
uint16_t getIconRain();

// Main icon dispatcher - draws weather icon based on condition
void drawWeatherIcon(int x, int y, WeatherCondition condition, bool isDay = true, int size = 32) {
    // Get theme-aware icon colors
    uint16_t cloudColor = getIconCloud();
    uint16_t cloudDarkColor = getIconCloudDark();
    uint16_t rainColor = getIconRain();
    uint16_t snowColor = getIconSnow();

    switch (condition) {
        case WEATHER_CLEAR:
            if (isDay) {
                drawIconSun(x, y, size);
            } else {
                drawIconMoon(x, y, size);
            }
            break;

        case WEATHER_PARTLY_CLOUDY:
            // Sun/moon peeking behind cloud
            if (isDay) {
                drawIconSun(x - size/8, y - size/8, size * 3/4);
            } else {
                drawIconMoon(x - size/8, y - size/8, size * 3/4);
            }
            drawIconCloud(x + size/8, y + size/4, size * 3/4, cloudColor);
            break;

        case WEATHER_CLOUDY:
            drawIconCloud(x, y, size, cloudColor);
            break;

        case WEATHER_FOG:
            drawIconFog(x, y, size);
            break;

        case WEATHER_DRIZZLE:
        case WEATHER_RAIN:
            drawIconCloud(x, y - size/8, size, cloudColor);
            drawRainDrops(x, y, size, rainColor);
            break;

        case WEATHER_FREEZING_RAIN:
            drawIconCloud(x, y - size/8, size, cloudDarkColor);
            drawRainDrops(x, y, size, rainColor);
            drawSnowFlakes(x + size/4, y, size, snowColor);
            break;

        case WEATHER_SNOW:
            drawIconCloud(x, y - size/8, size, cloudColor);
            drawSnowFlakes(x, y, size, snowColor);
            break;

        case WEATHER_THUNDERSTORM:
            drawIconCloud(x, y - size/8, size, cloudDarkColor);
            drawLightning(x, y, size);
            drawRainDrops(x + size/4, y, size, rainColor);
            break;

        default:  // WEATHER_UNKNOWN
            // Question mark or generic cloud
            drawIconCloud(x, y, size, getThemeGray());
            break;
    }
}

void initTftMinimal() {
    Serial.println(F("[TFT] Init starting..."));

    // Setup backlight pin FIRST
    pinMode(TFT_BL_PIN, OUTPUT);
    analogWriteRange(100);
    analogWriteFreq(1000);
    analogWrite(TFT_BL_PIN, getBrightness());
    Serial.println(F("[TFT] Backlight on"));

    ESP.wdtFeed();
    yield();

    // Initialize TFT
    Serial.println(F("[TFT] Calling tft.init()..."));
    tft.init();
    tft.setRotation(0);
    Serial.println(F("[TFT] tft.init() complete"));

    ESP.wdtFeed();
    yield();

    // Draw boot screen with smooth fonts
    tft.fillScreen(COLOR_BG);
    tft.setTextDatum(MC_DATUM);  // Middle center

    // "Epic" in bold 18pt cyan
    tft.setFreeFont(FSSB18);
    tft.setTextColor(COLOR_CYAN);
    tft.drawString("Epic", 120, 95, GFXFF);

    // "WeatherBox" in bold 18pt white
    tft.setTextColor(COLOR_WHITE);
    tft.drawString("WeatherBox", 120, 130, GFXFF);

    // Version in small gray
    tft.setFreeFont(FSS9);
    tft.setTextColor(COLOR_GRAY);
    tft.drawString("v" FIRMWARE_VERSION, 120, 165, GFXFF);

    // Status text at bottom
    tft.setTextColor(0x4208);  // Dark gray
    tft.drawString("Connecting...", 120, 228, GFXFF);

    Serial.println(F("[TFT] Boot screen displayed"));
    lastDisplayUpdate = millis();
}

// Update boot screen status text at bottom
void updateBootScreenStatus(const char* status) {
    // Clear the status area
    tft.fillRect(0, 200, 240, 40, COLOR_BG_DARK);

    // Draw new status
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FSS9);
    tft.setTextColor(COLOR_GRAY);
    tft.drawString(status, 120, 218, GFXFF);
}

// Show IP address on boot screen (called after WiFi connects)
void showBootScreenIP(const char* ip) {
    // Clear bottom area for IP
    tft.fillRect(0, 200, 240, 40, COLOR_BG_DARK);

    // Draw "Connected" status
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FSS9);
    tft.setTextColor(COLOR_GRAY);
    tft.drawString("Connected", 120, 210, GFXFF);

    // Draw IP address in cyan
    tft.setTextColor(COLOR_CYAN);
    tft.drawString(ip, 120, 228, GFXFF);
}

// Determine if we should use dark theme based on theme setting and time
bool shouldUseDarkTheme() {
    int themeMode = getThemeMode();

    if (themeMode == 1) return true;   // Always dark
    if (themeMode == 2) return false;  // Always light

    // Auto mode: dark at night based on isDay from weather
    const WeatherData& weather = getWeather(0);
    return !weather.current.isDay;
}

// Get current theme background color
uint16_t getThemeBg() {
    return shouldUseDarkTheme() ? COLOR_BG_DARK : COLOR_BG_LIGHT;
}

// Get current theme card color
uint16_t getThemeCard() {
    return shouldUseDarkTheme() ? COLOR_CARD_DARK : COLOR_CARD_LIGHT;
}

// Get current theme text color (for text that needs to contrast with background)
uint16_t getThemeText() {
    return shouldUseDarkTheme() ? COLOR_WHITE : COLOR_TEXT_DARK;
}

// Get theme-aware accent colors
uint16_t getThemeCyan() {
    return shouldUseDarkTheme() ? COLOR_CYAN : COLOR_CYAN_LIGHT;
}

uint16_t getThemeOrange() {
    return shouldUseDarkTheme() ? COLOR_ORANGE : COLOR_ORANGE_LIGHT;
}

uint16_t getThemeBlue() {
    return shouldUseDarkTheme() ? COLOR_BLUE : COLOR_BLUE_LIGHT;
}

uint16_t getThemeGray() {
    return shouldUseDarkTheme() ? COLOR_GRAY : COLOR_GRAY_LIGHT;
}

// Get theme-aware icon colors
uint16_t getIconCloud() {
    return shouldUseDarkTheme() ? ICON_CLOUD : ICON_CLOUD_LIGHT;
}

uint16_t getIconCloudDark() {
    return shouldUseDarkTheme() ? ICON_CLOUD_DARK : ICON_CLOUD_STORM_LIGHT;
}

uint16_t getIconSnow() {
    return shouldUseDarkTheme() ? ICON_SNOW : ICON_SNOW_LIGHT;
}

uint16_t getIconRain() {
    return shouldUseDarkTheme() ? ICON_RAIN : ICON_RAIN_LIGHT;
}

// ============================================================================
// ANIMATED GIF SUPPORT - DISABLED
// ============================================================================
// GIF playback disabled due to ESP8266 memory constraints.
// The AnimatedGIF library requires ~25KB RAM which causes heap exhaustion
// when combined with the web server and weather data.
// TODO: Consider ESP32 upgrade for GIF support

// Stub functions - do nothing but prevent crashes
void stopGif() {
    gifPlaying = false;
}

void updateGifScreen() {
    // No-op - GIF support disabled
}

// Draw GIF screen - shows message that GIF is not supported
void drawGifScreen() {
    tft.fillScreen(getThemeBg());

    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FSS12);
    tft.setTextColor(getThemeGray());
    tft.drawString("GIF Not Supported", 120, 110, GFXFF);

    tft.setFreeFont(FSS9);
    tft.drawString("ESP8266 memory too limited", 120, 140, GFXFF);
    tft.drawString("for animated GIF playback", 120, 160, GFXFF);
}

// Draw emergency safe mode screen
void drawSafeModeScreen() {
    // Yellow/orange warning background
    tft.fillScreen(0xFD20);  // Orange

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_BLACK);

    // Warning icon (smaller triangle)
    tft.fillTriangle(120, 10, 90, 55, 150, 55, TFT_BLACK);
    tft.fillTriangle(120, 16, 96, 51, 144, 51, 0xFD20);
    tft.setFreeFont(FSSB12);
    tft.drawString("!", 120, 28, GFXFF);

    // Title
    tft.setFreeFont(FSSB12);
    tft.drawString("SAFE MODE", 120, 70, GFXFF);

    // Info (combined into one line)
    tft.setFreeFont(FSS9);
    tft.drawString("Device paused - web active", 120, 100, GFXFF);

    // Instructions
    tft.drawString("Visit IP for firmware update:", 120, 130, GFXFF);

    // IP address (larger, more prominent)
    tft.setFreeFont(FSSB12);
    tft.drawString(WiFi.localIP().toString().c_str(), 120, 160, GFXFF);

    // Additional info
    tft.setFreeFont(FSS9);
    tft.drawString("or go to /update", 120, 190, GFXFF);
}

// Draw current weather screen (no sprites - direct to TFT)
void drawCurrentWeather() {
    const WeatherData& weather = getWeather(currentDisplayLocation);
    const WeatherLocation& location = getLocation(currentDisplayLocation);
    bool useCelsius = getUseCelsius();

    // UI nudge - positive moves content up, negative moves down
    int yOff = -getUiNudgeY();  // Negate because we subtract from Y coords

    // Background - use theme color based on day/night
    uint16_t bgColor = getThemeBg();
    uint16_t textColor = getThemeText();
    tft.fillScreen(bgColor);

    // Get time from NTP and apply timezone offset from primary location
    // Note: Uses location 0's timezone, assuming that's where the device is located
    const WeatherData& primaryWeather = getWeather(0);
    long localEpoch = timeClient.getEpochTime() + primaryWeather.utcOffsetSeconds;
    int hours = (localEpoch % 86400L) / 3600;
    int minutes = (localEpoch % 3600) / 60;

    // Calculate date components
    // Simple day calculation from epoch (days since Jan 1, 1970)
    unsigned long days = localEpoch / 86400;
    int year = 1970;
    while (true) {
        int daysInYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (days < (unsigned long)daysInYear) break;
        days -= daysInYear;
        year++;
    }
    // Calculate month and day
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) daysInMonth[1] = 29;
    int month = 0;
    while (days >= (unsigned long)daysInMonth[month]) {
        days -= daysInMonth[month];
        month++;
    }
    int day = days + 1;
    const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // 12-hour format
    int h12 = hours % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (hours < 12) ? "AM" : "PM";

    // Get theme-aware colors
    uint16_t cyanColor = getThemeCyan();
    uint16_t grayColor = getThemeGray();

    // ========== Header: Time (large, centered) with smaller AM/PM ==========
    char timeNumStr[16];
    snprintf(timeNumStr, sizeof(timeNumStr), "%d:%02d", h12, minutes);
    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(FSSB18);
    tft.setTextColor(cyanColor);

    // Calculate widths to center time + AM/PM together
    int16_t timeNumW = tft.textWidth(timeNumStr, GFXFF);
    tft.setFreeFont(FSS9);  // Smaller font for AM/PM
    int16_t ampmW = tft.textWidth(ampm, GFXFF);
    int timeSpacing = 4;
    int totalTimeW = timeNumW + timeSpacing + ampmW;
    int timeStartX = 120 - totalTimeW / 2;

    // Draw time numbers
    tft.setFreeFont(FSSB18);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(timeNumStr, timeStartX, 6 + yOff, GFXFF);

    // Draw AM/PM smaller, vertically centered with time
    tft.setFreeFont(FSS9);
    tft.drawString(ampm, timeStartX + timeNumW + timeSpacing, 12 + yOff, GFXFF);

    // ========== Info row: Globe + Location | Calendar + Date ==========
    int infoY = 42 + yOff;  // More space below time

    // Globe icon + Location name (left side)
    drawGlobe(15, infoY, grayColor);
    tft.setFreeFont(FSS9);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(grayColor);
    tft.drawString(location.name, 32, infoY, GFXFF);

    // Calendar icon + Date (right side)
    char dateStr[12];
    snprintf(dateStr, sizeof(dateStr), "%s %d", monthNames[month], day);
    int16_t dateW = tft.textWidth(dateStr, GFXFF);
    int dateX = 225 - dateW;
    drawCalendar(dateX - 16, infoY, grayColor);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(dateStr, dateX, infoY, GFXFF);

    // ========== Main content: Two columns ==========
    // Left column (0-119): Weather icon + condition text
    // Right column (120-239): Large temperature

    int mainY = 58 + yOff;
    int leftColCenter = 60;   // Center of left column
    int rightColCenter = 180; // Center of right column

    // Weather icon (64x64) centered in left column
    int iconX = leftColCenter - 32;
    drawWeatherIcon(iconX, mainY, weather.current.condition, weather.current.isDay, 64);

    // Condition text under icon - centered in left column
    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(FSS12);
    tft.setTextColor(textColor);
    tft.drawString(conditionToString(weather.current.condition), leftColCenter, mainY + 70, GFXFF);

    // Current temperature - very large custom numbers, centered in right column
    float temp = weather.current.temperature;
    if (!useCelsius) {
        temp = temp * 9.0 / 5.0 + 32.0;
    }

    // Build temperature string
    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%.0f", temp);

    // Use custom large numbers - 70px height for prominent display
    int tempHeight = 70;
    int16_t tempW = getLargeNumberWidth(tempStr, tempHeight);

    // Unit string (just C or F, no degree symbol)
    char unitStr[2];
    snprintf(unitStr, sizeof(unitStr), "%c", useCelsius ? 'C' : 'F');
    tft.setFreeFont(FSSB18);
    int16_t unitW = tft.textWidth(unitStr, GFXFF);

    // Add spacing between number and unit
    int tempSpacing = 8;
    int totalTempW = tempW + tempSpacing + unitW;
    int tempStartX = rightColCenter - totalTempW / 2;
    int tempY = mainY + 15;

    // Draw temperature number using custom large digits
    drawLargeNumber(tempStartX, tempY, tempStr, tempHeight, textColor);

    // Draw unit (smaller, top-aligned)
    tft.setFreeFont(FSSB18);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(textColor);
    tft.drawString(unitStr, tempStartX + tempW + tempSpacing, tempY + 5, GFXFF);

    // ========== Detail bar at bottom with rounded rectangle background ==========
    int barY = 175 + yOff;
    int barH = 36;
    int barMargin = 8;
    uint16_t cardColor = getThemeCard();

    // Draw rounded rectangle background (same style as forecast cards)
    tft.fillRoundRect(barMargin, barY, 240 - 2*barMargin, barH, 4, cardColor);

    // Get theme-aware accent colors for the bar
    uint16_t orangeColor = getThemeOrange();
    uint16_t blueColor = getThemeBlue();

    if (weather.forecastDays > 0) {
        float hi = weather.forecast[0].tempMax;
        float lo = weather.forecast[0].tempMin;
        if (!useCelsius) {
            hi = hi * 9.0 / 5.0 + 32.0;
            lo = lo * 9.0 / 5.0 + 32.0;
        }

        // Three sections within the bar, evenly spaced
        int sectionW = (240 - 2*barMargin) / 3;
        int section1X = barMargin;
        int section2X = barMargin + sectionW;
        int section3X = barMargin + 2*sectionW;
        int contentY = barY + 10;

        tft.setFreeFont(FSSB12);

        // High temp section
        drawArrowUp(section1X + 12, contentY, orangeColor);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(orangeColor);
        char hiStr[8];
        snprintf(hiStr, sizeof(hiStr), "%.0f", hi);
        tft.drawString(hiStr, section1X + 28, contentY - 2, GFXFF);

        // Low temp section
        drawArrowDown(section2X + 12, contentY, blueColor);
        tft.setTextColor(blueColor);
        char loStr[8];
        snprintf(loStr, sizeof(loStr), "%.0f", lo);
        tft.drawString(loStr, section2X + 28, contentY - 2, GFXFF);

        // Precipitation section with % symbol
        int precipVal = (int)weather.forecast[0].precipitationProb;
        uint16_t precipColor = precipVal > 30 ? cyanColor : grayColor;
        drawRaindrop(section3X + 12, contentY - 2, precipColor);
        tft.setTextColor(precipColor);
        char precip[8];
        snprintf(precip, sizeof(precip), "%d", precipVal);
        tft.drawString(precip, section3X + 28, contentY - 2, GFXFF);
        // Draw % after the number
        int16_t numW = tft.textWidth(precip, GFXFF);
        drawPercent(section3X + 30 + numW, contentY, precipColor);
    }

    // Screen dots at bottom (one per screen, not per location)
    int numLocs = getLocationCount();
    bool showForecastFlag = getShowForecast();
    bool customEnabled = getCustomScreenEnabled();
    int screensPerLoc = showForecastFlag ? 3 : 1;
    if (customEnabled) screensPerLoc++;
    int totalScreens = numLocs * screensPerLoc;
    int currentScreen = currentDisplayLocation * screensPerLoc + currentDisplayScreen;

    if (totalScreens > 1) {
        int dotSpacing = 10;
        int startX = 120 - (totalScreens - 1) * dotSpacing / 2;
        int dotY = 230 + yOff;  // Apply nudge to dots too
        if (dotY > 236) dotY = 236;  // Don't go off screen
        for (int i = 0; i < totalScreens; i++) {
            uint16_t dotColor = (i == currentScreen) ? cyanColor : grayColor;
            tft.fillCircle(startX + i * dotSpacing, dotY, 3, dotColor);
        }
    }
}

// Draw 3-day forecast screen
void drawForecast(int startDay) {
    const WeatherData& weather = getWeather(currentDisplayLocation);
    const WeatherLocation& location = getLocation(currentDisplayLocation);
    bool useCelsius = getUseCelsius();

    // UI nudge - positive moves content up, negative moves down
    int yOff = -getUiNudgeY();

    // Background - use theme color based on day/night
    uint16_t bgColor = getThemeBg();
    uint16_t cardColor = getThemeCard();
    uint16_t cyanColor = getThemeCyan();
    uint16_t grayColor = getThemeGray();
    uint16_t orangeColor = getThemeOrange();
    uint16_t blueColor = getThemeBlue();
    tft.fillScreen(bgColor);

    // Header: Time left (blue) with smaller AM/PM, Globe + Location right (grey)
    // Matches main screen style for consistency

    // Time (left aligned, blue)
    const WeatherData& primaryWeather = getWeather(0);
    long localEpoch = timeClient.getEpochTime() + primaryWeather.utcOffsetSeconds;
    int hours = (localEpoch % 86400L) / 3600;
    int minutes = (localEpoch % 3600) / 60;
    int h12 = hours % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (hours < 12) ? "AM" : "PM";

    // Draw time numbers
    char timeNumStr[16];
    snprintf(timeNumStr, sizeof(timeNumStr), "%d:%02d", h12, minutes);
    tft.setFreeFont(FSSB12);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(cyanColor);
    tft.drawString(timeNumStr, 8, 8 + yOff, GFXFF);

    // Draw AM/PM smaller, top-aligned with time
    int16_t timeNumW = tft.textWidth(timeNumStr, GFXFF);
    tft.setFreeFont(FSS9);
    tft.drawString(ampm, 8 + timeNumW + 4, 8 + yOff, GFXFF);

    // Globe icon + Location name (right aligned, grey)
    tft.setFreeFont(FSS9);
    int16_t locW = tft.textWidth(location.name, GFXFF);
    int locX = 232 - locW;
    drawGlobe(locX - 16, 8 + yOff, grayColor);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(grayColor);
    tft.drawString(location.name, locX, 8 + yOff, GFXFF);

    // Draw 3 forecast cards
    int cardW = 75;
    int cardH = 180;
    int gap = 5;
    int cardStartX = (240 - 3 * cardW - 2 * gap) / 2;

    for (int i = 0; i < 3; i++) {
        int dayIdx = startDay + i;
        if (dayIdx >= weather.forecastDays) continue;

        const ForecastDay& day = weather.forecast[dayIdx];
        int x = cardStartX + i * (cardW + gap);
        int y = 35 + yOff;

        // Card background - use theme color
        tft.fillRoundRect(x, y, cardW, cardH, 4, cardColor);

        // Day name - smooth font at top of card
        tft.setTextDatum(TC_DATUM);
        tft.setFreeFont(FSSB9);
        tft.setTextColor(cyanColor);
        tft.drawString(day.dayName, x + cardW/2, y + 10, GFXFF);

        // Weather icon (32x32 centered, pushed down more from day name)
        drawWeatherIcon(x + (cardW - 32)/2, y + 42, day.condition, true, 32);

        // Temperature high/low
        float hi = day.tempMax;
        float lo = day.tempMin;
        if (!useCelsius) {
            hi = hi * 9.0 / 5.0 + 32.0;
            lo = lo * 9.0 / 5.0 + 32.0;
        }

        char hiStr[8], loStr[8];
        snprintf(hiStr, sizeof(hiStr), "%.0f", hi);
        snprintf(loStr, sizeof(loStr), "%.0f", lo);

        // Layout: arrow (12px) + gap (4px) + number (centered in remaining ~45px)
        int arrowX = x + 8;
        int numAreaX = x + 28;  // Start of number area (after arrow + gap)
        int numAreaW = cardW - 28 - 4;  // Width for number (card width minus arrow area minus margin)

        // High temp with up arrow icon
        drawArrowUp(arrowX, y + 95, orangeColor);
        tft.setFreeFont(FSSB12);
        tft.setTextColor(orangeColor);
        tft.setTextDatum(TC_DATUM);
        tft.drawString(hiStr, numAreaX + numAreaW/2, y + 93, GFXFF);

        // Low temp with down arrow icon
        drawArrowDown(arrowX, y + 120, blueColor);
        tft.setTextColor(blueColor);
        tft.drawString(loStr, numAreaX + numAreaW/2, y + 118, GFXFF);

        // Precipitation with raindrop icon and % symbol
        int precipVal = (int)day.precipitationProb;
        uint16_t precipColor = precipVal > 30 ? cyanColor : grayColor;
        drawRaindrop(arrowX + 2, y + 148, precipColor);
        tft.setFreeFont(FSSB12);
        tft.setTextColor(precipColor);
        char precip[8];
        snprintf(precip, sizeof(precip), "%d", precipVal);
        // Draw number centered, then % symbol after
        int16_t numW = tft.textWidth(precip, GFXFF);
        int numX = numAreaX + (numAreaW - numW - 12) / 2;  // Center number + % together
        tft.setTextDatum(TL_DATUM);
        tft.drawString(precip, numX, y + 148, GFXFF);
        drawPercent(numX + numW + 2, y + 150, precipColor);
    }

    // Screen dots at bottom (one per screen, not per location)
    int numLocs = getLocationCount();
    bool showForecastFlag = getShowForecast();
    bool customEnabled = getCustomScreenEnabled();
    int screensPerLoc = showForecastFlag ? 3 : 1;
    if (customEnabled) screensPerLoc++;
    int totalScreens = numLocs * screensPerLoc;
    int currentScreen = currentDisplayLocation * screensPerLoc + currentDisplayScreen;

    if (totalScreens > 1) {
        int dotSpacing = 10;
        int dotStartX = 120 - (totalScreens - 1) * dotSpacing / 2;
        int dotY = 230 + yOff;
        if (dotY > 236) dotY = 236;  // Don't go off screen
        for (int i = 0; i < totalScreens; i++) {
            uint16_t dotColor = (i == currentScreen) ? cyanColor : grayColor;
            tft.fillCircle(dotStartX + i * dotSpacing, dotY, 3, dotColor);
        }
    }
}

// Draw custom text screen
void drawCustomScreen() {
    // Get theme-aware colors
    int yOff = -getUiNudgeY();
    uint16_t bgColor = getThemeBg();
    uint16_t cardColor = getThemeCard();
    uint16_t cyanColor = getThemeCyan();
    uint16_t grayColor = getThemeGray();
    uint16_t textColor = getThemeText();

    tft.fillScreen(bgColor);

    // ========== Header: Time (left) + Custom header text (right) ==========
    // Get time from NTP
    const WeatherData& primaryWeather = getWeather(0);
    long localEpoch = timeClient.getEpochTime() + primaryWeather.utcOffsetSeconds;
    int hours = (localEpoch % 86400L) / 3600;
    int minutes = (localEpoch % 3600) / 60;
    int h12 = hours % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (hours < 12) ? "AM" : "PM";

    // Draw time (left aligned, matches forecast header style)
    char timeNumStr[16];
    snprintf(timeNumStr, sizeof(timeNumStr), "%d:%02d", h12, minutes);
    tft.setFreeFont(FSSB12);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(cyanColor);
    tft.drawString(timeNumStr, 8, 8 + yOff, GFXFF);

    int16_t timeNumW = tft.textWidth(timeNumStr, GFXFF);
    tft.setFreeFont(FSS9);
    tft.drawString(ampm, 8 + timeNumW + 4, 8 + yOff, GFXFF);

    // Custom header text (right aligned, gray) with star icon
    const char* headerText = getCustomScreenHeader();
    if (strlen(headerText) > 0) {
        tft.setFreeFont(FSS9);
        int16_t headerW = tft.textWidth(headerText, GFXFF);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(grayColor);

        // Draw star icon to the left of the header text (same gray color as text)
        int textX = 232 - headerW;  // Right edge of text
        int starX = textX - 12;     // 12px gap left of text for star
        int starY = 14 + yOff;      // Center vertically with text
        int starSize = 4;           // Slightly smaller star
        // Draw a simple 5-pointed star using two overlapping triangles (gray to match text)
        tft.fillTriangle(starX, starY - starSize, starX - 3, starY + 2, starX + 3, starY + 2, grayColor);  // Top triangle
        tft.fillTriangle(starX - starSize, starY - 1, starX + starSize, starY - 1, starX, starY + 3, grayColor);  // Bottom triangle

        tft.drawString(headerText, textX, 8 + yOff, GFXFF);
    }

    // ========== Body: Dynamic text sizing ==========
    const char* bodyText = getCustomScreenBody();
    if (strlen(bodyText) > 0) {
        int bodyW = 220;  // Width (10px margin each side)

        // Determine font size based on text length
        const GFXfont* font;
        int lineHeight;

        int len = strlen(bodyText);
        if (len <= 40) {
            // Short text: large font with generous spacing
            font = FSSB18;
            lineHeight = 38;  // Comfortable spacing for large text
        } else if (len <= 80) {
            // Medium text: medium font with good spacing
            font = FSSB12;
            lineHeight = 30;  // Good spacing for medium text
        } else {
            // Long text: small font with comfortable spacing
            font = FSS9;
            lineHeight = 26;  // Readable spacing for small text
        }

        tft.setFreeFont(font);
        tft.setTextColor(textColor);
        tft.setTextDatum(TL_DATUM);

        // First pass: count lines needed for vertical centering
        char buffer[161];
        strncpy(buffer, bodyText, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        // Count lines
        int lineCount = 0;
        char* lineStart = buffer;
        char* lastSpace = NULL;
        char* p = buffer;

        while (*p) {
            if (*p == ' ') lastSpace = p;

            char saved = *(p + 1);
            *(p + 1) = '\0';
            int16_t lineW = tft.textWidth(lineStart, GFXFF);
            *(p + 1) = saved;

            if (lineW > bodyW || *p == '\n') {
                char* breakPoint = (lastSpace && lastSpace > lineStart) ? lastSpace : p;
                lineCount++;
                lineStart = (*breakPoint == ' ' || *breakPoint == '\n') ? breakPoint + 1 : breakPoint;
                lastSpace = NULL;
            }
            p++;
        }
        if (*lineStart) lineCount++;

        // Calculate vertical centering
        // Body area: y=32 to y=168 (136px available, leaves room for header and footer)
        int bodyAreaTop = 32 + yOff;
        int bodyAreaHeight = 136;
        int totalTextHeight = lineCount * lineHeight;
        int bodyY = bodyAreaTop + (bodyAreaHeight - totalTextHeight) / 2;
        if (bodyY < bodyAreaTop) bodyY = bodyAreaTop;

        // Second pass: actually draw the text
        strncpy(buffer, bodyText, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        int y = bodyY;
        int maxY = 165 + yOff;  // Stop before footer
        lineStart = buffer;
        lastSpace = NULL;
        p = buffer;

        while (*p && y < maxY) {
            if (*p == ' ') {
                lastSpace = p;
            }

            // Check line width
            char saved = *(p + 1);
            *(p + 1) = '\0';
            int16_t lineW = tft.textWidth(lineStart, GFXFF);
            *(p + 1) = saved;

            if (lineW > bodyW || *p == '\n') {
                // Need to wrap
                char* breakPoint = (lastSpace && lastSpace > lineStart) ? lastSpace : p;
                char savedChar = *breakPoint;
                *breakPoint = '\0';

                // Center the line
                int16_t actualW = tft.textWidth(lineStart, GFXFF);
                int centeredX = 120 - actualW / 2;
                tft.drawString(lineStart, centeredX, y, GFXFF);

                *breakPoint = savedChar;
                y += lineHeight;
                lineStart = (*breakPoint == ' ' || *breakPoint == '\n') ? breakPoint + 1 : breakPoint;
                lastSpace = NULL;
            }
            p++;
        }

        // Draw remaining text
        if (*lineStart && y < maxY) {
            int16_t actualW = tft.textWidth(lineStart, GFXFF);
            int centeredX = 120 - actualW / 2;
            tft.drawString(lineStart, centeredX, y, GFXFF);
        }
    }

    // ========== Footer: Rounded rectangle with custom text ==========
    const char* footerText = getCustomScreenFooter();
    int barY = 175 + yOff;
    int barH = 36;
    int barMargin = 8;

    tft.fillRoundRect(barMargin, barY, 240 - 2*barMargin, barH, 4, cardColor);

    if (strlen(footerText) > 0) {
        tft.setFreeFont(FSSB12);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(cyanColor);
        tft.drawString(footerText, 120, barY + 10, GFXFF);
    }

    // ========== Screen dots ==========
    int numLocs = getLocationCount();
    bool showForecastFlag = getShowForecast();
    bool customEnabled = getCustomScreenEnabled();
    int screensPerLoc = showForecastFlag ? 3 : 1;
    if (customEnabled) screensPerLoc++;
    int totalScreens = numLocs * screensPerLoc;
    int currentScreen = currentDisplayLocation * screensPerLoc + currentDisplayScreen;

    if (totalScreens > 1) {
        int dotSpacing = 10;
        int dotStartX = 120 - (totalScreens - 1) * dotSpacing / 2;
        int dotY = 230 + yOff;
        if (dotY > 236) dotY = 236;
        for (int i = 0; i < totalScreens; i++) {
            uint16_t dotColor = (i == currentScreen) ? cyanColor : grayColor;
            tft.fillCircle(dotStartX + i * dotSpacing, dotY, 3, dotColor);
        }
    }
}

// Main display update - call from loop()
void updateTftDisplay() {
    unsigned long now = millis();
    unsigned long cycleMs = (unsigned long)getScreenCycleTime() * 1000;

    // Check if time to change screen
    if (now - lastDisplayUpdate >= cycleMs) {
        lastDisplayUpdate = now;

        // Determine screens to show per location
        bool showForecast = getShowForecast();
        bool showCustom = getCustomScreenEnabled();
        int numLocations = getLocationCount();

        // Screens per location: current, (forecast1, forecast2), (custom)
        int screensPerLoc = 1;  // Always have current weather
        if (showForecast) screensPerLoc += 2;  // Add forecast screens
        if (showCustom) screensPerLoc += 1;    // Add custom screen at end

        // Draw appropriate screen FIRST (before incrementing)
        ESP.wdtFeed();
        yield();

        // Screen order depends on which screens are enabled
        // With forecast: 0=current, 1=forecast1-3, 2=forecast4-6, 3=custom
        // Without forecast: 0=current, 1=custom
        if (showForecast) {
            switch (currentDisplayScreen) {
                case 0:
                    drawCurrentWeather();
                    break;
                case 1:
                    drawForecast(0);  // Days 1-3
                    break;
                case 2:
                    drawForecast(3);  // Days 4-6
                    break;
                case 3:
                    if (showCustom) drawCustomScreen();
                    break;
            }
        } else {
            // No forecast screens
            if (currentDisplayScreen == 0) {
                drawCurrentWeather();
            } else if (currentDisplayScreen == 1 && showCustom) {
                drawCustomScreen();
            }
        }

        Serial.printf("[TFT] Screen %d, Location %d\n",
                      currentDisplayScreen, currentDisplayLocation);

        // Advance screen for NEXT cycle
        currentDisplayScreen++;
        if (currentDisplayScreen >= screensPerLoc) {
            currentDisplayScreen = 0;
            currentDisplayLocation = (currentDisplayLocation + 1) % numLocations;
        }
    }
}
#endif

// Note: FIRMWARE_VERSION and DEVICE_NAME are defined in config.h

// Objects
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// Watchdog timeout (8 seconds is max for ESP8266)
#define WDT_TIMEOUT_SECONDS 8

// Forward declarations
void setupWiFi();
void setupWebServer();
void setupWatchdog();
void handleRoot();
void handleAdmin();
void handleDisplayPreview();
void handleNotFound();
void feedWatchdog();

void setup() {
    // Initialize serial first for debugging
    Serial.begin(115200);
    delay(100);  // Let serial stabilize

    Serial.println();
    Serial.println(F("================================================"));
    Serial.printf_P(PSTR("%s Custom Firmware v%s\n"), DEVICE_NAME, FIRMWARE_VERSION);
    Serial.println(F("================================================"));
    Serial.println(F("[BOOT] Starting initialization..."));

    // Initialize hardware watchdog
    setupWatchdog();
    Serial.println(F("[BOOT] Watchdog timer enabled"));

    // Initialize LittleFS (SPIFFS is deprecated)
    Serial.print(F("[BOOT] Mounting LittleFS... "));
    if (!LittleFS.begin()) {
        Serial.println(F("FAILED!"));
        // Continue anyway - we can still work without filesystem
    } else {
        Serial.println(F("OK"));
        FSInfo fs_info;
        LittleFS.info(fs_info);
        Serial.printf_P(PSTR("[BOOT] LittleFS: %u/%u bytes used\n"),
                       fs_info.usedBytes, fs_info.totalBytes);
    }

    feedWatchdog();

    // Initialize display - MINIMAL SAFE TEST
#if ENABLE_TFT_TEST
    Serial.println(F("[BOOT] Initializing TFT (minimal test)..."));
    initTftMinimal();
#else
    Serial.println(F("[BOOT] Display: DISABLED"));
#endif

    // Initialize WiFi (this can take a while)
    Serial.println(F("[BOOT] Starting WiFi..."));
    setupWiFi();

    feedWatchdog();

    // Only proceed if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        // Initialize OTA - CRITICAL for future updates!
        Serial.println(F("[BOOT] Initializing OTA..."));
        initArduinoOTA(OTA_HOSTNAME);

        // Initialize NTP
        Serial.println(F("[BOOT] Starting NTP client..."));
        timeClient.begin();
        timeClient.update();  // Force initial update

        // Initialize web server (includes OTA web interface)
        Serial.println(F("[BOOT] Starting web server..."));
        setupWebServer();

        // Initialize web OTA (add /update endpoint)
        initWebOTA(&server);

        // Initialize weather system
        Serial.println(F("[BOOT] Initializing weather..."));
        initWeather();

        // Fetch initial weather data
        Serial.println(F("[BOOT] Fetching initial weather..."));
        forceWeatherUpdate();
    }

    feedWatchdog();

    // Print startup summary
    Serial.println(F("================================================"));
    Serial.println(F("[BOOT] Initialization complete!"));
    Serial.printf_P(PSTR("[BOOT] Free heap: %u bytes\n"), ESP.getFreeHeap());
    Serial.printf_P(PSTR("[BOOT] Chip ID: %08X\n"), ESP.getChipId());
    Serial.printf_P(PSTR("[BOOT] Flash size: %u bytes\n"), ESP.getFlashChipRealSize());

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf_P(PSTR("[BOOT] IP Address: %s\n"), WiFi.localIP().toString().c_str());
        Serial.printf_P(PSTR("[BOOT] Web UI: http://%s/\n"), WiFi.localIP().toString().c_str());
        Serial.printf_P(PSTR("[BOOT] OTA Update: http://%s/update\n"), WiFi.localIP().toString().c_str());

        // Show IP address on boot screen and give user time to see it
#if ENABLE_TFT_TEST
        showBootScreenIP(WiFi.localIP().toString().c_str());
        delay(3000);  // Give user 3 seconds to see the IP address
#endif
    }
    Serial.println(F("================================================"));
}

void loop() {
    // Feed watchdog at start of loop
    feedWatchdog();

    // Handle OTA updates - CRITICAL, must be called frequently
    handleOTA();

    // Skip other processing during OTA to ensure stability
    if (isOTAInProgress()) {
        return;
    }

    // Handle web server - ALWAYS process, even in safe mode
    server.handleClient();

    // In emergency safe mode, skip all other processing
    // This keeps web server responsive for firmware upload
    if (emergencySafeMode) {
        yield();
        return;
    }

    // Update NTP (library handles update interval internally)
    timeClient.update();

    // Update weather data (checks interval internally)
    updateWeather();

    // Update TFT display
#if ENABLE_TFT_TEST
    updateTftDisplay();
#endif

    // Small yield to prevent watchdog issues
    yield();
}

/**
 * Setup hardware watchdog timer
 * This will reset the ESP if it hangs for too long
 */
void setupWatchdog() {
    // Disable software watchdog (we're using hardware)
    ESP.wdtDisable();

    // Enable hardware watchdog with 8-second timeout
    ESP.wdtEnable(WDTO_8S);
}

/**
 * Feed the watchdog timer
 * Call this regularly in loop() to prevent reset
 */
void feedWatchdog() {
    ESP.wdtFeed();
    yield();  // Also yield to system tasks
}

/**
 * Setup WiFi using WiFiManager
 * Creates AP for configuration if no saved credentials
 */
void setupWiFi() {
    WiFiManager wifiManager;

    // Reset saved settings for testing (uncomment if needed)
    // wifiManager.resetSettings();

    // Set AP name
    String apName = String(DEVICE_NAME);

    // Set timeout for config portal (5 minutes)
    wifiManager.setConfigPortalTimeout(300);

    // Set minimum signal quality to show networks (%)
    wifiManager.setMinimumSignalQuality(15);

    // Set static IP if desired (optional)
    // wifiManager.setSTAStaticIPConfig(IPAddress(192,168,1,99),
    //                                   IPAddress(192,168,1,1),
    //                                   IPAddress(255,255,255,0));

    // Custom parameters could be added here for API keys, locations, etc.
    // WiFiManagerParameter custom_api_key("apikey", "Weather API Key", "", 40);
    // wifiManager.addParameter(&custom_api_key);

    Serial.println(F("[WIFI] Starting WiFi Manager..."));
    Serial.printf_P(PSTR("[WIFI] AP Name: %s\n"), apName.c_str());

    // Feed watchdog before potentially long operation
    feedWatchdog();

    // Try to connect, or start config portal
    // autoConnect will block until connected or timeout
    if (!wifiManager.autoConnect(apName.c_str())) {
        Serial.println(F("[WIFI] Failed to connect and hit timeout"));
        Serial.println(F("[WIFI] Restarting in 3 seconds..."));
        delay(3000);
        ESP.restart();
    }

    Serial.println(F("[WIFI] Connected successfully!"));
    Serial.printf_P(PSTR("[WIFI] SSID: %s\n"), WiFi.SSID().c_str());
    Serial.printf_P(PSTR("[WIFI] IP: %s\n"), WiFi.localIP().toString().c_str());
    Serial.printf_P(PSTR("[WIFI] RSSI: %d dBm\n"), WiFi.RSSI());
    Serial.printf_P(PSTR("[WIFI] MAC: %s\n"), WiFi.macAddress().c_str());

    // Update boot screen with IP address
#if ENABLE_TFT_TEST
    updateBootScreenStatus(WiFi.localIP().toString().c_str());
#endif
}

/**
 * Setup web server routes
 */
void setupWebServer() {
    // Main page
    server.on("/", HTTP_GET, handleRoot);

    // API endpoints
    server.on("/api/status", HTTP_GET, []() {
        JsonDocument doc;
        doc["version"] = FIRMWARE_VERSION;
        doc["device"] = DEVICE_NAME;
        doc["heap"] = ESP.getFreeHeap();
        doc["uptime"] = millis() / 1000;
        doc["ip"] = WiFi.localIP().toString();
        doc["rssi"] = WiFi.RSSI();
        doc["ssid"] = WiFi.SSID();
        doc["mac"] = WiFi.macAddress();
        doc["chipId"] = String(ESP.getChipId(), HEX);
        doc["flashSize"] = ESP.getFlashChipRealSize();
        doc["sketchSize"] = ESP.getSketchSize();
        doc["freeSketchSpace"] = ESP.getFreeSketchSpace();

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    server.on("/api/time", HTTP_GET, []() {
        JsonDocument doc;
        doc["epoch"] = timeClient.getEpochTime();
        doc["formatted"] = timeClient.getFormattedTime();
        doc["day"] = timeClient.getDay();

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Weather API endpoint - returns all locations
    server.on("/api/weather", HTTP_GET, []() {
        JsonDocument doc;

        // Return all locations as array
        JsonArray locations = doc["locations"].to<JsonArray>();
        for (int i = 0; i < getLocationCount(); i++) {
            JsonObject loc = locations.add<JsonObject>();
            JsonDocument locDoc;
            weatherToJson(getWeather(i), locDoc);
            loc.set(locDoc.as<JsonObject>());
        }

        // Add metadata
        doc["locationCount"] = getLocationCount();
        doc["maxLocations"] = MAX_WEATHER_LOCATIONS;
        doc["nextUpdateIn"] = getNextUpdateIn() / 1000;  // seconds
        doc["updateInterval"] = WEATHER_UPDATE_INTERVAL_MS / 1000;  // seconds

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Force weather refresh endpoint
    server.on("/api/weather/refresh", HTTP_GET, []() {
        bool success = forceWeatherUpdate();

        JsonDocument doc;
        doc["success"] = success;
        doc["message"] = success ? "Weather updated" : "Update failed";

        String response;
        serializeJson(doc, response);
        server.send(success ? 200 : 500, "application/json", response);
    });

    // Config API - GET returns location settings, POST saves them
    server.on("/api/config", HTTP_GET, []() {
        JsonDocument doc;

        // Return all locations as array
        JsonArray locArray = doc["locations"].to<JsonArray>();
        for (int i = 0; i < getLocationCount(); i++) {
            const WeatherLocation& loc = getLocation(i);
            JsonObject l = locArray.add<JsonObject>();
            l["name"] = loc.name;
            l["lat"] = loc.latitude;
            l["lon"] = loc.longitude;
            l["enabled"] = loc.enabled;
        }

        // Metadata
        doc["locationCount"] = getLocationCount();
        doc["maxLocations"] = MAX_WEATHER_LOCATIONS;

        // Display settings
        doc["useCelsius"] = getUseCelsius();
        doc["brightness"] = getBrightness();
        doc["nightModeEnabled"] = getNightModeEnabled();
        doc["nightModeStartHour"] = getNightModeStartHour();
        doc["nightModeEndHour"] = getNightModeEndHour();
        doc["nightModeBrightness"] = getNightModeBrightness();
        doc["showForecast"] = getShowForecast();
        doc["screenCycleTime"] = getScreenCycleTime();
        doc["themeMode"] = getThemeMode();
        doc["uiNudgeY"] = getUiNudgeY();

        // Custom screen settings
        doc["customScreenEnabled"] = getCustomScreenEnabled();
        doc["customScreenHeader"] = getCustomScreenHeader();
        doc["customScreenBody"] = getCustomScreenBody();
        doc["customScreenFooter"] = getCustomScreenFooter();

        // GIF support disabled
        doc["gifSupported"] = false;

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    server.on("/api/config", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"No data\"}");
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }

        // Check if using new array format
        if (doc["locations"].is<JsonArray>()) {
            JsonArray locArray = doc["locations"].as<JsonArray>();

            // Validate
            if (locArray.size() == 0) {
                server.send(400, "application/json", "{\"success\":false,\"message\":\"At least one location required\"}");
                return;
            }
            if (locArray.size() > MAX_WEATHER_LOCATIONS) {
                server.send(400, "application/json", "{\"success\":false,\"message\":\"Max 5 locations\"}");
                return;
            }

            // Clear existing locations and add new ones
            clearLocations();

            bool first = true;
            for (JsonObject loc : locArray) {
                const char* name = loc["name"];
                float lat = loc["lat"] | 0.0f;
                float lon = loc["lon"] | 0.0f;

                if (name && strlen(name) > 0 && (lat != 0 || lon != 0)) {
                    if (first) {
                        // Update first location (can't remove it)
                        updateLocation(0, name, lat, lon);
                        first = false;
                    } else {
                        addLocation(name, lat, lon);
                    }
                }
            }
        }
        // Fall back to old format for backward compatibility
        else if (doc["primary"].is<JsonObject>()) {
            JsonObject primary = doc["primary"];
            const char* name = primary["name"];
            float lat = primary["lat"] | 0.0f;
            float lon = primary["lon"] | 0.0f;
            if (name && (lat != 0 || lon != 0)) {
                updateLocation(0, name, lat, lon);
            }

            // Handle secondary if present
            if (doc["secondary"].is<JsonObject>()) {
                JsonObject secondary = doc["secondary"];
                bool enabled = secondary["enabled"] | false;
                if (enabled) {
                    const char* secName = secondary["name"];
                    float secLat = secondary["lat"] | 0.0f;
                    float secLon = secondary["lon"] | 0.0f;
                    if (secName && (secLat != 0 || secLon != 0)) {
                        if (getLocationCount() < 2) {
                            addLocation(secName, secLat, secLon);
                        } else {
                            updateLocation(1, secName, secLat, secLon);
                        }
                    }
                } else if (getLocationCount() > 1) {
                    // Remove secondary if disabled
                    removeLocation(1);
                }
            }
        }

        // Update display settings
        if (doc["useCelsius"].is<bool>()) {
            setUseCelsius(doc["useCelsius"] | false);
        }
        if (doc["brightness"].is<int>()) {
            setBrightness(doc["brightness"] | 50);
        }
        if (doc["nightModeEnabled"].is<bool>()) {
            setNightModeEnabled(doc["nightModeEnabled"] | true);
        }
        if (doc["nightModeStartHour"].is<int>()) {
            setNightModeStartHour(doc["nightModeStartHour"] | 22);
        }
        if (doc["nightModeEndHour"].is<int>()) {
            setNightModeEndHour(doc["nightModeEndHour"] | 7);
        }
        if (doc["nightModeBrightness"].is<int>()) {
            setNightModeBrightness(doc["nightModeBrightness"] | 20);
        }
        if (doc["showForecast"].is<bool>()) {
            setShowForecast(doc["showForecast"] | true);
        }
        if (doc["screenCycleTime"].is<int>()) {
            setScreenCycleTime(doc["screenCycleTime"] | 10);
        }
        if (doc["themeMode"].is<int>()) {
            setThemeMode(doc["themeMode"] | 0);
        }
        if (doc["uiNudgeY"].is<int>()) {
            setUiNudgeY(doc["uiNudgeY"] | 0);
        }

        // Custom screen settings
        if (doc["customScreenEnabled"].is<bool>()) {
            setCustomScreenEnabled(doc["customScreenEnabled"] | false);
        }
        if (doc["customScreenHeader"].is<const char*>()) {
            setCustomScreenHeader(doc["customScreenHeader"]);
        }
        if (doc["customScreenBody"].is<const char*>()) {
            setCustomScreenBody(doc["customScreenBody"]);
        }
        if (doc["customScreenFooter"].is<const char*>()) {
            setCustomScreenFooter(doc["customScreenFooter"]);
        }

        // Save and refresh weather
        saveWeatherConfig();
        forceWeatherUpdate();

        server.send(200, "application/json", "{\"success\":true,\"message\":\"Config saved\"}");
    });

    // Admin page - minimal location config
    server.on("/admin", HTTP_GET, handleAdmin);

    // Display preview page - simulates the TFT display in browser
    server.on("/preview", HTTP_GET, handleDisplayPreview);

    // Version endpoint (original firmware compatibility)
    server.on("/v.json", HTTP_GET, []() {
        JsonDocument doc;
        doc["v"] = FIRMWARE_VERSION;

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // Geocoding API - search for city by name
    server.on("/api/geocode", HTTP_GET, []() {
        if (!server.hasArg("q")) {
            server.send(400, "application/json", "{\"error\":\"Missing query parameter 'q'\"}");
            return;
        }

        String query = server.arg("q");
        if (query.length() < 2) {
            server.send(400, "application/json", "{\"error\":\"Query too short\"}");
            return;
        }

        // URL encode the query
        String encodedQuery = "";
        for (size_t i = 0; i < query.length(); i++) {
            char c = query.charAt(i);
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encodedQuery += c;
            } else if (c == ' ') {
                encodedQuery += "%20";
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
                encodedQuery += buf;
            }
        }

        // Build Open-Meteo geocoding URL
        // Request 20 results from API to include international cities (Canada, etc.)
        String url = "http://geocoding-api.open-meteo.com/v1/search?name=";
        url += encodedQuery;
        url += "&count=20&language=en&format=json";

        Serial.printf("[GEOCODE] Searching: %s\n", query.c_str());

        WiFiClient client;
        HTTPClient http;
        http.setTimeout(10000);

        if (!http.begin(client, url)) {
            server.send(500, "application/json", "{\"error\":\"HTTP begin failed\"}");
            return;
        }

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            http.end();
            server.send(500, "application/json", "{\"error\":\"Geocoding request failed\"}");
            return;
        }

        String payload = http.getString();
        http.end();

        // Parse and simplify the response
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            server.send(500, "application/json", "{\"error\":\"JSON parse failed\"}");
            return;
        }

        // Build simplified response
        JsonDocument response;
        JsonArray results = response["results"].to<JsonArray>();

        JsonArray apiResults = doc["results"];
        if (apiResults) {
            for (JsonObject r : apiResults) {
                JsonObject item = results.add<JsonObject>();
                item["name"] = r["name"];
                item["lat"] = r["latitude"];
                item["lon"] = r["longitude"];

                // Build display string: "City, State, Country"
                String display = r["name"].as<String>();
                if (r.containsKey("admin1") && !r["admin1"].isNull()) {
                    display += ", ";
                    display += r["admin1"].as<String>();
                }
                if (r.containsKey("country") && !r["country"].isNull()) {
                    display += ", ";
                    display += r["country"].as<String>();
                }
                item["display"] = display;
            }
        }

        response["count"] = results.size();

        String responseStr;
        serializeJson(response, responseStr);
        server.send(200, "application/json", responseStr);
    });

    // GIF support removed - AnimatedGIF library uses too much RAM for ESP8266
    // Static endpoints for compatibility with admin UI (return disabled status)
    server.on("/api/gif/status", HTTP_GET, []() {
        server.send(200, "application/json",
            "{\"gifSupported\":false,\"message\":\"GIF support disabled - ESP8266 memory constraints\"}");
    });

    // Emergency safe mode - stops normal operation for recovery
    server.on("/api/safemode", HTTP_GET, []() {
        emergencySafeMode = true;
        drawSafeModeScreen();
        server.send(200, "application/json",
            "{\"success\":true,\"message\":\"Safe mode activated. Device paused.\"}");
    });

    // Exit safe mode
    server.on("/api/safemode/exit", HTTP_GET, []() {
        emergencySafeMode = false;
        lastDisplayUpdate = 0;  // Force immediate screen redraw
        server.send(200, "application/json",
            "{\"success\":true,\"message\":\"Safe mode deactivated. Resuming normal operation.\"}");
    });

    // Safe mode status
    server.on("/api/safemode/status", HTTP_GET, []() {
        String response = emergencySafeMode ?
            "{\"safeMode\":true,\"message\":\"Device is in safe mode\"}" :
            "{\"safeMode\":false,\"message\":\"Normal operation\"}";
        server.send(200, "application/json", response);
    });

    // Reboot endpoint
    server.on("/reboot", HTTP_GET, []() {
        server.send(200, "text/html",
            F("<!DOCTYPE html><html><head>"
              "<meta name='viewport' content='width=device-width, initial-scale=1'>"
              "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
              "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
              ".box{text-align:center;}</style></head><body><div class='box'>"
              "<h1>Rebooting...</h1><p>Please wait, redirecting in 10 seconds.</p>"
              "<script>setTimeout(function(){location.href='/';},10000);</script>"
              "</div></body></html>"));
        delay(500);
        ESP.restart();
    });

    // Reset WiFi settings endpoint
    server.on("/reset", HTTP_GET, []() {
        server.send(200, "text/html",
            F("<!DOCTYPE html><html><head>"
              "<meta name='viewport' content='width=device-width, initial-scale=1'>"
              "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
              "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
              ".box{text-align:center;}</style></head><body><div class='box'>"
              "<h1>Factory Reset</h1><p>WiFi settings cleared. Rebooting...</p>"
              "<p>Connect to EpicWeatherBox AP to reconfigure.</p>"
              "</div></body></html>"));
        delay(500);

        // Clear WiFi credentials
        WiFi.disconnect(true);
        delay(1000);
        ESP.restart();
    });

    // Not found handler
    server.onNotFound(handleNotFound);

    // Start server
    server.begin();
    Serial.println(F("[WEB] HTTP server started on port 80"));
}

/**
 * Handle root page
 */
void handleRoot() {
    // Using F() macro to store strings in flash
    String html = F("<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>EpicWeatherBox</title>"
        "<style>"
        "*{box-sizing:border-box;}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "margin:0;padding:20px;background:linear-gradient(135deg,#1a1a2e 0%,#16213e 100%);"
        "color:#eee;min-height:100vh;}"
        ".container{max-width:600px;margin:0 auto;}"
        "h1{color:#00d4ff;text-align:center;margin-bottom:30px;}"
        ".card{background:rgba(255,255,255,0.05);border-radius:12px;padding:20px;"
        "margin-bottom:20px;border:1px solid rgba(255,255,255,0.1);}"
        ".card h3{margin-top:0;color:#00d4ff;}"
        ".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}"
        ".info-item{padding:10px;background:rgba(0,0,0,0.2);border-radius:8px;}"
        ".info-label{font-size:12px;color:#888;margin-bottom:4px;}"
        ".info-value{font-size:16px;font-weight:500;}"
        "a{color:#00d4ff;text-decoration:none;}"
        "a:hover{text-decoration:underline;}"
        ".links{display:flex;flex-wrap:wrap;gap:10px;}"
        ".link-btn{display:inline-block;padding:12px 20px;background:#00d4ff;color:#1a1a2e;"
        "border-radius:8px;font-weight:600;transition:all 0.3s;}"
        ".link-btn:hover{background:#00a8cc;text-decoration:none;transform:translateY(-2px);}"
        ".link-btn.warning{background:#ffc107;}"
        ".link-btn.danger{background:#dc3545;color:#fff;}"
        "</style></head><body>"
        "<div class='container'>"
        "<h1>EpicWeatherBox</h1>");

    html += F("<div class='card'><h3>Device Status</h3><div class='info-grid'>");
    html += F("<div class='info-item'><div class='info-label'>Firmware</div><div class='info-value'>");
    html += FIRMWARE_VERSION;
    html += F("</div></div>");
    html += F("<div class='info-item'><div class='info-label'>IP Address</div><div class='info-value'>");
    html += WiFi.localIP().toString();
    html += F("</div></div>");
    html += F("<div class='info-item'><div class='info-label'>Free Memory</div><div class='info-value'>");
    html += String(ESP.getFreeHeap());
    html += F(" bytes</div></div>");
    html += F("<div class='info-item'><div class='info-label'>Uptime</div><div class='info-value'>");

    // Format uptime nicely
    unsigned long uptime = millis() / 1000;
    if (uptime < 60) {
        html += String(uptime) + "s";
    } else if (uptime < 3600) {
        html += String(uptime / 60) + "m " + String(uptime % 60) + "s";
    } else {
        html += String(uptime / 3600) + "h " + String((uptime % 3600) / 60) + "m";
    }

    html += F("</div></div>");
    html += F("<div class='info-item'><div class='info-label'>WiFi Signal</div><div class='info-value'>");
    html += String(WiFi.RSSI());
    html += F(" dBm</div></div>");
    html += F("<div class='info-item'><div class='info-label'>Time</div><div class='info-value'>");
    html += timeClient.getFormattedTime();
    html += F("</div></div></div></div>");

    html += F("<div class='card'><h3>Quick Links</h3><div class='links'>"
        "<a href='/admin' class='link-btn'>Admin Panel</a>"
        "<a href='/preview' class='link-btn'>Display Preview</a>"
        "<a href='/update' class='link-btn'>Firmware Update</a>"
        "<a href='/reboot' class='link-btn warning'>Reboot</a>"
        "<a href='/reset' class='link-btn danger'>Factory Reset</a>"
        "</div></div>");

    html += F("<div class='card'><h3>API Endpoints</h3>"
        "<p><a href='/api/weather'>/api/weather</a> - Weather data</p>"
        "<p><a href='/api/config'>/api/config</a> - Location config</p>"
        "<p><a href='/api/status'>/api/status</a> - Device status</p>"
        "</div>");

    html += F("<div class='card'><h3>Project Links</h3>"
        "<p><a href='https://github.com/ryanmaule/epicweatherbox' target='_blank'>GitHub Repository</a> - Source code &amp; documentation</p>"
        "<p><a href='https://github.com/ryanmaule/epicweatherbox/releases' target='_blank'>Releases</a> - Download latest firmware</p>"
        "<p><a href='https://github.com/ryanmaule/epicweatherbox/issues' target='_blank'>Issues</a> - Report bugs or request features</p>"
        "<p style='margin-top:15px;padding-top:10px;border-top:1px solid #333'>"
        "<strong>Current Version:</strong> " FIRMWARE_VERSION "<br>"
        "<span style='color:#888;font-size:0.9em'>Check <a href='https://github.com/ryanmaule/epicweatherbox/releases/latest' target='_blank'>latest release</a> for updates. "
        "Use <a href='/update'>/update</a> to upload new firmware.</span></p>"
        "</div>");

    html += F("</div></body></html>");

    server.send(200, "text/html", html);
}

/**
 * Handle admin page - multi-location config with city search and add/remove
 */
void handleAdmin() {
    bool celsius = getUseCelsius();
    const char* unit = celsius ? "C" : "F";
    int locCount = getLocationCount();

    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Admin</title><style>"
        "*{box-sizing:border-box}body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px}"
        ".c{max-width:500px;margin:0 auto}h1{color:#00d4ff;text-align:center}"
        ".card{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin-bottom:15px}"
        ".card h3{color:#00d4ff;margin-top:0}label{display:block;margin:10px 0 5px;color:#aaa;font-size:0.9em}"
        "input,select{width:100%;padding:10px;border:1px solid #333;border-radius:6px;background:#2a2a4e;color:#eee}"
        ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
        "button{background:#00d4ff;color:#1a1a2e;border:none;padding:12px 20px;border-radius:6px;cursor:pointer;margin-top:15px}"
        "button:hover{background:#00a8cc}.status{padding:10px;border-radius:6px;margin-top:10px;display:none}"
        ".ok{background:rgba(0,200,100,0.2);color:#0c6}.err{background:rgba(200,50,50,0.2);color:#f66}"
        "a{color:#00d4ff}.hint{font-size:0.8em;color:#666;margin-top:5px}"
        ".search-box{display:flex;gap:10px}.search-box input{flex:1}.search-box button{margin-top:0}"
        ".results{max-height:200px;overflow-y:auto;margin-top:10px}"
        ".result{padding:10px;background:rgba(0,0,0,0.3);margin:5px 0;border-radius:6px;cursor:pointer}"
        ".result:hover{background:rgba(0,212,255,0.2)}.result small{color:#888}"
        ".loc-item{background:rgba(0,0,0,0.2);border-radius:8px;padding:12px;margin:8px 0;position:relative}"
        ".loc-item .name{font-weight:bold;font-size:1.1em}.loc-item .coords{color:#888;font-size:0.85em}"
        ".loc-item .remove{position:absolute;right:10px;top:50%;transform:translateY(-50%);background:#dc3545;"
        "color:#fff;border:none;padding:6px 12px;border-radius:4px;cursor:pointer;font-size:0.85em}"
        ".loc-item .remove:hover{background:#c82333}"
        ".add-btn{background:#28a745;width:100%}.add-btn:hover{background:#218838}"
        ".pending{border:2px dashed #00d4ff;background:rgba(0,212,255,0.1)}"
        ".toggle-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0}"
        ".toggle-row span{color:#eee;font-size:0.95em}"
        ".toggle-row input[type='checkbox']{width:auto;margin:0}"
        "</style></head><body><div class='c'><h1>EpicWeatherBox</h1>");

    // Current weather for all locations
    html += F("<div class='card'><h3>Current Weather</h3>");
    for (int i = 0; i < locCount; i++) {
        const WeatherData& w = getWeather(i);
        if (i > 0) html += F("<br>");
        if (w.valid) {
            html += String(w.locationName) + F(": ");
            html += String((int)w.current.temperature) + "°" + unit + ", ";
            html += conditionToString(w.current.condition);
        } else {
            html += String(w.locationName) + F(": No data");
        }
    }
    if (locCount == 0) {
        html += F("No locations configured");
    }
    html += F("</div>");

    // City search section
    html += F("<div class='card'><h3>Find Location</h3>"
        "<div class='search-box'><input type='text' id='search' placeholder='Type city name (e.g. Aurora)'>"
        "<button type='button' onclick='searchCity()'>Search</button></div>"
        "<div id='results' class='results'></div>"
        "<p class='hint'>Search by city name only. Click a result to select it.</p>"
        "<div id='pending' class='loc-item pending' style='display:none'>"
        "<div class='name' id='pendingName'>-</div>"
        "<div class='coords' id='pendingCoords'>-</div>"
        "<button class='add-btn' onclick='addPending()'>+ Add This Location</button></div></div>");

    // Configured locations
    html += F("<div class='card'><h3>Locations</h3><div id='locations'>");

    // Render current locations from server side
    for (int i = 0; i < locCount; i++) {
        const WeatherLocation& loc = getLocation(i);
        html += F("<div class='loc-item' data-idx='");
        html += String(i);
        html += F("'><div class='name'>");
        html += String(i + 1) + ". " + String(loc.name);
        html += F("</div><div class='coords'>");
        html += String(loc.latitude, 4) + ", " + String(loc.longitude, 4);
        html += F("</div>");
        // Only show remove button if more than 1 location
        if (locCount > 1) {
            html += F("<button class='remove' onclick='removeLoc(");
            html += String(i);
            html += F(")'>Remove</button>");
        }
        html += F("</div>");
    }

    html += F("</div><p class='hint'>Max ");
    html += String(MAX_WEATHER_LOCATIONS);
    html += F(" locations. Current: ");
    html += String(locCount);
    html += F("</p><p class='hint'><strong>Note:</strong> Location 1 is used for the time display timezone.</p>"
        "<p class='hint' style='margin-top:10px'>Weather data from <a href='https://open-meteo.com/' target='_blank'>Open-Meteo</a> (free, no API key required)</p></div>");

    // Settings - Temperature
    html += F("<div class='card'><h3>Settings</h3>"
        "<label>Temperature Unit</label><select id='unit'>"
        "<option value='f'");
    html += celsius ? "" : " selected";
    html += F(">Fahrenheit</option><option value='c'");
    html += celsius ? " selected" : "";
    html += F(">Celsius</option></select>");

    // Screen Cycle Time
    html += F("<label>Screen Cycle Time: <span id='cycleVal'>");
    html += String(getScreenCycleTime());
    html += F("</span>s</label><input type='range' id='cycleTime' min='5' max='60' value='");
    html += String(getScreenCycleTime());
    html += F("' oninput='document.getElementById(\"cycleVal\").textContent=this.value'>"
        "<p class='hint'>Seconds between screen changes (5-60)</p>");

    // Show Forecast
    html += F("<div class='toggle-row'><span>Show Forecast Screens</span><input type='checkbox' id='showForecast'");
    html += getShowForecast() ? " checked" : "";
    html += F("></div>"
        "<p class='hint'>Include 7-day forecast in screen rotation (always cycles between locations)</p>");

    // Theme Mode
    html += F("<label>Theme</label><select id='themeMode'>"
        "<option value='0'");
    html += (getThemeMode() == 0) ? " selected" : "";
    html += F(">Auto (dark at night)</option><option value='1'");
    html += (getThemeMode() == 1) ? " selected" : "";
    html += F(">Always Dark</option><option value='2'");
    html += (getThemeMode() == 2) ? " selected" : "";
    html += F(">Always Light</option></select>");

    // Brightness
    html += F("<label>Brightness: <span id='brtVal'>");
    html += String(getBrightness());
    html += F("</span>%</label><input type='range' id='brightness' min='10' max='100' value='");
    html += String(getBrightness());
    html += F("' oninput='document.getElementById(\"brtVal\").textContent=this.value'>");

    // Night Mode
    html += F("<div class='toggle-row'><span>Enable Night Mode</span><input type='checkbox' id='nightMode'");
    html += getNightModeEnabled() ? " checked" : "";
    html += F("></div>"
        "<p class='hint'>Automatically dims display and uses dark theme during night hours</p>"
        "<div class='row'><div><label>Night Start</label><select id='nightStart'>");
    for (int h = 0; h < 24; h++) {
        html += F("<option value='");
        html += String(h);
        html += "'";
        if (h == getNightModeStartHour()) html += F(" selected");
        html += ">";
        html += (h == 0) ? "12 AM" : (h < 12) ? String(h) + " AM" : (h == 12) ? "12 PM" : String(h - 12) + " PM";
        html += F("</option>");
    }
    html += F("</select></div><div><label>Night End</label><select id='nightEnd'>");
    for (int h = 0; h < 24; h++) {
        html += F("<option value='");
        html += String(h);
        html += "'";
        if (h == getNightModeEndHour()) html += F(" selected");
        html += ">";
        html += (h == 0) ? "12 AM" : (h < 12) ? String(h) + " AM" : (h == 12) ? "12 PM" : String(h - 12) + " PM";
        html += F("</option>");
    }
    html += F("</select></div></div>"
        "<label>Night Brightness: <span id='nightBrtVal'>");
    html += String(getNightModeBrightness());
    html += F("</span>%</label><input type='range' id='nightBrightness' min='5' max='50' value='");
    html += String(getNightModeBrightness());
    html += F("' oninput='document.getElementById(\"nightBrtVal\").textContent=this.value'>");

    html += F("<button onclick='saveSettings()'>Save Settings</button>"
        "<div id='st' class='status'></div></div>");

    // Display Position section
    html += F("<div class='card'><h3>Display Position</h3>"
        "<p class='hint'>Adjust the vertical position of the UI if your display is cut off by the frame.</p>"
        "<label>UI Nudge (pixels)</label>"
        "<div style='display:flex;align-items:center;gap:10px'>"
        "<input type='range' id='uiNudgeY' min='-20' max='20' value='");
    html += String(getUiNudgeY());
    html += F("' style='flex:1' oninput='document.getElementById(\"nudgeVal\").textContent=this.value'>"
        "<span id='nudgeVal' style='min-width:30px;text-align:center'>");
    html += String(getUiNudgeY());
    html += F("</span></div>"
        "<p class='hint'>Positive = move up, Negative = move down. Range: -20 to +20.</p>"
        "<button type='button' onclick='saveNudge()' style='margin-top:10px'>Apply Nudge</button>"
        "<div id='nudgeSt' class='status'></div></div>");

    // Custom Screen section
    html += F("<div class='card'><h3>Custom Screen</h3>"
        "<p class='hint'>Add an optional custom text screen that appears after each location's weather screens.</p>"
        "<div class='toggle-row'><span>Enable Custom Screen</span><input type='checkbox' id='customEnabled'");
    html += getCustomScreenEnabled() ? " checked" : "";
    html += F("></div>"
        "<div id='customFields' style='opacity:");
    html += getCustomScreenEnabled() ? "1" : "0.5";
    html += F("'>"
        "<label>Header Text (max 16 chars, top-right)</label>"
        "<input type='text' id='customHeader' maxlength='16' placeholder='e.g., My Quote' value='");
    // Escape any quotes in the header
    String header = getCustomScreenHeader();
    header.replace("'", "&#39;");
    html += header;
    html += F("'>"
        "<label>Body Text (max 160 chars, centered)</label>"
        "<textarea id='customBody' maxlength='160' rows='3' style='width:100%;padding:10px;border:1px solid #333;"
        "border-radius:6px;background:#2a2a4e;color:#eee;resize:vertical' placeholder='Your custom message...'>");
    String body = getCustomScreenBody();
    body.replace("<", "&lt;");
    body.replace(">", "&gt;");
    html += body;
    html += F("</textarea>"
        "<div style='font-size:0.75em;color:#666;margin-top:4px'><span id='bodyCount'>");
    html += String(strlen(getCustomScreenBody()));
    html += F("</span>/160 chars</div>"
        "<label>Footer Text (max 30 chars, bottom bar)</label>"
        "<input type='text' id='customFooter' maxlength='30' placeholder='e.g., Have a great day!' value='");
    String footer = getCustomScreenFooter();
    footer.replace("'", "&#39;");
    html += footer;
    html += F("'></div>"
        "<button type='button' onclick='saveCustom()' style='margin-top:10px'>Save Custom Screen</button>"
        "<div id='customSt' class='status'></div></div>");

    // Links
    html += F("<div class='card' style='text-align:center'>"
        "<a href='/'>Home</a> | <a href='/preview'>Display Preview</a> | "
        "<a href='/update'>Firmware</a> | <a href='/api/safemode' style='color:#ff6600'>Safe Mode</a> | <a href='/reboot'>Reboot</a></div>");

    // JavaScript - more complex now for multi-location management
    html += F("<script>"
        "let locations=[];let pendingLoc=null;const MAX=");
    html += String(MAX_WEATHER_LOCATIONS);
    html += F(";"
        // Load current locations from API
        "async function loadLocations(){"
        "try{const r=await fetch('/api/config');const d=await r.json();"
        "locations=d.locations||[];renderLocations();"
        "}catch(e){console.error('Load failed',e);}}"

        // Render locations list
        "function renderLocations(){"
        "const el=document.getElementById('locations');el.innerHTML='';"
        "locations.forEach((loc,i)=>{"
        "const div=document.createElement('div');div.className='loc-item';"
        "div.innerHTML='<div class=\"name\">'+(i+1)+'. '+loc.name+'</div>'"
        "+'<div class=\"coords\">'+loc.lat.toFixed(4)+', '+loc.lon.toFixed(4)+'</div>';"
        "if(locations.length>1){"
        "const btn=document.createElement('button');btn.className='remove';btn.textContent='Remove';"
        "btn.onclick=()=>removeLoc(i);div.appendChild(btn);}"
        "el.appendChild(div);});}"

        // Search city
        "async function searchCity(){"
        "const q=document.getElementById('search').value.trim();"
        "if(q.length<2){alert('Enter at least 2 characters');return;}"
        "const res=document.getElementById('results');res.innerHTML='Searching...';"
        "try{const r=await fetch('/api/geocode?q='+encodeURIComponent(q));"
        "const d=await r.json();if(d.error){res.innerHTML='<p>'+d.error+'</p>';return;}"
        "if(!d.results||d.results.length===0){res.innerHTML='<p>No results found</p>';return;}"
        "res.innerHTML='';d.results.forEach(loc=>{"
        "const div=document.createElement('div');div.className='result';"
        "div.innerHTML=loc.display+'<br><small>'+loc.lat.toFixed(4)+', '+loc.lon.toFixed(4)+'</small>';"
        "div.onclick=()=>selectLocation(loc);res.appendChild(div);});"
        "}catch(e){res.innerHTML='<p>Search failed</p>';}}"

        // Select location from search
        "function selectLocation(loc){"
        "pendingLoc=loc;"
        "document.getElementById('pending').style.display='block';"
        "document.getElementById('pendingName').textContent=loc.display||loc.name;"
        "document.getElementById('pendingCoords').textContent=loc.lat.toFixed(4)+', '+loc.lon.toFixed(4);"
        "document.getElementById('results').innerHTML='<p style=\"color:#0c6\">Selected - click Add to confirm</p>';}"

        // Add pending location
        "async function addPending(){"
        "if(!pendingLoc){alert('Select a location first');return;}"
        "if(locations.length>=MAX){alert('Max '+MAX+' locations');return;}"
        "locations.push({name:pendingLoc.name,lat:pendingLoc.lat,lon:pendingLoc.lon,enabled:true});"
        "await saveLocations();pendingLoc=null;"
        "document.getElementById('pending').style.display='none';}"

        // Remove location
        "async function removeLoc(idx){"
        "if(locations.length<=1){alert('Must have at least 1 location');return;}"
        "if(!confirm('Remove '+locations[idx].name+'?'))return;"
        "locations.splice(idx,1);await saveLocations();}"

        // Get all settings as object
        "function getSettings(){"
        "return{locations:locations,"
        "useCelsius:document.getElementById('unit').value==='c',"
        "brightness:parseInt(document.getElementById('brightness').value),"
        "nightModeEnabled:document.getElementById('nightMode').checked,"
        "nightModeStartHour:parseInt(document.getElementById('nightStart').value),"
        "nightModeEndHour:parseInt(document.getElementById('nightEnd').value),"
        "nightModeBrightness:parseInt(document.getElementById('nightBrightness').value),"
        "showForecast:document.getElementById('showForecast').checked,"
        "screenCycleTime:parseInt(document.getElementById('cycleTime').value),"
        "themeMode:parseInt(document.getElementById('themeMode').value)};}"

        // Save locations to server
        "async function saveLocations(){"
        "const st=document.getElementById('st');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(getSettings())});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.message;if(d.success){setTimeout(()=>location.reload(),2000);}"
        "}catch(e){st.className='status err';st.textContent='Error';}}"

        // Save settings only
        "async function saveSettings(){"
        "const st=document.getElementById('st');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(getSettings())});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.message;if(d.success){setTimeout(()=>location.reload(),2000);}"
        "}catch(e){st.className='status err';st.textContent='Error';}}"

        // Save UI nudge setting
        "async function saveNudge(){"
        "const st=document.getElementById('nudgeSt');st.style.display='block';st.className='status';"
        "st.textContent='Applying...';"
        "const nudge=parseInt(document.getElementById('uiNudgeY').value);"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({uiNudgeY:nudge})});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.success?'Applied! Screen will update on next cycle.':d.message;"
        "}catch(e){st.className='status err';st.textContent='Error applying';}}"

        // Save custom screen settings
        "async function saveCustom(){"
        "const st=document.getElementById('customSt');st.style.display='block';st.className='status';"
        "st.textContent='Saving...';"
        "const data={customScreenEnabled:document.getElementById('customEnabled').checked,"
        "customScreenHeader:document.getElementById('customHeader').value,"
        "customScreenBody:document.getElementById('customBody').value,"
        "customScreenFooter:document.getElementById('customFooter').value};"
        "try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify(data)});"
        "const d=await r.json();st.className='status '+(d.success?'ok':'err');"
        "st.textContent=d.success?'Saved! Custom screen updated.':d.message;"
        "}catch(e){st.className='status err';st.textContent='Error saving';}}"

        // Event listeners
        "document.getElementById('search').onkeypress=e=>{if(e.key==='Enter'){e.preventDefault();searchCity();}};"
        "document.getElementById('customEnabled').onchange=e=>{"
        "document.getElementById('customFields').style.opacity=e.target.checked?'1':'0.5';};"
        "document.getElementById('customBody').oninput=e=>{"
        "document.getElementById('bodyCount').textContent=e.target.value.length;};"
        "loadLocations();"
        "</script>");

    html += F("</div></body></html>");
    server.send(200, "text/html", html);
}

/**
 * Handle display preview page - HTML5 Canvas simulation of the TFT display
 * Shows what will be rendered on the actual 240x240 display
 */
void handleDisplayPreview() {
    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Display Preview - EpicWeatherBox</title><style>"
        "*{box-sizing:border-box}body{font-family:sans-serif;background:#0d0d1a;color:#eee;margin:0;padding:20px}"
        ".c{max-width:900px;margin:0 auto}h1{color:#00d4ff;text-align:center}"
        ".dual-preview{display:flex;gap:30px;justify-content:center;flex-wrap:wrap;margin:20px 0}"
        ".preview-box{text-align:center}"
        ".preview-label{color:#888;font-size:0.9em;margin-bottom:8px}"
        "canvas{border:8px solid #333;border-radius:12px;background:#000;image-rendering:pixelated}"
        ".controls{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin:15px 0;text-align:center}"
        "button{background:#00d4ff;color:#1a1a2e;border:none;padding:10px 20px;border-radius:6px;cursor:pointer;margin:5px}"
        "button:hover{background:#00a8cc}button.active{background:#00ff88}"
        ".info{margin-top:10px;color:#888;font-size:0.9em}"
        ".screen-label{color:#00d4ff;font-size:1.2em;margin:10px 0}"
        "a{color:#00d4ff}.card{background:rgba(255,255,255,0.05);border-radius:10px;padding:15px;margin:15px 0}"
        ".dots{display:flex;justify-content:center;gap:8px;margin:10px 0}"
        ".dot{width:10px;height:10px;border-radius:50%;background:#444}"
        ".dot.active{background:#00d4ff}"
        "</style></head><body><div class='c'><h1>Display Preview</h1>"
        "<div class='dual-preview'>"
        "<div class='preview-box'><div class='preview-label'>Boot Screen</div>"
        "<canvas id='bootCanvas' width='240' height='240'></canvas></div>"
        "<div class='preview-box'><div class='preview-label'>Main Rotation</div>"
        "<canvas id='display' width='240' height='240'></canvas></div></div>"
        "<div class='controls'>"
        "<div class='screen-label' id='screenLabel'>Current Weather</div>"
        "<div class='dots' id='screenDots'></div>"
        "<button onclick='prevScreen()'>◀ Prev</button>"
        "<button onclick='nextScreen()'>Next ▶</button>"
        "<button onclick='toggleAuto()' id='autoBtn'>Auto: ON</button>"
        "<button onclick='toggleTheme()' id='themeBtn'>Theme: Dark</button>"
        "<button onclick='refreshWeather()'>Refresh Weather</button>"
        "<div class='info'>Screen updates every 10 seconds when Auto is ON</div></div>"
        "<div class='card'><strong>Location:</strong> <span id='locName'>-</span> "
        "(<span id='locIdx'>1</span>/<span id='locTotal'>1</span>)</div>"
        "<div class='card' style='text-align:center'>"
        "<a href='/'>Home</a> | <a href='/admin'>Admin Panel</a> | "
        "<a href='/update'>Firmware</a> | <a href='/api/safemode' style='color:#ff6600'>Safe Mode</a> | <a href='/reboot'>Reboot</a></div>");

    // JavaScript for canvas rendering
    html += F("<script>"
        "const canvas=document.getElementById('display');"
        "const ctx=canvas.getContext('2d');"
        "const bootCanvas=document.getElementById('bootCanvas');"
        "const bootCtx=bootCanvas.getContext('2d');"
        "ctx.imageSmoothingEnabled=false;"
        "bootCtx.imageSmoothingEnabled=false;"
        "let weatherData=null,config=null,currentLoc=0,currentScreen=0,autoPlay=true,autoTimer=null;"
        "let showForecast=true,screenCycleTime=10,darkMode=true;"
        "const SCREENS_PER_LOC=3;"  // Current weather + 2 forecast screens (GIF removed)

        // Colors - dark and light themes
        "const DARK={BG:'#0a0a14',CARD:'#141428',WHITE:'#FFFFFF',GRAY:'#888888',CYAN:'#00D4FF',ORANGE:'#FF6B35',BLUE:'#4DA8DA',YELLOW:'#FFE000',GREEN:'#00FF88'};"
        "const LIGHT={BG:'#E8F4FC',CARD:'#FFFFFF',WHITE:'#1a1a2e',GRAY:'#555555',CYAN:'#0088AA',ORANGE:'#E85520',BLUE:'#2980B9',YELLOW:'#D4A800',GREEN:'#00AA55'};"
        "let C=DARK;"

        // WMO weather icons from CDN (32x32 SVGs)
        "const ICON_CDN='https://cdn.jsdelivr.net/gh/ryanmaule/epicweatherbox@main/images/wmo_icons/';"
        "const WMO_FILES={0:'wmo_0_clear.svg',1:'wmo_1_mainly_clear.svg',2:'wmo_2_partly_cloudy.svg',3:'wmo_3_overcast.svg',"
        "45:'wmo_45_fog.svg',48:'wmo_48_rime_fog.svg',51:'wmo_51_drizzle_light.svg',53:'wmo_53_drizzle_mod.svg',"
        "55:'wmo_55_drizzle_dense.svg',56:'wmo_56_freezing_drizzle_light.svg',57:'wmo_57_freezing_drizzle_dense.svg',"
        "61:'wmo_61_rain_light.svg',63:'wmo_63_rain_mod.svg',65:'wmo_65_rain_heavy.svg',66:'wmo_66_freezing_rain_light.svg',"
        "67:'wmo_67_freezing_rain_heavy.svg',71:'wmo_71_snow_light.svg',73:'wmo_73_snow_mod.svg',75:'wmo_75_snow_heavy.svg',"
        "77:'wmo_77_snow_grains.svg',80:'wmo_80_rain_showers_light.svg',81:'wmo_81_rain_showers_mod.svg',"
        "82:'wmo_82_rain_showers_violent.svg',85:'wmo_85_snow_showers_light.svg',86:'wmo_86_snow_showers_heavy.svg',"
        "95:'wmo_95_thunderstorm.svg',96:'wmo_96_thunderstorm_hail.svg',99:'wmo_99_thunderstorm_hail_heavy.svg'};"
        // Preload all icons
        "const wmoImgs={};"
        "Object.keys(WMO_FILES).forEach(k=>{const img=new Image();img.src=ICON_CDN+WMO_FILES[k];wmoImgs[k]=img;});"

        // Map any WMO code to available icon code
        "function getWmoCode(code){"
        "if(WMO_FILES[code])return code;"
        "if(code<=2)return WMO_FILES[code]?code:0;"
        "if(code===3)return 3;"
        "if(code>=45&&code<=48)return 45;"
        "if(code>=51&&code<=55)return 51;"
        "if(code>=56&&code<=57)return 56;"
        "if(code>=61&&code<=65)return 61;"
        "if(code>=66&&code<=67)return 66;"
        "if(code>=71&&code<=75)return 71;"
        "if(code>=77&&code<=79)return 77;"
        "if(code>=80&&code<=82)return 80;"
        "if(code>=85&&code<=86)return 85;"
        "if(code>=95)return 95;"
        "return 3;}"  // Default to overcast

        // Draw WMO icon from CDN (uses preloaded images)
        "function drawWmoIco(code,x,y,sz){"
        "const c=getWmoCode(code),img=wmoImgs[c];"
        "if(img&&img.complete)ctx.drawImage(img,x,y,sz,sz);}"

        // Format time with AM/PM
        "function fmtTime(){"
        "const n=new Date(),h=n.getHours(),m=n.getMinutes();"
        "const h12=h%12||12,ap=h<12?'AM':'PM';"
        "return{time:h12+':'+(m<10?'0':'')+m,ampm:ap};}"

        // Format date
        "function fmtDate(){"
        "const n=new Date(),mo=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];"
        "return mo[n.getMonth()]+' '+n.getDate();}"

        // Get tomorrow's date + offset
        "function getTomorrow(offset){"
        "const d=new Date();d.setDate(d.getDate()+1+offset);"
        "const days=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];"
        "return{day:days[d.getDay()],date:(d.getMonth()+1)+'/'+d.getDate()};}"

        // Temperature color
        "function tempCol(t){return t<0?C.BLUE:t<10?C.CYAN:t<20?C.WHITE:C.ORANGE;}"

        // Format temp
        "function fmtTemp(t,c){if(!c)t=t*9/5+32;return Math.round(t)+'°';}"

        // Draw globe icon (matches TFT drawGlobe)
        "function drawGlobeIcon(x,y,color){"
        "ctx.strokeStyle=color;ctx.lineWidth=1.5;"
        "ctx.beginPath();ctx.arc(x+6,y+6,6,0,Math.PI*2);ctx.stroke();"
        "ctx.beginPath();ctx.ellipse(x+6,y+6,3,6,0,0,Math.PI*2);ctx.stroke();"
        "ctx.beginPath();ctx.moveTo(x,y+6);ctx.lineTo(x+12,y+6);ctx.stroke();"
        "ctx.lineWidth=1;}"

        // Draw calendar icon (matches TFT drawCalendar)
        "function drawCalIcon(x,y,color){"
        "ctx.strokeStyle=color;ctx.lineWidth=1.5;"
        "ctx.strokeRect(x+1,y+3,10,9);"
        "ctx.beginPath();ctx.moveTo(x+3,y+1);ctx.lineTo(x+3,y+5);ctx.stroke();"
        "ctx.beginPath();ctx.moveTo(x+9,y+1);ctx.lineTo(x+9,y+5);ctx.stroke();"
        "ctx.beginPath();ctx.moveTo(x+1,y+6);ctx.lineTo(x+11,y+6);ctx.stroke();"
        "ctx.lineWidth=1;}"

        // Draw up arrow icon
        "function drawUpArrow(x,y,color){"
        "ctx.fillStyle=color;"
        "ctx.beginPath();ctx.moveTo(x+6,y);ctx.lineTo(x+12,y+8);ctx.lineTo(x,y+8);ctx.closePath();ctx.fill();}"

        // Draw down arrow icon
        "function drawDownArrow(x,y,color){"
        "ctx.fillStyle=color;"
        "ctx.beginPath();ctx.moveTo(x+6,y+8);ctx.lineTo(x+12,y);ctx.lineTo(x,y);ctx.closePath();ctx.fill();}"

        // Draw raindrop icon
        "function drawDropIcon(x,y,color){"
        "ctx.fillStyle=color;"
        "ctx.beginPath();ctx.moveTo(x+5,y);ctx.bezierCurveTo(x+5,y,x,y+6,x+2,y+9);"
        "ctx.bezierCurveTo(x+4,y+11,x+6,y+11,x+8,y+9);ctx.bezierCurveTo(x+10,y+6,x+5,y,x+5,y);ctx.fill();}"

        // Draw current weather screen - matches TFT layout
        "function drawCurrent(){"
        "const locs=weatherData?.locations||[];"
        "if(!locs.length){ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"
        "ctx.fillStyle=C.WHITE;ctx.font='16px sans-serif';ctx.textAlign='center';"
        "ctx.fillText('No weather data',120,120);return;}"
        "const loc=locs[currentLoc]||locs[0],w=loc.current||{};"
        "const useC=weatherData.useCelsius!==false;"
        "ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"

        // Header: Time (large, centered) with smaller AM/PM
        "const t=fmtTime();"
        "ctx.fillStyle=C.CYAN;ctx.font='bold 32px sans-serif';ctx.textAlign='left';"
        "const timeW=ctx.measureText(t.time).width;"
        "ctx.font='14px sans-serif';const ampmW=ctx.measureText(t.ampm).width;"
        "const totalW=timeW+4+ampmW;const startX=120-totalW/2;"
        "ctx.font='bold 32px sans-serif';ctx.fillText(t.time,startX,28);"
        "ctx.font='14px sans-serif';ctx.fillText(t.ampm,startX+timeW+4,20);"

        // Info row: Globe + Location (left), Calendar + Date (right)
        "const infoY=42;"
        "drawGlobeIcon(15,infoY-6,C.GRAY);"
        "ctx.fillStyle=C.GRAY;ctx.font='14px sans-serif';ctx.textAlign='left';"
        "ctx.fillText(loc.location||'Unknown',32,infoY+6);"
        "const dateStr=fmtDate();"
        "ctx.textAlign='right';const dateW=ctx.measureText(dateStr).width;"
        "drawCalIcon(225-dateW-16,infoY-6,C.GRAY);"
        "ctx.fillText(dateStr,225,infoY+6);"

        // Main content: Two columns
        // Left column (0-119): Weather icon (64x64) + condition text
        "drawWmoIco(w.weatherCode||0,28,58,64);"
        "ctx.fillStyle=C.WHITE;ctx.font='14px sans-serif';ctx.textAlign='center';"
        "const cond=w.condition||'Unknown';"
        "ctx.fillText(cond.length>12?cond.substring(0,12):cond,60,138);"

        // Right column: Large temperature with unit
        "const temp=w.temperature||0;"
        "const tempVal=useC?Math.round(temp):Math.round(temp*9/5+32);"
        "ctx.fillStyle=C.WHITE;ctx.font='bold 56px sans-serif';ctx.textAlign='center';"
        "const tempStr=tempVal+'°';"
        "ctx.fillText(tempStr,175,115);"
        "ctx.font='bold 18px sans-serif';ctx.fillText(useC?'C':'F',210,82);"

        // Detail bar at bottom with rounded rectangle background
        "ctx.fillStyle=C.CARD;"
        "ctx.beginPath();ctx.roundRect(8,175,224,36,4);ctx.fill();"

        "const fc=loc.forecast||[];if(fc.length>0){"
        "const today=fc[0];"
        "const hi=useC?Math.round(today.tempMax||0):Math.round((today.tempMax||0)*9/5+32);"
        "const lo=useC?Math.round(today.tempMin||0):Math.round((today.tempMin||0)*9/5+32);"

        // Three sections: High, Low, Precip
        "const secW=74,sec1X=12,sec2X=86,sec3X=160,contentY=185;"
        "drawUpArrow(sec1X,contentY,C.ORANGE);"
        "ctx.fillStyle=C.ORANGE;ctx.font='bold 16px sans-serif';ctx.textAlign='left';"
        "ctx.fillText(hi,sec1X+14,contentY+14);"

        "drawDownArrow(sec2X,contentY,C.BLUE);"
        "ctx.fillStyle=C.BLUE;ctx.fillText(lo,sec2X+14,contentY+14);"

        "const pp=today.precipProbability||today.precipitationProb||0;"
        "const pColor=pp>30?C.CYAN:C.GRAY;"
        "drawDropIcon(sec3X,contentY,pColor);"
        "ctx.fillStyle=pColor;ctx.fillText(Math.round(pp)+'%',sec3X+14,contentY+14);}"

        // Screen dots
        "drawDots();}"

        // Draw forecast screen (3 days starting from startIdx in forecast array)
        "function drawForecast(startIdx){"
        "const locs=weatherData?.locations||[];"
        "if(!locs.length)return;"
        "const loc=locs[currentLoc]||locs[0],fc=loc.forecast||[];"
        "const useC=weatherData.useCelsius!==false;"
        "ctx.fillStyle=C.BG;ctx.fillRect(0,0,240,240);"

        // Header: Time left (cyan) with smaller AM/PM, Globe + Location right (grey)
        "const t=fmtTime();"
        "ctx.fillStyle=C.CYAN;ctx.font='bold 18px sans-serif';ctx.textAlign='left';"
        "ctx.fillText(t.time,8,18);"
        "const timeW=ctx.measureText(t.time).width;"
        "ctx.font='12px sans-serif';ctx.fillText(t.ampm,12+timeW,14);"

        // Globe + Location (right aligned, grey)
        "ctx.fillStyle=C.GRAY;ctx.font='14px sans-serif';ctx.textAlign='right';"
        "const locName=loc.location||'Unknown';"
        "ctx.fillText(locName,232,16);"
        "const locW=ctx.measureText(locName).width;"
        "drawGlobeIcon(232-locW-18,4,C.GRAY);"

        // 3 forecast cards (skip day 0 which is today, so startIdx+1)
        "const cw=75,ch=180,sp=5,sx=(240-3*cw-2*sp)/2;"
        "for(let i=0;i<3;i++){"
        "const fi=startIdx+i+1;"  // +1 to skip today
        "if(fi>=fc.length)continue;"
        "const day=fc[fi],cx=sx+i*(cw+sp);"
        "ctx.fillStyle=C.CARD;"
        "ctx.beginPath();ctx.roundRect(cx,35,cw,ch,4);ctx.fill();"

        // Day name at top
        "ctx.fillStyle=C.CYAN;ctx.font='bold 14px sans-serif';ctx.textAlign='center';"
        "ctx.fillText(day.day||'---',cx+cw/2,52);"

        // Icon - 32x32 centered
        "drawWmoIco(day.weatherCode||0,cx+(cw-32)/2,62,32);"

        // High/Low temps with arrow icons
        "const hi=useC?Math.round(day.tempMax||0):Math.round((day.tempMax||0)*9/5+32);"
        "const lo=useC?Math.round(day.tempMin||0):Math.round((day.tempMin||0)*9/5+32);"
        "drawUpArrow(cx+8,110,C.ORANGE);"
        "ctx.fillStyle=C.ORANGE;ctx.font='bold 14px sans-serif';ctx.textAlign='center';"
        "ctx.fillText(hi,cx+cw/2+8,124);"
        "drawDownArrow(cx+8,135,C.BLUE);"
        "ctx.fillStyle=C.BLUE;ctx.fillText(lo,cx+cw/2+8,149);"

        // Precip with droplet icon
        "const pp=day.precipProbability||day.precipitationProb||0;"
        "const pColor=pp>30?C.CYAN:C.GRAY;"
        "drawDropIcon(cx+8,165,pColor);"
        "ctx.fillStyle=pColor;ctx.font='12px sans-serif';"
        "ctx.fillText(Math.round(pp)+'%',cx+cw/2+8,180);}"

        // Screen dots
        "drawDots();}"

        // Draw boot screen on separate canvas (always visible)
        // Layout matches actual TFT: centered vertically with text and optional GIF
        "function renderBootScreen(){"
        "bootCtx.fillStyle=C.BG;bootCtx.fillRect(0,0,240,240);"
        "bootCtx.textAlign='center';"
        // "Epic" in cyan at y=95 (matches TFT)
        "bootCtx.fillStyle=C.CYAN;bootCtx.font='bold 24px sans-serif';"
        "bootCtx.fillText('Epic',120,95);"
        // "WeatherBox" in white at y=130 (matches TFT)
        "bootCtx.fillStyle='#fff';bootCtx.font='bold 24px sans-serif';"
        "bootCtx.fillText('WeatherBox',120,130);"
        // Version in gray at y=165 (matches TFT)
        "bootCtx.fillStyle=C.GRAY;bootCtx.font='14px sans-serif';"
        "bootCtx.fillText('v1.0.0',120,165);"
        // Show "Connecting..." at bottom y=228 (matches TFT)
        "bootCtx.fillStyle='#666';bootCtx.font='12px sans-serif';"
        "bootCtx.fillText('Connecting...',120,228);}"

        // Draw screen indicator dots
        "function drawDots(){"
        "const locs=weatherData?.locations||[],nLocs=locs.length||1;"
        "const screens=SCREENS_PER_LOC;"
        "const total=nLocs*screens,cur=currentLoc*screens+Math.min(currentScreen,screens-1);"
        "const dotR=3,gap=10,sx=120-(total-1)*gap/2;"
        "for(let i=0;i<total;i++){"
        "ctx.fillStyle=i===cur?C.CYAN:C.GRAY;"
        "ctx.beginPath();ctx.arc(sx+i*gap,232,dotR,0,Math.PI*2);ctx.fill();}}"

        // Update HTML dots for main rotation
        "function updateHtmlDots(){"
        "const el=document.getElementById('screenDots');el.innerHTML='';"
        "const locs=weatherData?.locations||[],nLocs=locs.length||1;"
        "const screens=SCREENS_PER_LOC;"
        "const total=nLocs*screens,cur=currentLoc*screens+Math.min(currentScreen,screens-1);"
        "for(let i=0;i<total;i++){const d=document.createElement('div');"
        "d.className='dot'+(i===cur?' active':'');el.appendChild(d);}}"

        // Main render - boot screen is always on separate canvas
        "function render(){"
        "renderBootScreen();"  // Always render boot screen on left canvas
        "ctx.clearRect(0,0,240,240);"
        "const names=['Current Weather','Forecast Days 1-3','Forecast Days 4-6'];"
        "document.getElementById('screenLabel').textContent=names[currentScreen]||'Unknown';"
        "if(weatherData?.locations){"
        "const loc=weatherData.locations[currentLoc];"
        "document.getElementById('locName').textContent=loc?.location||'Unknown';"
        "document.getElementById('locIdx').textContent=currentLoc+1;"
        "document.getElementById('locTotal').textContent=weatherData.locations.length;}"
        "updateHtmlDots();"
        "switch(currentScreen){"
        "case 0:drawCurrent();break;"
        "case 1:drawForecast(0);break;"
        "case 2:drawForecast(3);break;}}"

        // Navigation - always cycles, but may skip forecast screens
        "function nextScreen(){"
        "const nLocs=weatherData?.locations?.length||1;"
        "if(!showForecast){"
        // Only current weather, skip to next location
        "currentLoc=(currentLoc+1)%nLocs;currentScreen=0;}"
        "else{"
        // Full rotation with forecast (3 screens per location)
        "currentScreen++;"
        "if(currentScreen>=SCREENS_PER_LOC){currentScreen=0;currentLoc=(currentLoc+1)%nLocs;}}"
        "render();}"

        "function prevScreen(){"
        "const nLocs=weatherData?.locations?.length||1;"
        "if(!showForecast){"
        "currentLoc=(currentLoc+nLocs-1)%nLocs;currentScreen=0;}"
        "else{"
        "currentScreen--;"
        "if(currentScreen<0){currentScreen=SCREENS_PER_LOC-1;currentLoc=(currentLoc+nLocs-1)%nLocs;}}"
        "render();}"

        "function toggleAuto(){"
        "autoPlay=!autoPlay;"
        "document.getElementById('autoBtn').textContent='Auto: '+(autoPlay?'ON':'OFF');"
        "document.getElementById('autoBtn').className=autoPlay?'active':'';"
        "if(autoPlay)startAuto();else stopAuto();}"

        "function toggleTheme(){"
        "darkMode=!darkMode;C=darkMode?DARK:LIGHT;"
        "document.getElementById('themeBtn').textContent='Theme: '+(darkMode?'Dark':'Light');"
        "document.getElementById('themeBtn').className=darkMode?'':'active';"
        "render();}"

        "function startAuto(){stopAuto();autoTimer=setInterval(nextScreen,screenCycleTime*1000);}"
        "function stopAuto(){if(autoTimer){clearInterval(autoTimer);autoTimer=null;}}"

        // Fetch config
        "async function fetchConfig(){"
        "try{const r=await fetch('/api/config');config=await r.json();"
        "showForecast=config.showForecast!==false;"
        "screenCycleTime=config.screenCycleTime||10;"
        "console.log('Config:',config);"
        "}catch(e){console.error('Config fetch failed',e);}}"

        // Fetch weather
        "async function fetchWeather(){"
        "try{const r=await fetch('/api/weather');weatherData=await r.json();"
        "console.log('Weather data:',weatherData);render();"
        "}catch(e){console.error('Fetch failed',e);}}"

        "function refreshWeather(){fetch('/api/weather/refresh').then(()=>setTimeout(fetchWeather,2000));}"

        // Init
        "fetchConfig().then(()=>{fetchWeather();});if(autoPlay)startAuto();setInterval(fetchWeather,60000);"
        "</script>");

    html += F("</div></body></html>");
    server.send(200, "text/html", html);
}

/**
 * Handle 404
 */
void handleNotFound() {
    String message = F("<!DOCTYPE html><html><head>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
        "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
        ".box{text-align:center;}a{color:#00d4ff;}</style></head><body>"
        "<div class='box'><h1>404 - Not Found</h1>"
        "<p>The requested URL was not found.</p>"
        "<p><a href='/'>Go to Home</a></p></div></body></html>");

    server.send(404, "text/html", message);
}
