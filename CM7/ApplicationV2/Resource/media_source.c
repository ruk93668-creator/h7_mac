// 定义物理媒体后端能力及身份比较规则。

#include "media_source.h"
#include <string.h>
// 比较对应的完整 Grant 或物理媒体身份。
int MRGrant_Equal(const MRGrant *left_grant, const MRGrant *right_grant)
{
    return left_grant && right_grant && !memcmp(left_grant, right_grant, sizeof(*left_grant));
}
// 比较对应的完整 Grant 或物理媒体身份。
int MRMedia_Equal(const MRMedia *left_media, const MRMedia *right_media)
{
    return left_media && right_media && !memcmp(left_media, right_media, sizeof(*left_media));
}
// 只读查询后端能力和物理资源证据。
SysResult_t MRSource_Query(const MRSource *source, MRSourceCaps *out)
{
    MRSourceCaps caps;
    SysResult_t result;
    //先核对后端是否提供完整的身份、归还及退出证据接口。
    if (!source || !out || !source->ops || !source->ops->is_owner || !source->ops->caps || !source->ops->role ||
        !source->ops->grant_valid || !source->ops->obtain || !source->ops->validate ||
        !source->ops->readers_quiescent || !source->ops->release || !source->ops->released || !source->ops->exit_safe)
        return SYS_ERR_INVALID_ARG;
    //仅物理资源的所有者可查询后端。
    if (!source->ops->is_owner(source->context))
        return SYS_ERR_INVALID_STATE;
    memset(&caps, 0, sizeof(caps));
    //先在局部对象收集能力，失败时保留原输出。
    result = source->ops->caps(source->context, &caps);
    if (result)
        return result;
    //Raw 和 JPEG 使用独立的持有容量及 Span 约束。
    if (!caps.owners ||
        (caps.backend == MR_RAW && (caps.concurrent != 1 || caps.spans != 1)) ||
        (caps.backend == MR_JPEG && (caps.concurrent > 7 || !caps.concurrent || caps.spans != 2)) ||
        (caps.backend != MR_RAW && caps.backend != MR_JPEG))
        return SYS_ERR_NOT_SUPPORTED;
    //只有后端能力合法才发布。
    *out = caps;
    return SYS_OK;
}

// 分配下一单调编号，耗尽时拒绝回绕。
SysResult_t MRNumber_Advance(uint32_t *sequence)
{
    if (!sequence)
        return SYS_ERR_INVALID_ARG;
    //编号耗尽时拒发，防止旧身份与新事务重复。
    if (*sequence == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;
    ++*sequence;
    return SYS_OK;
}
