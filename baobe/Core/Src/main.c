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

/* USART2 单字节接收 + 行缓冲（"T:YYYY-MM-DD HH:MM:SS" = 22 字符）*/
static uint8_t  g_uart_rx_byte;
static char     g_line_buf[32];
static uint8_t  g_line_len = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void render_screen(const char *date, uint8_t hh, uint8_t mm, uint8_t ss,
                          int16_t temp_x10, uint16_t rh_x10);
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

    /* 1Hz：推进本地秒表 + 刷屏 */
    if ((now - last_sec_tick) >= 1000U)
    {
      last_sec_tick += 1000U;
      tick_one_second();
      render_screen(g_date, g_hh, g_mm, g_ss, last_temp_x10, last_rh_x10);
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

/* 本地秒表 +1，加入必要的进位（日期仅在下一次同步帧重新赋值）*/
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
  /* 跨天后日期可能差 1 天，下次 NTP 帧会纠正 */
}

static void render_screen(const char *date, uint8_t hh, uint8_t mm, uint8_t ss,
                          int16_t temp_x10, uint16_t rh_x10)
{
  char time_str[16];
  char temp_str[16];
  char hum_str[16];

  int16_t temp_abs = (temp_x10 < 0) ? (int16_t)(-temp_x10) : temp_x10;

  snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hh, mm, ss);
  snprintf(temp_str, sizeof(temp_str), "T:%s%d.%dC",
           temp_x10 < 0 ? "-" : "", temp_abs / 10, temp_abs % 10);
  snprintf(hum_str,  sizeof(hum_str),  "H:%d.%d%%", rh_x10 / 10, rh_x10 % 10);

  st7305_fill(&g_lcd, ST7305_COLOR_WHITE);
  /* 日期：scale=3 → 10*18=180px宽、21px高 */
  st7305_draw_string(&g_lcd, 10,  20, date,     ST7305_COLOR_BLACK, 3);
  /* 时间：scale=5 → 8*30=240px宽、35px高 */
  st7305_draw_string(&g_lcd, 10,  70, time_str, ST7305_COLOR_BLACK, 5);
  /* 温湿度：scale=3 */
  st7305_draw_string(&g_lcd, 10, 180, temp_str, ST7305_COLOR_BLACK, 3);
  st7305_draw_string(&g_lcd, 10, 230, hum_str,  ST7305_COLOR_BLACK, 3);
  st7305_refresh(&g_lcd);
}

/**
  * @brief UART RX 完成回调：累积到 '\n' 后解析 "T:YYYY-MM-DD HH:MM:SS"
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

    /* 期望: T:YYYY-MM-DD HH:MM:SS  (22 字符) */
    if (g_line_len == 22
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
        /* 日期部分直接拷贝 10 个字符 "YYYY-MM-DD" */
        for (uint8_t i = 0; i < 10; i++)
        {
          g_date[i] = g_line_buf[2 + i];
        }
        g_date[10] = '\0';
        g_hh = hh;
        g_mm = mm;
        g_ss = ss;
        g_time_synced = 1;
      }
    }
    g_line_len = 0;
  }
  else if (g_line_len < sizeof(g_line_buf) - 1)
  {
    g_line_buf[g_line_len++] = (char)b;
  }
  else
  {
    /* 行过长，丢弃 */
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
