/* Production CPU solver/field references for actual Vulkan dispatch tests. */
#define main ExistingMotionTestsMain
#include "core/tests/motion_fields_test.c"
#undef main
typedef struct Camera3D {Vector3 position,target,up;float fovy;int projection;} Camera3D;
#include "core/trails/trail_ribbon_gpu.h"
#include "core/motion/motion_wind_gpu.h"
#include <assert.h>
#include <stdlib.h>

static TrailRibbonMaterial material;
static MotionReceiver receivers[TRAIL_RIBBON_MAX_NODES];
static MotionGpuScene scene;
static void Sample(void *user,Vector3 p,Vector3 v,float dt,int node,float offset,FieldSample *s,Vector3 *air) {
    (void)user;
    BodyPhysicalProperties physical=MotionBody_GetPhysicalProperties(&material.body);
    MediumProperties medium=Wind_EvaluateBackgroundMedium(p,1.f/60+offset,
        (Vector3){0,-9.81f*material.body.gravityScale,0});
    if(material.body.airDensityKgM3>0) medium.densityKgM3=material.body.airDensityKgM3;
    ReceiverConstraints constraints={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
    MotionFields_SampleSpatialBodyAtOffset(p,v,&physical,&medium,&constraints,dt,offset,
        MOTION_RECEIVER_TRAIL,&receivers[node],s);
    *air=medium.velocityMps;
    if(WindZone_IsActive()) s->accelerationMps2=MotionVec_Add(s->accelerationMps2,
        MotionVec_Scale(WindZone_Evaluate(p,v,1.f/60+offset),
            material.body.windAccelerationScale*material.body.windSusceptibility));
}
static void Bytes(const char *dir,const char *name,const void *p,size_t size) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(p,1,size,f)==size);fclose(f);
}
static void Fixture(int which,const char *dir) {
    MotionFields_Reset();Wind_Init();WindZone_Clear();memset(receivers,0,sizeof(receivers));
    WindMacroConfig macro={0};Wind_SetMacro(&macro);
    material=TrailRibbonMaterial_Default();material.body.linearDragPerSecond=.2f;
    material.body.windSusceptibility=.7f;material.body.windCouplingHz=3.5f;
    FieldDesc field=MotionField_Default();field.lifetime.durationSec=10;
    field.volume.radiusM=5;field.volume.coreFraction=.8f;
    if(which>=2) {
        field.flow.enabled=true;field.flow.velocityMps=(Vector3){2,1,.3f};
        field.flow.procedural=(MotionFlowDesc){.swirlSpeedMps=2,.turbulenceSpeedMps=1,.eddyLengthM=1};
        field.forceLawCount=1;field.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
            .springStiffnessNPerM=.8f,.magnitudeNewtons=.3f};
        field.preserveSphereOffsets=true;
        if(which==9) {
            Vector3 points[]={{0,0,0},{.5f,.7f,.2f},{1.5f,.8f,.5f},{2.5f,.2f,0}};
            assert(MotionPath_Build(&field.volume.path,points,4));
            field.volume.shape=FIELD_PATH_TUBE;
            field.flow.followSpeedMps=2;
            field.forceLaws[0].type=FORCE_LAW_PATH_GUIDE;
            field.forceLaws[0].forwardForceNewtons=.05f;
            field.preservePathLanes=true;
            field.forceLawCount=2;
            field.forceLaws[1]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,.magnitudeNewtons=.002f,
                .procedural={.turbulenceSpeedMps=.8f,.eddyLengthM=.4f}};
        }
        assert(MotionFields_CreateField(&field));
    }
    if(which==3) {
        macro.baseDirection=(Vector3){1,.2f,.3f};macro.gustAmplitude=.2f;macro.noiseScale=.3f;
        Wind_SetMacro(&macro);Wind_SpawnGust((Vector3){0},(Vector3){0,0,1},5,2,2);
    }
    if(which==4) {material.stretchCompliance=.0001f;material.bendCompliance=.01f;}
    if(which==5) {material.body.inverseMassKg=25;WindZone_Set((Vector3){1,0,0},3,.2f,.4f);material.body.windAccelerationScale=1;}
    TrailRibbonState state;
    bool pinned=which==1||which==4||which==7;
    assert(TrailRibbon_Initialize(&state,which==8?60:which==9?24:16,(Vector3){0,1,0},(Vector3){0,-1,.1f},1,
        (Vector3){.3f,.2f,0},pinned?TRAIL_RIBBON_HEAD_ANCHORED:TRAIL_RIBBON_FREE));
    TrailRibbonAnchor anchor={.previousPosition={0,1,0},.position={.02f,1.01f,0},
        .velocity={1.2f,.6f,0},.valid=pinned};
    if(which==7) {anchor.discontinuity=true;anchor.position.x=10;anchor.velocity=(Vector3){0};}
    TrailRibbonGpuNode nodes[TRAIL_RIBBON_MAX_NODES]={0},expected[TRAIL_RIBBON_MAX_NODES]={0};
    MotionGpuBody bodies[TRAIL_RIBBON_MAX_NODES]={0};
    TrailRibbonGpuParams params={0};params.meta[0]=state.count;params.meta[1]=material.constraintIterations;
    params.meta[2]=pinned;params.meta[3]=1;
    params.compliance=(Vector4){material.stretchCompliance,material.bendCompliance,0,0};
    params.anchor=MotionGpu_V4(anchor.position,pinned?1:0);
    params.anchorVelocity=MotionGpu_V4(anchor.velocity,anchor.discontinuity?1:0);
    if(which==6) {params.compliance.z=1;state.velocity[0]=anchor.velocity;}
    for(int i=0;i<state.count;i++) {
        nodes[i].positionRest=MotionGpu_V4(state.position[i],state.restLength[i]);
        nodes[i].velocity=MotionGpu_V4(which==6&&i==0?(Vector3){.3f,.2f,0}:state.velocity[i],0);
        nodes[i].previous=MotionGpu_V4(state.position[i],0);
        bodies[i]=MotionGpu_PackBodyForReceiver(&material.body,(Vector3){0},(Vector3){0},true,0,MOTION_RECEIVER_TRAIL);
    }
    MotionFields_Update(1.f/60);MotionFields_PackGpu(&scene);
    WindGPU wind;MotionGpu_PackWind(&wind);
    WindTerrainGPU terrain;MotionGpu_PackWindTerrain(Wind_GetTerrainGrid(),&terrain);
    assert(TrailRibbon_Advance(&state,&material,1.f/60,&anchor,Sample,NULL)==2);
    for(int i=0;i<state.count;i++) {
        assert(Field_FiniteVector(state.position[i])&&Field_FiniteVector(state.velocity[i]));
        expected[i].positionRest=MotionGpu_V4(state.position[i],state.restLength[i]);
        expected[i].velocity=MotionGpu_V4(state.velocity[i],i==0?state.accumulator:0);
        expected[i].previous=MotionGpu_V4(state.previous[i],0);
    }
    if(dir) {
        Bytes(dir,"ssbo0.bin",nodes,sizeof(nodes));Bytes(dir,"ssbo1.bin",&params,sizeof(params));
        Bytes(dir,"ssbo3.bin",&wind,sizeof(wind));Bytes(dir,"ssbo4.bin",&terrain,sizeof(terrain));
        Bytes(dir,"ssbo5.bin",&scene,sizeof(scene));Bytes(dir,"ssbo6.bin",bodies,sizeof(bodies));
        Bytes(dir,"expected0.bin",expected,sizeof(expected));
        char path[1024];snprintf(path,sizeof(path),"%s/config.txt",dir);
        FILE *f=fopen(path,"w");assert(f);fprintf(f,"1 1 %.9g %.9g .003\n",1.f/60,1.f/60);fclose(f);
    }
}
static void TransportFixture(int which,const char *dir) {
    MotionFields_Reset();Wind_Init();WindZone_Clear();
    WindMacroConfig macro={0};Wind_SetMacro(&macro);
    FieldDesc field=MotionField_Default();field.lifetime.durationSec=10;
    field.volume.shape=FIELD_PATH_TUBE;field.volume.radiusM=2;
    Vector3 points[]={{0,0,0},{.5f,.7f,.2f},{1.5f,.8f,.5f},{2.5f,.2f,0}};
    assert(MotionPath_Build(&field.volume.path,points,4));
    field.transform.position=(Vector3){3,2,-1};
    MotionFieldHandle handle=MotionFields_CreateField(&field);assert(handle);
    MotionPathTransportSnapshot view;assert(MotionFields_GetPathTransport(handle,&view));
    TrailRibbonGpuNode nodes[TRAIL_RIBBON_MAX_NODES]={0},expected[TRAIL_RIBBON_MAX_NODES]={0};
    MotionGpuBody bodies[TRAIL_RIBBON_MAX_NODES]={0};
    TrailRibbonGpuParams params={.meta={24,handle,0,3},.compliance={0,0,0,1.75f},.anchor={0,.13f,-.17f,0}};
    for(int i=0;i<24;i++) {
        Vector3 old={0,1,0};
        Vector3 q=MotionPathTransport_Sample(&view,params.compliance.w-i*.08f,(Vector3){0,.13f,-.17f});
        nodes[i].positionRest=MotionGpu_V4(old,i?.08f:0);nodes[i].previous=MotionGpu_V4(old,0);
        expected[i].positionRest=MotionGpu_V4(q,i?.08f:0);
        expected[i].velocity=MotionGpu_V4(MotionVec_Scale(MotionVec_Sub(q,old),60),0);
        expected[i].previous=MotionGpu_V4(old,0);
    }
    if(which==11) {MotionFields_Stop(handle);memcpy(expected,nodes,sizeof(nodes));}
    MotionFields_PackGpu(&scene);
    WindGPU wind;MotionGpu_PackWind(&wind);
    WindTerrainGPU terrain;MotionGpu_PackWindTerrain(Wind_GetTerrainGrid(),&terrain);
    if(dir) {
        Bytes(dir,"ssbo0.bin",nodes,sizeof(nodes));Bytes(dir,"ssbo1.bin",&params,sizeof(params));
        Bytes(dir,"ssbo3.bin",&wind,sizeof(wind));Bytes(dir,"ssbo4.bin",&terrain,sizeof(terrain));
        Bytes(dir,"ssbo5.bin",&scene,sizeof(scene));Bytes(dir,"ssbo6.bin",bodies,sizeof(bodies));
        Bytes(dir,"expected0.bin",expected,sizeof(expected));
        char path[1024];snprintf(path,sizeof(path),"%s/config.txt",dir);
        FILE *f=fopen(path,"w");assert(f);fprintf(f,"1 1 %.9g %.9g .0001\n",1.f/60,1.f/60);fclose(f);
    }
}
int main(int argc,char **argv) {
    if(argc>=3) {int which=atoi(argv[2]);if(which>=10) TransportFixture(which,argv[1]);else Fixture(which,argv[1]);}
    else {for(int i=0;i<10;i++) Fixture(i,NULL);TransportFixture(10,NULL);TransportFixture(11,NULL);}
    puts("PASS: production ribbon CPU references and GPU ABI fixtures (execution requires renderer harness)");
    return 0;
}
