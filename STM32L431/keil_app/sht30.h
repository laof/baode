#ifndef SHT30_H
#define SHT30_H

#include "stm32l4xx_hal.h"
#include <stdint.h>

typedef struct {
    int16_t temp_x10;
    uint16_t rh_x10;
} SHT30_Readout;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t address;
} SHT30_t;

void sht30_init(SHT30_t *dev, I2C_HandleTypeDef *hi2c, uint8_t addr_7bit);
HAL_StatusTypeDef sht30_read(SHT30_t *dev, SHT30_Readout *out);

#endif
