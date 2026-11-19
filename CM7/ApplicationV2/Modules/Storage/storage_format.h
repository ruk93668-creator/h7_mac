// 校验 Storage 的录像及抓拍配置。

#ifndef V2_STORAGE_FORMAT_H
#define V2_STORAGE_FORMAT_H

#include "../../Core/media_format.h"

#define VST_AVI 1U
#define VST_SNAPSHOT 2U
#define VST_PATH_SIZE 16U

typedef struct
{
    uint32_t mode, partition, file_limit, sink_entity;
    CoreFrameRate sampling;
    char path[VST_PATH_SIZE]; /* 0:/NAME.AVI 或 0:/NAME.JPG，CREATE_NEW */
} VStorageConfig;

typedef struct
{
    uint32_t max_payload, max_avi_fps;
} VStorageCaps; /* 由目标板的存储能力配置提供，不能从 Offer 副作用探测。 */

// 只校验 JPEG 和文件参数。成功返回毫秒采样周期，单次抓拍返回 0。
SysResult_t VStorage_ValidateFormat(const CoreMediaFormat *format, const VStorageConfig *config,
                                    const VStorageCaps *caps, uint32_t *sampling_interval_ms);
#endif
