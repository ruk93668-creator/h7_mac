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

static MRReceiptRecord *for_record(MRContext *c,const MRRecord *e) {
    uint32_t i; for(i=0;i<MR_RECEIPT_MAX;i++) if(c->receipts[i].occupied &&
       MRGrant_Equal(&c->receipts[i].value.g,&e->request.g) &&
       !memcmp(&c->receipts[i].value.h,&e->request.h,sizeof(MRHandoff))) return &c->receipts[i];
    return NULL;
}
static void terminal(MRRecord *e,MRReceiptRecord *t,SysResult_t result) {
    e->state=MR_TERMINAL; t->value.m=e->media; t->value.result=result;
}
SysResult_t MR_Admit(MRContext *c,const MRRequest *r) {
    MRRecord *e; MRAdmission *a; MRReceiptRecord *t=NULL; uint32_t i; SysResult_t z=enter(c); if(z) return z;
    e=record(c,r); if(!e) return leave(c,SYS_ERR_INVALID_ARG);
    if(e->state>=MR_ACCEPTED) return leave(c,e->state==MR_BLOCKED?SYS_ERR_INVALID_STATE:SYS_OK);
    if(e->state!=MR_COMMITTED) return leave(c,SYS_ERR_INVALID_STATE);
    if(c->receipt_issued==UINT32_MAX) return leave(c,SYS_ERR_NO_RESOURCE);
    for(i=0;i<MR_RECEIPT_MAX;i++) if(!c->receipts[i].occupied) { t=&c->receipts[i]; break; }
    if(!t) return leave(c,SYS_ERR_NO_RESOURCE);
    memset(t,0,sizeof(*t)); t->occupied=1; t->value.g=r->g; t->value.h=r->h;
    t->value.serial=++c->receipt_issued; e->state=MR_ACCEPTED;
    a=admission(c,&r->g);
    z=(!a||a->closed)?SYS_ERR_INVALID_STATE:c->source.ops->grant_valid(c->source.context,&r->g);
    if(z) terminal(e,t,z);
    return leave(c,SYS_OK);
}
SysResult_t MR_Close(MRContext *c,const MRRelease *r) {
    MRAdmission *a; SysResult_t z;
    if(!r||!r->request||!valid_g(&r->grant)) return SYS_ERR_INVALID_ARG;
    z=enter(c); if(z) return z;
    a=admission(c,&r->grant); if(!a) return leave(c,SYS_ERR_INVALID_STATE);
    if(a->closed) return leave(c,!memcmp(&a->closing,r,sizeof(*r))?SYS_OK:SYS_ERR_BUSY);
    a->closing=*r; a->closed=1; return leave(c,SYS_OK);
}
SysResult_t MR_Obtain(MRContext *c,const MRRequest *r,MRMedia *m,MRView *v) {
    MRRecord *e; MRAdmission *a; MRReceiptRecord *t; uint32_t i,n=0; SysResult_t z;
    if(!m||!v||!c || V2_Overlap(c,sizeof(*c),m,sizeof(*m)) || V2_Overlap(c,sizeof(*c),v,sizeof(*v)) || V2_Overlap(m,sizeof(*m),v,sizeof(*v))) return SYS_ERR_INVALID_ARG;
    z=enter(c); if(z) return z; e=record(c,r);
    if(!e) return leave(c,SYS_ERR_INVALID_ARG);
    if(e->state==MR_BOUND) { *m=e->media; *v=e->view; return leave(c,SYS_OK); }
    if(e->state!=MR_ACCEPTED) return leave(c,SYS_ERR_INVALID_STATE);
    t=for_record(c,e); if(!t) return leave(c,SYS_ERR_INVALID_STATE);
    a=admission(c,&r->g);
    if(!a||a->closed) { terminal(e,t,SYS_ERR_INVALID_STATE); return leave(c,SYS_ERR_INVALID_STATE); }
    z=c->source.ops->grant_valid(c->source.context,&r->g); if(z) { terminal(e,t,z); return leave(c,z); }
    for(i=0;i<MR_HANDOFF_MAX;i++) if(c->records[i].state==MR_BOUND || c->records[i].state==MR_BLOCKED || c->records[i].state==MR_RETURN_PENDING || c->records[i].state==MR_RELEASED) n++;
    if(n>=c->caps.concurrent) return leave(c,SYS_ERR_BUSY);
    z=c->source.ops->obtain(c->source.context,&r->g,&r->h,&e->media,&e->view);
    if(z) {
        if(e->media.id) { e->state=MR_BLOCKED; e->last_release=z; }
        else terminal(e,t,z);
        return leave(c,z);
    }
    e->state=MR_BLOCKED; /* preserve any actual backend acquisition on bad output */
    if(e->media.backend!=c->caps.backend || !e->media.session || !e->media.id || !e->media.generation ||
       !e->view.data || !e->view.bytes || (c->caps.spans==1 && (e->view.data2||e->view.bytes2)) ||
       ((e->view.bytes2!=0)!=(e->view.data2!=NULL))) return leave(c,SYS_ERR_INVALID_STATE);
    z=c->source.ops->validate(c->source.context,&r->g,&e->media,&e->view); if(z) return leave(c,z);
    e->state=MR_BOUND; *m=e->media; *v=e->view; return leave(c,SYS_OK);
}

SysResult_t MR_Return(MRContext *c,const MRRequest *r,const MRMedia *m) {
    MRRecord *e; MRReceiptRecord *t; SysResult_t z=enter(c); if(z) return z;
    e=record(c,r); if(!e||!MRMedia_Equal(&e->media,m)) return leave(c,SYS_ERR_INVALID_ARG);
    if(e->state==MR_TERMINAL) return leave(c,for_record(c,e)->value.result);
    if(e->state!=MR_BOUND && e->state!=MR_RETURN_PENDING && e->state!=MR_RELEASED) return leave(c,SYS_ERR_INVALID_STATE);
    t=for_record(c,e); if(!t) return leave(c,SYS_ERR_INVALID_STATE);
    if(e->state!=MR_RELEASED) {
        e->state=MR_RETURN_PENDING;
        z=c->source.ops->readers_quiescent(c->source.context,&r->g,m); if(z) return leave(c,z);
        z=c->source.ops->release(c->source.context,&r->g,m);
        if(z) { if(z!=SYS_ERR_BUSY && !e->last_release) e->last_release=z; return leave(c,z); }
        e->state=MR_RELEASED;
    }
    z=c->source.ops->released(c->source.context,&r->g,m); if(z) return leave(c,z);
    terminal(e,t,e->last_release); return leave(c,e->last_release);
}
SysResult_t MR_ReadReceipt(MRContext *c,const MRRequest *r,MRReceipt *out) {
    MRRecord *e; MRReceiptRecord *t; SysResult_t z;
    if(!c||!out||V2_Overlap(c,sizeof(*c),out,sizeof(*out))) return SYS_ERR_INVALID_ARG;
    z=enter(c); if(z) return z; e=record(c,r);
    if(!e||e->state!=MR_TERMINAL) return leave(c,SYS_ERR_BUSY);
    t=for_record(c,e); if(!t) return leave(c,SYS_ERR_INVALID_STATE);
    *out=t->value; return leave(c,SYS_OK);
}
SysResult_t MR_FlushReceipt(MRContext *c,const MRReceipt *r,MRSendReceipt send,void *context) {
    MRReceiptRecord *t; MRRecord *e; uint32_t i; SysResult_t z;
    if(!send) return SYS_ERR_INVALID_ARG;
    z=enter(c); if(z) return z; t=receipt(c,r);
    if(!t) return leave(c,SYS_ERR_INVALID_ARG);
    e=NULL; for(i=0;i<MR_HANDOFF_MAX;i++) if(c->records[i].state==MR_TERMINAL && for_record(c,&c->records[i])==t) { e=&c->records[i]; break; }
    if(!e) return leave(c,SYS_ERR_INVALID_STATE);
    if(t->enqueued) return leave(c,SYS_OK);
    z=send(context,&t->value); if(!z) t->enqueued=1;
    return leave(c,z);
}
SysResult_t MR_ConfirmConsumed(MRContext *c,const MRReceipt *r) {
    MRReceiptRecord *t; SysResult_t z=enter(c); if(z) return z;
    t=receipt(c,r); if(!t||!t->enqueued) return leave(c,SYS_ERR_INVALID_STATE);
    t->consumed=1; return leave(c,SYS_OK);
}
SysResult_t MR_Retire(MRContext *c,const MRReceipt *r) {
    MRReceiptRecord *t; uint32_t i; SysResult_t z=enter(c); if(z) return z;
    t=receipt(c,r); if(!t||!t->consumed) return leave(c,SYS_ERR_BUSY);
    for(i=0;i<MR_HANDOFF_MAX;i++) if(c->records[i].state==MR_TERMINAL && for_record(c,&c->records[i])==t) {
        memset(&c->records[i],0,sizeof(c->records[i])); memset(t,0,sizeof(*t)); return leave(c,SYS_OK);
    }
    return leave(c,SYS_ERR_INVALID_STATE);
}
