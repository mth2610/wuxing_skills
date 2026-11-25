#include "skills/fire/lhpt_xich_diem_trac_cuoc_skill/lhpt_xich_diem_trac_cuoc_skill.h"
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

#define MAX_XDTC_INST    6
#define XDTC_SPEED       14.5f /* VH_HIT_FAST >= 14.0 m/s, readable rolling fire kick */
#define XDTC_RANGE       9.5f
#define XDTC_HIT_RADIUS  0.52f

typedef struct {
    bool    active;
    bool    launched;
    int     ownerAgentId;
    Vector3 origin;
    Vector3 launchPos;
    Vector3 dir;
    Vector3 perp;
    Vector3 pos;
    float   elapsed;
    float   castSecs;
    float   travelled;
    float   emberAccum;
    float   damage;
    float   sizeScale;
} XichDiemInst;

static XichDiemInst s_inst[MAX_XDTC_INST];
static int          s_skillIndex = -1;

/* Pure every-frame Shader-Puff airborne fire (no trails, no VolumeTrail, no decals) */
static void EmitSteadyFirePuff(Vector3 pos, float radius, float srcRadius,
                               float life, int count, float intensity, bool dynLight)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIREBALL);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF; /* Airborne fire: mandatory Shader-Puff */
    cfg.emitterShape    = VFX_FIRE_EMITTER_SPHERE;
    cfg.emissionMode    = VFX_FIRE_EMIT_STEADY;
    cfg.motionType      = VFX_FIRE_MOTION_AIRFLOW;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = count;
    cfg.particleRadius  = radius;
    cfg.sourceRadius    = srcRadius;
    cfg.buoyancy        = 2.6f;
    cfg.turbulenceSpeed = 2.2f;
    cfg.distortion      = 0.58f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = life;
    cfg.coreHeat        = 1.35f;
    cfg.intensity       = intensity * 0.72f;
    cfg.emissiveBoost   = 1.40f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = dynLight;
    VFX_ComposeGuidedFire(pos, &cfg);
}

static void EmitDetonationPuff(Vector3 pos, float scale)
{
    VFX_FireConfig cfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&cfg, VFX_FIRE_PRESET_FIREBALL);
    cfg.preset          = VFX_FIRE_PRESET_CUSTOM;
    cfg.mode            = VFX_FIRE_MODE_SHADER_PUFF;
    cfg.emitterShape    = VFX_FIRE_EMITTER_SPHERE;
    cfg.emissionMode    = VFX_FIRE_EMIT_BURST;
    cfg.motionType      = VFX_FIRE_MOTION_BLAST;
    cfg.attachment      = VFX_FIRE_ATTACH_FREE;
    cfg.particleCount   = 80;
    cfg.particleRadius  = 0.44f * scale;
    cfg.sourceRadius    = 0.42f * scale;
    cfg.fieldSpeed      = 6.4f;
    cfg.buoyancy        = 3.2f;
    cfg.turbulenceSpeed = 2.4f;
    cfg.distortion      = 0.58f;
    cfg.softness        = 0.36f;
    cfg.lifetime        = 0.44f;
    cfg.coreHeat        = 1.40f;
    cfg.intensity       = 1.90f;
    cfg.emissiveBoost   = 1.45f;
    cfg.emitSparks      = true;
    cfg.emitSmoke       = false;
    cfg.dynamicLight    = true;
    VFX_ComposeGuidedFire(pos, &cfg);
}

static void AbortLhptXichDiem(int agentId)
{
    for (int i = 0; i < MAX_XDTC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveLhptXichDiem(int agentId)
{
    for (int i = 0; i < MAX_XDTC_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitLhptXichDiemTracCuocSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();
    for (int i = 0; i < MAX_XDTC_INST; i++) s_inst[i].active = false;

    s_skillIndex = Skill_GetIndexByName("LHPT_XICH_DIEM_TRAC_CUOC");
    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_XICH_DIEM_TRAC_CUOC");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortLhptXichDiem);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveLhptXichDiem);
    }
}

void CastLhptXichDiemTracCuocSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    int slot = -1;
    for (int i = 0; i < MAX_XDTC_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    const VoHocSkillDef *def = VoHoc_FindByName("LHPT_XICH_DIEM_TRAC_CUOC");
    float baseDmg = def ? def->baseDamage : 14.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale   = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir    = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    /* Khởi: Co gối ngang hông tung Trắc Cước (side_kick), lửa Shader-Puff bùng ngay gót chân */
    Skill_RequestAnim(agentId, "side_kick", 0.50f);
    const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(agentId);

    Vector3 footPos = Vector3Add(anchor, Vector3Scale(dir, 0.55f));
    footPos.y = MapManager_GetGroundHeightAt(footPos.x, footPos.z) + 0.95f;
    if (owner && anim) {
        Vector3 bonePos;
        if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                        "mixamorig:RightFoot", (Vector3){ 0.0f, 0.06f, 0.0f }, &bonePos)) {
            footPos = bonePos;
        }
    }

    XichDiemInst *inst = &s_inst[slot];
    *inst = (XichDiemInst){0};
    inst->active       = true;
    inst->launched     = false;
    inst->ownerAgentId = agentId;
    inst->origin       = anchor;
    inst->launchPos    = footPos;
    inst->dir          = dir;
    inst->perp         = (Vector3){ -dir.z, 0.0f, dir.x };
    inst->pos          = footPos;
    inst->castSecs     = def ? def->castSecs : 0.16f;
    inst->damage       = baseDmg * pveMult;
    inst->sizeScale    = scale;

    EmitSteadyFirePuff(footPos, 0.22f * scale, 0.12f * scale, 0.24f, 65, 2.25f, false);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

static void DetonateXichDiem(XichDiemInst *inst, Vector3 at)
{
    EmitDetonationPuff(at, inst->sizeScale);
    VFX_ComposeFireballBurst(at, VC_MAT_FIRE, 0.72f * inst->sizeScale, 0.78f);
    VFX_SpawnShockRing(at, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 1.35f * inst->sizeScale, 0.28f);
    VFX_ComposeEmberBurst(at, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 0.95f * inst->sizeScale, 0.85f);
    inst->active = false;
}

void UpdateLhptXichDiemTracCuocSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos;
    (void)enemyRadius;

    const ClashEvent *events = NULL;
    int eventCount = Combat_PeekEvents(&events);

    for (int i = 0; i < MAX_XDTC_INST; i++) {
        XichDiemInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        if (!inst->launched) {
            const Agent *owner = Entity_GetAgent(inst->ownerAgentId);
            const struct CharacterAnimState *anim = SkillManager_GetAgentAnim(inst->ownerAgentId);
            if (owner && anim) {
                Vector3 bonePos;
                if (SkillHelper_GetBoneWorldPos(anim, owner->position, owner->facingYaw,
                                                "mixamorig:RightFoot", (Vector3){ 0.0f, 0.06f, 0.0f }, &bonePos)) {
                    inst->pos = bonePos;
                }
            }
            /* Every-frame rolling fire wrapping the chambering foot */
            EmitSteadyFirePuff(inst->pos, 0.24f * inst->sizeScale, 0.14f * inst->sizeScale,
                               0.22f, 70, 2.35f, false);

            if (inst->elapsed >= inst->castSecs) {
                /* Phát: Đạp thẳng phóng cầu lửa cuộn (14.5 m/s) rời khỏi gót chân */
                inst->launched  = true;
                inst->launchPos = inst->pos;
                VFX_FireKickArc_Spawn(inst->launchPos, inst->dir, (Vector3){ 0.0f, 1.0f, 0.0f },
                                      1.15f * inst->sizeScale, 95.0f, 0.22f, inst->sizeScale);
                EmitSteadyFirePuff(inst->pos, 0.34f * inst->sizeScale, 0.22f * inst->sizeScale,
                                   0.34f, 135, 2.60f, true);
                VFX_ComposeEmberBurst(inst->pos, inst->dir, VC_MAT_FIRE, 0.70f * inst->sizeScale, 0.70f);
                Wind_SpawnGust(inst->pos, inst->dir, 1.3f * inst->sizeScale, 6.5f, 0.16f);
            }
            continue;
        }

        int myId = (s_skillIndex >= 0) ? (s_skillIndex * 1000 + i) : (9200 + i);
        bool clashedOut = false;
        for (int e = 0; e < eventCount; e++) {
            if (events[e].skillInstanceId != myId) continue;
            if (events[e].outcome == CLASH_B_WINS || events[e].outcome == CLASH_MUTUAL_DESTROY) {
                DetonateXichDiem(inst, events[e].clashPoint);
                clashedOut = true;
                break;
            }
        }
        if (clashedOut) continue;

        float maxDist = XDTC_RANGE * inst->sizeScale;
        inst->travelled += XDTC_SPEED * dt;
        float t = Clamp(inst->travelled / maxDist, 0.0f, 1.0f);

        /* Living aerodynamic fire arc: slight buoyant lift + gentle thermal sway */
        float liftY = sinf(t * PI) * 0.28f;
        float sway  = sinf(t * PI * 2.0f) * 0.16f * sinf(t * PI);
        Vector3 flatPos = Vector3Add(inst->launchPos, Vector3Scale(inst->dir, inst->travelled));
        float groundY = MapManager_GetGroundHeightAt(flatPos.x, flatPos.z);
        inst->pos = (Vector3){
            flatPos.x + inst->perp.x * sway,
            fmaxf(inst->launchPos.y + liftY, groundY + 0.85f),
            flatPos.z + inst->perp.z * sway
        };

        /* Every-frame billowing Shader-Puff fireball core (no ribbon/VolumeTrail) */
        EmitSteadyFirePuff(inst->pos, 0.34f * inst->sizeScale, 0.24f * inst->sizeScale,
                           0.34f, 75, 1.95f, true);

        inst->emberAccum += dt;
        if (inst->emberAccum >= 0.08f) {
            inst->emberAccum = 0.0f;
            VFX_ComposeEmberBurst(inst->pos, (Vector3){ 0.0f, 1.0f, 0.0f },
                                  VC_MAT_FIRE, 0.55f * inst->sizeScale, 0.50f);
        }

        int targets[8];
        int cnt = Entity_GetNearbyTargets(inst->pos, XDTC_HIT_RADIUS * inst->sizeScale + 0.35f, targets, 8);
        bool hitEnemy = false;
        for (int k = 0; k < cnt; k++) {
            int tid = targets[k];
            if (tid == inst->ownerAgentId || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
            Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, (Vector3){ 0.0f, 0.0f, 0.0f });
            Entity_ApplyStatus(tid, STATUS_BURN, 7.0f / 3.0f, 3.0f, inst->ownerAgentId);
            VoHoc_ApplyFireMark(inst->ownerAgentId, tid, 1);
            PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
            DetonateXichDiem(inst, inst->pos);
            hitEnemy = true;
            break;
        }
        if (hitEnemy) continue;

        if (inst->travelled >= maxDist) {
            DetonateXichDiem(inst, inst->pos);
            continue;
        }

        Combat_SubmitProjectile(inst->ownerAgentId, ELEM_FIRE, inst->pos,
                                XDTC_HIT_RADIUS * inst->sizeScale, 0.0f, 0.0f, myId);
    }
}

void DrawLhptXichDiemTracCuocSkill(void) { }

void UnloadLhptXichDiemTracCuocSkill(void)
{
    for (int i = 0; i < MAX_XDTC_INST; i++) s_inst[i].active = false;
}

bool IsLhptXichDiemTracCuocSkillCoiling(void)
{
    for (int i = 0; i < MAX_XDTC_INST; i++) {
        if (s_inst[i].active && !s_inst[i].launched) return true;
    }
    return false;
}

int GetLhptXichDiemTracCuocSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    if (!outProjectiles || maxProjectiles <= 0) return 0;
    for (int i = 0; i < MAX_XDTC_INST && count < maxProjectiles; i++) {
        if (s_inst[i].active && s_inst[i].launched) {
            outProjectiles[count].position = s_inst[i].pos;
            outProjectiles[count].radius   = XDTC_HIT_RADIUS * s_inst[i].sizeScale;
            outProjectiles[count].active   = true;
            count++;
        }
    }
    return count;
}

void DeactivateLhptXichDiemTracCuocProjectile(int index)
{
    int count = 0;
    for (int i = 0; i < MAX_XDTC_INST; i++) {
        if (s_inst[i].active && s_inst[i].launched) {
            if (count == index) {
                DetonateXichDiem(&s_inst[i], s_inst[i].pos);
                return;
            }
            count++;
        }
    }
}
