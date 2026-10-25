#include "bsp_usb.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>

//低于DCMI/DMA的5，沿用旧USB测试工程的6
#define USB_IRQ_PRIORITY 6U
static PCD_HandleTypeDef s_pcd;
static const BspUsbCallbacks_t * volatile s_callbacks;
static volatile bool s_ready;
static volatile bool s_started;

//配置独立HSI48并使用USB2 SOF校准
static SysResult_t Usb_ClockInit(void);
//转换HAL返回值
static SysResult_t Usb_HalResult(HAL_StatusTypeDef status);
//本阶段只允许EP0的两个方向
static bool Usb_ValidEp(uint8_t ep);

SysResult_t BSP_USB_Init(void)
{
    HAL_StatusTypeDef status;
    SysResult_t result;
    if (s_ready)
        return SYS_OK;
    result = Usb_ClockInit();
    if (result != SYS_OK)
        return result;

    //VDD33USB与VDD50USB直接接3.3V，不启用内部5V稳压器
    HAL_PWREx_DisableUSBReg();
    HAL_PWREx_EnableUSBVoltageDetector();
    s_pcd.Instance = USB2_OTG_FS;
    s_pcd.Init.dev_endpoints = 9U;
    s_pcd.Init.speed = PCD_SPEED_FULL;
    s_pcd.Init.dma_enable = DISABLE;
    s_pcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    s_pcd.Init.Sof_enable = DISABLE;
    s_pcd.Init.low_power_enable = DISABLE;
    s_pcd.Init.lpm_enable = DISABLE;
    s_pcd.Init.vbus_sensing_enable = DISABLE;
    s_pcd.Init.use_dedicated_ep1 = DISABLE;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    status = HAL_PCD_Init(&s_pcd);
    if (status == HAL_OK)
        status = HAL_PCDEx_SetRxFiFo(&s_pcd, 128U);
    if (status == HAL_OK)
        status = HAL_PCDEx_SetTxFiFo(&s_pcd, 0U, 64U);
    if (status != HAL_OK)
    {
        (void)HAL_PCD_DeInit(&s_pcd);
        return Usb_HalResult(status);
    }
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    s_ready = true;
    return SYS_OK;
}

SysResult_t BSP_USB_SetCallbacks(const BspUsbCallbacks_t *callbacks)
{
    if (callbacks == NULL || callbacks->setup == NULL || callbacks->reset == NULL ||
        callbacks->data_in == NULL || callbacks->data_out == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_ready || s_started)
        return SYS_ERR_INVALID_STATE;
    s_callbacks = callbacks;
    return SYS_OK;
}

SysResult_t BSP_USB_Start(void)
{
    HAL_StatusTypeDef status;
    if (!s_ready || s_callbacks == NULL)
        return SYS_ERR_INVALID_STATE;
    if (s_started)
        return SYS_ERR_BUSY;
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    status = HAL_PCD_Start(&s_pcd);
    if (status != HAL_OK)
        return Usb_HalResult(status);
    s_started = true;
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    return SYS_OK;
}

SysResult_t BSP_USB_Stop(void)
{
    HAL_StatusTypeDef status;
    if (!s_ready)
        return SYS_ERR_INVALID_STATE;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    status = HAL_PCD_Stop(&s_pcd);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    if (status == HAL_OK)
        s_started = false;
    return Usb_HalResult(status);
}

SysResult_t BSP_USB_DeInit(void)
{
    SysResult_t result;
    HAL_StatusTypeDef status;
    if (!s_ready)
        return SYS_OK;
    result = BSP_USB_Stop();
    if (result != SYS_OK)
        return result;
    status = HAL_PCD_DeInit(&s_pcd);
    if (status != HAL_OK)
        return Usb_HalResult(status);
    s_callbacks = NULL;
    s_ready = false;
    return SYS_OK;
}

SysResult_t BSP_USB_OpenEp(uint8_t ep, uint8_t type, uint16_t size)
{
    if (!s_ready)
        return SYS_ERR_INVALID_STATE;
    if (!Usb_ValidEp(ep) || type != EP_TYPE_CTRL || size != 64U)
        return SYS_ERR_NOT_SUPPORTED;
    return Usb_HalResult(HAL_PCD_EP_Open(&s_pcd, ep, size, type));
}
SysResult_t BSP_USB_CloseEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep)) return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Close(&s_pcd, ep));
}
SysResult_t BSP_USB_FlushEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep)) return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Flush(&s_pcd, ep));
}
SysResult_t BSP_USB_StallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep)) return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_SetStall(&s_pcd, ep));
}
SysResult_t BSP_USB_ClearStallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep)) return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_ClrStall(&s_pcd, ep));
}
bool BSP_USB_IsStallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep)) return false;
    return (ep & 0x80U) != 0U ? s_pcd.IN_ep[0].is_stall != 0U : s_pcd.OUT_ep[0].is_stall != 0U;
}
SysResult_t BSP_USB_SetAddress(uint8_t address)
{
    if (!s_ready || address > 127U) return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_SetAddress(&s_pcd, address));
}
SysResult_t BSP_USB_Transmit(uint8_t ep, uint8_t *data, uint32_t size)
{
    if (!s_ready || !Usb_ValidEp(ep) || (data == NULL && size != 0U)) return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Transmit(&s_pcd, ep, data, size));
}
SysResult_t BSP_USB_Receive(uint8_t ep, uint8_t *data, uint32_t size)
{
    if (!s_ready || ep != 0U || (data == NULL && size != 0U)) return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Receive(&s_pcd, ep, data, size));
}
uint32_t BSP_USB_GetRxSize(uint8_t ep)
{
    if (!s_ready || ep != 0U) return 0U;
    return HAL_PCD_EP_GetRxCount(&s_pcd, ep);
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcd)
{
    GPIO_InitTypeDef gpio = {0};
    if (pcd->Instance != USB2_OTG_FS) return;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    //原理图仅连接PA11/PA12，不配置未接线的PA9和PA10
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG2_FS;
    HAL_GPIO_Init(GPIOA, &gpio);
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, USB_IRQ_PRIORITY, 0U);
}
void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcd)
{
    if (pcd->Instance != USB2_OTG_FS) return;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    __HAL_RCC_USB2_OTG_FS_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}
void OTG_FS_IRQHandler(void)
{
    if (s_ready && s_started) HAL_PCD_IRQHandler(&s_pcd);
}
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL) s_callbacks->setup((uint8_t *)pcd->Setup);
}
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL) s_callbacks->data_in(ep, pcd->IN_ep[ep].xfer_buff);
}
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL) s_callbacks->data_out(ep, pcd->OUT_ep[ep].xfer_buff);
}
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL) s_callbacks->reset();
}
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->suspend != NULL) s_callbacks->suspend();
}
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->resume != NULL) s_callbacks->resume();
}
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->connect != NULL) s_callbacks->connect();
}
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->disconnect != NULL) s_callbacks->disconnect();
}

FRAMEWORK_INIT_REGISTER(BSP_USB_Init, INIT_LEVEL_BUS);

//内部功能函数实现
static bool Usb_ValidEp(uint8_t ep)
{
    return ep == 0U || ep == 0x80U;
}
static SysResult_t Usb_HalResult(HAL_StatusTypeDef status)
{
    if (status == HAL_OK) return SYS_OK;
    if (status == HAL_BUSY) return SYS_ERR_BUSY;
    if (status == HAL_TIMEOUT) return SYS_ERR_TIMEOUT;
    return SYS_ERR_IO;
}
static SysResult_t Usb_ClockInit(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef clock = {0};
    RCC_CRSInitTypeDef crs = {0};
    HAL_StatusTypeDef status;
    //只启用独立HSI48，不改PLL1/PLL2及公共PLL输入
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
    osc.HSI48State = RCC_HSI48_ON;
    osc.PLL.PLLState = RCC_PLL_NONE;
    status = HAL_RCC_OscConfig(&osc);
    if (status != HAL_OK) return Usb_HalResult(status);
    clock.PeriphClockSelection = RCC_PERIPHCLK_USB;
    clock.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    status = HAL_RCCEx_PeriphCLKConfig(&clock);
    if (status != HAL_OK) return Usb_HalResult(status);
    if (HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USB) != 48000000U)
        return SYS_ERR_INVALID_STATE;
    __HAL_RCC_CRS_CLK_ENABLE();
    crs.Prescaler = RCC_CRS_SYNC_DIV1;
    crs.Source = RCC_CRS_SYNC_SOURCE_USB2;
    crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
    crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
    crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
    crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
    HAL_RCCEx_CRSConfig(&crs);
    return SYS_OK;
}
