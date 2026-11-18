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
SysResult_t CSExecClient_Submit(CSExecClient *, const CSExecution *);
SysResult_t CSExecClient_Read(CSExecClient *, const CSIdentity *, CSExecResult *);
SysResult_t CSExecClient_Consume(CSExecClient *, const CSExecResult *);
SysResult_t CSExecServer_Init(CSExecServer *, CSExecutionEndpoint *, const CSExecLinkOps *, void *);
SysResult_t CSExecServer_Process(CSExecServer *, uint32_t now);
#endif
