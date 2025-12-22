# EpicWeatherBox

Custom open-source firmware for the GeekMagic SmallTV-Ultra, transforming it into a dedicated weather station with 7-day forecasts and dual location support.

## Features

- **7-Day Weather Forecast** - Extended forecast vs. the original 3-day limit
- **Dual Location Support** - Monitor weather in two cities simultaneously
- **Web-Based Configuration** - Easy setup via browser, no app required
- **Over-the-Air Updates** - Update firmware wirelessly
- **Open Source** - Customize and extend as you like

## Hardware

- **Device**: [GeekMagic SmallTV-Ultra](https://geekmagicclock.com/)
- **MCU**: ESP8266 (ESP-12E)
- **Display**: 1.54" 240x240 IPS TFT (ST7789)

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- SmallTV-Ultra device (or NodeMCU for development)

### Building

```bash
# Clone the repository
git clone https://github.com/ryanmaule/epicweatherbox.git
cd epicweatherbox

# Build firmware
pio run

# Upload via USB (first time, or NodeMCU)
pio run --target upload

# Upload via OTA (after initial flash)
pio run --target upload --upload-port <device-ip>
```

### First-Time Setup

1. Flash the firmware via USB
2. Connect to the `EpicWeatherBox` WiFi network
3. Configure your home WiFi through the captive portal
4. Access the web interface at `http://<device-ip>/`

## Project Structure

```
epicweatherbox/
├── src/
│   ├── main.cpp      # Main firmware entry point
│   ├── config.h      # Hardware configuration
│   ├── ota.h/.cpp    # Over-the-air update handling
│   └── ...
├── data/             # Web interface files (LittleFS)
├── docs/             # Documentation
└── platformio.ini    # Build configuration
```

## Development Status

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | OTA-capable base firmware | Complete |
| 2 | Weather API integration | In Progress |
| 3 | Web admin with display preview | Planned |
| 4 | Display driver | Planned |
| 5 | SmallTV deployment | Planned |

## API

The firmware uses [Open-Meteo](https://open-meteo.com/) for weather data - free, no API key required!

## Web Endpoints

| Endpoint | Description |
|----------|-------------|
| `/` | Device status page |
| `/update` | Firmware update page |
| `/api/status` | Device status JSON |
| `/api/time` | Current time JSON |
| `/reboot` | Reboot device |
| `/reset` | Factory reset |

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

- [GeekMagic](https://geekmagicclock.com/) for the SmallTV-Ultra hardware
- [Open-Meteo](https://open-meteo.com/) for free weather API
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) library
- [WiFiManager](https://github.com/tzapu/WiFiManager) library
