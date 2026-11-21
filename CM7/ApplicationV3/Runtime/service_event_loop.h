#ifndef APP_V3_SERVICE_EVENT_LOOP_H
#define APP_V3_SERVICE_EVENT_LOOP_H

#include "queue_pool.h"

typedef SysResult_t (*AppV3QueueReadable_t)(AppV3Queue_t queue, void *context);

//对象仅由所属任务访问。订阅和回调不转移队列所有权。
typedef struct
{
    RuntimeEventTarget_t owner;
    AppV3Queue_t queue;
    AppV3QueueReadable_t readable;
    void *context;
    bool initialized;
} AppV3EventLoop_t;

SysResult_t AppV3EventLoop_Init(AppV3EventLoop_t *loop, RuntimeEventTarget_t owner);
SysResult_t AppV3EventLoop_Subscribe(AppV3EventLoop_t *loop, AppV3Queue_t queue,
    AppV3QueueReadable_t readable, void *context);
SysResult_t AppV3EventLoop_Unsubscribe(AppV3EventLoop_t *loop);
SysResult_t AppV3EventLoop_Step(AppV3EventLoop_t *loop, uint32_t timeout_ms, uint32_t *events);

#endif
