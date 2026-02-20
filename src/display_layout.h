/**
 * EpicWeatherBox - Display Layout Constants
 *
 * Derives all layout values from DISPLAY_WIDTH / DISPLAY_HEIGHT
 * (defined in config.h). This allows the drawing code to adapt to
 * different display resolutions without touching every literal.
 *
 * On CYD (ILI9341 240x320), swapped TFT_WIDTH/HEIGHT + rotation 3
 * gives _width=240, _height=320 matching the physical panel.
 * Y_OFFSET=40 centers the 240px UI vertically in the 320px height.
 * On SmallTV (240x240), both offsets are 0.
 *
 * Include this file in any .cpp that draws to the TFT.
 */

#ifndef DISPLAY_LAYOUT_H
#define DISPLAY_LAYOUT_H

#include "config.h"

// =============================================================================
// CORE GEOMETRY
// =============================================================================

#define SCREEN_W          DISPLAY_WIDTH            // 240
#define SCREEN_H          DISPLAY_HEIGHT           // 240
#define SCREEN_MARGIN     8                        // Fixed margin

// Horizontal offset for centering 240px UI on wider displays (landscape CYD)
#define X_OFFSET          DISPLAY_X_OFFSET         // 0 (SmallTV) or 40 (CYD)

// Vertical offset (0 on both platforms now that CYD uses landscape)
#define Y_OFFSET          DISPLAY_Y_OFFSET         // 0

// Absolute positions that include X_OFFSET so all references auto-correct
#define SCREEN_CENTER_X   (X_OFFSET + SCREEN_W / 2)           // 120 or 160
#define SCREEN_RIGHT      (X_OFFSET + SCREEN_W - SCREEN_MARGIN) // 232 or 272

// =============================================================================
// CAROUSEL DOTS
// =============================================================================

#define DOT_Y_BASE        (Y_OFFSET + SCREEN_H - 10)  // 230
#define DOT_Y_MAX         (Y_OFFSET + SCREEN_H - 4)   // 236

// =============================================================================
// FOOTER BAR
// =============================================================================

#define FOOTER_Y_OFFSET   (Y_OFFSET + 175)            // 175
#define FOOTER_BAR_H      36                           // Footer bar height
#define FOOTER_BAR_MARGIN SCREEN_MARGIN                // Footer bar side margin

// =============================================================================
// BOOT SCREEN
// =============================================================================

#define BOOT_STATUS_Y     (Y_OFFSET + 195)             // 195
#define BOOT_TEXT_Y        (Y_OFFSET + 218)             // 218

// =============================================================================
// FORECAST CARDS
// =============================================================================

#define FORECAST_CARD_W   75                        // Card width
#define FORECAST_CARD_H   180                       // Card height

#endif // DISPLAY_LAYOUT_H
