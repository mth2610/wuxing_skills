#include "skills/earth/btbc_thach_tru_xung_thien_skill/btbc_thach_tru_xung_thien_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/wind/wind_system.h"
#include "post_process/camera_fx.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TTXT_INST      4
#define TTXT_RADIUS        3.2f
#define TTXT_TELEGRAPH_SEC 0.65f

typedef struct {
    bool      active;
    bool      erupted;
    int       ownerAgentId;
    AgentTeam casterTeam;
    Vector3   targetPos;
    float     elapsed;
    float     pebbleAccum;
    float     damage;
    float     sizeScale;
    int       heroSpikeHandle;
    int       ringSpikeHandle;
} ThachTruInst;

static ThachTruInst s_inst[MAX_TTXT_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(BtbcThachTruXungThien)

static void KillThachTruHandles(ThachTruInst *inst) {
    if (inst->heroSpikeHandle >= 0) {
        VFX_KillEarthSpike(inst->heroSpikeHandle);
        inst->heroSpikeHandle = -1;
    }
    if (inst->ringSpikeHandle >= 0) {
        VFX_KillEarthSpike(inst->ringSpikeHandle);
        inst->ringSpikeHandle = -1;
    }
}

static void AbortBtbcThachTru(int agentId) {
    for (int i = 0; i < MAX_TTXT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillThachTruHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveBtbcThachTru(int agentId) {
    for (int i = 0; i < MAX_TTXT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitBtbcThachTruXungThienSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_TTXT_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].heroSpikeHandle = -1;
        s_inst[i].ringSpikeHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("BTBC_THACH_TRU_XUNG_THIEN");
    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_THACH_TRU_XUNG_THIEN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortBtbcThachTru);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveBtbcThachTru);
    }
}

void CastBtbcThachTruXungThienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    (void)startPos;
    int slot = -1;
    for (int i = 0; i < MAX_TTXT_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillThachTruHandles(&s_inst[slot]);

    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_THACH_TRU_XUNG_THIEN");
    float baseDmg = def ? def->baseDamage : 27.5f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 tgt = target;
    tgt.y = MapManager_GetGroundHeightAt(tgt.x, tgt.z);

    ThachTruInst *inst = &s_inst[slot];
    inst->active = true;
    inst->erupted = false;
    inst->ownerAgentId = agentId;
    inst->casterTeam = owner ? owner->team : TEAM_ALLY;
    inst->targetPos = tgt;
    inst->elapsed = 0.0f;
    inst->pebbleAccum = 0.09f;
    inst->damage = baseDmg * pveMult;
    inst->sizeScale = scale;
    inst->heroSpikeHandle = -1;
    inst->ringSpikeHandle = -1;

    /* C9 Telegraph (0.65s): ground fissure cracks + hopping pebbles */
    Skill_RequestAnim(agentId, "wide_arm_spell_casting", TTXT_TELEGRAPH_SEC);
    VFX_EarthCrackConfig ccfg = VFX_EarthCrack_DefaultConfig();
    ccfg.spreadDuration = TTXT_TELEGRAPH_SEC;
    ccfg.holdDuration = 0.85f;
    ccfg.fadeDuration = 0.55f;
    ccfg.veinGlowIntensity = 0.28f;
    VFX_ComposeEarthCrackRadial(tgt, TTXT_RADIUS * scale, &ccfg);
    VFX_ComposeGroundDustRing(tgt, VC_MAT_EARTH, 1.35f * scale, 0.35f);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}

static void TriggerThachTruEruption(ThachTruInst *inst) {
    inst->erupted = true;
    Skill_RequestAnim(inst->ownerAgentId, "standing_2h_cast_spell_01", 0.45f);
    float r = TTXT_RADIUS * inst->sizeScale;

    /* Layer 1: 6 tall tilted hexagonal basalt hero pillars */
    VFX_EarthSpikeConfig heroCfg = VFX_EarthSpike_DefaultConfig();
    VFX_EarthSpike_ApplyPreset(&heroCfg, VFX_EARTH_SPIKE_PRESET_EARTH_SPIKES);
    heroCfg.shape = VFX_EARTH_SPIKE_SHAPE_PILLAR;
    heroCfg.style = VFX_EARTH_SPIKE_STYLE_BASALT;
    heroCfg.formation = VFX_EARTH_SPIKE_FORMATION_CLUSTER;
    heroCfg.exitMode = VFX_EARTH_SPIKE_EXIT_SHATTER;
    heroCfg.facetCount = 6;
    heroCfg.spikeCount = 6;
    heroCfg.formationRadius = r * 0.56f;
    heroCfg.height = 2.75f * inst->sizeScale;
    heroCfg.baseRadius = 0.44f * inst->sizeScale;
    heroCfg.topRadiusRatio = 0.66f;
    heroCfg.tiltMaxDeg = 22.0f;
    heroCfg.eruptDuration = 0.14f;
    heroCfg.holdDuration = 0.60f;
    heroCfg.retractDuration = 0.22f;
    heroCfg.emissionIntensity = 0.0f;
    heroCfg.emitDust = true;
    heroCfg.emitDebris = true;
    heroCfg.debrisChips = 12;
    heroCfg.spawnGroundCrack = false;
    heroCfg.groundWaveBurst = true;
    inst->heroSpikeHandle = VFX_ComposeEarthSpikeEx(inst->targetPos, inst->targetPos, &heroCfg);

    /* Layer 2: Outer ring of 8 jagged basalt spurs */
    VFX_EarthSpikeConfig ringCfg = heroCfg;
    ringCfg.shape = VFX_EARTH_SPIKE_SHAPE_SPIKE;
    ringCfg.formation = VFX_EARTH_SPIKE_FORMATION_RING;
    ringCfg.spikeCount = 8;
    ringCfg.formationRadius = r * 0.85f;
    ringCfg.height = 1.35f * inst->sizeScale;
    ringCfg.baseRadius = 0.32f * inst->sizeScale;
    ringCfg.topRadiusRatio = 0.22f;
    ringCfg.tiltMaxDeg = 28.0f;
    ringCfg.staggerDelay = 0.02f;
    ringCfg.holdDuration = 0.50f;
    ringCfg.debrisChips = 6;
    ringCfg.groundWaveBurst = false;
    inst->ringSpikeHandle = VFX_ComposeEarthSpikeEx(inst->targetPos, inst->targetPos, &ringCfg);

    CameraFX_Shake(0.45f);
    VFX_SpawnGroundWave(inst->targetPos, VC_MAT_EARTH, r * 1.15f, 0.38f);
    Wind_SpawnRadialBlast(inst->targetPos, r * 1.35f, 8.5f, 0.35f);
    PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);

    /* Apply 27.5 dmg, Launch 0.85s (v_y = 4.17), Weaken 35% (2.5s), +2 Chấn Ấn */
    int targets[16];
    int cnt = Entity_GetNearbyTargets(inst->targetPos, r, targets, 16);
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
        Entity_ApplyLaunch(tid, 4.17f, (Vector3){ 0.0f, 0.0f, 0.0f });
        Entity_ApplyStatus(tid, STATUS_WEAKEN, 0.35f, 2.5f, inst->ownerAgentId);
        VoHoc_ApplyEarthMark(inst->ownerAgentId, tid, 2);
    }
}

void UpdateBtbcThachTruXungThienSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_TTXT_INST; i++) {
        ThachTruInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (!inst->erupted) {
            inst->pebbleAccum += dt;
            if (inst->pebbleAccum >= 0.09f) {
                inst->pebbleAccum = 0.0f;
                float a = inst->elapsed * 21.0f;
                float rad = (0.6f + 1.8f * (inst->elapsed / TTXT_TELEGRAPH_SEC)) * inst->sizeScale;
                Vector3 p = inst->targetPos;
                p.x += cosf(a) * rad;
                p.z += sinf(a) * rad;
                VFX_ComposeDebrisShards(p, (Vector3){ 0.0f, 2.1f, 0.0f }, VC_MAT_EARTH, 0.036f, 4);
                VFX_ComposeImpactDustVariant(p, VC_MAT_EARTH, 0.45f * inst->sizeScale, 0.25f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
            }
            if (inst->elapsed >= TTXT_TELEGRAPH_SEC) {
                TriggerThachTruEruption(inst);
            }
        } else if (inst->elapsed >= TTXT_TELEGRAPH_SEC + 1.15f) {
            inst->heroSpikeHandle = -1;
            inst->ringSpikeHandle = -1;
            inst->active = false;
        }
    }
}

void DrawBtbcThachTruXungThienSkill(void) {
    for (int i = 0; i < MAX_TTXT_INST; i++) {
        const ThachTruInst *inst = &s_inst[i];
        if (!inst->active || inst->erupted) continue;
        float prog = Clamp(inst->elapsed / TTXT_TELEGRAPH_SEC, 0.0f, 1.0f);
        VFX_ComposeRuneCircle(inst->targetPos, (Vector3){ 0.0f, 1.0f, 0.0f },
                              VC_MAT_EARTH, TTXT_RADIUS * inst->sizeScale, prog, 1);
    }
}

void UnloadBtbcThachTruXungThienSkill(void) {
    for (int i = 0; i < MAX_TTXT_INST; i++) {
        if (s_inst[i].active) KillThachTruHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
