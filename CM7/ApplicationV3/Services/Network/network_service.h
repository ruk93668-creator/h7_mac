//正式网络服务复用 ApplicationV3 的启动屏障、Record 和 Mailbox。
#ifndef APP_V3_NETWORK_SERVICE_H
#define APP_V3_NETWORK_SERVICE_H

#include "app_context.h"
#include "eth_http.h"
#include "../../Runtime/service_mailbox.h"

//调试只读快照由 NetworkTask 更新；跨字段一致性请暂停 CPU 后读取。
//业务方通过 QUERY 的原有终结消息取得状态，不得写此镜像或读取 PCB。
typedef struct
{
    uint32_t used;//当前占用项数或字节数
    uint32_t peak;//运行期间峰值
    uint32_t errors;//分配失败累计值
} NetworkPoolStatus_t;

typedef struct
{
    uint32_t heartbeat;//网络任务轮询次数，不代表协议已就绪
    uint32_t ccr;//实际 SCB CCR，DCache 位为 0x10000
    uint32_t memory_valid;//最近 START 对 DMA MPU 窗口的检查结果
    uint32_t lifecycle;//AppV3HardwareState_t 的值
    uint32_t flags;//Link/IP/listener/client-open：1/2/4/8
    EthNetworkAddressStatus_t address;//DHCP 及当前地址，由唯一拥有者采样
    EthStatistics_t driver;//实际 MAC/DMA 统计，不能把 RBU 当作丢帧数
    EthTransportStats_t transport;//连接状态、发送环及 UDP 统计
    EthHttpStats_t http;//HTTP/MJPEG 状态与完整帧确认统计
    NetworkPoolStatus_t pbuf;//CPU 管理的 PBUF_POOL
    NetworkPoolStatus_t heap;//lwIP 静态 heap，单位字节
    NetworkPoolStatus_t tcp_pcb;//活动与 TIME_WAIT 共用的 PCB 池
    NetworkPoolStatus_t tcp_listen;//监听 PCB 池
    NetworkPoolStatus_t tcp_seg;//lwIP TCP 段池
    NetworkPoolStatus_t udp_pcb;//DHCP 与业务 UDP 共用池
    NetworkPoolStatus_t timers;//协议超时条目池
} NetworkServiceStatus_t;

extern volatile NetworkServiceStatus_t g_network_status;

//只有 NetworkTask 可以初始化、轮询和清理协议栈。
//初始化只发布软件服务，不把 PHY Link 或 DHCP 租约作为启动屏障。
SysResult_t AppV3_NetworkInit(const AppV3Context_t *context);
//仅用于 Record 发布前失败的回收，不代表运行中的网络停止。
void AppV3_NetworkDiscard(void);
//Registry 登记的常驻网络任务入口。
void AppV3_NetworkRun(void *argument);
//网络任务逐轮处理邮箱和资源，返回有界等待毫秒数。
uint32_t AppV3_NetworkAdvance(void);

//业务命令由登记的其他任务提交，SYS_OK 只表示入队。
//START 参数低两位为 static/DHCP/fallback（0/1/2），0x100 选择 HTTP。
typedef enum
{
    NETWORK_SERVICE_START = 16,
    NETWORK_SERVICE_STOP,
    NETWORK_SERVICE_RESTART,
    NETWORK_SERVICE_QUERY,
    //非 HTTP 服务向 argument 指定的网络字节序 IPv4 发起 TCP 5004 连接。
    //成功只表示发起；QUERY flags 的 0x08 表示已建立，连接错误由后续快照报告。
    NETWORK_SERVICE_CONNECT,
    //有序关闭 Client，实际退休仍由 STOP 的完整资源门禁确认。
    NETWORK_SERVICE_CLOSE_CLIENT
} NetworkServiceOperation_t;

//登记的普通任务提交，禁止 ISR、NetworkTask 自调度及阻塞等待。
//command_id 必须非零，且在同一来源内单调递增；argument 复制为值，不借用调用者 Buffer。
//SYS_OK 仅表示入队，队满返回 SYS_ERR_BUSY；业务结果通过现有 Service Events 取得。
//STOP/RESTART 最多等待 130 秒退休；失败保留未退休资源，须显式 STOP 重试，不得清状态冒充完成。
//全局 lwIP 初始化和定时器持续存在；成功 STOP 只证明协议端点、netif、DMA 和帧租约已退出。
SysResult_t NetworkService_Request(uint32_t command_id,
    NetworkServiceOperation_t operation, uint32_t argument);

#endif
