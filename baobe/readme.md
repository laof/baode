
## ST7305 4.2" 屏（SPI）

| STM32 | 屏幕 | 说明 |
|---|---|---|
| 3V3 | VCC | 电源 |
| GND | GND | 共地 |
| A5 | SCL (SCK) | SPI1_SCK |
| A7 | SDA (MOSI) | SPI1_MOSI |
| A4 | CS  | 片选 |
| A6 | DC  | 命令/数据 |
| B0 | RES | 复位 |
| —   | BLC | 反射屏，不接 |


## SHT30 温湿度（I2C）

| STM32 | SHT30 模块 | 说明 |
|---|---|---|
| 3V3 | VCC / VIN | 电源 |
| GND | GND | 共地 |
| B6 | SCL | I2C1_SCL |
| B7 | SDA | I2C1_SDA |
| GND | ADDR | 接 GND → 7-bit 地址 0x44；接 VDD → 0x45 |


模块板通常已带 10kΩ 上拉；裸芯片需在 SDA/SCL 各加 4.7~10kΩ 上拉到 3V3。


## ESP32-C3-Pro Mini 副控（UART 时间桥）

ESP32 联 Wi-Fi → NTP 同步 → 每 30s 通过 UART 发送 `T:HH:MM\n` 给 STM32。

| STM32 (USART2) | ESP32-C3 | 说明 |
|---|---|---|
| 3V3 / 独立供电 | 3V3 | 两板可共用电源，**必须共地** |
| GND | GND | 共地 |
| **A3** (USART2_RX) | **GPIO4** (Serial1 TX) | ESP→STM 时间数据（核心连接） |
| **A2** (USART2_TX) | **GPIO5** (Serial1 RX) | 预留双向，暂不用，可不接 |

波特率 **115200 8N1**，帧格式 `T:YYYY-MM-DD HH:MM:SS\n`（22 字节，例如 `T:2026-12-06 14:45:01\n`）。ESP32 每 10 秒发一帧，STM32 端按本地秒数自走，屏幕每秒刷新一次。

> ESP32-C3 默认 `Serial` 走 USB-CDC，调试日志看 USB；与 STM32 通讯走 `Serial1` (GPIO4/5)，互不冲突。
> ESP32 端 Wi-Fi SSID/密码在 `esp32-c3-mini.ino` 顶部的 `WIFI_SSID`/`WIFI_PASS` 修改。

