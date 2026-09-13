#ifndef CORE_FLUID_MOTION_H
#define CORE_FLUID_MOTION_H

typedef enum {
    FLUID_MOTION_WATER = 0,
    FLUID_MOTION_POISON,
    FLUID_MOTION_MUD,
    FLUID_MOTION_LAVA,
    FLUID_MOTION_LIQUID_METAL
} FluidMotionProfile;

typedef struct {
    float splashField;
    float splashVelocity;
    float normalLift;
    float turbulence;
    float impactViscosity;
    float settleViscosity;
    float gatherStrength;
    float restitution;
    float tangentRetention;
    float impactDuration;
    float lifetime;
} FluidMotionDesc;

/* Deliberately cheap artistic approximations. Viscosity is exponential
 * velocity damping and gatherStrength is an axis attraction; neither claims
 * to be a pressure, density, or constitutive solve. */
static inline FluidMotionDesc FluidMotion_Get(FluidMotionProfile profile)
{
    switch (profile) {
    case FLUID_MOTION_POISON:
        return (FluidMotionDesc){14.0f,0.88f,0.90f,0.85f,4.2f,9.0f,6.0f,0.06f,0.58f,0.16f,1.60f};
    case FLUID_MOTION_MUD:
        return (FluidMotionDesc){6.0f,0.42f,0.48f,0.16f,9.5f,16.0f,8.0f,0.00f,0.30f,0.12f,2.00f};
    case FLUID_MOTION_LAVA:
        return (FluidMotionDesc){7.5f,0.48f,0.58f,0.28f,8.0f,14.0f,7.0f,0.02f,0.40f,0.16f,2.20f};
    case FLUID_MOTION_LIQUID_METAL:
        return (FluidMotionDesc){11.0f,0.70f,0.78f,0.22f,5.5f,10.0f,10.0f,0.16f,0.74f,0.14f,1.75f};
    case FLUID_MOTION_WATER:
    default:
        return (FluidMotionDesc){18.0f,1.00f,1.00f,1.40f,3.2f,7.5f,5.5f,0.10f,0.68f,0.14f,1.45f};
    }
}

#endif
