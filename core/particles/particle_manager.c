#include "core/particles/particle_manager.h"

#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/mesh_adjacency.h"
#include "rlgl.h"
#include "raymath.h"
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

static bool ParticleManager_IsZeroMatrix(Matrix m)
{
    return m.m0 == 0.0f && m.m1 == 0.0f && m.m2 == 0.0f && m.m3 == 0.0f &&
           m.m4 == 0.0f && m.m5 == 0.0f && m.m6 == 0.0f && m.m7 == 0.0f &&
           m.m8 == 0.0f && m.m9 == 0.0f && m.m10 == 0.0f && m.m11 == 0.0f &&
           m.m12 == 0.0f && m.m13 == 0.0f && m.m14 == 0.0f && m.m15 == 0.0f;
}

static void ParticleManager_ApplySource(ParticleEmitterRuntime *emitter,
                                        ParticleConfig *particle)
{
    const ParticleEmissionSource *source = &emitter->desc.source;
    Vector3 position;
    if (source->type == PARTICLE_SOURCE_CONFIG_POSITION) return;
    if (source->type == PARTICLE_SOURCE_POINT) {
        position = source->point;
    } else if ((source->type == PARTICLE_SOURCE_MESH_VERTEX ||
                source->type == PARTICLE_SOURCE_MESH_EDGE) &&
               source->mesh && source->mesh->count > 0) {
        position = source->type == PARTICLE_SOURCE_MESH_VERTEX
                       ? MeshAdjacency_SampleVertex(source->mesh)
                       : MeshAdjacency_SampleEdge(source->mesh);
        if (!ParticleManager_IsZeroMatrix(source->transform))
            position = Vector3Transform(position, source->transform);
    } else {
        if (!emitter->warned) {
            emitter->warned = true;
            TraceLog(LOG_WARNING, "ParticleManager: emitter '%s' has an invalid mesh source",
                     emitter->desc.debugName ? emitter->desc.debugName : "unnamed");
        }
        return;
    }
    particle->position = position;
    particle->physics.position = position;
}

static bool ParticleManager_GPUCanRun(unsigned int modules)
{
    const unsigned int cpuOnly = PARTICLE_MODULE_GAMEPLAY_CALLBACK | PARTICLE_MODULE_GAMEPLAY_COLLISION |
                                 PARTICLE_MODULE_LEGACY_COMPAT;
    return s_caps.computeShader && s_caps.storageBuffer && (modules & cpuOnly) == 0;
}

static VFXResolvedAppearance ParticleManager_ResolveAppearance(const ParticleConfig *p)
{
    return VFXAppearance_Resolve(
        p->render.appearance,
        (VFXResolvedAppearance){
            .surface = (VFXSurfaceMode)p->render.blendMode,
            .contrast = p->render.contrastProfile,
            .bodyOpacity = p->render.blendMode == VFX_BLEND_ALPHA ? 1.0f : 0.0f,
            .emissionIntensity = p->render.emissiveBoost > 0.0f
                                     ? p->render.emissiveBoost : 1.0f,
            .emissionThreshold = 1.0f,
            .unlit = p->render.unlit != 0
        });
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
    s_caps.maxStorageBufferBytes = s_caps.computeShader
                                       ? MAX_GPU_PARTICLES * GPU_PARTICLE_DATA_STRIDE_BYTES : 0;
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
        ParticleConfig_Unify(&e->desc.particle);
        if (e->desc.particle.travelPath)
            e->desc.moduleFlags |= PARTICLE_MODULE_PATH_FOLLOW;
        bool gpuOK = ParticleManager_GPUCanRun(e->desc.moduleFlags);
        VFXResolvedAppearance appearance = ParticleManager_ResolveAppearance(&e->desc.particle);
        // The current GPU billboard draw is one additive batch. A named alpha
        // or premultiplied appearance must use the CPU renderer until blend is
        // part of the GPU bucket key; rendering it with the wrong law is worse
        // than the fallback. INHERIT keeps legacy backend selection exact.
        if (e->desc.particle.render.appearance != VFX_APPEARANCE_INHERIT &&
            appearance.surface != VFX_SURFACE_ADDITIVE)
            gpuOK = false;
        e->gpu = e->desc.simulationPolicy == PARTICLE_SIM_GPU_ONLY ||
                 (e->desc.simulationPolicy == PARTICLE_SIM_AUTO && gpuOK);
        e->status = PARTICLE_EMITTER_OK;
        if (e->desc.simulationPolicy == PARTICLE_SIM_GPU_ONLY && !gpuOK) {
            e->gpu = false;
            e->status = (e->desc.moduleFlags & (PARTICLE_MODULE_GAMEPLAY_CALLBACK | PARTICLE_MODULE_GAMEPLAY_COLLISION))
                            ? PARTICLE_EMITTER_UNSUPPORTED_MODULE : PARTICLE_EMITTER_GPU_UNAVAILABLE;
            s_stats.rejectedGpuOnlyEmitters++;
        } else if (e->desc.simulationPolicy == PARTICLE_SIM_AUTO && !gpuOK && e->desc.moduleFlags) {
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
        ParticleConfig spawned = e->desc.particle;
        ParticleManager_ApplySource(e, &spawned);
        if (e->gpu) {
            const ParticleConfig *p = &spawned;
            VFXResolvedAppearance appearance = ParticleManager_ResolveAppearance(p);
            VFXContrastLayer layer = appearance.surface == VFX_SURFACE_ADDITIVE
                                         ? VFX_CONTRAST_EMISSION
                                         : VFX_CONTRAST_BODY;
            float boost = appearance.emissionIntensity > 0.0f
                              ? appearance.emissionIntensity : 1.0f;
            if (layer == VFX_CONTRAST_EMISSION)
                boost = VFXContrast_ApplyEmissionIntensity(
                    boost, appearance.contrast);
            GpuParticleSystem_Spawn((GpuParticleConfig){ .position=p->position, .velocity=p->velocity,
                .colorStart=VFXContrast_ApplyColor(p->colorStart, appearance.contrast, layer),
                .colorEnd=VFXContrast_ApplyColor(p->colorEnd, appearance.contrast, layer), .radius=p->radius,
                .lifetime=p->lifetime, .forceField=p->forceField, .stretchStrength=p->stretchStrength,
                .stretchMinSpeed=p->stretchMinSpeed, .collisionEnabled=p->collisionEnabled,
                .collisionElasticity=p->collisionElasticity, .collisionFloorY=p->collisionFloorY,
                .axisOrigin=p->forceAxisOrigin, .axisDir=p->forceAxisDir,
                .travelPath=p->travelPath, .onTargetEmit=p->onTargetEmit,
                .onTargetEmitCount=p->onTargetEmitCount,
                .emissiveBoost=boost, .emitterId=e->ownerId,
                .renderMode=(int)e->desc.renderMode });
        } else ParticleSystem_SpawnFromEmitter(spawned, e->ownerId, (int)e->desc.renderMode);
    }
}

void ParticleManager_EmitBatch(ParticleEmitterHandle handle,
                               const ParticleConfig *particles, int count)
{
    if (handle < 0 || handle >= PARTICLE_MANAGER_MAX_EMITTERS || !particles || count <= 0) return;
    ParticleEmitterRuntime *e = &s_emitters[handle];
    if (!e->active || e->status != PARTICLE_EMITTER_OK) return;
    for (int i = 0; i < count; ++i) {
        ParticleConfig canonical = particles[i];
        ParticleConfig_Unify(&canonical);
        const ParticleConfig *p = &canonical;
        VFXResolvedAppearance appearance = ParticleManager_ResolveAppearance(p);
        bool appearanceFitsGpu = p->render.appearance == VFX_APPEARANCE_INHERIT ||
                                 appearance.surface == VFX_SURFACE_ADDITIVE;
        if (e->gpu && appearanceFitsGpu) {
            VFXContrastLayer layer = appearance.surface == VFX_SURFACE_ADDITIVE
                                         ? VFX_CONTRAST_EMISSION
                                         : VFX_CONTRAST_BODY;
            float boost = appearance.emissionIntensity > 0.0f
                              ? appearance.emissionIntensity : 1.0f;
            if (layer == VFX_CONTRAST_EMISSION)
                boost = VFXContrast_ApplyEmissionIntensity(
                    boost, appearance.contrast);
            GpuParticleSystem_Spawn((GpuParticleConfig){ .position=p->position, .velocity=p->velocity,
                .colorStart=VFXContrast_ApplyColor(p->colorStart, appearance.contrast, layer),
                .colorEnd=VFXContrast_ApplyColor(p->colorEnd, appearance.contrast, layer), .radius=p->radius,
                .lifetime=p->lifetime, .forceField=p->forceField, .stretchStrength=p->stretchStrength,
                .stretchMinSpeed=p->stretchMinSpeed, .collisionEnabled=p->collisionEnabled,
                .collisionElasticity=p->collisionElasticity, .collisionFloorY=p->collisionFloorY,
                .axisOrigin=p->forceAxisOrigin, .axisDir=p->forceAxisDir,
                .travelPath=p->travelPath, .onTargetEmit=p->onTargetEmit,
                .onTargetEmitCount=p->onTargetEmitCount,
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

bool ParticleManager_DrawSurfaceBackStream(const ParticleRenderStream *stream, Camera3D camera)
{
    if (!stream || stream->mode != PARTICLE_RENDER_SURFACE_INPUT || stream->backend != PARTICLE_RENDER_BACKEND_GPU) return false;
    GpuParticleSystem_DrawSurfaceBackEmitter(camera, stream->ownerId);
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
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    DrawParticles(c, t);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    GpuParticleSystem_Draw(c, t);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    rlDrawRenderBatchActive();
}
void ParticleManager_DrawBody(Camera3D c, Texture2D t)
{
    if (!s_initialized) return;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    DrawParticlesBody(c, t);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    // GPU billboards currently have an emissive-only material contract. Do
    // not force their glow sheets through alpha body compositing; black RGB in
    // a soft glow border would become a visible dark halo.
}
void ParticleManager_DrawEmission(Camera3D c, Texture2D t)
{
    if (!s_initialized) return;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    DrawParticlesEmission(c, t);
    GpuParticleSystem_Draw(c, t);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    rlDrawRenderBatchActive();
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
