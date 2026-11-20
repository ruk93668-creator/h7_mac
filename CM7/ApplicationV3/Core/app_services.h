#ifndef CM7_V3_APP_SERVICES_H
#define CM7_V3_APP_SERVICES_H

#include "app_context.h"
#include "app_messages.h"

//静态记录只用于查找服务，不授予任务或硬件写权限。
const RuntimeEventTarget_t *AppV3_FindService(RuntimeEventTarget_t owner);
SysResult_t AppV3_InitService(RuntimeEventTarget_t owner, const AppV3Context_t *context);
SysResult_t AppV3_ReportInitialization(RuntimeEventTarget_t owner, SysResult_t result);
SysResult_t AppV3_DrainInitialization(void);

#endif
