#ifndef WUXING_GAS_SYSTEM_H
#define WUXING_GAS_SYSTEM_H

#include "raylib.h"

typedef int GasVolumeHandle;

#define GAS_VOLUME_INVALID 0

typedef enum {
    GAS_SMOKE = 0,
    GAS_FIRE,
    GAS_ENERGY
} GasKind;

typedef enum {
    GAS_PRIORITY_AMBIENT = 0,
    GAS_PRIORITY_CAST,
    GAS_PRIORITY_ULTIMATE
} GasPriority;

typedef struct GasVolumeDesc {
    GasKind kind;
    GasPriority priority;
    Vector3 center;
    Vector3 size;
    float lifetime;
    Color bodyColor;
    Color emissionColor;
    float densityScale;
    float emissionGain;
    float buoyancy;
    float smokeWeight;
    /* Vorticity confinement strength. Higher values preserve more rolling,
     * torn structure after semi-Lagrangian advection; 0 disables it. */
    float turbulence;
    float densityDissipation;
    float temperatureDissipation;
    float reactionDissipation;
    /* Optical shaping applied after simulation. Zero inherits the material
     * preset, negative disables the control, and positive values are clamped.
     * detail/shadow accept [0, 2]; backgroundAdapt accepts [0, 1]. */
    float detailStrength;
    float shadowStrength;
    float backgroundAdapt;
} GasVolumeDesc;

typedef struct GasInjection {
    Vector3 position;
    float radius;
    Vector3 velocity;
    float density;
    float temperature;
    float reaction;
} GasInjection;

/* One-second rolling averages for the active gas volume. Draw timings are CPU
 * command-submission cost, not GPU execution time; use renderer timestamp
 * tracing on a presented surface for authoritative GPU milliseconds. */
typedef struct GasPerfStats {
    float updateCpuMsAvg;
    float atlasUploadCpuMsAvg;
    float raymarchSubmitCpuMsAvg;
    float compositeSubmitCpuMsAvg;
    float simSubstepsAvg;
    unsigned int sampleFrames;
    unsigned int atlasUploads;
    unsigned long long atlasUploadBytesTotal;
    int gridWidth;
    int gridHeight;
    int gridDepth;
    int raymarchWidth;
    int raymarchHeight;
    int raymarchSteps;
    unsigned long long raymarchAtlasTapUpperBound;
} GasPerfStats;

/* Returns a tuned starting point for smoke, fire, or magical energy gas. */
GasVolumeDesc GasVolume_Preset(GasKind kind);

/* Mobile-first v1 owns one simulated volume. A higher-priority request may
 * replace the current volume; rejected requests return GAS_VOLUME_INVALID. */
GasVolumeHandle GasVolume_Create(const GasVolumeDesc *desc);
void GasVolume_Destroy(GasVolumeHandle handle);
bool GasVolume_IsAlive(GasVolumeHandle handle);
void GasVolume_Inject(GasVolumeHandle handle, const GasInjection *injection);

/* Engine lifecycle and screen-space compositor hooks. */
void GasSystem_Init(int width, int height);
void GasSystem_Update(float dt);
bool GasSystem_HasPending(void);
void GasSystem_Prepare(Camera3D camera);
void GasSystem_Composite(void);
GasPerfStats GasSystem_GetPerfStats(void);
void GasSystem_ResetPerfStats(void);
void GasSystem_Unload(void);

#endif
