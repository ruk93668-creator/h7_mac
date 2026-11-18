#include "storage_format.h"
#include "../Camera/camera_format.h"
#include <string.h>

static int name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int path_valid(const VStorageConfig *c)
{
    uint32_t i = 3U;
    const char *ext = c->mode == VST_AVI ? ".AVI" : ".JPG";
    if (memcmp(c->path, "0:/", 3U))
        return 0;
    while (i < 11U && name_char(c->path[i]))
        ++i;
    return i > 3U && !memcmp(c->path + i, ext, 5U);
}

SysResult_t VStorage_ValidateFormat(const CoreMediaFormat *f, const VStorageConfig *c,
                                    const VStorageCaps *caps, uint32_t *ms)
{
    CameraConfig_t native;
    SysResult_t z;
    if (!f || !c || !caps || !ms || !caps->max_payload ||
        (c->mode != VST_AVI && c->mode != VST_SNAPSHOT) || c->partition > 4U ||
        c->file_limit < f->max_payload || !path_valid(c) ||
        V2_Overlap(ms, sizeof(*ms), c, sizeof(*c)) || V2_Overlap(ms, sizeof(*ms), f, sizeof(*f)) ||
        V2_Overlap(ms, sizeof(*ms), caps, sizeof(*caps)))
        return SYS_ERR_INVALID_ARG;
    if (f->backend != CORE_BACKEND_JPEG || f->pixel_format != CORE_PIXEL_JPEG ||
        f->max_payload > caps->max_payload)
        return SYS_ERR_NOT_SUPPORTED;
    z = VCamera_Translate(f, &native);
    if (z)
        return z;
    if (c->mode == VST_SNAPSHOT)
    {
        *ms = 0U;
        return SYS_OK;
    }
    z = CoreRate_Validate(&c->sampling);
    if (z)
        return z;
    /* AVI兼容接口接受整数FPS；不隐式舍入目标采样率。 */
    if (c->sampling.den != 1U || c->sampling.num > caps->max_avi_fps ||
        c->sampling.num > UINT16_MAX)
        return SYS_ERR_NOT_SUPPORTED;
    *ms = (1000U + c->sampling.num - 1U) / c->sampling.num;
    return SYS_OK;
}
