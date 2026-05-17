/**
 * @file app.c
 * @brief Main application: every 5 s read SHT30 and refresh the screen.
 *
 *   - RTC must be running (LSE). On the very first cold boot (BKP register
 *     marker absent), a default time is written so the display shows
 *     something sensible. Set the real time later via the UART command:
 *         T2026-05-17 14:30:00\n
 *
 *   - Sensor: SHT30 / SHT31 at I2C1 0x44.
 *   - Display: ST7305 4.2" mono, landscape 400 x 300.
 */
#include "app.h"
#include "main.h"
#include "rtc.h"
#include "usart.h"

#include "st7305.h"
#include "sht3x.h"
#include "display.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define REFRESH_PERIOD_MS   5000u

/* Marker we keep in an RTC backup register so we know whether the time
 * has ever been set on this board (survives reset, lost on power-loss
 * if VBAT not maintained). */
#define RTC_INIT_MARKER     0xCAFE5A5Au

static uint32_t s_last_tick;
static char     s_uart_line[40];
static uint8_t  s_uart_idx;

/* printf retarget over USART1 */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 10);
    return ch;
}

/* GNU/newlib stub for printf */
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, 100);
    return len;
}

/* ---- RTC helpers ---- */

static void rtc_set_datetime(uint16_t y, uint8_t mo, uint8_t d,
                             uint8_t h, uint8_t mi, uint8_t s)
{
    RTC_DateTypeDef date = {0};
    RTC_TimeTypeDef time = {0};

    date.Year    = (uint8_t)(y - 2000);
    date.Month   = mo;
    date.Date    = d;
    date.WeekDay = RTC_WEEKDAY_MONDAY;   /* not used for display */

    time.Hours   = h;
    time.Minutes = mi;
    time.Seconds = s;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;

    HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);

    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_INIT_MARKER);
}

static void rtc_get_datetime(uint16_t *y, uint8_t *mo, uint8_t *d,
                             uint8_t *h, uint8_t *mi)
{
    RTC_DateTypeDef date;
    RTC_TimeTypeDef time;
    /* HAL requires GetTime to be called BEFORE GetDate to unlock shadow regs */
    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);
    *y  = 2000 + date.Year;
    *mo = date.Month;
    *d  = date.Date;
    *h  = time.Hours;
    *mi = time.Minutes;
}

/* ---- UART line parser:  T2026-05-17 14:30:00\n ---- */
static void try_parse_set_time(const char *line)
{
    if (line[0] != 'T') return;
    unsigned y, mo, d, h, mi, s;
    if (sscanf(line + 1, "%u-%u-%u %u:%u:%u", &y, &mo, &d, &h, &mi, &s) == 6)
    {
        rtc_set_datetime((uint16_t)y, (uint8_t)mo, (uint8_t)d,
                         (uint8_t)h, (uint8_t)mi, (uint8_t)s);
        printf("RTC updated: %04u-%02u-%02u %02u:%02u:%02u\r\n",
               y, mo, d, h, mi, s);
    }
}

static void poll_uart(void)
{
    uint8_t ch;
    /* non-blocking single-byte poll */
    if (HAL_UART_Receive(&huart1, &ch, 1, 0) != HAL_OK) return;

    if (ch == '\r') return;
    if (ch == '\n')
    {
        s_uart_line[s_uart_idx] = '\0';
        try_parse_set_time(s_uart_line);
        s_uart_idx = 0;
        return;
    }
    if (s_uart_idx < sizeof(s_uart_line) - 1)
        s_uart_line[s_uart_idx++] = (char)ch;
    else
        s_uart_idx = 0;   /* overflow, drop line */
}

/* ---- Main app ---- */

static void render_now(void)
{
    DisplayData d = {0};
    rtc_get_datetime(&d.year, &d.month, &d.day, &d.hour, &d.minute);

    float t = NAN, h = NAN;
    d.sensor_ok = SHT3x_Measure(&t, &h);
    d.temp_c = t;
    d.rh     = h;

    printf("[%04u-%02u-%02u %02u:%02u] T=%.2fC RH=%.2f%% ok=%d\r\n",
           d.year, d.month, d.day, d.hour, d.minute, t, h, d.sensor_ok);

    Display_Render(&d);
}

void App_Init(void)
{
    printf("\r\n[boot] STM32L431 + ST7305 + SHT3x\r\n");

    /* If the RTC has never been set on this board, seed it. */
    HAL_PWR_EnableBkUpAccess();
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != RTC_INIT_MARKER)
    {
        rtc_set_datetime(2026, 5, 17, 14, 30, 0);
        printf("[boot] RTC seeded with default time.\r\n");
    }

    ST7305_Init();
    SHT3x_Init();

    render_now();
    s_last_tick = HAL_GetTick();
}

void App_Loop(void)
{
    poll_uart();

    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_tick) >= REFRESH_PERIOD_MS)
    {
        s_last_tick = now;
        render_now();
    }
}
