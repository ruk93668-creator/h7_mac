#ifndef CM7_V3_SERVICE_EVENTS_H
#define CM7_V3_SERVICE_EVENTS_H

#include "../Core/app_messages.h"

//真实结果状态独立于模拟运行位，常驻Service当前不开放动态退场
typedef enum
{
    APP_V3_HW_UNINITIALIZED = 0,
    APP_V3_HW_READY,
    APP_V3_HW_STARTING,
    APP_V3_HW_ACTIVE,
    APP_V3_HW_QUIESCING,
    APP_V3_HW_FAULT
} AppV3HardwareState_t;

//各服务发布的硬件状态与诊断值
typedef struct
{
    //服务已观察到的硬件生命周期状态
    AppV3HardwareState_t state;
    //本事务首个错误，后续清理结果不得覆盖
    SysResult_t first_fault;
    //硬件查询的主要计数或容量，具体单位由服务定义
    uint32_t primary;
    //硬件查询的辅助计数或容量，含义由服务定义
    uint32_t detail;
    //服务发布的状态位，只能按对应状态报告解释
    uint16_t flags;
} AppV3HardwareReport_t;


//与原命令匹配的终结报告
typedef struct
{
    //服务身份，用于筛选结果或发送生命周期命令
    RuntimeEventTarget_t service;
    //发起业务请求的任务，用于区分重复命令号
    RuntimeEventTarget_t requester;
    //发起者分配的命令编号，用于识别匹配结果
    uint32_t command_id;
    //所属通道的终结序号，用于检查发布顺序
    uint32_t sequence;
    //最近一次操作或终结结果，保留原错误码
    SysResult_t result;
    //随结果返回的值类型参数
    uint32_t parameter;
    //兼容既有结果布局的协议位，正式硬件路径保持为零
    bool simulated_running;
    //标记终结消息是否包含实际设备状态
    bool real_hardware;
    //由服务发布的硬件状态快照
    AppV3HardwareReport_t hardware;
} AppV3ServiceEvent_t;

//Control业务结果分发诊断
typedef struct
{
    //通过验证的结果总数
    uint32_t accepted;
    //格式、状态或身份不符的拒绝总数
    uint32_t rejected;
    //订阅者回调返回错误的累计次数
    uint32_t callback_failures;
    //首次业务分发错误，供诊断定位
    SysResult_t first_error;
    //最近被拒绝的消息类型
    AppMessageType_t rejected_type;
    //最近被拒绝的消息来源
    RuntimeEventTarget_t rejected_source;
    //最近被拒绝的命令编号
    uint32_t rejected_command_id;
} AppV3BusinessDiagnostics_t;

//回调由Control同步执行，只能短时处理，不阻塞；event仅在回调期间有效
typedef SysResult_t (*AppV3BusinessCallback_t)(const AppV3ServiceEvent_t *event, void *context);

//Control独占订阅和上下文生命周期；COUNT订阅所有服务
SysResult_t AppV3Business_Subscribe(RuntimeEventTarget_t service,
    AppV3BusinessCallback_t callback, void *context, uint32_t *ticket);
//当前回调执行期间撤销其自身票据返回BUSY；回调退出后再撤销并释放Context
SysResult_t AppV3Business_Unsubscribe(uint32_t ticket);
//普通任务零等待取本人的终结结果；BUSY表示同一命令仍待完成
SysResult_t AppV3Business_TakeResult(RuntimeEventTarget_t service, uint32_t command_id,
    AppV3ServiceEvent_t *result);

//提交侧内部接口：先预留结果槽；仅在请求未入队时撤销
SysResult_t AppV3Business_ReserveResult(RuntimeEventTarget_t requester, RuntimeEventTarget_t service,
    uint32_t command_id);
//撤销尚未提交的结果保留项
void AppV3Business_CancelReservation(RuntimeEventTarget_t requester, RuntimeEventTarget_t service,
    uint32_t command_id);

//由Control按批次发布可交付的终结结果
SysResult_t AppV3Business_Drain(uint32_t *consumed);
//复制当前业务结果诊断供调用者读取
SysResult_t AppV3Business_GetDiagnostics(AppV3BusinessDiagnostics_t *diagnostics);

#endif
