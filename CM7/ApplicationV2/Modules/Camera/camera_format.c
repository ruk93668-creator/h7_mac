// 将中立媒体配置转换成 Camera 端口配置。

#include "camera_format.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <string.h>
// 传感器和 Capture 都通过后才写入输出，避免调用方使用部分转换的配置。
SysResult_t VCamera_Translate(const CoreMediaFormat *format, CameraConfig_t *camera_config)
{
    CameraConfig_t native_config;
    SysResult_t result;
    //校验输入与输出存储，避免覆盖原格式。
    if (!camera_config || !format || V2_Overlap(format, sizeof(*format), camera_config, sizeof(*camera_config)))
        return SYS_ERR_INVALID_ARG;
    //先验证中立格式，再转换为设备枚举。
    result = CoreMedia_Validate(format);
    if (result)
        return result;
    memset(&native_config, 0, sizeof(native_config));
    native_config.width = (uint16_t)format->width;
    native_config.height = (uint16_t)format->height;
    //只映射底层显式支持的像素格式。
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
    //由真实传感器验证该配置。
    result = Ov5640_ValidateConfig(&native_config);
    if (result)
        return result;
    //同一配置还必须符合 Capture 的布局和 DMA 约束。
    result = CameraCapture_ValidateStreamConfig(&native_config);
    if (result)
        return result;
    //全部验证通过后才提交配置。
    *camera_config = native_config;
    return SYS_OK;
}
