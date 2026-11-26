#include "network_service.h"
#include "network_frames.h"
#include "../../Core/app_messages.h"
#include "../../Tasks/service_tasks.h"
#include "eth_execution.h"
#include "eth_http.h"
#include "eth_memory.h"
#include "platform_sync.h"
#include "platform_time.h"
#include "lwip/stats.h"
#include "stm32h7xx.h"

//保留旧 PCB 直到确认退休，等待包含 lwIP TIME_WAIT 的完整生命周期。
#define NETWORK_STOP_TIMEOUT_MS 130000U
#define NETWORK_POLL_MS 10U
#define NETWORK_HTTP 0x100U
//沿用 E7 的 50ms 请求周期；实际帧率由复制、背压和 TCP ACK 决定。
#define NETWORK_FRAME_PERIOD_MS 50U

static struct
{
    //现有异步命令、结果背压与单调事务身份。
    AppV3ServiceMailbox_t mailbox;
    AppV3HardwareRequest_t request;
    AppV3HardwareReport_t hardware;
    //启动屏障后常驻的只读配置，不能由 NetworkTask 改写。
    const AppV3Context_t *context;
    uint32_t mode;
    uint32_t stop_started;//资源退休的整体截止时间起点
    bool started;
    bool stack_observable;//全局 lwIP 初始化后统计对象一直有效
    bool interface_owned;//初始化中途失败也保留实例，STOP 成功才撤销
    bool transport_bound;//已绑定配置，可能仍有 TIME_WAIT PCB
    bool http_bound;//动态 Source 有效期覆盖全部私有帧借用
    bool stopping;
} s_network;

static EthNetwork_t s_interface __attribute__((aligned(32)));
static AppV3Queue_t s_endpoint;
volatile NetworkServiceStatus_t g_network_status;
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

static void NetworkService_Observe(void)
{
    uint16_t flags = 0U;
    s_network.hardware.primary = 0U;
    if (s_network.started)
    {
        if (netif_is_link_up(&s_interface.interface))
            flags |= 1U;
        if (!ip4_addr_isany(netif_ip4_addr(&s_interface.interface)))
            flags |= 2U;
        if (eth_tcp_listening())
            flags |= 4U;
        if (eth_tcp_get_state(eth_tcp_handle(ETH_TCP_CLIENT)) == ETH_TCP_OPEN)
            flags |= 8U;
        s_network.hardware.primary =
            ip4_addr_get_u32(netif_ip4_addr(&s_interface.interface));
        s_network.hardware.detail = (uint32_t)s_interface.last_driver_result;
    }
    s_network.hardware.flags = flags;
    ++g_network_status.heartbeat;
    g_network_status.ccr = SCB->CCR;
    g_network_status.lifecycle = (uint32_t)s_network.hardware.state;
    g_network_status.flags = flags;
    g_network_status.address = s_interface.address;
    if (!s_network.stack_observable)
        return;
    (void)eth_mac_get_statistics((EthStatistics_t *)&g_network_status.driver);
    eth_transport_get_statistics((EthTransportStats_t *)&g_network_status.transport);
    eth_http_get_statistics((EthHttpStats_t *)&g_network_status.http);
#define NETWORK_POOL_READ(field, pool) do { \
    g_network_status.field.used = lwip_stats.memp[pool]->used; \
    g_network_status.field.peak = lwip_stats.memp[pool]->max; \
    g_network_status.field.errors = lwip_stats.memp[pool]->err; \
} while (0)
    NETWORK_POOL_READ(pbuf, MEMP_PBUF_POOL);
    NETWORK_POOL_READ(tcp_pcb, MEMP_TCP_PCB);
    NETWORK_POOL_READ(tcp_listen, MEMP_TCP_PCB_LISTEN);
    NETWORK_POOL_READ(tcp_seg, MEMP_TCP_SEG);
    NETWORK_POOL_READ(udp_pcb, MEMP_UDP_PCB);
    NETWORK_POOL_READ(timers, MEMP_SYS_TIMEOUT);
#undef NETWORK_POOL_READ
    g_network_status.heap.used = lwip_stats.mem.used;
    g_network_status.heap.peak = lwip_stats.mem.max;
    g_network_status.heap.errors = lwip_stats.mem.err;
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
    g_network_status.memory_valid = (uint32_t)eth_memory_valid();
    if (!g_network_status.memory_valid)
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
    s_network.stack_observable = true;
    s_network.started = true;
    if (mode & NETWORK_HTTP)
    {
        result = eth_http_init_source(&s_interface, NetworkFrames_Source(), NETWORK_FRAME_PERIOD_MS);
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

//非 HTTP 通道提供有界回送，发送环不足时不消费接收数据。
static void NetworkService_DataPoll(void)
{
    uint8_t data[256];
    unsigned role;
    for (role = 0U; role < ETH_TCP_ROLES; ++role)
    {
        EthTcpStats_t stats;
        EthTcpHandle_t handle = eth_tcp_handle((EthTcpRole_t)role);
        uint16_t amount, available;
        eth_tcp_get_statistics((EthTcpRole_t)role, &stats);
        if (stats.state != ETH_TCP_OPEN || stats.rx_pending == 0U)
            continue;
        available = (uint16_t)(ETH_TCP_QUEUE_BYTES - stats.tx_pending);
        amount = stats.rx_pending < sizeof(data) ? stats.rx_pending : sizeof(data);
        if (amount > available)
            amount = available;
        if (amount != 0U)
        {
            amount = eth_tcp_read(handle, data, amount);
            if (amount != 0U && eth_tcp_send(handle, data, amount) != ERR_OK)
            {
                s_network.hardware.first_fault = SYS_ERR_IO;
                s_network.hardware.state = APP_V3_HW_FAULT;
            }
        }
    }
}

static void NetworkService_BeginStop(void)
{
    if (s_network.stopping)
        return;
    NetworkFrames_BeginStop();
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
    if (!NetworkFrames_Retired())
        return SYS_ERR_BUSY;
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
        (command.operation < NETWORK_SERVICE_START || command.operation > NETWORK_SERVICE_CLOSE_CLIENT))
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
        if (!s_network.stopping && s_interface.last_driver_result != ETH_OK &&
            s_interface.last_driver_result != ETH_LINK_DOWN &&
            s_interface.last_driver_result != ETH_BUSY)
        {
            //协议资源继续退休；故障只能由显式 STOP/RESTART 恢复，避免循环重启硬件。
            NetworkService_BeginStop();
            s_network.hardware.first_fault = SYS_ERR_IO;
            s_network.hardware.state = APP_V3_HW_FAULT;
        }
        eth_transport_poll();
        if (s_network.http_bound)
            eth_http_poll();
        else if (!s_network.stopping)
            NetworkService_DataPoll();
    }
    if (!s_network.started)
        eth_network_timers_poll();
    NetworkFrames_Poll(s_network.started && s_network.http_bound && !s_network.stopping &&
        s_network.hardware.state == APP_V3_HW_ACTIVE);
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
    else if (operation == NETWORK_SERVICE_CONNECT)
    {
        ip4_addr_t peer;
        ip4_addr_set_u32(&peer, s_network.request.command.argument);
        if (!s_network.started || s_network.http_bound || !eth_transport_ready() ||
            ip4_addr_isany(&peer) || ip4_addr_ismulticast(&peer) || ip4_addr_isloopback(&peer))
            result = SYS_ERR_INVALID_STATE;
        else if (eth_tcp_client_connect(&s_interface.interface, &peer, 5004U) != ERR_OK)
            result = SYS_ERR_IO;
    }
    else if (operation == NETWORK_SERVICE_CLOSE_CLIENT)
    {
        err_t closed = eth_tcp_close(eth_tcp_handle(ETH_TCP_CLIENT));
        if (closed != ERR_OK && closed != ERR_ALREADY && closed != ERR_CONN)
            result = SYS_ERR_IO;
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
    if (result == SYS_ERR_IO || result == SYS_ERR_TIMEOUT || result == SYS_ERR_PROTOCOL)
    {
        if (s_network.hardware.first_fault == SYS_OK)
            s_network.hardware.first_fault = result;
        s_network.hardware.state = APP_V3_HW_FAULT;
    }
    else if (result == SYS_OK && operation != NETWORK_SERVICE_QUERY)
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
