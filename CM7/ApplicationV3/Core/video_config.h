#ifndef CM7_VIDEO_CONFIG_H
#define CM7_VIDEO_CONFIG_H

#include "video_config_types.h"

typedef struct
{
    uint32_t record_fps;
    uint32_t segment_bytes;
    uint32_t segment_ms; // 0表示不按时长结束录像
    uint32_t total_bytes;
    uint32_t reserve_bytes;
    uint32_t max_segments;
    uint32_t partition;//0自动选卷，1至4指定MBR分区
} AppStorageConfig_t;

//返回唯一的冷启动默认配置，调用者不得修改
const VideoConfig_t *VideoConfig_GetDefault(void);


//读取独立SD策略，默认不发录像或抓拍请求
const AppStorageConfig_t *VideoConfig_GetStorage(void);

#endif
