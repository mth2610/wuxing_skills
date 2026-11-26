#include "skills/metal/tcdk_nhat_kiem_doan_thien_skill/tcdk_nhat_kiem_doan_thien_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/afterimage.h"
#include "core/time_fx.h"
#include "core/wind/wind_system.h"
#include "character/character_model.h"
#include "maps/map_manager.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_NKDT_INST  4
#define NKDT_DASH_DIST 4.5f
#define NKDT_HIT_RAD   1.35f

typedef struct {
    bool    active;
    bool    slashed;
    int     ownerAgentId;
    Vector3 startPos;
    Vector3 dir;
    float   elapsed;
    float   windupSecs;
    float   dashSecs;
    float   ghostAccum;
    float   damage;
    float   sizeScale;
    bool    hitIds[MAX_AGENTS];
} NhatKiemInst;

static NhatKiemInst s_inst[MAX_NKDT_INST];
static int          s_skillIndex = -1;
static int          s_luuTinhIdx = -1;

SKILL_EMPTY_PROJECTILE_API(TcdkNhatKiemDoanThien)

static void AbortTcdkNhatKiem(int agentId)
{
    for (int i = 0; i < MAX_NKDT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTcdkNhatKiem(int agentId)
{
    for (int i = 0; i < MAX_NKDT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTcdkNhatKiemDoanThienSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_NKDT_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("TCDK_NHAT_KIEM_DOAN_THIEN");
    s_luuTinhIdx = Skill_GetIndexByName("TCDK_LUU_TINH_THU");
    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_NHAT_KIEM_DOAN_THIEN");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTcdkNhatKiem);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTcdkNhatKiem);
    }
}

void CastTcdkNhatKiemDoanThienSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_NKDT_INST; i++) {
        if (!s_inst[i].active || s_inst[i].ownerAgentId == agentId) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_NHAT_KIEM_DOAN_THIEN");
    float baseDmg = def ? def->baseDamage : 40.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Tra kiếm vào vỏ, nén lò xo cơ bắp hạ thấp trọng tâm */
    Skill_RequestAnim(agentId, "sheath_sword_1", 0.25f);

    NhatKiemInst *inst = &s_inst[slot];
    *inst = (NhatKiemInst){0};
    inst->active       = true;
    inst->slashed      = false;
    inst->ownerAgentId = agentId;
    inst->startPos     = anchor;
    inst->dir          = dir;
    inst->windupSecs   = def ? def->castSecs : 0.25f;
    inst->dashSecs     = 0.12f;
    inst->ghostAccum   = 0.04f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;

    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

void UpdateTcdkNhatKiemDoanThienSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_NKDT_INST; i++) {
        NhatKiemInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (!owner || !owner->active) {
            inst->active = false;
            continue;
        }

        if (inst->elapsed < inst->windupSecs) continue;

        float dashEnd = inst->windupSecs + inst->dashSecs;
        if (!inst->slashed) {
            inst->slashed = true;
            Skill_RequestAnim(inst->ownerAgentId, "stable_sword_outward_slash", 0.35f);
            float dist = NKDT_DASH_DIST * inst->sizeScale;
            Vector3 pA = { inst->startPos.x, MapManager_GetGroundHeightAt(inst->startPos.x, inst->startPos.z) + 1.15f, inst->startPos.z };
            Vector3 midPos = Vector3Add(pA, Vector3Scale(inst->dir, dist * 0.45f));
            Vector3 slashNorm = (Vector3){ 0.08f, 1.0f, 0.0f };
            const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
            if (anim) {
                Vector3 bPivot, bDir, bNorm;
                if (SkillHelper_SampleBoneSwingPlane(anim, owner->position, owner->facingYaw,
                                                     "mixamorig:RightHand", (Vector3){ 0.0f, 0.85f, 0.0f },
                                                     &bPivot, &bDir, &bNorm)) {
                    slashNorm = bNorm;
                }
            }
            VFX_MetalSlash_Spawn(midPos, inst->dir, slashNorm, 2.55f * inst->sizeScale, 135.0f, 0.44f, inst->sizeScale, false);
            Vector3 pB = Vector3Add(pA, Vector3Scale(inst->dir, dist));
            VFX_MetalSeverLine_Spawn(pA, pB, 0.42f, inst->sizeScale);
            VFX_MartialShockCone_Spawn(pA, inst->dir, VC_MAT_METAL, 3.2f * inst->sizeScale, 1.05f * inst->sizeScale, 0.24f);
            VFX_SpawnShockRing(pA, inst->dir, VC_MAT_METAL, 1.8f * inst->sizeScale, 0.26f);
            Wind_SpawnGust(pA, inst->dir, 2.0f * inst->sizeScale, 11.0f, 0.22f);
        }

        if (inst->elapsed <= dashEnd) {
            float t = Clamp((inst->elapsed - inst->windupSecs) / inst->dashSecs, 0.0f, 1.0f);
            float dist = NKDT_DASH_DIST * inst->sizeScale * t;
            Vector3 cur = Vector3Add(inst->startPos, Vector3Scale(inst->dir, dist));
            cur.y = MapManager_GetGroundHeightAt(cur.x, cur.z);
            Entity_SetPosition(inst->ownerAgentId, cur);

            /* Ghost afterimages every 0.04s during high-speed Iaido dash */
            inst->ghostAccum += dt;
            if (inst->ghostAccum >= 0.04f && CharacterModel_IsLoaded()) {
                inst->ghostAccum = 0.0f;
                float yaw = atan2f(inst->dir.x, inst->dir.z);
                Matrix xf = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(cur.x, cur.y, cur.z));
                Afterimage_Spawn(CharacterModel_GetModel(), xf, ColorAlpha(ELEMENT_COLOR_METAL, 180), 0.26f);
            }

            /* Single-target primary assassination check along dash */
            int targets[8];
            int cnt = Entity_GetNearbyTargets(cur, NKDT_HIT_RAD * inst->sizeScale, targets, 8);
            for (int k = 0; k < cnt; k++) {
                int tid = targets[k];
                if (tid == inst->ownerAgentId || tid < 0 || tid >= MAX_AGENTS || inst->hitIds[tid]) continue;
                if (!Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
                inst->hitIds[tid] = true;

                float mult = 1.0f;
                VoHoc_OnNhatKiemConsume(inst->ownerAgentId, tid, s_skillIndex, s_luuTinhIdx, &mult);
                Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage * mult, 0.0f,
                                       Vector3Scale(inst->dir, 3.0f));
                Entity_ApplyStun(tid, 0.55f);
                TimeFX_Hitstop(0.065f, 0.06f);
                VFX_ComposeContactSparkMode(cur, VC_MAT_METAL, 1.0f * inst->sizeScale, 1.0f, CONTACT_SPARK_CENTRIFUGAL);
                if (mult > 1.01f) {
                    VFX_SpawnShockRing((Vector3){ cur.x, cur.y + 0.95f, cur.z }, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_METAL, 2.4f * inst->sizeScale, 0.34f);
}
                PlayImpactSound(EFFECT_PRESET_METAL_SHARD);
                break; /* Single-target finisher (N = 1.0) */
            }
        } else if (inst->elapsed <= dashEnd + 0.18f) {
            /* Thu: Trượt quán tính 1.2m, tra kiếm vào vỏ */
            float recT = (inst->elapsed - dashEnd) / 0.18f;
            float slide = (NKDT_DASH_DIST + 1.2f * (1.0f - powf(1.0f - recT, 2.0f))) * inst->sizeScale;
            Vector3 cur = Vector3Add(inst->startPos, Vector3Scale(inst->dir, slide));
            cur.y = MapManager_GetGroundHeightAt(cur.x, cur.z);
            Entity_SetPosition(inst->ownerAgentId, cur);
        } else {
            inst->active = false;
        }
    }
}

void DrawTcdkNhatKiemDoanThienSkill(void)
{
    for (int i = 0; i < MAX_NKDT_INST; i++) {
        const NhatKiemInst *inst = &s_inst[i];
        if (!inst->active || inst->elapsed >= inst->windupSecs) continue;
        const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
        if (!owner) continue;
        float prog = Clamp(inst->elapsed / inst->windupSecs, 0.0f, 1.0f);
        VFX_MetalIaidoCoil_Draw(owner->position, inst->dir, prog, NKDT_DASH_DIST * inst->sizeScale, camera);
    }
}

void UnloadTcdkNhatKiemDoanThienSkill(void)
{
    for (int i = 0; i < MAX_NKDT_INST; i++) s_inst[i].active = false;
}
