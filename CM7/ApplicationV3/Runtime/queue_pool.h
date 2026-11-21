#ifndef CM7_V3_QUEUE_POOL_H
#define CM7_V3_QUEUE_POOL_H

#include "app_events.h"
#include <stddef.h>

typedef struct
{
    uint32_t generation;
    uint8_t index;
} AppV3Queue_t;

SysResult_t AppV3Queue_Alloc(size_t item_bytes, UBaseType_t length, AppV3Queue_t *out);
SysResult_t AppV3Queue_Free(AppV3Queue_t queue);
SysResult_t AppV3Queue_Put(AppV3Queue_t queue, const void *item, TickType_t timeout);
SysResult_t AppV3Queue_Get(AppV3Queue_t queue, void *item, TickType_t timeout);
SysResult_t AppV3Queue_Peek(AppV3Queue_t queue, void *item, TickType_t timeout);
SysResult_t AppV3Queue_PutFromIsr(AppV3Queue_t queue, const void *item);
SysResult_t AppV3Queue_GetFromIsr(AppV3Queue_t queue, void *item);
SysResult_t AppV3Queue_Query(AppV3Queue_t queue, UBaseType_t *capacity, UBaseType_t *space);
SysResult_t AppV3Queue_Subscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner);
SysResult_t AppV3Queue_Unsubscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner);

#endif
