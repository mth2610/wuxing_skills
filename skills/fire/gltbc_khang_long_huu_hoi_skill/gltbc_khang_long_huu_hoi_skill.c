#include "skills/fire/gltbc_khang_long_huu_hoi_skill/gltbc_khang_long_huu_hoi_skill.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/vfx_model/vfx_model.h"
#include "core/vfx_light.h"
#include "core/time_fx.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_KLHH_INST      4
#define KLHH_SPINE_BONES   24
#define KLHH_BASE_SCALE    0.40f
#define KLHH_WINDUP_SECS   0.10f
#define KLHH_SPEED_MPS     19.5f
#define KLHH_MIN_RANGE_M   3.5f
#define KLHH_MAX_RANGE_M   13.0f
#define KLHH_DISSOLVE_SECS 0.36f
#define KLHH_HIT_RADIUS    1.55f
#define KLHH_SPARK_PERIOD  0.055f

typedef struct {
    bool                active;
    bool                launched;
    bool                impacted;
    int                 ownerAgentId;
    int                 modelHandle;
    int                 ribbonHandle;
    Matrix              ribbonXform;
    VFX_DragonCurveSpec curve;
    float               targetRange;
    float               flightSecs;
    float               headArc;
    float               elapsed;
    float               impactTime;
    float               sparkTimer;
    float               damage;
    float               sizeScale;
} KhangLongInst;

static KhangLongInst s_inst[MAX_KLHH_INST];
static int           s_skillIndex = -1;
static int           s_tiemLongIndex = -1;

static void ReleaseDragonInstance(KhangLongInst *inst)
{
    if (!inst) return;
    if (inst->modelHandle >= 0) {
        VFXModel_Kill(inst->modelHandle);
        inst->modelHandle = -1;
    }
    if (inst->ribbonHandle >= 0) {
        VFX_KillTrail(inst->ribbonHandle);
        inst->ribbonHandle = -1;
    }
    inst->active = false;
}

static void AbortGltbcKhangLong(int agentId)
{
    for (int i = 0; i < MAX_KLHH_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            ReleaseDragonInstance(&s_inst[i]);
        }
    }
}

static bool HasActiveGltbcKhangLong(int agentId)
{
    for (int i = 0; i < MAX_KLHH_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

bool IsGltbcKhangLongHuuHoiSkillCoiling(void)
{
    for (int i = 0; i < MAX_KLHH_INST; i++) {
        if (s_inst[i].active && !s_inst[i].launched) return true;
    }
    return false;
}

int GetGltbcKhangLongHuuHoiSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    if (!outProjectiles || maxProjectiles <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_KLHH_INST && count < maxProjectiles; i++) {
        if (!s_inst[i].active || s_inst[i].impacted) continue;
        Vector3 hitPos = VFX_DragonCurve_Evaluate(s_inst[i].headArc, &s_inst[i].curve);
        if (s_inst[i].modelHandle >= 0) {
            VFXModel_GetBoneWorldPos(s_inst[i].modelHandle, "head", &hitPos, NULL);
        }
        outProjectiles[count].position = hitPos;
        outProjectiles[count].radius   = KLHH_HIT_RADIUS * s_inst[i].sizeScale;
        outProjectiles[count].active   = true;
        count++;
    }
    return count;
}

void DeactivateGltbcKhangLongHuuHoiProjectile(int index)
{
    if (index < 0 || index >= MAX_KLHH_INST) return;
    if (s_inst[index].active && !s_inst[index].impacted) {
        s_inst[index].impacted        = true;
        s_inst[index].curve.impacted  = true;
        s_inst[index].curve.impactArc = s_inst[index].headArc;
        s_inst[index].impactTime      = s_inst[index].elapsed;
    }
}

void InitGltbcKhangLongHuuHoiSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_KLHH_INST; i++) {
        s_inst[i].active       = false;
        s_inst[i].modelHandle  = -1;
        s_inst[i].ribbonHandle = -1;
    }

    (void)VFX_DragonQi_GetAsset();
    s_skillIndex    = Skill_GetIndexByName("GLTBC_KHANG_LONG_HUU_HOI");
    s_tiemLongIndex = Skill_GetIndexByName("GLTBC_TIEM_LONG_VAT_DUNG");
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_KHANG_LONG_HUU_HOI");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortGltbcKhangLong);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveGltbcKhangLong);
    }
}

static void TriggerDragonImpact(KhangLongInst *inst, Vector3 impactPos)
{
    if (inst->impacted) return;
    inst->impacted        = true;
    inst->curve.impacted  = true;
    inst->curve.impactArc = inst->headArc;
    inst->impactTime      = inst->elapsed;

    if (inst->ribbonHandle >= 0) {
        VFX_KillTrail(inst->ribbonHandle);
        inst->ribbonHandle = -1;
    }

    VFX_ComposeDragonSlam(impactPos, inst->sizeScale * 1.05f, true);

    int targets[12];
    int cnt = Entity_GetNearbyTargets(impactPos, KLHH_HIT_RADIUS * inst->sizeScale, targets, 12);
    bool hitAny = false;
    for (int k = 0; k < cnt; k++) {
        int tid = targets[k];
        if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
        float mult = 1.0f;
        VoHoc_OnKhangLongConsume(inst->ownerAgentId, tid, s_skillIndex, s_tiemLongIndex, &mult);
        Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage * mult, 0.0f,
                               Vector3Scale(inst->curve.dir, 2.5f));
        Entity_ApplyStatus(tid, STATUS_BURN, 11.0f / 3.0f, 3.0f, inst->ownerAgentId);
        Entity_ApplyStun(tid, 0.55f);
        hitAny = true;
    }

    if (hitAny) {
        TimeFX_Hitstop(0.05f, 0.08f);
        PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
    }
}

void CastGltbcKhangLongHuuHoiSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_KLHH_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        ReleaseDragonInstance(&s_inst[0]);
    }

    int asset = VFX_DragonQi_GetAsset();
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_KHANG_LONG_HUU_HOI");
    float baseDmg = def ? def->baseDamage : 33.0f;
    float lvlMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    scale *= 0.94f + 0.12f * ((float)GetRandomValue(0, 100) / 100.0f);

    Skill_RequestAnim(agentId, "crane_wing_strike", 0.52f);

    Vector3 palmOrigin = Vector3Add(anchor, Vector3Scale(dir, 0.85f));
    float groundY      = MapManager_GetGroundHeightAt(palmOrigin.x, palmOrigin.z);
    palmOrigin.y       = groundY + 1.22f;

    float targetRange  = Clamp(d - 0.85f, KLHH_MIN_RANGE_M, KLHH_MAX_RANGE_M);
    float flightSecs   = targetRange / KLHH_SPEED_MPS;

    Color flameBody = (Color){ 228, 192, 86, 255 };
    Color flameGlow = (Color){ 255, 242, 166, 255 };

    KhangLongInst *inst = &s_inst[slot];
    *inst = (KhangLongInst){ 0 };
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->modelHandle  = -1;
    inst->ribbonHandle = -1;
    inst->targetRange  = targetRange;
    inst->flightSecs   = flightSecs;
    inst->headArc      = 0.35f;
    inst->damage       = baseDmg * lvlMult;
    inst->sizeScale    = scale;

    inst->curve.kind     = VFX_DRAGON_CURVE_S_SURGE;
    inst->curve.origin   = palmOrigin;
    inst->curve.dir      = dir;
    inst->curve.right    = (Vector3){ -dir.z, 0.0f, dir.x };
    inst->curve.groundY  = groundY;
    inst->curve.scale    = scale;
    inst->curve.range    = targetRange;
    inst->curve.latAmp   = 0.86f * scale;
    inst->curve.verAmp   = 0.32f * scale;
    inst->curve.waveSign = (GetRandomValue(0, 1) == 0) ? 1.0f : -1.0f;

    if (asset >= 0) {
        VFXModelSpawnParams mp = { 0 };
        mp.position      = palmOrigin;
        mp.scale         = KLHH_BASE_SCALE * scale;
        mp.loop          = true;
        mp.lifetime      = KLHH_WINDUP_SECS + flightSecs + KLHH_DISSOLVE_SECS + 0.15f;
        mp.fadeInTime    = 0.08f;
        mp.fadeOutTime   = KLHH_DISSOLVE_SECS;
        mp.style         = VFX_MODEL_STYLE_QI_SPIRIT;
        mp.tint          = flameBody;
        mp.glowColor     = flameGlow;
        mp.glowIntensity = 3.10f;
        mp.opacity       = 0.80f;
        inst->modelHandle = VFXModel_Spawn(asset, &mp);
        if (inst->modelHandle >= 0) {
            VFXModel_SetSpineReveal(inst->modelHandle, 0.0f, 0.25f);
            VFXModel_SetSpinePathOverride(inst->modelHandle, KLHH_SPINE_BONES,
                                          inst->headArc, KLHH_BASE_SCALE * scale,
                                          1.65f, 0.35f, VFX_DragonCurve_Evaluate, &inst->curve);
        }
    }

    inst->ribbonXform  = MatrixTranslate(palmOrigin.x, palmOrigin.y, palmOrigin.z);
    inst->ribbonHandle = VFX_ComposeTrail(&inst->ribbonXform, VC_MAT_HOLY, 0.16f * scale, 0.42f, TRAIL_PRESET_DRAGON);

    VFX_ComposePalmPrint(palmOrigin, dir, 1.15f * scale, 0.24f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateGltbcKhangLongHuuHoiSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_KLHH_INST; i++) {
        KhangLongInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        float jawOpen = 0.38f;
        float revStart = 0.0f;
        float revEnd   = 1.0f;

        if (!inst->launched) {
            float w = Clamp(inst->elapsed / KLHH_WINDUP_SECS, 0.0f, 1.0f);
            inst->headArc = 0.35f + 0.90f * (w * w);
            revEnd = 0.25f + 0.75f * w;
            jawOpen = 0.32f + 0.20f * w;
            if (inst->elapsed >= KLHH_WINDUP_SECS) inst->launched = true;
        } else if (!inst->impacted) {
            float ft = Clamp((inst->elapsed - KLHH_WINDUP_SECS) / fmaxf(inst->flightSecs, 0.05f), 0.0f, 1.0f);
            float accel = 0.72f * ft + 0.28f * (ft * ft);
            inst->headArc = 1.25f + (inst->targetRange - 1.25f) * accel;
            jawOpen = 0.48f;
        } else {
            float pt = Clamp((inst->elapsed - inst->impactTime) / KLHH_DISSOLVE_SECS, 0.0f, 1.0f);
            float inv = 1.0f - pt;
            inst->headArc = inst->curve.impactArc + 2.2f * (1.0f - inv * inv);
            revStart = pt; /* Burns away smoothly head -> tail */
            jawOpen = 0.72f;
        }

        if (inst->modelHandle >= 0) {
            VFXModel_SetSpineReveal(inst->modelHandle, revStart, revEnd);
            VFXModel_SetSpinePathOverride(inst->modelHandle, KLHH_SPINE_BONES,
                                          inst->headArc, KLHH_BASE_SCALE * inst->sizeScale,
                                          1.65f, jawOpen, VFX_DragonCurve_Evaluate, &inst->curve);
        }

        Vector3 headPos = VFX_DragonCurve_Evaluate(inst->headArc, &inst->curve);
        Vector3 tailPos = VFX_DragonCurve_Evaluate(inst->headArc - 3.2f * inst->sizeScale, &inst->curve);
        if (inst->modelHandle >= 0) {
            VFXModel_GetBoneWorldPos(inst->modelHandle, "head", &headPos, NULL);
            VFXModel_GetBoneWorldPos(inst->modelHandle, "tail_06", &tailPos, NULL);
        }
        inst->ribbonXform = MatrixTranslate(headPos.x, headPos.y, headPos.z);

        if (inst->launched && !inst->impacted) {
            inst->sparkTimer += dt;
            if (inst->sparkTimer >= KLHH_SPARK_PERIOD) {
                inst->sparkTimer = 0.0f;
                VFX_ComposeEmberBurst(tailPos, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_HOLY,
                                      0.38f * inst->sizeScale, 0.55f);
                Color auraLight = (Color){ 255, 240, 162, 255 };
                VFXLight_Spawn(headPos, auraLight, 2.4f * inst->sizeScale, 0.12f, VFX_PRIORITY_LOW);
            }

            int nearIds[8];
            int nearCnt = Entity_GetNearbyTargets(headPos, KLHH_HIT_RADIUS * inst->sizeScale, nearIds, 8);
            bool hitTarget = false;
            for (int k = 0; k < nearCnt; k++) {
                if (nearIds[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, nearIds[k])) {
                    hitTarget = true;
                    break;
                }
            }
            if (hitTarget || (inst->elapsed - KLHH_WINDUP_SECS) >= inst->flightSecs) {
                TriggerDragonImpact(inst, headPos);
            }
        }

        if (inst->impacted && (inst->elapsed - inst->impactTime) >= KLHH_DISSOLVE_SECS) {
            ReleaseDragonInstance(inst);
        }
    }
}

void DrawGltbcKhangLongHuuHoiSkill(void) { }

void UnloadGltbcKhangLongHuuHoiSkill(void)
{
    for (int i = 0; i < MAX_KLHH_INST; i++) ReleaseDragonInstance(&s_inst[i]);
}
