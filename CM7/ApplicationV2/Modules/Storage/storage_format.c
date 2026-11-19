// 校验 Storage 的录像及抓拍配置。

#include "storage_format.h"
#include "../Camera/camera_format.h"
#include <string.h>

// 检查目标文件路径是否符合模块的输入约束。
static int name_char(char character)
{
    return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_';
}

// 检查目标文件路径是否符合模块的输入约束。
static int path_valid(const VStorageConfig *config)
{
    uint32_t path_index = 3U;
    const char *extension = config->mode == VST_AVI ? ".AVI" : ".JPG";
    if (memcmp(config->path, "0:/", 3U))
        return 0;
    while (path_index < 11U && name_char(config->path[path_index]))
        ++path_index;
    return path_index > 3U && !memcmp(config->path + path_index, extension, 5U);
}

// 存储格式验证只计算采样周期，不触发挂载、写入或帧交接。
SysResult_t VStorage_ValidateFormat(const CoreMediaFormat *format, const VStorageConfig *config,
                                    const VStorageCaps *caps, uint32_t *sampling_interval_ms)
{
    CameraConfig_t native;
    SysResult_t result;
    if (!format || !config || !caps || !sampling_interval_ms || !caps->max_payload ||
        (config->mode != VST_AVI && config->mode != VST_SNAPSHOT) || config->partition > 4U ||
        config->file_limit < format->max_payload || !path_valid(config) ||
        V2_Overlap(sampling_interval_ms, sizeof(*sampling_interval_ms), config, sizeof(*config)) || V2_Overlap(sampling_interval_ms, sizeof(*sampling_interval_ms), format, sizeof(*format)) ||
        V2_Overlap(sampling_interval_ms, sizeof(*sampling_interval_ms), caps, sizeof(*caps)))
        return SYS_ERR_INVALID_ARG;
    if (format->backend != CORE_BACKEND_JPEG || format->pixel_format != CORE_PIXEL_JPEG ||
        format->max_payload > caps->max_payload)
        return SYS_ERR_NOT_SUPPORTED;
    result = VCamera_Translate(format, &native);
    if (result)
        return result;
    if (config->mode == VST_SNAPSHOT)
    {
        *sampling_interval_ms = 0U;
        return SYS_OK;
    }
    result = CoreRate_Validate(&config->sampling);
    if (result)
        return result;
    /* AVI兼容接口接受整数FPS；不隐式舍入目标采样率。 */
    if (config->sampling.den != 1U || config->sampling.num > caps->max_avi_fps ||
        config->sampling.num > UINT16_MAX)
        return SYS_ERR_NOT_SUPPORTED;
    *sampling_interval_ms = (1000U + config->sampling.num - 1U) / config->sampling.num;
    return SYS_OK;
}
