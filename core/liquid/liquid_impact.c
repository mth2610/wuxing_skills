#include "core/liquid/liquid_impact.h"

#include "core/particles/particle_manager.h"
#include "core/decals/decal_system.h"
#include "core/force_field.h"
#include "core/liquid/liquid_surface.h"
#include "core/liquid/liquid_pbd_gpu.h"
#include "core/liquid/liquid_body_recipe.h"
#include "core/gfx_quality.h"
#include "core/map_manager.h"
#include "core/presets/vc_material.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>
#include <math.h>
#include <stdlib.h>

#define LIQUID_HERO_MAX_BOUNCES 2
#define LIQUID_SECONDARY_MARKS_PER_FRAME 2
#define LIQUID_WET_MARK_MAX 32
#define LIQUID_FORCE_BODY_MAX_PARTICLES 768
#define LIQUID_BG_BATCH_MAX 64

typedef struct {
    Vector3 position, velocity;
    float radius, life, maxLife;
    unsigned char bounces;
    LiquidDesc material;
    float restitution;
    float tangentRetention;
    bool surfaceAdmitted, active;
} LiquidHeroDroplet;

typedef struct {
    Vector3 position, normal;
    float radius, life, maxLife;
    LiquidDesc material;
    bool surfaceAdmitted, active;
} LiquidWetMark;

typedef struct {
    ForceField field;
    ForceField coreField;
    ParticleEmitterHandle emitter;
    LiquidDesc material;
    LiquidMotionDesc motion;
    Vector3 point, normal, incoming;
    LiquidBodyRecipe recipe;
    float age, scale, kernelRadius, crownDuration;
    bool settling, active;
} LiquidForceBody;

static LiquidHeroDroplet s_hero[LIQUID_IMPACT_MAX_HERO_DROPLETS];
static int s_nextHero = 0;
static int s_secondaryMarksThisFrame = 0;
static LiquidWetMark s_wetMarks[LIQUID_WET_MARK_MAX];
static int s_nextWetMark = 0;
static LiquidForceBody s_forceBodies[LIQUID_IMPACT_MAX_BODIES];
static ParticleConfig s_forceBodySpawn[LIQUID_FORCE_BODY_MAX_PARTICLES];
static ParticleConfig s_bgSpawnBatch[LIQUID_BG_BATCH_MAX];
static ParticleEmitterHandle s_bgEmitter = PARTICLE_EMITTER_INVALID;
static unsigned int s_impactSeed = 1u;
static LiquidImpactCollisionQueryFn s_collisionQuery = NULL;
static void *s_collisionUserData = NULL;
static ForceField s_gravity;
static bool s_gravityReady = false;
static Color s_fluidBody;
static Color s_fluidGlow;
static Color s_fluidSoft;
static LiquidDesc s_fluidMaterial;

static bool LiquidImpact_ColorIsUnset(Color color)
{
    return color.r == 0 && color.g == 0 && color.b == 0;
}

static void LiquidImpact_Basis(Vector3 normal, Vector3 *outTangent, Vector3 *outBitangent)
{
    Vector3 reference = fabsf(normal.y) < 0.95f ? (Vector3){0.0f, 1.0f, 0.0f}
                                                : (Vector3){1.0f, 0.0f, 0.0f};
    *outTangent = Vector3Normalize(Vector3CrossProduct(reference, normal));
    *outBitangent = Vector3Normalize(Vector3CrossProduct(normal, *outTangent));
}

static void LiquidImpact_InitGravity(void)
{
    if (s_gravityReady) return;
    ForceField_Clear(&s_gravity);
    ForceField_AddLayer(&s_gravity, (ForceLayer){
        .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 9.81f
    });
    s_gravityReady = true;
}

static ParticleEmitterHandle LiquidImpact_GetBackgroundEmitter(void)
{
    if (s_bgEmitter != PARTICLE_EMITTER_INVALID) return s_bgEmitter;
    ParticleEmitterDesc desc = {0};
    desc.simulationPolicy = PARTICLE_SIM_AUTO;
    desc.renderMode = PARTICLE_RENDER_BILLBOARD; /* detached micro-spray */
    desc.moduleFlags = PARTICLE_MODULE_GRAVITY | PARTICLE_MODULE_DRAG |
                       PARTICLE_MODULE_COLOR_OVER_LIFE | PARTICLE_MODULE_SIZE_OVER_LIFE |
                       PARTICLE_MODULE_VELOCITY_STRETCH | PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName = "LiquidImpact background shared";
    desc.particle = (ParticleConfig){.forceField = &s_gravity};
    s_bgEmitter = ParticleManager_CreateEmitter(&desc);
    return s_bgEmitter;
}

static void LiquidImpact_EmitBackgroundBatch(const ParticleConfig *particles, int count)
{
    if (!particles || count <= 0) return;
    ParticleEmitterHandle emitter = LiquidImpact_GetBackgroundEmitter();
    if (emitter != PARTICLE_EMITTER_INVALID) {
        ParticleManager_EmitBatch(emitter, particles, count);
    }
}

static void LiquidImpact_SetBodyField(LiquidForceBody *body, LiquidBodyPhase phase, float stepSeconds)
{
    LiquidBodyContext context = {
        .center = body->point,
        .receiverPoint = body->point,
        .receiverNormal = body->normal,
        .incomingVelocity = body->incoming,
        .phaseAge = body->age,
        .stepSeconds = stepSeconds
    };
    LiquidBodyRecipe_BuildField(&body->recipe, phase, &context, false, &body->field);
    LiquidBodyRecipe_BuildField(&body->recipe, phase, &context, true, &body->coreField);
}

static bool LiquidImpact_SpawnForceBody(const LiquidImpactEvent *event,
                                       Vector3 normal, Vector3 incoming,
                                       float scale)
{
    LiquidForceBody *body = NULL;
    for (int i = 0; i < LIQUID_IMPACT_MAX_BODIES; ++i) {
        if (!s_forceBodies[i].active &&
            !ParticleManager_IsForceFieldInUse(&s_forceBodies[i].field) &&
            !ParticleManager_IsForceFieldInUse(&s_forceBodies[i].coreField)) {
            body = &s_forceBodies[i];
            break;
        }
    }
    if (!body) return false;

    *body = (LiquidForceBody){
        .emitter = PARTICLE_EMITTER_INVALID,
        .material = s_fluidMaterial,
        .motion = LiquidMotion_Get(event->motionProfile),
        .point = event->hitPoint,
        .normal = normal,
        .incoming = incoming,
        .scale = scale,
        .kernelRadius = scale * 0.028f,
        .active = true
    };
    body->recipe = (LiquidBodyRecipe){
        .motion = body->motion,
        .radius = scale * 0.30f,
        .kernelRadius = body->kernelRadius
    };
    body->crownDuration = LiquidBodyRecipe_CrownDuration(&body->recipe, incoming, normal);
    LiquidImpact_SetBodyField(body, LIQUID_BODY_IMPACT, 0.0f);

    ParticleEmitterDesc desc = {0};
    const char *cpuEnv = getenv("WUXING_LIQUID_CPU_ONLY");
    bool cpuOnly = cpuEnv && atoi(cpuEnv) != 0;
    desc.simulationPolicy = cpuOnly ? PARTICLE_SIM_CPU_ONLY : PARTICLE_SIM_AUTO;
    desc.renderMode = PARTICLE_RENDER_SURFACE_INPUT;
    desc.moduleFlags = PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName = "LiquidImpact force body";
    desc.particle = (ParticleConfig){.forceField = &body->field};
    body->emitter = ParticleManager_CreateEmitter(&desc);
    if (body->emitter == PARTICLE_EMITTER_INVALID) {
        body->active = false;
        return false;
    }

    const ParticleGPUCaps *caps = ParticleSystem_GetGPUCaps();
    int count = (caps->computeShader && !cpuOnly)
                ? (GfxQuality_Get() >= GFX_HIGH ? 768 : 512)
                : (GfxQuality_Get() <= GFX_LOW ? 192 : 320);
    unsigned int bodySeed = s_impactSeed++;
    for (int i = 0; i < count; ++i) {
        LiquidBodySeed seed = LiquidBodyRecipe_CrownSeed(&body->recipe, event->hitPoint,
            normal, incoming, i, count, bodySeed);
        bool coreParticle = seed.core;
        s_forceBodySpawn[i] = (ParticleConfig){
            .position = seed.position,
            .velocity = seed.velocity,
            .colorStart = VC_WithAlpha(s_fluidSoft, 0),
            .colorEnd = VC_WithAlpha(s_fluidBody, 0),
            .radius = body->kernelRadius,
            .lifetime = body->motion.lifetime,
            .forceField = coreParticle ? &body->coreField : &body->field,
            .forceAxisOrigin = event->hitPoint,
            .forceAxisDir = normal
        };
    }
    ParticleManager_EmitBatch(body->emitter, s_forceBodySpawn, count);
    return true;
}

static int LiquidImpact_BackgroundCount(float force01)
{
    const ParticleGPUCaps *caps = ParticleSystem_GetGPUCaps();
    int base = caps->computeShader ? 12 : 4;
    int extra = caps->computeShader ? 20 : 8;
    if (GfxQuality_Get() <= GFX_LOW) { base = 3; extra = 5; }
    int total = base + (int)(extra * force01);
    return total > LIQUID_BG_BATCH_MAX ? LIQUID_BG_BATCH_MAX : total;
}

static int LiquidImpact_HeroCount(float force01)
{
    int base = GfxQuality_Get() <= GFX_LOW ? 3 : 5;
    int extra = GfxQuality_Get() >= GFX_HIGH ? 9 : 5;
    return base + (int)(extra * force01);
}

static void LiquidImpact_AddResidue(Vector3 point, Vector3 normal, float radius,
                                    float opacity, const LiquidDesc *material, bool surfaceAdmitted)
{
    if (Vector3LengthSqr(normal) < 0.0001f) normal = (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 n = Vector3Normalize(normal);
    LiquidWetMark *mark = &s_wetMarks[s_nextWetMark++ % LIQUID_WET_MARK_MAX];
    float duration = 1.6f + 1.2f * Clamp(opacity, 0.0f, 1.0f);
    *mark = (LiquidWetMark){
        .position = Vector3Add(point, Vector3Scale(n, 0.008f)),
        .normal = n,
        .radius = radius,
        .life = duration,
        .maxLife = duration,
        .material = material ? *material : s_fluidMaterial,
        .surfaceAdmitted = surfaceAdmitted,
        .active = true
    };
}

static bool LiquidImpact_GroundSample(float x, float z, Vector3 *point,
                                      Vector3 *normal, void *userData)
{
    (void)userData;
    if (!MapManager_SampleGroundSurfaceAt(x, z, point, normal)) {
        *point = (Vector3){x, MapManager_GetGroundHeightAt(x, z), z};
        *normal = (Vector3){0.0f, 1.0f, 0.0f};
    }
    return true;
}

static bool LiquidImpact_DefaultGroundHit(Vector3 from, Vector3 to, float radius,
                                         LiquidImpactCollision *outHit)
{
    return LiquidBodyRecipe_SweepGround(from, to, radius, LiquidImpact_GroundSample,
        NULL, &outHit->position, &outHit->normal);
}

static bool LiquidImpact_QueryCollision(Vector3 from, Vector3 to, float radius,
                                       LiquidImpactCollision *outHit)
{
    if (s_collisionQuery)
        return s_collisionQuery(from, to, radius, outHit, s_collisionUserData);
    return LiquidImpact_DefaultGroundHit(from, to, radius, outHit);
}

static void LiquidImpact_SpawnMicroSplash(Vector3 point, Vector3 normal, float radius,
                                         Color body, Color soft)
{
    Vector3 tangent, bitangent;
    LiquidImpact_Basis(normal, &tangent, &bitangent);
    ParticleConfig batch[3];
    for (int i = 0; i < 3; ++i) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(angle)),
                                    Vector3Scale(bitangent, sinf(angle)));
        ParticleConfig p = {0};
        p.position = Vector3Add(point, Vector3Scale(normal, radius + 0.01f));
        p.velocity = Vector3Add(Vector3Scale(normal, 1.0f + 0.4f * i),
                                Vector3Scale(radial, 0.8f + 0.25f * i));
        p.colorStart = VC_WithAlpha(soft, 150);
        p.colorEnd = VC_WithAlpha(body, 0);
        p.radius = radius * 0.55f;
        p.lifetime = 0.22f;
        p.forceField = &s_gravity;
        p.stretchStrength = 0.12f;
        p.stretchMinSpeed = 0.20f;
        batch[i] = p;
    }
    LiquidImpact_EmitBackgroundBatch(batch, 3);
}

void LiquidImpact_SetCollisionQuery(LiquidImpactCollisionQueryFn query, void *userData)
{
    s_collisionQuery = query;
    s_collisionUserData = userData;
}

void LiquidImpact_SpawnWater(const LiquidImpactEvent *event)
{
    if (!event) return;
    LiquidImpact_InitGravity();
    Vector3 normal = event->hitNormal;
    if (Vector3LengthSqr(normal) < 0.0001f) normal = (Vector3){0.0f, 1.0f, 0.0f};
    normal = Vector3Normalize(normal);
    Vector3 impulse = event->impulseDirection;
    if (Vector3LengthSqr(impulse) < 0.0001f) impulse = normal;
    impulse = Vector3Normalize(impulse);
    float force01 = Clamp(event->force01, 0.0f, 1.0f);
    float scale = event->scale > 0.0f ? event->scale : 1.0f;

    LiquidDesc profileMaterial = LiquidSurface_ProfileDesc(event->motionProfile);
    LiquidMotionDesc motionDesc = LiquidMotion_Get(event->motionProfile);
    s_fluidBody = LiquidImpact_ColorIsUnset(event->bodyColor)
                ? profileMaterial.body : event->bodyColor;
    s_fluidGlow = LiquidImpact_ColorIsUnset(event->glowColor)
                ? profileMaterial.glow : event->glowColor;
    s_fluidSoft = LiquidImpact_ColorIsUnset(event->softColor)
                ? profileMaterial.soft : event->softColor;
    s_fluidMaterial = profileMaterial;
    s_fluidMaterial.body = s_fluidBody;
    s_fluidMaterial.glow = s_fluidGlow;
    s_fluidMaterial.soft = s_fluidSoft;

    bool useSurface = LiquidSurface_RequestBody(LIQUID_PRIORITY_CAST, event->hitPoint,
                                                scale * 0.30f, false);
    if (useSurface) {
        LiquidSurface_BindMaterial(&s_fluidMaterial);
        LiquidSurface_SetReconstructionRadiusFor(LIQUID_PRIORITY_CAST, scale * 0.022f);
    }

    Vector3 incoming = event->initialVelocity;
    if (Vector3LengthSqr(incoming) < 0.0001f)
        incoming = Vector3Scale(impulse, scale * (2.0f + force01 * 4.0f));

    /* Use profile-authored restitution rather than a hardcoded constant so
     * mud and lava stay cohesive while water and liquid metal rebound crisply. */
    Vector3 outgoing = LiquidBody_ContactVelocity(incoming, normal,
                                                  fmaxf(motionDesc.restitution, 0.22f * motionDesc.normalLift));
    LiquidImpact_AddResidue(event->hitPoint, normal,
                            scale * (0.30f + 0.65f * force01), force01,
                            &s_fluidMaterial, useSurface);
    if (event->externalBody) return;

    const char *cpuEnv = getenv("WUXING_LIQUID_CPU_ONLY");
    bool cpuOnly = cpuEnv && atoi(cpuEnv) != 0;
    bool pbdRequested = !cpuOnly && !event->forceFieldOnly &&
                        event->backend == LIQUID_IMPACT_BACKEND_PBD;
    bool gpuLiquid = useSurface && pbdRequested &&
                     !LiquidPBDGPU_IsActive() && LiquidPBDGPU_Init();
    if (gpuLiquid) LiquidPBDGPU_SpawnImpact(event->hitPoint, normal, incoming, force01, scale);
    bool forceBody = useSurface && !gpuLiquid &&
                     LiquidImpact_SpawnForceBody(event, normal, incoming, scale);
    Vector3 tangent, bitangent;
    LiquidImpact_Basis(normal, &tangent, &bitangent);

    /* Spawn ballistic hero droplets for fallback, plus a few satellite droplets
     * around force-field bodies for secondary terrain bounces and micro-spray. */
    int heroCount = gpuLiquid ? 0 : (forceBody ? (LiquidImpact_HeroCount(force01) / 3)
                                               : LiquidImpact_HeroCount(force01));
    for (int i = 0; i < heroCount; ++i) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        float spread = 0.35f + ((float)GetRandomValue(0, 1000) / 1000.0f) * 0.65f;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(angle)),
                                    Vector3Scale(bitangent, sinf(angle)));
        float speed = scale * (1.8f + 3.2f * force01) * spread * motionDesc.splashVelocity;
        LiquidHeroDroplet *d = &s_hero[s_nextHero++ % LIQUID_IMPACT_MAX_HERO_DROPLETS];
        Vector3 start = Vector3Add(event->hitPoint, Vector3Scale(normal, 0.03f));
        Vector3 velocity = Vector3Add(Vector3Add(Vector3Scale(outgoing, motionDesc.tangentRetention),
                                                 Vector3Scale(radial, speed * 0.65f)),
                                      Vector3Scale(normal, speed * 0.45f * motionDesc.normalLift));
        float radius = scale * (0.025f + ((float)GetRandomValue(0, 1000) / 1000.0f) * 0.04f);
        float lifetime = 0.35f + ((float)GetRandomValue(0, 1000) / 1000.0f) * 0.45f;
        *d = (LiquidHeroDroplet){
            .position = start,
            .velocity = velocity,
            .radius = radius,
            .life = lifetime,
            .maxLife = lifetime,
            .restitution = motionDesc.restitution,
            .tangentRetention = motionDesc.tangentRetention,
            .surfaceAdmitted = useSurface,
            .material = s_fluidMaterial,
            .active = true
        };
    }

    int bgCount = (gpuLiquid || forceBody) ? (LiquidImpact_BackgroundCount(force01) / 3)
                                           : LiquidImpact_BackgroundCount(force01);
    if (bgCount > LIQUID_BG_BATCH_MAX) bgCount = LIQUID_BG_BATCH_MAX;
    for (int i = 0; i < bgCount; ++i) {
        float angle = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(angle)),
                                    Vector3Scale(bitangent, sinf(angle)));
        ParticleConfig p = {0};
        p.position = Vector3Add(event->hitPoint, Vector3Scale(normal, 0.025f));
        p.velocity = Vector3Add(outgoing,
                                Vector3Add(Vector3Scale(radial, scale * (0.8f + force01) * motionDesc.splashVelocity),
                                           Vector3Scale(normal, scale * 1.0f * motionDesc.normalLift)));
        p.colorStart = VC_WithAlpha(s_fluidSoft, 170);
        p.colorEnd = VC_WithAlpha(s_fluidBody, 0);
        p.radius = scale * 0.022f;
        p.lifetime = 0.35f;
        p.forceField = &s_gravity;
        p.stretchStrength = 0.18f;
        p.stretchMinSpeed = 0.20f;
        s_bgSpawnBatch[i] = p;
    }
    if (bgCount > 0) {
        LiquidImpact_EmitBackgroundBatch(s_bgSpawnBatch, bgCount);
    }
}

void LiquidImpact_Update(float dt)
{
    LiquidImpact_InitGravity();
    if (LiquidPBDGPU_IsActive()) LiquidPBDGPU_Update(dt, 0.0f);

    for (int i = 0; i < LIQUID_IMPACT_MAX_BODIES; ++i) {
        LiquidForceBody *body = &s_forceBodies[i];
        if (!body->active) continue;
        body->age += dt;
        LiquidBodyPhase phase = LiquidBodyRecipe_PhaseAt(body->age, body->crownDuration);
        body->settling = (phase == LIQUID_BODY_SETTLE);
        LiquidBodyPhase fieldPhase = LiquidBodyRecipe_IntegrationPhaseAt(body->age, dt, body->crownDuration);
        LiquidImpact_SetBodyField(body, fieldPhase, dt);
        if (body->age >= body->motion.lifetime) {
            if (body->emitter != PARTICLE_EMITTER_INVALID)
                ParticleManager_DestroyEmitter(body->emitter);
            body->emitter = PARTICLE_EMITTER_INVALID;
            body->active = false;
        }
    }

    for (int i = 0; i < LIQUID_WET_MARK_MAX; ++i) {
        LiquidWetMark *mark = &s_wetMarks[i];
        if (!mark->active) continue;
        mark->life -= dt;
        if (mark->life <= 0.0f) mark->active = false;
    }

    s_secondaryMarksThisFrame = 0;
    for (int i = 0; i < LIQUID_IMPACT_MAX_HERO_DROPLETS; ++i) {
        LiquidHeroDroplet *d = &s_hero[i];
        if (!d->active) continue;
        d->life -= dt;
        if (d->life <= 0.0f) { d->active = false; continue; }
        Vector3 from = d->position;
        d->velocity.y -= 9.81f * dt;
        d->velocity = Vector3Scale(d->velocity, expf(-1.4f * dt));
        Vector3 to = Vector3Add(from, Vector3Scale(d->velocity, dt));
        LiquidImpactCollision hit;
        if (!LiquidImpact_QueryCollision(from, to, d->radius, &hit)) {
            d->position = to;
            continue;
        }
        if (Vector3LengthSqr(hit.normal) < 0.0001f) hit.normal = (Vector3){0.0f, 1.0f, 0.0f};
        hit.normal = Vector3Normalize(hit.normal);
        d->position = Vector3Add(hit.position, Vector3Scale(hit.normal, d->radius + 0.004f));
        Vector3 rebounded = LiquidBody_ContactVelocity(d->velocity, hit.normal, d->restitution);
        float vn = Vector3DotProduct(rebounded, hit.normal);
        Vector3 vt = Vector3Subtract(rebounded, Vector3Scale(hit.normal, vn));
        float keep = d->tangentRetention > 0.0f ? d->tangentRetention : 0.65f;
        d->velocity = Vector3Add(Vector3Scale(hit.normal, vn), Vector3Scale(vt, keep));
        d->bounces++;
        LiquidImpact_SpawnMicroSplash(hit.position, hit.normal, d->radius,
                                      d->material.body, d->material.soft);
        if (s_secondaryMarksThisFrame < LIQUID_SECONDARY_MARKS_PER_FRAME) {
            LiquidImpact_AddResidue(hit.position, hit.normal, d->radius * 4.5f, 0.35f,
                                    &d->material, d->surfaceAdmitted);
            s_secondaryMarksThisFrame++;
        }
        if (d->bounces >= LIQUID_HERO_MAX_BOUNCES || Vector3LengthSqr(d->velocity) < 0.2025f) {
            d->active = false;
        }
    }
}

void LiquidImpact_Draw(void)
{
    bool anyBodyActive = LiquidPBDGPU_IsActive();
    for (int i = 0; i < LIQUID_IMPACT_MAX_BODIES; ++i) {
        LiquidForceBody *body = &s_forceBodies[i];
        ParticleRenderStream stream;
        if (!body->active || body->emitter == PARTICLE_EMITTER_INVALID) continue;
        anyBodyActive = true;
        LiquidSurface_BindMaterial(&body->material);
        LiquidSurface_SetReconstructionRadiusFor(LIQUID_PRIORITY_CAST, body->kernelRadius);
        LiquidSurface_HintBody(body->point, LiquidBodyRecipe_EstimateRadius(&body->recipe, body->incoming, body->age));
        if (ParticleManager_GetSurfaceStream(body->emitter, &stream))
            LiquidSurface_SubmitParticleStream(&stream);
    }
    for (int i = 0; i < LIQUID_IMPACT_MAX_HERO_DROPLETS; ++i) {
        LiquidHeroDroplet *d = &s_hero[i];
        if (!d->active || !d->surfaceAdmitted) continue;
        anyBodyActive = true;
        float fade = d->maxLife > 0.0f ? Clamp(d->life / (d->maxLife * 0.25f), 0.25f, 1.0f) : 1.0f;
        float r = d->radius * 1.75f * fade;
        LiquidSurface_BindMaterial(&d->material);
        LiquidSurface_SetReconstructionRadiusFor(LIQUID_PRIORITY_CAST, r);
        LiquidSurface_RegisterParticle(d->position, r);
    }
    /* Anchor active splash bases with thin contact residue puddles only while
     * an impact body is already active so idle frames never pay SSF passes. */
    if (anyBodyActive) {
        for (int i = 0; i < LIQUID_WET_MARK_MAX; ++i) {
            LiquidWetMark *mark = &s_wetMarks[i];
            if (!mark->active || !mark->surfaceAdmitted) continue;
            float life01 = mark->maxLife > 0.0f ? Clamp(mark->life / mark->maxLife, 0.0f, 1.0f) : 0.0f;
            float spread = mark->radius * (0.32f + 0.18f * (1.0f - life01)) * sqrtf(life01);
            if (spread <= 0.01f) continue;
            LiquidSurface_BindMaterial(&mark->material);
            LiquidSurface_RegisterEllipsoid(mark->position,
                (Vector3){spread, fmaxf(spread * 0.12f, 0.012f), spread});
        }
    }
}

void LiquidImpact_GetStats(int *active, int *max)
{
    int count = 0;
    for (int i = 0; i < LIQUID_IMPACT_MAX_HERO_DROPLETS; ++i) if (s_hero[i].active) count++;
    for (int i = 0; i < LIQUID_IMPACT_MAX_BODIES; ++i) if (s_forceBodies[i].active) count++;
    if (LiquidPBDGPU_IsActive()) count++;
    if (active) *active = count;
    if (max) *max = LIQUID_IMPACT_MAX_HERO_DROPLETS + LIQUID_IMPACT_MAX_BODIES + 1;
}
