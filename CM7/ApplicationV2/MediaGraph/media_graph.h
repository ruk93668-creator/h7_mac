// 校验静态媒体拓扑和候选路径的格式约束。

// Graph 只校验类型拓扑及候选。目录、Ops 和名称必须是可信的只读对象。
// 调用方清零独占 Scratch 并持有互不重叠的输出及诊断对象。同一 Scratch 重入返回 BUSY。
// 不同 Scratch 可分别读取同一不可变目录。验证回调不得访问硬件、取得资源、调度操作或重用 Scratch。
// 成功仅复制本地候选，跨任务发布由调用方负责。查询返回借用指针，目录不隐式注册。
#ifndef V2_MEDIA_GRAPH_H
#define V2_MEDIA_GRAPH_H
#include "../Core/media_format.h"
#define GRAPH_ENTITY_MAX 16U
#define GRAPH_PAD_MAX 32U
#define GRAPH_LINK_MAX 32U
#define GRAPH_PIPELINE_MAX 8U
#define GRAPH_CAPS_MAX 32U
#define GRAPH_SINK_MAX 5U
#define GRAPH_DEP_MAX 4U
typedef struct
{
    GraphEntityId entity;
    GraphPadId pad; /* entity内0-based，不是GPIO */
} GraphEndpoint;

enum
{
    GRAPH_PAD_SINK = 1,
    GRAPH_PAD_SOURCE = 2
};
#define GRAPH_MEDIA_VIDEO CORE_MEDIA_VIDEO
#define GRAPH_MEDIA_METADATA CORE_MEDIA_METADATA
enum
{
    GRAPH_PROTO_SENSOR_BYTES = 1, /* sensor外部视频字节流 */
    GRAPH_PROTO_DCMI_WORDS = 2,   /* 外设到DMA的逻辑word流 */
    GRAPH_PROTO_DMA_CHUNKS = 3,   /* DMA写入后端的chunk */
    GRAPH_PROTO_FRAME_VIEW = 4,   /* 完整帧的不可变视图 */
    GRAPH_PROTO_METADATA = 5      /* FUTURE；当前无实现 */
};
#define GRAPH_PIXEL_Y8 CORE_PIXEL_Y8
#define GRAPH_PIXEL_RGB565 CORE_PIXEL_RGB565
#define GRAPH_PIXEL_YUY2 CORE_PIXEL_YUY2
#define GRAPH_PIXEL_JPEG CORE_PIXEL_JPEG
#define GRAPH_BACKEND_RAW CORE_BACKEND_RAW
#define GRAPH_BACKEND_JPEG CORE_BACKEND_JPEG
#define GRAPH_STREAM_RAW_FRAMES CORE_STREAM_RAW_FRAMES
#define GRAPH_STREAM_JPEG_FRAMES CORE_STREAM_JPEG_FRAMES
enum
{
    GRAPH_ENTITY_HW = 1,
    GRAPH_ENTITY_CAPTURE = 2,
    GRAPH_ENTITY_TRANSFER = 3,
    GRAPH_ENTITY_PROVIDER = 4,
    GRAPH_ENTITY_PROCESSOR = 5,
    GRAPH_ENTITY_CONSUMER = 6
};
enum
{
    GRAPH_E_IMPLEMENTED = 1U,
    GRAPH_E_FUTURE = 2U,
    GRAPH_E_TERMINAL = 4U
};
enum
{
    GRAPH_P_FANOUT = 1U,
    GRAPH_P_REQUIRED = 2U
};
enum
{
    GRAPH_L_AVAILABLE = 1U,
    GRAPH_L_FUTURE = 2U
};
enum
{
    GRAPH_PIPE_TEMPLATE = 1U
};
enum
{
    GRAPH_CAP_RATE_UNKNOWN = 1U,
    GRAPH_CAP_COARSE = 2U
};

typedef CoreMediaFormat GraphFormat;

typedef CoreFrameRate GraphFrameRate;

typedef struct GraphSinkPolicy
{
    GraphEntityId sink;
    uint16_t reserved; /* 0 */
    GraphFrameRate rate;
} GraphSinkPolicy;

typedef struct GraphCaps
{
    uint32_t media_mask, pixel_mask, backend_mask;
    uint16_t width_min, width_max, height_min, height_max;
    uint16_t width_step, height_step;
    uint32_t max_payload; /* 0表示本Pad不再加整帧上限 */
    uint16_t max_spans, flags;
    uint32_t max_fps_num, max_fps_den;
    uint32_t dma_alignment, frame_alignment;
} GraphCaps;
typedef struct GraphEntityDesc GraphEntityDesc;
typedef struct GraphIssue GraphIssue;
typedef struct GraphCheckContext
{
    uint32_t schema_version;
    uint32_t product_revision;
} GraphCheckContext;

typedef struct GraphEntityOps
{
    SysResult_t (*validate_format)(const GraphEntityDesc *entity, const GraphFormat *format,
                                   const GraphCheckContext *context, GraphIssue *issue);
    SysResult_t (*validate_sink_config)(const GraphEntityDesc *entity, const GraphFormat *format,
                                        const GraphFrameRate *rate,
                                        const GraphCheckContext *context, GraphIssue *issue);
} GraphEntityOps;

struct GraphEntityDesc
{
    GraphEntityId id;
    uint16_t kind;
    const char *name;
    uint16_t pad_first, pad_count;
    uint16_t exclusion_group; /* 0无互斥组，Provider共享1 */
    uint16_t flags;
    const GraphEntityOps *ops; /* 只有纯校验，无Start/Stop */
};

typedef struct GraphPadDesc
{
    GraphPadId id;
    uint16_t direction;
    GraphEntityId parent;
    uint16_t protocol;
    GraphCapsId caps;
    uint16_t flags;
    uint16_t max_links; /* 当前配置选中边数上限 */
    uint16_t reserved;  /* 必须0 */
} GraphPadDesc;

typedef struct GraphLinkDesc
{
    GraphLinkId id;
    uint16_t flags; /* 可用性，不是运行状态 */
    GraphEndpoint source, sink;
    uint16_t reserved[2]; /* 必须0 */
} GraphLinkDesc;

typedef struct GraphPipelineDesc
{
    GraphPipelineId id;
    uint16_t flags;
    const char *name;
    GraphEntityId source;
    uint16_t backend; /* 此描述固定一种Backend */
    GraphEntityId sinks[GRAPH_SINK_MAX];
    uint16_t sink_count;
    GraphLinkId links[GRAPH_LINK_MAX];
    uint16_t link_count;
    GraphPipelineId dependencies[GRAPH_DEP_MAX];
    uint16_t dependency_count;
    const GraphFormat *default_format;
    const GraphFrameRate *default_sink_rates; /* 长度等于sink_count，同序 */
} GraphPipelineDesc;

typedef struct GraphDesc
{
    uint32_t schema_version;
    const GraphEntityDesc *entities;
    const GraphPadDesc *pads;
    const GraphLinkDesc *links;
    const GraphPipelineDesc *pipelines;
    const GraphCaps *caps;
    uint16_t entity_count, pad_count, link_count, pipeline_count;
    uint16_t caps_count, reserved;
    const GraphCheckContext *check_context;
} GraphDesc;
typedef struct GraphSelection
{
    GraphPipelineId pipeline;
    uint16_t reserved;  /* 0 */
    GraphFormat format; /* Core规范化的媒体布局 */
    GraphSinkPolicy sink_policies[GRAPH_SINK_MAX];
    uint16_t sink_policy_count, reserved_tail;
} GraphSelection;

struct GraphIssue
{
    uint32_t code;
    GraphEntityId entity;
    GraphPadId pad;
    GraphLinkId link;
    GraphPipelineId pipeline;
    uint32_t field;
    uint32_t expected, actual;
    SysResult_t backend_result; /* 保留真实纯验证器返回码 */
};

typedef struct GraphWorkspace
{
    uint8_t indegree[GRAPH_ENTITY_MAX];
    uint8_t ready[GRAPH_ENTITY_MAX];
    uint8_t forward[GRAPH_ENTITY_MAX];
    uint8_t reverse[GRAPH_ENTITY_MAX];
    uint8_t dependency_indegree[GRAPH_PIPELINE_MAX];
    uint8_t dependency_ready[GRAPH_PIPELINE_MAX];
    uint8_t entity_order[GRAPH_ENTITY_MAX];
    uint8_t reserved[16];
} GraphWorkspace;

typedef struct GraphValidatedView
{
    const GraphDesc *graph;
    uint32_t pipeline_mask, entity_mask, link_mask;
    GraphFormat format; /* R2仅支持一个物理Source */
    GraphEntityId topo[GRAPH_ENTITY_MAX];
    GraphSinkPolicy sinks[GRAPH_SINK_MAX];
    uint16_t topo_count, sink_count;
} GraphValidatedView;

typedef struct GraphScratch
{
    GraphWorkspace work;
    GraphValidatedView candidate;
} GraphScratch;

// 校验目录结构、端口方向、连接及 Pipeline 描述。
SysResult_t Graph_ValidateCatalog(const GraphDesc *graph, GraphWorkspace *work, GraphIssue *issue);
// 校验所选路径的合并、扇出和端口格式。
SysResult_t Graph_ValidateSet(const GraphDesc *graph, const GraphSelection *selections,
                              uint16_t count, GraphScratch *scratch, GraphValidatedView *out,
                              GraphIssue *issue);
// 按身份查询静态拓扑描述。
const GraphEntityDesc *Graph_GetEntity(const GraphDesc *, GraphEntityId);
// 按身份查询静态拓扑描述。
const GraphLinkDesc *Graph_GetLink(const GraphDesc *, GraphLinkId);
// 按身份查询静态拓扑描述。
const GraphPipelineDesc *Graph_GetPipeline(const GraphDesc *, GraphPipelineId);

enum
{
    GRAPH_ISSUE_NONE,
    GRAPH_ISSUE_BOUND,
    GRAPH_ISSUE_ID,
    GRAPH_ISSUE_DIRECTION,
    GRAPH_ISSUE_LINK,
    GRAPH_ISSUE_FORMAT,
    GRAPH_ISSUE_BACKEND,
    GRAPH_ISSUE_DEGREE,
    GRAPH_ISSUE_PATH,
    GRAPH_ISSUE_CYCLE,
    GRAPH_ISSUE_FUTURE,
    GRAPH_ISSUE_POLICY,
    GRAPH_ISSUE_ADAPTER,
    GRAPH_ISSUE_ALIAS,
    GRAPH_ISSUE_DEPENDENCY
};
#endif
