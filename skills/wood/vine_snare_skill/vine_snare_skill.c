#include "skills/wood/vine_snare_skill/vine_snare_skill.h"
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

#define MAX_VINE_SNARES    6
#define VINE_CAST_SECS     0.45f
#define VINE_MAX_RANGE     11.0f
#define VINE_FLIGHT_SPEED  18.0f
#define VINE_ERUPT_SECS    0.38f
#define VINE_HOLD_SECS     1.60f
#define VINE_WITHER_SECS   0.45f
#define VINE_MANA_COST     20.0f
#define VINE_TRAIL_COUNT   3

typedef enum {
    VINE_IDLE = 0,
    VINE_FLIGHT,
    VINE_ERUPT,
    VINE_HOLD,
    VINE_WITHER
} VineState;

typedef struct {
    bool active;
    VineState state;
    int ownerAgentId;
    int casterTeam;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 dir;
    Vector3 perp;
    float flightDist;
    float flightTraveled;
    float timer;
    float growth;
    float wither;
    float sapPhase;
    float radius;
    float sizeScale;
    float damage;
    float knockback;
    bool dealtDamage;
    bool spawnedLeaves;
    unsigned int seed;
    int trailHandles[VINE_TRAIL_COUNT];
    Matrix trailMats[VINE_TRAIL_COUNT];
    Vector3 landingPts[VINE_TRAIL_COUNT];
} VineSnareInstance;

static VineSnareInstance s_vines[MAX_VINE_SNARES];
static int s_skillIndex = -1;

static void StopVineTrails(VineSnareInstance *v) {
    for (int k = 0; k < VINE_TRAIL_COUNT; k++) {
        if (v->trailHandles[k] >= 0) {
            VFX_TrailSetWidth(v->trailHandles[k], 0.0f);
            v->trailHandles[k] = -1;
        }
    }
}

static void AbortVineSnare(int agentId) {
    for (int i = 0; i < MAX_VINE_SNARES; i++) {
        if (s_vines[i].active && s_vines[i].ownerAgentId == agentId) {
            StopVineTrails(&s_vines[i]);
            s_vines[i].active = false;
            s_vines[i].state = VINE_IDLE;
        }
    }
}

static bool HasActiveVineSnare(int agentId) {
    for (int i = 0; i < MAX_VINE_SNARES; i++) {
        if (s_vines[i].active && s_vines[i].ownerAgentId == agentId) {
            return true;
        }
    }
    return false;
}

void InitVineSnareSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;

    for (int i = 0; i < MAX_VINE_SNARES; i++) {
        s_vines[i].active = false;
        s_vines[i].state = VINE_IDLE;
        for (int k = 0; k < VINE_TRAIL_COUNT; k++) {
            s_vines[i].trailHandles[k] = -1;
        }
    }

    s_skillIndex = Skill_GetIndexByName("VINE_SNARE");
    if (s_skillIndex >= 0) {
        RegisterSkillManaCost(s_skillIndex, VINE_MANA_COST);
        RegisterSkillCastAnimSeconds(s_skillIndex, VINE_CAST_SECS);
        RegisterSkillAbort(s_skillIndex, AbortVineSnare);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveVineSnare);
    }
}

void CastVineSnareSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_VINE_SNARES; i++) {
        if (!s_vines[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        StopVineTrails(&s_vines[0]);
    }

    float scale = Clamp(params.sizeScale > 0.05f ? params.sizeScale : 1.0f, 0.4f, 2.5f);
    Vector3 launchPos = { startPos.x, startPos.y + 0.90f, startPos.z };
    Vector3 delta = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    float dist = Vector3Length(delta);
    Vector3 dir = (dist > 0.01f) ? Vector3Scale(delta, 1.0f / dist) : (Vector3){ 0.0f, 0.0f, 1.0f };
    if (dist < 1.5f) dist = 1.5f;
    if (dist > VINE_MAX_RANGE) dist = VINE_MAX_RANGE;

    Vector3 groundTarget = {
        startPos.x + dir.x * dist,
        VFX_GroundHeightFromMap(startPos.x + dir.x * dist, startPos.z + dir.z * dist, NULL),
        startPos.z + dir.z * dist
    };

    const Agent *caster = Entity_GetAgent(agentId);
    VineSnareInstance *v = &s_vines[slot];
    *v = (VineSnareInstance){0};
    v->active = true;
    v->state = VINE_FLIGHT;
    v->ownerAgentId = agentId;
    v->casterTeam = (caster != NULL) ? caster->team : 0;
    v->startPos = launchPos;
    v->targetPos = groundTarget;
    v->dir = dir;
    v->perp = (Vector3){ -dir.z, 0.0f, dir.x };
    v->flightDist = Vector3Distance(launchPos, groundTarget);
    if (v->flightDist < 0.5f) v->flightDist = 0.5f;
    v->flightTraveled = 0.0f;
    v->timer = 0.0f;
    v->growth = 0.0f;
    v->wither = 0.0f;
    v->sapPhase = 0.0f;
    v->radius = 2.4f * scale;
    v->sizeScale = scale;
    v->seed = (unsigned int)GetRandomValue(1, 999999);

    v->damage = 32.0f * scale * (1.0f + 0.12f * (float)(params.level > 0 ? params.level - 1 : 0));
    v->knockback = 0.0f;

    Skill_RequestAnim(agentId, "inside_crescent_kick", VINE_CAST_SECS);

    // Spawn 3 wood-element trails that spiral/arc from the caster to the target ground
    for (int k = 0; k < VINE_TRAIL_COUNT; k++) {
        float ang = (float)k * (2.0f * PI / (float)VINE_TRAIL_COUNT) + (float)(v->seed % 360) * DEG2RAD;
        float rOut = 1.45f * scale;
        float lx = groundTarget.x + cosf(ang) * rOut;
        float lz = groundTarget.z + sinf(ang) * rOut;
        v->landingPts[k] = (Vector3){ lx, VFX_GroundHeightFromMap(lx, lz, NULL), lz };

        v->trailMats[k] = MatrixTranslate(launchPos.x, launchPos.y, launchPos.z);
        v->trailHandles[k] = VFX_ComposeTrail(&v->trailMats[k], VC_MAT_WOOD,
                                              0.14f * scale, 0.28f, TRAIL_PRESET_ENERGY);
        if (v->trailHandles[k] >= 0) {
            VFX_TrailSetHdrGain(v->trailHandles[k], 1.55f);
        }
    }

    PlayCastSound(EFFECT_PRESET_WOOD_BLOOM);
}

void UpdateVineSnareSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    (void)enemyPos;
    (void)enemyRadius;

    for (int i = 0; i < MAX_VINE_SNARES; i++) {
        VineSnareInstance *v = &s_vines[i];
        if (!v->active) continue;

        v->timer += dt;
        v->sapPhase += dt * 4.2f;

        if (v->state == VINE_FLIGHT) {
            v->flightTraveled += VINE_FLIGHT_SPEED * dt;
            float t = Clamp(v->flightTraveled / v->flightDist, 0.0f, 1.0f);

            // Advance 3 helical/curved trails from caster toward landing points
            for (int k = 0; k < VINE_TRAIL_COUNT; k++) {
                float phase = (float)k * (2.0f * PI / (float)VINE_TRAIL_COUNT);
                float env = sinf(t * PI);
                float spiralX = cosf(t * PI * 3.0f + phase) * 0.75f * v->sizeScale * env;
                float arcY = sinf(t * PI) * (1.25f + 0.35f * sinf(phase)) * v->sizeScale;

                Vector3 p = Vector3Lerp(v->startPos, v->landingPts[k], t);
                p.x += v->perp.x * spiralX;
                p.z += v->perp.z * spiralX;
                p.y += arcY;
                v->trailMats[k] = MatrixTranslate(p.x, p.y, p.z);
            }

            if (t >= 1.0f) {
                StopVineTrails(v);
                v->state = VINE_ERUPT;
                v->timer = 0.0f;
                v->growth = 0.08f;

                VFX_ComposeGroundWave((Vector3){ v->targetPos.x, v->targetPos.y + 0.05f, v->targetPos.z },
                                      VC_MAT_WOOD, v->radius, 0.70f, VFX_GroundHeightFromMap, NULL);
                VFX_ComposeImpactDust(v->targetPos, VC_MAT_WOOD, 1.05f * v->sizeScale, 0.60f);
                VFX_ComposeContactSpark((Vector3){ v->targetPos.x, v->targetPos.y + 0.35f, v->targetPos.z },
                                        VC_MAT_WOOD, 1.15f * v->sizeScale, 0.75f);
                PlayImpactSound(EFFECT_PRESET_WOOD_BLOOM);
            }
        } else if (v->state == VINE_ERUPT) {
            float t = Clamp(v->timer / VINE_ERUPT_SECS, 0.0f, 1.0f);
            v->growth = 0.08f + 0.92f * t;
            if (v->timer >= VINE_ERUPT_SECS) {
                v->state = VINE_HOLD;
                v->timer = 0.0f;
                v->growth = 1.0f;
            }
        } else if (v->state == VINE_HOLD) {
            int hitIds[8];
            int hits = Entity_GetNearbyTargets(v->targetPos, v->radius, hitIds, 8);
            bool anyEnemy = false;
            for (int h = 0; h < hits; h++) {
                if (Entity_CanDamageTarget(v->ownerAgentId, hitIds[h])) {
                    Entity_ApplyPull(hitIds[h], v->targetPos, 5.5f, 0.12f);
                    Entity_ApplyStun(hitIds[h], 0.16f);
                    anyEnemy = true;
                }
            }
            if (!v->dealtDamage && anyEnemy) {
                v->dealtDamage = true;
                Entity_ApplyAoEDamage(v->targetPos, v->radius, v->damage, v->knockback, v->casterTeam);
                PlayImpactSound(EFFECT_PRESET_WOOD_BLOOM);
            }
            if (v->timer >= VINE_HOLD_SECS) {
                v->state = VINE_WITHER;
                v->timer = 0.0f;
                if (!v->spawnedLeaves) {
                    v->spawnedLeaves = true;
                    VFX_Foliage_SpawnFreeLeaves((Vector3){ v->targetPos.x, v->targetPos.y + 0.9f, v->targetPos.z },
                                                v->radius * 0.75f, 48, 0.0035f, WOOD_VINE_STYLE_JADE_EMERALD);
                }
            }
        } else if (v->state == VINE_WITHER) {
            v->wither = Clamp(v->timer / VINE_WITHER_SECS, 0.0f, 1.0f);
            if (v->timer >= VINE_WITHER_SECS) {
                v->active = false;
                v->state = VINE_IDLE;
            }
        }
    }
}

void DrawVineSnareSkill(void) {
    for (int i = 0; i < MAX_VINE_SNARES; i++) {
        const VineSnareInstance *v = &s_vines[i];
        if (!v->active || v->state == VINE_FLIGHT) continue;

        // 3 thorny jade vines erupting from the 3 trail landing points toward the target
        for (int k = 0; k < VINE_TRAIL_COUNT; k++) {
            VFX_WoodVineConfig cfg = VFX_WoodVine_DefaultConfig();
            cfg.variant = (k == 0) ? WOOD_VINE_VARIANT_ENTANGLE
                                   : ((k == 1) ? WOOD_VINE_VARIANT_SPIKE_SPEAR : WOOD_VINE_VARIANT_SERPENTINE);
            cfg.style = WOOD_VINE_STYLE_JADE_EMERALD;
            cfg.combo = (k == 0) ? WOOD_VINE_COMBO_FULL_FLOURISH : WOOD_VINE_COMBO_LEAFY_TENDRIL;
            cfg.startPos = (k == 0) ? v->targetPos : v->landingPts[k];
            cfg.targetPos = (Vector3){ v->targetPos.x, v->targetPos.y + (k == 0 ? 1.85f : 1.25f) * v->sizeScale, v->targetPos.z };
            if (k == 0) {
                cfg.targetRadius = 0.45f * v->sizeScale;
                cfg.targetHeight = 1.85f * v->sizeScale;
                cfg.coilTurns = 2.4f;
                cfg.enableTwin = true;
                cfg.enableFlowers = true;
            }
            cfg.baseRadius = (k == 0 ? 0.075f : 0.060f) * v->sizeScale;
            cfg.growth = v->growth;
            cfg.wither = v->wither;
            cfg.sapPhase = v->sapPhase + (float)k * 1.1f;
            cfg.enableThorns = true;
            cfg.enableLeaves = true;
            cfg.castShadow = true;
            cfg.seed = v->seed + (unsigned int)(k * 97 + 13);
            VFX_ComposeWoodVine(&cfg);
        }

        if (v->state == VINE_ERUPT) {
            VFX_ComposeWoodVineSeedSprout(v->targetPos, v->growth, v->seed, WOOD_VINE_STYLE_JADE_EMERALD);
        }
    }
}

void UnloadVineSnareSkill(void) {
    for (int i = 0; i < MAX_VINE_SNARES; i++) {
        StopVineTrails(&s_vines[i]);
        s_vines[i].active = false;
        s_vines[i].state = VINE_IDLE;
    }
}

SKILL_EMPTY_PROJECTILE_API(VineSnare)
