#ifndef CORE_LIQUID_MOTION_H
#define CORE_LIQUID_MOTION_H

typedef enum {
    LIQUID_MOTION_WATER = 0,
    LIQUID_MOTION_POISON,
    LIQUID_MOTION_MUD,
    LIQUID_MOTION_LAVA,
    LIQUID_MOTION_LIQUID_METAL
} LiquidMotionProfile;

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
} LiquidMotionDesc;

/* Deliberately cheap artistic approximations. Viscosity is exponential
 * velocity damping and gatherStrength is an axis attraction; neither claims
 * to be a pressure, density, or constitutive solve. */
static inline LiquidMotionDesc LiquidMotion_Get(LiquidMotionProfile profile)
{
    switch (profile) {
    case LIQUID_MOTION_POISON:
        return (LiquidMotionDesc){14.5f, 0.88f, 0.90f, 0.92f, 4.2f, 9.2f, 6.4f, 0.06f, 0.58f, 0.16f, 1.65f};
    case LIQUID_MOTION_MUD:
        return (LiquidMotionDesc){5.8f, 0.40f, 0.46f, 0.15f, 10.0f, 17.5f, 8.8f, 0.00f, 0.26f, 0.12f, 2.05f};
    case LIQUID_MOTION_LAVA:
        return (LiquidMotionDesc){7.2f, 0.45f, 0.55f, 0.25f, 8.5f, 15.0f, 7.6f, 0.02f, 0.36f, 0.16f, 2.25f};
    case LIQUID_MOTION_LIQUID_METAL:
        return (LiquidMotionDesc){11.5f, 0.74f, 0.82f, 0.20f, 5.2f, 9.8f, 11.2f, 0.18f, 0.78f, 0.14f, 1.80f};
    case LIQUID_MOTION_WATER:
    default:
        return (LiquidMotionDesc){18.5f, 1.02f, 1.00f, 1.45f, 3.1f, 7.5f, 5.8f, 0.11f, 0.70f, 0.14f, 1.45f};
    }
}

/* Profile-dependent crown lobe count for Rayleigh-Plateau rim breakup. */
static inline float LiquidMotion_CrownLobes(LiquidMotionProfile profile)
{
    switch (profile) {
    case LIQUID_MOTION_POISON:       return 6.0f;
    case LIQUID_MOTION_MUD:          return 3.0f;
    case LIQUID_MOTION_LAVA:         return 3.0f;
    case LIQUID_MOTION_LIQUID_METAL: return 5.0f;
    case LIQUID_MOTION_WATER:
    default:                         return 7.0f;
    }
}

#endif
