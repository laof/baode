/**
 * @file font5x7.c
 *
 * Each glyph is 5 columns x 7 rows. A byte represents one column;
 * bit 0 is the topmost pixel, bit 6 is the bottommost pixel.
 * Rendering scales the glyph by an integer factor S, producing
 * blocks of S x S pixels per logical pixel, with a 1*S column gap
 * between characters.
 *
 * Supported glyphs (others render blank):
 *   ' '  '%'  '-'  '.'  '/'  '0'..'9'  ':'
 *   'A' 'C' 'E' 'H' 'I' 'M' 'P' 'R' 'T' 'U'
 */
#include "font5x7.h"
#include "st7305.h"
#include <ctype.h>

typedef struct {
    char     c;
    uint8_t  col[5];
} glyph_t;

static const glyph_t GLYPHS[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'%', {0x23,0x13,0x08,0x64,0x62}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'.', {0x00,0x60,0x60,0x00,0x00}},
    {'/', {0x20,0x10,0x08,0x04,0x02}},

    {'0', {0x3E,0x51,0x49,0x45,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1E}},

    {':', {0x00,0x36,0x36,0x00,0x00}},

    {'A', {0x7E,0x11,0x11,0x11,0x7E}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x3F,0x40,0x40,0x40,0x3F}},
};

static const glyph_t *find_glyph(char c)
{
    for (unsigned i = 0; i < sizeof(GLYPHS)/sizeof(GLYPHS[0]); i++)
        if (GLYPHS[i].c == c) return &GLYPHS[i];
    return &GLYPHS[0];   /* blank */
}

void Font_DrawChar(int x, int y, char c, uint8_t color, uint8_t scale)
{
    if (scale == 0) scale = 1;
    const glyph_t *g = find_glyph(c);

    for (int col = 0; col < FONT5X7_W; col++)
    {
        uint8_t bits = g->col[col];
        for (int row = 0; row < FONT5X7_H; row++)
        {
            if (bits & (1u << row))
                ST7305_FillRect(x + col * scale, y + row * scale,
                                scale, scale, color);
        }
    }
}

void Font_DrawString(int x, int y, const char *s, uint8_t color, uint8_t scale)
{
    if (scale == 0) scale = 1;
    int cx = x;
    while (*s)
    {
        Font_DrawChar(cx, y, *s, color, scale);
        cx += (FONT5X7_W + 1) * scale;   /* 1-col gap */
        s++;
    }
}

int Font_StringWidth(const char *s, uint8_t scale)
{
    if (scale == 0) scale = 1;
    int n = 0;
    while (*s) { n++; s++; }
    if (n == 0) return 0;
    return n * (FONT5X7_W + 1) * scale - scale;   /* no trailing gap */
}
