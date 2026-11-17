#ifndef CORE_GRANULAR_SIM_H
#define CORE_GRANULAR_SIM_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define GRANULAR_GPU_MAX_GRAINS 4096
#define GRANULAR_CPU_MAX_GRAINS 768

typedef enum {
    GRANULAR_MAT_DESERT_SAND = 0,   // Fine golden quartz sand (repose ~33 deg, high glint)
    GRANULAR_MAT_COARSE_GRAVEL,     // Angular river scree & pebbles (repose ~40 deg)
    GRANULAR_MAT_LOESS_SOIL,        // Dark cohesive Wuxia loam/earth (repose ~45 deg)
    GRANULAR_MAT_GOLD_IRON_SAND,    // Heavy metallic Kim Sa (repose ~36 deg, gold glint)
    GRANULAR_MAT_VOLCANIC_ASH,      // Light scoria ash with glowing ember veins (repose ~30 deg)
    GRANULAR_MAT_COUNT
} GranularMaterialId;

typedef struct {
    float densityKgM3;          // Bulk grain density (e.g. 1600 sand, 2400 gravel, 4800 iron sand)
    float springStiffness;      // DEM normal spring k_n (normalized acceleration units)
    float dashpotDamping;       // DEM normal dashpot eta_n (inelasticity)
    float shearFrictionMu;      // Coulomb mu = tan(angle_of_repose)
    float rollingResistance;    // Rotational / settling lock resistance
    float cohesion;             // Capillary / damp soil cohesion (0 for dry sand)
    float airDrag;              // Aerodynamic drag coefficient (s^-1)
    float saltationSplash;      // Ejection multiplier when fast grains hit resting bed
    float dustEntrainmentRate;  // Fine dust emission per unit of shear/impact work
    Color baseColor;            // Primary grain albedo
    Color shadowColor;          // Deep crevice / packing AO tint
    Color highlightColor;       // Specular quartz/mica/gold glint tint
    float glintIntensity;       // Micro-facet sparkle gain
    float glintSharpness;       // Specular exponent (64..256)
    float grainAspect;          // Flattening / angularity ratio
} GranularMaterialProfile;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float   radius;
    float   lifetime;
    float   seed;
    float   contactAO;          // [0..1] neighbor packing density for crevice shading
    uint32_t materialId;
    uint32_t flags;             // 0=free, 1=resting, 2=saltating
} GranularGrainState;

typedef struct {
    bool    active;
    int     activeCount;
    bool    usingGpuBackend;
    float   totalShearWork;
} GranularSimStats;

const GranularMaterialProfile *GranularSim_GetMaterialProfile(GranularMaterialId id);
bool GranularSim_Init(void);
void GranularSim_Unload(void);
void GranularSim_Reset(void);
int  GranularSim_SpawnCluster(const GranularGrainState *grains, int count,
                              Vector3 receiverPoint, Vector3 receiverNormal);
void GranularSim_ApplyForceImpulse(Vector3 center, float radius,
                                   Vector3 forceAccel, float swirlRadPerSec);
void GranularSim_SetTornadoFunnel(Vector3 basePos, float baseRadius, float topRadius,
                                  float height, float swirlSpeed, float updraftSpeed,
                                  bool active);
void GranularSim_Update(float dt);
#if !defined(CORE_GRANULAR_HEADLESS_TEST)
void GranularSim_DrawBody(Camera3D camera);
#endif
void GranularSim_DrawShadowPass(void);
int  GranularSim_GetActiveCount(void);
GranularSimStats GranularSim_GetStats(void);

// Deterministic CPU DEM step entry point for headless unit tests (no GPU/window needed).
float GranularSim_StepCpuDeterministic(GranularGrainState *grains, int count,
                                       const GranularMaterialProfile *mat,
                                       float dt, Vector3 receiverPoint,
                                       Vector3 receiverNormal);

#endif // CORE_GRANULAR_SIM_H
