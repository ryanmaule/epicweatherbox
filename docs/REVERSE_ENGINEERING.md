# Reverse Engineering Plan: SmallTV-Ultra Firmware

## Overview

This document outlines the systematic approach to reverse engineer the GeekMagic SmallTV-Ultra firmware (V9.0.41) to understand its functionality and create a compatible open-source replacement.

---

## Phase 1: Static Binary Analysis

### 1.1 Install Required Tools

```bash
# macOS
brew install binwalk
pip3 install esptool
pip3 install ubi_reader  # For filesystem extraction

# Optional for deep analysis
brew install --cask ghidra
brew install radare2
```

### 1.2 Firmware Structure Analysis

ESP8266 firmware typically has this structure:

| Offset | Size | Content |
|--------|------|---------|
| 0x0000 | 4B | Magic header (0xE9) |
| 0x0004 | Variable | Boot loader |
| 0x1000 | Variable | Application code |
| 0x3xx000 | Variable | SPIFFS filesystem |

**Commands to run:**

```bash
# Analyze firmware structure
binwalk original/FW-Smalltv-Ultra-V9.0.41.bin

# Extract embedded filesystems and compressed data
binwalk -e original/FW-Smalltv-Ultra-V9.0.41.bin -C analysis/extracted/

# Check for ESP8266 header
xxd -l 256 original/FW-Smalltv-Ultra-V9.0.41.bin

# Get detailed image info (requires esptool)
esptool.py image_info original/FW-Smalltv-Ultra-V9.0.41.bin
```

### 1.3 String Analysis (Completed)

Already extracted 9,425 strings. Key findings:

**API Endpoints:**
- `http://api.weatherapi.com/v1/forecast.json?key=`
- `http://api.openweathermap.org:80/data/2.5/weather?q=`
- `http://api.openweathermap.org:80/data/2.5/weather?id=`
- `http://worldtimeapi.org/api/timezone/UTC`

**Configuration Files (SPIFFS):**
- 25+ JSON configuration files identified
- HTML/JS/CSS assets (gzipped)
- GIF and JPG image assets

**Device Identifiers:**
- `SmallTV-Ultra`
- `Ultra-V9.0.41`
- `GeekMagic`
- `ESP-%02X%02X%02X` (MAC-based naming)

### 1.4 Further String Mining

```bash
# Find all HTTP endpoints
strings original/FW-Smalltv-Ultra-V9.0.41.bin | grep -E '^/' | sort -u

# Find potential function names
strings original/FW-Smalltv-Ultra-V9.0.41.bin | grep -E '^[a-z_]+\(' | head -50

# Find error messages (useful for understanding code flow)
strings original/FW-Smalltv-Ultra-V9.0.41.bin | grep -iE '(error|fail|invalid|wrong)'

# Find WiFi-related strings
strings original/FW-Smalltv-Ultra-V9.0.41.bin | grep -iE '(ssid|password|connect|wifi|ap_|sta_)'
```

---

## Phase 2: Live Device Analysis

### 2.1 Serial Console Capture

Connect to device via USB and capture boot messages:

```bash
# Find serial port
ls /dev/cu.* | grep -i usb

# Connect at 115200 baud (most common for ESP8266)
screen /dev/cu.usbserial-XXXX 115200

# Or use platformio
pio device monitor -b 115200
```

**Expected output:**
- Boot loader messages
- WiFi connection attempts
- Web server startup
- Memory information

### 2.2 Network Traffic Capture

While device is running, capture its network activity:

```bash
# On macOS, use Wireshark or tcpdump
# Filter for device IP address

# Capture HTTP requests to weather APIs
tcpdump -i en0 -A 'host api.weatherapi.com or host api.openweathermap.org'
```

**Goals:**
- Capture full API request URLs
- See how API keys are passed
- Understand response parsing

### 2.3 Web Interface Analysis

1. Connect to device's AP mode (`SmallTV-Ultra`)
2. Navigate to `http://192.168.4.1`
3. Use browser DevTools to:
   - Capture all HTTP requests
   - Download HTML/JS/CSS files
   - Map all endpoints

**Key endpoints to document:**
- `/` - Main page
- `/wifisave` - WiFi credential submission
- `/settings.html.gz` - Settings page
- `/doUpload` - Firmware upload

### 2.4 SPIFFS Dump from Device

If esptool can communicate with device:

```bash
# Read entire flash (adjust size based on chip)
esptool.py --port /dev/cu.usbserial-XXXX read_flash 0 0x400000 flash_dump.bin

# SPIFFS usually starts at 0x300000 for 4MB flash
# Extract SPIFFS partition
dd if=flash_dump.bin of=spiffs.bin bs=1 skip=$((0x300000))

# Mount/extract SPIFFS
# Use mkspiffs tool or Python spiffsimage
```

---

## Phase 3: Code Reconstruction

### 3.1 Identify Libraries Used

From strings, we can identify:

| Library | Evidence | Purpose |
|---------|----------|---------|
| ESP8266 Arduino Core | `core_esp8266_main.cpp` | Base framework |
| SPIFFS | `spiffs_api.h` | Filesystem |
| ESPAsyncWebServer | HTTP method handling | Web server |
| ArduinoJson | `.json` file handling | Config parsing |
| TFT_eSPI (likely) | Display code | LCD driver |

### 3.2 Function Mapping

Create a map of identified functions based on:
- Error messages
- Debug strings
- State machine strings

Example mappings:
```
"Connect WiFi..." -> wifi_connect()
"dhcp server start" -> start_ap_mode()
"Update Success! Rebooting..." -> handle_ota_upload()
```

### 3.3 Disassembly (Advanced)

For deeper analysis using Ghidra:

1. Import binary as "Xtensa:LE:32:default"
2. Set base address to 0x40200000 (typical ESP8266 code base)
3. Run auto-analysis
4. Look for:
   - Function prologues (`entry` instruction)
   - String references
   - API call patterns

---

## Phase 4: Hardware Documentation

### 4.1 Physical Inspection

Open device and document:
- [ ] MCU chip markings (ESP8266 variant)
- [ ] Flash chip model and size
- [ ] Display model (look for markings)
- [ ] Display connector pinout
- [ ] Any additional ICs

### 4.2 Display Identification

Common displays for ESP8266 devices:
- **ST7789** - 240x240 or 240x320, SPI
- **ILI9341** - 320x240, SPI
- **GC9A01** - 240x240 round, SPI

Identification methods:
1. Check for chip markings on flex cable
2. Look up FCC ID if visible
3. Compare connector pinout to known displays
4. Trial and error with TFT_eSPI configurations

### 4.3 GPIO Mapping

Document pin connections:
- Display: MOSI, SCK, DC, CS, RST, BL
- Any buttons or sensors
- USB-Serial chip connections

---

## Phase 5: Recreation Strategy

### 5.1 Core Components to Recreate

1. **WiFi Manager**
   - AP mode for setup
   - Client mode for operation
   - Credential storage in SPIFFS

2. **Web Server**
   - Serve configuration pages
   - Handle API endpoints
   - OTA update support

3. **Weather Client**
   - WeatherAPI.com integration (7-day forecast)
   - JSON parsing
   - Data caching

4. **Display Driver**
   - Initialize display
   - Draw time
   - Draw weather info
   - Animation support

5. **Configuration Manager**
   - Load/save JSON configs
   - Runtime settings

### 5.2 Development Approach

1. **Start with known-working code:**
   - Use ESP8266 Arduino examples as base
   - Leverage existing libraries (WiFiManager, TFT_eSPI, etc.)

2. **Incremental implementation:**
   - Get display working first
   - Add WiFi connectivity
   - Add web server
   - Add weather API
   - Add configuration

3. **Match original behavior where needed:**
   - Same AP SSID for easy transition
   - Compatible config file format (optional)

---

## Analysis Progress Checklist

### Binary Analysis
- [x] Extract strings from firmware
- [ ] Run binwalk analysis
- [ ] Extract embedded filesystem (if present in binary)
- [ ] Map firmware memory layout
- [ ] Identify code sections vs data sections

### Network Analysis
- [ ] Capture device boot messages
- [ ] Document all web endpoints
- [ ] Capture weather API requests
- [ ] Document API response format

### Hardware Documentation
- [ ] Identify ESP8266 module variant
- [ ] Document flash size
- [ ] Identify display controller
- [ ] Map GPIO pins
- [ ] Document power supply

### Code Recreation
- [ ] Set up PlatformIO project
- [ ] Implement display driver
- [ ] Implement WiFi manager
- [ ] Implement web server
- [ ] Implement weather client
- [ ] Implement 7-day forecast
- [ ] Implement dual location
- [ ] Final testing

---

## Appendix A: Extracted String Categories

### API URLs
```
http://api.weatherapi.com/v1/forecast.json?key=
http://api.openweathermap.org:80/data/2.5/weather?q=
http://api.openweathermap.org:80/data/2.5/weather?id=
http://worldtimeapi.org/api/timezone/UTC
```

### Web Endpoints
```
/index.html.gz
/settings.html.gz
/network.html.gz
/weather.html
/hotspot-detect.html
/wifisave
/doUpload
```

### Config Files
```
/config.json, /wifi.json, /city.json, /key.json, /fkey.json
/ntp.json, /dst.json, /unit.json, /font.json, /brt.json
/timebrt.json, /timecolor2.json, /hour12.json, /colon.json
/day.json, /delay.json, /gif.json, /img.json, /album.json
/app.json, /theme_list.json, /stock.json, /bili.json
/daytimer.json, /space.json, /lon.json, /v.json, /w_i.json
```

### Assets
```
/image/boot.gif, /image/boot.jpg
/gif/spaceman.gif, /image/spaceman.gif
/start.jpg, /%s.jpg, /n%s.jpg, /h.jpg, /t.jpg
```

---

## Appendix B: ESP8266 Memory Map

Standard ESP8266 memory layout (4MB flash):

| Address | Size | Content |
|---------|------|---------|
| 0x000000 | 4KB | Boot loader |
| 0x001000 | ~1MB | Application (OTA slot 1) |
| 0x101000 | ~1MB | Application (OTA slot 2) |
| 0x200000 | 4KB | EEPROM emulation |
| 0x201000 | 4KB | RF calibration |
| 0x300000 | 1MB | SPIFFS |
| 0x3FB000 | 16KB | System parameters |
| 0x3FC000 | 4KB | User parameters |
| 0x3FE000 | 8KB | WiFi config |

Note: Actual layout depends on board configuration and may vary.

---

## Appendix C: Common ESP8266 Arduino Libraries

| Library | Purpose | GitHub |
|---------|---------|--------|
| ESP8266WiFi | WiFi connectivity | Built-in |
| ESP8266WebServer | HTTP server | Built-in |
| ESPAsyncWebServer | Async HTTP | me-no-dev/ESPAsyncWebServer |
| ESPAsyncTCP | Async TCP | me-no-dev/ESPAsyncTCP |
| ArduinoJson | JSON handling | bblanchon/ArduinoJson |
| TFT_eSPI | Display driver | Bodmer/TFT_eSPI |
| WiFiManager | WiFi setup | tzapu/WiFiManager |
| NTPClient | Time sync | arduino-libraries/NTPClient |
| SPIFFS | Filesystem | Built-in |

---

## Next Steps

1. **Immediate**: Run binwalk on the firmware
2. **Short-term**: Connect to device and capture serial output
3. **Medium-term**: Extract SPIFFS from device
4. **Long-term**: Begin code recreation
