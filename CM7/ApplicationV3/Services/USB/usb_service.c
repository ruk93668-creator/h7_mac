#include "../../Core/app_context.h"
#include <stddef.h>

//UsbTask 保存已接受的配置，不打开 USB 视频端点。
static const VideoConfig_t *s_usb_config;

//校验USB服务配置，保留实际启流给后续业务入口
SysResult_t AppV3_UsbInit(const AppV3Context_t *context)
{
    //服务只接受一次启动配置，重复初始化不替换引用
    if (s_usb_config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context->video == NULL)
        return SYS_ERR_INVALID_STATE;

    //此阶段只校验业务选择，实际启流仍需 UVC 能力验证。
    if (context->video->output != VIDEO_OUTPUT_USB_UVC)
        return SYS_ERR_NOT_SUPPORTED;

    //校验通过后保存稳定的只读配置引用
    s_usb_config = context->video;
    return SYS_OK;
}
