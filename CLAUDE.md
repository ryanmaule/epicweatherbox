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

**Note**: PlatformIO is installed at `/Users/ryanmaule/Library/Python/3.9/bin/pio`

```bash
# PlatformIO path (use this for all pio commands)
PIO="/Users/ryanmaule/Library/Python/3.9/bin/pio"

# Build firmware
$PIO run

# Upload firmware via OTA (since USB is power-only)
$PIO run --target upload

# Upload LittleFS filesystem
$PIO run --target uploadfs

# Monitor serial output
$PIO device monitor

# Clean build
$PIO run --target clean
```

## Admin HTML System

The admin panel (`data/admin.html`) uses a gzip + PROGMEM + LittleFS caching system due to ESP8266 memory limits. **See `.claude/agents/esp8266-developer.md` for full documentation.**

**Quick Reference**:
```
data/admin.html → (build) → src/admin_html.h → (boot) → LittleFS:/admin.html.gz → (serve) → Browser
```

**When changing admin.html**:
1. Edit `data/admin.html`
2. **Bump version** in `config.h` (`FIRMWARE_VERSION`) - **CRITICAL!**
3. Clean build: `pio run --target clean && pio run -e esp8266`
4. Flash via OTA

**Why version matters**: Device caches `/admin.html.gz` on LittleFS. It only reprovisions from PROGMEM when the firmware version changes. Without a version bump, your changes won't appear!

**Force reprovision** (debugging only): `curl http://192.168.4.235/api/reprovision`

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
- **Color Format**: RGB565 (16-bit, R in high bits, B in low bits)

### Color Conversion
For detailed color conversion formulas and palettes, see the **TFT Designer Agent** at `.claude/agents/tft-designer.md`.

**Quick formula** (#RRGGBB to RGB565):
```
RGB565 = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)
```

**Example**: #4682B4 (steel blue) → 0x4416

**Note**: Code comments saying "BGR565" are misleading - the display uses standard RGB565.

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

## AI Agents

This project uses specialized AI agents to coordinate development. Agents are defined in `.claude/agents/` and can be invoked by asking Claude.

### Available Agents

| Agent | File | Invoke With |
|-------|------|-------------|
| **Project Manager** | `project-manager.md` | "What work is ready?", "Plan the session" |
| **TFT Designer** | `tft-designer.md` | "Design a screen for [feature]" |
| **ESP8266 Developer** | `esp8266-developer.md` | "Help me implement [feature]" |
| **Release Manager** | `release-manager.md` | "Validate the firmware for release" |
| **Documentation** | `documentation.md` | "Update documentation for [feature]" |

### Project Manager Agent

Coordinates development work, tracks issues via beads, and manages releases.

**Use for**:
- Starting a session: "What work is ready?"
- Planning work: "Create issues for this feature"
- Release coordination: "Prepare release vX.Y.Z"
- Session closure: "Close this session properly"

**Key commands**:
```bash
bd ready                    # Find available work
bd update <id> --status=in_progress  # Claim work
bd close <id>               # Complete work
bd sync                     # Sync with git
```

### TFT Designer Agent

Creates beautiful, accessible visual designs for the 240x240 TFT display.

**Use for**:
- "Design a screen for [feature]"
- "Generate an accessible color palette"
- "Check if these colors are accessible"
- "Update the dark/light theme colors"

**Capabilities**:
- Generate color palettes using [Colormind AI](http://colormind.io/api/)
- Verify accessibility with [Colour Contrast Checker](https://colourcontrast.cc/)
- Design screen layouts for 240x240 pixels
- Convert colors to/from RGB565 format
- Test designs on physical device

**Design Standards**:
- WCAG AA minimum (4.5:1 contrast for text)
- Both dark and light theme support
- Colorblind-accessible color choices
- Memory-efficient implementations (no sprites)

**Color Tools**:
```bash
# Generate palette with Colormind
curl 'http://colormind.io/api/' --data-binary '{"model":"ui"}'

# Check contrast at colourcontrast.cc (convert RGB565 to hex first)
```

### Release Manager Agent (CRITICAL)

**WARNING**: The SmallTV-Ultra has NO USB data connection - only power. Recovery from a bricked device requires hardware modifications (soldering). Always validate firmware before flashing!

**Use BEFORE EVERY OTA FLASH**:
- "Validate the firmware for release"
- "Check if it's safe to flash"
- "Is the firmware ready to deploy?"

**What It Checks**:
- OTA update feature enabled (CRITICAL - recovery lifeline)
- Build succeeds without errors
- Firmware size within limits (~1MB max)
- RAM usage not excessive (>85% is critical)
- Watchdog timer properly serviced
- Safe mode/recovery mechanism present

### Documentation Agent

Keeps all documentation current when features change.

**Use for**:
- "Update README.md for [feature]"
- "Document this in CLAUDE.md"
- "Verify documentation consistency"
- "Prepare documentation for release"

**Files it manages**:
- `README.md` - User-facing documentation
- `CLAUDE.md` - Developer/AI reference
- `src/config.h` - Version numbers

### ESP8266 Developer Agent

Implements features following embedded best practices.

**Use for**:
- "Implement [feature]"
- "Fix this memory issue"
- "Add a new display screen"
- "Optimize this code for ESP8266"

**Key constraints it enforces**:
- Memory management (80KB RAM limit)
- Watchdog timer feeding (yield/delay)
- OTA safety (never disable OTA)
- PROGMEM for constants

### Agentic Workflow

**Starting a session**:
```bash
bd stats                    # Project health
bd ready                    # Available work
bd update <id> --status=in_progress  # Claim work
```

**For visual/display features** (TFT Designer + ESP8266 Developer):
```
1. TFT Designer creates layout and color specs
2. ESP8266 Developer implements the design
3. TFT Designer reviews on-device, verifies accessibility
4. Iterate until satisfied
```

**During development** (ESP8266 Developer):
- Follow coding standards
- Mind memory constraints
- For display code, use TFT Designer color specs
- Commit frequently

**Before deployment** (Release Manager):
- "Validate the firmware for release"
- For visual changes: verify TFT Designer has tested on device
- Only flash if validation passes

**After completing features** (Documentation Agent):
- Update README.md and CLAUDE.md
- Close issues: `bd close <id>`

**Session completion** (Project Manager):
```bash
git status && git add . && bd sync && git commit -m "..." && bd sync && git push
```

### Standard Release Workflow

```bash
# 1. Ask Claude to validate
"Validate the firmware for release"

# 2. If passed, build firmware
/Users/ryanmaule/Library/Python/3.9/bin/pio run -e esp8266

# 3. Flash to device via OTA
# Navigate to http://192.168.4.235/update
# Upload .pio/build/esp8266/firmware.bin

# 4. Verify device boots and OTA still works
# Check http://192.168.4.235/admin loads

# 5. Create GitHub release
git tag v1.x.x
git push origin v1.x.x
# Upload firmware.bin to release
```

### Recovery Procedures

If OTA becomes unavailable after a flash:

1. **First, wait** - Device may be slow to boot (up to 60 seconds)
2. **Check AP mode** - Look for `EpicWeatherBox` WiFi network
3. **Safe mode endpoint** - Try `http://<ip>/api/safemode`
4. **Factory reset** - Try `http://<ip>/reset`
5. **Last resort** - Hardware modification required (solder UART pads)

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

**Last Updated**: 2025-12-24

**Current Version**: v1.6.5

### Research & Analysis - COMPLETE ✅
- [x] Initial firmware extraction and string analysis
- [x] Live device endpoint discovery
- [x] Download web assets from device
- [x] Document API endpoints
- [x] Create basic PlatformIO project
- [x] Identify display hardware (ST7789T3, 240x240, BGR color order)

### Phase 1: Minimal Safe Firmware - COMPLETE ✅
- [x] Implement WiFi manager (WiFiManager library)
- [x] Implement basic web server
- [x] Implement ArduinoOTA
- [x] Implement web-based OTA (`/update` endpoint)
- [x] Add hardware watchdog timer
- [x] Migrate to LittleFS (SPIFFS deprecated)

### Phase 2: Display Driver - COMPLETE ✅
- [x] TFT_eSPI initialization (ST7789, 240x240)
- [x] Confirmed BGR color order (not RGB)
- [x] Backlight PWM on GPIO5
- [x] Current weather screen (`drawCurrentWeather()`)
- [x] Forecast screens (`drawForecast()`)
- [x] Screen cycling (`updateTftDisplay()`)
- [x] FreeSans smooth fonts enabled
- [x] WMO weather icons via CDN
- [x] Custom large number renderer for temperature
- [x] Day/night theme mode (auto, dark, light)
- [x] IP address display on boot screen
- [x] UI vertical nudge setting

**Design decisions**:
- No sprites (ESP8266 can't allocate 115KB for full-screen sprite)
- Direct TFT drawing with TFT_eSPI
- GIF support removed due to memory constraints (caused crash loops)
- Custom screen uses dynamic font sizing based on text length:
  - 0-40 chars: FSSB18 (large, bold)
  - 41-80 chars: FSSB12 (medium, bold)
  - 81-160 chars: FSS9 (small, regular)

### Phase 3: WiFi & Web Server - COMPLETE ✅
- [x] ESPAsyncWebServer with admin panel
- [x] All settings configurable via web UI
- [x] `/set` API endpoint for settings
- [x] `/api/weather` endpoint for JSON data

### Phase 4: Time & NTP - COMPLETE ✅
- [x] NTPClient with timezone support
- [x] Time displayed on weather screens

### Phase 5: Weather API - COMPLETE ✅
- [x] Open-Meteo API integration (free, no API key)
- [x] 7-day forecast data
- [x] WMO weather code mapping
- [x] JsonStreamingParser for memory efficiency

### Phase 6: Dual Location - COMPLETE ✅
- [x] Two configurable locations (Seattle/Portland default)
- [x] Screen cycling between locations
- [x] Independent weather data per location

### Phase 7: Web Configuration - COMPLETE ✅
- [x] Full admin panel at device IP
- [x] Location settings (name, lat/lon)
- [x] Display settings (brightness, cycle time)
- [x] Display preview with live updates
- [x] Display preview matches TFT UI

### Phase 8: Polish & Optimization - COMPLETE ✅
- [x] Timezone support (automatic from location)
- [x] Multi-location support (up to 5)
- [x] Theme modes (auto/dark/light)
- [x] Night mode with auto-dimming
- [x] Display Preview improvements
- [x] Emergency Safe Mode endpoint
- [x] UI vertical position nudge
- [x] Removed unused dependencies
- [x] v1.0.0 public release

### Phase 9: Custom Screen Feature (v1.1.0) - COMPLETE ✅

**Custom Text Screen**: An optional screen that appears after each location's weather screens in the rotation cycle. This allows users to display custom messages, reminders, or information.

**Implementation**: `drawCustomScreen()` in main.cpp
- **Header Section**:
  - Left: Current time (12-hour format with AM/PM) in cyan
  - Right: Custom header text (max 16 chars) in gray
  - Cyan star icon (★) in top-right corner
  - Matches the forecast screen header style for consistency
- **Body Section**:
  - Centered text with automatic word wrapping
  - Dynamic font sizing based on total character count:
    - ≤40 chars: Large bold (FSSB18, 38px line spacing)
    - 41-80 chars: Medium bold (FSSB12, 30px line spacing)
    - 81-160 chars: Small regular (FSS9, 26px line spacing)
  - Improved vertical centering for better visual balance
  - Respects theme colors (text color adapts to dark/light mode)
- **Footer Section**:
  - Rounded rectangle box with theme card color
  - Centered text (max 30 chars) in cyan
  - Positioned at bottom of screen with 8px margin

**Configuration**: Admin panel → Custom tab
- Enable/disable toggle
- Three text inputs (header, body, footer)
- Character limits enforced client-side
- Settings saved to config.json
- Live preview shows exact rendering

**Screen Rotation Logic**:
- Custom screen appears after the 3 weather screens (current, forecast days 1-3, forecast days 4-6)
- Only shown if enabled in config
- Respects same cycle timing as other screens
- Integrated seamlessly into `updateTftDisplay()` state machine

**UI Improvements**:
- [x] Safe Mode screen layout fix (content moved up for UI nudge compatibility)
  - IP address now displays at y=160 instead of y=180
  - Prevents cutoff when using negative UI nudge values
- [x] Project Links section on home page
  - Links to GitHub repo, releases, and issues
  - Shows current firmware version
  - Provides upgrade instructions
- [x] Footer navigation reorganization
  - Added "Safe Mode" link to footer (highlighted in orange)
  - Consistent footer across all pages
- [x] v1.1.0 release

### Phase 10: Unified Carousel System (v1.3.0) - COMPLETE ✅

**New Admin UI**: Complete redesign of the admin panel with unified carousel system.

**Features**:
- **Unified Carousel**: Drag-and-drop reordering of all screen types
  - Location screens (weather)
  - Countdown screens (event countdowns)
  - Custom text screens
- **Max 3 of each type**: Up to 9 total screens in rotation
- **Modern UI**: Clean, responsive design with preview

**Countdown Screens**: Event countdown feature with themed icons
- Birthday (cake icon)
- Easter (bunny icon, auto-calculated date)
- Halloween (pumpkin icon)
- Valentine's Day (heart icon)
- Christmas (tree icon)
- Custom date (calendar icon with day number)

**Large Countdown Icons**: ~48px icons drawn with TFT primitives
- `drawCountdownIcon()` draws cake, bunny, pumpkin, heart, tree, or calendar
- Calendar icon displays the actual event day number

**Admin Improvements**:
- System tab with rounded info boxes
- Current Weather section with Next button to cycle locations
- Forecast mode toggle in Display settings
- Dynamic carousel description based on forecast setting
- Live preview of all screen types

**Bug Fixes**:
- Fixed duplicate Content-Encoding header for gzip serving
- Fixed carousel save bug that wiped items
- Fixed weather API backward compatibility with `primary` key
- Fixed star icon in custom screen header

- [x] v1.3.0 release

### Phase 11: Bug Fixes (v1.3.1) - COMPLETE ✅

**Bug Fixes**:
- Fixed carousel dots not advancing on location sub-screens
  - `drawCurrentWeather()` and `drawForecast()` now receive proper `currentScreenIdx` and `totalScreens` parameters
  - Dots correctly track position through all screens in the carousel
- Fixed custom screen footer bar height to match main weather screen (36px, was 26px)

**UI Improvements**:
- Simplified carousel description to "Drag to reorder screens" (removed dynamic forecast count text)
- Removed unused `/preview` endpoint (preview is now integrated in admin panel)
  - Saves ~15KB flash space

- [x] v1.3.1 release

### Phase 12: Admin UI Polish (v1.3.2) - COMPLETE ✅

**Admin Consolidation**:
- Root URL (`/`) now redirects to `/admin` panel
- Removed separate home page (saves ~4KB flash)
- API Endpoints section moved to System tab
- Project Links section moved to System tab

**Footer Improvements**:
- Added Factory Reset link (red, with confirmation prompt)
- Removed redundant Home link
- Fixed Safe Mode URL to `/api/safemode`

- [x] v1.3.2 release

### Phase 13: Update Notification & Cleanup (v1.3.3) - COMPLETE ✅

**GitHub Update Notification**:
- Added automatic check for new GitHub releases on admin panel load
- Shows green "vX.Y.Z available" badge next to current version in System tab
- Links directly to GitHub release page for easy download
- Silent failure on network errors (update check is optional)

**Code Cleanup**:
- Removed ~350 lines of dead code (`handleAdminLegacy` function)
  - Legacy admin page was no longer called after PROGMEM provisioning was stabilized
- Updated config.h comments (removed obsolete FEATURE_COUNTDOWN_TIMER)

- [x] v1.3.3 release

### Phase 14: Theme System (v1.4.1) - COMPLETE ✅

**Theme Infrastructure**:
- New files: `src/themes.h` and `src/themes.cpp`
- 3 themes total: Classic (original), Sunset (new warm palette), Custom (user-editable)
- Built-in themes stored in PROGMEM (0 RAM cost)
- User custom theme stored in `/themes.json` on LittleFS

**Theme Modes**:
- Auto: Uses dark theme at night, light during day (based on weather API `isDay`)
- Dark: Always dark theme
- Light: Always light theme

**Theme Colors** (7 per variant):
- Background, Card, Text, Cyan (time/headers), Orange (high temp), Blue (low temp), Gray (secondary)

**Sunset Theme** (designed by TFT Designer agent):
- Dark mode: Deep burgundy (#2D1B2E) background, warm coral accents (#FF8C42), soft pink highlights (#FFB7B2)
- Light mode: Warm cream (#FFF5E6) background, burnt orange accents (#D4652F), coral highlights (#C94C4C)

**Admin UI**:
- New "Theme" tab between Display and System
- Theme selector buttons (Classic, Sunset, Custom)
- Mode selector (Auto, Dark, Light)
- 14 color pickers for custom theme (7 dark + 7 light)
- RGB565 ↔ Hex color conversion in JavaScript
- Reset to Classic defaults button

**API Endpoints**:
- `GET /api/themes` - Returns active theme, mode, and custom colors
- `POST /api/themes` - Update theme selection, mode, and custom colors
- `GET /api/reprovision` - Force admin HTML refresh and reboot

**Footer Link**:
- Added "Reload Admin" link to force reprovision of admin.html from PROGMEM

**Files Modified**:
- `src/themes.h` - NEW: Theme structures and declarations
- `src/themes.cpp` - NEW: Theme management with built-in themes
- `src/main.cpp` - Removed color defines, added theme include and API endpoints
- `src/weather.cpp/h` - Removed themeMode (moved to themes module)
- `data/admin.html` - Added Theme tab with color pickers

**Memory Impact**:
- RAM: <50 bytes (theme state + custom colors)
- LittleFS: ~300 bytes (`/themes.json`)
- Flash: +2KB for theme code, +2KB for admin UI

- [x] v1.4.1 release

### Phase 15: YouTube Stats Feature (v1.6.0 - v1.6.5) - COMPLETE ✅

**YouTube Channel Stats Screen**: Display YouTube channel statistics in the carousel rotation.

**Features**:
- Fetches stats from YouTube Data API v3 (subscribers, views, videos)
- Requires user's own API key (free from Google Cloud Console)
- 30-minute refresh interval to conserve API quota
- Large centered YouTube logo (red rounded rect with play button)
- Channel name displayed below logo
- Subscriber count prominently displayed in cyan
- Views and Videos in side-by-side rounded cards

**Technical Implementation**:
- Uses WiFiClientSecure with `setBufferSizes(512, 512)` to reduce SSL memory from 32KB to 1KB
- Requires ~20KB free heap for HTTPS connection
- Config stored in `/youtube_config.json` on LittleFS
- Max 1 YouTube screen per carousel (vs 3 for other types)

**Admin UI**:
- YouTube configuration via modal overlay (not separate tab)
- API key and channel handle inputs
- Test button to verify connection
- "Add to Carousel" button
- Stats display in modal when configured

**API Endpoints**:
- `GET /api/youtube` - Get config and stats
- `POST /api/youtube` - Save config
- `GET /api/youtube/refresh` - Force stats refresh

**Memory Impact**:
- RAM: 55.8% (was ~53%)
- Flash: 64.0% (was ~53%)
- Free heap: ~25KB at runtime

**Technical Challenges Solved**:
- HTTPS on ESP8266 with limited memory by reducing SSL buffer sizes
- Efficient JSON parsing without loading entire response into memory
- Graceful error handling for API failures

- [x] v1.6.0 release (YouTube Stats feature)
- [x] v1.6.1 release (Bug fixes)
- [x] v1.6.2 release (UI improvements)
- [x] v1.6.3 release (Memory optimizations)
- [x] v1.6.4 release (Error handling)
- [x] v1.6.5 release (Polish and stability)

### Current Device Status
- **Firmware**: v1.6.5
- **GitHub Release**: https://github.com/ryanmaule/epicweatherbox/releases/tag/v1.6.5
- **Device IP**: 192.168.4.235
- **OTA URL**: http://192.168.4.235/update
- **Admin URL**: http://192.168.4.235/admin
- **Build Stats**: RAM ~56%, Flash ~64%, ~25KB free heap

## Future Enhancements

Ideas for future releases (not yet planned):

### Hardware Upgrade Path
- **ESP32 Migration**: The ESP8266 is memory-constrained (~30KB free heap). An ESP32 version could enable:
  - GIF animation support (currently disabled due to memory limitations)
  - More weather data points
  - Faster processing
  - More simultaneous locations
  - Live web preview streaming

### Feature Ideas
- **Weather Alerts**: Display severe weather warnings
- **Historical Data**: Temperature/precipitation graphs
- **Astronomy Data**: Sunrise/sunset, moon phase
- **Air Quality Index**: PM2.5, AQI data
- **Sports Scores**: Live game updates
- **Calendar Integration**: Display upcoming events
- **MQTT Support**: Home automation integration
- **Timezone Auto-detection**: From IP geolocation
- **Multiple Custom Screens**: Different messages for different times of day

### UI Enhancements
- **Touch Support**: If hardware supports it
- **On-device Configuration**: Button-based setup without web UI
- **Screen Transitions**: Fade/slide animations between screens
- **Weather Icons**: Better icon set or animated weather graphics
- **Customizable Layouts**: User-defined screen arrangements
