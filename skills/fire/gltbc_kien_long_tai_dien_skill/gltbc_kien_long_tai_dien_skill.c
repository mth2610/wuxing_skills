#include "skills/fire/gltbc_kien_long_tai_dien_skill/gltbc_kien_long_tai_dien_skill.h"
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

#define MAX_KLTD_INST    4
#define KLTD_SPINE_BONES 24
#define KLTD_BASE_SCALE  0.36f
#define KLTD_FLIGHT_SECS 0.42f
#define KLTD_DISSOLVE    0.30f
#define KLTD_HIT_RADIUS  1.85f

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
} KienLongInst;

static KienLongInst s_inst[MAX_KLTD_INST];
static int          s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(GltbcKienLongTaiDien)

static void ReleaseKienLong(KienLongInst *inst)
{
    if (!inst) return;
    if (inst->modelHandle >= 0) { VFXModel_Kill(inst->modelHandle); inst->modelHandle = -1; }
    if (inst->ribbonHandle >= 0) { VFX_KillTrail(inst->ribbonHandle); inst->ribbonHandle = -1; }
    inst->active = false;
}

static void AbortKienLong(int agentId)
{
    for (int i = 0; i < MAX_KLTD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) ReleaseKienLong(&s_inst[i]);
    }
}

static bool HasActiveKienLong(int agentId)
{
    for (int i = 0; i < MAX_KLTD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitGltbcKienLongTaiDienSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_KLTD_INST; i++) {
        s_inst[i].active       = false;
        s_inst[i].modelHandle  = -1;
        s_inst[i].ribbonHandle = -1;
    }
    (void)VFX_DragonQi_GetAsset();

    s_skillIndex = Skill_GetIndexByName("GLTBC_KIEN_LONG_TAI_DIEN");
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_KIEN_LONG_TAI_DIEN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortKienLong);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveKienLong);
    }
}

void CastGltbcKienLongTaiDienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_KLTD_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) { slot = 0; ReleaseKienLong(&s_inst[0]); }

    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_KIEN_LONG_TAI_DIEN");
    float baseDmg = def ? def->baseDamage : 21.0f;
    float lvlMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Heavy armor-breaking horizontal palm sweep */
    Skill_RequestAnim(agentId, "hook_punch", 0.48f);

    Vector3 origin = Vector3Add(anchor, Vector3Scale(dir, 0.9f));
    float gY = MapManager_GetGroundHeightAt(origin.x, origin.z);
    origin.y = gY + 1.05f;

    KienLongInst *inst = &s_inst[slot];
    *inst = (KienLongInst){ 0 };
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->modelHandle  = -1;
    inst->ribbonHandle = -1;
    inst->damage       = baseDmg * lvlMult;
    inst->sizeScale    = scale;
    inst->headArc      = 0.5f;

    inst->curve.kind     = VFX_DRAGON_CURVE_S_SURGE;
    inst->curve.origin   = origin;
    inst->curve.dir      = dir;
    inst->curve.right    = (Vector3){ -dir.z, 0.0f, dir.x };
    inst->curve.groundY  = gY;
    inst->curve.scale    = scale;
    inst->curve.range    = 6.8f;
    inst->curve.latAmp   = 1.15f * scale; /* Wide sweeping armor-break arc */
    inst->curve.verAmp   = 0.16f * scale;
    inst->curve.waveSign = (GetRandomValue(0, 1) == 0) ? 1.0f : -1.0f;

    Color flameBody = (Color){ 228, 192, 86, 255 };
    Color flameGlow = (Color){ 255, 242, 166, 255 };

    int asset = VFX_DragonQi_GetAsset();
    if (asset >= 0) {
        VFXModelSpawnParams mp = { 0 };
        mp.position      = origin;
        mp.scale         = KLTD_BASE_SCALE * scale;
        mp.loop          = true;
        mp.lifetime      = KLTD_FLIGHT_SECS + KLTD_DISSOLVE + 0.1f;
        mp.fadeInTime    = 0.08f;
        mp.fadeOutTime   = KLTD_DISSOLVE;
        mp.style         = VFX_MODEL_STYLE_QI_SPIRIT;
        mp.tint          = flameBody;
        mp.glowColor     = flameGlow;
        mp.glowIntensity = 3.10f;
        mp.opacity       = 0.80f;
        inst->modelHandle = VFXModel_Spawn(asset, &mp);
        if (inst->modelHandle >= 0) {
            VFXModel_SetSpinePathOverride(inst->modelHandle, KLTD_SPINE_BONES,
                                          inst->headArc, KLTD_BASE_SCALE * scale,
                                          1.55f, 0.42f, VFX_DragonCurve_Evaluate, &inst->curve);
        }
    }

    inst->ribbonXform  = MatrixTranslate(origin.x, origin.y, origin.z);
    inst->ribbonHandle = VFX_ComposeTrail(&inst->ribbonXform, VC_MAT_HOLY, 0.15f * scale, 0.35f, TRAIL_PRESET_DRAGON);
    VFX_ComposePalmPrint(origin, dir, 1.45f * scale, 0.30f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateGltbcKienLongTaiDienSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_KLTD_INST; i++) {
        KienLongInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        float ft = Clamp(inst->elapsed / KLTD_FLIGHT_SECS, 0.0f, 1.0f);
        inst->headArc = 0.5f + 6.5f * (0.70f * ft + 0.30f * ft * ft);

        float revStart = (inst->elapsed > KLTD_FLIGHT_SECS)
            ? Clamp((inst->elapsed - KLTD_FLIGHT_SECS) / KLTD_DISSOLVE, 0.0f, 1.0f)
            : 0.0f;

        if (inst->modelHandle >= 0) {
            VFXModel_SetSpineReveal(inst->modelHandle, revStart, 1.0f);
            VFXModel_SetSpinePathOverride(inst->modelHandle, KLTD_SPINE_BONES,
                                          inst->headArc, KLTD_BASE_SCALE * inst->sizeScale,
                                          1.55f, 0.45f, VFX_DragonCurve_Evaluate, &inst->curve);
        }

        Vector3 headPos = VFX_DragonCurve_Evaluate(inst->headArc, &inst->curve);
        if (inst->modelHandle >= 0) VFXModel_GetBoneWorldPos(inst->modelHandle, "head", &headPos, NULL);
        inst->ribbonXform = MatrixTranslate(headPos.x, headPos.y, headPos.z);

        if (!inst->struck && inst->elapsed >= 0.18f) {
            inst->struck = true;
            Vector3 stampPos = Vector3Add(inst->curve.origin, Vector3Scale(inst->curve.dir, 2.4f));
            stampPos.y = MapManager_GetGroundHeightAt(stampPos.x, stampPos.z) + 0.05f;
            VFX_ComposePalmPrint(stampPos, (Vector3){ 0.0f, 1.0f, 0.0f }, 1.85f * inst->sizeScale, 0.36f);

            int targets[12];
            int cnt = Entity_GetNearbyTargets(stampPos, KLTD_HIT_RADIUS * inst->sizeScale, targets, 12);
            bool hitAny = false;
            for (int k = 0; k < cnt; k++) {
                int tid = targets[k];
                if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
                Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f,
                                       Vector3Scale(inst->curve.dir, 1.5f));
                Entity_ApplyStatus(tid, STATUS_BURN, 10.0f / 3.0f, 3.0f, inst->ownerAgentId);
                Entity_ApplyStatus(tid, STATUS_VULNERABLE, 0.20f, 3.0f, inst->ownerAgentId);
                VoHoc_ApplyDragonMark(inst->ownerAgentId, tid, 1);
                hitAny = true;
            }
            if (hitAny) {
                TimeFX_Hitstop(0.045f, 0.08f);
                PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
            }
        }

        if (inst->elapsed >= KLTD_FLIGHT_SECS + KLTD_DISSOLVE) ReleaseKienLong(inst);
    }
}

void DrawGltbcKienLongTaiDienSkill(void) { }

void UnloadGltbcKienLongTaiDienSkill(void)
{
    for (int i = 0; i < MAX_KLTD_INST; i++) ReleaseKienLong(&s_inst[i]);
}
