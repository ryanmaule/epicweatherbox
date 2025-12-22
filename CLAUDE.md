# SmallTV-Ultra Custom Firmware Project

## Project Overview

This project aims to reverse engineer the closed-source GeekMagic SmallTV-Ultra firmware and create a custom, open-source alternative with enhanced features.

### Target Device
- **Device**: SmallTV-Ultra by GeekMagic
- **MCU**: ESP8266
- **Original Firmware Version**: V9.0.41
- **Original Source**: https://github.com/GeekMagicClock/smalltv-ultra (binary releases only)

### Project Goals
1. Reverse engineer the original firmware to understand functionality
2. Create custom open-source firmware with:
   - 7-day weather forecast (vs original 3-day)
   - Dual location weather display
   - Streamlined feature set (remove unused features to save memory)
   - Maintain core functionality: WiFi setup, web admin, time/weather display

## Original Firmware Features (Discovered)

### Network Features
- WiFi AP mode for initial setup (SSID: `SmallTV-Ultra`)
- WiFi client mode
- Web server for configuration
- Captive portal (`/hotspot-detect.html`)
- OTA firmware updates

### API Integrations
- **Weather**:
  - WeatherAPI.com: `http://api.weatherapi.com/v1/forecast.json`
  - OpenWeatherMap: `http://api.openweathermap.org/data/2.5/weather`
- **Time**: WorldTimeAPI: `http://worldtimeapi.org/api/timezone/UTC`

### Web Endpoints (from strings)
- `/index.html.gz` - Main interface
- `/settings.html.gz` - Settings page
- `/network.html.gz` - Network configuration
- `/weather.html` - Weather display
- `/wifisave` - WiFi credential save endpoint
- `/doUpload` - Firmware upload endpoint

### Configuration Files (SPIFFS)
| File | Purpose |
|------|---------|
| `/config.json` | Main configuration |
| `/wifi.json` | WiFi credentials |
| `/city.json` | Weather location |
| `/key.json` | API key (OpenWeatherMap) |
| `/fkey.json` | API key (WeatherAPI) |
| `/weather.html` | Weather display template |
| `/ntp.json` | NTP server config |
| `/dst.json` | Daylight saving time |
| `/unit.json` | Temperature units |
| `/font.json` | Font settings |
| `/brt.json` | Brightness |
| `/timebrt.json` | Time-based brightness |
| `/timecolor2.json` | Time display colors |
| `/hour12.json` | 12/24 hour format |
| `/colon.json` | Colon display style |
| `/day.json` | Day display settings |
| `/delay.json` | Animation delays |
| `/gif.json` | GIF settings |
| `/img.json` | Image settings |
| `/album.json` | Photo album |
| `/app.json` | App settings |
| `/theme_list.json` | Available themes |
| `/stock.json` | Stock ticker (if enabled) |
| `/bili.json` | Bilibili integration |
| `/daytimer.json` | Day timer settings |
| `/space.json` | Space/layout settings |
| `/lon.json` | Longitude |
| `/v.json` | Version info |

### Assets
- Boot animation: `/image/boot.gif`, `/image/boot.jpg`
- Spaceman animation: `/gif/spaceman.gif`, `/image/spaceman.gif`
- Theme images: `/%s.jpg`, `/n%s.jpg`, `/h.jpg`, `/t.jpg`
- Start screen: `/start.jpg`

## Development Environment

### Required Tools
```bash
# ESP8266 Development
pip install esptool
pip install platformio

# Reverse Engineering
brew install binwalk        # Firmware analysis
brew install ghidra         # Disassembly (optional, advanced)

# Development
brew install arduino-cli    # Or use Arduino IDE / PlatformIO
```

### Hardware Requirements
- SmallTV-Ultra device
- USB-C cable for programming
- Computer with serial port access

### Recommended IDE
- **PlatformIO** (VSCode extension) - Recommended
- Arduino IDE with ESP8266 board support

## Project Structure

```
smalltv-custom/
├── CLAUDE.md                    # This file
├── docs/
│   ├── REVERSE_ENGINEERING.md   # RE findings and notes
│   ├── HARDWARE.md              # Hardware documentation
│   └── API.md                   # API documentation
├── original/
│   ├── FW-Smalltv-Ultra-V9.0.41.bin
│   ├── FW-Smalltv-Ultra-V9.0.41.zip
│   └── firmware_strings.txt     # Extracted strings
├── analysis/
│   ├── extracted/               # Binwalk extracted files
│   └── notes/                   # Analysis notes
├── src/
│   ├── main.cpp                 # Main firmware
│   ├── config.h                 # Configuration
│   ├── wifi_manager.cpp         # WiFi handling
│   ├── web_server.cpp           # Web interface
│   ├── weather.cpp              # Weather API
│   ├── display.cpp              # Display driver
│   └── ...
├── data/                        # SPIFFS filesystem
│   ├── index.html
│   ├── settings.html
│   └── ...
├── platformio.ini               # PlatformIO config
└── README.md                    # User documentation
```

## Build Commands

```bash
# Build firmware
pio run

# Upload firmware
pio run --target upload

# Upload SPIFFS filesystem
pio run --target uploadfs

# Monitor serial output
pio device monitor

# Clean build
pio run --target clean
```

## Key Technical Notes

### ESP8266 Specifications
- Flash: Likely 4MB (based on firmware size ~500KB)
- RAM: 80KB
- CPU: 80/160 MHz

### Display (Confirmed)
- **Controller**: ST7789T3
- **Size**: 1.54" IPS TFT
- **Resolution**: 240x240 pixels
- **Interface**: SPI (Mode 3 required)
- **Ribbon marking**: GMT154-06

### Display Pinout
| Function | GPIO | Notes |
|----------|------|-------|
| CS | GPIO4 | Chip Select |
| DC | GPIO0 | Data/Command |
| RST | GPIO2 | Reset |
| BL | GPIO5 | Backlight (PWM) |
| MOSI | GPIO13 | SPI Data |
| SCLK | GPIO14 | SPI Clock |

### Programming Requirements
**Good news**: The stock firmware includes a web-based OTA update feature!

**To flash custom firmware (no soldering required)**:
1. Build firmware in PlatformIO → produces `.bin` file
2. Navigate to `http://<device-ip>/update`
3. Upload the `.bin` file
4. Wait for reboot

**Important considerations**:
- Back up ability to restore: Keep the original firmware `.bin` file
- Ensure custom firmware also includes OTA update capability
- If custom firmware bricks device, soldering would be required to recover

**USB-C port**: Power only, no data. All programming via WiFi OTA.

### Memory Considerations
- Original firmware: 514KB
- SPIFFS partition: Remaining flash
- Keep code optimized to leave room for:
  - 7-day forecast data
  - Dual location storage
  - GIF animations

## Custom Firmware Priorities

### Must Have (P0)
1. WiFi AP setup mode
2. WiFi client connection
3. Web-based configuration
4. Time display with NTP sync
5. Weather display (single location)
6. OTA updates

### Should Have (P1)
1. 7-day weather forecast
2. Dual location weather
3. Brightness control
4. Theme support

### Nice to Have (P2)
1. GIF animations
2. Photo album
3. Custom fonts

### Remove (to save memory)
1. Bilibili integration
2. Stock ticker
3. Unused animation features

## API Keys Required

- **WeatherAPI.com**: Free tier allows forecast data
- **OpenWeatherMap**: Backup/alternative

## Useful Resources

### Core Libraries
- [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
- [TFT_eSPI Library](https://github.com/Bodmer/TFT_eSPI) - Display driver
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) - JSON parsing
- [WiFiManager](https://github.com/tzapu/WiFiManager) - WiFi setup portal

### Weather APIs
- [Open-Meteo](https://open-meteo.com/) - **FREE 7-day forecast, no API key required!**
- [OpenWeatherMap](https://openweathermap.org/) - Current weather (free tier)
- [Bodmer's OpenWeather Library](https://github.com/Bodmer/OpenWeather)

### Reference Implementations
- [AlexeyMal ESP8266 Weather Station](https://github.com/AlexeyMal/esp8266-weather-station) - Open-Meteo + ESP8266, 8-day forecast
- [fazibear/pix](https://github.com/fazibear/pix) - TFT_eSPI config for SmallTV
- [ThingPulse Weather Station](https://github.com/ThingPulse/esp8266-weather-station) - Mature weather station framework
- [keohanoi GIF Display](https://github.com/keohanoi/geekmagic-clock-esp8266-smalltv-gif-display) - GIF on SmallTV hardware

### Hardware Documentation
- [Elektroda SmallTV-Ultra Pinout](https://www.elektroda.com/news/news4113933.html) - Confirmed GPIO mapping
- [Home Assistant ESPHome Thread](https://community.home-assistant.io/t/installing-esphome-on-geekmagic-smart-weather-clock-smalltv-pro/618029)

## Notes for Claude

When working on this project:
1. ESP8266 has limited RAM - optimize for memory usage
2. Use PROGMEM for constant strings
3. Prefer async operations to avoid blocking
4. Test with serial monitor for debugging
5. Back up original firmware before flashing custom
6. The display driver needs to be identified from hardware

## Git Workflow (IMPORTANT!)

**Commit frequently!** Don't let work accumulate without committing.

### When to Commit
- After completing any task or feature
- After fixing a bug
- After updating documentation
- After a successful build/test
- Before ending a session
- After any significant code change

### Commit Message Format
```
<type>: <short description>

<optional longer description>
```

Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`

### Standard Workflow
```bash
# Check what changed
git status

# Stage changes
git add <files>   # or git add . for all

# Commit with message
git commit -m "feat: add weather API integration"

# Sync beads (if using beads for issue tracking)
bd sync --from-main
```

### Before Session End Checklist
1. `git status` - Check for uncommitted changes
2. `git add .` - Stage all changes
3. `bd sync --from-main` - Sync beads updates
4. `git commit -m "..."` - Commit with descriptive message

### Branch Strategy
- `main` - Stable, working code
- Feature branches for larger changes (optional)

## Live Device Analysis (Completed)

A live device was accessed at `http://192.168.4.235/` running firmware V9.0.40.

### Key Discoveries

**Storage**: 3MB total SPIFFS, ~120KB free after assets
- GIF directory: `/gif/` - 4 GIF files (~390KB)
- Image directory: `/image/` - 10 files (~484KB)

**Settings API**: All configuration via `/set` endpoint with GET parameters:
```
/set?theme=1              # Change theme
/set?brt=50               # Brightness
/set?cd1=Seoul            # Weather city
/set?key=xxx              # OpenWeatherMap key
/set?fkey=xxx             # WeatherAPI key
/set?reboot=1             # Reboot device
/set?reset=1              # Factory reset
```

**Themes** (7 total):
1. Weather Clock Today
2. Weather Forecast (uses WeatherAPI.com - 3 day limit on free tier)
3. Photo Album
4. Time Style 1-3
5. Simple Weather Clock

**Full API documentation**: See [analysis/API_ENDPOINTS.md](analysis/API_ENDPOINTS.md)

### Files Downloaded from Device
- All HTML pages (gzipped): index, settings, network, weather, time, image
- JavaScript: settings.js, jquery.min.js
- CSS: style.css
- Config JSONs: v.json, config.json, city.json, theme_list.json, key.json, fkey.json, timebrt.json, space.json, wifi.json

### Important Implementation Notes

1. **WeatherAPI.com free tier**: Limited to 3-day forecast. For 7-day:
   - Need paid plan OR
   - Use different API (OpenMeteo is free with 7-day)

2. **File naming**: Must be short - original uses filenames like "h.jpg", "t.jpg"

3. **GIF size**: Must be 80x80 pixels for weather screen

4. **Image size**: 240x240 pixels for photo album (confirms display resolution)

5. **Web assets are gzipped**: Must serve with proper content-encoding

## Progress Tracking

### Research & Analysis
- [x] Initial firmware extraction and string analysis
- [x] Live device endpoint discovery
- [x] Download web assets from device
- [x] Document API endpoints
- [x] Create basic PlatformIO project
- [x] Identify display hardware (ST7789T3, 240x240 confirmed)
- [ ] Complete firmware binary analysis with binwalk

### Phase 1: Minimal Safe Firmware - COMPLETE ✅
- [x] Implement WiFi manager (WiFiManager library)
- [x] Implement basic web server
- [x] Implement ArduinoOTA
- [x] Implement web-based OTA (`/update` endpoint)
- [x] Add hardware watchdog timer
- [x] Migrate to LittleFS (SPIFFS deprecated)
- **Build: v0.2.0-dev** - RAM: 42.9%, Flash: 38.9%

### Phase 2-8: Remaining Work
- [ ] Implement display driver (TFT_eSPI)
- [ ] Implement web server with /set endpoint
- [ ] Implement NTP time sync
- [ ] Implement weather API client (Open-Meteo)
- [ ] Test 7-day forecast
- [ ] Implement dual location
- [ ] Final optimization and testing
