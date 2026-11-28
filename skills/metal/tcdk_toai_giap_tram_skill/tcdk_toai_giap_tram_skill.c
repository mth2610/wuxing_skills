#include "skills/metal/tcdk_toai_giap_tram_skill/tcdk_toai_giap_tram_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/time_fx.h"
#include "core/wind/wind_system.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TGT_INST   4
#define TGT_RANGE      2.4f

typedef struct {
    bool    active;
    bool    cleaved;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    float   elapsed;
    float   castSecs;
    float   damage;
    float   sizeScale;
} ToaiGiapInst;

static ToaiGiapInst s_inst[MAX_TGT_INST];
static int          s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(TcdkToaiGiapTram)

static void AbortTcdkToaiGiap(int agentId)
{
    for (int i = 0; i < MAX_TGT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTcdkToaiGiap(int agentId)
{
    for (int i = 0; i < MAX_TGT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTcdkToaiGiapTramSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_TGT_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("TCDK_TOAI_GIAP_TRAM");
    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_TOAI_GIAP_TRAM");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTcdkToaiGiap);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTcdkToaiGiap);
    }
}

void CastTcdkToaiGiapTramSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_TGT_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_TOAI_GIAP_TRAM");
    float baseDmg = def ? def->baseDamage : 24.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Bước chéo chân trụ, giơ cao trường kiếm tích thế trọng lực */
    Skill_RequestAnim(agentId, "wuxia_sword_draw", 0.22f);

    ToaiGiapInst *inst = &s_inst[slot];
    inst->active       = true;
    inst->cleaved      = false;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->dir          = dir;
    inst->elapsed      = 0.0f;
    inst->castSecs     = def ? def->castSecs : 0.25f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;

    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

static void TriggerToaiGiapCleave(ToaiGiapInst *inst)
{
    inst->cleaved = true;
    const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
    Vector3 anchor = owner ? owner->position : inst->origin;
    float r = TGT_RANGE * inst->sizeScale;

    /* Phát & Thu: Bổ trọng lực 90° tầm 2.4m, đồng bộ mặt phẳng mũi kiếm */
    Skill_RequestAnim(inst->ownerAgentId, "wuxia_sword_heavy_cleave", 0.55f);
    Vector3 pivot = { anchor.x, MapManager_GetGroundHeightAt(anchor.x, anchor.z) + 1.05f, anchor.z };
    Vector3 right = { -inst->dir.z, 0.0f, inst->dir.x };
    Vector3 planeNormal = Vector3Normalize(Vector3Add(right, (Vector3){ 0.0f, 0.12f, 0.0f }));
    Vector3 slashDir = inst->dir;
    const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
    if (owner && anim) {
        Vector3 bonePivot, boneDir, boneNormal;
        if (SkillHelper_SampleBoneSwingPlane(anim, owner->position, owner->facingYaw,
                                             "mixamorig:RightHand", (Vector3){ 0.0f, 0.85f, 0.0f },
                                             &bonePivot, &boneDir, &boneNormal)) {
            pivot = bonePivot;
            planeNormal = boneNormal;
            if (Vector3DotProduct(boneDir, inst->dir) > 0.2f)
                slashDir = boneDir;
        }
    }
    VFX_MetalSlash_Spawn(pivot, slashDir, planeNormal, r, 95.0f, 0.30f, inst->sizeScale, true);
    VFX_MartialShockCone_Spawn(pivot, slashDir, VC_MAT_METAL, r * 0.95f, 0.95f * inst->sizeScale, 0.24f);

    Vector3 hitCenter = Vector3Add(anchor, Vector3Scale(inst->dir, r * 0.55f));
Wind_SpawnRadialBlast(hitCenter, r * 0.95f, 6.0f, 0.18f);

    int targets[12];
    int cnt = Entity_GetNearbyTargets(hitCenter, r * 0.65f, targets, 12);
    bool hitAny = false;
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
        Entity_ApplyStatus(tid, STATUS_VULNERABLE, 0.20f, 3.0f, inst->ownerAgentId);
        Entity_ApplyStatus(tid, STATUS_BLEED, 10.0f / 3.0f, 3.0f, inst->ownerAgentId);
        VoHoc_ApplyMetalMark(inst->ownerAgentId, tid, 1);
        hitAny = true;
    }

    if (hitAny) {
        TimeFX_Hitstop(0.045f, 0.08f);
        VFX_ComposeContactSparkMode(hitCenter, VC_MAT_METAL, 0.85f * inst->sizeScale, 0.90f, CONTACT_SPARK_CENTRIFUGAL);
        PlayImpactSound(EFFECT_PRESET_METAL_SHARD);
    }
}

void UpdateTcdkToaiGiapTramSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_TGT_INST; i++) {
        ToaiGiapInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (!inst->cleaved && inst->elapsed >= inst->castSecs) {
            TriggerToaiGiapCleave(inst);
        }
        if (inst->elapsed >= inst->castSecs + 0.30f) {
            inst->active = false;
        }
    }
}

void DrawTcdkToaiGiapTramSkill(void) { }

void UnloadTcdkToaiGiapTramSkill(void)
{
    for (int i = 0; i < MAX_TGT_INST; i++) s_inst[i].active = false;
}
