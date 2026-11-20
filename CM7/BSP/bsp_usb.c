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
    uint32_t queued_bytes;
    uint32_t completed_bytes;
    uint16_t current_packet_length;
    bool active;
    bool failed;
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
//Bulk IN独占目标事件；其他端点及控制请求不在此实现中推进
#define USB_IN_STOP_TERMINAL_EVENTS (USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD | USB_OTG_DIEPINT_TOC)
#define USB_IN_STOP_EVENTS (USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD | \
                            USB_OTG_DIEPINT_TOC | USB_OTG_DIEPINT_ITTXFE | \
                            USB_OTG_DIEPINT_INEPNE | USB_OTG_DIEPINT_INEPNM | USB_OTG_DIEPINT_NAK)

typedef struct
{
    BspUsbInStopProof_t proof;
    uint32_t began_at;
    uint32_t budget_ms;
    uint32_t poll_count;
    volatile bool fenced;
} UsbBulkInStop_t;

static UsbBulkInStop_t s_bulk_in[BSP_USB_ENDPOINT_COUNT];
static uint32_t s_bulk_controller;
static volatile uint32_t s_bulk_session = 1U;
static uint32_t s_bulk_endpoint_seed[BSP_USB_ENDPOINT_COUNT];
static uint32_t s_bulk_transfer_seed[BSP_USB_ENDPOINT_COUNT];
static uint32_t s_bulk_attempt_seed;
static volatile uint8_t s_bulk_fifo_owner;

//仅在数条状态/掩码发布指令中保持原PRIMASK，绝不跨硬件等待
static uint32_t Usb_BulkPublishLock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return saved;
}

static void Usb_BulkPublishUnlock(uint32_t saved)
{
    __DMB();
    __set_PRIMASK(saved);
}

static bool Usb_BulkIdentityMatches(const BspUsbInIdentity_t *a, const BspUsbInIdentity_t *b)
{
    return a->controller_generation == b->controller_generation &&
           a->session_generation == b->session_generation &&
           a->endpoint_generation == b->endpoint_generation &&
           a->transfer_sequence == b->transfer_sequence &&
           a->endpoint_address == b->endpoint_address;
}

static bool Usb_BulkEndpoint(uint8_t ep)
{
    uint8_t index = ep & 0x0FU;
    return Usb_ValidEp(ep) && (ep & 0x80U) != 0U && index != 0U &&
           s_pcd.IN_ep[index].maxpacket != 0U && s_pcd.IN_ep[index].type == EP_TYPE_BULK;
}

static void Usb_BulkFault(uint8_t ep, SysResult_t error)
{
    BspUsbInStopProof_t *proof = &s_bulk_in[ep].proof;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    s_bulk_in[ep].fenced = true;
    USBx_DEVICE->DAINTMSK &= ~(1UL << ep);
    USBx_DEVICE->DIEPEMPMSK &= ~(1UL << ep);
    if (proof->failure_count == 0U)
    {
        proof->first_error = error;
        proof->failed_stage = proof->stage;
    }
    if (proof->failure_count != UINT32_MAX) ++proof->failure_count;
    proof->state = BSP_USB_IN_FAULTED;
    proof->stage = BSP_USB_STOP_FAULT;
    proof->result = BSP_USB_STOP_RECOVERY_REQUIRED;
    proof->error = error;
    if (!proof->fifo_complete) proof->fifo_result = error;
}

bool BSP_USB_BulkInHasReferences(void)
{
    uint8_t ep;
    for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
        if (s_bulk_in[ep].proof.source_owned || s_bulk_in[ep].proof.pool_owned || s_bulk_in[ep].fenced)
            return true;
    return false;
}

//Reset/Disconnect改变会话资格，但绝不清除旧引用或恢复目标掩码
static void Usb_BulkInvalidateSession(void)
{
    uint8_t ep;
    if (s_bulk_session != UINT32_MAX) ++s_bulk_session;
    for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
        if (s_bulk_in[ep].proof.source_owned || s_bulk_in[ep].proof.pool_owned || s_bulk_in[ep].fenced)
        {
            uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
            s_bulk_in[ep].proof.events |= USBx_INEP(ep)->DIEPINT;
            Usb_BulkFault(ep, SYS_ERR_INVALID_STATE);
        }
}

//在任何W1C之前保存原始事件；当前包最多结算一次
static void Usb_BulkCapture(uint8_t ep, uint32_t events)
{
    BspUsbInStopProof_t *proof = &s_bulk_in[ep].proof;
    UsbTxTransfer_t *transfer = &s_tx_transfer[ep];
    proof->events |= events;
    if ((events & USB_OTG_DIEPINT_XFRC) != 0U && !proof->xfrc_claimed && transfer->active)
    {
        proof->xfrc_claimed = true;
        transfer->completed_bytes += transfer->current_packet_length;
        proof->completed_bytes = transfer->completed_bytes;
        ++proof->packet_completions;
        proof->completed_undelivered = transfer->completed_bytes == transfer->total;
    }
    if ((events & USB_OTG_DIEPINT_INEPNE) != 0U) proof->nak_effective = true;
    if ((events & USB_OTG_DIEPINT_EPDISD) != 0U) proof->disable_observed = true;
}

SysResult_t BSP_USB_GetBulkInProof(uint8_t ep, BspUsbInStopProof_t *proof)
{
    uint32_t saved;
    if (proof == NULL || !s_ready || !Usb_BulkEndpoint(ep)) return SYS_ERR_INVALID_ARG;
    saved = Usb_BulkPublishLock();
    *proof = s_bulk_in[ep & 0x0FU].proof;
    Usb_BulkPublishUnlock(saved);
    return SYS_OK;
}

SysResult_t BSP_USB_BeginBulkInStop(const BspUsbInIdentity_t *identity,
                                  uint32_t timeout_ms, BspUsbInStopProof_t *output)
{
    uint32_t saved;
    uint32_t bit;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    uint8_t ep;
    UsbBulkInStop_t *stop;
    BspUsbInStopProof_t *proof;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (identity == NULL || output == NULL || timeout_ms == 0U || timeout_ms > 1000U ||
        !s_ready || !Usb_BulkEndpoint(identity->endpoint_address)) return SYS_ERR_INVALID_ARG;
    if (s_pcd.Init.dma_enable != DISABLE) return SYS_ERR_NOT_SUPPORTED;
    ep = identity->endpoint_address & 0x0FU;
    stop = &s_bulk_in[ep];
    proof = &stop->proof;
    saved = Usb_BulkPublishLock();
    if (!Usb_BulkIdentityMatches(identity, &proof->transfer))
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    if (proof->stage != BSP_USB_STOP_NONE)
    { *output = *proof; Usb_BulkPublishUnlock(saved); return SYS_OK; }
    if (s_bulk_attempt_seed == UINT32_MAX)
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_NO_RESOURCE; }
    proof->stop_attempt = ++s_bulk_attempt_seed;
    proof->initial_ctl = USBx_INEP(ep)->DIEPCTL;
    proof->initial_events = USBx_INEP(ep)->DIEPINT;
    bit = 1UL << ep;
    proof->saved_daint_mask = USBx_DEVICE->DAINTMSK & bit;
    proof->saved_empty_mask = USBx_DEVICE->DIEPEMPMSK & bit;
    stop->fenced = true;
    USBx_DEVICE->DIEPEMPMSK &= ~bit;
    USBx_DEVICE->DAINTMSK &= ~bit;
    __DSB();
    stop->began_at = HAL_GetTick();
    stop->budget_ms = timeout_ms;
    proof->state = BSP_USB_IN_QUIESCING;
    proof->result = BSP_USB_STOP_PENDING;
    proof->fifo_result = SYS_ERR_BUSY;
    Usb_BulkCapture(ep, proof->initial_events);
    if ((proof->initial_events & USB_OTG_DIEPINT_EPDISD) != 0U)
    {
        //尚未发出本StopAttempt的EPDIS，已有禁用事件不能冒充本次完成
        Usb_BulkFault(ep, SYS_ERR_INVALID_STATE);
    }
    else if (!s_tx_transfer[ep].active && !proof->source_owned && !proof->pool_owned &&
        (proof->initial_ctl & USB_OTG_DIEPCTL_EPENA) == 0U &&
        (proof->initial_events & USB_IN_STOP_TERMINAL_EVENTS) == 0U)
    {
        //正常完成或从未Arm的空闲端点没有需取消的硬件传输
        proof->fifo_complete = true;
        proof->fifo_result = SYS_OK;
        proof->events_retired = true;
        proof->stage = BSP_USB_STOP_PROVEN;
        proof->state = BSP_USB_IN_QUIESCENT;
        proof->result = proof->transfer.transfer_sequence == 0U ?
                        BSP_USB_STOP_COMPLETED : BSP_USB_STOP_ALREADY_COMPLETED;
    }
    else
    {
        USBx_INEP(ep)->DIEPCTL |= USB_OTG_DIEPCTL_SNAK;
        proof->stage = BSP_USB_STOP_NAK;
    }
    *output = *proof;
    Usb_BulkPublishUnlock(saved);
    return SYS_OK;
}

//每次最多推进一个硬件阶段；不调用具有巨大内部循环上限的LL Flush
SysResult_t BSP_USB_PollBulkInStop(const BspUsbInIdentity_t *identity,
                                 uint32_t attempt, BspUsbInStopProof_t *output)
{
    uint8_t ep;
    uint32_t saved;
    uint32_t ctl;
    uint32_t events;
    uint32_t reset;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    UsbBulkInStop_t *stop;
    BspUsbInStopProof_t *proof;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (identity == NULL || output == NULL || !s_ready || !Usb_BulkEndpoint(identity->endpoint_address))
        return SYS_ERR_INVALID_ARG;
    ep = identity->endpoint_address & 0x0FU;
    stop = &s_bulk_in[ep];
    proof = &stop->proof;
    saved = Usb_BulkPublishLock();
    if (attempt == 0U || attempt != proof->stop_attempt ||
        !Usb_BulkIdentityMatches(identity, &proof->transfer))
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    if (proof->stage == BSP_USB_STOP_FAULT || proof->stage == BSP_USB_STOP_COMMITTED)
    { *output = *proof; Usb_BulkPublishUnlock(saved); return SYS_OK; }
    ctl = USBx_INEP(ep)->DIEPCTL;
    events = USBx_INEP(ep)->DIEPINT;
    reset = s_pcd.Instance->GRSTCTL;
    Usb_BulkCapture(ep, events);
    if (++stop->poll_count >= 4096U ||
        (uint32_t)(HAL_GetTick() - stop->began_at) >= stop->budget_ms)
        Usb_BulkFault(ep, SYS_ERR_TIMEOUT);
    else if (identity->session_generation != s_bulk_session ||
             (s_pcd.Instance->GINTSTS & USB_OTG_GINTSTS_USBRST) != 0U)
        Usb_BulkFault(ep, SYS_ERR_INVALID_STATE);
    else if (proof->stage == BSP_USB_STOP_NAK && proof->disable_observed)
        Usb_BulkFault(ep, SYS_ERR_INVALID_STATE);
    else if (proof->stage == BSP_USB_STOP_NAK && proof->nak_effective)
    {
        if ((ctl & USB_OTG_DIEPCTL_EPENA) != 0U)
        {
            USBx_INEP(ep)->DIEPCTL |= USB_OTG_DIEPCTL_EPDIS | USB_OTG_DIEPCTL_SNAK;
            proof->stage = BSP_USB_STOP_DISABLE;
        }
        else if (proof->xfrc_claimed)
            proof->stage = BSP_USB_STOP_AHB;
        else
            Usb_BulkFault(ep, SYS_ERR_IO);
    }
    else if (proof->stage == BSP_USB_STOP_DISABLE && proof->disable_observed &&
             (ctl & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS)) == 0U)
        proof->stage = BSP_USB_STOP_AHB;
    else if (proof->stage == BSP_USB_STOP_AHB && proof->pio_users == 0U &&
             (reset & USB_OTG_GRSTCTL_AHBIDL) != 0U &&
             (reset & (USB_OTG_GRSTCTL_TXFFLSH | USB_OTG_GRSTCTL_RXFFLSH)) == 0U &&
             s_bulk_fifo_owner == 0U)
    {
        if (((ctl & USB_OTG_DIEPCTL_TXFNUM) >> USB_OTG_DIEPCTL_TXFNUM_Pos) != ep)
            Usb_BulkFault(ep, SYS_ERR_IO);
        else
        {
            s_bulk_fifo_owner = ep;
            s_pcd.Instance->GRSTCTL = (reset & ~USB_OTG_GRSTCTL_TXFNUM) |
                                     ((uint32_t)ep << USB_OTG_GRSTCTL_TXFNUM_Pos) | USB_OTG_GRSTCTL_TXFFLSH;
            proof->stage = BSP_USB_STOP_FIFO;
        }
    }
    else if (proof->stage == BSP_USB_STOP_FIFO)
    {
        if (((reset & USB_OTG_GRSTCTL_TXFNUM) >> USB_OTG_GRSTCTL_TXFNUM_Pos) != ep)
            Usb_BulkFault(ep, SYS_ERR_IO);
        else if ((reset & USB_OTG_GRSTCTL_TXFFLSH) == 0U)
        {
            proof->fifo_result = SYS_OK;
            proof->fifo_complete = true;
            proof->stage = BSP_USB_STOP_RETIRE;
            s_bulk_fifo_owner = 0U;
        }
    }
    else if (proof->stage == BSP_USB_STOP_RETIRE && proof->pio_users == 0U)
    {
        USBx_INEP(ep)->DIEPINT = events & USB_IN_STOP_TERMINAL_EVENTS;
        __DSB();
        if ((USBx_INEP(ep)->DIEPINT & USB_IN_STOP_TERMINAL_EVENTS) == 0U &&
            (USBx_INEP(ep)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) == 0U)
        {
            proof->events_retired = true;
            proof->state = BSP_USB_IN_QUIESCENT;
            proof->stage = BSP_USB_STOP_PROVEN;
            proof->result = BSP_USB_STOP_COMPLETED;
        }
    }
    *output = *proof;
    Usb_BulkPublishUnlock(saved);
    return SYS_OK;
}

//只接受同Session同Attempt下迟到的硬件完成；本接口不归还任何引用或掩码
SysResult_t BSP_USB_ReconcileBulkInStop(const BspUsbInIdentity_t *identity,
                                      uint32_t attempt, BspUsbInStopProof_t *output)
{
    uint8_t ep;
    uint32_t saved, ctl, events, reset;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    UsbBulkInStop_t *stop;
    BspUsbInStopProof_t *proof;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (identity == NULL || output == NULL || !s_ready || !Usb_BulkEndpoint(identity->endpoint_address))
        return SYS_ERR_INVALID_ARG;
    ep = identity->endpoint_address & 15U;
    stop = &s_bulk_in[ep]; proof = &stop->proof;
    saved = Usb_BulkPublishLock();
    if (attempt == 0U || attempt != proof->stop_attempt || !Usb_BulkIdentityMatches(identity, &proof->transfer))
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    *output = *proof;
    if (proof->stage != BSP_USB_STOP_FAULT || proof->first_error != SYS_ERR_TIMEOUT ||
        proof->error != SYS_ERR_TIMEOUT || proof->fifo_conflicted || proof->reconcile_count != 0U ||
        identity->session_generation != s_bulk_session || proof->pio_users != 0U ||
        (s_pcd.Instance->GINTSTS & (USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_OTGINT)) != 0U)
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    ctl = USBx_INEP(ep)->DIEPCTL; events = USBx_INEP(ep)->DIEPINT;
    reset = s_pcd.Instance->GRSTCTL;
    if ((ctl & (USB_OTG_DIEPCTL_EPENA | USB_OTG_DIEPCTL_EPDIS)) != 0U ||
        (reset & USB_OTG_GRSTCTL_AHBIDL) == 0U ||
        (reset & (USB_OTG_GRSTCTL_TXFFLSH | USB_OTG_GRSTCTL_RXFFLSH)) != 0U)
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_BUSY; }
    if (proof->failed_stage == BSP_USB_STOP_DISABLE && s_bulk_fifo_owner == 0U &&
        (events & USB_OTG_DIEPINT_EPDISD) != 0U)
    {
        Usb_BulkCapture(ep, events);
        proof->stage = BSP_USB_STOP_AHB;
    }
    else if (proof->failed_stage == BSP_USB_STOP_FIFO && s_bulk_fifo_owner == ep &&
             ((reset & USB_OTG_GRSTCTL_TXFNUM) >> USB_OTG_GRSTCTL_TXFNUM_Pos) == ep)
    {
        Usb_BulkCapture(ep, events);
        proof->fifo_complete = true;
        proof->fifo_result = SYS_OK;
        proof->stage = BSP_USB_STOP_RETIRE;
        s_bulk_fifo_owner = 0U;
    }
    else
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    ++proof->reconcile_count;
    proof->state = BSP_USB_IN_QUIESCING;
    proof->result = BSP_USB_STOP_PENDING;
    //保留first_error/failed_stage/failure_count；仅给已证明可继续的收尾一个新预算
    proof->error = SYS_OK;
    stop->began_at = HAL_GetTick(); stop->poll_count = 0U;
    *output = *proof;
    Usb_BulkPublishUnlock(saved);
    return SYS_OK;
}

SysResult_t BSP_USB_CommitBulkInRetirement(const BspUsbInIdentity_t *identity,
                                         uint32_t attempt, BspUsbInStopProof_t *output)
{
    uint8_t ep;
    uint32_t bit;
    uint32_t saved;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    BspUsbInStopProof_t *proof;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (identity == NULL || output == NULL || !s_ready || !Usb_BulkEndpoint(identity->endpoint_address))
        return SYS_ERR_INVALID_ARG;
    ep = identity->endpoint_address & 0x0FU;
    proof = &s_bulk_in[ep].proof;
    saved = Usb_BulkPublishLock();
    if (attempt == 0U || attempt != proof->stop_attempt || !Usb_BulkIdentityMatches(identity, &proof->transfer))
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    if (proof->stage == BSP_USB_STOP_COMMITTED)
    { *output = *proof; Usb_BulkPublishUnlock(saved); return SYS_OK; }
    if (proof->stage != BSP_USB_STOP_PROVEN || !proof->fifo_complete || !proof->events_retired ||
        proof->pio_users != 0U || identity->session_generation != s_bulk_session ||
        (USBx_INEP(ep)->DIEPCTL & USB_OTG_DIEPCTL_EPENA) != 0U ||
        (USBx_INEP(ep)->DIEPINT & USB_IN_STOP_TERMINAL_EVENTS) != 0U ||
        (USBx_INEP(ep)->DIEPINT & USBx_DEVICE->DIEPMSK & USB_IN_STOP_EVENTS) != 0U ||
        (s_pcd.Instance->GINTSTS & USB_OTG_GINTSTS_USBRST) != 0U)
    {
        Usb_BulkFault(ep, SYS_ERR_INVALID_STATE);
        *output = *proof;
        Usb_BulkPublishUnlock(saved);
        return SYS_ERR_INVALID_STATE;
    }
    if (proof->source_owned) ++proof->source_returns;
    if (proof->pool_owned) ++proof->pool_returns;
    proof->source_owned = false;
    proof->pool_owned = false;
    memset(&s_tx_transfer[ep], 0, sizeof(s_tx_transfer[ep]));
    s_pcd.IN_ep[ep].xfer_buff = NULL;
    s_pcd.IN_ep[ep].xfer_len = 0U;
    s_pcd.IN_ep[ep].xfer_count = 0U;
    proof->stage = BSP_USB_STOP_COMMITTED;
    proof->state = BSP_USB_IN_IDLE;
    s_bulk_in[ep].fenced = false;
    bit = 1UL << ep;
    USBx_DEVICE->DAINTMSK = (USBx_DEVICE->DAINTMSK & ~bit) | proof->saved_daint_mask;
    USBx_DEVICE->DIEPEMPMSK = (USBx_DEVICE->DIEPEMPMSK & ~bit) | proof->saved_empty_mask;
    *output = *proof;
    Usb_BulkPublishUnlock(saved);
    return SYS_OK;
}

//OUT使用独立身份、引用及全局NAK lease；不复用IN完成标志。
#define USB_OUT_TERMINAL (USB_OTG_DOEPINT_XFRC | USB_OTG_DOEPINT_EPDISD)
typedef struct
{
    BspUsbOutStopProof_t proof;
    uint32_t began_at, budget, polls;
} UsbOutSlot_t;
typedef struct
{
    uint32_t cookie, sequence, events;
    bool active, forward, deferred;
} UsbOutEvent_t;
typedef struct
{
    uint32_t id, bulk_mask, iso_mask, marker, saved_nak_mask, saved_disable_mask;
    bool effective, fault, releasing;
} UsbOutLease_t;
static UsbOutSlot_t s_out_slot[BSP_USB_ENDPOINT_COUNT];
static UsbOutEvent_t s_out_event[BSP_USB_ENDPOINT_COUNT];
static UsbOutLease_t s_out_lease;
static uint32_t s_out_controller, s_out_session = 1U;
static uint32_t s_out_endpoint_seed[BSP_USB_ENDPOINT_COUNT], s_out_transfer_seed[BSP_USB_ENDPOINT_COUNT];
static uint32_t s_out_attempt_seed, s_out_lease_seed, s_out_rx_seed, s_out_event_seed;
static uint32_t s_out_boundary_latch, s_out_copy_cookie, s_out_copy_ep, s_out_rx_pending;
static bool s_out_copy_active, s_out_session_tainted, s_out_bind_window;
static uint32_t Usb_OutRxBegin(PCD_HandleTypeDef *, uint32_t, uint8_t *, uint16_t, uint32_t *);
static void Usb_OutRxEnd(PCD_HandleTypeDef *, uint32_t, uint32_t, uint16_t, uint32_t);
static void Usb_OutDrainDeferred(PCD_HandleTypeDef *);
static uint32_t Usb_OutEventBegin(PCD_HandleTypeDef *, uint8_t, uint32_t, uint32_t *);
static void Usb_OutEventEnd(PCD_HandleTypeDef *, uint8_t, uint32_t, uint32_t);
static void Usb_OutNak(PCD_HandleTypeDef *, uint32_t, uint8_t);
static void Usb_OutBoundary(PCD_HandleTypeDef *, uint32_t);
static uint32_t Usb_OutCanDetach(PCD_HandleTypeDef *);
static const PCD_LifecycleHooks s_out_hooks =
{
    PCD_LIFECYCLE_VERSION, Usb_OutRxBegin, Usb_OutRxEnd,
    Usb_OutEventBegin, Usb_OutEventEnd, Usb_OutNak, Usb_OutBoundary, Usb_OutCanDetach
};
static bool Usb_OutEnabled(void) { return s_pcd.Lifecycle == &s_out_hooks; }
static bool Usb_OutTarget(uint8_t ep)
{
    return Usb_ValidEp(ep) && ep != 0U && (ep & 0x80U) == 0U &&
           s_pcd.OUT_ep[ep].maxpacket != 0U && s_pcd.OUT_ep[ep].type == EP_TYPE_BULK;
}
static bool Usb_OutMemory(uint8_t *buffer, uint32_t count)
{
    uint32_t first = (uint32_t)buffer;
    const uint32_t base[] = {0x20000000U, 0x24000000U, 0x30000000U, 0x38000000U};
    const uint32_t end[]  = {0x20020000U, 0x24080000U, 0x30048000U, 0x38010000U};
    uint32_t i;
    if (buffer == NULL) return count == 0U;
    for (i = 0U; i < 4U; ++i)
        if (first >= base[i] && first < end[i] && count <= end[i] - first) return true;
    return false;
}
static void Usb_OutFault(uint8_t ep, SysResult_t error)
{
    BspUsbOutStopProof_t *p = &s_out_slot[ep].proof;
    if (p->first_error == SYS_OK) { p->first_error = error; p->failed_stage = p->stage; }
    {
        bool changed = p->stage != BSP_USB_OUT_FAULT || p->error != error;
        p->error = error; p->stage = BSP_USB_OUT_FAULT; p->fenced = true;
        if (changed && s_callbacks != NULL && s_callbacks->out_fault != NULL)
            s_callbacks->out_fault(error);
    }
}
bool BSP_USB_BulkOutHasReferences(void)
{
    uint32_t ep;
    if (s_out_lease.id != 0U || s_out_lease.fault || s_out_copy_active || s_out_rx_pending != 0U) return true;
    for (ep = 0U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
        if (s_out_slot[ep].proof.buffer_owned || s_out_slot[ep].proof.fenced ||
            s_out_slot[ep].proof.pio_references != 0U || s_out_event[ep].active) return true;
    return false;
}
static void Usb_OutInvalidate(void)
{
    uint8_t ep;
    bool owned = BSP_USB_BulkOutHasReferences();
    if (s_out_session != UINT32_MAX) ++s_out_session;
    if (s_out_lease.id != 0U) s_out_lease.fault = true;
    if (owned) s_out_session_tainted = true;
    for (ep = 0U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
        if (s_out_slot[ep].proof.buffer_owned || s_out_slot[ep].proof.fenced)
            Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
}
static void Usb_OutBoundary(PCD_HandleTypeDef *pcd, uint32_t raw)
{
    uint32_t boundary = (raw & USB_OTG_GINTSTS_USBRST) != 0U ? 1U : 0U;
    if ((raw & USB_OTG_GINTSTS_OTGINT) != 0U &&
        (pcd->Instance->GOTGINT & USB_OTG_GOTGINT_SEDET) != 0U) boundary |= 2U;
    if ((boundary & ~s_out_boundary_latch) != 0U) Usb_OutInvalidate();
    s_out_boundary_latch = boundary;
    //生效后、合法解除前，任何已观测的屏障丢失都不可复活为同一lease。
    if (s_out_lease.id != 0U && s_out_lease.effective && !s_out_lease.releasing && !s_out_lease.fault)
    {
        uint32_t ep; uint32_t USBx_BASE = (uint32_t)pcd->Instance;
        if ((raw & USB_OTG_GINTSTS_BOUTNAKEFF) == 0U || (USBx_DEVICE->DCTL & USB_OTG_DCTL_GONSTS) == 0U)
        {
            s_out_lease.fault = true;
            for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
                if (((s_out_lease.bulk_mask | s_out_lease.iso_mask) & (1UL << ep)) != 0U)
                    Usb_OutFault((uint8_t)ep, SYS_ERR_INVALID_STATE);
        }
    }
}
static uint32_t Usb_OutCanDetach(PCD_HandleTypeDef *pcd)
{
    uint32_t ep; uint32_t USBx_BASE = (uint32_t)pcd->Instance;
    if (BSP_USB_BulkOutHasReferences() ||
        (pcd->Instance->GINTSTS & USB_OTG_GINTSTS_RXFLVL) != 0U) return 0U;
    for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
        if ((USBx_OUTEP(ep)->DOEPCTL & USB_OTG_DOEPCTL_EPENA) != 0U ||
            (USBx_OUTEP(ep)->DOEPINT & USB_OUT_TERMINAL) != 0U) return 0U;
    return 1U;
}
SysResult_t BSP_USB_EnableBulkOutLifecycle(void)
{
    uint32_t ep, saved; HAL_StatusTypeDef status;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    if (__get_IPSR() != 0U || !s_ready || s_started) return SYS_ERR_INVALID_STATE;
    if (Usb_OutEnabled()) return SYS_OK;
    if (!s_out_bind_window) return SYS_ERR_INVALID_STATE;
    if (s_pcd.Init.dma_enable != DISABLE) return SYS_ERR_NOT_SUPPORTED;
    if (s_out_controller == UINT32_MAX || s_out_session == UINT32_MAX) return SYS_ERR_NO_RESOURCE;
    saved = Usb_BulkPublishLock();
    if (BSP_USB_BulkOutHasReferences() ||
        (s_pcd.Instance->GINTSTS & (USB_OTG_GINTSTS_RXFLVL | USB_OTG_GINTSTS_BOUTNAKEFF)) != 0U ||
        (USBx_DEVICE->DCTL & USB_OTG_DCTL_GONSTS) != 0U ||
        (USBx_DEVICE->DCTL & USB_OTG_DCTL_SDIS) == 0U)
    { Usb_BulkPublishUnlock(saved); return SYS_ERR_BUSY; }
    for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
        if (s_pcd.OUT_ep[ep].maxpacket != 0U)
        { Usb_BulkPublishUnlock(saved); return SYS_ERR_BUSY; }
    status = HAL_PCD_SetLifecycleHooks(&s_pcd, &s_out_hooks);
    if (status == HAL_OK)
    {
        ++s_out_controller; s_out_session_tainted = false;
        memset(s_out_slot, 0, sizeof(s_out_slot)); memset(s_out_event, 0, sizeof(s_out_event));
    }
    Usb_BulkPublishUnlock(saved);
    return Usb_HalResult(status);
}
//调用者先持有短发布锁；不改变已有lease的身份，也不偷取未知硬件NAK。
static SysResult_t Usb_OutClaim(uint8_t ep, bool iso)
{
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    if (s_out_lease.id == 0U)
    {
        if (s_out_lease_seed == UINT32_MAX) return SYS_ERR_NO_RESOURCE;
        if ((s_pcd.Instance->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U ||
            (USBx_DEVICE->DCTL & USB_OTG_DCTL_GONSTS) != 0U) return SYS_ERR_BUSY;
        memset(&s_out_lease, 0, sizeof(s_out_lease));
        s_out_lease.id = ++s_out_lease_seed;
        s_out_lease.saved_nak_mask = s_pcd.Instance->GINTMSK & USB_OTG_GINTMSK_GONAKEFFM;
        s_out_lease.saved_disable_mask = USBx_DEVICE->DOEPMSK & USB_OTG_DOEPMSK_EPDM;
        s_pcd.Instance->GINTMSK |= USB_OTG_GINTMSK_GONAKEFFM;
        USBx_DEVICE->DOEPMSK |= USB_OTG_DOEPMSK_EPDM;
        USBx_DEVICE->DCTL |= USB_OTG_DCTL_SGONAK;
    }
    if (s_out_lease.fault || s_out_lease.releasing) return SYS_ERR_INVALID_STATE;
    if (iso) s_out_lease.iso_mask |= 1UL << ep;
    else s_out_lease.bulk_mask |= 1UL << ep;
    return SYS_OK;
}
static bool Usb_OutEffective(void)
{
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    return s_out_lease.id != 0U && !s_out_lease.fault && s_out_lease.effective &&
           s_out_lease.marker != 0U &&
           (s_pcd.Instance->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U &&
           (USBx_DEVICE->DCTL & USB_OTG_DCTL_GONSTS) != 0U;
}
//解除命令与硬件完成分开：一次检查，不在ISR/Task中轮询等待。
static void Usb_OutReleaseIfEmpty(void)
{
    uint32_t ep; uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    if (s_out_lease.id == 0U || s_out_lease.fault || s_out_lease.bulk_mask != 0U || s_out_lease.iso_mask != 0U) return;
    if (!s_out_lease.releasing)
    {
        if (!Usb_OutEffective()) return;
        s_out_lease.releasing = true;
        USBx_DEVICE->DCTL |= USB_OTG_DCTL_CGONAK;
        __DSB();
    }
    if ((s_pcd.Instance->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF) != 0U ||
        (USBx_DEVICE->DCTL & USB_OTG_DCTL_GONSTS) != 0U) return;
    for (ep = 1U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
        if (s_out_slot[ep].proof.lease_id == s_out_lease.id && s_out_slot[ep].proof.lease_drop_requested &&
            s_out_slot[ep].proof.transfer.session_generation == s_out_session)
            s_out_slot[ep].proof.lease_done = true;
    s_pcd.Instance->GINTMSK = (s_pcd.Instance->GINTMSK & ~USB_OTG_GINTMSK_GONAKEFFM) | s_out_lease.saved_nak_mask;
    USBx_DEVICE->DOEPMSK = (USBx_DEVICE->DOEPMSK & ~USB_OTG_DOEPMSK_EPDM) | s_out_lease.saved_disable_mask;
    memset(&s_out_lease, 0, sizeof(s_out_lease));
}
static void Usb_OutNak(PCD_HandleTypeDef *pcd, uint32_t action, uint8_t ep)
{
    uint32_t i; uint32_t USBx_BASE = (uint32_t)pcd->Instance;
    if (action == PCD_LC_NAK_EFFECTIVE)
        pcd->Instance->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
    if (action == PCD_LC_NAK_ISO_REQUEST)
    {
        if (ep == 0U || ep >= BSP_USB_ENDPOINT_COUNT) { s_out_lease.fault = true; return; }
        s_out_slot[ep].proof.fenced = true;
        if (Usb_OutClaim(ep, true) != SYS_OK) { Usb_OutFault(ep, SYS_ERR_BUSY); return; }
        s_out_slot[ep].proof.lease_id = s_out_lease.id;
    }
    if (!Usb_OutEffective()) return;
    pcd->Instance->GINTMSK &= ~USB_OTG_GINTMSK_GONAKEFFM;
    for (i = 1U; i < BSP_USB_ENDPOINT_COUNT; ++i)
        if ((s_out_lease.iso_mask & (1UL << i)) != 0U && !s_out_slot[i].proof.disable_requested)
        {
            if ((USBx_OUTEP(i)->DOEPCTL & USB_OTG_DOEPCTL_EPENA) == 0U)
            { Usb_OutFault((uint8_t)i, SYS_ERR_INVALID_STATE); s_out_lease.fault = true; continue; }
            s_out_slot[i].proof.disable_requested = true;
            USBx_OUTEP(i)->DOEPCTL |= USB_OTG_DOEPCTL_EPDIS | USB_OTG_DOEPCTL_SNAK;
        }
}
static uint32_t Usb_OutRxBegin(PCD_HandleTypeDef *pcd, uint32_t status, uint8_t *destination,
                               uint16_t count, uint32_t *cookie)
{
    uint32_t ep = status & USB_OTG_GRXSTSP_EPNUM, kind = (status & USB_OTG_GRXSTSP_PKTSTS) >> 17;
    BspUsbOutStopProof_t *p;
    *cookie = 0U;
    if (ep >= BSP_USB_ENDPOINT_COUNT || s_out_rx_seed == UINT32_MAX || s_out_copy_active || s_out_rx_pending != 0U)
    { s_out_lease.fault = true; return 0U; }
    *cookie = ++s_out_rx_seed; s_out_rx_pending = *cookie;
    p = &s_out_slot[ep].proof;
    if (kind == 1U && count == 0U)
    {
        if (s_out_lease.id == 0U || s_out_lease.fault) { s_out_lease.fault = true; return 0U; }
        return 1U;
    }
    if (kind == STS_SETUP_UPDT)
    {
        if (ep == 0U && p->buffer_owned) Usb_OutFault(0U, SYS_ERR_INVALID_STATE);
        if (ep != 0U || count != 8U || destination != (uint8_t *)pcd->Setup)
        { Usb_OutFault((uint8_t)ep, SYS_ERR_PROTOCOL); return 0U; }
    }
    else if (kind == STS_DATA_UPDT)
    {
        //EP0零长度状态没有内存写入，也可能由HAL专用Setup重装路径接收。
        if (ep == 0U && count == 0U) return 1U;
        if (!p->buffer_owned || p->transfer.session_generation != s_out_session ||
            p->received > p->capacity || count > p->capacity - p->received ||
            count > pcd->OUT_ep[ep].maxpacket ||
            destination != (uint8_t *)((uint32_t)p->buffer + p->received) || pcd->OUT_ep[ep].xfer_count != p->received ||
            !Usb_OutMemory(destination, count))
        { Usb_OutFault((uint8_t)ep, SYS_ERR_IO); return 0U; }
    }
    else
    {
        if (count != 0U || (kind != 3U && kind != 4U))
        { Usb_OutFault((uint8_t)ep, SYS_ERR_PROTOCOL); return 0U; }
        return 1U;
    }
    s_out_copy_active = true; s_out_copy_cookie = *cookie; s_out_copy_ep = ep;
    ++p->pio_references;
    return 1U;
}
static void Usb_OutRxEnd(PCD_HandleTypeDef *pcd, uint32_t status, uint32_t cookie,
                         uint16_t count, uint32_t copied)
{
    uint32_t ep = status & USB_OTG_GRXSTSP_EPNUM, kind = (status & USB_OTG_GRXSTSP_PKTSTS) >> 17;
    BspUsbOutStopProof_t *p;
    if (ep >= BSP_USB_ENDPOINT_COUNT) return;
    p = &s_out_slot[ep].proof;
    if (cookie == 0U || cookie != s_out_rx_pending)
    { Usb_OutFault((uint8_t)ep, SYS_ERR_INVALID_STATE); return; }
    s_out_rx_pending = 0U;
    if (s_out_copy_active && cookie == s_out_copy_cookie && ep == s_out_copy_ep)
    {
        if (p->pio_references != 0U) --p->pio_references;
        s_out_copy_active = false;
        if (copied != 0U && kind == STS_DATA_UPDT) p->received += count;
    }
    if (copied == 0U) { Usb_OutFault((uint8_t)ep, SYS_ERR_IO); return; }
    if (kind == 1U && s_out_lease.id != 0U && !s_out_lease.fault)
    { s_out_lease.marker = cookie; s_out_lease.effective = true; }
    Usb_OutBoundary(pcd, pcd->Instance->GINTSTS);
    Usb_OutDrainDeferred(pcd);
}
static uint32_t Usb_OutEventBegin(PCD_HandleTypeDef *pcd, uint8_t ep, uint32_t events, uint32_t *cookie)
{
    uint32_t USBx_BASE = (uint32_t)pcd->Instance;
    BspUsbOutStopProof_t *p;
    UsbOutEvent_t *e;
    *cookie = 0U;
    Usb_OutBoundary(pcd, pcd->Instance->GINTSTS);
    if (ep >= BSP_USB_ENDPOINT_COUNT) return 0U;
    p = &s_out_slot[ep].proof; e = &s_out_event[ep];
    if ((events & USB_OUT_TERMINAL) == 0U) return PCD_LC_FORWARD_XFRC;
    if (e->active || s_out_event_seed == UINT32_MAX)
    { Usb_OutFault(ep, SYS_ERR_NO_RESOURCE); return 0U; }
    e->cookie = ++s_out_event_seed; e->sequence = p->transfer.transfer_sequence;
    e->events = events; e->active = true; e->forward = false; e->deferred = false; *cookie = e->cookie;
    p->events |= events & USB_OUT_TERMINAL;
    if ((events & USB_OTG_DOEPINT_EPDISD) != 0U)
    {
        if (!p->disable_requested || p->lease_id != s_out_lease.id || !Usb_OutEffective())
            Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
        else p->disable_observed = true;
    }
    if ((events & USB_OTG_DOEPINT_XFRC) != 0U && p->fenced &&
        p->transfer.session_generation == s_out_session && p->pio_references == 0U &&
        (USBx_OUTEP(ep)->DOEPCTL & USB_OTG_DOEPCTL_EPENA) == 0U)
        p->normal_complete = true;
    if ((events & USB_OTG_DOEPINT_XFRC) != 0U && !p->fenced)
    {
        if (ep == 0U || (p->buffer_owned && p->transfer.session_generation == s_out_session &&
            pcd->OUT_ep[ep].xfer_count == p->received &&
            pcd->OUT_ep[ep].xfer_buff == (uint8_t *)((uint32_t)p->buffer + p->received) &&
            p->pio_references == 0U && (events & USB_OTG_DOEPINT_EPDISD) == 0U &&
            (USBx_OUTEP(ep)->DOEPCTL & USB_OTG_DOEPCTL_EPENA) == 0U))
        {
            p->normal_complete = true;
            e->deferred = (pcd->Instance->GINTSTS & USB_OTG_GINTSTS_RXFLVL) != 0U;
            e->forward = !e->deferred;
        }
        else Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
    }
    return e->forward ? PCD_LC_FORWARD_XFRC : 0U;
}
static void Usb_OutEventEnd(PCD_HandleTypeDef *pcd, uint8_t ep, uint32_t events, uint32_t cookie)
{
    uint32_t USBx_BASE = (uint32_t)pcd->Instance;
    UsbOutEvent_t *e;
    BspUsbOutStopProof_t *p;
    bool iso_done = false;
    if (ep >= BSP_USB_ENDPOINT_COUNT || cookie == 0U) return;
    Usb_OutBoundary(pcd, pcd->Instance->GINTSTS);
    e = &s_out_event[ep]; p = &s_out_slot[ep].proof;
    if (!e->active || e->cookie != cookie) { Usb_OutFault(ep, SYS_ERR_INVALID_STATE); return; }
    if (!e->deferred) e->active = false;
    if (e->sequence != p->transfer.transfer_sequence) { e->active = false; return; } //普通回调已安全rearm，不改新Transfer
    if ((USBx_OUTEP(ep)->DOEPINT & (events & USB_OUT_TERMINAL)) != 0U)
    { Usb_OutFault(ep, SYS_ERR_IO); return; }
    p->events_retired = true;
    if (e->deferred) { Usb_OutDrainDeferred(pcd); return; }
    if ((s_out_lease.iso_mask & (1UL << ep)) != 0U && p->disable_observed &&
        p->transfer.session_generation == s_out_session && p->error == SYS_OK && p->pio_references == 0U && Usb_OutEffective() &&
        (USBx_OUTEP(ep)->DOEPCTL & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)) == 0U)
    {
        if (p->buffer_owned) ++p->source_returns;
        p->buffer_owned = false; p->fenced = false;
        s_out_lease.iso_mask &= ~(1UL << ep);
        pcd->OUT_ep[ep].is_iso_incomplete = 0U;
        iso_done = true;
        Usb_OutReleaseIfEmpty();
    }
    if (iso_done) HAL_PCD_ISOOUTIncompleteCallback(pcd, ep);
}
//真实终止事件已W1C但RX状态尚未消费时，保留其身份和Buffer，不提前回调rearm。
static void Usb_OutDrainDeferred(PCD_HandleTypeDef *pcd)
{
    uint8_t ep;
    uint32_t USBx_BASE = (uint32_t)pcd->Instance;
    if (s_out_rx_pending != 0U || s_out_copy_active ||
        (pcd->Instance->GINTSTS & USB_OTG_GINTSTS_RXFLVL) != 0U) return;
    for (ep = 0U; ep < BSP_USB_ENDPOINT_COUNT; ++ep)
    {
        UsbOutEvent_t *e = &s_out_event[ep];
        BspUsbOutStopProof_t *p = &s_out_slot[ep].proof;
        if (!e->active || !e->deferred || !p->events_retired) continue;
        if (e->sequence != p->transfer.transfer_sequence || p->error != SYS_OK ||
            p->transfer.session_generation != s_out_session || p->pio_references != 0U ||
            (USBx_OUTEP(ep)->DOEPCTL & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)) != 0U ||
            (USBx_OUTEP(ep)->DOEPINT & USB_OUT_TERMINAL) != 0U)
        { Usb_OutFault(ep, SYS_ERR_INVALID_STATE); continue; }
        if (!p->fenced)
        {
            e->forward = true; e->deferred = false;
            HAL_PCD_DataOutStageCallback(pcd, ep);
        }
        //回调可重装新Transfer；这里只退休旧事件容器，不改变新Transfer记录。
        e->active = false; e->deferred = false;
    }
}
static SysResult_t Usb_OutArm(uint8_t ep, uint8_t *buffer, uint32_t capacity)
{
    BspUsbOutStopProof_t *p = &s_out_slot[ep].proof;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    Usb_OutBoundary(&s_pcd, s_pcd.Instance->GINTSTS);
    if ((s_pcd.Instance->GINTSTS & USB_OTG_GINTSTS_USBRST) != 0U ||
        (s_pcd.Instance->GOTGINT & USB_OTG_GOTGINT_SEDET) != 0U) return SYS_ERR_INVALID_STATE;
    Usb_OutReleaseIfEmpty();
    if (s_out_session_tainted || s_out_lease.fault || p->fenced || p->pio_references != 0U || p->buffer_owned)
        return SYS_ERR_BUSY;
    if (!Usb_OutMemory(buffer, capacity)) return SYS_ERR_INVALID_ARG;
    if (s_out_transfer_seed[ep] == UINT32_MAX || s_out_session == UINT32_MAX) return SYS_ERR_NO_RESOURCE;
    if (ep != 0U && (USBx_OUTEP(ep)->DOEPINT & USB_OUT_TERMINAL) != 0U)
    { Usb_OutFault(ep, SYS_ERR_INVALID_STATE); return SYS_ERR_INVALID_STATE; }
    memset(p, 0, sizeof(*p));
    p->transfer.controller_generation = s_out_controller; p->transfer.session_generation = s_out_session;
    p->transfer.endpoint_generation = s_out_endpoint_seed[ep];
    p->transfer.transfer_sequence = ++s_out_transfer_seed[ep]; p->transfer.endpoint_address = ep;
    p->buffer = buffer; p->capacity = capacity; p->buffer_owned = true;
    return SYS_OK;
}
SysResult_t BSP_USB_GetBulkOutProof(uint8_t ep, BspUsbOutStopProof_t *output)
{
    uint32_t saved;
    if (output == NULL || !s_ready || !Usb_OutEnabled() || !Usb_ValidEp(ep) ||
        (ep & 0x80U) != 0U || s_pcd.OUT_ep[ep].maxpacket == 0U) return SYS_ERR_INVALID_ARG;
    saved = Usb_BulkPublishLock(); *output = s_out_slot[ep].proof; Usb_BulkPublishUnlock(saved); return SYS_OK;
}
SysResult_t BSP_USB_BeginBulkOutStop(const BspUsbOutIdentity_t *id, uint32_t timeout, BspUsbOutStopProof_t *output)
{
    uint32_t saved; uint8_t ep; SysResult_t result;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    BspUsbOutStopProof_t *p;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (id == NULL || output == NULL || !s_ready || !Usb_OutEnabled() || !Usb_OutTarget(id->endpoint_address) ||
        timeout == 0U || timeout > 1000U) return SYS_ERR_INVALID_ARG;
    ep = id->endpoint_address; p = &s_out_slot[ep].proof; saved = Usb_BulkPublishLock();
    if (!Usb_BulkIdentityMatches(id, &p->transfer)) { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    if (p->stage != BSP_USB_OUT_NONE) { *output = *p; Usb_BulkPublishUnlock(saved); return SYS_OK; }
    if (s_out_attempt_seed == UINT32_MAX)
    { Usb_OutFault(ep, SYS_ERR_NO_RESOURCE); *output = *p; Usb_BulkPublishUnlock(saved); return SYS_ERR_NO_RESOURCE; }
    p->stop_attempt = ++s_out_attempt_seed; p->fenced = true; p->stage = BSP_USB_OUT_WAIT_NAK;
    s_out_slot[ep].began_at = HAL_GetTick(); s_out_slot[ep].budget = timeout; s_out_slot[ep].polls = 0U;
    result = Usb_OutClaim(ep, false);
    if (result != SYS_OK) Usb_OutFault(ep, result);
    else p->lease_id = s_out_lease.id;
    if ((USBx_OUTEP(ep)->DOEPINT & USB_OTG_DOEPINT_EPDISD) != 0U) Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
    *output = *p; Usb_BulkPublishUnlock(saved); return SYS_OK;
}
static bool Usb_OutIdentity(uint8_t ep, const BspUsbOutIdentity_t *id, uint32_t attempt)
{
    return attempt != 0U && attempt == s_out_slot[ep].proof.stop_attempt &&
           Usb_BulkIdentityMatches(id, &s_out_slot[ep].proof.transfer);
}
SysResult_t BSP_USB_PollBulkOutStop(const BspUsbOutIdentity_t *id, uint32_t attempt, BspUsbOutStopProof_t *output)
{
    uint32_t saved, ctl; uint8_t ep;
    uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    UsbOutSlot_t *slot; BspUsbOutStopProof_t *p;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (id == NULL || output == NULL || !s_ready || !Usb_OutEnabled() || !Usb_OutTarget(id->endpoint_address)) return SYS_ERR_INVALID_ARG;
    ep = id->endpoint_address; slot = &s_out_slot[ep]; p = &slot->proof; saved = Usb_BulkPublishLock();
    if (!Usb_OutIdentity(ep, id, attempt)) { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    if (p->stage == BSP_USB_OUT_FAULT || p->stage == BSP_USB_OUT_COMMITTED)
    { *output = *p; Usb_BulkPublishUnlock(saved); return SYS_OK; }
    Usb_OutBoundary(&s_pcd, s_pcd.Instance->GINTSTS);
    ctl = USBx_OUTEP(ep)->DOEPCTL;
    if (p->stage != BSP_USB_OUT_FAULT)
    {
        if (id->session_generation != s_out_session || s_out_lease.fault)
            Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
        else if (++slot->polls >= 4096U || (uint32_t)(HAL_GetTick() - slot->began_at) >= slot->budget)
            Usb_OutFault(ep, SYS_ERR_TIMEOUT);
        else if (!p->lease_done && (p->lease_id != s_out_lease.id ||
                 (!p->lease_drop_requested && (s_out_lease.bulk_mask & (1UL << ep)) == 0U))) Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
        else if (p->stage == BSP_USB_OUT_WAIT_NAK && Usb_OutEffective())
        {
            p->barrier_sequence = s_out_lease.marker;
            if ((ctl & USB_OTG_DOEPCTL_EPENA) != 0U)
            {
                p->disable_requested = true;
                USBx_OUTEP(ep)->DOEPCTL |= USB_OTG_DOEPCTL_EPDIS | USB_OTG_DOEPCTL_SNAK;
                p->stage = BSP_USB_OUT_DISABLE;
            }
            else if ((p->normal_complete && p->events_retired) || id->transfer_sequence == 0U)
                p->stage = BSP_USB_OUT_DRAIN;
            else Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
        }
        else if (p->stage == BSP_USB_OUT_DISABLE && p->disable_observed && p->events_retired &&
                 (ctl & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)) == 0U)
            p->stage = BSP_USB_OUT_DRAIN;
        else if (p->stage == BSP_USB_OUT_DRAIN && p->pio_references == 0U && !s_out_copy_active &&
                 !s_out_event[ep].active &&
                 (ctl & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)) == 0U &&
                 (USBx_OUTEP(ep)->DOEPINT & USB_OUT_TERMINAL) == 0U)
        {
            p->events_retired = true;
            if (!p->lease_drop_requested)
            {
                if (!Usb_OutEffective()) Usb_OutFault(ep, SYS_ERR_INVALID_STATE);
                else
                {
                    p->lease_drop_requested = true;
                    s_out_lease.bulk_mask &= ~(1UL << ep);
                    if (s_out_lease.bulk_mask != 0U || s_out_lease.iso_mask != 0U) p->lease_done = true;
                }
            }
            if (p->stage != BSP_USB_OUT_FAULT)
            {
                Usb_OutReleaseIfEmpty();
                if (s_out_lease.fault) Usb_OutFault(ep, SYS_ERR_IO);
                else if (p->lease_done) p->stage = BSP_USB_OUT_PROVEN;
            }
        }
    }
    *output = *p; Usb_BulkPublishUnlock(saved); return SYS_OK;
}
SysResult_t BSP_USB_CommitBulkOutRetirement(const BspUsbOutIdentity_t *id, uint32_t attempt, BspUsbOutStopProof_t *output)
{
    uint32_t saved; uint8_t ep; uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
    BspUsbOutStopProof_t *p;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (id == NULL || output == NULL || !s_ready || !Usb_OutEnabled() || !Usb_OutTarget(id->endpoint_address)) return SYS_ERR_INVALID_ARG;
    ep = id->endpoint_address; p = &s_out_slot[ep].proof; saved = Usb_BulkPublishLock();
    if (!Usb_OutIdentity(ep, id, attempt)) { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    if (p->stage == BSP_USB_OUT_COMMITTED) { *output = *p; Usb_BulkPublishUnlock(saved); return SYS_OK; }
    Usb_OutBoundary(&s_pcd, s_pcd.Instance->GINTSTS);
    if (p->stage != BSP_USB_OUT_PROVEN || !p->lease_done || p->pio_references != 0U ||
        s_out_copy_active || s_out_event[ep].active || id->session_generation != s_out_session ||
        (USBx_OUTEP(ep)->DOEPCTL & (USB_OTG_DOEPCTL_EPENA | USB_OTG_DOEPCTL_EPDIS)) != 0U ||
        (USBx_OUTEP(ep)->DOEPINT & USB_OUT_TERMINAL) != 0U)
    { Usb_OutFault(ep, SYS_ERR_INVALID_STATE); *output = *p; Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    if (p->buffer_owned) ++p->source_returns;
    p->buffer_owned = false; p->fenced = false; p->stage = BSP_USB_OUT_COMMITTED;
    s_rx_active[ep] = false; s_pcd.OUT_ep[ep].xfer_buff = NULL;
    s_pcd.OUT_ep[ep].xfer_len = 0U; s_pcd.OUT_ep[ep].xfer_count = 0U;
    *output = *p; Usb_BulkPublishUnlock(saved); return SYS_OK;
}
SysResult_t BSP_USB_ReconcileBulkOutStop(const BspUsbOutIdentity_t *id, uint32_t attempt, BspUsbOutStopProof_t *output)
{
    uint8_t ep; uint32_t saved;
    BspUsbOutStopProof_t *p;
    if (__get_IPSR() != 0U) return SYS_ERR_INVALID_STATE;
    if (id == NULL || output == NULL || !s_ready || !Usb_OutEnabled() || !Usb_OutTarget(id->endpoint_address)) return SYS_ERR_INVALID_ARG;
    ep = id->endpoint_address; p = &s_out_slot[ep].proof; saved = Usb_BulkPublishLock();
    if (!Usb_OutIdentity(ep, id, attempt)) { Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    Usb_OutBoundary(&s_pcd, s_pcd.Instance->GINTSTS);
    Usb_OutReleaseIfEmpty();
    if (p->stage != BSP_USB_OUT_FAULT || p->first_error != SYS_ERR_TIMEOUT || p->error != SYS_ERR_TIMEOUT ||
        p->reconcile_count != 0U || id->session_generation != s_out_session || p->pio_references != 0U ||
        s_out_lease.fault ||
        !((p->failed_stage == BSP_USB_OUT_DRAIN && p->lease_done && p->lease_drop_requested) ||
          ((p->failed_stage == BSP_USB_OUT_WAIT_NAK || p->failed_stage == BSP_USB_OUT_DISABLE) &&
           p->lease_id == s_out_lease.id && Usb_OutEffective())))
    { *output = *p; Usb_BulkPublishUnlock(saved); return SYS_ERR_INVALID_STATE; }
    ++p->reconcile_count; p->error = SYS_OK; p->stage = p->failed_stage;
    s_out_slot[ep].began_at = HAL_GetTick(); s_out_slot[ep].polls = 0U;
    *output = *p; Usb_BulkPublishUnlock(saved); return SYS_OK;
}

SysResult_t BSP_USB_Init(void)
{
    HAL_StatusTypeDef status;
    SysResult_t result;
    if (s_ready)
        return SYS_OK;

    if (s_bulk_controller == UINT32_MAX) return SYS_ERR_NO_RESOURCE;
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

    //开启1ms SOF事件，BSP只向上转交
    s_pcd.Init.Sof_enable = ENABLE;

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
    ++s_bulk_controller;
    s_out_bind_window = true;
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
    s_out_bind_window = false;
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
    if (BSP_USB_BulkInHasReferences() || BSP_USB_BulkOutHasReferences() || s_bulk_fifo_owner != 0U) return SYS_ERR_BUSY;
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
    if (!Usb_ValidEp(ep)) return SYS_ERR_INVALID_ARG;
    if ((ep & 0x80U) != 0U && (s_bulk_in[ep & 0x0FU].fenced ||
        s_bulk_in[ep & 0x0FU].proof.source_owned || s_bulk_in[ep & 0x0FU].proof.pool_owned)) return SYS_ERR_BUSY;
    if ((ep & 0x80U) != 0U && type == EP_TYPE_BULK &&
        s_bulk_endpoint_seed[ep & 0x0FU] == UINT32_MAX) return SYS_ERR_NO_RESOURCE;
    result = Usb_ValidateEp(ep, type, size);
    if (result != SYS_OK)
        return result;
    if ((ep & 0x80U) != 0U && (size + 3U) / 4U > s_tx_fifo_words[ep & 0x0FU])
        return SYS_ERR_NO_RESOURCE;
    if (Usb_OutEnabled() && (ep & 0x80U) == 0U)
    {
        if (s_out_slot[ep].proof.buffer_owned || s_out_slot[ep].proof.fenced || s_out_event[ep].active)
            return SYS_ERR_BUSY;
        if (s_out_endpoint_seed[ep] == UINT32_MAX) return SYS_ERR_NO_RESOURCE;
    }
    entry = Usb_PcdEp(ep);
    previous = *entry;
    status = HAL_PCD_EP_Open(&s_pcd, ep, size, type);

    //HAL先写上下文再检查锁，失败时恢复，避免残留虚假占用
    if (status != HAL_OK)
        *entry = previous;
    if (status == HAL_OK && (ep & 0x80U) != 0U && type == EP_TYPE_BULK)
    {
        uint8_t index = ep & 0x0FU;
        memset(&s_bulk_in[index], 0, sizeof(s_bulk_in[index]));
        s_bulk_in[index].proof.transfer.controller_generation = s_bulk_controller;
        s_bulk_in[index].proof.transfer.session_generation = s_bulk_session;
        s_bulk_in[index].proof.transfer.endpoint_generation = ++s_bulk_endpoint_seed[index];
        s_bulk_in[index].proof.transfer.endpoint_address = ep;
        s_bulk_in[index].proof.state = BSP_USB_IN_IDLE;
    }
    if (status == HAL_OK && Usb_OutEnabled() && (ep & 0x80U) == 0U)
    {
        BspUsbOutStopProof_t *p = &s_out_slot[ep].proof;
        memset(p, 0, sizeof(*p));
        p->transfer.controller_generation = s_out_controller; p->transfer.session_generation = s_out_session;
        p->transfer.endpoint_generation = ++s_out_endpoint_seed[ep]; p->transfer.endpoint_address = ep;
    }
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
    if (s_bulk_fifo_owner != 0U) return SYS_ERR_BUSY;
    if (Usb_OutEnabled() && (ep & 0x80U) == 0U &&
        (s_out_slot[ep].proof.buffer_owned || s_out_slot[ep].proof.fenced || s_out_event[ep].active)) return SYS_ERR_BUSY;
    entry = Usb_PcdEp(ep);
    if (entry->maxpacket == 0U)
        return SYS_ERR_INVALID_STATE;
    //EP0由Core关闭，不经过仅供数据端点使用的Abort接口
    if ((ep & 0x0FU) == 0U)
    {
        uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
        uint32_t interrupt = (ep & 0x80U) != 0U ? 1U : (1UL << 16);

        //先屏蔽本方向，取消失败也不把旧完成交给后续事务
        USBx_DEVICE->DAINTMSK &= ~interrupt;
        if ((ep & 0x80U) != 0U)
            USBx_DEVICE->DIEPEMPMSK &= ~1UL;
        status = HAL_PCD_EP_Abort(&s_pcd, ep);
        if (status != HAL_OK)
            return Usb_HalResult(status);

        //IN只清TX0，OUT不清共享RX，避免影响其他端点
        if ((ep & 0x80U) != 0U)
        {
            status = HAL_PCD_EP_Flush(&s_pcd, ep);
            if (status != HAL_OK)
                return Usb_HalResult(status);
        }

        //确认停止后按W1C应答旧事务；不清SETUP位或其他端点
        if ((ep & 0x80U) != 0U)
            USBx_INEP(0U)->DIEPINT = USB_OTG_DIEPINT_XFRC |
                USB_OTG_DIEPINT_EPDISD | USB_OTG_DIEPINT_TOC;
        else
            USBx_OUTEP(0U)->DOEPINT = USB_OTG_DOEPINT_XFRC |
                USB_OTG_DOEPINT_EPDISD;
        __DSB();
    }
    else
    {
        result = BSP_USB_AbortEp(ep);
        if (result != SYS_OK)
            return result;
    }
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
    if (Usb_OutEnabled() && (ep & 0x80U) == 0U &&
        (s_out_slot[index].proof.buffer_owned || s_out_slot[index].proof.fenced || s_out_event[index].active)) return SYS_ERR_BUSY;
    if (Usb_BulkEndpoint(ep))
        return s_bulk_in[index].fenced || s_bulk_in[index].proof.source_owned ||
               s_bulk_in[index].proof.pool_owned ? SYS_ERR_BUSY : SYS_OK;
    if (s_bulk_fifo_owner != 0U) return SYS_ERR_BUSY;
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
    {
        uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;

        //停止并清FIFO后只应答本端点旧事件，保留其他端点
        USBx_INEP(index)->DIEPINT = USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD | USB_OTG_DIEPINT_TOC;
        s_pcd.IN_ep[index].is_iso_incomplete = 0U;
        __DSB();
        memset(&s_tx_transfer[index], 0, sizeof(s_tx_transfer[index]));
    }
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
    if (BSP_USB_BulkInHasReferences() || BSP_USB_BulkOutHasReferences() || s_bulk_fifo_owner != 0U) return SYS_ERR_BUSY;
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
    if (s_bulk_fifo_owner != 0U || (Usb_OutEnabled() && BSP_USB_BulkOutHasReferences())) return SYS_ERR_BUSY;
    //成套OUT模式不提供单端点RX Flush；HAL专用Setup接收不一定持有普通Buffer记录。
    if (Usb_OutEnabled() && (ep & 0x80U) == 0U) return SYS_ERR_NOT_SUPPORTED;
    if (!s_ready || !Usb_ValidEp(ep))
        return SYS_ERR_INVALID_ARG;
    if ((ep & 0x80U) != 0U && s_bulk_in[ep & 0x0FU].fenced) return SYS_ERR_BUSY;
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

    if ((ep & 0x80U) != 0U && s_bulk_in[ep & 0x0FU].fenced) return SYS_ERR_BUSY;
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

    if ((ep & 0x80U) != 0U && s_bulk_in[ep & 0x0FU].fenced) return SYS_ERR_BUSY;
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
    bool bulk = ep != 0U && s_pcd.IN_ep[ep].type == EP_TYPE_BULK;
    if (bulk) ++s_bulk_in[ep].proof.pio_users;
    if (length != 0U) memcpy(s_tx_pio_pool[ep], source, length);
    if (padded > length) memset(&s_tx_pio_pool[ep][length], 0, padded - length);
    if (bulk) --s_bulk_in[ep].proof.pio_users;
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
    if (transfer->active || (index != 0U && s_bulk_in[index].fenced)) return SYS_ERR_BUSY;
    if (entry->type == EP_TYPE_BULK && index != 0U &&
        (s_bulk_session == UINT32_MAX || s_bulk_transfer_seed[index] == UINT32_MAX)) return SYS_ERR_NO_RESOURCE;
    if (entry->type == EP_TYPE_ISOC && size > entry->maxpacket) return SYS_ERR_INVALID_ARG;
    if (size > 0x7FFFFU || (index != 0U && size > entry->maxpacket * 1023U))
        return SYS_ERR_INVALID_ARG;

    if (entry->type == EP_TYPE_BULK && index != 0U)
    {
        uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
        if ((USBx_INEP(index)->DIEPINT & USB_IN_STOP_TERMINAL_EVENTS) != 0U)
        { Usb_BulkFault(index, SYS_ERR_INVALID_STATE); return SYS_ERR_INVALID_STATE; }
    }
    packet = (uint16_t)(size > entry->maxpacket ? entry->maxpacket : size);
    //新事务前清除本端点旧完成，防止旧XFRC归到新缓冲区
    if (index != 0U && entry->type == EP_TYPE_ISOC)
    {
        uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
        USBx_INEP(index)->DIEPINT = USB_OTG_DIEPINT_XFRC | USB_OTG_DIEPINT_EPDISD | USB_OTG_DIEPINT_TOC;
        entry->is_iso_incomplete = 0U;
        __DSB();
    }
    transfer->failed = false;
    transfer->source = data;
    transfer->total = size;
    transfer->queued_bytes = packet;
    transfer->current_packet_length = packet;
    transfer->active = true;
    if (entry->type == EP_TYPE_BULK && index != 0U)
    {
        BspUsbInStopProof_t *proof = &s_bulk_in[index].proof;
        BspUsbInIdentity_t identity = proof->transfer;
        memset(proof, 0, sizeof(*proof));
        proof->transfer = identity;
        proof->transfer.session_generation = s_bulk_session;
        proof->transfer.transfer_sequence = ++s_bulk_transfer_seed[index];
        proof->state = BSP_USB_IN_ACTIVE;
        proof->queued_bytes = packet;
        proof->current_packet_length = packet;
        proof->source_owned = data != NULL;
        proof->pool_owned = true;
    }
    Usb_PrepareTxPacket(index, data, packet);
    status = HAL_PCD_EP_Transmit(&s_pcd, ep, s_tx_pio_pool[index], packet);
    if (status != HAL_OK)
    {
        if (entry->type == EP_TYPE_BULK && index != 0U) Usb_BulkFault(index, Usb_HalResult(status));
        else memset(transfer, 0, sizeof(*transfer));
    }
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
        HAL_StatusTypeDef status;
        uint32_t saved = Usb_BulkPublishLock();
        if (Usb_OutEnabled())
        {
            SysResult_t result = Usb_OutArm(ep, data, size);
            if (result != SYS_OK) { Usb_BulkPublishUnlock(saved); return result; }
        }
        status = HAL_PCD_EP_Receive(&s_pcd, ep, data, size);
        if (status == HAL_OK && (ep & 0x0FU) != 0U) s_rx_active[ep & 0x0FU] = true;
        if (status != HAL_OK && Usb_OutEnabled()) Usb_OutFault(ep, Usb_HalResult(status));
        Usb_BulkPublishUnlock(saved);
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

    //Sleep期间保持USB2控制器时钟
    __HAL_RCC_USB2_OTG_FS_CLK_SLEEP_ENABLE();

    //片内FS PHY不使用ULPI接口的Sleep时钟
    __HAL_RCC_USB2_OTG_FS_ULPI_CLK_SLEEP_DISABLE();
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
    {
        if ((s_pcd.Instance->GINTSTS & USB_OTG_GINTSTS_USBRST) != 0U)
            Usb_BulkInvalidateSession();
        if (s_bulk_fifo_owner != 0U)
        {
            uint32_t raw = s_pcd.Instance->GINTSTS;
            uint32_t USBx_BASE = (uint32_t)s_pcd.Instance;
            //HAL逐分支重读GINTSTS，不能仅凭入口SOF后仍调用其完整处理器。
            //此受限分支只派发SOF；之后到达的Reset/EP事件保留给下一次IRQ。
            if (raw == USB_OTG_GINTSTS_SOF &&
                (s_pcd.Instance->GINTMSK & USB_OTG_GINTMSK_SOFM) != 0U)
            {
                s_pcd.FrameNumber = (USBx_DEVICE->DSTS & USB_OTG_DSTS_FNSOF_Msk) >> USB_OTG_DSTS_FNSOF_Pos;
                HAL_PCD_SOFCallback(&s_pcd);
                s_pcd.Instance->GINTSTS = USB_OTG_GINTSTS_SOF;
                return;
            }
            //所有未证明无共享写命令的组合保留失败，HAL仍处理Reset/Disconnect。
            s_bulk_in[s_bulk_fifo_owner].proof.fifo_conflicted = true;
            Usb_BulkFault(s_bulk_fifo_owner, SYS_ERR_BUSY);
        }
        HAL_PCD_IRQHandler(&s_pcd);
    }
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
    if (ep != 0U && (!transfer->active || transfer->failed)) return;
    if (ep != 0U && pcd->IN_ep[ep].type == EP_TYPE_BULK)
    {
        BspUsbInStopProof_t *proof = &s_bulk_in[ep].proof;
        if (s_bulk_in[ep].fenced)
        {
            proof->events |= USB_OTG_DIEPINT_XFRC;
            Usb_BulkFault(ep, SYS_ERR_INVALID_STATE);
            return;
        }
        if (!transfer->active) return;
        transfer->completed_bytes += transfer->current_packet_length;
        proof->completed_bytes = transfer->completed_bytes;
        ++proof->packet_completions;
        if (transfer->completed_bytes == transfer->total)
        {
            if (proof->source_owned) ++proof->source_returns;
            if (proof->pool_owned) ++proof->pool_returns;
            proof->source_owned = false;
            proof->pool_owned = false;
            proof->state = BSP_USB_IN_IDLE;
        }
    }
    if (!transfer->active)
    {
        s_callbacks->data_in(ep, pcd->IN_ep[ep].xfer_buff);
        return;
    }
    if (ep == 0U)
    {
        callback_data = transfer->source == NULL ? NULL :
                       (uint8_t *)(transfer->source + transfer->current_packet_length);
        memset(transfer, 0, sizeof(*transfer));
        s_callbacks->data_in(ep, callback_data);
        return;
    }
    if (transfer->queued_bytes < transfer->total)
    {
        packet = (uint16_t)((transfer->total - transfer->queued_bytes) > pcd->IN_ep[ep].maxpacket ?
                            pcd->IN_ep[ep].maxpacket : (transfer->total - transfer->queued_bytes));
        Usb_PrepareTxPacket(ep, transfer->source + transfer->queued_bytes, packet);
        transfer->queued_bytes += packet;
        transfer->current_packet_length = packet;
        if (pcd->IN_ep[ep].type == EP_TYPE_BULK)
        {
            s_bulk_in[ep].proof.queued_bytes = transfer->queued_bytes;
            s_bulk_in[ep].proof.current_packet_length = packet;
        }
        status = HAL_PCD_EP_Transmit(&s_pcd, (uint8_t)(0x80U | ep), s_tx_pio_pool[ep], packet);
        if (status == HAL_OK) return;
        if (pcd->IN_ep[ep].type == EP_TYPE_BULK)
        {
            Usb_BulkFault(ep, Usb_HalResult(status));
            if (s_callbacks->data_in_error != NULL)
                s_callbacks->data_in_error(ep, Usb_HalResult(status));
            return;
        }
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
        if (Usb_OutEnabled())
        {
            BspUsbOutStopProof_t *p;
            Usb_OutBoundary(pcd, pcd->Instance->GINTSTS);
            if (ep >= BSP_USB_ENDPOINT_COUNT || !s_out_event[ep].active || !s_out_event[ep].forward) return;
            p = &s_out_slot[ep].proof;
            if (s_out_event[ep].sequence != p->transfer.transfer_sequence || p->fenced || p->pio_references != 0U) return;
            if (p->buffer_owned) ++p->source_returns;
            p->buffer_owned = false;
        }
        if (ep < BSP_USB_ENDPOINT_COUNT) s_rx_active[ep] = false;
        s_callbacks->data_out(ep, pcd->OUT_ep[ep].xfer_buff);
    }
}

//USB总线复位，通知Platform重建EP0和枚举状态
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && Usb_OutEnabled() && !s_out_session_tainted) Usb_OutInvalidate();
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
    if (pcd == &s_pcd) Usb_BulkInvalidateSession();
    if (pcd == &s_pcd && Usb_OutEnabled() && !s_out_session_tainted) Usb_OutInvalidate();
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->disconnect != NULL)
        s_callbacks->disconnect();
}

//SOF仅通知上层，不构包、不提交数据
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *pcd)
{
    if (pcd == &s_pcd && Usb_OutEnabled() && s_out_lease.releasing)
    {
        Usb_OutBoundary(pcd, pcd->Instance->GINTSTS);
        Usb_OutReleaseIfEmpty();
    }
    if (pcd == &s_pcd && s_callbacks != NULL && s_callbacks->sof != NULL)
        s_callbacks->sof((uint16_t)pcd->FrameNumber);
}

//Incomplete保留PIO所有权，必须由明确Abort归还
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *pcd, uint8_t ep)
{
    if (pcd == &s_pcd && s_callbacks != NULL)
    {
        if (ep < BSP_USB_ENDPOINT_COUNT) s_tx_transfer[ep].failed = true;
        if (s_callbacks->iso_incomplete_in != NULL) s_callbacks->iso_incomplete_in(ep);
    }
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
