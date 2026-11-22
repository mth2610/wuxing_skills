#include "skills/earth/btbc_bang_son_toai_chuong_skill/btbc_bang_son_toai_chuong_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "core/wind/wind_system.h"
#include "post_process/camera_fx.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_BSTC_INST   4
#define BSTC_HIT_RADIUS 1.6f

typedef struct {
    bool      active;
    bool      impacted;
    int       ownerAgentId;
    AgentTeam casterTeam;
    Vector3   origin;
    Vector3   dir;
    float     elapsed;
    float     impactSecs;
    float     damage;
    float     sizeScale;
    int       craterHandle;
} BangSonInst;

static BangSonInst s_inst[MAX_BSTC_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(BtbcBangSonToaiChuong)

static void KillBangSonHandles(BangSonInst *inst) {
    if (inst->craterHandle >= 0) {
        VFX_KillEarthSpike(inst->craterHandle);
        inst->craterHandle = -1;
    }
}

static void AbortBtbcBangSon(int agentId) {
    for (int i = 0; i < MAX_BSTC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillBangSonHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveBtbcBangSon(int agentId) {
    for (int i = 0; i < MAX_BSTC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitBtbcBangSonToaiChuongSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_BSTC_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].craterHandle = -1;
    }

    s_skillIndex = Skill_GetIndexByName("BTBC_BANG_SON_TOAI_CHUONG");
    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_BANG_SON_TOAI_CHUONG");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortBtbcBangSon);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveBtbcBangSon);
    }
}

void CastBtbcBangSonToaiChuongSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_BSTC_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillBangSonHandles(&s_inst[slot]);

    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_BANG_SON_TOAI_CHUONG");
    float baseDmg = def ? def->baseDamage : 28.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    BangSonInst *inst = &s_inst[slot];
    inst->active = true;
    inst->impacted = false;
    inst->ownerAgentId = agentId;
    inst->casterTeam = owner ? owner->team : TEAM_ALLY;
    inst->origin = anchor;
    inst->dir = dir;
    inst->elapsed = 0.0f;
    inst->impactSecs = 0.22f;
    inst->damage = baseDmg * pveMult;
    inst->sizeScale = scale;
    inst->craterHandle = -1;

    /* Wind-up gather dust under double-palm stance */
    Skill_RequestAnim(agentId, "crane_wing_strike", 0.22f);
    const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(agentId);
    if (anim) {
        SkillHelper_StartMeleeTrails(agentId, anim, anchor, atan2f(dir.x, dir.z), 2, 0.38f);
    }
    VFX_ComposeGroundDustRing(anchor, VC_MAT_EARTH, 0.75f * scale, 0.35f);
}

static void TriggerBangSonImpact(BangSonInst *inst) {
    inst->impacted = true;
    Skill_RequestAnim(inst->ownerAgentId, "hell_slammer_a", 0.35f);
    const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
    Vector3 basePos = owner ? owner->position : inst->origin;
    Vector3 impactPos = Vector3Add(basePos, Vector3Scale(inst->dir, 0.85f * inst->sizeScale));
    impactPos.y = MapManager_GetGroundHeightAt(impactPos.x, impactPos.z);

    /* Heavy hitstop + camera trauma + shattered-earth crater ring */
    TimeFX_Hitstop(0.075f, 0.06f);
    CameraFX_Shake(0.50f);

    VFX_EarthSpikeConfig craterCfg = VFX_EarthSpike_DefaultConfig();
    VFX_EarthSpike_ApplyPreset(&craterCfg, VFX_EARTH_SPIKE_PRESET_EARTH_SPIKES);
    craterCfg.shape = VFX_EARTH_SPIKE_SHAPE_SLAB;
    craterCfg.style = VFX_EARTH_SPIKE_STYLE_BASALT;
    craterCfg.formation = VFX_EARTH_SPIKE_FORMATION_RING;
    craterCfg.exitMode = VFX_EARTH_SPIKE_EXIT_SHATTER;
    craterCfg.facetCount = 6;
    craterCfg.spikeCount = 8;
    craterCfg.formationRadius = 1.15f * inst->sizeScale;
    craterCfg.height = 0.48f * inst->sizeScale;
    craterCfg.baseRadius = 0.28f * inst->sizeScale;
    craterCfg.topRadiusRatio = 0.55f;
    craterCfg.tiltMaxDeg = 32.0f;
    craterCfg.eruptDuration = 0.08f;
    craterCfg.holdDuration = 0.32f;
    craterCfg.retractDuration = 0.16f;
    craterCfg.emissionIntensity = 0.0f;
    craterCfg.emitDust = true;
    craterCfg.emitDebris = true;
    craterCfg.debrisChips = 10;
    craterCfg.spawnGroundCrack = false;
    craterCfg.groundWaveBurst = false;
    inst->craterHandle = VFX_ComposeEarthSpikeEx(impactPos, impactPos, &craterCfg);

    VFX_EarthCrackConfig ccfg = VFX_EarthCrack_DefaultConfig();
    ccfg.spreadDuration = 0.12f;
    ccfg.holdDuration = 1.60f;
    ccfg.fadeDuration = 0.65f;
    ccfg.veinGlowIntensity = 0.25f;
    VFX_ComposeEarthCrackRadial(impactPos, 2.25f * inst->sizeScale, &ccfg);
    VFX_ComposeGroundDustRing(impactPos, VC_MAT_EARTH, 1.45f * inst->sizeScale, 0.60f);
    VFX_ComposeImpactDustVariant(impactPos, VC_MAT_EARTH, 1.25f * inst->sizeScale, 0.65f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
    VFX_ComposeDebrisShards(impactPos, (Vector3){ 0.0f, 3.8f, 0.0f }, VC_MAT_EARTH, 0.058f * inst->sizeScale, 12);
    VFX_ComposeGroundWave(impactPos, VC_MAT_EARTH, 2.1f * inst->sizeScale, 0.32f, VFX_GroundHeightFromMap, NULL);
    Wind_SpawnRadialBlast(impactPos, 2.8f * inst->sizeScale, 8.0f, 0.30f);

    float r = BSTC_HIT_RADIUS * inst->sizeScale;
    int targets[16];
    int cnt = Entity_GetNearbyTargets(impactPos, r, targets, 16);
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;

        float mult = 1.0f;
        bool aftershock = VoHoc_OnBangSonConsume(inst->ownerAgentId, tid, s_skillIndex, impactPos, &mult);
        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage * mult, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
        Entity_ApplyRoot(tid, 1.30f);
        Entity_ApplyStatus(tid, STATUS_VULNERABLE, 0.25f, 3.5f, inst->ownerAgentId);

        if (aftershock) {
            VFX_ComposeGroundDustRing(impactPos, VC_MAT_EARTH, 2.4f * inst->sizeScale, 0.70f);
            VFX_ComposeGroundWave(impactPos, VC_MAT_EARTH, 2.6f * inst->sizeScale, 0.35f, VFX_GroundHeightFromMap, NULL);
        }
    }
}

void UpdateBtbcBangSonToaiChuongSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_BSTC_INST; i++) {
        BangSonInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (!inst->impacted && inst->elapsed >= inst->impactSecs) {
            TriggerBangSonImpact(inst);
        }
        if (inst->elapsed >= inst->impactSecs + 0.65f) {
            inst->craterHandle = -1;
            inst->active = false;
        }
    }
}

void DrawBtbcBangSonToaiChuongSkill(void) { }

void UnloadBtbcBangSonToaiChuongSkill(void) {
    for (int i = 0; i < MAX_BSTC_INST; i++) {
        if (s_inst[i].active) KillBangSonHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
