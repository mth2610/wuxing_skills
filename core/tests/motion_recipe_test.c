/* Real registry/default builder bootstrap; no renderer, GPU or game runtime. */
#define main MotionRecipeBootstrapMain
#include "core/tests/motion_fields_test.c"
#undef main
#include "core/motion/motion_recipe.h"
#include <assert.h>
#include <string.h>
static void RejectUnchanged(const MotionFieldRecipe *recipe) {
    FieldDesc sentinel;memset(&sentinel,0x5a,sizeof(sentinel));FieldDesc out=sentinel;
    assert(!MotionFieldRecipe_Build(recipe,&out));assert(!memcmp(&out,&sentinel,sizeof(out)));
}
int main(void) {
    MotionFieldRecipe r={.kind=MOTION_RECIPE_MOVING_GUIDE,.origin={1,2,3},.axis={0,0,2},
        .referenceBody=BodyPhysicalProperties_Sphere(.004f,600,.47f),.guidance=GUIDE_BALANCED,
        .radiusM=1,.speedMps=2,.swirlSpeedMps=1,.turbulenceSpeedMps=.5f,.gravityMps2=9.81f,
        .maxForceNewtons=.1f,.forwardForceNewtons=.03f,.durationSec=2,.attackSec=.1f,.fadeSec=.2f};
    FieldDesc d;assert(MotionFieldRecipe_Build(&r,&d));
    assert(d.receiverMask==MOTION_RECEIVER_ALL_COMPONENTS && d.volume.shape==FIELD_SPHERE);
    assert(d.preserveSphereOffsets && d.forceLawCount==2 && d.forceLaws[0].type==FORCE_LAW_MOVING_GUIDE);
    GuideTuning tuning=MotionFieldRecipe_Tuning(&r);
    assert(d.forceLaws[0].magnitudeNewtons==tuning.maxForceNewtons);
    assert(d.forceLaws[0].springStiffnessNPerM==tuning.stiffnessNPerM);
    assert(d.forceLaws[1].type==FORCE_LAW_CURL_FORCE && d.forceLaws[1].magnitudeNewtons==tuning.turbulenceForceNewtons);
    assert(d.flow.addBackgroundVelocity && d.flow.procedural.turbulenceSpeedMps==0);
    assert(d.lifetime.durationSec==2 && d.lifetime.attackSec==.1f && d.lifetime.fadeSec==.2f);
    Vector3 points[]={{0,0,0},{1,0,0},{2,1,0}};MotionPath path;
    assert(MotionPath_Build(&path,points,3));r.path=&path;
    assert(MotionFieldRecipe_Build(&r,&d));
    assert(d.trajectory.mode==FIELD_TRAJECTORY_PATH && d.trajectory.path.count==3 && d.trajectory.speedMps==2);
    r.kind=MOTION_RECIPE_PATH_STREAM;assert(MotionFieldRecipe_Build(&r,&d));
    assert(d.volume.shape==FIELD_PATH_TUBE && d.trajectory.mode==FIELD_TRAJECTORY_STATIC &&
        d.volume.path.count==3 && d.preservePathLanes && d.rotatePathLanes && !d.preserveSphereOffsets);
    assert(d.forceLaws[0].type==FORCE_LAW_PATH_GUIDE && d.flow.followSpeedMps==2);
    assert(d.forceLaws[0].forwardForceNewtons==MotionFieldRecipe_Tuning(&r).forwardForceNewtons);
    path.points[1].x=999;assert(d.volume.path.points[1].x==1);path.points[1].x=1;
    r.path=NULL;r.kind=MOTION_RECIPE_ORBIT;assert(MotionFieldRecipe_Build(&r,&d));
    assert(d.volume.shape==FIELD_SPHERE && d.volume.path.count==0 && d.trajectory.mode==FIELD_TRAJECTORY_STATIC);
    assert(d.preserveSphereOffsets && d.flow.axis.z==1 && d.flow.procedural.swirlSpeedMps==1);
    r.speedMps=0;assert(MotionFieldRecipe_Build(&r,&d));
    r.kind=MOTION_RECIPE_AIRFLOW;r.speedMps=2;assert(MotionFieldRecipe_Build(&r,&d));
    assert(d.forceLawCount==0 && !d.preserveSphereOffsets && !d.preservePathLanes);
    assert(d.flow.velocityMps.z==2 && d.flow.procedural.turbulenceSpeedMps==.5f);
    r.kind=MOTION_RECIPE_MOVING_GUIDE;r.guidance=GUIDE_MANUAL;assert(MotionFieldRecipe_Build(&r,&d));
    assert(d.forceLawCount==1 && d.forceLaws[0].type==FORCE_LAW_RADIAL_ATTRACTION && !d.preserveSphereOffsets);
    assert(d.forceLaws[0].magnitudeNewtons==.1f && d.flow.procedural.turbulenceSpeedMps==.5f);
    r.path=&path;r.kind=MOTION_RECIPE_PATH_STREAM;assert(MotionFieldRecipe_Build(&r,&d));
    assert(d.forceLaws[0].type==FORCE_LAW_PATH_GUIDE && d.forceLaws[0].forwardForceNewtons==.03f && !d.rotatePathLanes);
    r.receiverMask=MOTION_RECEIVER_TRAIL;assert(MotionFieldRecipe_Build(&r,&d) && d.receiverMask==MOTION_RECEIVER_TRAIL);
    MotionFieldRecipe invalid=r;invalid.path=NULL;RejectUnchanged(&invalid);
#define REJECT(member,value) do {invalid=r;invalid.member=(value);RejectUnchanged(&invalid);} while(0)
    REJECT(kind,99);REJECT(radiusM,0);REJECT(radiusM,NAN);REJECT(speedMps,-1);REJECT(speedMps,NAN);
    REJECT(swirlSpeedMps,NAN);REJECT(turbulenceSpeedMps,-1);REJECT(durationSec,0);REJECT(attackSec,-1);REJECT(fadeSec,NAN);
    REJECT(guidance,99);REJECT(maxForceNewtons,-1);REJECT(forwardForceNewtons,-1);
    invalid=r;invalid.origin.x=NAN;RejectUnchanged(&invalid);
    invalid=r;invalid.axis.y=NAN;RejectUnchanged(&invalid);
    assert(!MotionFieldRecipe_Build(NULL,&d) && !MotionFieldRecipe_Build(&r,NULL));

    /* Verify 1-liner recipe presets */
    MotionFieldRecipe orbitR = MotionRecipe_Orbit((Vector3){0,1,0}, (Vector3){0,1,0}, 2.0f, 5.0f, 3.0f);
    assert(MotionFieldRecipe_Build(&orbitR, &d));
    assert(d.volume.shape == FIELD_SPHERE && d.volume.radiusM == 2.0f && d.flow.procedural.swirlSpeedMps == 5.0f);

    MotionFieldRecipe airflowR = MotionRecipe_Airflow((Vector3){0,0,0}, (Vector3){0,0,1}, 3.0f, 8.0f, 1.0f, 2.5f);
    assert(MotionFieldRecipe_Build(&airflowR, &d));
    assert(d.flow.velocityMps.z == 8.0f && d.flow.procedural.turbulenceSpeedMps == 1.0f);

    MotionFieldRecipe streamR = MotionRecipe_PathStream(&path, 1.5f, 6.0f, 4.0f, 4.0f);
    assert(MotionFieldRecipe_Build(&streamR, &d));
    assert(d.volume.shape == FIELD_PATH_TUBE && d.flow.followSpeedMps == 6.0f);

    MotionFieldRecipe thrustR = MotionRecipe_SpiralThrust((Vector3){1,1,1}, (Vector3){1,0,0}, 1.2f, 10.0f, 15.0f, 2.0f);
    assert(MotionFieldRecipe_Build(&thrustR, &d));
    assert(d.volume.shape == FIELD_SPHERE && d.trajectory.speedMps == 10.0f && d.flow.procedural.swirlSpeedMps == 15.0f);

    puts("PASS: shared recipes preserve manual/auto force units, path/sphere/airflow contracts, presets and atomic rejection");
    return 0;
}
