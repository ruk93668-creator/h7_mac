#include "runtime_event.h"
#include <stddef.h>
#include <stdbool.h>

//保存Application提供的静态事件后端，不依赖具体RTOS
static const RuntimeEventPort_t *s_port;

//校验公共事件目标与提示位
static bool RuntimeEvent_Valid(RuntimeEventTarget_t target, uint32_t events);

//绑定唯一的静态事件后端
SysResult_t RuntimeEvent_Bind(const RuntimeEventPort_t *port)
{

    //后端表和两个上下文入口必须完整，避免调用空指针
    if (port == NULL || port->post == NULL || port->post_from_isr == NULL)
        return SYS_ERR_INVALID_ARG;

    //只允许初始化阶段绑定一次，不在运行中替换接收关系
    if (s_port != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //仅保存表地址，调用者必须保证表及context持续有效
    s_port = port;
    return SYS_OK;
}

//发送普通上下文事件提示
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target, uint32_t events)
{

    //先校验目标和提示位，公共接口不能伪造BOOT_READY
    if (!RuntimeEvent_Valid(target, events))
        return SYS_ERR_INVALID_ARG;

    //尚未绑定时返回状态错误，不丢给空后端
    if (s_port == NULL)
        return SYS_ERR_INVALID_STATE;

    //交由Application检查调用环境并完成通知，Common不操作任务
    return s_port->post(s_port->context, target, events);
}

//发送中断上下文事件提示
SysResult_t RuntimeEvent_PostFromIsr(RuntimeEventTarget_t target, uint32_t events)
{

    //先校验目标和提示位，公共接口不能伪造BOOT_READY
    if (!RuntimeEvent_Valid(target, events))
        return SYS_ERR_INVALID_ARG;

    //尚未绑定时返回状态错误，不丢给空后端
    if (s_port == NULL)
        return SYS_ERR_INVALID_STATE;

    //交由Application检查调用环境并完成通知，Common不操作任务
    return s_port->post_from_isr(s_port->context, target, events);
}

//校验公共事件目标与提示位
static bool RuntimeEvent_Valid(RuntimeEventTarget_t target, uint32_t events)
{

    //目标必须有效，提示位非零且只能来自公开允许的位集合
    return (uint32_t)target < RUNTIME_TARGET_COUNT && events != 0U &&
        (events & ~RUNTIME_EVENT_USER_MASK) == 0U;
}
