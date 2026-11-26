#include "skills/fire/gltbc_phi_long_tai_thien_skill/gltbc_phi_long_tai_thien_skill.h"
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

#define MAX_PLTT_INST    3
#define PLTT_SPINE_BONES 24
#define PLTT_BASE_SCALE  0.42f
#define PLTT_DIVE_SECS   0.52f
#define PLTT_DISSOLVE    0.38f
#define PLTT_HIT_RADIUS  2.65f

typedef struct {
    bool                active;
    bool                impacted;
    int                 ownerAgentId;
    int                 modelHandle;
    int                 ribbonHandle;
    Matrix              ribbonXform;
    VFX_DragonCurveSpec curve;
    Vector3             impactPoint;
    float               headArc;
    float               elapsed;
    float               damage;
    float               sizeScale;
} PhiLongInst;

static PhiLongInst s_inst[MAX_PLTT_INST];
static int         s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(GltbcPhiLongTaiThien)

static void ReleasePhiLong(PhiLongInst *inst)
{
    if (!inst) return;
    if (inst->modelHandle >= 0) { VFXModel_Kill(inst->modelHandle); inst->modelHandle = -1; }
    if (inst->ribbonHandle >= 0) { VFX_KillTrail(inst->ribbonHandle); inst->ribbonHandle = -1; }
    inst->active = false;
}

static void AbortPhiLong(int agentId)
{
    for (int i = 0; i < MAX_PLTT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) ReleasePhiLong(&s_inst[i]);
    }
}

static bool HasActivePhiLong(int agentId)
{
    for (int i = 0; i < MAX_PLTT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitGltbcPhiLongTaiThienSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_PLTT_INST; i++) {
        s_inst[i].active       = false;
        s_inst[i].modelHandle  = -1;
        s_inst[i].ribbonHandle = -1;
    }
    (void)VFX_DragonQi_GetAsset();

    s_skillIndex = Skill_GetIndexByName("GLTBC_PHI_LONG_TAI_THIEN");
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_PHI_LONG_TAI_THIEN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortPhiLong);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActivePhiLong);
    }
}

void CastGltbcPhiLongTaiThienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_PLTT_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) { slot = 0; ReleasePhiLong(&s_inst[0]); }

    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_PHI_LONG_TAI_THIEN");
    float baseDmg = def ? def->baseDamage : 24.0f;
    float lvlMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    float clampedDist = Clamp(d, 2.5f, 8.5f);
    Vector3 impactPt  = Vector3Add(anchor, Vector3Scale(dir, clampedDist));
    impactPt.y        = MapManager_GetGroundHeightAt(impactPt.x, impactPt.z);

    /* Aerial leap + downward palm slam */
    Skill_RequestAnim(agentId, "mutant_jump_attack", 0.72f);
    Entity_ApplyLaunch(agentId, 4.4f, Vector3Scale(dir, 4.5f));

    PhiLongInst *inst = &s_inst[slot];
    *inst = (PhiLongInst){ 0 };
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->modelHandle  = -1;
    inst->ribbonHandle = -1;
    inst->impactPoint  = impactPt;
    inst->damage       = baseDmg * lvlMult;
    inst->sizeScale    = scale;
    inst->headArc      = 0.2f;

    inst->curve.kind     = VFX_DRAGON_CURVE_DIVE_SLAM;
    inst->curve.origin   = (Vector3){ anchor.x, anchor.y + 1.4f, anchor.z };
    inst->curve.dir      = dir;
    inst->curve.right    = (Vector3){ -dir.z, 0.0f, dir.x };
    inst->curve.groundY  = impactPt.y;
    inst->curve.scale    = scale;
    inst->curve.range    = clampedDist / 0.65f;
    inst->curve.latAmp   = 0.85f * scale;
    inst->curve.verAmp   = 0.25f * scale;
    inst->curve.waveSign = (GetRandomValue(0, 1) == 0) ? 1.0f : -1.0f;

    Color flameBody = (Color){ 228, 192, 86, 255 };
    Color flameGlow = (Color){ 255, 242, 166, 255 };

    int asset = VFX_DragonQi_GetAsset();
    if (asset >= 0) {
        VFXModelSpawnParams mp = { 0 };
        mp.position      = inst->curve.origin;
        mp.scale         = PLTT_BASE_SCALE * scale;
        mp.loop          = true;
        mp.lifetime      = PLTT_DIVE_SECS + PLTT_DISSOLVE + 0.15f;
        mp.fadeInTime    = 0.10f;
        mp.fadeOutTime   = PLTT_DISSOLVE;
        mp.style         = VFX_MODEL_STYLE_QI_SPIRIT;
        mp.tint          = flameBody;
        mp.glowColor     = flameGlow;
        mp.glowIntensity = 3.10f;
        mp.opacity       = 0.80f;
        inst->modelHandle = VFXModel_Spawn(asset, &mp);
        if (inst->modelHandle >= 0) {
            VFXModel_SetSpinePathOverride(inst->modelHandle, PLTT_SPINE_BONES,
                                          inst->headArc, PLTT_BASE_SCALE * scale,
                                          1.75f, 0.55f, VFX_DragonCurve_Evaluate, &inst->curve);
        }
    }

    Vector3 startHead = VFX_DragonCurve_Evaluate(0.2f, &inst->curve);
    inst->ribbonXform  = MatrixTranslate(startHead.x, startHead.y, startHead.z);
    inst->ribbonHandle = VFX_ComposeTrail(&inst->ribbonXform, VC_MAT_HOLY, 0.18f * scale, 0.45f, TRAIL_PRESET_DRAGON);
    /* Telegraph ground ring at landing target */
    VFX_ComposePalmPrint((Vector3){ impactPt.x, impactPt.y + 0.05f, impactPt.z },
                         (Vector3){ 0.0f, 1.0f, 0.0f }, 1.6f * scale, PLTT_DIVE_SECS);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateGltbcPhiLongTaiThienSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_PLTT_INST; i++) {
        PhiLongInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        float dtRatio = Clamp(inst->elapsed / PLTT_DIVE_SECS, 0.0f, 1.0f);
        float revStart = 0.0f;

        if (!inst->impacted) {
            /* Accelerating downward corkscrew plunge */
            inst->headArc = 0.2f + inst->curve.range * (0.45f * dtRatio + 0.55f * dtRatio * dtRatio);
        } else {
            float pt = Clamp((inst->elapsed - PLTT_DIVE_SECS) / PLTT_DISSOLVE, 0.0f, 1.0f);
            inst->headArc = inst->curve.range + 1.2f * pt;
            revStart = pt;
        }

        if (inst->modelHandle >= 0) {
            VFXModel_SetSpineReveal(inst->modelHandle, revStart, 1.0f);
            VFXModel_SetSpinePathOverride(inst->modelHandle, PLTT_SPINE_BONES,
                                          inst->headArc, PLTT_BASE_SCALE * inst->sizeScale,
                                          1.75f, inst->impacted ? 0.72f : 0.55f,
                                          VFX_DragonCurve_Evaluate, &inst->curve);
        }

        Vector3 headPos = VFX_DragonCurve_Evaluate(inst->headArc, &inst->curve);
        if (inst->modelHandle >= 0) VFXModel_GetBoneWorldPos(inst->modelHandle, "head", &headPos, NULL);
        inst->ribbonXform = MatrixTranslate(headPos.x, headPos.y, headPos.z);

        if (!inst->impacted && inst->elapsed >= PLTT_DIVE_SECS) {
            inst->impacted = true;
            if (inst->ribbonHandle >= 0) { VFX_KillTrail(inst->ribbonHandle); inst->ribbonHandle = -1; }

            Vector3 slamPos = inst->impactPoint;
            slamPos.y += 0.25f;
            VFX_ComposeDragonSlam(slamPos, inst->sizeScale * 1.20f, true);

            int targets[16];
            int cnt = Entity_GetNearbyTargets(slamPos, PLTT_HIT_RADIUS * inst->sizeScale, targets, 16);
            bool hitAny = false;
            for (int k = 0; k < cnt; k++) {
                int tid = targets[k];
                if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
                Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
                Entity_ApplyStatus(tid, STATUS_BURN, 18.0f / 3.0f, 3.0f, inst->ownerAgentId);
                /* 0.70s airtime launch */
                Entity_ApplyLaunch(tid, 3.45f, (Vector3){ 0.0f, 0.0f, 0.0f });
                VoHoc_ApplyDragonMark(inst->ownerAgentId, tid, 2);
                hitAny = true;
            }
            if (hitAny) {
                TimeFX_Hitstop(0.055f, 0.07f);
                PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
            }
        }

        if (inst->elapsed >= PLTT_DIVE_SECS + PLTT_DISSOLVE) ReleasePhiLong(inst);
    }
}

void DrawGltbcPhiLongTaiThienSkill(void) { }

void UnloadGltbcPhiLongTaiThienSkill(void)
{
    for (int i = 0; i < MAX_PLTT_INST; i++) ReleasePhiLong(&s_inst[i]);
}
