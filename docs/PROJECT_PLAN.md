# SmallTV-Ultra Custom Firmware - Project Plan

## Project Summary

| Item | Details |
|------|---------|
| **Goal** | Custom open-source firmware for GeekMagic SmallTV-Ultra |
| **MCU** | ESP8266 (ESP12E) |
| **Display** | ST7789T3, 240x240, SPI |
| **Key Features** | 7-day weather, dual location, OTA updates |
| **Status** | Planning / Pre-Development |

---

## Phase Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Phase 0: Development Environment Setup                           [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 1: Minimal Safe Firmware (OTA-capable)                     [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 2: Display Driver                                          [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 3: WiFi & Web Server                                       [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 4: Time & NTP                                              [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 5: Weather API Integration                                 [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 6: Dual Location Support                                   [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 7: Web Configuration Interface                             [TODO] │
├─────────────────────────────────────────────────────────────────────────┤
│ Phase 8: Polish & Optimization                                   [TODO] │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Phase 0: Development Environment Setup

**Goal**: Prepare all tools and test hardware before touching the real device.

### Hardware Acquisition
- [ ] Order NodeMCU ESP8266 development board
- [ ] Order ST7789 240x240 SPI display module (for testing)
- [ ] Order jumper wires / breadboard
- [ ] Verify USB serial drivers installed

### Software Setup
- [ ] Install PlatformIO extension in VSCode
- [ ] Install ESP8266 platform: `pio platform install espressif8266`
- [ ] Install esptool: `pip install esptool`
- [ ] Clone/setup project repository

### Verification
- [ ] Build project: `pio run` (should compile without errors)
- [ ] Verify library downloads complete
- [ ] Document any build issues and resolutions

---

## Phase 1: Minimal Safe Firmware (CRITICAL)

**Goal**: Create the simplest possible firmware that includes OTA capability. This is the most critical phase - if we flash firmware without working OTA, we brick the device.

### Core Requirements
- [x] WiFi AP mode on boot (SSID: `SmallTV-Ultra`)
- [x] WiFi client mode with saved credentials
- [x] ArduinoOTA enabled and working
- [x] Web-based OTA update page (`/update`)
- [x] Basic serial debug output
- [x] Watchdog timer enabled

### Files to Create/Modify
- [x] `src/main.cpp` - Minimal boot sequence
- [x] `src/ota.h` / `src/ota.cpp` - OTA update handling
- [x] `src/config.h` - Pin definitions (already exists)

### Test Procedure (on NodeMCU)
1. [ ] Flash via USB: `pio run --target upload`
2. [ ] Connect to `SmallTV-Ultra` AP
3. [ ] Verify captive portal or web page loads
4. [ ] Connect device to home WiFi
5. [ ] Verify ArduinoOTA discovery: `pio device list`
6. [ ] Flash update via OTA: `pio run --target upload --upload-port <IP>`
7. [ ] Repeat OTA flash 3+ times to confirm reliability

### Success Criteria
- [x] Device boots and creates AP (code complete, needs hardware test)
- [x] Device connects to WiFi (code complete, needs hardware test)
- [ ] OTA update works reliably (needs hardware test)
- [ ] Device survives power cycles (needs hardware test)

---

## Phase 2: Display Driver

**Goal**: Get the ST7789 display working with TFT_eSPI library.

### Configuration
- [ ] Verify TFT_eSPI build flags in `platformio.ini`
- [ ] Test SPI mode (mode 0 vs mode 3)
- [ ] Test SPI frequency (start low, increase)

### Implementation
- [ ] Create `src/display.h` - Display interface
- [ ] Create `src/display.cpp` - TFT_eSPI wrapper
- [ ] Implement `initDisplay()` - Initialize with correct settings
- [ ] Implement `setBacklight(uint8_t level)` - PWM on GPIO5
- [ ] Implement `testPattern()` - Color bars for verification
- [ ] Implement `drawText(x, y, text, color, size)`
- [ ] Implement `fillScreen(color)`

### Pin Verification (from hardware RE)
```cpp
#define TFT_CS    4   // Chip select
#define TFT_DC    0   // Data/Command
#define TFT_RST   2   // Reset
#define TFT_BL    5   // Backlight (PWM)
#define TFT_MOSI  13  // SPI data
#define TFT_SCLK  14  // SPI clock
```

### Test Procedure
1. [ ] Run color bar test pattern
2. [ ] Test backlight brightness levels (0-255)
3. [ ] Test text rendering at multiple sizes
4. [ ] Verify 240x240 pixel boundaries
5. [ ] Check for screen tearing or artifacts

### Success Criteria
- [ ] Display shows correct colors (no RGB swap issues)
- [ ] Text renders cleanly
- [ ] Backlight control works
- [ ] No visual artifacts

---

## Phase 3: WiFi & Web Server

**Goal**: Robust WiFi connectivity with configuration portal and basic web server.

### WiFi Manager
- [ ] Integrate tzapu/WiFiManager library
- [ ] Configure captive portal appearance
- [ ] Add custom parameters (location name, coordinates)
- [ ] Handle connection failures gracefully
- [ ] Store credentials in SPIFFS

### Web Server (ESPAsyncWebServer)
- [ ] Create `src/web_server.h` / `src/web_server.cpp`
- [ ] Implement base routes:
  - [ ] `GET /` - Main status page
  - [ ] `GET /api/status` - JSON device status
  - [ ] `GET /update` - OTA update page
  - [ ] `POST /update` - OTA file upload handler
  - [ ] `GET /reboot` - Reboot device
  - [ ] `GET /reset` - Factory reset (clear WiFi)
- [ ] Serve gzipped static files from SPIFFS
- [ ] Implement `/set` endpoint (original firmware compatibility)

### SPIFFS Setup
- [ ] Create `data/` directory structure
- [ ] Create minimal `data/index.html`
- [ ] Create `data/style.css`
- [ ] Build filesystem: `pio run --target buildfs`
- [ ] Upload filesystem: `pio run --target uploadfs`

### Test Procedure
1. [ ] First boot - verify AP mode and captive portal
2. [ ] Configure WiFi - verify connection
3. [ ] Access web interface from browser
4. [ ] Test all API endpoints
5. [ ] Test OTA update via web page
6. [ ] Test factory reset and reconfiguration

### Success Criteria
- [ ] Clean captive portal UX
- [ ] Stable WiFi connection
- [ ] Web interface accessible
- [ ] OTA updates work via web page

---

## Phase 4: Time & NTP

**Goal**: Accurate time display with timezone support.

### Implementation
- [ ] Create `src/time_manager.h` / `src/time_manager.cpp`
- [ ] Implement NTP sync with multiple servers
- [ ] Implement timezone handling (POSIX format)
- [ ] Implement DST rules
- [ ] Add fallback for offline operation

### Functions
```cpp
void initTimeManager();
void updateTime();              // Call from loop()
time_t getLocalTime();
String getTimeString();         // "12:34" or "12:34:56"
String getDateString();         // "Dec 18"
String getDayOfWeek();          // "Wednesday"
bool isTimeValid();
```

### Configuration
- [ ] Add timezone to config.json
- [ ] Add 12/24 hour preference
- [ ] Add NTP server preference

### Display Integration
- [ ] Create clock display layout
- [ ] Implement time update animation (colon blink)
- [ ] Add date display

### Test Procedure
1. [ ] Verify NTP sync on boot
2. [ ] Verify correct timezone offset
3. [ ] Verify DST handling (if applicable)
4. [ ] Test offline behavior
5. [ ] Run for 24+ hours, check drift

### Success Criteria
- [ ] Time accurate to within 1 second of reference
- [ ] Timezone correctly applied
- [ ] Survives network outages

---

## Phase 5: Weather API Integration

**Goal**: Fetch and display 7-day weather forecast using Open-Meteo API.

### API Client
- [ ] Create `src/weather.h` / `src/weather.cpp`
- [ ] Implement Open-Meteo API client
- [ ] Use streaming JSON parser (memory efficient)
- [ ] Handle API errors gracefully
- [ ] Implement retry logic

### Data Structures
```cpp
struct CurrentWeather {
    float temperature;
    int weatherCode;
    int humidity;
    float windSpeed;
    bool isDay;
};

struct DailyForecast {
    int weatherCode;
    float tempMax;
    float tempMin;
    float precipitation;
    char dayName[4];
};

struct WeatherData {
    CurrentWeather current;
    DailyForecast daily[7];
    unsigned long lastUpdate;
    bool valid;
};
```

### Weather Code Mapping
- [ ] Create WMO code to icon mapping
- [ ] Create WMO code to text description mapping
- [ ] Create weather icon bitmaps (16x16 and 32x32)

### Display Layouts
- [ ] Current weather (large icon, temp, description)
- [ ] 7-day forecast (row of small icons with temps)
- [ ] Combined clock + weather

### Configuration
- [ ] Location coordinates (lat/lon)
- [ ] Temperature units (C/F)
- [ ] Update interval (default 20 min)

### Test Procedure
1. [ ] Fetch weather for known location
2. [ ] Verify JSON parsing
3. [ ] Verify all 7 days populated
4. [ ] Test with bad coordinates
5. [ ] Test with network outage
6. [ ] Monitor memory during updates

### Success Criteria
- [ ] Weather data fetched reliably
- [ ] 7-day forecast displayed correctly
- [ ] Memory stable over multiple updates
- [ ] Graceful error handling

---

## Phase 6: Dual Location Support

**Goal**: Display weather for two locations.

### Data Structures
```cpp
struct Location {
    char name[32];
    float latitude;
    float longitude;
    char timezone[32];
    bool enabled;
};

// Config stores two locations
Location locations[2];
```

### Implementation
- [ ] Extend weather client for multiple locations
- [ ] Implement location switching logic
- [ ] Create dual-location display layout
- [ ] Add alternating display mode (optional)

### Display Options
- [ ] Split screen (left/right)
- [ ] Alternating (switch every N seconds)
- [ ] Swipe gesture (if hardware supports)

### Configuration
- [ ] Add second location to config.json
- [ ] Add display mode preference
- [ ] Add web interface for second location

### Test Procedure
1. [ ] Configure two locations
2. [ ] Verify both fetch correctly
3. [ ] Verify display layout
4. [ ] Test with one location invalid
5. [ ] Test timezone differences

### Success Criteria
- [ ] Both locations display correctly
- [ ] Smooth switching between locations
- [ ] Independent timezone handling

---

## Phase 7: Web Configuration Interface

**Goal**: Full-featured web interface for device configuration.

### Pages
- [ ] `index.html` - Dashboard / status
- [ ] `settings.html` - Device settings
- [ ] `weather.html` - Weather configuration
- [ ] `network.html` - WiFi settings
- [ ] `update.html` - Firmware update

### Settings to Configure
| Setting | Type | Default |
|---------|------|---------|
| Location 1 Name | string | "Home" |
| Location 1 Lat/Lon | float | 0, 0 |
| Location 2 Name | string | "" |
| Location 2 Lat/Lon | float | 0, 0 |
| Theme | int | 1 |
| Brightness | int | 50 |
| Night Mode Enabled | bool | true |
| Night Mode Start | int | 22 |
| Night Mode End | int | 7 |
| Night Mode Brightness | int | 10 |
| Temperature Units | string | "C" |
| 24 Hour Format | bool | true |
| Update Interval | int | 20 |

### API Endpoints
- [ ] `GET /config.json` - Get current config
- [ ] `POST /config` - Save config
- [ ] `GET /set?key=value` - Quick set (original compatibility)
- [ ] `GET /api/weather` - Current weather data
- [ ] `GET /api/forecast` - 7-day forecast data
- [ ] `GET /v.json` - Version info

### Implementation
- [ ] Design responsive HTML/CSS
- [ ] Implement JavaScript for dynamic updates
- [ ] Gzip compress static files
- [ ] Create config save/load functions

### Test Procedure
1. [ ] Access all pages
2. [ ] Change each setting
3. [ ] Verify persistence after reboot
4. [ ] Test on mobile browser
5. [ ] Verify original firmware compatibility

### Success Criteria
- [ ] All settings configurable via web
- [ ] Changes persist
- [ ] Mobile-friendly

---

## Phase 8: Polish & Optimization

**Goal**: Production-ready firmware.

### Stability
- [ ] Run 7-day stability test
- [ ] Monitor heap fragmentation
- [ ] Test WiFi reconnection
- [ ] Test API failure recovery
- [ ] Add watchdog resets

### Memory Optimization
- [ ] Profile flash usage: target < 700KB
- [ ] Profile RAM usage: target < 60KB
- [ ] Use PROGMEM for strings
- [ ] Optimize JSON buffers
- [ ] Remove unused fonts

### User Experience
- [ ] Add boot animation/splash screen
- [ ] Add connection status indicator
- [ ] Add error messages on display
- [ ] Add brightness auto-adjust (optional)
- [ ] Smooth display transitions

### Documentation
- [ ] Write user manual (README.md)
- [ ] Document OTA update process
- [ ] Document configuration options
- [ ] Create troubleshooting guide
- [ ] Add inline code comments

### Release
- [ ] Create GitHub release
- [ ] Build release firmware binary
- [ ] Write release notes
- [ ] Tag version 1.0.0

---

## Risk Register

| Risk | Impact | Probability | Mitigation |
|------|--------|-------------|------------|
| OTA fails, device bricked | Critical | Medium | Extensive OTA testing on NodeMCU first |
| Display doesn't work | High | Low | Verified pinout from multiple sources |
| Memory overflow | High | Medium | Use streaming JSON, profile early |
| API rate limited | Medium | Low | Use Open-Meteo (no limits) |
| WiFi unstable | Medium | Medium | Watchdog + auto-reconnect |

---

## Hardware Bill of Materials

| Item | Qty | Purpose | Status |
|------|-----|---------|--------|
| NodeMCU ESP8266 | 1 | Development/testing | To Order |
| ST7789 240x240 Display | 1 | Development/testing | To Order |
| Jumper Wires | 10 | Display connection | To Order |
| SmallTV-Ultra | 1 | Target device | Have |
| USB-C Cable | 1 | Power | Have |

---

## Milestone Summary

| Milestone | Description | Dependencies |
|-----------|-------------|--------------|
| M0 | Environment ready, project builds | None |
| M1 | OTA working on NodeMCU | M0 |
| M2 | Display working on NodeMCU | M1 |
| M3 | WiFi + Web server working | M1 |
| M4 | Clock display working | M2, M3 |
| M5 | Weather display working | M4 |
| M6 | Dual location working | M5 |
| M7 | Web config complete | M6 |
| M8 | Flash to real device | M1-M7, extensive testing |
| M9 | Release v1.0.0 | M8, stability testing |

---

## Current Status

**Last Updated**: 2025-12-18

### Completed
- [x] Reverse engineering of original firmware
- [x] API endpoint documentation
- [x] Hardware pinout confirmed
- [x] PlatformIO project created
- [x] TFT_eSPI configuration set
- [x] Basic main.cpp skeleton
- [x] Implementation plan documented
- [x] **Phase 1: Minimal Safe Firmware (OTA-capable)** - COMPLETE

### Phase 1 Implementation Details
| Component | Status | Notes |
|-----------|--------|-------|
| WiFi AP mode | ✅ Done | SSID: `SmallTV-Ultra` via WiFiManager |
| WiFi client mode | ✅ Done | Auto-connects to saved credentials |
| ArduinoOTA | ✅ Done | Port 8266, mDNS: `smalltv-ultra.local` |
| Web-based OTA | ✅ Done | `/update` endpoint with progress UI |
| Watchdog timer | ✅ Done | 8-second hardware watchdog |
| Serial debug | ✅ Done | 115200 baud with tagged output |
| LittleFS | ✅ Done | Migrated from deprecated SPIFFS |

**Build Stats (v0.2.0-dev):**
- RAM: 42.9% (35,160 / 81,920 bytes)
- Flash: 38.9% (406,464 / 1,044,464 bytes)

### In Progress
- [ ] Hardware testing on NodeMCU development board

### Blocked
- [ ] None

### Next Actions
1. Test firmware on NodeMCU dev board
2. Verify OTA updates work reliably (3+ test cycles)
3. Proceed to Phase 2: Display Driver

---

## Quick Reference

### Build Commands
```bash
# Build
pio run

# Upload via USB
pio run --target upload

# Upload via OTA
pio run --target upload --upload-port 192.168.1.xxx

# Build LittleFS filesystem
pio run --target buildfs

# Upload LittleFS filesystem
pio run --target uploadfs

# Serial monitor
pio device monitor

# Clean
pio run --target clean
```

### Key Files
| File | Purpose |
|------|---------|
| `platformio.ini` | Build configuration |
| `src/main.cpp` | Entry point |
| `src/config.h` | Pin definitions |
| `src/ota.h` | OTA interface |
| `src/ota.cpp` | OTA implementation (ArduinoOTA + Web) |
| `data/` | LittleFS filesystem |
| `CLAUDE.md` | Project instructions |

### GPIO Pinout
| GPIO | Function |
|------|----------|
| 0 | TFT_DC |
| 2 | TFT_RST |
| 4 | TFT_CS |
| 5 | TFT_BL (PWM) |
| 13 | TFT_MOSI |
| 14 | TFT_SCLK |
