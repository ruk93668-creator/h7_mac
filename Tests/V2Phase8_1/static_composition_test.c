#include "../../CM7/ApplicationV2/Configuration/v2_board_v1.h"
#include "app_messages.h"
#include "../LegacyLifecycle/Source/app_camera_qualification_internal.h"
#include "platform_sync.h"
#include <stddef.h>
#include <string.h>

volatile uint32_t g_failure;
volatile uint32_t g_sizes[8];

#define CHECK(condition) do { if (!(condition)) { g_failure = __LINE__; return g_failure; } } while (0)

static V2CompositionScratch s_scratch;
static V2CompositionCandidate s_candidate, s_saved, s_applied;

extern void Native_UnboundTrap(void);

// 静态装配不得触碰任务或硬件入口。误调用时测试立即失败。
SysResult_t App_CameraQualificationAccess(AppCameraQualification_t **core,
    const AppQualificationResourceOps_t **ops, bool preparing)
{
    (void)core;
    (void)ops;
    (void)preparing;
    Native_UnboundTrap();
    return SYS_ERR_INVALID_STATE;
}

bool App_MessagesIsOwner(RuntimeEventTarget_t target)
{
    (void)target;
    Native_UnboundTrap();
    return false;
}

uint32_t Platform_SyncGetException(void)
{
    Native_UnboundTrap();
    return 0U;
}
static V2Issue s_issue;
static GraphIssue s_graph_issue;

uint32_t Board_Run(uint32_t scenario)
{
    const CSCatalog *catalog = V2BoardV1_Catalog();
    const V2Registry *registry = catalog->registry;
    const V2Desired *dual = &catalog->profiles[V2_BOARD_JPEG_USB_STORAGE - 1U].desired;
    CoreFrameRate capture = {0U, 0U};
    GraphSelection selections[2];
    V2Desired desired = *dual;
    GraphDesc graph = *catalog->graph;
    V2Registry partial = *registry;
    V2ModuleDesc descriptors[3];
    GraphLinkDesc links[6];
    SysResult_t result;
    g_failure = 0U;
    g_sizes[0] = sizeof(AppMessage_t);
    g_sizes[1] = sizeof(AppLifecycleMessage_t);
    g_sizes[2] = sizeof(V2CompositionScratch);
    g_sizes[3] = sizeof(V2CompositionCandidate);
    g_sizes[4] = sizeof(V2Issue);
    g_sizes[5] = sizeof(GraphIssue);
    g_sizes[6] = sizeof(CSCatalog);
    g_sizes[7] = sizeof(CSProfile);
    memset(&s_scratch, 0, sizeof(s_scratch));
    memset(&s_candidate, 0xA5, sizeof(s_candidate));
    s_saved = s_candidate;
    if (scenario < 4U)
    {
        CHECK(V2BoardV1_Compose(scenario + 1U, &s_scratch, &s_candidate, &s_issue, &s_graph_issue) == SYS_OK);
        CHECK(s_candidate.candidate_only == 1U);
        CHECK(s_candidate.modules.count == (scenario == 3U ? 3U : 2U));
        CHECK(s_candidate.modules.order[0].value == V2_BOARD_CAMERA);
        CHECK(s_candidate.media.sink_count == (scenario == 3U ? 2U : 1U));
        CHECK(s_candidate.media.format.backend == (scenario ? CORE_BACKEND_JPEG : CORE_BACKEND_RAW));
        CHECK(V2BoardV1_CaptureTarget(s_candidate.media.format.backend, &capture) == SYS_OK);
        CHECK(capture.num == (scenario ? 25U : 10U) && capture.den == 1U);
        if (scenario == 3U)
        {
            CHECK(s_candidate.media.sinks[0].rate.num == 20U && s_candidate.media.sinks[0].rate.den == 1U);
            CHECK(s_candidate.media.sinks[1].rate.num == 5U && s_candidate.media.sinks[1].rate.den == 1U);
        }
        return 0U;
    }

    memcpy(selections, dual->selections, sizeof(selections));
    desired.selections = selections;
    memcpy(descriptors, registry->modules, sizeof(descriptors));
    memcpy(links, graph.links, sizeof(links));
    switch (scenario)
    {
    case 4U:
        CHECK(registry->count == 3U && catalog->profile_count == 4U);
        CHECK(V2Registry_Validate(registry, &s_issue) == SYS_OK);
        CHECK(V2Registry_Get(registry, (V2ModuleId){V2_BOARD_CAMERA})->lifecycle_ops != NULL);
        CHECK(V2Registry_Get(registry, (V2ModuleId){V2_BOARD_USB})->lifecycle_ops != NULL);
        CHECK(V2Registry_Get(registry, (V2ModuleId){V2_BOARD_STORAGE})->lifecycle_ops != NULL);
        return 0U;
    case 5U:
        partial.count = 2U;  // USB 和 Storage 仍注册，Camera 能力缺失。
        break;
    case 6U:
        partial.modules = descriptors;
        descriptors[1].id = descriptors[0].id;
        break;
    case 7U:
        graph.links = links;
        links[0].sink = links[0].source;
        break;
    case 8U:
        selections[0] = catalog->profiles[0].desired.selections[0];  // Raw 与 JPEG 互斥。
        break;
    case 9U:
        selections[0].sink_policies[0] = (GraphSinkPolicy){{5U}, 0U, {26U, 1U}};
        selections[0].sink_policy_count = 1U;
        break;
    case 10U:
        selections[1].sink_policies[0] = (GraphSinkPolicy){{6U}, 0U, {26U, 1U}};
        selections[1].sink_policy_count = 1U;
        break;
    case 11U:
        CHECK(V2BoardV1_Compose(0U, &s_scratch, &s_candidate, &s_issue, &s_graph_issue) == SYS_ERR_NOT_FOUND);
        CHECK(!memcmp(&s_candidate, &s_saved, sizeof(s_candidate)));
        return 0U;
    case 12U:
        desired = catalog->profiles[0].desired;
        selections[0] = desired.selections[0];
        desired.selections = selections;
        selections[0].sink_policies[0] = (GraphSinkPolicy){{5U}, 0U, {11U, 1U}};
        selections[0].sink_policy_count = 1U;
        break;
    case 13U:
        CHECK(g_sizes[0] == 52U && g_sizes[1] == 56U);
        CHECK(V2BoardV1_CaptureTarget(99U, &capture) == SYS_ERR_NOT_SUPPORTED);
        CHECK(capture.num == 0U && capture.den == 0U);
        return 0U;
    case 14U:
        desired = catalog->profiles[0].desired;
        graph.links = links;
        links[3].sink.entity.value = 6U;  // Raw 不能接到 JPEG Storage 端口。
        break;
    case 15U:
        selections[0].format.max_payload = 40001U;
        selections[0].format.size_image = 40001U;
        selections[1].format = selections[0].format;
        break;
    case 16U:
        CHECK(V2BoardV1_Compose(V2_BOARD_RAW_USB, &s_scratch, &s_applied, &s_issue, &s_graph_issue) == SYS_OK);
        s_saved = s_applied;
        CHECK(V2BoardV1_Compose(V2_BOARD_JPEG_USB_STORAGE, &s_scratch, &s_candidate, &s_issue, &s_graph_issue) == SYS_OK);
        CHECK(!memcmp(&s_applied, &s_saved, sizeof(s_applied)));
        CHECK(s_candidate.media.format.backend == CORE_BACKEND_JPEG);
        return 0U;
    default:
        CHECK(0);
    }
    result = V2Composition_Validate(&partial, &graph, catalog->bindings, catalog->binding_count,
                                    &desired, catalog->validate, &s_scratch, &s_candidate,
                                    &s_issue, &s_graph_issue);
    CHECK(result != SYS_OK);
    CHECK(!memcmp(&s_candidate, &s_saved, sizeof(s_candidate)));
    CHECK(!s_scratch.busy);
    if (scenario == 5U)
        CHECK(s_issue.capability != 0U);
    if (scenario == 6U)
        CHECK(result == V2_CONFLICT);
    if (scenario == 7U)
        CHECK(s_graph_issue.code == GRAPH_ISSUE_LINK);
    if (scenario == 12U)
        CHECK(s_issue.result == SYS_ERR_NOT_SUPPORTED);
    return 0U;
}
