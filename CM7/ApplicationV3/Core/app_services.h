#ifndef CM7_V3_APP_SERVICES_H
#define CM7_V3_APP_SERVICES_H

#include "app_context.h"
#include "app_messages.h"
#include "../Runtime/service_record.h"

//静态记录只用于查找服务，不授予任务或硬件写权限。
const RuntimeEventTarget_t *AppV3_FindService(RuntimeEventTarget_t owner);
SysResult_t AppV3_StartServices(uint32_t *created);
SysResult_t AppV3_InitService(RuntimeEventTarget_t owner, const AppV3Context_t *context);
SysResult_t AppV3_ReportInitialization(RuntimeEventTarget_t owner, SysResult_t result);
SysResult_t AppV3_DrainInitialization(void);
SysResult_t AppV3_ExpireInitialization(void);
SysResult_t AppV3_GetStartupQueue(AppV3Queue_t *queue);

//验证打开的Record确实属于Registry中的固定服务实例
bool AppV3_IsServiceInstance(RuntimeEventTarget_t owner, const AppV3ServiceInstance_t *instance);

//启动聚合成功后，三个私有队列才共同对外接收请求
bool AppV3_BusinessReady(void);

#endif
