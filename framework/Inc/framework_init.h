#ifndef FRAMEWORK_INIT_H
#define FRAMEWORK_INIT_H
#include "sys_result.h"

typedef enum
{
    INIT_LEVEL_PLATFORM = 0,
    INIT_LEVEL_BUS,
    INIT_LEVEL_DEVICE,
    INIT_LEVEL_MEDIA,
    INIT_LEVEL_SERVICE,
    INIT_LEVEL_APPLICATION,

    INIT_LEVEL_COUNT
} InitLevel_t;

//模块初始化回调，成功返回SYS_OK
typedef SysResult_t (*FrameworkInitFunc_t)(void);

typedef struct
{
    const char          *name;
    InitLevel_t          level;
    FrameworkInitFunc_t  init;
} FrameworkInitEntry_t;

//按等级执行模块初始化，首次失败即返回
SysResult_t Framework_InitRuntime(void);
#endif /* FRAMEWORK_INIT_H */
