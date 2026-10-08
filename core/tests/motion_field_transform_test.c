#define main ExistingMotionTestsMain
#include "core/tests/motion_fields_test.c"
#undef main
#include "core/motion/motion_gpu.h"
#include <assert.h>
int main(void) {
    MotionFields_Reset();
    FieldDesc field=MotionField_Default();field.volume.radiusM=1;
    field.lifetime.durationSec=1;field.forceLawCount=1;
    field.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_ACCELERATION,.accelerationMps2={2,0,0}};
    MotionFieldHandle h=MotionFields_CreateField(&field);assert(h);
    MotionFields_Update(.25f);
    FieldTransform frame=field.transform;frame.position=(Vector3){5,0,0};
    frame.frameVelocityMps=(Vector3){3,0,0};
    assert(MotionFields_SetTransform(h,&frame));
    BodyPhysicalProperties body={.massKg=1};MediumProperties medium={0};
    ReceiverConstraints constraints={.mode=RECEIVER_FREE,.permittedAxes={1,1,1}};
    FieldSample sample;
    MotionFields_SampleExternalBody((Vector3){0},(Vector3){0},&body,&medium,&constraints,MOTION_RECEIVER_TRAIL,&sample);
    assert(sample.accelerationMps2.x==0);
    MotionFields_SampleExternalBody((Vector3){5,0,0},(Vector3){0},&body,&medium,&constraints,MOTION_RECEIVER_TRAIL,&sample);
    assert(sample.accelerationMps2.x==2);
    static MotionGpuScene scene;MotionFields_PackGpu(&scene);
    assert(scene.fields[0].position.x==5&&scene.fields[0].frameVelocity.x==3);
    assert(scene.fields[0].volume.z==.25f);
    frame.position.x=NAN;assert(!MotionFields_SetTransform(h,&frame));
    MotionFields_Update(.8f);assert(!MotionFields_IsAlive(h));
    assert(!MotionFields_SetTransform(h,&field.transform));
    puts("PASS: moving typed field updates CPU bounds/GPU frame without resetting lifetime or moving receivers");
    return 0;
}
