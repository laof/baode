
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

ESP32 联 Wi-Fi → NTP 同步 → 每 10s 通过 UART 发送 `T:YYYY-MM-DD HH:MM:SS\n` 给 STM32。

**物理接线（在原有基础上加 3 根）**

| ESP32-C3 | STM32 (USART2) | 说明 |
|---|---|---|
| **GPIO4** (Serial1 TX) | **PA3** (USART2_RX) | 时间数据 ESP→STM（必接） |
| **GPIO5** (Serial1 RX) | **PA2** (USART2_TX) | 预留双向，暂时可不接 |
| **GND** | **GND** | 共地（必接） |

> **供电说明**：
> - 推荐**两块板各自插 USB 独立供电**，此时**只连 GND**，不要连 VCC。
> - 如果只想给一块板供电、由它带另一块：再加一根 **3V3 ↔ 3V3**（例如 STM32 调试器供电，把 STM32 的 3V3 接到 ESP32 的 3V3 引脚）。
> - **严禁** 5V ↔ 3V3 互连，会烧板。GND 任何情况下都必须连通，否则 UART 没有参考电平。

波特率 **115200 8N1**，帧格式 `T:YYYY-MM-DD HH:MM:SS\n`（21 字节正文 + LF，例如 `T:2026-12-06 14:45:01\n`）。ESP32 每 10 秒发一帧，STM32 端按本地秒数自走，屏幕每秒刷新一次。

> ESP32-C3 默认 `Serial` 走 USB-CDC，调试日志看 USB；与 STM32 通讯走 `Serial1` (GPIO4/5)，互不冲突。
> Wi-Fi SSID/密码放在 `esp32-c3-mini/secrets.h`（已被 `.gitignore` 排除），首次使用复制 `secrets.h.example` 为 `secrets.h` 并填入。

