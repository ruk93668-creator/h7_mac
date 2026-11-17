#include "media_resource.h"
#include <string.h>
#define MAGIC 0x4D525632U
static SysResult_t enter(MRContext *c) {
    if(!c || c->magic!=MAGIC) return SYS_ERR_INVALID_STATE;
    if(!c->source.ops->is_owner(c->source.context)) return SYS_ERR_INVALID_STATE;
    if(c->busy) return SYS_ERR_BUSY;
    c->busy=1; return SYS_OK;
}
static SysResult_t leave(MRContext *c,SysResult_t r) { c->busy=0; return r; }
static int valid_g(const MRGrant *g) { return g && g->session && g->consumer && g->consumer<=3 && g->generation && g->acquire; }
static MRAdmission *admission(MRContext *c,const MRGrant *g) {
    uint32_t i; for(i=0;i<MR_GRANT_MAX;i++) if(c->admission[i].occupied && MRGrant_Equal(&c->admission[i].g,g)) return &c->admission[i];
    return NULL;
}
static MRRecord *record(MRContext *c,const MRRequest *r) {
    uint32_t i; if(!r) return NULL;
    for(i=0;i<MR_HANDOFF_MAX;i++) if(c->records[i].state && !memcmp(&c->records[i].request,r,sizeof(*r))) return &c->records[i];
    return NULL;
}
static MRReceiptRecord *receipt(MRContext *c,const MRReceipt *r) {
    uint32_t i; if(!r) return NULL;
    for(i=0;i<MR_RECEIPT_MAX;i++) if(c->receipts[i].occupied && !memcmp(&c->receipts[i].value,r,sizeof(*r))) return &c->receipts[i];
    return NULL;
}
SysResult_t MR_Init(MRContext *c,const MRSource *s) {
    MRSourceCaps caps; SysResult_t r;
    if(!c || c->magic || V2_Overlap(c,sizeof(*c),s,sizeof(*s))) return SYS_ERR_INVALID_ARG;
    r=MRSource_Query(s,&caps); if(r) return r;
    memset(c,0,sizeof(*c)); c->source=*s; c->caps=caps; c->magic=MAGIC; return SYS_OK;
}
SysResult_t MR_AssociateGrant(MRContext *c,const MRGrant *g) {
    uint32_t i,j; MRAdmission *a=NULL; SysResult_t r;
    if(!valid_g(g)) return SYS_ERR_INVALID_ARG;
    r=enter(c); if(r) return r;
    if(!(c->caps.owners & (1U<<(g->consumer-1)))) return leave(c,SYS_ERR_NOT_SUPPORTED);
    for(i=0;i<MR_GRANT_MAX;i++) if(c->admission[i].occupied && c->admission[i].g.consumer==g->consumer) {
        a=&c->admission[i];
        if(MRGrant_Equal(&a->g,g)) return leave(c,a->closed?SYS_ERR_INVALID_STATE:SYS_OK);
        if(!a->closed || g->session<a->g.session || (g->session==a->g.session && g->generation<=a->g.generation)) return leave(c,SYS_ERR_INVALID_STATE);
        for(j=0;j<MR_HANDOFF_MAX;j++) if(c->records[j].state && MRGrant_Equal(&c->records[j].request.g,&a->g)) return leave(c,SYS_ERR_BUSY);
        r=c->source.ops->exit_safe(c->source.context,&a->g); if(r) return leave(c,r);
        break;
    }
    if(!a) for(i=0;i<MR_GRANT_MAX;i++) if(!c->admission[i].occupied) { a=&c->admission[i]; break; }
    if(!a) return leave(c,SYS_ERR_NO_RESOURCE);
    r=c->source.ops->grant_valid(c->source.context,g); if(r) return leave(c,r);
    memset(a,0,sizeof(*a)); a->g=*g; a->occupied=1; return leave(c,SYS_OK);
}
SysResult_t MR_Stage(MRContext *c,const MRGrant *g,const MRBridge *b,const MRSlot *s,MRRequest *out) {
    MRRecord *e=NULL; MRAdmission *a; uint32_t i; SysResult_t r;
    if(!valid_g(g)||!b||!s||!out||!c || V2_Overlap(c,sizeof(*c),out,sizeof(*out))) return SYS_ERR_INVALID_ARG;
    if((g->consumer==MR_USB && (!b->epoch||!b->lease||!b->command)) || (g->consumer==MR_SD && (!s->session||!s->generation))) return SYS_ERR_INVALID_ARG;
    r=enter(c); if(r) return r;
    a=admission(c,g); if(!a||a->closed) return leave(c,SYS_ERR_INVALID_STATE);
    if(c->issued==UINT32_MAX) return leave(c,SYS_ERR_NO_RESOURCE);
    for(i=0;i<MR_HANDOFF_MAX;i++) if(!c->records[i].state) { e=&c->records[i]; break; }
    if(!e) return leave(c,SYS_ERR_NO_RESOURCE);
    memset(e,0,sizeof(*e)); e->request.g=*g; e->request.b=*b; e->request.s=*s;
    e->request.h.session=g->session; e->request.h.sequence=++c->issued; e->state=MR_LOCAL; *out=e->request;
    return leave(c,SYS_OK);
}
SysResult_t MR_Commit(MRContext *c,const MRRequest *r) {
    MRRecord *e; SysResult_t z=enter(c); if(z) return z;
    e=record(c,r); if(!e) return leave(c,SYS_ERR_INVALID_ARG);
    if(e->state==MR_COMMITTED) return leave(c,SYS_OK);
    if(e->state!=MR_LOCAL) return leave(c,SYS_ERR_INVALID_STATE);
    e->state=MR_COMMITTED; return leave(c,SYS_OK);
}
SysResult_t MR_CancelLocal(MRContext *c,const MRRequest *r) {
    MRRecord *e; SysResult_t z=enter(c); if(z) return z;
    e=record(c,r); if(!e||e->state!=MR_LOCAL) return leave(c,SYS_ERR_INVALID_STATE);
    memset(e,0,sizeof(*e)); return leave(c,SYS_OK);
}
