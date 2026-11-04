#include "app_uvc_bridge.h"
#include "video_config.h"
#include "usb_mode.h"
#include "framework_init.h"
#include "camera_capture.h"
#include "usb_uvc.h"
#include <stddef.h>
#include <string.h>

//USB侧只在收到ReturnAck后才允许请求下一帧
typedef enum
{
    BRIDGE_USB_IDLE = 0,
    BRIDGE_USB_REQUEST,
    BRIDGE_USB_HELD,
    BRIDGE_USB_LOANED,
    BRIDGE_USB_RETURN
} BridgeUsbState_t;

//Camera侧上下文只由CameraTask修改
typedef struct
{
    uint32_t epoch;
    uint32_t next_lease;
    bool held;
    bool request_pending;
    bool offer_pending;
    bool ack_pending;
    bool release_pending;
    uint32_t request_id;
    AppMessage_t offer;
    AppMessage_t ack;
    AppMessage_t last_ack;
} BridgeCamera_t;

//USB侧上下文只由UsbTask修改
typedef struct
{
    BridgeUsbState_t state;
    uint32_t next_request;
    bool request_sent;
    bool return_sent;
    AppMessage_t lease;
} BridgeUsb_t;

//保存Camera所有者的租借账本
static BridgeCamera_t s_camera_bridge;

//保存USB所有者的租借账本
static BridgeUsb_t s_usb_bridge;

//保存冷启动时接受的唯一帧配置
static CameraConfig_t s_bridge_config;

//保存当前配置对应的帧字节数
static uint32_t s_bridge_frame_bytes;

//记录图像参数是否已经校验保存
static bool s_bridge_configured;

//记录输出准备已经完成
static bool s_bridge_output_prepared;

//提供只读调试计数，不参与跨任务释放判断
volatile AppBridgeDebug_t g_bridge_debug;

//准备冷启动输出配置
static SysResult_t App_UvcBridgePrepareOutput(void);

//比较租借的全部身份凭据
static bool Bridge_SameLease(const AppMessage_t *a, const AppMessage_t *b);

//提交Camera侧尚未送达的回复
static bool Bridge_CameraFlush(void);

//取得持有帧的Media归还确认
static bool Bridge_CameraRelease(void);

//向Camera提交帧归还
static void Bridge_UsbReturn(void);

//保存桥接所需的静态图像配置
SysResult_t App_UvcBridgeConfigure(const CameraConfig_t *config)
{
    uint32_t bytes;
    SysResult_t result;

    if (s_bridge_configured)
        return SYS_ERR_INVALID_STATE;
    result = CameraConfig_GetFrameSize(config, &bytes);
    if (result != SYS_OK)
        return result;
    if (config->format != CAMERA_PIXEL_FORMAT_YUV422)
        return SYS_ERR_NOT_SUPPORTED;
    s_bridge_config = *config;
    s_bridge_frame_bytes = bytes;
    s_bridge_configured = true;
    return SYS_OK;
}

//建立新的采集代次
SysResult_t App_UvcBridgeCameraBegin(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA) || !s_bridge_configured)
        return SYS_ERR_INVALID_STATE;
    if (!App_UvcBridgeCameraIsIdle())
        return SYS_ERR_BUSY;
    if (s_camera_bridge.epoch == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;

    //即使后续硬件启动失败，也不重用已经发布过的epoch
    s_camera_bridge.epoch++;
    s_camera_bridge.next_lease = 0U;
    g_bridge_debug.stream_epoch = s_camera_bridge.epoch;
    return SYS_OK;
}

//确认Camera侧不再持有租借帧
bool App_UvcBridgeCameraIsIdle(void)
{
    return App_MessagesIsOwner(RUNTIME_TARGET_CAMERA) &&
        !s_camera_bridge.held && !s_camera_bridge.offer_pending &&
        !s_camera_bridge.ack_pending && !s_camera_bridge.request_pending;
}

//由CameraTask推进请求、Offer和归还确认
void App_UvcBridgeCameraProcess(bool enabled)
{
    AppMessage_t message;
    uint32_t i;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA) || !s_bridge_configured)
        return;

    //队列满时保留完整回复和凭据，不能取得第二张Media帧
    if (!Bridge_CameraFlush())
        return;
    if (s_camera_bridge.release_pending && !Bridge_CameraRelease())
        return;
    if (!Bridge_CameraFlush())
        return;

    for (i = 0U; i < 4U && App_MessagesReceive(APP_CHANNEL_TO_CAMERA, &message); i++)
    {
        if (message.type == APP_MESSAGE_FRAME_REQUEST)
        {

            //一个USB所有者最多有一个未完成请求
            if (message.command_id == 0U || s_camera_bridge.held || s_camera_bridge.request_pending)
            {
                g_bridge_debug.rejected_messages++;
                continue;
            }
            s_camera_bridge.request_id = message.command_id;
            s_camera_bridge.request_pending = true;
        }
        else if (message.type == APP_MESSAGE_FRAME_RETURN)
        {
            if (s_camera_bridge.held && Bridge_SameLease(&message, &s_camera_bridge.offer))
            {

                //只有当前epoch和lease都一致，才允许调用无参Media释放接口
                s_camera_bridge.release_pending = true;
                if (!Bridge_CameraRelease() || !Bridge_CameraFlush())
                    return;
            }
            else if (s_camera_bridge.last_ack.lease_id != 0U &&
                Bridge_SameLease(&message, &s_camera_bridge.last_ack))
            {

                //重复Return只重发已完成的ACK，不能再次释放Media
                s_camera_bridge.ack = s_camera_bridge.last_ack;
                s_camera_bridge.ack_pending = true;
                if (!Bridge_CameraFlush())
                    return;
            }
            else
                g_bridge_debug.rejected_messages++;
        }
        else
            g_bridge_debug.rejected_messages++;
    }

    if (!s_camera_bridge.request_pending || s_camera_bridge.held)
        return;
    memset(&s_camera_bridge.offer, 0, sizeof(s_camera_bridge.offer));
    s_camera_bridge.offer.type = APP_MESSAGE_FRAME_OFFER;
    s_camera_bridge.offer.source = RUNTIME_TARGET_CAMERA;
    s_camera_bridge.offer.command_id = s_camera_bridge.request_id;
    s_camera_bridge.offer.stream_epoch = s_camera_bridge.epoch;

    if (enabled)
    {
        if (s_camera_bridge.next_lease == UINT32_MAX)
            s_camera_bridge.offer.result = SYS_ERR_NO_RESOURCE;
        else if (!CameraCapture_StreamGetFrame(&s_camera_bridge.offer.frame))
            return;
        else
        {

            //先登记持有再发送，USB高优先级任务可能在入队后立即运行
            s_camera_bridge.held = true;
            s_camera_bridge.offer.lease_id = ++s_camera_bridge.next_lease;
            g_bridge_debug.acquired_frames++;
        }
    }
    else
        s_camera_bridge.offer.result = SYS_ERR_INVALID_STATE;

    s_camera_bridge.request_pending = false;
    s_camera_bridge.offer_pending = true;
    (void)Bridge_CameraFlush();
}

//由UsbTask推进请求、借用和Return
void App_UvcBridgeUsbProcess(bool enabled)
{
    UsbUvcStreamStatus_t status;
    SysResult_t result;
    AppMessage_t message;
    uint32_t i;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) || !s_bridge_configured)
        return;

    for (i = 0U; i < 4U && App_MessagesReceive(APP_CHANNEL_TO_USB, &message); i++)
    {
        if (message.type == APP_MESSAGE_RETURN_ACK && s_usb_bridge.state == BRIDGE_USB_RETURN &&
            s_usb_bridge.return_sent && Bridge_SameLease(&message, &s_usb_bridge.lease) &&
            message.result == SYS_OK)
        {

            //Camera已实际释放后，USB才清除本轮凭据并允许下一次请求
            memset(&s_usb_bridge.lease, 0, sizeof(s_usb_bridge.lease));
            s_usb_bridge.state = BRIDGE_USB_IDLE;
        }
        else if (message.type == APP_MESSAGE_FRAME_OFFER && s_usb_bridge.state == BRIDGE_USB_REQUEST &&
            s_usb_bridge.request_sent && message.command_id == s_usb_bridge.lease.command_id)
        {
            if (message.result != SYS_OK && message.lease_id == 0U)
            {
                s_usb_bridge.state = BRIDGE_USB_IDLE;
                g_bridge_debug.usb_result = message.result;
                return;
            }
            if (message.stream_epoch == 0U || message.lease_id == 0U)
            {
                g_bridge_debug.usb_rejected_messages++;
                continue;
            }
            s_usb_bridge.lease = message;
            s_usb_bridge.state = BRIDGE_USB_HELD;
        }
        else
            g_bridge_debug.usb_rejected_messages++;
    }

    if (s_usb_bridge.state == BRIDGE_USB_LOANED)
    {
        if (!enabled)
            (void)USB_UVC_CancelFrame();
        result = USB_UVC_GetStreamStatus(&status);
        if ((result == SYS_OK && !status.frame_owned) || result == SYS_ERR_INVALID_STATE)
        {

            //仅合法UsbTask环境下，Class安全销毁才可把INVALID_STATE视作归还证明
            s_usb_bridge.state = BRIDGE_USB_RETURN;
            s_usb_bridge.return_sent = false;
        }
        else
            return;
    }

    if (s_usb_bridge.state == BRIDGE_USB_HELD)
    {
        CameraFrame_t *frame = &s_usb_bridge.lease.frame;
        if (!enabled || frame->data == NULL || frame->width != s_bridge_config.width ||
            frame->height != s_bridge_config.height || frame->format != s_bridge_config.format ||
            frame->size_bytes != s_bridge_frame_bytes)
            result = SYS_ERR_INVALID_ARG;
        else
            result = USB_UVC_SubmitFrame(frame->data, frame->size_bytes);
        g_bridge_debug.usb_result = result;
        if (result == SYS_OK)
        {
            s_usb_bridge.state = BRIDGE_USB_LOANED;
            g_bridge_debug.submitted_frames++;
            return;
        }

        //提交失败未借用新帧，仍必须通过Return/ACK归还Camera
        s_usb_bridge.state = BRIDGE_USB_RETURN;
        s_usb_bridge.return_sent = false;
    }
    if (s_usb_bridge.state == BRIDGE_USB_RETURN)
    {
        Bridge_UsbReturn();
        return;
    }
    if (s_usb_bridge.state == BRIDGE_USB_IDLE)
    {
        result = USB_UVC_GetStreamStatus(&status);
        if (!enabled || result != SYS_OK || !status.can_submit)
            return;
        if (s_usb_bridge.next_request == UINT32_MAX)
        {
            g_bridge_debug.usb_result = SYS_ERR_NO_RESOURCE;
            return;
        }
        memset(&s_usb_bridge.lease, 0, sizeof(s_usb_bridge.lease));
        s_usb_bridge.lease.type = APP_MESSAGE_FRAME_REQUEST;
        s_usb_bridge.lease.source = RUNTIME_TARGET_USB;
        s_usb_bridge.lease.command_id = ++s_usb_bridge.next_request;
        s_usb_bridge.request_sent = false;
        s_usb_bridge.state = BRIDGE_USB_REQUEST;
    }
    if (s_usb_bridge.state == BRIDGE_USB_REQUEST && !s_usb_bridge.request_sent)
    {
        if (!enabled)
        {

            //尚未入队的请求没有占用Media，可直接撤销
            s_usb_bridge.state = BRIDGE_USB_IDLE;
            return;
        }
        if (App_MessagesSend(APP_CHANNEL_TO_CAMERA, &s_usb_bridge.lease) == SYS_OK)
            s_usb_bridge.request_sent = true;
        else
            g_bridge_debug.usb_queue_retries++;
    }
}

//确认USB侧已完成归还握手
bool App_UvcBridgeUsbIsIdle(void)
{
    return App_MessagesIsOwner(RUNTIME_TARGET_USB) && s_usb_bridge.state == BRIDGE_USB_IDLE;
}

//比较租借的全部身份凭据
static bool Bridge_SameLease(const AppMessage_t *a, const AppMessage_t *b)
{
    return a->stream_epoch == b->stream_epoch && a->lease_id == b->lease_id &&
        a->command_id == b->command_id;
}

//提交Camera侧尚未送达的回复
static bool Bridge_CameraFlush(void)
{
    if (s_camera_bridge.ack_pending)
    {
        if (App_MessagesSend(APP_CHANNEL_TO_USB, &s_camera_bridge.ack) != SYS_OK)
        {
            g_bridge_debug.queue_retries++;
            return false;
        }
        s_camera_bridge.ack_pending = false;
    }
    if (s_camera_bridge.offer_pending)
    {
        if (App_MessagesSend(APP_CHANNEL_TO_USB, &s_camera_bridge.offer) != SYS_OK)
        {
            g_bridge_debug.queue_retries++;
            return false;
        }
        s_camera_bridge.offer_pending = false;
    }
    return true;
}

//取得持有帧的Media归还确认
static bool Bridge_CameraRelease(void)
{
    SysResult_t result = CameraCapture_StreamReleaseFrame();
    g_bridge_debug.camera_result = result;
    if (result != SYS_OK)
        return false;

    //Media确认释放后才生成成功ACK，队列满也不会再次调用Release
    s_camera_bridge.held = false;
    s_camera_bridge.release_pending = false;
    s_camera_bridge.ack = s_camera_bridge.offer;
    s_camera_bridge.ack.type = APP_MESSAGE_RETURN_ACK;
    s_camera_bridge.ack.result = SYS_OK;
    s_camera_bridge.ack_pending = true;
    s_camera_bridge.last_ack = s_camera_bridge.ack;
    g_bridge_debug.released_frames++;
    return true;
}

//向Camera提交帧归还
static void Bridge_UsbReturn(void)
{
    AppMessage_t message = s_usb_bridge.lease;
    if (s_usb_bridge.return_sent)
        return;
    message.type = APP_MESSAGE_FRAME_RETURN;
    message.source = RUNTIME_TARGET_USB;
    if (App_MessagesSend(APP_CHANNEL_TO_CAMERA, &message) == SYS_OK)
        s_usb_bridge.return_sent = true;
    else
        g_bridge_debug.usb_queue_retries++;
}

//冷启动只保存参数和Class选择，不启动采集或USB连接
FRAMEWORK_INIT_REGISTER(App_UvcBridgePrepareOutput, INIT_LEVEL_PLATFORM);

static SysResult_t App_UvcBridgePrepareOutput(void)
{
    const VideoConfig_t *config = VideoConfig_GetDefault();
    VideoStreamConfig_t stream;
    SysResult_t result;
    UsbMode_t mode;

    if (s_bridge_output_prepared)
        return SYS_ERR_INVALID_STATE;
    if (config->output == VIDEO_OUTPUT_ETH || config->output == VIDEO_OUTPUT_WIFI)
        return SYS_ERR_NOT_SUPPORTED;

    if (config->output == VIDEO_OUTPUT_USB_UVC)
    {
        stream.camera = config->camera;
        stream.fps = config->output_fps;
        result = USB_UVC_ValidateConfig(&stream);
        if (result != SYS_OK)
            return result;
        result = USB_UVC_Configure(&stream);
        if (result != SYS_OK)
            return result;
        result = App_UvcBridgeConfigure(&config->camera);
        if (result != SYS_OK)
            return result;
        mode = USB_MODE_UVC;
    }
    else if (config->output == VIDEO_OUTPUT_USB_TEST)
        mode = USB_MODE_TEST;
    else if (config->output == VIDEO_OUTPUT_NONE)
        mode = USB_MODE_NONE;
    else
        return SYS_ERR_INVALID_ARG;

    result = USB_Mode_SelectBootMode(mode, mode == USB_MODE_UVC ? USB_UVC_Register : NULL);
    if (result != SYS_OK)
        return result;
    s_bridge_output_prepared = true;
    return SYS_OK;
}
