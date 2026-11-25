#include "network_service.h"
#include "../../Core/app_messages.h"
#include "../../Tasks/service_tasks.h"
#include "eth_execution.h"
#include "eth_http.h"
#include "eth_memory.h"
#include "platform_sync.h"
#include "platform_time.h"

//保留旧 PCB 直到确认退休，等待包含 lwIP TIME_WAIT 的完整生命周期。
#define NETWORK_STOP_TIMEOUT_MS 130000U
#define NETWORK_POLL_MS 10U
#define NETWORK_HTTP 0x100U

static struct
{
    AppV3ServiceMailbox_t mailbox;
    AppV3HardwareRequest_t request;
    AppV3HardwareReport_t hardware;
    const AppV3Context_t *context;
    uint32_t mode;
    uint32_t stop_started;
    bool started;
    bool interface_owned;
    bool transport_bound;
    bool http_bound;
    bool stopping;
} s_network;

static EthNetwork_t s_interface __attribute__((aligned(32)));
static AppV3Queue_t s_endpoint;
static const uint8_t s_mac[6] = {0x02, 0x47, 0x47, 0x00, 0x00, 0x01};

//排除 ISR、临界区和其他任务，所有 Raw API 使用同一拥有者。
static int NetworkService_IsOwner(void *context)
{
    (void)context;
    return App_MessagesIsOwner(RUNTIME_TARGET_NETWORK) &&
        Platform_SyncGetPrimask() == 0U && Platform_SyncGetBasepri() == 0U;
}

static err_t NetworkService_Udp(const ip4_addr_t *ip, uint16_t port,
    const uint8_t *data, uint16_t length, void *arg)
{
    (void)arg;
    return eth_udp_send(ip, port, data, length);
}

//正式视频源接入前明确报告无帧，不获取 Camera 资源。
static EthMjpegAcquire_t NetworkService_Acquire(void *context,
    EthMjpegFrame_t *frame, uint32_t *lease)
{
    (void)context;
    (void)frame;
    (void)lease;
    return ETH_MJPEG_UNAVAILABLE;
}

static void NetworkService_Release(void *context, uint32_t lease)
{
    (void)context;
    (void)lease;
}

static const EthMjpegSource_t s_source =
{
    NULL, NetworkService_Acquire, NetworkService_Release
};

static void NetworkService_Observe(void)
{
    uint16_t flags = 0U;
    if (s_network.started)
    {
        if (netif_is_link_up(&s_interface.interface))
            flags |= 1U;
        if (!ip4_addr_isany(netif_ip4_addr(&s_interface.interface)))
            flags |= 2U;
        if (eth_tcp_listening())
            flags |= 4U;
        s_network.hardware.primary =
            ip4_addr_get_u32(netif_ip4_addr(&s_interface.interface));
        s_network.hardware.detail = (uint32_t)s_interface.last_driver_result;
    }
    s_network.hardware.flags = flags;
}

static SysResult_t NetworkService_Start(uint32_t mode)
{
    EthNetworkAddressConfig_t address = {.mode = ETH_ADDRESS_STATIC};
    EthTransportConfig_t transport = {0};
    err_t result;
    if ((mode & ~UINT32_C(0x103)) != 0U || (mode & 3U) > 2U)
        return SYS_ERR_INVALID_ARG;
    if (s_network.started || s_network.interface_owned)
        return SYS_ERR_ALREADY_EXISTS;
    if (!eth_memory_valid())
        return SYS_ERR_INVALID_STATE;
    address.mode = (EthNetworkAddressMode_t)(mode & 3U);
    IP4_ADDR(&address.ip, 192, 168, 50, 2);
    IP4_ADDR(&address.mask, 255, 255, 255, 0);
    ip4_addr_set_zero(&address.gateway);
    address.timeout_ms = 30000U;
    s_network.interface_owned = true;
    result = eth_network_init_address(&s_interface, s_mac, &address);
    if (result != ERR_OK)
        return SYS_ERR_IO;
    s_network.started = true;
    if (mode & NETWORK_HTTP)
    {
        result = eth_http_init_source(&s_interface, &s_source, 200U);
        s_network.http_bound = result == ERR_OK;
    }
    else
    {
        transport.listen_port = 5003U;
        transport.udp_port = 5005U;
        transport.udp_receive = NetworkService_Udp;
        result = eth_transport_init(&s_interface, &transport);
    }
    s_network.transport_bound = result == ERR_OK;
    if (result != ERR_OK)
        return SYS_ERR_IO;
    s_network.mode = mode;
    return SYS_OK;
}

static void NetworkService_BeginStop(void)
{
    if (s_network.stopping)
        return;
    if (s_network.http_bound)
        eth_http_begin_stop();
    if (s_network.transport_bound)
        eth_transport_begin_stop();
    s_network.stop_started = Platform_TimeGetMs();
    s_network.stopping = true;
    s_network.hardware.state = APP_V3_HW_QUIESCING;
}

static SysResult_t NetworkService_Stop(void)
{
    err_t result;
    if (s_network.transport_bound)
    {
        result = eth_transport_finish_stop();
        if (result == ERR_INPROGRESS)
            return SYS_ERR_BUSY;
        if (result != ERR_OK)
            return SYS_ERR_IO;
        s_network.transport_bound = false;
    }
    if (s_network.http_bound)
    {
        if (eth_http_finish_stop() != ERR_OK)
            return SYS_ERR_BUSY;
        s_network.http_bound = false;
    }
    if (s_network.interface_owned)
    {
        if (eth_network_stop(&s_interface) != ERR_OK)
            return SYS_ERR_IO;
        s_network.interface_owned = false;
    }
    s_network.started = false;
    s_network.stopping = false;
    return SYS_OK;
}

//一次只接受一个事务，旧命令和结果仍由共用 Mailbox 保留。
static SysResult_t NetworkService_Readable(AppV3Queue_t queue, void *argument)
{
    AppV3ServiceCommand_t command = {0};
    SysResult_t result;
    (void)argument;
    if (!NetworkService_IsOwner(NULL) ||
        queue.index != s_network.mailbox.queue.index ||
        queue.generation != s_network.mailbox.queue.generation)
        return SYS_ERR_INVALID_STATE;
    if (s_network.request.pending)
        return SYS_ERR_BUSY;
    result = AppV3Mailbox_Receive(&s_network.mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;
    if (result == SYS_OK &&
        (command.operation < NETWORK_SERVICE_START || command.operation > NETWORK_SERVICE_QUERY))
        result = SYS_ERR_NOT_SUPPORTED;
    if (result != SYS_OK)
        return AppV3Mailbox_CompleteHardware(&s_network.mailbox, &command, result, &s_network.hardware);
    s_network.request.command = command;
    s_network.request.started_ms = Platform_TimeGetMs();
    s_network.request.pending = true;
    if (command.operation == NETWORK_SERVICE_STOP || command.operation == NETWORK_SERVICE_RESTART)
        NetworkService_BeginStop();
    return SYS_OK;
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
    s_network.hardware.state = APP_V3_HW_READY;
    return SYS_OK;
}

void AppV3_NetworkDiscard(void)
{
    if (NetworkService_IsOwner(NULL))
        AppV3Mailbox_Discard(&s_network.mailbox, &s_endpoint);
}

uint32_t AppV3_NetworkAdvance(void)
{
    SysResult_t result = SYS_OK;
    uint32_t operation;
    if (!NetworkService_IsOwner(NULL))
        return NETWORK_POLL_MS;
    if (s_network.started)
    {
        eth_network_poll(&s_interface);
        eth_transport_poll();
        if (s_network.http_bound)
            eth_http_poll();
    }
    NetworkService_Observe();
    if (!s_network.request.pending)
        return NETWORK_POLL_MS;
    operation = s_network.request.command.operation;
    if (operation == NETWORK_SERVICE_START)
    {
        if (s_network.hardware.state == APP_V3_HW_FAULT)
            result = SYS_ERR_INVALID_STATE;
        else
            result = NetworkService_Start(s_network.request.command.argument);
    }
    else if (operation == NETWORK_SERVICE_STOP || operation == NETWORK_SERVICE_RESTART)
    {
        result = NetworkService_Stop();
        if (result == SYS_ERR_BUSY &&
            Platform_TimeGetMs() - s_network.stop_started < NETWORK_STOP_TIMEOUT_MS)
            return NETWORK_POLL_MS;
        if (result == SYS_ERR_BUSY)
            result = SYS_ERR_TIMEOUT;
        if (result == SYS_OK && operation == NETWORK_SERVICE_RESTART)
            result = NetworkService_Start(s_network.request.command.argument);
    }
    if (result != SYS_OK && result != SYS_ERR_ALREADY_EXISTS)
    {
        if (s_network.hardware.first_fault == SYS_OK)
            s_network.hardware.first_fault = result;
        s_network.hardware.state = APP_V3_HW_FAULT;
    }
    else if (operation != NETWORK_SERVICE_QUERY)
        s_network.hardware.state = s_network.started ? APP_V3_HW_ACTIVE : APP_V3_HW_READY;
    //START 的终结表示软件服务已接纳，不表示 DHCP 或 listener 已建立。
    s_network.request.pending = false;
    NetworkService_Observe();
    if (AppV3Mailbox_CompleteHardware(&s_network.mailbox, &s_network.request.command,
        result, &s_network.hardware) != SYS_OK && !s_network.mailbox.pending_valid)
        s_network.hardware.state = APP_V3_HW_FAULT;
    return NETWORK_POLL_MS;
}

SysResult_t NetworkService_Request(uint32_t command_id,
    NetworkServiceOperation_t operation, uint32_t argument)
{
    return AppV3Mailbox_Request(RUNTIME_TARGET_NETWORK, &s_endpoint,
        command_id, operation, argument);
}

void AppV3_NetworkRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_network.mailbox, AppV3_NetworkAdvance);
}
