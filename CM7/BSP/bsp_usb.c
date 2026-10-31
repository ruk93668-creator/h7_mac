//USB硬件层：配置时钟和PCD，通过事件表通知Platform

//主机请求：IRQ -> HAL PCD -> 回调表 -> USB Device Core
#include "bsp_usb.h"
#include "framework_init.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

//低于DCMI/DMA的5，沿用旧USB测试工程的6
#define USB_IRQ_PRIORITY 6U

//USB2 FIFO共1024个Word，RX由所有OUT端点共享
#define USB_FIFO_TOTAL_WORDS 1024U
#define USB_RX_FIFO_WORDS    320U
#define USB_TX0_FIFO_WORDS   64U
#define USB_TX_MIN_WORDS     16U

//固定RX容纳1023字节最大OUT包及9组端点状态信息
#if USB_RX_FIFO_WORDS < (13U + 257U + 2U * BSP_USB_ENDPOINT_COUNT + 1U)
#error USB_RX_FIFO_too_small
#endif

static uint16_t s_tx_fifo_words[BSP_USB_ENDPOINT_COUNT];
static bool s_fifo_valid;

//每个IN端点独占一个PIO包缓冲，尾部补齐到32位读取边界
#define BSP_USB_PIO_PACKET_SIZE (1023U + 3U)
#define BSP_USB_PIO_SLOT_SIZE ((BSP_USB_PIO_PACKET_SIZE + 31U) & ~31U)
static uint8_t s_tx_pio_pool[BSP_USB_ENDPOINT_COUNT][BSP_USB_PIO_SLOT_SIZE] __attribute__((aligned(32)));

typedef struct
{
    const uint8_t *source;
    uint32_t total;
    uint32_t offset;
    uint16_t packet_length;
    bool active;
} UsbTxTransfer_t;

static UsbTxTransfer_t s_tx_transfer[BSP_USB_ENDPOINT_COUNT];
static bool s_rx_active[BSP_USB_ENDPOINT_COUNT];

//BSP私有PCD句柄，保存USB硬件和端点运行状态
static PCD_HandleTypeDef s_pcd;

//Platform提供的事件表，ISR通过它向上通知，不依赖USB Core头文件
static const BspUsbCallbacks_t * volatile s_callbacks;

//PCD及FIFO已初始化，可以绑定回调和启动USB
static volatile bool s_ready;

//USB已启动，用于控制中断入口是否交给HAL处理
static volatile bool s_started;

//校验Full-Speed端点地址、类型和最大包长
static SysResult_t Usb_ValidateEp(uint8_t ep, uint8_t type, uint16_t size);

//停止时设置完整FIFO布局，成功后才提交容量
static SysResult_t Usb_ConfigureFifos(const BspUsbEndpointConfig_t *config, uint32_t count);

//按IN和OUT方向访问同编号端点各自的PCD上下文
static USB_OTG_EPTypeDef *Usb_PcdEp(uint8_t ep);

//复制一个USB包并补齐PIO的32位读取尾部
static void Usb_PrepareTxPacket(uint8_t ep, const uint8_t *source, uint16_t length);

//判断除目标端点外是否仍有OUT接收占用共享RX FIFO
static bool Usb_OtherRxActive(uint8_t ep);

//使用独立HSI48，并用USB2每毫秒SOF作为CRS校准参考
static SysResult_t Usb_ClockInit(void);

//把HAL状态转换为工程统一的SysResult_t返回码
static SysResult_t Usb_HalResult(HAL_StatusTypeDef status);

//端点编号0到8，方向位独立，其余地址位必须为0
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

    //保留控制器9组端点能力，端点由Core和Class按需打开
    s_pcd.Init.dev_endpoints = BSP_USB_ENDPOINT_COUNT;

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

    //关闭硬件VBUS检测
    s_pcd.Init.vbus_sensing_enable = DISABLE;

    //EP1使用OTG_FS公共中断，不使用独立中断入口
    s_pcd.Init.use_dedicated_ep1 = DISABLE;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    status = HAL_PCD_Init(&s_pcd);
    result = status == HAL_OK ? Usb_ConfigureFifos(NULL, 0U) : Usb_HalResult(status);
    if (result != SYS_OK)
    {
        (void)HAL_PCD_DeInit(&s_pcd);
        return result;
    }
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    s_ready = true;
    return SYS_OK;
}

//仅在停止且未打开数据端点时规划，拒绝破坏活动FIFO
SysResult_t BSP_USB_ConfigureEndpoints(const BspUsbEndpointConfig_t *config, uint32_t count)
{
    uint8_t i;
    if (!s_ready)
        return SYS_ERR_INVALID_STATE;
    if (s_started)
        return SYS_ERR_BUSY;
    for (i = 1U; i < BSP_USB_ENDPOINT_COUNT; ++i)
        if (s_pcd.IN_ep[i].maxpacket != 0U || s_pcd.OUT_ep[i].maxpacket != 0U)
            return SYS_ERR_BUSY;
    return Usb_ConfigureFifos(config, count);
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
    if (!s_ready || !s_fifo_valid || s_callbacks == NULL)
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
    uint8_t ep;
    bool have_out = false;
    if (!s_ready)
        return SYS_ERR_INVALID_STATE;
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);

    //先停止所有已打开方向，不能在硬件仍引用缓冲区时释放所有权
    for (ep = 0U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
    {
        if (s_pcd.IN_ep[ep].maxpacket != 0U)
        {
            status = HAL_PCD_EP_Abort(&s_pcd, (uint8_t)(0x80U | ep));
            if (status != HAL_OK)
                return Usb_HalResult(status);
        }
        if (s_pcd.OUT_ep[ep].maxpacket != 0U)
        {
            status = HAL_PCD_EP_Abort(&s_pcd, ep);
            if (status != HAL_OK)
                return Usb_HalResult(status);
            have_out = true;
        }
    }
    if (have_out)
    {
        status = HAL_PCD_EP_Flush(&s_pcd, 0U);
        if (status != HAL_OK)
            return Usb_HalResult(status);
    }
    status = HAL_PCD_Stop(&s_pcd);
    HAL_NVIC_ClearPendingIRQ(OTG_FS_IRQn);
    if (status == HAL_OK)
    {
        s_started = false;
        memset(s_tx_transfer, 0, sizeof(s_tx_transfer));
        memset(s_rx_active, 0, sizeof(s_rx_active));
    }
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
    s_fifo_valid = false;
    return SYS_OK;
}

//按硬件能力及当前FIFO布局打开端点
SysResult_t BSP_USB_OpenEp(uint8_t ep, uint8_t type, uint16_t size)
{
    SysResult_t result;
    HAL_StatusTypeDef status;
    USB_OTG_EPTypeDef *entry;
    USB_OTG_EPTypeDef previous;
    if (!s_ready || !s_fifo_valid)
        return SYS_ERR_INVALID_STATE;
    result = Usb_ValidateEp(ep, type, size);
    if (result != SYS_OK)
        return result;
    if ((ep & 0x80U) != 0U && (size + 3U) / 4U > s_tx_fifo_words[ep & 0x0FU])
        return SYS_ERR_NO_RESOURCE;
    entry = Usb_PcdEp(ep);
    previous = *entry;
    status = HAL_PCD_EP_Open(&s_pcd, ep, size, type);

    //HAL先写上下文再检查锁，失败时恢复，避免残留虚假占用
    if (status != HAL_OK)
        *entry = previous;
    return Usb_HalResult(status);
}

//取消传输并清理FIFO成功后，才允许关闭端点
SysResult_t BSP_USB_CloseEp(uint8_t ep)
{
    HAL_StatusTypeDef status;
    USB_OTG_EPTypeDef *entry;
    SysResult_t result;
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    entry = Usb_PcdEp(ep);
    if (entry->maxpacket == 0U)
        return SYS_ERR_INVALID_STATE;
    result = BSP_USB_AbortEp(ep);
    if (result != SYS_OK)
        return result;
    status = HAL_PCD_EP_Close(&s_pcd, ep);
    if (status == HAL_OK)
    {
        entry->maxpacket = 0U;
        entry->is_stall = 0U;
        entry->xfer_buff = NULL;
        if ((ep & 0x80U) != 0U)
            memset(&s_tx_transfer[ep & 0x0FU], 0, sizeof(s_tx_transfer[0]));
        else
            s_rx_active[ep & 0x0FU] = false;
    }
    return Usb_HalResult(status);
}
//取消指定方向的传输；OUT只有在其他接收空闲时才清空共享RX FIFO
SysResult_t BSP_USB_AbortEp(uint8_t ep)
{
    HAL_StatusTypeDef status;
    uint8_t index;
    if (!s_ready || !Usb_ValidEp(ep) || (ep & 0x0FU) == 0U)
        return SYS_ERR_INVALID_ARG;
    index = ep & 0x0FU;
    if (Usb_PcdEp(ep)->maxpacket == 0U)
        return SYS_ERR_INVALID_STATE;
    if ((ep & 0x80U) == 0U && s_rx_active[index] && Usb_OtherRxActive(ep))
        return SYS_ERR_BUSY;
    status = HAL_PCD_EP_Abort(&s_pcd, ep);
    if (status != HAL_OK)
        return Usb_HalResult(status);
    if ((ep & 0x80U) != 0U || s_rx_active[index])
    {
        status = HAL_PCD_EP_Flush(&s_pcd, ep);
        if (status != HAL_OK)
            return Usb_HalResult(status);
    }
    if ((ep & 0x80U) != 0U)
        memset(&s_tx_transfer[index], 0, sizeof(s_tx_transfer[index]));
    else
        s_rx_active[index] = false;
    return SYS_OK;
}

//生命周期清理先停止所有数据端点，再分别清IN FIFO并统一清OUT共享FIFO
SysResult_t BSP_USB_AbortAll(void)
{
    HAL_StatusTypeDef status;
    uint8_t ep;
    bool have_out = false;
    for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
    {
        if (s_pcd.IN_ep[ep].maxpacket != 0U)
        {
            status = HAL_PCD_EP_Abort(&s_pcd, (uint8_t)(0x80U | ep));
            if (status != HAL_OK) return Usb_HalResult(status);
        }
        if (s_pcd.OUT_ep[ep].maxpacket != 0U)
        {
            status = HAL_PCD_EP_Abort(&s_pcd, ep);
            if (status != HAL_OK) return Usb_HalResult(status);
            have_out = true;
        }
    }
    for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
    {
        if (s_pcd.IN_ep[ep].maxpacket != 0U)
        {
            status = HAL_PCD_EP_Flush(&s_pcd, (uint8_t)(0x80U | ep));
            if (status != HAL_OK) return Usb_HalResult(status);
        }
    }
    if (have_out)
    {
        status = HAL_PCD_EP_Flush(&s_pcd, 0U);
        if (status != HAL_OK) return Usb_HalResult(status);
    }
    memset(s_tx_transfer, 0, sizeof(s_tx_transfer));
    memset(s_rx_active, 0, sizeof(s_rx_active));
    return SYS_OK;
}


//清空空闲端点FIFO；OUT方向共享RX，存在任一接收时禁止清空
SysResult_t BSP_USB_FlushEp(uint8_t ep)
{
    uint8_t i;
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    if (Usb_PcdEp(ep)->maxpacket == 0U)
        return SYS_ERR_INVALID_STATE;
    if ((ep & 0x80U) != 0U)
    {
        if (s_tx_transfer[ep & 0x0FU].active) return SYS_ERR_BUSY;
    }
    else
    {
        for (i = 1U; i < BSP_USB_ENDPOINT_COUNT; ++i)
            if (s_rx_active[i]) return SYS_ERR_BUSY;
    }
    return Usb_HalResult(HAL_PCD_EP_Flush(&s_pcd, ep));
}

//设置端点STALL，通知主机当前请求不能处理
SysResult_t BSP_USB_StallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;

    if (Usb_PcdEp(ep)->maxpacket == 0U)
        return SYS_ERR_INVALID_STATE;

    //按实际端点类型拒绝等时HALT，不绑定端点编号
    if ((ep & 0x0FU) != 0U && Usb_PcdEp(ep)->type == EP_TYPE_ISOC)
        return SYS_ERR_NOT_SUPPORTED;
    return Usb_HalResult(HAL_PCD_EP_SetStall(&s_pcd, ep));
}

//清除端点STALL，允许后续控制传输
SysResult_t BSP_USB_ClearStallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;

    if (Usb_PcdEp(ep)->maxpacket == 0U)
        return SYS_ERR_INVALID_STATE;

    //按实际端点类型拒绝等时HALT，不绑定端点编号
    if ((ep & 0x0FU) != 0U && Usb_PcdEp(ep)->type == EP_TYPE_ISOC)
        return SYS_ERR_NOT_SUPPORTED;
    return Usb_HalResult(HAL_PCD_EP_ClrStall(&s_pcd, ep));
}

//准备PIO包，保证HAL按32位读取时尾部仍处于已分配内存
static void Usb_PrepareTxPacket(uint8_t ep, const uint8_t *source, uint16_t length)
{
    uint32_t padded = ((uint32_t)length + 3U) & ~3UL;
    if (length != 0U) memcpy(s_tx_pio_pool[ep], source, length);
    if (padded > length) memset(&s_tx_pio_pool[ep][length], 0, padded - length);
}

//判断除目标端点外是否仍有OUT接收占用共享RX FIFO
static bool Usb_OtherRxActive(uint8_t ep)
{
    uint8_t i;
    for (i = 1U; i < BSP_USB_ENDPOINT_COUNT; ++i)
        if (i != (ep & 0x0FU) && s_rx_active[i]) return true;
    return false;
}

//按地址最高位区分IN和OUT，查询端点STALL状态
bool BSP_USB_IsStallEp(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep))
        return false;

    //方向位选择IN或OUT数组，低4位选择实际端点编号
    if ((ep & 0x80U) != 0U)
        return s_pcd.IN_ep[ep & 0x0FU].is_stall != 0U;
    return s_pcd.OUT_ep[ep & 0x0FU].is_stall != 0U;
}

//设置主机分配的7位USB设备地址
SysResult_t BSP_USB_SetAddress(uint8_t address)
{
    if (!s_ready || address > 127U)
        return SYS_ERR_INVALID_ARG;
    return Usb_HalResult(HAL_PCD_SetAddress(&s_pcd, address));
}

//每次只提交一个有尾部补齐的PIO包，多包传输由完成回调逐包推进
SysResult_t BSP_USB_Transmit(uint8_t ep, uint8_t *data, uint32_t size)
{
    USB_OTG_EPTypeDef *entry;
    UsbTxTransfer_t *transfer;
    uint8_t index;
    uint16_t packet;
    HAL_StatusTypeDef status;
    if (ep == 0U) ep = 0x80U;
    if (!s_ready || !Usb_ValidEp(ep) || (ep & 0x80U) == 0U ||
        (data == NULL && size != 0U))
        return SYS_ERR_INVALID_ARG;
    entry = Usb_PcdEp(ep);
    if (entry->maxpacket == 0U) return SYS_ERR_INVALID_STATE;
    index = ep & 0x0FU;
    transfer = &s_tx_transfer[index];
    if (transfer->active) return SYS_ERR_BUSY;
    if (entry->type == EP_TYPE_ISOC && size > entry->maxpacket) return SYS_ERR_INVALID_ARG;
    if (size > 0x7FFFFU || (index != 0U && size > entry->maxpacket * 1023U))
        return SYS_ERR_INVALID_ARG;

    packet = (uint16_t)(size > entry->maxpacket ? entry->maxpacket : size);
    transfer->source = data;
    transfer->total = size;
    transfer->offset = packet;
    transfer->packet_length = packet;
    transfer->active = true;
    Usb_PrepareTxPacket(index, data, packet);
    status = HAL_PCD_EP_Transmit(&s_pcd, ep, s_tx_pio_pool[index], packet);
    if (status != HAL_OK) memset(transfer, 0, sizeof(*transfer));
    return Usb_HalResult(status);
}

//准备指定OUT端点接收，EP0支持零长度状态包
SysResult_t BSP_USB_Receive(uint8_t ep, uint8_t *data, uint32_t size)
{
    USB_OTG_EPTypeDef *entry;
    if (!s_ready || !Usb_ValidEp(ep) || (ep & 0x80U) != 0U ||
        (data == NULL && size != 0U))
        return SYS_ERR_INVALID_ARG;
    entry = Usb_PcdEp(ep);
    if (entry->maxpacket == 0U)
        return SYS_ERR_INVALID_STATE;
    if ((ep & 0x0FU) != 0U)
    {
        //HAL会向上取整接收量，调用者按整包容量提供缓冲区
        if (size == 0U || size % entry->maxpacket != 0U ||
            size > 0x7FFFFU || size > entry->maxpacket * 1023U)
            return SYS_ERR_INVALID_ARG;
        if (entry->type == EP_TYPE_ISOC && size != entry->maxpacket)
            return SYS_ERR_INVALID_ARG;
    }
    {
        HAL_StatusTypeDef status = HAL_PCD_EP_Receive(&s_pcd, ep, data, size);
        if (status == HAL_OK && (ep & 0x0FU) != 0U) s_rx_active[ep & 0x0FU] = true;
        return Usb_HalResult(status);
    }
}

//读取指定OUT端点实际接收的字节数
uint32_t BSP_USB_GetRxSize(uint8_t ep)
{
    if (!s_ready || !Usb_ValidEp(ep) || (ep & 0x80U) != 0U ||
        Usb_PcdEp(ep)->maxpacket == 0U)
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
    UsbTxTransfer_t *transfer;
    uint8_t *callback_data;
    uint16_t packet;
    HAL_StatusTypeDef status;
    if (pcd != &s_pcd || s_callbacks == NULL || ep >= BSP_USB_ENDPOINT_COUNT) return;
    transfer = &s_tx_transfer[ep];
    if (!transfer->active)
    {
        s_callbacks->data_in(ep, pcd->IN_ep[ep].xfer_buff);
        return;
    }
    if (ep == 0U)
    {
        callback_data = transfer->source == NULL ? NULL :
                       (uint8_t *)(transfer->source + transfer->packet_length);
        memset(transfer, 0, sizeof(*transfer));
        s_callbacks->data_in(ep, callback_data);
        return;
    }
    if (transfer->offset < transfer->total)
    {
        packet = (uint16_t)((transfer->total - transfer->offset) > pcd->IN_ep[ep].maxpacket ?
                            pcd->IN_ep[ep].maxpacket : (transfer->total - transfer->offset));
        Usb_PrepareTxPacket(ep, transfer->source + transfer->offset, packet);
        transfer->offset += packet;
        transfer->packet_length = packet;
        status = HAL_PCD_EP_Transmit(&s_pcd, (uint8_t)(0x80U | ep), s_tx_pio_pool[ep], packet);
        if (status == HAL_OK) return;
        memset(transfer, 0, sizeof(*transfer));
        if (s_callbacks->data_in_error != NULL)
            s_callbacks->data_in_error(ep, Usb_HalResult(status));
        return;
    }
    callback_data = transfer->source == NULL ? NULL : (uint8_t *)(transfer->source + transfer->total);
    memset(transfer, 0, sizeof(*transfer));
    s_callbacks->data_in(ep, callback_data);
}

//主机发送阶段完成，将端点和接收缓冲区转交Platform
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
    {
        if (ep < BSP_USB_ENDPOINT_COUNT) s_rx_active[ep] = false;
        s_callbacks->data_out(ep, pcd->OUT_ep[ep].xfer_buff);
    }
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

//HAL报告IN等时包未完成，不在BSP解释Class协议
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
    {
        if (ep < BSP_USB_ENDPOINT_COUNT) memset(&s_tx_transfer[ep], 0, sizeof(s_tx_transfer[ep]));
        if (s_callbacks->iso_incomplete_in != NULL) s_callbacks->iso_incomplete_in(ep);
    }
}

//HAL报告PCD全局硬件错误
void HAL_PCD_ErrorCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->error != NULL)
        s_callbacks->error(SYS_ERR_IO);
}

//HAL报告OUT等时包未完成
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
    {
        if (ep < BSP_USB_ENDPOINT_COUNT) s_rx_active[ep] = false;
        if (s_callbacks->iso_incomplete_out != NULL) s_callbacks->iso_incomplete_out(ep);
    }
}

//BUS阶段只准备硬件，待Platform和Class就绪后再启动连接
FRAMEWORK_INIT_REGISTER(BSP_USB_Init, INIT_LEVEL_BUS);

//内部功能函数实现

//端点编号0到8，方向位独立，其余地址位必须为0
static bool Usb_ValidEp(uint8_t ep)
{
    return (ep & 0x70U) == 0U && (ep & 0x0FU) < BSP_USB_ENDPOINT_COUNT;
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
	
	// 检查USB内核时钟源是否选择HSI48
	if (__HAL_RCC_GET_USB_SOURCE() != RCC_USBCLKSOURCE_HSI48)
		return SYS_ERR_INVALID_STATE;

	// 检查HSI48振荡器是否就绪
	if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSI48RDY) == 0U)
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

//按方向定位PCD资源
static USB_OTG_EPTypeDef *Usb_PcdEp(uint8_t ep)
{
    return (ep & 0x80U) != 0U ? &s_pcd.IN_ep[ep & 0x0FU] : &s_pcd.OUT_ep[ep & 0x0FU];
}

//FS规范：Bulk为8/16/32/64，Interrupt为1到64，ISO为1到1023
static SysResult_t Usb_ValidateEp(uint8_t ep, uint8_t type, uint16_t size)
{
    if (!Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    if ((ep & 0x0FU) == 0U)
        return type == EP_TYPE_CTRL && size == BSP_USB_EP0_MAX_PACKET_SIZE ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
    if (type == EP_TYPE_BULK)
        return size == 8U || size == 16U || size == 32U || size == 64U ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
    if (type == EP_TYPE_INTR)
        return size >= 1U && size <= 64U ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
    if (type == EP_TYPE_ISOC)
        return size >= 1U && size <= 1023U ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
    return SYS_ERR_NOT_SUPPORTED;
}

//每个IN对应同编号TX FIFO，未使用的FIFO也保留16个Word
static SysResult_t Usb_ConfigureFifos(const BspUsbEndpointConfig_t *config, uint32_t count)
{
    uint16_t words[BSP_USB_ENDPOINT_COUNT];
    uint32_t total = USB_RX_FIFO_WORDS + USB_TX0_FIFO_WORDS;
    uint32_t i;
    uint32_t required;
    HAL_StatusTypeDef status;
    SysResult_t result;
    if ((config == NULL && count != 0U) || count > BSP_USB_MAX_ENDPOINT_CONFIGS)
        return SYS_ERR_INVALID_ARG;
    words[0] = USB_TX0_FIFO_WORDS;
    for (i = 1U; i < BSP_USB_ENDPOINT_COUNT; ++i)
        words[i] = USB_TX_MIN_WORDS;
    for (i = 0U; i < count; ++i)
    {
        result = Usb_ValidateEp(config[i].address, config[i].type, config[i].max_packet);
        if (result != SYS_OK)
            return result;
        if ((config[i].address & 0x80U) != 0U)
        {
            required = (config[i].max_packet + 3U) / 4U;
            if (required > words[config[i].address & 0x0FU])
                words[config[i].address & 0x0FU] = (uint16_t)required;
        }
    }
    for (i = 1U; i < BSP_USB_ENDPOINT_COUNT; ++i)
        total += words[i];
    if (total > USB_FIFO_TOTAL_WORDS)
        return SYS_ERR_NO_RESOURCE;

    //先完整校验，再写硬件；失败时禁止用半套FIFO启动
    s_fifo_valid = false;
    status = HAL_PCDEx_SetRxFiFo(&s_pcd, USB_RX_FIFO_WORDS);
    for (i = 0U; status == HAL_OK && i < BSP_USB_ENDPOINT_COUNT; ++i)
        status = HAL_PCDEx_SetTxFiFo(&s_pcd, (uint8_t)i, words[i]);
    if (status != HAL_OK)
        return Usb_HalResult(status);
    for (i = 0U; i < BSP_USB_ENDPOINT_COUNT; ++i)
        s_tx_fifo_words[i] = words[i];
    s_fifo_valid = true;
    return SYS_OK;
}
