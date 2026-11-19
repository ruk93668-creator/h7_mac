// 描述 V1 板的模块、媒体连接和默认配置。

#include "v2_board_v1.h"
#include "../Modules/Camera/camera_module.h"
#include "../Modules/USB/usb_module.h"
#include "../Modules/Storage/storage_module.h"
#include "app_storage.h"
#include "camera_mjpeg_fifo.h"

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define PIXEL_BIT(pixel) (1U << ((pixel) - 1U))
#define BACKEND_BIT(backend) (1U << ((backend) - 1U))

enum
{
    CAP_CAPTURE = 1,
    CAP_UVC,
    CAP_RECORDING
};

enum
{
    ENTITY_SENSOR = 1,
    ENTITY_CAPTURE,
    ENTITY_RAW,
    ENTITY_JPEG,
    ENTITY_USB,
    ENTITY_STORAGE
};

enum
{
    PIPE_RAW_USB = 1,
    PIPE_JPEG_USB,
    PIPE_JPEG_STORAGE
};

// 速率分别用于采集预算、USB 交付和 Storage 采样，均不代表实测速率。
enum
{
    PROFILE_JPEG_BYTES = 40000U,
    PROFILE_RAW_BYTES = 160U * 120U * 2U,
    RAW_CAPTURE_TARGET = 10U,
    JPEG_CAPTURE_TARGET = 25U,
    USB_JPEG_RATE = 20U,
    STORAGE_SAMPLE_RATE = 5U
};

static const V2Provided s_capture =
{
    {CAP_CAPTURE},
    1U,
    0U
};
static const V2Provided s_uvc =
{
    {CAP_UVC},
    1U,
    0U
};
static const V2Provided s_recording =
{
    {CAP_RECORDING},
    1U,
    0U
};
static const V2Requirement s_requires_capture =
{
    {CAP_CAPTURE},
    1U,
    0U,
    1U,
    0U,
    {0U},
    0U
};

// USB 和 Storage 都依赖采集能力。Resolver 据此决定启动顺序。
static const V2ModuleDesc s_modules[] =
{
    {
        .id = {V2_BOARD_USB},
        .major = 1U,
        .provided = &s_uvc,
        .provided_count = 1U,
        .required = &s_requires_capture,
        .required_count = 1U,
        .lifecycle_ops = &VUSB_LifecycleOps
    },
    {
        .id = {V2_BOARD_STORAGE},
        .major = 1U,
        .provided = &s_recording,
        .provided_count = 1U,
        .required = &s_requires_capture,
        .required_count = 1U,
        .lifecycle_ops = &VStorage_LifecycleOps
    },
    {
        .id = {V2_BOARD_CAMERA},
        .major = 1U,
        .provided = &s_capture,
        .provided_count = 1U,
        .lifecycle_ops = &VCamera_LifecycleOps
    }
};
static const V2Registry s_registry =
{
    s_modules,
    ARRAY_COUNT(s_modules),
    0U
};

// 委托传感器和 Capture 的只读检查，保留底层否决权。
static SysResult_t validate_capture(const GraphEntityDesc *entity,
                                    const GraphFormat *format,
                                    const GraphCheckContext *context,
                                    GraphIssue *issue)
{
    CameraConfig_t camera_config;
    (void)entity;
    (void)context;
    (void)issue;
    return VCamera_Translate(format, &camera_config);
}

// 使用选定的 USB 交付速率核对 UVC 能力。
static SysResult_t validate_usb(const GraphEntityDesc *entity,
                                const GraphFormat *format,
                                const GraphFrameRate *rate,
                                const GraphCheckContext *context,
                                GraphIssue *issue)
{
    VideoStreamConfig_t stream_config;
    (void)entity;
    (void)context;
    (void)issue;
    return VUSB_Translate(format, rate, &stream_config);
}

// 先检查采样配置与板级容量，验证过程中不挂载卷或创建文件。
static SysResult_t validate_storage(const GraphEntityDesc *entity,
                                    const GraphFormat *format,
                                    const GraphFrameRate *rate,
                                    const GraphCheckContext *context,
                                    GraphIssue *issue)
{
    const VStorageCaps caps = {APP_STORAGE_FRAME_LIMIT, JPEG_CAPTURE_TARGET};
    VStorageConfig config =
    {
        .mode = VST_AVI,
        .partition = 3U,
        .file_limit = 4UL * 1024UL * 1024UL,
        .sink_entity = ENTITY_STORAGE,
        .path = "0:/REC.AVI"
    };
    uint32_t sampling_interval_ms;
    (void)entity;
    (void)context;
    (void)issue;
    config.sampling = *rate;
    return VStorage_ValidateFormat(format, &config, &caps, &sampling_interval_ms);
}

static const GraphEntityOps s_capture_ops =
{
    validate_capture,
    NULL
};
static const GraphEntityOps s_usb_ops =
{
    NULL,
    validate_usb
};
static const GraphEntityOps s_storage_ops =
{
    NULL,
    validate_storage
};

// Raw 和 JPEG 使用同一个互斥组。
static const GraphEntityDesc s_entities[] =
{
    {
        {ENTITY_SENSOR},
        GRAPH_ENTITY_HW,
        "OV5640",
        0U,
        1U,
        0U,
        GRAPH_E_IMPLEMENTED,
        &s_capture_ops
    },
    {
        {ENTITY_CAPTURE},
        GRAPH_ENTITY_CAPTURE,
        "CameraCapture",
        1U,
        2U,
        0U,
        GRAPH_E_IMPLEMENTED,
        &s_capture_ops
    },
    {
        {ENTITY_RAW},
        GRAPH_ENTITY_PROVIDER,
        "CameraStream",
        3U,
        2U,
        1U,
        GRAPH_E_IMPLEMENTED,
        &s_capture_ops
    },
    {
        {ENTITY_JPEG},
        GRAPH_ENTITY_PROVIDER,
        "CameraMjpegFifo",
        5U,
        2U,
        1U,
        GRAPH_E_IMPLEMENTED,
        &s_capture_ops
    },
    {
        {ENTITY_USB},
        GRAPH_ENTITY_CONSUMER,
        "UVC",
        7U,
        1U,
        0U,
        GRAPH_E_IMPLEMENTED | GRAPH_E_TERMINAL,
        &s_usb_ops
    },
    {
        {ENTITY_STORAGE},
        GRAPH_ENTITY_CONSUMER,
        "Storage",
        8U,
        1U,
        0U,
        GRAPH_E_IMPLEMENTED | GRAPH_E_TERMINAL,
        &s_storage_ops
    }
};

enum
{
    CAPS_SHARED = 1,
    CAPS_RAW,
    CAPS_JPEG
};
#define BOARD_CAPS(pixels, backends, spans)                                    \
    {                                                                          \
        .media_mask = CORE_MEDIA_VIDEO,                                        \
        .pixel_mask = (pixels),                                                \
        .backend_mask = (backends),                                            \
        .width_min = 160U,                                                     \
        .width_max = 320U,                                                     \
        .height_min = 120U,                                                    \
        .height_max = 240U,                                                    \
        .width_step = 160U,                                                    \
        .height_step = 120U,                                                   \
        .max_payload = PROFILE_JPEG_BYTES,                                     \
        .max_spans = (spans),                                                  \
        .flags = GRAPH_CAP_COARSE | GRAPH_CAP_RATE_UNKNOWN,                    \
        .max_fps_den = 1U,                                                     \
        .dma_alignment = 4U,                                                   \
        .frame_alignment = 1U                                                  \
    }

// 这里描述拓扑允许的粗粒度范围，实际限制仍由对应的硬件验证器核对。
static const GraphCaps s_caps[] =
{
    BOARD_CAPS(PIXEL_BIT(CORE_PIXEL_YUY2) | PIXEL_BIT(CORE_PIXEL_JPEG),
               BACKEND_BIT(CORE_BACKEND_RAW) | BACKEND_BIT(CORE_BACKEND_JPEG), 2U),
    BOARD_CAPS(PIXEL_BIT(CORE_PIXEL_YUY2), BACKEND_BIT(CORE_BACKEND_RAW), 1U),
    BOARD_CAPS(PIXEL_BIT(CORE_PIXEL_JPEG), BACKEND_BIT(CORE_BACKEND_JPEG), 2U)
};

#define INPUT(parent_id, protocol_id, caps_id)                                 \
    {                                                                          \
        {0U},                                                                  \
        GRAPH_PAD_SINK,                                                        \
        {parent_id},                                                           \
        protocol_id,                                                           \
        {caps_id},                                                             \
        GRAPH_P_REQUIRED,                                                      \
        1U,                                                                    \
        0U                                                                     \
    }
#define OUTPUT(pad_id, parent_id, protocol_id, caps_id, degree, flags)         \
    {                                                                          \
        {pad_id},                                                              \
        GRAPH_PAD_SOURCE,                                                      \
        {parent_id},                                                           \
        protocol_id,                                                           \
        {caps_id},                                                             \
        flags,                                                                 \
        degree,                                                                \
        0U                                                                     \
    }

static const GraphPadDesc s_pads[] =
{
    OUTPUT(0U, ENTITY_SENSOR, GRAPH_PROTO_SENSOR_BYTES, CAPS_SHARED, 1U, 0U),
    INPUT(ENTITY_CAPTURE, GRAPH_PROTO_SENSOR_BYTES, CAPS_SHARED),
    OUTPUT(1U, ENTITY_CAPTURE, GRAPH_PROTO_DMA_CHUNKS, CAPS_SHARED, 1U, 0U),
    INPUT(ENTITY_RAW, GRAPH_PROTO_DMA_CHUNKS, CAPS_RAW),
    OUTPUT(1U, ENTITY_RAW, GRAPH_PROTO_FRAME_VIEW, CAPS_RAW, 1U, 0U),
    INPUT(ENTITY_JPEG, GRAPH_PROTO_DMA_CHUNKS, CAPS_JPEG),
    // JPEG 允许两路交付，Raw 输出限制为一路。
    OUTPUT(1U, ENTITY_JPEG, GRAPH_PROTO_FRAME_VIEW, CAPS_JPEG, 2U, GRAPH_P_FANOUT),
    INPUT(ENTITY_USB, GRAPH_PROTO_FRAME_VIEW, CAPS_SHARED),
    INPUT(ENTITY_STORAGE, GRAPH_PROTO_FRAME_VIEW, CAPS_JPEG)
};

#define LINK(id, source, pad, dest)                                            \
    {                                                                          \
        {id},                                                                  \
        GRAPH_L_AVAILABLE,                                                     \
        {{source}, {pad}},                                                     \
        {{dest}, {0U}},                                                        \
        {0U, 0U}                                                               \
    }
// 链路编号对应下方各 Pipeline 的选路表。
static const GraphLinkDesc s_links[] =
{
    LINK(1U, ENTITY_SENSOR, 0U, ENTITY_CAPTURE),
    LINK(2U, ENTITY_CAPTURE, 1U, ENTITY_RAW),
    LINK(3U, ENTITY_CAPTURE, 1U, ENTITY_JPEG),
    LINK(4U, ENTITY_RAW, 1U, ENTITY_USB),
    LINK(5U, ENTITY_JPEG, 1U, ENTITY_USB),
    LINK(6U, ENTITY_JPEG, 1U, ENTITY_STORAGE)
};

#define RAW_FORMAT                                                             \
    {                                                                          \
        CORE_MEDIA_VIDEO,                                                      \
        CORE_PIXEL_YUY2,                                                       \
        CORE_STREAM_RAW_FRAMES,                                                \
        160U,                                                                  \
        120U,                                                                  \
        320U,                                                                  \
        PROFILE_RAW_BYTES,                                                     \
        PROFILE_RAW_BYTES,                                                     \
        1U,                                                                    \
        1U,                                                                    \
        CORE_BACKEND_RAW,                                                      \
        32U,                                                                   \
        32U,                                                                   \
        0U                                                                     \
    }
#define JPEG_FORMAT                                                            \
    {                                                                          \
        CORE_MEDIA_VIDEO,                                                      \
        CORE_PIXEL_JPEG,                                                       \
        CORE_STREAM_JPEG_FRAMES,                                               \
        320U,                                                                  \
        240U,                                                                  \
        0U,                                                                    \
        PROFILE_JPEG_BYTES,                                                    \
        PROFILE_JPEG_BYTES,                                                    \
        1U,                                                                    \
        2U,                                                                    \
        CORE_BACKEND_JPEG,                                                     \
        32U,                                                                   \
        1U,                                                                    \
        0U                                                                     \
    }
static const GraphFormat s_raw_format = RAW_FORMAT;
static const GraphFormat s_jpeg_format = JPEG_FORMAT;
static const CoreFrameRate s_raw_rate = {RAW_CAPTURE_TARGET, 1U};
static const CoreFrameRate s_jpeg_usb_rate = {USB_JPEG_RATE, 1U};
static const CoreFrameRate s_storage_rate = {STORAGE_SAMPLE_RATE, 1U};

static const GraphPipelineDesc s_pipelines[] =
{
    {
        .id = {PIPE_RAW_USB},
        .name = "raw-usb",
        .source = {ENTITY_SENSOR},
        .backend = CORE_BACKEND_RAW,
        .sinks = {{ENTITY_USB}},
        .sink_count = 1U,
        .links = {{1U}, {2U}, {4U}},
        .link_count = 3U,
        .default_format = &s_raw_format,
        .default_sink_rates = &s_raw_rate
    },
    {
        .id = {PIPE_JPEG_USB},
        .name = "jpeg-usb",
        .source = {ENTITY_SENSOR},
        .backend = CORE_BACKEND_JPEG,
        .sinks = {{ENTITY_USB}},
        .sink_count = 1U,
        .links = {{1U}, {3U}, {5U}},
        .link_count = 3U,
        .default_format = &s_jpeg_format,
        .default_sink_rates = &s_jpeg_usb_rate
    },
    {
        .id = {PIPE_JPEG_STORAGE},
        .name = "jpeg-storage",
        .source = {ENTITY_SENSOR},
        .backend = CORE_BACKEND_JPEG,
        .sinks = {{ENTITY_STORAGE}},
        .sink_count = 1U,
        .links = {{1U}, {3U}, {6U}},
        .link_count = 3U,
        .default_format = &s_jpeg_format,
        .default_sink_rates = &s_storage_rate
    }
};
static const GraphCheckContext s_graph_context =
{
    1U,
    1U
};
static const GraphDesc s_graph =
{
    1U,
    s_entities,
    s_pads,
    s_links,
    s_pipelines,
    s_caps,
    ARRAY_COUNT(s_entities),
    ARRAY_COUNT(s_pads),
    ARRAY_COUNT(s_links),
    ARRAY_COUNT(s_pipelines),
    ARRAY_COUNT(s_caps),
    0U,
    &s_graph_context
};

static const V2EntityBinding s_bindings[] =
{
    {
        {ENTITY_SENSOR},
        0U,
        {V2_BOARD_CAMERA}
    },
    {
        {ENTITY_CAPTURE},
        0U,
        {V2_BOARD_CAMERA}
    },
    {
        {ENTITY_RAW},
        0U,
        {V2_BOARD_CAMERA}
    },
    {
        {ENTITY_JPEG},
        0U,
        {V2_BOARD_CAMERA}
    },
    {
        {ENTITY_USB},
        0U,
        {V2_BOARD_USB}
    },
    {
        {ENTITY_STORAGE},
        0U,
        {V2_BOARD_STORAGE}
    }
};
static const V2ModuleId s_usb_roots[] = {{V2_BOARD_USB}};
static const V2ModuleId s_storage_roots[] = {{V2_BOARD_STORAGE}};
static const V2ModuleId s_dual_roots[] = {{V2_BOARD_USB}, {V2_BOARD_STORAGE}};
static const GraphSelection s_raw_selection[] =
{
    {
        .pipeline = {PIPE_RAW_USB},
        .format = RAW_FORMAT,
        .sink_policies = {{{ENTITY_USB}, 0U, {RAW_CAPTURE_TARGET, 1U}}},
        .sink_policy_count = 1U
    }
};
static const GraphSelection s_jpeg_selection[] =
{
    {
        .pipeline = {PIPE_JPEG_USB},
        .format = JPEG_FORMAT,
        .sink_policies = {{{ENTITY_USB}, 0U, {USB_JPEG_RATE, 1U}}},
        .sink_policy_count = 1U
    },
    {
        .pipeline = {PIPE_JPEG_STORAGE},
        .format = JPEG_FORMAT,
        .sink_policies = {{{ENTITY_STORAGE}, 0U, {STORAGE_SAMPLE_RATE, 1U}}},
        .sink_policy_count = 1U
    }
};
// 将入口模块和媒体路线绑定成可选配置。
static const CSProfile s_profiles[] =
{
    {
        V2_BOARD_RAW_USB,
        {s_usb_roots, 1U, s_raw_selection, 1U}
    },
    {
        V2_BOARD_JPEG_USB,
        {s_usb_roots, 1U, s_jpeg_selection, 1U}
    },
    {
        V2_BOARD_JPEG_STORAGE,
        {s_storage_roots, 1U, s_jpeg_selection + 1, 1U}
    },
    {
        V2_BOARD_JPEG_USB_STORAGE,
        {s_dual_roots, 2U, s_jpeg_selection, 2U}
    }
};

// 读取采集目标，供消费者速率上限校验。
SysResult_t V2BoardV1_CaptureTarget(uint32_t backend,
                                    CoreFrameRate *target)
{
    if (!target)
    {
        return SYS_ERR_INVALID_ARG;
    }
    if (backend != CORE_BACKEND_RAW && backend != CORE_BACKEND_JPEG)
    {
        return SYS_ERR_NOT_SUPPORTED;
    }
    target->num = backend == CORE_BACKEND_RAW ? RAW_CAPTURE_TARGET : JPEG_CAPTURE_TARGET;
    target->den = 1U;
    return SYS_OK;
}

// 检查板级资源预算，实际帧交接由 Media 和对应任务核验。
static SysResult_t validate_resources(const GraphValidatedView *view,
                                      const V2Resolution *resolution,
                                      V2Issue *issue)
{
    CoreFrameRate capture_target;
    uint16_t sink_index;
    (void)issue;
    if (V2BoardV1_CaptureTarget(view->format.backend, &capture_target) ||
        !view->sink_count || resolution->count != view->sink_count + 1U)
    {
        return SYS_ERR_NOT_SUPPORTED;
    }
    if (view->format.backend == CORE_BACKEND_RAW && view->sink_count != 1U)
    {
        return SYS_ERR_NOT_SUPPORTED;
    }
    if (view->format.backend == CORE_BACKEND_JPEG &&
        (view->sink_count > 2U || !CAMERA_MJPEG_MAX_LOANS || !APP_STORAGE_SLOT_COUNT))
    {
        return SYS_ERR_NOT_SUPPORTED;
    }
    for (sink_index = 0U;
         sink_index < view->sink_count;
         ++sink_index)
    {
        const GraphSinkPolicy *sink = &view->sinks[sink_index];
        if ((sink->sink.value != ENTITY_USB && sink->sink.value != ENTITY_STORAGE) ||
            (view->format.backend == CORE_BACKEND_RAW && sink->sink.value != ENTITY_USB) ||
            (uint64_t)sink->rate.num * capture_target.den >
                (uint64_t)capture_target.num * sink->rate.den)
        {
            return SYS_ERR_NOT_SUPPORTED;
        }
    }
    return SYS_OK;
}

static const CSCatalog s_catalog =
{
    &s_registry,
    &s_graph,
    s_bindings,
    s_profiles,
    validate_resources,
    ARRAY_COUNT(s_bindings),
    ARRAY_COUNT(s_profiles)
};

// 返回可共享的只读目录，调用方不需要登记或初始化硬件。
const CSCatalog *V2BoardV1_Catalog(void)
{
    return &s_catalog;
}

// 按配置编号生成候选，由调用方独占工作区。
SysResult_t V2BoardV1_Compose(uint32_t profile_id,
                              V2CompositionScratch *scratch,
                              V2CompositionCandidate *candidate,
                              V2Issue *issue,
                              GraphIssue *graph_issue)
{
    uint16_t profile_index;
    for (profile_index = 0U;
         profile_index < s_catalog.profile_count;
         ++profile_index)
    {
        const CSProfile *profile_entry = &s_catalog.profiles[profile_index];
        if (profile_entry->id == profile_id)
        {
            return V2Composition_Validate(s_catalog.registry,
                                          s_catalog.graph,
                                          s_catalog.bindings,
                                          s_catalog.binding_count,
                                          &profile_entry->desired,
                                          s_catalog.validate,
                                          scratch,
                                          candidate,
                                          issue,
                                          graph_issue);
        }
    }
    return SYS_ERR_NOT_FOUND;
}
