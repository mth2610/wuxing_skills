#include "skills/water/ttqp_bang_trieu_dao_hai_skill/ttqp_bang_trieu_dao_hai_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/liquid/liquid_swe.h"
#include "core/liquid/liquid_impact.h"
#include "core/composition/visual_composer.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BTDH_INST    4
#define MAX_BTDH_HIT_IDS 16

typedef struct {
    bool    active;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    float   elapsed;
    float   duration;
    float   damage;
    float   sizeScale;
    int     hitIds[MAX_BTDH_HIT_IDS];
    int     hitCount;
    bool    splash1Done;
    bool    splash2Done;
    int     sweHandle;
} BangTrieuInst;

static BangTrieuInst s_inst[MAX_BTDH_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TtqpBangTrieuDaoHai)

static void SpawnWaveCrashSSF(Vector3 pos, Vector3 dir, float force01, float scale) {
    LiquidImpactEvent ev = { 0 };
    ev.hitPoint = pos;
    ev.hitNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.impulseDirection = Vector3Normalize((Vector3){ dir.x, 0.45f, dir.z });
    ev.initialVelocity = (Vector3){ dir.x * 8.0f, 3.8f, dir.z * 8.0f };
    ev.force01 = force01;
    ev.scale = scale;
    LiquidImpact_SpawnWater(&ev);
}

static bool AlreadyHit(const BangTrieuInst *inst, int id) {
    for (int i = 0; i < inst->hitCount; i++) {
        if (inst->hitIds[i] == id) return true;
    }
    return false;
}

static void AbortTtqpBangTrieu(int agentId) {
    for (int i = 0; i < MAX_BTDH_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
            s_inst[i].active = false;
            s_inst[i].sweHandle = -1;
        }
    }
}

static bool HasActiveTtqpBangTrieu(int agentId) {
    for (int i = 0; i < MAX_BTDH_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTtqpBangTrieuDaoHaiSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_BTDH_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TTQP_BANG_TRIEU_DAO_HAI");
    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_BANG_TRIEU_DAO_HAI");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTtqpBangTrieu);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTtqpBangTrieu);
    }
}

void CastTtqpBangTrieuDaoHaiSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_BTDH_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        if (s_inst[0].sweHandle >= 0) LiquidSWE_Kill(s_inst[0].sweHandle);
    }

    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_BANG_TRIEU_DAO_HAI");
    float baseDmg = def ? def->baseDamage : 16.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    Vector3 dir = Vector3Subtract(target, startPos);
    dir.y = 0.0f;
    float len = Vector3Length(dir);
    if (len < 0.01f) dir = (Vector3){ 0.0f, 0.0f, 1.0f };
    else dir = Vector3Scale(dir, 1.0f / len);

    float range = 6.0f * scale;
    Vector3 endPos = Vector3Add(startPos, Vector3Scale(dir, range));

    Skill_RequestAnim(agentId, "two_hand_spell_casting", 0.42f);

    BangTrieuInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->origin = startPos;
    inst->dir = dir;
    inst->elapsed = 0.0f;
    inst->duration = 1.50f;
    inst->damage = baseDmg;
    inst->sizeScale = scale;
    inst->hitCount = 0;
    inst->splash1Done = false;
    inst->splash2Done = false;

    LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
    desc.pattern = LIQUID_SWE_PATTERN_CURLING_WAVE;
    desc.source = startPos;
    desc.target = endPos;
    desc.waveHeight = 1.20f * scale;
    desc.waveSpeed = 11.5f;
    desc.width = 5.6f * scale;
    desc.length = range;
    desc.duration = inst->duration;
    desc.curlIntensity = 1.35f;
    desc.deepColor = ELEMENT_COLOR_WATER;
    desc.cryoMode = 0.25f;
    inst->sweHandle = LiquidSWE_Spawn(&desc);

    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTtqpBangTrieuDaoHaiSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;
    const float cosHalfCone = cosf(35.0f * (PI / 180.0f)); /* 70 deg cone */

    for (int i = 0; i < MAX_BTDH_INST; i++) {
        BangTrieuInst *inst = &s_inst[i];
        if (!inst->active) continue;
        inst->elapsed += dt;

        /* Progressive wave front: advances 0 -> 6.0m over 0.45s so distant targets can react/dodge */
        if (inst->elapsed <= 0.55f) {
            float frac = inst->elapsed / 0.45f;
            if (frac > 1.0f) frac = 1.0f;
            float waveDist = frac * 6.0f * inst->sizeScale;
            Vector3 crest = Vector3Add(inst->origin, Vector3Scale(inst->dir, waveDist));
            Combat_DeflectProjectilesInRadius(crest, 2.3f * inst->sizeScale, inst->ownerAgentId);

            if (!inst->splash1Done && frac >= 0.45f) {
                inst->splash1Done = true;
                SpawnWaveCrashSSF(crest, inst->dir, 0.80f, 1.20f * inst->sizeScale);
            }
            if (!inst->splash2Done && frac >= 0.88f) {
                inst->splash2Done = true;
                SpawnWaveCrashSSF(crest, inst->dir, 0.90f, 1.35f * inst->sizeScale);
            }

            int ids[24];
            int cnt = Entity_GetNearbyTargets(inst->origin, waveDist + 0.6f, ids, 24);
            for (int k = 0; k < cnt; k++) {
                if (AlreadyHit(inst, ids[k])) continue;
                if (!Entity_CanDamageTarget(inst->ownerAgentId, ids[k])) continue;
                const Agent *t = Entity_GetAgent(ids[k]);
                if (!t) continue;
                Vector3 toT = Vector3Subtract(t->position, inst->origin);
                toT.y = 0.0f;
                float d = Vector3Length(toT);
                if (d > 0.01f) {
                    float dot = (toT.x * inst->dir.x + toT.z * inst->dir.z) / d;
                    if (dot < cosHalfCone) continue;
                }
                if (inst->hitCount < MAX_BTDH_HIT_IDS) inst->hitIds[inst->hitCount++] = ids[k];
                Vector3 kb = Vector3Scale(inst->dir, 5.5f);
                Entity_ApplyDamageFrom(ids[k], inst->ownerAgentId, inst->damage, 0.0f, kb);
                Entity_AddModifier(ids[k], 0.70f, 1.5f); /* Slow 30% for 1.5s */
                VoHoc_ApplyWaterMark(inst->ownerAgentId, ids[k], 1);
                PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
            }
        }

        if (inst->elapsed >= inst->duration) {
            if (inst->sweHandle >= 0) LiquidSWE_Kill(inst->sweHandle);
            inst->sweHandle = -1;
            inst->active = false;
        }
    }
}

void DrawTtqpBangTrieuDaoHaiSkill(void) {}

void UnloadTtqpBangTrieuDaoHaiSkill(void) {
    for (int i = 0; i < MAX_BTDH_INST; i++) {
        if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
}
