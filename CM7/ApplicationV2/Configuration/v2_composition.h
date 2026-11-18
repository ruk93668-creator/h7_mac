// 验证配置候选的模块绑定和媒体路径。

/* Unified pure Core/Composition entry, no execution or Runtime Apply.
 * Caller zero-initializes scratch, keeps all referenced const catalogs alive,
 * and owns disjoint inputs/scratch/output/issues with one owner per scratch.
 * ResourceValidator is mandatory and pure: Phase 1 tests inject a MOCK only.
 * Missing/failed validation leaves out unchanged. candidate_only is always 1.
 * Module Ops/Lifecycle hooks are intentionally not executed or implemented.
 */
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
typedef SysResult_t (*V2ResourceValidator)(const GraphValidatedView *, const V2Resolution *,
                                           V2Issue *);
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
SysResult_t V2Composition_Validate(const V2Registry *, const GraphDesc *, const V2EntityBinding *,
                                   uint16_t, const V2Desired *, V2ResourceValidator,
                                   V2CompositionScratch *, V2CompositionCandidate *, V2Issue *,
                                   GraphIssue *);
#endif
