#ifndef CM7_BSP_USB_H
#define CM7_BSP_USB_H
#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

//EP0控制端点最大包长
#define BSP_USB_EP0_MAX_PACKET_SIZE 64U

//USB2控制器包含EP0，共9组双向端点
#define BSP_USB_ENDPOINT_COUNT 9U

//每个非零端点最多保留三种传输类型的规划需求
#define BSP_USB_MAX_ENDPOINT_CONFIGS 48U

typedef struct
{
    uint8_t address;      //方向位加端点编号
    uint8_t type;         //USB类型：1等时、2批量、3中断
    uint16_t max_packet;  //描述符声明的最大包长
} BspUsbEndpointConfig_t;

//停止状态一次规划整个Class所需FIFO，不打开端点
SysResult_t BSP_USB_ConfigureEndpoints(const BspUsbEndpointConfig_t *config, uint32_t count);

//PCD事件由Platform接收，BSP不依赖USB Device Core
typedef struct
{
    void (*setup)(uint8_t *data);
    void (*data_out)(uint8_t ep, uint8_t *data);
    void (*data_in)(uint8_t ep, uint8_t *data);
    void (*reset)(void);
    void (*suspend)(void);
    void (*resume)(void);
    void (*connect)(void);
    void (*disconnect)(void);

    //等时端点未完成，由Platform转交当前Class
    void (*iso_incomplete_in)(uint8_t ep);

    //OUT方向同样只上报端点编号
    void (*iso_incomplete_out)(uint8_t ep);

    //普通IN传输分包续传失败
    void (*data_in_error)(uint8_t ep, SysResult_t result);

    //通用1ms总线事件，不解释Class协议
    void (*sof)(uint16_t frame);

    //可选成套OUT契约的受控故障通知：只门控，不释放或停止硬件
    void (*out_fault)(SysResult_t result);

} BspUsbCallbacks_t;

//初始化USB2 FS内部PHY、时钟和PCD，不连接主机
SysResult_t BSP_USB_Init(void);

//绑定静态事件表，只允许在USB停止时调用
SysResult_t BSP_USB_SetCallbacks(const BspUsbCallbacks_t *callbacks);

//连接主机并使能USB中断
SysResult_t BSP_USB_Start(void);

//断开主机并禁用USB中断，可再次启动
SysResult_t BSP_USB_Stop(void);

//释放PCD和USB引脚，不关闭其他模块使用的GPIO时钟
SysResult_t BSP_USB_DeInit(void);

//按FS规范校验地址、类型和包长，IN端点需满足已规划FIFO容量
SysResult_t BSP_USB_OpenEp(uint8_t ep, uint8_t type, uint16_t size);

//取消指定端点的传输，成功后缓冲区不再被硬件访问
SysResult_t BSP_USB_AbortEp(uint8_t ep);

//停止所有非零端点传输，统一清理共享RX FIFO
SysResult_t BSP_USB_AbortAll(void);

//关闭指定方向的端点
SysResult_t BSP_USB_CloseEp(uint8_t ep);

//清空指定方向的端点的FIFO
SysResult_t BSP_USB_FlushEp(uint8_t ep);

//设置指定方向的STALL，等时端点不支持
SysResult_t BSP_USB_StallEp(uint8_t ep);

//清除指定方向的STALL，等时端点不支持
SysResult_t BSP_USB_ClearStallEp(uint8_t ep);

//读取端点STALL状态
bool BSP_USB_IsStallEp(uint8_t ep);

//更新USB设备地址
SysResult_t BSP_USB_SetAddress(uint8_t address);

//发送IN数据；等时每次不超过最大包长，Bulk和Interrupt支持多包

//EP0保留0x00和0x80两种调用地址；缓冲区保持有效直到完成回调
SysResult_t BSP_USB_Transmit(uint8_t ep, uint8_t *data, uint32_t size);

//准备OUT接收，非零端点缓冲区容量须为最大包长整数倍
SysResult_t BSP_USB_Receive(uint8_t ep, uint8_t *data, uint32_t size);

//取得指定OUT端点实际接收字节数
uint32_t BSP_USB_GetRxSize(uint8_t ep);

//Bulk IN停止身份在整个启动周期不回绕；不表示SDMMC或Class已释放
typedef struct
{
    uint32_t controller_generation;
    uint32_t session_generation;
    uint32_t endpoint_generation;
    uint32_t transfer_sequence;
    uint8_t endpoint_address;
} BspUsbInIdentity_t;

typedef enum
{
    BSP_USB_IN_CLOSED = 0, BSP_USB_IN_IDLE, BSP_USB_IN_ACTIVE,
    BSP_USB_IN_QUIESCING, BSP_USB_IN_QUIESCENT, BSP_USB_IN_FAULTED
} BspUsbInState_t;

typedef enum
{
    BSP_USB_STOP_NONE = 0, BSP_USB_STOP_NAK, BSP_USB_STOP_DISABLE,
    BSP_USB_STOP_AHB, BSP_USB_STOP_FIFO, BSP_USB_STOP_RETIRE,
    BSP_USB_STOP_PROVEN, BSP_USB_STOP_COMMITTED, BSP_USB_STOP_FAULT
} BspUsbInStopStage_t;

typedef enum
{
    BSP_USB_STOP_PENDING = 0, BSP_USB_STOP_COMPLETED, BSP_USB_STOP_FAILED,
    BSP_USB_STOP_RECOVERY_REQUIRED, BSP_USB_STOP_ALREADY_COMPLETED
} BspUsbInStopResult_t;

typedef struct
{
    BspUsbInIdentity_t transfer;
    uint32_t stop_attempt;
    BspUsbInState_t state;
    BspUsbInStopStage_t stage;
    BspUsbInStopResult_t result;
    SysResult_t error;
    uint32_t queued_bytes;
    uint32_t completed_bytes;
    uint32_t current_packet_length;
    uint32_t initial_ctl;
    uint32_t initial_events;
    uint32_t saved_daint_mask;
    uint32_t saved_empty_mask;
    uint32_t events;
    uint32_t packet_completions;
    uint32_t source_returns;
    uint32_t pool_returns;
    uint32_t pio_users;
    bool source_owned;
    bool pool_owned;
    bool nak_effective;
    bool disable_observed;
    bool xfrc_claimed;
    bool completed_undelivered;
    bool fifo_complete;
    bool events_retired;
    SysResult_t fifo_result;
    SysResult_t first_error;
    BspUsbInStopStage_t failed_stage;
    uint32_t failure_count;
    uint32_t reconcile_count;
    bool fifo_conflicted;
} BspUsbInStopProof_t;

//三个接口仅由同一USB任务串行推进；ISR、错误身份及耗尽均拒绝
SysResult_t BSP_USB_GetBulkInProof(uint8_t ep, BspUsbInStopProof_t *proof);
SysResult_t BSP_USB_BeginBulkInStop(const BspUsbInIdentity_t *identity,
                                  uint32_t timeout_ms, BspUsbInStopProof_t *proof);
SysResult_t BSP_USB_PollBulkInStop(const BspUsbInIdentity_t *identity,
                                 uint32_t attempt, BspUsbInStopProof_t *proof);
SysResult_t BSP_USB_CommitBulkInRetirement(const BspUsbInIdentity_t *identity,
                                         uint32_t attempt, BspUsbInStopProof_t *proof);
//显式同Attempt复核；仅允许已取得独立硬件完成证据的超时收尾
SysResult_t BSP_USB_ReconcileBulkInStop(const BspUsbInIdentity_t *identity,
                                      uint32_t attempt, BspUsbInStopProof_t *proof);
//只查询资源引用，不能据此替代USB或外部DMA停止证明
bool BSP_USB_BulkInHasReferences(void);

//成套OUT生命周期只在成功的全新Init与首次Start之间显式启用，不追认旧会话或在途传输
//当前只允许内部CPU可写SRAM Buffer；独立于SDMMC IDMA/文件系统所有权
SysResult_t BSP_USB_EnableBulkOutLifecycle(void);
typedef BspUsbInIdentity_t BspUsbOutIdentity_t;
typedef enum
{
    BSP_USB_OUT_NONE = 0, BSP_USB_OUT_WAIT_NAK, BSP_USB_OUT_DISABLE,
    BSP_USB_OUT_DRAIN, BSP_USB_OUT_PROVEN, BSP_USB_OUT_COMMITTED, BSP_USB_OUT_FAULT
} BspUsbOutStopStage_t;
typedef struct
{
    BspUsbOutIdentity_t transfer;
    uint32_t stop_attempt, lease_id, barrier_sequence;
    uint8_t *buffer;
    uint32_t capacity, received, pio_references, events, source_returns;
    BspUsbOutStopStage_t stage, failed_stage;
    SysResult_t error, first_error;
    uint32_t reconcile_count;
    bool buffer_owned, fenced, normal_complete, disable_requested, disable_observed;
    bool events_retired, lease_done, lease_drop_requested;
} BspUsbOutStopProof_t;
SysResult_t BSP_USB_GetBulkOutProof(uint8_t ep, BspUsbOutStopProof_t *proof);
SysResult_t BSP_USB_BeginBulkOutStop(const BspUsbOutIdentity_t *identity, uint32_t timeout_ms,
                                   BspUsbOutStopProof_t *proof);
SysResult_t BSP_USB_PollBulkOutStop(const BspUsbOutIdentity_t *identity, uint32_t attempt,
                                  BspUsbOutStopProof_t *proof);
SysResult_t BSP_USB_CommitBulkOutRetirement(const BspUsbOutIdentity_t *identity, uint32_t attempt,
                                          BspUsbOutStopProof_t *proof);
SysResult_t BSP_USB_ReconcileBulkOutStop(const BspUsbOutIdentity_t *identity, uint32_t attempt,
                                       BspUsbOutStopProof_t *proof);
bool BSP_USB_BulkOutHasReferences(void);

#endif
