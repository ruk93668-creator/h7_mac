#ifndef V2_CONTROL_EXECUTION_H
#define V2_CONTROL_EXECUTION_H

#include "control_service.h"

typedef struct
{
    uint32_t magic, busy, boot, highest, outstanding, ready;
    CSExecution accepted;
    CSExecResult result;
    EMContext *manager;
    int (*owner)(void *);
    SysResult_t (*snapshot)(void *, CSSnapshot *);
    void *port;
} CSExecutionEndpoint;

/* 接收侧只在Execution owner调用。不同任务时，发送端用复制消息交付整笔工作。 */
SysResult_t CSExec_Init(CSExecutionEndpoint *, uint32_t boot, EMContext *, int (*owner)(void *),
                        SysResult_t (*snapshot)(void *, CSSnapshot *), void *);
SysResult_t CSExec_Submit(CSExecutionEndpoint *, const CSExecution *);
SysResult_t CSExec_Process(CSExecutionEndpoint *, uint32_t now);
SysResult_t CSExec_Read(CSExecutionEndpoint *, const CSIdentity *, CSExecResult *);
SysResult_t CSExec_Consume(CSExecutionEndpoint *, const CSExecResult *);
#endif
