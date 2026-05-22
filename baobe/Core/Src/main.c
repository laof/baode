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
static volatile uint32_t g_esp_last_seen_ms = 0;   /* 最后一次收到任意帧的 tick */
static volatile uint8_t  g_wifi_up = 0;            /* 0=down/unknown, 1=up */

/* 天气：0=晴 1=多云 2=阴/雾 3=雨 4=雪，-1=未知 */
static volatile int8_t   g_w_code   = -1;
static volatile int8_t   g_w_temp_c = 0;
static volatile uint32_t g_w_last_seen_ms = 0;
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
                          int8_t w_code, int8_t w_temp_c);
static void tick_one_second(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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

  /* SHT30 I2C 7-bit 地址：ADDR 脚接 GND => 0x44；接 VDD => 0x45 */
  sht30_init(&g_sht30, &hi2c1, 0x44);

  /* 启动 USART2 接收（单字节中断，循环重启）*/
  HAL_UART_Receive_IT(&huart2, &g_uart_rx_byte, 1);

  uint32_t last_sec_tick    = HAL_GetTick();
  uint32_t last_sensor_tick = 0;
  uint32_t last_ping_tick   = 0;
  int16_t  last_temp_x10 = 0;
  uint16_t last_rh_x10   = 0;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    /* 每 5s 向副控发一次 ping "P\n"，让副控回 S:W/S:N 报 Wi-Fi 状态 */
    if ((now - last_ping_tick) >= 5000U)
    {
      last_ping_tick = now;
      static const uint8_t ping[2] = { 'P', '\n' };
      HAL_UART_Transmit(&huart2, (uint8_t *)ping, sizeof(ping), 10);
    }

    /* 1Hz：推进本地秒表 + 刷屏 */
    if ((now - last_sec_tick) >= 1000U)
    {
      last_sec_tick += 1000U;
      tick_one_second();
      uint8_t esp_online = ((now - g_esp_last_seen_ms) < 12000U) && (g_esp_last_seen_ms != 0);
      int8_t  w_code   = (g_w_last_seen_ms != 0) ? g_w_code   : (int8_t)-1;
      int8_t  w_temp_c = (g_w_last_seen_ms != 0) ? g_w_temp_c : (int8_t)0;
      render_screen(g_date, g_hh, g_mm, g_ss, last_temp_x10, last_rh_x10,
                    esp_online, g_wifi_up, w_code, w_temp_c);
    }

    /* SHT30 每 5s 读一次即可（传感器本身变化不快）*/
    if ((now - last_sensor_tick) >= 5000U)
    {
      last_sensor_tick = now;
      SHT30_Readout r;
      if (sht30_read(&g_sht30, &r) == HAL_OK)
      {
        last_temp_x10 = r.temp_x10;
        last_rh_x10   = r.rh_x10;
      }
    }
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

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
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
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
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
  hi2c1.Init.Timing = 0x10909CEC;          /* 100kHz @ PCLK1=80MHz */
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
static void draw_icon_esp(uint16_t x, uint16_t y)
{
  /* \u5929\u7ebf */
  fill_rect(x + 10, y,     2, 4, ST7305_COLOR_BLACK);
  /* \u82af\u7247\u4e3b\u4f53\u63cf\u8fb9 16\u00d712 */
  fill_rect(x + 3,  y + 4, 16, 2, ST7305_COLOR_BLACK);
  fill_rect(x + 3,  y + 14, 16, 2, ST7305_COLOR_BLACK);
  fill_rect(x + 3,  y + 4, 2,  12, ST7305_COLOR_BLACK);
  fill_rect(x + 17, y + 4, 2,  12, ST7305_COLOR_BLACK);
  /* \u4e2d\u95f4\u5c0f\u65b9\u5757\u4ee3\u8868\u82af\u7247\u6807\u8bb0 */
  fill_rect(x + 9,  y + 8, 4,  4,  ST7305_COLOR_BLACK);
  /* \u5de6\u53f3\u5404 3 \u6761\u5f15\u811a */
  for (uint8_t i = 0; i < 3; i++)
  {
    fill_rect(x,      y + 6 + i * 3, 3, 1, ST7305_COLOR_BLACK);
    fill_rect(x + 19, y + 6 + i * 3, 3, 1, ST7305_COLOR_BLACK);
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

/* 经典 WiFi 图标 28×20：底部圆点 + 3 道递增同心弧。未联网时整块不画。 */
static void draw_icon_signal(uint16_t x, uint16_t y, uint8_t has_signal)
{
  if (!has_signal) return;

  const int cx = 13;   /* 弧心 x（图标本地坐标） */
  const int cy = 19;   /* 弧心 y（底部圆点中心） */

  /* 3 道圆弧的外/内半径平方，厚度 2px */
  const int r_out2[3] = { 14*14, 10*10, 6*6 };
  const int r_in2 [3] = { 12*12,  8*8, 4*4 };

  for (int py = 0; py <= cy; py++)
  {
    int dy = cy - py;            /* 向上为正 */
    int dy2 = dy * dy;
    for (int px = 0; px < 28; px++)
    {
      int dx = px - cx;
      int d2 = dx * dx + dy2;
      /* 限制弧线张开角度约 140°：dy >= |dx| * tan(20°) ≈ |dx|*0.36 */
      /* 用整数：100*dy >= 36*|dx|，即 25*dy >= 9*|dx| */
      int adx = dx < 0 ? -dx : dx;
      if (25 * dy < 9 * adx) continue;

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

  /* 底部实心圆点 */
  fill_rect((uint16_t)(x + cx - 1), (uint16_t)(y + cy - 1), 3, 3, ST7305_COLOR_BLACK);
}

/* \u672c\u5730\u79d2\u8868 +1\uff0c\u52a0\u5165\u5fc5\u8981\u7684\u8fdb\u4f4d\uff08\u65e5\u671f\u4ec5\u5728\u4e0b\u4e00\u6b21\u540c\u6b65\u5e27\u91cd\u65b0\u8d4b\u503c\uff09*/
static void tick_one_second(void)
{
  if (g_ss < 59)
  {
    g_ss++;
    return;
  }
  g_ss = 0;
  if (g_mm < 59)
  {
    g_mm++;
    return;
  }
  g_mm = 0;
  g_hh = (uint8_t)((g_hh + 1) % 24);
}

static void render_screen(const char *date, uint8_t hh, uint8_t mm, uint8_t ss,
                          int16_t temp_x10, uint16_t rh_x10,
                          uint8_t esp_online, uint8_t wifi_up,
                          int8_t w_code, int8_t w_temp_c)
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

  /* === 顶部：只放 ESP + 信号塔 === */
  if (esp_online)
  {
    draw_icon_esp(236, 4);
    draw_icon_signal(266, 4, wifi_up);
  }

  /* === 露点行（最看重，放第二位）：3x 大水滴 + size=5 大字，偏干时 "!" === */
  if (dew_valid)
  {
    draw_icon_drop(10, 32, 3);    /* 3x → 42×48 */
    st7305_draw_string(&g_lcd, 66, 36, dew_str, ST7305_COLOR_BLACK, 5);
    if (dry)
    {
      uint16_t dew_w = (uint16_t)(strlen(dew_str) * 6 * 5);
      uint16_t bx = (uint16_t)(66 + dew_w + 10);
      fill_rect(bx, 36,      5, 28, ST7305_COLOR_BLACK);
      fill_rect(bx, 36 + 34, 5, 5,  ST7305_COLOR_BLACK);
    }
  }

  /* === 天气行（2x 图标 + size=4 温度，与其他行同字号 === */
  if (w_code >= 0)
  {
    draw_icon_weather(10, 110, 2, w_code);    /* 2x → 56×44 */
    st7305_draw_string(&g_lcd, 80, 116, wtemp_str, ST7305_COLOR_BLACK, 4);
  }

  /* === 室内温湿度：size=4，与天气温度同字号 === */
  st7305_draw_string(&g_lcd, 10, 200, temp_str, ST7305_COLOR_BLACK, 4);
  st7305_draw_string(&g_lcd, 10, 250, hum_str,  ST7305_COLOR_BLACK, 4);

  /* 底部：MM-DD HH:MM:SS（去年份），size=3 比原来更接近上方字号 */
  st7305_draw_string(&g_lcd, 10, 370, dt_str, ST7305_COLOR_BLACK, 3);

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
        frame_ok = 1;
      }
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
        g_w_last_seen_ms = HAL_GetTick();
        frame_ok = 1;
      }
    }
    if (frame_ok)
    {
      g_esp_last_seen_ms = HAL_GetTick();
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
