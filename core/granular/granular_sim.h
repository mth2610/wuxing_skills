#ifndef CORE_GRANULAR_SIM_H
#define CORE_GRANULAR_SIM_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define GRANULAR_GPU_MAX_GRAINS 4096
#define GRANULAR_CPU_MAX_GRAINS 768
#define GRANULAR_MAX_ACTIVE_WAVES 4

typedef enum {
    GRANULAR_MAT_DESERT_SAND = 0,   // Fine golden quartz sand (repose ~33 deg, high glint)
    GRANULAR_MAT_COARSE_GRAVEL,     // Angular river scree & pebbles (repose ~40 deg)
    GRANULAR_MAT_LOESS_SOIL,        // Dark cohesive Wuxia loam/earth (repose ~45 deg)
    GRANULAR_MAT_GOLD_IRON_SAND,    // Heavy metallic Kim Sa (repose ~36 deg, gold glint)
    GRANULAR_MAT_VOLCANIC_ASH,      // Light scoria ash with glowing ember veins (repose ~30 deg)
    GRANULAR_MAT_DRY_LEAF,          // Crisp autumn leaf lamina (ultralight, high flutter & lift)
    GRANULAR_MAT_FRESH_LEAF,        // Supple emerald spirit leaf lamina (wrap SSS, midrib vein)
    GRANULAR_MAT_DRY_TWIG,          // Elongated woody branch segment (anisotropic rod)
    GRANULAR_MAT_FLOWER_PETAL,      // Silky blossom petal (high buoyancy, spiral flutter)
    GRANULAR_MAT_ICE_SHARD,         // Crystalline frost/ice shard (low mu, high Fresnel/glint)
    GRANULAR_MAT_SNOW_POWDER,       // Cohesive powder snow (sintering cohesion, ice sparkle)
    GRANULAR_MAT_COUNT
} GranularMaterialId;

typedef enum {
    GRANULAR_MORPH_FINE_CONTINUUM = 0, // Dense overlapping granular continuum (sand, ash, snow)
    GRANULAR_MORPH_ANGULAR_CLOD   = 1, // Discrete faceted pebbles & soil clods
    GRANULAR_MORPH_PLANAR_LAMINA  = 2, // Thin aerofoil leaves & petals with 3D tumble & flutter
    GRANULAR_MORPH_ELONGATED_TWIG = 3, // Anisotropic woody segments & splinters
    GRANULAR_MORPH_CRYSTAL_SHARD  = 4  // Faceted translucent ice/mineral shards
} GranularMorphology;

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
    // Generalized morphology & aerodynamic coupling fields (append-only)
    uint32_t morphology;             // GranularMorphology
    float referenceMassKg;           // Representative grain mass for MotionFields Newton coupling
    float liftCoefficient;           // Planar aerofoil lift & cross-flow flutter gain
    float flutterFreqHz;             // Harmonic tumble/flutter frequency in air (Hz)
    float criticalEntrainmentMps;    // Shields critical threshold to break ground repose
} GranularMaterialProfile;

typedef enum {
    GRANULAR_WAVE_NONE = 0,
    GRANULAR_WAVE_RADIAL_IMPULSE,    // Spherical/cylindrical blast or suction + swirl
    GRANULAR_WAVE_CONICAL_FUNNEL,    // Burgers-Rott tornado funnel with sinuous spine
    GRANULAR_WAVE_SPIRAL_CONVERGE,   // Tu Luc: inward accretion spiral + ground tremor lift
    GRANULAR_WAVE_TRAVELLING_JET     // Chuong Luc: advancing shockfront + helical vortex stream
} GranularWaveKind;

typedef struct {
    GranularWaveKind kind;
    Vector3 origin;
    Vector3 direction;        // Unit axis / travel direction
    float   radius;           // Base / tube radius (m)
    float   topRadius;        // Funnel top radius or jet expansion cone (m)
    float   length;           // Funnel height or jet max travel distance (m)
    float   axialSpeed;       // Updraft or jet forward target speed (m/s)
    float   waveSpeed;        // Advancing wavefront propagation speed c (m/s)
    float   swirlSpeed;       // Tangential vortex speed (m/s or rad/s)
    float   inwardPull;       // Radial convergence / suction acceleration (m/s^2)
    float   liftAccel;        // Upward entrainment boost at wavefront (m/s^2)
    float   analyticLock;     // [0..1] kinodynamic manifold stiffness lambda
    float   duration;         // Total active duration (s)
    float   attackTime;       // Envelope ramp-in (s)
    float   fadeTime;         // Envelope ramp-out (s)
    float   elapsed;          // Runtime age (s)
    bool    loopTop;          // Recycle grains exiting top of funnel
    Vector3 stormVelocity;    // Translational velocity of moving funnel/wave (m/s)
    float   spinDown;         // [0..1] spin-down collapse progress (0=active, 1=settled)
} GranularWaveDesc;

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
int  GranularSim_AppendCluster(const GranularGrainState *grains, int count,
                               Vector3 receiverPoint, Vector3 receiverNormal);
int  GranularSim_ScatterBed(Vector3 center, float radius, int count,
                            const GranularMaterialId *palette, int paletteCount,
                            float grainRadius, float lifetime, unsigned int seed);
int  GranularSim_SpawnWave(const GranularWaveDesc *wave);
void GranularSim_ClearWaves(void);
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
float GranularSim_StepCpuDeterministicWithWaves(GranularGrainState *grains, int count,
                                                const GranularMaterialProfile *matOverride,
                                                GranularWaveDesc *waves, int waveCount,
                                                float dt, float elapsedTime,
                                                Vector3 receiverPoint, Vector3 receiverNormal);

#endif // CORE_GRANULAR_SIM_H
