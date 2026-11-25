#include "skills/metal/tcdk_luu_tinh_thu_skill/tcdk_luu_tinh_thu_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/skill_helper.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/wind/wind_system.h"
#include "maps/map_manager.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_LTT_INST   6
#define MAX_LTT_HIT    12
#define LTT_SPEED      18.0f
#define LTT_RANGE      9.5f
#define LTT_HIT_RADIUS 0.45f

typedef struct {
    bool    active;
    bool    launched;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 dir;
    Vector3 pos;
    float   elapsed;
    float   castSecs;
    float   travelled;
    float   damage;
    float   sizeScale;
    int     lanceHandle;
    int     hitIds[MAX_LTT_HIT];
    int     hitCount;
} LuuTinhInst;

static LuuTinhInst s_inst[MAX_LTT_INST];
static int         s_skillIndex = -1;

static bool AlreadyHit(const LuuTinhInst *inst, int agentId)
{
    for (int i = 0; i < inst->hitCount; i++) {
        if (inst->hitIds[i] == agentId) return true;
    }
    return false;
}

static void KillLuuTinhHandles(LuuTinhInst *inst)
{
    if (inst->lanceHandle > 0) {
        VFX_MetalLance_Kill(inst->lanceHandle);
        inst->lanceHandle = -1;
    }
}

static void AbortTcdkLuuTinh(int agentId)
{
    for (int i = 0; i < MAX_LTT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillLuuTinhHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveTcdkLuuTinh(int agentId)
{
    for (int i = 0; i < MAX_LTT_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitTcdkLuuTinhThuSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_LTT_INST; i++) {
        s_inst[i].active      = false;
        s_inst[i].lanceHandle = -1;
    }
    s_skillIndex = Skill_GetIndexByName("TCDK_LUU_TINH_THU");
    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_LUU_TINH_THU");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortTcdkLuuTinh);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTcdkLuuTinh);
    }
}

void CastTcdkLuuTinhThuSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_LTT_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        slot = 0;
        KillLuuTinhHandles(&s_inst[0]);
    }

    const VoHocSkillDef *def = VoHoc_FindByName("TCDK_LUU_TINH_THU");
    float baseDmg = def ? def->baseDamage : 13.5f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Thu kiếm ngang sườn, xoay cổ tay lấy trục */
    Skill_RequestAnim(agentId, "withdrawing_sword", 0.15f);

    LuuTinhInst *inst = &s_inst[slot];
    *inst = (LuuTinhInst){0};
    inst->active       = true;
    inst->launched     = false;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->dir          = dir;
    inst->pos          = Vector3Add(anchor, Vector3Scale(dir, 0.55f));
    inst->pos.y        = MapManager_GetGroundHeightAt(inst->pos.x, inst->pos.z) + 1.10f;
    inst->castSecs     = def ? def->castSecs : 0.15f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;
    inst->lanceHandle  = -1;

    PlayCastSound(EFFECT_PRESET_METAL_SHARD);
}

void UpdateTcdkLuuTinhThuSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    const ClashEvent *events = NULL;
    int eventCount = Combat_PeekEvents(&events);

    for (int i = 0; i < MAX_LTT_INST; i++) {
        LuuTinhInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (!inst->launched) {
            if (inst->elapsed >= inst->castSecs) {
                /* Phát: Đâm thẳng phóng luồng kiếm khí xuyên tuyến từ mũi kiếm (18 m/s) */
                inst->launched = true;
                Skill_RequestAnim(inst->ownerAgentId, "thrust_slash", 0.25f);
                const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
                const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
                if (owner && anim) {
                    Vector3 tipPos;
                    if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                                    "mixamorig:RightHand", (Vector3){ 0.0f, 0.85f, 0.0f }, &tipPos)) {
                        inst->pos = tipPos;
                    }
                }
                inst->lanceHandle = VFX_MetalLance_Spawn(inst->pos, inst->dir, inst->sizeScale, 0.60f);
                VFX_MartialShockCone_Spawn(inst->pos, inst->dir, VC_MAT_METAL, 1.9f * inst->sizeScale, 0.58f * inst->sizeScale, 0.18f);
                Wind_SpawnGust(inst->pos, inst->dir, 1.25f * inst->sizeScale, 7.0f, 0.14f);
            }
            continue;
        }

        int myId = (s_skillIndex >= 0) ? (s_skillIndex * 1000 + i) : (9100 + i);
        bool clashedOut = false;
        for (int e = 0; e < eventCount; e++) {
            if (events[e].skillInstanceId != myId) continue;
            if (events[e].outcome == CLASH_B_WINS || events[e].outcome == CLASH_MUTUAL_DESTROY) {
                VFX_ComposeContactSparkMode(events[e].clashPoint, VC_MAT_METAL, 0.70f * inst->sizeScale, 0.85f, CONTACT_SPARK_CENTRIFUGAL);
                clashedOut = true;
                break;
            }
        }
        if (clashedOut) {
            KillLuuTinhHandles(inst);
            inst->active = false;
            continue;
        }

        float step = LTT_SPEED * dt;
        inst->pos = Vector3Add(inst->pos, Vector3Scale(inst->dir, step));
        inst->pos.y = MapManager_GetGroundHeightAt(inst->pos.x, inst->pos.z) + 1.10f;
        inst->travelled += step;
        VFX_MetalLance_SetPos(inst->lanceHandle, inst->pos, inst->dir);

        /* Piercing line check (0.78x damage after primary target) */
        int targets[8];
        int cnt = Entity_GetNearbyTargets(inst->pos, LTT_HIT_RADIUS * inst->sizeScale + 0.35f, targets, 8);
        for (int k = 0; k < cnt; k++) {
            int tid = targets[k];
            if (tid == inst->ownerAgentId || AlreadyHit(inst, tid) || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
            if (inst->hitCount < MAX_LTT_HIT) inst->hitIds[inst->hitCount++] = tid;

            float mult = (inst->hitCount == 1) ? 1.0f : 0.78f;
            Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage * mult, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
            VoHoc_ApplyMetalMark(inst->ownerAgentId, tid, 1);
            VFX_ComposeContactSparkMode(inst->pos, VC_MAT_METAL, 0.65f * inst->sizeScale, 0.80f, CONTACT_SPARK_CENTRIFUGAL);
            PlayImpactSound(EFFECT_PRESET_METAL_SHARD);
        }

        if (inst->travelled >= LTT_RANGE * inst->sizeScale) {
            KillLuuTinhHandles(inst);
            inst->active = false;
            continue;
        }

        /* Submit for Đấu Pháp clash detection (0 dmg here since piercing loop applies exact damage) */
        Combat_SubmitProjectile(inst->ownerAgentId, ELEM_METAL, inst->pos,
                                LTT_HIT_RADIUS * inst->sizeScale, 0.0f, 0.0f, myId);
    }
}

void DrawTcdkLuuTinhThuSkill(void) { }

void UnloadTcdkLuuTinhThuSkill(void)
{
    for (int i = 0; i < MAX_LTT_INST; i++) {
        KillLuuTinhHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}

bool IsTcdkLuuTinhThuSkillCoiling(void)
{
    for (int i = 0; i < MAX_LTT_INST; i++) {
        if (s_inst[i].active && !s_inst[i].launched) return true;
    }
    return false;
}

int GetTcdkLuuTinhThuSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    if (!outProjectiles || maxProjectiles <= 0) return 0;
    for (int i = 0; i < MAX_LTT_INST && count < maxProjectiles; i++) {
        if (s_inst[i].active && s_inst[i].launched) {
            outProjectiles[count].position = s_inst[i].pos;
            outProjectiles[count].radius   = LTT_HIT_RADIUS * s_inst[i].sizeScale;
            outProjectiles[count].active   = true;
            count++;
        }
    }
    return count;
}

void DeactivateTcdkLuuTinhThuProjectile(int index)
{
    int cur = 0;
    for (int i = 0; i < MAX_LTT_INST; i++) {
        if (s_inst[i].active && s_inst[i].launched) {
            if (cur == index) {
                KillLuuTinhHandles(&s_inst[i]);
                s_inst[i].active = false;
                return;
            }
            cur++;
        }
    }
}
