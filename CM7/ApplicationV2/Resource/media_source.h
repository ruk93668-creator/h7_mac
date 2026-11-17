#ifndef V2_MEDIA_SOURCE_H
#define V2_MEDIA_SOURCE_H
#include "../Core/media_format.h"
#include <stdbool.h>
/* Camera-owner synchronous ports. All callbacks are bounded, non-reentrant.
 * Ports borrow existing physical resources; never instantiate another pool.
 * Media tokens come from the backend, never from an address comparison.
 */
#define MR_RAW 1U
#define MR_JPEG 2U
#define MR_USB 1U
#define MR_SD 2U
#define MR_INTERNAL 3U
#define MR_CAP_USB 1U
#define MR_CAP_SD 2U
#define MR_CAP_INTERNAL 4U
typedef struct { uint32_t session, consumer, generation, acquire; } MRGrant;
typedef struct { MRGrant grant; uint32_t request; } MRRelease;
typedef struct { uint32_t session, sequence; } MRHandoff;
typedef struct { uint32_t epoch, lease, command; } MRBridge;
typedef struct { uint32_t session, slot, generation; } MRSlot;
typedef struct { uint32_t backend, session, id, generation; } MRMedia;
typedef struct { const uint8_t *data; uint32_t bytes; const uint8_t *data2; uint32_t bytes2; } MRView;
typedef struct { uint32_t backend, owners, concurrent, spans; } MRSourceCaps;
typedef struct {
    bool (*is_owner)(void *);
    SysResult_t (*caps)(void *, MRSourceCaps *);
    SysResult_t (*grant_valid)(void *, const MRGrant *);
    SysResult_t (*obtain)(void *, const MRGrant *, const MRHandoff *, MRMedia *, MRView *);
    SysResult_t (*validate)(void *, const MRGrant *, const MRMedia *, const MRView *);
    SysResult_t (*readers_quiescent)(void *, const MRGrant *, const MRMedia *);
    SysResult_t (*release)(void *, const MRGrant *, const MRMedia *);
    SysResult_t (*released)(void *, const MRGrant *, const MRMedia *);
    SysResult_t (*exit_safe)(void *, const MRGrant *);
} MRSourceOps;
typedef struct { const MRSourceOps *ops; void *context; } MRSource;
SysResult_t MRSource_Query(const MRSource *, MRSourceCaps *);
int MRGrant_Equal(const MRGrant *, const MRGrant *);
int MRMedia_Equal(const MRMedia *, const MRMedia *);
#endif
