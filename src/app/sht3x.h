/**
 * @file  sht3x.h
 * @brief SHT30 / SHT31 I2C driver (single-shot, high repeatability, CRC).
 *        Address 0x44 by default (ADDR pin tied to GND).
 */
#ifndef SHT3X_H
#define SHT3X_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

bool SHT3x_Init(void);                                 /* soft reset */
bool SHT3x_Measure(float *temp_c, float *rh_percent);  /* blocking, ~20ms */

#ifdef __cplusplus
}
#endif

#endif /* SHT3X_H */
