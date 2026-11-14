#ifndef APP_CAMERA_CONSUMER_H
#define APP_CAMERA_CONSUMER_H
#include "app_camera_qualification.h"

//阶段只描述本方向事务进度，不能替代Camera资格表或帧租约账本
#define APP_CONSUMER_IDLE 0U
#define APP_CONSUMER_QUERY 1U
#define APP_CONSUMER_ACQUIRE 2U
#define APP_CONSUMER_GRANTED 3U
#define APP_CONSUMER_RELEASE 4U
#define APP_CONSUMER_STOP_IDLE 5U
#define APP_CONSUMER_FAILED 6U

//仅由所属USB或Storage任务使用，普通停止不得重新初始化
//request保留完整原文供ConfirmConsumed匹配，所有编号由原Tx分配
 typedef struct
{
    AppLifecycleTxContext_t tx;
    AppLifecycleMessage_t request;
    uint32_t next_generation;
    uint32_t generation;
    uint32_t session;
    uint32_t first_request;
    uint32_t phase;
    uint32_t granted;
    SysResult_t result;
    SysResult_t receive_result;
} AppCameraConsumer_t;

//只读诊断分别显示本方向进度与授权结果，不证明帧已归还
 typedef struct
{
    uint32_t phase;
    uint32_t session;
    uint32_t generation;
    uint32_t request_id;
    uint32_t granted;
    SysResult_t result;
    SysResult_t receive_result;
} AppCameraConsumerStatus_t;

//首次绑定原Tx方向，已初始化对象不能复位
SysResult_t App_CameraConsumerInit(AppCameraConsumer_t *consumer, uint32_t role, uint32_t id);
//预留本次QUERY、ACQUIRE、RELEASE、STOP编号，失败和取消不回退
SysResult_t App_CameraConsumerBegin(AppCameraConsumer_t *consumer);
//开始释放协商，不能根据本地停止直接清除granted
SysResult_t App_CameraConsumerEnd(AppCameraConsumer_t *consumer, SysResult_t business, SysResult_t cleanup);
//接收完整身份匹配回复，迟到消息不能覆盖当前事务
SysResult_t App_CameraConsumerReceive(AppCameraConsumer_t *consumer, const AppLifecycleMessage_t *message);
//消费固定回复后才建立合法后继，不执行任何硬件或文件操作
SysResult_t App_CameraConsumerProcess(AppCameraConsumer_t *consumer);
//由Task提供合法运输函数，队列满保留同一请求和编号
SysResult_t App_CameraConsumerFlush(AppCameraConsumer_t *consumer, AppLifecycleSendFn send, void *context);
//复制诊断结果，不消费协议记录或采样硬件
void App_CameraConsumerGetStatus(const AppCameraConsumer_t *consumer, AppCameraConsumerStatus_t *status);
#endif
