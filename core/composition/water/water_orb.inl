#include "core/liquid/liquid_body_recipe.h"
#include "core/liquid/liquid_metaball3d.h"
#include "core/motion/motion_fields.h"
#include "core/map_manager.h"

// water_orb.inl — Water orb: a coherent liquid projectile driven by motion fields
// and force fields, supporting both 3D raymarched metaballs and SSF.

#define WATER_ORB_MAX       2
#define WATER_ORB_PARTICLES 2048

typedef enum { WATER_ORB_FLIGHT, WATER_ORB_IMPACT, WATER_ORB_SETTLE } WaterOrbPhase;

typedef struct {
    Vector3 start, target, center, velocity, hitNormal, impactVelocity;
    Vector3 impactPos;
    float age, phaseAge, travelTime, radius, crownDuration, settleDuration;
    Color body, glow, soft;
    LiquidDesc material;
    LiquidMotionDesc motion;
    LiquidBodyRecipe recipe;
    ForceField field;
    MotionFieldHandle motionField;
    ParticleEmitterHandle emitter;
    WaterOrbPhase phase;
    bool surfaceAdmitted, active;
    VFX_LiquidOrbConfig config;
} WaterOrb;

static WaterOrb s_waterOrbs[WATER_ORB_MAX];
static int s_nextWaterOrb = 0;
static ParticleConfig s_waterOrbSpawn[WATER_ORB_PARTICLES];
static VFX_LiquidOrbConfig s_liveLiquidOrbConfig;
static bool s_liveLiquidOrbConfigInit = false;

VFX_LiquidOrbConfig VFX_LiquidOrb_DefaultConfig(void)
{
    VFX_LiquidOrbConfig c = {0};
    c.start = (Vector3){-2.0f, 1.2f, 0.0f};
    c.target = (Vector3){2.5f, 1.8f, 0.8f};
    c.speed = 9.0f;
    c.radius = 0.38f;
    c.swirlSpeed = 4.0f;
    c.turbulenceSpeed = 0.8f;
    c.pullForce = 3.5f;
    c.guidancePreset = GUIDE_BALANCED;
    c.motionProfile = LIQUID_MOTION_WATER;
    c.renderBackend = LIQUID_RENDER_METABALL3D;
    c.blobCount = 16;
    c.blobRadius = 0.18f;
    c.threshold = 0.42f;
    c.ior = 1.333f;
    c.roughness = 0.04f;
    c.opacity = 1.2f;
    return c;
}

VFX_LiquidOrbConfig *VFX_LiquidOrb_GetLiveConfig(void)
{
    if (!s_liveLiquidOrbConfigInit) {
        s_liveLiquidOrbConfig = VFX_LiquidOrb_DefaultConfig();
        s_liveLiquidOrbConfigInit = true;
    }
    return &s_liveLiquidOrbConfig;
}

int VFX_LiquidOrb_GetParams(VFX_LiquidOrbConfig *c, VFX_ParamDef *out, int max)
{
    if (!c || !out || max <= 0) return 0;
    int n = 0;
    static const char *const backendNames[] = {"Metaball 3D", "Screen Space Fluid"};
    static const char *const profileNames[] = {"Water", "Poison", "Mud", "Lava", "Liquid Metal"};
    static const char *const guidanceNames[] = {"Manual", "Loose", "Balanced", "Tight"};

    #define ORB_PARAM(pname, pgroup, ptype, pval, plo, phi, pstep, pminInt, pmaxInt, pnames, pcount) \
        do { if (n < max) out[n++] = (VFX_ParamDef){.name=pname, .group=pgroup, .type=ptype, \
            .valPtr=pval, .minFloat=plo, .maxFloat=phi, .stepFloat=pstep, \
            .minInt=pminInt, .maxInt=pmaxInt, .enumNames=pnames, .enumCount=pcount}; } while(0)

    ORB_PARAM("Render backend", "Liquid", VFX_PARAM_ENUM, &c->renderBackend, 0, 1, 0, 0, 1, backendNames, 2);
    ORB_PARAM("Motion profile", "Liquid", VFX_PARAM_ENUM, &c->motionProfile, 0, 4, 0, 0, 4, profileNames, 5);
    ORB_PARAM("Body radius m", "Liquid", VFX_PARAM_FLOAT, &c->radius, 0.10f, 2.0f, 0.05f, 0, 0, NULL, 0);
    ORB_PARAM("Blob count", "Liquid", VFX_PARAM_INT, &c->blobCount, 4, 64, 1, 4, 64, NULL, 0);
    ORB_PARAM("Blob radius m", "Liquid", VFX_PARAM_FLOAT, &c->blobRadius, 0.05f, 0.5f, 0.01f, 0, 0, NULL, 0);
    ORB_PARAM("Threshold", "Liquid", VFX_PARAM_FLOAT, &c->threshold, 0.20f, 0.85f, 0.02f, 0, 0, NULL, 0);

    ORB_PARAM("Travel speed m/s", "Field", VFX_PARAM_FLOAT, &c->speed, 1.0f, 30.0f, 0.5f, 0, 0, NULL, 0);
    ORB_PARAM("Swirl speed m/s", "Field", VFX_PARAM_FLOAT, &c->swirlSpeed, -20.0f, 20.0f, 0.5f, 0, 0, NULL, 0);
    ORB_PARAM("Turbulence m/s", "Field", VFX_PARAM_FLOAT, &c->turbulenceSpeed, 0.0f, 20.0f, 0.5f, 0, 0, NULL, 0);
    ORB_PARAM("Pull force N", "Field", VFX_PARAM_FLOAT, &c->pullForce, 0.0f, 10.0f, 0.1f, 0, 0, NULL, 0);
    ORB_PARAM("Guidance", "Field", VFX_PARAM_ENUM, &c->guidancePreset, 0, 3, 0, 0, 3, guidanceNames, 4);

    ORB_PARAM("Refraction IOR", "Optics", VFX_PARAM_FLOAT, &c->ior, 1.0f, 2.5f, 0.02f, 0, 0, NULL, 0);
    ORB_PARAM("Roughness", "Optics", VFX_PARAM_FLOAT, &c->roughness, 0.02f, 1.0f, 0.02f, 0, 0, NULL, 0);
    ORB_PARAM("Opacity /m", "Optics", VFX_PARAM_FLOAT, &c->opacity, 0.1f, 20.0f, 0.2f, 0, 0, NULL, 0);

    #undef ORB_PARAM
    return n;
}

static void WaterOrb_Clear(WaterOrb *orb)
{
    if (orb->active && orb->emitter != PARTICLE_EMITTER_INVALID)
        ParticleManager_DestroyEmitter(orb->emitter);
    if (orb->motionField != MOTION_FIELD_INVALID)
        MotionFields_Stop(orb->motionField);
    orb->motionField = MOTION_FIELD_INVALID;
    orb->emitter = PARTICLE_EMITTER_INVALID;
    orb->active = false;
}

static void WaterOrb_SetPhaseField(WaterOrb *orb, LiquidBodyPhase phase, float stepSeconds)
{
    LiquidBodyContext context = {.center = orb->center, .receiverPoint = orb->target,
        .receiverNormal = orb->hitNormal, .incomingVelocity = orb->velocity,
        .phaseAge = orb->phaseAge, .stepSeconds = stepSeconds, .applyContactImpulse = true};
    LiquidBodyRecipe_BuildField(&orb->recipe, phase, &context, false, &orb->field);
}

void VFX_ComposeLiquidOrbEx(const VFX_LiquidOrbConfig *config)
{
    if (!config) return;
    LiquidMotionProfile profile = (LiquidMotionProfile)config->motionProfile;
    LiquidDesc material = LiquidSurface_ProfileDesc(profile);
    if (config->ior > 1.0f) material.ior = config->ior;
    if (config->roughness > 0.0f) material.roughnessScale = config->roughness * 5.0f;
    if (config->opacity > 0.0f) material.opacityPerMetre = config->opacity;

    LiquidMotionDesc motion = LiquidMotion_Get(profile);
    WaterOrb *orb = NULL;
    for (int i = 0; i < WATER_ORB_MAX; ++i) {
        int slot = (s_nextWaterOrb + i) % WATER_ORB_MAX;
        if (!s_waterOrbs[slot].active &&
            !ParticleManager_IsForceFieldInUse(&s_waterOrbs[slot].field)) {
            orb = &s_waterOrbs[slot];
            s_nextWaterOrb = (slot + 1) % WATER_ORB_MAX;
            break;
        }
    }
    if (!orb) return;
    WaterOrb_Clear(orb);

    Vector3 flight = Vector3Subtract(config->target, config->start);
    float dist = Vector3Length(flight);
    float speed = config->speed > 0.1f ? config->speed : 8.0f;
    float travelTime = fmaxf(dist / speed, 0.25f);

    *orb = (WaterOrb){.start = config->start, .target = config->target, .center = config->start,
        .velocity = Vector3Scale(flight, 1.0f / travelTime), .travelTime = travelTime,
        .radius = config->radius > 0.05f ? config->radius : 0.44f, .hitNormal = (Vector3){0, 1, 0},
        .body = material.body, .glow = material.glow, .soft = material.soft,
        .material = material, .motion = motion,
        .emitter = PARTICLE_EMITTER_INVALID, .motionField = MOTION_FIELD_INVALID,
        .phase = WATER_ORB_FLIGHT, .active = true, .config = *config};

    orb->recipe = (LiquidBodyRecipe){.motion = motion, .radius = orb->radius,
        .kernelRadius = orb->radius * 0.09f};
    orb->crownDuration = LiquidBodyRecipe_CrownDuration(&orb->recipe, orb->velocity, orb->hitNormal);
    WaterOrb_SetPhaseField(orb, LIQUID_BODY_FLIGHT, 0.0f);

    // Create physical motion field guide along trajectory
    MotionFieldRecipe r = {
        .kind = MOTION_RECIPE_MOVING_GUIDE,
        .origin = config->start,
        .axis = flight,
        .radiusM = orb->radius * 2.2f,
        .speedMps = speed,
        .swirlSpeedMps = config->swirlSpeed,
        .turbulenceSpeedMps = config->turbulenceSpeed,
        .maxForceNewtons = config->pullForce,
        .forwardForceNewtons = config->pullForce * 0.4f,
        .guidance = (GuidePreset)config->guidancePreset,
        .receiverMask = MOTION_RECEIVER_ALL_COMPONENTS,
        .durationSec = travelTime + motion.lifetime
    };
    FieldDesc fieldDesc;
    if (MotionFieldRecipe_Build(&r, &fieldDesc)) {
        orb->motionField = MotionFields_CreateField(&fieldDesc);
    }

    bool forceSurface = getenv("WUXING_LIQUID_FORCE_SURFACE") &&
                      atoi(getenv("WUXING_LIQUID_FORCE_SURFACE")) != 0;
    orb->surfaceAdmitted = forceSurface ||
        (orb->config.renderBackend == LIQUID_RENDER_SSF &&
         LiquidSurface_RequestBody(LIQUID_PRIORITY_CAST, config->start, orb->radius, false));

    ParticleEmitterDesc desc = {0};
    bool cpuOnly = (orb->config.renderBackend == LIQUID_RENDER_METABALL3D) ||
                   (getenv("WUXING_LIQUID_CPU_ONLY") && atoi(getenv("WUXING_LIQUID_CPU_ONLY")) != 0);
    desc.simulationPolicy = cpuOnly ? PARTICLE_SIM_CPU_ONLY : PARTICLE_SIM_AUTO;
    desc.renderMode = orb->surfaceAdmitted ? PARTICLE_RENDER_SURFACE_INPUT : PARTICLE_RENDER_BILLBOARD;
    desc.moduleFlags=PARTICLE_MODULE_FORCE_FIELD;
    desc.debugName = "WaterOrb SSF batch";
    desc.particle = (ParticleConfig){.forceField = &orb->field};
    orb->emitter = ParticleManager_CreateEmitter(&desc);
    if (orb->emitter == PARTICLE_EMITTER_INVALID) { orb->active = false; return; }

    const ParticleGPUCaps *caps = ParticleSystem_GetGPUCaps();
    int count=caps->computeShader ? ((orb->config.renderBackend == LIQUID_RENDER_METABALL3D)
        ? (orb->config.blobCount > 0 ? orb->config.blobCount : 24)
        : (!cpuOnly
            ? (GfxQuality_Get() >= GFX_HIGH ? WATER_ORB_PARTICLES :
               GfxQuality_Get() <= GFX_LOW ? 768 : 1536)
            : (GfxQuality_Get() <= GFX_LOW ? 192 : 384)))
        : (GfxQuality_Get() <= GFX_LOW ? 192 : 384);

    for (int i = 0; i < count; ++i) {
        Vector3 offset = LiquidBodyRecipe_VolumeOffset(orb->radius * 0.80f, i, count, 0u);
        s_waterOrbSpawn[i] = (ParticleConfig){
            .position = Vector3Add(config->start, offset),
            .velocity = orb->velocity,
            .colorStart = VC_WithAlpha(orb->soft, orb->surfaceAdmitted ? 0 : 150),
            .colorEnd = VC_WithAlpha(orb->body, 0),
            .radius = orb->config.blobRadius > 0.01f ? orb->config.blobRadius : (0.44f * 0.09f),
            .lifetime = orb->travelTime + motion.lifetime,
            .forceField = &orb->field,
            .forceAxisOrigin = config->target,
            .forceAxisDir = orb->hitNormal
        };
        s_waterOrbSpawn[i].physics.receiveMotionFields = false;
        s_waterOrbSpawn[i].physics.spatialMotionOnly = false;
    }
    ParticleManager_EmitBatch(orb->emitter, s_waterOrbSpawn, count);

    if (orb->surfaceAdmitted) {
        LiquidSurface_BindMaterial(&orb->material);
        LiquidSurface_SetReconstructionRadius(0.44f * 0.09f);
    }
}

void VFX_LiquidOrb_Spawn(Vector3 start, Vector3 target, LiquidMotionProfile profile)
{
    VFX_LiquidOrbConfig *cfg = VFX_LiquidOrb_GetLiveConfig();
    cfg->start = start;
    cfg->target = target;
    if (cfg->motionProfile == 0 && profile != LIQUID_MOTION_WATER)
        cfg->motionProfile = (int)profile;
    VFX_ComposeLiquidOrbEx(cfg);
}

void VFX_ComposeWaterOrb(Vector3 start, Vector3 target)
{
    LiquidMotionProfile profile = LIQUID_MOTION_WATER;
    const char *profileOverride = getenv("WUXING_LIQUID_ORB_PROFILE");
    if (!profileOverride) profileOverride = getenv("WUXING_FLUID_ORB_PROFILE");
    if (profileOverride) {
        int value = atoi(profileOverride);
        if (value >= LIQUID_MOTION_WATER && value <= LIQUID_MOTION_LIQUID_METAL)
            profile = (LiquidMotionProfile)value;
    }
    VFX_LiquidOrb_Spawn(start,target,profile);
}

static void WaterOrb_Update(float dt)
{
    for (int i = 0; i < WATER_ORB_MAX; ++i) {
        WaterOrb *orb = &s_waterOrbs[i];
        if (!orb->active) continue;
        if (orb->config.renderBackend == LIQUID_RENDER_METABALL3D) {
            SceneTargets_RequestSceneSnapshot();
        }
        orb->age += dt;

        if (orb->phase == WATER_ORB_FLIGHT) {
            float t = Clamp(orb->age / orb->travelTime, 0.0f, 1.0f);
            orb->center = Vector3Lerp(orb->start, orb->target, t);
            orb->field.layers[0].origin = orb->center;
            orb->field.layers[1].origin = orb->center;

            bool hit = false;
            Vector3 hitNormal = (Vector3){0, 1, 0};

            // 1. Collision with target
            if (Vector3Distance(orb->center, orb->target) <= orb->radius * 0.90f || orb->age >= orb->travelTime) {
                hit = true;
                orb->center = orb->target;
                Vector3 toTarget = Vector3Subtract(orb->target, orb->start);
                hitNormal = (Vector3Length(toTarget) > 1e-4f) ? Vector3Normalize(Vector3Negate(toTarget)) : (Vector3){0, 1, 0};
                if (hitNormal.y < 0.25f) hitNormal.y = 0.60f;
                hitNormal = Vector3Normalize(hitNormal);
            }

            // 2. Collision with terrain / ground
            float groundY = MapManager_GetGroundHeightAt(orb->center.x, orb->center.z);
            if (!hit && orb->center.y <= groundY + orb->radius * 0.85f) {
                hit = true;
                orb->center.y = groundY + orb->radius * 0.35f;
                float eps = 0.25f;
                float hL = MapManager_GetGroundHeightAt(orb->center.x - eps, orb->center.z);
                float hR = MapManager_GetGroundHeightAt(orb->center.x + eps, orb->center.z);
                float hD = MapManager_GetGroundHeightAt(orb->center.x, orb->center.z - eps);
                float hU = MapManager_GetGroundHeightAt(orb->center.x, orb->center.z + eps);
                hitNormal = Vector3Normalize((Vector3){(hL - hR) / (2.0f * eps), 1.0f, (hD - hU) / (2.0f * eps)});
            }

            if (hit) {
                orb->phase = WATER_ORB_IMPACT;
                orb->impactPos = orb->center;
                orb->hitNormal = hitNormal;
                orb->impactVelocity = orb->velocity;
                orb->phaseAge = 0.0f;
                orb->crownDuration = fminf(orb->crownDuration, 0.35f);
                orb->settleDuration = 0.40f;
                WaterOrb_SetPhaseField(orb, LIQUID_BODY_IMPACT, dt);
                continue;
            }
            continue;
        }

        // IMPACT & SETTLE PHASES
        orb->phaseAge=fmaxf(orb->age-orb->travelTime,0.0f);
        if (orb->phaseAge >= (orb->crownDuration + orb->settleDuration)) {
            WaterOrb_Clear(orb);
            continue;
        }
        orb->phase = (orb->phaseAge < orb->crownDuration) ? WATER_ORB_IMPACT : WATER_ORB_SETTLE;
        LiquidBodyPhase fieldPhase = LiquidBodyRecipe_IntegrationPhaseAt(orb->phaseAge, dt, orb->crownDuration);
        WaterOrb_SetPhaseField(orb,fieldPhase,dt);
    }
}

static void WaterOrb_SubmitSurface(void)
{
    for (int i = 0; i < WATER_ORB_MAX; ++i) {
        WaterOrb *orb = &s_waterOrbs[i];
        if (!orb->active) continue;

        if (orb->config.renderBackend == LIQUID_RENDER_METABALL3D) {
            float thresh = orb->config.threshold > 0.05f ? orb->config.threshold : 0.42f;
            float rScale = 1.95f; // Scale visible radius to Wyvill field support at threshold ~0.42

            if (orb->phase == WATER_ORB_FLIGHT) {
                // 1. Dominant cohesive fluid sphere core
                float rCore = orb->radius * rScale;
                LiquidMetaball3D_RegisterBlob(orb->center, rCore);

                // 2. Trailing teardrop elongation along -velocity
                Vector3 vDir = (Vector3Length(orb->velocity) > 1e-4f) ? Vector3Normalize(orb->velocity) : (Vector3){1, 0, 0};
                Vector3 tail1 = Vector3Subtract(orb->center, Vector3Scale(vDir, orb->radius * 0.32f));
                LiquidMetaball3D_RegisterBlob(tail1, rCore * 0.76f);

                Vector3 tail2 = Vector3Subtract(orb->center, Vector3Scale(vDir, orb->radius * 0.65f));
                LiquidMetaball3D_RegisterBlob(tail2, rCore * 0.52f);

                // 3. Fluid surface tension wobble (Rayleigh droplet oscillation)
                Vector3 up = (fabsf(vDir.y) < 0.90f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
                Vector3 side = Vector3Normalize(Vector3CrossProduct(up, vDir));
                Vector3 norm = Vector3CrossProduct(vDir, side);

                float wobblePhase = orb->age * 12.0f;
                float wobbleAmp = orb->radius * 0.12f;

                Vector3 wobblePos1 = Vector3Add(orb->center, Vector3Scale(side, cosf(wobblePhase) * wobbleAmp));
                wobblePos1 = Vector3Add(wobblePos1, Vector3Scale(norm, sinf(wobblePhase) * wobbleAmp));
                LiquidMetaball3D_RegisterBlob(wobblePos1, rCore * 0.64f);

                Vector3 wobblePos2 = Vector3Add(orb->center, Vector3Scale(side, -cosf(wobblePhase * 1.35f) * wobbleAmp * 0.75f));
                wobblePos2 = Vector3Add(wobblePos2, Vector3Scale(norm, -sinf(wobblePhase * 1.35f) * wobbleAmp * 0.75f));
                LiquidMetaball3D_RegisterBlob(wobblePos2, rCore * 0.58f);

                // 4. Detached trailing micro-droplet
                float dropCycle = fmodf(orb->age * 3.8f, 1.0f);
                Vector3 dropPos = Vector3Subtract(orb->center, Vector3Scale(vDir, orb->radius * (1.15f + dropCycle * 0.75f)));
                float dropRad = (1.0f - dropCycle) * (rCore * 0.28f);
                if (dropRad > 0.04f) {
                    LiquidMetaball3D_RegisterBlob(dropPos, dropRad);
                }
            } else {
                // IMPACT & SETTLE PHASE:
                // Pancake compression on contact surface + expanding radial splash crown
                float totalImpactTime = fmaxf(orb->crownDuration + orb->settleDuration, 0.1f);
                float impactT01 = Clamp(orb->phaseAge / totalImpactTime, 0.0f, 1.0f);
                float crownT01 = Clamp(orb->phaseAge / fmaxf(orb->crownDuration, 0.05f), 0.0f, 1.0f);
                float fade = 1.0f - impactT01;

                Vector3 n = orb->hitNormal;
                Vector3 up = (fabsf(n.y) < 0.90f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
                Vector3 tX = Vector3Normalize(Vector3CrossProduct(up, n));
                Vector3 tZ = Vector3CrossProduct(n, tX);

                // Center flattening puddle blob
                float centerRad = orb->radius * 1.70f * fade;
                if (centerRad > 0.05f) {
                    LiquidMetaball3D_RegisterBlob(Vector3Add(orb->impactPos, Vector3Scale(n, 0.03f)), centerRad);
                }

                // Radial splash droplets expanding in crown
                int splashCount = 10;
                float splashRadius = orb->radius * (1.0f + 2.5f * crownT01);
                float splashHeight = orb->radius * (0.80f * sinf(crownT01 * 3.14159f) * (1.0f - crownT01 * 0.35f));

                for (int s = 0; s < splashCount; ++s) {
                    float angle = ((float)s / (float)splashCount) * 2.0f * 3.14159f;
                    float rVar = 1.0f + 0.20f * sinf((float)s * 3.8f);
                    Vector3 splashPos = orb->impactPos;
                    splashPos = Vector3Add(splashPos, Vector3Scale(tX, cosf(angle) * splashRadius * rVar));
                    splashPos = Vector3Add(splashPos, Vector3Scale(tZ, sinf(angle) * splashRadius * rVar));
                    splashPos = Vector3Add(splashPos, Vector3Scale(n, splashHeight * rVar + 0.02f));

                    float dropletRad = orb->radius * 0.40f * (1.0f - crownT01 * 0.80f) * fade * rScale;
                    if (dropletRad > 0.04f) {
                        LiquidMetaball3D_RegisterBlob(splashPos, dropletRad);
                    }
                }
            }

            LiquidMetaball3D_SetMaterial(&orb->material);
            LiquidMetaball3D_SetThreshold(thresh);
        } else {
            // SCREEN SPACE FLUID (SSF) BRANCH
            LiquidSurface_BindMaterial(&orb->material);
            float surfaceRadius = (orb->phase == WATER_ORB_FLIGHT) ? orb->radius :
                LiquidBodyRecipe_EstimateRadius(&orb->recipe, orb->velocity, orb->phaseAge);
            LiquidSurface_HintBody(orb->center,surfaceRadius);
            LiquidSurface_SetReconstructionRadius(orb->radius * 0.42f);

            if (orb->phase == WATER_ORB_FLIGHT) {
                // Register 3D spherical fluid core + teardrop
                LiquidSurface_RegisterParticle(orb->center, orb->radius * 0.92f);
                Vector3 vDir = (Vector3Length(orb->velocity) > 1e-4f) ? Vector3Normalize(orb->velocity) : (Vector3){1, 0, 0};
                Vector3 tail1 = Vector3Subtract(orb->center, Vector3Scale(vDir, orb->radius * 0.35f));
                LiquidSurface_RegisterParticle(tail1, orb->radius * 0.72f);
                Vector3 tail2 = Vector3Subtract(orb->center, Vector3Scale(vDir, orb->radius * 0.65f));
                LiquidSurface_RegisterParticle(tail2, orb->radius * 0.50f);

                // Subtle surface oscillation
                Vector3 up = (fabsf(vDir.y) < 0.90f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
                Vector3 side = Vector3Normalize(Vector3CrossProduct(up, vDir));
                Vector3 norm = Vector3CrossProduct(vDir, side);
                float wobblePhase = orb->age * 12.0f;
                float wobbleAmp = orb->radius * 0.12f;
                Vector3 wobblePos1 = Vector3Add(orb->center, Vector3Scale(side, cosf(wobblePhase) * wobbleAmp));
                wobblePos1 = Vector3Add(wobblePos1, Vector3Scale(norm, sinf(wobblePhase) * wobbleAmp));
                LiquidSurface_RegisterParticle(wobblePos1, orb->radius * 0.60f);
            } else {
                // SSF Impact: flattening puddle + splash crown
                float totalImpactTime = fmaxf(orb->crownDuration + orb->settleDuration, 0.1f);
                float impactT01 = Clamp(orb->phaseAge / totalImpactTime, 0.0f, 1.0f);
                float crownT01 = Clamp(orb->phaseAge / fmaxf(orb->crownDuration, 0.05f), 0.0f, 1.0f);
                float fade = 1.0f - impactT01;

                Vector3 n = orb->hitNormal;
                Vector3 up = (fabsf(n.y) < 0.90f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
                Vector3 tX = Vector3Normalize(Vector3CrossProduct(up, n));
                Vector3 tZ = Vector3CrossProduct(n, tX);

                float centerRad = orb->radius * 1.5f * fade;
                if (centerRad > 0.04f) {
                    LiquidSurface_RegisterParticle(Vector3Add(orb->impactPos, Vector3Scale(n, 0.02f)), centerRad);
                }

                int splashCount = 10;
                float splashRadius = orb->radius * (1.0f + 2.4f * crownT01);
                float splashHeight = orb->radius * (0.80f * sinf(crownT01 * 3.14159f) * (1.0f - crownT01 * 0.35f));

                for (int s = 0; s < splashCount; ++s) {
                    float angle = ((float)s / (float)splashCount) * 2.0f * 3.14159f;
                    float rVar = 1.0f + 0.20f * sinf((float)s * 3.8f);
                    Vector3 splashPos = orb->impactPos;
                    splashPos = Vector3Add(splashPos, Vector3Scale(tX, cosf(angle) * splashRadius * rVar));
                    splashPos = Vector3Add(splashPos, Vector3Scale(tZ, sinf(angle) * splashRadius * rVar));
                    splashPos = Vector3Add(splashPos, Vector3Scale(n, splashHeight * rVar + 0.02f));

                    float dropletRad = orb->radius * 0.38f * (1.0f - crownT01 * 0.80f) * fade;
                    if (dropletRad > 0.03f) {
                        LiquidSurface_RegisterParticle(splashPos, dropletRad);
                    }
                }
            }

            // Also submit any particle stream if emitter is active
            ParticleRenderStream stream;
            if (orb->surfaceAdmitted && ParticleManager_GetSurfaceStream(orb->emitter, &stream)) {
                LiquidSurface_SubmitParticleStream(&stream);
            }
        }
    }
}
