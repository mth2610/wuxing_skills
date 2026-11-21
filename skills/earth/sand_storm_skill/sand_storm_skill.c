#include "skills/earth/sand_storm_skill/sand_storm_skill.h"

#include "core/granular/granular_sim.h"
#include "core/map_manager.h"
#include "core/motion/motion_fields.h"
#include "core/presets/vfx_presets.h"
#include "entities/entities.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_SAND_STORM_INSTANCES 4
#define SAND_SPINUP_DURATION     0.32f
#define SAND_TORNADO_SPEED       6.6f
#define SAND_TORNADO_MAX_RANGE   10.5f
#define SAND_COMPRESS_DURATION   0.26f
#define SAND_COLLAPSE_DURATION   2.20f

typedef enum {
    SAND_PHASE_IDLE = 0,
    SAND_PHASE_SPINUP,
    SAND_PHASE_ADVANCE,
    SAND_PHASE_COMPRESS,
    SAND_PHASE_COLLAPSE
} SandStormPhase;

typedef struct {
    bool active;
    int ownerAgentId;
    SandStormPhase phase;
    float timer;
    float totalAge;
    float progress;
    float maxRange;
    float speed;
    float hitRadius;
    float damage;
    float sizeScale;
    float implosion;
    bool hitApplied;

    Vector3 startPos;
    Vector3 targetPos;
    Vector3 tornadoPos;
    Vector3 tornadoVel;
    Vector3 forward;
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

static void SandStorm_ReleaseTornado(SandStormInstance *inst, bool haboobBurst)
{
    (void)inst;
    if (haboobBurst) {
        GranularSim_ReleaseHaboobBurst(inst->tornadoPos, 4.6f * inst->sizeScale,
                                       13.5f, 4.2f, 5.6f);
    } else {
        GranularSim_SetTornadoFunnelEx((Vector3){0}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false);
    }
}

static void AbortSandStorm(int agentId)
{
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        if (s_sandInstances[i].active && s_sandInstances[i].ownerAgentId == agentId) {
            SandStorm_ReleaseTornado(&s_sandInstances[i], false);
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
    }
    /* Pre-warm GranularSim at startup so first cast has zero compile/alloc hitch. */
    GranularSim_Init();

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
        SandStorm_ReleaseTornado(&s_sandInstances[slot], false);
    }

    SandStormInstance *inst = &s_sandInstances[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->phase = SAND_PHASE_SPINUP;
    inst->timer = 0.0f;
    inst->totalAge = 0.0f;
    inst->progress = 0.0f;
    inst->implosion = 0.0f;
    inst->hitApplied = false;
    inst->tornadoVel = (Vector3){0};

    inst->sizeScale = params.sizeScale > 0.05f ? params.sizeScale : 1.0f;
    Skill_RequestAnim(agentId, "hurricane_kick", SAND_SPINUP_DURATION);
    inst->speed = SAND_TORNADO_SPEED;
    inst->hitRadius = 1.55f * inst->sizeScale;
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

    const float baseR      = 0.25f * inst->sizeScale;
    const float topR       = 2.15f * inst->sizeScale;
    const float height     = 4.60f * inst->sizeScale;
    const float transitDur = inst->maxRange / inst->speed;
    const float activeDur  = SAND_SPINUP_DURATION + transitDur + SAND_COMPRESS_DURATION;
    const float totalLife  = activeDur + SAND_COLLAPSE_DURATION + 0.5f;

    // 1. Granular tornado column (900 funnel grains + 90 ground path-bed grains)
    //    Sand-dominant (70% desert/gold sand, 18% warm loess soil, 12% sparse rock/shale)
    enum { FUNNEL_GRAIN_COUNT = 900, PATH_BED_COUNT = 90, TOTAL_GRAIN_COUNT = FUNNEL_GRAIN_COUNT + PATH_BED_COUNT };
    static GranularGrainState s_grains[TOTAL_GRAIN_COUNT];

    for (int i = 0; i < FUNNEL_GRAIN_COUNT; i++) {
        unsigned int s = (unsigned int)i * 747796405u + 2891336453u;
        float u0 = SandStorm_Hash01(s + 1u);
        float u1 = SandStorm_Hash01(s + 2u);
        float u2 = SandStorm_Hash01(s + 3u);
        float u3 = SandStorm_Hash01(s + 4u);

        uint32_t matId = GRANULAR_MAT_DESERT_SAND;
        float gRadius  = (0.015f + 0.008f * u2) * inst->sizeScale;
        float hn       = 0.04f + 0.94f * u1;

        if (u3 > 0.94f) {
            matId   = GRANULAR_MAT_COARSE_GRAVEL;
            gRadius = (0.026f + 0.014f * u2) * inst->sizeScale;
            hn      = 0.04f + 0.45f * u1;
        } else if (u3 > 0.88f) {
            matId   = GRANULAR_MAT_SHALE_SHARD;
            gRadius = (0.024f + 0.012f * u2) * inst->sizeScale;
            hn      = 0.06f + 0.58f * u1;
        } else if (u3 > 0.70f) {
            matId   = GRANULAR_MAT_LOESS_SOIL;
            gRadius = (0.020f + 0.010f * u2) * inst->sizeScale;
            hn      = 0.04f + 0.86f * u1;
        } else if (u3 > 0.58f) {
            matId   = GRANULAR_MAT_GOLD_IRON_SAND;
            gRadius = (0.016f + 0.008f * u2) * inst->sizeScale;
        }

        float y = 0.06f + hn * height;
        float coneR = Motion_TornadoFunnelRadius(hn, baseR, topR);
        float r = coneR * (0.70f + 0.32f * u2);
        float angle = u0 * 2.0f * PI;

        s_grains[i] = (GranularGrainState){
            .position = {
                inst->startPos.x + cosf(angle) * r,
                inst->startPos.y + y,
                inst->startPos.z + sinf(angle) * r
            },
            .velocity = {
                -sinf(angle) * 9.5f,
                3.6f + u3 * 2.0f,
                cosf(angle) * 9.5f
            },
            .radius     = gRadius,
            .lifetime   = totalLife * (0.90f + 0.20f * u2),
            .seed       = u0,
            .contactAO  = 0.16f,
            .materialId = matId,
            .flags      = 0u
        };
    }

    Vector3 rightVec = {-inst->forward.z, 0.0f, inst->forward.x};
    for (int i = 0; i < PATH_BED_COUNT; i++) {
        unsigned int s = (unsigned int)i * 1597334677u + 17u;
        float u0 = SandStorm_Hash01(s + 1u);
        float u1 = SandStorm_Hash01(s + 2u);
        float u2 = SandStorm_Hash01(s + 3u);
        float along = u0 * inst->maxRange;
        float lat = (u1 - 0.5f) * 2.8f * inst->sizeScale;
        Vector3 gp = {
            inst->startPos.x + inst->forward.x * along + rightVec.x * lat,
            0.0f,
            inst->startPos.z + inst->forward.z * along + rightVec.z * lat
        };
        gp.y = MapManager_GetGroundHeightAt(gp.x, gp.z) + 0.03f;
        uint32_t matId = GRANULAR_MAT_DESERT_SAND;
        float bedRadius = (0.017f + 0.008f * u2) * inst->sizeScale;
        if (u2 >= 0.92f) {
            matId = GRANULAR_MAT_COARSE_GRAVEL;
            bedRadius = (0.024f + 0.012f * u2) * inst->sizeScale;
        } else if (u2 >= 0.84f) {
            matId = GRANULAR_MAT_SHALE_SHARD;
            bedRadius = (0.022f + 0.010f * u2) * inst->sizeScale;
        } else if (u2 >= 0.70f) {
            matId = GRANULAR_MAT_LOESS_SOIL;
            bedRadius = (0.020f + 0.009f * u2) * inst->sizeScale;
        }
        s_grains[FUNNEL_GRAIN_COUNT + i] = (GranularGrainState){
            .position   = gp,
            .velocity   = {0.0f, 0.0f, 0.0f},
            .radius     = bedRadius,
            .lifetime   = totalLife,
            .seed       = u0,
            .contactAO  = 0.28f,
            .materialId = matId,
            .flags      = 1u
        };
    }
    GranularSim_SpawnCluster(s_grains, TOTAL_GRAIN_COUNT, inst->startPos, (Vector3){0.0f, 1.0f, 0.0f});

    // 2. Analytical tornado funnel
    GranularSim_SetTornadoFunnelEx(inst->startPos, baseR, topR, height,
                                   10.2f, 5.0f, 0.28f * inst->sizeScale, 0.0f, true);
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

        const float baseR  = 0.25f * inst->sizeScale;
        const float topR   = 2.15f * inst->sizeScale;
        const float height = 4.60f * inst->sizeScale;

        if (inst->phase == SAND_PHASE_SPINUP) {
            inst->tornadoVel = (Vector3){0};
            if (inst->timer >= SAND_SPINUP_DURATION) {
                inst->phase = SAND_PHASE_ADVANCE;
                inst->timer = 0.0f;
            }
        } else if (inst->phase == SAND_PHASE_ADVANCE) {
            float transitTime = inst->maxRange / fmaxf(inst->speed, 1.0f);
            inst->progress = Clamp(inst->timer / fmaxf(transitTime, 0.1f), 0.0f, 1.0f);
            Vector3 prevPos = inst->tornadoPos;
            inst->tornadoPos = Vector3Lerp(inst->startPos, inst->targetPos, inst->progress);
            inst->tornadoPos.y = MapManager_GetGroundHeightAt(inst->tornadoPos.x, inst->tornadoPos.z);
            if (dt > 1e-4f) {
                Vector3 instVel = Vector3Scale(Vector3Subtract(inst->tornadoPos, prevPos), 1.0f / dt);
                inst->tornadoVel = Vector3Lerp(inst->tornadoVel, instVel, fminf(dt * 8.0f, 1.0f));
            }
            GranularSim_SetTornadoFunnelEx(inst->tornadoPos, baseR, topR, height,
                                           10.2f, 5.2f, 0.28f * inst->sizeScale, 0.0f, true);

            if (!inst->hitApplied) {
                int hits[8];
                int hitCount = Entity_GetNearbyTargets(inst->tornadoPos, inst->hitRadius, hits, 8);
                bool anyHit = false;
                Vector3 kb = {inst->forward.x * 3.8f, 2.4f, inst->forward.z * 3.8f};
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
                inst->phase = SAND_PHASE_COMPRESS;
                inst->timer = 0.0f;
            }
        } else if (inst->phase == SAND_PHASE_COMPRESS) {
            float p = Clamp(inst->timer / SAND_COMPRESS_DURATION, 0.0f, 1.0f);
            inst->implosion = p * p * (3.0f - 2.0f * p);
            inst->tornadoVel = Vector3Scale(inst->tornadoVel, expf(-8.0f * dt));
            GranularSim_SetTornadoFunnelEx(inst->tornadoPos, baseR, topR, height,
                                           10.5f + 4.5f * inst->implosion, 4.6f,
                                           0.22f * inst->sizeScale, inst->implosion, true);
            if (inst->timer >= SAND_COMPRESS_DURATION) {
                inst->phase = SAND_PHASE_COLLAPSE;
                inst->timer = 0.0f;
                inst->implosion = 0.0f;
                SandStorm_ReleaseTornado(inst, true);
                PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
            }
        } else if (inst->phase == SAND_PHASE_COLLAPSE) {
            if (inst->timer >= SAND_COLLAPSE_DURATION) {
                SandStorm_ReleaseTornado(inst, false);
                inst->active = false;
                inst->phase = SAND_PHASE_IDLE;
            }
        }
    }
}

void DrawSandStormSkill(void)
{
    /* GranularSim and ParticleSystem render through their own Core passes. */
}

void UnloadSandStormSkill(void)
{
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES; i++) {
        SandStorm_ReleaseTornado(&s_sandInstances[i], false);
        s_sandInstances[i].active = false;
        s_sandInstances[i].phase = SAND_PHASE_IDLE;
    }
}

int GetSandStormSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_SAND_STORM_INSTANCES && count < maxProjectiles; i++) {
        const SandStormInstance *inst = &s_sandInstances[i];
        if (!inst->active || (inst->phase != SAND_PHASE_ADVANCE && inst->phase != SAND_PHASE_COMPRESS)) continue;
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
        if (!inst->active || (inst->phase != SAND_PHASE_ADVANCE && inst->phase != SAND_PHASE_COMPRESS)) continue;
        if (cur == index) {
            inst->hitApplied = true;
            return;
        }
        cur++;
    }
}
