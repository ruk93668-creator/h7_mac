// 将中立媒体配置转换为 USB/UVC 端口配置。

#include "usb_format.h"
#include "../Camera/camera_format.h"
#include "usb_uvc_desc.h"
#include <string.h>
// UVC 描述符有最终否决权，成功后才发布端口配置。
SysResult_t VUSB_Translate(const CoreMediaFormat *format, const CoreFrameRate *delivery_rate,
                           VideoStreamConfig_t *stream_config)
{
    VideoStreamConfig_t native_config;
    SysResult_t result;
    if (!format || !delivery_rate || !stream_config || V2_Overlap(format, sizeof(*format), stream_config, sizeof(*stream_config)) ||
        V2_Overlap(delivery_rate, sizeof(*delivery_rate), stream_config, sizeof(*stream_config)))
        return SYS_ERR_INVALID_ARG;
    result = CoreRate_Validate(delivery_rate);
    if (result)
        return result;
    /* 旧UVC配置只有整数FPS，不能静默舍入用户请求。 */
    if (delivery_rate->den != 1 || delivery_rate->num > UINT16_MAX)
        return SYS_ERR_NOT_SUPPORTED;
    memset(&native_config, 0, sizeof(native_config));
    result = VCamera_Translate(format, &native_config.camera);
    if (result)
        return result;
    native_config.fps = (uint16_t)delivery_rate->num;
    native_config.max_frame_bytes = format->max_payload;
    result = USB_UVC_ValidateFSConfig(&native_config);
    if (result)
        return result;
    *stream_config = native_config;
    return 0;
}
