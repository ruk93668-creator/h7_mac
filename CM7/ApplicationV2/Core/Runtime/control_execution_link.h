// 在控制服务和执行任务之间传送请求、回复与消费确认。

#ifndef V2_CONTROL_EXECUTION_LINK_H
#define V2_CONTROL_EXECUTION_LINK_H

#include "control_execution.h"

typedef struct
{
    int (*control_owner)(void *);
    int (*execution_owner)(void *);
    SysResult_t (*send_work)(void *, const CSExecution *);
    SysResult_t (*receive_work)(void *, CSExecution *);
    SysResult_t (*send_result)(void *, const CSExecResult *);
    SysResult_t (*receive_result)(void *, CSExecResult *);
    SysResult_t (*send_ack)(void *, const CSExecResult *);
    SysResult_t (*receive_ack)(void *, CSExecResult *);
} CSExecLinkOps;

typedef struct
{
    uint32_t magic, busy, active, cached;
    CSIdentity id;
    CSExecResult result;
    const CSExecLinkOps *ops;
    void *port;
} CSExecClient;

typedef struct
{
    uint32_t magic, busy, phase, highest;
    CSExecution work;
    CSExecResult result;
    CSExecutionEndpoint *endpoint;
    const CSExecLinkOps *ops;
    void *port;
} CSExecServer;

/* 独立owner对象，通过值队列交换；发送返回非OK必须没有入队副作用。 */
SysResult_t CSExecClient_Init(CSExecClient *, const CSExecLinkOps *, void *);
// 提交带完整身份的异步执行请求。
SysResult_t CSExecClient_Submit(CSExecClient *, const CSExecution *);
// 按指定执行身份读取已冻结结果。
SysResult_t CSExecClient_Read(CSExecClient *, const CSIdentity *, CSExecResult *);
// 确认执行结果已消费，允许相应运输记录退休。
SysResult_t CSExecClient_Consume(CSExecClient *, const CSExecResult *);
// 初始化对应执行端点并绑定其所有者和端口。
SysResult_t CSExecServer_Init(CSExecServer *, CSExecutionEndpoint *, const CSExecLinkOps *, void *);
// 在执行所有者上下文推进请求及结果运输。
SysResult_t CSExecServer_Process(CSExecServer *, uint32_t now);
#endif
