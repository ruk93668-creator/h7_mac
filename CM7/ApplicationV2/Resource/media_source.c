#include "media_source.h"
#include <string.h>
int MRGrant_Equal(const MRGrant *a,const MRGrant *b) { return a && b && !memcmp(a,b,sizeof(*a)); }
int MRMedia_Equal(const MRMedia *a,const MRMedia *b) { return a && b && !memcmp(a,b,sizeof(*a)); }
SysResult_t MRSource_Query(const MRSource *s, MRSourceCaps *out) {
    MRSourceCaps c; SysResult_t r;
    if(!s || !out || !s->ops || !s->ops->is_owner || !s->ops->caps ||
       !s->ops->grant_valid || !s->ops->obtain || !s->ops->validate ||
       !s->ops->readers_quiescent || !s->ops->release || !s->ops->released || !s->ops->exit_safe)
        return SYS_ERR_INVALID_ARG;
    if(!s->ops->is_owner(s->context)) return SYS_ERR_INVALID_STATE;
    memset(&c,0,sizeof(c)); r=s->ops->caps(s->context,&c); if(r) return r;
    if(!c.owners || (c.owners & ~7U) ||
       (c.backend==MR_RAW && (c.concurrent!=1 || c.spans!=1)) ||
       (c.backend==MR_JPEG && (c.concurrent>2 || !c.concurrent || c.spans!=2)) ||
       (c.backend!=MR_RAW && c.backend!=MR_JPEG)) return SYS_ERR_NOT_SUPPORTED;
    *out=c; return SYS_OK;
}
