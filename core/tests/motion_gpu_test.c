/* CPU references and production std430 packs for the renderer's real GPU test. */
#define main ExistingMotionTestsMain
#include "core/tests/motion_fields_test.c"
#undef main
#include "core/motion/motion_gpu.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define GPU_TEST_COUNT 8192
static int fixtureCount=32;
static float fixtureDt=.1f;
static float particles[GPU_TEST_COUNT][36], expected[GPU_TEST_COUNT][36];
static MotionGpuBody bodies[GPU_TEST_COUNT];
static MotionGpuScene scene;
static Vector4 windBytes[3+3*MAX_VORTICLES+3], terrainBytes[514], unusedBytes[2048];
static void WriteBytes(const char *dir,const char *name,const void *data,size_t size) {
  char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,name);
  FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,size,f)==size);fclose(f);
}
static void Fixture(int which,const char *dir) {
  MotionFields_Reset();Wind_Init();WindZone_Clear();
  WindMacroConfig stillAir={0};Wind_SetMacro(&stillAir);
  ParticleDynamicsProfile profile={.inverseMassKg=250,.gravityScale=1,
    .linearDragPerSecond=.2f,.windCouplingHz=3.5f,.windSusceptibility=.7f};
  FieldDesc d=MotionField_Default();d.volume.radiusM=3;d.volume.coreFraction=.7f;
  d.lifetime.durationSec=10;
  d.forceLawCount=1;d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
    .springStiffnessNPerM=.8f,.magnitudeNewtons=.4f};
  d.flow.enabled=true;d.flow.velocityMps=(Vector3){3,1,0};
  d.preserveSphereOffsets=true;
  if(which==0) d.forceLawCount=0;
  if(which==1) {
    Vector3 route[]={{0,0,0},{1,.4f,.3f},{2,0,1},{4,1,0}};
    MotionPath_Build(&d.trajectory.path,route,4);d.trajectory.mode=FIELD_TRAJECTORY_PATH;
    d.trajectory.speedMps=12;d.flow.procedural=(MotionFlowDesc){.turbulenceSpeedMps=4,.swirlSpeedMps=5,.eddyLengthM=3};
  }
  if(which==2) {
    Vector3 route[]={{-4,-2,-1},{0,0,0},{2,2,3},{4,1,5}};
    MotionPath_Build(&d.volume.path,route,4);d.volume.shape=FIELD_PATH_TUBE;
    d.preservePathLanes=true;d.rotatePathLanes=true;d.flow.followSpeedMps=14;
    d.forceLaws[0].type=FORCE_LAW_PATH_GUIDE;d.forceLaws[0].forwardForceNewtons=.3f;
    d.flow.procedural.swirlSpeedMps=4;
  }
  if(which==3) {
    d.forceLaws[0].type=FORCE_LAW_SPRING;
    d.volume.shape=FIELD_BOX;d.volume.halfExtentsM=(Vector3){3,2,4};
    d.transform.axisX=(Vector3){.70710678f,.70710678f,0};
    d.transform.axisY=(Vector3){-.70710678f,.70710678f,0};
    profile.aerodynamicAreaM2=.01f;profile.aerodynamicDragCoefficient=.8f;
    profile.densityKgM3=500;
    d.forceLaws[1]=(ForceLaw){.type=FORCE_LAW_DRAG};d.forceLawCount=2;
  }
  if(which==4) {
    d.forceLaws[0].magnitudeNewtons=.015f;
    MotionFields_CreateField(&d);d.transform.position=(Vector3){.3f,.5f,.2f};
    d.volume.shape=FIELD_CAPSULE;d.volume.capsuleStart=(Vector3){0,-2,0};d.volume.capsuleEnd=(Vector3){0,2,0};
    d.forceLaws[0].type=FORCE_LAW_SPRING;
  }
  if(which==5) { WindZone_Set((Vector3){1,.2f,.3f},3,2,.7f);profile.windAccelerationScale=1; }
  if(which==6) {
    d.flow.addBackgroundVelocity=true;
    WindMacroConfig macro={.baseDirection={2,.1f,1},.gustAmplitude=.8f,.noiseScale=.14f,.noiseSpeed=1};
    Wind_SetMacro(&macro);Wind_SpawnGust((Vector3){0},(Vector3){1,0,0},4,3,2);
    Wind_TriggerGuidingWind((Vector3){-1,0,0},(Vector3){10,1,3},12,2);Wind_Update(.2f);
    VorticleData published={.active=true,.position={0},.direction={0,0,1},.radius=4,
      .strength=100,.type=VORTICLE_LINEAR_GUST,.lifetime=1,.maxLifetime=1};
    Wind_SetMotionAirflow(&published,1);
  }
  if(which==7) {d.flow.procedural.turbulenceSpeedMps=4;d.flow.procedural.eddyLengthM=3;}
  if(which==9) {d.volume.radiusM=30;d.flow.procedural=(MotionFlowDesc){.turbulenceSpeedMps=4,.swirlSpeedMps=5,.eddyLengthM=3};}
  if(which==11) {
    d.flow.procedural=(MotionFlowDesc){.turbulenceSpeedMps=4,.swirlSpeedMps=5,.eddyLengthM=3};
    for(int k=0;k<4;k++) assert(MotionFields_CreateField(&d));
  }
  if(which==10) {
    Vector3 route[33];
    for(int k=0;k<33;k++) {float t=(float)k/32;route[k]=(Vector3){12*t,2*sinf(t*3.14159265f),3*sinf(t*3.14159265f)};}
    assert(MotionPath_Build(&d.volume.path,route,33));d.volume.shape=FIELD_PATH_TUBE;
    d.volume.radiusM=1.2f;d.volume.coreFraction=.25f;d.preservePathLanes=true;
    d.rotatePathLanes=true;d.flow.followSpeedMps=24;d.flow.velocityMps=(Vector3){0};
    d.flow.procedural.swirlSpeedMps=12;
    d.forceLaws[0].type=FORCE_LAW_PATH_GUIDE;d.forceLaws[0].forwardForceNewtons=.8f;
    d.forceLaws[1]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,.magnitudeNewtons=.04f,
      .procedural={.turbulenceSpeedMps=8,.eddyLengthM=1.2f}};d.forceLawCount=2;
    for(int k=0;k<4;k++) assert(MotionFields_CreateField(&d));
  }
  assert(MotionFields_CreateField(&d));
  const float dt=fixtureDt;MotionFields_Update(dt);MotionFields_PackGpu(&scene);
  assert(scene.meta[1]==MOTION_GPU_ABI_VERSION);
  memset(windBytes,0,sizeof(windBytes));
  WindMacroConfig macro=Wind_GetMacro();int n=0;const VorticleData *v=Wind_GetActiveVorticles(&n);
  windBytes[0]=MotionGpu_V4(macro.baseDirection,macro.gustAmplitude);
  windBytes[1]=(Vector4){macro.noiseScale,macro.noiseSpeed,n,macro.terrainLiftK};
  windBytes[2]=(Vector4){macro.heightGradientK,Wind_GetPublishedMotionAirflowCount(),0,0};
  for(int k=0;k<n;k++) {
    windBytes[3+k*3]=MotionGpu_V4(v[k].position,v[k].radius);
    windBytes[4+k*3]=MotionGpu_V4(v[k].direction,v[k].strength);
    windBytes[5+k*3]=(Vector4){v[k].type,v[k].lifetime,v[k].maxLifetime,v[k].inwardPull};
  }
  WindGuidingGust guiding=Wind_GetGuidingWindState();
  windBytes[3+3*MAX_VORTICLES]=MotionGpu_V4(guiding.playerPos,guiding.active?1:0);
  windBytes[4+3*MAX_VORTICLES]=MotionGpu_V4(guiding.targetPos,guiding.intensity);
  windBytes[5+3*MAX_VORTICLES]=MotionGpu_V4(guiding.direction,guiding.speed);
  Vector3 acceleration={.3f,.4f,.2f},force={.001f,-.002f,.003f};
  BodyPhysicalProperties physical=MotionBody_GetPhysicalProperties(&profile);
  ReceiverConstraints constraints={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
  memset(particles,0,sizeof(particles));memset(bodies,0,sizeof(bodies));
  clock_t referenceStart=clock();
  for(int i=0;i<fixtureCount;i++) {
    int seed=i%32;
    Vector3 position={((seed%4)-1.5f)*.2f,((seed/4%4)-1.5f)*.2f,(seed/16-.5f)*.3f};
    if(which==7) position.x+=2.6f;
    Vector3 velocity={1,.2f,-.3f};float *p=particles[i];
    p[0]=position.x;p[1]=position.y;p[2]=position.z;p[3]=.02f;
    p[4]=velocity.x;p[5]=velocity.y;p[6]=velocity.z;
    p[16]=10;p[17]=10;p[19]=1;p[20]=-1;p[22]=which==8?.4f:-1;p[26]=-1;
    bodies[i]=MotionGpu_PackBody(&profile,acceleration,force,true,0);
    memcpy(expected[i],p,sizeof(particles[i]));expected[i][16]-=dt;
    MotionReceiver receiver={0};float remaining=dt;
    while(remaining>1e-6f) {
      float offset=-remaining,step=fminf(remaining,1.f/120);remaining-=step;
      MediumProperties medium=Wind_EvaluateBackgroundMedium(position,dt+offset,(Vector3){0,-9.81f,0});
      FieldSample sample;MotionFields_SampleSpatialBodyAtOffset(position,velocity,&physical,&medium,
        &constraints,step,offset,MOTION_RECEIVER_PARTICLE,&receiver,&sample);
      Vector3 a=acceleration;
      if(which==5) a=MotionVec_Add(a,MotionVec_Scale(WindZone_Evaluate(position,velocity,dt+offset),profile.windSusceptibility));
      velocity=MotionBody_AdvanceFieldVelocity(velocity,&profile,a,force,&sample,medium.velocityMps,step);
      position=MotionVec_Add(position,MotionVec_Scale(velocity,step));
    }
    assert(Field_FiniteVector(position)&&Field_FiniteVector(velocity));
    if(which==8 && position.y<=0) {position.y=.005f;velocity.y=-velocity.y*.4f;velocity.x*=.75f;velocity.z*=.75f;}
    expected[i][0]=position.x;expected[i][1]=position.y;expected[i][2]=position.z;
    expected[i][4]=velocity.x;expected[i][5]=velocity.y;expected[i][6]=velocity.z;
  }
  if(fixtureCount>32) printf("CPU reference integration: %d particles %.3f ms (CPU clock; includes fixture preparation)\n",
      fixtureCount,1000.0*(clock()-referenceStart)/CLOCKS_PER_SEC);
  if(dir) {
    WriteBytes(dir,"ssbo0.bin",particles,fixtureCount*sizeof(particles[0]));
    WriteBytes(dir,"ssbo1.bin",unusedBytes,sizeof(unusedBytes));
    WriteBytes(dir,"ssbo2.bin",unusedBytes,sizeof(unusedBytes));
    WriteBytes(dir,"ssbo3.bin",windBytes,sizeof(windBytes));
    WriteBytes(dir,"ssbo4.bin",terrainBytes,sizeof(terrainBytes));
    WriteBytes(dir,"ssbo5.bin",&scene,sizeof(scene));
    WriteBytes(dir,"ssbo6.bin",bodies,fixtureCount*sizeof(bodies[0]));
    WriteBytes(dir,"expected0.bin",expected,fixtureCount*sizeof(expected[0]));
    char path[1024];snprintf(path,sizeof(path),"%s/config.txt",dir);
    FILE *f=fopen(path,"w");assert(f);fprintf(f,"%d 1 %.9g %.9g .002\n",fixtureCount,dt,dt);fclose(f);
  }
}
int main(int argc,char **argv) {
  FieldDesc inactive=MotionField_Default();
  inactive.volume.path.count=100000;inactive.trajectory.path.count=100000;
  MotionGpuField packed;MotionGpu_PackField(&inactive,1,0,&packed);
  assert(packed.halfExtents.w==0 && packed.axisX.w==0);
  if(argc>=3) {
    if(argc>=4) {fixtureCount=atoi(argv[3]);assert(fixtureCount>0&&fixtureCount<=GPU_TEST_COUNT);fixtureDt=1.f/60;}
    Fixture(atoi(argv[2]),argv[1]);
  }
  else for(int i=0;i<12;i++) Fixture(i,NULL);
  puts("PASS: production Motion GPU layouts and finite CPU reference fixtures");return 0;
}
