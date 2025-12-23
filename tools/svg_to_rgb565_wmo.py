#!/usr/bin/env python3
"""
Convert WMO weather code SVG icons to RGB565 C arrays for ESP8266/TFT_eSPI.
Maps directly from Open-Meteo WMO weather codes to icons.
"""

import re
import json
from pathlib import Path

def hex_to_rgb565(hex_color):
    """Convert hex color (#RRGGBB) to RGB565 16-bit value."""
    hex_color = hex_color.lstrip('#')
    r = int(hex_color[0:2], 16)
    g = int(hex_color[2:4], 16)
    b = int(hex_color[4:6], 16)

    # Convert to RGB565: 5 bits red, 6 bits green, 5 bits blue
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F

    rgb565 = (r5 << 11) | (g6 << 5) | b5
    return rgb565

def parse_svg_rects(svg_content):
    """Parse SVG rect elements and return pixel data."""
    pixels = {}

    # Find all rect elements
    rect_pattern = r'<rect\s+x="(\d+)"\s+y="(\d+)"\s+width="(\d+)"\s+height="(\d+)"\s+fill="(#[0-9A-Fa-f]{6})"'

    for match in re.finditer(rect_pattern, svg_content):
        x = int(match.group(1))
        y = int(match.group(2))
        w = int(match.group(3))
        h = int(match.group(4))
        color = match.group(5)

        # Fill in all pixels covered by this rect
        for py in range(y, y + h):
            for px in range(x, x + w):
                pixels[(px, py)] = color

    return pixels

def generate_icon_array(name, pixels, size=32):
    """Generate C array for icon."""
    TRANSPARENT = 0x0000

    lines = []
    lines.append(f"static const uint16_t PROGMEM icon_{name}[{size * size}] = {{")

    for y in range(size):
        row_values = []
        for x in range(size):
            if (x, y) in pixels:
                rgb565 = hex_to_rgb565(pixels[(x, y)])
                row_values.append(f"0x{rgb565:04X}")
            else:
                row_values.append(f"0x{TRANSPARENT:04X}")

        line = "    " + ", ".join(row_values) + ","
        if y == size - 1:
            line = line.rstrip(',')
        lines.append(line)

    lines.append("};")
    return "\n".join(lines)

def main():
    svg_dir = Path(__file__).parent.parent / "images" / "open_meteo_weather_icons_32px_svg"
    output_file = Path(__file__).parent.parent / "src" / "weather_icons_rgb565.h"

    if not svg_dir.exists():
        print(f"Error: Directory not found: {svg_dir}")
        return

    # Load manifest
    manifest_path = svg_dir / "manifest.json"
    with open(manifest_path, 'r') as f:
        manifest = json.load(f)

    output_lines = []
    output_lines.append("/**")
    output_lines.append(" * Weather Icons - RGB565 32x32 Sprites")
    output_lines.append(" * Generated from Open-Meteo WMO weather code SVG icons")
    output_lines.append(" * Direct mapping from WMO codes to icons")
    output_lines.append(" */")
    output_lines.append("")
    output_lines.append("#ifndef WEATHER_ICONS_RGB565_H")
    output_lines.append("#define WEATHER_ICONS_RGB565_H")
    output_lines.append("")
    output_lines.append("#include <Arduino.h>")
    output_lines.append("")
    output_lines.append("// Transparent pixel value")
    output_lines.append("#define ICON_TRANSPARENT 0x0000")
    output_lines.append("")
    output_lines.append("// =============================================================================")
    output_lines.append("// WMO WEATHER CODE ICONS (32x32 RGB565)")
    output_lines.append("// =============================================================================")
    output_lines.append("")

    # Process each icon from manifest
    icon_names = []
    wmo_codes = []

    for entry in manifest:
        code = entry['code']
        key = entry['key']
        label = entry['label']
        svg_file = svg_dir / entry['file']

        if not svg_file.exists():
            print(f"Warning: SVG file not found: {svg_file}")
            continue

        with open(svg_file, 'r') as f:
            svg_content = f.read()

        pixels = parse_svg_rects(svg_content)
        icon_name = f"wmo_{code}"

        output_lines.append(f"// WMO {code}: {label}")
        output_lines.append(generate_icon_array(icon_name, pixels))
        output_lines.append("")

        icon_names.append(icon_name)
        wmo_codes.append(code)
        print(f"Processed: WMO {code} - {label}")

    # Generate lookup function
    output_lines.append("// =============================================================================")
    output_lines.append("// WMO CODE TO ICON LOOKUP")
    output_lines.append("// =============================================================================")
    output_lines.append("")
    output_lines.append("/**")
    output_lines.append(" * Get icon array for WMO weather code")
    output_lines.append(" * @param wmoCode WMO weather interpretation code (0-99)")
    output_lines.append(" * @return Pointer to RGB565 icon array, or nullptr if unknown code")
    output_lines.append(" */")
    output_lines.append("inline const uint16_t* getIconForWMOCode(int wmoCode) {")
    output_lines.append("    switch (wmoCode) {")

    for entry in manifest:
        code = entry['code']
        output_lines.append(f"        case {code}: return icon_wmo_{code};")

    output_lines.append("        default: return icon_wmo_3;  // Fallback to overcast")
    output_lines.append("    }")
    output_lines.append("}")
    output_lines.append("")
    output_lines.append("#endif // WEATHER_ICONS_RGB565_H")
    output_lines.append("")

    # Write output file
    with open(output_file, 'w') as f:
        f.write("\n".join(output_lines))

    print(f"\nGenerated: {output_file}")
    print(f"Total icons: {len(icon_names)}")

if __name__ == "__main__":
    main()
