#ifndef CORE_TRAIL_ATTACHMENT_H
#define CORE_TRAIL_ATTACHMENT_H
#include "core/trails/trail_ribbon_solver.h"
#include <stdint.h>
#define TRAIL_ATTACHMENT_CAPACITY 128
/* Generation checked. Zero is invalid. Transform values are owned snapshots. */
typedef uint32_t TrailAttachmentHandle;
typedef struct {
    Matrix previous, current;
    float dt;
    uint32_t generation;
    bool active, discontinuity;
} TrailAttachmentSlot;
typedef struct { TrailAttachmentSlot slots[TRAIL_ATTACHMENT_CAPACITY]; } TrailAttachmentRegistry;

static inline Vector3 TrailAttachment_TransformPoint(Matrix m,Vector3 p) {
    return (Vector3){m.m0*p.x+m.m4*p.y+m.m8*p.z+m.m12,
        m.m1*p.x+m.m5*p.y+m.m9*p.z+m.m13,
        m.m2*p.x+m.m6*p.y+m.m10*p.z+m.m14};
}
static inline bool TrailAttachment_TransformFinite(Matrix m) {
    const float values[]={m.m0,m.m1,m.m2,m.m3,m.m4,m.m5,m.m6,m.m7,
        m.m8,m.m9,m.m10,m.m11,m.m12,m.m13,m.m14,m.m15};
    for(int i=0;i<16;i++) if(!isfinite(values[i])) return false;
    return true;
}
static inline TrailAttachmentSlot *TrailAttachmentRegistry_Get(TrailAttachmentRegistry *r,TrailAttachmentHandle h) {
    unsigned index=(h&255u);
    if(!r || !index || index>TRAIL_ATTACHMENT_CAPACITY) return NULL;
    TrailAttachmentSlot *s=&r->slots[index-1];
    return s->active && s->generation==(h>>8)?s:NULL;
}
static inline void TrailAttachmentRegistry_Reset(TrailAttachmentRegistry *r) {
    if(r) for(int i=0;i<TRAIL_ATTACHMENT_CAPACITY;i++) r->slots[i].active=false;
}
static inline TrailAttachmentHandle TrailAttachmentRegistry_Create(TrailAttachmentRegistry *r,Matrix transform) {
    if(!r || !TrailAttachment_TransformFinite(transform)) return 0;
    for(int i=0;i<TRAIL_ATTACHMENT_CAPACITY;i++) if(!r->slots[i].active) {
        TrailAttachmentSlot *s=&r->slots[i];
        s->generation=(s->generation+1)&0x00ffffffu;
        if(!s->generation) s->generation=1;
        s->active=true;s->previous=s->current=transform;s->dt=0;s->discontinuity=false;
        return (s->generation<<8)|(unsigned)(i+1);
    }
    return 0;
}
static inline bool TrailAttachmentRegistry_Update(TrailAttachmentRegistry *r,TrailAttachmentHandle h,
    Matrix transform,float dt,bool discontinuity) {
    TrailAttachmentSlot *s=TrailAttachmentRegistry_Get(r,h);
    if(!s || !TrailAttachment_TransformFinite(transform) || !isfinite(dt) || dt<=0) return false;
    s->previous=s->current;s->current=transform;s->dt=dt;s->discontinuity=discontinuity;
    return true;
}
static inline bool TrailAttachmentRegistry_Snapshot(TrailAttachmentRegistry *r,TrailAttachmentHandle h,
    Vector3 localOffset,TrailRibbonAnchor *anchor) {
    if(!anchor) return false;
    *anchor=(TrailRibbonAnchor){0};
    TrailAttachmentSlot *s=TrailAttachmentRegistry_Get(r,h);
    if(!s || !Field_FiniteVector(localOffset)) return false;
    anchor->previousPosition=TrailAttachment_TransformPoint(s->previous,localOffset);
    anchor->position=TrailAttachment_TransformPoint(s->current,localOffset);
    anchor->velocity=s->dt>0 && !s->discontinuity?MotionVec_Scale(
        MotionVec_Sub(anchor->position,anchor->previousPosition),1/s->dt):(Vector3){0};
    anchor->valid=true;anchor->discontinuity=s->discontinuity;
    return true;
}
static inline void TrailAttachmentRegistry_Destroy(TrailAttachmentRegistry *r,TrailAttachmentHandle h) {
    TrailAttachmentSlot *s=TrailAttachmentRegistry_Get(r,h);
    if(s) s->active=false;
}
#endif
