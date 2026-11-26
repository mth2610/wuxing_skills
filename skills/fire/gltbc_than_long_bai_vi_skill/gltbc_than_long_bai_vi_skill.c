#include "skills/fire/gltbc_than_long_bai_vi_skill/gltbc_than_long_bai_vi_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/vfx_model/vfx_model.h"
#include "core/wind/wind_system.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TLBV_INST    4
#define TLBV_SPINE_BONES 24
#define TLBV_BASE_SCALE  0.34f
#define TLBV_DURATION    0.85f

typedef struct {
    bool                active;
    int                 ownerAgentId;
    int                 modelHandle;
    int                 ribbonHandle;
    Matrix              ribbonXform;
    VFX_DragonCurveSpec curve;
    float               elapsed;
    float               sizeScale;
} ThanLongInst;

static ThanLongInst s_inst[MAX_TLBV_INST];
static int          s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(GltbcThanLongBaiVi)

static void ReleaseThanLong(ThanLongInst *inst)
{
    if (!inst) return;
    if (inst->modelHandle >= 0) { VFXModel_Kill(inst->modelHandle); inst->modelHandle = -1; }
    if (inst->ribbonHandle >= 0) { VFX_KillTrail(inst->ribbonHandle); inst->ribbonHandle = -1; }
    inst->active = false;
}

static void AbortThanLong(int agentId)
{
    for (int i = 0; i < MAX_TLBV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) ReleaseThanLong(&s_inst[i]);
    }
}

static bool HasActiveThanLong(int agentId)
{
    for (int i = 0; i < MAX_TLBV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitGltbcThanLongBaiViSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_TLBV_INST; i++) {
        s_inst[i].active       = false;
        s_inst[i].modelHandle  = -1;
        s_inst[i].ribbonHandle = -1;
    }
    (void)VFX_DragonQi_GetAsset();

    s_skillIndex = Skill_GetIndexByName("GLTBC_THAN_LONG_BAI_VI");
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_THAN_LONG_BAI_VI");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortThanLong);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveThanLong);
    }
}

void CastGltbcThanLongBaiViSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_TLBV_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) { slot = 0; ReleaseThanLong(&s_inst[0]); }

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 center = owner ? owner->position : startPos;
    float gY = MapManager_GetGroundHeightAt(center.x, center.z);
    center.y = gY + 1.05f;

    /* Cleanse CC + Shield + Block + Speed + Mana (Support Slot 6) */
    Skill_RequestAnim(agentId, "hurricane_kick", 0.55f);
    Entity_Cleanse(agentId);
    Entity_AddShield(agentId, 20.0f, 4.0f);
    Entity_AddModifier(agentId, 1.35f, 3.5f);
    Entity_RestoreMana(agentId, 20.0f);

    ThanLongInst *inst = &s_inst[slot];
    *inst = (ThanLongInst){ 0 };
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->modelHandle  = -1;
    inst->ribbonHandle = -1;
    inst->sizeScale    = scale;

    inst->curve.kind        = VFX_DRAGON_CURVE_ORBIT;
    inst->curve.origin      = center;
    inst->curve.dir         = (Vector3){ 1.0f, 0.0f, 0.0f };
    inst->curve.right       = (Vector3){ 0.0f, 0.0f, 1.0f };
    inst->curve.scale       = scale;
    inst->curve.radius      = 1.85f * scale;
    inst->curve.verAmp      = 0.22f * scale;
    inst->curve.waveSign    = 1.0f;
    inst->curve.phaseOffset = owner ? owner->facingYaw : 0.0f;

    Color flameBody = ColorLerp(ELEMENT_COLOR_FIRE, ORANGE, 0.45f);
    Color flameGlow = ColorLerp(ORANGE, GOLD, 0.55f);

    int asset = VFX_DragonQi_GetAsset();
    if (asset >= 0) {
        VFXModelSpawnParams mp = { 0 };
        mp.position      = center;
        mp.scale         = TLBV_BASE_SCALE * scale;
        mp.loop          = true;
        mp.lifetime      = TLBV_DURATION + 0.1f;
        mp.fadeInTime    = 0.08f;
        mp.fadeOutTime   = 0.28f;
        mp.style         = VFX_MODEL_STYLE_QI_SPIRIT;
        mp.tint          = flameBody;
        mp.glowColor     = flameGlow;
        mp.glowIntensity = 2.15f;
        mp.opacity       = 0.82f;
        inst->modelHandle = VFXModel_Spawn(asset, &mp);
        if (inst->modelHandle >= 0) {
            VFXModel_SetSpinePathOverride(inst->modelHandle, TLBV_SPINE_BONES,
                                          0.0f, TLBV_BASE_SCALE * scale,
                                          1.6f, 0.40f, VFX_DragonCurve_Evaluate, &inst->curve);
        }
    }

    Vector3 hp = VFX_DragonCurve_Evaluate(0.0f, &inst->curve);
    inst->ribbonXform  = MatrixTranslate(hp.x, hp.y, hp.z);
    inst->ribbonHandle = VFX_ComposeTrail(&inst->ribbonXform, VC_MAT_FIRE, 0.15f * scale, 0.38f, TRAIL_PRESET_MAIN);
    VFX_ComposePalmPrint((Vector3){ center.x, gY + 0.05f, center.z },
                         (Vector3){ 0.0f, 1.0f, 0.0f }, 2.1f * scale, 0.35f);
    Wind_SpawnRadialBlast(center, 2.5f * scale, 6.5f, 0.20f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateGltbcThanLongBaiViSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_TLBV_INST; i++) {
        ThanLongInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (owner) {
            float gY = MapManager_GetGroundHeightAt(owner->position.x, owner->position.z);
            inst->curve.origin = (Vector3){ owner->position.x, gY + 1.05f, owner->position.z };
        }

        float t = Clamp(inst->elapsed / TLBV_DURATION, 0.0f, 1.0f);
        float arc = t * 14.5f; /* Fast 360+ degree protective dragon tail whip */
        float revStart = (t > 0.62f) ? Clamp((t - 0.62f) / 0.38f, 0.0f, 1.0f) : 0.0f;

        if (inst->modelHandle >= 0) {
            VFXModel_SetSpineReveal(inst->modelHandle, revStart, 1.0f);
            VFXModel_SetSpinePathOverride(inst->modelHandle, TLBV_SPINE_BONES,
                                          arc, TLBV_BASE_SCALE * inst->sizeScale,
                                          1.6f, 0.45f, VFX_DragonCurve_Evaluate, &inst->curve);
        }

        Vector3 hp = VFX_DragonCurve_Evaluate(arc, &inst->curve);
        if (inst->modelHandle >= 0) VFXModel_GetBoneWorldPos(inst->modelHandle, "head", &hp, NULL);
        inst->ribbonXform = MatrixTranslate(hp.x, hp.y, hp.z);

        if (inst->elapsed >= TLBV_DURATION) ReleaseThanLong(inst);
    }
}

void DrawGltbcThanLongBaiViSkill(void) { }

void UnloadGltbcThanLongBaiViSkill(void)
{
    for (int i = 0; i < MAX_TLBV_INST; i++) ReleaseThanLong(&s_inst[i]);
}
