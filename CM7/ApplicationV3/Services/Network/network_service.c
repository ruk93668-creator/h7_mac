#include "network_service.h"
#include "../../Core/app_messages.h"
#include "../../Tasks/service_tasks.h"
#include "eth_execution.h"
#include "platform_sync.h"

static struct
{
    AppV3ServiceMailbox_t mailbox;
    const AppV3Context_t *context;
} s_network;

static AppV3Queue_t s_endpoint;

//排除 ISR、临界区和其他任务，所有 Raw API 使用同一拥有者。
static int NetworkService_IsOwner(void *context)
{
    (void)context;
    return App_MessagesIsOwner(RUNTIME_TARGET_NETWORK) &&
        Platform_SyncGetPrimask() == 0U && Platform_SyncGetBasepri() == 0U;
}

//未接入的命令给出明确拒绝，不能将空处理伪报为启动成功。
static SysResult_t NetworkService_Readable(AppV3Queue_t queue, void *argument)
{
    AppV3ServiceCommand_t command = {0};
    SysResult_t result;
    (void)argument;

    if (!NetworkService_IsOwner(NULL) ||
        queue.index != s_network.mailbox.queue.index ||
        queue.generation != s_network.mailbox.queue.generation)
        return SYS_ERR_INVALID_STATE;
    result = AppV3Mailbox_Receive(&s_network.mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;
    if (result == SYS_OK)
        result = SYS_ERR_NOT_SUPPORTED;
    return AppV3Mailbox_Complete(&s_network.mailbox, &command, result, false, 0U);
}

SysResult_t AppV3_NetworkInit(const AppV3Context_t *context)
{
    SysResult_t result;

    if (!NetworkService_IsOwner(NULL) || context == NULL)
        return SYS_ERR_INVALID_STATE;
    if (s_network.context != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    result = AppV3Mailbox_Init(&s_network.mailbox, RUNTIME_TARGET_NETWORK,
        NetworkService_Readable, &s_endpoint);
    if (result != SYS_OK)
        return result;
    if (!eth_execution_bind(NetworkService_IsOwner, NULL))
    {
        AppV3Mailbox_Discard(&s_network.mailbox, &s_endpoint);
        return SYS_ERR_INVALID_STATE;
    }
    s_network.context = context;
    return SYS_OK;
}

void AppV3_NetworkDiscard(void)
{
    if (!NetworkService_IsOwner(NULL))
        return;
    AppV3Mailbox_Discard(&s_network.mailbox, &s_endpoint);
    //上下文门禁绑定仍有效，未发布服务不得在其他任务重新初始化。
}

uint32_t AppV3_NetworkAdvance(void)
{
    return 10U;
}

void AppV3_NetworkRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_network.mailbox, AppV3_NetworkAdvance);
}
