#include "usb_service.h"
#include "../../Core/app_messages.h"

#include "../../Tasks/service_tasks.h"
#include "usb_device.h"
#include "usb_uvc.h"
#include "platform_time.h"


//仅由UsbTask写入的设备资源与命令上下文
typedef struct
{
    //所属服务独占的命令邮箱及回执暂存
    AppV3ServiceMailbox_t mailbox;
    //初始化阶段提供的只读配置，生命周期内由所属模块读取
    const VideoConfig_t *config;
    //由服务发布的硬件状态快照
    AppV3HardwareReport_t hardware;
    //正在推进的硬件命令及起始时间
    AppV3HardwareRequest_t request;
    //记录启动失败的原因，供停止回退后报告
    SysResult_t rollback_fault;
    //描述符已按当前输出配置准备成功
    bool descriptor_ready;
    //当前设备会话中UVC类已注册
    bool class_registered;
    //已接管底层资源，退出时必须证明设备停止
    bool stop_required;
    //本次停止调用已成功，仍需核对端点资源
    bool stop_applied;
    //已启动设备，正在等待真实配置状态
    bool waiting_configured;
    //已对当前读者提交取消，仍须等待退出证据
    bool cancel_requested;
} UsbServiceContext_t;

//只有所属任务修改私有状态。发布后API只读取单独保存的队列入口
static UsbServiceContext_t s_usb;
//只保存对外可提交的队列入口，私有上下文仍由服务任务写入
static AppV3Queue_t s_endpoint;

//USB进展复查周期，单位毫秒
#define USB_HW_POLL_MS 10U
//等待主机完成设备配置的上限，单位毫秒
#define USB_HW_ENUM_TIMEOUT_MS 5000U
//等待退出与停止完成的上限，单位毫秒
#define USB_HW_STOP_TIMEOUT_MS 1000U
//对应usb_uvc_class.c的UVC_VIDEO_EP_ADDRESS
#define USB_HW_VIDEO_EP 0x81U
//同一链路快照已确认当前处于配置状态
#define USB_HW_CONFIGURED 1U
//视频端点已完成停止核验
#define USB_HW_VIDEO_QUIET 2U
//帧可能仍有读者持有，禁止提前归还
#define USB_HW_FRAME_HELD 4U

//帧交接状态由UsbTask独立维护，枚举状态不能用于归还帧。
//帧归还必须匹配原UVC提交号，以证明读者已退出。
typedef enum
{
    USB_FRAME_IDLE = 0,
    USB_FRAME_WAIT_OFFER,
    USB_FRAME_CANCEL_PENDING,
    USB_FRAME_CANCEL_SENT,
    USB_FRAME_OFFERED,
    USB_FRAME_READING,
    USB_FRAME_RETURN_PENDING,
    USB_FRAME_WAIT_ACK,
    USB_FRAME_CONSUMED_PENDING
} UsbFrameState_t;

//UsbTask独占的当前帧交接事务
static struct
{
    //从申请推进到消费确认，未闭合前禁止启动下一次交接
    UsbFrameState_t state;
    //本次帧交接与Camera对应的请求序号
    uint32_t request_id;
    //已经受理的帧消息，保留归还交接所需身份
    AppMessage_t transfer;
    //最近一次操作或终结结果，保留原错误码
    SysResult_t result;
} s_frame;

//失联或停止先停止新请求。已经交给本地读者的帧须由读者明确结束读取。
static void UsbFrame_Close(void)
{
    if (s_frame.state == USB_FRAME_WAIT_OFFER)
        s_frame.state = USB_FRAME_CANCEL_PENDING;
    else if (s_frame.state == USB_FRAME_OFFERED)
    {
        s_frame.transfer.frame = (CameraFrame_t){0};
        s_frame.state = USB_FRAME_RETURN_PENDING;
    }
}

//按请求号与租约匹配Camera的帧和归还确认
static void UsbFrame_Receive(const AppMessage_t *message)
{
    if (message->source != RUNTIME_TARGET_CAMERA || message->command_id != s_frame.request_id)
        return;

    if (message->type == APP_MESSAGE_FRAME_OFFER &&
        (s_frame.state == USB_FRAME_WAIT_OFFER || s_frame.state == USB_FRAME_CANCEL_PENDING ||
         s_frame.state == USB_FRAME_CANCEL_SENT))
    {
        //负向OFFER同样需要消费确认，避免最后一笔取消无法退休。
        if (message->result != SYS_OK && message->stream_epoch == 0U && message->lease_id == 0U)
        {
            s_frame.transfer = *message;
            s_frame.result = message->result;
            s_frame.state = USB_FRAME_CONSUMED_PENDING;
        }
        else if (message->result == SYS_OK && message->stream_epoch != 0U && message->lease_id != 0U)
        {
            bool can_read = s_frame.state == USB_FRAME_WAIT_OFFER &&
                message->frame.data != NULL && message->frame.size_bytes != 0U;
            s_frame.transfer = *message;
            s_frame.state = USB_FRAME_OFFERED;
            if (!can_read)
            {
                s_frame.result = SYS_ERR_INVALID_STATE;
                UsbFrame_Close();
            }
        }
    }
    else if (message->type == APP_MESSAGE_RETURN_ACK && s_frame.state == USB_FRAME_WAIT_ACK &&
        message->stream_epoch == s_frame.transfer.stream_epoch &&
        message->lease_id == s_frame.transfer.lease_id && message->result == SYS_OK)
    {
        if (s_frame.result == SYS_ERR_BUSY)
            s_frame.result = SYS_OK;
        s_frame.state = USB_FRAME_CONSUMED_PENDING;
    }
}

//尝试提交待发的归还或确认，队满时保留原记录
static void UsbFrame_Flush(void)
{
    AppMessage_t message = {.source = RUNTIME_TARGET_USB, .command_id = s_frame.request_id};
    UsbFrameState_t after;

    //队列失败时只保留原身份，不得推断已归还或已消费。
    switch (s_frame.state)
    {
        case USB_FRAME_CANCEL_PENDING:
            message.type = APP_MESSAGE_FRAME_CANCEL_REQUEST;
            after = USB_FRAME_CANCEL_SENT;
            break;
        case USB_FRAME_RETURN_PENDING:
            message.type = APP_MESSAGE_FRAME_RETURN;
            after = USB_FRAME_WAIT_ACK;
            break;
        case USB_FRAME_CONSUMED_PENDING:
            message.type = APP_MESSAGE_RETURN_ACK_CONSUMED;
            after = USB_FRAME_IDLE;
            break;
        default:
            return;
    }
    if (s_frame.state != USB_FRAME_CANCEL_PENDING)
    {
        message.stream_epoch = s_frame.transfer.stream_epoch;
        message.lease_id = s_frame.transfer.lease_id;
    }
    if (App_MessagesSend(APP_CHANNEL_TO_CAMERA, &message) == SYS_OK)
    {
        s_frame.state = after;
        if (after == USB_FRAME_IDLE)
            s_frame.transfer = (AppMessage_t){.command_id = 0U};
    }
}

//推进UsbTask的交接事务，停止后只处理尾部消息
static void UsbFrame_Advance(bool closing)
{
    AppMessage_t message;
    uint32_t count;

    if (closing)
        UsbFrame_Close();
    for (count = 0U; count < 4U && App_MessagesReceive(APP_CHANNEL_TO_USB, &message); ++count)
        UsbFrame_Receive(&message);
    if (closing)
        UsbFrame_Close();
    UsbFrame_Flush();
}

//仅UsbTask在准入开放时申请一帧
SysResult_t UsbService_RequestFrame(void)
{
    AppMessage_t request = {.type = APP_MESSAGE_FRAME_REQUEST, .source = RUNTIME_TARGET_USB};
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) || s_usb.hardware.state != APP_V3_HW_ACTIVE ||
        (s_usb.hardware.flags & USB_HW_CONFIGURED) == 0U)
        return SYS_ERR_INVALID_STATE;
    if (s_frame.state != USB_FRAME_IDLE)
        return SYS_ERR_BUSY;
    if (s_frame.request_id == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;

    //发送失败表示请求未受理，下一次仍使用同一个编号。
    request.command_id = s_frame.request_id + 1U;
    result = App_MessagesSend(APP_CHANNEL_TO_CAMERA, &request);
    if (result == SYS_OK)
    {
        s_frame.request_id = request.command_id;
        s_frame.state = USB_FRAME_WAIT_OFFER;
        s_frame.result = SYS_ERR_BUSY;
    }
    return result;
}

//取得当前租约关联的只读帧和分发序号
SysResult_t UsbService_ReadFrame(CameraFrame_t *frame, uint32_t *frame_id)
{
    if (frame == NULL || frame_id == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) || s_usb.hardware.state != APP_V3_HW_ACTIVE ||
        (s_usb.hardware.flags & USB_HW_CONFIGURED) == 0U)
        return SYS_ERR_INVALID_STATE;
    if (s_frame.state != USB_FRAME_OFFERED)
        return s_frame.state == USB_FRAME_IDLE && s_frame.result != SYS_OK ? s_frame.result : SYS_ERR_BUSY;

    //此后直到读者调用Release，都不能根据STOP、超时或失联推断读者已退出。
    *frame = s_frame.transfer.frame;
    *frame_id = s_frame.transfer.frame_id;
    s_frame.state = USB_FRAME_READING;
    return SYS_OK;
}

//确认读取方退出后登记归还，队列满也保留身份
SysResult_t UsbService_ReleaseFrame(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) || s_frame.state != USB_FRAME_READING)
        return SYS_ERR_INVALID_STATE;
    //调用者确认所有读取者已退出。成功只表示提交归还意图。
    s_frame.transfer.frame = (CameraFrame_t){0};
    s_frame.state = USB_FRAME_RETURN_PENDING;
    return SYS_OK;
}

//关闭当前申请入口并推进已有交接退出
SysResult_t UsbService_CancelFrameRequest(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) ||
        (s_frame.state != USB_FRAME_WAIT_OFFER && s_frame.state != USB_FRAME_OFFERED))
        return SYS_ERR_INVALID_STATE;
    UsbFrame_Close();
    return SYS_OK;
}

//本服务已关联的UVC提交号，归还后清零
static uint32_t s_uvc_reader_id;

//只承认真正的完成、取消或关闭作为退场理由
static bool UsbService_IsExitReason(UsbUvcReaderExit_t reason)
{
    return reason == USB_UVC_READER_EXIT_EOF || reason == USB_UVC_READER_EXIT_CANCELLED ||
        reason == USB_UVC_READER_EXIT_CLOSED;
}

//验证单次成功Submit。Class缺失或换代时，只读证明仍关联原提交。
static SysResult_t UsbService_CheckReaderProof(bool *retired)
{
    UsbUvcReaderProof_t proof;
    SysResult_t result = USB_UVC_ReadReaderProof(&proof);

    *retired = false;
    if (result != SYS_OK)
        return result;
    if (proof.accepted_id != s_uvc_reader_id)
        return SYS_ERR_PROTOCOL;
    if (proof.accepted_first_fault != SYS_OK && s_usb.hardware.first_fault == SYS_OK)
        s_usb.hardware.first_fault = proof.accepted_first_fault;
    *retired = proof.retired_id == s_uvc_reader_id && UsbService_IsExitReason(proof.retired_reason);
    return SYS_OK;
}

//先推进旧提交，再交给帧归还和消费确认；这里不允许调用USB_Device_Stop。
static SysResult_t UsbService_AdvanceReader(bool closing)
{
    bool retired;
    UsbUvcStreamStatus_t stream;
    SysResult_t result;

    if (s_uvc_reader_id == 0U)
        return SYS_OK;

    result = UsbService_CheckReaderProof(&retired);
    if (result != SYS_OK)
        return result;
    if (!retired && !closing && (s_usb.hardware.flags & USB_HW_CONFIGURED) != 0U &&
        (USB_UVC_GetStreamStatus(&stream) != SYS_OK || !stream.frame_owned))
    {
        //短暂解绑与再配置可能在两轮之间完成，先重查旧证明以排除恰好退休的竞争。
        result = UsbService_CheckReaderProof(&retired);
        if (result != SYS_OK || !retired)
            return result == SYS_OK ? SYS_ERR_PROTOCOL : result;
    }
    if (!retired && closing)
    {
        //显式停止允许推进取消；失败不关闭旧租约，也不推断Class已退休。
        if (!s_usb.cancel_requested)
        {
            result = USB_UVC_CancelFrame();
            if (result != SYS_OK)
            {
                //读证明可能与Class解绑并发，核对当轮真实末包/Close结果。
                if (UsbService_CheckReaderProof(&retired) != SYS_OK || !retired)
                    return result;
            }
            else
                s_usb.cancel_requested = true;
        }
        if (!retired)
        {
            USB_UVC_Process();
            result = UsbService_CheckReaderProof(&retired);
            if (result != SYS_OK)
                return result;
        }
    }
    if (!retired)
        return SYS_ERR_BUSY;

    result = UsbService_ReleaseFrame();
    if (result == SYS_OK)
    {
        s_uvc_reader_id = 0U;
        s_usb.cancel_requested = false;
        s_usb.hardware.flags &= (uint16_t)~USB_HW_FRAME_HELD;
    }
    return result;
}

//将已借出的JPEG转换成两段只读视图。验证长度、尺寸和上限后才交给Class。
static SysResult_t UsbService_SubmitJpeg(const CameraFrame_t *frame)
{
    VideoFrameView_t view;
    const VideoConfig_t *config = s_usb.config;

    if (frame->format != CAMERA_PIXEL_FORMAT_JPEG || frame->width != config->camera.width ||
        frame->height != config->camera.height || frame->size_bytes < 4U ||
        frame->size_bytes2 >= frame->size_bytes ||
        (frame->size_bytes2 == 0U) != (frame->data2 == NULL))
        return SYS_ERR_PROTOCOL;

    //超出UVC配置值的帧正常跳过并归还，绝不截断JPEG。
    if (frame->size_bytes > config->output_max_frame_bytes)
        return SYS_ERR_BUSY;
    view = (VideoFrameView_t){
        .spans = {{frame->data, frame->size_bytes - frame->size_bytes2}, {frame->data2, frame->size_bytes2}},
        .size_bytes = frame->size_bytes,
        .span_count = frame->size_bytes2 == 0U ? 1U : 2U
    };
    if (!VideoFrameView_Valid(&view))
        return SYS_ERR_PROTOCOL;
    return USB_UVC_SubmitFrameView(&view);
}

//检查链路与提交凭据后按当前格式交给UVC读取
static SysResult_t UsbService_SubmitVideo(void)
{
    UsbUvcStreamStatus_t stream;
    UsbUvcReaderProof_t before;
    CameraFrame_t frame;
    uint32_t frame_id;
    SysResult_t result;

    //只有Host真实协商并打开Alt1，才允许申请或提交业务帧。
    if (s_uvc_reader_id != 0U || (s_frame.state != USB_FRAME_IDLE && s_frame.state != USB_FRAME_OFFERED))
        return SYS_ERR_BUSY;
    result = USB_UVC_GetStreamStatus(&stream);
    if (result != SYS_OK || !stream.can_submit)
        return SYS_ERR_BUSY;
    if (s_frame.state == USB_FRAME_IDLE)
        return UsbService_RequestFrame();

    result = USB_UVC_ReadReaderProof(&before);
    if (result != SYS_OK)
        return result;
    if (before.accepted_id != before.retired_id || before.accepted_id == UINT32_MAX)
        return SYS_ERR_BUSY;

    result = UsbService_ReadFrame(&frame, &frame_id);
    if (result != SYS_OK)
        return result;
    if (s_usb.config->camera.format == CAMERA_PIXEL_FORMAT_JPEG)
        result = UsbService_SubmitJpeg(&frame);
    else if (frame.format != CAMERA_PIXEL_FORMAT_YUV422 || frame.width != 160U || frame.height != 120U ||
        frame.size_bytes != 38400U || frame.size_bytes2 != 0U || frame.data2 != NULL)
        result = SYS_ERR_PROTOCOL;
    else
        result = USB_UVC_SubmitFrame(frame.data, frame.size_bytes);

    if (result == SYS_OK)
    {
        //唯一UsbTask成功提交一次，IRQ只退休不分配ID；留存预期ID可跨解绑核对。
        s_uvc_reader_id = before.accepted_id + 1U;
        s_usb.hardware.flags |= USB_HW_FRAME_HELD;
        return SYS_OK;
    }

    //提交明确失败时Class从未接管该帧，才可直接归还给CameraTask。
    if (UsbService_ReleaseFrame() != SYS_OK)
        return SYS_ERR_PROTOCOL;
    return result == SYS_ERR_INVALID_STATE ? SYS_ERR_BUSY : result;
}

//准备描述符并注册启动设备，失败时保留停止义务
static SysResult_t UsbService_BeginDevice(void)
{
    SysResult_t result;

    if (!s_usb.descriptor_ready)
    {
        VideoStreamConfig_t config = {s_usb.config->camera, s_usb.config->output_fps,
            s_usb.config->output_max_frame_bytes};
        result = USB_UVC_Configure(&config);
        if (result != SYS_OK)
            return result;
        s_usb.descriptor_ready = true;
    }
    //Register和Start均可能留下待STOP的端点或FIFO配置，调用前由USB Service登记清理义务
    s_usb.stop_required = true;
    s_usb.stop_applied = false;

    if (!s_usb.class_registered)
    {
        result = USB_UVC_Register();
        if (result != SYS_OK)
            return result;
        s_usb.class_registered = true;
    }
    result = USB_Device_Start();
    if (result == SYS_OK)
        s_usb.waiting_configured = true;
    return result;
}

//已有Class事实优先；INVALID_STATE只说明没有活动Class，不能单独证明STOP
static SysResult_t UsbService_CheckFrame(void)
{
    UsbUvcStreamStatus_t stream;
    SysResult_t result = USB_UVC_GetStreamStatus(&stream);

    if (result == SYS_ERR_INVALID_STATE)
    {
        //上一轮已观察到持有时，丢失Class查询不能当成归还
        return (s_usb.hardware.flags & USB_HW_FRAME_HELD) != 0U ? SYS_ERR_BUSY : SYS_OK;
    }
    if (result != SYS_OK)
        return result;
    s_usb.hardware.primary = stream.completed_frames;
    if (!stream.frame_owned)
    {
        s_usb.hardware.flags &= (uint16_t)~USB_HW_FRAME_HELD;
        return SYS_OK;
    }

    //已登记读者由前置流程核验退休，此处阻止未登记的Class持帧越过整设备STOP
    s_usb.hardware.flags |= USB_HW_FRAME_HELD;
    if (s_usb.cancel_requested && stream.result != SYS_OK && stream.result != SYS_ERR_BUSY)
        return stream.result;
    if (!s_usb.cancel_requested)
    {
        result = USB_UVC_CancelFrame();
        if (result != SYS_OK)
            return result;
        s_usb.cancel_requested = true;
    }
    USB_UVC_Process();
    return SYS_ERR_BUSY;
}

//先确认无源帧，再执行Pcd STOP及Class DeInit，并检查视频端点已经失效
static SysResult_t UsbService_StopDevice(void)
{
    UsbDeviceEndpointState_t endpoint;
    SysResult_t result;

    //既有UVC停止仍独立执行；持有或未确认的交接不能提前宣告READY。
    if (s_frame.state != USB_FRAME_IDLE)
        return SYS_ERR_BUSY;
    if (!s_usb.stop_required)
        return SYS_OK;
    if (!s_usb.stop_applied)
    {
        result = UsbService_CheckFrame();
        if (result != SYS_OK)
            return result;
        result = USB_Device_Stop();
        if (result != SYS_OK)
            return result;
        s_usb.stop_applied = true;
    }
    result = USB_Device_GetEpState(USB_HW_VIDEO_EP, &endpoint);
    if (result != SYS_OK)
        return result;
    if (USB_Device_IsConfigured() || endpoint.opened || endpoint.busy)
        return SYS_ERR_BUSY;
    s_usb.stop_required = false;
    s_usb.stop_applied = false;
    s_usb.waiting_configured = false;
    s_usb.cancel_requested = false;
    s_usb.hardware.flags = USB_HW_VIDEO_QUIET;
    return SYS_OK;
}

//真实START、STOP和RECOVER只登记当前命令，QUERY返回实际状态快照
static SysResult_t UsbService_HardwareCommand(const AppV3ServiceCommand_t *command, SysResult_t result)
{
    AppV3HardwareState_t state = s_usb.hardware.state;

    if (result != SYS_OK || command->operation == USB_SERVICE_HARDWARE_QUERY)
        return AppV3Mailbox_CompleteHardware(&s_usb.mailbox, command, result, &s_usb.hardware);
    if (command->operation == USB_SERVICE_START)
    {
        if (state != APP_V3_HW_READY)
            result = state == APP_V3_HW_ACTIVE ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_INVALID_STATE;
        else
            s_usb.hardware = (AppV3HardwareReport_t){.state = APP_V3_HW_STARTING};
    }
    else if (state == APP_V3_HW_ACTIVE || state == APP_V3_HW_FAULT)
        s_usb.hardware.state = APP_V3_HW_QUIESCING;
    else
        return AppV3Mailbox_CompleteHardware(&s_usb.mailbox, command,
            state == APP_V3_HW_READY ? SYS_OK : SYS_ERR_INVALID_STATE, &s_usb.hardware);

    if (result != SYS_OK)
        return AppV3Mailbox_CompleteHardware(&s_usb.mailbox, command, result, &s_usb.hardware);
    s_usb.rollback_fault = SYS_OK;
    //显式恢复允许重新推进取消，但未证明的源帧持有标志仍保留
    s_usb.cancel_requested = false;
    s_usb.request = (AppV3HardwareRequest_t){*command, Platform_TimeGetMs(), true};
    return SYS_OK;
}

//更新硬件状态并带原编号发布生命周期终结
static void UsbService_FinishHardware(SysResult_t result, AppV3HardwareState_t state)
{
    if (result != SYS_OK && s_usb.hardware.first_fault == SYS_OK)
        s_usb.hardware.first_fault = result;
    s_usb.hardware.state = state;
    s_usb.request.pending = false;
    (void)AppV3Mailbox_CompleteHardware(&s_usb.mailbox, &s_usb.request.command, result, &s_usb.hardware);
}


//仅ACTIVE允许健康挂起；START仍须观察到真实CONFIGURED
static SysResult_t UsbService_ObserveDevice(bool *suspended)
{
    SysResult_t result;
    UsbDeviceLinkState_t link;
    bool configured;
    configured = USB_Device_ReadLinkState(&link);
    if (configured)
        s_usb.hardware.flags |= USB_HW_CONFIGURED;
    else
        s_usb.hardware.flags &= (uint16_t)~USB_HW_CONFIGURED;

    //挂起保留USB所有权和现有持有标志，暂停UVC推进直到主机恢复配置
    if (suspended != NULL)
        *suspended = !configured && link.dev_state == USBD_STATE_SUSPENDED &&
            link.dev_old_state == USBD_STATE_CONFIGURED && link.started &&
            !link.stop_required && link.last_result == SYS_OK;
    if (suspended == NULL || !*suspended)
        USB_UVC_Process();
    result = USB_Device_GetLastResult();
    //采样时已有设备级错误时保留它，后续成功不能掩盖
    if (suspended != NULL && result == SYS_OK)
        result = link.last_result;
    if (result == SYS_OK && configured)
    {
        UsbUvcStreamStatus_t stream;
        result = USB_UVC_GetStreamStatus(&stream);
        if (result == SYS_ERR_INVALID_STATE)
            return SYS_ERR_BUSY;
        if (result == SYS_OK)
        {
            s_usb.hardware.primary = stream.completed_frames;
            if (stream.frame_owned)
            {
                //只认可本任务已绑定提交的占用，未知读者保留故障。
                s_usb.hardware.flags |= USB_HW_FRAME_HELD;
                if (s_uvc_reader_id != 0U)
                    return SYS_OK;
                return SYS_ERR_INVALID_STATE;
            }
            //这里只观察当前Class，旧提交的持有标记必须等匹配的退休凭据再清除。
            if (s_uvc_reader_id != 0U)
                return SYS_OK;
            s_usb.hardware.flags &= (uint16_t)~USB_HW_FRAME_HELD;
        }
    }
    return result;
}

//仅UsbTask推进枚举、按配置执行视频帧提交及有界STOP。
uint32_t AppV3_UsbAdvance(void)
{
    uint32_t now;
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return APP_EVENTS_WAIT_FOREVER;
    if (s_usb.hardware.state == APP_V3_HW_ACTIVE)
    {
        bool suspended;
        result = UsbService_ObserveDevice(&suspended);
        if (((s_usb.hardware.flags & USB_HW_CONFIGURED) == 0U && (!suspended || result != SYS_OK)) ||
            (result != SYS_OK && result != SYS_ERR_BUSY))
        {
            if (s_usb.hardware.first_fault == SYS_OK)
                s_usb.hardware.first_fault = result == SYS_OK || result == SYS_ERR_BUSY ? SYS_ERR_IO : result;
            s_usb.hardware.state = APP_V3_HW_FAULT;
        }
    }

    //STOP前先推进匹配旧读者；帧交接退出证明未闭合仍不能调用Device STOP。
    result = UsbService_AdvanceReader(s_usb.hardware.state != APP_V3_HW_ACTIVE);
    if (result != SYS_OK && result != SYS_ERR_BUSY)
    {
        if (s_usb.hardware.first_fault == SYS_OK)
            s_usb.hardware.first_fault = result;
        if (s_usb.hardware.state == APP_V3_HW_ACTIVE)
            s_usb.hardware.state = APP_V3_HW_FAULT;
    }
    //停止或异常只关闭申请，待发Return和消费确认在FAULT中继续排空。
    UsbFrame_Advance(s_usb.hardware.state != APP_V3_HW_ACTIVE);
    if (s_usb.hardware.state == APP_V3_HW_ACTIVE && (s_usb.hardware.flags & USB_HW_CONFIGURED) != 0U)
    {
        result = UsbService_SubmitVideo();
        if (result != SYS_OK && result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
        {
            if (s_usb.hardware.first_fault == SYS_OK)
                s_usb.hardware.first_fault = result;
            s_usb.hardware.state = APP_V3_HW_FAULT;
        }
    }
    if (s_usb.hardware.state == APP_V3_HW_ACTIVE)
        return USB_HW_POLL_MS;
    if (!s_usb.request.pending)
        return s_frame.state != USB_FRAME_IDLE ? USB_HW_POLL_MS : APP_EVENTS_WAIT_FOREVER;

    now = Platform_TimeGetMs();
    if (s_usb.hardware.state == APP_V3_HW_STARTING)
    {
        result = now - s_usb.request.started_ms >= USB_HW_ENUM_TIMEOUT_MS ? SYS_ERR_TIMEOUT :
            (s_usb.waiting_configured ? UsbService_ObserveDevice(NULL) : UsbService_BeginDevice());
        if (result == SYS_OK && (s_usb.hardware.flags & USB_HW_CONFIGURED) != 0U)
            UsbService_FinishHardware(SYS_OK, APP_V3_HW_ACTIVE);
        if (result != SYS_OK && result != SYS_ERR_BUSY)
        {
            s_usb.rollback_fault = result;
            s_usb.hardware.first_fault = result;
            s_usb.hardware.state = APP_V3_HW_QUIESCING;
            s_usb.request.started_ms = now;
        }
        return USB_HW_POLL_MS;
    }

    result = UsbService_StopDevice();
    if (result == SYS_ERR_BUSY && now - s_usb.request.started_ms < USB_HW_STOP_TIMEOUT_MS)
        return USB_HW_POLL_MS;
    if (result == SYS_ERR_BUSY)
        result = SYS_ERR_TIMEOUT;
    UsbService_FinishHardware(s_usb.rollback_fault != SYS_OK ? s_usb.rollback_fault : result,
        result == SYS_OK ? APP_V3_HW_READY : APP_V3_HW_FAULT);
    return USB_HW_POLL_MS;
}

//一次可读回调只接收一条命令，Event Loop负责控制批次预算
static SysResult_t UsbService_Readable(AppV3Queue_t queue, void *argument)
{
    UsbServiceContext_t *context = &s_usb;
    AppV3ServiceCommand_t command = {0};
    SysResult_t result;
    (void)argument;

    //所属任务与队列都要匹配，禁止跨Service取消息
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) ||
        queue.index != context->mailbox.queue.index ||
        queue.generation != context->mailbox.queue.generation)
        return SYS_ERR_INVALID_STATE;

    if (context->request.pending)
        return SYS_ERR_BUSY;
    result = AppV3Mailbox_Receive(&context->mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;

    if (command.operation >= USB_SERVICE_START && command.operation <= USB_SERVICE_RECOVER)
        return UsbService_HardwareCommand(&command, result);

    if (result == SYS_OK)
        result = SYS_ERR_NOT_SUPPORTED;
    return AppV3Mailbox_Complete(&context->mailbox, &command, result, false, 0U);
}

//准备本服务的配置、队列与事件订阅，再由Registry发布Record
SysResult_t AppV3_UsbInit(const AppV3Context_t *context)
{
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return SYS_ERR_INVALID_STATE;
    if (s_usb.config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context == NULL || context->video == NULL)
        return SYS_ERR_INVALID_STATE;

    //准备阶段只检查输出选择，USB端点在准备时保持关闭
    if (context->video->output != VIDEO_OUTPUT_USB_UVC)
        return SYS_ERR_NOT_SUPPORTED;
    result = AppV3Mailbox_Init(&s_usb.mailbox, RUNTIME_TARGET_USB, UsbService_Readable, &s_endpoint);
    if (result == SYS_OK)
    {
        s_usb.config = context->video;
        s_usb.hardware.state = APP_V3_HW_READY;
    }
    return result;
}

//Record发布失败时只回收未公开的队列和订阅
void AppV3_UsbDiscard(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return;
    AppV3Mailbox_Discard(&s_usb.mailbox, &s_endpoint);
    s_usb.config = NULL;
    s_usb.hardware.state = APP_V3_HW_UNINITIALIZED;
}

//公开API持有Record期间提交值类型消息，发送方不接触私有Context
SysResult_t UsbService_Request(uint32_t command_id, UsbServiceOperation_t operation, uint32_t argument)
{
    return AppV3Mailbox_Request(RUNTIME_TARGET_USB, &s_endpoint, command_id, operation, argument);
}

//固定Registry指向本服务的实际线程入口

//从UsbTask上下文运行队列和硬件事件循环
void AppV3_UsbRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_usb.mailbox, AppV3_UsbAdvance);
}
