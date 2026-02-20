/**
 * EpicWeatherBox - Display Layout Constants
 *
 * Derives all layout values from DISPLAY_WIDTH / DISPLAY_HEIGHT
 * (defined in config.h). This allows the drawing code to adapt to
 * different display resolutions without touching every literal.
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
#define SCREEN_CENTER_X   (SCREEN_W / 2)           // 120
#define SCREEN_MARGIN     8                        // Fixed margin
#define SCREEN_RIGHT      (SCREEN_W - SCREEN_MARGIN) // 232

// =============================================================================
// CAROUSEL DOTS
// =============================================================================

#define DOT_Y_BASE        (SCREEN_H - 10)          // 230
#define DOT_Y_MAX         (SCREEN_H - 4)           // 236

// =============================================================================
// FOOTER BAR
// =============================================================================

#define FOOTER_Y_OFFSET   175                       // Footer bar Y position
#define FOOTER_BAR_H      36                        // Footer bar height
#define FOOTER_BAR_MARGIN SCREEN_MARGIN             // Footer bar side margin

// =============================================================================
// BOOT SCREEN
// =============================================================================

#define BOOT_STATUS_Y     195                       // Boot status area top
#define BOOT_TEXT_Y        218                       // Boot status text Y

// =============================================================================
// FORECAST CARDS
// =============================================================================

#define FORECAST_CARD_W   75                        // Card width
#define FORECAST_CARD_H   180                       // Card height

#endif // DISPLAY_LAYOUT_H
