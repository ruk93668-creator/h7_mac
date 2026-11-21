#include "app_services.h"
#include "app_status.h"
#include "../Tasks/service_tasks.h"
#include <stddef.h>

SysResult_t AppV3_CameraInit(const AppV3Context_t *context);
SysResult_t AppV3_UsbInit(const AppV3Context_t *context);
SysResult_t AppV3_StorageInit(const AppV3Context_t *context);

typedef struct
{
    AppV3ThreadSpec_t thread;
    SysResult_t (*init)(const AppV3Context_t *context);
} AppV3ServiceRecord_t;

#define STACK_WORDS(stack) (sizeof(stack) / sizeof((stack)[0]))

static StackType_t s_camera_stack[1536U] __attribute__((aligned(8)));
static StackType_t s_usb_stack[2048U] __attribute__((aligned(8)));
static StackType_t s_storage_stack[2048U] __attribute__((aligned(8)));

//服务清单是任务身份、启动入口、存储和配置的唯一来源。
static const AppV3ServiceRecord_t s_services[] =
{
    {{RUNTIME_TARGET_CAMERA, "Camera", AppV3_ServiceRun, s_camera_stack, STACK_WORDS(s_camera_stack), 3U,
       RUNTIME_EVENT_BOOT_READY}, AppV3_CameraInit},
    {{RUNTIME_TARGET_USB, "Usb", AppV3_ServiceRun, s_usb_stack, STACK_WORDS(s_usb_stack), 4U,
       RUNTIME_EVENT_BOOT_READY}, AppV3_UsbInit},
    {{RUNTIME_TARGET_STORAGE, "Storage", AppV3_ServiceRun, s_storage_stack, STACK_WORDS(s_storage_stack), 1U,
       RUNTIME_EVENT_BOOT_READY}, AppV3_StorageInit}
};

SysResult_t AppV3_StartServices(uint32_t *created)
{
    size_t service_index;

    if (created == NULL)
        return SYS_ERR_INVALID_ARG;

    *created = 0U;
    //创建阶段无调度，任何部分失败都禁止后续 BOOT。
    for (service_index = 0U; service_index < sizeof(s_services) / sizeof(s_services[0]); ++service_index)
    {
        SysResult_t result = AppV3_CreateThread(&s_services[service_index].thread);
        if (result != SYS_OK)
            return result;
        ++*created;
    }
    return SYS_OK;
}

//Control 独占首个初始化错误，复用原 IPC 状态出口。
static AppControlStatus_t s_startup_status;

static const AppV3ServiceRecord_t *AppV3_FindRecord(RuntimeEventTarget_t owner)
{
    size_t service_index;

    for (service_index = 0U; service_index < sizeof(s_services) / sizeof(s_services[0]); ++service_index)
    {
        if (s_services[service_index].thread.owner == owner)
            return &s_services[service_index];
    }
    return NULL;
}

const RuntimeEventTarget_t *AppV3_FindService(RuntimeEventTarget_t owner)
{
    const AppV3ServiceRecord_t *service = AppV3_FindRecord(owner);

    //查找只公开只读任务身份，初始化回调留在私有记录中。
    return service == NULL ? NULL : &service->thread.owner;
}

SysResult_t AppV3_InitService(RuntimeEventTarget_t owner, const AppV3Context_t *context)
{
    const AppV3ServiceRecord_t *service = AppV3_FindRecord(owner);

    //查找服务不授予写权限，初始化只能在对应任务内执行。
    if (service == NULL || context == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(owner))
        return SYS_ERR_INVALID_STATE;

    return service->init(context);
}

SysResult_t AppV3_ReportInitialization(RuntimeEventTarget_t owner, SysResult_t result)
{
    AppMessage_t message = {.type = APP_MESSAGE_FAULT};

    //沿用原消息 ABI，零命令编号保留给 V3 启动报告。
    if (AppV3_FindService(owner) == NULL || result > SYS_OK)
        return SYS_ERR_INVALID_ARG;

    if (result == SYS_OK)
        message.type = APP_MESSAGE_ACK;
    message.source = owner;
    message.result = result;
    return App_MessagesSend(APP_CHANNEL_CONTROL, &message);
}

SysResult_t AppV3_DrainInitialization(void)
{
    AppMessage_t message;
    bool accepted = false;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;

    //每个服务只提交一份启动报告，通知仅用于唤醒。
    while (App_MessagesReceive(APP_CHANNEL_CONTROL, &message))
    {
        if (message.command_id != 0U || AppV3_FindService(message.source) == NULL)
            continue;
        if (!((message.type == APP_MESSAGE_ACK && message.result == SYS_OK) ||
              (message.type == APP_MESSAGE_FAULT && message.result < SYS_OK)))
            continue;

        //后续成功或第二个错误不能覆盖最早观察到的错误。
        if (s_startup_status.last_fault == SYS_OK)
            s_startup_status.last_fault = message.result;
        accepted = true;
    }

    //配置准备只报告结果，不能写成视频正在运行。
    if (!accepted)
        return SYS_OK;
    s_startup_status.result = s_startup_status.last_fault;
    return App_StatusPublishControl(&s_startup_status);
}
