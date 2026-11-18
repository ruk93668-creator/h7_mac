// 将中立媒体配置转换为 USB/UVC 端口配置。

#include "usb_format.h"
#include "../Camera/camera_format.h"
#include "usb_uvc_desc.h"
#include <string.h>
// 将中立格式交由模块端口验证，保留实际能力约束和错误结果。
SysResult_t VUSB_Translate(const CoreMediaFormat *format, const CoreFrameRate *rate,
                           VideoStreamConfig_t *out)
{
    VideoStreamConfig_t native_config;
    SysResult_t result;
    if (!format || !rate || !out || V2_Overlap(format, sizeof(*format), out, sizeof(*out)) ||
        V2_Overlap(rate, sizeof(*rate), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = CoreRate_Validate(rate);
    if (result)
        return result;
    /* 旧UVC配置只有整数FPS，不能静默舍入用户请求。 */
    if (rate->den != 1 || rate->num > UINT16_MAX)
        return SYS_ERR_NOT_SUPPORTED;
    memset(&native_config, 0, sizeof(native_config));
    result = VCamera_Translate(format, &native_config.camera);
    if (result)
        return result;
    native_config.fps = (uint16_t)rate->num;
    native_config.max_frame_bytes = format->max_payload;
    result = USB_UVC_ValidateFSConfig(&native_config);
    if (result)
        return result;
    *out = native_config;
    return 0;
}
