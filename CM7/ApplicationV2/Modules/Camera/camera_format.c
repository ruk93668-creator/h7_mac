// 将中立媒体配置转换成 Camera 端口配置。

#include "camera_format.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <string.h>
// 将中立格式交由模块端口验证，保留实际能力约束和错误结果。
SysResult_t VCamera_Translate(const CoreMediaFormat *format, CameraConfig_t *out)
{
    CameraConfig_t native_config;
    SysResult_t result;
    if (!out || !format || V2_Overlap(format, sizeof(*format), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = CoreMedia_Validate(format);
    if (result)
        return result;
    memset(&native_config, 0, sizeof(native_config));
    native_config.width = (uint16_t)format->width;
    native_config.height = (uint16_t)format->height;
    switch (format->pixel_format)
    {
    case CORE_PIXEL_Y8:
        native_config.format = CAMERA_PIXEL_FORMAT_Y8;
        break;
    case CORE_PIXEL_RGB565:
        native_config.format = CAMERA_PIXEL_FORMAT_RGB565;
        break;
    case CORE_PIXEL_YUY2:
        native_config.format = CAMERA_PIXEL_FORMAT_YUV422;
        break;
    case CORE_PIXEL_JPEG:
        native_config.format = CAMERA_PIXEL_FORMAT_JPEG;
        break;
    default:
        return SYS_ERR_NOT_SUPPORTED;
    }
    result = Ov5640_ValidateConfig(&native_config);
    if (result)
        return result;
    result = CameraCapture_ValidateStreamConfig(&native_config);
    if (result)
        return result;
    *out = native_config;
    return SYS_OK;
}
