#include "visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "core/particle_system.h"
#include "core/decal_system.h"
#include "core/vfx_light.h"
#include "core/trail_system.h"
#include "core/camera_fx.h"
#include "core/screen_distort.h"
#include "core/time_fx.h"
#include "raymath.h"
#include <stdlib.h>
#include <math.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_CONCURRENT_CAST_EFFECTS 16
static ForceField s_castPullFlds[MAX_CONCURRENT_CAST_EFFECTS];
static int s_castPullFldNextSlot = 0;

#define MAX_CONCURRENT_PROJECTILE_TRAILS 32
static ForceField s_flightFlds[MAX_CONCURRENT_PROJECTILE_TRAILS];
static int s_flightFldNextSlot = 0;

void VFX_ComposeSmokePuff(Vector3 pos, float size) {
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 15;
    cfg.countMax = 25;
    cfg.speedMin = 2.0f;
    cfg.speedMax = 4.0f;
    cfg.radiusMin = size * 0.3f;
    cfg.radiusMax = size * 0.8f;
    cfg.lifetimeMin = 1.5f;
    cfg.lifetimeMax = 2.5f;
    cfg.pitchRange = PI;
    cfg.upwardBias = 2.0f;
    
    cfg.colorStart = (Color){100, 100, 100, 200};
    cfg.colorEnd = (Color){50, 50, 50, 0};
    
    static ForceField f = {0};
    if (f.layerCount == 0) {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 5.0f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;
    
    ParticleSystem_SpawnRadialBurst(pos, size, &cfg);
}

void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration) {
    Vector3 dir = Vector3Subtract(end, start);
    float len = Vector3Length(dir);
    if (len < 0.1f) return;
    dir = Vector3Scale(dir, 1.0f / len);
    
    int numPuffs = (int)(len / 2.0f) + 1;
    
    ParticleRadialBurstConfig cfg = {0};
    cfg.countMin = 2;
    cfg.countMax = 4;
    cfg.speedMin = 0.5f;
    cfg.speedMax = 1.0f;
    cfg.radiusMin = 0.5f;
    cfg.radiusMax = 1.2f;
    cfg.lifetimeMin = duration * 0.8f;
    cfg.lifetimeMax = duration * 1.2f;
    cfg.pitchRange = PI;
    cfg.upwardBias = 1.0f;
    
    cfg.colorStart = (Color){100, 100, 100, 200};
    cfg.colorEnd = (Color){50, 50, 50, 0};
    
    static ForceField f = {0};
    if (f.layerCount == 0) {
        ForceLayer fl = {0};
        fl.type = FORCE_VISCOSITY;
        fl.strength = 3.0f;
        ForceField_AddLayer(&f, fl);
    }
    cfg.forceField = &f;
    
    for (int i = 0; i <= numPuffs; i++) {
        float t = (float)i / (float)numPuffs;
        Vector3 pos = Vector3Add(start, Vector3Scale(dir, t * len));
        ParticleSystem_SpawnRadialBurst(pos, 1.0f, &cfg);
    }
}

void VFX_ComposeFissure(Vector3 start, Vector3 end, float width) {
    int numDecals = 4;
    for (int i = 0; i <= numDecals; i++) {
        float t = (float)i / numDecals;
        Vector3 pos = Vector3Add(start, Vector3Scale(Vector3Subtract(end, start), t));
        SpawnGroundDecal(DECAL_PRESET_EARTH_SHATTER, pos, width * 3.0f, GetRandomValue(-10, 10) * 0.1f);
    }
    VFX_ComposeSmokeTrail(start, end, 3.0f);
    CameraFX_Shake(0.3f);
}

void VFX_ComposeLightningBeam(Vector3 start, Vector3 end, float duration) {
    float dist = Vector3Distance(start, end);
    float speed = dist / fmaxf(duration, 0.001f);
    SpawnLightningTrail(start, end, 1.0f, speed);
    
    VFXLight_Spawn(start, (Color){0, 200, 255, 255}, 3.0f, 0.2f, 0);
    VFXLight_Spawn(end, (Color){0, 200, 255, 255}, 4.0f, 0.3f, 0);
}

void VFX_ComposeImpact(Vector3 pos, EffectPresetType preset, float scale) {
    const VFX_ImpactPreset *p = VFX_Preset_GetImpact(preset);
    if (p == NULL) return;

    PlayImpactSound(preset);

    if (scale >= 1.5f) TimeFX_Hitstop(0.09f, 0.05f);

    /* 1. Screen Distortion */
    if (p->distortEnabled) {
        ScreenDistort_Add(pos, p->distortRadius * scale, p->distortStrength, p->distortLife, p->distortSpeed);
    }

    /* 2. Light Spawn */
    if (p->lightEnabled) {
        VFXLight_Spawn(pos, p->lightColor, p->lightRadius * scale, p->lightLife, VFX_PRIORITY_LOW);
    }

    /* 3. Decal Spawn */
    if (p->decalEnabled) {
        SpawnGroundDecal(p->decalPreset, pos, p->decalScale * scale, p->decalLife);
    }

    /* 4. Particle Radial Burst */
    if (p->particlesEnabled) {
        ParticleSystem_SpawnRadialBurst(pos, scale, &p->particles);
    }
}

void VFX_ComposeCast(Vector3 pos, EffectPresetType preset, float scale) {
    const VFX_CastPreset *p = VFX_Preset_GetCast(preset);
    if (p == NULL) return;

    PlayCastSound(preset);

    /* 1. Light Flash */
    VFXLight_Spawn(pos, p->flashColor, p->lightRadius * scale, p->lightLifetime, VFX_PRIORITY_LOW);

    /* 2. Inward-pulling force field setup */
    ForceField *castPullFld = &s_castPullFlds[s_castPullFldNextSlot];
    s_castPullFldNextSlot = (s_castPullFldNextSlot + 1) % MAX_CONCURRENT_CAST_EFFECTS;
    ForceField_Clear(castPullFld);
    
    float spawnRadius = p->spawnRadius * scale;
    float pullStrength = p->pullStrength * scale;
    ForceField_AddLayer(castPullFld, (ForceLayer){
        .type = FORCE_GRAVITY_POINT,
        .origin = pos,
        .strength = pullStrength,
        .radius = spawnRadius * 1.5f,
        .falloff = 1.0f
    });

    /* 3. Spawn gathering particles */
    int count = (int)((float)p->particleCount * scale);
    for (int i = 0; i < count; i++) {
        float a = (float)i / count * 2.0f * PI;
        float r = spawnRadius * (0.6f + 0.4f * ((float)rand() / (float)RAND_MAX));
        Vector3 spawnPos = {
            pos.x + cosf(a) * r,
            pos.y + ((float)rand() / (float)RAND_MAX) * spawnRadius * 0.5f,
            pos.z + sinf(a) * r
        };
        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = (Vector3){ 0.0f, 0.0f, 0.0f },
            .radius = ((float)rand() / (float)RAND_MAX * 0.012f + 0.005f) * scale,
            .lifetime = (float)rand() / (float)RAND_MAX * 0.5f + 0.4f,
            .gradient = p->gradient,
            .forceField = castPullFld
        });
    }
}

int VFX_ComposeProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed) {
    const VFX_ProjectilePreset *p = VFX_Preset_GetProjectile(preset);
    if (p == NULL) return -1;

    /* 1. Setup directional flight force field */
    ForceField *flightFld = &s_flightFlds[s_flightFldNextSlot];
    s_flightFldNextSlot = (s_flightFldNextSlot + 1) % MAX_CONCURRENT_PROJECTILE_TRAILS;
    ForceField_Clear(flightFld);
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, start));
    ForceField_AddLayer(flightFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = dir, .strength = 3.25f });
    ForceField_AddLayer(flightFld, (ForceLayer){ .type = FORCE_NOISE_PERLIN, .strength = 0.2f, .noiseScale = 0.08f, .noiseSpeed = 2.0f });

    /* 2. Setup tail dust config */
    static ParticleConfig s_tailEmit;
    s_tailEmit = (ParticleConfig){
        .radius = 0.3f * scale,
        .lifetime = 0.4f,
        .gradient = p->gradient,
        .forceField = flightFld
    };

    /* 3. Spawn head particle */
    SpawnParticle((ParticleConfig){
        .position = start,
        .velocity = Vector3Scale(dir, speed),
        .colorStart = p->tint,
        .colorEnd = p->tint,
        .radius = 0.5f * scale,
        .lifetime = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.5f,
        .gradient = p->gradient,
        .forceField = flightFld,
        .onLiveEmit = &s_tailEmit,
        .onLiveEmitRate = 120.0f
    });

    /* 4. Spawn trail ribbon */
    TrailConfig cfg = {
        .type = TRAIL_TYPE_PROJECTILE,
        .pos = start,
        .vel = Vector3Scale(dir, speed),
        .len = 4.0f * scale,
        .thick = 0.8f * scale,
        .trailLength = 20.0f * scale,
        .life = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.5f,
        .target = target,
        .scale = scale,
        .tint = p->tint,
        .forceField = flightFld,
        .gradient = p->gradient
    };
    return SpawnTrailEntity(cfg);
}

void VFX_ComposeTriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg) {
    if (cfg == NULL) return;

    /* Step 1: screen distortion */
    if (cfg->distortEnabled) {
        ScreenDistort_Add(pos, cfg->distortRadius, cfg->distortStrength,
                          cfg->distortLife, cfg->distortSpeed);
    }

    /* Step 2: ground decal */
    if (cfg->decalEnabled) {
        float rotation = cfg->decalRandomRotation
            ? (float)GetRandomValue(0, 360)
            : cfg->decalFixedRotation;
        DecalSystem_Add(pos, rotation, cfg->decalScale * sizeScale,
                        cfg->decalTex, cfg->decalLife, cfg->decalTint);
    }

    /* Step 3: point light flash */
    if (cfg->lightEnabled) {
        VFXLight_Spawn(pos, cfg->lightColor, cfg->lightRadius * sizeScale, cfg->lightLife, VFX_PRIORITY_LOW);
    }

    /* Step 4: radial particle burst */
    if (cfg->particlesEnabled) {
        ParticleSystem_SpawnRadialBurst(pos, sizeScale, &cfg->particles);
    }
}
