// fonts.h — Font utilities for the clock display.
//
// Provides a bold simulation by drawing text with a 1-pixel offset.
// This gives a visible "bold" look without needing external font files,
// saving flash space on the ESP8266.
#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Draw text bold by rendering it twice with a 1-pixel horizontal offset.
// Works with any text size using the built-in 6x8 font.
static inline void gfxPrintBold(Arduino_GFX* g, int x, int y, const char* text,
                                 uint16_t color, uint8_t size = 1) {
  if (!g || !text || !text[0]) return;
  g->setTextSize(size);
  g->setTextColor(color);
  // Main pass
  g->setCursor(x, y);
  g->print(text);
  // Bold offset pass (1 pixel right, 1 pixel down for thickness)
  g->setCursor(x + 1, y);
  g->print(text);
  g->setCursor(x, y + 1);
  g->print(text);
}

// Draw centered bold text
static inline void gfxDrawCenteredBold(const char* s, int y, uint8_t size, uint16_t color) {
  Arduino_GFX* g = gfxDev();
  if (!g || !s || !s[0]) return;
  int w = (int)strlen(s) * 6 * size;
  int x = (TFT_WIDTH - w) / 2;
  if (x < 0) x = 0;
  gfxPrintBold(g, x, y, s, color, size);
}
