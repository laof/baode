#ifndef ST7305_H
#define ST7305_H

#include "stm32l4xx_hal.h"
#include <stdint.h>

/* 横屏使用坐标（驱动内部把 (x,y) 旋转 90° CW 映射到 300×400 原生面板） */
#define ST7305_WIDTH 400
#define ST7305_HEIGHT 300
#define ST7305_NATIVE_WIDTH 300
#define ST7305_NATIVE_HEIGHT 400
#define ST7305_BUFFER_SIZE 15000

#define ST7305_COLOR_WHITE 0
#define ST7305_COLOR_BLACK 1

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *dc_port;
    uint16_t dc_pin;
    GPIO_TypeDef *rst_port;
    uint16_t rst_pin;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
} ST7305_t;

void st7305_init(ST7305_t *lcd);
void st7305_fill(ST7305_t *lcd, uint8_t color);
void st7305_draw_pixel(ST7305_t *lcd, uint16_t x, uint16_t y, uint8_t color);
void st7305_draw_char(ST7305_t *lcd, uint16_t x, uint16_t y, char ch, uint8_t color, uint8_t scale);
void st7305_draw_string(ST7305_t *lcd, uint16_t x, uint16_t y, const char *str, uint8_t color, uint8_t scale);
void st7305_refresh(ST7305_t *lcd);
void st7305_sleep_in(ST7305_t *lcd);
void st7305_sleep_out(ST7305_t *lcd);

#endif
