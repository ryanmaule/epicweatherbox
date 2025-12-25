# EpicWeatherBox

Custom open-source firmware for the GeekMagic SmallTV-Ultra, transforming it into a sleek dedicated weather station with 7-day forecasts, multi-location support, and YouTube stats.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP8266-orange.svg)
![Version](https://img.shields.io/badge/version-1.6.5-green.svg)

## Features

- **7-Day Weather Forecast** - Extended forecast with high/low temps and precipitation probability
- **Multi-Location Support** - Monitor up to 3 weather locations, cycling through each automatically
- **YouTube Stats** - Display your channel's subscriber count, views, and video count
- **Countdown Timers** - Track days until birthdays, holidays, or custom events
- **Custom Text Screens** - Display custom messages in the rotation
- **Unified Carousel** - Drag-and-drop reordering of all screen types
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

## Hardware

- **Device**: [GeekMagic SmallTV-Ultra](https://geekmagicclock.com/)
- **MCU**: ESP8266 (ESP-12E) @ 160MHz
- **Display**: 1.54" 240x240 IPS TFT (ST7789)
- **Connectivity**: WiFi 802.11 b/g/n

> **Note**: The USB-C port is power-only; all programming is done via WiFi OTA.

## Quick Start

### Option 1: Flash Pre-built Firmware (Easiest)

1. Download the latest `firmware.bin` from [Releases](https://github.com/ryanmaule/epicweatherbox/releases)
2. If your device has stock firmware, go to `http://<device-ip>/update`
3. Upload the `firmware.bin` file
4. Wait for reboot, then connect to the `EpicWeatherBox` WiFi network
5. Configure your home WiFi through the captive portal
6. Access the admin panel at `http://<new-device-ip>/admin`

### Option 2: Build from Source

```bash
# Clone the repository
git clone https://github.com/ryanmaule/epicweatherbox.git
cd epicweatherbox

# Build firmware (requires PlatformIO)
pio run

# The firmware binary will be at:
# .pio/build/esp8266/firmware.bin

# Upload via web interface at http://<device-ip>/update
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

- **Resolution**: 240x240 pixels
- **Controller**: ST7789
- **Color Format**: RGB565
- **Backlight**: PWM controlled

## Project Structure

```
epicweatherbox/
├── src/
│   ├── main.cpp        # Main firmware (web server, display, setup/loop)
│   ├── weather.cpp/h   # Weather API & config management
│   ├── themes.cpp/h    # Theme system with built-in and custom themes
│   ├── ota.cpp/h       # Over-the-air update handling
│   ├── config.h        # Configuration constants
│   └── admin_html.h    # Compressed admin panel HTML
├── data/
│   └── admin.html      # Admin panel source
├── platformio.ini      # Build configuration
└── README.md           # This file
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
