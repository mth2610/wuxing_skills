#include "skills/metal/metal_blade_storm_skill/metal_blade_storm_skill.h"

#include "core/composition/visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "entities/entities.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_MBS_INSTANCES   6
#define MAX_MBS_BLADES      5

#define MBS_SUMMON_TIME     0.65f
#define MBS_LAUNCH_INTERVAL 0.10f
#define MBS_MAX_RANGE       13.5f
#define MBS_BLADE_SPEED     25.0f
#define MBS_HIT_RADIUS      0.55f

typedef enum {
    MBS_IDLE = 0,
    MBS_SUMMON,
    MBS_LAUNCH
} MbsState;

typedef struct {
    bool     active;
    int      ownerAgentId;
    int      casterTeam;
    MbsState state;
    float    timer;
    float    launchTimer;

    Vector3  casterPos;
    Vector3  targetPos;
    Vector3  forward;
    float    sizeScale;

    int      bladesHandle;
    int      totalBlades;
    int      bladesLaunched;
    bool     bladeAlive[MAX_MBS_BLADES];
    Vector3  bladePos[MAX_MBS_BLADES];

    float    damagePerBlade;
    float    knockback;
} MbsInstance;

static MbsInstance s_instances[MAX_MBS_INSTANCES];
static int         s_skillIndex = -1;

static void KillInstanceBlades(MbsInstance *inst)
{
    if (inst->bladesHandle > 0) {
        VFX_MetalBlades_Kill(inst->bladesHandle);
        inst->bladesHandle = -1;
    }
}

static void AbortMetalBladeStorm(int agentId)
{
    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].ownerAgentId == agentId) {
            KillInstanceBlades(&s_instances[i]);
            s_instances[i].active = false;
            s_instances[i].state = MBS_IDLE;
        }
    }
}

static bool HasActiveMetalBladeStorm(int agentId)
{
    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].ownerAgentId == agentId) {
            return true;
        }
    }
    return false;
}

static Vector3 ClampTargetRange(Vector3 startPos, Vector3 target)
{
    Vector3 delta = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    float dist = Vector3Length(delta);
    if (dist < 0.001f) {
        return (Vector3){ startPos.x, startPos.y + 0.9f, startPos.z + 6.0f };
    }
    float clamped = (dist > MBS_MAX_RANGE) ? MBS_MAX_RANGE : dist;
    Vector3 dir = Vector3Scale(delta, 1.0f / dist);
    return (Vector3){
        startPos.x + dir.x * clamped,
        VFX_GroundHeightFromMap(startPos.x + dir.x * clamped, startPos.z + dir.z * clamped, NULL) + 0.90f,
        startPos.z + dir.z * clamped
    };
}

void InitMetalBladeStormSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;

    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        s_instances[i].active = false;
        s_instances[i].state = MBS_IDLE;
        s_instances[i].bladesHandle = -1;
    }

    s_skillIndex = Skill_GetIndexByName("METAL_BLADE_STORM");
    if (s_skillIndex >= 0) {
        RegisterSkillManaCost(s_skillIndex, 24.0f);
        RegisterSkillCastAnimSeconds(s_skillIndex, MBS_SUMMON_TIME);
        RegisterSkillAbort(s_skillIndex, AbortMetalBladeStorm);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveMetalBladeStorm);
    }
}

void CastMetalBladeStormSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        if (!s_instances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        KillInstanceBlades(&s_instances[slot]);
    }

    float scale = (params.sizeScale > 0.05f) ? params.sizeScale : 1.0f;
    Vector3 fwd = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    if (Vector3LengthSqr(fwd) < 0.0001f) {
        fwd = (Vector3){ 0.0f, 0.0f, 1.0f };
    } else {
        fwd = Vector3Normalize(fwd);
    }

    MbsInstance *inst = &s_instances[slot];
    *inst = (MbsInstance){0};
    inst->active         = true;
    inst->ownerAgentId   = agentId;
    const Agent *caster  = Entity_GetAgent(agentId);
    inst->casterTeam     = (caster != NULL) ? caster->team : 0;
    inst->state          = MBS_SUMMON;
    inst->timer          = 0.0f;
    inst->launchTimer    = 0.0f;
    inst->casterPos      = startPos;
    inst->targetPos      = ClampTargetRange(startPos, target);
    inst->forward        = fwd;
    inst->sizeScale      = scale;
    inst->totalBlades    = MAX_MBS_BLADES;
    inst->bladesLaunched = 0;

    float totalDmg       = Skill_CalculateDamage(SKILL_CAT_PROJECTILE, params);
    float baseDmg        = (totalDmg > 0.0f) ? totalDmg : 50.0f;
    inst->damagePerBlade = baseDmg / (float)inst->totalBlades;
    inst->knockback      = Skill_CalculateKnockback(SKILL_CAT_PROJECTILE, params) * 0.45f;

    // 1. Draw sword / Kiếm Quyết summon stance
    Skill_RequestAnim(agentId, "withdrawing_sword", MBS_SUMMON_TIME);

    // 2. Summon 5 Bát Diện Hán Kiếm in a crescent fan in front of caster
    VFX_MetalBladesConfig mcfg = VFX_MetalBlades_DefaultConfig();
    mcfg.bladeCount      = inst->totalBlades;
    mcfg.bladeScale      = 1.25f * scale;
    mcfg.haloRadius      = 1.15f * scale;
    mcfg.haloHeight      = 1.15f * scale;
    mcfg.arcDeg          = 140.0f;
    mcfg.emergeDuration  = MBS_SUMMON_TIME * 0.55f;
    mcfg.bladeSpeed      = MBS_BLADE_SPEED;
    mcfg.maxRange        = MBS_MAX_RANGE;
    mcfg.trailWidth      = 0.09f * scale;
    mcfg.autoLaunchDelay = 0.0f;
    inst->bladesHandle   = VFX_ComposeMetalBlades(startPos, fwd, &mcfg);

    for (int b = 0; b < inst->totalBlades; b++) {
        inst->bladeAlive[b] = true;
        inst->bladePos[b]   = startPos;
    }

    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

void UpdateMetalBladeStormSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        MbsInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        inst->timer += dt;

        const Agent *caster = Entity_GetAgent(inst->ownerAgentId);
        if (caster != NULL) {
            inst->casterPos = caster->position;
            VFX_MetalBlades_SetOrigin(inst->bladesHandle, inst->casterPos, inst->forward);
        }

        if (inst->state == MBS_SUMMON) {
            if (inst->timer >= MBS_SUMMON_TIME) {
                inst->state = MBS_LAUNCH;
                inst->timer = 0.0f;
                inst->launchTimer = MBS_LAUNCH_INTERVAL;
                Skill_RequestAnim(inst->ownerAgentId, "thrust_slash", 0.55f);
            }
        } else if (inst->state == MBS_LAUNCH) {
            inst->launchTimer += dt;
            if (inst->bladesLaunched < inst->totalBlades && inst->launchTimer >= MBS_LAUNCH_INTERVAL) {
                inst->launchTimer -= MBS_LAUNCH_INTERVAL;
                VFX_MetalBlades_LaunchNext(inst->bladesHandle, inst->targetPos);
                inst->bladesLaunched++;
                PlayCastSound(EFFECT_PRESET_METAL_SHARD);
            }
        }

        bool anyAlive = false;
        for (int b = 0; b < inst->totalBlades; b++) {
            if (!inst->bladeAlive[b]) continue;
            Vector3 bPos = {0};
            bool bLaunched = false;
            bool bActive = false;
            if (!VFX_MetalBlades_GetBladeState(inst->bladesHandle, b, &bPos, &bLaunched, &bActive) || !bActive) {
                inst->bladeAlive[b] = false;
                continue;
            }
            anyAlive = true;
            inst->bladePos[b] = bPos;

            if (bLaunched) {
                int hitIds[8];
                int hits = Entity_GetNearbyTargets(bPos, 1.25f * inst->sizeScale, hitIds, 8);
                bool anyEnemy = false;
                for (int h = 0; h < hits; h++) {
                    const Agent *ag = Entity_GetAgent(hitIds[h]);
                    if (ag != NULL && ag->team != inst->casterTeam) {
                        anyEnemy = true;
                        break;
                    }
                }
                float hitDist = MBS_HIT_RADIUS * inst->sizeScale + enemyRadius;
                Vector3 enemyChest = { enemyPos.x, bPos.y, enemyPos.z };
                if (anyEnemy || (enemyRadius > 0.0f && Vector3Distance(bPos, enemyChest) <= hitDist)) {
                    Entity_ApplyAoEDamage(bPos, 1.35f * inst->sizeScale,
                                          inst->damagePerBlade, inst->knockback, inst->casterTeam);
                    VFX_MetalBlades_ImpactBlade(inst->bladesHandle, b, bPos);
                    PlayImpactSound(EFFECT_PRESET_METAL_SHARD);
                    inst->bladeAlive[b] = false;
                }
            }
        }

        if (inst->state == MBS_LAUNCH && inst->bladesLaunched >= inst->totalBlades && !anyAlive) {
            KillInstanceBlades(inst);
            inst->active = false;
            inst->state = MBS_IDLE;
        }
    }
}

void DrawMetalBladeStormSkill(void)
{
}

void UnloadMetalBladeStormSkill(void)
{
    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        KillInstanceBlades(&s_instances[i]);
        s_instances[i].active = false;
        s_instances[i].state = MBS_IDLE;
    }
}

bool IsMetalBladeStormSkillCoiling(void)
{
    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].state == MBS_SUMMON) {
            return true;
        }
    }
    return false;
}

int GetMetalBladeStormSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_MBS_INSTANCES && count < maxProjectiles; i++) {
        const MbsInstance *inst = &s_instances[i];
        if (!inst->active) continue;
        for (int b = 0; b < inst->totalBlades && count < maxProjectiles; b++) {
            if (!inst->bladeAlive[b]) continue;
            outProjectiles[count++] = (SkillProjectile){
                .position = inst->bladePos[b],
                .radius   = MBS_HIT_RADIUS * inst->sizeScale,
                .active   = true
            };
        }
    }
    return count;
}

void DeactivateMetalBladeStormProjectile(int index)
{
    int cur = 0;
    for (int i = 0; i < MAX_MBS_INSTANCES; i++) {
        MbsInstance *inst = &s_instances[i];
        if (!inst->active) continue;
        for (int b = 0; b < inst->totalBlades; b++) {
            if (!inst->bladeAlive[b]) continue;
            if (cur == index) {
                VFX_MetalBlades_ImpactBlade(inst->bladesHandle, b, inst->bladePos[b]);
                inst->bladeAlive[b] = false;
                return;
            }
            cur++;
        }
    }
}
