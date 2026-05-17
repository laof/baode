/**
 * @file    st7305.c
 * @brief   ST7305 driver implementation for STM32 HAL.
 *
 * Framebuffer layout (native 300x400 mono, 4 px/byte horizontal, 2 rows
 * packed per byte vertically — matching ST7305 RAM format):
 *
 *   bytes per native row pair = ceil(300/4) = 75
 *   row pairs                 = 400/2       = 200
 *   total buffer              = 75 * 200    = 15000 bytes
 *
 *   For pixel (nx, ny) in native coords:
 *     byte = (ny/2)*75 + (nx/4)
 *     bit  = 7 - ((nx % 4) * 2 + (ny % 2))
 *
 * Landscape -> native rotation (90° CW):
 *     nx = 299 - y;   ny = x;
 */

#include "st7305.h"
#include "spi.h"        /* hspi1, from CubeMX */
#include <string.h>

/* ---- SPI / GPIO helpers ---- */
#define LCD_SPI         (&hspi1)

#define CS_LOW()        HAL_GPIO_WritePin(LCD_CS_GPIO_Port,  LCD_CS_Pin,  GPIO_PIN_RESET)
#define CS_HIGH()       HAL_GPIO_WritePin(LCD_CS_GPIO_Port,  LCD_CS_Pin,  GPIO_PIN_SET)
#define DC_CMD()        HAL_GPIO_WritePin(LCD_DC_GPIO_Port,  LCD_DC_Pin,  GPIO_PIN_RESET)
#define DC_DAT()        HAL_GPIO_WritePin(LCD_DC_GPIO_Port,  LCD_DC_Pin,  GPIO_PIN_SET)
#define RES_LOW()       HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_RESET)
#define RES_HIGH()      HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_SET)

/* ---- Framebuffer ---- */
#define FB_BYTES_PER_ROW   75
#define FB_ROW_PAIRS       200
#define FB_SIZE            (FB_BYTES_PER_ROW * FB_ROW_PAIRS)   /* 15000 */

static uint8_t s_fb[FB_SIZE];

/* ---- Low-level writes ---- */
static void wr_cmd(uint8_t c)
{
    DC_CMD();
    CS_LOW();
    HAL_SPI_Transmit(LCD_SPI, &c, 1, HAL_MAX_DELAY);
    CS_HIGH();
}

static void wr_dat(uint8_t d)
{
    DC_DAT();
    CS_LOW();
    HAL_SPI_Transmit(LCD_SPI, &d, 1, HAL_MAX_DELAY);
    CS_HIGH();
}

static void wr_buf(const uint8_t *buf, uint32_t len)
{
    DC_DAT();
    CS_LOW();
    /* HAL_SPI_Transmit length is uint16, so chunk it */
    while (len)
    {
        uint16_t n = (len > 0xF000) ? 0xF000 : (uint16_t)len;
        HAL_SPI_Transmit(LCD_SPI, (uint8_t *)buf, n, HAL_MAX_DELAY);
        buf += n;
        len -= n;
    }
    CS_HIGH();
}

/* ---- Init sequence (from vendor sample, HPM mode, 16Hz/8Hz) ---- */
static void st7305_init_seq(void)
{
    RES_HIGH(); HAL_Delay(10);
    RES_LOW();  HAL_Delay(10);
    RES_HIGH(); HAL_Delay(50);

    wr_cmd(0xD6); wr_dat(0x17); wr_dat(0x02);              /* NVM Load */
    wr_cmd(0xD1); wr_dat(0x01);                            /* Booster Enable */

    wr_cmd(0xC0); wr_dat(0x11); wr_dat(0x04);              /* Gate Voltage */

    wr_cmd(0xC1); wr_dat(0x41); wr_dat(0x41); wr_dat(0x41); wr_dat(0x41);
    wr_cmd(0xC2); wr_dat(0x19); wr_dat(0x19); wr_dat(0x19); wr_dat(0x19);
    wr_cmd(0xC4); wr_dat(0x41); wr_dat(0x41); wr_dat(0x41); wr_dat(0x41);
    wr_cmd(0xC5); wr_dat(0x19); wr_dat(0x19); wr_dat(0x19); wr_dat(0x19);

    wr_cmd(0xD8); wr_dat(0xA6); wr_dat(0xE9);              /* HPM/LPM frame ctl */

    wr_cmd(0xB2); wr_dat(0x05);                            /* HPM 16Hz, LPM 8Hz */

    wr_cmd(0xB3);
    wr_dat(0xE5); wr_dat(0xF6); wr_dat(0x05); wr_dat(0x46);
    wr_dat(0x77); wr_dat(0x77); wr_dat(0x77); wr_dat(0x77);
    wr_dat(0x76); wr_dat(0x45);

    wr_cmd(0xB4);
    wr_dat(0x05); wr_dat(0x46);
    wr_dat(0x77); wr_dat(0x77); wr_dat(0x77); wr_dat(0x77);
    wr_dat(0x76); wr_dat(0x45);

    wr_cmd(0x62); wr_dat(0x32); wr_dat(0x03); wr_dat(0x1F);
    wr_cmd(0xB7); wr_dat(0x13);
    wr_cmd(0xB0); wr_dat(0x64);                            /* 400 gate lines */

    wr_cmd(0x11);                                          /* Sleep out */
    HAL_Delay(120);

    wr_cmd(0xC9); wr_dat(0x00);

    wr_cmd(0x36); wr_dat(0x48);                            /* MADCTL: MX=1, BGR=1 */
    wr_cmd(0x3A); wr_dat(0x11);                            /* 3 write for 24bit */
    wr_cmd(0xB9); wr_dat(0x20);                            /* Mono */
    wr_cmd(0xB8); wr_dat(0x29);                            /* Frame inversion */

    wr_cmd(0x21);                                          /* Inverse */

    wr_cmd(0x2A); wr_dat(0x12); wr_dat(0x2B);              /* Column window */
    wr_cmd(0x2B); wr_dat(0x00); wr_dat(0xC7);              /* Row window */

    wr_cmd(0x35); wr_dat(0x00);                            /* TE */
    wr_cmd(0xD0); wr_dat(0xFF);                            /* Auto power down */
    wr_cmd(0x38);                                          /* HPM ON */
    wr_cmd(0x29);                                          /* Display ON */
}

static void st7305_set_window(void)
{
    wr_cmd(0x2A); wr_dat(0x12); wr_dat(0x2A);
    wr_cmd(0x2B); wr_dat(0x00); wr_dat(0xC7);
    wr_cmd(0x2C);
}

/* ---- Public API ---- */

void ST7305_Init(void)
{
    st7305_init_seq();
    ST7305_Clear(COLOR_WHITE);
    ST7305_Flush();
}

void ST7305_Clear(uint8_t color)
{
    memset(s_fb, color ? 0xFF : 0x00, FB_SIZE);
}

void ST7305_Flush(void)
{
    st7305_set_window();
    wr_buf(s_fb, FB_SIZE);
}

/* Landscape (x,y) -> native (nx, ny) with 90° CW rotation */
static inline void put_native(int nx, int ny, uint8_t color)
{
    if ((unsigned)nx >= LCD_NATIVE_W || (unsigned)ny >= LCD_NATIVE_H) return;

    uint32_t idx = (uint32_t)(ny >> 1) * FB_BYTES_PER_ROW + (uint32_t)(nx >> 2);
    uint8_t  bit = (uint8_t)(7 - (((nx & 3) << 1) + (ny & 1)));

    if (color) s_fb[idx] |=  (uint8_t)(1u << bit);
    else       s_fb[idx] &= (uint8_t)~(1u << bit);
}

void ST7305_DrawPixel(int x, int y, uint8_t color)
{
    if ((unsigned)x >= LCD_W || (unsigned)y >= LCD_H) return;
    /* 90° CW: nx = (LCD_NATIVE_W-1) - y, ny = x */
    put_native((LCD_NATIVE_W - 1) - y, x, color);
}

void ST7305_DrawHLine(int x, int y, int w, uint8_t color)
{
    for (int i = 0; i < w; i++) ST7305_DrawPixel(x + i, y, color);
}

void ST7305_DrawVLine(int x, int y, int h, uint8_t color)
{
    for (int i = 0; i < h; i++) ST7305_DrawPixel(x, y + i, color);
}

void ST7305_FillRect(int x, int y, int w, int h, uint8_t color)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ST7305_DrawPixel(x + i, y + j, color);
}

void ST7305_DrawRect(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    ST7305_DrawHLine(x,         y,         w, color);
    ST7305_DrawHLine(x,         y + h - 1, w, color);
    ST7305_DrawVLine(x,         y,         h, color);
    ST7305_DrawVLine(x + w - 1, y,         h, color);
}
