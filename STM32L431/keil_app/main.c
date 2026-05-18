#include "stm32l4xx_hal.h"
#include "st7305.h"
#include "sht30.h"
#include <stdio.h>
#include <string.h>

SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c1;
RTC_HandleTypeDef hrtc;

#define LCD_DC_GPIO_Port GPIOA
#define LCD_DC_Pin GPIO_PIN_3
#define LCD_RST_GPIO_Port GPIOA
#define LCD_RST_Pin GPIO_PIN_2
#define LCD_CS_GPIO_Port GPIOA
#define LCD_CS_Pin GPIO_PIN_4

static ST7305_t lcd;
static SHT30_t sht30;

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C1_Init(void);
static void MX_RTC_Init(void);

static void render_screen(const RTC_TimeTypeDef *time, const SHT30_Readout *sensor) {
    char time_str[6];
    char temp_str[16];
    char hum_str[16];

    int16_t temp_x10 = sensor->temp_x10;
    uint16_t rh_x10 = sensor->rh_x10;
    int16_t temp_abs = (temp_x10 < 0) ? (int16_t)(-temp_x10) : temp_x10;

    snprintf(time_str, sizeof(time_str), "%02d:%02d", time->Hours, time->Minutes);
    snprintf(temp_str, sizeof(temp_str), "T:%s%d.%dC", temp_x10 < 0 ? "-" : "", temp_abs / 10, temp_abs % 10);
    snprintf(hum_str, sizeof(hum_str), "H:%d.%d%%", rh_x10 / 10, rh_x10 % 10);

    st7305_fill(&lcd, ST7305_COLOR_WHITE);
    st7305_draw_string(&lcd, 10, 20, time_str, ST7305_COLOR_BLACK, 4);
    st7305_draw_string(&lcd, 10, 120, temp_str, ST7305_COLOR_BLACK, 3);
    st7305_draw_string(&lcd, 10, 180, hum_str, ST7305_COLOR_BLACK, 3);
    st7305_refresh(&lcd);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_I2C1_Init();
    MX_RTC_Init();

    lcd.hspi = &hspi1;
    lcd.dc_port = LCD_DC_GPIO_Port;
    lcd.dc_pin = LCD_DC_Pin;
    lcd.rst_port = LCD_RST_GPIO_Port;
    lcd.rst_pin = LCD_RST_Pin;
    lcd.cs_port = LCD_CS_GPIO_Port;
    lcd.cs_pin = LCD_CS_Pin;

    st7305_init(&lcd);
    sht30_init(&sht30, &hi2c1, 0x44);

    uint32_t last_tick = 0;

    while (1) {
        if (HAL_GetTick() - last_tick >= 5000U) {
            last_tick = HAL_GetTick();

            RTC_TimeTypeDef time = {0};
            RTC_DateTypeDef date = {0};
            SHT30_Readout sensor = {0};

            HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
            HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);

            if (sht30_read(&sht30, &sensor) != HAL_OK) {
                sensor.temp_x10 = 0;
                sensor.rh_x10 = 0;
            }

            render_screen(&time, &sensor);
        }
    }
}

static void SystemClock_Config(void) {
    /* Use CubeMX to generate this function for your clock tree. */
}

static void MX_GPIO_Init(void) {
    /* Use CubeMX to generate GPIO init, then ensure LCD pins are outputs. */
}

static void MX_SPI1_Init(void) {
    /* Use CubeMX to init SPI1: Mode Master, 8-bit, MSB, CPOL=0, CPHA=0. */
}

static void MX_I2C1_Init(void) {
    /* Use CubeMX to init I2C1 for SHT30 (100k or 400k). */
}

static void MX_RTC_Init(void) {
    /* Use CubeMX to init RTC (LSE preferred). */
}
