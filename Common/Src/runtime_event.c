#include "runtime_event.h"
#include <stddef.h>
#include <stdbool.h>

static const RuntimeEventPort_t *s_port;

//检查公开事件，禁止业务伪造启动屏障
static bool RuntimeEvent_Valid(RuntimeEventTarget_t target, uint32_t events);

SysResult_t RuntimeEvent_Bind(const RuntimeEventPort_t *port)
{
    if (port == NULL || port->post == NULL || port->post_from_isr == NULL)
        return SYS_ERR_INVALID_ARG;
    if (s_port != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    s_port = port;
    return SYS_OK;
}

SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target, uint32_t events)
{
    if (!RuntimeEvent_Valid(target, events))
        return SYS_ERR_INVALID_ARG;
    if (s_port == NULL)
        return SYS_ERR_INVALID_STATE;

    return s_port->post(s_port->context, target, events);
}

SysResult_t RuntimeEvent_PostFromIsr(RuntimeEventTarget_t target, uint32_t events)
{
    if (!RuntimeEvent_Valid(target, events))
        return SYS_ERR_INVALID_ARG;
    if (s_port == NULL)
        return SYS_ERR_INVALID_STATE;

    return s_port->post_from_isr(s_port->context, target, events);
}

//内部功能函数实现
static bool RuntimeEvent_Valid(RuntimeEventTarget_t target, uint32_t events)
{
    return (uint32_t)target < RUNTIME_TARGET_COUNT && events != 0U &&
        (events & ~RUNTIME_EVENT_USER_MASK) == 0U;
}
