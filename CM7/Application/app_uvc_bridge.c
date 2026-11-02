#include "app_uvc_bridge.h"
#include "video_config.h"
#include "usb_mode.h"
#include "framework_init.h"
#include "camera_capture.h"
#include "usb_uvc.h"
#include "stm32h7xx.h"
#include <stddef.h>

typedef enum
{
    UVC_BRIDGE_IDLE = 0, //没有持有Media图像
    UVC_BRIDGE_HELD,     //持有图像，USB尚未借用或已归还
    UVC_BRIDGE_LOANED    //USB仍可能访问图像
} UvcBridgeState_t;

typedef struct
{
    UvcBridgeState_t state;
    CameraFrame_t frame;

    bool release_pending; //归还失败后，只重试释放

    uint32_t submitted_frames;
    uint32_t released_frames;
    uint32_t rejected_frames;
} UvcBridgeContext_t;

static CameraConfig_t s_bridge_config;
static uint32_t s_bridge_frame_bytes;
static bool s_bridge_configured;
static VideoOutput_t s_bridge_output;
static bool s_bridge_output_prepared;

//USB应用在注册Class前读取配置，不初始化摄像头或Media
static SysResult_t App_UvcBridgePrepareOutput(void);

//唯一桥接上下文，零初始化时处于IDLE
static UvcBridgeContext_t s_bridge;

//USB应用在注册Class前读取配置，不初始化摄像头和Media
static SysResult_t App_UvcBridgePrepareOutput(void);

//归还Media图像，成功后才清除本地持有状态
static void UvcBridge_ReleaseFrame(void);

SysResult_t App_UvcBridgeConfigure(const CameraConfig_t *config)
{
    uint32_t bytes;
    SysResult_t result;

    if (s_bridge.state != UVC_BRIDGE_IDLE || s_bridge_configured)
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

void App_UvcBridgeProcess(void)
{
    UsbUvcStreamStatus_t status;
    SysResult_t result;
	
    if (s_bridge_output_prepared && s_bridge_output != VIDEO_OUTPUT_USB_UVC)
        return;

    //仅允许CM7正常主循环调用，IRQ返回值不能作为归还证明
    if ( !s_bridge_configured || __get_IPSR() != 0U || __get_PRIMASK() != 0U)
        return;

    switch (s_bridge.state)
    {
        case UVC_BRIDGE_IDLE:
        {
            //USB尚未完成Commit和Alt1时，不提前占用Media缓冲区
            result = USB_UVC_GetStreamStatus(&status);
            if ( result != SYS_OK || !status.can_submit)
                return;

            if ( !CameraCapture_StreamGetFrame(&s_bridge.frame))
                return;

            //获取成功即持有图像，非法帧也必须归还Media
            s_bridge.state = UVC_BRIDGE_HELD;
            if ( s_bridge.frame.width != s_bridge_config.width ||
                 s_bridge.frame.height != s_bridge_config.height ||
                 s_bridge.frame.format != s_bridge_config.format ||
                 s_bridge.frame.size_bytes != s_bridge_frame_bytes ||
                 s_bridge.frame.data == NULL)
            {
                s_bridge.rejected_frames++;
                UvcBridge_ReleaseFrame();
            }
            return;
        }

        case UVC_BRIDGE_HELD:
        {
            //上次归还失败，不允许再次提交同一帧
            if ( s_bridge.release_pending)
            {
                UvcBridge_ReleaseFrame();
                return;
            }

            result = USB_UVC_SubmitFrame(
                s_bridge.frame.data,
                s_bridge.frame.size_bytes
            );

            if ( result == SYS_OK)
            {
                //提交成功只代表借用，必须保留原图像
                s_bridge.state = UVC_BRIDGE_LOANED;
                s_bridge.submitted_frames++;
            }
            else
            {
                //失败没有借用新帧，归还失败则保留HELD重试
                s_bridge.rejected_frames++;
                UvcBridge_ReleaseFrame();
            }
            return;
        }

        case UVC_BRIDGE_LOANED:
        {
            result = USB_UVC_GetStreamStatus(&status);
            if ( result == SYS_OK)
            {
                //即使发生错误或取消，USB仍持有时也不能归还Media
                if ( status.frame_owned)
                    return;
            }
            else if ( result != SYS_ERR_INVALID_STATE)
            {
                //其他查询错误不能证明USB已停止访问图像
                return;
            }

            //主循环下INVALID_STATE表示Class已安全销毁
            //或有效快照确认frame_owned为false，才允许归还
            UvcBridge_ReleaseFrame();
            return;
        }

        default:
            return;
    }
}

static void UvcBridge_ReleaseFrame(void)
{
    //调用前必须确认USB没有借用，或已安全归还
    s_bridge.state = UVC_BRIDGE_HELD;
    s_bridge.release_pending = true;

    if ( CameraCapture_StreamReleaseFrame() != SYS_OK)
        return;

    //Media确认接收后，才清除图像地址并回到IDLE
    s_bridge.frame.data = NULL;
    s_bridge.frame.size_bytes = 0U;
    s_bridge.release_pending = false;
    s_bridge.released_frames++;
    s_bridge.state = UVC_BRIDGE_IDLE;
}

//只负责USB应用配置，USB Core仍由DEVICE阶段初始化
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
    s_bridge_output = config->output;
    s_bridge_output_prepared = true;
    return SYS_OK;
}
