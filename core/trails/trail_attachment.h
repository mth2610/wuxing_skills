#ifndef CORE_TRAIL_ATTACHMENT_H
#define CORE_TRAIL_ATTACHMENT_H
#include "core/trails/trail_ribbon_solver.h"
#include "core/motion/motion_frame.h"
/* Source compatibility: trails and typed fields now share owned Motion frames. */
#define TRAIL_ATTACHMENT_CAPACITY MOTION_FRAME_CAPACITY
typedef MotionFrameHandle TrailAttachmentHandle;
typedef MotionFrameSlot TrailAttachmentSlot;
typedef MotionFrameRegistry TrailAttachmentRegistry;
static inline Vector3 TrailAttachment_TransformPoint(Matrix m,Vector3 p) {return MotionFrame_TransformPoint(m,p);}
static inline bool TrailAttachment_TransformFinite(Matrix m) {return MotionFrame_TransformFinite(m);}
static inline TrailAttachmentSlot *TrailAttachmentRegistry_Get(TrailAttachmentRegistry *r,TrailAttachmentHandle h) {return MotionFrameRegistry_Get(r,h);}
static inline void TrailAttachmentRegistry_Reset(TrailAttachmentRegistry *r) {MotionFrameRegistry_Reset(r);}
static inline TrailAttachmentHandle TrailAttachmentRegistry_Create(TrailAttachmentRegistry *r,Matrix m) {return MotionFrameRegistry_Create(r,m);}
static inline bool TrailAttachmentRegistry_Update(TrailAttachmentRegistry *r,TrailAttachmentHandle h,Matrix m,float dt,bool discontinuity) {return MotionFrameRegistry_Update(r,h,m,dt,discontinuity);}
static inline bool TrailAttachmentRegistry_Snapshot(TrailAttachmentRegistry *r,TrailAttachmentHandle h,Vector3 offset,TrailRibbonAnchor *anchor) {
    if(!anchor) return false;
    MotionFrameSnapshot s;
    bool valid=MotionFrameRegistry_Snapshot(r,h,offset,&s);
    *anchor=(TrailRibbonAnchor){.previousPosition=s.previousPosition,.position=s.position,
        .velocity=s.velocity,.valid=s.valid,.discontinuity=s.discontinuity};
    return valid;
}
static inline void TrailAttachmentRegistry_Destroy(TrailAttachmentRegistry *r,TrailAttachmentHandle h) {MotionFrameRegistry_Destroy(r,h);}
#endif
