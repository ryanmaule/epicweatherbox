# Live Device Access Methods

Since you have the firmware installed and running on your device, we can attempt several methods to extract data and understand the system better.

## Method 1: Network Service Discovery

### Find Device IP
First, identify your device's IP address on your network.

```bash
# If device is in client mode, find it on your network
# Replace with your network range
nmap -sP 192.168.1.0/24 | grep -B2 "SmallTV\|Espressif\|ESP"

# Or check your router's DHCP client list
```

### Port Scanning
Check what services are running:

```bash
# Replace DEVICE_IP with your device's IP
DEVICE_IP="192.168.1.xxx"

# Quick scan of common ports
nmap -p 22,23,80,81,443,8080,8081,8266 $DEVICE_IP

# Full port scan (takes longer)
nmap -p- $DEVICE_IP

# Service version detection
nmap -sV -p 22,23,80,8080,8266 $DEVICE_IP
```

**Ports to look for:**
- **22**: SSH (unlikely but possible)
- **23**: Telnet (sometimes enabled for debugging)
- **80**: Web server (known to be present)
- **8080**: Alternative web server
- **8266**: Common ESP8266 debug port

## Method 2: Web Interface Exploration

### Enumerate All Endpoints
Use the web app to discover hidden endpoints:

```bash
DEVICE_IP="192.168.1.xxx"

# Test known endpoints from firmware strings
curl -s "http://$DEVICE_IP/"
curl -s "http://$DEVICE_IP/settings.html"
curl -s "http://$DEVICE_IP/network.html"
curl -s "http://$DEVICE_IP/weather.html"

# Try to get config files directly
curl -s "http://$DEVICE_IP/config.json"
curl -s "http://$DEVICE_IP/wifi.json"
curl -s "http://$DEVICE_IP/city.json"
curl -s "http://$DEVICE_IP/key.json"
curl -s "http://$DEVICE_IP/fkey.json"
curl -s "http://$DEVICE_IP/theme_list.json"
curl -s "http://$DEVICE_IP/v.json"

# Common ESP8266 debug endpoints
curl -s "http://$DEVICE_IP/debug"
curl -s "http://$DEVICE_IP/status"
curl -s "http://$DEVICE_IP/info"
curl -s "http://$DEVICE_IP/heap"
curl -s "http://$DEVICE_IP/reboot"
curl -s "http://$DEVICE_IP/reset"
```

### Download Web Assets
```bash
DEVICE_IP="192.168.1.xxx"
mkdir -p analysis/web_assets

# Download main pages
wget -O analysis/web_assets/index.html "http://$DEVICE_IP/"
wget -O analysis/web_assets/settings.html "http://$DEVICE_IP/settings.html"
wget -O analysis/web_assets/network.html "http://$DEVICE_IP/network.html"
wget -O analysis/web_assets/weather.html "http://$DEVICE_IP/weather.html"

# Try compressed versions
wget -O analysis/web_assets/index.html.gz "http://$DEVICE_IP/index.html.gz"
wget -O analysis/web_assets/settings.html.gz "http://$DEVICE_IP/settings.html.gz"
```

### Brute Force Directory Listing
```bash
# Common ESP8266 paths to try
DEVICE_IP="192.168.1.xxx"
PATHS=(
    "/"
    "/list"
    "/dir"
    "/files"
    "/fs"
    "/spiffs"
    "/data"
    "/edit"
    "/upload"
    "/update"
    "/ota"
    "/api"
    "/api/config"
    "/api/wifi"
    "/api/weather"
    "/system"
    "/admin"
    "/debug"
    "/log"
    "/logs"
    "/console"
    "/terminal"
)

for path in "${PATHS[@]}"; do
    echo "Testing: $path"
    curl -s -o /dev/null -w "%{http_code}" "http://$DEVICE_IP$path"
    echo ""
done
```

## Method 3: Web Interface File Manager

Many ESP8266 web servers include a file manager. Look for:

1. **SPIFFS File Manager**: Often at `/edit` or `/files`
2. **Directory listing**: Sometimes `/list?dir=/`

```bash
DEVICE_IP="192.168.1.xxx"

# Try file manager endpoints
curl -s "http://$DEVICE_IP/edit"
curl -s "http://$DEVICE_IP/list?dir=/"
curl -s "http://$DEVICE_IP/fs?dir=/"

# Some implementations use POST
curl -X POST "http://$DEVICE_IP/list" -d "dir=/"
```

## Method 4: Telnet Access (if available)

```bash
DEVICE_IP="192.168.1.xxx"

# Try connecting
telnet $DEVICE_IP 23

# If it connects, try common credentials:
# admin/admin
# root/root
# (blank)/(blank)
```

## Method 5: Serial Console

Even with firmware running, serial console often shows debug output:

```bash
# Find serial port when device is connected via USB
ls /dev/cu.* | grep -i usb

# Connect (typically 115200 baud)
screen /dev/cu.usbserial-* 115200

# Or use picocom
picocom -b 115200 /dev/cu.usbserial-*
```

**What to look for in serial output:**
- Boot messages
- IP address
- Error messages
- Debug prints
- Memory info

## Method 6: Capture Network Traffic

### Using Wireshark/tcpdump
```bash
# Capture all traffic from device
DEVICE_IP="192.168.1.xxx"
sudo tcpdump -i en0 host $DEVICE_IP -w device_traffic.pcap

# Filter for HTTP only
sudo tcpdump -i en0 host $DEVICE_IP and port 80 -A
```

### What to capture:
- Weather API requests (see full URL with API key)
- NTP requests
- Any other external connections

## Method 7: AP Mode Analysis

Put device in AP mode and analyze:

```bash
# Connect to device's WiFi (SmallTV-Ultra)
# Default IP is usually 192.168.4.1

curl -s "http://192.168.4.1/"
curl -s "http://192.168.4.1/list?dir=/"

# Scan local network from device's perspective
nmap -sP 192.168.4.0/24
```

## Method 8: Browser DevTools

1. Open browser DevTools (F12)
2. Go to Network tab
3. Navigate through all device pages
4. Note:
   - All requests made
   - Response headers
   - Cookies/session info
   - WebSocket connections

## Method 9: Firmware Update Inspection

The device supports OTA updates. We might be able to:

1. Intercept firmware update requests
2. See where it checks for updates
3. Download web assets during the upload process

## Data Collection Checklist

After trying the above methods, document:

- [ ] Device IP address
- [ ] Open ports found
- [ ] Accessible endpoints
- [ ] Downloaded config files
- [ ] Downloaded web assets
- [ ] API keys found (if any)
- [ ] Serial console output
- [ ] Network traffic captures

## Quick Start Script

Save this as `scan_device.sh`:

```bash
#!/bin/bash
DEVICE_IP="${1:-192.168.1.xxx}"
OUTPUT_DIR="analysis/device_scan"
mkdir -p "$OUTPUT_DIR"

echo "Scanning device at $DEVICE_IP..."

# Port scan
echo "Port scan:"
nmap -p 22,23,80,81,443,8080,8266 "$DEVICE_IP" | tee "$OUTPUT_DIR/ports.txt"

# Endpoint discovery
echo -e "\nEndpoint discovery:"
ENDPOINTS=(
    "/" "/settings.html" "/network.html" "/weather.html"
    "/config.json" "/wifi.json" "/v.json" "/theme_list.json"
    "/list?dir=/" "/edit" "/debug" "/status" "/info"
)

for ep in "${ENDPOINTS[@]}"; do
    code=$(curl -s -o "$OUTPUT_DIR/$(echo $ep | tr '/?=' '_').html" -w "%{http_code}" "http://$DEVICE_IP$ep")
    echo "$ep -> $code"
done

echo -e "\nResults saved to $OUTPUT_DIR/"
```

Run with:
```bash
chmod +x scan_device.sh
./scan_device.sh 192.168.1.xxx
```

## Security Notes

- The device likely has minimal security
- Captured API keys should be kept private
- Don't share network captures containing credentials
- Consider changing API keys after extraction
