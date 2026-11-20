#ifndef CM7_V3_APP_CONTEXT_H
#define CM7_V3_APP_CONTEXT_H

#include "video_config.h"

//只共享只读配置，各服务状态由其所属任务维护。
typedef struct
{
    const VideoConfig_t *video;
    const AppStorageConfig_t *storage;
} AppV3Context_t;

const AppV3Context_t *AppV3_GetContext(void);

#endif
