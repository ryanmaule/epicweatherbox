#!/usr/bin/env python3
"""
Convert pixel-art SVG weather icons to RGB565 C arrays for ESP8266/TFT_eSPI.
"""

import re
import os
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
    # Transparent color (we'll use 0x0000 or a specific transparent marker)
    TRANSPARENT = 0x0000

    lines = []
    lines.append(f"// {name} icon - {size}x{size} RGB565")
    lines.append(f"static const uint16_t PROGMEM icon_{name}[{size * size}] = {{")

    for y in range(size):
        row_values = []
        for x in range(size):
            if (x, y) in pixels:
                rgb565 = hex_to_rgb565(pixels[(x, y)])
                row_values.append(f"0x{rgb565:04X}")
            else:
                row_values.append(f"0x{TRANSPARENT:04X}")

        # Format row
        line = "    " + ", ".join(row_values) + ","
        if y == size - 1:
            line = line.rstrip(',')  # Remove trailing comma on last row
        lines.append(line)

    lines.append("};")
    return "\n".join(lines)

def process_svg_file(filepath):
    """Process a single SVG file and return the C array."""
    with open(filepath, 'r') as f:
        content = f.read()

    # Get icon name from filename
    name = Path(filepath).stem.replace('-', '_')

    pixels = parse_svg_rects(content)
    return generate_icon_array(name, pixels)

def main():
    svg_dir = Path(__file__).parent.parent / "images" / "weather_pixel_icons_svg"

    if not svg_dir.exists():
        print(f"Error: Directory not found: {svg_dir}")
        return

    # Process all SVG files
    svg_files = sorted(svg_dir.glob("*.svg"))

    print("// =============================================================================")
    print("// WEATHER ICONS - RGB565 32x32 Sprites")
    print("// Generated from SVG pixel art icons")
    print("// =============================================================================")
    print()
    print("// Transparent pixel value (black = 0x0000)")
    print("#define ICON_TRANSPARENT 0x0000")
    print()

    for svg_file in svg_files:
        print(f"// --- {svg_file.name} ---")
        print(process_svg_file(svg_file))
        print()

if __name__ == "__main__":
    main()
