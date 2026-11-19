// 将中立媒体配置转换为 USB/UVC 端口配置。

#ifndef V2_USB_FORMAT_H
#define V2_USB_FORMAT_H
#include "../../Core/media_format.h"
#include "video_config_types.h"
#define VU_MODE_NONE 0U
#define VU_MODE_TEST 1U
#define VU_MODE_UVC 2U
// 校验采集格式及 USB 交付速率，成功才写入端口配置。
SysResult_t VUSB_Translate(const CoreMediaFormat *format, const CoreFrameRate *delivery_rate,
                           VideoStreamConfig_t *stream_config);
#endif
