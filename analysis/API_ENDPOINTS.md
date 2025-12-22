# SmallTV-Ultra API Endpoints Documentation

This document details all discovered API endpoints from the live device at v9.0.40.

## Web Interface Pages

| Endpoint | Description | Status |
|----------|-------------|--------|
| `/` | Settings page (theme selection, brightness, factory reset, reboot, firmware update) | 200 |
| `/settings.html` | Same as root - alias | 200 |
| `/network.html` | WiFi network configuration | 200 |
| `/weather.html` | Weather settings, city, API keys, GIF upload | 200 |
| `/time.html` | Time settings, colors, 12/24h, NTP, DST | 200 |
| `/image.html` | Photo album/image settings | 200 |
| `/update` | OTA firmware update page | 200 |

## Static Assets

| Endpoint | Description | Gzipped |
|----------|-------------|---------|
| `/css/style.css` | Main stylesheet | Yes |
| `/js/settings.js` | JavaScript for all pages | Yes |
| `/js/jquery.min.js` | jQuery library | Yes |

## Configuration JSON Files (GET)

| Endpoint | Description | Content |
|----------|-------------|---------|
| `/v.json` | Version info | `{"m":"SmallTV-Ultra","v":"Ultra-V9.0.40"}` |
| `/config.json` | WiFi credentials | `{"a":"SSID","p":"****"}` |
| `/wifi.json` | WiFi scan results | `{"aps":[{...}]}` |
| `/city.json` | Weather location | `{"ct":"City","t":"-5","mt":"0","cd":"123456","loc":"City,CC"}` |
| `/theme_list.json` | Theme config | `{"list":"1,1,0,0,0,0,0","sw_en":"1","sw_i":"10"}` |
| `/key.json` | OpenWeatherMap API key | `{"key":"..."}` |
| `/fkey.json` | WeatherAPI.com key | `{"fkey":"..."}` |
| `/timebrt.json` | Night mode settings | `{"en":1,"t1":22,"t2":7,"b2":20}` |
| `/space.json` | Storage info | `{"total":3121152,"free":122380}` |

## File Management Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/filelist?dir=/gif` | GET | List files in /gif directory |
| `/filelist?dir=/image` | GET | List files in /image directory |
| `/doUpload?dir=/gif` | POST | Upload file to specified directory |
| `/delete?file=/gif/name.gif` | GET | Delete specified file |

## Settings API (`/set` endpoint)

All settings are changed via GET requests to `/set` with query parameters.

### Theme Settings
| Parameter | Description | Example |
|-----------|-------------|---------|
| `theme` | Set current theme (1-7) | `/set?theme=1` |
| `theme_list` | Themes for auto-switch | `/set?theme_list=1,1,0,0,0,0,0` |
| `sw_en` | Enable auto-switch | `/set?sw_en=1` |
| `theme_interval` | Auto-switch interval (seconds) | `/set?theme_interval=10` |

### Display Settings
| Parameter | Description | Example |
|-----------|-------------|---------|
| `brt` | Brightness (-10 to 100) | `/set?brt=50` |
| `t1` | Night mode start hour | `/set?t1=22` |
| `t2` | Night mode end hour | `/set?t2=7` |
| `b1` | Day brightness | `/set?b1=50` |
| `b2` | Night brightness | `/set?b2=20` |
| `en` | Enable night mode | `/set?en=1` |

### Weather Settings
| Parameter | Description | Example |
|-----------|-------------|---------|
| `cd1` | City name or ID | `/set?cd1=Seoul` or `/set?cd1=1835848` |
| `cd2` | Secondary (unused?) | `/set?cd2=1000` |
| `key` | OpenWeatherMap API key | `/set?key=xxx` |
| `fkey` | WeatherAPI.com API key | `/set?fkey=xxx` |
| `w_i` | Weather update interval (minutes) | `/set?w_i=20` |
| `t_u` | Temperature unit | `/set?t_u=°C` |
| `w_u` | Wind speed unit | `/set?w_u=m/s` |
| `p_u` | Pressure unit | `/set?p_u=hPa` |

### Time Settings
| Parameter | Description | Example |
|-----------|-------------|---------|
| `ntp` | NTP server | `/set?ntp=pool.ntp.org` |
| `hour` | 12-hour format (0/1) | `/set?hour=1` |
| `day` | Date format (1-5) | `/set?day=1` |
| `colon` | Enable colon blink | `/set?colon=1` |
| `dst` | Enable DST | `/set?dst=1` |
| `font` | Font style (1-2) | `/set?font=1` |
| `hc` | Hour color (hex) | `/set?hc=%23FFFFFF` |
| `mc` | Minute color (hex) | `/set?mc=%23FEBA01` |
| `sc` | Second color (hex) | `/set?sc=%23FF5900` |

### Media Settings
| Parameter | Description | Example |
|-----------|-------------|---------|
| `gif` | Set current GIF | `/set?gif=/gif/spaceman.gif` |
| `clear` | Clear directory contents | `/set?clear=gif` |

### System Settings
| Parameter | Description | Example |
|-----------|-------------|---------|
| `reset` | Factory reset | `/set?reset=1` |
| `reboot` | Reboot device | `/set?reboot=1` |

## Date Format Options

| Value | Format |
|-------|--------|
| 1 | DD/MM/YYYY |
| 2 | YYYY/MM/DD |
| 3 | MM/DD/YYYY |
| 4 | MM/DD |
| 5 | DD/MM |

## Theme Numbers

| Number | Theme Name |
|--------|------------|
| 1 | Weather Clock Today |
| 2 | Weather Forecast |
| 3 | Photo Album |
| 4 | Time Style 1 |
| 5 | Time Style 2 |
| 6 | Time Style 3 |
| 7 | Simple Weather Clock |

## External APIs Used

### Weather Data
1. **OpenWeatherMap** (current weather):
   - `http://api.openweathermap.org:80/data/2.5/weather?q={city}`
   - `http://api.openweathermap.org:80/data/2.5/weather?id={city_id}`

2. **WeatherAPI.com** (forecast):
   - `http://api.weatherapi.com/v1/forecast.json?key={key}&q={city}&days=3`

### Time Sync
- `http://worldtimeapi.org/api/timezone/UTC`
- NTP: `pool.ntp.org` (configurable)

## Storage Structure

Based on `/filelist` endpoint:

```
/
├── gif/              # GIF animations (80x80 recommended)
│   ├── spaceman.gif
│   └── ...
├── image/            # Photo album images (240x240)
│   ├── spaceman.gif
│   └── ...
├── css/
│   └── style.css
├── js/
│   ├── settings.js
│   └── jquery.min.js
└── [html files]
```

## Response Codes

| Response | Meaning |
|----------|---------|
| `OK` | Success |
| `fail` | Failure |
| `Empty` | No files in directory |

## Notes

1. All HTML/JS/CSS files are served gzip-compressed
2. Settings are persisted to SPIFFS JSON files
3. The `/set` endpoint returns "OK" on success
4. File upload uses multipart/form-data to `/doUpload`
5. Storage total: 3MB, allocate wisely for 7-day forecast data
