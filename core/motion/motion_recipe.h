#ifndef CORE_MOTION_RECIPE_H
#define CORE_MOTION_RECIPE_H
#include "core/motion/motion_fields.h"

/* Compile once at birth. Recipes produce ordinary fields: no new receiver
 * solver, shader variant, runtime allocation or per-node recipe evaluation. */
typedef enum {
    MOTION_RECIPE_MOVING_GUIDE, MOTION_RECIPE_PATH_STREAM,
    MOTION_RECIPE_ORBIT, MOTION_RECIPE_AIRFLOW,
    MOTION_RECIPE_CONVERGE, MOTION_RECIPE_BLAST
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
    /* For CONVERGE and BLAST, radial attraction/repulsion acts as an active
     * trajectory guide; external gravity must not artificially inflate the omega
     * derivation into violent 25G spikes. */
    float effectiveGravity = (r->kind == MOTION_RECIPE_CONVERGE || r->kind == MOTION_RECIPE_BLAST) ? 0.0f : r->gravityMps2;
    return GuideTuning_Derive(&r->referenceBody,r->radiusM,
        (r->kind==MOTION_RECIPE_ORBIT||r->kind==MOTION_RECIPE_CONVERGE||r->kind==MOTION_RECIPE_BLAST)?0:r->speedMps,
        r->swirlSpeedMps,r->turbulenceSpeedMps,effectiveGravity,r->guidance);
}
/* Failure leaves out untouched. AIRFLOW supplies medium velocity, so body
 * drag/wind coupling controls its response. Other recipes compile bounded
 * Newton actuators against the reference body; actual body mass still wins. */
static inline bool MotionFieldRecipe_Build(const MotionFieldRecipe *r,FieldDesc *out) {
    if(!r || !out || r->kind<MOTION_RECIPE_MOVING_GUIDE || r->kind>MOTION_RECIPE_BLAST ||
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
    if(r->kind==MOTION_RECIPE_CONVERGE) {
        /* When launch speed is already high or automatic guidance is used, scale pull force
         * so radial acceleration stays smooth (~1.5G to 2.5G max) without velocity stacking. */
        float convergePull = automatic ? (pull * 0.25f) : pull;
        d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_RADIAL_ATTRACTION,.magnitudeNewtons=convergePull};
        d.preserveSphereOffsets=false;
    } else if(r->kind==MOTION_RECIPE_BLAST) {
        float blastPush = automatic ? (pull * 0.35f) : pull;
        d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_RADIAL_ATTRACTION,.magnitudeNewtons=-blastPush};
        d.preserveSphereOffsets=false;
    } else {
        d.forceLaws[0]=(ForceLaw){.type=automatic?FORCE_LAW_MOVING_GUIDE:FORCE_LAW_RADIAL_ATTRACTION,
            .magnitudeNewtons=pull,.springStiffnessNPerM=stiffness};
        d.preserveSphereOffsets=automatic;
    }
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

/* =========================================================================
 * 1-LINER RECIPE PRESETS FOR SKILL & VFX AUTHORING
 * ========================================================================= */

// 1. Quỹ đạo quay tròn quanh tâm / nhân vật (kiếm khí, rune, khiên chắn bảo hộ)
static inline MotionFieldRecipe MotionRecipe_Orbit(Vector3 center, Vector3 axis,
    float radiusM, float swirlSpeedMps, float durationSec) {
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_ORBIT,
        .origin = center,
        .axis = MotionVec_Length(axis) > 1e-5f ? axis : (Vector3){0, 1, 0},
        .referenceBody = BodyPhysicalProperties_Sphere(0.004f, 600.0f, 0.47f),
        .guidance = GUIDE_BALANCED,
        .radiusM = radiusM > 0.0f ? radiusM : 1.0f,
        .swirlSpeedMps = swirlSpeedMps,
        .durationSec = durationSec > 0.0f ? durationSec : 1.0f,
        .attackSec = 0.1f,
        .fadeSec = 0.2f,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    return r;
}

// 2. Luồng gió / đối lưu thẳng có vận tốc định hướng
static inline MotionFieldRecipe MotionRecipe_Airflow(Vector3 origin, Vector3 dir,
    float radiusM, float speedMps, float turbulenceMps, float durationSec) {
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_AIRFLOW,
        .origin = origin,
        .axis = MotionVec_Length(dir) > 1e-5f ? dir : (Vector3){0, 0, 1},
        .referenceBody = BodyPhysicalProperties_Sphere(0.004f, 600.0f, 0.47f),
        .guidance = GUIDE_BALANCED,
        .radiusM = radiusM > 0.0f ? radiusM : 1.5f,
        .speedMps = speedMps,
        .turbulenceSpeedMps = turbulenceMps,
        .durationSec = durationSec > 0.0f ? durationSec : 1.0f,
        .attackSec = 0.05f,
        .fadeSec = 0.15f,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    return r;
}

// 3. Đường dẫn dòng chảy theo spline (rồng bay, dải lụa tiên đạo, kiếm kích)
static inline MotionFieldRecipe MotionRecipe_PathStream(const MotionPath *path,
    float radiusM, float speedMps, float swirlSpeedMps, float durationSec) {
    Vector3 first = (path && path->count > 0) ? path->points[0] : (Vector3){0};
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_PATH_STREAM,
        .origin = first,
        .axis = (Vector3){0, 1, 0},
        .path = path,
        .referenceBody = BodyPhysicalProperties_Sphere(0.004f, 600.0f, 0.47f),
        .guidance = GUIDE_BALANCED,
        .radiusM = radiusM > 0.0f ? radiusM : 1.0f,
        .speedMps = speedMps,
        .swirlSpeedMps = swirlSpeedMps,
        .durationSec = durationSec > 0.0f ? durationSec : 2.0f,
        .attackSec = 0.1f,
        .fadeSec = 0.2f,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    return r;
}

// 4. Xoắn ốc tiến tới (chưởng lực xoắn xuyên tâm, phi kiếm kích)
static inline MotionFieldRecipe MotionRecipe_SpiralThrust(Vector3 origin, Vector3 forwardDir,
    float radiusM, float forwardSpeedMps, float swirlSpeedMps, float durationSec) {
    Vector3 fwd = MotionVec_Length(forwardDir) > 1e-5f ? MotionVec_Normalize(forwardDir) : (Vector3){0, 0, 1};
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_MOVING_GUIDE,
        .origin = origin,
        .axis = fwd,
        .referenceBody = BodyPhysicalProperties_Sphere(0.004f, 600.0f, 0.47f),
        .guidance = GUIDE_BALANCED,
        .radiusM = radiusM > 0.0f ? radiusM : 1.2f,
        .speedMps = forwardSpeedMps,
        .swirlSpeedMps = swirlSpeedMps,
        .durationSec = durationSec > 0.0f ? durationSec : 1.5f,
        .attackSec = 0.05f,
        .fadeSec = 0.2f,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    return r;
}

// 5. Trường tụ lực / hố đen / tụ khí (hút particle và trail về tâm)
static inline MotionFieldRecipe MotionRecipe_Converge(Vector3 center, float radiusM,
    float pullForceNewtons, float durationSec) {
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_CONVERGE,
        .origin = center,
        .axis = (Vector3){0, 1, 0},
        .referenceBody = BodyPhysicalProperties_Sphere(0.004f, 600.0f, 0.47f),
        .guidance = GUIDE_MANUAL,
        .radiusM = radiusM > 0.0f ? radiusM : 3.0f,
        .maxForceNewtons = pullForceNewtons > 0.0f ? pullForceNewtons : 1.5f,
        .durationSec = durationSec > 0.0f ? durationSec : 1.0f,
        .attackSec = 0.05f,
        .fadeSec = 0.15f,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    return r;
}

// 6. Trường đẩy tỏa tròn / nổ xung kích (đẩy tung particle và vệt trail ra ngoài)
static inline MotionFieldRecipe MotionRecipe_Blast(Vector3 center, float radiusM,
    float blastForceNewtons, float durationSec) {
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_BLAST,
        .origin = center,
        .axis = (Vector3){0, 1, 0},
        .referenceBody = BodyPhysicalProperties_Sphere(0.004f, 600.0f, 0.47f),
        .guidance = GUIDE_MANUAL,
        .radiusM = radiusM > 0.0f ? radiusM : 4.0f,
        .maxForceNewtons = blastForceNewtons > 0.0f ? blastForceNewtons : 3.0f,
        .durationSec = durationSec > 0.0f ? durationSec : 0.6f,
        .attackSec = 0.02f,
        .fadeSec = 0.25f,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS
    };
    return r;
}

#endif
