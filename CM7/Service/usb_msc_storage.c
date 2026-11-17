//单槽有界请求邮箱：USB任务提交，Storage任务执行，三身份匹配领取完成
#include "usb_msc_storage.h"
#include "platform_sync.h"
#include "runtime_event.h"
#include <stddef.h>

typedef enum { MSC_MAIL_EMPTY = 0, MSC_MAIL_QUEUED, MSC_MAIL_RUNNING, MSC_MAIL_DRAIN, MSC_MAIL_DONE } MscMailState_t;
static struct
{
    const MscStorageDeviceOps_t *device;
    MscMailState_t state;
    MscBlockRequest_t request;
    MscBlockCompletion_t completion;
    MscScsiMedia_t media;
    uint32_t generation;
    uint32_t last_token;
    bool cancelled;
    bool initialized;
} s_storage;
static uint32_t MscStorage_Lock(void)
{
    uint32_t mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    return mask;
}
static void MscStorage_Unlock(uint32_t mask)
{
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
}
static void MscStorage_Notify(RuntimeEventTarget_t target)
{
    if (Platform_SyncGetException() == 0U)
        (void)RuntimeEvent_Post(target, RUNTIME_EVENT_WAKE);
    else (void)RuntimeEvent_PostFromIsr(target, RUNTIME_EVENT_WAKE);
}
static bool MscStorage_Match(const MscBlockRequest_t *request)
{
    return request != NULL && request->epoch == s_storage.request.epoch &&
        request->token == s_storage.request.token && request->generation == s_storage.request.generation;
}
static void MscStorage_Media(MscScsiMedia_t *media, uint32_t *generation)
{
    uint32_t mask = MscStorage_Lock();
    *media = s_storage.media; *generation = s_storage.generation;
    MscStorage_Unlock(mask);
}
static SysResult_t MscStorage_Submit(const MscBlockRequest_t *request)
{
    uint32_t mask;
    if (Platform_SyncGetException() != 0U) return SYS_ERR_INVALID_STATE;
    if (request == NULL || request->epoch == 0U || request->token == 0U ||
        (request->operation != MSC_SCSI_READ && request->operation != MSC_SCSI_WRITE &&
        request->operation != MSC_SCSI_SYNC && request->operation != MSC_SCSI_EJECT)) return SYS_ERR_INVALID_ARG;
    mask = MscStorage_Lock();
    if (!s_storage.initialized) { MscStorage_Unlock(mask); return SYS_ERR_INVALID_STATE; }
    if (s_storage.state != MSC_MAIL_EMPTY) { MscStorage_Unlock(mask); return SYS_ERR_BUSY; }
    if (request->token <= s_storage.last_token || request->generation != s_storage.generation || !s_storage.media.present || !s_storage.media.ready)
    { MscStorage_Unlock(mask); return SYS_ERR_INVALID_STATE; }
    if ((request->operation == MSC_SCSI_READ || request->operation == MSC_SCSI_WRITE) &&
        (request->buffer == NULL || request->blocks == 0U || request->blocks > 2U ||
        request->lba >= s_storage.media.block_count || request->blocks > s_storage.media.block_count - request->lba))
    { MscStorage_Unlock(mask); return SYS_ERR_INVALID_ARG; }
    if (request->operation == MSC_SCSI_WRITE && s_storage.media.write_protected)
    { MscStorage_Unlock(mask); return SYS_ERR_NOT_SUPPORTED; }
    s_storage.last_token = request->token;
    s_storage.request = *request;
    s_storage.cancelled = false;
    s_storage.state = MSC_MAIL_QUEUED;
    MscStorage_Unlock(mask);
    MscStorage_Notify(RUNTIME_TARGET_STORAGE);
    return SYS_OK;
}
static SysResult_t MscStorage_Poll(const MscBlockRequest_t *request, MscBlockCompletion_t *completion)
{
    uint32_t mask = MscStorage_Lock();
    if (completion == NULL || s_storage.state == MSC_MAIL_EMPTY || !MscStorage_Match(request))
    { MscStorage_Unlock(mask); return SYS_ERR_INVALID_STATE; }
    if (s_storage.state != MSC_MAIL_DONE) { MscStorage_Unlock(mask); return SYS_ERR_BUSY; }
    *completion = s_storage.completion;
    s_storage.state = MSC_MAIL_EMPTY;
    MscStorage_Unlock(mask);
    return SYS_OK;
}
static void MscStorage_Cancel(uint32_t epoch)
{
    uint32_t mask = MscStorage_Lock();
    if (s_storage.state != MSC_MAIL_EMPTY && s_storage.request.epoch == epoch)
    {
        s_storage.cancelled = true;
        //仅尚未开始或已经确认硬件空闲的请求可立即退役
        if (s_storage.state == MSC_MAIL_QUEUED || s_storage.state == MSC_MAIL_DONE)
            s_storage.state = MSC_MAIL_EMPTY;
    }
    MscStorage_Unlock(mask);
    MscStorage_Notify(RUNTIME_TARGET_STORAGE);
}
static bool MscStorage_Drained(uint32_t epoch)
{
    uint32_t mask = MscStorage_Lock();
    bool drained = s_storage.state == MSC_MAIL_EMPTY || s_storage.request.epoch != epoch ||
        s_storage.state == MSC_MAIL_DONE;
    MscStorage_Unlock(mask);
    return drained;
}
static const MscBackendOps_t s_storage_ops =
{ MscStorage_Media, MscStorage_Submit, MscStorage_Poll, MscStorage_Cancel, MscStorage_Drained };
const MscBackendOps_t *USB_MSC_StorageBackend(void) { return &s_storage_ops; }

SysResult_t USB_MSC_StorageInit(const MscStorageDeviceOps_t *device)
{
    SysResult_t result;
    if (Platform_SyncGetException() != 0U || s_storage.device != NULL || device == NULL ||
        device->init == NULL || device->media == NULL || device->execute == NULL || device->quiescent == NULL)
        return SYS_ERR_INVALID_STATE;
    s_storage.device = device;
    result = device->init();
    //失败锁定，不由USB断线/重枚举自动重新初始化或挂载
    if (result != SYS_OK) return result;
    s_storage.initialized = true;
    USB_MSC_StorageProcess();
    return SYS_OK;
}
void USB_MSC_StorageProcess(void)
{
    uint32_t mask;
    MscBlockRequest_t request;
    MscBlockCompletion_t completion;
    MscScsiMedia_t media;
    uint32_t generation;
    bool execute = false;
    if (Platform_SyncGetException() != 0U || !s_storage.initialized) return;
    mask = MscStorage_Lock();
    if (s_storage.state == MSC_MAIL_QUEUED)
    {
        request = s_storage.request;
        s_storage.state = MSC_MAIL_RUNNING;
        execute = true;
    }
    MscStorage_Unlock(mask);
    if (execute)
    {
        //重新核对介质代次后才执行；锁外允许SDMMC阻塞并被USB Reset抢占
        s_storage.device->media(&media, &generation);
        completion.result = SYS_ERR_INVALID_STATE;
        completion.completed_blocks = 0U;
        if (generation == request.generation && media.ready && media.present)
            s_storage.device->execute(&request, &completion);
        mask = MscStorage_Lock();
        s_storage.completion = completion;
        s_storage.state = MSC_MAIL_DRAIN;
        MscStorage_Unlock(mask);
    }
    //不清软件Busy伪造取消成功；硬件仍活动时一直保留请求及缓冲
    if (s_storage.state == MSC_MAIL_DRAIN && s_storage.device->quiescent())
    {
        mask = MscStorage_Lock();
        s_storage.state = s_storage.cancelled ? MSC_MAIL_EMPTY : MSC_MAIL_DONE;
        MscStorage_Unlock(mask);
        MscStorage_Notify(RUNTIME_TARGET_USB);
    }
    s_storage.device->media(&media, &generation);
    mask = MscStorage_Lock();
    s_storage.media = media; s_storage.generation = generation;
    MscStorage_Unlock(mask);
}
