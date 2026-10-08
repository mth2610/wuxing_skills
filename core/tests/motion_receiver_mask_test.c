/* Exercise actual registry and GPU packing with append-only receiver bits. */
#define main ExistingMotionTestsMain
#include "core/tests/motion_fields_test.c"
#undef main
#include "core/motion/motion_gpu.h"
#include <assert.h>

int main(void) {
    MotionFields_Reset();
    FieldDesc field = MotionField_Default();
    assert(field.receiverMask == MOTION_RECEIVER_ALL_COMPONENTS);
    field.receiverMask = 0;
    field.forceLawCount = 1;
    field.forceLaws[0] = (ForceLaw){.type=FORCE_LAW_ACCELERATION,
        .accelerationMps2={2,0,0}};
    assert(MotionFields_CreateField(&field));
    BodyPhysicalProperties body = {.massKg=1};
    MediumProperties medium = {0};
    ReceiverConstraints constraints = {.mode=RECEIVER_FREE,
        .permittedAxes={1,1,1}};
    FieldSample sample;
    MotionFields_SampleExternalBody((Vector3){0}, (Vector3){0}, &body,
        &medium, &constraints, MOTION_RECEIVER_TRAIL, &sample);
    assert(sample.accelerationMps2.x == 2);
    static MotionGpuScene gpu;
    MotionFields_PackGpu(&gpu);
    assert(gpu.meta[0] == 1);
    assert(gpu.fields[0].identity[2] == MOTION_RECEIVER_ALL_COMPONENTS);
    MotionFields_Reset();
    field.receiverMask = MOTION_RECEIVER_ALL;
    assert(MotionFields_CreateField(&field));
    MotionFields_SampleExternalBody((Vector3){0}, (Vector3){0}, &body,
        &medium, &constraints, MOTION_RECEIVER_TRAIL, &sample);
    assert(sample.accelerationMps2.x == 0);
    MotionFields_SampleExternalBody((Vector3){0}, (Vector3){0}, &body,
        &medium, &constraints, MOTION_RECEIVER_PARTICLE, &sample);
    assert(sample.accelerationMps2.x == 2);
    MotionFields_PackGpu(&gpu);
    assert(gpu.fields[0].identity[2] == MOTION_RECEIVER_ALL);
    puts("motion_receiver_mask: zero masks cover all components; explicit legacy masks retain selection");
    return 0;
}
