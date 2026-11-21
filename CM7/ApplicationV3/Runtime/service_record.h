#ifndef APP_V3_SERVICE_RECORD_H
#define APP_V3_SERVICE_RECORD_H

#include "app_context.h"
#include "queue_pool.h"

typedef struct
{
    RuntimeEventTarget_t owner;
    const AppV3Context_t *config;
} AppV3ServiceInstance_t;

typedef struct
{
    const AppV3ServiceInstance_t *instance;
    uint32_t ticket;
    RuntimeEventTarget_t owner;
} AppV3RecordRef_t;

//在调度前分配固定数量的就绪队列
SysResult_t AppV3Record_Init(void);
//由所属服务发布只读实例，实例必须在引用有效期间存续
SysResult_t AppV3Record_Create(RuntimeEventTarget_t owner, const AppV3ServiceInstance_t *instance);
//由所属服务发布真实启动错误并唤醒等待者
SysResult_t AppV3Record_Fail(RuntimeEventTarget_t owner, SysResult_t result);
//尝试取得服务实例的只读引用，等待上限用Tick指定
SysResult_t AppV3Record_Open(RuntimeEventTarget_t owner, TickType_t timeout, AppV3RecordRef_t *out);
//仅原持有任务能关闭自己的引用
SysResult_t AppV3Record_Close(AppV3RecordRef_t *reference);
//由发布任务撤回没有等待者和引用的记录
SysResult_t AppV3Record_Destroy(RuntimeEventTarget_t owner);

//查询独立的发布事实，实际错误通过 out 返回。
SysResult_t AppV3Record_Query(RuntimeEventTarget_t owner, SysResult_t *out);

#endif
