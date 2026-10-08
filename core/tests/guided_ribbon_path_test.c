/* Production spatial registry + chain solver, no renderer or GPU execution. */
#define main GuidedPathBootstrapMain
#include "core/tests/motion_fields_test.c"
#undef main
#include "core/motion/motion_recipe.h"
#include "core/trails/trail_ribbon_solver.h"
#include <assert.h>
static MotionReceiver pathReceivers[TRAIL_RIBBON_MAX_NODES];
static void SamplePath(void *user,Vector3 p,Vector3 v,float dt,int node,float offset,
    FieldSample *sample,Vector3 *air) {
    MotionBodyProfile *profile=user;
    BodyPhysicalProperties body=MotionBody_GetPhysicalProperties(profile);
    MediumProperties medium={.densityKgM3=1.225f};
    ReceiverConstraints constraints={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
    MotionFields_SampleSpatialBodyAtOffset(p,v,&body,&medium,&constraints,dt,offset,
        MOTION_RECEIVER_TRAIL,&pathReceivers[node],sample);
    *air=medium.velocityMps;
}
int main(void) {
    MotionFields_Reset();
    Vector3 points[33];
    for(int i=0;i<33;i++) {
        float t=i/32.f;
        points[i]=(Vector3){4*t,0,2*sinf(t*3.14159265359f)};
    }
    MotionPath path;assert(MotionPath_Build(&path,points,33));
    MotionFieldRecipe recipe={.kind=MOTION_RECIPE_PATH_STREAM,.path=&path,
        .referenceBody=BodyPhysicalProperties_Sphere(.004f,600,.47f),
        .guidance=GUIDE_TIGHT,.radiusM=.8f,.speedMps=2,.durationSec=5,
        .receiverMask=MOTION_RECEIVER_TRAIL};
    FieldDesc field;assert(MotionFieldRecipe_Build(&recipe,&field));
    assert(MotionFields_CreateField(&field));
    TrailRibbonMaterial material=TrailRibbonMaterial_Default();
    material.body.gravityScale=0;
    TrailRibbonState chain;
    assert(TrailRibbon_Initialize(&chain,24,(Vector3){0},
        MotionVec_Scale(path.tangents[0],-1),.8f,
        (Vector3){0},TRAIL_RIBBON_FREE));
    float maxError=0,maxStretch=0,maxBend=0;
    for(int frame=0;frame<240;frame++) {
        MotionFields_Update(1.f/120);
        TrailRibbon_Advance(&chain,&material,1.f/120,NULL,SamplePath,&material.body);
        if(frame<60) continue; /* The initial tail starts behind endpoint A. */
        MotionPathSample head=MotionPath_Project(&path,chain.position[0],0,31);
        MotionPathSample tail=MotionPath_Project(&path,chain.position[23],0,31);
        assert(head.distance>tail.distance+.5f);
        for(int node=0;node<24;node++) {
            MotionPathSample nearest=MotionPath_Project(&path,chain.position[node],0,31);
            float error=MotionVec_Length(MotionVec_Sub(chain.position[node],nearest.position));
            maxError=fmaxf(maxError,error);
            if(node) maxStretch=fmaxf(maxStretch,fabsf(MotionVec_Length(
                MotionVec_Sub(chain.position[node],chain.position[node-1]))-.8f/23));
        }
        Vector3 a=MotionVec_Normalize(MotionVec_Sub(chain.position[0],chain.position[5]));
        Vector3 b=MotionVec_Normalize(MotionVec_Sub(chain.position[18],chain.position[23]));
        maxBend=fmaxf(maxBend,MotionVec_Length(MotionVec_Cross(a,b)));
    }
    printf("Spline free ribbon: max deviation %.5fm, segment error %.5fm, bend %.3f\n",maxError,maxStretch,maxBend);
    assert(maxError<.20f && maxStretch<.01f && maxBend>.15f);
    assert(chain.mode==TRAIL_RIBBON_FREE);
    puts("PASS: free chain transports along curved field with ordered tail, spacing and visible bend");
    return 0;
}
