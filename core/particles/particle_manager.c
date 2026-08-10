#include "core/particles/particle_manager.h"

#include "core/particles/gpu/particle_gpu_legacy.h"
#include "rlgl.h"
#include <string.h>

typedef struct ParticleEmitterRuntime {
    bool active, gpu, warned;
    int ownerId;
    ParticleEmitterStatus status;
    ParticleEmitterDesc desc;
} ParticleEmitterRuntime;

static ParticleEmitterRuntime s_emitters[PARTICLE_MANAGER_MAX_EMITTERS];
static ParticleGPUCaps s_caps;
static ParticleManagerStats s_stats;
static bool s_initialized;
static int s_nextOwnerId = 1;

static int ParticleManager_NextOwnerId(void)
{
    int id = s_nextOwnerId++;
    if (s_nextOwnerId <= 0) s_nextOwnerId = 1;
    return id;
}

static bool ParticleManager_GPUCanRun(unsigned int modules)
{
    const unsigned int cpuOnly = PARTICLE_MODULE_GAMEPLAY_CALLBACK | PARTICLE_MODULE_GAMEPLAY_COLLISION |
                                 PARTICLE_MODULE_LEGACY_COMPAT;
    return s_caps.computeShader && s_caps.storageBuffer && (modules & cpuOnly) == 0;
}

static void ParticleManager_RefreshStats(void)
{
    int cpu = 0, max = 0;
    ParticleSystem_GetStats(&cpu, &max);
    (void)max;
    s_stats.activeCpuParticles = cpu;
    s_stats.activeGpuParticles = GpuParticleSystem_ActiveCount();
}

void ParticleManager_Init(void)
{
    if (s_initialized) return;
    memset(s_emitters, 0, sizeof(s_emitters));
    memset(&s_stats, 0, sizeof(s_stats));
    s_nextOwnerId = 1;
    InitParticleSystem();
    GpuParticleSystem_Init();
    /* Probe once, after renderer/backend initialization. The compute system
     * already validates shader/buffer creation, so these are usable caps. */
    s_caps.computeShader = GpuParticleSystem_IsComputeActive();
    s_caps.storageBuffer = s_caps.computeShader;
    s_caps.indirectDraw = false; /* current raylib stream uses instanced draw */
    s_caps.instancing = s_caps.computeShader;
    s_caps.maxWorkGroupSize = s_caps.computeShader ? 256 : 0;
    s_caps.maxStorageBufferBytes = s_caps.computeShader ? MAX_GPU_PARTICLES * 64 : 0;
    s_initialized = true;
}

void ParticleManager_Unload(void)
{
    if (!s_initialized) return;
    GpuParticleSystem_Unload();
    UnloadParticleSystem();
    memset(s_emitters, 0, sizeof(s_emitters));
    s_initialized = false;
}

const ParticleGPUCaps *ParticleSystem_GetGPUCaps(void) { return &s_caps; }

ParticleEmitterHandle ParticleManager_CreateEmitter(const ParticleEmitterDesc *desc)
{
    if (!s_initialized || !desc) return PARTICLE_EMITTER_INVALID;
    for (int i = 0; i < PARTICLE_MANAGER_MAX_EMITTERS; ++i) {
        ParticleEmitterRuntime *e = &s_emitters[i];
        if (e->active) continue;
        memset(e, 0, sizeof(*e)); e->active = true; e->ownerId = ParticleManager_NextOwnerId(); e->desc = *desc;
        bool gpuOK = ParticleManager_GPUCanRun(desc->moduleFlags);
        e->gpu = desc->simulationPolicy == PARTICLE_SIM_GPU_ONLY ||
                 (desc->simulationPolicy == PARTICLE_SIM_AUTO && gpuOK);
        e->status = PARTICLE_EMITTER_OK;
        if (desc->simulationPolicy == PARTICLE_SIM_GPU_ONLY && !gpuOK) {
            e->gpu = false;
            e->status = (desc->moduleFlags & (PARTICLE_MODULE_GAMEPLAY_CALLBACK | PARTICLE_MODULE_GAMEPLAY_COLLISION))
                            ? PARTICLE_EMITTER_UNSUPPORTED_MODULE : PARTICLE_EMITTER_GPU_UNAVAILABLE;
            s_stats.rejectedGpuOnlyEmitters++;
        } else if (desc->simulationPolicy == PARTICLE_SIM_AUTO && !gpuOK && desc->moduleFlags) {
            s_stats.fallbackCount++;
        }
        s_stats.emitterCount++;
        return i;
    }
    return PARTICLE_EMITTER_INVALID;
}

void ParticleManager_DestroyEmitter(ParticleEmitterHandle handle)
{
    if (handle < 0 || handle >= PARTICLE_MANAGER_MAX_EMITTERS || !s_emitters[handle].active) return;
    s_emitters[handle].active = false;
    if (s_stats.emitterCount > 0) s_stats.emitterCount--;
}

void ParticleManager_Emit(ParticleEmitterHandle handle, int count)
{
    if (handle < 0 || handle >= PARTICLE_MANAGER_MAX_EMITTERS || count <= 0) return;
    ParticleEmitterRuntime *e = &s_emitters[handle];
    if (!e->active || e->status != PARTICLE_EMITTER_OK) {
        if (e->active && !e->warned) { TraceLog(LOG_WARNING, "ParticleManager: GPU_ONLY emitter '%s' rejected (%d)", e->desc.debugName ? e->desc.debugName : "unnamed", e->status); e->warned = true; }
        return;
    }
    // A packed volume sheet is decoded by particle_lit.fs, which only the CPU
    // billboard path binds — the GPU backend has its own shader and would read
    // the sheet's three density channels as a colour, turning fire green. Route
    // those emitters to the CPU path rather than rendering them wrong.
    if (e->gpu && e->desc.particle.render.volumeSheet) {
        if (!e->warned) {
            e->warned = true;
            TraceLog(LOG_INFO, "ParticleManager: emitter '%s' uses a volume sheet — "
                               "forced onto the CPU path (no GPU-backend decoder)",
                     e->desc.debugName ? e->desc.debugName : "unnamed");
        }
        e->gpu = false;
    }
    for (int i = 0; i < count; ++i) {
        if (e->gpu) {
            const ParticleConfig *p = &e->desc.particle;
            VFXContrastLayer layer = p->render.blendMode == VFX_BLEND_ADDITIVE
                                         ? VFX_CONTRAST_EMISSION
                                         : VFX_CONTRAST_BODY;
            float boost = p->render.emissiveBoost > 0.0f
                              ? p->render.emissiveBoost
                              : 1.0f;
            if (layer == VFX_CONTRAST_EMISSION)
                boost = VFXContrast_ApplyEmissionIntensity(
                    boost, p->render.contrastProfile);
            GpuParticleSystem_Spawn((GpuParticleConfig){ .position=p->position, .velocity=p->velocity,
                .colorStart=VFXContrast_ApplyColor(p->colorStart, p->render.contrastProfile, layer),
                .colorEnd=VFXContrast_ApplyColor(p->colorEnd, p->render.contrastProfile, layer), .radius=p->radius,
                .lifetime=p->lifetime, .forceField=p->forceField, .stretchStrength=p->stretchStrength,
                .stretchMinSpeed=p->stretchMinSpeed, .collisionEnabled=p->collisionEnabled,
                .collisionElasticity=p->collisionElasticity, .collisionFloorY=p->collisionFloorY,
                .axisOrigin=p->forceAxisOrigin, .axisDir=p->forceAxisDir,
                .emissiveBoost=boost, .emitterId=e->ownerId,
                .renderMode=(int)e->desc.renderMode });
        } else ParticleSystem_SpawnFromEmitter(e->desc.particle, e->ownerId, (int)e->desc.renderMode);
    }
}

void ParticleManager_EmitBatch(ParticleEmitterHandle handle,
                               const ParticleConfig *particles, int count)
{
    if (handle < 0 || handle >= PARTICLE_MANAGER_MAX_EMITTERS || !particles || count <= 0) return;
    ParticleEmitterRuntime *e = &s_emitters[handle];
    if (!e->active || e->status != PARTICLE_EMITTER_OK) return;
    for (int i = 0; i < count; ++i) {
        const ParticleConfig *p = &particles[i];
        if (e->gpu) {
            VFXContrastLayer layer = p->render.blendMode == VFX_BLEND_ADDITIVE
                                         ? VFX_CONTRAST_EMISSION
                                         : VFX_CONTRAST_BODY;
            float boost = p->render.emissiveBoost > 0.0f
                              ? p->render.emissiveBoost
                              : 1.0f;
            if (layer == VFX_CONTRAST_EMISSION)
                boost = VFXContrast_ApplyEmissionIntensity(
                    boost, p->render.contrastProfile);
            GpuParticleSystem_Spawn((GpuParticleConfig){ .position=p->position, .velocity=p->velocity,
                .colorStart=VFXContrast_ApplyColor(p->colorStart, p->render.contrastProfile, layer),
                .colorEnd=VFXContrast_ApplyColor(p->colorEnd, p->render.contrastProfile, layer), .radius=p->radius,
                .lifetime=p->lifetime, .forceField=p->forceField, .stretchStrength=p->stretchStrength,
                .stretchMinSpeed=p->stretchMinSpeed, .collisionEnabled=p->collisionEnabled,
                .collisionElasticity=p->collisionElasticity, .collisionFloorY=p->collisionFloorY,
                .axisOrigin=p->forceAxisOrigin, .axisDir=p->forceAxisDir,
                .emissiveBoost=boost, .emitterId=e->ownerId,
                .renderMode=(int)e->desc.renderMode });
        } else {
            ParticleSystem_SpawnFromEmitter(*p, e->ownerId, (int)e->desc.renderMode);
        }
    }
}

ParticleEmitterStatus ParticleManager_GetEmitterStatus(ParticleEmitterHandle handle)
{ return (handle < 0 || handle >= PARTICLE_MANAGER_MAX_EMITTERS || !s_emitters[handle].active) ? PARTICLE_EMITTER_INVALID_HANDLE : s_emitters[handle].status; }

bool ParticleManager_GetSurfaceStream(ParticleEmitterHandle handle, ParticleRenderStream *outStream)
{
    if (!outStream || handle < 0 || handle >= PARTICLE_MANAGER_MAX_EMITTERS || !s_emitters[handle].active) return false;
    ParticleEmitterRuntime *e = &s_emitters[handle];
    if (e->desc.renderMode != PARTICLE_RENDER_SURFACE_INPUT || e->status != PARTICLE_EMITTER_OK) return false;
    *outStream = (ParticleRenderStream){ e->desc.renderMode, e->gpu ? PARTICLE_RENDER_BACKEND_GPU : PARTICLE_RENDER_BACKEND_CPU, handle, e->ownerId, e };
    return true;
}

int ParticleManager_CopySurfaceSamples(const ParticleRenderStream *stream, ParticleSurfaceSample *outSamples, int maxSamples)
{
    if (!stream || !outSamples || maxSamples <= 0 || stream->mode != PARTICLE_RENDER_SURFACE_INPUT) return 0;
    if (stream->backend != PARTICLE_RENDER_BACKEND_CPU) return 0; /* GPU raster path owns GPU samples. */
    return ParticleSystem_GetSurfaceSamples(stream->ownerId, outSamples, maxSamples);
}

bool ParticleManager_DrawSurfaceStream(const ParticleRenderStream *stream, Camera3D camera, Texture2D texture)
{
    if (!stream || stream->mode != PARTICLE_RENDER_SURFACE_INPUT || stream->backend != PARTICLE_RENDER_BACKEND_GPU) return false;
    GpuParticleSystem_DrawSurfaceEmitter(camera, texture, stream->ownerId);
    return true;
}

bool ParticleManager_DrawSurfaceThicknessStream(const ParticleRenderStream *stream, Camera3D camera)
{
    if (!stream || stream->mode != PARTICLE_RENDER_SURFACE_INPUT || stream->backend != PARTICLE_RENDER_BACKEND_GPU) return false;
    GpuParticleSystem_DrawSurfaceThicknessEmitter(camera, stream->ownerId);
    return true;
}

void ParticleManager_Update(float dt) { if (!s_initialized) return; UpdateParticles(dt); GpuParticleSystem_Update(dt); ParticleManager_RefreshStats(); }
void ParticleManager_Draw(Camera3D c, Texture2D t)
{
    if (!s_initialized) return;

    // CPU particles select alpha/additive per particle and restore the default
    // alpha state when finished. GPU billboards currently have one emissive
    // blend law, so bind it explicitly here instead of inheriting whichever
    // mode the CPU path happened to leave behind. Without this ownership split,
    // GPU VFX switched between alpha and additive based on CPU activity.
    DrawParticles(c, t);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    GpuParticleSystem_Draw(c, t);
    rlDrawRenderBatchActive();
    EndBlendMode();
}
void ParticleManager_DrawBody(Camera3D c, Texture2D t)
{
    if (!s_initialized) return;
    DrawParticlesBody(c, t);
    // GPU billboards currently have an emissive-only material contract. Do
    // not force their glow sheets through alpha body compositing; black RGB in
    // a soft glow border would become a visible dark halo.
}
void ParticleManager_DrawEmission(Camera3D c, Texture2D t)
{
    if (!s_initialized) return;
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    DrawParticlesEmission(c, t);
    GpuParticleSystem_Draw(c, t);
    rlDrawRenderBatchActive();
    EndBlendMode();
}
bool ParticleManager_HasEmissionParticles(void)
{
    return ParticleSystem_HasAdditiveParticles() || GpuParticleSystem_ActiveCount() > 0;
}
void ParticleManager_GetStats(ParticleManagerStats *outStats) { if (!outStats) return; ParticleManager_RefreshStats(); *outStats = s_stats; }

void ParticleManager_SpawnCompatibility(ParticleConfig config)
{
    if (!s_initialized) { ParticleSystem_SpawnLegacy(config); return; }
    ParticleEmitterDesc desc = { PARTICLE_SIM_AUTO, PARTICLE_RENDER_BILLBOARD, config,
                                 PARTICLE_MODULE_LEGACY_COMPAT, "SpawnParticle compatibility" };
    ParticleEmitterHandle h = ParticleManager_CreateEmitter(&desc);
    if (h != PARTICLE_EMITTER_INVALID) { ParticleManager_Emit(h, 1); ParticleManager_DestroyEmitter(h); }
}
