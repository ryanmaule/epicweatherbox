# Current Sprint: Pre-Development Setup

**Sprint Goal**: Prepare development environment and create minimal OTA-capable firmware

---

## Sprint Backlog

### Ready to Start (No Hardware Required)

- [ ] **P1-001**: Complete minimal OTA firmware code
  - Files: `src/main.cpp`, `src/ota.h`, `src/ota.cpp`
  - Acceptance: Code compiles, includes WiFi AP + OTA

- [ ] **P1-002**: Add web-based OTA update page
  - Files: `src/web_server.cpp`, `data/update.html`
  - Acceptance: `/update` endpoint serves upload form

- [ ] **P1-003**: Add ArduinoOTA support
  - Files: `src/ota.cpp`
  - Acceptance: `pio device list` shows device

- [ ] **P2-001**: Create display driver wrapper
  - Files: `src/display.h`, `src/display.cpp`
  - Acceptance: Code compiles, init function defined

- [ ] **P2-002**: Create weather icon bitmaps
  - Files: `src/icons.h`
  - Acceptance: 8 weather icons defined (16x16, 32x32)

### Blocked (Waiting for Hardware)

- [ ] **P1-004**: Test OTA on NodeMCU
  - Blocker: Need NodeMCU board

- [ ] **P2-003**: Test display on NodeMCU
  - Blocker: Need NodeMCU + ST7789

- [ ] **P2-004**: Verify pin configuration
  - Blocker: Need hardware

---

## Task Details

### P1-001: Complete Minimal OTA Firmware

**Description**: Create the absolute minimum firmware that:
1. Boots and prints to Serial
2. Creates WiFi AP if no saved credentials
3. Connects to WiFi using saved credentials
4. Enables ArduinoOTA for network flashing
5. Serves web page with OTA upload form
6. Handles OTA uploads

**Code Structure**:
```cpp
// main.cpp
void setup() {
    Serial.begin(115200);
    initOTA();       // Setup ArduinoOTA
    setupWiFi();     // WiFiManager
    setupWebServer(); // Include /update endpoint
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
}
```

**Files to Create/Modify**:
- `src/main.cpp` - Simplify existing, add OTA
- `src/ota.h` - OTA function declarations
- `src/ota.cpp` - ArduinoOTA setup

---

### P1-002: Web-Based OTA Update Page

**Description**: Create `/update` endpoint with file upload form.

**HTML Template**:
```html
<!DOCTYPE html>
<html>
<head>
    <title>Firmware Update</title>
    <meta name="viewport" content="width=device-width">
</head>
<body>
    <h1>Firmware Update</h1>
    <form method="POST" action="/update" enctype="multipart/form-data">
        <input type="file" name="firmware" accept=".bin">
        <button type="submit">Upload</button>
    </form>
    <p>Current version: {VERSION}</p>
</body>
</html>
```

**Server Handlers**:
```cpp
server.on("/update", HTTP_GET, handleUpdatePage);
server.on("/update", HTTP_POST, handleUpdateComplete, handleUpdateUpload);
```

---

### P2-001: Display Driver Wrapper

**Description**: Create abstraction layer over TFT_eSPI for cleaner main code.

**Interface**:
```cpp
// display.h
#pragma once

void initDisplay();
void setBacklight(uint8_t brightness);  // 0-255
void fillScreen(uint16_t color);
void drawText(int x, int y, const char* text, uint16_t color, uint8_t size);
void drawCenteredText(int y, const char* text, uint16_t color, uint8_t size);
void testPattern();  // Color bars for debugging

// Colors (RGB565)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
```

---

### P2-002: Weather Icon Bitmaps

**Description**: Create bitmap arrays for weather icons.

**Icons Needed**:
| WMO Code | Description | Icon |
|----------|-------------|------|
| 0 | Clear | sun |
| 1-3 | Partly cloudy | cloud-sun |
| 45-48 | Fog | fog |
| 51-55 | Drizzle | cloud-drizzle |
| 61-65 | Rain | cloud-rain |
| 71-77 | Snow | snowflake |
| 80-82 | Showers | cloud-showers |
| 95-99 | Thunderstorm | cloud-bolt |

**Format**: Use PROGMEM arrays for flash storage
```cpp
const uint16_t icon_sun_16x16[] PROGMEM = {
    // 256 uint16_t values (RGB565)
};
```

---

## Definition of Done

A task is complete when:
1. Code compiles without warnings
2. Functionality works as described
3. Memory usage documented
4. Code committed with descriptive message

---

## Notes

- **No time estimates** - work proceeds as fast as practical
- **Hardware arrival** will unblock multiple tasks
- **Focus on OTA reliability** - this is the most critical feature
- **Keep code simple** - avoid premature optimization

---

## Resources

### NodeMCU Pinout (for testing)
```
NodeMCU    ST7789
-------    ------
D1 (5)  -> BL
D2 (4)  -> CS
D3 (0)  -> DC
D4 (2)  -> RST
D7 (13) -> SDA (MOSI)
D5 (14) -> SCL (SCLK)
3V3     -> VCC
GND     -> GND
```

### Test URLs
- http://192.168.4.1/ - Default AP IP
- http://<device-ip>/ - When connected to WiFi
- http://<device-ip>/update - OTA page
- http://<device-ip>/api/status - JSON status
