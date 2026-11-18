#ifndef V2_USB_FORMAT_H
#define V2_USB_FORMAT_H
#include "../../Core/media_format.h"
#include "video_config_types.h"
#define VU_MODE_NONE 0U
#define VU_MODE_TEST 1U
#define VU_MODE_UVC 2U
SysResult_t VUSB_Translate(const CoreMediaFormat *, const CoreFrameRate *, VideoStreamConfig_t *);
#endif
