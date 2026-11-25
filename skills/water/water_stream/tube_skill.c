#include "skills/water/water_stream/tube_skill.h"
#include "core/color_gradient.h"
#include "core/composition/visual_composer.h"
#include "core/force_field.h"
#include "core/particles/particle_system.h"
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

#define MAX_TIDAL_STREAMS 6
#define TIDAL_STREAM_CAST_SECS 0.45f
#define TIDAL_STREAM_SPEED 13.0f
#define TIDAL_STREAM_RANGE 12.0f
#define TIDAL_STREAM_HIT_RADIUS 1.35f
#define TIDAL_STREAM_MANA_COST 18.0f

typedef struct {
    bool active;
    int ownerAgentId;
    int casterTeam;
    Vector3 startPos;
    Vector3 targetPos;
    Vector3 dir;
    Vector3 headPos;
    float distTraveled;
    float maxDist;
    float sizeScale;
    float elapsed;
    float duration;
    float sprayAccum;
    float mistAccum;
    bool hitDealt;
    int tsunamiHandle;
    float damage;
    float knockback;
} TidalStreamInstance;

static TidalStreamInstance s_streams[MAX_TIDAL_STREAMS];
static int s_skillIndex = -1;
static ColorGradient s_splashGrad;
static ForceField s_splashField;
static bool s_sharedReady = false;

static void EnsureSharedInit(void) {
    if (s_sharedReady) return;
    ColorGradient_StandardFade(&s_splashGrad, ELEMENT_COLOR_WATER, 0.45f, 0.25f);
    ForceField_Clear(&s_splashField);
    ForceField_AddLayer(&s_splashField, (ForceLayer){
        .type = FORCE_GRAVITY_DIR,
        .direction = { 0.0f, -1.0f, 0.0f },
        .strength = 9.81f
    });
    ForceField_AddLayer(&s_splashField, (ForceLayer){
        .type = FORCE_DRAG,
        .strength = 1.8f
    });
    s_sharedReady = true;
}

static void SpawnStreamDroplets(Vector3 pos, Vector3 dir, float scale, int count) {
    for (int i = 0; i < count; i++) {
        ParticleConfig cfg = {0};
        cfg.position = (Vector3){
            pos.x + (Random01() - 0.5f) * 1.4f * scale,
            pos.y + 0.15f + Random01() * 0.65f * scale,
            pos.z + (Random01() - 0.5f) * 1.4f * scale
        };
        cfg.velocity = (Vector3){
            dir.x * (2.5f + Random01() * 3.5f) + (Random01() - 0.5f) * 2.8f,
            1.8f + Random01() * 2.6f,
            dir.z * (2.5f + Random01() * 3.5f) + (Random01() - 0.5f) * 2.8f
        };
        cfg.radius = Math_Mix(0.04f, 0.10f, Random01()) * scale;
        cfg.lifetime = Math_Mix(0.35f, 0.70f, Random01());
        cfg.colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.85f);
        cfg.colorEnd = ColorAlpha(ELEMENT_COLOR_WATER, 0.0f);
        cfg.gradient = &s_splashGrad;
        cfg.forceField = &s_splashField;
        cfg.render.blendMode = VFX_BLEND_ADDITIVE;
        cfg.render.unlit = 1;
        cfg.render.emissiveBoost = 1.25f;
        SpawnParticle(cfg);
    }
}

static void AbortTidalStream(int agentId) {
    for (int i = 0; i < MAX_TIDAL_STREAMS; i++) {
        if (s_streams[i].active && s_streams[i].ownerAgentId == agentId) {
            if (s_streams[i].tsunamiHandle >= 0) {
                VFX_KillTsunami(s_streams[i].tsunamiHandle);
                s_streams[i].tsunamiHandle = -1;
            }
            s_streams[i].active = false;
        }
    }
}

static bool HasActiveTidalStream(int agentId) {
    for (int i = 0; i < MAX_TIDAL_STREAMS; i++) {
        if (s_streams[i].active && s_streams[i].ownerAgentId == agentId) {
            return true;
        }
    }
    return false;
}

void InitTubeSkill(int screenWidth, int screenHeight) {
    (void)screenWidth;
    (void)screenHeight;
    EnsureSharedInit();

    for (int i = 0; i < MAX_TIDAL_STREAMS; i++) {
        s_streams[i].active = false;
        s_streams[i].tsunamiHandle = -1;
    }

    s_skillIndex = Skill_GetIndexByName("TIDAL_STREAM");
    if (s_skillIndex >= 0) {
        RegisterSkillManaCost(s_skillIndex, TIDAL_STREAM_MANA_COST);
        RegisterSkillCastAnimSeconds(s_skillIndex, TIDAL_STREAM_CAST_SECS);
        RegisterSkillAbort(s_skillIndex, AbortTidalStream);
        RegisterSkillLifecycleQuery(s_skillIndex, HasActiveTidalStream);
    }
}

void CastTubeSkill(int agentId, Vector3 startPos, Vector3 target, float twistPhase, float sizeScale) {
    (void)twistPhase;
    EnsureSharedInit();

    int slot = -1;
    for (int i = 0; i < MAX_TIDAL_STREAMS; i++) {
        if (!s_streams[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = 0;
        if (s_streams[0].tsunamiHandle >= 0) {
            VFX_KillTsunami(s_streams[0].tsunamiHandle);
        }
    }

    float scale = Clamp(sizeScale > 0.05f ? sizeScale : 1.0f, 0.4f, 2.5f);
    Vector3 fwd = { target.x - startPos.x, 0.0f, target.z - startPos.z };
    float rawDist = Vector3Length(fwd);
    if (rawDist < 0.01f) {
        fwd = (Vector3){ 0.0f, 0.0f, 1.0f };
    } else {
        fwd = Vector3Scale(fwd, 1.0f / rawDist);
    }

    float maxDist = fmaxf(rawDist, TIDAL_STREAM_RANGE * 0.75f);
    if (maxDist > TIDAL_STREAM_RANGE) maxDist = TIDAL_STREAM_RANGE;

    Vector3 groundStart = {
        startPos.x,
        VFX_GroundHeightFromMap(startPos.x, startPos.z, NULL),
        startPos.z
    };
    Vector3 streamTarget = {
        groundStart.x + fwd.x * maxDist,
        VFX_GroundHeightFromMap(groundStart.x + fwd.x * maxDist, groundStart.z + fwd.z * maxDist, NULL),
        groundStart.z + fwd.z * maxDist
    };

    const Agent *caster = Entity_GetAgent(agentId);
    TidalStreamInstance *st = &s_streams[slot];
    *st = (TidalStreamInstance){0};
    st->active = true;
    st->ownerAgentId = agentId;
    st->casterTeam = (caster != NULL) ? caster->team : 0;
    st->startPos = groundStart;
    st->targetPos = streamTarget;
    st->dir = fwd;
    st->headPos = groundStart;
    st->distTraveled = 0.0f;
    st->maxDist = maxDist;
    st->sizeScale = scale;
    st->elapsed = 0.0f;
    st->duration = 2.2f;
    st->hitDealt = false;

    SkillParams params = { .level = 1, .sizeScale = scale };
    st->damage = Skill_CalculateDamage(SKILL_CAT_PROJECTILE, params);
    if (st->damage <= 0.0f) st->damage = 38.0f;
    st->knockback = Skill_CalculateKnockback(SKILL_CAT_PROJECTILE, params);

    Skill_RequestAnim(agentId, "two_hand_spell_casting", TIDAL_STREAM_CAST_SECS);

    // Tsunami water-stream branch (Swift Flash river dash wave)
    VFX_TsunamiConfig tcfg = VFX_Tsunami_Preset(VFX_TSUNAMI_PRESET_SWIFT_FLASH);
    tcfg.pattern = VFX_TSUNAMI_TIDAL_WAVE;
    tcfg.waveHeight = 1.45f * scale;
    tcfg.waveSpeed = TIDAL_STREAM_SPEED;
    tcfg.width = 6.2f * scale;
    tcfg.length = maxDist;
    tcfg.duration = st->duration;
    tcfg.curlIntensity = 0.72f;
    tcfg.drag = 0.28f;
    tcfg.emitCrestSpray = true;
    tcfg.emitWetGround = true;
    tcfg.skillPower = 1.35f;
    st->tsunamiHandle = VFX_ComposeTsunamiEx(groundStart, streamTarget, &tcfg);

    // Rich cast details: ground ripple + hydro mist + initial spray
    VFX_SpawnGroundWave((Vector3){ groundStart.x, groundStart.y + 0.05f, groundStart.z },
                        VC_MAT_WATER, 2.2f * scale, 0.38f);
    VFX_ComposeMistVeilEx(groundStart, VC_MAT_WATER, 1.8f * scale, 0.55f);
    SpawnStreamDroplets(groundStart, fwd, scale, 18);
    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

void UpdateTubeSkill(float dt) {
    for (int i = 0; i < MAX_TIDAL_STREAMS; i++) {
        TidalStreamInstance *st = &s_streams[i];
        if (!st->active) continue;

        st->elapsed += dt;
        if (st->distTraveled < st->maxDist) {
            float step = TIDAL_STREAM_SPEED * dt;
            st->distTraveled = fminf(st->distTraveled + step, st->maxDist);
            st->headPos.x = st->startPos.x + st->dir.x * st->distTraveled;
            st->headPos.z = st->startPos.z + st->dir.z * st->distTraveled;
            st->headPos.y = VFX_GroundHeightFromMap(st->headPos.x, st->headPos.z, NULL) + 0.45f;

            st->sprayAccum += dt * 45.0f;
            if (st->sprayAccum >= 1.0f) {
                int count = (int)st->sprayAccum;
                st->sprayAccum -= (float)count;
                if (count > 8) count = 8;
                SpawnStreamDroplets(st->headPos, st->dir, st->sizeScale, count);
            }

            st->mistAccum += dt;
            if (st->mistAccum >= 0.14f) {
                st->mistAccum = 0.0f;
                VFX_ComposeMistVeilEx(st->headPos, VC_MAT_WATER, 1.4f * st->sizeScale, 0.35f);
            }

            if (!st->hitDealt) {
                int hitIds[8];
                int hits = Entity_GetNearbyTargets(st->headPos, TIDAL_STREAM_HIT_RADIUS * st->sizeScale,
                                                   hitIds, 8);
                bool anyEnemy = false;
                for (int h = 0; h < hits; h++) {
                    if (Entity_CanDamageTarget(st->ownerAgentId, hitIds[h])) {
                        anyEnemy = true;
                        break;
                    }
                }
                if (anyEnemy) {
                    st->hitDealt = true;
                    Entity_ApplyAoEDamageFrom(st->ownerAgentId, st->headPos, 2.4f * st->sizeScale,
                                              st->damage, st->knockback);
                    VFX_SpawnGroundWave((Vector3){ st->headPos.x, st->headPos.y - 0.35f, st->headPos.z },
                                        VC_MAT_WATER, 2.8f * st->sizeScale, 0.42f);
                    SpawnStreamDroplets(st->headPos, st->dir, st->sizeScale * 1.2f, 22);
                    PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);
                }
            }
        }

        if (st->elapsed >= st->duration) {
            st->active = false;
            st->tsunamiHandle = -1;
        }
    }
}

void DrawTubeSkill(void) {
    // Visuals are driven by VC_Tsunami + ParticleSystem + MistVeil.
}

void UnloadTubeSkill(void) {
    for (int i = 0; i < MAX_TIDAL_STREAMS; i++) {
        if (s_streams[i].active && s_streams[i].tsunamiHandle >= 0) {
            VFX_KillTsunami(s_streams[i].tsunamiHandle);
        }
        s_streams[i].active = false;
        s_streams[i].tsunamiHandle = -1;
    }
}

int GetTubeSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_TIDAL_STREAMS && count < maxProjectiles; i++) {
        if (!s_streams[i].active || s_streams[i].distTraveled >= s_streams[i].maxDist) continue;
        outProjectiles[count++] = (SkillProjectile){
            .position = s_streams[i].headPos,
            .radius = TIDAL_STREAM_HIT_RADIUS * s_streams[i].sizeScale,
            .active = true
        };
    }
    return count;
}

void DeactivateTubeProjectile(int index) {
    int count = 0;
    for (int i = 0; i < MAX_TIDAL_STREAMS; i++) {
        if (!s_streams[i].active || s_streams[i].distTraveled >= s_streams[i].maxDist) continue;
        if (count == index) {
            s_streams[i].distTraveled = s_streams[i].maxDist;
            return;
        }
        count++;
    }
}
