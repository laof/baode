/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *５２１
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7305.h"
#include "sht30.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;
RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN PV */
static ST7305_t g_lcd;
static SHT30_t  g_sht30;

/* 本地维护的时钟（被 UART 同步帧出现时覆盖，平时按秒自走）*/
static volatile uint8_t  g_time_synced = 0;
static char              g_date[11]    = "2026-01-01";  /* YYYY-MM-DD\0 */
static volatile uint8_t  g_hh = 0, g_mm = 0, g_ss = 0;

/* USART2 单字节接收 + 行缓冲 */
static uint8_t  g_uart_rx_byte;
static char     g_line_buf[32];
static uint8_t  g_line_len = 0;

/* 副控连接/Wi-Fi 状态 */
static volatile uint32_t g_esp_last_seen_ms = 0;   /* 最后一次收到任意帧的 uptime 秒（变量名保留兼容） */
static volatile uint8_t  g_wifi_up = 0;            /* 0=down/unknown, 1=up */

/* 天气：0=晴 1=多云 2=阴/雾 3=雨 4=雪，-1=未知 */
static volatile int8_t   g_w_code   = -1;
static volatile int8_t   g_w_temp_c = 0;
static volatile uint32_t g_w_last_seen_ms = 0;

/* 副控电源管理：每 3 小时唤醒 ESP32 一次，完成校时/天气后切电 */
#define ESP_CYCLE_S       (3UL * 3600UL)            /* 3 小时一周期 */
#define ESP_TIMEOUT_S     (90UL)                    /* 单次会话最长 90s */
#define ESP_RETRY_S       (10UL * 60UL)             /* 失败后 10 分钟重试 */
static volatile uint8_t  g_esp_power_on    = 0;     /* 当前是否已上电 */
static volatile uint8_t  g_esp_session_done= 0;     /* 收到 D 帧，可以切电 */
static uint32_t          g_esp_power_on_s   = 0;    /* 本次上电时的 uptime 秒 */
static uint32_t          g_esp_last_done_s  = 0;    /* 上一次成功完成时的 uptime 秒，0=尚未完成 */
static uint8_t           g_esp_first_boot   = 1;    /* 首次启动需要立刻拉 ESP */

/* 上电后的秒计数（由 RTC 唤醒驱动，跨 Stop 依然准确）*/
static volatile uint32_t g_uptime_s = 0;
static volatile uint8_t  g_wakeup_pending = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void render_screen(const char *date, uint8_t hh, uint8_t mm, uint8_t ss,
                          int16_t temp_x10, uint16_t rh_x10,
                          uint8_t esp_online, uint8_t wifi_up,
                          int8_t w_code, int8_t w_temp_c,
                          uint8_t bat_bars);
static void MX_RTC_Init(void);
static void rtc_set_wakeup_seconds(uint32_t s);
static void rtc_read_into_globals(void);
static void rtc_write_datetime(const char *date, uint8_t hh, uint8_t mm, uint8_t ss);
static void enable_usart2_stop_wakeup(void);
static void enter_stop2_and_restore(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ===== 副控 ESP32 电源控制 ===== */
static void esp_power_set(uint8_t on)
{
  HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  g_esp_power_on = on;
  if (on)
  {
    g_esp_power_on_s    = g_uptime_s;
    g_esp_session_done  = 0;
    g_wifi_up           = 0;
    g_esp_last_seen_ms  = 0;
  }
}

/* ===== 电池电量（PA1 -> ADC1_IN6，100k/100k 分压，VREFINT 校准）===== */
static ADC_HandleTypeDef hadc_bat;

static void bat_init(void)
{
  __HAL_RCC_ADC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* PA1 模拟输入 */
  GPIO_InitTypeDef g = {0};
  g.Pin  = GPIO_PIN_1;
  g.Mode = GPIO_MODE_ANALOG;
  g.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &g);

  hadc_bat.Instance                   = ADC1;
  hadc_bat.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc_bat.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc_bat.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc_bat.Init.ScanConvMode          = ADC_SCAN_DISABLE;
  hadc_bat.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc_bat.Init.LowPowerAutoWait      = DISABLE;
  hadc_bat.Init.ContinuousConvMode    = DISABLE;
  hadc_bat.Init.NbrOfConversion       = 1;
  hadc_bat.Init.DiscontinuousConvMode = DISABLE;
  hadc_bat.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc_bat.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc_bat.Init.DMAContinuousRequests = DISABLE;
  hadc_bat.Init.Overrun               = ADC_OVR_DATA_PRESERVED;
  hadc_bat.Init.OversamplingMode      = ENABLE;
  hadc_bat.Init.Oversampling.Ratio                  = ADC_OVERSAMPLING_RATIO_16;
  hadc_bat.Init.Oversampling.RightBitShift          = ADC_RIGHTBITSHIFT_4;
  hadc_bat.Init.Oversampling.TriggeredMode          = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
  hadc_bat.Init.Oversampling.OversamplingStopReset  = ADC_REGOVERSAMPLING_CONTINUED_MODE;
  if (HAL_ADC_Init(&hadc_bat) != HAL_OK) { return; }
  HAL_ADCEx_Calibration_Start(&hadc_bat, ADC_SINGLE_ENDED);
}

static uint16_t bat_read_ch(uint32_t channel)
{
  /* 每次读取前重开时钟（bat_read_mv 完成后会再次关闭）*/
  __HAL_RCC_ADC_CLK_ENABLE();
  ADC_ChannelConfTypeDef c = {0};
  c.Channel      = channel;
  c.Rank         = ADC_REGULAR_RANK_1;
  c.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
  c.SingleDiff   = ADC_SINGLE_ENDED;
  c.OffsetNumber = ADC_OFFSET_NONE;
  c.Offset       = 0;
  if (HAL_ADC_ConfigChannel(&hadc_bat, &c) != HAL_OK) return 0;
  if (HAL_ADC_Start(&hadc_bat) != HAL_OK) return 0;
  if (HAL_ADC_PollForConversion(&hadc_bat, 100) != HAL_OK) { HAL_ADC_Stop(&hadc_bat); return 0; }
  uint16_t v = (uint16_t)HAL_ADC_GetValue(&hadc_bat);
  HAL_ADC_Stop(&hadc_bat);
  return v;
}

/* 返回当前 VBAT 毫伏；用 VREFINT 校准 VDDA，再算分压后的电池电压 */
static uint32_t bat_read_mv(void)
{
  uint16_t vref_raw = bat_read_ch(ADC_CHANNEL_VREFINT);
  uint16_t pa1_raw  = bat_read_ch(ADC_CHANNEL_6);
  if (vref_raw == 0) return 0;
  uint16_t vref_cal = *(uint16_t *)0x1FFF75AAU;          /* STM32L4 VREFINT 校准地址 */
  uint32_t vdda_mv  = (3000UL * vref_cal) / vref_raw;     /* 校准值在 3.0V 下测得 */
  /* PA1 经 100k/100k 分压看到 VBAT/2，所以 *2 还原 */
  uint32_t vbat_mv  = ((uint32_t)pa1_raw * vdda_mv * 2UL) / 4095UL;

  /* 量完立刻关 ADC 时钟，避免持续耗 ~150 µA（ADEN 已在 HAL_ADC_Stop 内清零）*/
  __HAL_RCC_ADC_CLK_DISABLE();
  return vbat_mv;
}

/* LiPo 单节经验分段：5 = 100..81%, 4 = 80..61%, 3 = 60..41%, 2 = 40..21%, 1 = 20..6%, 0 = 极低 */
static uint8_t bat_mv_to_bars(uint32_t mv)
{
  if (mv >= 4100U) return 5;
  if (mv >= 3900U) return 4;
  if (mv >= 3800U) return 3;
  if (mv >= 3700U) return 2;
  if (mv >= 3500U) return 1;
  return 0;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  g_lcd.hspi     = &hspi1;
  g_lcd.cs_port  = LCD_CS_GPIO_Port;  g_lcd.cs_pin  = LCD_CS_Pin;
  g_lcd.dc_port  = LCD_DS_GPIO_Port;  g_lcd.dc_pin  = LCD_DS_Pin;   /* DS = DC */
  g_lcd.rst_port = LCD_RES_GPIO_Port; g_lcd.rst_pin = LCD_RES_Pin;

  st7305_init(&g_lcd);

  bat_init();

  /* SHT30 I2C 7-bit 地址：ADDR 脚接 GND => 0x44；接 VDD => 0x45 */
  sht30_init(&g_sht30, &hi2c1, 0x44);

  /* 启动 USART2 接收（单字节中断，循环重启）*/
  HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1);

  /* RTC + 5s 周期唤醒 + USART2 Stop 唤醒：构成 Stop 2 低功耗的基础 */
  MX_RTC_Init();
  enable_usart2_stop_wakeup();
  // 每5秒刷新一次，既能保证时间显示更新，又能定期读传感器和电池电压
  rtc_set_wakeup_seconds(5);

  /* 调试器拔下后强制让 Stop 模式真的停掉内核时钟（插着 SWD 时会被这条覆盖） */
  HAL_DBGMCU_DisableDBGStopMode();

  int16_t  last_temp_x10 = 0;
  uint16_t last_rh_x10   = 0;
  uint8_t  last_bat_bars = bat_mv_to_bars(bat_read_mv());
  uint32_t last_bat_s    = 0;

  /* 开机立即给副控上电，做第一次校时/天气 */
  esp_power_set(1);
  g_esp_power_on_s  = g_uptime_s;
  g_esp_first_boot  = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    /* 副控电源状态机：每 3 小时上电一次，完成会话或超时后切电 */
    if (g_esp_power_on)
    {
      if (g_esp_session_done)
      {
        esp_power_set(0);
        g_esp_last_done_s = g_uptime_s ? g_uptime_s : 1U;
      }
      else if ((g_uptime_s - g_esp_power_on_s) >= ESP_TIMEOUT_S)
      {
        esp_power_set(0);
        g_esp_last_done_s = g_uptime_s - (ESP_CYCLE_S - ESP_RETRY_S);
        if (g_esp_last_done_s == 0) g_esp_last_done_s = 1U;
      }
    }
    else
    {
      uint32_t since = g_uptime_s - g_esp_last_done_s;
      if (g_esp_last_done_s != 0 && since >= ESP_CYCLE_S)
      {
        esp_power_set(1);
        g_esp_power_on_s = g_uptime_s;
      }
    }

    /* 读 SHT30（每次唤醒，5s 一次） */
    {
      SHT30_Readout r;
      if (sht30_read(&g_sht30, &r) == HAL_OK)
      {
        last_temp_x10 = r.temp_x10;
        last_rh_x10   = r.rh_x10;
      }
    }

    /* 电池每 30s 采一次 */
    if ((g_uptime_s - last_bat_s) >= 30U)
    {
      last_bat_s    = g_uptime_s;
      last_bat_bars = bat_mv_to_bars(bat_read_mv());
    }

    /* 从 RTC 读最新时间到 g_hh/g_mm/g_ss/g_date，然后刷屏 */
    rtc_read_into_globals();
    {
      uint8_t esp_online = (g_esp_last_seen_ms != 0) &&
                           ((g_uptime_s - g_esp_last_seen_ms) < 60U);
      int8_t  w_code     = (g_w_last_seen_ms != 0) ? g_w_code   : (int8_t)-1;
      int8_t  w_temp_c   = (g_w_last_seen_ms != 0) ? g_w_temp_c : (int8_t)0;
      /* ST7305 是双稳态屏，IC 自身常态待机 ~30µA，开 SLPIN 需 120ms 唤醒 delay
         反而让平均功耗变高。这里不进 sleep，直接刷屏。 */
      render_screen(g_date, g_hh, g_mm, g_ss, last_temp_x10, last_rh_x10,
                    esp_online, g_wifi_up, w_code, w_temp_c, last_bat_bars);
    }

    /* 重新挂上 UART 接收（Stop 唤醒后中断链路可能需要重新 arm） */
    HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1);

    /* 进入 Stop 2 等待下次 RTC 唤醒或 UART 起始位唤醒 */
    g_wakeup_pending = 0;
    enter_stop2_and_restore();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef p = {0};

  /* 低功耗策略：SYSCLK = HSI 16MHz，调压器 Range 2，Flash 2WS
     - 不启 PLL，关闭 MSI
     - Run 电流约 1 mA，为 80MHz PLL 的 ~1/8
     - SPI/UART/RTC 所需有效时钟均在下面调整 */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /* 1) 开 LSE + HSI16；MSI 暂保留（可能是当前 SYSCLK 源）；PLL 关闭 */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_OFF;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* 2) SYSCLK = HSI16，AHB/APB 不分频，Flash 2WS（兼容 Range 1/Range 2 @16MHz）*/
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  /* 3) 现在 SYSCLK 是 HSI，可以安全关掉 MSI */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_OFF;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* 4) 切到 Range 2 调压器（要求 SYSCLK ≤ 26MHz，现为 16MHz，满足）*/
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2) != HAL_OK)
  {
    Error_Handler();
  }

  /* RTC 时钟源 = LSE，USART2 时钟源 = HSI16（供 Stop 模式唤醒使用）*/
  p.PeriphClockSelection = RCC_PERIPHCLK_RTC | RCC_PERIPHCLK_USART2;
  p.RTCClockSelection    = RCC_RTCCLKSOURCE_LSE;
  p.Usart2ClockSelection = RCC_USART2CLKSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&p) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;   /* Mode 0 for ST7305 */
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;       /* Mode 0 for ST7305 */
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;  /* PCLK2 16MHz /2 = 8MHz */
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function (PB6=SCL, PB7=SDA, 100kHz)
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00303D5B;          /* 100kHz @ PCLK1=16MHz (HSI16) */
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization (PA2=TX, PA3=RX, 115200 8N1)
  */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* ===== RTC + Stop2 低功耗辅助 ===== */

/* 初始化 RTC（LSE 32.768kHz 驱动，1Hz 计时）。首次上电填默认时间，后续复位保留 RTC 计时。*/
static void MX_RTC_Init(void)
{
  __HAL_RCC_RTC_ENABLE();
  __HAL_RCC_RTCAPB_CLK_ENABLE();

  hrtc.Instance            = RTC;
  hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv   = 127;
  hrtc.Init.SynchPrediv    = 255;
  hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK) { Error_Handler(); }

  /* 首次上电才写默认时间，靠备份寄存器 0 作为“已初始化”标记 */
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != 0x32F2U)
  {
    RTC_DateTypeDef d = {0};
    RTC_TimeTypeDef t = {0};
    d.Year    = 26;
    d.Month   = RTC_MONTH_JANUARY;
    d.Date    = 1;
    d.WeekDay = RTC_WEEKDAY_THURSDAY;
    t.Hours   = 0; t.Minutes = 0; t.Seconds = 0;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x32F2U);
  }

  HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}

/* 设置 RTC 周期唤醒（1Hz 时钟源，1–65535s）*/
static void rtc_set_wakeup_seconds(uint32_t s)
{
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, (uint32_t)(s - 1U), RTC_WAKEUPCLOCK_CK_SPRE_16BITS);
}

/* 从 RTC 读取当前日期时间到 g_date / g_hh / g_mm / g_ss */
static void rtc_read_into_globals(void)
{
  RTC_TimeTypeDef t;
  RTC_DateTypeDef d;
  /* HAL 要求读 Time 后紧接着读 Date 以解锁影子寄存器 */
  HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
  HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);
  g_hh = t.Hours; g_mm = t.Minutes; g_ss = t.Seconds;
  snprintf(g_date, sizeof(g_date), "20%02d-%02d-%02d", d.Year, d.Month, d.Date);
}

/* 将 "YYYY-MM-DD" + HH:MM:SS 写入 RTC */
static void rtc_write_datetime(const char *date, uint8_t hh, uint8_t mm, uint8_t ss)
{
  if (date == NULL) return;
  int yyyy = (date[0]-'0')*1000 + (date[1]-'0')*100 + (date[2]-'0')*10 + (date[3]-'0');
  int mo   = (date[5]-'0')*10  + (date[6]-'0');
  int dd   = (date[8]-'0')*10  + (date[9]-'0');
  if (yyyy < 2000 || yyyy > 2099 || mo < 1 || mo > 12 || dd < 1 || dd > 31) return;

  RTC_DateTypeDef d = {0};
  RTC_TimeTypeDef t = {0};
  d.Year  = (uint8_t)(yyyy - 2000);
  d.Month = (uint8_t)mo;
  d.Date  = (uint8_t)dd;
  d.WeekDay = RTC_WEEKDAY_MONDAY;
  t.Hours = hh; t.Minutes = mm; t.Seconds = ss;
  t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  t.StoreOperation = RTC_STOREOPERATION_RESET;
  HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
  HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
}

/* 启用 USART2 在 Stop 下被起始位唤醒（USART2 时钟源须为 HSI16/LSE）*/
static void enable_usart2_stop_wakeup(void)
{
  UART_WakeUpTypeDef w = {0};
  w.WakeUpEvent = UART_WAKEUP_ON_STARTBIT;
  if (HAL_UARTEx_StopModeWakeUpSourceConfig(&huart2, w) != HAL_OK) { Error_Handler(); }
  if (HAL_UARTEx_EnableStopMode(&huart2) != HAL_OK) { Error_Handler(); }

  __HAL_UART_ENABLE_IT(&huart2, UART_IT_WUF);
  HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}

/* 进入 Stop 2，醒来后重跑时钟配置恢复 PLL = 80MHz */
static void enter_stop2_and_restore(void)
{
  HAL_SuspendTick();
  HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);
  /* 醒来后 SYSCLK 退回 MSI，PLL 被关闭。重跑时钟配置。*/
  SystemClock_Config();
  HAL_ResumeTick();
}

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *h)
{
  (void)h;
  g_uptime_s      += 5U;
  g_wakeup_pending = 1;
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LCD_CS_Pin|LCD_DS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : LCD_CS_Pin LCD_DS_Pin */
  GPIO_InitStruct.Pin = LCD_CS_Pin|LCD_DS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LCD_RES_Pin */
  GPIO_InitStruct.Pin = LCD_RES_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(LCD_RES_GPIO_Port, &GPIO_InitStruct);

  /* ESP 电源使能：先确保关电，再配为推挽输出 */
  HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = ESP_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ESP_EN_GPIO_Port, &GPIO_InitStruct);

  /* 剩下所有未使用的 GPIO 都配为 Analog + No-Pull，消除浮空漏电（SWD: PA13/PA14 保留）*/
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Pin  = GPIO_PIN_0 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
                       | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  GPIO_InitStruct.Pin  = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5
                       | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11
                       | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  GPIO_InitStruct.Pin  = GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_2  | GPIO_PIN_3
                       | GPIO_PIN_4  | GPIO_PIN_5  | GPIO_PIN_6  | GPIO_PIN_7
                       | GPIO_PIN_8  | GPIO_PIN_9  | GPIO_PIN_10 | GPIO_PIN_11
                       | GPIO_PIN_12 | GPIO_PIN_13;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/* ===== \u5c0f\u56fe\u6807\u7ed8\u5236\u8f85\u52a9 ===== */
static void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color)
{
  for (uint16_t j = 0; j < h; j++)
    for (uint16_t i = 0; i < w; i++)
      st7305_draw_pixel(&g_lcd, x + i, y + j, color);
}

/* ESP \u6a21\u5757\u56fe\u6807\uff1a\u5929\u7ebf + \u82af\u7247\u8eab\u4f53 + \u5f15\u811a\uff0c\u5360 22\u00d720 */
#define ICON_ESP_W 26
#define ICON_ESP_H 22
static void draw_icon_esp(uint16_t x, uint16_t y)
{
  /* antenna: vertical bar + top horizontal bar */
  fill_rect(x + 12, y,     2, 6, ST7305_COLOR_BLACK);
  fill_rect(x + 9,  y,     8, 2, ST7305_COLOR_BLACK);
  /* chip body outline 22x16, 2px stroke */
  fill_rect(x + 2,  y + 6,  22, 2,  ST7305_COLOR_BLACK);
  fill_rect(x + 2,  y + 20, 22, 2,  ST7305_COLOR_BLACK);
  fill_rect(x + 2,  y + 6,  2,  16, ST7305_COLOR_BLACK);
  fill_rect(x + 22, y + 6,  2,  16, ST7305_COLOR_BLACK);
  /* solid center block as chip marker */
  fill_rect(x + 10, y + 11, 6,  6,  ST7305_COLOR_BLACK);
  /* 4 pins on each side (2px thick, 4px pitch) */
  for (uint8_t i = 0; i < 4; i++)
  {
    fill_rect(x,      y + 8 + i * 4, 2, 2, ST7305_COLOR_BLACK);
    fill_rect(x + 24, y + 8 + i * 4, 2, 2, ST7305_COLOR_BLACK);
  }
}

/* WiFi \u56fe\u6807\uff1a4 \u9053\u7531\u5927\u5230\u5c0f\u7684\u4fe1\u53f7\u6761\uff0c\u5360 26\u00d720
 * failed = 1 \u65f6\uff0c\u53f3\u4fa7\u52a0\u4e00\u4e2a\u7c97 "!" */
static void draw_icon_wifi(uint16_t x, uint16_t y, uint8_t failed)
{
  fill_rect(x,      y,      22, 3, ST7305_COLOR_BLACK);  /* \u6700\u5927\u5f27 */
  fill_rect(x + 3,  y + 5,  16, 3, ST7305_COLOR_BLACK);  /* \u4e2d\u5f27 */
  fill_rect(x + 6,  y + 10, 10, 3, ST7305_COLOR_BLACK);  /* \u5c0f\u5f27 */
  fill_rect(x + 9,  y + 15, 4,  3, ST7305_COLOR_BLACK);  /* \u5706\u70b9 */

  if (failed)
  {
    /* \u53f3\u4fa7\u201c!\u201d\uff1a\u7ad6\u6761 + \u4e0b\u70b9 */
    fill_rect(x + 26, y,      3, 12, ST7305_COLOR_BLACK);
    fill_rect(x + 26, y + 15, 3, 3,  ST7305_COLOR_BLACK);
  }
}

/* 水滴图标 14×16 × s */
static void draw_icon_drop(uint16_t x, uint16_t y, uint8_t s)
{
  fill_rect((uint16_t)(x + 6 * s), (uint16_t)(y),         (uint16_t)( 2 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + 5 * s), (uint16_t)(y +  2 * s), (uint16_t)( 4 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + 4 * s), (uint16_t)(y +  4 * s), (uint16_t)( 6 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + 3 * s), (uint16_t)(y +  6 * s), (uint16_t)( 8 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + 2 * s), (uint16_t)(y +  8 * s), (uint16_t)(10 * s), (uint16_t)( 6 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + 3 * s), (uint16_t)(y + 14 * s), (uint16_t)( 8 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
}

/* 云块起点 (x, y)，占 28×14 × s，多个天气图标共用 */
static void draw_cloud(uint16_t x, uint16_t y, uint8_t s)
{
  fill_rect((uint16_t)(x +  9 * s), (uint16_t)(y +  1 * s), (uint16_t)(10 * s), (uint16_t)(4 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x +  2 * s), (uint16_t)(y +  7 * s), (uint16_t)( 6 * s), (uint16_t)(5 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + 20 * s), (uint16_t)(y +  7 * s), (uint16_t)( 6 * s), (uint16_t)(5 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x +  5 * s), (uint16_t)(y +  4 * s), (uint16_t)(18 * s), (uint16_t)(8 * s), ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x +  1 * s), (uint16_t)(y + 10 * s), (uint16_t)(26 * s), (uint16_t)(3 * s), ST7305_COLOR_BLACK);
}

/* 天气图标 28×22 × s. code: 0=sun 1=partly 2=cloud 3=rain 4=snow */
static void draw_icon_weather(uint16_t x, uint16_t y, uint8_t s, int8_t code)
{
  if (code < 0 || code > 4) return;

  if (code == 0)
  {
    fill_rect((uint16_t)(x +  8 * s), (uint16_t)(y +  5 * s), (uint16_t)(12 * s), (uint16_t)(12 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 13 * s), (uint16_t)(y),          (uint16_t)( 2 * s), (uint16_t)( 4 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 13 * s), (uint16_t)(y + 18 * s), (uint16_t)( 2 * s), (uint16_t)( 4 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x),          (uint16_t)(y + 10 * s), (uint16_t)( 4 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 24 * s), (uint16_t)(y + 10 * s), (uint16_t)( 4 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x +  2 * s), (uint16_t)(y +  1 * s), (uint16_t)( 3 * s), (uint16_t)( 3 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 23 * s), (uint16_t)(y +  1 * s), (uint16_t)( 3 * s), (uint16_t)( 3 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x +  2 * s), (uint16_t)(y + 18 * s), (uint16_t)( 3 * s), (uint16_t)( 3 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 23 * s), (uint16_t)(y + 18 * s), (uint16_t)( 3 * s), (uint16_t)( 3 * s), ST7305_COLOR_BLACK);
    return;
  }

  if (code == 1)
  {
    fill_rect((uint16_t)(x + 19 * s), (uint16_t)(y),          (uint16_t)( 6 * s), (uint16_t)( 6 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 17 * s), (uint16_t)(y +  2 * s), (uint16_t)( 2 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 25 * s), (uint16_t)(y +  2 * s), (uint16_t)( 2 * s), (uint16_t)( 2 * s), ST7305_COLOR_BLACK);
    draw_cloud(x, (uint16_t)(y + 8 * s), s);
    return;
  }

  draw_cloud(x, (uint16_t)(y + 2 * s), s);

  if (code == 3)
  {
    fill_rect((uint16_t)(x +  6 * s), (uint16_t)(y + 17 * s), (uint16_t)(2 * s), (uint16_t)(5 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 13 * s), (uint16_t)(y + 17 * s), (uint16_t)(2 * s), (uint16_t)(5 * s), ST7305_COLOR_BLACK);
    fill_rect((uint16_t)(x + 20 * s), (uint16_t)(y + 17 * s), (uint16_t)(2 * s), (uint16_t)(5 * s), ST7305_COLOR_BLACK);
  }
  else if (code == 4)
  {
    for (uint8_t i = 0; i < 3; i++)
    {
      uint16_t cx = (uint16_t)(x + (6 + i * 7) * s);
      uint16_t cy = (uint16_t)(y + 18 * s);
      fill_rect(cx,                       (uint16_t)(cy + 1 * s), (uint16_t)(3 * s), (uint16_t)(1 * s), ST7305_COLOR_BLACK);
      fill_rect((uint16_t)(cx + 1 * s),   cy,                     (uint16_t)(1 * s), (uint16_t)(3 * s), ST7305_COLOR_BLACK);
    }
  }
}

/* 经典 WiFi 图标 22×17：底部倒三角 + 3 道完整同心半圆弧。未联网时整块不画。 */
#define ICON_WIFI_W 22
#define ICON_WIFI_H 17
static void draw_icon_signal(uint16_t x, uint16_t y, uint8_t has_signal)
{
  if (!has_signal) return;

  const int cx = 11;   /* 弧心 x（图标本地坐标） */
  const int cy = 13;   /* 弧心 y：把下面留 3 行给倒三角 */

  /* 3 道圆弧的外/内半径平方，厚度 2px，弧间留 1px 间隙 */
  const int r_out2[3] = { 11*11, 8*8, 5*5 };
  const int r_in2 [3] = { 10*10, 7*7, 4*4 };

  /* 半圆裁掉两侧，仅保留中间约 100° 的弧段（贴近图片观感）：
     dy >= |dx| * tan(40°) ≈ |dx| * 0.839 → 整数判定 6*dy >= 5*|dx| */
  for (int py = 0; py <= cy; py++)
  {
    int dy  = cy - py;
    int dy2 = dy * dy;
    for (int px = 0; px < ICON_WIFI_W; px++)
    {
      int dx  = px - cx;
      int adx = dx < 0 ? -dx : dx;
      if (6 * dy < 5 * adx) continue;
      int d2 = dx * dx + dy2;
      for (int i = 0; i < 3; i++)
      {
        if (d2 <= r_out2[i] && d2 >= r_in2[i])
        {
          st7305_draw_pixel(&g_lcd, (uint16_t)(x + px), (uint16_t)(y + py), ST7305_COLOR_BLACK);
          break;
        }
      }
    }
  }

  /* 底部倒三角（指向下方），与最内弧之间留 1 行空隙 */
  fill_rect((uint16_t)(x + cx - 2), (uint16_t)(y + 14), 5, 1, ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + cx - 1), (uint16_t)(y + 15), 3, 1, ST7305_COLOR_BLACK);
  fill_rect((uint16_t)(x + cx),     (uint16_t)(y + 16), 1, 1, ST7305_COLOR_BLACK);
}


/* 电池图标 27×14：实心外壳描边 + 右侧正极 + 内部 5 格电量条 */
#define ICON_BAT_W 27
#define ICON_BAT_H 14
static void draw_icon_battery(uint16_t x, uint16_t y, uint8_t bars)
{
  /* 主体 24×14，2px 粗描边 */
  fill_rect(x,      y,       24, 2,  ST7305_COLOR_BLACK);
  fill_rect(x,      y + 12,  24, 2,  ST7305_COLOR_BLACK);
  fill_rect(x,      y,       2,  14, ST7305_COLOR_BLACK);
  fill_rect(x + 22, y,       2,  14, ST7305_COLOR_BLACK);
  /* 右侧正极 3×6 */
  fill_rect(x + 24, y + 4,   3,  6,  ST7305_COLOR_BLACK);
  /* 5 格电量条：每格 3×8，间距 1px（左起 x+3） */
  if (bars > 5) bars = 5;
  for (uint8_t i = 0; i < bars; i++)
  {
    fill_rect((uint16_t)(x + 3 + i * 4), (uint16_t)(y + 3), 3, 8, ST7305_COLOR_BLACK);
  }
}

/* 月亮图标 12×14：实心圆盘减去偏移圆盘形成弯月 */
#define ICON_MOON_W 12
#define ICON_MOON_H 14
static void draw_icon_moon(uint16_t x, uint16_t y)
{
  for (int py = 0; py < ICON_MOON_H; py++)
  {
    for (int px = 0; px < ICON_MOON_W; px++)
    {
      int dx1 = px - 5, dy1 = py - 7;
      int dx2 = px - 9, dy2 = py - 6;
      if (dx1 * dx1 + dy1 * dy1 <= 49 && dx2 * dx2 + dy2 * dy2 > 36)
      {
        st7305_draw_pixel(&g_lcd, (uint16_t)(x + px), (uint16_t)(y + py), ST7305_COLOR_BLACK);
      }
    }
  }
}

/* ===== 农历转换（1900-2049）=====
 * 每年一个 uint32:
 *   bits 0-3  : 闰月月份（0 = 无闰）
 *   bits 4-15 : 12 个常月日数，bit15=正月，1=30天，0=29天
 *   bit 16    : 闰月日数，1=30天，0=29天
 */
static const uint32_t LUNAR_INFO[] = {
  0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2, /*1900-1909*/
  0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977, /*1910-1919*/
  0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970, /*1920-1929*/
  0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950, /*1930-1939*/
  0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557, /*1940-1949*/
  0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0, /*1950-1959*/
  0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0, /*1960-1969*/
  0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6, /*1970-1979*/
  0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570, /*1980-1989*/
  0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x055c0,0x0ab60,0x096d5,0x092e0, /*1990-1999*/
  0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5, /*2000-2009*/
  0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930, /*2010-2019*/
  0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530, /*2020-2029*/
  0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45, /*2030-2039*/
  0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0, /*2040-2049*/
};
#define LUNAR_BASE_YEAR 1900
#define LUNAR_LAST_YEAR (LUNAR_BASE_YEAR + (int)(sizeof(LUNAR_INFO)/sizeof(LUNAR_INFO[0])) - 1)

static int lunar_leap_month(int y) { return (int)(LUNAR_INFO[y - LUNAR_BASE_YEAR] & 0xF); }
static int lunar_leap_days (int y) { if (!lunar_leap_month(y)) return 0;
                                     return (LUNAR_INFO[y - LUNAR_BASE_YEAR] & 0x10000) ? 30 : 29; }
static int lunar_month_days(int y, int m) {
  return (LUNAR_INFO[y - LUNAR_BASE_YEAR] & ((uint32_t)0x10000 >> m)) ? 30 : 29;
}
static int lunar_year_days(int y) {
  int sum = 348;  /* 12 * 29 */
  uint32_t info = LUNAR_INFO[y - LUNAR_BASE_YEAR];
  for (uint32_t i = 0x8000; i > 0x8; i >>= 1) if (info & i) sum++;
  return sum + lunar_leap_days(y);
}

/* 公历 ymd -> Julian Day Number（Gregorian） */
static int ymd_to_jdn(int y, int m, int d)
{
  int a  = (14 - m) / 12;
  int yy = y + 4800 - a;
  int mm = m + 12 * a - 3;
  return d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
}

/* 公历 -> 农历，out_leap=1 表示当前月是闰月。范围外返回 0 */
static int gregorian_to_lunar(int gy, int gm, int gd,
                              int *out_y, int *out_m, int *out_d, int *out_leap)
{
  int offset = ymd_to_jdn(gy, gm, gd) - ymd_to_jdn(LUNAR_BASE_YEAR, 1, 31);
  if (offset < 0) return 0;
  int year = LUNAR_BASE_YEAR, ydays;
  while (year <= LUNAR_LAST_YEAR && offset >= (ydays = lunar_year_days(year)))
  {
    offset -= ydays;
    year++;
  }
  if (year > LUNAR_LAST_YEAR) return 0;
  int leap = lunar_leap_month(year);
  int m = 1, is_leap = 0;
  while (m <= 12)
  {
    int md = lunar_month_days(year, m);
    if (offset < md) { is_leap = 0; break; }
    offset -= md;
    if (m == leap)
    {
      int lmd = lunar_leap_days(year);
      if (offset < lmd) { is_leap = 1; break; }
      offset -= lmd;
    }
    m++;
  }
  *out_y = year; *out_m = m; *out_d = offset + 1; *out_leap = is_leap;
  return 1;
}


static void render_screen(const char *date, uint8_t hh, uint8_t mm, uint8_t ss,
                          int16_t temp_x10, uint16_t rh_x10,
                          uint8_t esp_online, uint8_t wifi_up,
                          int8_t w_code, int8_t w_temp_c,
                          uint8_t bat_bars)
{
  char time_str[16];
  char temp_str[16];
  char hum_str[16];
  char wtemp_str[8];
  char dew_str[16];
  char dt_str[24];

  int16_t temp_abs = (temp_x10 < 0) ? (int16_t)(-temp_x10) : temp_x10;

  /* 露点（×10°C）：Magnus 近似 */
  int16_t dew_x10 = 0;
  uint8_t dew_valid = 0;
  if (rh_x10 > 0)
  {
    float T  = (float)temp_x10 * 0.1f;
    float RH = (float)rh_x10  * 0.1f;
    if (RH < 1.0f)   RH = 1.0f;
    if (RH > 100.0f) RH = 100.0f;
    const float a = 17.27f, b = 237.7f;
    float gam = (a * T) / (b + T) + logf(RH / 100.0f);
    float td  = (b * gam) / (a - gam);
    if (td >  99.9f) td =  99.9f;
    if (td < -99.9f) td = -99.9f;
    dew_x10 = (int16_t)(td * 10.0f);
    dew_valid = 1;
  }
  int16_t dew_abs = (dew_x10 < 0) ? (int16_t)(-dew_x10) : dew_x10;
  /* 露点 < 8.0°C 认为偏干，需要加湿 */
  uint8_t dry = (uint8_t)(dew_valid && (dew_x10 < 80));

  (void)ss;
  snprintf(time_str, sizeof(time_str), "%02d:%02d", hh, mm);
  snprintf(temp_str, sizeof(temp_str), "T:%s%d.%dC",
           temp_x10 < 0 ? "-" : "", temp_abs / 10, temp_abs % 10);
  snprintf(hum_str,  sizeof(hum_str),  "H:%d.%d%%", rh_x10 / 10, rh_x10 % 10);
  snprintf(wtemp_str, sizeof(wtemp_str), "%dC", (int)w_temp_c);
  snprintf(dew_str,  sizeof(dew_str),  "%s%d.%dC",
           dew_x10 < 0 ? "-" : "", dew_abs / 10, dew_abs % 10);
  /* 底部去掉年份，避免与放大后的其他字号失调（保持整体字号梯度均衡） */
  snprintf(dt_str, sizeof(dt_str), "%s %s", date + 5, time_str);

  st7305_fill(&g_lcd, ST7305_COLOR_WHITE);

  /* === 顶部左：电池图标（始终显示，与右侧图标共享中线 cy=16） === */
  draw_icon_battery(4, (uint16_t)(16 - ICON_BAT_H / 2), bat_bars);

  /* === 顶部右：WiFi + ESP 图标，两者共享同一垂直中心 (y=16) === */
  if (esp_online)
  {
    /* 右对齐到屏宽 300，留 4px 边距；图标间留 4px 间隙 */
    const uint16_t esp_x  = (uint16_t)(300 - 4 - ICON_ESP_W);                   /* 270 */
    const uint16_t wifi_x = (uint16_t)(esp_x - 4 - ICON_WIFI_W);                /* 244 */
    const uint16_t cy_top = 16;                                                 /* 顶部行共同中线 */
    draw_icon_signal(wifi_x, (uint16_t)(cy_top - ICON_WIFI_H / 2), wifi_up);
    draw_icon_esp(esp_x,     (uint16_t)(cy_top - ICON_ESP_H  / 2));
  }

  /* === 露点行：3x 水滴(42×48) + size=5 大字(高=35)，共享中线 y = 62+24 = 86 === */
  if (dew_valid)
  {
    const uint16_t drop_y    = 62;
    const uint16_t drop_h    = 48;                       /* 16 * 3 */
    const uint16_t cy_dew    = (uint16_t)(drop_y + drop_h / 2);  /* 86 */
    const uint16_t dew_txt_h = (uint16_t)(7 * 5);        /* 35 */
    const uint16_t dew_txt_y = (uint16_t)(cy_dew - dew_txt_h / 2); /* 69 */
    draw_icon_drop(10, drop_y, 3);
    st7305_draw_string(&g_lcd, 76, dew_txt_y, dew_str, ST7305_COLOR_BLACK, 5);
    if (dry)
    {
      uint16_t dew_w = (uint16_t)(strlen(dew_str) * 6 * 5);
      uint16_t bx = (uint16_t)(76 + dew_w + 14);
      /* "!" 高度 = 28 + 6 间隙 + 5 点 = 39，居中到 cy_dew */
      uint16_t by = (uint16_t)(cy_dew - 19);
      fill_rect(bx, by,           5, 28, ST7305_COLOR_BLACK);
      fill_rect(bx, (uint16_t)(by + 34), 5, 5,  ST7305_COLOR_BLACK);
    }
  }

  /* === 天气行：2x 图标(56×44) + size=4 温度(高=28)，共享中线 y = 140+22 = 162 === */
  if (w_code >= 0)
  {
    const uint16_t wx        = 10;
    const uint16_t wy        = 140;
    const uint16_t wh        = 44;                       /* 22 * 2 */
    const uint16_t cy_wx     = (uint16_t)(wy + wh / 2);  /* 162 */
    const uint16_t wtxt_h    = (uint16_t)(7 * 4);        /* 28 */
    const uint16_t wtxt_y    = (uint16_t)(cy_wx - wtxt_h / 2);   /* 148 */
    draw_icon_weather(wx, wy, 2, w_code);
    st7305_draw_string(&g_lcd, 90, wtxt_y, wtemp_str, ST7305_COLOR_BLACK, 4);
  }

  /* === 室内温湿度：size=4，与天气温度同字号 === */
  st7305_draw_string(&g_lcd, 10, 230, temp_str, ST7305_COLOR_BLACK, 4);
  st7305_draw_string(&g_lcd, 10, 280, hum_str,  ST7305_COLOR_BLACK, 4);

  /* 底部左：MM-DD HH:MM（去年份），size=2，与右侧农历同字号 */
  st7305_draw_string(&g_lcd, 10, 373, dt_str, ST7305_COLOR_BLACK, 2);

  /* 底部右：月亮图标 + 农历 MM-DD（size=2），与公历共享中线 y=380 */
  {
    int gy = 0, gmo = 0, gda = 0;
    if (sscanf(date, "%d-%d-%d", &gy, &gmo, &gda) == 3)
    {
      int ly, lm, ld, leap;
      if (gregorian_to_lunar(gy, gmo, gda, &ly, &lm, &ld, &leap))
      {
        (void)ly;
        char lstr[8];
        snprintf(lstr, sizeof(lstr), "%s%d-%d", leap ? "*" : "", lm, ld);
        const uint16_t row_cy   = 380;                       /* 与公历日期同中线 */
        const uint16_t lun_scale = 2;
        const uint16_t lun_w    = (uint16_t)(strlen(lstr) * 6 * lun_scale);
        const uint16_t lun_h    = (uint16_t)(7 * lun_scale);  /* 14 */
        const uint16_t gap      = 6;
        const uint16_t lun_x    = (uint16_t)(300 - 4 - lun_w);
        const uint16_t lun_y    = (uint16_t)(row_cy - lun_h / 2);
        const uint16_t moon_x   = (uint16_t)(lun_x - gap - ICON_MOON_W);
        const uint16_t moon_y   = (uint16_t)(row_cy - ICON_MOON_H / 2);
        draw_icon_moon(moon_x, moon_y);
        st7305_draw_string(&g_lcd, lun_x, lun_y, lstr, ST7305_COLOR_BLACK, lun_scale);
      }
    }
  }

  st7305_refresh(&g_lcd);
}

/**
  * @brief UART RX 完成回调：解析多种帧
  *   - "T:YYYY-MM-DD HH:MM:SS\n" (21 字符) -> 同步时间
  *   - "S:W\n" / "S:N\n" (3 字符)          -> Wi-Fi 状态上报
  * 任意一次成功收到完整帧都会刷新 ESP 心跳时间。
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART2)
  {
    return;
  }

  uint8_t b = g_uart_rx_byte;

  if (b == '\r')
  {
    /* ignore */
  }
  else if (b == '\n')
  {
    g_line_buf[g_line_len] = '\0';

    uint8_t frame_ok = 0;

    /* T:YYYY-MM-DD HH:MM:SS  (21 \u5b57\u7b26) */
    if (g_line_len == 21
        && g_line_buf[0]  == 'T' && g_line_buf[1]  == ':'
        && g_line_buf[6]  == '-' && g_line_buf[9]  == '-'
        && g_line_buf[12] == ' '
        && g_line_buf[15] == ':' && g_line_buf[18] == ':')
    {
      uint8_t hh = (uint8_t)((g_line_buf[13] - '0') * 10 + (g_line_buf[14] - '0'));
      uint8_t mm = (uint8_t)((g_line_buf[16] - '0') * 10 + (g_line_buf[17] - '0'));
      uint8_t ss = (uint8_t)((g_line_buf[19] - '0') * 10 + (g_line_buf[20] - '0'));
      if (hh < 24 && mm < 60 && ss < 60)
      {
        for (uint8_t i = 0; i < 10; i++)
        {
          g_date[i] = g_line_buf[2 + i];
        }
        g_date[10] = '\0';
        g_hh = hh;
        g_mm = mm;
        g_ss = ss;
        g_time_synced = 1;
        rtc_write_datetime(g_date, hh, mm, ss);
        frame_ok = 1;
      }
    }
    /* D\n 会话结束，主控可切电 */
    else if (g_line_len == 1 && g_line_buf[0] == 'D')
    {
      g_esp_session_done = 1;
      frame_ok = 1;
    }
    /* S:W \u6216 S:N (3 \u5b57\u7b26) */
    else if (g_line_len == 3 && g_line_buf[0] == 'S' && g_line_buf[1] == ':')
    {
      if (g_line_buf[2] == 'W')
      {
        g_wifi_up = 1;
        frame_ok = 1;
      }
      else if (g_line_buf[2] == 'N')
      {
        g_wifi_up = 0;
        frame_ok = 1;
      }
    }
    /* W:<code>,<temp>  变长，code 为 1 位数字，temp 可带负号 */
    else if (g_line_len >= 5 && g_line_buf[0] == 'W' && g_line_buf[1] == ':'
             && g_line_buf[2] >= '0' && g_line_buf[2] <= '9'
             && g_line_buf[3] == ',')
    {
      int8_t wcode = (int8_t)(g_line_buf[2] - '0');
      int    sign  = 1;
      uint8_t i    = 4;
      if (g_line_buf[i] == '-') { sign = -1; i++; }
      else if (g_line_buf[i] == '+') { i++; }
      int    val   = 0;
      uint8_t got  = 0;
      while (i < g_line_len && g_line_buf[i] >= '0' && g_line_buf[i] <= '9')
      {
        val = val * 10 + (g_line_buf[i] - '0');
        got = 1;
        i++;
      }
      if (got && wcode <= 4)
      {
        int v = sign * val;
        if (v < -99) v = -99;
        if (v >  99) v =  99;
        g_w_code         = wcode;
        g_w_temp_c       = (int8_t)v;
        g_w_last_seen_ms = g_uptime_s;
        frame_ok = 1;
      }
    }
    if (frame_ok)
    {
      g_esp_last_seen_ms = g_uptime_s;
    }
    g_line_len = 0;
  }
  else if (g_line_len < sizeof(g_line_buf) - 1)
  {
    g_line_buf[g_line_len++] = (char)b;
  }
  else
  {
    /* \u884c\u8fc7\u957f\uff0c\u4e22\u5f03 */
    g_line_len = 0;
  }

  HAL_UART_Receive_IT(huart, &g_uart_rx_byte, 1);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
