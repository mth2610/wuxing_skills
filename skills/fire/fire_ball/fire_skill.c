#include "skills/fire/fire_ball/fire_skill.h"
#include "core/composition/visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/utils_math.h"
#include "entities/entities.h"
#include "raymath.h"
#include "skills/skill_helper.h"
#include "skills/skill_manager.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_INFERNO_INSTANCES 8
#define INFERNO_CAST_SECS     0.48f
#define INFERNO_SPEED         16.5f
#define INFERNO_MAX_RANGE     13.5f
#define INFERNO_HIT_RADIUS    0.95f
#define INFERNO_VORTEX_SECS   1.55f
#define INFERNO_MANA_COST     26.0f

typedef enum {
    INFERNO_IDLE = 0,
    INFERNO_FLIGHT,
    INFERNO_VORTEX
} InfernoState;

typedef struct {
    bool active;
    InfernoState state;
    int ownerAgentId;
    int casterTeam;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 currentPos;
    Vector3 groundImpact;
    Vector3 dir;
    Vector3 perp;
    float distTotal;
    float distTraveled;
    float weavePhase;
    float sizeScale;
    float vortexTimer;
    float sparkAccum;
    float damage;
    float knockback;
    int mainTrailHandle;
    int coreTrailHandle;
    Matrix headMat;
    Matrix orbitMat;
} InfernoInstance;

static InfernoInstance s_instances[MAX_INFERNO_INSTANCES];
static int s_skillIndex = -1;

static void StopInfernoTrails(InfernoInstance *inst) {
    if (inst->mainTrailHandle >= 0) {
        VFX_TrailSetWidth(inst->mainTrailHandle, 0.0f);
        inst->mainTrailHandle = -1;
    }
    if (inst->coreTrailHandle >= 0) {
        VFX_TrailSetWidth(inst->coreTrailHandle, 0.0f);
        inst->coreTrailHandle = -1;
    }
}

static void AbortInferno(int agentId) {
    for (int i = 0; i < MAX_INFERNO_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].ownerAgentId == agentId) {
            StopInfernoTrails(&s_instances[i]);
            s_instances[i].active = false;
            s_instances[i].state = INFERNO_IDLE;
        }
    }
}

static bool HasActiveInferno(int agentId) {
    for (int i = 0; i < MAX_INFERNO_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].ownerAgentId == agentId) {
            return true;
        }
    }
    return false;
}

static void DetonateInferno(InfernoInstance *inst, Vector3 at) {
    StopInfernoTrails(inst);
    inst->state = INFERNO_VORTEX;
    inst->vortexTimer = 0.0f;
    inst->currentPos = at;
    inst->groundImpact = (Vector3){
        at.x,
        VFX_GroundHeightFromMap(at.x, at.z, NULL) + 0.08f,
        at.z
    };

    // Billowing Shader-Puff fireball detonation burst (no flipbook/normal Mode 4)
    VFX_FireConfig burstCfg = VFX_Fire_DefaultConfig();
    VFX_Fire_ApplyPreset(&burstCfg, VFX_FIRE_PRESET_FIREBALL);
    burstCfg.preset = VFX_FIRE_PRESET_CUSTOM;
    burstCfg.mode = VFX_FIRE_MODE_SHADER_PUFF;
    burstCfg.particleCount = 140;
    burstCfg.particleRadius = 0.48f * inst->sizeScale;
    burstCfg.sourceRadius = 0.55f * inst->sizeScale;
    burstCfg.fieldSpeed = 7.5f;
    burstCfg.lifetime = 0.55f;
    burstCfg.coreHeat = 1.75f;
    burstCfg.intensity = 2.80f;
    burstCfg.emitSparks = true;
    burstCfg.emitSmoke = false;
    burstCfg.dynamicLight = true;
    VFX_ComposeGuidedFire(at, &burstCfg);

    VFX_ComposeEmberBurst(at, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 1.35f * inst->sizeScale, 0.95f);
    VFX_ComposeGroundWave(inst->groundImpact, VC_MAT_FIRE, 3.1f * inst->sizeScale, 0.90f,
                          VFX_GroundHeightFromMap, NULL);
    VFX_ComposeImpactDust(inst->groundImpact, VC_MAT_FIRE, 1.05f * inst->sizeScale, 0.65f);

    Entity_ApplyAoEDamageFrom(inst->ownerAgentId, at, 2.75f * inst->sizeScale,
                              inst->damage, inst->knockback);
    PlayImpactSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void InitFireSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    VFX_Fire_Warmup();

    for (int i = 0; i < MAX_INFERNO_INSTANCES; i++) {
        s_instances[i].active = false;
        s_instances[i].state = INFERNO_IDLE;
        s_instances[i].mainTrailHandle = -1;
        s_instances[i].coreTrailHandle = -1;
    }

    s_skillIndex = Skill_GetIndexByName("INFERNO_SURGE");
    if (s_skillIndex >= 0) {
        RegisterSkillManaCost(s_skillIndex, INFERNO_MANA_COST);
        RegisterSkillCastAnimSeconds(s_skillIndex, INFERNO_CAST_SECS);
        RegisterSkillAbort(s_skillIndex, AbortInferno);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveInferno);
    }
}

void CastFireSkill(int agentId, Vector3 startPos, Vector3 target, float twistPhase, float sizeScale) {
    int slot = -1;
    for (int i = 0; i < MAX_INFERNO_INSTANCES; i++) {
        if (!s_instances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        StopInfernoTrails(&s_instances[0]);
    }

    float scale = Clamp(sizeScale > 0.05f ? sizeScale : 1.0f, 0.4f, 2.5f);
    Vector3 launchPos = { startPos.x, startPos.y + 1.05f, startPos.z };
    Vector3 delta = Vector3Subtract(target, launchPos);
    delta.y = 0.0f;
    float dist = Vector3Length(delta);
    Vector3 dir = (dist > 0.01f) ? Vector3Scale(delta, 1.0f / dist) : (Vector3){ 0.0f, 0.0f, 1.0f };
    if (dist < 1.5f) dist = 1.5f;
    if (dist > INFERNO_MAX_RANGE) dist = INFERNO_MAX_RANGE;

    Vector3 endPos = {
        launchPos.x + dir.x * dist,
        VFX_GroundHeightFromMap(launchPos.x + dir.x * dist, launchPos.z + dir.z * dist, NULL) + 0.65f,
        launchPos.z + dir.z * dist
    };

    const Agent *caster = Entity_GetAgent(agentId);
    InfernoInstance *inst = &s_instances[slot];
    *inst = (InfernoInstance){0};
    inst->active = true;
    inst->state = INFERNO_FLIGHT;
    inst->ownerAgentId = agentId;
    inst->casterTeam = (caster != NULL) ? caster->team : 0;
    inst->startPos = launchPos;
    inst->targetPos = endPos;
    inst->currentPos = launchPos;
    inst->dir = dir;
    inst->perp = (Vector3){ -dir.z, 0.0f, dir.x };
    inst->distTotal = dist;
    inst->distTraveled = 0.0f;
    inst->weavePhase = twistPhase;
    inst->sizeScale = scale;

    inst->damage = 46.0f * scale;
    inst->knockback = 0.0f;

    inst->headMat = MatrixTranslate(launchPos.x, launchPos.y, launchPos.z);
    inst->orbitMat = inst->headMat;
    inst->mainTrailHandle = VFX_ComposeTrail(&inst->headMat, VC_MAT_FIRE,
                                             0.36f * scale, 0.30f, TRAIL_PRESET_MAIN);
    if (inst->mainTrailHandle >= 0) {
        VFX_TrailSetHdrGain(inst->mainTrailHandle, 2.1f);
    }
    inst->coreTrailHandle = VFX_ComposeTrail(&inst->orbitMat, VC_MAT_FIRE,
                                             0.15f * scale, 0.24f, TRAIL_PRESET_ENERGY);
    if (inst->coreTrailHandle >= 0) {
        VFX_TrailSetHdrGain(inst->coreTrailHandle, 2.4f);
    }

    Skill_RequestAnim(agentId, "fireball", INFERNO_CAST_SECS);
    VFX_ComposeEmberBurst(launchPos, (Vector3){ 0.0f, 1.0f, 0.0f }, VC_MAT_FIRE, 0.85f * scale, 0.65f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
}

void UpdateFireSkill(float dt) {
    for (int i = 0; i < MAX_INFERNO_INSTANCES; i++) {
        InfernoInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        if (inst->state == INFERNO_FLIGHT) {
            inst->distTraveled += INFERNO_SPEED * dt;
            float t = Clamp(inst->distTraveled / inst->distTotal, 0.0f, 1.0f);

            float weave = sinf(t * PI * 2.0f + inst->weavePhase) * 0.35f * sinf(t * PI);
            float arcY = sinf(t * PI) * 0.55f;
            Vector3 base = Vector3Lerp(inst->startPos, inst->targetPos, t);
            inst->currentPos = (Vector3){
                base.x + inst->perp.x * weave,
                base.y + arcY,
                base.z + inst->perp.z * weave
            };

            // Update main flame ribbon + spiraling ember core ribbon
            float spinAng = t * 18.0f + inst->weavePhase;
            float orbitR = 0.26f * inst->sizeScale;
            inst->headMat = MatrixTranslate(inst->currentPos.x, inst->currentPos.y, inst->currentPos.z);
            inst->orbitMat = MatrixTranslate(
                inst->currentPos.x + inst->perp.x * cosf(spinAng) * orbitR,
                inst->currentPos.y + sinf(spinAng) * orbitR,
                inst->currentPos.z + inst->perp.z * cosf(spinAng) * orbitR
            );

            // Billowing Shader-Puff Guided Fire core at projectile head
            VFX_FireConfig headCfg = VFX_Fire_DefaultConfig();
            VFX_Fire_ApplyPreset(&headCfg, VFX_FIRE_PRESET_FIREBALL);
            headCfg.preset = VFX_FIRE_PRESET_CUSTOM;
            headCfg.mode = VFX_FIRE_MODE_SHADER_PUFF;
            headCfg.emitterShape = VFX_FIRE_EMITTER_SPHERE;
            headCfg.emissionMode = VFX_FIRE_EMIT_STEADY;
            headCfg.motionType = VFX_FIRE_MOTION_AIRFLOW;
            headCfg.attachment = VFX_FIRE_ATTACH_FREE;
            headCfg.sourceRadius = 0.22f * inst->sizeScale;
            headCfg.particleCount = 140;
            headCfg.particleRadius = 0.34f * inst->sizeScale;
            headCfg.lifetime = 0.32f;
            headCfg.buoyancy = 2.5f;
            headCfg.turbulenceSpeed = 2.0f;
            headCfg.distortion = 0.55f;
            headCfg.softness = 0.35f;
            headCfg.coreHeat = 1.60f;
            headCfg.intensity = 2.60f;
            headCfg.emissiveBoost = 2.1f;
            headCfg.emitSparks = true;
            headCfg.emitSmoke = false;
            headCfg.dynamicLight = true;
            VFX_ComposeGuidedFire(inst->currentPos, &headCfg);

            inst->sparkAccum += dt;
            if (inst->sparkAccum >= 0.09f) {
                inst->sparkAccum = 0.0f;
                VFX_ComposeEmberBurst(inst->currentPos, (Vector3){ 0.0f, 1.0f, 0.0f },
                                      VC_MAT_FIRE, 0.55f * inst->sizeScale, 0.45f);
            }

            int hitIds[8];
            int hits = Entity_GetNearbyTargets(inst->currentPos, INFERNO_HIT_RADIUS * inst->sizeScale,
                                               hitIds, 8);
            bool anyEnemy = false;
            for (int h = 0; h < hits; h++) {
                if (Entity_CanDamageTarget(inst->ownerAgentId, hitIds[h])) {
                    anyEnemy = true;
                    break;
                }
            }
            if (anyEnemy || t >= 1.0f) {
                DetonateInferno(inst, inst->currentPos);
            }
        } else if (inst->state == INFERNO_VORTEX) {
            inst->vortexTimer += dt;

            // Tall, billowing Shader-Puff Guided Fire Tornado at ground impact
            VFX_FireConfig vortexCfg = VFX_Fire_DefaultConfig();
            VFX_Fire_ApplyPreset(&vortexCfg, VFX_FIRE_PRESET_FIRE_TORNADO);
            vortexCfg.preset = VFX_FIRE_PRESET_CUSTOM;
            vortexCfg.mode = VFX_FIRE_MODE_SHADER_PUFF;
            vortexCfg.particleCount = 300;
            vortexCfg.particleRadius = 0.42f * inst->sizeScale;
            vortexCfg.sourceRadius = 0.75f * inst->sizeScale;
            vortexCfg.buoyancy = 8.5f;
            vortexCfg.swirlSpeed = 14.0f;
            vortexCfg.distortion = 0.55f;
            vortexCfg.softness = 0.35f;
            vortexCfg.coreHeat = 1.60f;
            vortexCfg.intensity = 2.50f;
            vortexCfg.lifetime = 0.80f;
            vortexCfg.emitSparks = true;
            vortexCfg.emitSmoke = false;
            vortexCfg.dynamicLight = true;
            VFX_ComposeGuidedFire(inst->groundImpact, &vortexCfg);

            if (inst->vortexTimer >= INFERNO_VORTEX_SECS) {
                inst->active = false;
                inst->state = INFERNO_IDLE;
            }
        }
    }
}

void DrawFireSkill(void) {
    // Visuals are rendered by VC_Trail + Guided Fire (flame_volume) + FireballBurst + EmberBurst.
}

void UnloadFireSkill(void) {
    for (int i = 0; i < MAX_INFERNO_INSTANCES; i++) {
        StopInfernoTrails(&s_instances[i]);
        s_instances[i].active = false;
        s_instances[i].state = INFERNO_IDLE;
    }
}

int GetFireSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_INFERNO_INSTANCES && count < maxProjectiles; i++) {
        if (!s_instances[i].active || s_instances[i].state != INFERNO_FLIGHT) continue;
        outProjectiles[count++] = (SkillProjectile){
            .position = s_instances[i].currentPos,
            .radius = INFERNO_HIT_RADIUS * s_instances[i].sizeScale,
            .active = true
        };
    }
    return count;
}

void DeactivateFireProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_INFERNO_INSTANCES; i++) {
        if (!s_instances[i].active || s_instances[i].state != INFERNO_FLIGHT) continue;
        if (count == index) {
            DetonateInferno(&s_instances[i], s_instances[i].currentPos);
            return;
        }
        count++;
    }
}
