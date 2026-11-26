#include "skills/earth/earth_pillar_prison_skill/earth_pillar_prison_skill.h"
#include "core/composition/visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/skill_boilerplate.h"
#include "entities/entities.h"
#include "raymath.h"
#include "skills/skill_helper.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_PILLAR_PRISONS       4
#define PILLAR_PRISON_CAST_SECS  0.48f
#define PILLAR_PRISON_MAX_RANGE  10.5f
#define PILLAR_PRISON_TRAVEL_SPD 19.0f
#define PILLAR_PRISON_HOLD_SECS  1.75f
#define PILLAR_PRISON_EXIT_SECS  0.40f
#define PILLAR_PRISON_MANA_COST  28.0f
#define PILLAR_TRAIL_COUNT       3

typedef enum {
    PRISON_IDLE = 0,
    PRISON_TRAVEL,
    PRISON_HOLD,
    PRISON_EXIT
} PillarPrisonState;

typedef struct {
    bool active;
    PillarPrisonState state;
    int ownerAgentId;
    int casterTeam;
    Vector3 startPos;
    Vector3 pos;
    Vector3 dir;
    Vector3 perp;
    float travelDist;
    float traveled;
    float dustAccum;
    float timer;
    float sizeScale;
    float radius;
    float damage;
    float knockback;
    bool dealtDamage;
    int spikeHandle;
    int trailHandles[PILLAR_TRAIL_COUNT];
    Matrix trailMats[PILLAR_TRAIL_COUNT];
} PillarPrisonInstance;

static PillarPrisonInstance s_prisons[MAX_PILLAR_PRISONS];
static int s_skillIndex = -1;

static void StopPrisonTrails(PillarPrisonInstance *p) {
    for (int k = 0; k < PILLAR_TRAIL_COUNT; k++) {
        if (p->trailHandles[k] >= 0) {
            VFX_TrailSetWidth(p->trailHandles[k], 0.0f);
            p->trailHandles[k] = -1;
        }
    }
}

static void KillPrisonSpike(PillarPrisonInstance *p) {
    if (p->spikeHandle >= 0) {
        VFX_KillEarthSpike(p->spikeHandle);
        p->spikeHandle = -1;
    }
}

static void AbortEarthPillarPrison(int agentId) {
    for (int i = 0; i < MAX_PILLAR_PRISONS; i++) {
        if (s_prisons[i].active && s_prisons[i].ownerAgentId == agentId) {
            StopPrisonTrails(&s_prisons[i]);
            KillPrisonSpike(&s_prisons[i]);
            s_prisons[i].active = false;
            s_prisons[i].state = PRISON_IDLE;
        }
    }
}

static bool HasActiveEarthPillarPrison(int agentId) {
    for (int i = 0; i < MAX_PILLAR_PRISONS; i++) {
        if (s_prisons[i].active && s_prisons[i].ownerAgentId == agentId) {
            return true;
        }
    }
    return false;
}

void InitEarthPillarPrisonSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;

    for (int i = 0; i < MAX_PILLAR_PRISONS; i++) {
        s_prisons[i].active = false;
        s_prisons[i].state = PRISON_IDLE;
        s_prisons[i].spikeHandle = -1;
        for (int k = 0; k < PILLAR_TRAIL_COUNT; k++) {
            s_prisons[i].trailHandles[k] = -1;
        }
    }

    s_skillIndex = Skill_GetIndexByName("EARTH_PILLAR_PRISON");
    if (s_skillIndex >= 0) {
        RegisterSkillManaCost(s_skillIndex, PILLAR_PRISON_MANA_COST);
        RegisterSkillCastAnimSeconds(s_skillIndex, PILLAR_PRISON_CAST_SECS);
        RegisterSkillAbort(s_skillIndex, AbortEarthPillarPrison);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveEarthPillarPrison);
    }
}

void CastEarthPillarPrisonSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_PILLAR_PRISONS; i++) {
        if (!s_prisons[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        StopPrisonTrails(&s_prisons[0]);
        KillPrisonSpike(&s_prisons[0]);
    }

    float scale = Clamp(params.sizeScale > 0.05f ? params.sizeScale : 1.0f, 0.4f, 2.5f);
    Vector3 delta = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    float dist = Vector3Length(delta);
    Vector3 dir = (dist > 0.01f) ? Vector3Scale(delta, 1.0f / dist) : (Vector3){ 0.0f, 0.0f, 1.0f };
    if (dist < 1.5f) dist = 1.5f;
    if (dist > PILLAR_PRISON_MAX_RANGE) dist = PILLAR_PRISON_MAX_RANGE;

    Vector3 groundStart = {
        startPos.x,
        VFX_GroundHeightFromMap(startPos.x, startPos.z, NULL) + 0.12f,
        startPos.z
    };
    Vector3 groundTarget = {
        startPos.x + dir.x * dist,
        VFX_GroundHeightFromMap(startPos.x + dir.x * dist, startPos.z + dir.z * dist, NULL),
        startPos.z + dir.z * dist
    };

    const Agent *caster = Entity_GetAgent(agentId);
    PillarPrisonInstance *p = &s_prisons[slot];
    *p = (PillarPrisonInstance){0};
    p->active = true;
    p->state = PRISON_TRAVEL;
    p->ownerAgentId = agentId;
    p->casterTeam = (caster != NULL) ? caster->team : 0;
    p->startPos = groundStart;
    p->pos = groundTarget;
    p->dir = dir;
    p->perp = (Vector3){ -dir.z, 0.0f, dir.x };
    p->travelDist = dist;
    p->traveled = 0.0f;
    p->timer = 0.0f;
    p->sizeScale = scale;
    p->radius = 2.25f * scale;
    p->spikeHandle = -1;

    p->damage = 46.0f * scale * (1.0f + 0.12f * (float)(params.level > 0 ? params.level - 1 : 0));
    p->knockback = 0.0f;

    Skill_RequestAnim(agentId, "hell_slammer_a", PILLAR_PRISON_CAST_SECS);

    // Seismic wave + 3 ground-hugging earth trails racing from caster's stomp to target
VFX_ComposeImpactDust(groundStart, VC_MAT_EARTH, 1.15f * scale, 0.65f);
    for (int k = 0; k < PILLAR_TRAIL_COUNT; k++) {
        p->trailMats[k] = MatrixTranslate(groundStart.x, groundStart.y, groundStart.z);
        p->trailHandles[k] = VFX_ComposeTrail(&p->trailMats[k], VC_MAT_EARTH,
                                              0.16f * scale, 0.25f, TRAIL_PRESET_ENERGY);
        if (p->trailHandles[k] >= 0) {
            VFX_TrailSetHdrGain(p->trailHandles[k], 1.45f);
        }
    }
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}

void UpdateEarthPillarPrisonSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_PILLAR_PRISONS; i++) {
        PillarPrisonInstance *p = &s_prisons[i];
        if (!p->active) continue;

        p->timer += dt;
        if (p->state == PRISON_TRAVEL) {
            p->traveled += PILLAR_PRISON_TRAVEL_SPD * dt;
            float t = Clamp(p->traveled / p->travelDist, 0.0f, 1.0f);

            Vector3 center = Vector3Lerp(p->startPos, p->pos, t);
            float spread = sinf(t * PI) * 0.95f * p->sizeScale;
            for (int k = 0; k < PILLAR_TRAIL_COUNT; k++) {
                float lat = (float)(k - 1) * spread;
                float px = center.x + p->perp.x * lat;
                float pz = center.z + p->perp.z * lat;
                float py = VFX_GroundHeightFromMap(px, pz, NULL) + 0.16f;
                p->trailMats[k] = MatrixTranslate(px, py, pz);
            }

            p->dustAccum += dt;
            if (p->dustAccum >= 0.07f) {
                p->dustAccum = 0.0f;
                VFX_ComposeImpactDust(center, VC_MAT_EARTH, 0.70f * p->sizeScale, 0.45f);
            }

            if (t >= 1.0f) {
                StopPrisonTrails(p);
                p->state = PRISON_HOLD;
                p->timer = 0.0f;

                VFX_EarthSpikeConfig cfg = VFX_EarthSpike_DefaultConfig();
                VFX_EarthSpike_ApplyPreset(&cfg, VFX_EARTH_SPIKE_PRESET_EARTH_PRISON);
                cfg.shape = VFX_EARTH_SPIKE_SHAPE_PILLAR;
                cfg.style = VFX_EARTH_SPIKE_STYLE_GRANITE;
                cfg.formation = VFX_EARTH_SPIKE_FORMATION_RING;
                cfg.exitMode = VFX_EARTH_SPIKE_EXIT_SINK;
                cfg.spikeCount = 7;
                cfg.formationRadius = 2.0f * p->sizeScale;
                cfg.height = 2.35f * p->sizeScale;
                cfg.baseRadius = 0.34f * p->sizeScale;
                cfg.topRadiusRatio = 0.72f;
                cfg.eruptDuration = 0.18f;
                cfg.holdDuration = PILLAR_PRISON_HOLD_SECS;
                cfg.retractDuration = PILLAR_PRISON_EXIT_SECS;
                cfg.baseColor = ELEMENT_COLOR_EARTH;
                cfg.accentColor = (Color){ 245, 195, 110, 255 };
                cfg.emitDust = true;
                cfg.dustScale = 1.2f * p->sizeScale;
                cfg.emitDebris = true;
                cfg.debrisChips = 18;
                cfg.spawnGroundCrack = true;
                cfg.groundWaveBurst = true;
                cfg.screenShake = false;
                cfg.traumaAmount = 0.0f;
                p->spikeHandle = VFX_ComposeEarthSpikeEx(p->pos, p->pos, &cfg);

                PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
            }
        } else if (p->state == PRISON_HOLD) {
            int hitIds[8];
            int hits = Entity_GetNearbyTargets(p->pos, p->radius, hitIds, 8);
            bool anyEnemy = false;
            for (int h = 0; h < hits; h++) {
                if (Entity_CanDamageTarget(p->ownerAgentId, hitIds[h])) {
                    Entity_ApplyStun(hitIds[h], 0.16f);
                    anyEnemy = true;
                }
            }
            if (!p->dealtDamage && p->timer >= 0.15f && anyEnemy) {
                p->dealtDamage = true;
                Entity_ApplyAoEDamageFrom(p->ownerAgentId, p->pos, p->radius, p->damage, p->knockback);
            }
            if (p->timer >= PILLAR_PRISON_HOLD_SECS) {
                p->state = PRISON_EXIT;
                p->timer = 0.0f;
            }
        } else if (p->state == PRISON_EXIT) {
            if (p->timer >= PILLAR_PRISON_EXIT_SECS) {
                p->active = false;
                p->state = PRISON_IDLE;
                p->spikeHandle = -1;
            }
        }
    }
}

void DrawEarthPillarPrisonSkill(void) {
    // Visuals are rendered by VC_Trail + VC_EarthSpike (Earth Pillar Prison).
}

void UnloadEarthPillarPrisonSkill(void) {
    for (int i = 0; i < MAX_PILLAR_PRISONS; i++) {
        StopPrisonTrails(&s_prisons[i]);
        KillPrisonSpike(&s_prisons[i]);
        s_prisons[i].active = false;
        s_prisons[i].state = PRISON_IDLE;
    }
}

SKILL_EMPTY_PROJECTILE_API(EarthPillarPrison)
