#ifndef FRAMEWORK_EVENT_H
#define FRAMEWORK_EVENT_H

#include <stdint.h>
#include "sys_result.h"



typedef uint16_t FrameworkEventId_t;

typedef struct
{
    FrameworkEventId_t id;
    uint32_t data;
} FrameworkEvent_t; 

//处理已出队事件，event仅在本次回调期间有效
typedef void (*FrameworkEventHandler_t)(
    const FrameworkEvent_t *event
);

//按事件ID注册处理函数，拒绝重复订阅
SysResult_t Framework_EventSubscribe(
    FrameworkEventId_t id,
    FrameworkEventHandler_t handler
);

//复制事件到FIFO队尾，等待主循环分发
SysResult_t Framework_EventPost(
    const FrameworkEvent_t *event
);

//每轮最多分发4个事件，调用匹配的订阅回调
void Framework_EventDispatch(void);

#endif /* FRAMEWORK_EVENT_H */
