#ifndef CM7_VIDEO_CONFIG_H
#define CM7_VIDEO_CONFIG_H

#include "video_config_types.h"

//返回唯一的冷启动默认配置，调用者不得修改
const VideoConfig_t *VideoConfig_GetDefault(void);

//当前只支持冷启动配置，非空更新请求返回不支持
SysResult_t VideoConfig_RequestUpdate(const VideoConfig_t *config);

#endif
