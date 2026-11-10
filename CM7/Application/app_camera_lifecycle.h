#ifndef CM7_APP_CAMERA_LIFECYCLE_H
#define CM7_APP_CAMERA_LIFECYCLE_H

#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define APP_LIFECYCLE_VERSION 1U
#define APP_LIFECYCLE_REQUEST 1U
#define APP_LIFECYCLE_REPLY 2U
#define APP_LIFECYCLE_CAMERA 1U
#define APP_LIFECYCLE_USB 2U
#define APP_LIFECYCLE_STORAGE 3U
#define APP_CONSUMER_USB 1U
#define APP_CONSUMER_STORAGE 2U

//生命周期操作与旧AppMessage帧消息编号属于不同空间
#define APP_LIFECYCLE_SESSION_QUERY 1U
#define APP_LIFECYCLE_ACQUIRE 2U
#define APP_LIFECYCLE_RELEASE 3U
#define APP_LIFECYCLE_STOP_IF_IDLE 4U
#define APP_LIFECYCLE_RELEASE_REQUEST 5U
#define APP_LIFECYCLE_FAULT_NOTICE 6U

//各完成条件独立表达，协议校验不替代实际资源核验
#define APP_LIFECYCLE_INPUT_CLOSED (1UL << 0)
#define APP_LIFECYCLE_WORK_QUIESCENT (1UL << 1)
#define APP_LIFECYCLE_FRAMES_DRAINED (1UL << 2)
#define APP_LIFECYCLE_FILE_FINISHED (1UL << 3)
#define APP_LIFECYCLE_FILE_CLOSED (1UL << 4)
#define APP_LIFECYCLE_VOLUME_RELEASED (1UL << 5)
#define APP_LIFECYCLE_QUALIFICATION_RELEASED (1UL << 6)
#define APP_LIFECYCLE_HARDWARE_STOPPED (1UL << 7)
#define APP_LIFECYCLE_ALL_FLAGS 0xFFUL
#define APP_LIFECYCLE_CONSUMER_FLAGS 0x3FUL

//只描述消费者事务身份，不复用stream_epoch或lease_id
typedef struct
{
    uint32_t session_id;//Camera分配的资源会话，查询请求可为零
    uint32_t request_id;//事务发起方分配，回复原样回显
    uint32_t consumer_id;//固定USB或Storage身份
    uint32_t consumer_generation;//消费者新的START尝试代次
} AppLifecycleIdentity_t;

//固定56B普通C消息，不包含枚举对象、指针或RTOS类型
typedef struct
{
    uint32_t version;//拒绝未知协议布局
    uint32_t kind;//请求或回复
    uint32_t operation;//本事务的操作，回复保持原值
    uint32_t source;//真实发送角色
    uint32_t target;//唯一接收角色
    AppLifecycleIdentity_t identity;
    uint32_t related_request_id;//自主请求为零，Shutdown释放关联Camera请求
    uint32_t completion_flags;//分开表达业务、帧、资格和硬件完成
    int32_t result;//本事务结果，不代表其他阶段已经完成
    int32_t business_result;//保留业务失败，不能被安全归还覆盖
    int32_t cleanup_result;//保留清理或卷所有权失败
} AppLifecycleMessage_t;

#define APP_LIFECYCLE_ASSERT(name, expression) typedef char name[(expression) ? 1 : -1]
APP_LIFECYCLE_ASSERT(AppLifecycleIdentitySize, sizeof(AppLifecycleIdentity_t) == 16U);
APP_LIFECYCLE_ASSERT(AppLifecycleMessageSize, sizeof(AppLifecycleMessage_t) == 56U);
APP_LIFECYCLE_ASSERT(AppLifecycleIdentityOffset, offsetof(AppLifecycleMessage_t, identity) == 20U);
APP_LIFECYCLE_ASSERT(AppLifecycleRelatedOffset, offsetof(AppLifecycleMessage_t, related_request_id) == 36U);
APP_LIFECYCLE_ASSERT(AppLifecycleFlagsOffset, offsetof(AppLifecycleMessage_t, completion_flags) == 40U);
APP_LIFECYCLE_ASSERT(AppLifecycleResultOffset, offsetof(AppLifecycleMessage_t, result) == 44U);
APP_LIFECYCLE_ASSERT(AppLifecycleBusinessOffset, offsetof(AppLifecycleMessage_t, business_result) == 48U);
APP_LIFECYCLE_ASSERT(AppLifecycleCleanupOffset, offsetof(AppLifecycleMessage_t, cleanup_result) == 52U);
#undef APP_LIFECYCLE_ASSERT

#define APP_LIFECYCLE_TX_EMPTY 0U
#define APP_LIFECYCLE_TX_READY 1U
#define APP_LIFECYCLE_TX_WAIT_REPLY 2U
#define APP_LIFECYCLE_TX_COMMITTED 3U
#define APP_LIFECYCLE_TX_COMPLETE 4U
#define APP_LIFECYCLE_TX_SENDING 5U

//由单个任务持有，未收到匹配回复时不得复用请求记录
typedef struct
{
    uint32_t phase;
    AppLifecycleMessage_t message;
    AppLifecycleMessage_t reply;
} AppLifecyclePending_t;

//保存一个请求的原文及终结回复，防止重试重复执行
typedef struct
{
    uint32_t occupied;
    uint32_t completed;
    AppLifecycleMessage_t request;
    AppLifecycleMessage_t reply;
} AppLifecycleReplay_t;

//传输回调只返回是否入队，不表示接收方已经执行
typedef SysResult_t (*AppLifecycleSendFn)(void *context, const AppLifecycleMessage_t *message);

//检查固定协议字段、方向与完成标志的声明权限
bool App_LifecycleValidate(const AppLifecycleMessage_t *message);

//匹配唯一待决请求及其回复，不接受其他会话或消费者代次
bool App_LifecycleMatchesReply(const AppLifecycleMessage_t *request, const AppLifecycleMessage_t *reply);

//按调用者提供的当前身份检查消息，不自行建立消费者状态
bool App_LifecycleMatchesCurrent(const AppLifecycleMessage_t *message, uint32_t session,
    uint32_t consumer, uint32_t generation);

//构造回复原文，业务方仍须提供真实结果和查询会话
SysResult_t App_LifecycleMakeReply(const AppLifecycleMessage_t *request, uint32_t session,
    SysResult_t result, AppLifecycleMessage_t *reply);

//由唯一所有者批量分配编号，便于为未来清理事务预留额度
SysResult_t App_LifecycleAllocateIds(uint32_t *sequence, uint32_t count, uint32_t *first);

//保存完整待发记录，拒绝覆盖尚未终结的事务
SysResult_t App_LifecycleStage(AppLifecyclePending_t *pending, const AppLifecycleMessage_t *message);

//只重试尚未入队的原文，成功入队后等待回复或显式确认消费
SysResult_t App_LifecycleFlush(AppLifecyclePending_t *pending, AppLifecycleSendFn send, void *context);

//接收匹配回复，超时或不匹配回复不能清除待决请求
SysResult_t App_LifecycleAcceptReply(AppLifecyclePending_t *pending, const AppLifecycleMessage_t *reply);

//只清理协议已终结记录，不执行业务停止或帧释放
SysResult_t App_LifecycleClear(AppLifecyclePending_t *pending);

//仅取消READY请求，其他阶段及待发回复均保持原记录
SysResult_t App_LifecycleCancelReady(AppLifecyclePending_t *pending);

//登记请求，同身份不同内容视为协议错误，重复原文不重复受理
SysResult_t App_LifecycleReplayBegin(AppLifecycleReplay_t *record, const AppLifecycleMessage_t *request);

//保存已由业务方完成的回复，协议层不执行任何业务
SysResult_t App_LifecycleReplayComplete(AppLifecycleReplay_t *record, const AppLifecycleMessage_t *reply);

//复制缓存回复，读取不会消耗记录
SysResult_t App_LifecycleReplayGetReply(const AppLifecycleReplay_t *record, AppLifecycleMessage_t *reply);

//没有安全失效边界时不允许退休，已完成记录返回NOT_SUPPORTED
SysResult_t App_LifecycleReplayRetire(AppLifecycleReplay_t *record);

#endif
