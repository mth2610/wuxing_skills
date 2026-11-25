#include "skills/earth/btbc_thiet_son_khao_skill/btbc_thiet_son_khao_skill.h"
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

#define MAX_TSK_INST   4
#define TSK_DASH_DIST  2.8f
#define TSK_HIT_RADIUS 1.6f

typedef struct {
    bool      active;
    bool      impacted;
    int       ownerAgentId;
    AgentTeam casterTeam;
    Vector3   startPos;
    Vector3   dir;
    float     elapsed;
    float     windupSecs;
    float     dashSecs;
    float     damage;
    float     sizeScale;
    float     furrowAccum;
    int       armorHandle;
} ThietSonInst;

static ThietSonInst s_inst[MAX_TSK_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(BtbcThietSonKhao)

static void KillThietSonHandles(ThietSonInst *inst) {
    if (inst->armorHandle >= 0) {
        VFX_StoneArmor_Stop(inst->armorHandle);
        inst->armorHandle = -1;
    }
}

static void AbortBtbcThietSon(int agentId) {
    for (int i = 0; i < MAX_TSK_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillThietSonHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveBtbcThietSon(int agentId) {
    for (int i = 0; i < MAX_TSK_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitBtbcThietSonKhaoSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_TSK_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].armorHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("BTBC_THIET_SON_KHAO");
    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_THIET_SON_KHAO");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortBtbcThietSon);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveBtbcThietSon);
    }
}

void CastBtbcThietSonKhaoSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_TSK_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillThietSonHandles(&s_inst[slot]);

    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_THIET_SON_KHAO");
    float baseDmg = def ? def->baseDamage : 21.5f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    Entity_AddShield(agentId, 14.0f * pveMult, 2.5f);

    ThietSonInst *inst = &s_inst[slot];
    inst->active = true;
    inst->impacted = false;
    inst->ownerAgentId = agentId;
    inst->casterTeam = owner ? owner->team : TEAM_ALLY;
    inst->startPos = anchor;
    inst->dir = dir;
    inst->elapsed = 0.0f;
    inst->windupSecs = 0.10f;
    inst->dashSecs = 0.22f;
    inst->damage = baseDmg * pveMult;
    inst->sizeScale = scale;
    inst->furrowAccum = 0.0f;
    inst->armorHandle = VFX_StoneArmor_Spawn(anchor, dir, VFX_STONE_ARMOR_SHOULDER_RAM, scale, 0.55f);

    /* Heavy stance stomp + ground crack + plowed furrow path */
    Skill_RequestAnim(agentId, "crouch_idle", 0.12f);
    VFX_EarthCrackConfig ccfg = VFX_EarthCrack_DefaultConfig();
    ccfg.spreadDuration = 0.18f;
    ccfg.holdDuration = 1.25f;
    ccfg.fadeDuration = 0.55f;
    ccfg.veinGlowIntensity = 0.18f;
    Vector3 endPos = Vector3Add(anchor, Vector3Scale(dir, TSK_DASH_DIST * scale));
    VFX_ComposeEarthCrackAB(anchor, endPos, 0.44f * scale, &ccfg);
    VFX_ComposeGroundDustRing(anchor, VC_MAT_EARTH, 0.78f * scale, 0.50f);
    Wind_SpawnRadialBlast(anchor, 1.6f * scale, 4.5f, 0.20f);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}

static void TriggerThietSonImpact(ThietSonInst *inst, Vector3 hitPos) {
    inst->impacted = true;
    float r = TSK_HIT_RADIUS * inst->sizeScale;

    int targets[16];
    int cnt = Entity_GetNearbyTargets(hitPos, r, targets, 16);
    bool hitAny = false;
    for (int k = 0; k < cnt; k++) {
        if (targets[k] == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, targets[k])) continue;
        Entity_ApplyDamageFrom(targets[k], inst->ownerAgentId, inst->damage, 0.0f, Vector3Scale(inst->dir, 2.8f));
        Entity_ApplyStun(targets[k], 0.60f);
        VoHoc_ApplyEarthMark(inst->ownerAgentId, targets[k], 1);
        hitAny = true;
    }

    if (hitAny) TimeFX_Hitstop(0.045f, 0.08f);
    CameraFX_Shake(0.32f);
    VFX_SpawnGroundWave(hitPos, VC_MAT_EARTH, 1.85f * inst->sizeScale, 0.32f);
    VFX_MartialShockCone_Spawn((Vector3){ hitPos.x, hitPos.y + 0.95f, hitPos.z }, inst->dir,
                               VC_MAT_EARTH, 2.5f * inst->sizeScale, 1.15f * inst->sizeScale, 0.28f);
    VFX_ComposeGroundDustRing(hitPos, VC_MAT_EARTH, 1.15f * inst->sizeScale, 0.55f);
    VFX_ComposeImpactDustVariant(hitPos, VC_MAT_EARTH, 1.10f * inst->sizeScale, 0.60f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
    VFX_ComposeDebrisShards(hitPos, (Vector3){ inst->dir.x * 2.8f, 3.4f, inst->dir.z * 2.8f },
                            VC_MAT_EARTH, 0.058f * inst->sizeScale, 10);
    Wind_SpawnRadialBlast(hitPos, 2.4f * inst->sizeScale, 6.5f, 0.25f);
    PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
}

void UpdateBtbcThietSonKhaoSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_TSK_INST; i++) {
        ThietSonInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (!owner || !owner->active) {
            KillThietSonHandles(inst);
            inst->active = false;
            continue;
        }

        if (inst->elapsed >= inst->windupSecs && inst->elapsed <= inst->windupSecs + inst->dashSecs) {
            if (inst->elapsed - dt < inst->windupSecs) {
                Skill_RequestAnim(inst->ownerAgentId, "mutant_jump_attack", 0.28f);
                const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
                if (anim) {
                    SkillHelper_StartMeleeTrails(inst->ownerAgentId, anim, owner->position,
                                                 atan2f(inst->dir.x, inst->dir.z), 0, 0.28f);
                }
            }
            float t = (inst->elapsed - inst->windupSecs) / inst->dashSecs;
            Vector3 p = Vector3Add(inst->startPos, Vector3Scale(inst->dir, TSK_DASH_DIST * inst->sizeScale * t));
            p.y = MapManager_GetGroundHeightAt(p.x, p.z);
            Entity_SetPosition(inst->ownerAgentId, p);
            VFX_StoneArmor_SetTransform(inst->armorHandle, p, inst->dir);

            /* Continuous earth-plowing wake under feet */
            inst->furrowAccum += dt;
            if (inst->furrowAccum >= 0.035f) {
                inst->furrowAccum = 0.0f;
                VFX_ComposeDebrisShards(p, (Vector3){ -inst->dir.x * 2.0f, 2.2f, -inst->dir.z * 2.0f },
                                        VC_MAT_EARTH, 0.042f * inst->sizeScale, 3);
                VFX_ComposeImpactDustVariant(p, VC_MAT_EARTH, 0.52f * inst->sizeScale, 0.35f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
                Wind_SpawnGust(p, inst->dir, 1.5f * inst->sizeScale, 7.5f, 0.14f);
            }

            if (!inst->impacted) {
                int targets[8];
                int cnt = Entity_GetNearbyTargets(p, 1.15f * inst->sizeScale, targets, 8);
                for (int k = 0; k < cnt; k++) {
                    if (targets[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, targets[k])) {
                        TriggerThietSonImpact(inst, p);
                        break;
                    }
                }
            }
        } else if (inst->elapsed > inst->windupSecs + inst->dashSecs) {
            if (!inst->impacted) {
                TriggerThietSonImpact(inst, owner->position);
            }
            KillThietSonHandles(inst);
            inst->active = false;
        } else {
            VFX_StoneArmor_SetTransform(inst->armorHandle, owner->position, inst->dir);
        }
    }
}

void DrawBtbcThietSonKhaoSkill(void) { }

void UnloadBtbcThietSonKhaoSkill(void) {
    for (int i = 0; i < MAX_TSK_INST; i++) {
        if (s_inst[i].active) KillThietSonHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
