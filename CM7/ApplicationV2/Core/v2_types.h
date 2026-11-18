// 定义 V2 共用身份、状态和容量相关类型。

/* Phase 1 value types only. No task, hardware, session allocator or resource owner.
 * Module/capability/consumer/request IDs: uint32, zero invalid. Graph global IDs:
 * uint16, 1..catalog count; Pad IDs: uint16, zero-based within their Entity.
 * IDs from different wrappers/namespaces are not interchangeable. Status is a
 * fixed-width operation projection, never a module lifecycle state machine.
 */
#ifndef V2_TYPES_H
#define V2_TYPES_H
#include "sys_result.h"
#include <stddef.h>
#include <stdint.h>
typedef struct
{
    uint32_t value;
} V2ModuleId;
typedef struct
{
    uint32_t value;
} V2CapabilityId;
typedef struct
{
    uint32_t value;
} V2ConsumerId;
typedef struct
{
    uint32_t value;
} V2RequestId;
typedef struct
{
    uint16_t value;
} GraphEntityId;
typedef struct
{
    uint16_t value;
} GraphPadId;
typedef struct
{
    uint16_t value;
} GraphLinkId;
typedef struct
{
    uint16_t value;
} GraphPipelineId;
typedef struct
{
    uint16_t value;
} GraphCapsId;
typedef uint32_t V2Status;
enum
{
    V2_STATUS_IDLE,
    V2_STATUS_PENDING,
    V2_STATUS_DONE,
    V2_STATUS_FAILED
};
#define V2_SUCCESS SYS_OK
#define V2_BUSY SYS_ERR_BUSY
#define V2_INVALID_ARGUMENT SYS_ERR_INVALID_ARG
#define V2_NOT_FOUND SYS_ERR_NOT_FOUND
#define V2_NOT_SUPPORTED SYS_ERR_NOT_SUPPORTED
#define V2_RESOURCE_EXHAUSTED SYS_ERR_NO_RESOURCE
#define V2_CONFLICT SYS_ERR_ALREADY_EXISTS
#define V2_TIMEOUT SYS_ERR_TIMEOUT
#define V2_INVALID_STATE SYS_ERR_INVALID_STATE
#define V2_IO_ERROR SYS_ERR_IO
#define V2_INTERNAL_ERROR (-11)
#define V2_INTERFACE_VERSION 1U
// 检查对应身份是否为有效值。
static inline int V2_IdValid(uint32_t id) { return id != 0U; }
// 检查对应身份是否为有效值。
static inline int V2_ModuleIdValid(V2ModuleId id) { return V2_IdValid(id.value); }
// 检查对应身份是否为有效值。
static inline int V2_CapabilityIdValid(V2CapabilityId id) { return V2_IdValid(id.value); }
// 检查对应身份是否为有效值。
static inline int V2_ConsumerIdValid(V2ConsumerId id) { return V2_IdValid(id.value); }
// 检查对应身份是否为有效值。
static inline int V2_RequestIdValid(V2RequestId id) { return V2_IdValid(id.value); }
// 检查状态码是否在 V2 公开定义范围内。
static inline int V2_StatusValid(V2Status status)
{
    return (uint32_t)status <= (uint32_t)V2_STATUS_FAILED;
}
/* Caller-owned disjoint storage. No operation owns its input descriptors. */
static inline int V2_Overlap(const void *first_buffer, size_t na, const void *second_buffer, size_t nb)
{
    uintptr_t first_address = (uintptr_t)first_buffer, second_address = (uintptr_t)second_buffer;
    if (na > UINTPTR_MAX - first_address || nb > UINTPTR_MAX - second_address)
        return 1;
    return na && nb && first_address < second_address + nb && second_address < first_address + na;
}
#endif
