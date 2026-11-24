#ifndef CM7_V3_APP_SERVICES_H
#define CM7_V3_APP_SERVICES_H

#include "app_context.h"
#include "app_messages.h"
#include "../Runtime/service_record.h"

//静态记录只用于查找服务，不授予任务或硬件写权限。
const RuntimeEventTarget_t *AppV3_FindService(RuntimeEventTarget_t owner);
//按静态服务清单创建任务，任一步骤失败时返回原错误
SysResult_t AppV3_StartServices(uint32_t *created);
//由所属任务准备并发布服务实例
SysResult_t AppV3_InitService(RuntimeEventTarget_t owner, const AppV3Context_t *context);
//提交所属任务的启动结果供Control消费
SysResult_t AppV3_ReportInitialization(RuntimeEventTarget_t owner, SysResult_t result);
//由Control汇总已收到的服务启动结果
SysResult_t AppV3_DrainInitialization(void);
//由Control为未完成的启动提交超时结论
SysResult_t AppV3_ExpireInitialization(void);
//取得启动闭环使用的队列票据
SysResult_t AppV3_GetStartupQueue(AppV3Queue_t *queue);

//验证打开的Record确实属于Registry中的固定服务实例
bool AppV3_IsServiceInstance(RuntimeEventTarget_t owner, const AppV3ServiceInstance_t *instance);

//启动聚合成功后，三个私有队列才共同对外接收请求
bool AppV3_BusinessReady(void);

#endif
