#include "skills/earth/btbc_dia_liet_chan_ba_skill/btbc_dia_liet_chan_ba_skill.h"
#include "skills/skill_boilerplate.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "skills/vo_hoc/vo_hoc_sets.h"
#include "core/composition/visual_composer.h"
#include "core/material/material_system.h"
#include "core/wind/wind_system.h"
#include "post_process/camera_fx.h"
#include "maps/map_manager.h"
#include "combat/combat.h"
#include "entities/entities.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_DLCB_INST      4
#define MAX_DLCB_HIT       32
#define DLCB_RANGE         6.5f
#define DLCB_HALF_CONE_RAD (32.5f * DEG2RAD)
#define DLCB_SPEED         15.5f
#define DLCB_WAVE_COUNT    5

typedef struct {
    bool      active;
    int       ownerAgentId;
    AgentTeam casterTeam;
    Vector3   origin;
    Vector3   dir;
    float     elapsed;
    float     frontDist;
    float     crestVfxAccum;
    float     damage;
    float     sizeScale;
    int       deflectsRemaining;
    int       hitIds[MAX_DLCB_HIT];
    int       hitCount;
    int       waveHandles[DLCB_WAVE_COUNT];
} DiaLietInst;

static DiaLietInst s_inst[MAX_DLCB_INST];
static int s_skillIndex = -1;

SKILL_EMPTY_PROJECTILE_API(BtbcDiaLietChanBa)

static bool AlreadyHit(const DiaLietInst *inst, int agentId) {
    for (int i = 0; i < inst->hitCount; i++) {
        if (inst->hitIds[i] == agentId) return true;
    }
    return false;
}

static void EmitRidgeTri(Vector3 a, Vector3 b, Vector3 c) {
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    float l = Vector3Length(n);
    n = (l > 1e-5f) ? Vector3Scale(n, 1.0f / l) : (Vector3){ 0.0f, 1.0f, 0.0f };
    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
}

/* Chiseled multi-faceted basalt dorsal ridge (<= 0.56m height) */
static void DrawSculptedBasaltRidge(float w, float h, float d, int seed) {
    float hw = w * 0.5f, hd = d * 0.5f;
    float j0 = (seed & 1) ? 1.10f : 0.90f;
    float j1 = (seed & 2) ? 0.88f : 1.08f;

    Vector3 base[6] = {
        { -hw,        0.0f, -hd * 0.65f },
        {  0.0f,      0.0f, -hd * 1.15f * j0 },
        {  hw,        0.0f, -hd * 0.65f },
        {  hw * 0.88f,0.0f,  hd * 0.72f },
        {  0.0f,      0.0f,  hd * 1.05f * j1 },
        { -hw * 0.88f,0.0f,  hd * 0.72f }
    };
    Vector3 apexF = {  0.06f * w, h,        -hd * 0.18f };
    Vector3 apexB = { -0.05f * w, h * 0.82f,  hd * 0.26f };

    rlBegin(RL_TRIANGLES);
    EmitRidgeTri(base[0], base[1], apexF);
    EmitRidgeTri(base[1], base[2], apexF);
    EmitRidgeTri(base[2], base[3], apexB);
    EmitRidgeTri(base[2], apexB,   apexF);
    EmitRidgeTri(base[3], base[4], apexB);
    EmitRidgeTri(base[4], base[5], apexB);
    EmitRidgeTri(base[5], base[0], apexF);
    EmitRidgeTri(base[5], apexF,   apexB);
    rlEnd();
}

static void KillDiaLietHandles(DiaLietInst *inst) {
    for (int k = 0; k < DLCB_WAVE_COUNT; k++) {
        if (inst->waveHandles[k] >= 0) {
            VFX_KillSeismicWave(inst->waveHandles[k]);
            inst->waveHandles[k] = -1;
        }
    }
}

static void AbortBtbcDiaLiet(int agentId) {
    for (int i = 0; i < MAX_DLCB_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) {
            KillDiaLietHandles(&s_inst[i]);
            s_inst[i].active = false;
        }
    }
}

static bool HasActiveBtbcDiaLiet(int agentId) {
    for (int i = 0; i < MAX_DLCB_INST; i++) {
        if (s_inst[i].active && s_inst[i].ownerAgentId == agentId) return true;
    }
    return false;
}

void InitBtbcDiaLietChanBaSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    for (int i = 0; i < MAX_DLCB_INST; i++) {
        s_inst[i].active = false;
        for (int k = 0; k < DLCB_WAVE_COUNT; k++) s_inst[i].waveHandles[k] = -1;
    }
    s_skillIndex = Skill_GetIndexByName("BTBC_DIA_LIET_CHAN_BA");
    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_DIA_LIET_CHAN_BA");
    if (s_skillIndex >= 0 && def) {
        RegisterSkillManaCost(s_skillIndex, def->mana);
        RegisterSkillCooldown(s_skillIndex, def->cooldown);
        RegisterSkillCastAnimSeconds(s_skillIndex, def->castSecs);
        RegisterSkillAbort(s_skillIndex, AbortBtbcDiaLiet);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveBtbcDiaLiet);
    }
}

void CastBtbcDiaLietChanBaSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_DLCB_INST; i++) {
        if (!s_inst[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;
    if (s_inst[slot].active) KillDiaLietHandles(&s_inst[slot]);

    const VoHocSkillDef *def = VoHoc_FindByName("BTBC_DIA_LIET_CHAN_BA");
    float baseDmg = def ? def->baseDamage : 18.0f;
    float pveMult = (params.level > 1) ? (1.0f + 0.18f * (float)(params.level - 1)) : 1.0f;
    float scale = (params.sizeScale > 0.1f) ? params.sizeScale : 1.0f;

    const Agent *owner = Entity_GetAgent(agentId);
    Vector3 anchor = owner ? owner->position : startPos;
    Vector3 dir = Vector3Subtract(target, anchor);
    dir.y = 0.0f;
    float d = Vector3Length(dir);
    dir = (d < 0.01f) ? (Vector3){ 0.0f, 0.0f, 1.0f } : Vector3Scale(dir, 1.0f / d);

    DiaLietInst *inst = &s_inst[slot];
    inst->active = true;
    inst->ownerAgentId = agentId;
    inst->casterTeam = owner ? owner->team : TEAM_ALLY;
    inst->origin = anchor;
    inst->dir = dir;
    inst->elapsed = 0.0f;
    inst->frontDist = 0.4f;
    inst->crestVfxAccum = 0.0f;
    inst->damage = baseDmg * pveMult;
    inst->sizeScale = scale;
    inst->deflectsRemaining = 1;
    inst->hitCount = 0;

    /* 5 fanned seismic waves across the 65° cone */
    Skill_RequestAnim(agentId, "hell_slammer_a", 0.35f);
    static const float fanAngles[DLCB_WAVE_COUNT] = {
        -26.0f * DEG2RAD, -13.0f * DEG2RAD, 0.0f, 13.0f * DEG2RAD, 26.0f * DEG2RAD
    };
    float range = DLCB_RANGE * scale;
    for (int k = 0; k < DLCB_WAVE_COUNT; k++) {
        float c = cosf(fanAngles[k]), s = sinf(fanAngles[k]);
        Vector3 wDir = { dir.x * c - dir.z * s, 0.0f, dir.x * s + dir.z * c };
        Vector3 wEnd = Vector3Add(anchor, Vector3Scale(wDir, range));
        VFX_SeismicWaveConfig cfg = VFX_SeismicWave_DefaultConfig();
        VFX_SeismicWave_ApplyPreset(&cfg, VFX_SEISMIC_PRESET_JAGGED_FRACTURE);
        cfg.travelSpeed = DLCB_SPEED;
        cfg.waveWidth = 1.85f * scale;
        cfg.waveAmplitude = 0.54f * scale;
        cfg.impactBurst = (k == 2);
        cfg.impactRadius = 2.2f * scale;
        cfg.veinGlow = 0.0f;
        inst->waveHandles[k] = VFX_ComposeSeismicWaveEx(anchor, wEnd, &cfg);
    }

    VFX_ComposeGroundDustRing(anchor, VC_MAT_EARTH, 1.05f * scale, 0.50f);
    Wind_SpawnRadialBlast(anchor, 2.0f * scale, 6.0f, 0.22f);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
    CameraFX_Shake(0.28f);
}

void UpdateBtbcDiaLietChanBaSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_DLCB_INST; i++) {
        DiaLietInst *inst = &s_inst[i];
        if (!inst->active) continue;

        inst->elapsed += dt;
        float maxR = DLCB_RANGE * inst->sizeScale;
        inst->frontDist = fminf(maxR, 0.4f + inst->elapsed * DLCB_SPEED);

        /* Advancing crest debris & dust bursts */
        inst->crestVfxAccum += dt;
        if (inst->crestVfxAccum >= 0.065f && inst->frontDist < maxR) {
            inst->crestVfxAccum = 0.0f;
            Vector3 crestPos = Vector3Add(inst->origin, Vector3Scale(inst->dir, inst->frontDist));
            crestPos.y = MapManager_GetGroundHeightAt(crestPos.x, crestPos.z);
            VFX_ComposeDebrisShards(crestPos, (Vector3){ inst->dir.x * 2.2f, 2.8f, inst->dir.z * 2.2f },
                                    VC_MAT_EARTH, 0.042f * inst->sizeScale, 4);
        }

        if (inst->deflectsRemaining > 0) {
            Vector3 crestPos = Vector3Add(inst->origin, Vector3Scale(inst->dir, inst->frontDist));
            float crestHalfW = fmaxf(1.2f, inst->frontDist * tanf(DLCB_HALF_CONE_RAD));
            CombatDeflectInfo defs[4];
            int broken = Combat_DeflectProjectilesInRadiusEx(crestPos, crestHalfW, inst->ownerAgentId,
                                                             inst->deflectsRemaining, defs, 4);
            if (broken > 0) {
                inst->deflectsRemaining = 0;
                for (int d = 0; d < broken; d++) {
                    VFX_ComposeDeflectFlash(defs[d].pos, inst->dir, VC_MAT_EARTH, 0.95f * inst->sizeScale);
                }
                PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
            }
        }

        int targets[16];
        int cnt = Entity_GetNearbyTargets(inst->origin, inst->frontDist + 0.6f, targets, 16);
        for (int k = 0; k < cnt; k++) {
            int tid = targets[k];
            if (tid == inst->ownerAgentId || AlreadyHit(inst, tid) || !Entity_CanDamageTarget(inst->ownerAgentId, tid)) continue;
            const Agent *tgt = Entity_GetAgent(tid);
            if (!tgt) continue;
            Vector3 toT = Vector3Subtract(tgt->position, inst->origin);
            toT.y = 0.0f;
            float dist = Vector3Length(toT);
            if (dist < 0.05f || dist > inst->frontDist + 0.4f) continue;
            Vector3 nDir = Vector3Scale(toT, 1.0f / dist);
            float cosA = Vector3DotProduct(inst->dir, nDir);
            if (cosA >= cosf(DLCB_HALF_CONE_RAD)) {
                if (inst->hitCount < MAX_DLCB_HIT) inst->hitIds[inst->hitCount++] = tid;
                Entity_ApplyDamageFrom(tid, inst->ownerAgentId, inst->damage, 0.0f, Vector3Scale(nDir, 4.5f));
                VoHoc_ApplyEarthMark(inst->ownerAgentId, tid, 1);
            }
        }

        if (inst->elapsed >= (maxR / DLCB_SPEED) + 0.35f) {
            KillDiaLietHandles(inst);
            inst->active = false;
        }
    }
}

void DrawBtbcDiaLietChanBaSkill(void) {
    EffectMaterial mat;
    Material_Get(&mat, MAT_ROCK);
    mat.params.baseColor = (Color){ 98, 86, 74, 255 }; // lint: allow-color
    mat.params.rimStrength = 0.72f;
    mat.params.fresnelPower = 2.6f;
    mat.params.emissiveIntensity = 0.0f;
    mat.vfxOutputEnabled = 0;

    bool began = false;
    for (int i = 0; i < MAX_DLCB_INST; i++) {
        const DiaLietInst *inst = &s_inst[i];
        if (!inst->active) continue;
        if (!began) { Material_Begin(mat); began = true; }

        float maxR = DLCB_RANGE * inst->sizeScale;
        float baseYaw = atan2f(inst->dir.x, inst->dir.z) * RAD2DEG;

        for (int r = 0; r < 5; r++) {
            float ringDist = 1.0f + (float)r * (maxR - 1.2f) / 4.0f;
            float delta = fabsf(inst->frontDist - ringDist);
            if (delta > 1.30f) continue;
            float phase = cosf((delta / 1.30f) * (PI * 0.5f));
            float h = 0.54f * phase * inst->sizeScale; /* strictly <= 0.6m */
            if (h <= 0.04f) continue;

            int spurs = 3 + r;
            for (int s = 0; s < spurs; s++) {
                float frac = (spurs > 1) ? ((float)s / (float)(spurs - 1) - 0.5f) : 0.0f;
                float jitYaw = ((s + r) & 1) ? 14.0f : -12.0f;
                float ang = frac * (2.0f * DLCB_HALF_CONE_RAD);
                float c = cosf(ang), sn = sinf(ang);
                Vector3 wDir = { inst->dir.x * c - inst->dir.z * sn, 0.0f, inst->dir.x * sn + inst->dir.z * c };
                Vector3 rp = Vector3Add(inst->origin, Vector3Scale(wDir, ringDist));
                rp.y = MapManager_GetGroundHeightAt(rp.x, rp.z) - 0.02f;

                rlPushMatrix();
                rlTranslatef(rp.x, rp.y, rp.z);
                rlRotatef(baseYaw + ang * RAD2DEG + jitYaw, 0.0f, 1.0f, 0.0f);
                DrawSculptedBasaltRidge(0.60f * inst->sizeScale, h, 0.42f * inst->sizeScale, r * 7 + s);
                rlPopMatrix();
            }
        }
    }
    if (began) Material_End();
}

void UnloadBtbcDiaLietChanBaSkill(void) {
    for (int i = 0; i < MAX_DLCB_INST; i++) {
        if (s_inst[i].active) KillDiaLietHandles(&s_inst[i]);
        s_inst[i].active = false;
    }
}
