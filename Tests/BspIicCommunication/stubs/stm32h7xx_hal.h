#ifndef TEST_STM32H7XX_HAL_H
#define TEST_STM32H7XX_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct {
    uint32_t Timing,OwnAddress1,AddressingMode,DualAddressMode,OwnAddress2;
    uint32_t OwnAddress2Masks,GeneralCallMode,NoStretchMode;
} I2C_InitTypeDef;
typedef struct { void *Instance; I2C_InitTypeDef Init; uint32_t State; uint32_t ErrorCode; } I2C_HandleTypeDef;
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
uint32_t Mock_I2C2Source(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
#define __HAL_RCC_GET_I2C2_SOURCE Mock_I2C2Source
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *handle,uint32_t value);
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *handle,uint32_t value);
void HAL_GPIO_Init(void *port,GPIO_InitTypeDef *gpio);
void HAL_GPIO_DeInit(void *port,uint32_t pins);
void HAL_I2C_MspInit(I2C_HandleTypeDef *handle);
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle);

typedef enum {
 HAL_I2C_STATE_RESET=0, HAL_I2C_STATE_READY=0x20,
 HAL_I2C_STATE_BUSY_TX=0x21, HAL_I2C_STATE_ABORT=0x60
} HAL_I2C_StateTypeDef;
#define HAL_I2C_ERROR_NONE 0U
#define HAL_I2C_ERROR_AF 4U
#define HAL_I2C_ERROR_TIMEOUT 0x20U
typedef enum { I2C2_EV_IRQn=33, I2C2_ER_IRQn=34 } IRQn_Type;
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t priority,uint32_t sub);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq);
void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef *h);
void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef *h);
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *h);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h,uint16_t address,uint8_t *data,uint16_t len,uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *h,uint16_t address,uint8_t *data,uint16_t len);
HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *h,uint16_t address);
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *h);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *h);
uint32_t HAL_GetTick(void);
/* Route CMSIS intrinsics to the test scheduler, not host/CPU PRIMASK. */
uint32_t Mock_GetPrimask(void);
void Mock_DisableIrq(void);
void Mock_SetPrimask(uint32_t value);
#define __get_PRIMASK Mock_GetPrimask
#define __disable_irq Mock_DisableIrq
#define __set_PRIMASK Mock_SetPrimask

#endif
