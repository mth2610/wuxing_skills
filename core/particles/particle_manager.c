#include "core/particles/particle_manager.h"

#include "core/particles/gpu/particle_gpu_legacy.h"
#include "core/particles/particle_field_capabilities.h"
#include "rlgl.h"
#include "raymath.h"
#include <string.h>
#include <stdlib.h>

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

static bool ParticleManager_RequiresCpuFacing(const ParticleConfig *particle)
{
    const Vector3 axis = particle->render.facingDirection;
    return particle->render.facingMode == VFX_FACING_CROSS_BILLBOARD &&
           (axis.x != 0.0f || axis.y != 0.0f || axis.z != 0.0f);
}

#include "core/particles/particle_motion_capabilities.h"

/* The GPU billboard backend owns one shared draw texture: DefaultSprite.
 * It cannot silently replace an authored texture without also replacing that
 * texture's colour language (the default sprite has an orange rim). Keep
 * custom-texture particles on the CPU path, which binds their texture per
 * particle and therefore preserves the element palette. */
static bool ParticleManager_RequiresCpuTexture(const ParticleConfig *particle)
{
    return particle->render.texture.id != 0 &&
           particle->render.texture.id != ParticleSystem_DefaultSprite().id;
}

/* A textureless billboard selects the shared default material. Its RGB owns
 * the white-core/orange-rim structure; particle authoring still controls alpha
 * and lifetime. Gradients are explicit colour treatments, not defaults. */
static Color ParticleManager_DefaultSpriteColor(Color color, bool defaultSprite)
{
    if (defaultSprite)
        color.r = color.g = color.b = 255;
    return color;
}

static void ParticleManager_ApplySource(ParticleEmitterRuntime *emitter,
                                        ParticleConfig *particle)
{
    if (!Emission_ApplyParticleSource(&emitter->desc.source, particle) && !emitter->warned) {
        emitter->warned = true;
        TraceLog(LOG_WARNING, "ParticleManager: emitter '%s' has an invalid mesh source",
                 emitter->desc.debugName ? emitter->desc.debugName : "unnamed");
    }
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
        bool requiresCpuDynamics = ParticleMotion_RequiresCpuDynamics(&e->desc.particle);
        bool gpuOK = ParticleManager_GPUCanRun(e->desc.moduleFlags);
        if (ParticleManager_RequiresCpuFacing(&e->desc.particle)) gpuOK = false;
        if (requiresCpuDynamics) gpuOK = false;
        if (ParticleManager_RequiresCpuTexture(&e->desc.particle)) gpuOK = false;
        VFXResolvedAppearance appearance = ParticleManager_ResolveAppearance(&e->desc.particle);
        // Named appearances retain their material compatibility gate. Spatial
        // INHERIT receivers use explicit alpha/additive GPU buckets.
        if (e->desc.particle.render.appearance != VFX_APPEARANCE_INHERIT &&
            appearance.surface != VFX_SURFACE_ADDITIVE)
            gpuOK = false;
        e->gpu = e->desc.simulationPolicy == PARTICLE_SIM_GPU_ONLY ||
                 (e->desc.simulationPolicy == PARTICLE_SIM_AUTO && gpuOK);
        if(e->desc.particle.physics.spatialMotionOnly && getenv("WUXING_PARTICLE_MOTION_TRACE"))
            TraceLog(LOG_INFO,"PARTICLE_MOTION: %s backend=%s compute=%d",
                e->desc.debugName?e->desc.debugName:"spatial",e->gpu?"GPU":"CPU",s_caps.computeShader);
        e->status = PARTICLE_EMITTER_OK;
        if (e->desc.simulationPolicy == PARTICLE_SIM_GPU_ONLY && !gpuOK) {
            e->gpu = false;
            e->status = (requiresCpuDynamics ||
                         (e->desc.moduleFlags & (PARTICLE_MODULE_GAMEPLAY_CALLBACK | PARTICLE_MODULE_GAMEPLAY_COLLISION)))
                            ? PARTICLE_EMITTER_UNSUPPORTED_MODULE : PARTICLE_EMITTER_GPU_UNAVAILABLE;
            s_stats.rejectedGpuOnlyEmitters++;
        } else if (e->desc.simulationPolicy == PARTICLE_SIM_AUTO && !gpuOK && e->desc.moduleFlags) {
            s_stats.fallbackCount++;
        }
        if (!e->gpu && (!ParticleField_CpuSupported(e->desc.particle.forceField) ||
            (e->desc.particle.travelPath && !ParticleField_CpuSupported(
                e->desc.particle.travelPath->arrivalForceField)) ||
            (e->desc.moduleFlags & PARTICLE_MODULE_VECTOR_FIELD)))
            e->status = PARTICLE_EMITTER_UNSUPPORTED_MODULE;
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
        if (e->active && !e->warned) { TraceLog(LOG_WARNING, "ParticleManager: emitter '%s' rejected (%d)", e->desc.debugName ? e->desc.debugName : "unnamed", e->status); e->warned = true; }
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
        if (!ParticleField_CpuSupported(e->desc.particle.forceField) ||
            (e->desc.moduleFlags & PARTICLE_MODULE_VECTOR_FIELD)) {
            e->status = PARTICLE_EMITTER_UNSUPPORTED_MODULE;
            return;
        }
    }
    for (int i = 0; i < count; ++i) {
        ParticleConfig spawned = e->desc.particle;
        ParticleManager_ApplySource(e, &spawned);
        if (e->gpu) {
            const ParticleConfig *p = &spawned;
            bool defaultSpriteColors = p->render.texture.id == 0 &&
                                       p->render.gradient == NULL;
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
                .colorStart=VFXContrast_ApplyColor(ParticleManager_DefaultSpriteColor(p->colorStart, defaultSpriteColors), appearance.contrast, layer),
                .colorEnd=VFXContrast_ApplyColor(ParticleManager_DefaultSpriteColor(p->colorEnd, defaultSpriteColors), appearance.contrast, layer), .radius=p->radius,
                .lifetime=p->lifetime, .forceField=p->forceField, .stretchStrength=p->stretchStrength,
                .stretchMinSpeed=p->stretchMinSpeed, .collisionEnabled=p->collisionEnabled,
                .collisionElasticity=p->collisionElasticity, .collisionFloorY=p->collisionFloorY,
                .axisOrigin=p->forceAxisOrigin, .axisDir=p->forceAxisDir,
                .travelPath=p->travelPath, .onTargetEmit=p->onTargetEmit,
                .onTargetEmitCount=p->onTargetEmitCount,
                .emissiveBoost=boost, .windInfluence=p->windInfluence,
                .emitterId=e->ownerId,
                .renderMode=(int)e->desc.renderMode,
                .spatialMotionOnly=p->physics.spatialMotionOnly,
                .receiveMotionFields=p->physics.receiveMotionFields, .dynamics=p->physics.dynamics,
                .initialImpulseNs=p->physics.initialImpulseNs,
                .initialAccelerationMps2=p->physics.initialAccelerationMps2,
                .constantForceNewtons=p->physics.constantForceNewtons,
                .drag=p->physics.spatialMotionOnly?p->drag:0, .blendMode=p->render.blendMode });
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
        bool defaultSpriteColors = p->render.texture.id == 0 &&
                                   p->render.gradient == NULL;
        bool appearanceFitsGpu = p->render.appearance == VFX_APPEARANCE_INHERIT ||
                                 appearance.surface == VFX_SURFACE_ADDITIVE;
        if (e->gpu && appearanceFitsGpu && !ParticleManager_RequiresCpuFacing(p) &&
            !ParticleMotion_RequiresCpuDynamics(p) &&
            !ParticleManager_RequiresCpuTexture(p)) {
            VFXContrastLayer layer = appearance.surface == VFX_SURFACE_ADDITIVE
                                         ? VFX_CONTRAST_EMISSION
                                         : VFX_CONTRAST_BODY;
            float boost = appearance.emissionIntensity > 0.0f
                              ? appearance.emissionIntensity : 1.0f;
            if (layer == VFX_CONTRAST_EMISSION)
                boost = VFXContrast_ApplyEmissionIntensity(
                    boost, appearance.contrast);
            GpuParticleSystem_Spawn((GpuParticleConfig){ .position=p->position, .velocity=p->velocity,
                .colorStart=VFXContrast_ApplyColor(ParticleManager_DefaultSpriteColor(p->colorStart, defaultSpriteColors), appearance.contrast, layer),
                .colorEnd=VFXContrast_ApplyColor(ParticleManager_DefaultSpriteColor(p->colorEnd, defaultSpriteColors), appearance.contrast, layer), .radius=p->radius,
                .lifetime=p->lifetime, .forceField=p->forceField, .stretchStrength=p->stretchStrength,
                .stretchMinSpeed=p->stretchMinSpeed, .collisionEnabled=p->collisionEnabled,
                .collisionElasticity=p->collisionElasticity, .collisionFloorY=p->collisionFloorY,
                .axisOrigin=p->forceAxisOrigin, .axisDir=p->forceAxisDir,
                .travelPath=p->travelPath, .onTargetEmit=p->onTargetEmit,
                .onTargetEmitCount=p->onTargetEmitCount,
                .emissiveBoost=boost, .windInfluence=p->windInfluence,
                .emitterId=e->ownerId,
                .renderMode=(int)e->desc.renderMode,
                .spatialMotionOnly=p->physics.spatialMotionOnly,
                .receiveMotionFields=p->physics.receiveMotionFields, .dynamics=p->physics.dynamics,
                .initialImpulseNs=p->physics.initialImpulseNs,
                .initialAccelerationMps2=p->physics.initialAccelerationMps2,
                .constantForceNewtons=p->physics.constantForceNewtons,
                .drag=p->physics.spatialMotionOnly?p->drag:0, .blendMode=p->render.blendMode });
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


static bool ParticleManager_SurfaceRoutes(const ParticleSurfaceCaptureStream *streams,
                                          int count, GpuSurfaceRoute *routes)
{
    if (!streams || count <= 0 || count > PARTICLE_SURFACE_CAPTURE_MAX_STREAMS) return false;
    for (int i = 0; i < count; ++i) {
        if (streams[i].stream.mode != PARTICLE_RENDER_SURFACE_INPUT ||
            streams[i].stream.backend != PARTICLE_RENDER_BACKEND_GPU) return false;
        routes[i] = (GpuSurfaceRoute){streams[i].stream.ownerId, streams[i].materialId};
    }
    return true;
}

bool ParticleManager_DrawSurfaceStreams(const ParticleSurfaceCaptureStream *streams,
                                        int count, Camera3D camera, Texture2D texture)
{
    GpuSurfaceRoute routes[PARTICLE_SURFACE_CAPTURE_MAX_STREAMS];
    if (!ParticleManager_SurfaceRoutes(streams, count, routes)) return false;
    return GpuParticleSystem_DrawSurfaceEmitters(camera, texture, routes, count);
}

bool ParticleManager_DrawSurfaceBackStreams(const ParticleSurfaceCaptureStream *streams,
                                            int count, Camera3D camera)
{
    GpuSurfaceRoute routes[PARTICLE_SURFACE_CAPTURE_MAX_STREAMS];
    if (!ParticleManager_SurfaceRoutes(streams, count, routes)) return false;
    return GpuParticleSystem_DrawSurfaceBackEmitters(camera, routes, count);
}

int ParticleManager_GetSurfaceCaptureInstanceCount(void)
{
    return GpuParticleSystem_GetSurfaceCaptureInstanceCount();
}

bool ParticleManager_IsForceFieldInUse(const ForceField *field)
{
    if (!s_initialized || !field) return false;
    return ParticleSystem_IsForceFieldInUse(field) ||
           GpuParticleSystem_IsForceFieldInUse(field);
}

void ParticleManager_SetSurfaceCaptureFrontDepth(Texture2D texture)
{
    GpuParticleSystem_SetSurfaceCaptureFrontDepth(texture);
}

int ParticleManager_CountSurfaceSamples(const ParticleRenderStream *stream)
{
    if (!stream || stream->mode != PARTICLE_RENDER_SURFACE_INPUT ||
        stream->backend != PARTICLE_RENDER_BACKEND_CPU) return 0;
    return ParticleSystem_CountSurfaceSamples(stream->ownerId);
}

int ParticleManager_CopySurfaceSamplesSpaced(const ParticleRenderStream *stream,
                                            ParticleSurfaceSample *samples, int maxSamples)
{
    if (!stream || stream->mode != PARTICLE_RENDER_SURFACE_INPUT ||
        stream->backend != PARTICLE_RENDER_BACKEND_CPU) return 0;
    return ParticleSystem_GetSurfaceSamplesSpaced(stream->ownerId, samples, maxSamples);
}

void ParticleManager_Update(float dt) { if (!s_initialized) return; UpdateParticles(dt); GpuParticleSystem_Update(dt); ParticleManager_RefreshStats(); }
void ParticleManager_Draw(Camera3D c, Texture2D t)
{
    if (!s_initialized) return;

    // Each backend owns its blend state; compute buckets separate alpha bodies
    // from additive emission without downloading particle positions.
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    DrawParticles(c, t);
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADDITIVE);
    GpuParticleSystem_Draw(c, ParticleSystem_DefaultSprite());
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
    GpuParticleSystem_DrawLayer(c,ParticleSystem_DefaultSprite(),1);
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
    GpuParticleSystem_DrawLayer(c, ParticleSystem_DefaultSprite(),2);
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
