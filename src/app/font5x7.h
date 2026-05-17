/**
 * @file  font5x7.h
 * @brief Minimal 5x7 ASCII font, MSB=top of column, 5 bytes per glyph.
 *        Only the characters needed by this app are populated; others
 *        render as blank space.
 *
 *        Supported glyphs:  space  % - . / 0..9 : A C E H I M P R T U
 */
#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FONT5X7_W   5
#define FONT5X7_H   7

void Font_DrawChar  (int x, int y, char c, uint8_t color, uint8_t scale);
void Font_DrawString(int x, int y, const char *s, uint8_t color, uint8_t scale);
int  Font_StringWidth(const char *s, uint8_t scale);     /* px, incl 1-col gap */

#ifdef __cplusplus
}
#endif

#endif
