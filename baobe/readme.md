
## ST7305 4.2" 屏（SPI）

| STM32 | 屏幕 | 说明 |
|---|---|---|
| 3V3 | VCC | 电源 |
| GND | GND | 共地 |
| PA5 | SCL (SCK) | SPI1_SCK |
| PA7 | SDA (MOSI) | SPI1_MOSI |
| PA4 | CS  | 片选 |
| PA6 | DC  | 命令/数据 |
| PB0 | RES | 复位 |
| —   | BLC | 反射屏，不接 |


## SHT30 温湿度（I2C）

| STM32 | SHT30 模块 | 说明 |
|---|---|---|
| 3V3 | VCC / VIN | 电源 |
| GND | GND | 共地 |
| PB6 | SCL | I2C1_SCL |
| PB7 | SDA | I2C1_SDA |
| GND | ADDR | 接 GND → 7-bit 地址 0x44；接 VDD → 0x45 |


模块板通常已带 10kΩ 上拉；裸芯片需在 SDA/SCL 各加 4.7~10kΩ 上拉到 3V3。
