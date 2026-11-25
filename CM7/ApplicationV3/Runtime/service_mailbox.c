#include "service_mailbox.h"
#include "service_record.h"
#include "service_events.h"
#include "../Core/app_services.h"

//私有命令必须能放入通用消息槽，保持AppMessage布局
typedef char AppV3ServiceCommandFits[(sizeof(AppV3ServiceCommand_t) <= sizeof(AppMessage_t)) ? 1 : -1];

//先订阅再交出发布入口，任何失败都回收未公开队列
SysResult_t AppV3Mailbox_Init(AppV3ServiceMailbox_t *mailbox, RuntimeEventTarget_t owner,
    AppV3QueueReadable_t readable, AppV3Queue_t *endpoint)
{
    SysResult_t result;

    if (!App_MessagesIsOwner(owner) || mailbox->queue.generation != 0U)
        return SYS_ERR_INVALID_STATE;
    result = AppV3Queue_Alloc(sizeof(AppV3ServiceCommand_t), APP_V3_SERVICE_DEPTH, &mailbox->queue);
    if (result != SYS_OK)
        return result;

    mailbox->owner = owner;
    result = AppV3EventLoop_Init(&mailbox->loop, owner);
    if (result == SYS_OK)
        result = AppV3EventLoop_Subscribe(&mailbox->loop, mailbox->queue, readable, mailbox);
    if (result != SYS_OK)
    {
        AppV3Mailbox_Discard(mailbox, endpoint);
        return result;
    }
    *endpoint = mailbox->queue;
    return SYS_OK;
}

//仅用于发布失败的回退；此时Record尚未授权任何请求
void AppV3Mailbox_Discard(AppV3ServiceMailbox_t *mailbox, AppV3Queue_t *endpoint)
{
    SysResult_t result, recorded;

    if (!App_MessagesIsOwner(mailbox->owner))
        return;
    if (AppV3Record_Query(mailbox->owner, &recorded) == SYS_OK && recorded == SYS_OK)
        return;
    if (mailbox->loop.readable != NULL)
    {
        result = AppV3EventLoop_Unsubscribe(&mailbox->loop);
        configASSERT(result == SYS_OK);
    }
    result = AppV3Queue_Free(mailbox->queue);
    configASSERT(result == SYS_OK);
    if (result != SYS_OK)
        return;
    *endpoint = (AppV3Queue_t){0};
    mailbox->queue = (AppV3Queue_t){0};
    mailbox->loop = (AppV3EventLoop_t){.initialized = false};
}

//持有Record期间只复制发布的入口，队列命令不保存调用者指针
SysResult_t AppV3Mailbox_Request(RuntimeEventTarget_t owner, const AppV3Queue_t *endpoint,
    uint32_t command_id, uint32_t operation, uint32_t argument)
{
    AppV3RecordRef_t reference = {0};
    RuntimeEventTarget_t requester;
    SysResult_t result, close_result;

    if (command_id == 0U)
        return SYS_ERR_INVALID_ARG;
    for (requester = RUNTIME_TARGET_CAMERA; requester < RUNTIME_TARGET_COUNT; ++requester)
    {
        if (App_MessagesIsOwner(requester))
            break;
    }
    //公开请求只允许登记的普通任务，禁止向自己的Service同步调度
    if (requester == RUNTIME_TARGET_COUNT || requester == owner)
        return SYS_ERR_INVALID_STATE;

    result = AppV3Record_Open(owner, 0U, &reference);
    if (result != SYS_OK)
        return result;

    //必须匹配固定Registry发布的对象，其他同名Record不能使用旧入口
    if (!AppV3_IsServiceInstance(owner, reference.instance))
        result = SYS_ERR_INVALID_STATE;
    else if (!AppV3_BusinessReady())
        result = SYS_ERR_BUSY;
    else
    {
        AppV3ServiceCommand_t command = {command_id, operation, argument, requester};
        //普通任务先预留唯一终结位置，成功入队后无需依赖通知或订阅者
        if (requester != RUNTIME_TARGET_CONTROL)
            result = AppV3Business_ReserveResult(requester, owner, command_id);
        if (result == SYS_OK)
        {
            result = AppV3Queue_Put(*endpoint, &command, 0U);
            if (result != SYS_OK && requester != RUNTIME_TARGET_CONTROL)
                AppV3Business_CancelReservation(requester, owner, command_id);
        }
    }
    //关闭的是刚由本任务打开的引用。入队结果独立于后续业务完成
    close_result = AppV3Record_Close(&reference);
    configASSERT(close_result == SYS_OK);
    return result;
}

//Control队列背压时保留旧结果，直到原消息真正入队
SysResult_t AppV3Mailbox_Flush(AppV3ServiceMailbox_t *mailbox)
{
    SysResult_t result;

    if (!App_MessagesIsOwner(mailbox->owner))
        return SYS_ERR_INVALID_STATE;
    if (!mailbox->pending_valid)
        return SYS_OK;
    result = App_MessagesSend(APP_CHANNEL_CONTROL, &mailbox->pending);
    if (result == SYS_OK)
        mailbox->pending_valid = false;
    return result == SYS_ERR_NO_RESOURCE ? SYS_ERR_BUSY : result;
}

//在所属任务取走一条请求；未交付旧结果前不能接受下一项业务
SysResult_t AppV3Mailbox_Receive(AppV3ServiceMailbox_t *mailbox, AppV3ServiceCommand_t *command)
{
    SysResult_t result, recorded;
    uint32_t *previous;

    if (!App_MessagesIsOwner(mailbox->owner) || !AppV3_BusinessReady() ||
        AppV3Record_Query(mailbox->owner, &recorded) != SYS_OK || recorded != SYS_OK)
        return SYS_ERR_INVALID_STATE;
    result = AppV3Mailbox_Flush(mailbox);
    if (result != SYS_OK)
        return result;
    result = AppV3Queue_Get(mailbox->queue, command, 0U);
    if (result != SYS_OK)
        return result;
    if ((uint32_t)command->requester >= RUNTIME_TARGET_COUNT || command->command_id == 0U)
        return SYS_ERR_INVALID_ARG;

    //同一来源的编号单调，重复或迟到请求不得再次改变业务状态
    previous = &mailbox->last_command[command->requester];
    if (command->command_id == *previous)
        return SYS_ERR_ALREADY_EXISTS;
    if (command->command_id < *previous)
        return SYS_ERR_PROTOCOL;
    if (mailbox->sequence == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;
    *previous = command->command_id;
    return SYS_OK;
}

//统一终结消息提交和背压，有无硬件快照通过独立标记区分
static SysResult_t Mailbox_Complete(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, bool simulated_running,
    uint32_t parameter, const AppV3HardwareReport_t *hardware)
{
    if (!App_MessagesIsOwner(mailbox->owner) || mailbox->pending_valid)
        return SYS_ERR_INVALID_STATE;
    if (mailbox->sequence != UINT32_MAX)
        ++mailbox->sequence;
    mailbox->pending = (AppMessage_t)
    {
        .type = result == SYS_OK ? APP_MESSAGE_ACK : APP_MESSAGE_FAULT,
        .source = mailbox->owner,
        .command_id = command->command_id,
        .stream_epoch = mailbox->sequence,
        .lease_id = parameter,
        .result = result,
        .frame_id = simulated_running ? 1U : 0U,
        .timestamp_ms = command->requester,
        .frame = { .size_bytes = APP_V3_SERVICE_RESULT_TAG }
    };
    if (hardware != NULL)
    {
        //保留52B运输布局，真实诊断只用原无指针字段
        mailbox->pending.frame_id = hardware->state;
        mailbox->pending.lease_id = hardware->primary;
        mailbox->pending.frame.width = hardware->flags;
        mailbox->pending.frame.height = hardware->first_fault < -65535 ? (uint16_t)(-SYS_ERR_IO) :
            (uint16_t)(-hardware->first_fault);
        mailbox->pending.frame.size_bytes = APP_V3_HARDWARE_RESULT_TAG;
        mailbox->pending.frame.size_bytes2 = hardware->detail;
    }
    mailbox->pending_valid = true;
    return AppV3Mailbox_Flush(mailbox);
}

//通用终结用于无法执行的请求，保留兼容消息标记
SysResult_t AppV3Mailbox_Complete(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, bool simulated_running, uint32_t parameter)
{
    return Mailbox_Complete(mailbox, command, result, simulated_running, parameter, NULL);
}

//真实命令返回独立硬件快照，运行兼容位保持为零
SysResult_t AppV3Mailbox_CompleteHardware(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, const AppV3HardwareReport_t *report)
{
    return Mailbox_Complete(mailbox, command, result, false, 0U, report);
}
