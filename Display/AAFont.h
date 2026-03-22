/*
 * AAFont.h - Anti-aliased font renderer voor Arduino GigaDisplay
 *
 * 4-bit alpha per pixel (2 pixels per byte, high nibble eerst).
 * Elke glyph begint op een byte-grens in de bitmap.
 * Gebruik AAFont_setDisplay() eenmalig in initDisplay().
 *
 * API:
 *   AAFont_setDisplay(&display);
 *   AAFont_drawString(AA_VOL, "-34.5 dB", 0, 300,
 *                    C_GREEN, C_BG, AA_CENTER, SCREEN_W);
 */

#pragma once
#include <Arduino.h>
#include <Arduino_GigaDisplay_GFX.h>

// ── Struct definities ────────────────────────────────────────────────────────
#ifndef AAFONT_STRUCTS
#define AAFONT_STRUCTS
struct AAGlyph {
  uint32_t bitmapOffset;  // byte-offset in bitmap (byte-aligned per glyph)
  uint8_t  width, height;
  uint8_t  xAdvance;
  int8_t   xOffset, yOffset;
};
struct AAFont {
  const uint8_t*  bitmap;
  const AAGlyph*  glyph;
  uint8_t first, last, yAdvance;
};
#endif

// ── Uitlijning ────────────────────────────────────────────────────────────────
enum AAAlign { AA_LEFT, AA_CENTER, AA_RIGHT };

// ── Display referentie ────────────────────────────────────────────────────────
static GigaDisplay_GFX* _aaDisplay = nullptr;
inline void AAFont_setDisplay(GigaDisplay_GFX* d) { _aaDisplay = d; }

// ── Kleur blending (RGB565) ───────────────────────────────────────────────────
static inline uint16_t aaBlend(uint16_t fg, uint16_t bg, uint8_t alpha4) {
  if (alpha4 == 0)  return bg;
  if (alpha4 == 15) return fg;
  uint8_t fgR = (fg >> 11) & 0x1F;
  uint8_t fgG = (fg >>  5) & 0x3F;
  uint8_t fgB =  fg        & 0x1F;
  uint8_t bgR = (bg >> 11) & 0x1F;
  uint8_t bgG = (bg >>  5) & 0x3F;
  uint8_t bgB =  bg        & 0x1F;
  uint8_t a = alpha4;
  return ((uint16_t)((fgR*a + bgR*(15-a) + 8) >> 4) << 11)
       | ((uint16_t)((fgG*a + bgG*(15-a) + 8) >> 4) <<  5)
       |  (uint16_t)((fgB*a + bgB*(15-a) + 8) >> 4);
}

// ── Tekstbreedte berekenen ────────────────────────────────────────────────────
static int16_t AAFont_stringWidth(const AAFont* font, const char* str) {
  int16_t w = 0;
  uint8_t first = font->first;
  uint8_t last  = font->last;
  const AAGlyph* glyphBase = font->glyph;
  for (const char* p = str; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c < first || c > last) continue;
    w += glyphBase[c - first].xAdvance;
  }
  return w;
}

// ── Tekst tekenen ─────────────────────────────────────────────────────────────
static void AAFont_drawString(const AAFont* font, const char* str,
                               int16_t x, int16_t baseline,
                               uint16_t fgColor, uint16_t bgColor,
                               AAAlign align = AA_LEFT, int16_t areaW = 0) {
  if (!_aaDisplay || !str) return;

  int16_t startX = x;
  if (align == AA_CENTER || align == AA_RIGHT) {
    int16_t tw = AAFont_stringWidth(font, str);
    if (align == AA_CENTER) startX = x + (areaW - tw) / 2;
    else                    startX = x +  areaW - tw;
  }

  const uint8_t* bitmapBase = font->bitmap;
  const AAGlyph* glyphBase  = font->glyph;
  uint8_t first = font->first;
  uint8_t last  = font->last;
  int16_t curX  = startX;

  // startWrite/endWrite zorgen dat de refresh thread de drawPixel writes
  // naar het scherm kopieert. Zonder endWrite() blijven pixels in de
  // framebuffer maar worden ze nooit via dsi_lcdDrawImage() doorgestuurd.
  _aaDisplay->startWrite();

  for (const char* p = str; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c < first || c > last) { curX += 8; continue; }

    const AAGlyph& g = glyphBase[c - first];

    int16_t  gx       = curX + g.xOffset;
    int16_t  gy       = baseline + g.yOffset;
    uint32_t byteIdx  = g.bitmapOffset;
    bool     hiNibble = true;

    for (uint8_t row = 0; row < g.height; row++) {
      for (uint8_t col = 0; col < g.width; col++) {
        uint8_t raw    = pgm_read_byte(&bitmapBase[byteIdx]);
        uint8_t alpha4 = hiNibble ? (raw >> 4) & 0x0F : raw & 0x0F;
        if (!hiNibble) byteIdx++;
        hiNibble = !hiNibble;

        if (alpha4 > 0) {
          _aaDisplay->drawPixel(gx + col, gy + row,
                                aaBlend(fgColor, bgColor, alpha4));
        }
      }
    }
    curX += g.xAdvance;
  }

  _aaDisplay->endWrite();  // triggert refresh thread → dsi_lcdDrawImage()
}

// ── Geschaalde tekst — voor grote volume/waarde weergave ──────────────────────
// scale_x16: schaalfactor * 16 (bijv. 24 = 1.5x, 32 = 2x)
static int16_t AAFont_stringWidthScaled(const AAFont* font, const char* str, uint8_t scale_x16) {
  if (!font || !str) return 0;
  uint8_t first = font->first, last = font->last;
  const AAGlyph* glyphBase = font->glyph;
  int16_t w = 0;
  for (const char* p = str; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c < first || c > last) continue;
    w += (int16_t)(glyphBase[c - first].xAdvance * scale_x16 + 8) / 16;
  }
  return w;
}

static void AAFont_drawStringScaled(const AAFont* font, const char* str,
                                    int16_t x, int16_t baseline,
                                    uint16_t fgColor, uint16_t bgColor,
                                    uint8_t scale_x16,
                                    AAAlign align = AA_LEFT, int16_t areaW = 0) {
  if (!_aaDisplay || !str) return;

  int16_t startX = x;
  if (align == AA_CENTER || align == AA_RIGHT) {
    int16_t tw = AAFont_stringWidthScaled(font, str, scale_x16);
    if (align == AA_CENTER) startX = x + (areaW - tw) / 2;
    else                    startX = x +  areaW - tw;
  }

  const uint8_t* bitmapBase = font->bitmap;
  const AAGlyph* glyphBase  = font->glyph;
  uint8_t first = font->first, last = font->last;
  int16_t curX  = startX;

  _aaDisplay->startWrite();

  for (const char* p = str; *p; p++) {
    uint8_t c = (uint8_t)*p;
    if (c < first || c > last) {
      curX += (int16_t)(8 * scale_x16 + 8) / 16;
      continue;
    }
    const AAGlyph& g = glyphBase[c - first];

    int16_t gx0 = curX + (int16_t)(g.xOffset * scale_x16 + 8) / 16;
    int16_t gy0 = baseline + (int16_t)(g.yOffset * scale_x16 + 8) / 16;

    uint32_t byteIdx = g.bitmapOffset;
    bool hiNibble = true;

    for (uint8_t row = 0; row < g.height; row++) {
      int16_t sy  = gy0 + (int16_t)(row     * scale_x16) / 16;
      int16_t sy2 = gy0 + (int16_t)((row+1) * scale_x16) / 16;
      int16_t ph  = sy2 - sy; if (ph < 1) ph = 1;

      for (uint8_t col = 0; col < g.width; col++) {
        uint8_t raw    = pgm_read_byte(&bitmapBase[byteIdx]);
        uint8_t alpha4 = hiNibble ? (raw >> 4) & 0x0F : raw & 0x0F;
        if (!hiNibble) byteIdx++;
        hiNibble = !hiNibble;

        if (alpha4 > 0) {
          int16_t sx  = gx0 + (int16_t)(col     * scale_x16) / 16;
          int16_t sx2 = gx0 + (int16_t)((col+1) * scale_x16) / 16;
          int16_t pw  = sx2 - sx; if (pw < 1) pw = 1;
          uint16_t blended = aaBlend(fgColor, bgColor, alpha4);
          _aaDisplay->fillRect(sx, sy, pw, ph, blended);
        }
      }
    }
    curX += (int16_t)(g.xAdvance * scale_x16 + 8) / 16;
  }

  _aaDisplay->endWrite();  // triggert refresh thread → dsi_lcdDrawImage()
}
