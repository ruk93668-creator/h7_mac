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

SysResult_t AppV3Record_Init(void);
SysResult_t AppV3Record_Create(RuntimeEventTarget_t owner, const AppV3ServiceInstance_t *instance);
SysResult_t AppV3Record_Fail(RuntimeEventTarget_t owner, SysResult_t result);
SysResult_t AppV3Record_Open(RuntimeEventTarget_t owner, TickType_t timeout, AppV3RecordRef_t *out);
SysResult_t AppV3Record_Close(AppV3RecordRef_t *reference);
SysResult_t AppV3Record_Destroy(RuntimeEventTarget_t owner);

//返回当前发布结果，尚未完成时返回 BUSY；不会取得引用。
SysResult_t AppV3Record_Query(RuntimeEventTarget_t owner);

#endif
