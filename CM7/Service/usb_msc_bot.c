//BOT只在UsbTask推进数据与块请求；ISR仅登记事件，取消不释放Worker缓冲
#include "usb_msc_bot.h"
#include "usb_msc_desc.h"
#include "usb_device.h"
#include "platform_sync.h"
#include <string.h>

#define MSC_CHUNK 1024U
#define MSC_RESET_TIMEOUT 2000U
#define MSC_IO_TIMEOUT 30000U

typedef enum
{
    BOT_OFF = 0, BOT_START, BOT_CBW, BOT_READ, BOT_IN,
    BOT_OUT, BOT_WRITE, BOT_SYNC, BOT_FINISH, BOT_CSW,
    BOT_RESET_DRAIN, BOT_RESET_STATUS, BOT_RESET_CLEAR, BOT_FAULT
} MscBotState_t;

static struct
{
    MscBotState_t state;
    const MscBackendOps_t *backend;
    MscScsiContext_t scsi;
    MscScsiContext_t next_scsi;
    MscScsiCommand_t command;
    MscBotCase_t matrix;
    MscBlockRequest_t request;
    uint32_t epoch, request_seed, generation, tag, host_length, transferred;
    uint32_t remaining, chunk, in_sequence, control_token, reset_tick, io_tick;
    uint32_t rx_length;
    uint8_t status, clear_mask;
    bool active, rx_ready, pending;
    uint8_t cbw[64] __attribute__((aligned(32)));
    uint8_t csw[16] __attribute__((aligned(4)));
    uint8_t data[MSC_CHUNK] __attribute__((aligned(32)));
} s_bot;

static uint32_t MscBot_Read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void MscBot_Write32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

MscBotCase_t USB_MSC_BotCase(uint32_t host, bool host_in, uint32_t device, bool device_in)
{
    MscBotCase_t result = {0U, 0U, 0U, false, false};
    if (host == 0U)
    {
        result.number = device == 0U ? 1U : (device_in ? 2U : 3U);
        result.status = device == 0U ? 0U : 2U;
    }
    else if (host_in)
    {
        if (device == 0U) { result.number = 4U; result.stall_in = true; }
        else if (!device_in) { result.number = 8U; result.stall_in = true; result.status = 2U; }
        else
        {
            result.number = host > device ? 5U : (host == device ? 6U : 7U);
            result.transfer = host < device ? host : device;
            result.stall_in = host > device;
            result.status = host < device ? 2U : 0U;
        }
    }
    else
    {
        if (device == 0U) { result.number = 9U; result.stall_out = true; }
        else if (device_in) { result.number = 10U; result.stall_out = true; result.status = 2U; }
        else if (host < device) { result.number = 13U; result.stall_out = true; result.status = 2U; }
        else
        {
            result.number = host == device ? 12U : 11U;
            result.transfer = device;
            result.stall_out = host > device;
        }
    }
    return result;
}

static void MscBot_Fault(void)
{
    s_bot.state = BOT_FAULT;
    //保留缓冲区与请求身份，排空只能由backend实际确认
    if (s_bot.backend != NULL) s_bot.backend->cancel(s_bot.epoch);
    (void)USB_Device_StallEp(USB_MSC_IN_EP);
    (void)USB_Device_StallEp(USB_MSC_OUT_EP);
}
static bool MscBot_Idle(uint8_t ep)
{
    UsbDeviceEndpointState_t state;
    return USB_Device_GetEpState(ep, &state) == SYS_OK && !state.busy;
}
static bool MscBot_Transmit(uint8_t *buffer, uint32_t length, MscBotState_t state)
{
    if (USB_Device_Transmit(USB_MSC_IN_EP, buffer, length) != SYS_OK)
    { MscBot_Fault(); return false; }
    s_bot.in_sequence = USB_Device_GetInSequence(USB_MSC_IN_EP);
    s_bot.state = state;
    return true;
}
static void MscBot_Receive(uint8_t *buffer, uint32_t length, MscBotState_t state)
{
    s_bot.rx_ready = false;
    if (USB_Device_Receive(USB_MSC_OUT_EP, buffer, length) != SYS_OK)
        MscBot_Fault();
    else s_bot.state = state;
}
static bool MscBot_InDone(void)
{
    UsbDeviceEndpointState_t ep;
    if (USB_Device_GetEpState(USB_MSC_IN_EP, &ep) != SYS_OK || ep.last_result != SYS_OK)
    { MscBot_Fault(); return false; }
    return !ep.busy && USB_Device_GetInCompleteSequence(USB_MSC_IN_EP) == s_bot.in_sequence;
}
static void MscBot_Submit(MscScsiOperation_t operation, uint32_t now)
{
    SysResult_t result;
    if (s_bot.request_seed == UINT32_MAX) { MscBot_Fault(); return; }
    s_bot.request.epoch = s_bot.epoch;
    s_bot.request.token = ++s_bot.request_seed;
    s_bot.request.generation = s_bot.generation;
    s_bot.request.operation = operation;
    s_bot.request.lba = s_bot.command.lba + s_bot.transferred / 512U;
    s_bot.request.blocks = s_bot.chunk / 512U;
    s_bot.request.buffer = s_bot.data;
    result = s_bot.backend->submit(&s_bot.request);
    if (result != SYS_OK) { MscBot_Fault(); return; }
    s_bot.pending = true;
    s_bot.io_tick = now;
    s_bot.state = operation == MSC_SCSI_READ ? BOT_READ :
        (operation == MSC_SCSI_WRITE ? BOT_WRITE : BOT_SYNC);
}
static bool MscBot_Poll(uint32_t now, MscBlockCompletion_t *completion)
{
    SysResult_t result = s_bot.backend->poll(&s_bot.request, completion);
    if (result == SYS_ERR_BUSY)
    {
        if ((uint32_t)(now - s_bot.io_tick) >= MSC_IO_TIMEOUT) MscBot_Fault();
        return false;
    }
    if (result != SYS_OK) { MscBot_Fault(); return false; }
    s_bot.pending = false;
    if (completion->completed_blocks > s_bot.request.blocks) { MscBot_Fault(); return false; }
    if (completion->result != SYS_OK || completion->completed_blocks != s_bot.request.blocks)
    {
        if (s_bot.request.operation == MSC_SCSI_WRITE)
            s_bot.transferred += completion->completed_blocks * 512U;
        USB_MSC_ScsiSense(&s_bot.scsi, 3U,
            s_bot.request.operation == MSC_SCSI_WRITE ? 0x0CU : 0x11U, 0U);
        s_bot.status = s_bot.status == 2U ? 2U : 1U;
        s_bot.remaining = 0U;
        //部分失败终止主机Data Stage；不接受剩余OUT为下一笔CBW
        s_bot.matrix.stall_in = (s_bot.cbw[12] & 0x80U) != 0U && s_bot.host_length != 0U;
        s_bot.matrix.stall_out = (s_bot.cbw[12] & 0x80U) == 0U && s_bot.host_length != 0U;
        s_bot.state = BOT_FINISH;
        return false;
    }
    return true;
}
static void MscBot_Next(uint32_t now)
{
    s_bot.chunk = s_bot.remaining > MSC_CHUNK ? MSC_CHUNK : s_bot.remaining;
    if (s_bot.chunk == 0U) { s_bot.state = BOT_FINISH; return; }
    if (s_bot.command.operation == MSC_SCSI_RESPONSE)
    {
        memcpy(s_bot.data, s_bot.command.response, s_bot.chunk);
        (void)MscBot_Transmit(s_bot.data, s_bot.chunk, BOT_IN);
    }
    else if (s_bot.command.operation == MSC_SCSI_READ)
    {
        //Case7允许不足扇区的主机IN，但后端仍以完整扇区读取
        uint32_t transfer = s_bot.chunk;
        s_bot.chunk = (transfer + 511U) & ~511U;
        MscBot_Submit(MSC_SCSI_READ, now);
        s_bot.chunk = transfer;
    }
    else MscBot_Receive(s_bot.data, s_bot.chunk, BOT_OUT);
}
static void MscBot_Plan(uint32_t now)
{
    MscScsiMedia_t media;
    uint32_t generation;
    if (s_bot.rx_length != 31U || MscBot_Read32(s_bot.cbw) != 0x43425355U)
    { MscBot_Fault(); return; }
    if ((s_bot.cbw[12] & 0x7FU) != 0U || s_bot.cbw[13] != 0U ||
        s_bot.cbw[14] == 0U || s_bot.cbw[14] > 16U)
    { MscBot_Fault(); return; }
    s_bot.tag = MscBot_Read32(s_bot.cbw + 4U);
    s_bot.host_length = MscBot_Read32(s_bot.cbw + 8U);
    s_bot.backend->media(&media, &generation);
    if (generation != s_bot.generation)
    {
        s_bot.scsi.unit_attention = true;
        s_bot.scsi.ejected = false;
        s_bot.scsi.prevent = false;
        s_bot.generation = generation;
    }
    //成功提交CSW后才提交PREVENT/LOAD/SENSE等命令的状态副作用
    s_bot.next_scsi = s_bot.scsi;
    (void)USB_MSC_ScsiDecode(&s_bot.next_scsi, &media, s_bot.cbw + 15U, s_bot.cbw[14], &s_bot.command);
    if (s_bot.command.failed)
        s_bot.scsi = s_bot.next_scsi;
    s_bot.matrix = USB_MSC_BotCase(s_bot.host_length, (s_bot.cbw[12] & 0x80U) != 0U,
        s_bot.command.length, s_bot.command.operation != MSC_SCSI_WRITE);
    s_bot.status = s_bot.matrix.status != 0U ? 2U : (s_bot.command.failed ? 1U : 0U);
    s_bot.remaining = s_bot.matrix.transfer;
    s_bot.transferred = 0U;
    if (s_bot.remaining != 0U) MscBot_Next(now);
    else if (s_bot.status == 0U && (s_bot.command.operation == MSC_SCSI_SYNC ||
        s_bot.command.operation == MSC_SCSI_EJECT))
    {
        s_bot.chunk = 0U;
        MscBot_Submit(s_bot.command.operation, now);
    }
    else s_bot.state = BOT_FINISH;
}

SysResult_t USB_MSC_BotStart(const MscBackendOps_t *backend)
{
    if (backend == NULL || s_bot.active || s_bot.epoch == UINT32_MAX ||
        (s_bot.backend != NULL && s_bot.backend != backend)) return SYS_ERR_INVALID_STATE;
    s_bot.backend = backend;
    s_bot.active = true;
    s_bot.state = BOT_START;
    s_bot.control_token = 0U;
    //START必须先等上一个epoch排空；不能在Init里清零旧请求或缓冲
    return SYS_OK;
}
void USB_MSC_BotStop(void)
{
    s_bot.active = false;
    s_bot.rx_ready = false;
    s_bot.control_token = 0U;
    if (s_bot.backend != NULL) s_bot.backend->cancel(s_bot.epoch);
    s_bot.state = BOT_OFF;
}
void USB_MSC_BotDataIn(void)
{
    //真实完成序号由Platform记录，迟到回调不能自行推动BOT
}
void USB_MSC_BotDataOut(uint32_t length)
{
    if (s_bot.state == BOT_CBW || s_bot.state == BOT_OUT)
    { s_bot.rx_length = length; s_bot.rx_ready = true; }
}
void USB_MSC_BotClearHalt(uint8_t endpoint)
{
    //非法CBW/phase error的故障锁定不能仅靠Clear HALT绕过Reset
    if (s_bot.state == BOT_FAULT)
    {
        (void)USB_Device_StallEp(endpoint);
        return;
    }
    if (s_bot.state == BOT_RESET_STATUS && USB_Device_WasControlStatusComplete(s_bot.control_token))
        s_bot.state = BOT_RESET_CLEAR;
    if (s_bot.state == BOT_RESET_CLEAR)
        s_bot.clear_mask |= endpoint == USB_MSC_IN_EP ? 1U : 2U;
}
SysResult_t USB_MSC_BotReset(uint32_t token, uint32_t now)
{
    if (!s_bot.active || token == 0U || s_bot.epoch == UINT32_MAX) return SYS_ERR_INVALID_STATE;
    s_bot.control_token = token;
    s_bot.reset_tick = now;
    s_bot.clear_mask = 0U;
    s_bot.rx_ready = false;
    s_bot.backend->cancel(s_bot.epoch);
    //Abort不清HALT；失败保留端点所有权，由任务重试排空
    (void)USB_Device_AbortEp(USB_MSC_IN_EP);
    (void)USB_Device_AbortEp(USB_MSC_OUT_EP);
    s_bot.state = BOT_RESET_DRAIN;
    return SYS_OK;
}
void USB_MSC_BotProcess(uint32_t now)
{
    uint32_t lock;
    MscBlockCompletion_t completion;
    UsbControlStatus_t control;
    if (Platform_SyncGetException() != 0U) return;
    lock = USB_Device_Lock();
    if (!s_bot.active) { USB_Device_Unlock(lock); return; }
    switch (s_bot.state)
    {
        case BOT_START:
            if (!s_bot.backend->drained(s_bot.epoch) || !MscBot_Idle(USB_MSC_IN_EP) ||
                !MscBot_Idle(USB_MSC_OUT_EP)) break;
            ++s_bot.epoch;
            s_bot.pending = false;
            s_bot.scsi.unit_attention = true;
            MscBot_Receive(s_bot.cbw, sizeof(s_bot.cbw), BOT_CBW);
            break;
        case BOT_CBW:
            if (s_bot.rx_ready) { s_bot.rx_ready = false; MscBot_Plan(now); }
            break;
        case BOT_READ:
            if (MscBot_Poll(now, &completion)) (void)MscBot_Transmit(s_bot.data, s_bot.chunk, BOT_IN);
            break;
        case BOT_IN:
            if (MscBot_InDone())
            {
                s_bot.remaining -= s_bot.chunk; s_bot.transferred += s_bot.chunk;
                MscBot_Next(now);
            }
            break;
        case BOT_OUT:
            if (!s_bot.rx_ready) break;
            s_bot.rx_ready = false;
            if (s_bot.rx_length != s_bot.chunk)
            {
                s_bot.status = 2U;
                s_bot.remaining = 0U;
                s_bot.matrix.stall_out = true;
                s_bot.state = BOT_FINISH;
                break;
            }
            MscBot_Submit(MSC_SCSI_WRITE, now);
            break;
        case BOT_WRITE:
            if (MscBot_Poll(now, &completion))
            {
                s_bot.remaining -= s_bot.chunk; s_bot.transferred += s_bot.chunk;
                MscBot_Next(now);
            }
            break;
        case BOT_SYNC:
            if (MscBot_Poll(now, &completion))
            {
                if (s_bot.command.operation == MSC_SCSI_EJECT) s_bot.next_scsi.ejected = true;
                s_bot.state = BOT_FINISH;
            }
            break;
        case BOT_FINISH:
        {
            UsbDeviceEndpointState_t ep;
            if (s_bot.matrix.stall_out)
            {
                if (USB_Device_StallEp(USB_MSC_OUT_EP) != SYS_OK) { MscBot_Fault(); break; }
                s_bot.matrix.stall_out = false;
            }
            if (s_bot.matrix.stall_in)
            {
                if (USB_Device_StallEp(USB_MSC_IN_EP) != SYS_OK) { MscBot_Fault(); break; }
                s_bot.matrix.stall_in = false;
                break;
            }
            if (USB_Device_GetEpState(USB_MSC_IN_EP, &ep) != SYS_OK || ep.stalled) break;
            MscBot_Write32(s_bot.csw, 0x53425355U);
            MscBot_Write32(s_bot.csw + 4U, s_bot.tag);
            MscBot_Write32(s_bot.csw + 8U, s_bot.host_length - s_bot.transferred);
            s_bot.csw[12] = s_bot.status;
            (void)MscBot_Transmit(s_bot.csw, 13U, BOT_CSW);
            break;
        }
        case BOT_CSW:
            if (!MscBot_InDone()) break;
            if (s_bot.status == 2U) { MscBot_Fault(); break; }
            if (s_bot.status == 0U)
            {
                s_bot.scsi = s_bot.next_scsi;
                if (s_bot.command.clear_sense) USB_MSC_ScsiSense(&s_bot.scsi, 0U, 0U, 0U);
            }
            MscBot_Receive(s_bot.cbw, sizeof(s_bot.cbw), BOT_CBW);
            break;
        case BOT_RESET_DRAIN:
            if (USB_Device_GetControlStatus(s_bot.control_token, &control) != SYS_OK ||
                control != USB_CONTROL_WAITING) { MscBot_Fault(); break; }
            if ((uint32_t)(now - s_bot.reset_tick) >= MSC_RESET_TIMEOUT)
            { (void)USB_Device_FailControlStatus(s_bot.control_token); MscBot_Fault(); break; }
            if (USB_Device_AbortEp(USB_MSC_IN_EP) != SYS_OK ||
                USB_Device_AbortEp(USB_MSC_OUT_EP) != SYS_OK || !s_bot.backend->drained(s_bot.epoch)) break;
            s_bot.pending = false;
            if (USB_Device_CompleteControlStatus(s_bot.control_token) != SYS_OK) { MscBot_Fault(); break; }
            s_bot.state = BOT_RESET_STATUS;
            break;
        case BOT_RESET_STATUS:
            if (USB_Device_WasControlStatusComplete(s_bot.control_token))
            { s_bot.state = BOT_RESET_CLEAR; break; }
            if (USB_Device_GetControlStatus(s_bot.control_token, &control) != SYS_OK ||
                control == USB_CONTROL_CANCELLED || control == USB_CONTROL_FAILED)
            { MscBot_Fault(); break; }
            if (control == USB_CONTROL_COMPLETE) s_bot.state = BOT_RESET_CLEAR;
            else if ((uint32_t)(now - s_bot.reset_tick) >= MSC_RESET_TIMEOUT) MscBot_Fault();
            break;
        case BOT_RESET_CLEAR:
            if (s_bot.clear_mask == 3U)
            {
                ++s_bot.epoch;
                MscBot_Receive(s_bot.cbw, sizeof(s_bot.cbw), BOT_CBW);
            }
            break;
        default:
            break;
    }
    USB_Device_Unlock(lock);
}
