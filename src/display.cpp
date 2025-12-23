/**
 * EpicWeatherBox Firmware - Display Driver Implementation
 *
 * Pixel art weather display with 10-second cycling between locations.
 * Each location shows 3 screens: Current, Days 1-3 forecast, Days 4-6 forecast.
 */

#include "display.h"
#include "config.h"
#include "weather_icons_rgb565.h"
#include <NTPClient.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>

// External NTP client (from main.cpp)
extern NTPClient timeClient;

// =============================================================================
// TFT DISPLAY INSTANCE
// =============================================================================

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite sprite = TFT_eSprite(&tft);  // Double buffering sprite

// =============================================================================
// STATE VARIABLES
// =============================================================================

static int currentLocationIndex = 0;
static ScreenType currentScreen = SCREEN_CURRENT_WEATHER;
static unsigned long lastScreenChange = 0;
static unsigned long lastFrameTime = 0;
static int displayBrightness = BRIGHTNESS_DEFAULT;
static bool displayOn = true;
static bool needsRedraw = true;

// =============================================================================
// ANIMATED GIF SUPPORT
// =============================================================================

static AnimatedGIF gif;
static File gifFile;
static int gifOffsetX = 0;  // X offset for centering GIF on screen
static int gifOffsetY = 0;  // Y offset for GIF placement

// GIF file callbacks for AnimatedGIF library
void* GIFOpenFile(const char* fname, int32_t* pSize) {
    gifFile = LittleFS.open(fname, "r");
    if (!gifFile) {
        Serial.printf("[GIF] Failed to open: %s\n", fname);
        return nullptr;
    }
    *pSize = gifFile.size();
    Serial.printf("[GIF] Opened %s, size: %d bytes\n", fname, *pSize);
    return &gifFile;
}

void GIFCloseFile(void* pHandle) {
    if (gifFile) {
        gifFile.close();
    }
}

int32_t GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    int32_t iBytesRead = 0;
    if (gifFile) {
        iBytesRead = gifFile.read(pBuf, iLen);
    }
    return iBytesRead;
}

int32_t GIFSeekFile(GIFFILE* pFile, int32_t iPosition) {
    if (gifFile) {
        gifFile.seek(iPosition);
        return iPosition;
    }
    return -1;
}

// Draw callback - called for each line of each frame
void GIFDraw(GIFDRAW* pDraw) {
    uint8_t* s;
    uint16_t* d;
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth + pDraw->iX > SCREEN_WIDTH)
        iWidth = SCREEN_WIDTH - pDraw->iX;

    if (iWidth <= 0) return;

    // Calculate actual y position with offset
    y = pDraw->iY + pDraw->y + gifOffsetY;
    if (y < 0 || y >= SCREEN_HEIGHT) return;

    s = pDraw->pPixels;

    // Handle transparent pixels if needed
    if (pDraw->ucDisposalMethod == 2) {
        // Restore to background - draw transparent pixels
        for (x = 0; x < iWidth; x++) {
            if (s[x] == pDraw->ucTransparent) {
                // Skip transparent pixels
                continue;
            }
        }
    }

    // Draw the line directly to TFT using palette lookup
    uint16_t lineBuffer[240];  // Max width line buffer
    uint16_t* pPalette = pDraw->pPalette;

    for (x = 0; x < iWidth; x++) {
        if (pDraw->ucHasTransparency && s[x] == pDraw->ucTransparent) {
            // For transparent pixels, keep existing - read from TFT would be slow
            // so we just draw background color
            lineBuffer[x] = 0x0841;  // Dark background
        } else {
            lineBuffer[x] = pPalette[s[x]];
        }
    }

    // Draw the line at the correct position
    int drawX = pDraw->iX + gifOffsetX;
    if (drawX < 0) {
        lineBuffer[0] = lineBuffer[-drawX];  // Adjust for negative offset
        drawX = 0;
    }

    tft.pushImage(drawX, y, iWidth, 1, lineBuffer);
}

// =============================================================================
// RGB565 COLOR WEATHER ICONS (32x32)
// Icons are defined in weather_icons_rgb565.h with direct WMO code mapping
// =============================================================================

// Note: getIconForWMOCode() is defined in weather_icons_rgb565.h

// =============================================================================
// INITIALIZATION
// =============================================================================

void initDisplay() {
    Serial.println(F("[DISPLAY] Initializing TFT..."));

    // Initialize TFT
    tft.init();
    tft.setRotation(0);  // Portrait mode
    tft.fillScreen(TFT_BLACK);

    // Setup backlight PWM
    pinMode(TFT_BL_PIN, OUTPUT);
    analogWriteRange(100);
    analogWriteFreq(1000);
    setDisplayBrightness(displayBrightness);

    // Create sprite for double buffering (full screen)
    sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    sprite.setSwapBytes(true);

    Serial.println(F("[DISPLAY] TFT initialized"));
    Serial.printf("[DISPLAY] Resolution: %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);

    // Show boot screen
    drawBootScreen();
    delay(2500);  // Display boot screen for 2.5 seconds

    // Try to play boot GIF if uploaded
    playBootGif();

    // Draw initial screen
    needsRedraw = true;
    lastScreenChange = millis();
}

// =============================================================================
// MAIN UPDATE LOOP
// =============================================================================

void updateDisplay() {
    if (!displayOn) return;

    unsigned long now = millis();

    // Check if it's time to change screen (use configurable cycle time)
    if (now - lastScreenChange >= (unsigned long)(getScreenCycleTime() * 1000)) {
        lastScreenChange = now;
        needsRedraw = true;

        // Stop loop GIF if we're switching away from GIF screen
        if (currentScreen == SCREEN_GIF_ANIMATION) {
            stopLoopGif();
        }

        // Determine how many screens to cycle through
        bool gifEnabled = getGifScreenEnabled() && gifFileExists("/screen.gif");
        bool forecastEnabled = getShowForecast();

        if (!forecastEnabled) {
            // Forecast disabled - stay on current weather, cycle locations only
            currentScreen = SCREEN_CURRENT_WEATHER;
            // Cycle through locations
            int locationCount = getLocationCount();
            if (locationCount > 1) {
                currentLocationIndex = (currentLocationIndex + 1) % locationCount;
            }
        } else {
            // Forecast enabled - cycle through all screens
            // Advance to next screen
            currentScreen = (ScreenType)((currentScreen + 1) % SCREEN_TYPE_COUNT);

            // Skip GIF screen if not enabled
            if (currentScreen == SCREEN_GIF_ANIMATION && !gifEnabled) {
                currentScreen = SCREEN_CURRENT_WEATHER;
            }

            // If we looped back to current weather, move to next location
            if (currentScreen == SCREEN_CURRENT_WEATHER) {
                int locationCount = getLocationCount();
                if (locationCount > 0) {
                    currentLocationIndex = (currentLocationIndex + 1) % locationCount;
                }
            }
        }

        Serial.printf("[DISPLAY] Screen: %d, Location: %d\n", currentScreen, currentLocationIndex);
    }

    // Redraw if needed
    if (needsRedraw) {
        needsRedraw = false;

        // Get background color based on day/night
        const WeatherData& weather = getWeather(currentLocationIndex);
        uint16_t bgColor = weather.current.isDay ? COLOR_BG_DAY : COLOR_BG_NIGHT;

        sprite.fillSprite(bgColor);

        switch (currentScreen) {
            case SCREEN_CURRENT_WEATHER:
                drawCurrentWeatherScreen(currentLocationIndex);
                break;
            case SCREEN_FORECAST_1_3:
                drawForecastScreen(currentLocationIndex, 0);
                break;
            case SCREEN_FORECAST_4_6:
                drawForecastScreen(currentLocationIndex, 3);
                break;
            case SCREEN_GIF_ANIMATION:
                drawGifScreen();
                return;  // drawGifScreen pushes to display itself
        }

        // Push sprite to display
        sprite.pushSprite(0, 0);
    }
}

void refreshDisplay() {
    needsRedraw = true;
}

// =============================================================================
// BRIGHTNESS CONTROL
// =============================================================================

void setDisplayBrightness(int brightness) {
    displayBrightness = constrain(brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    analogWrite(TFT_BL_PIN, displayBrightness);
}

int getDisplayBrightness() {
    return displayBrightness;
}

void setDisplayOn(bool on) {
    displayOn = on;
    if (on) {
        setDisplayBrightness(displayBrightness);
        needsRedraw = true;
    } else {
        analogWrite(TFT_BL_PIN, 0);
    }
}

bool isDisplayOn() {
    return displayOn;
}

// =============================================================================
// CURRENT WEATHER SCREEN
// =============================================================================

void drawCurrentWeatherScreen(int locationIndex) {
    const WeatherData& weather = getWeather(locationIndex);
    const WeatherLocation& location = getLocation(locationIndex);

    // Get current time
    unsigned long epochTime = timeClient.getEpochTime();
    int hours = (epochTime % 86400L) / 3600;
    int minutes = (epochTime % 3600) / 60;

    // Draw time at top (large pixel font)
    sprite.setTextColor(COLOR_TEXT_WHITE);
    sprite.setTextDatum(TC_DATUM);

    // Time in big font
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hours, minutes);
    sprite.setTextSize(1);
    sprite.setFreeFont(&FreeSansBold24pt7b);
    sprite.drawString(timeStr, SCREEN_WIDTH / 2, 15);

    // Location name below time
    sprite.setFreeFont(&FreeSans12pt7b);
    sprite.setTextColor(COLOR_TEXT_LIGHT);
    sprite.drawString(location.name, SCREEN_WIDTH / 2, 65);

    // Draw weather icon (centered, large)
    int iconX = (SCREEN_WIDTH - 64) / 2;
    int iconY = 95;
    drawWeatherIcon(weather.current.weatherCode, iconX, iconY, 64);

    // Current temperature (big)
    sprite.setTextDatum(TC_DATUM);
    drawTemperature(weather.current.temperature, SCREEN_WIDTH / 2, 170, 3);

    // Condition text
    sprite.setFreeFont(&FreeSans9pt7b);
    sprite.setTextColor(COLOR_TEXT_LIGHT);
    sprite.drawString(conditionToString(weather.current.condition), SCREEN_WIDTH / 2, 210);

    // Location indicator dots at bottom
    int numLocations = getLocationCount();
    if (numLocations > 1) {
        int dotSpacing = 12;
        int startX = (SCREEN_WIDTH - (numLocations - 1) * dotSpacing) / 2;
        for (int i = 0; i < numLocations; i++) {
            uint16_t dotColor = (i == locationIndex) ? COLOR_TEXT_WHITE : COLOR_TEXT_LIGHT;
            sprite.fillCircle(startX + i * dotSpacing, 232, 3, dotColor);
        }
    }
}

// =============================================================================
// FORECAST SCREEN
// =============================================================================

void drawForecastScreen(int locationIndex, int startDay) {
    const WeatherData& weather = getWeather(locationIndex);
    const WeatherLocation& location = getLocation(locationIndex);

    // Title
    sprite.setTextColor(COLOR_TEXT_WHITE);
    sprite.setTextDatum(TC_DATUM);
    sprite.setFreeFont(&FreeSans12pt7b);

    char title[32];
    if (startDay == 0) {
        snprintf(title, sizeof(title), "%s - Days 1-3", location.name);
    } else {
        snprintf(title, sizeof(title), "%s - Days 4-6", location.name);
    }
    sprite.drawString(title, SCREEN_WIDTH / 2, 10);

    // Draw 3 forecast cards
    int cardWidth = 70;
    int cardHeight = 160;
    int cardSpacing = 8;
    int startX = (SCREEN_WIDTH - 3 * cardWidth - 2 * cardSpacing) / 2;
    int cardY = 45;

    for (int i = 0; i < 3; i++) {
        int dayIndex = startDay + i;
        if (dayIndex >= weather.forecastDays) continue;

        const ForecastDay& day = weather.forecast[dayIndex];
        int cardX = startX + i * (cardWidth + cardSpacing);

        // Draw card background
        drawCard(cardX, cardY, cardWidth, cardHeight, COLOR_CARD_BG);

        // Day name
        sprite.setTextDatum(TC_DATUM);
        sprite.setTextColor(COLOR_TEXT_WHITE);
        sprite.setFreeFont(&FreeSans9pt7b);
        sprite.drawString(day.dayName, cardX + cardWidth / 2, cardY + 8);

        // Weather icon
        int iconX = cardX + (cardWidth - 32) / 2;
        int iconY = cardY + 35;
        drawWeatherIcon(day.weatherCode, iconX, iconY, 32);

        // High temp
        sprite.setTextDatum(TC_DATUM);
        sprite.setTextColor(getTempColor(day.tempMax));
        sprite.setFreeFont(&FreeSans9pt7b);
        String highTemp = formatTemp(day.tempMax);
        sprite.drawString(highTemp, cardX + cardWidth / 2, cardY + 80);

        // Low temp
        sprite.setTextColor(getTempColor(day.tempMin));
        String lowTemp = formatTemp(day.tempMin);
        sprite.drawString(lowTemp, cardX + cardWidth / 2, cardY + 105);

        // Precipitation probability
        if (day.precipitationProb > 0) {
            sprite.setTextColor(COLOR_RAIN);
            sprite.setFreeFont(&FreeMono9pt7b);
            char precip[8];
            snprintf(precip, sizeof(precip), "%d%%", (int)day.precipitationProb);
            sprite.drawString(precip, cardX + cardWidth / 2, cardY + 135);
        }
    }

    // Location indicator dots at bottom
    int numLocations = getLocationCount();
    if (numLocations > 1) {
        int dotSpacing = 12;
        int startXDots = (SCREEN_WIDTH - (numLocations - 1) * dotSpacing) / 2;
        for (int i = 0; i < numLocations; i++) {
            uint16_t dotColor = (i == locationIndex) ? COLOR_TEXT_WHITE : COLOR_TEXT_LIGHT;
            sprite.fillCircle(startXDots + i * dotSpacing, 232, 3, dotColor);
        }
    }
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

void drawWeatherIcon(int wmoCode, int x, int y, int size) {
    Serial.printf("[DISPLAY] Drawing icon for WMO code: %d, size: %d\n", wmoCode, size);
    const uint16_t* icon = getIconForWMOCode(wmoCode);
    if (!icon) {
        Serial.println("[DISPLAY] WARNING: No icon found for WMO code!");
        return;
    }

    if (size == 32) {
        // Draw 32x32 RGB565 icon with transparency (0x0000 = transparent)
        for (int py = 0; py < 32; py++) {
            for (int px = 0; px < 32; px++) {
                uint16_t pixel = pgm_read_word(&icon[py * 32 + px]);
                if (pixel != ICON_TRANSPARENT) {
                    sprite.drawPixel(x + px, y + py, pixel);
                }
            }
        }
    } else if (size == 64) {
        // Scale up 32x32 to 64x64 (2x) with transparency
        for (int py = 0; py < 32; py++) {
            for (int px = 0; px < 32; px++) {
                uint16_t pixel = pgm_read_word(&icon[py * 32 + px]);
                if (pixel != ICON_TRANSPARENT) {
                    // Draw 2x2 block for scaling
                    sprite.fillRect(x + px * 2, y + py * 2, 2, 2, pixel);
                }
            }
        }
    }
}

void drawTemperature(float temp, int x, int y, int size) {
    sprite.setTextColor(getTempColor(temp));
    sprite.setTextDatum(TC_DATUM);

    if (size == 3) {
        sprite.setFreeFont(&FreeSansBold18pt7b);
    } else if (size == 2) {
        sprite.setFreeFont(&FreeSans12pt7b);
    } else {
        sprite.setFreeFont(&FreeSans9pt7b);
    }

    String tempStr = formatTemp(temp);
    sprite.drawString(tempStr, x, y);
}

uint16_t getTempColor(float temp) {
    // Color based on temperature (assuming Celsius or converted)
    if (temp < 0) return COLOR_TEXT_BLUE;
    if (temp < 10) return COLOR_TEXT_CYAN;
    if (temp < 20) return COLOR_TEXT_WHITE;
    if (temp < 30) return COLOR_TEXT_ORANGE;
    return COLOR_TEXT_ORANGE;  // Hot
}

String formatTemp(float temp) {
    bool useCelsius = getUseCelsius();
    if (!useCelsius) {
        temp = temp * 9.0 / 5.0 + 32.0;
    }
    char buf[12];
    snprintf(buf, sizeof(buf), "%.0f%s", temp, useCelsius ? "C" : "F");
    return String(buf);
}

void drawCard(int x, int y, int w, int h, uint16_t color) {
    // Draw rounded rectangle (pixel art style - just use regular rect)
    sprite.fillRoundRect(x, y, w, h, 4, color);
}

// =============================================================================
// BOOT SCREEN
// =============================================================================

void drawBootScreen() {
    Serial.println(F("[DISPLAY] Drawing boot screen..."));

    // Clear to dark background
    tft.fillScreen(0x0841);  // Very dark blue-gray

    // Draw "EpicWeatherBox" title centered
    tft.setTextColor(0x07FF);  // Cyan
    tft.setTextDatum(MC_DATUM);  // Middle-center
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.drawString("Epic", SCREEN_WIDTH / 2, 85);

    tft.setTextColor(0xFFFF);  // White
    tft.drawString("WeatherBox", SCREEN_WIDTH / 2, 125);

    // Draw version below
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(0x8410);  // Gray
    tft.drawString("v" FIRMWARE_VERSION, SCREEN_WIDTH / 2, 165);

    // Status text at bottom (will be updated with IP)
    tft.setFreeFont(NULL);  // Default font
    tft.setTextSize(1);
    tft.setTextColor(0x4208);  // Dark gray
    tft.drawString("Connecting...", SCREEN_WIDTH / 2, 228);

    Serial.println(F("[DISPLAY] Boot screen displayed"));
}

// Update the boot screen status text (bottom line)
void updateBootStatus(const char* status) {
    // Clear the status area
    tft.fillRect(0, 215, SCREEN_WIDTH, 25, 0x0841);

    // Draw new status
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(0x8410);  // Brighter gray for IP
    tft.setTextDatum(MC_DATUM);
    tft.drawString(status, SCREEN_WIDTH / 2, 228);
}

// =============================================================================
// GIF FILE HELPERS
// =============================================================================

// File paths for GIFs
#define BOOT_GIF_PATH "/boot.gif"
#define SCREEN_GIF_PATH "/screen.gif"

bool gifFileExists(const char* path) {
    return LittleFS.exists(path);
}

// GIF size limits to prevent memory issues
#define MAX_GIF_FILE_SIZE (512 * 1024)   // 512KB max file size
#define MAX_GIF_DIMENSION 240             // Max width/height (screen size)

// Boot crash detection file
#define BOOT_CRASH_FLAG "/boot_crash.flag"

// Check if a GIF file is valid and safe to play
bool validateGif(const char* path) {
    if (!LittleFS.exists(path)) {
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[GIF] Cannot open %s for validation\n", path);
        return false;
    }

    // Check file size
    size_t fileSize = f.size();
    if (fileSize > MAX_GIF_FILE_SIZE) {
        Serial.printf("[GIF] %s too large: %d bytes (max %d)\n", path, fileSize, MAX_GIF_FILE_SIZE);
        f.close();
        return false;
    }

    // Check GIF header magic bytes
    uint8_t header[6];
    if (f.read(header, 6) != 6) {
        Serial.printf("[GIF] %s: Cannot read header\n", path);
        f.close();
        return false;
    }
    f.close();

    // Valid GIF starts with "GIF87a" or "GIF89a"
    if (memcmp(header, "GIF87a", 6) != 0 && memcmp(header, "GIF89a", 6) != 0) {
        Serial.printf("[GIF] %s: Invalid GIF header\n", path);
        return false;
    }

    // Try to open with AnimatedGIF to validate dimensions
    gif.begin(GIF_PALETTE_RGB565_BE);
    if (gif.open(path, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
        int w = gif.getCanvasWidth();
        int h = gif.getCanvasHeight();
        gif.close();

        if (w > MAX_GIF_DIMENSION || h > MAX_GIF_DIMENSION) {
            Serial.printf("[GIF] %s: Dimensions too large: %dx%d (max %d)\n", path, w, h, MAX_GIF_DIMENSION);
            return false;
        }

        Serial.printf("[GIF] %s validated: %dx%d, %d bytes\n", path, w, h, fileSize);
        return true;
    } else {
        Serial.printf("[GIF] %s: Failed to decode, error: %d\n", path, gif.getLastError());
        return false;
    }
}

// Set boot crash flag before attempting GIF playback
void setBootCrashFlag() {
    File f = LittleFS.open(BOOT_CRASH_FLAG, "w");
    if (f) {
        f.write('1');
        f.close();
    }
}

// Clear boot crash flag after successful GIF playback
void clearBootCrashFlag() {
    if (LittleFS.exists(BOOT_CRASH_FLAG)) {
        LittleFS.remove(BOOT_CRASH_FLAG);
    }
}

// Check if last boot crashed during GIF playback
bool checkBootCrashFlag() {
    return LittleFS.exists(BOOT_CRASH_FLAG);
}

// Delete problematic GIF that caused boot crash
void deleteProblematicGif(const char* path) {
    if (LittleFS.exists(path)) {
        LittleFS.remove(path);
        Serial.printf("[GIF] Deleted problematic GIF: %s\n", path);
    }
}

bool playBootGif() {
    // Check if boot GIF exists
    if (!gifFileExists(BOOT_GIF_PATH)) {
        Serial.println(F("[DISPLAY] No boot GIF found"));
        return false;
    }

    // Check for previous boot crash - if so, delete the GIF and skip
    if (checkBootCrashFlag()) {
        Serial.println(F("[GIF] Previous boot crashed! Deleting boot GIF to prevent boot loop."));
        deleteProblematicGif(BOOT_GIF_PATH);
        clearBootCrashFlag();
        return false;
    }

    // Validate GIF before playing
    if (!validateGif(BOOT_GIF_PATH)) {
        Serial.println(F("[GIF] Boot GIF validation failed, deleting"));
        deleteProblematicGif(BOOT_GIF_PATH);
        return false;
    }

    Serial.println(F("[DISPLAY] Playing boot GIF..."));

    // Set crash flag before attempting playback
    setBootCrashFlag();

    // Initialize the GIF decoder
    gif.begin(GIF_PALETTE_RGB565_BE);  // Big-endian for TFT_eSPI

    if (gif.open(BOOT_GIF_PATH, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
        Serial.printf("[GIF] Boot GIF: %dx%d\n", gif.getCanvasWidth(), gif.getCanvasHeight());

        // Center the GIF on screen
        gifOffsetX = (SCREEN_WIDTH - gif.getCanvasWidth()) / 2;
        gifOffsetY = (SCREEN_HEIGHT - gif.getCanvasHeight()) / 2;

        // Clear screen with dark background
        tft.fillScreen(0x0841);

        // Play through all frames once (or loop for a set time)
        unsigned long startTime = millis();
        unsigned long maxPlayTime = 5000;  // Max 5 seconds for boot GIF

        while (millis() - startTime < maxPlayTime) {
            int frameDelay = gif.playFrame(true, nullptr);
            if (frameDelay < 0) {
                // End of animation - for boot, we only play once
                break;
            }
            // Small yield for WiFi/system
            yield();
        }

        gif.close();

        // Clear crash flag - we made it through successfully!
        clearBootCrashFlag();

        Serial.println(F("[GIF] Boot GIF complete"));
        return true;
    } else {
        Serial.printf("[GIF] Failed to open boot GIF, error: %d\n", gif.getLastError());
        clearBootCrashFlag();
        return false;
    }
}

// Static state for loop GIF playback
static bool loopGifActive = false;
static unsigned long loopGifStartTime = 0;
static bool screenGifValidated = false;
static bool screenGifValid = false;

void drawGifScreen() {
    // Apply timezone offset from primary location
    const WeatherData& weather = getWeather(0);
    long localEpoch = timeClient.getEpochTime() + weather.utcOffsetSeconds;
    int hours = (localEpoch % 86400L) / 3600;
    int minutes = (localEpoch % 3600) / 60;

    // Check if screen GIF exists and is valid (uses module-level statics)
    if (!screenGifValidated) {
        screenGifValid = gifFileExists(SCREEN_GIF_PATH) && validateGif(SCREEN_GIF_PATH);
        screenGifValidated = true;
        if (!screenGifValid && gifFileExists(SCREEN_GIF_PATH)) {
            // Invalid GIF - delete it
            deleteProblematicGif(SCREEN_GIF_PATH);
        }
    }

    if (!screenGifValid) {
        // No GIF or invalid - show placeholder with time header
        tft.fillScreen(0x0841);  // Dark background

        // Draw time header
        tft.setTextColor(COLOR_TEXT_WHITE);
        tft.setTextDatum(TC_DATUM);
        tft.setFreeFont(&FreeSansBold18pt7b);

        char timeStr[12];
        int h12 = hours % 12;
        if (h12 == 0) h12 = 12;
        const char* ampm = (hours < 12) ? "AM" : "PM";
        snprintf(timeStr, sizeof(timeStr), "%d:%02d %s", h12, minutes, ampm);
        tft.drawString(timeStr, SCREEN_WIDTH / 2, 15);

        // Draw separator line
        tft.drawFastHLine(20, 55, SCREEN_WIDTH - 40, 0x2104);

        // No GIF placeholder
        tft.setTextColor(0x4208);
        tft.setFreeFont(&FreeSans9pt7b);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No GIF uploaded", SCREEN_WIDTH / 2, 140);
        tft.drawString("Upload via Admin panel", SCREEN_WIDTH / 2, 160);
        return;
    }

    // Start GIF playback if not already active
    if (!loopGifActive) {
        gif.begin(GIF_PALETTE_RGB565_BE);
        if (gif.open(SCREEN_GIF_PATH, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
            Serial.printf("[GIF] Screen GIF: %dx%d\n", gif.getCanvasWidth(), gif.getCanvasHeight());

            // Position GIF in lower portion of screen (below time header)
            int gifHeight = gif.getCanvasHeight();
            int gifWidth = gif.getCanvasWidth();
            gifOffsetX = (SCREEN_WIDTH - gifWidth) / 2;
            gifOffsetY = 60 + (180 - gifHeight) / 2;  // Center in GIF area (y=60, h=180)

            // Clear screen and draw time header once
            tft.fillScreen(0x0841);

            // Draw time header
            tft.setTextColor(COLOR_TEXT_WHITE);
            tft.setTextDatum(TC_DATUM);
            tft.setFreeFont(&FreeSansBold18pt7b);

            char timeStr[12];
            int h12 = hours % 12;
            if (h12 == 0) h12 = 12;
            const char* ampm = (hours < 12) ? "AM" : "PM";
            snprintf(timeStr, sizeof(timeStr), "%d:%02d %s", h12, minutes, ampm);
            tft.drawString(timeStr, SCREEN_WIDTH / 2, 15);

            // Draw separator line
            tft.drawFastHLine(20, 55, SCREEN_WIDTH - 40, 0x2104);

            loopGifActive = true;
            loopGifStartTime = millis();
        } else {
            Serial.printf("[GIF] Failed to open screen GIF, error: %d\n", gif.getLastError());
            return;
        }
    }

    // Play one frame of the GIF
    if (loopGifActive) {
        int frameDelay = gif.playFrame(true, nullptr);
        if (frameDelay < 0) {
            // End of animation - reset to loop
            gif.reset();
        }
    }
}

void stopLoopGif() {
    if (loopGifActive) {
        gif.close();
        loopGifActive = false;
        Serial.println(F("[GIF] Loop GIF stopped"));
    }
}

// Call this after uploading a new GIF to force re-validation
void invalidateGifCache() {
    screenGifValidated = false;
    screenGifValid = false;
    Serial.println(F("[GIF] GIF cache invalidated"));
}
