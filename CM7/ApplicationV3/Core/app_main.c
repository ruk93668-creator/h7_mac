#include "app_context.h"
#include "app_main.h"
#include "framework_init.h"
#include "../Runtime/service_record.h"
#include <stddef.h>

//Framework 在 BOOT 前写一次，之后各服务只读。
static AppV3Context_t s_context;

SysResult_t App_Init(void)
{
    s_context.video = VideoConfig_GetDefault();
    s_context.storage = VideoConfig_GetStorage();

    //缺少配置时不放行业务任务。
    if (s_context.video == NULL || s_context.storage == NULL)
        return SYS_ERR_INVALID_STATE;
    return AppV3Record_Init();
}

const AppV3Context_t *AppV3_GetContext(void)
{
    return &s_context;
}

FRAMEWORK_INIT_REGISTER(App_Init, INIT_LEVEL_APPLICATION);
