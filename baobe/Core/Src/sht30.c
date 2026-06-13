#include "sht30.h"

#define SHT30_CMD_MEASURE_HIGHREP 0x2400

static uint8_t sht30_crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0xFF;

    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

void sht30_init(SHT30_t *dev, I2C_HandleTypeDef *hi2c, uint8_t addr_7bit) {
    dev->hi2c = hi2c;
    dev->address = (uint8_t)(addr_7bit << 1);
}

HAL_StatusTypeDef sht30_read(SHT30_t *dev, SHT30_Readout *out) {
    uint8_t cmd[2] = { (uint8_t)(SHT30_CMD_MEASURE_HIGHREP >> 8), (uint8_t)SHT30_CMD_MEASURE_HIGHREP };
    uint8_t rx[6];
    HAL_StatusTypeDef status;

    /* 50ms 超时：SHT30 未接/上拉缺失时快速返回，避免主循环 HSI16 满速转 (~1mA) */
    status = HAL_I2C_Master_Transmit(dev->hi2c, dev->address, cmd, 2, 50);
    if (status != HAL_OK) {
        return status;
    }

    HAL_Delay(15);

    status = HAL_I2C_Master_Receive(dev->hi2c, dev->address, rx, 6, 50);
    if (status != HAL_OK) {
        return status;
    }

    if (sht30_crc8(rx, 2) != rx[2] || sht30_crc8(&rx[3], 2) != rx[5]) {
        return HAL_ERROR;
    }

    uint16_t raw_t = (uint16_t)(rx[0] << 8) | rx[1];
    uint16_t raw_rh = (uint16_t)(rx[3] << 8) | rx[4];

    int32_t temp_x10 = -450 + (1750 * (int32_t)raw_t + 32767) / 65535;
    uint32_t rh_x10 = (1000U * raw_rh + 32767) / 65535;

    out->temp_x10 = (int16_t)temp_x10;
    out->rh_x10 = (uint16_t)rh_x10;

    return HAL_OK;
}
