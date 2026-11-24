#ifndef CM7_VIDEO_CONFIG_H
#define CM7_VIDEO_CONFIG_H

#include "video_config_types.h"

//录像目标卷与单文件结束策略
typedef struct
{
    //AVI起始帧率，收尾按实测时长修正
    uint32_t record_fps;
    //单段AVI容量上限，单位字节，包含尾部索引
    uint32_t segment_bytes;
    uint32_t segment_ms; // 0表示不按时长结束录像
    //累计容量保留字段，当前不用于滚动覆盖
    uint32_t total_bytes;
    //卷保留容量字段，当前不用于录前空间判定
    uint32_t reserve_bytes;
    //分段数量保留字段，当前由调用者显式启动新文件
    uint32_t max_segments;
    uint32_t partition;//0自动选卷，1至4指定MBR分区
} AppStorageConfig_t;

//返回唯一的冷启动默认配置，调用者不得修改
const VideoConfig_t *VideoConfig_GetDefault(void);


//读取独立SD策略，默认不发录像或抓拍请求
const AppStorageConfig_t *VideoConfig_GetStorage(void);

#endif
