#include "usb_format.h"
#include "../Camera/camera_format.h"
#include "usb_uvc.h"
#include <string.h>
SysResult_t VUSB_Translate(const CoreMediaFormat *f, const CoreFrameRate *rate,
                           VideoStreamConfig_t *out)
{
    VideoStreamConfig_t n;
    SysResult_t z;
    if (!f || !rate || !out || V2_Overlap(f, sizeof(*f), out, sizeof(*out)) ||
        V2_Overlap(rate, sizeof(*rate), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = CoreRate_Validate(rate);
    if (z)
        return z;
    /* 旧UVC配置只有整数FPS，不能静默舍入用户请求。 */
    if (rate->den != 1 || rate->num > UINT16_MAX)
        return SYS_ERR_NOT_SUPPORTED;
    memset(&n, 0, sizeof(n));
    z = VCamera_Translate(f, &n.camera);
    if (z)
        return z;
    n.fps = (uint16_t)rate->num;
    n.max_frame_bytes = f->max_payload;
    z = USB_UVC_ValidateConfig(&n);
    if (z)
        return z;
    *out = n;
    return 0;
}
