#include "core/skill_helper.h"
#include "core/time_fx.h"
#include "core/skill_manager.h"
#include "core/vfx_light.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/camera_fx.h"
#include "core/particle_system.h"
#include "core/force_field.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/trail_system.h"
#include "core/ribbon_strip.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#define MAX_VOLUMES 32
#define MAX_EMITTERS 32

static DamageVolume s_volumes[MAX_VOLUMES];
static ParticleEmitter s_emitters[MAX_EMITTERS];

#include "core/presets/vfx_presets.h"

// Pools for SpawnLightningTrail / SpawnLightningFollowerTrail, same
// round-robin-slot pattern as s_flightFlds. Separate from s_lightningFld
// (used by particle emitter presets) because trail zigzag needs a much
// sharper/faster noise profile than particle spark jitter.


static bool s_helpersInitialized = false;

void InitHelperResources(void) {
    if (s_helpersInitialized) return;
    VFX_Presets_Init();
    s_helpersInitialized = true;
}
// Audio Preset Implementation — no real per-element SFX assets exist under
// assets/ yet (verified: no .wav/.ogg files anywhere in assets/), so these
// are stub/warning-only for now. Each preset warns once (not every call) via
// a static "already warned" flag, then returns without playing or crashing.
// Once real asset files land (e.g. assets/sounds/fire_cast.wav), replace the
// TraceLog branch below with ResourceManager_LoadSound(path) + PlaySound().
static bool s_castSoundWarned[8] = { false };
static bool s_impactSoundWarned[8] = { false };

void PlayCastSound(EffectPresetType preset) {
    if (preset < 0 || preset >= 8) return;
    if (!s_castSoundWarned[preset]) {
        s_castSoundWarned[preset] = true;
        TraceLog(LOG_WARNING, "AUDIO: no cast SFX asset for EffectPresetType %d yet (stub, not crashing)", preset);
    }
    // TODO: once assets/sounds/*.wav exist per element, load via
    // ResourceManager_LoadSound(path) and PlaySound() here.
}

void PlayImpactSound(EffectPresetType preset) {
    if (preset < 0 || preset >= 8) return;
    if (!s_impactSoundWarned[preset]) {
        s_impactSoundWarned[preset] = true;
        TraceLog(LOG_WARNING, "AUDIO: no impact SFX asset for EffectPresetType %d yet (stub, not crashing)", preset);
    }
    // TODO: once assets/sounds/*.wav exist per element, load via
    // ResourceManager_LoadSound(path) and PlaySound() here.
}

// 2. Damage Volume Implementation
void DamageVolume_Init(void) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        s_volumes[i].active = false;
    }
}

void DamageVolume_Update(float dt) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (!s_volumes[i].active) continue;

        s_volumes[i].timer += dt;
        s_volumes[i].tickTimer += dt;

        if (s_volumes[i].tickTimer >= s_volumes[i].tickInterval) {
            s_volumes[i].tickTimer = 0.0f;
            float damage = s_volumes[i].damagePerSecond * s_volumes[i].tickInterval;
            ApplyAoEDamage(s_volumes[i].center, s_volumes[i].radius, damage, 0.0f);
        }

        if (s_volumes[i].timer >= s_volumes[i].duration) {
            s_volumes[i].active = false;
        }
    }
}

int SpawnDamageVolume(DamageVolume config) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (!s_volumes[i].active) {
            s_volumes[i] = config;
            s_volumes[i].timer = 0.0f;
            s_volumes[i].tickTimer = 0.0f;
            s_volumes[i].active = true;
            return i;
        }
    }
    return -1;
}

void DamageVolume_Unload(void) {
    DamageVolume_Init();
}

// 3. Skill Timeline Implementation
void Timeline_Start(SkillTimeline *t, float duration) {
    t->current = 0.0f;
    t->duration = duration;
}

bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt) {
    return (t->current - dt < triggerTime) && (t->current >= triggerTime);
}

bool Timeline_Finished(SkillTimeline *t) {
    return t->current >= t->duration;
}

// 3b. Layered Timeline Implementation
void Timeline_LayeredStart(LayeredTimeline *t) {
    t->current = 0.0f;
    t->layerCount = 0;
}

bool Timeline_AddLayer(LayeredTimeline *t, const char *tag, float start, float duration) {
    if (t->layerCount >= TIMELINE_MAX_LAYERS)
        return false;
    t->layers[t->layerCount].tag = tag;
    t->layers[t->layerCount].start = start;
    t->layers[t->layerCount].duration = duration;
    t->layerCount++;
    return true;
}

bool Timeline_IsLayerActive(const LayeredTimeline *t, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= t->layerCount)
        return false;
    const TimelineLayer *layer = &t->layers[layerIndex];
    return (t->current >= layer->start) && (t->current < layer->start + layer->duration);
}

float Timeline_LayerProgress(const LayeredTimeline *t, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= t->layerCount)
        return 0.0f;
    const TimelineLayer *layer = &t->layers[layerIndex];
    if (layer->duration <= 0.0f)
        return (t->current >= layer->start) ? 1.0f : 0.0f;
    return Clamp((t->current - layer->start) / layer->duration, 0.0f, 1.0f);
}

bool Timeline_LayerEvent(const LayeredTimeline *t, int layerIndex, float dt) {
    if (layerIndex < 0 || layerIndex >= t->layerCount)
        return false;
    float triggerTime = t->layers[layerIndex].start;
    return (t->current - dt < triggerTime) && (t->current >= triggerTime);
}

// 3c. Curve-driven flight implementation
void SkillHelper_StepCurveFlight(const SkillCurve *speedCurve, float elapsed, float dt,
                                  float maxDuration, float maxRange, float targetDistance,
                                  float *traveled, bool *arrived) {
    float capDistance = fminf(targetDistance, maxRange);
    float t01 = (maxDuration > 0.0f) ? Clamp(elapsed / maxDuration, 0.0f, 1.0f) : 1.0f;
    float speed = SkillCurve_Eval(speedCurve, t01);

    *traveled += speed * dt;
    if (*traveled > maxRange)
        *traveled = maxRange;

    bool timeUp = (elapsed + dt) >= maxDuration;
    *arrived = (*traveled >= capDistance) || timeUp;
    if (*arrived && *traveled > capDistance)
        *traveled = capDistance;
}

// 3d. Single-force-layer evaluation
Vector3 SkillHelper_EvaluateForceLayer(const ForceLayer *layer, Vector3 pos, Vector3 vel,
                                        float time, Vector3 axisOrigin, Vector3 axisDir) {
    ForceField ff = {0};
    ForceField_AddLayer(&ff, *layer);
    return ForceField_Evaluate(&ff, pos, vel, time, axisOrigin, axisDir);
}

// 3e. Configurable, always-additive tunable force mix
void SkillForceMix_AddLayers(const SkillForceMix *mix, ForceField *ff) {
    if (mix->windStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){
            .type = FORCE_WIND, .strength = mix->windStrength,
            .direction = { mix->windDirX, mix->windDirY, mix->windDirZ },
            .noiseScale = mix->windNoiseScale, .noiseSpeed = mix->windNoiseSpeed });
    if (mix->perlinStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){
            .type = FORCE_NOISE_PERLIN, .strength = mix->perlinStrength,
            .noiseScale = mix->perlinNoiseScale, .noiseSpeed = mix->perlinNoiseSpeed });
    if (mix->curlStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){
            .type = FORCE_NOISE_CURL, .strength = mix->curlStrength,
            .noiseScale = mix->curlNoiseScale, .noiseSpeed = mix->curlNoiseSpeed });
    if (mix->gravDirStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){
            .type = FORCE_GRAVITY_DIR, .strength = mix->gravDirStrength,
            .direction = { mix->gravDirX, mix->gravDirY, mix->gravDirZ } });
    if (mix->gravPtStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){
            .type = FORCE_GRAVITY_POINT, .strength = mix->gravPtStrength,
            .origin = { mix->gravPtOriginX, mix->gravPtOriginY, mix->gravPtOriginZ } });
    if (mix->vortexStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){
            .type = FORCE_VORTEX, .strength = mix->vortexStrength,
            .origin = { mix->vortexOriginX, mix->vortexOriginY, mix->vortexOriginZ },
            .direction = { mix->vortexDirX, mix->vortexDirY, mix->vortexDirZ } });
    if (mix->dragStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){ .type = FORCE_DRAG, .strength = mix->dragStrength });
    if (mix->viscosityStrength != 0.0f)
        ForceField_AddLayer(ff, (ForceLayer){ .type = FORCE_VISCOSITY, .strength = mix->viscosityStrength });
}

int SkillForceMix_MakeTunables(SkillForceMix *mix, const char *labelPrefix,
                                const char *phase, SkillTunableEntry *outEntries) {
    int n = 0;
    // dir_y-type fields default to 1.0 (not 0) — FORCE_WIND/FORCE_GRAVITY_DIR
    // compute their base force as direction*strength (core/force_field.c),
    // so an all-zero direction would silently make strength alone do ~nothing
    // even at max value. A default "mostly upward" direction means dialing
    // up just that type's strength already produces a visible push.
    struct { const char *suffix; float *field; float min, max, def; } fields[] = {
        {"wind_strength",     &mix->windStrength,     -8.0f, 8.0f, 0.0f},
        {"wind_dir_x",        &mix->windDirX,         -1.0f, 1.0f, 0.0f},
        {"wind_dir_y",        &mix->windDirY,         -1.0f, 1.0f, 1.0f},
        {"wind_dir_z",        &mix->windDirZ,         -1.0f, 1.0f, 0.0f},
        {"wind_noise_scale",  &mix->windNoiseScale,    0.0f, 5.0f, 1.5f},
        {"wind_noise_speed",  &mix->windNoiseSpeed,    0.0f, 5.0f, 0.6f},
        {"perlin_strength",   &mix->perlinStrength,   -8.0f, 8.0f, 0.0f},
        {"perlin_noise_scale",&mix->perlinNoiseScale,  0.0f, 5.0f, 1.5f},
        {"perlin_noise_speed",&mix->perlinNoiseSpeed,  0.0f, 5.0f, 0.6f},
        {"curl_strength",     &mix->curlStrength,     -8.0f, 8.0f, 0.0f},
        {"curl_noise_scale",  &mix->curlNoiseScale,    0.0f, 5.0f, 2.0f},
        {"curl_noise_speed",  &mix->curlNoiseSpeed,    0.0f, 5.0f, 1.0f},
        {"gravdir_strength",  &mix->gravDirStrength,  -8.0f, 8.0f, 0.0f},
        {"gravdir_x",         &mix->gravDirX,         -1.0f, 1.0f, 0.0f},
        {"gravdir_y",         &mix->gravDirY,         -1.0f, 1.0f, 1.0f},
        {"gravdir_z",         &mix->gravDirZ,         -1.0f, 1.0f, 0.0f},
        {"gravpt_strength",   &mix->gravPtStrength,   -8.0f, 8.0f, 0.0f},
        {"gravpt_origin_x",   &mix->gravPtOriginX,    -5.0f, 5.0f, 0.0f},
        {"gravpt_origin_y",   &mix->gravPtOriginY,    -5.0f, 5.0f, 0.0f},
        {"gravpt_origin_z",   &mix->gravPtOriginZ,    -5.0f, 5.0f, 0.0f},
        {"vortex_strength",   &mix->vortexStrength,   -8.0f, 8.0f, 0.0f},
        {"vortex_origin_x",   &mix->vortexOriginX,    -5.0f, 5.0f, 0.0f},
        {"vortex_origin_y",   &mix->vortexOriginY,    -5.0f, 5.0f, 0.0f},
        {"vortex_origin_z",   &mix->vortexOriginZ,    -5.0f, 5.0f, 0.0f},
        {"vortex_dir_x",      &mix->vortexDirX,       -1.0f, 1.0f, 0.0f},
        {"vortex_dir_y",      &mix->vortexDirY,       -1.0f, 1.0f, 1.0f},
        {"vortex_dir_z",      &mix->vortexDirZ,       -1.0f, 1.0f, 0.0f},
        {"drag_strength",     &mix->dragStrength,      0.0f, 20.0f, 0.0f},
        {"viscosity_strength",&mix->viscosityStrength, 0.0f, 20.0f, 0.0f},
    };
    for (int i = 0; i < SKILL_FORCE_MIX_TUNABLE_COUNT; i++) {
        *fields[i].field = fields[i].def; // seed the struct field itself — RegisterSkillTunables
                                           // never applies defaultValue on its own (unlike
                                           // Tuning_RegisterFloat); SkillTunables_LoadPersisted,
                                           // called right after this, overwrites from a saved
                                           // .tuning file if present, same ordering as SkillCurve_SetConstant.
        snprintf(outEntries[n].label, sizeof(outEntries[n].label), "%s%s", labelPrefix, fields[i].suffix);
        outEntries[n].value = fields[i].field;
        outEntries[n].min = fields[i].min;
        outEntries[n].max = fields[i].max;
        outEntries[n].defaultValue = fields[i].def;
        outEntries[n].phase = phase;
        outEntries[n].curve = NULL;
        n++;
    }
    return n;
}

// 4. Particle Emitter System Implementation
void EmitterSystem_Init(void) {
    InitHelperResources();
    for (int i = 0; i < MAX_EMITTERS; i++) {
        s_emitters[i].active = false;
    }
}

void EmitterSystem_Update(float dt) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!s_emitters[i].active) continue;

        s_emitters[i].timer += dt;
        if (s_emitters[i].timer >= s_emitters[i].duration) {
            s_emitters[i].active = false;
            continue;
        }

        s_emitters[i].spawnAccum += s_emitters[i].rate * dt;
        int count = (int)s_emitters[i].spawnAccum;
        s_emitters[i].spawnAccum -= count;

        ColorGradient *grad = NULL;
        ForceField *fld = NULL;

        switch (s_emitters[i].type) {
            case EMITTER_FIRE: grad = &s_fireGrad; fld = &s_fireFld; break;
            case EMITTER_SNOW: grad = &s_snowGrad; fld = &s_snowFld; break;
            case EMITTER_WATER_SPURT: grad = &s_waterGrad; fld = &s_waterFld; break;
            case EMITTER_SHOCKED_SPARKS: grad = &s_lightningGrad; fld = &s_lightningFld; break;
            case EMITTER_WOOD_LEAVES: grad = &s_woodGrad; fld = &s_woodFld; break;
            case EMITTER_EARTH_DUST: grad = &s_earthGrad; fld = &s_earthFld; break;
            case EMITTER_METAL_SPARKS: grad = &s_metalGrad; fld = &s_metalFld; break;
            case EMITTER_TAIJI_MOTES: grad = &s_taijiGrad; fld = &s_taijiFld; break;
        }

        for (int k = 0; k < count; k++) {
            // Real-world-scaled ÷100 (root CLAUDE.md §scale).
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.02f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.02f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.02f
            };
            Vector3 pos = Vector3Add(s_emitters[i].pos, offset);
            Vector3 vel = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.10f,
                ((float)rand() / (float)RAND_MAX * 0.15f + 0.10f),
                ((float)rand() / (float)RAND_MAX - 0.5f) * 0.10f
            };

            SpawnParticle((ParticleConfig){
                .position = pos,
                .velocity = vel,
                .radius = (float)GetRandomValue(8, 20) / 1000.0f,
                .lifetime = (float)GetRandomValue(5, 12) / 10.0f,
                .gradient = grad,
                .forceField = fld
            });
        }
    }
}

int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!s_emitters[i].active) {
            s_emitters[i].type = type;
            s_emitters[i].pos = pos;
            s_emitters[i].rate = ratePerSecond;
            s_emitters[i].duration = duration;
            s_emitters[i].timer = 0.0f;
            s_emitters[i].spawnAccum = 0.0f;
            s_emitters[i].active = true;
            return i;
        }
    }
    return -1;
}

void Emitter_Stop(int emitterId) {
    if (emitterId >= 0 && emitterId < MAX_EMITTERS) {
        s_emitters[emitterId].active = false;
    }
}

void EmitterSystem_Unload(void) {
    EmitterSystem_Init();
}

static void DrawCorePyramid(Vector3 pos, float radius, float height, Color color) {
    Vector3 top = { pos.x, pos.y + height, pos.z };
    Vector3 base[4];
    for (int i = 0; i < 4; i++) {
        float angle = i * 0.5f * 3.14159265f;
        base[i] = (Vector3){ pos.x + cosf(angle) * radius, pos.y, pos.z + sinf(angle) * radius };
    }

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        
        // Tính face normal cho mặt bên
        Vector3 edge1 = Vector3Subtract(base[next], base[i]);
        Vector3 edge2 = Vector3Subtract(top, base[i]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        
        rlNormal3f(normal.x, normal.y, normal.z);
        rlVertex3f(base[i].x, base[i].y, base[i].z);
        rlVertex3f(base[next].x, base[next].y, base[next].z);
        rlVertex3f(top.x, top.y, top.z);
    }
    
    // Đáy hình vuông (2 tam giác)
    Vector3 edgeB1 = Vector3Subtract(base[1], base[0]);
    Vector3 edgeB2 = Vector3Subtract(base[3], base[0]);
    Vector3 baseNormal = Vector3Normalize(Vector3CrossProduct(edgeB2, edgeB1)); // Hướng xuống dưới
    rlNormal3f(baseNormal.x, baseNormal.y, baseNormal.z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlVertex3f(base[1].x, base[1].y, base[1].z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[3].x, base[3].y, base[3].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlEnd();
}

static void DrawCoreTetrahedron(Vector3 pos, float radius, float height, Color color) {
    Vector3 top = { pos.x, pos.y + height, pos.z };
    Vector3 base[3];
    for (int i = 0; i < 3; i++) {
        float angle = i * (2.0f * 3.14159265f / 3.0f);
        base[i] = (Vector3){ pos.x + cosf(angle) * radius, pos.y, pos.z + sinf(angle) * radius };
    }

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 3; i++) {
        int next = (i + 1) % 3;
        
        // Tính face normal cho mặt bên
        Vector3 edge1 = Vector3Subtract(base[next], base[i]);
        Vector3 edge2 = Vector3Subtract(top, base[i]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        
        rlNormal3f(normal.x, normal.y, normal.z);
        rlVertex3f(base[i].x, base[i].y, base[i].z);
        rlVertex3f(base[next].x, base[next].y, base[next].z);
        rlVertex3f(top.x, top.y, top.z);
    }
    
    // Đáy tam giác
    Vector3 edgeB1 = Vector3Subtract(base[1], base[0]);
    Vector3 edgeB2 = Vector3Subtract(base[2], base[0]);
    Vector3 baseNormal = Vector3Normalize(Vector3CrossProduct(edgeB2, edgeB1));
    rlNormal3f(baseNormal.x, baseNormal.y, baseNormal.z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlVertex3f(base[1].x, base[1].y, base[1].z);
    rlEnd();
}

// 5. Mesh Preset Implementation
void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color) {
    // Đảm bảo reset vertex color để shader chạy đúng
    rlColor4ub(color.r, color.g, color.b, color.a);

    switch (type) {
        case MESH_PRESET_DISC:
            DrawCorePlanePolygon(pos, scale.x, 24, color);
            break;
        case MESH_PRESET_RING:
            DrawCoreTorus(pos, scale.x * 0.85f, scale.x, 16, 24, color);
            break;
        case MESH_PRESET_CONE:
            DrawCoreCone(pos, scale.x, scale.y, 16, color);
            break;
        case MESH_PRESET_TORNADO: {
            for (int i = 0; i < 3; i++) {
                float r = scale.x * (1.0f + i * 0.15f);
                DrawCoreCylinder(pos, (Vector3){pos.x, pos.y + scale.y, pos.z}, r * 0.5f, r, 12, color);
            }
            break;
        }
        case MESH_PRESET_CYLINDER:
            DrawCoreCylinder(pos, (Vector3){pos.x, pos.y + scale.y, pos.z}, scale.x, scale.x, 16, color);
            break;
        case MESH_PRESET_SPHERE:
            DrawCoreSphere(pos, scale.x, 16, 16, color);
            break;
        case MESH_PRESET_SHOCKWAVE:
            DrawCoreTorus(pos, scale.x * 0.92f, scale.x, 8, 24, color);
            break;
        case MESH_PRESET_PYRAMID:
            DrawCorePyramid(pos, scale.x, scale.y, color);
            break;
        case MESH_PRESET_TETRAHEDRON:
            DrawCoreTetrahedron(pos, scale.x, scale.y, color);
            break;
    }
}

// 6. Shader Material System Implementation
EffectMaterial Material_LoadElement(EffectPresetType element) {
    EffectMaterialParams p = {0};
    switch (element) {
        case EFFECT_PRESET_WATER_SPLASH:
            p.baseColor = ELEMENT_COLOR_WATER;
            p.rimStrength = 1.0f; p.fresnelPower = 4.0f;
            p.emissiveIntensity = 0.6f; p.distortionStrength = 0.25f;
            p.translucency = 0.85f;
            break;
        case EFFECT_PRESET_WOOD_BLOOM:
            p.baseColor = ELEMENT_COLOR_WOOD;
            p.rimStrength = 0.8f; p.fresnelPower = 3.5f;
            p.emissiveIntensity = 0.7f; p.distortionStrength = 0.15f;
            p.translucency = 0.3f;
            break;
        case EFFECT_PRESET_FIRE_EXPLOSION:
            p.baseColor = ELEMENT_COLOR_FIRE;
            p.rimStrength = 1.2f; p.fresnelPower = 3.0f;
            p.emissiveIntensity = 1.5f; p.distortionStrength = 0.4f;
            p.translucency = 0.0f;
            break;
        case EFFECT_PRESET_EARTH_CRACK:
            p.baseColor = ELEMENT_COLOR_EARTH;
            p.rimStrength = 0.5f; p.fresnelPower = 2.5f;
            p.emissiveIntensity = 0.4f; p.distortionStrength = 0.05f;
            p.translucency = 0.0f;
            break;
        case EFFECT_PRESET_METAL_SHARD:
            p.baseColor = ELEMENT_COLOR_METAL;
            p.rimStrength = 1.8f; p.fresnelPower = 6.0f;
            p.emissiveIntensity = 1.0f; p.distortionStrength = 0.08f;
            p.translucency = 0.2f;
            break;
        case EFFECT_PRESET_TAIJI_BURST:
            p.baseColor = ELEMENT_COLOR_TAIJI;
            p.rimStrength = 2.0f; p.fresnelPower = 2.0f;
            p.emissiveIntensity = 2.0f; p.distortionStrength = 0.6f;
            p.translucency = 0.3f;
            break;
        default:
            break;
    }
    return Material_LoadCustom(p);
}

// 7. Ground Decal Implementation
void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration) {
    Texture2D tex = {0};
    Color tint = WHITE;

    switch (type) {
        // Earth
        case DECAL_PRESET_CRACK:
            tex = ResourceManager_LoadTexture("assets/textures/crack.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.7f);
            break;
        case DECAL_PRESET_EARTH_SHATTER:
            tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.75f);
            break;
        case DECAL_PRESET_EARTH_RUNE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_earth_rune.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f);
            break;

        // Fire
        case DECAL_PRESET_BURN:
            tex = ResourceManager_LoadTexture("assets/textures/scorch_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.65f);
            break;
        case DECAL_PRESET_FIRE_LAVA:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lava_crack.png");
            tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.8f);
            break;

        // Water
        case DECAL_PRESET_WATER:
            tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
            break;
        case DECAL_PRESET_WATER_SPLASH:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_splash_ring.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.6f);
            break;
        case DECAL_PRESET_WATER_RIPPLE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
            break;
        case DECAL_PRESET_ICE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_frost_ring.png");
            tint = ColorAlpha(WHITE, 0.55f);
            break;

        // Wood
        case DECAL_PRESET_WOOD_ROOT:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_root_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.75f);
            break;
        case DECAL_PRESET_WOOD_MOSS:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
            tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f);
            break;

        // Metal
        case DECAL_PRESET_METAL_SLASH:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_slash_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.8f);
            break;
        case DECAL_PRESET_METAL_CRATER:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_impact_crater.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.75f);
            break;
        case DECAL_PRESET_METAL_RUNE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_metal_rune.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.85f);
            break;

        // Taiji
        case DECAL_PRESET_TAIJI_RING:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_taiji_ring.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.8f);
            break;
        case DECAL_PRESET_TAIJI_LIGHTNING:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lightning_char.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.85f);
            break;
        case DECAL_PRESET_TAIJI_WIND:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_wind_groove.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.5f);
            break;

        // Generic — no element tint baked in, caller adjusts via radius/duration only
        case DECAL_PRESET_GENERIC_IMPACT_RING:
            tex = ResourceManager_LoadTexture("assets/textures/generic/impact_ring.png");
            tint = ColorAlpha(WHITE, 0.7f);
            break;
        case DECAL_PRESET_GENERIC_GLOW:
            tex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
            tint = ColorAlpha(WHITE, 0.4f);
            break;
        case DECAL_PRESET_GENERIC_SHADOW:
            tex = ResourceManager_LoadTexture("assets/textures/generic/shadow_blob.png");
            tint = ColorAlpha(BLACK, 0.5f);
            break;
    }

    Vector3 decalPos = { pos.x, pos.y + 0.02f, pos.z };
    float rotation = (float)GetRandomValue(0, 360);

    // CORE_ISSUES.md Item 4b — lava crack / ripple decal cuộn ra ngoài tâm
    // theo thời gian (decal_flow.fs) thay vì texture đứng yên như mọi preset
    // khác. Glow chỉ bật cho FIRE_LAVA (khe nứt phát sáng) — WATER_RIPPLE
    // cuộn nhưng không cần glow.
    if (type == DECAL_PRESET_FIRE_LAVA) {
        DecalSystem_AddFlowEx(decalPos, rotation, 0.0f, radius, radius, tex,
                              duration, tint, BLEND_ALPHA, 0.02f, 0.6f, 0.8f, 1.5f);
    } else if (type == DECAL_PRESET_WATER_RIPPLE) {
        DecalSystem_AddFlowEx(decalPos, rotation, 0.0f, radius, radius, tex,
                              duration, tint, BLEND_ALPHA, 0.02f, 0.6f, 0.8f, 0.0f);
    } else {
        DecalSystem_Add(decalPos, rotation, radius, tex, duration, tint);
    }
}

void SpawnGroundDecalEx(DecalPresetType type, Vector3 pos,
                        float scaleStart, float scaleEnd, float lifetime,
                        float rotSpeed, float yOffset) {
    Texture2D tex = {0};
    Color tint = WHITE;

    switch (type) {
        case DECAL_PRESET_CRACK:          tex = ResourceManager_LoadTexture("assets/textures/crack.png"); tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.7f); break;
        case DECAL_PRESET_EARTH_SHATTER:  tex = ResourceManager_LoadTexture("assets/textures/tex_crack_mask.png"); tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.75f); break;
        case DECAL_PRESET_EARTH_RUNE:     tex = ResourceManager_LoadTexture("assets/textures/decals/decal_earth_rune.png"); tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f); break;
        case DECAL_PRESET_BURN:           tex = ResourceManager_LoadTexture("assets/textures/scorch_mark.png"); tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.65f); break;
        case DECAL_PRESET_FIRE_LAVA:      tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lava_crack.png"); tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.8f); break;
        case DECAL_PRESET_WATER:          tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png"); tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f); break;
        case DECAL_PRESET_WATER_SPLASH:   tex = ResourceManager_LoadTexture("assets/textures/decals/decal_splash_ring.png"); tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.6f); break;
        case DECAL_PRESET_WATER_RIPPLE:   tex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png"); tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f); break;
        case DECAL_PRESET_ICE:            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_frost_ring.png"); tint = ColorAlpha(WHITE, 0.55f); break;
        case DECAL_PRESET_WOOD_ROOT:      tex = ResourceManager_LoadTexture("assets/textures/decals/decal_root_mark.png"); tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.75f); break;
        case DECAL_PRESET_WOOD_MOSS:      tex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png"); tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f); break;
        case DECAL_PRESET_METAL_SLASH:    tex = ResourceManager_LoadTexture("assets/textures/decals/decal_slash_mark.png"); tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.8f); break;
        case DECAL_PRESET_METAL_CRATER:   tex = ResourceManager_LoadTexture("assets/textures/decals/decal_impact_crater.png"); tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.75f); break;
        case DECAL_PRESET_METAL_RUNE:     tex = ResourceManager_LoadTexture("assets/textures/decals/decal_metal_rune.png"); tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.85f); break;
        case DECAL_PRESET_TAIJI_RING:     tex = ResourceManager_LoadTexture("assets/textures/decals/decal_taiji_ring.png"); tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.8f); break;
        case DECAL_PRESET_TAIJI_LIGHTNING: tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lightning_char.png"); tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.85f); break;
        case DECAL_PRESET_TAIJI_WIND:     tex = ResourceManager_LoadTexture("assets/textures/decals/decal_wind_groove.png"); tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.5f); break;
        case DECAL_PRESET_GENERIC_IMPACT_RING: tex = ResourceManager_LoadTexture("assets/textures/generic/impact_ring.png"); tint = ColorAlpha(WHITE, 0.7f); break;
        case DECAL_PRESET_GENERIC_GLOW:   tex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png"); tint = ColorAlpha(WHITE, 0.4f); break;
        case DECAL_PRESET_GENERIC_SHADOW: tex = ResourceManager_LoadTexture("assets/textures/generic/shadow_blob.png"); tint = ColorAlpha(BLACK, 0.5f); break;
    }

    Vector3 decalPos = { pos.x, pos.y + yOffset, pos.z };
    float rotation = (float)GetRandomValue(0, 360);
    DecalSystem_AddEx(decalPos, rotation, rotSpeed, scaleStart, scaleEnd, tex,
                      lifetime, tint, BLEND_ALPHA, yOffset);
}

// 8. Camera Impulse Implementation
void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse) {
    float dist = Vector3Distance(origin, camera.target);
    float factor = 1.0f / (1.0f + dist * impulse.falloff);
    float resultTrauma = impulse.magnitude * factor;
    if (resultTrauma > 0.05f) {
        CameraFX_Shake(resultTrauma);
    }
}

// 9. ForceField Preset Implementation
ForceField ForceField_CreatePreset(ForceFieldPreset preset) {
    ForceField fld;
    ForceField_Clear(&fld);

    switch (preset) {
        case FORCE_PRESET_FIRE_UPDRAFT:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 20.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 12.0f, .noiseScale = 0.06f, .noiseSpeed = 1.2f });
            break;
        case FORCE_PRESET_SNOW_BLIZZARD:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.6f, -1.0f, 0.3f}, .strength = 14.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 8.0f, .noiseScale = 0.08f, .noiseSpeed = 0.9f });
            break;
        case FORCE_PRESET_WATER_VORTEX:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 30.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 18.0f, .noiseScale = 0.05f, .noiseSpeed = 2.2f });
            break;
        case FORCE_PRESET_EARTH_RUMBLE:
            // Heavy downward pull + low-freq curl — debris/rubble feel
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 40.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.04f, .noiseSpeed = 0.5f });
            break;
        case FORCE_PRESET_WOOD_GROWTH:
            // Slow upward drift + mid-freq swaying curl — vines/spores feel
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 8.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 10.0f, .noiseScale = 0.07f, .noiseSpeed = 0.8f });
            break;
        case FORCE_PRESET_METAL_IMPLOSION:
            // Radial inward pull — shards/sparks sucked to impact point
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 25.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 22.0f, .noiseScale = 0.1f, .noiseSpeed = 3.5f });
            break;
        case FORCE_PRESET_TAIJI_ORBIT:
            // Persistent high-speed curl — yin-yang orbital motion
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 35.0f, .noiseScale = 0.06f, .noiseSpeed = 4.0f });
            break;
    }
    return fld;
}

// 10. Skill Builder Implementation
void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale) {
    ctx->target = target;
    ctx->scale = scale;
    ctx->hasExplosion = false;
    ctx->hasDecal = false;
    ctx->hasDamageVolume = false;
}

void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx) {
    ctx->hasExplosion = true;
    ctx->explosionEffect = vfx;
}

void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration) {
    ctx->hasDecal = true;
    ctx->decalType = decal;
    ctx->decalRadius = radius;
    ctx->decalDuration = duration;
}

void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration) {
    ctx->hasDamageVolume = true;
    ctx->damageRadius = radius;
    ctx->damageDps = dps;
    ctx->damageDuration = duration;
}

// Cast-stage hook — its own trigger point, separate from SkillBuilder_Build()
// which fires at impact. Call after SkillBuilder_Start() (needs ctx->target/
// ctx->scale already set) at cast time; fires SpawnCastEffect immediately
// rather than deferring it, since cast happens earlier in the skill
// lifecycle than the impact-time Build() call.
void SkillBuilder_AddCastEffect(SkillBuildContext *ctx, EffectPresetType preset) {
    SpawnCastEffect(ctx->target, preset, ctx->scale);
}

void SkillBuilder_Build(SkillBuildContext *ctx) {
    if (ctx->hasExplosion) {
        SpawnImpactEffect(ctx->target, ctx->explosionEffect, ctx->scale);
    }
    if (ctx->hasDecal) {
        SpawnGroundDecal(ctx->decalType, ctx->target, ctx->decalRadius * ctx->scale, ctx->decalDuration);
    }
    if (ctx->hasDamageVolume) {
        DamageVolume vol = {
            .shape = SHAPE_CIRCLE,
            .center = ctx->target,
            .radius = ctx->damageRadius * ctx->scale,
            .damagePerSecond = ctx->damageDps,
            .tickInterval = 0.25f,
            .duration = ctx->damageDuration
        };
        SpawnDamageVolume(vol);
    }
}

// ============================================================
// 11. SkillBuilder archetype extensions (Item 23)
// ============================================================

#include "core/vfx_proc_ray.h"
#include "core/vfx_light.h"
#include "core/emitter_system.h"
#include "raymath.h"
#ifndef PI
#define PI 3.1415926535f
#endif

#define SB_MAX_BEAMS       8
#define SB_MAX_GROUNDWAVES 8
#define SB_MAX_ORBITALS    8
#define SB_ORBITALS_PER_GROUP 8
#define SB_MAX_AURAS       8
#define SB_AURA_RING_K     8

typedef struct {
    bool  active;
    int   procRayId;
    Vector3 from, to;
    float duration, elapsed;
    EffectPresetType element;
} SB_Beam;

typedef struct {
    bool  active;
    Vector3 origin, dir;
    float range, speed;
    float elapsed;
    EffectPresetType element;
    float scale;
} SB_GroundWave;

typedef struct {
    bool  active;
    Vector3 center;
    EffectPresetType element;
    int   count;
    float radius, duration, elapsed;
    float phases[SB_ORBITALS_PER_GROUP];
    int   trailIds[SB_ORBITALS_PER_GROUP];
} SB_Orbital;

typedef struct {
    bool  active;
    Vector3 center;
    EffectPresetType element;
    float radius, duration, elapsed;
    int   emitterIds[SB_AURA_RING_K];
} SB_Aura;

static SB_Beam       s_beams[SB_MAX_BEAMS];
static SB_GroundWave s_gwaves[SB_MAX_GROUNDWAVES];
static SB_Orbital    s_orbitals[SB_MAX_ORBITALS];
static SB_Aura       s_auras[SB_MAX_AURAS];

// colour helper: EffectPresetType → ELEMENT_COLOR_*
static Color SB_ElementColor(EffectPresetType e) {
    switch (e) {
        case EFFECT_PRESET_WATER_SPLASH:   return ELEMENT_COLOR_WATER;
        case EFFECT_PRESET_WOOD_BLOOM:     return ELEMENT_COLOR_WOOD;
        case EFFECT_PRESET_FIRE_EXPLOSION: return ELEMENT_COLOR_FIRE;
        case EFFECT_PRESET_EARTH_CRACK:    return ELEMENT_COLOR_EARTH;
        case EFFECT_PRESET_METAL_SHARD:    return ELEMENT_COLOR_METAL;
        case EFFECT_PRESET_TAIJI_BURST:    return ELEMENT_COLOR_TAIJI;
        default:                           return WHITE;
    }
}

int SkillBuilder_SpawnBeam(Vector3 from, Vector3 to, EffectPresetType element,
                           float width, float duration) {
    (void)width;
    for (int i = 0; i < SB_MAX_BEAMS; i++) {
        if (!s_beams[i].active) {
            s_beams[i].active = true;
            s_beams[i].from = from;
            s_beams[i].to   = to;
            s_beams[i].element = element;
            s_beams[i].duration = duration;
            s_beams[i].elapsed  = 0.0f;
            s_beams[i].procRayId = SpawnProcRay(ProcRay_EnergyConfig(), 1.0f);
            ProcRay_SetBrightness(s_beams[i].procRayId, 1.4f);
            Color c = SB_ElementColor(element);
            VFXLight_Spawn(from, c, 2.5f, duration, VFX_PRIORITY_LOW);
            VFXLight_Spawn(to,   c, 2.5f, duration, VFX_PRIORITY_LOW);
            return i;
        }
    }
    return -1;
}

void SkillBuilder_KillBeam(int handle) {
    if (handle < 0 || handle >= SB_MAX_BEAMS || !s_beams[handle].active) return;
    ProcRay_Kill(s_beams[handle].procRayId);
    s_beams[handle].active = false;
}

void SkillBuilder_SpawnGroundWave(Vector3 origin, Vector3 dir,
                                  EffectPresetType element,
                                  float range, float speed) {
    float len = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (len > 0.001f) { dir.x /= len; dir.y /= len; dir.z /= len; }
    for (int i = 0; i < SB_MAX_GROUNDWAVES; i++) {
        if (!s_gwaves[i].active) {
            s_gwaves[i].active = true;
            s_gwaves[i].origin  = origin;
            s_gwaves[i].dir     = dir;
            s_gwaves[i].range   = range;
            s_gwaves[i].speed   = speed;
            s_gwaves[i].elapsed = 0.0f;
            s_gwaves[i].element = element;
            s_gwaves[i].scale   = 0.5f;
            SpawnImpactEffect(origin, element, 0.4f);
            SpawnCastEffect(origin, element, 0.5f);
            return;
        }
    }
}

int SkillBuilder_SpawnOrbitals(Vector3 center, EffectPresetType element,
                               int count, float radius, float duration) {
    if (count > SB_ORBITALS_PER_GROUP) count = SB_ORBITALS_PER_GROUP;
    for (int i = 0; i < SB_MAX_ORBITALS; i++) {
        if (!s_orbitals[i].active) {
            s_orbitals[i].active   = true;
            s_orbitals[i].center   = center;
            s_orbitals[i].element  = element;
            s_orbitals[i].count    = count;
            s_orbitals[i].radius   = radius;
            s_orbitals[i].duration = duration;
            s_orbitals[i].elapsed  = 0.0f;
            for (int j = 0; j < count; j++) {
                s_orbitals[i].phases[j] = (2.0f * PI * j) / count;
                s_orbitals[i].trailIds[j] = -1;
            }
            return i;
        }
    }
    return -1;
}

int SkillBuilder_SpawnAuraRing(Vector3 center, EffectPresetType element,
                               float radius, float duration) {
    for (int i = 0; i < SB_MAX_AURAS; i++) {
        if (!s_auras[i].active) {
            s_auras[i].active   = true;
            s_auras[i].center   = center;
            s_auras[i].element  = element;
            s_auras[i].radius   = radius;
            s_auras[i].duration = duration;
            s_auras[i].elapsed  = 0.0f;
            Color c = SB_ElementColor(element);
            for (int k = 0; k < SB_AURA_RING_K; k++) {
                float angle = (2.0f * PI * k) / SB_AURA_RING_K;
                Vector3 p = { center.x + cosf(angle) * radius, center.y,
                              center.z + sinf(angle) * radius };
                EmitterConfig ecfg = {0};
                ecfg.baseParticle.colorStart = c;
                ecfg.baseParticle.colorEnd   = ColorAlpha(c, 0);
                ecfg.baseParticle.radius     = 0.08f;
                ecfg.baseParticle.lifetime   = 1.2f;
                ecfg.baseParticle.velocity   = (Vector3){0, 0.5f, 0};
                ecfg.spawnRate = 4.0f;
                ecfg.randomPosOffset = 0.05f;
                s_auras[i].emitterIds[k] = CreateEmitter(ecfg, p);
            }
            SpawnImpactEffect(center, element, radius * 0.5f);
            VFXLight_Spawn(center, c, radius * 1.5f, duration, VFX_PRIORITY_LOW);
            return i;
        }
    }
    return -1;
}

void SkillBuilder_KillAuraRing(int handle) {
    if (handle < 0 || handle >= SB_MAX_AURAS || !s_auras[handle].active) return;
    for (int k = 0; k < SB_AURA_RING_K; k++)
        if (s_auras[handle].emitterIds[k] >= 0)
            StopEmitter(s_auras[handle].emitterIds[k]);
    s_auras[handle].active = false;
}

void SkillBuilder_Update(float dt) {
    for (int i = 0; i < SB_MAX_BEAMS; i++) {
        if (!s_beams[i].active) continue;
        s_beams[i].elapsed += dt;
        if (s_beams[i].elapsed >= s_beams[i].duration) {
            ProcRay_Kill(s_beams[i].procRayId);
            s_beams[i].active = false;
        }
    }
    for (int i = 0; i < SB_MAX_GROUNDWAVES; i++) {
        if (!s_gwaves[i].active) continue;
        s_gwaves[i].elapsed += dt;
        float travelDist = s_gwaves[i].speed * s_gwaves[i].elapsed;
        if (travelDist >= s_gwaves[i].range) s_gwaves[i].active = false;
    }
    for (int i = 0; i < SB_MAX_ORBITALS; i++) {
        if (!s_orbitals[i].active) continue;
        s_orbitals[i].elapsed += dt;
        s_orbitals[i].center = s_orbitals[i].center; // static center
        if (s_orbitals[i].elapsed >= s_orbitals[i].duration)
            s_orbitals[i].active = false;
    }
    for (int i = 0; i < SB_MAX_AURAS; i++) {
        if (!s_auras[i].active) continue;
        s_auras[i].elapsed += dt;
        if (s_auras[i].elapsed >= s_auras[i].duration) {
            SkillBuilder_KillAuraRing(i);
        }
    }
}

void SkillBuilder_DrawWorld(Camera3D cam) {
    for (int i = 0; i < SB_MAX_BEAMS; i++) {
        if (!s_beams[i].active) continue;
        Vector3 dir = Vector3Subtract(s_beams[i].to, s_beams[i].from);
        float len = Vector3Length(dir);
        if (len < 0.001f) continue;
        dir = Vector3Scale(dir, 1.0f / len);
        ProcRay_Update(s_beams[i].procRayId, s_beams[i].from, dir, len, 1.0f, 0.0f);
        ProcRay_Draw(s_beams[i].procRayId, cam);
    }
    for (int i = 0; i < SB_MAX_GROUNDWAVES; i++) {
        if (!s_gwaves[i].active) continue;
        float r = s_gwaves[i].speed * s_gwaves[i].elapsed;
        Color c = SB_ElementColor(s_gwaves[i].element);
        DrawEffectMesh(MESH_PRESET_SHOCKWAVE,
                       s_gwaves[i].origin, (Vector3){r, 0.15f, r}, c);
    }
    for (int i = 0; i < SB_MAX_ORBITALS; i++) {
        if (!s_orbitals[i].active) continue;
        float angSpeed = 1.5f;
        float baseAngle = s_orbitals[i].elapsed * angSpeed;
        Color c = SB_ElementColor(s_orbitals[i].element);
        float alpha = 1.0f - s_orbitals[i].elapsed / s_orbitals[i].duration;
        c.a = (unsigned char)(c.a * alpha);
        for (int j = 0; j < s_orbitals[i].count; j++) {
            float ang = baseAngle + s_orbitals[i].phases[j];
            float scale = 0.12f + 0.04f * sinf(ang * 2.3f);
            Vector3 p = {
                s_orbitals[i].center.x + cosf(ang) * s_orbitals[i].radius,
                s_orbitals[i].center.y + 0.3f + 0.1f * sinf(ang * 1.7f + j),
                s_orbitals[i].center.z + sinf(ang) * s_orbitals[i].radius
            };
            DrawEffectMesh(MESH_PRESET_TETRAHEDRON, p,
                           (Vector3){scale, scale, scale}, c);
        }
    }
}

// ============================================================
// 12. Chain-targeting helper (Item 28)
// ============================================================

#define CHAIN_MAX_QUEUE 32
typedef struct {
    Vector3 from, to;
    float   delay;
    float   elapsed;
    float   scale;
    bool    active;
} ChainLightningEntry;

static ChainLightningEntry s_chainQueue[CHAIN_MAX_QUEUE];

int SkillHelper_ChainTargets(Vector3 origin, float jumpRadius,
                             int maxJumps, Vector3 *outPoints, int maxOut) {
    if (!outPoints || maxOut <= 0 || maxJumps <= 0) return 0;
    int visited[16] = {-1, -1, -1, -1, -1, -1, -1, -1,
                       -1, -1, -1, -1, -1, -1, -1, -1};
    int visitedCount = 0;
    int found = 0;
    Vector3 current = origin;

    while (found < maxJumps && found < maxOut) {
        int ids[16];
        int n = SkillManager_GetNearbyTargets(current, jumpRadius, ids, 16);
        if (n == 0) break;

        int best = -1;
        float bestDist = 1e30f;
        for (int i = 0; i < n; i++) {
            bool alreadyHit = false;
            for (int v = 0; v < visitedCount; v++)
                if (visited[v] == ids[i]) { alreadyHit = true; break; }
            if (alreadyHit) continue;
            Vector3 p;
            if (!SkillManager_GetAgentPos(ids[i], &p)) continue;
            float dx = p.x - current.x, dz = p.z - current.z;
            float d = sqrtf(dx*dx + dz*dz);
            if (d < bestDist) { bestDist = d; best = ids[i]; }
        }
        if (best < 0) break;

        Vector3 nextPos;
        if (!SkillManager_GetAgentPos(best, &nextPos)) break;
        outPoints[found++] = nextPos;
        if (visitedCount < 16) visited[visitedCount++] = best;
        current = nextPos;
    }
    return found;
}

void SpawnChainLightning(const Vector3 *points, int count,
                         float scale, float hopDelay) {
    if (!points || count < 2) return;
    for (int i = 0; i + 1 < count; i++) {
        for (int j = 0; j < CHAIN_MAX_QUEUE; j++) {
            if (!s_chainQueue[j].active) {
                s_chainQueue[j].active  = true;
                s_chainQueue[j].from    = points[i];
                s_chainQueue[j].to      = points[i + 1];
                s_chainQueue[j].delay   = hopDelay * i;
                s_chainQueue[j].elapsed = 0.0f;
                s_chainQueue[j].scale   = scale;
                break;
            }
        }
    }
}

void SkillHelper_Update(float dt) {
    for (int i = 0; i < CHAIN_MAX_QUEUE; i++) {
        if (!s_chainQueue[i].active) continue;
        s_chainQueue[i].elapsed += dt;
        if (s_chainQueue[i].elapsed >= s_chainQueue[i].delay) {
            SpawnLightningTrail(s_chainQueue[i].from, s_chainQueue[i].to,
                                s_chainQueue[i].scale, 8.0f);
            s_chainQueue[i].active = false;
        }
    }
    SkillBuilder_Update(dt);
}

// ============================================================
// Item 32: DamageVolume stats
// ============================================================
void DamageVolume_GetStats(int *active, int *max) {
    int n = 0;
    for (int i = 0; i < MAX_VOLUMES; i++)
        if (s_volumes[i].active) n++;
    *active = n;
    *max = MAX_VOLUMES;
}
