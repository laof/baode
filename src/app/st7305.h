/**
 * @file    st7305.h
 * @brief   ST7305 4.2" Mono TFT 300x400 SPI driver (STM32 HAL).
 *          Provides a 300x400 portrait native framebuffer and a 400x300
 *          landscape pixel API (rotated 90° CW).
 *
 * Pinout (must match CubeMX user labels):
 *   SPI1: PA5(SCK) / PA7(MOSI)
 *   GPIO: PA4 -> LCD_CS, PA6 -> LCD_DC, PB0 -> LCD_RES
 */
#ifndef ST7305_H
#define ST7305_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

/* ---- Logical (landscape) dimensions used by the app ---- */
#define LCD_W   400
#define LCD_H   300

/* ---- Native (portrait) dimensions of the panel ---- */
#define LCD_NATIVE_W   300
#define LCD_NATIVE_H   400

/* Color (1bpp). 1 = black pixel on, 0 = clear */
#define COLOR_BLACK    1
#define COLOR_WHITE    0

#ifdef __cplusplus
extern "C" {
#endif

void ST7305_Init(void);
void ST7305_Clear(uint8_t color);          /* fills framebuffer */
void ST7305_Flush(void);                   /* push framebuffer to panel */

/* Landscape coordinate API: (x,y) in [0..LCD_W) x [0..LCD_H) */
void ST7305_DrawPixel(int x, int y, uint8_t color);
void ST7305_FillRect(int x, int y, int w, int h, uint8_t color);
void ST7305_DrawHLine(int x, int y, int w, uint8_t color);
void ST7305_DrawVLine(int x, int y, int h, uint8_t color);
void ST7305_DrawRect(int x, int y, int w, int h, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif /* ST7305_H */
