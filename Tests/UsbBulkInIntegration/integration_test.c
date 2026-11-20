//真实BSP/Platform及Service；仅HAL/Core边界使用显式可诊断夹具
#include "../UsbBulkIn/bulk_in_test.c"
#include "framework_init.h"
#undef FRAMEWORK_INIT_REGISTER
#define FRAMEWORK_INIT_REGISTER(func, level)
#include "../../CM7/Service/usb_mode.c"
uint32_t Platform_TimeGetMs(void) { return s_test_tick; }
SysResult_t Usb_TestRegister(void) { return SYS_OK; }
SysResult_t USB_MSC_Register(void) { return SYS_OK; }
//Application原文件只读编入测试；没有修改其生产实现
#include "../../CM7/Application/app_usb.c"
bool App_UvcBridgeUsbIsIdle(void) { return true; }
uint32_t USB_UVC_GetCompletedFrameCount(void) { return 0U; }
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *status)
{ (void)status; return SYS_ERR_BUSY; }
volatile uint32_t g_integration[30];
extern uint32_t g_allow_terminal;
static uint8_t TestClassDeInit(USBD_HandleTypeDef *device, uint8_t config)
{ (void)device; (void)config; USB_Device_Free(s_class_memory); return USBD_OK; }
int Integration_Step(uint32_t op, uint32_t arg)
{
    SysResult_t result = SYS_OK;
    UsbStopProgress_t progress;
    uint32_t USBx_BASE = (uint32_t)USB2_OTG_FS;
    if (op == 0U)
    {
        BulkInTest_Step(0U, 18U);
        memset(&s_usb_mode, 0, sizeof(s_usb_mode));
        s_usb_mode.initialized = true; s_usb_mode.mode = USB_MODE_MSC;
        s_usb_mode.state = USB_MODE_STATE_RUNNING;
        s_class_driver.DeInit = Usb_ManagedClassDeInit;
        s_class_callbacks.DeInit = TestClassDeInit;
        s_endpoints[1][1].opened = true; s_endpoints[1][1].type = USBD_EP_TYPE_BULK;
    }
    else if (op == 1U) result = USB_Mode_Stop();
    else if (op == 2U) USB_Mode_Process();
    else if (op == 3U) result = USB_Mode_RequestReconnect(USB_MODE_MSC);
    else if (op == 4U) s_test_tick = arg;
    else if (op == 5U) s_control_busy = arg != 0U;
    else if (op == 6U) ++s_stop_progress.transaction_id;
    else if (op == 7U) result = BulkInTest_Step(1U, 18U);
    else if (op == 8U) result = USB_Device_PollBulkInStop(&s_test_transaction, &s_test_proof);
    else if (op == 9U) result = USB_Device_ReconcileBulkInStop(&s_test_transaction, &s_test_proof);
    else if (op == 10U) result = USB_Device_CommitBulkInRetirement(&s_test_transaction, &s_test_proof);
    else if (op == 11U) OTG_FS_IRQHandler();
    else if (op == 12U) HAL_PCD_DisconnectCallback(&s_pcd);
    else if (op == 13U) { s_pcd.Instance->GINTSTS |= USB_OTG_GINTSTS_USBRST; OTG_FS_IRQHandler(); }
    else if (op == 14U) g_allow_terminal = 1U;
    else if (op == 15U) result = BSP_USB_FlushEp(0x81U);
    else if (op == 16U) ++s_test_transaction.stop_attempt;
    else if (op == 17U) s_bulk_in[2].proof.pio_users = arg;
    else if (op == 18U) s_stop_progress_seed = UINT32_MAX;
    else if (op == 19U) result = USB_Device_Stop();
    else if (op == 20U) s_usb_mode.stop_polls = 4095U;
    else if (op == 21U) result = USB_Device_CloseEp(0x82U);
    else if (op == 22U) result = USB_Device_AbortEp(0x82U);
    else if (op == 23U) s_bulk_in[2].poll_count = 4095U;
    else if (op == 24U)
    {
        AppMessage_t message = {.type = APP_MESSAGE_STOP};
        message.type = APP_MESSAGE_STOP; message.command_id = 77U;
        s_usb.started = true;
        App_USBHandleCommand(&message);
    }
    else if (op == 25U) { App_USBProcess(); App_USBFinishRound(false); }
    else if (op == 26U) App_USBConfirmAckEnqueued();
    else if (op == 27U)
    {
        UsbBulkInStopTransaction_t bad = s_test_transaction;
        bad.transfer.endpoint_address = 0x8FU;
        result = USB_Device_ReconcileBulkInStop(&bad, &s_test_proof);
    }
    else if (op == 28U)
    {
        s_endpoints[0][2].opened = true; s_endpoints[0][2].busy = true;
        s_pcd.OUT_ep[2].maxpacket = 64U; s_pcd.OUT_ep[2].type = EP_TYPE_BULK;
        s_pcd.OUT_ep[2].xfer_buff = s_test_data + 128U; s_rx_active[2] = true;
    }
    else if (op == 29U)
    {
        s_initialized = false; (void)USB_Device_Stop(); s_initialized = true;
    }
    USB_Device_GetStopProgress(&progress);
    g_integration[0] = result; g_integration[1] = s_usb_mode.state;
    g_integration[2] = s_usb_mode.last_result;
    g_integration[3] = progress.transaction_id; g_integration[4] = progress.registered_pending;
    g_integration[5] = progress.state; g_integration[6] = s_bulk_in[2].proof.stage;
    g_integration[7] = s_bulk_in[2].proof.stop_attempt;
    g_integration[8] = s_bulk_in[2].proof.source_owned; g_integration[9] = s_bulk_in[2].proof.pool_owned;
    g_integration[10] = s_bulk_in[2].proof.source_returns; g_integration[11] = s_bulk_in[2].proof.pool_returns;
    g_integration[12] = s_bulk_in[2].proof.first_error; g_integration[13] = s_bulk_in[2].proof.error;
    g_integration[14] = s_bulk_in[2].proof.failed_stage; g_integration[15] = s_bulk_in[2].proof.reconcile_count;
    g_integration[16] = s_bulk_fifo_owner; g_integration[17] = s_bulk_in[2].proof.fifo_conflicted;
    g_integration[18] = USBx_DEVICE->DAINTMSK; g_integration[19] = s_test_nvic_changes;
    g_integration[20] = s_endpoints[1][2].busy; g_integration[21] = g_bulk_unexpected;
    g_integration[22] = s_bulk_in[2].proof.transfer.transfer_sequence;
    g_integration[23] = (uint32_t)s_tx_transfer[2].source;
    g_integration[24] = s_usb.ack_pending; g_integration[25] = s_usb.ack.result;
    g_integration[26] = s_usb.fault; g_integration[27] = s_usb.stopping;
    g_integration[28] = s_endpoints[0][2].busy;
    g_integration[29] = s_endpoints[0][2].opened;
    return 0;
}
