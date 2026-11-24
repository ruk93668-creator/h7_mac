#ifndef CM7_V3_QUEUE_POOL_H
#define CM7_V3_QUEUE_POOL_H

#include "app_events.h"
#include <stddef.h>

//可按槽号和代号验证的队列票据
typedef struct
{
    //槽位或采集实例的代号，用于排除已销毁凭据
    uint32_t generation;
    //静态表中的槽号，与代号一起核验
    uint8_t index;
} AppV3Queue_t;

//按项大小和深度分配静态队列，输出包含代次的句柄
SysResult_t AppV3Queue_Alloc(size_t item_bytes, UBaseType_t length, AppV3Queue_t *out);
//队列无订阅、无在途操作且已清空时才允许释放
SysResult_t AppV3Queue_Free(AppV3Queue_t queue);
//任务收发以Tick为单位等待，零表示立即返回
SysResult_t AppV3Queue_Put(AppV3Queue_t queue, const void *item, TickType_t timeout);
//从任务上下文读取所属队列，可按提供的时限等待
SysResult_t AppV3Queue_Get(AppV3Queue_t queue, void *item, TickType_t timeout);
//读队首并保留数据，等待参数与Get一致
SysResult_t AppV3Queue_Peek(AppV3Queue_t queue, void *item, TickType_t timeout);
//仅从合法外设IRQ访问，固定零等待
SysResult_t AppV3Queue_PutFromIsr(AppV3Queue_t queue, const void *item);
//从中断读取队列且不等待，返回调度唤醒信息
SysResult_t AppV3Queue_GetFromIsr(AppV3Queue_t queue, void *item);
//查询总容量及当前剩余空间
SysResult_t AppV3Queue_Query(AppV3Queue_t queue, UBaseType_t *capacity, UBaseType_t *space);
//订阅关系属于指定任务，只负责发送队列可读提示
SysResult_t AppV3Queue_Subscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner);
//撤销指定任务的队列进展通知
SysResult_t AppV3Queue_Unsubscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner);

#endif
