// 在执行端提交、推进和确认控制操作。

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
// 提交带完整身份的异步执行请求。
SysResult_t CSExec_Submit(CSExecutionEndpoint *, const CSExecution *);
// 在执行所有者上下文推进请求及结果运输。
SysResult_t CSExec_Process(CSExecutionEndpoint *, uint32_t now);
// 按指定执行身份读取已冻结结果。
SysResult_t CSExec_Read(CSExecutionEndpoint *, const CSIdentity *, CSExecResult *);
// 确认执行结果已消费，允许相应运输记录退休。
SysResult_t CSExec_Consume(CSExecutionEndpoint *, const CSExecResult *);
#endif
