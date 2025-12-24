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
#include "themes.h"      // Theme system with color management
#include "admin_html.h"  // Generated gzipped admin HTML

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

// Colors are now managed by themes.h/themes.cpp
// Icon colors used as default values for drawing functions
#define ICON_SUN       0x07FF  // Yellow/cyan (used for sun icon rays)
#define ICON_CLOUD     0xFFFF  // White cloud (dark mode default)
#define ICON_RAIN      0xFD00  // Light blue rain (dark mode default)
#define ICON_SNOW      0xFFFF  // White snow (dark mode default)
#define ICON_LIGHTNING 0x07FF  // Yellow lightning bolt

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
    uint16_t c = getThemeGray();

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
            // Narrower minus sign - half the width of a digit
            {
                int minusW = w / 2;
                tft.fillRoundRect(left, mid, minusW, t, t/2, color);
                return minusW + gap;
            }
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
            total += t + gap;  // Narrow '1'
        } else if (numStr[i] == '-') {
            total += w / 2 + gap;  // Narrow minus sign
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
    // Use hardcoded dark theme colors for boot (before themes loaded)
    tft.fillScreen(0x0841);  // Dark background
    tft.setTextDatum(MC_DATUM);  // Middle center

    // "Epic" in bold 18pt cyan
    tft.setFreeFont(FSSB18);
    tft.setTextColor(0x07FF);  // Cyan
    tft.drawString("Epic", 120, 95, GFXFF);

    // "WeatherBox" in bold 18pt white
    tft.setTextColor(0xFFFF);  // White
    tft.drawString("WeatherBox", 120, 130, GFXFF);

    // Version in small gray
    tft.setFreeFont(FSS9);
    tft.setTextColor(0x8410);  // Gray
    tft.drawString("v" FIRMWARE_VERSION, 120, 165, GFXFF);

    // Status text at bottom (y=218 to match IP position - keeps 4+ pixels from bottom edge)
    tft.setTextColor(0x4208);  // Dark gray
    tft.drawString("Connecting...", 120, 218, GFXFF);

    Serial.println(F("[TFT] Boot screen displayed"));
    lastDisplayUpdate = millis();
}

// Update boot screen status text at bottom
void updateBootScreenStatus(const char* status) {
    // Clear the status area (y=195 to bottom) - hardcoded dark theme for boot
    tft.fillRect(0, 195, 240, 45, 0x0841);  // Dark background

    // Draw new status at y=218 (4+ pixels from bottom edge)
    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FSS9);
    tft.setTextColor(0x8410);  // Gray
    tft.drawString(status, 120, 218, GFXFF);
}

// Show IP address on boot screen (called after WiFi connects)
// Shows IP in gray first, then transitions to cyan for emphasis
void showBootScreenIP(const char* ip) {
    // Clear bottom area for IP display - hardcoded dark theme for boot
    tft.fillRect(0, 195, 240, 45, 0x0841);  // Dark background

    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(FSS9);

    // First: show IP in gray
    tft.setTextColor(0x8410);  // Gray
    tft.drawString(ip, 120, 218, GFXFF);  // y=218 keeps text 4+ pixels from bottom edge

    delay(400);

    // Then: redraw IP in cyan for emphasis (same position)
    tft.setTextColor(0x07FF);  // Cyan
    tft.drawString(ip, 120, 218, GFXFF);
}

// Theme functions are now in themes.cpp
// shouldUseDarkTheme(), getThemeBg(), getThemeCard(), getThemeText()
// getThemeCyan(), getThemeOrange(), getThemeBlue(), getThemeGray()
// getIconCloud(), getIconCloudDark(), getIconSnow(), getIconRain()

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
void drawCurrentWeather(int currentScreen, int totalScreens) {
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
    // Use short string version for better fit (e.g., "P.Cloudy" instead of "Partly Cloudy")
    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(FSS12);
    tft.setTextColor(textColor);
    tft.drawString(conditionToShortString(weather.current.condition), leftColCenter, mainY + 70, GFXFF);

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

    // Screen dots at bottom
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
void drawForecast(int startDay, int currentScreen, int totalScreens) {
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

    // Screen dots at bottom
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

// =============================================================================
// COUNTDOWN SCREEN
// =============================================================================

// Calculate Easter Sunday for a given year (Anonymous Gregorian algorithm / Computus)
void calculateEaster(int year, int* month, int* day) {
    int a = year % 19;
    int b = year / 100;
    int c = year % 100;
    int d = b / 4;
    int e = b % 4;
    int f = (b + 8) / 25;
    int g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4;
    int k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    *month = (h + l - 7 * m + 114) / 31;
    *day = ((h + l - 7 * m + 114) % 31) + 1;
}

// Days in each month (handles leap years)
int daysInMonth(int month, int year) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        return 29;  // Leap year February
    }
    return days[month - 1];
}

// Convert date to days since epoch (simple calculation)
long dateToDays(int year, int month, int day) {
    long days = 0;
    for (int y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    for (int m = 1; m < month; m++) {
        days += daysInMonth(m, year);
    }
    days += day;
    return days;
}

// Calculate days until target date
int daysUntil(int targetYear, int targetMonth, int targetDay,
              int currentYear, int currentMonth, int currentDay) {
    long targetDays = dateToDays(targetYear, targetMonth, targetDay);
    long currentDays = dateToDays(currentYear, currentMonth, currentDay);
    return (int)(targetDays - currentDays);
}

// Get next occurrence of an event (handles recurring events)
void getNextEventDate(const CountdownEvent& event, int currentYear, int currentMonth, int currentDay,
                      int* targetYear, int* targetMonth, int* targetDay) {
    switch (event.type) {
        case COUNTDOWN_EASTER:
            calculateEaster(currentYear, targetMonth, targetDay);
            *targetYear = currentYear;
            // If Easter has passed, get next year's
            if (*targetMonth < currentMonth ||
                (*targetMonth == currentMonth && *targetDay <= currentDay)) {
                calculateEaster(currentYear + 1, targetMonth, targetDay);
                *targetYear = currentYear + 1;
            }
            break;

        case COUNTDOWN_HALLOWEEN:
            *targetMonth = 10; *targetDay = 31;
            *targetYear = currentYear;
            if (currentMonth > 10 || (currentMonth == 10 && currentDay > 31)) {
                *targetYear = currentYear + 1;
            }
            break;

        case COUNTDOWN_VALENTINE:
            *targetMonth = 2; *targetDay = 14;
            *targetYear = currentYear;
            if (currentMonth > 2 || (currentMonth == 2 && currentDay > 14)) {
                *targetYear = currentYear + 1;
            }
            break;

        case COUNTDOWN_CHRISTMAS:
            *targetMonth = 12; *targetDay = 25;
            *targetYear = currentYear;
            if (currentMonth == 12 && currentDay > 25) {
                *targetYear = currentYear + 1;
            }
            break;

        case COUNTDOWN_BIRTHDAY:
        case COUNTDOWN_CUSTOM:
        default:
            *targetMonth = event.month;
            *targetDay = event.day;
            *targetYear = currentYear;
            // Recurring - if passed this year, use next year
            if (currentMonth > event.month ||
                (currentMonth == event.month && currentDay > event.day)) {
                *targetYear = currentYear + 1;
            }
            break;
    }
}

// Get event type name
const char* getEventTypeName(uint8_t type) {
    switch (type) {
        case COUNTDOWN_BIRTHDAY: return "Birthday";
        case COUNTDOWN_EASTER: return "Easter";
        case COUNTDOWN_HALLOWEEN: return "Halloween";
        case COUNTDOWN_VALENTINE: return "Valentine's";
        case COUNTDOWN_CHRISTMAS: return "Christmas";
        case COUNTDOWN_CUSTOM: return "Event";
        default: return "Event";
    }
}

// Draw large countdown icon (approx 48x48 pixels, centered at cx,cy)
// For custom events, dayNum is displayed on the calendar icon
void drawCountdownIcon(int cx, int cy, uint8_t type, uint16_t color, uint8_t dayNum = 0) {
    switch (type) {
        case COUNTDOWN_BIRTHDAY:
            // Large cake icon
            tft.fillRect(cx - 2, cy - 22, 4, 8, TFT_YELLOW);  // Candle flame
            tft.fillRect(cx - 1, cy - 14, 2, 8, color);       // Candle
            tft.fillRoundRect(cx - 18, cy - 6, 36, 14, 4, color);  // Cake top
            tft.fillRoundRect(cx - 20, cy + 6, 40, 16, 4, color);  // Cake bottom
            break;

        case COUNTDOWN_EASTER:
            // Large bunny icon
            tft.fillRoundRect(cx - 10, cy - 24, 8, 20, 4, color);  // Left ear
            tft.fillRoundRect(cx + 2, cy - 24, 8, 20, 4, color);   // Right ear
            tft.fillCircle(cx, cy + 4, 18, color);  // Head/body
            tft.fillCircle(cx - 6, cy - 2, 3, TFT_BLACK);  // Left eye
            tft.fillCircle(cx + 6, cy - 2, 3, TFT_BLACK);  // Right eye
            break;

        case COUNTDOWN_HALLOWEEN:
            // Large pumpkin icon
            tft.fillRect(cx - 3, cy - 24, 6, 8, TFT_GREEN);  // Stem
            tft.fillCircle(cx, cy + 2, 22, TFT_ORANGE);  // Main pumpkin
            tft.fillTriangle(cx - 8, cy - 4, cx - 4, cy + 4, cx - 12, cy + 4, TFT_BLACK);  // Left eye
            tft.fillTriangle(cx + 8, cy - 4, cx + 4, cy + 4, cx + 12, cy + 4, TFT_BLACK);  // Right eye
            tft.fillTriangle(cx, cy + 6, cx - 8, cy + 14, cx + 8, cy + 14, TFT_BLACK);  // Mouth
            break;

        case COUNTDOWN_VALENTINE:
            // Large heart icon
            tft.fillCircle(cx - 10, cy - 6, 14, TFT_RED);   // Left lobe
            tft.fillCircle(cx + 10, cy - 6, 14, TFT_RED);  // Right lobe
            tft.fillTriangle(cx - 24, cy - 2, cx + 24, cy - 2, cx, cy + 24, TFT_RED);  // Bottom
            break;

        case COUNTDOWN_CHRISTMAS:
            // Large tree icon
            tft.fillTriangle(cx, cy - 22, cx - 16, cy - 6, cx + 16, cy - 6, TFT_GREEN);   // Top
            tft.fillTriangle(cx, cy - 12, cx - 22, cy + 6, cx + 22, cy + 6, TFT_GREEN);  // Middle
            tft.fillTriangle(cx, cy - 2, cx - 26, cy + 18, cx + 26, cy + 18, TFT_GREEN); // Bottom
            tft.fillRect(cx - 5, cy + 16, 10, 10, 0x8420);  // Trunk (brown)
            tft.fillCircle(cx, cy - 16, 3, TFT_YELLOW);  // Star
            break;

        case COUNTDOWN_CUSTOM:
        default:
            // Large calendar icon
            tft.fillRoundRect(cx - 20, cy - 16, 40, 38, 4, color);  // Box
            tft.fillRect(cx - 20, cy - 16, 40, 12, color);  // Header
            tft.drawLine(cx - 18, cy - 4, cx + 18, cy - 4, getThemeBg());  // Divider
            // Calendar rings
            tft.fillRoundRect(cx - 12, cy - 22, 6, 10, 2, color);
            tft.fillRoundRect(cx + 6, cy - 22, 6, 10, 2, color);
            // Date number in calendar (use provided dayNum, default to 25)
            {
                char dayStr[4];
                snprintf(dayStr, sizeof(dayStr), "%d", dayNum > 0 ? dayNum : 25);
                tft.setFreeFont(FSSB12);
                tft.setTextDatum(MC_DATUM);
                tft.setTextColor(getThemeBg());
                tft.drawString(dayStr, cx, cy + 8, GFXFF);
            }
            break;
    }
}

// Draw countdown screen for a specific countdown event
void drawCountdownScreen(uint8_t countdownIndex, int currentScreen, int totalScreens) {
    const CountdownEvent& event = getCountdown(countdownIndex);

    // Get theme colors
    int yOff = -getUiNudgeY();
    uint16_t bgColor = getThemeBg();
    uint16_t cardColor = getThemeCard();
    uint16_t cyanColor = getThemeCyan();
    uint16_t grayColor = getThemeGray();
    uint16_t textColor = getThemeText();

    tft.fillScreen(bgColor);

    // Get current time from primary location
    const WeatherData& primaryWeather = getWeather(0);
    long localEpoch = timeClient.getEpochTime() + primaryWeather.utcOffsetSeconds;
    int hours = (localEpoch % 86400L) / 3600;
    int minutes = (localEpoch % 3600) / 60;
    int h12 = hours % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (hours < 12) ? "AM" : "PM";

    // Calculate current date from epoch
    unsigned long daysFromEpoch = localEpoch / 86400;
    int year = 1970;
    while (true) {
        int daysInYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (daysFromEpoch < (unsigned long)daysInYear) break;
        daysFromEpoch -= daysInYear;
        year++;
    }
    int month = 1;
    while (daysFromEpoch >= (unsigned long)daysInMonth(month, year)) {
        daysFromEpoch -= daysInMonth(month, year);
        month++;
    }
    int day = daysFromEpoch + 1;

    // HEADER: Time (left) + "Countdown" (right)
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%d:%02d", h12, minutes);
    tft.setFreeFont(FSSB12);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(cyanColor);
    tft.drawString(timeStr, 8, 8 + yOff, GFXFF);

    int16_t timeW = tft.textWidth(timeStr, GFXFF);
    tft.setFreeFont(FSS9);
    tft.drawString(ampm, 8 + timeW + 4, 10 + yOff, GFXFF);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(grayColor);
    tft.drawString("Countdown", 232, 10 + yOff, GFXFF);

    // Get target date and days until
    int targetYear, targetMonth, targetDay;
    getNextEventDate(event, year, month, day, &targetYear, &targetMonth, &targetDay);
    int daysLeft = daysUntil(targetYear, targetMonth, targetDay, year, month, day);

    // Draw large icon (centered at 120, 75)
    // Pass targetDay for custom events to display on calendar icon
    drawCountdownIcon(120, 75 + yOff, event.type, cyanColor, (uint8_t)targetDay);

    // Event title (smaller, below icon)
    const char* title = (strlen(event.title) > 0) ? event.title : getEventTypeName(event.type);
    tft.setFreeFont(FSSB12);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(textColor);
    tft.drawString(title, 120, 120 + yOff, GFXFF);

    // Days remaining (large number)
    char daysStr[32];
    if (daysLeft == 0) {
        snprintf(daysStr, sizeof(daysStr), "TODAY!");
        tft.setTextColor(cyanColor);
    } else if (daysLeft == 1) {
        snprintf(daysStr, sizeof(daysStr), "1 day");
        tft.setTextColor(cyanColor);
    } else if (daysLeft <= 7) {
        snprintf(daysStr, sizeof(daysStr), "%d days", daysLeft);
        tft.setTextColor(cyanColor);
    } else {
        snprintf(daysStr, sizeof(daysStr), "%d days", daysLeft);
        tft.setTextColor(textColor);
    }
    tft.setFreeFont(FSSB18);
    tft.drawString(daysStr, 120, 155 + yOff, GFXFF);

    // Target date with day of week
    const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* monthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    // Calculate day of week using Zeller's formula
    int m = targetMonth, y = targetYear, d = targetDay;
    if (m < 3) { m += 12; y--; }
    int dow = (d + 13*(m+1)/5 + y + y/4 - y/100 + y/400) % 7;
    dow = (dow + 6) % 7;  // Convert to 0=Sunday

    char dateStr[32];
    snprintf(dateStr, sizeof(dateStr), "%s, %s %d", dayNames[dow], monthNames[targetMonth-1], targetDay);
    tft.setFreeFont(FSS9);
    tft.setTextColor(grayColor);
    tft.drawString(dateStr, 120, 185 + yOff, GFXFF);

    // Draw navigation dots at bottom
    if (totalScreens > 1) {
        int dotSpacing = 12;
        int dotStartX = 120 - (totalScreens - 1) * dotSpacing / 2;
        int dotY = 230 + yOff;
        if (dotY > 236) dotY = 236;
        for (int i = 0; i < totalScreens; i++) {
            uint16_t dotColor = (i == currentScreen) ? cyanColor : grayColor;
            tft.fillCircle(dotStartX + i * dotSpacing, dotY, 3, dotColor);
        }
    }
}

// Draw custom screen for a specific custom screen config (carousel version)
void drawCustomScreenByIndex(uint8_t customIndex, int currentScreen, int totalScreens) {
    const CustomScreenConfig& config = getCustomScreenConfig(customIndex);

    // Get theme colors
    int yOff = -getUiNudgeY();
    uint16_t bgColor = getThemeBg();
    uint16_t cardColor = getThemeCard();
    uint16_t cyanColor = getThemeCyan();
    uint16_t grayColor = getThemeGray();
    uint16_t textColor = getThemeText();

    tft.fillScreen(bgColor);

    // Get current time
    const WeatherData& primaryWeather = getWeather(0);
    long localEpoch = timeClient.getEpochTime() + primaryWeather.utcOffsetSeconds;
    int hours = (localEpoch % 86400L) / 3600;
    int minutes = (localEpoch % 3600) / 60;
    int h12 = hours % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (hours < 12) ? "AM" : "PM";

    // HEADER
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%d:%02d", h12, minutes);
    tft.setFreeFont(FSSB12);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(cyanColor);
    tft.drawString(timeStr, 8, 8 + yOff, GFXFF);

    int16_t timeW = tft.textWidth(timeStr, GFXFF);
    tft.setFreeFont(FSS9);
    tft.drawString(ampm, 8 + timeW + 4, 10 + yOff, GFXFF);

    // Header text (right side) with star icon
    if (strlen(config.header) > 0) {
        tft.setFreeFont(FSS9);
        int16_t headerW = tft.textWidth(config.header, GFXFF);
        int textX = 232 - headerW;
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(grayColor);
        tft.drawString(config.header, textX, 10 + yOff, GFXFF);

        // Draw star icon to the left of header text
        int starX = textX - 12;
        int starY = 14 + yOff;
        int starSize = 4;
        tft.fillTriangle(starX, starY - starSize, starX - 3, starY + 2, starX + 3, starY + 2, grayColor);
        tft.fillTriangle(starX - starSize, starY - 1, starX + starSize, starY - 1, starX, starY + 3, grayColor);
    } else {
        // No header, just draw star in corner
        int starX = 224;
        int starY = 14 + yOff;
        int starSize = 4;
        tft.fillTriangle(starX, starY - starSize, starX - 3, starY + 2, starX + 3, starY + 2, grayColor);
        tft.fillTriangle(starX - starSize, starY - 1, starX + starSize, starY - 1, starX, starY + 3, grayColor);
    }

    // BODY - centered text with word wrap
    if (strlen(config.body) > 0) {
        int bodyLen = strlen(config.body);
        int fontSize;
        int lineHeight;

        if (bodyLen <= 40) {
            tft.setFreeFont(FSSB18);
            fontSize = 18;
            lineHeight = 38;
        } else {
            tft.setFreeFont(FSSB12);
            fontSize = 12;
            lineHeight = 30;
        }

        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(textColor);

        // Simple word wrap
        String text = config.body;
        int maxWidth = 220;
        int startY = 100 + yOff;
        int lineCount = 0;
        String lines[4];

        String currentLine = "";
        int wordStart = 0;
        for (int i = 0; i <= text.length() && lineCount < 4; i++) {
            if (i == text.length() || text[i] == ' ' || text[i] == '\n') {
                String word = text.substring(wordStart, i);
                String testLine = currentLine + (currentLine.length() > 0 ? " " : "") + word;
                if (tft.textWidth(testLine.c_str(), GFXFF) <= maxWidth) {
                    currentLine = testLine;
                } else {
                    if (currentLine.length() > 0 && lineCount < 4) {
                        lines[lineCount++] = currentLine;
                    }
                    currentLine = word;
                }
                wordStart = i + 1;
            }
        }
        if (currentLine.length() > 0 && lineCount < 4) {
            lines[lineCount++] = currentLine;
        }

        // Calculate vertical center
        int totalHeight = lineCount * lineHeight;
        int bodyStartY = 100 + yOff - totalHeight / 2 + lineHeight / 2;

        for (int i = 0; i < lineCount; i++) {
            tft.drawString(lines[i].c_str(), 120, bodyStartY + i * lineHeight, GFXFF);
        }
    }

    // FOOTER - rounded bar at bottom (matches main weather screen footer)
    if (strlen(config.footer) > 0) {
        int barY = 175 + yOff;
        int barH = 36;
        int barMargin = 8;
        tft.fillRoundRect(barMargin, barY, 240 - 2*barMargin, barH, 4, cardColor);
        tft.setFreeFont(FSSB12);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(cyanColor);
        tft.drawString(config.footer, 120, barY + 10, GFXFF);  // Text centered in bar
    }

    // Navigation dots
    if (totalScreens > 1) {
        int dotSpacing = 12;
        int dotStartX = 120 - (totalScreens - 1) * dotSpacing / 2;
        int dotY = 230 + yOff;
        if (dotY > 236) dotY = 236;
        for (int i = 0; i < totalScreens; i++) {
            uint16_t dotColor = (i == currentScreen) ? cyanColor : grayColor;
            tft.fillCircle(dotStartX + i * dotSpacing, dotY, 3, dotColor);
        }
    }
}

// Track carousel position
static uint8_t currentCarouselIndex = 0;
static uint8_t currentSubScreen = 0;  // For locations: 0=current, 1=forecast1, 2=forecast2

// Calculate total screens in carousel for dot indicators
int calculateTotalScreens() {
    int total = 0;
    uint8_t count = getCarouselCount();
    bool showForecast = getShowForecast();

    for (uint8_t i = 0; i < count; i++) {
        const CarouselItem& item = getCarouselItem(i);
        switch (item.type) {
            case CAROUSEL_LOCATION:
                total += showForecast ? 3 : 1;  // current + forecast1 + forecast2 (or just current)
                break;
            case CAROUSEL_COUNTDOWN:
            case CAROUSEL_CUSTOM:
                total += 1;
                break;
        }
    }
    return total > 0 ? total : 1;
}

// Calculate current screen index for dot indicator
int calculateCurrentScreenIndex() {
    int index = 0;
    bool showForecast = getShowForecast();

    for (uint8_t i = 0; i < currentCarouselIndex; i++) {
        const CarouselItem& item = getCarouselItem(i);
        switch (item.type) {
            case CAROUSEL_LOCATION:
                index += showForecast ? 3 : 1;
                break;
            case CAROUSEL_COUNTDOWN:
            case CAROUSEL_CUSTOM:
                index += 1;
                break;
        }
    }
    index += currentSubScreen;
    return index;
}

// Main display update - call from loop()
// Uses carousel system for flexible screen ordering
void updateTftDisplay() {
    static bool firstRun = true;
    unsigned long now = millis();
    unsigned long cycleMs = (unsigned long)getScreenCycleTime() * 1000;

    // Check if time to change screen (or first run - show immediately)
    if (firstRun || (now - lastDisplayUpdate >= cycleMs)) {
        firstRun = false;
        lastDisplayUpdate = now;

        uint8_t carouselCount = getCarouselCount();
        if (carouselCount == 0) {
            // Fallback: if no carousel items, show current weather for location 0
            drawCurrentWeather(0, 1);  // Single screen, no dots
            return;
        }

        // Feed watchdog and yield
        ESP.wdtFeed();
        yield();

        const CarouselItem& item = getCarouselItem(currentCarouselIndex);
        int totalScreens = calculateTotalScreens();
        int currentScreenIdx = calculateCurrentScreenIndex();
        bool showForecast = getShowForecast();

        switch (item.type) {
            case CAROUSEL_LOCATION: {
                // Location shows 3 screens (or 1 if forecast disabled)
                // Temporarily set currentDisplayLocation for drawCurrentWeather/drawForecast
                currentDisplayLocation = item.dataIndex;

                if (showForecast) {
                    switch (currentSubScreen) {
                        case 0:
                            drawCurrentWeather(currentScreenIdx, totalScreens);
                            break;
                        case 1:
                            drawForecast(0, currentScreenIdx, totalScreens);  // Days 1-3
                            break;
                        case 2:
                            drawForecast(3, currentScreenIdx, totalScreens);  // Days 4-6
                            break;
                    }
                    currentSubScreen++;
                    if (currentSubScreen >= 3) {
                        currentSubScreen = 0;
                        currentCarouselIndex = (currentCarouselIndex + 1) % carouselCount;
                    }
                } else {
                    // Only show current weather
                    drawCurrentWeather(currentScreenIdx, totalScreens);
                    currentCarouselIndex = (currentCarouselIndex + 1) % carouselCount;
                }
                break;
            }

            case CAROUSEL_COUNTDOWN:
                drawCountdownScreen(item.dataIndex, currentScreenIdx, totalScreens);
                currentCarouselIndex = (currentCarouselIndex + 1) % carouselCount;
                break;

            case CAROUSEL_CUSTOM:
                drawCustomScreenByIndex(item.dataIndex, currentScreenIdx, totalScreens);
                currentCarouselIndex = (currentCarouselIndex + 1) % carouselCount;
                break;
        }

        Serial.printf("[TFT] Carousel %d/%d, SubScreen %d, Total %d\n",
                      currentCarouselIndex, carouselCount, currentSubScreen, totalScreens);
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
void handleAdmin();
void handleNotFound();
void feedWatchdog();

/**
 * Provision admin.html.gz to LittleFS from PROGMEM
 * Called on boot to ensure the latest admin UI is available.
 * Only writes if version has changed (avoids unnecessary flash wear).
 */
void provisionAdminHtml() {
    const char* ADMIN_VER_PATH = "/admin.version";
    const char* ADMIN_GZ_PATH = "/admin.html.gz";

    // Check if current version matches
    if (LittleFS.exists(ADMIN_VER_PATH)) {
        File vf = LittleFS.open(ADMIN_VER_PATH, "r");
        if (vf) {
            String currentVersion = vf.readString();
            vf.close();
            currentVersion.trim();
            if (currentVersion == admin_html_version) {
                Serial.println(F("[ADMIN] HTML up to date"));
                return;  // Already up to date
            }
            Serial.printf("[ADMIN] Version mismatch: %s != %s\n",
                         currentVersion.c_str(), admin_html_version);
        }
    }

    Serial.printf("[ADMIN] Provisioning admin.html.gz (%u bytes)...\n", admin_html_gz_len);

    // Write gzipped HTML from PROGMEM to LittleFS
    File f = LittleFS.open(ADMIN_GZ_PATH, "w");
    if (!f) {
        Serial.println(F("[ADMIN] Failed to open file for writing"));
        return;
    }

    // Copy from PROGMEM to file (byte by byte to avoid RAM buffer)
    for (size_t i = 0; i < admin_html_gz_len; i++) {
        f.write(pgm_read_byte(&admin_html_gz[i]));
        // Feed watchdog every 1KB to prevent reset during long write
        if (i % 1024 == 0) {
            ESP.wdtFeed();
            yield();
        }
    }
    f.close();

    // Update version file
    File vf = LittleFS.open(ADMIN_VER_PATH, "w");
    if (vf) {
        vf.print(admin_html_version);
        vf.close();
    }

    Serial.println(F("[ADMIN] Provisioning complete"));
}

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

        // Provision admin HTML from PROGMEM to LittleFS (if version changed)
        provisionAdminHtml();
    }

    feedWatchdog();

    // Initialize theme system (loads from LittleFS)
    Serial.println(F("[BOOT] Initializing themes..."));
    initThemes();

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
    // Redirect root to admin panel
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Location", "/admin", true);
        server.send(302, "text/plain", "");
    });

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

        // Add primary key for backward compatibility (first location)
        if (getLocationCount() > 0) {
            JsonDocument primaryDoc;
            weatherToJson(getWeather(0), primaryDoc);
            doc["primary"] = primaryDoc;
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

        // Carousel items
        JsonArray carouselArray = doc["carousel"].to<JsonArray>();
        for (uint8_t i = 0; i < getCarouselCount(); i++) {
            const CarouselItem& item = getCarouselItem(i);
            JsonObject c = carouselArray.add<JsonObject>();
            c["type"] = item.type;
            c["dataIndex"] = item.dataIndex;
        }

        // Countdown events
        JsonArray countdownArray = doc["countdowns"].to<JsonArray>();
        for (uint8_t i = 0; i < getCountdownCount(); i++) {
            const CountdownEvent& cd = getCountdown(i);
            JsonObject c = countdownArray.add<JsonObject>();
            c["type"] = cd.type;
            c["month"] = cd.month;
            c["day"] = cd.day;
            c["title"] = cd.title;
        }

        // Custom screens (multiple)
        JsonArray customArray = doc["customScreens"].to<JsonArray>();
        for (uint8_t i = 0; i < getCustomScreenCount(); i++) {
            const CustomScreenConfig& cs = getCustomScreenConfig(i);
            JsonObject c = customArray.add<JsonObject>();
            c["header"] = cs.header;
            c["body"] = cs.body;
            c["footer"] = cs.footer;
        }

        // Metadata
        doc["locationCount"] = getLocationCount();
        doc["maxLocations"] = MAX_WEATHER_LOCATIONS;

        // Display settings (both flat and nested for compatibility)
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

        // Display settings as nested object for new admin UI
        JsonObject display = doc["display"].to<JsonObject>();
        display["unit"] = getUseCelsius() ? "c" : "f";
        display["cycle"] = getScreenCycleTime();
        display["brightness"] = getBrightness();

        // Custom screen settings (legacy - single screen)
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

            // Note: Allow empty locations if the carousel has countdown/custom screens only
            // (For backward compat, we still require at least one location if carousel isn't specified)
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

        // Update display settings (handle nested object from new admin UI)
        if (doc["display"].is<JsonObject>()) {
            JsonObject display = doc["display"];
            if (display["unit"].is<const char*>()) {
                const char* unit = display["unit"];
                setUseCelsius(unit && strcmp(unit, "c") == 0);
            }
            if (display["cycle"].is<int>()) {
                setScreenCycleTime(display["cycle"] | 10);
            }
            if (display["brightness"].is<int>()) {
                setBrightness(display["brightness"] | 50);
            }
        }

        // Also handle flat format for backward compatibility
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

        // Custom screen settings (legacy - single screen)
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

        // Countdown events (new carousel system)
        if (doc["countdowns"].is<JsonArray>()) {
            JsonArray cdArray = doc["countdowns"].as<JsonArray>();
            // Clear existing countdowns by removing them one by one
            while (getCountdownCount() > 0) {
                removeCountdown(0);
            }
            for (JsonObject cd : cdArray) {
                uint8_t type = cd["type"] | 0;
                uint8_t month = cd["month"] | 1;
                uint8_t day = cd["day"] | 1;
                const char* title = cd["title"];
                addCountdown(type, month, day, title ? title : "");
            }
            Serial.printf("[API] Updated %d countdowns\n", getCountdownCount());
        }

        // Custom screens (new carousel system - multiple screens)
        if (doc["customScreens"].is<JsonArray>()) {
            JsonArray csArray = doc["customScreens"].as<JsonArray>();
            // Clear existing custom screens
            while (getCustomScreenCount() > 0) {
                removeCustomScreenConfig(0);
            }
            for (JsonObject cs : csArray) {
                const char* header = cs["header"];
                const char* body = cs["body"];
                const char* footer = cs["footer"];
                addCustomScreenConfig(
                    header ? header : "",
                    body ? body : "",
                    footer ? footer : ""
                );
            }
            Serial.printf("[API] Updated %d custom screens\n", getCustomScreenCount());
        }

        // Carousel order (new carousel system)
        if (doc["carousel"].is<JsonArray>()) {
            JsonArray carouselArray = doc["carousel"].as<JsonArray>();
            CarouselItem items[MAX_CAROUSEL_ITEMS];
            uint8_t count = 0;
            for (JsonObject c : carouselArray) {
                if (count >= MAX_CAROUSEL_ITEMS) break;
                items[count].type = c["type"] | 0;
                items[count].dataIndex = c["dataIndex"] | 0;
                count++;
            }
            setCarousel(items, count);
            Serial.printf("[API] Updated carousel with %d items\n", count);
        }

        // Save and refresh weather
        saveWeatherConfig();
        forceWeatherUpdate();

        server.send(200, "application/json", "{\"success\":true,\"message\":\"Config saved\"}");
    });

    // Themes API - GET returns all themes, POST updates custom theme
    server.on("/api/themes", HTTP_GET, []() {
        JsonDocument doc;

        doc["activeTheme"] = getActiveTheme();
        doc["themeMode"] = getThemeMode();

        // List all themes
        JsonArray themes = doc["themes"].to<JsonArray>();

        // Built-in: Classic
        JsonObject classic = themes.add<JsonObject>();
        classic["name"] = "Classic";
        classic["index"] = THEME_CLASSIC;
        classic["builtin"] = true;

        // Built-in: Sunset
        JsonObject sunset = themes.add<JsonObject>();
        sunset["name"] = "Sunset";
        sunset["index"] = THEME_SUNSET;
        sunset["builtin"] = true;

        // User: Custom
        JsonObject custom = themes.add<JsonObject>();
        custom["name"] = "Custom";
        custom["index"] = THEME_CUSTOM;
        custom["builtin"] = false;

        // Include custom theme colors for editing
        const ThemeColors& darkColors = getCustomThemeDark();
        JsonObject dark = custom["dark"].to<JsonObject>();
        dark["bg"] = darkColors.bg;
        dark["card"] = darkColors.card;
        dark["text"] = darkColors.text;
        dark["cyan"] = darkColors.cyan;
        dark["orange"] = darkColors.orange;
        dark["blue"] = darkColors.blue;
        dark["gray"] = darkColors.gray;

        const ThemeColors& lightColors = getCustomThemeLight();
        JsonObject light = custom["light"].to<JsonObject>();
        light["bg"] = lightColors.bg;
        light["card"] = lightColors.card;
        light["text"] = lightColors.text;
        light["cyan"] = lightColors.cyan;
        light["orange"] = lightColors.orange;
        light["blue"] = lightColors.blue;
        light["gray"] = lightColors.gray;

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    server.on("/api/themes", HTTP_POST, []() {
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

        // Set active theme
        if (doc["activeTheme"].is<int>()) {
            setActiveTheme(doc["activeTheme"] | 0);
        }

        // Set theme mode
        if (doc["themeMode"].is<int>()) {
            setThemeMode(doc["themeMode"] | 0);
        }

        // Update custom theme colors
        if (doc["custom"].is<JsonObject>()) {
            JsonObject custom = doc["custom"];
            ThemeColors dark, light;

            // Load current defaults
            dark = getCustomThemeDark();
            light = getCustomThemeLight();

            // Update dark colors
            if (custom["dark"].is<JsonObject>()) {
                JsonObject d = custom["dark"];
                if (d["bg"].is<int>()) dark.bg = d["bg"];
                if (d["card"].is<int>()) dark.card = d["card"];
                if (d["text"].is<int>()) dark.text = d["text"];
                if (d["cyan"].is<int>()) dark.cyan = d["cyan"];
                if (d["orange"].is<int>()) dark.orange = d["orange"];
                if (d["blue"].is<int>()) dark.blue = d["blue"];
                if (d["gray"].is<int>()) dark.gray = d["gray"];
            }

            // Update light colors
            if (custom["light"].is<JsonObject>()) {
                JsonObject l = custom["light"];
                if (l["bg"].is<int>()) light.bg = l["bg"];
                if (l["card"].is<int>()) light.card = l["card"];
                if (l["text"].is<int>()) light.text = l["text"];
                if (l["cyan"].is<int>()) light.cyan = l["cyan"];
                if (l["orange"].is<int>()) light.orange = l["orange"];
                if (l["blue"].is<int>()) light.blue = l["blue"];
                if (l["gray"].is<int>()) light.gray = l["gray"];
            }

            updateCustomTheme(dark, light);
        }

        // Reset custom theme to defaults
        if (doc["resetCustom"].is<bool>() && doc["resetCustom"]) {
            resetCustomTheme();
        }

        server.send(200, "application/json", "{\"success\":true,\"message\":\"Theme saved\"}");
    });

    // Admin page - minimal location config
    server.on("/admin", HTTP_GET, handleAdmin);

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

    // Force admin HTML reprovision - deletes version file to trigger refresh on reboot
    server.on("/api/reprovision", HTTP_GET, []() {
        // Delete the version file to force reprovisioning
        LittleFS.remove("/admin.version");
        LittleFS.remove("/admin.html.gz");
        Serial.println(F("[ADMIN] Admin files deleted, will reprovision on reboot"));
        server.send(200, "application/json",
            "{\"success\":true,\"message\":\"Admin files cleared. Rebooting to reprovision...\"}");
        delay(500);
        ESP.restart();
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
 * Handle admin page - serves new Carousel UI from LittleFS
 * Falls back to legacy embedded HTML if file not found
 */
void handleAdmin() {
    const char* HTML_FILE = "/admin.html.gz";

    // Try to serve from LittleFS first (gzipped)
    // Note: streamFile() auto-adds Content-Encoding:gzip for .gz files
    if (LittleFS.exists(HTML_FILE)) {
        File f = LittleFS.open(HTML_FILE, "r");
        if (f) {
            size_t fileSize = f.size();
            server.streamFile(f, "text/html");
            f.close();
            Serial.printf("[ADMIN] Served %s (%u bytes gzipped)\n", HTML_FILE, fileSize);
            return;
        }
    }

    // If we get here, admin.html.gz is missing - try to re-provision
    Serial.println(F("[ADMIN] File missing, attempting re-provision..."));
    provisionAdminHtml();

    // Try again after provisioning
    if (LittleFS.exists(HTML_FILE)) {
        File f = LittleFS.open(HTML_FILE, "r");
        if (f) {
            size_t fileSize = f.size();
            server.streamFile(f, "text/html");
            f.close();
            Serial.printf("[ADMIN] Served %s after re-provision (%u bytes)\n", HTML_FILE, fileSize);
            return;
        }
    }

    // If still failing, show error page with reboot option
    Serial.println(F("[ADMIN] Re-provision failed, showing error page"));
    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Admin Error</title><style>"
        "body{font-family:sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:40px;text-align:center}"
        ".card{background:#2a2a4e;border-radius:10px;padding:30px;max-width:400px;margin:50px auto}"
        "h2{color:#ff6b35}p{color:#aaa;margin:20px 0}"
        "button{background:#00d4ff;color:#1a1a2e;border:none;padding:15px 30px;border-radius:6px;cursor:pointer;margin:10px}"
        "button:hover{background:#00a8cc}button.warn{background:#ff6b35}"
        "</style></head><body><div class='card'>"
        "<h2>Admin Page Error</h2>"
        "<p>The admin interface could not be loaded. This may indicate a file system issue.</p>"
        "<button onclick=\"location.href='/reboot'\">Reboot Device</button>"
        "<button class='warn' onclick=\"location.href='/api/safemode'\">Enter Safe Mode</button>"
        "</div></body></html>");
    server.send(500, "text/html", html);
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
