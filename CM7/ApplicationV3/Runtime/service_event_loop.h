#ifndef APP_V3_SERVICE_EVENT_LOOP_H
#define APP_V3_SERVICE_EVENT_LOOP_H

#include "queue_pool.h"

//所属任务在队列可读时同步调用的入口
typedef SysResult_t (*AppV3QueueReadable_t)(AppV3Queue_t queue, void *context);

//对象仅由所属任务访问。订阅和回调不转移队列所有权。
typedef struct
{
    //唯一拥有操作权限的任务身份
    RuntimeEventTarget_t owner;
    //绑定当前对象的内核或应用队列句柄
    AppV3Queue_t queue;
    //事件循环分发到当前订阅队列的回调
    AppV3QueueReadable_t readable;
    //回调期间可访问的订阅者私有数据
    void *context;
    //准备完成标志，防止使用未建立的对象
    bool initialized;
} AppV3EventLoop_t;

//在该循环的所属任务内初始化零填充对象
SysResult_t AppV3EventLoop_Init(AppV3EventLoop_t *loop, RuntimeEventTarget_t owner);
//为当前循环绑定唯一队列及可读回调
SysResult_t AppV3EventLoop_Subscribe(AppV3EventLoop_t *loop, AppV3Queue_t queue,
    AppV3QueueReadable_t readable, void *context);
//撤回当前循环的订阅，由原订阅任务执行
SysResult_t AppV3EventLoop_Unsubscribe(AppV3EventLoop_t *loop);
//有界分发；用完预算时阻塞一个Tick，让低优先级任务有运行机会
SysResult_t AppV3EventLoop_Step(AppV3EventLoop_t *loop, uint32_t timeout_ms, uint32_t *events);

#endif
