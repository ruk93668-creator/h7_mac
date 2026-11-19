// 定义物理媒体后端能力及身份比较规则。

#ifndef V2_MEDIA_SOURCE_H
#define V2_MEDIA_SOURCE_H
#include "../Core/media_format.h"
#include <stdbool.h>
// 由 Camera 所有者同步调用；回调有界且不可重入。复用现有物理池，媒体身份只能来自后端凭据。
#define MR_RAW 1U
#define MR_JPEG 2U
// 后端所有者标签独立于公开消费者 ID，内部持有不代表授权。
#define MR_USB 1U
#define MR_SD 2U
#define MR_INTERNAL 3U
#define MR_CAP_USB 1U
#define MR_CAP_SD 2U
#define MR_CAP_INTERNAL 4U
typedef struct
{
    uint32_t session, consumer, generation, acquire;
} MRGrant;
typedef struct
{
    MRGrant grant;
    uint32_t request;
} MRRelease;
typedef struct
{
    uint32_t session, sequence;
} MRHandoff;
typedef struct
{
    uint32_t epoch, lease, command;
} MRBridge;
typedef struct
{
    uint32_t session, slot, generation;
} MRSlot;
typedef struct
{
    uint32_t backend, session, id, generation;
} MRMedia;
typedef struct
{
    const uint8_t *data;
    uint32_t bytes;
    const uint8_t *data2;
    uint32_t bytes2;
} MRView;
typedef struct
{
    uint32_t backend, owners, concurrent, spans; // concurrent 表示并行交接上限，物理引用由后端维护。
} MRSourceCaps;
typedef struct
{
    bool (*is_owner)(void *);
    SysResult_t (*caps)(void *, MRSourceCaps *);
    SysResult_t (*role)(void *, const MRGrant *, uint32_t *);
    SysResult_t (*grant_valid)(void *, const MRGrant *);
    SysResult_t (*obtain)(void *, const MRGrant *, const MRHandoff *, MRMedia *, MRView *);
    SysResult_t (*validate)(void *, const MRGrant *, const MRMedia *, const MRView *);
    SysResult_t (*readers_quiescent)(void *, const MRGrant *, const MRMedia *);
    SysResult_t (*release)(void *, const MRGrant *, const MRMedia *);
    SysResult_t (*released)(void *, const MRGrant *, const MRMedia *);
    SysResult_t (*exit_safe)(void *, const MRGrant *);
} MRSourceOps;
typedef struct
{
    const MRSourceOps *ops;
    void *context;
} MRSource;
// 分配下一单调编号，耗尽时拒绝回绕。
SysResult_t MRNumber_Advance(uint32_t *);
// 只读查询后端能力和物理资源证据。
SysResult_t MRSource_Query(const MRSource *, MRSourceCaps *);
// 比较对应的完整 Grant 或物理媒体身份。
int MRGrant_Equal(const MRGrant *, const MRGrant *);
// 比较对应的完整 Grant 或物理媒体身份。
int MRMedia_Equal(const MRMedia *, const MRMedia *);
#endif
