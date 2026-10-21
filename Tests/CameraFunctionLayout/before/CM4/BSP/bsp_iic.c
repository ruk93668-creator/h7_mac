#include "bsp_iic.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

#define BSP_IIC_KERNEL_HZ  120000000UL
#define BSP_IIC_TIMING     0x307075B1UL

#define BSP_IIC_SYNC_TIMEOUT_MS  100U
#define BSP_IIC_ASYNC_TIMEOUT_MS 100U

#define BSP_IIC_TX_BUFFER_SIZE  129U

static uint8_t s_tx_buffer[BSP_IIC_TX_BUFFER_SIZE];

static volatile SysResult_t s_tx_result;//

static uint32_t s_tx_start_tick;//超时检测

static uint8_t s_tx_addr7;
typedef enum
{
    BSP_IIC_IDLE = 0,
    BSP_IIC_TRANSFERRING,
    BSP_IIC_COMPLETED,
	BSP_IIC_ABORTING,
	BSP_IIC_FAULT

} BspIicTransferState_t;

static volatile BspIicTransferState_t s_tx_state =
    BSP_IIC_IDLE;

/* Keep the HAL handle private: later transfer APIs belong in this module. */
static I2C_HandleTypeDef hi2c2;
static bool s_initialized;
static bool s_initializing;

static SysResult_t BSP_IIC_MapStatus(HAL_StatusTypeDef status)
{
    switch (status)
    {
        case HAL_OK:      return SYS_OK;
        case HAL_BUSY:    return SYS_ERR_BUSY;
        case HAL_TIMEOUT: return SYS_ERR_TIMEOUT;
        case HAL_ERROR:
        default:          return SYS_ERR_IO;
    }
}

static SysResult_t BSP_IIC_Init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {0};
    HAL_StatusTypeDef status;
    SysResult_t result;

    if (s_initializing)
        return SYS_ERR_BUSY;

    if (s_initialized)
    {
        /* Do not silently reuse a timing value after a clock-tree change. */
        if (__HAL_RCC_GET_I2C2_SOURCE() != RCC_I2C123CLKSOURCE_D2PCLK1)
            return SYS_ERR_INVALID_STATE;

        return HAL_RCC_GetPCLK1Freq() == BSP_IIC_KERNEL_HZ
               ? SYS_OK : SYS_ERR_INVALID_STATE;
    }

    s_initializing = true;

    /* Configure the actual kernel mux, not just the APB peripheral gate.
     * I2C1/2/3 share this mux on STM32H747; future users must coordinate it.
     * Keep fallible RCC configuration here so its status can be returned.
     */
    clock.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
    clock.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
    status = HAL_RCCEx_PeriphCLKConfig(&clock);
    result = BSP_IIC_MapStatus(status);
    if (result != SYS_OK)
        goto finish;

    /* This HAL version cannot query I2C2 with GetPeriphCLKFreq.
     * Validate the kernel mux before treating PCLK1 as the I2C2 clock.
     */
    if ((__HAL_RCC_GET_I2C2_SOURCE() != RCC_I2C123CLKSOURCE_D2PCLK1) ||
        (HAL_RCC_GetPCLK1Freq() != BSP_IIC_KERNEL_HZ))
    {
        result = SYS_ERR_INVALID_STATE;
        goto finish;
    }

    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = BSP_IIC_TIMING;
    hi2c2.Init.OwnAddress1 = 0U;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0U;
    hi2c2.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    /* HAL invokes the strong MSP callback below on the first initialization. */
    status = HAL_I2C_Init(&hi2c2);
    result = BSP_IIC_MapStatus(status);
    if (result != SYS_OK)
        goto cleanup;

    status = HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE);
    result = BSP_IIC_MapStatus(status);
    if (result != SYS_OK)
        goto cleanup;

    status = HAL_I2CEx_ConfigDigitalFilter(&hi2c2, 0U);
    result = BSP_IIC_MapStatus(status);
    if (result != SYS_OK)
        goto cleanup;

    s_initialized = true;
    goto finish;

cleanup:
    /* Release partially initialized hardware; preserve the first error.
     * HAL DeInit also restores RESET so a later retry runs MSP Init again.
     */
    (void)HAL_I2C_DeInit(&hi2c2);
finish:
    s_initializing = false;
    return result;
}

/* Sole application MSP implementation for I2C2. HAL's library version is weak.
 * Do not also compile CubeMX i2c.c with another strong MSP/hi2c2 owner.
 */
void HAL_I2C_MspInit(I2C_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle == NULL || handle->Instance != I2C2)
        return;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(GPIOB, &gpio);
    __HAL_RCC_I2C2_CLK_ENABLE();

    /* CM4 uses PRIORITYGROUP_4: 0/0 matches the current priority baseline.
     * EV and ER share a preemption level to avoid nesting on this handle.
     */
    HAL_NVIC_SetPriority(I2C2_EV_IRQn, 0U, 0U);
    HAL_NVIC_SetPriority(I2C2_ER_IRQn, 0U, 0U);
    HAL_NVIC_EnableIRQ(I2C2_EV_IRQn);
    HAL_NVIC_EnableIRQ(I2C2_ER_IRQn);
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *handle)
{
    if (handle == NULL || handle->Instance != I2C2)
        return;

    /* Disable delivery before releasing the peripheral and its pins. */
    HAL_NVIC_DisableIRQ(I2C2_EV_IRQn);
    HAL_NVIC_DisableIRQ(I2C2_ER_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C2_EV_IRQn);
    HAL_NVIC_ClearPendingIRQ(I2C2_ER_IRQn);
    __HAL_RCC_I2C2_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_10 | GPIO_PIN_11);
}

/* Strong CM4 vector entries; keep the HAL handle private to this module. */
void I2C2_EV_IRQHandler(void)
{
    HAL_I2C_EV_IRQHandler(&hi2c2);
}

void I2C2_ER_IRQHandler(void)
{
    HAL_I2C_ER_IRQHandler(&hi2c2);
}

FRAMEWORK_INIT_REGISTER(
    BSP_IIC_Init,
    INIT_LEVEL_BUS
);

//阻塞发送，只用于初始化通讯
SysResult_t BSP_IIC_WriteSync(
    uint8_t addr7,
    uint8_t *data,
    uint16_t len)
{
    HAL_I2C_StateTypeDef hal_state;

    if ( data == NULL ||
		 len == 0U ||
		 addr7 < 0x08U ||
		 addr7 > 0x77U)
		return SYS_ERR_INVALID_ARG;

	if (s_tx_state == BSP_IIC_FAULT)
		return SYS_ERR_IO;
	
    if (s_tx_state != BSP_IIC_IDLE)
        return SYS_ERR_BUSY;

    hal_state = HAL_I2C_GetState(&hi2c2);

    if (hal_state == HAL_I2C_STATE_RESET)
        return SYS_ERR_INVALID_STATE;

    if (hal_state != HAL_I2C_STATE_READY)
        return SYS_ERR_BUSY;
	
    return BSP_IIC_MapStatus(
		HAL_I2C_Master_Transmit(&hi2c2, 
		(uint16_t)addr7 << 1u, 
		data, 
		len, 
		BSP_IIC_SYNC_TIMEOUT_MS));
}

SysResult_t BSP_IIC_WriteAsync(
    uint8_t addr7,
    const uint8_t *data,
    uint16_t len
)
{
	HAL_StatusTypeDef status;
	
    if ( data == NULL ||
		 len == 0U ||
		 len > BSP_IIC_TX_BUFFER_SIZE ||
		 addr7 < 0x08U ||
		 addr7 > 0x77U )
		return SYS_ERR_INVALID_ARG;
	
	if (s_tx_state == BSP_IIC_FAULT)
		return SYS_ERR_IO;
	
	if ( s_tx_state != BSP_IIC_IDLE)
		return SYS_ERR_BUSY;
	
	if ( HAL_I2C_GetState(&hi2c2) != HAL_I2C_STATE_READY)
		return SYS_ERR_BUSY;
	
	memcpy(s_tx_buffer, data, len);
	
	s_tx_addr7 = addr7;
	
	s_tx_state = BSP_IIC_TRANSFERRING;
	
	s_tx_start_tick = HAL_GetTick();
	
	status = HAL_I2C_Master_Transmit_IT(&hi2c2, 
								addr7 << 1u, 
								s_tx_buffer, 
								len);
	
	if (status != HAL_OK)
	{
		/* 当前 HAL 启动失败且未留下进行中的传输 */
		s_tx_state = BSP_IIC_IDLE;

		return BSP_IIC_MapStatus(status);
	}

	return SYS_OK;
}

bool BSP_IIC_TakeResult(SysResult_t *result)
{
	HAL_StatusTypeDef status;
    uint32_t elapsed;
	uint32_t primask;
	
    if (result == NULL)
        return false;

    /* 1. 已经完成的事务，优先交付结果 */
    if (s_tx_state == BSP_IIC_COMPLETED)
    {
        *result = s_tx_result;
        s_tx_state = BSP_IIC_IDLE;
        return true;
    }

    /* 2. 只有正在传输时才检查超时 */
    if (s_tx_state != BSP_IIC_TRANSFERRING)
        return false;

    /* 3. 计算经过时间 */
    elapsed = HAL_GetTick() - s_tx_start_tick;

    if (elapsed < BSP_IIC_ASYNC_TIMEOUT_MS)
        return false;
		
	primask = __get_PRIMASK();
	__disable_irq();

	//防止进入临界区之前，发送完成中断已修改状态
	if (s_tx_state != BSP_IIC_TRANSFERRING)
	{
		__set_PRIMASK(primask);
		return false;
	}
	
	//超时处理
	s_tx_state = BSP_IIC_ABORTING;
	
	status = HAL_I2C_Master_Abort_IT(
    &hi2c2,
    (uint16_t)s_tx_addr7 << 1u);
	
	if (status != HAL_OK)
	{//HAL_I2C_Master_Abort_IT执行失败
		*result = SYS_ERR_IO;
		s_tx_state = BSP_IIC_FAULT;
		__set_PRIMASK(primask);
		return true;
	}
	
	__set_PRIMASK(primask);
    return false;
}

void HAL_I2C_MasterTxCpltCallback(
    I2C_HandleTypeDef *hi2c)
{
    /* 1. 确认是 I2C2 */
	if ( hi2c != &hi2c2)
		return;

    /* 2. 确认当前有正在执行的异步事务 */
	if (s_tx_state != BSP_IIC_TRANSFERRING)
		return;
	
    /* 3. 保存成功结果 */
	s_tx_result = SYS_OK;
	
    /* 4. 更新事务状态为 COMPLETED */
	s_tx_state = BSP_IIC_COMPLETED;
}

void HAL_I2C_AbortCpltCallback(
    I2C_HandleTypeDef *hi2c)
{
    /* 1. 确认是 I2C2 */
	if ( hi2c != &hi2c2)
		return;

    /* 2. 确认当前有正在执行的异步事务 */
	if (s_tx_state != BSP_IIC_ABORTING)
		return;
	
    //结束这次通信，汇报结果
	s_tx_result = SYS_ERR_TIMEOUT;
	
    /* 4. 更新事务状态为 COMPLETED */
	s_tx_state = BSP_IIC_COMPLETED;
}

void HAL_I2C_ErrorCallback(
    I2C_HandleTypeDef *hi2c
)
{
	uint32_t error;
	
    /* 1. 确认是 I2C2 */
	if ( hi2c != &hi2c2)
		return;
	
	if (s_tx_state != BSP_IIC_TRANSFERRING)
		return;
	
	error = HAL_I2C_GetError(hi2c);

	if ((error & HAL_I2C_ERROR_TIMEOUT) != 0U)
	{
		s_tx_result = SYS_ERR_TIMEOUT;
	}
	else
	{
		s_tx_result = SYS_ERR_IO;
	}
	
	s_tx_state = BSP_IIC_COMPLETED;	
}


