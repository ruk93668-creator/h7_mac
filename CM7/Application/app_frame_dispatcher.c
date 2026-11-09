#include "app_frame_dispatcher.h"
#include "app_events.h"
#include "platform_time.h"

//按用途静态绑定副本服务，不进行字符串查找或动态注册
typedef struct
{
    bool (*wants)(void *context, uint32_t now);

    SysResult_t (*offer)(void *context, const CameraFrame_t *frame, const AppFrameMeta_t *meta);
    void *context;
} FrameCopyService_t;

//Camera独占会话和帧编号，不能由Storage或USB修改
static uint32_t s_epoch, s_next_frame;

//查询副本服务是否订阅当前帧，未开启时不复制
static bool Dispatcher_StorageWants(void *context, uint32_t now);

//把副本请求交给存储服务，返回失败不取消USB原帧
static SysResult_t Dispatcher_StorageOffer(void *context, const CameraFrame_t *frame, const AppFrameMeta_t *meta);

//静态服务记录只提供能力入口，不授予Media所有权
static const FrameCopyService_t s_storage_service =
{
    .wants = Dispatcher_StorageWants,

    .offer = Dispatcher_StorageOffer,

    .context = NULL
};

//只验证本轮实现的输出，网络能力不能伪装可用
SysResult_t App_FrameDispatcherValidateOutput(VideoOutput_t output)
{
    switch (output)
    {
        case VIDEO_OUTPUT_USB_UVC:
        case VIDEO_OUTPUT_NONE:
        case VIDEO_OUTPUT_USB_TEST:
            return SYS_OK;
        case VIDEO_OUTPUT_ETH:
        case VIDEO_OUTPUT_WIFI:
            return SYS_ERR_NOT_SUPPORTED;
        default:
            return SYS_ERR_INVALID_ARG;
    }
}

//建立严格递增的采集会话，不复用旧帧标识
SysResult_t App_FrameDispatcherBegin(uint32_t epoch)
{
    if (!App_EventsIsOwner(RUNTIME_TARGET_CAMERA) || epoch == 0U || epoch <= s_epoch)
        return SYS_ERR_INVALID_STATE;
    s_epoch = epoch;
    s_next_frame = 0U;
    return SYS_OK;
}

//先形成元数据，再按存储需求复制；不在这里释放原帧
SysResult_t App_FrameDispatcherPublish(const CameraFrame_t *frame, AppFrameMeta_t *meta)
{
    if (!App_EventsIsOwner(RUNTIME_TARGET_CAMERA) || frame == NULL || meta == NULL || s_epoch == 0U)
        return SYS_ERR_INVALID_STATE;
    if (s_next_frame == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;
    meta->frame_id = ++s_next_frame;
    meta->timestamp_ms = Platform_TimeGetMs();
    meta->stream_epoch = s_epoch;
    meta->width = frame->width;
    meta->height = frame->height;
    if (s_storage_service.wants(s_storage_service.context, meta->timestamp_ms))
        return s_storage_service.offer(s_storage_service.context, frame, meta);
    return SYS_OK;
}

//AI需要独立资源协议，本阶段只留下明确拒绝的边界
SysResult_t App_FrameDispatcherRequestAi(void)
{
    return SYS_ERR_NOT_SUPPORTED;
}

//适配现有副本服务，Context为空是当前单实例的显式绑定
static SysResult_t Dispatcher_StorageOffer(void *context, const CameraFrame_t *frame, const AppFrameMeta_t *meta)
{
    (void)context;
    return App_StorageOfferFrame(frame, meta);
}

//帧编号耗尽时在取得Media帧之前拒绝，不交付重复标识
bool App_FrameDispatcherCanAcquire(void)
{
    return App_EventsIsOwner(RUNTIME_TARGET_CAMERA) && s_epoch != 0U && s_next_frame != UINT32_MAX;
}

//静态服务记录先判断需求，再决定是否取得仅用于副本的原帧
bool App_FrameDispatcherNeedsCopy(void)
{
    return App_EventsIsOwner(RUNTIME_TARGET_CAMERA) &&
        s_storage_service.wants(s_storage_service.context, Platform_TimeGetMs());
}

//查询同样通过Ops+Context，不把服务内部状态暴露给Bridge
static bool Dispatcher_StorageWants(void *context, uint32_t now)
{
    (void)context;
    return App_StorageWantsFrame(now);
}
