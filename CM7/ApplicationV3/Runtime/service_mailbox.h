#ifndef CM7_V3_SERVICE_MAILBOX_H
#define CM7_V3_SERVICE_MAILBOX_H

#include "service_event_loop.h"
#include "service_events.h"

//每个服务邮箱固定的命令槽数量
#define APP_V3_SERVICE_DEPTH 4U
//无硬件快照的通用终结消息标签
#define APP_V3_SERVICE_RESULT_TAG 0x56334231UL
//带实际硬件快照的终结消息标签
#define APP_V3_HARDWARE_RESULT_TAG 0x56334331UL

//值类型业务命令，队列内不保存调用者地址
typedef struct
{
    //发起者分配的命令编号，用于识别匹配结果
    uint32_t command_id;
    //服务或协调器正在处理的操作编号
    uint32_t operation;
    //入队时按值保存的命令参数
    uint32_t argument;
    //发起业务请求的任务，用于区分重复命令号
    RuntimeEventTarget_t requester;
} AppV3ServiceCommand_t;

//保存正在推进的单个命令；硬件状态仍由各Service拥有
typedef struct
{
    //等待硬件终结的原始请求，保留发起者和编号
    AppV3ServiceCommand_t command;
    //当前操作开始时间，单位毫秒
    uint32_t started_ms;
    //存在待处理的请求或队列背压下暂存的终结结果
    bool pending;
} AppV3HardwareRequest_t;

//单个服务独占的异步命令与终结邮箱
typedef struct
{
    //服务执行命令订阅的事件循环
    AppV3EventLoop_t loop;
    //绑定当前对象的内核或应用队列句柄
    AppV3Queue_t queue;
    //存在待处理的请求或队列背压下暂存的终结结果
    AppMessage_t pending;
    //各发起者已受理的最高命令序号
    uint32_t last_command[RUNTIME_TARGET_COUNT];
    //所属通道的终结序号，用于检查发布顺序
    uint32_t sequence;
    //唯一拥有操作权限的任务身份
    RuntimeEventTarget_t owner;
    //终结结果暂存有效，入队前不能覆盖
    bool pending_valid;
} AppV3ServiceMailbox_t;

//在所属任务内分配命令队列并登记可读回调
SysResult_t AppV3Mailbox_Init(AppV3ServiceMailbox_t *mailbox, RuntimeEventTarget_t owner,
    AppV3QueueReadable_t readable, AppV3Queue_t *endpoint);
//清理尚未公开的邮箱及订阅
void AppV3Mailbox_Discard(AppV3ServiceMailbox_t *mailbox, AppV3Queue_t *endpoint);
//代登记任务校验并提交命令，成功只表示入队
SysResult_t AppV3Mailbox_Request(RuntimeEventTarget_t owner, const AppV3Queue_t *endpoint,
    uint32_t command_id, uint32_t operation, uint32_t argument);
//重试提交已保存的终结结果，队满时保留原记录
SysResult_t AppV3Mailbox_Flush(AppV3ServiceMailbox_t *mailbox);
//由所属任务取出并校验一条命令
SysResult_t AppV3Mailbox_Receive(AppV3ServiceMailbox_t *mailbox, AppV3ServiceCommand_t *command);
//通过兼容的通用终结格式发布结果，背压时保留原身份
SysResult_t AppV3Mailbox_Complete(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, bool simulated_running, uint32_t parameter);

//使用硬件快照完成请求，并按原队列顺序发布终结
SysResult_t AppV3Mailbox_CompleteHardware(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, const AppV3HardwareReport_t *report);

#endif
