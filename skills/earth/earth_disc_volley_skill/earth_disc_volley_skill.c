#include "skills/earth/earth_disc_volley_skill/earth_disc_volley_skill.h"

#include "core/composition/visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "entities/entities.h"
#include "raymath.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_EDV_INSTANCES 6
#define MAX_EDV_DISCS     6

#define EDV_STOMP_RISE_TIME  0.42f
#define EDV_SLICE_INTERVAL   0.24f
#define EDV_SINK_TIME        0.30f
#define EDV_MAX_DISC_RANGE   11.5f
#define EDV_DISC_SPEED       16.5f
#define EDV_DISC_HIT_RADIUS  0.65f

typedef enum {
    EDV_IDLE = 0,
    EDV_STOMP_RISE,
    EDV_SLICE_BARRAGE,
    EDV_SINK_EXIT
} EdvState;

typedef struct {
    bool    active;
    Vector3 pos;
    Vector3 vel;
    float   distTraveled;
    float   maxRange;
    float   radius;
} EdvDisc;

typedef struct {
    bool     active;
    int      ownerAgentId;
    int      casterTeam;
    EdvState state;
    float    timer;
    float    sliceTimer;

    Vector3  casterPos;
    Vector3  forward;
    Vector3  pillarPos;
    float    pillarMaxHeight;
    float    currentHeight;
    float    pillarRadius;
    float    sizeScale;
    int      spikeHandle;

    int      totalSlices;
    int      slicesFired;
    float    damagePerDisc;
    float    knockback;

    EdvDisc  discs[MAX_EDV_DISCS];
} EdvInstance;

static EdvInstance s_instances[MAX_EDV_INSTANCES];
static int         s_skillIndex = -1;

static Vector3 GroundAt(float x, float z) {
    float y = VFX_GroundHeightFromMap(x, z, NULL);
    return (Vector3){ x, y, z };
}

static int SpawnStonePillarStep(Vector3 pillarPos, float height, float radius,
                                float eruptDuration, bool withBurstVfx) {
    VFX_EarthSpikeConfig cfg = VFX_EarthSpike_DefaultConfig();
    VFX_EarthSpike_ApplyPreset(&cfg, VFX_EARTH_SPIKE_PRESET_STONE_PILLAR);
    cfg.shape             = VFX_EARTH_SPIKE_SHAPE_PILLAR;
    cfg.style             = VFX_EARTH_SPIKE_STYLE_SOIL;
    cfg.formation         = VFX_EARTH_SPIKE_FORMATION_SINGLE;
    cfg.exitMode          = VFX_EARTH_SPIKE_EXIT_SINK;
    cfg.spikeCount        = 1;
    cfg.height            = height;
    cfg.baseRadius        = radius;
    cfg.topRadiusRatio    = 0.88f;
    cfg.facetCount        = 7;
    cfg.heightSegments    = 5;
    cfg.jaggedness        = 0.18f;
    cfg.tiltMaxDeg        = 3.0f;
    cfg.eruptDuration     = eruptDuration;
    cfg.holdDuration      = 3.0f;
    cfg.retractDuration   = 0.28f;
    cfg.yOvershoot        = withBurstVfx ? 0.08f : 0.0f;
    cfg.baseColor         = ELEMENT_COLOR_EARTH;
    cfg.accentColor       = (Color){ 245, 195, 110, 255 };
    cfg.ambientGain       = 0.35f;
    cfg.specularGain      = 1.20f;
    cfg.emissionIntensity = 0.70f;
    cfg.emitDust          = withBurstVfx;
    cfg.dustScale         = 1.15f;
    cfg.emitDebris        = withBurstVfx;
    cfg.debrisChips       = withBurstVfx ? 14 : 0;
    cfg.spawnGroundCrack  = withBurstVfx;
    cfg.groundWaveBurst   = withBurstVfx;
    cfg.screenShake       = withBurstVfx;
    cfg.traumaAmount      = withBurstVfx ? 0.22f : 0.0f;
    cfg.windImpulse       = withBurstVfx;
    cfg.windRadius        = 2.2f;
    return VFX_ComposeEarthSpikeEx(pillarPos, pillarPos, &cfg);
}

static void KillInstanceSpike(EdvInstance *inst) {
    if (inst->spikeHandle >= 0) {
        VFX_KillEarthSpike(inst->spikeHandle);
        inst->spikeHandle = -1;
    }
}

static void AbortEarthDiscVolley(int agentId) {
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].ownerAgentId == agentId) {
            KillInstanceSpike(&s_instances[i]);
            s_instances[i].active = false;
            s_instances[i].state = EDV_IDLE;
        }
    }
}

static bool HasActiveEarthDiscVolley(int agentId) {
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].ownerAgentId == agentId) {
            return true;
        }
    }
    return false;
}

static void SpawnEarthDisc(EdvInstance *inst, Vector3 sliceOrigin, int sliceIdx) {
    float tNorm = (inst->totalSlices > 1)
                      ? ((float)sliceIdx / (float)(inst->totalSlices - 1)) - 0.5f
                      : 0.0f;
    float jitterDeg = (float)GetRandomValue(-35, 35) * 0.1f;
    float spreadRad = (tNorm * 13.0f + jitterDeg) * DEG2RAD;

    float cs = cosf(spreadRad);
    float sn = sinf(spreadRad);
    Vector3 dir = {
        inst->forward.x * cs - inst->forward.z * sn,
        0.0f,
        inst->forward.x * sn + inst->forward.z * cs
    };
    dir = Vector3Normalize(dir);

    float range = EDV_MAX_DISC_RANGE * (0.90f + (float)GetRandomValue(0, 15) * 0.01f);
    Vector3 discTarget = {
        sliceOrigin.x + dir.x * range,
        VFX_GroundHeightFromMap(sliceOrigin.x + dir.x * range,
                                sliceOrigin.z + dir.z * range, NULL) + 0.35f,
        sliceOrigin.z + dir.z * range
    };

    VFX_GuidedEarthConfig cfg = VFX_GuidedEarth_DefaultConfig();
    cfg.preset            = VFX_EARTH_PRESET_CUSTOM;
    cfg.style             = VFX_EARTH_STYLE_JAGGED_SLATE;
    cfg.spawnMode         = VFX_EARTH_SPAWN_INSTANT_LAUNCH;
    cfg.trajectoryMode    = VFX_EARTH_TRAJ_PARABOLIC_ARC;
    cfg.impactMode        = VFX_EARTH_IMPACT_SHATTER;
    cfg.rockCount         = 1;
    cfg.rockRadius        = 0.42f * inst->sizeScale;
    cfg.rockSquashY       = 0.20f;
    cfg.rockSquashZ       = 1.05f;
    cfg.jaggedness        = 0.32f;
    cfg.clusterSpread     = 0.0f;
    cfg.sizeVariation     = 0.0f;
    cfg.emergeDuration    = 0.0f;
    cfg.hoverDuration     = 0.0f;
    cfg.travelSpeed       = EDV_DISC_SPEED;
    cfg.arcHeight         = 0.18f;
    cfg.arcVariation      = 0.0f;
    cfg.trajectorySpread  = 0.0f;
    cfg.targetSpread      = 0.0f;
    cfg.gravityScale      = 0.25f;
    cfg.tumbleSpeed       = 520.0f;
    cfg.emitDustTrail     = true;
    cfg.dustRate          = 18.0f;
    cfg.emitTrailRibbon   = true;
    cfg.ribbonWidth       = 0.22f * inst->sizeScale;
    cfg.shatterChips      = 10;
    cfg.shatterVelocity   = 4.5f;
    cfg.impactDustRadius  = 0.85f * inst->sizeScale;
    cfg.impactShake       = 0.14f;
    cfg.impactCraterCrack = false;
    cfg.impactShockRing   = false;
    cfg.matId             = VC_MAT_EARTH;
    cfg.rockBaseColor     = ELEMENT_COLOR_EARTH;
    cfg.veinColor         = (Color){ 245, 195, 110, 255 };
    cfg.veinIntensity     = 0.70f;
    cfg.ambientGain       = 0.35f;
    cfg.specularGain      = 1.20f;
    VFX_ComposeGuidedEarthEx(sliceOrigin, discTarget, &cfg);

    for (int d = 0; d < MAX_EDV_DISCS; d++) {
        if (!inst->discs[d].active) {
            Vector3 toTarget = Vector3Subtract(discTarget, sliceOrigin);
            float dist = Vector3Length(toTarget);
            Vector3 vel = (dist > 0.001f)
                              ? Vector3Scale(Vector3Normalize(toTarget), EDV_DISC_SPEED)
                              : Vector3Scale(dir, EDV_DISC_SPEED);
            inst->discs[d].active       = true;
            inst->discs[d].pos          = sliceOrigin;
            inst->discs[d].vel          = vel;
            inst->discs[d].distTraveled = 0.0f;
            inst->discs[d].maxRange     = range;
            inst->discs[d].radius       = EDV_DISC_HIT_RADIUS * inst->sizeScale;
            break;
        }
    }

    Vector3 shardVel = { dir.x * 2.6f, 2.2f, dir.z * 2.6f };
    VFX_ComposeDebrisShards(sliceOrigin, shardVel, VC_MAT_EARTH, 0.20f * inst->sizeScale, 10);
    VFX_ComposeImpactDust(sliceOrigin, VC_MAT_EARTH, 0.90f * inst->sizeScale, 0.55f);
    PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
}

void InitEarthDiscVolleySkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;

    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        s_instances[i].active = false;
        s_instances[i].state = EDV_IDLE;
        s_instances[i].spikeHandle = -1;
    }

    s_skillIndex = Skill_GetIndexByName("EARTH_DISC_VOLLEY");
    if (s_skillIndex >= 0) {
        RegisterSkillCastAnimSeconds(s_skillIndex, EDV_STOMP_RISE_TIME);
        RegisterSkillAbort(s_skillIndex, AbortEarthDiscVolley);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveEarthDiscVolley);
    }
}

void CastEarthDiscVolleySkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        if (!s_instances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        KillInstanceSpike(&s_instances[slot]);
    }

    float scale = (params.sizeScale > 0.05f) ? params.sizeScale : 1.0f;
    Vector3 fwd = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    if (Vector3LengthSqr(fwd) < 0.0001f) {
        fwd = (Vector3){ 0.0f, 0.0f, 1.0f };
    } else {
        fwd = Vector3Normalize(fwd);
    }

    EdvInstance *inst = &s_instances[slot];
    *inst = (EdvInstance){0};
    inst->active          = true;
    inst->ownerAgentId    = agentId;
    const Agent *caster   = Entity_GetAgent(agentId);
    inst->casterTeam      = (caster != NULL) ? caster->team : 0;
    inst->state           = EDV_STOMP_RISE;
    inst->timer           = 0.0f;
    inst->sliceTimer      = 0.0f;
    inst->casterPos       = startPos;
    inst->forward         = fwd;
    inst->pillarPos       = GroundAt(startPos.x + fwd.x * 1.35f, startPos.z + fwd.z * 1.35f);
    inst->sizeScale       = scale;
    inst->pillarMaxHeight = 1.55f * scale;
    inst->currentHeight   = inst->pillarMaxHeight;
    inst->pillarRadius    = 0.42f * scale;
    inst->spikeHandle     = -1;
    inst->totalSlices     = GetRandomValue(4, 6);
    inst->slicesFired     = 0;

    float totalDmg        = Skill_CalculateDamage(SKILL_CAT_PROJECTILE, params);
    inst->damagePerDisc   = (totalDmg > 0.0f ? totalDmg : 45.0f) / (float)inst->totalSlices;
    inst->knockback       = Skill_CalculateKnockback(SKILL_CAT_PROJECTILE, params) * 0.45f;

    Skill_RequestAnim(agentId, "earth_stomp_slam", EDV_STOMP_RISE_TIME);

    // Raise shadowed stone pillar using core EarthSpike VFX
    inst->spikeHandle = SpawnStonePillarStep(inst->pillarPos, inst->pillarMaxHeight,
                                             inst->pillarRadius, EDV_STOMP_RISE_TIME * 0.85f, true);
    VFX_ComposeImpactDust((Vector3){ inst->pillarPos.x, inst->pillarPos.y + 0.12f, inst->pillarPos.z },
                          VC_MAT_EARTH, 1.15f * scale, 0.70f);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}

void UpdateEarthDiscVolleySkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        EdvInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        inst->timer += dt;

        if (inst->state == EDV_STOMP_RISE) {
            if (inst->timer >= EDV_STOMP_RISE_TIME) {
                inst->state = EDV_SLICE_BARRAGE;
                inst->timer = 0.0f;
                inst->sliceTimer = EDV_SLICE_INTERVAL;
                inst->currentHeight = inst->pillarMaxHeight;
            }
        } else if (inst->state == EDV_SLICE_BARRAGE) {
            inst->sliceTimer += dt;
            if (inst->slicesFired < inst->totalSlices && inst->sliceTimer >= EDV_SLICE_INTERVAL) {
                inst->sliceTimer -= EDV_SLICE_INTERVAL;

                Skill_RequestAnim(inst->ownerAgentId, "earth_pillar_punch", EDV_SLICE_INTERVAL);

                float sliceStep = inst->pillarMaxHeight / (float)inst->totalSlices;
                float topY = inst->pillarPos.y + inst->currentHeight - sliceStep * 0.45f;
                Vector3 sliceOrigin = { inst->pillarPos.x, topY, inst->pillarPos.z };

                // Shorten the stone pillar by one slice
                inst->currentHeight = fmaxf(0.0f, inst->currentHeight - sliceStep);
                KillInstanceSpike(inst);
                if (inst->currentHeight > 0.18f) {
                    inst->spikeHandle = SpawnStonePillarStep(inst->pillarPos, inst->currentHeight,
                                                             inst->pillarRadius, 0.005f, false);
                }

                SpawnEarthDisc(inst, sliceOrigin, inst->slicesFired);
                inst->slicesFired++;
            }

            if (inst->slicesFired >= inst->totalSlices && inst->sliceTimer >= 0.16f) {
                KillInstanceSpike(inst);
                inst->state = EDV_SINK_EXIT;
                inst->timer = 0.0f;
            }
        }

        bool anyDiscAlive = false;
        for (int d = 0; d < MAX_EDV_DISCS; d++) {
            EdvDisc *disc = &inst->discs[d];
            if (!disc->active) continue;

            float stepDist = Vector3Length(disc->vel) * dt;
            disc->pos = Vector3Add(disc->pos, Vector3Scale(disc->vel, dt));
            disc->distTraveled += stepDist;

            bool hitEnemy = (Vector3Distance(disc->pos, enemyPos) <= (disc->radius + enemyRadius));
            bool outOfRange = (disc->distTraveled >= disc->maxRange);

            if (hitEnemy || outOfRange) {
                if (hitEnemy) {
                    Entity_ApplyAoEDamage(disc->pos, 1.35f * inst->sizeScale,
                                          inst->damagePerDisc, inst->knockback, inst->casterTeam);
                    PlayImpactSound(EFFECT_PRESET_EARTH_CRACK);
                }
                VFX_ComposeImpactDust(disc->pos, VC_MAT_EARTH, 1.05f * inst->sizeScale, 0.65f);
                VFX_ComposeDebrisShards(disc->pos, (Vector3){0.0f, 3.2f, 0.0f},
                                        VC_MAT_EARTH, 0.18f * inst->sizeScale, 10);
                disc->active = false;
            } else {
                anyDiscAlive = true;
            }
        }

        if (inst->state == EDV_SINK_EXIT && inst->timer >= EDV_SINK_TIME && !anyDiscAlive) {
            inst->active = false;
            inst->state = EDV_IDLE;
        }
    }
}

void DrawEarthDiscVolleySkill(void) {
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        const EdvInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        if (inst->state == EDV_SINK_EXIT) {
            float t01 = Clamp(inst->timer / EDV_SINK_TIME, 0.0f, 1.0f);
            Vector3 dissPos = { inst->pillarPos.x, inst->pillarPos.y + 0.12f, inst->pillarPos.z };
            VFX_ComposeDissolveExit(dissPos, VC_MAT_EARTH, 0.90f * inst->sizeScale, t01);
        }
    }
}

void UnloadEarthDiscVolleySkill(void) {
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        KillInstanceSpike(&s_instances[i]);
        s_instances[i].active = false;
        s_instances[i].state = EDV_IDLE;
    }
}

bool IsEarthDiscVolleySkillCoiling(void) {
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].state == EDV_STOMP_RISE) {
            return true;
        }
    }
    return false;
}

int GetEarthDiscVolleySkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_EDV_INSTANCES && count < maxProjectiles; i++) {
        const EdvInstance *inst = &s_instances[i];
        if (!inst->active) continue;
        for (int d = 0; d < MAX_EDV_DISCS && count < maxProjectiles; d++) {
            if (!inst->discs[d].active) continue;
            outProjectiles[count++] = (SkillProjectile){
                .position = inst->discs[d].pos,
                .radius = inst->discs[d].radius,
                .active = true
            };
        }
    }
    return count;
}

void DeactivateEarthDiscVolleyProjectile(int index) {
    int cur = 0;
    for (int i = 0; i < MAX_EDV_INSTANCES; i++) {
        EdvInstance *inst = &s_instances[i];
        if (!inst->active) continue;
        for (int d = 0; d < MAX_EDV_DISCS; d++) {
            if (!inst->discs[d].active) continue;
            if (cur == index) {
                inst->discs[d].active = false;
                return;
            }
            cur++;
        }
    }
}
