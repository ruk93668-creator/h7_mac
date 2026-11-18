// 将中立媒体配置转换为 USB/UVC 端口配置。

#ifndef V2_USB_FORMAT_H
#define V2_USB_FORMAT_H
#include "../../Core/media_format.h"
#include "video_config_types.h"
#define VU_MODE_NONE 0U
#define VU_MODE_TEST 1U
#define VU_MODE_UVC 2U
// 将中立格式交由模块端口验证，保留实际能力约束和错误结果。
SysResult_t VUSB_Translate(const CoreMediaFormat *, const CoreFrameRate *, VideoStreamConfig_t *);
#endif
