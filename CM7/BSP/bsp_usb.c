//USB硬件层：配置时钟和PCD，通过事件表通知Platform

//主机请求：IRQ -> HAL PCD -> 回调表 -> USB Device Core
#include "bsp_usb.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>

//低于DCMI/DMA的5，沿用旧USB测试工程的6
#define USB_IRQ_PRIORITY 6U

//BSP私有PCD句柄，保存USB硬件和端点运行状态
static PCD_HandleTypeDef s_pcd;

//Platform提供的事件表，ISR通过它向上通知，不依赖USB Core头文件
static const BspUsbCallbacks_t * volatile s_callbacks;

//PCD及FIFO已初始化，可以绑定回调和启动USB
static volatile bool s_ready;

//USB已启动，用于控制中断入口是否交给HAL处理
static volatile bool s_started;

//使用独立HSI48，并用USB2每毫秒SOF作为CRS校准参考
static SysResult_t Usb_ClockInit(void);

//把HAL状态转换为工程统一的SysResult_t返回码
static SysResult_t Usb_HalResult(HAL_StatusTypeDef status);

//只接受EP0 OUT的0x00和EP0 IN的0x80地址
static bool Usb_ValidEp(uint8_t ep);

//初始化USB时钟、PCD和FIFO，此时不连接主机
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

    //使用PA11/PA12对应的USB2 OTG FS控制器
    s_pcd.Instance = USB2_OTG_FS;

    //配置控制器端点数量，当前业务仅打开EP0
    s_pcd.Init.dev_endpoints = 9U;

    //使用12Mbit/s Full-Speed
    s_pcd.Init.speed = PCD_SPEED_FULL;

    //CPU读写USB FIFO，不使用USB内部DMA
    s_pcd.Init.dma_enable = DISABLE;

    //使用片内FS PHY，不使用外部ULPI PHY
    s_pcd.Init.phy_itface = PCD_PHY_EMBEDDED;

    //不向软件上报SOF回调，硬件CRS仍可使用SOF校准
    s_pcd.Init.Sof_enable = DISABLE;

    //不在USB挂起时切换系统低功耗状态
    s_pcd.Init.low_power_enable = DISABLE;

    //关闭USB LPM链路电源管理
    s_pcd.Init.lpm_enable = DISABLE;

    //原理图没有将VBUS接入PA9，关闭硬件VBUS检测
    s_pcd.Init.vbus_sensing_enable = DISABLE;

    //不使用EP1独立中断
    s_pcd.Init.use_dedicated_ep1 = DISABLE;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    status = HAL_PCD_Init(&s_pcd);
    if (status == HAL_OK)

        //接收FIFO分配128个32位Word
        status = HAL_PCDEx_SetRxFiFo(&s_pcd, 128U);
    if (status == HAL_OK)

        //EP0发送FIFO分配64个32位Word
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

//绑定Platform事件表，必须在USB停止时调用
SysResult_t BSP_USB_SetCallbacks(const BspUsbCallbacks_t *callbacks)
{
    if (callbacks == NULL || callbacks->setup == NULL || callbacks->reset == NULL ||
        callbacks->data_in == NULL || callbacks->data_out == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_ready || s_started)
        return SYS_ERR_INVALID_STATE;

    //只保存表指针，不复制对象，事件表必须持续有效
    s_callbacks = callbacks;
    return SYS_OK;
}

//启动USB设备连接，准备好软件状态后再开放中断
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

    //启动成功后提交软件状态
    s_started = true;
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    return SYS_OK;
}

//屏蔽USB中断并停止PCD，返回实际停止结果
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

//先停止通信，再释放PCD、GPIO和事件绑定
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

//打开64字节控制端点，本阶段只支持EP0
SysResult_t BSP_USB_OpenEp(uint8_t ep, uint8_t type, uint16_t size)
{
    if (!s_ready)
        return SYS_ERR_INVALID_STATE;
    if (!Usb_ValidEp(ep) || type != EP_TYPE_CTRL || size != 64U)
        return SYS_ERR_NOT_SUPPORTED;
    return Usb_HalResult(HAL_PCD_EP_Open(&s_pcd, ep, size, type));
}

//关闭指定方向的EP0端点
SysResult_t BSP_USB_CloseEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Close(&s_pcd, ep));
}

//清空端点FIFO，丢弃尚未处理的数据
SysResult_t BSP_USB_FlushEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Flush(&s_pcd, ep));
}

//设置端点STALL，通知主机当前请求不能处理
SysResult_t BSP_USB_StallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_SetStall(&s_pcd, ep));
}

//清除端点STALL，允许后续控制传输
SysResult_t BSP_USB_ClearStallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_ClrStall(&s_pcd, ep));
}

//按地址最高位区分IN和OUT，查询端点STALL状态
bool BSP_USB_IsStallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return false;
    return (ep & 0x80U) != 0U ? s_pcd.IN_ep[0].is_stall != 0U : s_pcd.OUT_ep[0].is_stall != 0U;
}

//设置主机分配的7位USB设备地址
SysResult_t BSP_USB_SetAddress(uint8_t address)
{
    if (!s_ready || address > 127U)
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_SetAddress(&s_pcd, address));
}

//安排设备向主机发送数据，长度为0时发送状态包
SysResult_t BSP_USB_Transmit(uint8_t ep, uint8_t *data, uint32_t size)
{
    if (!s_ready || !Usb_ValidEp(ep) || (data == NULL && size != 0U))
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Transmit(&s_pcd, ep, data, size));
}

//安排EP0接收主机数据，长度为0时接收状态包
SysResult_t BSP_USB_Receive(uint8_t ep, uint8_t *data, uint32_t size)
{
    if (!s_ready || ep != 0U || (data == NULL && size != 0U))
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_EP_Receive(&s_pcd, ep, data, size));
}

//读取最近一次EP0 OUT实际接收的字节数
uint32_t BSP_USB_GetRxSize(uint8_t ep)
{
    if (!s_ready || ep != 0U)
        return 0U;
    return HAL_PCD_EP_GetRxCount(&s_pcd, ep);
}

//由HAL_PCD_Init调用，配置USB引脚、时钟和中断优先级
void HAL_PCD_MspInit(PCD_HandleTypeDef *pcd)
{
    GPIO_InitTypeDef gpio = {0};
    if (pcd->Instance != USB2_OTG_FS) return;
    __HAL_RCC_GPIOA_CLK_ENABLE();

    //原理图仅连接PA11/PA12，不配置未接线的PA9和PA10

    //PA11为D-，PA12为D+
    gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;

    //USB复用推挽，由USB PHY控制引脚
    gpio.Mode = GPIO_MODE_AF_PP;

    //不使用GPIO上下拉，连接控制由USB PHY负责
    gpio.Pull = GPIO_NOPULL;

    //设置GPIO输出速度等级
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    //选择AF10 USB2 OTG FS复用功能
    gpio.Alternate = GPIO_AF10_OTG2_FS;
    HAL_GPIO_Init(GPIOA, &gpio);
    __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, USB_IRQ_PRIORITY, 0U);
}

//由HAL_PCD_DeInit调用，关闭USB中断并释放引脚
void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcd)
{
    if (pcd->Instance != USB2_OTG_FS) return;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    __HAL_RCC_USB2_OTG_FS_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}

//USB中断入口，交给HAL识别具体事件
void OTG_FS_IRQHandler(void)
{
    if (s_ready && s_started)
        HAL_PCD_IRQHandler(&s_pcd);
}

//收到8字节SETUP请求，转交Platform解析标准请求
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
        s_callbacks->setup((uint8_t *)pcd->Setup);
}

//设备发送阶段完成，将端点和发送缓冲区转交Platform
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
        s_callbacks->data_in(ep, pcd->IN_ep[ep].xfer_buff);
}

//主机发送阶段完成，将端点和接收缓冲区转交Platform
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
        s_callbacks->data_out(ep, pcd->OUT_ep[ep].xfer_buff);
}

//USB总线复位，通知Platform重建EP0和枚举状态
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
        s_callbacks->reset();
}

//总线挂起，存在对应上层回调时才通知
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->suspend != NULL)
        s_callbacks->suspend();
}

//总线恢复，通知上层恢复USB协议状态
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->resume != NULL)
        s_callbacks->resume();
}

//检测到连接事件，通知上层USB Device Core
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->connect != NULL)
        s_callbacks->connect();
}

//检测到断开事件，通知上层清除连接状态
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->disconnect != NULL)
        s_callbacks->disconnect();
}

//BUS阶段只准备硬件，待Platform和Class就绪后再启动连接
FRAMEWORK_INIT_REGISTER(BSP_USB_Init, INIT_LEVEL_BUS);

//内部功能函数实现

//只接受EP0 OUT的0x00和EP0 IN的0x80地址
static bool Usb_ValidEp(uint8_t ep)
{
    return ep == 0U || ep == 0x80U;
}

//把HAL状态转换为工程统一的SysResult_t返回码
static SysResult_t Usb_HalResult(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
        return SYS_OK;
    if (status == HAL_BUSY)
        return SYS_ERR_BUSY;
    if (status == HAL_TIMEOUT)
        return SYS_ERR_TIMEOUT;
    return SYS_ERR_IO;
}

//使用独立HSI48，并用USB2每毫秒SOF作为CRS校准参考
static SysResult_t Usb_ClockInit(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef clock = {0};
    RCC_CRSInitTypeDef crs = {0};
    HAL_StatusTypeDef status;

    //只启用独立HSI48，不改PLL1/PLL2及公共PLL输入

    //只配置独立48MHz内部振荡器
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;

    //开启HSI48
    osc.HSI48State = RCC_HSI48_ON;

    //保持现有PLL设置，避免影响相机和SDRAM
    osc.PLL.PLLState = RCC_PLL_NONE;
    status = HAL_RCC_OscConfig(&osc);
    if (status != HAL_OK)
        return Usb_HalResult(status);

    //只修改USB外设内核时钟选择
    clock.PeriphClockSelection = RCC_PERIPHCLK_USB;

    //将USB内核时钟接到HSI48
    clock.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    status = HAL_RCCEx_PeriphCLKConfig(&clock);
    if (status != HAL_OK)
        return Usb_HalResult(status);
    if (HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USB) != 48000000U)
        return SYS_ERR_INVALID_STATE;
    __HAL_RCC_CRS_CLK_ENABLE();

    //SOF同步信号不分频
    crs.Prescaler = RCC_CRS_SYNC_DIV1;

    //选USB2 SOF作为校准参考，连接主机后才有该信号
    crs.Source = RCC_CRS_SYNC_SOURCE_USB2;

    //在参考信号上升沿进行同步
    crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;

    //按48MHz时钟和1kHz SOF计算校准计数周期
    crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);

    //使用HAL默认允许误差计数
    crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;

    //使用默认HSI48初始修调值，后续由CRS自动校准
    crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
    HAL_RCCEx_CRSConfig(&crs);
    return SYS_OK;
}
