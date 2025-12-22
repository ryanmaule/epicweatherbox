# EpicWeatherBox Development Plan

## Overview

This plan outlines the development approach for EpicWeatherBox firmware, focusing on building features on the NodeMCU test device with a web-based display preview before deploying to the actual SmallTV-Ultra hardware.

## Development Strategy

### Why Web Preview First?

1. **Safety** - NodeMCU has USB recovery; SmallTV-Ultra doesn't
2. **Speed** - Faster iteration without hardware display debugging
3. **Visibility** - See exactly what will render on the 240x240 screen
4. **Testing** - Validate weather data, layouts, and timing before hardware

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    EpicWeatherBox Firmware                   │
├─────────────────────────────────────────────────────────────┤
│  Weather API (Open-Meteo)                                   │
│  ├── 7-day forecast data                                    │
│  ├── Dual location support                                  │
│  └── Cached responses (save API calls)                      │
├─────────────────────────────────────────────────────────────┤
│  Display Renderer                                           │
│  ├── Weather screens (current, forecast, dual location)     │
│  ├── Time display                                           │
│  └── Outputs to: Web Canvas OR TFT_eSPI                     │
├─────────────────────────────────────────────────────────────┤
│  Web Admin Interface                                        │
│  ├── 240x240 canvas preview (simulates TFT screen)          │
│  ├── Location configuration                                 │
│  ├── Display settings (brightness, themes, intervals)       │
│  └── Real-time preview with auto-refresh                    │
├─────────────────────────────────────────────────────────────┤
│  Core Services                                              │
│  ├── WiFi Manager (AP setup + client mode)                  │
│  ├── NTP time sync                                          │
│  ├── OTA updates (ArduinoOTA + Web)                         │
│  └── LittleFS configuration storage                         │
└─────────────────────────────────────────────────────────────┘
```

---

## Phase A: Weather API Integration

**Goal**: Get real 7-day weather data from Open-Meteo (free, no API key required)

### Tasks

1. **Create weather.h interface**
   - Define WeatherData struct (temp, conditions, precipitation, wind)
   - Define ForecastDay struct (7 days of data)
   - Function declarations for fetch/parse

2. **Implement weather.cpp**
   - HTTP client for Open-Meteo API
   - JSON parsing with ArduinoJson
   - Caching to reduce API calls (20-min default interval)
   - Error handling and retry logic

3. **Add /api/weather endpoint**
   - Return current weather + 7-day forecast as JSON
   - Include both configured locations

4. **Dual location support**
   - Store two locations in LittleFS
   - Fetch weather for both on update interval
   - Configurable via web admin

### Open-Meteo API

```
GET https://api.open-meteo.com/v1/forecast
  ?latitude=47.6062
  &longitude=-122.3321
  &daily=temperature_2m_max,temperature_2m_min,precipitation_sum,weathercode,windspeed_10m_max
  &current_weather=true
  &temperature_unit=fahrenheit
  &windspeed_unit=mph
  &precipitation_unit=inch
  &timezone=auto
  &forecast_days=7
```

---

## Phase B: Web Admin with Display Preview

**Goal**: Build configuration UI with live 240x240 canvas that shows exactly what the TFT will display

### Tasks

1. **Create admin.html page**
   - Responsive layout for mobile/desktop
   - Navigation between settings sections
   - Consistent styling with existing pages

2. **Implement 240x240 canvas preview**
   - HTML5 canvas at exact display resolution
   - JavaScript rendering that mirrors TFT output
   - Auto-refresh every few seconds
   - Manual refresh button

3. **Location configuration UI**
   - City name input with geocoding lookup
   - Lat/long display (from geocoding)
   - Primary and secondary location
   - "Use current location" button (browser geolocation)

4. **Display settings UI**
   - Brightness slider (0-100%)
   - Display cycle interval (how long each screen shows)
   - Theme selection (when we add themes)
   - Temperature unit (F/C)
   - Time format (12h/24h)

5. **Preview rendering system**
   - `/api/preview` endpoint returns display state as JSON
   - Canvas draws: background, weather icons, text, time
   - Cycle through screens: Current → Forecast → Location 2

### Screen Layouts (240x240)

```
┌────────────────────────┐  ┌────────────────────────┐
│   CURRENT WEATHER      │  │   7-DAY FORECAST       │
│                        │  │                        │
│      ☀️ 72°F           │  │  Mon Tue Wed Thu Fri   │
│    Seattle, WA         │  │  68° 65° 70° 72° 68°   │
│                        │  │  ☀️  🌧️  ⛅  ☀️  🌧️    │
│  Wind: 5 mph           │  │                        │
│  Precip: 0%            │  │  Sat Sun               │
│                        │  │  64° 66°               │
│     12:34 PM           │  │  🌧️  ⛅                │
└────────────────────────┘  └────────────────────────┘

┌────────────────────────┐
│   DUAL LOCATION        │
│                        │
│  Seattle      Portland │
│    72°F         68°F   │
│     ☀️           🌧️     │
│                        │
│     12:34 PM           │
└────────────────────────┘
```

---

## Phase C: Display Abstraction Layer

**Goal**: Create rendering functions that work for both web preview and TFT hardware

### Tasks

1. **Create renderer.h interface**
   - Abstract drawing primitives (rect, text, image)
   - Screen layout functions (drawCurrentWeather, drawForecast, etc.)
   - Platform-agnostic color/font definitions

2. **Implement web_renderer.cpp**
   - Generates JSON describing what to draw
   - Canvas JavaScript interprets and renders
   - Used for preview mode on NodeMCU

3. **Implement tft_renderer.cpp** (Phase 2 completion)
   - Uses TFT_eSPI library
   - Same interface as web renderer
   - Activated when running on SmallTV hardware

4. **Display state machine**
   - Cycle through screens on timer
   - Configurable duration per screen
   - Smooth transitions (optional)

---

## Phase D: Final Integration

**Goal**: Deploy to SmallTV-Ultra with working display

### Tasks

1. Flash WeatherBuddy to SmallTV via web OTA
2. Verify display initializes correctly
3. Test all screens render properly
4. Validate OTA still works on real hardware
5. Final polish and bug fixes

---

## File Structure

```
src/
├── main.cpp              # Entry point, setup/loop
├── config.h              # Pin definitions, constants
├── ota.h / ota.cpp       # OTA update handling
├── weather.h / weather.cpp   # Weather API client
├── renderer.h            # Display abstraction interface
├── web_renderer.cpp      # Web preview renderer
├── tft_renderer.cpp      # TFT hardware renderer
└── admin.h / admin.cpp   # Web admin routes

data/                     # LittleFS filesystem
├── admin.html            # Web admin interface
├── admin.js              # Canvas rendering logic
├── admin.css             # Admin styles
└── config.json           # Saved configuration
```

---

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Device status page |
| `/admin` | GET | Web admin interface |
| `/update` | GET/POST | OTA firmware update |
| `/api/status` | GET | Device status JSON |
| `/api/weather` | GET | Current weather + forecast |
| `/api/config` | GET/POST | Read/write configuration |
| `/api/preview` | GET | Display state for canvas |
| `/api/locations` | GET/POST | Location management |
| `/reboot` | GET | Reboot device |
| `/reset` | GET | Factory reset |

---

## Timeline

| Phase | Description | Dependencies |
|-------|-------------|--------------|
| A | Weather API Integration | None |
| B | Web Admin with Preview | Phase A |
| C | Display Abstraction | Phase A, B |
| D | SmallTV Deployment | Phase A, B, C |

---

## Success Criteria

- [ ] Weather data fetches successfully from Open-Meteo
- [ ] 7-day forecast displays in web preview
- [ ] Dual location weather works
- [ ] Configuration persists across reboots
- [ ] Web preview matches intended TFT output
- [ ] Display cycles through screens automatically
- [ ] Final deployment to SmallTV works
