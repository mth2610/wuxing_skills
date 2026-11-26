#include "skills/fire/gltbc_tiem_long_vat_dung_skill/gltbc_tiem_long_vat_dung_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/vfx_model/vfx_model.h"
#include "core/time_fx.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_TLVD_INST    4
#define TLVD_SPINE_BONES 24
#define TLVD_BASE_SCALE  0.34f
#define TLVD_DURATION    0.68f
#define TLVD_HIT_RADIUS  1.50f

typedef struct {
    bool                active;
    bool                struck;
    int                 ownerAgentId;
    int                 modelHandle;
    int                 ribbonHandle;
    Matrix              ribbonXform;
    VFX_DragonCurveSpec curve;
    float               headArc;
    float               elapsed;
    float               damage;
    float               sizeScale;
} TiemLongInst;

static TiemLongInst s_inst[MAX_TLVD_INST];
static int          s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(GltbcTiemLongVatDung)

static void ReleaseTiemLong(TiemLongInst *inst)
{
    if (!inst) return;
    if (inst->modelHandle >= 0) { VFXModel_Kill(inst->modelHandle); inst->modelHandle = -1; }
    if (inst->ribbonHandle >= 0) { VFX_KillTrail(inst->ribbonHandle); inst->ribbonHandle = -1; }
    inst->active = false;
}

static void AbortTiemLong(int agentId)
{
    for (int i = 0; i < MAX_TLVD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) ReleaseTiemLong(&s_inst[i]);
    }
}

static bool HasActiveTiemLong(int agentId)
{
    for (int i = 0; i < MAX_TLVD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitGltbcTiemLongVatDungSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_TLVD_INST; i++) {
        s_inst[i].active       = false;
        s_inst[i].modelHandle  = -1;
        s_inst[i].ribbonHandle = -1;
    }
    (void)VFX_DragonQi_GetAsset();

    s_skillIndex = Skill_GetIndexByName("GLTBC_TIEM_LONG_VAT_DUNG");
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_TIEM_LONG_VAT_DUNG");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTiemLong);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTiemLong);
    }
}

void CastGltbcTiemLongVatDungSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_TLVD_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) { slot = 0; ReleaseTiemLong(&s_inst[0]); }

    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_TIEM_LONG_VAT_DUNG");
    float baseDmg = def ? def->baseDamage : 14.0f;
    float lvlMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Fast gap-close dash + rising upper-palm strike */
    Skill_RequestAnim(agentId, "elbow_punch", 0.42f);
    Entity_Dash(agentId, dir, 14.0f);

    Vector3 eruptOrigin = Vector3Add(anchor, Vector3Scale(dir, 1.5f));
    float gY = MapManager_GetGroundHeightAt(eruptOrigin.x, eruptOrigin.z);
    eruptOrigin.y = gY;

    TiemLongInst *inst = &s_inst[slot];
    *inst = (TiemLongInst){ 0 };
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->modelHandle  = -1;
    inst->ribbonHandle = -1;
    inst->damage       = baseDmg * lvlMult;
    inst->sizeScale    = scale;
    inst->headArc      = 0.4f;

    inst->curve.kind     = VFX_DRAGON_CURVE_RISING_ERUPT;
    inst->curve.origin   = eruptOrigin;
    inst->curve.dir      = dir;
    inst->curve.right    = (Vector3){ -dir.z, 0.0f, dir.x };
    inst->curve.groundY  = gY;
    inst->curve.scale    = scale;
    inst->curve.latAmp   = 0.52f * scale;
    inst->curve.verAmp   = 0.20f * scale;
    inst->curve.waveSign = (GetRandomValue(0, 1) == 0) ? 1.0f : -1.0f;

    Color flameBody = ColorLerp(ELEMENT_COLOR_FIRE, ORANGE, 0.45f);
    Color flameGlow = ColorLerp(ORANGE, GOLD, 0.48f);

    int asset = VFX_DragonQi_GetAsset();
    if (asset >= 0) {
        VFXModelSpawnParams mp = { 0 };
        mp.position      = eruptOrigin;
        mp.scale         = TLVD_BASE_SCALE * scale;
        mp.loop          = true;
        mp.lifetime      = TLVD_DURATION + 0.1f;
        mp.fadeInTime    = 0.08f;
        mp.fadeOutTime   = 0.25f;
        mp.style         = VFX_MODEL_STYLE_QI_SPIRIT;
        mp.tint          = flameBody;
        mp.glowColor     = flameGlow;
        mp.glowIntensity = 2.0f;
        mp.opacity       = 0.80f;
        inst->modelHandle = VFXModel_Spawn(asset, &mp);
        if (inst->modelHandle >= 0) {
            VFXModel_SetSpineReveal(inst->modelHandle, 0.0f, 0.30f);
            VFXModel_SetSpinePathOverride(inst->modelHandle, TLVD_SPINE_BONES,
                                          inst->headArc, TLVD_BASE_SCALE * scale,
                                          1.4f, 0.35f, VFX_DragonCurve_Evaluate, &inst->curve);
        }
    }

    inst->ribbonXform  = MatrixTranslate(eruptOrigin.x, gY + 0.5f, eruptOrigin.z);
    inst->ribbonHandle = VFX_ComposeTrail(&inst->ribbonXform, VC_MAT_FIRE, 0.12f * scale, 0.32f, TRAIL_PRESET_MAIN);
    VFX_ComposePalmPrint((Vector3){ eruptOrigin.x, gY + 0.05f, eruptOrigin.z },
                         (Vector3){ 0.0f, 1.0f, 0.0f }, 1.25f * scale, 0.28f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateGltbcTiemLongVatDungSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_TLVD_INST; i++) {
        TiemLongInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        float t = Clamp(inst->elapsed / TLVD_DURATION, 0.0f, 1.0f);
        inst->headArc = 0.4f + 5.8f * t;

        float revStart = (t > 0.55f) ? Clamp((t - 0.55f) / 0.45f, 0.0f, 1.0f) : 0.0f;
        float revEnd   = Clamp(0.30f + t * 1.8f, 0.0f, 1.0f);

        if (inst->modelHandle >= 0) {
            VFXModel_SetSpineReveal(inst->modelHandle, revStart, revEnd);
            VFXModel_SetSpinePathOverride(inst->modelHandle, TLVD_SPINE_BONES,
                                          inst->headArc, TLVD_BASE_SCALE * inst->sizeScale,
                                          1.4f, 0.42f, VFX_DragonCurve_Evaluate, &inst->curve);
        }

        Vector3 headPos = VFX_DragonCurve_Evaluate(inst->headArc, &inst->curve);
        if (inst->modelHandle >= 0) VFXModel_GetBoneWorldPos(inst->modelHandle, "head", &headPos, NULL);
        inst->ribbonXform = MatrixTranslate(headPos.x, headPos.y, headPos.z);

        if (!inst->struck && inst->elapsed >= 0.16f) {
            inst->struck = true;
            Vector3 hitCenter = Vector3Add(inst->curve.origin, Vector3Scale(inst->curve.dir, 1.2f));
            hitCenter.y = inst->curve.groundY + 1.0f;

            int targets[8];
            int cnt = Entity_GetNearbyTargets(hitCenter, TLVD_HIT_RADIUS * inst->sizeScale, targets, 8);
            bool hitAny = false;
            for (int k = 0; k < cnt; k++) {
                int tid = targets[k];
                if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
                Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f,
                                       Vector3Scale(inst->curve.dir, 1.2f));
                Entity_ApplyStatus(tid, STATUS_BURN, 7.0f / 3.0f, 3.0f, inst->ownerAgentId);
                VoHoc_ApplyDragonMark(inst->ownerAgentId, tid, 1);
                hitAny = true;
            }
            if (hitAny) {
                /* Next Station Jianghu 2 signature: Tiềm Long hit reduces cooldowns */
                SkillManager_ReduceAgentCooldowns(inst->ownerAgentId, 0.55f);
                VFX_ComposeEmberBurst(hitCenter, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE,
                                      0.85f * inst->sizeScale, 0.85f);
                TimeFX_Hitstop(0.035f, 0.10f);
                PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
            }
        }

        if (inst->elapsed >= TLVD_DURATION) ReleaseTiemLong(inst);
    }
}

void DrawGltbcTiemLongVatDungSkill(void) { }

void UnloadGltbcTiemLongVatDungSkill(void)
{
    for (int i = 0; i < MAX_TLVD_INST; i++) ReleaseTiemLong(&s_inst[i]);
}
