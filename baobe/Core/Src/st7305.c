#include "st7305.h"
#include <string.h>

static uint8_t s_buffer[ST7305_BUFFER_SIZE];

static void st7305_write_cmd(ST7305_t *lcd, uint8_t cmd) {
    HAL_GPIO_WritePin(lcd->dc_port, lcd->dc_pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(lcd->hspi, &cmd, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_SET);
}

static void st7305_write_data(ST7305_t *lcd, uint8_t data) {
    HAL_GPIO_WritePin(lcd->dc_port, lcd->dc_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(lcd->hspi, &data, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_SET);
}

static void st7305_address(ST7305_t *lcd) {
    st7305_write_cmd(lcd, 0x2A);
    st7305_write_data(lcd, 0x12);
    st7305_write_data(lcd, 0x2A);

    st7305_write_cmd(lcd, 0x2B);
    st7305_write_data(lcd, 0x00);
    st7305_write_data(lcd, 0xC7);

    st7305_write_cmd(lcd, 0x2C);
}

static const uint8_t *st7305_get_glyph(char ch) {
    static const uint8_t glyph_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t glyph_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t glyph_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t glyph_3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t glyph_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t glyph_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t glyph_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t glyph_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t glyph_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t glyph_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
    static const uint8_t glyph_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t glyph_dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t glyph_c[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t glyph_pct[5] = {0x63, 0x13, 0x08, 0x64, 0x63};
    static const uint8_t glyph_t[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t glyph_h[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t glyph_dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t glyph_space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

    switch (ch) {
        case '0': return glyph_0;
        case '1': return glyph_1;
        case '2': return glyph_2;
        case '3': return glyph_3;
        case '4': return glyph_4;
        case '5': return glyph_5;
        case '6': return glyph_6;
        case '7': return glyph_7;
        case '8': return glyph_8;
        case '9': return glyph_9;
        case ':': return glyph_colon;
        case '.': return glyph_dot;
        case 'C': return glyph_c;
        case '%': return glyph_pct;
        case 'T': return glyph_t;
        case 'H': return glyph_h;
        case '-': return glyph_dash;
        case ' ': return glyph_space;
        default: return glyph_space;
    }
}

void st7305_init(ST7305_t *lcd) {
    HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(lcd->rst_port, lcd->rst_pin, GPIO_PIN_SET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(lcd->rst_port, lcd->rst_pin, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(lcd->rst_port, lcd->rst_pin, GPIO_PIN_SET);
    HAL_Delay(120);

    st7305_write_cmd(lcd, 0xD6);
    st7305_write_data(lcd, 0x17);
    st7305_write_data(lcd, 0x02);

    st7305_write_cmd(lcd, 0xD1);
    st7305_write_data(lcd, 0x01);

    st7305_write_cmd(lcd, 0xC0);
    st7305_write_data(lcd, 0x11);
    st7305_write_data(lcd, 0x04);

    st7305_write_cmd(lcd, 0xC1);
    st7305_write_data(lcd, 0x41);
    st7305_write_data(lcd, 0x41);
    st7305_write_data(lcd, 0x41);
    st7305_write_data(lcd, 0x41);

    st7305_write_cmd(lcd, 0xC2);
    st7305_write_data(lcd, 0x19);
    st7305_write_data(lcd, 0x19);
    st7305_write_data(lcd, 0x19);
    st7305_write_data(lcd, 0x19);

    st7305_write_cmd(lcd, 0xC4);
    st7305_write_data(lcd, 0x41);
    st7305_write_data(lcd, 0x41);
    st7305_write_data(lcd, 0x41);
    st7305_write_data(lcd, 0x41);

    st7305_write_cmd(lcd, 0xC5);
    st7305_write_data(lcd, 0x19);
    st7305_write_data(lcd, 0x19);
    st7305_write_data(lcd, 0x19);
    st7305_write_data(lcd, 0x19);

    st7305_write_cmd(lcd, 0xD8);
    st7305_write_data(lcd, 0xA6);
    st7305_write_data(lcd, 0xE9);

    st7305_write_cmd(lcd, 0xB2);
    st7305_write_data(lcd, 0x05);

    st7305_write_cmd(lcd, 0xB3);
    st7305_write_data(lcd, 0xE5);
    st7305_write_data(lcd, 0xF6);
    st7305_write_data(lcd, 0x05);
    st7305_write_data(lcd, 0x46);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x76);
    st7305_write_data(lcd, 0x45);

    st7305_write_cmd(lcd, 0xB4);
    st7305_write_data(lcd, 0x05);
    st7305_write_data(lcd, 0x46);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x77);
    st7305_write_data(lcd, 0x76);
    st7305_write_data(lcd, 0x45);

    st7305_write_cmd(lcd, 0x62);
    st7305_write_data(lcd, 0x32);
    st7305_write_data(lcd, 0x03);
    st7305_write_data(lcd, 0x1F);

    st7305_write_cmd(lcd, 0xB7);
    st7305_write_data(lcd, 0x13);

    st7305_write_cmd(lcd, 0xB0);
    st7305_write_data(lcd, 0x64);

    st7305_write_cmd(lcd, 0x11);
    HAL_Delay(255);

    st7305_write_cmd(lcd, 0xC9);
    st7305_write_data(lcd, 0x00);

    st7305_write_cmd(lcd, 0x36);
    st7305_write_data(lcd, 0x48);

    st7305_write_cmd(lcd, 0x3A);
    st7305_write_data(lcd, 0x11);

    st7305_write_cmd(lcd, 0xB9);
    st7305_write_data(lcd, 0x20);

    st7305_write_cmd(lcd, 0xB8);
    st7305_write_data(lcd, 0x29);

    st7305_write_cmd(lcd, 0x20);   /* Display Inversion OFF：0x00=面板原色, 0xFF=黑 */

    st7305_write_cmd(lcd, 0x2A);
    st7305_write_data(lcd, 0x12);
    st7305_write_data(lcd, 0x2B);

    st7305_write_cmd(lcd, 0x2B);
    st7305_write_data(lcd, 0x00);
    st7305_write_data(lcd, 0xC7);

    st7305_write_cmd(lcd, 0x35);
    st7305_write_data(lcd, 0x00);

    st7305_write_cmd(lcd, 0xD0);
    st7305_write_data(lcd, 0xFF);

    st7305_write_cmd(lcd, 0x39);
    st7305_write_cmd(lcd, 0x29);

    st7305_fill(lcd, ST7305_COLOR_WHITE);
    st7305_refresh(lcd);

    /* 切到 HPM 让首屏快速可见；如需省电，调用方可再发 0x39 切回 LPM */
    st7305_write_cmd(lcd, 0x38);
}

void st7305_fill(ST7305_t *lcd, uint8_t color) {
    memset(s_buffer, color ? 0xFF : 0x00, ST7305_BUFFER_SIZE);
    (void)lcd;
}

void st7305_draw_pixel(ST7305_t *lcd, uint16_t x, uint16_t y, uint8_t color) {
    if (x >= ST7305_WIDTH || y >= ST7305_HEIGHT) {
        return;
    }

    /* 横屏 (W=400, H=300) → 原生 (W=300, H=400) 旋转 90° 顺时针 */
    uint16_t nx = (uint16_t)(ST7305_NATIVE_WIDTH - 1U - y);
    uint16_t ny = x;

    uint16_t real_x = nx / 4;
    uint16_t real_y = ny / 2;
    uint16_t index = real_y * 75 + real_x;
    uint8_t one_two = (ny % 2 == 0) ? 0 : 1;
    uint8_t line_bit_4 = nx % 4;
    uint8_t write_bit = 7 - (line_bit_4 * 2 + one_two);

    if (color) {
        s_buffer[index] |= (1U << write_bit);
    } else {
        s_buffer[index] &= (uint8_t)~(1U << write_bit);
    }

    (void)lcd;
}

void st7305_draw_char(ST7305_t *lcd, uint16_t x, uint16_t y, char ch, uint8_t color, uint8_t scale) {
    const uint8_t *glyph = st7305_get_glyph(ch);

    for (uint8_t col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (uint8_t row = 0; row < 7; row++) {
            if (bits & (1U << row)) {
                for (uint8_t sx = 0; sx < scale; sx++) {
                    for (uint8_t sy = 0; sy < scale; sy++) {
                        st7305_draw_pixel(lcd, x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

void st7305_draw_string(ST7305_t *lcd, uint16_t x, uint16_t y, const char *str, uint8_t color, uint8_t scale) {
    uint16_t cursor_x = x;

    while (*str) {
        st7305_draw_char(lcd, cursor_x, y, *str, color, scale);
        cursor_x += (uint16_t)(6 * scale);
        str++;
    }
}

void st7305_refresh(ST7305_t *lcd) {
    st7305_address(lcd);
    HAL_GPIO_WritePin(lcd->dc_port, lcd->dc_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(lcd->hspi, s_buffer, ST7305_BUFFER_SIZE, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(lcd->cs_port, lcd->cs_pin, GPIO_PIN_SET);
}

/* 进入 Sleep In：驱动 IC 待机电流从 ~30µA 降到 ~5µA，画面保留。*/
void st7305_sleep_in(ST7305_t *lcd) {
    st7305_write_cmd(lcd, 0x28);  /* Display OFF */
    st7305_write_cmd(lcd, 0x10);  /* Sleep In   */
    HAL_Delay(5);
}

/* 退出 Sleep In：下一次刷屏前调用 */
void st7305_sleep_out(ST7305_t *lcd) {
    st7305_write_cmd(lcd, 0x11);  /* Sleep Out  */
    HAL_Delay(5);
    st7305_write_cmd(lcd, 0x29);  /* Display ON */
}
