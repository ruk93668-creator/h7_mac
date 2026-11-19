// 本阶段只允许静态 Configure/Cleanup；全部原生资源端口关闭。
#include "v2_rtos_runtime.h"
#include "v2_rtos_transport.h"
#include "control_execution.h"
#include "../../Configuration/v2_board_v1.h"
#include "app_storage.h"
#include "camera_mjpeg_fifo.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define DIAG_SESSION 1U
#define DIAG_PROFILE V2_BOARD_JPEG_USB_STORAGE
#define DIAG_TIMEOUT_MS 15000U

static VCamera s_camera;
static VUSB s_usb;
static VStorage s_storage;
static VCameraEndpoint s_camera_endpoint;
static VUSBEndpoint s_usb_endpoint;
static VStorageEndpoint s_storage_endpoint;
static VCameraClient s_camera_client;
static VUSBClient s_usb_client;
static VStorageClient s_storage_client;
static EMEndpoint s_clients[3];
static EMContext s_manager;
static CSService s_control;
static CSExecutionEndpoint s_execution;
static V2CompositionCandidate s_candidate;
static CSSnapshot s_snapshot;
static CSIdentity s_transaction;
static bool s_published;
static bool s_attempted[RUNTIME_TARGET_COUNT];
static bool s_initialized[RUNTIME_TARGET_COUNT];
volatile V2RuntimeDebug g_v2_runtime_debug;

// 唯一受信调用者是 ControlTask 内建诊断。未绑定任何 IPC 或外部入口。
static const CSPeer s_local_diagnostic = {CS_USB, 1U, 1U};

static int camera_owner(void *port)
{
    (void)port;
    return V2Rtos_Owner(RUNTIME_TARGET_CAMERA);
}
static bool camera_source_owner(void *port)
{
    return camera_owner(port) != 0;
}
static int usb_owner(void *port)
{
    (void)port;
    return V2Rtos_Owner(RUNTIME_TARGET_USB);
}
static int storage_owner(void *port)
{
    (void)port;
    return V2Rtos_Owner(RUNTIME_TARGET_STORAGE);
}
static int control_owner(void *port)
{
    (void)port;
    return V2Rtos_Owner(RUNTIME_TARGET_CONTROL);
}

static void no_work(void *port)
{
    (void)port;
}

/* 每个关闭端口有正确的函数类型。调用失败不产生任何硬件或资源事实。 */
#define REFUSE_0(name) static SysResult_t name(void *port)                                        \
    { (void)port; return SYS_ERR_NOT_SUPPORTED; }
#define REFUSE_1(name, first_type) static SysResult_t name(void *port, first_type first)            \
    { (void)port; (void)first; return SYS_ERR_NOT_SUPPORTED; }
#define REFUSE_2(name, first_type, second_type)                                                     \
    static SysResult_t name(void *port, first_type first, second_type second)                       \
    { (void)port; (void)first; (void)second; return SYS_ERR_NOT_SUPPORTED; }
#define REFUSE_3(name, first_type, second_type, third_type)                                          \
    static SysResult_t name(void *port, first_type first, second_type second, third_type third)     \
    { (void)port; (void)first; (void)second; (void)third; return SYS_ERR_NOT_SUPPORTED; }

REFUSE_0(no_native)
REFUSE_1(no_lifecycle_read, AppLifecycleMessage_t *)
REFUSE_1(no_lifecycle_send, const AppLifecycleMessage_t *)
REFUSE_1(no_grant, const MRGrant *)
REFUSE_1(no_request_out, MRRequest *)
REFUSE_1(no_request, const MRRequest *)
REFUSE_1(no_usb_offer, VUSBDelivery *)
REFUSE_1(no_receipt_read, MRReceipt *)
REFUSE_1(no_receipt, const MRReceipt *)
REFUSE_1(no_storage_offer, VStorageOffer *)
REFUSE_1(no_mount, const VStorageConfig *)
REFUSE_1(no_release, const MRRelease *)
REFUSE_1(no_camera_consume, const VCameraResult *)
REFUSE_2(no_usb_start, uint32_t, const VideoStreamConfig_t *)
REFUSE_2(no_usb_frame, const MRRequest *, const VUSBFrameData *)
REFUSE_2(no_storage_slot, const MRGrant *, const MRSlot *)
REFUSE_2(no_storage_return, const MRRequest *, const MRMedia *)
REFUSE_2(no_file_open, const VStorageConfig *, const CoreMediaFormat *)
REFUSE_2(no_camera_request, const LCRequest *, const CameraConfig_t *)
REFUSE_2(no_camera_result, const V2ExecId *, VCameraResult *)
REFUSE_2(no_role, const MRGrant *, uint32_t *)
REFUSE_2(no_media, const MRGrant *, const MRMedia *)
REFUSE_3(no_media_validate, const MRGrant *, const MRMedia *, const MRView *)

static SysResult_t no_media_obtain(void *port, const MRGrant *grant, const MRHandoff *handoff,
                                  MRMedia *media, MRView *view)
{
    (void)port;
    (void)grant;
    (void)handoff;
    (void)media;
    (void)view;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t no_write(void *port, const VStorageConfig *config, const CoreMediaFormat *format,
                            const MRView *view, uint32_t timestamp)
{
    (void)port;
    (void)config;
    (void)format;
    (void)view;
    (void)timestamp;
    return SYS_ERR_NOT_SUPPORTED;
}

static SysResult_t diagnostic_camera_caps(void *port, MRSourceCaps *out)
{
    (void)port;
    *out = (MRSourceCaps){MR_JPEG, MR_CAP_USB | MR_CAP_SD, CAMERA_MJPEG_MAX_LOANS, 2U};
    return SYS_OK;
}
static SysResult_t diagnostic_layout(void *port, const CoreMediaFormat *format,
                                     const CameraConfig_t *native, const MRSourceCaps *caps)
{
    (void)port;
    (void)native;
    return format->backend == caps->backend ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}
static SysResult_t diagnostic_gate(void *port, const LCRequest *request, uint32_t stage)
{
    (void)port;
    // ACTIVATE 进入生命周期并冻结原始拒绝结果，故障由原 EM 补偿。
    if (request->id.operation == LC_ACTIVATE && stage == VC_GATE_PROVE)
        return SYS_OK;
    return request->id.operation == LC_CONFIGURE || request->id.operation == LC_CLEANUP
               ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}
static SysResult_t diagnostic_camera_facts(void *port, VCameraObservation *out)
{
    (void)port;
    // 仅描述未连接物理端口的诊断实例。不能作为 Legacy 或 RELEASE 的证明。
    *out = (VCameraObservation){VC_COLD_MASK, 0U, SYS_OK, SYS_OK, 0U};
    return SYS_OK;
}
static SysResult_t diagnostic_usb_facts(void *port, VUSBObservation *out)
{
    (void)port;
    *out = (VUSBObservation){VUSB_MODE_STOPPED | VUSB_READERS_QUIET | VUSB_BRIDGE_IDLE, SYS_OK};
    return SYS_OK;
}
static SysResult_t diagnostic_storage_facts(void *port, VStorageFacts *out)
{
    (void)port;
    *out = (VStorageFacts){VST_VOLUME_RELEASED | VST_IO_QUIET};
    return SYS_OK;
}

static const MRSourceOps s_camera_source =
{
    camera_source_owner, diagnostic_camera_caps, no_role, no_grant, no_media_obtain,
    no_media_validate, no_media, no_media, no_media, no_grant
};
static const VCameraResources s_camera_resources =
{
    .jpeg_source = {&s_camera_source, NULL}
};
static const VCameraOps s_camera_ops =
{
    camera_owner, diagnostic_layout, diagnostic_gate, no_camera_request, no_camera_result,
    no_camera_consume, diagnostic_camera_facts, no_release, no_work, no_work, no_work, no_work
};
static const VUSBOps s_usb_ops =
{
    usb_owner, no_usb_start, no_work, no_native, no_work, no_native, diagnostic_usb_facts
};
static const VUSBConsumerOps s_usb_consumer = {usb_owner, no_lifecycle_send, no_lifecycle_read};
static const VUSBFrameOps s_usb_frame =
{
    usb_owner, no_grant, no_request_out, no_request, no_request, no_request, no_usb_offer,
    no_usb_frame, no_usb_frame, no_usb_frame, no_receipt_read, no_receipt, no_receipt
};
static const VStorageConsumerOps s_storage_consumer = {storage_owner, no_lifecycle_send, no_lifecycle_read};
static const VStorageBackendOps s_storage_backend =
{
    no_mount, no_file_open, no_write, no_native, no_native, no_native, no_native, diagnostic_storage_facts
};
static const VStorageFrameOps s_storage_frame =
{
    no_storage_slot, no_storage_slot, no_storage_offer, no_storage_return,
    no_receipt_read, no_receipt, no_receipt
};
static const VStoragePorts s_storage_ports =
{
    &s_storage_backend, NULL, &s_storage_frame, NULL, &s_storage_consumer, NULL
};

static SysResult_t initialize_module(RuntimeEventTarget_t task)
{
    const CSCatalog *catalog = V2BoardV1_Catalog();
    uint16_t sink_index;
    SysResult_t result;

    if (task == RUNTIME_TARGET_CAMERA)
    {
        result = VCamera_Init(&s_camera, V2_BOARD_CAMERA, DIAG_SESSION, &s_camera_ops, NULL,
                              &s_camera_resources);
        if (!result)
            result = VCamera_BindCandidate(&s_camera, catalog->registry, &s_candidate);
        if (!result)
            result = VCameraEndpoint_Init(&s_camera_endpoint, &s_camera, &V2Rtos_Camera);
        return result;
    }

    for (sink_index = 0U; sink_index < s_candidate.media.sink_count; ++sink_index)
    {
        const GraphSinkPolicy *sink = &s_candidate.media.sinks[sink_index];
        const V2EntityBinding *binding = &catalog->bindings[sink->sink.value - 1U];
        if (task == RUNTIME_TARGET_USB && binding->module.value == V2_BOARD_USB)
        {
            result = VUSB_Init(&s_usb, V2_BOARD_USB, DIAG_SESSION, &s_usb_ops, NULL,
                              &s_usb_consumer, NULL, &s_usb_frame, NULL);
            if (!result)
                result = VUSB_BindCandidate(&s_usb, catalog->registry, &s_candidate,
                                            VU_MODE_UVC, &sink->rate);
            if (!result)
                result = VUSBEndpoint_Init(&s_usb_endpoint, &s_usb, &V2Rtos_Usb);
            return result;
        }
        if (task == RUNTIME_TARGET_STORAGE && binding->module.value == V2_BOARD_STORAGE)
        {
            const VStorageCaps caps = {APP_STORAGE_FRAME_LIMIT, 25U};
            VStorageConfig config = {.mode = VST_AVI, .partition = 3U,
                                      .file_limit = 4UL * 1024UL * 1024UL, .path = "0:/REC.AVI"};
            config.sink_entity = sink->sink.value;
            config.sampling = sink->rate;
            result = VStorage_Init(&s_storage, V2_BOARD_STORAGE, DIAG_SESSION, &caps, &s_storage_ports);
            if (!result)
                result = VStorage_BindCandidate(&s_storage, catalog->registry, &s_candidate, &config);
            if (!result)
                result = VStorageEndpoint_Init(&s_storage_endpoint, &s_storage, &V2Rtos_Storage);
            return result;
        }
    }
    return SYS_ERR_NOT_FOUND;
}

static SysResult_t submit(void *port, const LCRequest *request)
{
    const EMEndpoint *endpoint = port;
    if (request->id.operation != LC_CONFIGURE && request->id.operation != LC_CLEANUP &&
        !(request->id.operation == LC_ACTIVATE && endpoint->module == V2_BOARD_CAMERA))
        return SYS_ERR_NOT_SUPPORTED;
    return endpoint->submit(endpoint->port, request);
}
static SysResult_t read(void *port, const V2ExecId *id, LCCompletion *completion)
{
    const EMEndpoint *endpoint = port;
    return endpoint->read(endpoint->port, id, completion);
}
static SysResult_t consume(void *port, const LCCompletion *completion)
{
    const EMEndpoint *endpoint = port;
    const V2Registry *registry = V2BoardV1_Catalog()->registry;
    const V2ModuleDesc *module = V2Registry_Get(registry, (V2ModuleId){endpoint->module});
    SysResult_t result = endpoint->consume(endpoint->port, completion);
    if (!result)
    {
        // ControlTask 的计划快照只采信匹配并已提交消费确认的完成消息。
        size_t index = (size_t)(module - registry->modules);
        s_snapshot.states[index] = completion->fact;
        ++g_v2_runtime_debug.confirmed[index];
    }
    return result;
}

static SysResult_t snapshot(void *port, CSSnapshot *out)
{
    (void)port;
    *out = s_snapshot;
    return SYS_OK;
}
static SysResult_t authorize(void *port, const CSPeer *peer, uint32_t operation, uint32_t profile,
                             uint32_t module, uint32_t transition)
{
    (void)port;
    (void)module;
    (void)transition;
    return !memcmp(peer, &s_local_diagnostic, sizeof(*peer)) &&
               (!operation || (operation == CS_START && profile == DIAG_PROFILE))
               ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}
static SysResult_t submit_plan(void *port, const CSExecution *work)
{
    return CSExec_Submit(port, work);
}
static SysResult_t read_plan(void *port, const CSIdentity *id, CSExecResult *result)
{
    return CSExec_Read(port, id, result);
}
static SysResult_t consume_plan(void *port, const CSExecResult *result)
{
    return CSExec_Consume(port, result);
}
static const CSPortOps s_control_ops = {control_owner, authorize, snapshot, submit_plan, read_plan, consume_plan};

static SysResult_t initialize_control(void)
{
    const CSCatalog *catalog = V2BoardV1_Catalog();
    V2Issue issue;
    GraphIssue graph_issue;
    EMEndpoint endpoints[3];
    size_t index;
    SysResult_t result = V2BoardV1_Compose(DIAG_PROFILE, &s_control.scratch, &s_candidate,
                                          &issue, &graph_issue);
    if (!result)
        result = VCameraClient_Init(&s_camera_client, V2_BOARD_CAMERA, DIAG_SESSION, &V2Rtos_Camera);
    if (!result)
        result = VUSBClient_Init(&s_usb_client, V2_BOARD_USB, DIAG_SESSION, &V2Rtos_Usb);
    if (!result)
        result = VStorageClient_Init(&s_storage_client, V2_BOARD_STORAGE, DIAG_SESSION, &V2Rtos_Storage);
    if (!result)
        result = VCameraClient_Endpoint(&s_camera_client, &s_clients[0]);
    if (!result)
        result = VUSBClient_Endpoint(&s_usb_client, &s_clients[1]);
    if (!result)
        result = VStorageClient_Endpoint(&s_storage_client, &s_clients[2]);
    if (result)
        return result;
    s_snapshot.revision = 1U;
    s_snapshot.prepared_profile = DIAG_PROFILE;
    for (index = 0U; index < 3U; ++index)
    {
        s_snapshot.states[index] = LC_UNCONFIGURED;
        endpoints[index] = (EMEndpoint){s_clients[index].module, &s_clients[index], submit, read, consume};
    }
    result = EM_Init(&s_manager, DIAG_SESSION, endpoints, 3U, control_owner, NULL);
    if (!result)
        result = CSExec_Init(&s_execution, DIAG_SESSION, &s_manager, control_owner, snapshot, NULL);
    if (!result)
        result = CS_Init(&s_control, DIAG_SESSION, catalog, &s_control_ops, &s_execution);
    if (!result)
    {
        taskENTER_CRITICAL();
        s_published = true;  // 候选是本次 BOOT 的不可变值。
        taskEXIT_CRITICAL();
    }
    return result;
}

static void advance_control(uint32_t now)
{
    CSReply reply;
    SysResult_t result;
    if (g_v2_runtime_debug.final)
        return;
    if (!s_transaction.ticket)
    {
        CSRequest request = {.version = CS_VERSION, .boot = DIAG_SESSION, .sequence = 1U,
                             .operation = CS_START, .profile = DIAG_PROFILE,
                             .revision = 1U, .timeout = DIAG_TIMEOUT_MS};
        result = CS_Open(&s_control, &s_local_diagnostic, &request.session);
        if (!result)
            result = CS_Submit(&s_control, &s_local_diagnostic, &request, now, &reply);
        if (result)
        {
            g_v2_runtime_debug.first_error = result;
            g_v2_runtime_debug.final = 1U;
            return;
        }
        s_transaction = reply.id;
    }
    // 两个角色在同一 ControlTask 串行推进，直接调用已限定所有者的执行端口。
    (void)CSExec_Process(&s_execution, now);
    (void)CS_Process(&s_control, now);
    if (!CS_Query(&s_control, &s_local_diagnostic, &s_transaction, &reply) &&
        reply.phase == CS_FINAL && !CS_Confirm(&s_control, &s_local_diagnostic, &reply))
    {
        g_v2_runtime_debug.first_error = reply.first_error;
        g_v2_runtime_debug.recovery_error = reply.recovery_error;
        g_v2_runtime_debug.execution_state = reply.execution_state;
        g_v2_runtime_debug.final = 1U;
    }
}

void V2Runtime_Round(RuntimeEventTarget_t task, uint32_t events, uint32_t now)
{
    bool published;
    if (!V2Rtos_Enter(task, events))
        return;
    if (task != RUNTIME_TARGET_CONTROL && task != RUNTIME_TARGET_CAMERA &&
        task != RUNTIME_TARGET_USB && task != RUNTIME_TARGET_STORAGE)
        return;
    if (!s_attempted[task])
    {
        if (task != RUNTIME_TARGET_CONTROL)
        {
            taskENTER_CRITICAL();
            published = s_published;
            taskEXIT_CRITICAL();
            if (!published)
                return;
        }
        s_attempted[task] = true;
        s_initialized[task] = (task == RUNTIME_TARGET_CONTROL ? initialize_control()
                                                           : initialize_module(task)) == SYS_OK;
        if (task == RUNTIME_TARGET_CONTROL)
            g_v2_runtime_debug.ready = s_initialized[task];
    }
    if (!s_initialized[task])
        return;
    switch (task)
    {
    case RUNTIME_TARGET_CONTROL: advance_control(now); break;
    case RUNTIME_TARGET_CAMERA: (void)VCameraEndpoint_RunRound(&s_camera_endpoint, now); break;
    case RUNTIME_TARGET_USB: (void)VUSBEndpoint_RunRound(&s_usb_endpoint, now); break;
    case RUNTIME_TARGET_STORAGE: (void)VStorageEndpoint_RunRound(&s_storage_endpoint, now); break;
    default: break;
    }
}
