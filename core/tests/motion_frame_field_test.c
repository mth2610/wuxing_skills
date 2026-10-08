/* Reuse the field suite's production C bootstrap, including its standalone
 * vector helpers and Wind implementation. Its main is not run here. */
#define main MotionFieldsBootstrapMain
#include "core/tests/motion_fields_test.c"
#undef main
#include <assert.h>
static bool NearFrame(float a,float b) {return fabsf(a-b)<.0002f;}
int main(void) {
    MotionFields_Reset();
    Matrix identity={.m0=1,.m5=1,.m10=1,.m15=1};
    MotionFrameHandle frame=MotionFrame_Create(identity);
    FieldDesc desc=MotionField_Default();desc.lifetime.durationSec=10;
    desc.volume.radiusM=1;desc.forceLawCount=1;
    desc.forceLaws[0]=(ForceLaw){.type=FORCE_LAW_ACCELERATION,.accelerationMps2={1,0,0}};
    MotionFieldHandle field=MotionFields_CreateField(&desc);
    FieldTransform local=desc.transform;local.position=(Vector3){1,0,0};
    assert(field && MotionFields_BindFrame(field,frame,&local));
    MotionTargetRuntime *target=Motion_FindTarget(field);
    assert(target->physical.transform.position.x==1);
    Matrix moved=identity;moved.m0=0;moved.m1=1;moved.m4=-1;moved.m5=0;moved.m12=4;
    assert(MotionFrame_Update(frame,moved,.5f,false));
    /* Frame writes are cached only by registry update, not receiver sampling. */
    assert(target->physical.transform.position.x==1);
    MotionFields_Update(.5f);
    FieldTransform transform=target->physical.transform;
    assert(NearFrame(transform.position.x,4) && NearFrame(transform.position.y,1));
    assert(NearFrame(transform.axisX.y,1));
    assert(NearFrame(transform.angularVelocityRadPerSec.z,3.14159265f));
    assert(NearFrame(transform.frameVelocityMps.x,8-3.14159265f));
    BodyPhysicalProperties body={.massKg=1,.immersionFraction=1};
    MediumProperties medium={0};ReceiverConstraints constraints={.mode=RECEIVER_FREE};FieldSample sample;
    MotionFields_SampleBody(transform.position,(Vector3){0},&body,&medium,&constraints,.01f,MOTION_RECEIVER_TRAIL,NULL,&sample);
    assert(NearFrame(sample.accelerationMps2.x,0) && NearFrame(sample.accelerationMps2.y,1));
    MotionGpuScene gpu={0};MotionFields_PackGpu(&gpu);
    assert(gpu.meta[0]==1);
    assert(NearFrame(gpu.fields[0].position.x,4) && NearFrame(gpu.fields[0].position.y,1));
    assert(NearFrame(gpu.fields[0].axisX.y,1));
    assert(NearFrame(gpu.fields[0].frameVelocity.x,8-3.14159265f));
    assert(NearFrame(gpu.fields[0].angularVelocity.z,3.14159265f));
    /* Late choreography refresh updates GPU state without aging fields. */
    float ageBefore=target->age;
    moved.m12=5;assert(MotionFrame_Update(frame,moved,.1f,false));
    MotionFields_RefreshFrames();assert(target->age==ageBefore);
    assert(target->physical.transform.position.x==5);
    uint32_t revision=target->frameRevision;MotionFields_RefreshFrames();assert(target->frameRevision==revision && target->age==ageBefore);
    MotionFields_PackGpu(&gpu);assert(gpu.fields[0].position.x==5);
    /* Destroy freezes the last field position; reused slots cannot rebind. */
    MotionFrame_Destroy(frame);MotionFrameHandle next=MotionFrame_Create(identity);
    assert(next!=frame);MotionFields_Update(.1f);
    assert(NearFrame(target->physical.transform.position.x,5) && !target->frame);
    assert(target->physical.transform.frameVelocityMps.x==0 && target->physical.transform.angularVelocityRadPerSec.z==0);
    assert(!MotionFields_BindFrame(field,frame,NULL));
    assert(MotionFields_BindFrame(field,next,NULL));
    moved=identity;moved.m12=2;assert(MotionFrame_Update(next,moved,.5f,true));
    MotionFields_Update(.1f);
    assert(target->physical.transform.position.x==2 && target->physical.transform.frameVelocityMps.x==0);
    target->physical.transform.frameVelocityMps=(Vector3){3,2,1};
    target->physical.transform.angularVelocityRadPerSec=(Vector3){1,2,3};
    assert(MotionFields_BindFrame(field,0,NULL));
    assert(target->physical.transform.frameVelocityMps.x==0 && target->physical.transform.angularVelocityRadPerSec.z==0);
    moved.m12=9;assert(MotionFrame_Update(next,moved,.5f,false));MotionFields_Update(.1f);
    assert(target->physical.transform.position.x==2);
    assert(MotionFields_BindFrame(field,next,NULL));
    transform=desc.transform;transform.position.x=7;
    assert(MotionFields_SetTransform(field,&transform) && !target->frame);
    moved.m12=11;assert(MotionFrame_Update(next,moved,.5f,false));MotionFields_Update(.1f);
    assert(target->physical.transform.position.x==7);
    Matrix scaled=identity;scaled.m0=2;
    MotionFrameHandle bad=MotionFrame_Create(scaled);
    assert(bad && !MotionFields_BindFrame(field,bad,NULL));
    assert(MotionFields_BindFrame(field,next,NULL));
    assert(MotionFrame_Update(next,scaled,.1f,false));MotionFields_Update(.1f);
    assert(!target->frame && target->physical.transform.position.x==11);
    MotionFields_Reset();MotionFrameSnapshot snapshot;
    assert(MotionFrame_Snapshot(next,(Vector3){0},&snapshot));
    MotionFrame_Reset();assert(!MotionFrame_Snapshot(next,(Vector3){0},&snapshot));
    puts("PASS: field-frame cache, rigid composition, rotated response, GPU packing, freeze, unbind and reset");
    return 0;
}
