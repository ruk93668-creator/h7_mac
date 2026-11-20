//真实BSP与Platform代码进入同一个软件夹具；只替换硬件提交和外部Core边界
#include "../../CM7/BSP/bsp_usb.c"
#define s_started s_platform_started
#define s_callbacks s_platform_callbacks
#include "../../CM7/Platform/usb_device.c"
#undef s_started
#undef s_callbacks

volatile uint32_t g_bulk_observation[48];
volatile uint32_t g_bulk_unexpected;
static uint8_t s_test_data[256];
static uint32_t s_test_tick;
static uint32_t s_test_transmits;
static uint32_t s_test_class_completions;
static uint32_t s_test_nvic_changes;
static bool s_test_transmit_error;
static UsbBulkInStopTransaction_t s_test_transaction;
static BspUsbInStopProof_t s_test_proof;

void *memset(void *dst, int value, size_t length)
{ volatile uint8_t *p = dst; while (length-- != 0U) *p++ = (uint8_t)value; return dst; }
void *memcpy(void *dst, const void *src, size_t length)
{ volatile uint8_t *p = dst; const volatile uint8_t *q = src; while (length-- != 0U) *p++ = *q++; return dst; }
void __aeabi_memclr(void *dst, size_t n) { volatile uint8_t *p = dst; while (n-- != 0U) *p++ = 0U; }
void __aeabi_memclr4(void *dst, size_t n) { volatile uint8_t *p = dst; while (n-- != 0U) *p++ = 0U; }
void __aeabi_memcpy(void *dst, const void *src, size_t n) { volatile uint8_t *p = dst; const volatile uint8_t *q = src; while (n-- != 0U) *p++ = *q++; }
void __aeabi_memcpy4(void *dst, const void *src, size_t n) { volatile uint8_t *p = dst; const volatile uint8_t *q = src; while (n-- != 0U) *p++ = *q++; }
uint32_t HAL_GetTick(void) { return s_test_tick; }
void HAL_NVIC_EnableIRQ(IRQn_Type irq) { (void)irq; ++s_test_nvic_changes; }
void HAL_NVIC_DisableIRQ(IRQn_Type irq) { (void)irq; ++s_test_nvic_changes; }
HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *pcd, uint8_t address, uint8_t *buffer, uint32_t size)
{
    uint8_t ep = address & 15U;
    uint32_t USBx_BASE = (uint32_t)pcd->Instance;
    pcd->IN_ep[ep].xfer_buff = buffer;
    pcd->IN_ep[ep].xfer_len = size;
    pcd->IN_ep[ep].xfer_count = 0U;
    USBx_INEP(ep)->DIEPCTL |= USB_OTG_DIEPCTL_EPENA;
    ++s_test_transmits;
    return s_test_transmit_error ? HAL_ERROR : HAL_OK;
}
USBD_StatusTypeDef USBD_LL_DataInStage(USBD_HandleTypeDef *device, uint8_t ep, uint8_t *data)
{ (void)device; (void)ep; (void)data; ++s_test_class_completions; return USBD_OK; }
static void TestSetup(uint8_t *data) { (void)data; }
static void TestOut(uint8_t ep, uint8_t *data) { (void)ep; (void)data; }
static const BspUsbCallbacks_t s_test_callbacks = { TestSetup, TestOut, Usb_DataIn, Usb_Reset, NULL, NULL, NULL, Usb_Disconnect, NULL, NULL, Usb_DataInError, NULL };

//op=0准备、1Begin、2正常XFRC回调、3Poll、4Commit、5时间超时、6新传输、
//7旧身份、8Reset边界、9Disconnect边界、10PIO引用注入、11迟到回调、12重复Begin
int BulkInTest_Step(uint32_t op, uint32_t scenario)
{
    SysResult_t result = SYS_OK;
    uint32_t USBx_BASE = (uint32_t)USB2_OTG_FS;
    uint32_t size = (scenario == 3U || scenario == 5U || scenario == 34U) ? 128U : 64U;
    if (op == 0U)
    {
        memset(s_bulk_in, 0, sizeof(s_bulk_in));
        memset(s_tx_transfer, 0, sizeof(s_tx_transfer));
        s_bulk_controller = 1U; s_bulk_session = 1U; s_bulk_attempt_seed = 0U; s_bulk_fifo_owner = 0U;
        s_ready = true; s_started = true; s_callbacks = &s_test_callbacks;
        s_pcd.Instance = USB2_OTG_FS; s_pcd.Init.dma_enable = DISABLE;
        s_pcd.IN_ep[2].num = 2U; s_pcd.IN_ep[2].type = EP_TYPE_BULK; s_pcd.IN_ep[2].maxpacket = 64U;
        s_pcd.IN_ep[1].num = 1U; s_pcd.IN_ep[1].type = EP_TYPE_BULK; s_pcd.IN_ep[1].maxpacket = 64U;
        s_bulk_in[2].proof.transfer.controller_generation = 1U;
        s_bulk_in[2].proof.transfer.session_generation = 1U;
        s_bulk_in[2].proof.transfer.endpoint_generation = 1U;
        s_bulk_in[2].proof.transfer.endpoint_address = 0x82U;
        s_bulk_in[2].proof.state = BSP_USB_IN_IDLE;
        s_bulk_in[1].proof.transfer = s_bulk_in[2].proof.transfer;
        s_bulk_in[1].proof.transfer.endpoint_address = 0x81U;
        s_bulk_in[1].proof.state = BSP_USB_IN_IDLE;
        USBx_INEP(1)->DIEPCTL = 1UL << 22;
        USBx_INEP(2)->DIEPCTL = 2UL << 22;
        USBx_DEVICE->DAINTMSK = 0x00010007U;
        USBx_DEVICE->DIEPEMPMSK = 7U;
        s_initialized = true; s_platform_started = true; s_registered = true;
        s_class_state = USB_CLASS_ACTIVE; s_stop_required = false; s_class_memory_used = true;
        s_endpoints[1][2].opened = true; s_endpoints[1][2].type = USBD_EP_TYPE_BULK;
        if (scenario != 1U) result = USB_Device_Transmit(0x82U, s_test_data, size);
        s_test_nvic_changes = 0U;
    }
    else if (op == 1U)
    {
        result = USB_Device_GetBulkInTransfer(0x82U, &s_test_transaction);
        if (result == SYS_OK) result = USB_Device_BeginBulkInStop(&s_test_transaction, 10U, &s_test_proof);
    }
    else if (op == 2U) HAL_PCD_DataInStageCallback(&s_pcd, 2U);
    else if (op == 3U) result = USB_Device_PollBulkInStop(&s_test_transaction, &s_test_proof);
    else if (op == 4U) result = USB_Device_CommitBulkInRetirement(&s_test_transaction, &s_test_proof);
    else if (op == 5U) s_test_tick = 11U;
    else if (op == 6U) result = USB_Device_Transmit(0x82U, s_test_data + 128U, 64U);
    else if (op == 7U)
    {
        UsbBulkInStopTransaction_t invalid = s_test_transaction;
        ++invalid.transfer.endpoint_generation;
        result = USB_Device_PollBulkInStop(&invalid, &s_test_proof);
    }
    else if (op == 8U)
    {
        s_pcd.Instance->GINTSTS |= USB_OTG_GINTSTS_USBRST;
        OTG_FS_IRQHandler();
        HAL_PCD_ResetCallback(&s_pcd);
    }
    else if (op == 9U) HAL_PCD_DisconnectCallback(&s_pcd);
    else if (op == 10U) s_bulk_in[2].proof.pio_users = 1U;
    else if (op == 11U) HAL_PCD_DataInStageCallback(&s_pcd, 2U);
    else if (op == 12U) result = USB_Device_BeginBulkInStop(&s_test_transaction, 10U, &s_test_proof);
    else if (op == 13U)
    {
        //EP1用真实BSP PIO提交/完成路径，EP2的mask保持不动
        result = BSP_USB_Transmit(0x81U, s_test_data + 192U, 64U);
        USBx_INEP(1)->DIEPCTL &= ~USB_OTG_DIEPCTL_EPENA;
        HAL_PCD_DataInStageCallback(&s_pcd, 1U);
    }
    else if (op == 25U) result = Usb_DrainBulkInStops();
    else if (op == 26U) result = USB_Device_Stop();
    else if (op == 21U)
    {
        s_test_transmit_error = true;
        result = USB_Device_Transmit(0x82U, s_test_data + 128U, 64U);
    }
    else if (op == 22U)
    {
        s_ready = false;
        s_bulk_controller = UINT32_MAX;
        result = BSP_USB_Init();
    }
    else if (op == 23U)
    {
        s_bulk_session = UINT32_MAX;
        result = USB_Device_Transmit(0x82U, s_test_data + 128U, 64U);
    }
    else if (op == 24U)
    {
        s_test_transmit_error = true;
        HAL_PCD_DataInStageCallback(&s_pcd, 2U);
    }
    else if (op == 19U) result = BSP_USB_FlushEp(0x82U);
    else if (op == 20U)
    {
        s_bulk_endpoint_seed[2] = UINT32_MAX;
        result = BSP_USB_OpenEp(0x82U, EP_TYPE_BULK, 64U);
    }
    else if (op == 18U) OTG_FS_IRQHandler();
    else if (op == 14U) s_bulk_attempt_seed = UINT32_MAX;
    else if (op == 15U) s_bulk_transfer_seed[2] = UINT32_MAX;
    else if (op == 16U) result = USB_Device_CommitBulkInRetirement(&s_test_transaction, &s_test_proof);
    if (scenario == 25U && op == 0U)
    {
        USBx_DEVICE->DAINTMSK &= ~4UL;
        USBx_DEVICE->DIEPEMPMSK &= ~4UL;
    }
    s_test_proof = s_bulk_in[2].proof;
    g_bulk_observation[0] = result;
    g_bulk_observation[1] = s_test_proof.state;
    g_bulk_observation[2] = s_test_proof.stage;
    g_bulk_observation[3] = s_test_proof.result;
    g_bulk_observation[4] = s_test_proof.transfer.transfer_sequence;
    g_bulk_observation[5] = s_test_proof.stop_attempt;
    g_bulk_observation[6] = s_test_proof.source_owned;
    g_bulk_observation[7] = s_test_proof.pool_owned;
    g_bulk_observation[8] = s_test_proof.queued_bytes;
    g_bulk_observation[9] = s_test_proof.completed_bytes;
    g_bulk_observation[10] = s_test_proof.current_packet_length;
    g_bulk_observation[11] = s_test_proof.events;
    g_bulk_observation[12] = s_test_proof.packet_completions;
    g_bulk_observation[13] = s_test_proof.source_returns;
    g_bulk_observation[14] = s_test_proof.pool_returns;
    g_bulk_observation[15] = s_test_class_completions;
    g_bulk_observation[16] = s_test_transmits;
    g_bulk_observation[17] = USBx_DEVICE->DAINTMSK;
    g_bulk_observation[18] = USBx_DEVICE->DIEPEMPMSK;
    g_bulk_observation[19] = s_test_proof.fifo_complete;
    g_bulk_observation[20] = s_test_proof.events_retired;
    g_bulk_observation[21] = s_test_proof.completed_undelivered;
    g_bulk_observation[22] = s_endpoints[1][2].busy;
    g_bulk_observation[23] = s_test_nvic_changes;
    g_bulk_observation[24] = s_test_proof.pio_users;
    g_bulk_observation[25] = s_test_proof.error;
    g_bulk_observation[26] = s_test_proof.transfer.controller_generation;
    g_bulk_observation[27] = s_test_proof.transfer.session_generation;
    g_bulk_observation[28] = s_test_proof.transfer.endpoint_generation;
    g_bulk_observation[29] = s_bulk_session;
    g_bulk_observation[30] = s_bulk_in[1].proof.state;
    g_bulk_observation[31] = (uint32_t)s_tx_transfer[2].source;
    g_bulk_observation[32] = g_bulk_unexpected;
    g_bulk_observation[33] = s_test_proof.fifo_result;
    g_bulk_observation[34] = s_class_memory_used;
    g_bulk_observation[35] = s_class_state;
    g_bulk_observation[36] = s_stop_required;
    g_bulk_observation[37] = s_last_result;
    return 0;
}
