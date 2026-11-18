// 校验模块描述符，解析提供方和依赖关系。

/* Compile-time catalog supplied by its caller; never mutated or owned here.
 * All referenced arrays must remain alive/immutable for each complete call.
 * Valid C storage and disjoint input/scratch/output/issue ranges are required.
 * Resolve is bounded and synchronous. It has no callbacks or activation side
 * effects, commits out only on success, and returns original SysResult errors.
 * No implicit provider fallback; ambiguous candidates require an explicit pin.
 */
#ifndef V2_REGISTRY_H
#define V2_REGISTRY_H
#include "../Core/media_format.h"
#define V2_MODULE_MAX 16U
#define V2_PROVIDED_MAX 32U
#define V2_REQUIRED_MAX 64U
#define V2_OPTIONAL_MAX 32U
#define V2_CONFLICT_MAX 32U
#define V2_DESCRIPTOR_ONLY 1U
#define V2_MODULE_FUTURE 2U
#define V2_OPTIONAL_AUTO 1U
typedef struct
{
    V2CapabilityId id;
    uint16_t major, minor;
} V2Provided;
typedef struct
{
    V2CapabilityId id;
    uint16_t min_major, min_minor, max_major, max_minor;
    V2ModuleId preferred;
    uint32_t flags;
} V2Requirement;
struct V2ModuleOps;
struct V2LifecycleOps;
typedef struct
{
    V2ModuleId id;
    uint16_t major, minor;
    const V2Provided *provided;
    uint16_t provided_count;
    const V2Requirement *required;
    uint16_t required_count;
    const V2Requirement *optional;
    uint16_t optional_count;
    const V2CapabilityId *conflicts;
    uint16_t conflict_count;
    const struct V2ModuleOps *ops;
    const struct V2LifecycleOps *lifecycle_ops;
    uint32_t flags;
} V2ModuleDesc;
typedef struct
{
    const V2ModuleDesc *modules;
    uint16_t count;
    uint16_t reserved;
} V2Registry;
typedef struct
{
    uint32_t code, module, capability, related;
    SysResult_t result;
} V2Issue;
typedef struct
{
    uint16_t selected_mask, dependencies[V2_MODULE_MAX];
    V2ModuleId order[V2_MODULE_MAX];
    uint16_t count, optional_missing;
} V2Resolution;
typedef struct
{
    V2Resolution candidate;
    uint8_t selected[16], done[16];
} V2ResolverScratch;
// 校验静态注册表及模块描述符的完整性。
SysResult_t V2Registry_Validate(const V2Registry *, V2Issue *);
// 按模块身份查询静态描述符。
const V2ModuleDesc *V2Registry_Get(const V2Registry *, V2ModuleId);
// 枚举与指定依赖匹配的提供方。
SysResult_t V2Registry_Providers(const V2Registry *, const V2Requirement *, uint16_t *mask);
// 解析模块依赖和提供方，生成稳定执行顺序。
SysResult_t V2Resolver_Resolve(const V2Registry *, const V2ModuleId *, uint16_t,
                               V2ResolverScratch *, V2Resolution *, V2Issue *);
#endif
