#include "skills/earth/sand_storm_skill/sand_storm_skill.h"

#include "core/composition/visual_composer.h"
#include "core/granular/granular_sim.h"
#include "core/motion/motion_fields.h"
#include "core/presets/vfx_presets.h"
#include "core/trails/trail_ribbon.h"
#include "core/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_SAND_STORM_INSTANCES 4
#define SAND_STORM_RIBBON_COUNT  3
#define SAND_SPINUP_DURATION     0.28f
#define SAND_TORNADO_SPEED       6.8f
#define SAND_TORNADO_MAX_RANGE   10.5f
#define SAND_DUNE_HOLD_DURATION  2.40f

typedef enum {
    SAND_PHASE_IDLE = 0,
    SAND_PHASE_SPINUP,
    SAND_PHASE_ADVANCE,
    SAND_PHASE_COLLAPSE
} SandStormPhase;

typedef struct {
    bool active;
    int ownerAgentId;
    int casterTeam;
    SandStormPhase phase;
    float timer;
    float totalAge;
    float dustTimer;
    float progress;
    float maxRange;
    float speed;
    float hitRadius;
    float damage;
    float sizeScale;
    bool hitApplied;

    Vector3 startPos;
    Vector3 targetPos;
    Vector3 tornadoPos;
    Vector3 forward;

    MotionFieldHandle tornadoField;
    TrailAttachmentHandle ribbonAttach[SAND_STORM_RIBBON_COUNT];
    int ribbonTrailId[SAND_STORM_RIBBON_COUNT];
} SandStormInstance;

static SandStormInstance s_sandInstances[MAX_SAND_STORM_INSTANCES];
static int s_sandSkillIndex = -1;

static float SandStorm_Hash01(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffu) / 16777216.0f;
}

static Vector3 SandStorm_RibbonOrbitPos(const SandStormInstance *inst, int idx, float age)
{
    static const float hFrac[SAND_STORM_RIBBON_COUNT] = { 0.28f, 0.56f, 0.78f };
    static const float speedMul[SAND_STORM_RIBBON_COUNT] = { 7.8f, -6.2f, 9.4f };
    static const float phaseOff[SAND_STORM_RIBBON_COUNT] = { 0.0f, 2.094f, 4.188f };

    float baseR = 0.24f * inst->sizeScale;
    float topR = 1.38f * inst->sizeScale;
    float height = 3.65f * inst->sizeScale;
    float hn = hFrac[idx];
    float r = (baseR + (topR - baseR) * powf(hn, 1.30f)) * 0.92f;
    float ang = age * speedMul[idx] + phaseOff[idx];
    return (Vector3){
        inst->tornadoPos.x + cosf(ang) * r,
        inst->tornadoPos.y + hn * height,
        inst->tornadoPos.z + sinf(ang) * r
    };
}

static void SandStorm_ReleaseTornado(SandStormInstance *inst)
{
    if (inst->tornadoField != MOTION_FIELD_INVALID) {
        MotionFields_Stop(inst->tornadoField);
        inst->tornadoField = MOTION_FIELD_INVALID;
    }
    for (int r = 0; r < SAND_STORM_RIBBON_COUNT; r++) {
        if (inst->ribbonTrailId[r] >= 0) {
            TrailRibbon_ReleaseHead(inst->ribbonTrailId[r]);
            inst->ribbonTrailId[r] = -1;
        }
        if (inst->ribbonAttach[r] != 0) {
            TrailAttachment_Destroy(inst->ribbonAttach[r]);
            inst->ribbonAttach[r] = 0;
        }
    }
    GranularSim_SetTornadoFunnel((Vector3){0}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false);
}

static void AbortSandStorm(int agentId)
{
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        if (s_sandInstances[i].active && s_sandInstances[i].ownerAgentId == agentId) {
            SandStorm_ReleaseTornado(&s_sandInstances[i]);
            s_sandInstances[i].active = false;
            s_sandInstances[i].phase = SAND_PHASE_IDLE;
        }
    }
}

static bool HasActiveSandStorm(int agentId)
{
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        if (s_sandInstances[i].active && s_sandInstances[i].ownerAgentId == agentId &&
            s_sandInstances[i].phase != SAND_PHASE_COLLAPSE) {
            return true;
        }
    }
    return false;
}

void InitSandStormSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        s_sandInstances[i].active = false;
        s_sandInstances[i].phase = SAND_PHASE_IDLE;
        s_sandInstances[i].tornadoField = MOTION_FIELD_INVALID;
        for (int r = 0; r < SAND_STORM_RIBBON_COUNT; r++) {
            s_sandInstances[i].ribbonAttach[r] = 0;
            s_sandInstances[i].ribbonTrailId[r] = -1;
        }
    }
    s_sandSkillIndex = Skill_GetIndexByName("SAND_STORM");
    if (s_sandSkillIndex >= 0) {
        RegisterSkillManaCost(s_sandSkillIndex, 24.0f);
        RegisterSkillCastAnimSeconds(s_sandSkillIndex, SAND_SPINUP_DURATION);
        RegisterSkillAbort(s_sandSkillIndex, AbortSandStorm);
        RegisterSkillLifecycleQuery(s_sandSkillIndex, HasActiveSandStorm);
    }
}

void CastSandStormSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        if (!s_sandInstances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        SandStorm_ReleaseTornado(&s_sandInstances[slot]);
    }

    SandStormInstance *inst = &s_sandInstances[slot];
    const Agent *caster = Entity_GetAgent(agentId);
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->casterTeam = caster ? (int)caster->team : (int)TEAM_ALLY;
    inst->phase = SAND_PHASE_SPINUP;
    inst->timer = 0.0f;
    inst->totalAge = 0.0f;
    inst->dustTimer = 0.0f;
    inst->progress = 0.0f;
    inst->hitApplied = false;
    inst->tornadoField = MOTION_FIELD_INVALID;
    for (int r = 0; r < SAND_STORM_RIBBON_COUNT; r++) {
        inst->ribbonAttach[r] = 0;
        inst->ribbonTrailId[r] = -1;
    }

    inst->sizeScale = params.sizeScale > 0.05f ? params.sizeScale : 1.0f;
    Skill_RequestAnim(agentId, "hurricane_kick", SAND_SPINUP_DURATION);
    inst->speed = SAND_TORNADO_SPEED;
    inst->hitRadius = 1.45f * inst->sizeScale;
    inst->damage = params.damage > 0.0f ? params.damage : 26.0f;

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float dist = Vector3Length(dir);
    inst->forward = dist > 0.05f ? Vector3Scale(dir, 1.0f / dist) : (Vector3){1.0f, 0.0f, 0.0f};
    inst->maxRange = fminf(fmaxf(dist, 3.5f), SAND_TORNADO_MAX_RANGE * inst->sizeScale);

    inst->startPos = startPos;
    inst->startPos.y = MapManager_GetGroundHeightAt(startPos.x, startPos.z);
    inst->targetPos = Vector3Add(inst->startPos, Vector3Scale(inst->forward, inst->maxRange));
    inst->targetPos.y = MapManager_GetGroundHeightAt(inst->targetPos.x, inst->targetPos.z);
    inst->tornadoPos = inst->startPos;

    // 1. Seed 2,800 stratified multi-material 3D grains (Sand + Soil Clods + Gravel + Twigs + Leaves)
    GranularSim_Init();
    static GranularGrainState s_grains[2800];
    const int count = 2800;
    const float baseR = 0.24f * inst->sizeScale;
    const float topR = 1.38f * inst->sizeScale;
    const float height = 3.65f * inst->sizeScale;
    const float transitDur = inst->maxRange / inst->speed;
    const float totalLife = SAND_SPINUP_DURATION + transitDur + SAND_DUNE_HOLD_DURATION + 0.8f;

    for (int i = 0; i < count; i++) {
        unsigned int s = (unsigned int)i * 747796405u + 2891336453u;
        float u0 = SandStorm_Hash01(s + 1u);
        float u1 = SandStorm_Hash01(s + 2u);
        float u2 = SandStorm_Hash01(s + 3u);
        float u3 = SandStorm_Hash01(s + 4u);

        uint32_t matId = GRANULAR_MAT_DESERT_SAND;
        float gRadius = 0.018f + 0.006f * u3;
        float hn = u1 * u1;

        // Stratified debris mix:
        // 62% Desert Sand, 16% Loess Soil Clods, 12% Coarse Gravel, 6% Dry Twigs, 4% Dry Leaves
        if (u3 > 0.96f) {
            matId = GRANULAR_MAT_DRY_LEAF;
            gRadius = (0.032f + 0.012f * u2) * inst->sizeScale;
            hn = 0.25f + 0.72f * u1;
        } else if (u3 > 0.90f) {
            matId = GRANULAR_MAT_DRY_TWIG;
            gRadius = (0.030f + 0.012f * u2) * inst->sizeScale;
            hn = 0.15f + 0.70f * u1;
        } else if (u3 > 0.78f) {
            matId = GRANULAR_MAT_COARSE_GRAVEL;
            gRadius = (0.032f + 0.018f * u2) * inst->sizeScale;
            hn = u1 * u1 * 0.42f; // Heavy stones stay near the base skirt
        } else if (u3 > 0.62f) {
            matId = GRANULAR_MAT_LOESS_SOIL;
            gRadius = (0.028f + 0.016f * u2) * inst->sizeScale;
            hn = u1 * 0.75f;
        }

        float y = 0.05f + hn * height;
        float coneR = baseR + (topR - baseR) * powf(hn, 1.35f);
        float r = coneR * (0.18f + 0.87f * u2);
        float angle = u0 * 2.0f * PI;

        s_grains[i] = (GranularGrainState){
            .position = {
                inst->startPos.x + cosf(angle) * r,
                inst->startPos.y + y,
                inst->startPos.z + sinf(angle) * r
            },
            .velocity = {
                -sinf(angle) * 7.2f,
                2.5f + u3 * 1.5f,
                cosf(angle) * 7.2f
            },
            .radius = gRadius,
            .lifetime = totalLife * (0.90f + 0.20f * u2),
            .seed = u0,
            .contactAO = 0.15f,
            .materialId = matId,
            .flags = 0u
        };
    }
    GranularSim_SpawnCluster(s_grains, count, inst->startPos, (Vector3){0.0f, 1.0f, 0.0f});

    // 1b. Pre-scatter 280 resting ground debris grains along the path to be entrained by the storm
    static GranularGrainState s_pathBed[280];
    Vector3 rightVec = {-inst->forward.z, 0.0f, inst->forward.x};
    for (int i = 0; i < 280; i++) {
        unsigned int s = (unsigned int)i * 1597334677u + 17u;
        float u0 = SandStorm_Hash01(s + 1u);
        float u1 = SandStorm_Hash01(s + 2u);
        float u2 = SandStorm_Hash01(s + 3u);
        float along = u0 * inst->maxRange;
        float lat = (u1 - 0.5f) * 2.4f * inst->sizeScale;
        Vector3 gp = {
            inst->startPos.x + inst->forward.x * along + rightVec.x * lat,
            0.0f,
            inst->startPos.z + inst->forward.z * along + rightVec.z * lat
        };
        gp.y = MapManager_GetGroundHeightAt(gp.x, gp.z) + 0.03f;
        uint32_t matId = (u2 < 0.35f) ? GRANULAR_MAT_LOESS_SOIL
                       : (u2 < 0.65f ? GRANULAR_MAT_COARSE_GRAVEL
                                     : (u2 < 0.85f ? GRANULAR_MAT_DRY_LEAF : GRANULAR_MAT_DRY_TWIG));
        s_pathBed[i] = (GranularGrainState){
            .position = gp,
            .velocity = {0.0f, 0.0f, 0.0f},
            .radius = 0.030f + 0.014f * u2,
            .lifetime = totalLife,
            .seed = u0,
            .contactAO = 0.25f,
            .materialId = matId,
            .flags = 1u // Resting bed ready for storm entrainment
        };
    }
    GranularSim_AppendCluster(s_pathBed, 280, inst->startPos, (Vector3){0.0f, 1.0f, 0.0f});

    // 1c. GPU-simulated Helical Wind & Dust Ribbons (2x TRAIL_PRESET_SMOKE + 1x TRAIL_PRESET_WIND_BLADE)
    for (int r = 0; r < SAND_STORM_RIBBON_COUNT; r++) {
        Vector3 rPos = SandStorm_RibbonOrbitPos(inst, r, 0.0f);
        inst->ribbonAttach[r] = TrailAttachment_Create(MatrixTranslate(rPos.x, rPos.y, rPos.z));
        if (inst->ribbonAttach[r] != 0) {
            TrailRibbonConfig rcfg = TrailRibbon_Default();
            rcfg.mode = TRAIL_RIBBON_HEAD_ANCHORED;
            rcfg.attachment = inst->ribbonAttach[r];
            rcfg.nodeCount = (r == 2) ? 20 : 24;
            rcfg.lengthM = (r == 2) ? 2.6f * inst->sizeScale : 3.2f * inst->sizeScale;
            rcfg.widthM = (r == 2) ? 0.24f * inst->sizeScale : 0.48f * inst->sizeScale;
            rcfg.lifetimeSec = SAND_SPINUP_DURATION + transitDur + 0.35f;
            rcfg.headPosition = rPos;
            rcfg.tailDirection = (Vector3){0.0f, -0.4f, 0.0f};
            VFX_TrailRibbonApplyPreset(&rcfg, (r == 2) ? TRAIL_PRESET_WIND_BLADE : TRAIL_PRESET_SMOKE, VC_MAT_EARTH);
            inst->ribbonTrailId[r] = TrailRibbon_Spawn(&rcfg);
        }
    }

    // 2. Moving FIELD_CAPSULE MotionField (swirl + curl turbulence + updraft)
    FieldDesc fd = MotionField_Default();
    fd.receiverMask = MOTION_RECEIVER_PARTICLE;
    fd.volume.shape = FIELD_CAPSULE;
    fd.volume.capsuleStart = (Vector3){0.0f, 0.0f, 0.0f};
    fd.volume.capsuleEnd = (Vector3){0.0f, height, 0.0f};
    fd.volume.radiusM = topR * 1.35f;
    fd.transform.position = inst->startPos;
    fd.lifetime.durationSec = SAND_SPINUP_DURATION + transitDur;
    fd.lifetime.attackSec = 0.08f;
    fd.lifetime.fadeSec = 0.25f;
    fd.flow.enabled = true;
    fd.flow.axis = (Vector3){0.0f, 1.0f, 0.0f};
    fd.flow.velocityMps = (Vector3){0.0f, 0.8f, 0.0f};
    fd.flow.procedural.swirlSpeedMps = 0.0f;
    fd.flow.procedural.turbulenceSpeedMps = 1.2f;
    fd.flow.procedural.eddyLengthM = 0.75f;
    fd.forceLaws[0] = (ForceLaw){
        .type = FORCE_LAW_CURL_FORCE,
        .procedural = {.turbulenceSpeedMps = 1.2f, .swirlSpeedMps = 0.0f, .eddyLengthM = 0.75f}
    };
    fd.forceLawCount = 1;
    inst->tornadoField = MotionFields_CreateField(&fd);

    // 3. Analytical Burgers-Rott conical tornado funnel
    GranularSim_SetTornadoFunnel(inst->startPos, baseR, topR, height, 7.5f, 3.8f, true);

    // 4. Ground dust skirt
    VFX_ComposeImpactDustVariant(inst->startPos, VC_MAT_EARTH, 1.6f * inst->sizeScale,
                                 0.90f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}

void UpdateSandStormSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        SandStormInstance *inst = &s_sandInstances[i];
        if (!inst->active) continue;

        inst->timer += dt;
        inst->totalAge += dt;

        // Update orbiting GPU trail attachments during spinup & advance
        if (inst->phase == SAND_PHASE_SPINUP || inst->phase == SAND_PHASE_ADVANCE) {
            for (int r = 0; r < SAND_STORM_RIBBON_COUNT; r++) {
                if (inst->ribbonAttach[r] != 0) {
                    Vector3 rPos = SandStorm_RibbonOrbitPos(inst, r, inst->totalAge);
                    TrailAttachment_Update(inst->ribbonAttach[r], MatrixTranslate(rPos.x, rPos.y, rPos.z), dt, false);
                }
            }
        }

        if (inst->phase == SAND_PHASE_SPINUP) {
            if (inst->timer >= SAND_SPINUP_DURATION) {
                inst->phase = SAND_PHASE_ADVANCE;
                inst->timer = 0.0f;
            }
        } else if (inst->phase == SAND_PHASE_ADVANCE) {
            float transitTime = inst->maxRange / fmaxf(inst->speed, 1.0f);
            inst->progress = Clamp(inst->timer / fmaxf(transitTime, 0.1f), 0.0f, 1.0f);
            inst->tornadoPos = Vector3Lerp(inst->startPos, inst->targetPos, inst->progress);
            inst->tornadoPos.y = MapManager_GetGroundHeightAt(inst->tornadoPos.x, inst->tornadoPos.z);

            float baseR = 0.22f * inst->sizeScale;
            float topR = 1.35f * inst->sizeScale;
            float height = 3.60f * inst->sizeScale;

            if (inst->tornadoField != MOTION_FIELD_INVALID) {
                FieldTransform xf = {
                    .position = inst->tornadoPos,
                    .axisX = {1.0f, 0.0f, 0.0f},
                    .axisY = {0.0f, 1.0f, 0.0f},
                    .axisZ = {0.0f, 0.0f, 1.0f},
                    .frameVelocityMps = Vector3Scale(inst->forward, inst->speed)
                };
                MotionFields_SetTransform(inst->tornadoField, &xf);
            }
            GranularSim_SetTornadoFunnel(inst->tornadoPos, baseR, topR, height,
                                         7.5f, 3.8f, true);

            // Ground-hugging scouring dust puff at base skirt (1 clean call per 0.14s)
            inst->dustTimer += dt;
            if (inst->dustTimer >= 0.14f) {
                inst->dustTimer = 0.0f;
                VFX_ComposeImpactDustVariant(
                    Vector3Add(inst->tornadoPos, (Vector3){0.0f, 0.15f, 0.0f}),
                    VC_MAT_EARTH, 1.45f * inst->sizeScale, 0.78f,
                    VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
            }

            if (!inst->hitApplied) {
                int hits[8];
                int hitCount = Entity_GetNearbyTargets(inst->tornadoPos, inst->hitRadius, hits, 8);
                bool anyHit = false;
                Vector3 kb = {inst->forward.x * 3.5f, 2.2f, inst->forward.z * 3.5f};
                for (int h = 0; h < hitCount; h++) {
                    if (!Entity_CanDamageTarget(inst->ownerAgentId, hits[h])) continue;
                    Entity_ApplyDamageFrom(hits[h], inst->ownerAgentId, inst->damage, 0.0f, kb);
                    anyHit = true;
                }
                if (anyHit) {
                    inst->hitApplied = true;
                }
            }

            if (inst->progress >= 1.0f) {
                inst->phase = SAND_PHASE_COLLAPSE;
                inst->timer = 0.0f;
                SandStorm_ReleaseTornado(inst);
                VFX_ComposeImpactDustVariant(inst->tornadoPos, VC_MAT_EARTH,
                                             2.0f * inst->sizeScale, 1.0f,
                                             VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
                PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
            }
        } else if (inst->phase == SAND_PHASE_COLLAPSE) {
            if (inst->timer >= SAND_DUNE_HOLD_DURATION) {
                SandStorm_ReleaseTornado(inst);
                inst->active = false;
                inst->phase = SAND_PHASE_IDLE;
            }
        }
    }
}

void DrawSandStormSkill(void)
{
    /* GranularSim and ImpactDust render through their own Core passes. */
}

void UnloadSandStormSkill(void)
{
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        SandStorm_ReleaseTornado(&s_sandInstances[i]);
        s_sandInstances[i].active = false;
        s_sandInstances[i].phase = SAND_PHASE_IDLE;
    }
}

int GetSandStormSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES && count < maxProjectiles; i++) {
        const SandStormInstance *inst = &s_sandInstances[i];
        if (!inst->active || inst->phase != SAND_PHASE_ADVANCE) continue;
        outProjectiles[count++] = (SkillProjectile){
            .position = inst->tornadoPos,
            .radius = inst->hitRadius,
            .active = true
        };
    }
    return count;
}

void DeactivateSandStormProjectile(int index)
{
    int cur = 0;
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        SandStormInstance *inst = &s_sandInstances[i];
        if (!inst->active || inst->phase != SAND_PHASE_ADVANCE) continue;
        if (cur == index) {
            inst->hitApplied = true;
            return;
        }
        cur++;
    }
}
