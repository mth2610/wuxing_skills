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
    /* Exercise the production integrator, not a copy of the drag formula. */
    body=(ParticleDynamicsProfile){.inverseMassKg=1};
    sample=(FieldSample){0};
    v=MotionBody_AdvanceFieldVelocity((Vector3){10,0,0},&body,(Vector3){0},
        (Vector3){0},&sample,(Vector3){3,0,0},.1f);
    if(v.x!=10) return 1;
    body.windCouplingHz=2;body.windSusceptibility=1;
    v=MotionBody_AdvanceFieldVelocity(v,&body,(Vector3){0},(Vector3){0},
        &sample,(Vector3){3,0,0},.1f);
    if(fabsf(v.x-(3+7*expf(-.2f)))>1e-5f) return 1;
    body.windCouplingHz=0;sample.forceNewtons=(Vector3){2,0,0};
    v=MotionBody_AdvanceFieldVelocity((Vector3){10,0,0},&body,(Vector3){0},
        (Vector3){0},&sample,(Vector3){3,0,0},.1f);
    if(fabsf(v.x-10.2f)>1e-5f) return 1;
    puts("Independent particle integration: drag relative to air, zero drag and direct forces PASSED");
    return 0;
}
