# EpicWeatherBox

Custom open-source firmware for the GeekMagic SmallTV-Ultra and ESP32-2432S028 (CYD), transforming them into sleek dedicated weather stations with 7-day forecasts, multi-location support, and YouTube stats.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP8266%20%7C%20ESP32-orange.svg)
![Version](https://img.shields.io/badge/version-1.10.15-green.svg)

## Features

- **7-Day Weather Forecast** - Extended forecast with high/low temps and precipitation probability
- **Multi-Location Support** - Monitor up to 3 weather locations, cycling through each automatically
- **YouTube Stats** - Display your channel's subscriber count, views, and video count
- **Custom Image Screens** - Upload up to 3 JPG images to display in rotation
- **Countdown Timers** - Track days until birthdays, holidays, or custom events
- **Custom Text Screens** - Display custom messages in the rotation
- **Unified Carousel** - Drag-and-drop reordering of all screen types with inline editing
- **Theme System** - Classic, Sunset, or fully custom color themes
- **Dark & Light Modes** - Auto-switches based on time of day, or set manually
- **Web-Based Admin** - Full configuration via browser with live display preview
- **Over-the-Air Updates** - Update firmware wirelessly via web interface
- **Night Mode** - Automatic dimming during nighttime hours
- **No Weather API Key Required** - Uses free Open-Meteo weather API
- **Open Source** - Fully customizable, MIT licensed

## Screen Types

The display cycles through screens in your configured carousel order:

### Weather Screens (per location)
1. **Current Weather** - Large temperature, conditions, high/low, current time
2. **3-Day Forecast** - Days 1-3 with icons and temperature ranges
3. **Extended Forecast** - Days 4-6 with icons and temperature ranges

### Countdown Screens
- Track days until events with themed icons (birthday cake, Christmas tree, etc.)
- Auto-calculates Easter date each year
- Custom dates with calendar icon showing the day number

### Custom Text Screens
- Header: Time + custom text + star icon
- Body: Centered text with dynamic font sizing (up to 160 chars)
- Footer: Styled rounded box with custom text

### YouTube Stats Screen
- Large YouTube-style play button logo
- Channel name
- Subscriber count prominently displayed
- Views and videos in side-by-side cards

## Supported Hardware

### SmallTV-Ultra (ESP8266)

- **Device**: [GeekMagic SmallTV-Ultra](https://geekmagicclock.com/)
- **MCU**: ESP8266 (ESP-12E) @ 160MHz
- **Display**: 1.54" 240x240 IPS TFT (ST7789)
- **Connectivity**: WiFi 802.11 b/g/n
- **Programming**: WiFi OTA only (USB-C is power-only)

### CYD — ESP32-2432S028 (ESP32)

- **Device**: ESP32-2432S028 ("Cheap Yellow Display")
- **MCU**: ESP32 @ 240MHz
- **Display**: 2.8" 240x320 IPS TFT (ILI9341)
- **Connectivity**: WiFi 802.11 b/g/n
- **Programming**: USB serial (CH340) or WiFi OTA

## Quick Start

### SmallTV-Ultra (ESP8266)

#### Option 1: Flash Pre-built Firmware (Easiest)

1. Download the latest `firmware.bin` from [Releases](https://github.com/ryanmaule/epicweatherbox/releases)
2. If your device has stock firmware, go to `http://<device-ip>/update`
3. Upload the `firmware.bin` file
4. Wait for reboot, then connect to the `EpicWeatherBox` WiFi network
5. Configure your home WiFi through the captive portal
6. Access the admin panel at `http://<new-device-ip>/admin`

#### Upgrading from Stock SmallTV-Ultra Firmware

If you get a "Not Enough Space" error when flashing from stock GeekMagic firmware, you'll need to use the **recovery firmware** first:

1. Download `recovery.bin` from [Releases](https://github.com/ryanmaule/epicweatherbox/releases)
2. Flash `recovery.bin` via the stock firmware's update page (`http://<device-ip>/update`)
3. Connect to the `EpicWeatherBox-Recovery` WiFi network (open, no password)
4. A captive portal will automatically open with two options:
   - **Upload Firmware**: Flash directly from the recovery network
   - **Join Network**: Connect the device to your home WiFi first, then flash from your regular network
5. Upload the full `firmware.bin`
6. Device will reboot with full EpicWeatherBox firmware

> **Why is this needed?** The stock firmware uses more flash space than our recovery firmware. The recovery image is minimal (~350KB) and clears the filesystem, making room for the full EpicWeatherBox firmware (~670KB).

### CYD — ESP32-2432S028 (ESP32)

The CYD is flashed via USB since it has a CH340 serial chip.

#### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- USB cable connected to the CYD

#### Flash Steps

```bash
# Clone the repository
git clone https://github.com/ryanmaule/epicweatherbox.git
cd epicweatherbox

# Build and flash the CYD firmware via USB
pio run -e cyd --target upload

# Upload the LittleFS filesystem (for admin panel)
pio run -e cyd --target uploadfs
```

After flashing:

1. Connect to the `EpicWeatherBox` WiFi network
2. Configure your home WiFi through the captive portal
3. Access the admin panel at `http://<device-ip>/admin`
4. Subsequent updates can be done via WiFi OTA at `http://<device-ip>/update`

> **Note**: The CYD requires a one-time patch to the TFT_eSPI library after installing dependencies. Add `#ifndef TFT_WIDTH` / `#ifndef TFT_HEIGHT` guards around the defines in `.pio/libdeps/cyd/TFT_eSPI/TFT_Drivers/ILI9341_Defines.h`. Without this, the display dimensions won't be set correctly. See `CLAUDE.md` for details.

### Build from Source (Either Platform)

```bash
# Clone the repository
git clone https://github.com/ryanmaule/epicweatherbox.git
cd epicweatherbox

# Build for SmallTV-Ultra (ESP8266) — default
pio run -e esp8266

# Build for CYD (ESP32)
pio run -e cyd

# Firmware binaries:
# .pio/build/esp8266/firmware.bin  (SmallTV-Ultra)
# .pio/build/cyd/firmware.bin      (CYD)
```

## Configuration

### Web Admin Panel

Access the admin panel at `http://<device-ip>/admin` to configure:

- **Carousel** - Drag-and-drop screen ordering (up to 9 screens total)
- **Locations** - Add up to 3 weather locations with geocoded search
- **Countdown** - Add up to 3 countdown timers for events
- **Custom Screens** - Add up to 3 custom text screens
- **YouTube** - Configure API key and channel for stats display
- **Display Settings** - Brightness, screen cycle time, temperature units
- **Theme** - Choose Classic, Sunset, or create custom colors
- **Night Mode** - Start/end hours, dimmed brightness
- **Display Position** - Vertical nudge for frame alignment

### YouTube Stats Setup

1. Get a YouTube Data API v3 key from [Google Cloud Console](https://console.cloud.google.com/)
2. Click the YouTube button in the admin panel
3. Enter your API key and channel handle (e.g., `@YourChannel`)
4. Click "Add to Carousel" to include in rotation

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/admin` | GET | Admin configuration panel |
| `/update` | GET | Firmware update page |
| `/api/status` | GET | Device status (uptime, heap, version) |
| `/api/config` | GET/POST | Get or set configuration |
| `/api/weather` | GET | Current weather data |
| `/api/weather/refresh` | GET | Force weather data refresh |
| `/api/youtube` | GET/POST | YouTube configuration and stats |
| `/api/youtube/refresh` | GET | Force YouTube stats refresh |
| `/api/themes` | GET/POST | Theme configuration |
| `/api/search?q=city` | GET | Search for cities by name |
| `/api/safemode` | GET | Enter emergency safe mode |
| `/reboot` | GET | Reboot device |
| `/reset` | GET | Factory reset |

## Emergency Safe Mode

If the device gets stuck in a reboot loop:

```bash
curl http://<device-ip>/api/safemode
```

This freezes the display and allows firmware upload via `/update`.

## Technical Details

### Memory Usage (v1.6.5)

- **Flash**: ~64% of 1MB
- **RAM**: ~56% of 80KB
- **Free Heap**: ~25KB during normal operation

### Weather Data

- **Source**: [Open-Meteo API](https://open-meteo.com/) (free, no registration)
- **Update Interval**: Every 20 minutes
- **Forecast Days**: 7 days

### YouTube Data

- **Source**: YouTube Data API v3 (requires free API key)
- **Update Interval**: Every 30 minutes
- **Quota Usage**: 1 unit per refresh (10,000 units/day free)

### Display Specifications

| | SmallTV-Ultra | CYD |
|--|--|--|
| **Resolution** | 240x240 | 240x320 (240x240 UI area) |
| **Controller** | ST7789 | ILI9341 |
| **Color Format** | RGB565 | RGB565 |
| **Backlight** | PWM (GPIO5, inverted) | PWM (GPIO21) |

## Project Structure

```
epicweatherbox/
├── src/
│   ├── main.cpp          # Main firmware (web server, display, setup/loop)
│   ├── weather.cpp/h     # Weather API & config management
│   ├── themes.cpp/h      # Theme system with built-in and custom themes
│   ├── ota.cpp/h         # Over-the-air update handling
│   ├── config.h          # Configuration constants & platform defines
│   ├── platform.h        # Platform abstraction (ESP8266/ESP32)
│   ├── display_layout.h  # Layout constants derived from config
│   └── admin_html.h      # Compressed admin panel HTML
├── data/
│   └── admin.html        # Admin panel source
├── platformio.ini        # Build config (esp8266 + cyd environments)
└── README.md             # This file
```

## Troubleshooting

### Device won't connect to WiFi
- Visit `http://<device-ip>/reset` to clear WiFi settings
- Or wait for the device to create the `EpicWeatherBox` AP

### Display is cut off by frame
- Use the "UI Nudge" slider in Admin panel to adjust

### Weather not updating
- Check status at `http://<device-ip>/api/status`
- Force refresh at `http://<device-ip>/api/weather/refresh`

### YouTube stats not loading
- Verify API key is correct
- Check channel handle format (e.g., `@ChannelName`)
- Ensure sufficient free heap (~20KB needed for HTTPS)

### Device stuck in reboot loop
- Quickly access `http://<device-ip>/api/safemode` after reboot
- Upload working firmware via `/update`

## Version History

### v1.10.15 (2026-02-21)
- **CYD (ESP32-2432S028) Support** - Second hardware platform with ILI9341 240x320 display
- Platform abstraction layer for ESP8266/ESP32 (WiFi, PWM, watchdog, filesystem)
- Display layout constants with configurable X/Y offsets for different panel sizes
- Fixed boot screen smile rendering and geocode search crash/ranking

### v1.10.12 (2025-12-28)
- **Custom Image Screens** - Upload up to 3 JPG images to display in rotation
- Header text for image screens (optional label in top-right)
- Streaming JPEG decode for memory efficiency
- Fixed admin panel initialization race condition
- Orphaned image cleanup on carousel save

### v1.8.1 (2025-12-26)
- **Enhanced Recovery Mode** - Captive portal with WiFi join option before flashing
- **Styled WiFi Setup** - WiFiManager captive portal now matches EpicWeatherBox dark theme

### v1.8.0 (2025-12-26)
- **Night Mode UI restored** - Enable/disable, start/end times, and brightness settings
- **Sunrise/Sunset options** - Night mode can start at sunset and end at sunrise using weather data
- **Recovery firmware** - Minimal firmware for upgrading from stock SmallTV-Ultra
- Weather API now fetches sunrise/sunset times from Open-Meteo

### v1.7.0 (2024-12-25)
- Location search with city dropdown (uses Open-Meteo geocoding API)
- Edit button on carousel items for inline editing
- Add buttons now disabled at limits instead of hidden

### v1.6.5 (2024-12-24)
- YouTube Stats screen with channel subscriber/view/video counts
- HTTPS memory optimization for ESP8266 (reduced SSL buffers)
- YouTube-style logo and improved layout

### v1.5.0 (2024-12-24)
- Theme system with Classic, Sunset, and Custom themes
- 14 customizable colors (7 dark + 7 light mode)
- Theme tab in admin panel with color pickers

### v1.3.0 (2024-12-24)
- Unified carousel system with drag-and-drop reordering
- Countdown screens with themed icons
- Large 48px countdown icons drawn with TFT primitives
- Admin UI redesign with modern layout

### v1.1.0 (2024-12-23)
- Custom text screen feature
- Dynamic font sizing based on text length
- Safe mode screen layout improvements

### v1.0.0 (2024-12-23)
- Initial public release
- 7-day weather forecast for multiple locations
- Web-based admin panel with live preview
- Dark/light theme modes
- Over-the-air firmware updates

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- [GeekMagic](https://geekmagicclock.com/) for the SmallTV-Ultra hardware
- [Open-Meteo](https://open-meteo.com/) for the free weather API
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) display library
- [WiFiManager](https://github.com/tzapu/WiFiManager) captive portal library
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) JSON library

---

**Made with love for weather enthusiasts everywhere.**
