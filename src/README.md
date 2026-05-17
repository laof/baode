# 时钟 + 温湿度（STM32L431CBT6 + ST7305 4.2" Mono TFT + SHT30）

> 板子：STM32L431CBT6（8M HSE，LSE 32.768 kHz），ST-Link  
> 屏：沃乐康 4.2" 单色反射屏 W420HC018MONO-12Z，主控 ST7305，300×400，SPI，**横屏 400×300 使用**  
> 传感器：SHT30 / SHT31（I2C，地址 0x44）  
> 行为：每 5 秒读 1 次温湿度并刷新屏幕；时钟只显示 时:分

---

## 1. 接线图

### 1.1 ST7305 4.2" Mono TFT（12-pin 0.5mm FPC，仅用 8 根线，SPI 4-wire 写命令）

| FPC Pin | 屏丝印 | 含义 | STM32L431CBT6 | 说明 |
|---|---|---|---|---|
| —  | VDD  | 3.3V 电源 | **3V3** | LDO 输出，建议加 10uF |
| —  | GND  | 地        | **GND** | — |
| —  | SCL  | SPI 时钟  | **PA5**（SPI1_SCK）   | 推挽，最高 40MHz；本工程 10MHz |
| —  | SDA  | SPI MOSI  | **PA7**（SPI1_MOSI）  | 主写从（屏只接收） |
| —  | CS   | 片选      | **PA4**（GPIO 输出）  | 低有效；用普通 GPIO，不用 NSS |
| —  | DC   | 数据/命令 | **PA6**（GPIO 输出）  | 高=数据，低=命令 |
| —  | RES  | 复位      | **PB0**（GPIO 输出）  | 低有效，上电拉一次 |
| —  | TE   | 撕裂同步  | 悬空 | 本工程不用 |
| —  | LED+/LED- | 背光  | 悬空 | 反射屏无需背光 |

> 注：ST7305 是只写器件，**MISO 不接**。CS/DC/RES 用普通 GPIO 推挽即可。

### 1.2 SHT30 / SHT31（I2C，3.3V 供电，地址 0x44）

| SHT30 | STM32L431CBT6 | 说明 |
|---|---|---|
| VDD | **3V3** | 加 100nF 退耦 |
| GND | **GND** | — |
| SCL | **PB6**（I2C1_SCL） | 需要 4.7 kΩ 上拉到 3V3 |
| SDA | **PB7**（I2C1_SDA） | 需要 4.7 kΩ 上拉到 3V3 |
| ADDR | **GND** | 接地 → 地址 0x44；接 VDD → 0x45（本工程按 0x44） |

### 1.3 调试串口（USART1，可选，用于打印）

| STM32 | USB-TTL |
|---|---|
| **PA9** TX | RX |
| **PA10** RX | TX |
| GND | GND |

### 1.4 ST-Link（SWD，烧录调试）

| ST-Link | STM32 |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND   | GND  |
| 3V3   | 3V3（如自供电则不接） |

### 1.5 LSE 晶振

- PC14 / PC15 接 32.768 kHz 晶振 + 2× 12.5pF 电容（板子已有）

### 1.6 接线总览图

```
                ┌────────────────────────────────────┐
                │   STM32L431CBT6 (8M HSE + LSE)     │
                │                                    │
   ST-Link SWD ─┤ PA13/PA14                          │
                │                                    │
   USB-TTL  ────┤ PA9  TX ────────► RX (调试)        │
                │ PA10 RX ◄──────── TX               │
                │                                    │
   ST7305 屏   ─┤ PA5  SCK ──────► SCL               │
                │ PA7  MOSI ─────► SDA               │
                │ PA4  GPIO ─────► CS                │
                │ PA6  GPIO ─────► DC                │
                │ PB0  GPIO ─────► RES               │
                │                  3V3/GND           │
                │                                    │
   SHT30       ─┤ PB6  I2C1_SCL ─► SCL  ┐           │
                │ PB7  I2C1_SDA ─► SDA  ├ 4.7kΩ 上拉│
                │                  3V3/GND           │
                │                                    │
   LSE 32.768k ─┤ PC14/PC15                          │
                └────────────────────────────────────┘
```

---

## 2. STM32CubeMX 配置（新建工程时按此勾选）

1. **MCU**：`STM32L431CBT6`
2. **System Core → RCC**
   - HSE：`Crystal/Ceramic Resonator`
   - LSE：`Crystal/Ceramic Resonator`
3. **Clock Configuration**
   - HSE = 8 MHz，PLL 倍频到 **SYSCLK = 80 MHz**
   - RTC 时钟源选 **LSE**
4. **System Core → SYS**
   - Debug：`Serial Wire`
   - Timebase Source：`SysTick`
5. **Timers → RTC**
   - `Activate Clock Source`、`Activate Calendar`
   - 时间格式 24H；初始时间随便填，运行后通过代码或串口改
6. **Connectivity → SPI1**
   - Mode：`Transmit Only Master`
   - Data Size：8 bits，MSB First
   - CPOL = High，CPHA = 2 Edge（Mode 3）
   - Prescaler：使波特率 ≈ 10 MHz（SYSCLK 80 MHz → /8）
   - NSS：`Disable`（CS 用 GPIO 软件控制）
7. **Connectivity → I2C1**
   - Mode：`I2C`
   - Speed：`Fast Mode`（400 kHz）
8. **Connectivity → USART1**
   - Mode：`Asynchronous`，115200 8N1
9. **GPIO 输出**（在 Pinout 视图右键设 GPIO_Output）
   - **PA4** → User Label: `LCD_CS`，初始 High，推挽，High Speed
   - **PA6** → User Label: `LCD_DC`，初始 High，推挽，High Speed
   - **PB0** → User Label: `LCD_RES`，初始 High，推挽，High Speed
10. **Project Manager**
    - Toolchain：`MDK-ARM V5`
    - Code Generator：勾选 `Generate peripheral initialization as a pair of '.c/.h' files`
11. 生成代码 → 用 Keil 打开 → 把本目录 `app/` 下的所有文件加入工程（`Core/Src` 与 `Core/Inc` 同级新建一个 `App` 分组）→ 在 `Core/Src/main.c` 中按 §4 加几行集成代码。

---

## 3. 工程文件清单

```
src/
├── README.md                  ← 本文件
└── app/
    ├── st7305.h / st7305.c    ← ST7305 SPI 驱动 + 帧缓冲 + 90° 旋转(400×300)
    ├── sht3x.h  / sht3x.c     ← SHT30/31 I2C 驱动（CRC 校验）
    ├── font8x16.h / .c        ← 8×16 ASCII 字体（可按倍率缩放）
    ├── display.h / display.c  ← 高层渲染：时钟/温度/湿度页面
    ├── app.h / app.c          ← 5 秒主循环、首次上电写 RTC
    └── main_integration.txt   ← main.c 集成代码片段
```

## 4. 在 CubeMX 生成的 `Core/Src/main.c` 里要做的事

只在 `USER CODE` 区段加 3 处代码（详见 `app/main_integration.txt`）：

```c
/* USER CODE BEGIN Includes */
#include "app.h"
/* USER CODE END Includes */

/* 在 MX_*_Init() 全部调用完之后 */
/* USER CODE BEGIN 2 */
App_Init();
/* USER CODE END 2 */

/* USER CODE BEGIN WHILE */
while (1)
{
    App_Loop();        /* 内部自带 5s 节拍，非阻塞调度 */
/* USER CODE END WHILE */
```

## 5. 烧录

Keil → Options → Debug → 选 **ST-Link**，Settings 里 Reset and Run 勾上 → F8 下载。

## 6. 修改时间

首次运行后，串口（115200 8N1）发送下面格式即可设置 RTC：

```
T2026-05-17 14:30:00\n
```

（解析逻辑在 `app.c` 里，可按需删减）
