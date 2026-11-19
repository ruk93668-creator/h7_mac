// 验证配置候选的模块绑定和媒体路径。

// 只校验组合候选。调用方清零独占的 Scratch，保持目录有效，并保证各输入、输出和诊断对象互不重叠。
// 必须提供无副作用的资源验证器。验证失败保留原输出，成功仅发布候选，不执行模块生命周期。
#ifndef V2_COMPOSITION_H
#define V2_COMPOSITION_H
#include "../MediaGraph/media_graph.h"
#include "v2_registry.h"
typedef struct
{
    GraphEntityId entity;
    uint16_t reserved;
    V2ModuleId module;
} V2EntityBinding;
typedef struct
{
    const V2ModuleId *roots;
    uint16_t root_count;
    const GraphSelection *selections;
    uint16_t selection_count;
} V2Desired;
// 只读校验候选资源约束，失败时通过 diagnostic 返回原因。
typedef SysResult_t (*V2ResourceValidator)(const GraphValidatedView *media,
                                           const V2Resolution *modules, V2Issue *diagnostic);
typedef struct
{
    V2Resolution modules;
    GraphValidatedView media;
    uint32_t candidate_only;
} V2CompositionCandidate;
typedef struct
{
    uint32_t busy;
    V2ResolverScratch resolver;
    GraphScratch graph;
    V2CompositionCandidate candidate;
} V2CompositionScratch;
// 校验组合候选，候选成立不代表运行配置已经切换。
SysResult_t V2Composition_Validate(const V2Registry *registry, const GraphDesc *graph,
                                   const V2EntityBinding *bindings, uint16_t binding_count,
                                   const V2Desired *desired, V2ResourceValidator validate_resources,
                                   V2CompositionScratch *scratch, V2CompositionCandidate *candidate,
                                   V2Issue *issue, GraphIssue *graph_issue);
#endif
