# SmallTV-Ultra Custom Firmware Implementation Plan

## Overview

Based on research of existing open-source implementations, this plan outlines the architecture for a "spiritual successor" firmware focused on weather display with 7-day forecast and dual location support.

## Key Decision: Open-Meteo API

**Chosen API**: [Open-Meteo](https://open-meteo.com/)

**Reasons**:
1. **FREE** - No API key required, no rate limits for personal use
2. **7+ day forecast** included in free tier
3. **No authentication** - simpler code, no key management
4. **Proven on ESP8266** - AlexeyMal's implementation works well
5. **Reliable** - Uses data from national weather services

**API Endpoint**:
```
https://api.open-meteo.com/v1/forecast?
  latitude={lat}&longitude={lon}&
  current=temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m&
  daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum&
  forecast_days=7&
  timezone=auto
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        SmallTV-Ultra Custom                      │
├─────────────────────────────────────────────────────────────────┤
│  main.cpp                                                        │
│  ├── Setup: WiFi → NTP → Weather → Display                      │
│  └── Loop: Update weather (20min) → Update display (1sec)       │
├─────────────────────────────────────────────────────────────────┤
│  Modules:                                                        │
│  ├── wifi_manager.cpp    - WiFiManager + captive portal         │
│  ├── weather_client.cpp  - Open-Meteo API + JSON parsing        │
│  ├── display.cpp         - TFT_eSPI + weather rendering         │
│  ├── time_manager.cpp    - NTP + timezone handling              │
│  ├── config_manager.cpp  - SPIFFS JSON config load/save         │
│  └── web_server.cpp      - Settings interface (/set endpoint)   │
├─────────────────────────────────────────────────────────────────┤
│  Libraries:                                                      │
│  ├── TFT_eSPI           - ST7789 display driver                 │
│  ├── ArduinoJson        - JSON parsing                          │
│  ├── WiFiManager        - WiFi setup portal                     │
│  ├── NTPClient          - Time sync                             │
│  ├── Timezone           - DST handling                          │
│  └── ESPAsyncWebServer  - Web configuration                     │
└─────────────────────────────────────────────────────────────────┘
```

## Module Specifications

### 1. Weather Client (`weather_client.cpp`)

**Data Structures**:
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
    char dayName[4];  // "Mon", "Tue", etc.
};

struct WeatherData {
    CurrentWeather current;
    DailyForecast daily[7];  // 7-day forecast
    unsigned long lastUpdate;
    bool valid;
};

// For dual location support
struct Location {
    char name[32];
    float latitude;
    float longitude;
    char timezone[32];
};
```

**Key Functions**:
```cpp
bool fetchWeather(Location& loc, WeatherData& data);
int weatherCodeToIcon(int code);  // Maps WMO codes to icons
const char* weatherCodeToText(int code);
```

**Weather Code Mapping** (WMO codes):
| Code | Description | Icon |
|------|-------------|------|
| 0 | Clear sky | ☀️ |
| 1-3 | Partly cloudy | ⛅ |
| 45-48 | Fog | 🌫️ |
| 51-55 | Drizzle | 🌧️ |
| 61-65 | Rain | 🌧️ |
| 71-77 | Snow | ❄️ |
| 80-82 | Rain showers | 🌦️ |
| 95-99 | Thunderstorm | ⛈️ |

### 2. Display Manager (`display.cpp`)

**Themes** (simplified from original):
1. **Weather Now** - Current conditions + 7-day forecast
2. **Dual Location** - Two locations side by side
3. **Clock + Weather** - Large time with current conditions

**Screen Layout for 240x240**:

```
Theme 1: Weather Now (7-day)
┌────────────────────────┐
│  Location Name    12:34│  (20px header)
├────────────────────────┤
│       ☀️               │
│      23°C              │  (100px current)
│    Sunny               │
├────────────────────────┤
│Mon Tue Wed Thu Fri Sat Sun│
│☀️  ⛅  🌧️  🌧️  ☀️  ☀️  ⛅ │  (100px forecast)
│25  24  22  20  23  25  24 │
│18  17  15  14  16  18  17 │
└────────────────────────┘

Theme 2: Dual Location
┌───────────┬────────────┐
│  Aurora   │   Seoul    │
│    ☀️     │    ⛅      │
│   23°C    │   18°C     │
├───────────┴────────────┤
│  7-day for Location 1  │
└────────────────────────┘

Theme 3: Clock + Weather
┌────────────────────────┐
│                        │
│       12:34            │  (Large clock)
│      Dec 18            │
├────────────────────────┤
│  ☀️ 23°C  Sunny        │
│  Aurora, CA            │
└────────────────────────┘
```

**Key Functions**:
```cpp
void initDisplay();
void setBacklight(uint8_t brightness);  // PWM on GPIO5
void drawCurrentWeather(WeatherData& data);
void draw7DayForecast(WeatherData& data);
void drawDualLocation(WeatherData& loc1, WeatherData& loc2);
void drawClock(time_t now);
void drawWeatherIcon(int x, int y, int code, int size);
```

### 3. Time Manager (`time_manager.cpp`)

```cpp
void initTime();
void updateTime();  // Call from loop
time_t getLocalTime();
String getFormattedTime();  // "12:34"
String getFormattedDate();  // "Dec 18"
String getDayName(time_t t); // "Mon"
```

### 4. Config Manager (`config_manager.cpp`)

**Config File**: `/config.json`
```json
{
  "version": 1,
  "locations": [
    {"name": "Aurora", "lat": 51.5, "lon": -0.1, "tz": "America/Toronto"},
    {"name": "Seoul", "lat": 37.5, "lon": 127.0, "tz": "Asia/Seoul"}
  ],
  "theme": 1,
  "brightness": 50,
  "nightMode": {
    "enabled": true,
    "start": 22,
    "end": 7,
    "brightness": 20
  },
  "updateInterval": 20,
  "use24Hour": true
}
```

### 5. Web Server (`web_server.cpp`)

**Endpoints** (compatible with original where possible):
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Main settings page |
| `/set` | GET | Apply settings (query params) |
| `/config.json` | GET | Current config |
| `/v.json` | GET | Version info |
| `/reboot` | GET | Reboot device |

**Example**: `/set?lat1=51.5&lon1=-0.1&name1=Aurora&theme=2&brt=75`

## Implementation Phases

### Phase 0: Safe OTA Foundation (CRITICAL)
Since we're flashing via the stock firmware's OTA update, we MUST ensure our firmware:
- [ ] Includes working OTA update capability (ArduinoOTA or web-based)
- [ ] Connects to WiFi reliably
- [ ] Has a fallback/recovery mechanism
- [ ] Test OTA update works BEFORE adding complex features

**Risk mitigation**: If our firmware bricks the device (no WiFi, no OTA), recovery requires soldering.

### Phase 1: Basic Display (Week 1)
- [ ] TFT_eSPI initialization with correct pins
- [ ] Backlight control (PWM)
- [ ] Draw test patterns
- [ ] Display "Hello World"

### Phase 2: WiFi + Time (Week 1)
- [ ] WiFiManager integration
- [ ] NTP time sync
- [ ] Display clock on screen

### Phase 3: Weather API (Week 2)
- [ ] Open-Meteo API client
- [ ] JSON parsing (streaming for memory)
- [ ] Current weather display
- [ ] 7-day forecast display

### Phase 4: Dual Location (Week 2)
- [ ] Second location config
- [ ] Alternating or split display
- [ ] Location switching

### Phase 5: Web Interface (Week 3)
- [ ] Serve settings page
- [ ] /set endpoint
- [ ] Config persistence (SPIFFS)

### Phase 6: Polish (Week 3-4)
- [ ] Weather icons (custom bitmaps)
- [ ] Night mode
- [ ] Error handling
- [ ] OTA updates

## Memory Budget

| Component | Flash | RAM |
|-----------|-------|-----|
| ESP8266 Core + WiFi | ~300KB | ~30KB |
| TFT_eSPI | ~20KB | ~2KB |
| ArduinoJson | ~15KB | ~1KB (streaming) |
| Weather data (2 loc) | - | ~2KB |
| Display buffer | - | ~1KB |
| Web server | ~20KB | ~5KB |
| **Total** | ~355KB | ~41KB |
| **Available** | 1MB | 80KB |
| **Margin** | 645KB | 39KB |

## File Structure

```
src/
├── main.cpp              # Entry point
├── config.h              # Pin definitions, constants
├── weather_client.h/cpp  # Open-Meteo API
├── display.h/cpp         # TFT rendering
├── time_manager.h/cpp    # NTP + timezone
├── config_manager.h/cpp  # SPIFFS config
├── web_server.h/cpp      # Settings interface
└── icons.h               # Weather icon bitmaps

data/                     # SPIFFS filesystem
├── config.json           # User settings
├── index.html            # Settings page
└── style.css             # Styles
```

## Testing Checklist

- [ ] Display initializes correctly
- [ ] Backlight brightness control works
- [ ] WiFi captive portal appears on first boot
- [ ] WiFi reconnects after power cycle
- [ ] NTP syncs time correctly
- [ ] Timezone/DST handled properly
- [ ] Open-Meteo API returns data
- [ ] 7-day forecast displays correctly
- [ ] Dual location works
- [ ] Web settings save correctly
- [ ] Device survives 24+ hours running
- [ ] Memory usage stable (no leaks)

## References

- [AlexeyMal ESP8266 Weather Station](https://github.com/AlexeyMal/esp8266-weather-station) - Open-Meteo implementation
- [fazibear/pix](https://github.com/fazibear/pix) - TFT_eSPI config
- [Open-Meteo API Docs](https://open-meteo.com/en/docs)
- [WMO Weather Codes](https://open-meteo.com/en/docs#weathervariables)
