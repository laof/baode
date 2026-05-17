/**
 * @file display.h
 * @brief High-level rendering: clock (HH:MM), temperature, humidity.
 *        Landscape 400 x 300.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;     /* 2000..2099 */
    uint8_t  month;    /* 1..12 */
    uint8_t  day;      /* 1..31 */
    uint8_t  hour;     /* 0..23 */
    uint8_t  minute;   /* 0..59 */
    float    temp_c;   /* -40..125 */
    float    rh;       /* 0..100  ; NAN if sensor failed */
    bool     sensor_ok;
} DisplayData;

void Display_Render(const DisplayData *d);   /* clears, draws, flushes */

#ifdef __cplusplus
}
#endif

#endif
