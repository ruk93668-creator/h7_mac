#include "app_services.h"
#include "app_status.h"
#include "../Runtime/service_record.h"
#include "../Tasks/service_tasks.h"
#include "platform_sync.h"
#include "../Services/Camera/camera_service.h"
#include "../Services/USB/usb_service.h"
#include "../Services/Storage/storage_service.h"
#include <stddef.h>

//静态服务登记记录，关联线程、启动入口和只读配置
typedef struct
{
    //登记服务入口和静态线程资源
    AppV3ThreadSpec_t thread;
    SysResult_t (*init)(const AppV3Context_t *context);
    void (*discard)(void);
    //记录服务就绪后可借用的实例
    AppV3ServiceInstance_t *instance;
} AppV3ServiceRecord_t;

//把静态栈数组长度换算成内核需要的字数
#define STACK_WORDS(stack) (sizeof(stack) / sizeof((stack)[0]))

//Camera任务独占栈，按八字节对齐
static StackType_t s_camera_stack[1536U] __attribute__((aligned(8)));
//USB任务独占栈，按八字节对齐
static StackType_t s_usb_stack[2048U] __attribute__((aligned(8)));
//Storage任务独占栈，按八字节对齐
static StackType_t s_storage_stack[2048U] __attribute__((aligned(8)));

//三个服务的静态实例，由各自初始化流程维护
static AppV3ServiceInstance_t s_camera, s_usb, s_storage;

//服务清单是任务身份、启动入口、存储和配置的唯一来源。
static const AppV3ServiceRecord_t s_services[] =
{
    {
        {
            RUNTIME_TARGET_CAMERA,
            "Camera",
            AppV3_CameraRun,
            s_camera_stack,
            STACK_WORDS(s_camera_stack),
            3U,
            RUNTIME_EVENT_BOOT_READY
        },
        AppV3_CameraInit,
        AppV3_CameraDiscard,
        &s_camera
    },

    {
        {
            RUNTIME_TARGET_USB,
            "Usb",
            AppV3_UsbRun,
            s_usb_stack,
            STACK_WORDS(s_usb_stack),
            4U,
            RUNTIME_EVENT_BOOT_READY
        },
        AppV3_UsbInit,
        AppV3_UsbDiscard,
        &s_usb
    },

    {
        {
            RUNTIME_TARGET_STORAGE,
            "Storage",
            AppV3_StorageRun,
            s_storage_stack,
            STACK_WORDS(s_storage_stack),
            1U,
            RUNTIME_EVENT_BOOT_READY
        },
        AppV3_StorageInit,
        AppV3_StorageDiscard,
        &s_storage
    }
};

//按照固定服务清单创建任务并记录实际创建数量
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
//Control已收齐的服务启动完成位
static uint32_t s_startup_completed;
//启动结果发布标记，发布后方可处理业务命令
static volatile bool s_startup_published;

//按任务身份查找编译期服务描述
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

//返回已登记的服务身份
const RuntimeEventTarget_t *AppV3_FindService(RuntimeEventTarget_t owner)
{
    const AppV3ServiceRecord_t *service = AppV3_FindRecord(owner);

    //查找只公开只读任务身份，初始化回调留在私有记录中。
    return service == NULL ? NULL : &service->thread.owner;
}

//在所属任务中准备服务并发布真实初始化结果
SysResult_t AppV3_InitService(RuntimeEventTarget_t owner, const AppV3Context_t *context)
{
    const AppV3ServiceRecord_t *service = AppV3_FindRecord(owner);
    SysResult_t result;

    //查找服务不授予写权限，初始化只能在对应任务内执行。
    if (service == NULL || context == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(owner))
        return SYS_ERR_INVALID_STATE;

    //重复调用不得写正在被其他任务读取的公开对象。
    if (AppV3Record_Query(owner, &result) != SYS_ERR_BUSY)
        return SYS_ERR_ALREADY_EXISTS;
    result = service->init(context);
    //将原初始化失败发布给等待者，并用同一结果生成启动报告
    if (result != SYS_OK)
        return AppV3Record_Fail(owner, result);

    //底层初始化完成以后才发布本次创建的只读服务对象。
    *service->instance = (AppV3ServiceInstance_t){owner, context};
    result = AppV3Record_Create(owner, service->instance);
    if (result != SYS_OK)
        service->discard();
    return result;
}

//将已发布的服务结果提交到专用启动通道
SysResult_t AppV3_ReportInitialization(RuntimeEventTarget_t owner, SysResult_t result)
{
    AppMessage_t message = {.type = APP_MESSAGE_FAULT};
    SysResult_t recorded;

    //沿用原消息 ABI，零命令编号保留给 V3 启动报告。
    if (AppV3_FindService(owner) == NULL || result > SYS_OK)
        return SYS_ERR_INVALID_ARG;

    //初始化返回值必须与该服务已经发布的事实一致。
    if (!App_MessagesIsOwner(owner) || AppV3Record_Query(owner, &recorded) != SYS_OK || recorded != result)
        return SYS_ERR_INVALID_STATE;
    if (result == SYS_OK)
        message.type = APP_MESSAGE_ACK;
    message.source = owner;
    message.result = result;
    return App_MessagesSend(APP_CHANNEL_STARTUP, &message);
}

//从服务清单计算整体启动所需的任务集合
static uint32_t AppV3_StartupRequired(void)
{
    size_t service_index;
    uint32_t required = 0U;

    //聚合只依据启动清单，不能另设需要维护的服务数量或身份表。
    for (service_index = 0U; service_index < sizeof(s_services) / sizeof(s_services[0]); ++service_index)
        required |= 1UL << s_services[service_index].thread.owner;
    return required;
}

//仅发布已满足条件的整体启动结果
static SysResult_t AppV3_PublishStartup(void)
{
    SysResult_t result;

    if (s_startup_published)
        return SYS_OK;
    if (s_startup_status.last_fault == SYS_OK && s_startup_completed != AppV3_StartupRequired())
        return SYS_OK;

    //成功需全部真实结果；失败保留首次错误并且只发布一次。
    s_startup_status.result = s_startup_status.last_fault;
    result = App_StatusPublishControl(&s_startup_status);
    if (result == SYS_OK)
    {
        //先写结果，再允许其他任务观察启动事实
        Platform_SyncMemoryBarrier();
        s_startup_published = true;
    }
    return result;
}

//由Control限量接收并核对启动报告
SysResult_t AppV3_DrainInitialization(void)
{
    AppMessage_t message;
    uint32_t received;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;

    //仅消费独立启动通道，每轮最多处理一队列容量。
    for (received = 0U; received < 4U && App_MessagesReceive(APP_CHANNEL_STARTUP, &message); ++received)
    {
        uint32_t owner_bit;
        SysResult_t recorded;

        //专用通道仍需校验身份与消息类型，拒绝非法启动报告
        if (message.command_id != 0U || AppV3_FindService(message.source) == NULL)
            continue;
        if (!((message.type == APP_MESSAGE_ACK && message.result == SYS_OK) ||
              (message.type == APP_MESSAGE_FAULT && message.result < SYS_OK)))
            continue;

        //按真实发布结果核验 ACK/FAULT；重复报告不能再次推进计数或首错。
        owner_bit = 1UL << message.source;
        if ((s_startup_completed & owner_bit) != 0U ||
            AppV3Record_Query(message.source, &recorded) != SYS_OK || recorded != message.result)
            continue;
        s_startup_completed |= owner_bit;
        if (s_startup_status.last_fault == SYS_OK)
            s_startup_status.last_fault = message.result;
    }

    return AppV3_PublishStartup();
}

//冻结启动超时结果并交由原状态接口发布
SysResult_t AppV3_ExpireInitialization(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;

    //已完成的启动保留其结果，截止后到达的成功不能覆盖超时。
    if (s_startup_completed != AppV3_StartupRequired() && s_startup_status.last_fault == SYS_OK)
        s_startup_status.last_fault = SYS_ERR_TIMEOUT;
    return AppV3_PublishStartup();
}

//只验证Record身份，避免其他同名发布借用旧队列入口
bool AppV3_IsServiceInstance(RuntimeEventTarget_t owner, const AppV3ServiceInstance_t *instance)
{
    const AppV3ServiceRecord_t *service = AppV3_FindRecord(owner);
    return service != NULL && instance == service->instance;
}

//Control发布全部服务就绪结果后才允许提交业务命令
bool AppV3_BusinessReady(void)
{
    bool published = s_startup_published;
    Platform_SyncMemoryBarrier();
    return published && s_startup_status.result == SYS_OK;
}
