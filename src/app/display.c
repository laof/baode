/**
 * @file display.c
 *
 * Layout (LCD_W=400, LCD_H=300):
 *
 *   +-------------------------------------------+
 *   |                                           |
 *   |   2026-05-17           (date, scale 3)    |   y= 16, h=21
 *   |                                           |
 *   |       1 4 : 3 0        (clock, scale 10)  |   y= 70, h=70  centered
 *   |                                           |
 *   |                                           |
 *   |  T : 2 3 . 5 C   H : 5 6 %   (scale 4)    |   y=220, h=28
 *   |                                           |
 *   +-------------------------------------------+
 */
#include "display.h"
#include "st7305.h"
#include "font5x7.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static void center_string(int y, const char *s, uint8_t scale)
{
    int w = Font_StringWidth(s, scale);
    int x = (LCD_W - w) / 2;
    if (x < 0) x = 0;
    Font_DrawString(x, y, s, COLOR_BLACK, scale);
}

void Display_Render(const DisplayData *d)
{
    char buf[32];

    ST7305_Clear(COLOR_WHITE);

    /* Outer frame */
    ST7305_DrawRect(2, 2, LCD_W - 4, LCD_H - 4, COLOR_BLACK);

    /* ---- Date line ---- */
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u",
             (unsigned)d->year, (unsigned)d->month, (unsigned)d->day);
    center_string(16, buf, 3);   /* 5*3=15 wide, 7*3=21 tall per char */

    /* separator */
    ST7305_DrawHLine(20, 50, LCD_W - 40, COLOR_BLACK);

    /* ---- Clock HH:MM ---- */
    snprintf(buf, sizeof(buf), "%02u:%02u",
             (unsigned)d->hour, (unsigned)d->minute);
    /* scale 10  ->  glyph 50x70, total 5 chars*(50+10)-10 = 290 px wide */
    center_string(80, buf, 10);

    /* separator */
    ST7305_DrawHLine(20, 200, LCD_W - 40, COLOR_BLACK);

    /* ---- Temperature + Humidity ---- */
    if (d->sensor_ok && !isnan(d->temp_c) && !isnan(d->rh))
    {
        char tline[24], hline[24];
        /* one decimal for temp, integer for humidity */
        int  t_int = (int)d->temp_c;
        int  t_dec = (int)((d->temp_c - t_int) * 10.0f + 0.5f);
        if (t_dec >= 10) { t_int += 1; t_dec = 0; }
        if (t_dec < 0)   { t_dec = -t_dec; }   /* unlikely, snprintf below safer */

        snprintf(tline, sizeof(tline), "T:%d.%d C", t_int, t_dec);
        snprintf(hline, sizeof(hline), "H:%d %%",   (int)(d->rh + 0.5f));

        /* place on left/right halves, scale 4 ->  20x28 per char */
        int tw = Font_StringWidth(tline, 4);
        int hw = Font_StringWidth(hline, 4);
        Font_DrawString((LCD_W/2 - tw) / 2,            230, tline, COLOR_BLACK, 4);
        Font_DrawString(LCD_W/2 + (LCD_W/2 - hw) / 2,  230, hline, COLOR_BLACK, 4);
    }
    else
    {
        center_string(230, "SENSOR ERR", 4);
    }

    ST7305_Flush();
}
