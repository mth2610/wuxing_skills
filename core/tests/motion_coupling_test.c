/* Production sampler/integrator: external forces must share the guide solve. */
#define main MotionExistingTestsMain
#include "core/tests/motion_fields_test.c"
#undef main
#include <assert.h>

static float SettleGuide(int rate, int copies, float mass, float forceCap) {
  BodyPhysicalProperties physical={.massKg=mass};
  ParticleDynamicsProfile body={.inverseMassKg=1/mass,.gravityScale=1};
  FieldDesc d=MotionField_Default();
  d.volume.radiusM=10;d.volume.coreFraction=.9f;d.lifetime.durationSec=100;
  d.forceLawCount=1;
  d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
    .springStiffnessNPerM=.4f,.magnitudeNewtons=forceCap};
  MediumProperties medium={0};
  Vector3 position={0},velocity={0};float dt=1.0f/rate;
  for(int i=0;i<rate*4;i++) {
    FieldSample sample={0};
    for(int j=0;j<copies;j++) {
      FieldSample one=Field_EvaluateStep(&d,1,position,velocity,&physical,&medium,dt);
      FieldSample_Combine(&sample,&one);
    }
    Vector3 before=velocity;
    velocity=MotionBody_AdvanceFieldVelocity(velocity,&body,(Vector3){0},
        (Vector3){0},&sample,(Vector3){0},dt);
    /* Guidance must respect the authored total force budget even when saturated. */
    float guideForce=(velocity.y-before.y)/dt*mass+mass*9.81f;
    assert(fabsf(guideForce)<=copies*forceCap+1e-4f);
    position=MotionVec_Add(position,MotionVec_Scale(velocity,dt));
    assert(Field_FiniteVector(position));
  }
  return position.y;
}

static void TestTubeForceLimits(void) {
  FieldDesc d=MotionField_Default();
  Vector3 points[]={{-20,-20,-20},{20,20,20}};
  assert(MotionPath_Build(&d.volume.path,points,2));
  d.volume.shape=FIELD_PATH_TUBE;d.volume.radiusM=10;d.volume.coreFraction=.9f;
  d.lifetime.durationSec=100;d.flow.followSpeedMps=1;
  d.forceLawCount=1;d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_PATH_GUIDE,
    .springStiffnessNPerM=.4f,.magnitudeNewtons=.03f,.forwardForceNewtons=.02f};
  BodyPhysicalProperties physical={.massKg=.004f};
  ParticleDynamicsProfile body={.inverseMassKg=250};
  MediumProperties medium={0};
  Vector3 tangent=MotionVec_Normalize((Vector3){1,1,1});
  Vector3 normal=MotionVec_Normalize((Vector3){1,-1,0});
  Vector3 force=MotionVec_Scale(normal,.01f);
  Vector3 position=MotionVec_Scale(normal,.5f),velocity=MotionVec_Scale(tangent,3);
  float dt=1.f/30;
  for(int i=0;i<120;i++) {
    FieldSample sample=Field_EvaluateStep(&d,1,position,velocity,&physical,&medium,dt);
    assert(sample.controllerCount==2);
    Vector3 next=MotionBody_AdvanceFieldVelocity(velocity,&body,(Vector3){0},force,
        &sample,(Vector3){0},dt);
    Vector3 actual=MotionVec_Sub(MotionVec_Scale(MotionVec_Sub(next,velocity),physical.massKg/dt),force);
    float forward=MotionVec_Dot(actual,tangent);
    Vector3 lateral=MotionVec_Sub(actual,MotionVec_Scale(tangent,forward));
    assert(fabsf(forward)<=.020001f && MotionVec_Length(lateral)<=.030001f);
    velocity=next;position=MotionVec_Add(position,MotionVec_Scale(velocity,dt));
    assert(Field_FiniteVector(position));
  }
}

static void TestMetadataBoundaries(void) {
  FieldDesc d=MotionField_Default();d.lifetime.durationSec=100;
  d.forceLawCount=1;d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
    .springStiffnessNPerM=.4f,.magnitudeNewtons=1};
  BodyPhysicalProperties body={.massKg=.004f};MediumProperties medium={0};
  Vector3 position={.1f,0,0};
  FieldSample raw=Field_Evaluate(&d,1,position,(Vector3){0},&body,&medium);
  assert(raw.controllerCount==0 && raw.forceNewtons.x<0);
  FieldSample one=Field_EvaluateStep(&d,1,position,(Vector3){0},&body,&medium,1.f/120);
  FieldSample combined={0};
  for(int i=0;i<12;i++) FieldSample_Combine(&combined,&one);
  assert(combined.controllerCount==FIELD_MAX_CONTROLLER_RESPONSES);
  assert(fabsf(combined.forceNewtons.x-12*one.forceNewtons.x)<1e-6f);
  MotionFields_Reset();assert(MotionFields_CreateField(&d));
  ReceiverConstraints constraints={.mode=RECEIVER_TRACER,.permittedAxes={1,1,1}};
  MotionFields_SampleExternalBodyStep(position,(Vector3){0},&body,&medium,&constraints,
      1.f/120,MOTION_RECEIVER_ALL,&combined);
  assert(combined.controllerCount==0 && MotionVec_Length(combined.forceNewtons)==0);
  constraints.mode=RECEIVER_ROOTED;constraints.permittedAxes=(Vector3){0,1,0};
  MotionFields_SampleExternalBodyStep(position,(Vector3){0},&body,&medium,&constraints,
      1.f/120,MOTION_RECEIVER_ALL,&combined);
  assert(combined.controllerCount==0 && combined.forceNewtons.x==0);
}

static void TestPreparedFrame(void) {
  FieldDesc d=MotionField_Default();d.lifetime.durationSec=100;
  d.trajectory.mode=FIELD_TRAJECTORY_PATH;d.trajectory.speedMps=2;
  Vector3 points[]={{0,0,0},{1,.5f,1},{2,0,2}};
  assert(MotionPath_Build(&d.trajectory.path,points,3));
  d.forceLawCount=1;d.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_MOVING_GUIDE,
    .springStiffnessNPerM=.4f,.magnitudeNewtons=1};
  FieldDesc moved=d;
  moved.transform.position=(Vector3){2,3,4};
  moved.transform.axisX=(Vector3){0,0,1};moved.transform.axisZ=(Vector3){-1,0,0};
  FieldTransform frame=FieldTrajectory_TransformFrom(&d,&moved.transform,.3f);
  BodyPhysicalProperties body={.massKg=.004f};MediumProperties medium={0};
  for(int i=0;i<20;i++) {
    Vector3 p=MotionVec_Add(frame.position,(Vector3){i*.01f,.1f,-.2f});
    FieldSample a=Field_EvaluateStep(&moved,.3f,p,(Vector3){0},&body,&medium,1.f/120);
    FieldSample b=Field_EvaluateFrameStepPass(&d,.3f,p,(Vector3){0},&body,&medium,
        true,false,false,NULL,1.f/120,&frame);
    assert(MotionVec_Length(MotionVec_Sub(a.forceNewtons,b.forceNewtons))<1e-6f);
    assert(a.controllerCount==b.controllerCount);
  }
}

int main(void) {
  TestTubeForceLimits();
  TestMetadataBoundaries();
  TestPreparedFrame();
  for(int rate=30;rate<=480;rate*=2) {
    float y=SettleGuide(rate,1,.004f,1);
    printf("%d Hz guide sag: %.7f m (expected -.0981)\n",rate,y);
    assert(fabsf(y+.0981f)<.0002f);
    assert(fabsf(SettleGuide(rate,2,.004f,1)+.04905f)<.0002f);
    assert(fabsf(SettleGuide(rate,1,.016f,1)+.3924f)<.0002f);
  }
  SettleGuide(120,1,.004f,.01f);
  puts("PASS: coupled gravity, overlapping guides, actual mass and bounded force");
  return 0;
}
