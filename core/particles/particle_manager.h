#ifndef CORE_PARTICLE_MANAGER_H
#define CORE_PARTICLE_MANAGER_H

/* Backend-neutral particle façade.  Descriptors are copied into a fixed
 * emitter pool; pointed-to ForceFields, curves, gradients and textures remain
 * caller-owned and must outlive every particle emitted from the emitter. */
#include "core/particles/particle_system.h"
#include "raylib.h"
#include <stdbool.h>

#define PARTICLE_MANAGER_MAX_EMITTERS 128

typedef struct ParticleGPUCaps {
    bool computeShader;
    bool storageBuffer;
    bool indirectDraw;
    bool instancing;
    int maxWorkGroupSize;
    int maxStorageBufferBytes;
} ParticleGPUCaps;

typedef enum ParticleSimulationPolicy {
    PARTICLE_SIM_AUTO = 0,
    PARTICLE_SIM_CPU_ONLY,
    PARTICLE_SIM_GPU_ONLY
} ParticleSimulationPolicy;

typedef enum ParticleRenderMode {
    PARTICLE_RENDER_BILLBOARD = 0,
    PARTICLE_RENDER_MESH,
    PARTICLE_RENDER_RIBBON,
    PARTICLE_RENDER_SURFACE_INPUT
} ParticleRenderMode;

/* One module description, with a capability mask instead of backend-specific
 * module APIs.  Set bits for modules which are not expressible by ParticleConfig. */
typedef enum ParticleModuleFlags {
    PARTICLE_MODULE_GRAVITY           = 1u << 0,
    PARTICLE_MODULE_DRAG              = 1u << 1,
    PARTICLE_MODULE_COLOR_OVER_LIFE   = 1u << 2,
    PARTICLE_MODULE_SIZE_OVER_LIFE    = 1u << 3,
    PARTICLE_MODULE_VELOCITY_STRETCH  = 1u << 4,
    PARTICLE_MODULE_FORCE_FIELD       = 1u << 5,
    PARTICLE_MODULE_VECTOR_FIELD      = 1u << 6, /* GPU only */
    PARTICLE_MODULE_GAMEPLAY_CALLBACK = 1u << 7, /* CPU only */
    PARTICLE_MODULE_GAMEPLAY_COLLISION= 1u << 8, /* CPU only */
    PARTICLE_MODULE_DEPTH_COLLISION   = 1u << 9, /* GPU preferred */
    /* Used only by SpawnParticle's wrapper until a legacy descriptor is fully
     * canonicalized. It preserves visual behaviour by selecting CPU. */
    PARTICLE_MODULE_LEGACY_COMPAT     = 1u << 10
} ParticleModuleFlags;

typedef struct ParticleEmitterDesc {
    ParticleSimulationPolicy simulationPolicy;
    ParticleRenderMode renderMode;
    ParticleConfig particle;
    unsigned int moduleFlags;
    const char *debugName; /* static string, used only for one-shot warnings */
} ParticleEmitterDesc;

typedef int ParticleEmitterHandle;
#define PARTICLE_EMITTER_INVALID (-1)

typedef enum ParticleEmitterStatus {
    PARTICLE_EMITTER_OK = 0,
    PARTICLE_EMITTER_INVALID_HANDLE,
    PARTICLE_EMITTER_GPU_UNAVAILABLE,
    PARTICLE_EMITTER_UNSUPPORTED_MODULE,
    PARTICLE_EMITTER_POOL_EXHAUSTED
} ParticleEmitterStatus;

typedef enum ParticleRenderBackend { PARTICLE_RENDER_BACKEND_CPU = 0, PARTICLE_RENDER_BACKEND_GPU } ParticleRenderBackend;
typedef struct ParticleRenderStream {
    ParticleRenderMode mode;
    ParticleRenderBackend backend;
    ParticleEmitterHandle emitter;
    /* Monotonic owner id. This remains valid after an emitter slot is reused,
     * so a retired splash can never be captured as part of a later one. */
    int ownerId;
    /* Backend-private stream. Consumers must rasterize through the manager;
     * they must never map/read back GPU particle buffers. */
    const void *privateData;
} ParticleRenderStream;

typedef struct ParticleManagerStats {
    int activeCpuParticles;
    int activeGpuParticles;
    int emitterCount;
    int rejectedGpuOnlyEmitters;
    int fallbackCount;
} ParticleManagerStats;

void ParticleManager_Init(void);
void ParticleManager_Unload(void);
const ParticleGPUCaps *ParticleSystem_GetGPUCaps(void);
ParticleEmitterHandle ParticleManager_CreateEmitter(const ParticleEmitterDesc *desc);
void ParticleManager_DestroyEmitter(ParticleEmitterHandle handle);
void ParticleManager_Emit(ParticleEmitterHandle handle, int count);
ParticleEmitterStatus ParticleManager_GetEmitterStatus(ParticleEmitterHandle handle);
bool ParticleManager_GetSurfaceStream(ParticleEmitterHandle handle, ParticleRenderStream *outStream);
int ParticleManager_CopySurfaceSamples(const ParticleRenderStream *stream, ParticleSurfaceSample *outSamples, int maxSamples);
bool ParticleManager_DrawSurfaceStream(const ParticleRenderStream *stream, Camera3D camera, Texture2D texture);
bool ParticleManager_DrawSurfaceThicknessStream(const ParticleRenderStream *stream, Camera3D camera);
void ParticleManager_Update(float dt);
void ParticleManager_Draw(Camera3D camera, Texture2D fallbackTexture);
void ParticleManager_DrawBody(Camera3D camera, Texture2D fallbackTexture);
void ParticleManager_DrawEmission(Camera3D camera, Texture2D fallbackTexture);
/* Whether drawing the emission target would submit any CPU/GPU billboard. */
bool ParticleManager_HasEmissionParticles(void);
void ParticleManager_GetStats(ParticleManagerStats *outStats);

/* Compatibility bridge used exclusively by SpawnParticle. */
void ParticleManager_SpawnCompatibility(ParticleConfig config);

#endif
