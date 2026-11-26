#include "skills/fire/gltbc_long_chien_vu_da_skill/gltbc_long_chien_vu_da_skill.h"
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

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_LCVD_INST    3
#define LCVD_SPINE_BONES 24
#define LCVD_BASE_SCALE  0.34f
#define LCVD_SPEED_MPS   17.5f
#define LCVD_DISSOLVE    0.34f
#define LCVD_HIT_RADIUS  2.10f

typedef struct {
    bool                active;
    bool                impacted;
    int                 ownerAgentId;
    int                 modelHandles[2];
    int                 ribbonHandles[2];
    Matrix              ribbonXforms[2];
    VFX_DragonCurveSpec curves[2];
    float               targetRange;
    float               flightSecs;
    float               headArc;
    float               elapsed;
    float               impactTime;
    float               damage;
    float               sizeScale;
} LongChienInst;

static LongChienInst s_inst[MAX_LCVD_INST];
static int           s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(GltbcLongChienVuDa)

static void ReleaseLongChien(LongChienInst *inst)
{
    if (!inst) return;
    for (int j = 0; j < 2; j++) {
        if (inst->modelHandles[j] >= 0) { VFXModel_Kill(inst->modelHandles[j]); inst->modelHandles[j] = -1; }
        if (inst->ribbonHandles[j] >= 0) { VFX_KillTrail(inst->ribbonHandles[j]); inst->ribbonHandles[j] = -1; }
    }
    inst->active = false;
}

static void AbortLongChien(int agentId)
{
    for (int i = 0; i < MAX_LCVD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) ReleaseLongChien(&s_inst[i]);
    }
}

static bool HasActiveLongChien(int agentId)
{
    for (int i = 0; i < MAX_LCVD_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitGltbcLongChienVuDaSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_LCVD_INST; i++) {
        s_inst[i].active = false;
        s_inst[i].modelHandles[0] = s_inst[i].modelHandles[1] = -1;
        s_inst[i].ribbonHandles[0] = s_inst[i].ribbonHandles[1] = -1;
    }
    (void)VFX_DragonQi_GetAsset();

    s_skillIndex = Skill_GetIndexByName("GLTBC_LONG_CHIEN_VU_DA");
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_LONG_CHIEN_VU_DA");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLongChien);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLongChien);
    }
}

void CastGltbcLongChienVuDaSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_LCVD_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) { slot = 0; ReleaseLongChien(&s_inst[0]); }

    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_LONG_CHIEN_VU_DA");
    float baseDmg = def ? def->baseDamage : 16.0f;
    float lvlMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Twin-palm alternating barrage */
    Skill_RequestAnim(agentId, "taiji_cloud_hands", 0.65f);

    Vector3 origin = Vector3Add(anchor, Vector3Scale(dir, 0.9f));
    float gY = MapManager_GetGroundHeightAt(origin.x, origin.z);
    origin.y = gY + 1.25f;

    float targetRange = Clamp(d - 0.9f, 4.0f, 11.5f);
    float flightSecs  = targetRange / LCVD_SPEED_MPS;

    LongChienInst *inst = &s_inst[slot];
    *inst = (LongChienInst){ 0 };
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->modelHandles[0] = inst->modelHandles[1] = -1;
    inst->ribbonHandles[0] = inst->ribbonHandles[1] = -1;
    inst->targetRange  = targetRange;
    inst->flightSecs   = flightSecs;
    inst->headArc      = 0.5f;
    inst->damage       = baseDmg * lvlMult;
    inst->sizeScale    = scale;

    Color flameBody = (Color){ 228, 192, 86, 255 };
    Color flameGlow = (Color){ 255, 242, 166, 255 };
    int asset = VFX_DragonQi_GetAsset();

    for (int j = 0; j < 2; j++) {
        inst->curves[j].kind        = VFX_DRAGON_CURVE_HELIX;
        inst->curves[j].origin      = origin;
        inst->curves[j].dir         = dir;
        inst->curves[j].right       = (Vector3){ -dir.z, 0.0f, dir.x };
        inst->curves[j].groundY     = gY;
        inst->curves[j].scale       = scale;
        inst->curves[j].range       = targetRange;
        inst->curves[j].latAmp      = 0.72f * scale;
        inst->curves[j].verAmp      = 0.48f * scale;
        inst->curves[j].waveSign    = 1.0f;
        inst->curves[j].phaseOffset = (j == 0) ? 0.0f : PI;

        if (asset >= 0) {
            VFXModelSpawnParams mp = { 0 };
            mp.position      = origin;
            mp.scale         = LCVD_BASE_SCALE * scale;
            mp.loop          = true;
            mp.lifetime      = flightSecs + LCVD_DISSOLVE + 0.15f;
            mp.fadeInTime    = 0.08f;
            mp.fadeOutTime   = LCVD_DISSOLVE;
            mp.style         = VFX_MODEL_STYLE_QI_SPIRIT;
            mp.tint          = flameBody;
            mp.glowColor     = flameGlow;
            mp.glowIntensity = 3.10f;
            mp.opacity       = 0.80f;
            inst->modelHandles[j] = VFXModel_Spawn(asset, &mp);
            if (inst->modelHandles[j] >= 0) {
                VFXModel_SetSpinePathOverride(inst->modelHandles[j], LCVD_SPINE_BONES,
                                              inst->headArc, LCVD_BASE_SCALE * scale,
                                              1.7f, 0.45f, VFX_DragonCurve_Evaluate, &inst->curves[j]);
            }
        }
        inst->ribbonXforms[j]  = MatrixTranslate(origin.x, origin.y, origin.z);
        inst->ribbonHandles[j] = VFX_ComposeTrail(&inst->ribbonXforms[j], VC_MAT_HOLY,
                                                  0.12f * scale, 0.35f, TRAIL_PRESET_DRAGON);
    }

    VFX_ComposePalmPrint(origin, dir, 1.30f * scale, 0.25f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateGltbcLongChienVuDaSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_LCVD_INST; i++) {
        LongChienInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        float revStart = 0.0f;
        if (!inst->impacted) {
            float ft = Clamp(inst->elapsed / fmaxf(inst->flightSecs, 0.05f), 0.0f, 1.0f);
            inst->headArc = 0.5f + (inst->targetRange - 0.5f) * (0.75f * ft + 0.25f * ft * ft);
        } else {
            float pt = Clamp((inst->elapsed - inst->impactTime) / LCVD_DISSOLVE, 0.0f, 1.0f);
            inst->headArc = inst->curves[0].impactArc + 1.8f * pt;
            revStart = pt;
        }

        Vector3 centerHead = { 0 };
        for (int j = 0; j < 2; j++) {
            if (inst->modelHandles[j] >= 0) {
                VFXModel_SetSpineReveal(inst->modelHandles[j], revStart, 1.0f);
                VFXModel_SetSpinePathOverride(inst->modelHandles[j], LCVD_SPINE_BONES,
                                              inst->headArc, LCVD_BASE_SCALE * inst->sizeScale,
                                              1.7f, inst->impacted ? 0.68f : 0.45f,
                                              VFX_DragonCurve_Evaluate, &inst->curves[j]);
            }
            Vector3 hp = VFX_DragonCurve_Evaluate(inst->headArc, &inst->curves[j]);
            if (inst->modelHandles[j] >= 0) VFXModel_GetBoneWorldPos(inst->modelHandles[j], "head", &hp, NULL);
            inst->ribbonXforms[j] = MatrixTranslate(hp.x, hp.y, hp.z);
            centerHead = Vector3Add(centerHead, Vector3Scale(hp, 0.5f));
        }

        if (!inst->impacted) {
            int nearIds[8];
            int nearCnt = Entity_GetNearbyTargets(centerHead, LCVD_HIT_RADIUS * inst->sizeScale, nearIds, 8);
            bool hitAny = false;
            for (int k = 0; k < nearCnt; k++) {
                if (nearIds[k] != inst->ownerAgentId && Entity_CanDamageTarget(inst->ownerAgentId, nearIds[k])) {
                    hitAny = true;
                    break;
                }
            }
            if (hitAny || inst->elapsed >= inst->flightSecs) {
                inst->impacted = true;
                inst->impactTime = inst->elapsed;
                for (int j = 0; j < 2; j++) {
                    inst->curves[j].impacted  = true;
                    inst->curves[j].impactArc = inst->headArc;
                    if (inst->ribbonHandles[j] >= 0) {
                        VFX_KillTrail(inst->ribbonHandles[j]);
                        inst->ribbonHandles[j] = -1;
                    }
                }
                VFX_ComposeDragonSlam(centerHead, inst->sizeScale * 0.95f, false);

                int targets[12];
                int cnt = Entity_GetNearbyTargets(centerHead, LCVD_HIT_RADIUS * inst->sizeScale, targets, 12);
                bool damaged = false;
                for (int k = 0; k < cnt; k++) {
                    int tid = targets[k];
                    if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
                    Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f,
                                           Vector3Scale(inst->curves[0].dir, 3.2f));
                    Entity_ApplyStatus(tid, STATUS_BURN, 11.0f / 3.0f, 3.0f, inst->ownerAgentId);
                    VoHoc_ApplyDragonMark(inst->ownerAgentId, tid, 1);
                    damaged = true;
                }
                if (damaged) {
                    TimeFX_Hitstop(0.045f, 0.08f);
                    PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
                }
            }
        }

        if (inst->impacted && (inst->elapsed - inst->impactTime) >= LCVD_DISSOLVE) {
            ReleaseLongChien(inst);
        }
    }
}

void DrawGltbcLongChienVuDaSkill(void) { }

void UnloadGltbcLongChienVuDaSkill(void)
{
    for (int i = 0; i < MAX_LCVD_INST; i++) ReleaseLongChien(&s_inst[i]);
}
