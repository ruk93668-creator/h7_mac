// 将中立媒体配置转换成 Camera 端口配置。

#ifndef V2_CAMERA_FORMAT_H
#define V2_CAMERA_FORMAT_H
#include "../../Core/media_format.h"
#include "camera_config.h"
// 无副作用能力校验，OV5640 和 CameraCapture 仍可否决。不初始化传感器、配置 DCMI、取帧或复制硬件限制。
SysResult_t VCamera_Translate(const CoreMediaFormat *format, CameraConfig_t *camera_config);
#endif
