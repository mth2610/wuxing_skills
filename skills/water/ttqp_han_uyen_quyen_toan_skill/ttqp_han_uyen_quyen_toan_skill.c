#include "skills/water/ttqp_han_uyen_quyen_toan_skill/ttqp_han_uyen_quyen_toan_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/liquid/liquid_swe.h"
#include "core/liquid/liquid_impact.h"
#include "core/composition/visual_composer.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_HYQT_INST       4
#define HYQT_TELEGRAPH_SECS 0.70f /* Mandatory >= 0.60s counterplay telegraph (C9) */

typedef struct {
    bool    active;
    int     ownerAgentId;
    Vector3 center;
    float   elapsed;
    float   duration;
    float   damage;
    float   sizeScale;
    bool    erupted;
    int     sweHandle;
} HanUyenInst;

static HanUyenInst s_inst[MAX_HYQT_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TtqpHanUyenQuyenToan)

static void SpawnGeyserSSF(Vector3 pos, float upVel, float force01, float scale) {
    LiquidImpactEvent ev = { 0 };
    ev.hitPoint = pos;
    ev.hitNormal = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.impulseDirection = (Vector3){ 0.0f, 1.0f, 0.0f };
    ev.initialVelocity = (Vector3){ 0.0f, upVel, 0.0f };
    ev.force01 = force01;
    ev.scale = scale;
    LiquidImpact_SpawnWater(&ev);
}

static void AbortTtqpHanUyen(int agentId) {
    for (int i = 0; i < MAX_HYQT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
            s_inst[i].active = false;
            s_inst[i].sweHandle = -1;
        }
    }
}

static bool HasActiveTtqpHanUyen(int agentId) {
    for (int i = 0; i < MAX_HYQT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTtqpHanUyenQuyenToanSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_HYQT_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TTQP_HAN_UYEN_QUYEN_TOAN");
    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_HAN_UYEN_QUYEN_TOAN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTtqpHanUyen);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTtqpHanUyen);
    }
}

void CastTtqpHanUyenQuyenToanSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_HYQT_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        if (s_inst[0].sweHandle >= 0) LiquidSWE_Kill(s_inst[0].sweHandle);
    }

    const VoHocSkillDef *def = VoHoc_FindByName("TTQP_HAN_UYEN_QUYEN_TOAN");
    float baseDmg = def ? def->baseDamage : 27.5f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    Vector3 delta = Vector3Subtract(target, startPos);
    delta.y = 0.0f;
    float d = Vector3Length(delta);
    if (d > 8.5f * scale) {
        delta = Vector3Scale(delta, (8.5f * scale) / d);
    }
    Vector3 center = Vector3Add(startPos, delta);

    HanUyenInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->center = center;
    inst->elapsed = 0.0f;
    inst->duration = HYQT_TELEGRAPH_SECS + 1.75f;
    inst->damage = baseDmg;
    inst->sizeScale = scale;
    inst->erupted = false;
    inst->sweHandle = -1; /* Spawned at eruption after telegraph */

    /* Phase 1 Telegraph: subtle bubbling SSF water & frost mist warning on ground */
    Skill_RequestAnim(agentId, "taiji_cloud_hands", HYQT_TELEGRAPH_SECS);
    SpawnGeyserSSF(center, 2.2f, 0.35f, 0.9f * scale);
    VFX_ComposeMistVeilEx(center, VC_MAT_WATER, 3.5f * scale, HYQT_TELEGRAPH_SECS + 0.6f);
    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTtqpHanUyenQuyenToanSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;
    for (int i = 0; i < MAX_HYQT_INST; i++) {
        HanUyenInst *inst = &s_inst[i];
        if (!inst->active) continue;
        inst->elapsed += dt;

        /* Phase 2: After 0.70s clear ground telegraph -> SWE cryo spout + SSF geyser + Freeze */
        if (!inst->erupted && inst->elapsed >= HYQT_TELEGRAPH_SECS) {
            inst->erupted = true;
            Skill_RequestAnim(inst->ownerAgentId, "standing_2h_cast_spell_01", 0.45f);
            float radius = 3.5f * inst->sizeScale;

            LiquidSWEDesc desc = LiquidSWE_DefaultDesc();
            desc.pattern = LIQUID_SWE_PATTERN_WATER_SPOUT;
            desc.source = inst->center;
            desc.target = inst->center;
            desc.waveHeight = 1.55f * inst->sizeScale;
            desc.waveSpeed = 9.5f;
            desc.width = 7.0f * inst->sizeScale;
            desc.length = 7.0f * inst->sizeScale;
            desc.duration = inst->duration - HYQT_TELEGRAPH_SECS;
            desc.deepColor = ELEMENT_COLOR_WATER;
            desc.cryoMode = 1.0f; /* Flash-freeze water cyclone */
            inst->sweHandle = LiquidSWE_Spawn(&desc);

            SpawnGeyserSSF(inst->center, 8.5f, 1.0f, 1.45f * inst->sizeScale);

            int ids[24];
            int cnt = Entity_GetNearbyTargets(inst->center, radius, ids, 24);
            bool anyHit = false;
            for (int k = 0; k < cnt; k++) {
                if (!Entity_CanDamageTarget(inst->ownerAgentId, ids[k])) continue;
                Entity_ApplyPull(ids[k], inst->center, 8.5f, 0.35f);
                Entity_ApplyDamageFrom(ids[k], inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0 });
                Entity_ApplyFreeze(ids[k], 0.85f);
                VoHoc_ApplyWaterMark(inst->ownerAgentId, ids[k], 2);
                anyHit = true;
            }
            PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
            (void)anyHit;
        }

        if (inst->elapsed >= inst->duration) {
            if (inst->sweHandle >= 0) LiquidSWE_Kill(inst->sweHandle);
            inst->sweHandle = -1;
            inst->active = false;
        }
    }
}

void DrawTtqpHanUyenQuyenToanSkill(void) {
    for (int i = 0; i < MAX_HYQT_INST; i++) {
        HanUyenInst *inst = &s_inst[i];
        if (!inst->active) continue;
        float radius = 3.5f * inst->sizeScale;
        if (!inst->erupted) {
            /* High-readability ground telegraph ring + converging water ripple */
            float t01 = inst->elapsed / HYQT_TELEGRAPH_SECS;
            VFX_ComposeRuneCircle(inst->center, (Vector3){ 0.0f, 1.0f, 0.0f },
                                  VC_MAT_WATER, radius, t01, 2);
            VFX_ComposeWaterRing(inst->center, radius * (1.0f - 0.55f * t01), t01);
        } else {
            /* Cryo ice crystals erupting around the frozen spout */
            float post = (inst->elapsed - HYQT_TELEGRAPH_SECS) / 0.85f;
            if (post < 1.0f) {
                VFX_DrawIceCrystalBurst(inst->center, 7, 42 + i * 13, post);
            }
        }
    }
}

void UnloadTtqpHanUyenQuyenToanSkill(void) {
    for (int i = 0; i < MAX_HYQT_INST; i++) {
        if (s_inst[i].sweHandle >= 0) LiquidSWE_Kill(s_inst[i].sweHandle);
        s_inst[i].active = false;
        s_inst[i].sweHandle = -1;
    }
}
