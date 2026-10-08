#ifndef CORE_MOTION_RECIPE_H
#define CORE_MOTION_RECIPE_H
#include "core/motion/motion_fields.h"

/* Compile once at birth. Recipes produce ordinary fields: no new receiver
 * solver, shader variant, runtime allocation or per-node recipe evaluation. */
typedef enum {
    MOTION_RECIPE_MOVING_GUIDE, MOTION_RECIPE_PATH_STREAM,
    MOTION_RECIPE_ORBIT, MOTION_RECIPE_AIRFLOW
} MotionFieldRecipeKind;
typedef struct MotionFieldRecipe {
    MotionFieldRecipeKind kind;
    Vector3 origin, axis;
    const MotionPath *path; /* Local metres; required only for PATH_STREAM. */
    BodyPhysicalProperties referenceBody;
    GuidePreset guidance;
    float radiusM, speedMps, swirlSpeedMps, turbulenceSpeedMps;
    float gravityMps2, maxForceNewtons, forwardForceNewtons;
    float durationSec, attackSec, fadeSec;
    unsigned int receiverMask;
} MotionFieldRecipe;

static inline GuideTuning MotionFieldRecipe_Tuning(const MotionFieldRecipe *r) {
    if(!r) return (GuideTuning){0};
    return GuideTuning_Derive(&r->referenceBody,r->radiusM,
        r->kind==MOTION_RECIPE_ORBIT?0:r->speedMps,r->swirlSpeedMps,
        r->turbulenceSpeedMps,r->gravityMps2,r->guidance);
}
/* Failure leaves out untouched. AIRFLOW supplies medium velocity, so body
 * drag/wind coupling controls its response. Other recipes compile bounded
 * Newton actuators against the reference body; actual body mass still wins. */
static inline bool MotionFieldRecipe_Build(const MotionFieldRecipe *r,FieldDesc *out) {
    if(!r || !out || r->kind<MOTION_RECIPE_MOVING_GUIDE || r->kind>MOTION_RECIPE_AIRFLOW ||
       !Field_FiniteVector(r->origin) || !Field_FiniteVector(r->axis) ||
       !isfinite(r->radiusM) || r->radiusM<=0 || !isfinite(r->speedMps) || r->speedMps<0 ||
       !isfinite(r->swirlSpeedMps) || !isfinite(r->turbulenceSpeedMps) || r->turbulenceSpeedMps<0 ||
       !isfinite(r->durationSec) || r->durationSec<=0 || !isfinite(r->attackSec) || r->attackSec<0 ||
       !isfinite(r->fadeSec) || r->fadeSec<0 || r->guidance<GUIDE_MANUAL || r->guidance>GUIDE_TIGHT)
        return false;
    MotionPath path;
    if(r->path && !MotionPath_Build(&path,r->path->points,r->path->count)) return false;
    if(r->kind==MOTION_RECIPE_PATH_STREAM && !r->path) return false;
    FieldDesc d=MotionField_Default();
    d.transform.position=r->origin; d.volume.radiusM=r->radiusM; d.volume.coreFraction=.25f;
    d.receiverMask=r->receiverMask?r->receiverMask:MOTION_RECEIVER_ALL_COMPONENTS;
    d.lifetime=(FieldLifetime){.durationSec=r->durationSec,.attackSec=r->attackSec,.fadeSec=r->fadeSec};
    d.flow.enabled=true; d.flow.addBackgroundVelocity=true;
    d.flow.axis=MotionVec_Length(r->axis)>1e-5f?MotionVec_Normalize(r->axis):(Vector3){0,1,0};
    d.flow.procedural.swirlSpeedMps=r->swirlSpeedMps;
    d.flow.procedural.eddyLengthM=r->radiusM*.3f;
    if(r->kind==MOTION_RECIPE_AIRFLOW) {
        d.flow.velocityMps=MotionVec_Scale(d.flow.axis,r->speedMps);
        d.flow.procedural.turbulenceSpeedMps=r->turbulenceSpeedMps;
        *out=d; return true;
    }
    bool automatic=r->guidance!=GUIDE_MANUAL;
    GuideTuning t=MotionFieldRecipe_Tuning(r);
    float pull=automatic?t.maxForceNewtons:r->maxForceNewtons;
    float forward=automatic?t.forwardForceNewtons:r->forwardForceNewtons;
    float stiffness=automatic?t.stiffnessNPerM:pull/r->radiusM;
    if(!isfinite(pull) || pull<0 || !isfinite(forward) || forward<0 ||
       !isfinite(stiffness) || (automatic && stiffness<=0)) return false;
    d.forceLawCount=1;
    d.forceLaws[0]=(ForceLaw){.type=automatic?FORCE_LAW_MOVING_GUIDE:FORCE_LAW_RADIAL_ATTRACTION,
        .magnitudeNewtons=pull,.springStiffnessNPerM=stiffness};
    d.preserveSphereOffsets=automatic;
    if(r->kind==MOTION_RECIPE_PATH_STREAM) {
        d.volume.shape=FIELD_PATH_TUBE; d.volume.path=path;
        d.preserveSphereOffsets=false; d.preservePathLanes=true; d.rotatePathLanes=automatic;
        d.forceLaws[0].type=FORCE_LAW_PATH_GUIDE;
        d.forceLaws[0].forwardForceNewtons=forward; d.flow.followSpeedMps=r->speedMps;
    } else if(r->kind==MOTION_RECIPE_MOVING_GUIDE && r->path) {
        d.trajectory.mode=FIELD_TRAJECTORY_PATH; d.trajectory.path=path;
        d.trajectory.speedMps=r->speedMps;
    }
    d.flow.procedural.turbulenceSpeedMps=automatic?0:r->turbulenceSpeedMps;
    if(automatic && r->turbulenceSpeedMps>0) {
        if(!isfinite(t.turbulenceForceNewtons)) return false;
        d.forceLaws[d.forceLawCount++]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,
            .magnitudeNewtons=t.turbulenceForceNewtons,
            .procedural={.turbulenceSpeedMps=t.turbulenceSpeedMps,.eddyLengthM=t.eddyLengthM}};
    }
    *out=d; return true;
}
#endif
