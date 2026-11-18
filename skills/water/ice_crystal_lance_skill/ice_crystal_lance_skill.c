#include "skills/water/ice_crystal_lance_skill/ice_crystal_lance_skill.h"
#include "core/composition/visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/skill_boilerplate.h"
#include "entities/entities.h"
#include "raymath.h"
#include "skills/skill_helper.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_ICE_LANCE_INSTANCES 6
#define ICE_LANCE_CAST_SECS     0.44f
#define ICE_LANCE_WAVE_SPEED    16.0f
#define ICE_LANCE_MAX_RANGE     12.5f
#define ICE_LANCE_HIT_RADIUS    1.15f
#define ICE_LANCE_HOLD_SECS     1.60f
#define ICE_LANCE_MANA_COST     22.0f

typedef struct {
    bool active;
    int ownerAgentId;
    int casterTeam;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 dir;
    Vector3 headPos;
    float distTotal;
    float distTraveled;
    float sizeScale;
    float elapsed;
    float totalLifetime;
    int crystalHandle;
    bool hitDealt;
    float damage;
    float knockback;
} IceCrystalLanceInstance;

static IceCrystalLanceInstance s_lances[MAX_ICE_LANCE_INSTANCES];
static int s_skillIndex = -1;

static void AbortIceCrystalLance(int agentId) {
    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES; i++) {
        if (s_lances[i].active && s_lances[i].ownerAgentId == agentId) {
            if (s_lances[i].crystalHandle >= 0) {
                VFX_KillIceCrystal(s_lances[i].crystalHandle);
                s_lances[i].crystalHandle = -1;
            }
            s_lances[i].active = false;
        }
    }
}

static bool HasActiveIceCrystalLance(int agentId) {
    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES; i++) {
        if (s_lances[i].active && s_lances[i].ownerAgentId == agentId) {
            return true;
        }
    }
    return false;
}

void InitIceCrystalLanceSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;

    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES; i++) {
        s_lances[i].active = false;
        s_lances[i].crystalHandle = -1;
    }

    s_skillIndex = Skill_GetIndexByName("ICE_CRYSTAL_LANCE");
    if (s_skillIndex >= 0) {
        RegisterSkillManaCost(s_skillIndex, ICE_LANCE_MANA_COST);
        RegisterSkillCastAnimSeconds(s_skillIndex, ICE_LANCE_CAST_SECS);
        RegisterSkillAbort(s_skillIndex, AbortIceCrystalLance);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveIceCrystalLance);
    }
}

void CastIceCrystalLanceSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES; i++) {
        if (!s_lances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        if (s_lances[0].crystalHandle >= 0) {
            VFX_KillIceCrystal(s_lances[0].crystalHandle);
        }
    }

    float scale = Clamp(params.sizeScale > 0.05f ? params.sizeScale : 1.0f, 0.4f, 2.5f);
    Vector3 fwd = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    float dist = Vector3Length(fwd);
    if (dist < 0.01f) {
        fwd = (Vector3){ 0.0f, 0.0f, 1.0f };
    } else {
        fwd = Vector3Scale(fwd, 1.0f / dist);
    }
    if (dist < 4.5f) dist = 4.5f;
    if (dist > ICE_LANCE_MAX_RANGE) dist = ICE_LANCE_MAX_RANGE;

    Vector3 groundStart = {
        startPos.x + fwd.x * 0.8f,
        VFX_GroundHeightFromMap(startPos.x + fwd.x * 0.8f, startPos.z + fwd.z * 0.8f, NULL),
        startPos.z + fwd.z * 0.8f
    };
    Vector3 groundTarget = {
        groundStart.x + fwd.x * dist,
        VFX_GroundHeightFromMap(groundStart.x + fwd.x * dist, groundStart.z + fwd.z * dist, NULL),
        groundStart.z + fwd.z * dist
    };

    const Agent *caster = Entity_GetAgent(agentId);
    IceCrystalLanceInstance *inst = &s_lances[slot];
    *inst = (IceCrystalLanceInstance){0};
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->casterTeam = (caster != NULL) ? caster->team : 0;
    inst->startPos = groundStart;
    inst->targetPos = groundTarget;
    inst->dir = fwd;
    inst->headPos = groundStart;
    inst->distTotal = dist;
    inst->distTraveled = 0.0f;
    inst->sizeScale = scale;
    inst->elapsed = 0.0f;
    inst->totalLifetime = (dist / ICE_LANCE_WAVE_SPEED) + ICE_LANCE_HOLD_SECS;
    inst->hitDealt = false;

    inst->damage = Skill_CalculateDamage(SKILL_CAT_PROJECTILE, params);
    if (inst->damage <= 0.0f) inst->damage = 42.0f;
    inst->knockback = 0.0f;

    Skill_RequestAnim(agentId, "elbow_punch", ICE_LANCE_CAST_SECS);

    VFX_IceCrystalConfig icfg = VFX_IceCrystal_DefaultConfig();
    VFX_IceCrystal_ApplyPreset(&icfg, VFX_ICE_PRESET_FROST_SPIKE_WAVE);
    icfg.crystalCount = 12;
    icfg.height = 2.4f * scale;
    icfg.radius = 0.24f * scale;
    icfg.formationRadius = 1.15f * scale;
    icfg.eruptDuration = 0.24f;
    icfg.holdDuration = ICE_LANCE_HOLD_SECS;
    icfg.shatterDuration = 0.35f;
    icfg.travelSpeed = ICE_LANCE_WAVE_SPEED;
    icfg.emitFrostMist = true;
    icfg.emitSnowMotes = true;
    icfg.emitShatterChips = true;
    inst->crystalHandle = VFX_ComposeIceCrystalEx(groundStart, groundTarget, &icfg);

    VFX_FreezePatch_Spawn(groundTarget, 2.5f * scale, 0.35f, ICE_LANCE_HOLD_SECS, 0.55f,
                          ELEMENT_COLOR_WATER, WHITE);
    VFX_ComposeMistVeil(groundStart, 1.5f * scale, 0.45f);
    PlayCastSound(EFFECT_PRESET_ICE_SHATTER);
}

void UpdateIceCrystalLanceSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES; i++) {
        IceCrystalLanceInstance *inst = &s_lances[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (inst->distTraveled < inst->distTotal) {
            inst->distTraveled = fminf(inst->distTraveled + ICE_LANCE_WAVE_SPEED * dt, inst->distTotal);
            inst->headPos.x = inst->startPos.x + inst->dir.x * inst->distTraveled;
            inst->headPos.z = inst->startPos.z + inst->dir.z * inst->distTraveled;
            inst->headPos.y = VFX_GroundHeightFromMap(inst->headPos.x, inst->headPos.z, NULL) + 0.6f;

            if (!inst->hitDealt) {
                int hitIds[8];
                int hits = Entity_GetNearbyTargets(inst->headPos, ICE_LANCE_HIT_RADIUS * inst->sizeScale,
                                                   hitIds, 8);
                bool anyEnemy = false;
                for (int h = 0; h < hits; h++) {
                    const Agent *ag = Entity_GetAgent(hitIds[h]);
                    if (ag != NULL && ag->team != inst->casterTeam) {
                        anyEnemy = true;
                        Entity_ApplyStun(hitIds[h], 0.45f);
                    }
                }
                if (anyEnemy || inst->distTraveled >= inst->distTotal) {
                    if (anyEnemy) {
                        inst->hitDealt = true;
                        Entity_ApplyAoEDamage(inst->headPos, 2.3f * inst->sizeScale,
                                              inst->damage, inst->knockback, inst->casterTeam);
                        VFX_ComposeGroundWave((Vector3){ inst->headPos.x, inst->headPos.y - 0.5f, inst->headPos.z },
                                              VC_MAT_WATER, 2.4f * inst->sizeScale, 0.75f,
                                              VFX_GroundHeightFromMap, NULL);
                        PlayImpactSound(EFFECT_PRESET_ICE_SHATTER);
                    }
                }
            }
        }

        if (inst->elapsed >= inst->totalLifetime) {
            inst->active = false;
            inst->crystalHandle = -1;
        }
    }
}

void DrawIceCrystalLanceSkill(void) {
    // Visuals are rendered by VC_IceCrystal + VC_FreezePatch + MistVeil.
}

void UnloadIceCrystalLanceSkill(void) {
    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES; i++) {
        if (s_lances[i].active && s_lances[i].crystalHandle >= 0) {
            VFX_KillIceCrystal(s_lances[i].crystalHandle);
        }
        s_lances[i].active = false;
        s_lances[i].crystalHandle = -1;
    }
}

bool IsIceCrystalLanceSkillCoiling(void) {
    return false;
}

int GetIceCrystalLanceSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES && count < maxProjectiles; i++) {
        if (!s_lances[i].active || s_lances[i].distTraveled >= s_lances[i].distTotal) continue;
        outProjectiles[count++] = (SkillProjectile){
            .position = s_lances[i].headPos,
            .radius = ICE_LANCE_HIT_RADIUS * s_lances[i].sizeScale,
            .active = true
        };
    }
    return count;
}

void DeactivateIceCrystalLanceProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_ICE_LANCE_INSTANCES; i++) {
        if (!s_lances[i].active || s_lances[i].distTraveled >= s_lances[i].distTotal) continue;
        if (count == index) {
            s_lances[i].distTraveled = s_lances[i].distTotal;
            return;
        }
        count++;
    }
}
