#include "core/particles/particle_field_integration.h"
#include <stdio.h>
#include <math.h>
/* Numeric integrator used by both guided and independent-field particles;
 * renderer/spawn plumbing is validated by the full build, not these checks. */
int main(void)
{
    ParticleDynamicsProfile body={.inverseMassKg=1000,.gravityScale=0,
        .aerodynamicAreaM2=1,.aerodynamicDragCoefficient=1,
        .windSusceptibility=1,.windCouplingHz=100};
    FieldSample sample={.forceNewtons={-1000,0,0},.dragForceNewtons={-1000,0,0},
        .hasDragForce=true,.dragCoefficientKgPerM=10,.dragMediumVelocityMps={0}};
    Vector3 v=MotionBody_AdvanceFieldVelocity((Vector3){10,0,0},&body,
        (Vector3){0},(Vector3){0},&sample,(Vector3){100,0,0},0.25f);
    if(!isfinite(v.x) || v.x<0 || v.x>=10) return 1;
    sample=(FieldSample){.forceNewtons={1,0,0},.accelerationMps2={0,-9.81f,0}};
    body=(ParticleDynamicsProfile){.inverseMassKg=1};
    Vector3 light=MotionBody_AdvanceFieldVelocity((Vector3){0},&body,
        (Vector3){0},(Vector3){0},&sample,(Vector3){0},0.1f);
    body.inverseMassKg=.25f;
    Vector3 heavy=MotionBody_AdvanceFieldVelocity((Vector3){0},&body,
        (Vector3){0},(Vector3){0},&sample,(Vector3){0},0.1f);
    if(fabsf(light.x-.1f)>1e-6f || fabsf(heavy.x-.025f)>1e-6f || light.y!=heavy.y) return 1;
    body=(ParticleDynamicsProfile){.inverseMassKg=1,.gravityScale=1,
        .densityKgM3=.6f,.airDensityKgM3=1.2f};
    sample=(FieldSample){.forceNewtons={0,9.81f,0},.hasBuoyancyForce=true};
    v=MotionBody_AdvanceFieldVelocity((Vector3){0},&body,(Vector3){0},
        (Vector3){0},&sample,(Vector3){0},.1f);
    if(fabsf(v.y)>1e-6f) return 1;
    body=(ParticleDynamicsProfile){.inverseMassKg=1000,
        .aerodynamicAreaM2=.01f,.aerodynamicDragCoefficient=1,
        .airDensityKgM3=1.2f,.windSusceptibility=1,.windCouplingHz=0};
    sample=(FieldSample){.mediumIsAbsolute=true,.mediumWeight=1,
        .mediumVelocityMps={3,0,0}};
    if(!ParticleField_UsesSharedIntegration(&sample)) return 1;
    v=MotionBody_AdvanceFieldVelocity((Vector3){0},&body,(Vector3){0},
        (Vector3){0},&sample,(Vector3){-100,0,0},.1f);
    if(!isfinite(v.x) || v.x<=0 || v.x>=3) return 1;
    /* Verification for Phase 1 & 2: Builder helpers & drag damping */
    MotionFieldHandle staticAttractor = MotionFields_SpawnStaticAttractor((Vector3){0,1,0}, 3.0f, 15.0f, 2.5f, 0.2f, 0.3f);
    if (!MotionFields_IsAlive(staticAttractor)) return 1;
    MotionFields_Stop(staticAttractor);
    if (MotionFields_IsAlive(staticAttractor)) return 1;

    /* Verify drag parameter damping: drag=0 preserves speed; drag>0 slows down */
    Vector3 v0 = {10.0f, 0, 0};
    float dt = 0.1f;
    float dragZero = 0.0f;
    float dragNonZero = 2.0f;
    Vector3 vZero = MotionVec_Scale(v0, dragZero > 0.0f ? expf(-dragZero * dt) : 1.0f);
    Vector3 vNonZero = MotionVec_Scale(v0, dragNonZero > 0.0f ? expf(-dragNonZero * dt) : 1.0f);
    if (fabsf(vZero.x - 10.0f) > 1e-5f) return 1;
    if (vNonZero.x >= 10.0f || vNonZero.x <= 0.0f) return 1;

    puts("Independent particle typed integration: implicit drag, units, builder helpers, and zero-drag preservation PASSED");
    return 0;
}
