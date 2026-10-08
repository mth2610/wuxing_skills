#ifndef CORE_MOTION_FRAME_H
#define CORE_MOTION_FRAME_H
#include "core/motion/physical_field.h"
#include <stdint.h>
#define MOTION_FRAME_CAPACITY 128
/* Generation checked. Zero is invalid. Transform values are owned snapshots. */
typedef uint32_t MotionFrameHandle;
/* Shared frame snapshot. Point velocity is the finite difference of the exact
 * transformed offset; transform metadata carries rigid angular velocity. */
typedef struct {
    Vector3 previousPosition, position, velocity;
    bool valid, discontinuity;
    uint32_t revision;
    FieldTransform transform;
    Matrix previous, current;
} MotionFrameSnapshot;
typedef struct {
    Matrix previous, current;
    FieldTransform transform;
    float dt;
    uint32_t generation, revision;
    bool active, discontinuity;
} MotionFrameSlot;
typedef struct { MotionFrameSlot slots[MOTION_FRAME_CAPACITY]; } MotionFrameRegistry;

static inline Vector3 MotionFrame_TransformPoint(Matrix m,Vector3 p) {
    return (Vector3){m.m0*p.x+m.m4*p.y+m.m8*p.z+m.m12,
        m.m1*p.x+m.m5*p.y+m.m9*p.z+m.m13,
        m.m2*p.x+m.m6*p.y+m.m10*p.z+m.m14};
}
static inline bool MotionFrame_TransformFinite(Matrix m) {
    const float values[]={m.m0,m.m1,m.m2,m.m3,m.m4,m.m5,m.m6,m.m7,
        m.m8,m.m9,m.m10,m.m11,m.m12,m.m13,m.m14,m.m15};
    for(int i=0;i<16;i++) if(!isfinite(values[i])) return false;
    return true;
}
static inline Vector3 MotionFrame_AngularVelocity(Matrix previous,Matrix current,float dt);
static inline void MotionFrame_CacheTransform(MotionFrameSlot *s) {
    s->transform=(FieldTransform){
        .position={s->current.m12,s->current.m13,s->current.m14},
        .axisX={s->current.m0,s->current.m1,s->current.m2},
        .axisY={s->current.m4,s->current.m5,s->current.m6},
        .axisZ={s->current.m8,s->current.m9,s->current.m10}};
    if(s->dt>0 && !s->discontinuity) {
        s->transform.frameVelocityMps=MotionVec_Scale((Vector3){
            s->current.m12-s->previous.m12,s->current.m13-s->previous.m13,
            s->current.m14-s->previous.m14},1/s->dt);
        s->transform.angularVelocityRadPerSec=MotionFrame_AngularVelocity(s->previous,s->current,s->dt);
    }
}
static inline bool MotionFrame_IsRigidTransform(const FieldTransform *t) {
    return t && Field_FiniteVector(t->position) &&
        Field_FiniteVector(t->frameVelocityMps) && Field_FiniteVector(t->angularVelocityRadPerSec) &&
        fabsf(MotionVec_Length(t->axisX)-1)<=.001f &&
        fabsf(MotionVec_Length(t->axisY)-1)<=.001f &&
        fabsf(MotionVec_Length(t->axisZ)-1)<=.001f &&
        fabsf(MotionVec_Dot(t->axisX,t->axisY))<=.001f &&
        MotionVec_Dot(MotionVec_Cross(t->axisX,t->axisY),t->axisZ)>=.999f;
}
static inline MotionFrameSlot *MotionFrameRegistry_Get(MotionFrameRegistry *r,MotionFrameHandle h) {
    unsigned index=(h&255u);
    if(!r || !index || index>MOTION_FRAME_CAPACITY) return NULL;
    MotionFrameSlot *s=&r->slots[index-1];
    return s->active && s->generation==(h>>8)?s:NULL;
}
static inline void MotionFrameRegistry_Reset(MotionFrameRegistry *r) {
    if(r) for(int i=0;i<MOTION_FRAME_CAPACITY;i++) r->slots[i].active=false;
}
static inline MotionFrameHandle MotionFrameRegistry_Create(MotionFrameRegistry *r,Matrix transform) {
    if(!r || !MotionFrame_TransformFinite(transform)) return 0;
    for(int i=0;i<MOTION_FRAME_CAPACITY;i++) if(!r->slots[i].active) {
        MotionFrameSlot *s=&r->slots[i];
        s->generation=(s->generation+1)&0x00ffffffu;
        if(!s->generation) s->generation=1;
        s->active=true;s->previous=s->current=transform;s->dt=0;s->discontinuity=false;s->revision=1;MotionFrame_CacheTransform(s);
        return (s->generation<<8)|(unsigned)(i+1);
    }
    return 0;
}
static inline bool MotionFrameRegistry_Update(MotionFrameRegistry *r,MotionFrameHandle h,
    Matrix transform,float dt,bool discontinuity) {
    MotionFrameSlot *s=MotionFrameRegistry_Get(r,h);
    if(!s || !MotionFrame_TransformFinite(transform) || !isfinite(dt) || dt<=0) return false;
    s->previous=s->current;s->current=transform;s->dt=dt;s->discontinuity=discontinuity;
    if(!++s->revision) s->revision=1;
    MotionFrame_CacheTransform(s);
    return true;
}
/* World rotation delta R_current * transpose(R_previous), converted to its
 * shortest axis-angle. Diagonal extraction remains stable near half turns. */
static inline Vector3 MotionFrame_AngularVelocity(Matrix previous,Matrix current,float dt) {
    Vector3 a[3]={{current.m0,current.m1,current.m2},{current.m4,current.m5,current.m6},{current.m8,current.m9,current.m10}};
    Vector3 b[3]={{previous.m0,previous.m1,previous.m2},{previous.m4,previous.m5,previous.m6},{previous.m8,previous.m9,previous.m10}};
    float r[3][3]={{0}};
    for(int k=0;k<3;k++) {
        const float av[3]={a[k].x,a[k].y,a[k].z},bv[3]={b[k].x,b[k].y,b[k].z};
        for(int i=0;i<3;i++) for(int j=0;j<3;j++) r[i][j]+=av[i]*bv[j];
    }
    float angle=acosf(Motion_Clamp((r[0][0]+r[1][1]+r[2][2]-1)*.5f,-1,1));
    if(angle<1e-6f) return (Vector3){0};
    Vector3 axis={r[2][1]-r[1][2],r[0][2]-r[2][0],r[1][0]-r[0][1]};
    if(angle>3.13f) {
        int k=r[1][1]>r[0][0]?1:0;if(r[2][2]>r[k][k]) k=2;
        float v[3]={0};v[k]=sqrtf(fmaxf(0,(r[k][k]+1)*.5f));
        if(v[k]<1e-6f) return (Vector3){0};
        for(int i=0;i<3;i++) if(i!=k) v[i]=(r[k][i]+r[i][k])/(4*v[k]);
        Vector3 half={v[0],v[1],v[2]};
        if(MotionVec_Dot(half,axis)<0) half=MotionVec_Scale(half,-1);
        axis=half;
    }
    return MotionVec_Scale(MotionVec_Normalize(axis),angle/dt);
}
static inline bool MotionFrameRegistry_Snapshot(MotionFrameRegistry *r,MotionFrameHandle h,
    Vector3 localOffset,MotionFrameSnapshot *anchor) {
    if(!anchor) return false;
    *anchor=(MotionFrameSnapshot){0};
    MotionFrameSlot *s=MotionFrameRegistry_Get(r,h);
    if(!s || !Field_FiniteVector(localOffset)) return false;
    anchor->revision=s->revision;
    anchor->previous=s->previous;anchor->current=s->current;
    anchor->previousPosition=MotionFrame_TransformPoint(s->previous,localOffset);
    anchor->position=MotionFrame_TransformPoint(s->current,localOffset);
    anchor->velocity=s->dt>0 && !s->discontinuity?MotionVec_Scale(
        MotionVec_Sub(anchor->position,anchor->previousPosition),1/s->dt):(Vector3){0};
    anchor->transform=s->transform;
    anchor->valid=true;anchor->discontinuity=s->discontinuity;
    return true;
}
static inline void MotionFrameRegistry_Destroy(MotionFrameRegistry *r,MotionFrameHandle h) {
    MotionFrameSlot *s=MotionFrameRegistry_Get(r,h);
    if(s) s->active=false;
}
/* Global static pool, shared by fields and component attachments. Update frames
 * before MotionFields_Update and component updates. Reset invalidates handles. */
MotionFrameHandle MotionFrame_Create(Matrix transform);
bool MotionFrame_Update(MotionFrameHandle handle,Matrix transform,float dt,bool discontinuity);
void MotionFrame_Destroy(MotionFrameHandle handle);
void MotionFrame_Reset(void);
bool MotionFrame_Snapshot(MotionFrameHandle handle,Vector3 localOffset,MotionFrameSnapshot *snapshot);
#endif
