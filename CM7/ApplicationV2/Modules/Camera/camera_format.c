#include "camera_format.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <string.h>
SysResult_t VCamera_Translate(const CoreMediaFormat *f, CameraConfig_t *out)
{
    CameraConfig_t n;
    SysResult_t z;
    if (!out || !f || V2_Overlap(f, sizeof(*f), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    z = CoreMedia_Validate(f);
    if (z)
        return z;
    memset(&n, 0, sizeof(n));
    n.width = (uint16_t)f->width;
    n.height = (uint16_t)f->height;
    switch (f->pixel_format)
    {
    case CORE_PIXEL_Y8:
        n.format = CAMERA_PIXEL_FORMAT_Y8;
        break;
    case CORE_PIXEL_RGB565:
        n.format = CAMERA_PIXEL_FORMAT_RGB565;
        break;
    case CORE_PIXEL_YUY2:
        n.format = CAMERA_PIXEL_FORMAT_YUV422;
        break;
    case CORE_PIXEL_JPEG:
        n.format = CAMERA_PIXEL_FORMAT_JPEG;
        break;
    default:
        return SYS_ERR_NOT_SUPPORTED;
    }
    z = Ov5640_ValidateConfig(&n);
    if (z)
        return z;
    z = CameraCapture_ValidateStreamConfig(&n);
    if (z)
        return z;
    *out = n;
    return SYS_OK;
}
