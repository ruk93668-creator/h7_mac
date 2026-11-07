#ifndef RUNTIME_EVENT_H
#define RUNTIME_EVENT_H

#include "sys_result.h"
#include <stdint.h>

typedef enum
{
    RUNTIME_TARGET_CAMERA = 0,
    RUNTIME_TARGET_USB,
    RUNTIME_TARGET_CONTROL,
    RUNTIME_TARGET_IPC,
    RUNTIME_TARGET_COUNT
} RuntimeEventTarget_t;

//通知只提示状态变化，不传递帧所有权
#define RUNTIME_EVENT_BOOT_READY       (1UL << 0)
#define RUNTIME_EVENT_CAMERA_PROGRESS  (1UL << 1)
#define RUNTIME_EVENT_USB_PROGRESS     (1UL << 2)
#define RUNTIME_EVENT_WAKE             (1UL << 3)
#define RUNTIME_EVENT_FAULT            (1UL << 4)
#define RUNTIME_EVENT_USER_MASK        (0x1EUL)

//普通C后端，不向底层暴露RTOS句柄
//表及context必须静态有效，绑定后不允许替换
typedef struct
{
    SysResult_t (*post)(void *context, RuntimeEventTarget_t target, uint32_t events);

    SysResult_t (*post_from_isr)(void *context, RuntimeEventTarget_t target, uint32_t events);

    void *context;
} RuntimeEventPort_t;

//绑定唯一的静态事件后端
SysResult_t RuntimeEvent_Bind(const RuntimeEventPort_t *port);

//发送普通上下文事件提示
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target, uint32_t events);

//发送中断上下文事件提示
SysResult_t RuntimeEvent_PostFromIsr(RuntimeEventTarget_t target, uint32_t events);

#endif
