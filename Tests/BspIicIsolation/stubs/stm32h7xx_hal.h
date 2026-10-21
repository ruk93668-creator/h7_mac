#ifndef TEST_STM32H7XX_HAL_H
#define TEST_STM32H7XX_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct {
    uint32_t Timing,OwnAddress1,AddressingMode,DualAddressMode,OwnAddress2;
    uint32_t OwnAddress2Masks,GeneralCallMode,NoStretchMode;
} I2C_InitTypeDef;
typedef struct { void *Instance; I2C_InitTypeDef Init; uint32_t State; } I2C_HandleTypeDef;
typedef struct { uint64_t PeriphClockSelection; uint32_t I2c123ClockSelection; } RCC_PeriphCLKInitTypeDef;
typedef struct { uint32_t Pin,Mode,Pull,Speed,Alternate; } GPIO_InitTypeDef;
#define I2C2 ((void *)0x40005800U)
#define GPIOB ((void *)0x58020400U)
#define RCC_PERIPHCLK_I2C2 UINT64_C(0x200)
#define RCC_I2C123CLKSOURCE_D2PCLK1 0U
#define I2C_ADDRESSINGMODE_7BIT 1U
#define I2C_DUALADDRESS_DISABLE 0U
#define I2C_OA2_NOMASK 0U
#define I2C_GENERALCALL_DISABLE 0U
#define I2C_NOSTRETCH_DISABLE 0U
#define I2C_ANALOGFILTER_ENABLE 0U
#define GPIO_PIN_10 (1U<<10)
#define GPIO_PIN_11 (1U<<11)
#define GPIO_MODE_AF_OD 0x12U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_AF4_I2C2 4U
void Test_GPIOClock(void);
void Test_I2CClock(unsigned enabled);
#define __HAL_RCC_GPIOB_CLK_ENABLE() Test_GPIOClock()
#define __HAL_RCC_I2C2_CLK_ENABLE() Test_I2CClock(1U)
#define __HAL_RCC_I2C2_CLK_DISABLE() Test_I2CClock(0U)
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *clock);
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t peripheral);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *handle,uint32_t value);
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *handle,uint32_t value);
void HAL_GPIO_Init(void *port,GPIO_InitTypeDef *gpio);
void HAL_GPIO_DeInit(void *port,uint32_t pins);
void HAL_I2C_MspInit(I2C_HandleTypeDef *handle);
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle);
#endif
