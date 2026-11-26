#include "skills/fire/gltbc_mat_van_bat_vu_skill/gltbc_mat_van_bat_vu_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/vfx_model/vfx_model.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_MVBV_INST    2
#define MVBV_SPINE_BONES 24
#define MVBV_BASE_SCALE  0.34f
#define MVBV_DURATION    5.0f
#define MVBV_RADIUS      3.8f

typedef struct {
    bool                active;
    int                 ownerAgentId;
    int                 modelHandle;
    VFX_DragonCurveSpec curve;
    Vector3             center;
    float               elapsed;
    float               tickTimer;
    float               sizeScale;
} MatVanInst;

static MatVanInst s_inst[MAX_MVBV_INST];
static int        s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(GltbcMatVanBatVu)

static void ReleaseMatVan(MatVanInst *inst)
{
    if (!inst) return;
    if (inst->modelHandle >= 0) { VFXModel_Kill(inst->modelHandle); inst->modelHandle = -1; }
    inst->active = false;
}

static void AbortMatVan(int agentId)
{
    for (int i = 0; i < MAX_MVBV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) ReleaseMatVan(&s_inst[i]);
    }
}

static bool HasActiveMatVan(int agentId)
{
    for (int i = 0; i < MAX_MVBV_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitGltbcMatVanBatVuSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_MVBV_INST; i++) {
        s_inst[i].active      = false;
        s_inst[i].modelHandle = -1;
    }
    (void)VFX_DragonQi_GetAsset();

    s_skillIndex = Skill_GetIndexByName("GLTBC_MAT_VAN_BAT_VU");
    const VoHocSkillDef *def = VoHoc_FindByName("GLTBC_MAT_VAN_BAT_VU");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortMatVan);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveMatVan);
    }
}

void CastGltbcMatVanBatVuSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    (void)target;
    int slot = -1;
    for (int i = 0; i < MAX_MVBV_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) { slot = 0; ReleaseMatVan(&s_inst[0]); }

    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;
    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 center = owner ? owner->position : startPos;
    float gY = MapManager_GetGroundHeightAt(center.x, center.z);
    center.y = gY;

    Skill_RequestAnim(agentId, "casting", 0.55f);
    /* Immediate CDR pulse on cast */
    SkillManager_ReduceAgentCooldowns(agentId, 2.5f);

    MatVanInst *inst = &s_inst[slot];
    *inst = (MatVanInst){ 0 };
    inst->active       = true;
    inst->ownerAgentId = agentId;
    inst->modelHandle  = -1;
    inst->center       = center;
    inst->sizeScale    = scale;

    inst->curve.kind        = VFX_DRAGON_CURVE_ORBIT;
    inst->curve.origin      = (Vector3){ center.x, gY + 1.45f, center.z };
    inst->curve.dir         = (Vector3){ 1.0f, 0.0f, 0.0f };
    inst->curve.right       = (Vector3){ 0.0f, 0.0f, 1.0f };
    inst->curve.scale       = scale;
    inst->curve.radius      = MVBV_RADIUS * 0.72f * scale;
    inst->curve.verAmp      = 0.35f * scale;
    inst->curve.waveSign    = 1.0f;
    inst->curve.phaseOffset = 0.0f;

    Color flameBody = (Color){ 228, 192, 86, 255 };
    Color flameGlow = (Color){ 255, 242, 166, 255 };

    int asset = VFX_DragonQi_GetAsset();
    if (asset >= 0) {
        VFXModelSpawnParams mp = { 0 };
        mp.position      = inst->curve.origin;
        mp.scale         = MVBV_BASE_SCALE * scale;
        mp.loop          = true;
        mp.lifetime      = MVBV_DURATION + 0.15f;
        mp.fadeInTime    = 0.25f;
        mp.fadeOutTime   = 0.40f;
        mp.style         = VFX_MODEL_STYLE_QI_SPIRIT;
        mp.tint          = flameBody;
        mp.glowColor     = flameGlow;
        mp.glowIntensity = 3.10f;
        mp.opacity       = 0.80f;
        inst->modelHandle = VFXModel_Spawn(asset, &mp);
        if (inst->modelHandle >= 0) {
            VFXModel_SetSpinePathOverride(inst->modelHandle, MVBV_SPINE_BONES,
                                          0.0f, MVBV_BASE_SCALE * scale,
                                          1.2f, 0.25f, VFX_DragonCurve_Evaluate, &inst->curve);
        }
    }

    VFX_ComposePalmPrint((Vector3){ center.x, gY + 0.05f, center.z },
                         (Vector3){ 0.0f, 1.0f, 0.0f }, MVBV_RADIUS * scale, 0.55f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateGltbcMatVanBatVuSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_MVBV_INST; i++) {
        MatVanInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed   += dt;
        inst->tickTimer += dt;

        float arc = inst->elapsed * 7.2f; /* Steady majestic coiling orbit */
        if (inst->modelHandle >= 0) {
            VFXModel_SetSpinePathOverride(inst->modelHandle, MVBV_SPINE_BONES,
                                          arc, MVBV_BASE_SCALE * inst->sizeScale,
                                          1.2f, 0.25f, VFX_DragonCurve_Evaluate, &inst->curve);
        }

        if (inst->tickTimer >= 1.0f) {
            inst->tickTimer -= 1.0f;
            float rad = MVBV_RADIUS * inst->sizeScale;

            /* Ally mana regen + haste inside zone */
            const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
            if (owner && Vector3Distance(owner->position, inst->center) <= rad) {
                Entity_RestoreMana(inst->ownerAgentId, 7.0f);
                Entity_AddModifier(inst->ownerAgentId, 1.25f, 1.2f);
            }

            /* Enemies inside zone suffer armor break + Long Ấn */
            int targets[12];
            int cnt = Entity_GetNearbyTargets(inst->center, rad, targets, 12);
            for (int k = 0; k < cnt; k++) {
                int tid = targets[k];
                if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
                Entity_ApplyStatus(tid, STATUS_VULNERABLE, 0.20f, 1.5f, inst->ownerAgentId);
                VoHoc_ApplyDragonMark(inst->ownerAgentId, tid, 1);
            }

            VFX_SpawnShockRing((Vector3){ inst->center.x, inst->center.y + 0.05f, inst->center.z },
                               (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_HOLY, rad * 0.85f, 0.45f);
        }

        if (inst->elapsed >= MVBV_DURATION) ReleaseMatVan(inst);
    }
}

void DrawGltbcMatVanBatVuSkill(void) { }

void UnloadGltbcMatVanBatVuSkill(void)
{
    for (int i = 0; i < MAX_MVBV_INST; i++) ReleaseMatVan(&s_inst[i]);
}
