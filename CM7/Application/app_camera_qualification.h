#ifndef CM7_APP_CAMERA_QUALIFICATION_H
#define CM7_APP_CAMERA_QUALIFICATION_H

#include "app_camera_lifecycle_replay.h"

#define APP_QUAL_INACTIVE 0U
#define APP_QUAL_ACQUIRING 1U
#define APP_QUAL_ACTIVE 2U
#define APP_QUAL_RELEASING 3U
#define APP_QUAL_NONE 0U
#define APP_QUAL_DECIDED 1U
#define APP_QUAL_BLOCKED 2U
#define APP_RESOURCE_UNSTARTED 0U
#define APP_RESOURCE_PREPARING 1U
#define APP_RESOURCE_RUNNING 2U
#define APP_RESOURCE_STOPPING 3U
#define APP_RESOURCE_STOPPED_CONFIRMED 4U
#define APP_RESOURCE_FAULT_DRAINING 5U

//只读请求结果与全局资源状态分开，complete为0表示仍在推进
typedef struct
{
    uint32_t complete;
    SysResult_t result;
    uint32_t hardware_stopped;
} AppQualificationStopResult_t;

//资源状态只读取App_Camera权威对象，归还证据由后续独立适配提供
//所有回调只能在Camera所有者上下文串行执行，不能重入本核心
//released/idle未绑定表示没有证据，不能默认排空成功
typedef struct
{
    uint32_t (*state)(void *context);
    SysResult_t (*start)(void *context);
    SysResult_t (*request_stop)(void *context, const AppLifecycleIdentity_t *identity);
    SysResult_t (*read_stop_result)(void *context, const AppLifecycleIdentity_t *identity,
        AppQualificationStopResult_t *result);
    SysResult_t (*consume_stop_result)(void *context, const AppLifecycleIdentity_t *identity);
    SysResult_t (*released)(void *context, const AppLifecycleIdentity_t *grant);
    SysResult_t (*idle)(void *context);
} AppQualificationResourceOps_t;

//只保存当前业务效果的冻结参数，原始请求和固定回复仍由Rx持有
typedef struct
{
    uint32_t request_id;//绑定当前方向的请求，不保存历史事务
    uint32_t reply_session;//QUERY分配后不能因提交失败再次分配
    uint32_t flags;//只记录已经核实的完成条件
    int32_t result;//冻结的资格操作结果
    int32_t business_result;//保留消费者原业务故障
    int32_t cleanup_result;//保留消费者原清理故障
    uint32_t phase;//NONE、DECIDED或BLOCKED
    int32_t submit_error;//记录提交异常，不覆盖实际业务事实
} AppQualificationDecision_t;

//每路资格只保存语义键，不复制帧引用计数或整份消息
typedef struct
{
    uint32_t phase;//资格阶段，退出等待仍保留grant
    uint32_t grant_session;//当前grant的会话
    uint32_t grant_generation;//不被失败的新申请代次覆盖
    uint32_t grant_request;//建立grant的ACQUIRE编号
    uint32_t highest_seen_generation;//已经消耗的申请代次
    uint32_t work_request;//首次Promote的请求编号
    uint32_t work_session;//提交复核使用原完整事务身份
    uint32_t work_generation;//与grant分离，允许拒绝更高代次
    uint32_t work_operation;//防止提交时窗口被换成其他操作
    uint32_t work_related;//保留关联释放请求身份
    AppQualificationDecision_t decision;
} AppQualificationEntry_t;

//由App_Camera私有持有，不保存第二份硬件运行状态
//首次使用前清零，禁止普通停止或会话切换时重新Init
typedef struct
{
    AppQualificationEntry_t entries[2];//USB、Storage独立资格及提交记录
    AppLifecycleRxContext_t rx[2];//仅消费者到Camera的两个现有接收窗口
    uint32_t session_id;//资源周期的单调会话编号
    uint32_t session_closed;//旧周期完成后的惰性换代门控
    uint32_t initialized;//一次初始化标记
    uint32_t api_busy;//阻止资源回调重入资格核心
} AppCameraQualification_t;

//只读视图不推进业务，也不作为真实帧归还证据
typedef struct
{
    uint32_t phase;
    uint32_t session_id;
    uint32_t grant_generation;
    uint32_t highest_seen_generation;
    uint32_t decision_phase;
    SysResult_t submit_error;
} AppQualificationStatus_t;

//给未来消费者的新尝试分配代次，失败和取消均不得回退计数
SysResult_t App_QualificationNextGeneration(uint32_t *counter, uint32_t *generation);
//绑定两个固定接收方向，不能重置已运行的编号或会话
SysResult_t App_QualificationInit(AppCameraQualification_t *core);
//仅运输到现有Rx，重复和候选都不在此执行业务
SysResult_t App_QualificationReceive(AppCameraQualification_t *core,
    const AppLifecycleMessage_t *request, uint32_t *disposition);
//每轮每方向最多推进一笔，等待证据时仍返回任务调度边界
SysResult_t App_QualificationProcess(AppCameraQualification_t *core,
    const AppQualificationResourceOps_t *ops, void *context);
//由所属任务提供原消息发送函数，队列背压只重试原回复
SysResult_t App_QualificationFlush(AppCameraQualification_t *core, uint32_t consumer,
    AppLifecycleSendFn send, void *context);
//读取资格语义，不调用资源回调、运输或归还
SysResult_t App_QualificationGetStatus(const AppCameraQualification_t *core,
    uint32_t consumer, AppQualificationStatus_t *status);

#define APP_QUAL_ASSERT(name, expression) typedef char name[(expression) ? 1 : -1]
APP_QUAL_ASSERT(AppQual_DecisionSize, sizeof(AppQualificationDecision_t) == 32U);
APP_QUAL_ASSERT(AppQual_EntrySize, sizeof(AppQualificationEntry_t) == 72U);
APP_QUAL_ASSERT(AppQual_CoreSize, sizeof(AppCameraQualification_t) == 824U);
APP_QUAL_ASSERT(AppQual_RxOffset, offsetof(AppCameraQualification_t, rx) == 144U);
#undef APP_QUAL_ASSERT
#endif
