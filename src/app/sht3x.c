/**
 * @file sht3x.c
 */
#include "sht3x.h"
#include "i2c.h"   /* hi2c1 */

#define SHT3X_ADDR_7BIT  0x44
#define SHT3X_ADDR       (SHT3X_ADDR_7BIT << 1)

/* Commands (MSB first) */
#define CMD_SOFT_RESET   0x30A2
#define CMD_MEAS_HIGH    0x2400   /* clock-stretch disabled, high rep, ~15ms */

static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

static HAL_StatusTypeDef send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return HAL_I2C_Master_Transmit(&hi2c1, SHT3X_ADDR, buf, 2, 100);
}

bool SHT3x_Init(void)
{
    if (send_cmd(CMD_SOFT_RESET) != HAL_OK) return false;
    HAL_Delay(20);
    return true;
}

bool SHT3x_Measure(float *temp_c, float *rh_percent)
{
    if (send_cmd(CMD_MEAS_HIGH) != HAL_OK) return false;
    HAL_Delay(20);                       /* high-rep typ 12.5ms, max 15ms */

    uint8_t rx[6];
    if (HAL_I2C_Master_Receive(&hi2c1, SHT3X_ADDR, rx, 6, 100) != HAL_OK) return false;

    if (crc8(&rx[0], 2) != rx[2]) return false;
    if (crc8(&rx[3], 2) != rx[5]) return false;

    uint16_t raw_t = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t raw_h = ((uint16_t)rx[3] << 8) | rx[4];

    if (temp_c)     *temp_c     = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    if (rh_percent) *rh_percent =          100.0f * ((float)raw_h / 65535.0f);
    return true;
}
