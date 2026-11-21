#ifndef CM7_V3_SERVICE_EVENTS_H
#define CM7_V3_SERVICE_EVENTS_H

#include "../Core/app_messages.h"

typedef struct
{
    RuntimeEventTarget_t service;
    RuntimeEventTarget_t requester;
    uint32_t command_id;
    uint32_t sequence;
    SysResult_t result;
    uint32_t parameter;
    bool simulated_running;
} AppV3ServiceEvent_t;

typedef struct
{
    uint32_t accepted;
    uint32_t rejected;
    uint32_t callback_failures;
    SysResult_t first_error;
    AppMessageType_t rejected_type;
    RuntimeEventTarget_t rejected_source;
    uint32_t rejected_command_id;
} AppV3BusinessDiagnostics_t;

//回调由Control同步执行，只能短时处理，不阻塞；event仅在回调期间有效
typedef SysResult_t (*AppV3BusinessCallback_t)(const AppV3ServiceEvent_t *event, void *context);

//Control独占订阅和上下文生命周期；COUNT订阅所有服务
SysResult_t AppV3Business_Subscribe(RuntimeEventTarget_t service,
    AppV3BusinessCallback_t callback, void *context, uint32_t *ticket);
//当前回调执行期间撤销其自身票据返回BUSY；回调退出后再撤销并释放Context
SysResult_t AppV3Business_Unsubscribe(uint32_t ticket);
//普通任务零等待取本人的终结结果；BUSY表示同一命令仍待完成
SysResult_t AppV3Business_TakeResult(RuntimeEventTarget_t service, uint32_t command_id,
    AppV3ServiceEvent_t *result);

//提交侧内部接口：先预留结果槽；仅在请求未入队时撤销
SysResult_t AppV3Business_ReserveResult(RuntimeEventTarget_t requester, RuntimeEventTarget_t service,
    uint32_t command_id);
void AppV3Business_CancelReservation(RuntimeEventTarget_t requester, RuntimeEventTarget_t service,
    uint32_t command_id);

SysResult_t AppV3Business_Drain(uint32_t *consumed);
SysResult_t AppV3Business_GetDiagnostics(AppV3BusinessDiagnostics_t *diagnostics);

#endif
