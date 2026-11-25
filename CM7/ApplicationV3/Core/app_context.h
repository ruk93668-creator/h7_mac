#ifndef CM7_V3_APP_CONTEXT_H
#define CM7_V3_APP_CONTEXT_H

#include "video_config.h"

//只共享只读配置，各服务状态由其所属任务维护。
typedef struct
{
    //只读视频配置，由初始化统一选定
    const VideoConfig_t *video;
    //由初始化装配的只读录像配置
    const AppStorageConfig_t *storage;
} AppV3Context_t;

//返回初始化过程中装配的只读应用上下文
const AppV3Context_t *AppV3_GetContext(void);

#endif
