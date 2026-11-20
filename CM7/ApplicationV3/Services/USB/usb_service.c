#include "../../Core/app_context.h"
#include <stddef.h>

//UsbTask 保存已接受的配置，不打开 USB 视频端点。
static const VideoConfig_t *s_usb_config;

SysResult_t AppV3_UsbInit(const AppV3Context_t *context)
{
    if (s_usb_config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context->video == NULL)
        return SYS_ERR_INVALID_STATE;

    //此阶段只校验业务选择，实际启流仍需 UVC 能力验证。
    if (context->video->output != VIDEO_OUTPUT_USB_UVC)
        return SYS_ERR_NOT_SUPPORTED;

    s_usb_config = context->video;
    return SYS_OK;
}
