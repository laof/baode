/**
  ****************************(C) COPYRIGHT 2021 Boring_TECH*********************
  * @file       Drv_spi.c/h
  * @brief      将HAL库SPI函数进行二次封装，给相应的器件进行调用
  * @note       
  * @history
  *  Version    Date            Author          Modification
  *  V3.0.0     2020.7.14     	              	1. done
  *
  @verbatim
  ==============================================================================

  ==============================================================================
  @endverbatim
  ****************************(C) COPYRIGHT 2021 Boring_TECH*********************
  */
#include "BSP_spi.h"
#include "spi.h"

/**
  * @brief          封装SPI2读写函数（片外SPI Flash使用）
  * @param[in]     	TxData 发出的数据
  * @retval         RxData 收到的数据
  */
uint8_t SPI2_ReadWriteByte(uint8_t TxData)
{
    uint8_t Rxdata;
    HAL_SPI_TransmitReceive(&hspi2,&TxData,&Rxdata,1, 1000);       
 	return Rxdata;          		    //返回收到的数据		
}






