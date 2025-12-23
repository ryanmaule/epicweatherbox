# EpicWeatherBox

Custom open-source firmware for the GeekMagic SmallTV-Ultra, transforming it into a sleek dedicated weather station with 7-day forecasts and multi-location support.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP8266-orange.svg)
![Version](https://img.shields.io/badge/version-1.1.0-green.svg)

## Features

- **7-Day Weather Forecast** - Extended forecast with high/low temps and precipitation probability
- **Multi-Location Support** - Monitor up to 5 locations, cycling through each automatically
- **Custom Screen** - Display custom text messages in the rotation (header, body, footer)
- **Beautiful Display** - Clean, modern UI with weather icons and smooth fonts
- **Dark & Light Themes** - Auto-switches based on time of day, or set manually
- **Web-Based Admin** - Full configuration via browser with live display preview
- **Over-the-Air Updates** - Update firmware wirelessly via web interface
- **Night Mode** - Automatic dimming during nighttime hours
- **No API Key Required** - Uses free Open-Meteo weather API
- **Open Source** - Fully customizable, MIT licensed

## Display Screens

The display automatically cycles through multiple screens for each location:

1. **Current Weather** - Large temperature display, weather condition, high/low temps, and current time
2. **3-Day Forecast** - Days 1-3 with weather icons and temperature ranges
3. **Extended Forecast** - Days 4-6 with weather icons and temperature ranges
4. **Custom Screen** (optional) - Configurable text message after each location's weather screens
   - **Header**: Current time (left) + custom text (right, max 16 chars)
   - **Body**: Centered text with dynamic font sizing (max 160 chars)
   - **Footer**: Rounded box with custom text (max 30 chars)

> **Tip**: Visit `http://<device-ip>/preview` for a live browser simulation of the display!

## Hardware

- **Device**: [GeekMagic SmallTV-Ultra](https://geekmagicclock.com/)
- **MCU**: ESP8266 (ESP-12E) @ 160MHz
- **Display**: 1.54" 240x240 IPS TFT (ST7789)
- **Connectivity**: WiFi 802.11 b/g/n

> **Note**: This firmware is designed specifically for the SmallTV-Ultra. The USB-C port is power-only; all programming is done via WiFi OTA.

## Quick Start

### Option 1: Flash Pre-built Firmware (Easiest)

1. Download the latest `firmware.bin` from [Releases](https://github.com/ryanmaule/epicweatherbox/releases)
2. If your device has stock firmware, go to `http://<device-ip>/update`
3. Upload the `firmware.bin` file
4. Wait for reboot, then connect to the `EpicWeatherBox` WiFi network
5. Configure your home WiFi through the captive portal
6. Done! Access the admin panel at `http://<new-device-ip>/admin`

### Option 2: Build from Source

#### Prerequisites

- [PlatformIO](https://platformio.org/) CLI or VSCode extension
- Git

#### Build & Flash

```bash
# Clone the repository
git clone https://github.com/ryanmaule/epicweatherbox.git
cd epicweatherbox

# Build firmware
pio run

# The firmware binary will be at:
# .pio/build/esp8266/firmware.bin

# Upload via web interface at http://<device-ip>/update
```

## Configuration

### Web Admin Panel

Access the admin panel at `http://<device-ip>/admin` to configure:

- **Locations** - Add up to 5 cities with geocoded search
- **Custom Screen** - Optional text screen that appears after each location's weather
  - Configurable header text (shows alongside time)
  - Body text with automatic font sizing based on length
  - Footer text in a styled rounded box
  - Enable/disable toggle
- **Temperature Units** - Celsius or Fahrenheit
- **Display Settings** - Brightness, screen cycle time
- **Night Mode** - Start/end hours, dimmed brightness
- **Theme** - Auto, always dark, or always light
- **Display Position** - Vertical nudge for frame alignment

### Display Preview

Visit `http://<device-ip>/preview` to see a live simulation of what's shown on the device.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Device home page |
| `/admin` | GET | Admin configuration panel |
| `/preview` | GET | Live display preview |
| `/update` | GET | Firmware update page |
| `/api/status` | GET | Device status (uptime, heap, version) |
| `/api/config` | GET/POST | Get or set configuration |
| `/api/weather` | GET | Current weather data for all locations |
| `/api/weather/refresh` | GET | Force weather data refresh |
| `/api/search?q=city` | GET | Search for cities by name |
| `/api/safemode` | GET | Enter emergency safe mode |
| `/api/safemode/exit` | GET | Exit safe mode |
| `/reboot` | GET | Reboot device |
| `/reset` | GET | Factory reset (clears WiFi) |

## Emergency Safe Mode

If the device gets stuck in a reboot loop, you can activate safe mode:

```bash
curl http://<device-ip>/api/safemode
```

This freezes the display (showing a warning screen with the IP address), stops all processing except the web server, and allows you to upload new firmware via `/update`.

> **Note**: The safe mode screen layout was improved in v1.1.0 to prevent IP address cutoff when using the UI nudge feature.

## Technical Details

### Memory Usage

- **Flash**: ~54% of 1MB
- **RAM**: ~51% of 80KB
- **Free Heap**: ~30KB during normal operation

### Weather Data

- **Source**: [Open-Meteo API](https://open-meteo.com/) (free, no registration)
- **Update Interval**: Every 20 minutes
- **Forecast Days**: 7 days
- **Data Includes**: Temperature, conditions, precipitation, wind

### Display Specifications

- **Resolution**: 240x240 pixels
- **Controller**: ST7789
- **Color Format**: RGB565 (BGR order)
- **Backlight**: PWM controlled on GPIO5

## Project Structure

```
epicweatherbox/
├── src/
│   ├── main.cpp        # Main firmware (web server, display, setup/loop)
│   ├── weather.cpp     # Weather API integration & config management
│   ├── weather.h       # Weather data structures & function declarations
│   └── ota.h           # Over-the-air update handling
├── images/             # Weather icons (WMO format)
├── platformio.ini      # Build configuration
├── CLAUDE.md           # Development notes
└── README.md           # This file
```

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

### Development Setup

1. Clone the repo
2. Open in VSCode with PlatformIO extension
3. Build with `pio run`
4. Upload via web interface or OTA

## Troubleshooting

### Device won't connect to WiFi
- Hold the device's button during boot to reset WiFi settings
- Or visit `http://<device-ip>/reset` if accessible

### Display is cut off by frame
- Use the "UI Nudge" slider in Admin panel to shift the display up/down

### Weather not updating
- Check WiFi connection at `http://<device-ip>/api/status`
- Force refresh at `http://<device-ip>/api/weather/refresh`

### Device stuck in reboot loop
- Quickly access `http://<device-ip>/api/safemode` after a reboot
- Then upload working firmware via `/update`

## Version History

### v1.1.0 (2025-12-23)
- **New Feature**: Custom Text Screen
  - Configurable screen that appears after each location's weather screens
  - Header: Time (left) + custom text (right, max 16 chars)
  - Body: Centered text with dynamic font sizing (max 160 chars)
  - Footer: Rounded box with custom text (max 30 chars)
  - Configure via Admin panel → Custom tab
- **UI Improvements**:
  - Fixed Safe Mode screen layout to prevent IP address cutoff with UI nudge
  - Added Project Links section to home page (GitHub repo, releases, issues)
  - Shows current firmware version with upgrade instructions
  - Footer links reorganized for better navigation

### v1.0.0 (2025-12-23)
- Initial public release
- 7-day weather forecast for up to 5 locations
- Web-based admin panel with live display preview
- Dark/light theme modes with auto-switching
- Night mode with automatic dimming
- Over-the-air firmware updates
- Emergency safe mode for recovery

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
