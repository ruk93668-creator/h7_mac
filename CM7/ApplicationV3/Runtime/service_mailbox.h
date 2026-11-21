#ifndef CM7_V3_SERVICE_MAILBOX_H
#define CM7_V3_SERVICE_MAILBOX_H

#include "service_event_loop.h"
#include "../Core/app_messages.h"

#define APP_V3_SERVICE_DEPTH 4U
#define APP_V3_SERVICE_RESULT_TAG 0x56334231UL

typedef struct
{
    uint32_t command_id;
    uint32_t operation;
    uint32_t argument;
    RuntimeEventTarget_t requester;
} AppV3ServiceCommand_t;

typedef struct
{
    AppV3EventLoop_t loop;
    AppV3Queue_t queue;
    AppMessage_t pending;
    uint32_t last_command[RUNTIME_TARGET_COUNT];
    uint32_t sequence;
    RuntimeEventTarget_t owner;
    bool pending_valid;
} AppV3ServiceMailbox_t;

SysResult_t AppV3Mailbox_Init(AppV3ServiceMailbox_t *mailbox, RuntimeEventTarget_t owner,
    AppV3QueueReadable_t readable, AppV3Queue_t *endpoint);
void AppV3Mailbox_Discard(AppV3ServiceMailbox_t *mailbox, AppV3Queue_t *endpoint);
SysResult_t AppV3Mailbox_Request(RuntimeEventTarget_t owner, const AppV3Queue_t *endpoint,
    uint32_t command_id, uint32_t operation, uint32_t argument);
SysResult_t AppV3Mailbox_Flush(AppV3ServiceMailbox_t *mailbox);
SysResult_t AppV3Mailbox_Receive(AppV3ServiceMailbox_t *mailbox, AppV3ServiceCommand_t *command);
SysResult_t AppV3Mailbox_Complete(AppV3ServiceMailbox_t *mailbox,
    const AppV3ServiceCommand_t *command, SysResult_t result, bool simulated_running, uint32_t parameter);

#endif
