/* Production registry and solver; no copied physics equations. */
#define main MotionExistingTestsMain
#include "core/tests/motion_fields_test.c"
#undef main
#include <assert.h>

static void TestAutoGuide(void) {
  BodyPhysicalProperties ref=BodyPhysicalProperties_Sphere(.004f,600,.47f);
  GuideTuning tuning=GuideTuning_Derive(&ref,1.2f,3,2,0,9.81f,GUIDE_BALANCED);
  assert(tuning.stiffnessNPerM>0 && tuning.maxForceNewtons>ref.massKg*9.81f);
  assert(tuning.forwardForceNewtons>0 && tuning.settlingTimeSec>0);
  FieldDesc d=MotionField_Default();
  d.lifetime.durationSec=20;d.volume.radiusM=1.2f;d.volume.coreFraction=.5f;
  d.forceLawCount=1;
  d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
    .magnitudeNewtons=tuning.maxForceNewtons,.springStiffnessNPerM=tuning.stiffnessNPerM};
  MediumProperties air={0};
  Vector3 pos={.2f,0,0};
  FieldSample s=Field_EvaluateStep(&d,1,pos,(Vector3){0},&ref,&air,1.0f/120);
  assert(s.forceNewtons.x<0 && MotionVec_Length(s.forceNewtons)<=tuning.maxForceNewtons);
  BodyPhysicalProperties heavy=ref;heavy.massKg*=4;
  FieldSample h=Field_EvaluateStep(&d,1,pos,(Vector3){0},&heavy,&air,1.0f/120);
  assert(-s.forceNewtons.x/ref.massKg > -h.forceNewtons.x/heavy.massKg);
  float endpoints[3];
  for(int rate=30;rate<=120;rate*=2) {
    Vector3 p={.2f,0,0},v={0};float dt=1.0f/rate;
    for(int step=0;step<rate;step++) {
      FieldSample f=Field_EvaluateStep(&d,1,p,v,&ref,&air,dt);
      v=MotionVec_Add(v,MotionVec_Scale(f.forceNewtons,dt/ref.massKg));
      p=MotionVec_Add(p,MotionVec_Scale(v,dt));
      assert(isfinite(p.x) && fabsf(p.x)<=.21f);
    }
    endpoints[rate==30?0:rate==60?1:2]=p.x;
  }
  assert(fabsf(endpoints[0]-endpoints[2])<.01f);
  /* A stiff controller remains dissipative at a coarse step. */
  d.forceLaws[0].magnitudeNewtons=1e6f;
  d.forceLaws[0].springStiffnessNPerM=1e4f;
  Vector3 p={.2f,0,0},v={0};
  MotionFields_Reset();assert(MotionFields_CreateField(&d));
  ReceiverConstraints constraints={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
  for(int i=0;i<20;i++) {
    MotionFields_SampleExternalBodyStep(p,v,&ref,&air,&constraints,.1f,MOTION_RECEIVER_ALL,&s);
    v=MotionVec_Add(v,MotionVec_Scale(s.forceNewtons,.1f/ref.massKg));
    p=MotionVec_Add(p,MotionVec_Scale(v,.1f));
    assert(isfinite(p.x) && fabsf(p.x)<.21f);
  }
}
static Vector3 SimulateBurst(int rate) {
  MotionFields_Reset();
  BodyPhysicalProperties ref=BodyPhysicalProperties_Sphere(.004f,600,.47f);
  GuideTuning t=GuideTuning_Derive(&ref,1.2f,3,4,0,9.81f,GUIDE_BALANCED);
  FieldDesc d=MotionField_Default();
  Vector3 path[]={{0,1,0},{2,1.2f,0},{4,1,0}};
  MotionPath_Build(&d.trajectory.path,path,3);
  d.trajectory.mode=FIELD_TRAJECTORY_PATH;d.trajectory.speedMps=3;
  d.volume.radiusM=1.2f;d.volume.coreFraction=.25f;d.lifetime.durationSec=5;
  d.preserveSphereOffsets=true;
  d.flow.enabled=true;d.flow.addBackgroundVelocity=true;d.flow.axis=(Vector3){1,0,0};
  d.flow.procedural.swirlSpeedMps=4;
  d.forceLawCount=1;d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
    .magnitudeNewtons=t.maxForceNewtons,.springStiffnessNPerM=t.stiffnessNPerM};
  assert(MotionFields_CreateField(&d));
  Vector3 p[32],v[32]={{0}};MotionReceiver receivers[32]={{0}};
  ParticleDynamicsProfile bodies[32];
  ReceiverConstraints constraints={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
  for(int i=0;i<32;i++) {
    float y=1-2*(i+.5f)/32,angle=i*2.39996323f,r=sqrtf(1-y*y);
    p[i]=(Vector3){.45f*y,1+.45f*r*cosf(angle),.45f*r*sinf(angle)};
    float mass=.004f*(.75f+.5f*(i%7)/6);
    BodyPhysicalProperties b=BodyPhysicalProperties_Sphere(mass,600,.47f);
    bodies[i]=(ParticleDynamicsProfile){.inverseMassKg=1/mass,.gravityScale=1,
      .densityKgM3=600,.aerodynamicAreaM2=b.projectedAreaM2,
      .aerodynamicDragCoefficient=.47f,.windSusceptibility=1};
  }
  for(int frame=0;frame<rate*2;frame++) {
    float dt=1.0f/rate;MotionFields_Update(dt);
    for(int i=0;i<32;i++) {
      BodyPhysicalProperties b=MotionBody_GetPhysicalProperties(&bodies[i]);
      MediumProperties m={.densityKgM3=1.225f,.gravityMps2={0,-9.81f,0},.velocityMps={0,0,.5f}};
      float remaining=dt;
      while(remaining>1e-6f) {
        float offset=-remaining,step=fminf(remaining,1.0f/120);remaining-=step;
        FieldSample s;
        MotionFields_SampleBodyAtOffset(p[i],v[i],&b,&m,&constraints,step,offset,
          MOTION_RECEIVER_PARTICLE,&receivers[i],&s);
        v[i]=MotionBody_AdvanceFieldVelocity(v[i],&bodies[i],(Vector3){0},(Vector3){0},&s,m.velocityMps,step);
        p[i]=MotionVec_Add(p[i],MotionVec_Scale(v[i],step));
        assert(Field_FiniteVector(p[i]) && Field_FiniteVector(v[i]));
      }
    }
  }
  Vector3 centroid={0};
  for(int i=0;i<32;i++) {
    centroid=MotionVec_Add(centroid,MotionVec_Scale(p[i],1.0f/32));
    assert(receivers[i].pathLaneFields[0]!=0);
    assert(MotionVec_Length(MotionVec_Sub(p[i],(Vector3){4,1,0}))<1.1f);
  }
  assert(MotionVec_Length(MotionVec_Sub(centroid,(Vector3){4,1,0}))<.4f);
  return centroid;
}
static void TestTemporalSampling(void) {
  MotionFields_Reset();FieldDesc d=MotionField_Default();
  Vector3 points[]={{0,0,0},{10,0,0}};
  MotionPath_Build(&d.trajectory.path,points,2);
  d.trajectory.mode=FIELD_TRAJECTORY_PATH;d.trajectory.speedMps=30;
  d.volume.radiusM=.1f;d.forceLawCount=1;
  d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_ACCELERATION,.accelerationMps2={0,2,0}};
  assert(MotionFields_CreateField(&d));MotionFields_Update(1.0f/30);
  BodyPhysicalProperties b={.massKg=1};MediumProperties m={0};
  ReceiverConstraints c={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};FieldSample s;
  MotionFields_SampleBodyAtOffset((Vector3){0},(Vector3){0},&b,&m,&c,1.0f/120,
      -1.0f/30,MOTION_RECEIVER_ALL,NULL,&s);
  assert(s.accelerationMps2.y==2); /* Earlier support survives current bounds culling. */
  MotionFields_SampleBody((Vector3){0},(Vector3){0},&b,&m,&c,1.0f/120,MOTION_RECEIVER_ALL,NULL,&s);
  assert(s.accelerationMps2.y==0);
  MotionFields_SampleBodyAtOffset((Vector3){0},(Vector3){0},&b,&m,&c,1.0f/120,
      -1,MOTION_RECEIVER_ALL,NULL,&s);
  assert(s.accelerationMps2.y==0);
}
static void TestLightForwardServo(void) {
  FieldDesc d=MotionField_Default();Vector3 points[]={{0,0,0},{10,0,0}};
  d.volume.shape=FIELD_PATH_TUBE;d.volume.radiusM=1;
  MotionPath_Build(&d.volume.path,points,2);
  d.flow.followSpeedMps=3;d.forceLawCount=1;
  d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_PATH_GUIDE,.forwardForceNewtons=.08f};
  BodyPhysicalProperties body={.massKg=1e-6f};MediumProperties m={0};
  Vector3 v={2.999f,0,0};
  for(int i=0;i<30;i++) {
    FieldSample s=Field_EvaluateStep(&d,1,(Vector3){1,0,0},v,&body,&m,1.0f/120);
    v=MotionVec_Add(v,MotionVec_Scale(s.forceNewtons,1/(120*body.massKg)));
    assert(isfinite(v.x) && v.x>=2.999f && v.x<=3.00001f);
  }
}
static void TestRotatingTube(void) {
  MotionFields_Reset();
  BodyPhysicalProperties b=BodyPhysicalProperties_Sphere(.004f,600,.47f);
  GuideTuning tuning=GuideTuning_Derive(&b,1.2f,3,4,0,9.81f,GUIDE_BALANCED);
  FieldDesc d=MotionField_Default();Vector3 points[]={{0,1,0},{6,1,0},{12,1,0}};
  d.volume.shape=FIELD_PATH_TUBE;d.volume.radiusM=1.2f;d.volume.coreFraction=.25f;
  MotionPath_Build(&d.volume.path,points,3);
  d.lifetime.durationSec=10;d.preservePathLanes=true;d.rotatePathLanes=true;
  d.flow.enabled=true;d.flow.addBackgroundVelocity=true;d.flow.followSpeedMps=3;
  d.flow.procedural.swirlSpeedMps=4;d.forceLawCount=1;
  d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_PATH_GUIDE,
    .magnitudeNewtons=tuning.maxForceNewtons,.springStiffnessNPerM=tuning.stiffnessNPerM,
    .forwardForceNewtons=tuning.forwardForceNewtons};
  assert(MotionFields_CreateField(&d));
  ParticleDynamicsProfile body={.inverseMassKg=250,.densityKgM3=600,.gravityScale=1,
    .aerodynamicAreaM2=b.projectedAreaM2,.aerodynamicDragCoefficient=.47f,.windSusceptibility=1};
  MediumProperties m={.densityKgM3=1.225f,.gravityMps2={0,-9.81f,0},.velocityMps={0,0,.5f}};
  ReceiverConstraints c={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
  MotionReceiver r={0};Vector3 p={.1f,1.3f,0},v={0};float dt=1.0f/120;
  for(int i=0;i<240;i++) {
    MotionFields_Update(dt);FieldSample s;
    MotionFields_SampleBodyAtOffset(p,v,&b,&m,&c,dt,-dt,MOTION_RECEIVER_ALL,&r,&s);
    v=MotionBody_AdvanceFieldVelocity(v,&body,(Vector3){0},(Vector3){0},&s,m.velocityMps,dt);
    p=MotionVec_Add(p,MotionVec_Scale(v,dt));
    assert(Field_FiniteVector(p) && fabsf(p.y-1)<.8f && fabsf(p.z)<.8f);
  }
  assert(p.x>5 && p.x<6.3f && r.pathLaneFields[0]!=0);
  assert(p.z>.05f); /* Rotates away from its original z=0 lane. */
}
static float TurbulenceDisplacement(float turbulence, int second, bool actuator) {
 MotionFields_Reset();
 BodyPhysicalProperties b=BodyPhysicalProperties_Sphere(.004f,600,.47f);
 GuideTuning t=GuideTuning_Derive(&b,1.2f,3,0,turbulence,9.81f,GUIDE_BALANCED);
 FieldDesc d=MotionField_Default();d.volume.radiusM=1.2f;d.volume.coreFraction=.25f;
 d.lifetime.durationSec=10;d.preserveSphereOffsets=true;d.flow.enabled=true;
 d.flow.procedural.turbulenceSpeedMps=actuator?0:turbulence;d.flow.procedural.eddyLengthM=.36f;
 d.forceLawCount=1;d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
 .magnitudeNewtons=t.maxForceNewtons,.springStiffnessNPerM=t.stiffnessNPerM};
 if(actuator && turbulence>0)
   d.forceLaws[d.forceLawCount++]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,
     .magnitudeNewtons=t.turbulenceForceNewtons,
     .procedural={.turbulenceSpeedMps=t.turbulenceSpeedMps,.eddyLengthM=t.eddyLengthM}};
 assert(MotionFields_CreateField(&d));
 ParticleDynamicsProfile body={.inverseMassKg=250,.densityKgM3=600,.gravityScale=0,
 .aerodynamicAreaM2=b.projectedAreaM2,.aerodynamicDragCoefficient=.47f,.windSusceptibility=1};
 MediumProperties m={.densityKgM3=1.225f};ReceiverConstraints c={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
 Vector3 p[16],v[16]={{0}},start[16];MotionReceiver receivers[16]={{0}};
 for(int j=0;j<16;j++)start[j]=p[j]=(Vector3){.2f*cosf(j*2.4f),.2f*sinf(j*2.4f),.01f*j};
 float sum=0;int n=0;float dt=1.f/120;
 for(int i=0;i<240;i++){
 if(second && i==60){d.transform.position=(Vector3){.3f,0,0};assert(MotionFields_CreateField(&d));}
 MotionFields_Update(dt);
 for(int j=0;j<16;j++){
 FieldSample s;MotionFields_SampleBodyAtOffset(p[j],v[j],&b,&m,&c,dt,-dt,MOTION_RECEIVER_ALL,&receivers[j],&s);
 v[j]=MotionBody_AdvanceFieldVelocity(v[j],&body,(Vector3){0},(Vector3){0},&s,m.velocityMps,dt);
 p[j]=MotionVec_Add(p[j],MotionVec_Scale(v[j],dt));
 if(i>=120){Vector3 e=MotionVec_Sub(p[j],start[j]);sum+=MotionVec_Dot(e,e);n++;}
 }}return sqrtf(sum/n);
}
static void TestTurbulenceResponse(void) {
 /* Reference 4 g body, 600 kg/m3, physical quadratic drag; no gravity or swirl
  * so measured displacement is caused by curl airflow alone. */
 float quiet=TurbulenceDisplacement(0,0,false);
 float low=TurbulenceDisplacement(4,0,false),high=TurbulenceDisplacement(16,0,false);
 float overlapping=TurbulenceDisplacement(16,1,false);
 assert(quiet<1e-6f && low>.005f);
 assert(high>4*low && high<.2f);
 /* A single cast must respond without requiring another field. Compatible
  * overlapping media average their velocity, rather than doubling turbulence. */
 assert(overlapping>.01f && overlapping<high);
 printf("Curl-only RMS: 4 m/s %.4f m; 16 m/s %.4f m; overlapping %.4f m\n",low,high,overlapping);
}
static void TestGuidedTurbulenceShape(void) {
  float quiet=TurbulenceDisplacement(0,0,true);
  float medium=TurbulenceDisplacement(4,0,true);
  float strong=TurbulenceDisplacement(8,0,true);
  /* Joint guidance no longer amplifies the external curl kick through the
   * old split. Still require a visible, monotonic deformation of the cloud. */
  assert(quiet<1e-6f && medium>.08f && strong>medium && strong<.5f);
  BodyPhysicalProperties body={.massKg=.004f};
  GuideTuning a=GuideTuning_Derive(&body,1.2f,3,0,4,9.81f,GUIDE_BALANCED);
  GuideTuning b=GuideTuning_Derive(&body,1.2f,3,0,80,9.81f,GUIDE_BALANCED);
  assert(a.stiffnessNPerM==b.stiffnessNPerM &&
      b.turbulenceForceNewtons<=b.maxForceNewtons*.250001f);
  printf("Guided curl force RMS: 4 m/s %.4f m; 8 m/s %.4f m\n",medium,strong);
}
static void TestCurlForce(void) {
  MotionFields_Reset();
  FieldDesc d=MotionField_Default();
  d.volume.radiusM=1.2f;d.volume.coreFraction=.25f;
  d.lifetime.durationSec=2;d.lifetime.fadeSec=.5f;
  d.forceLawCount=1;
  d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,.magnitudeNewtons=.2f,
    .procedural={.turbulenceSpeedMps=4,.eddyLengthM=.36f}};
  assert(MotionFields_CreateField(&d));
  BodyPhysicalProperties light={.massKg=.004f},heavy={.massKg=.016f};
  MediumProperties vacuum={0};Vector3 p={.13f,.07f,.19f};
  FieldSample a=Field_EvaluateStep(&d,.37f,p,(Vector3){0},&light,&vacuum,1.f/120);
  FieldSample b=Field_EvaluateStep(&d,.37f,p,(Vector3){0},&heavy,&vacuum,1.f/120);
  float force=MotionVec_Length(a.forceNewtons);
  assert(force>.01f && force<=.200001f && a.mediumWeight==0);
  assert(MotionVec_Length(MotionVec_Sub(a.forceNewtons,b.forceNewtons))<1e-6f);
  ParticleDynamicsProfile body={.inverseMassKg=1/light.massKg};
  Vector3 vl=MotionBody_AdvanceFieldVelocity((Vector3){0},&body,(Vector3){0},
      (Vector3){0},&a,(Vector3){0},1.f/120);
  body.inverseMassKg=1/heavy.massKg;
  Vector3 vh=MotionBody_AdvanceFieldVelocity((Vector3){0},&body,(Vector3){0},
      (Vector3){0},&b,(Vector3){0},1.f/120);
  assert(MotionVec_Length(MotionVec_Sub(vl,MotionVec_Scale(vh,4)))<1e-6f);
  a=Field_Evaluate(&d,1.75f,p,(Vector3){0},&light,&vacuum);
  d.lifetime.fadeSec=0;
  b=Field_Evaluate(&d,1.75f,p,(Vector3){0},&light,&vacuum);
  assert(MotionVec_Length(MotionVec_Sub(a.forceNewtons,MotionVec_Scale(b.forceNewtons,.5f)))<1e-6f);
  a=Field_Evaluate(&d,.37f,(Vector3){2,0,0},(Vector3){0},&light,&vacuum);
  assert(MotionVec_Length(a.forceNewtons)==0);
  d.forceLaws[0].procedural.turbulenceSpeedMps=0;
  a=Field_Evaluate(&d,.37f,p,(Vector3){0},&light,&vacuum);
  assert(MotionVec_Length(a.forceNewtons)==0);
  d.forceLaws[0].procedural.turbulenceSpeedMps=NAN;
  assert(!MotionFields_CreateField(&d));
  puts("PASS: curl actuator in vacuum, fixed Newton budget, inverse-mass response, fade, support and zero/invalid amplitude");
}
static void TestCloudCoherence(void) {
  BodyPhysicalProperties body={.massKg=.004f};
  GuideTuning t=GuideTuning_Derive(&body,1.2f,3,4,8,9.81f,GUIDE_BALANCED);
  FieldDesc d=MotionField_Default();
  d.volume.radiusM=1.2f;d.volume.coreFraction=.25f;
  d.lifetime.durationSec=10;d.forceLawCount=1;
  d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_CURL_FORCE,
    .magnitudeNewtons=t.turbulenceForceNewtons,
    .procedural={.turbulenceSpeedMps=t.turbulenceSpeedMps,.eddyLengthM=t.eddyLengthM}};
  MediumProperties medium={0};
  float correlation=0,differenceEnergy=0,totalEnergy=0;int pairs=0;
  /* Nearby cloud members should participate in the same roll. Sample many
   * locations and phases so a coincidental direction cannot satisfy this. */
  for(int i=0;i<32;i++) for(int j=0;j<32;j++) {
    float angle=i*2.399963f,time=.1f+j*.073f;
    Vector3 p={.3f*cosf(angle),.3f*sinf(angle),.2f*sinf(i*.73f)};
    Vector3 q=MotionVec_Add(p,(Vector3){.08f,0,0});
    Vector3 u=Field_Evaluate(&d,time,p,(Vector3){0},&body,&medium).forceNewtons;
    Vector3 v=Field_Evaluate(&d,time,q,(Vector3){0},&body,&medium).forceNewtons;
    float product=MotionVec_Length(u)*MotionVec_Length(v);
    if(product>1e-10f) {correlation+=MotionVec_Dot(u,v)/product;pairs++;}
    Vector3 difference=MotionVec_Sub(u,v);
    differenceEnergy+=MotionVec_Dot(difference,difference);
    totalEnergy+=MotionVec_Dot(u,u)+MotionVec_Dot(v,v);
  }
  assert(pairs>0 && totalEnergy>0);
  assert(correlation/pairs>.9f && sqrtf(differenceEnergy/totalEnergy)<.3f);
  printf("Cloud neighbor correlation %.5f; relative difference %.5f\n",
      correlation/pairs,sqrtf(differenceEnergy/totalEnergy));
}

int main(void) {
  TestCloudCoherence();
  TestCurlForce();
  TestGuidedTurbulenceShape();
  TestTurbulenceResponse();
  TestAutoGuide();
  TestLightForwardServo();
  TestTemporalSampling();
  TestRotatingTube();
  Vector3 a=SimulateBurst(30),b=SimulateBurst(60),c=SimulateBurst(120);
  assert(MotionVec_Length(MotionVec_Sub(a,c))<.03f && MotionVec_Length(MotionVec_Sub(b,c))<.03f);
  puts("PASS: derived budgets, mass response, stiff stability, temporal support and 32-body curved burst at 30/60/120 Hz");
  return 0;
}
