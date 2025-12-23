#!/usr/bin/env python3
"""
Generate base64 data URIs from SVG files for embedding in JavaScript.
Creates a mapping of WMO codes to SVG data URIs.
"""

import json
import base64
import os
from pathlib import Path

def main():
    svg_dir = Path(__file__).parent.parent / "images" / "open_meteo_weather_icons_32px_svg"
    manifest_path = svg_dir / "manifest.json"

    with open(manifest_path) as f:
        manifest = json.load(f)

    # Build the JavaScript object
    print("// WMO code to SVG data URI mapping")
    print("const WMO_ICONS = {")

    for i, entry in enumerate(manifest):
        code = entry["code"]
        svg_file = svg_dir / entry["file"]

        with open(svg_file, "rb") as f:
            svg_data = f.read()

        # Create data URI
        b64 = base64.b64encode(svg_data).decode('ascii')
        data_uri = f"data:image/svg+xml;base64,{b64}"

        comma = "," if i < len(manifest) - 1 else ""
        print(f"  {code}: '{data_uri}'{comma}")

    print("};")
    print()
    print("// Get icon data URI for WMO code")
    print("function getWmoIcon(code) {")
    print("  if (WMO_ICONS[code]) return WMO_ICONS[code];")
    print("  // Fallback mapping for codes not in the set")
    print("  if (code <= 2) return WMO_ICONS[code] || WMO_ICONS[0];")
    print("  if (code === 3) return WMO_ICONS[3];")
    print("  if (code >= 45 && code <= 48) return WMO_ICONS[45];")
    print("  if (code >= 51 && code <= 55) return WMO_ICONS[51];")
    print("  if (code >= 56 && code <= 57) return WMO_ICONS[56];")
    print("  if (code >= 61 && code <= 65) return WMO_ICONS[61];")
    print("  if (code >= 66 && code <= 67) return WMO_ICONS[66];")
    print("  if (code >= 71 && code <= 75) return WMO_ICONS[71];")
    print("  if (code >= 77) return WMO_ICONS[77];")
    print("  if (code >= 80 && code <= 82) return WMO_ICONS[80];")
    print("  if (code >= 85 && code <= 86) return WMO_ICONS[85];")
    print("  if (code >= 95) return WMO_ICONS[95];")
    print("  return WMO_ICONS[3]; // Default to overcast")
    print("}")

if __name__ == "__main__":
    main()
