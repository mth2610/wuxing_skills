#ifndef SKILL_HELPER_H
#define SKILL_HELPER_H

#include "raylib.h"
#include <stdbool.h>

// 1. Effect Preset
typedef enum {
    EFFECT_PRESET_FIRE_EXPLOSION,
    EFFECT_PRESET_ICE_SHATTER,
    EFFECT_PRESET_WATER_SPLASH,
    EFFECT_PRESET_LIGHTNING_IMPACT,
    EFFECT_PRESET_EARTH_CRACK
} EffectPresetType;

void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale);

// 2. Damage Volume
typedef enum {
    SHAPE_CIRCLE,
    SHAPE_BOX,
    SHAPE_CONE
} ShapeType;

typedef struct {
    ShapeType shape;
    Vector3 center;
    float radius;
    float damagePerSecond;
    float tickInterval;
    float duration;
    bool active;
    float timer;
    float tickTimer;
} DamageVolume;

void DamageVolume_Init(void);
void DamageVolume_Update(float dt);
int SpawnDamageVolume(DamageVolume config);
void DamageVolume_Unload(void);

// 3. Skill Timeline
typedef struct {
    float current;
    float duration;
} SkillTimeline;

void Timeline_Start(SkillTimeline *t, float duration);
bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt);
bool Timeline_Finished(SkillTimeline *t);

// 4. Particle Emitter System
typedef enum {
    EMITTER_FIRE,
    EMITTER_SNOW,
    EMITTER_WATER_SPURT,
    EMITTER_SHOCKED_SPARKS
} EmitterPreset;

typedef struct {
    EmitterPreset type;
    Vector3 pos;
    float rate;       // Số hạt phát ra mỗi giây
    float duration;   // Thời gian tồn tại
    float timer;      // Bộ đếm thời gian
    float spawnAccum; // Tích luỹ hạt sinh
    bool active;
} ParticleEmitter;

void EmitterSystem_Init(void);
void EmitterSystem_Update(float dt);
int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration);
void Emitter_Stop(int emitterId);
void EmitterSystem_Unload(void);

// 5. Mesh Preset
typedef enum {
    MESH_PRESET_DISC,
    MESH_PRESET_RING,
    MESH_PRESET_CONE,
    MESH_PRESET_TORNADO,
    MESH_PRESET_CYLINDER,
    MESH_PRESET_SPHERE,
    MESH_PRESET_SHOCKWAVE,
    MESH_PRESET_PYRAMID,
    MESH_PRESET_TETRAHEDRON
} MeshPresetType;

void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color);

// 6. Shader Material System
typedef enum {
    MATERIAL_FIRE,
    MATERIAL_ICE,
    MATERIAL_WATER,
    MATERIAL_PORTAL
} MaterialPreset;

typedef struct {
    Shader shader;
    MaterialPreset preset;
    int uTimeLoc;
    int uDissolveLoc;
} EffectMaterial;

EffectMaterial Material_Load(MaterialPreset preset);
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
void Material_Begin(EffectMaterial mat);
void Material_End(void);

// 7. Ground Decal
typedef enum {
    DECAL_PRESET_BURN,
    DECAL_PRESET_CRACK,
    DECAL_PRESET_ICE,
    DECAL_PRESET_WATER
} DecalPresetType;

void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration);

// 8. Camera Impulse
typedef struct {
    float magnitude;
    float duration;
    float frequency;
    float falloff;
} CameraImpulse;

void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse);

// 9. ForceField Preset
typedef enum {
    FORCE_PRESET_FIRE_UPDRAFT,
    FORCE_PRESET_SNOW_BLIZZARD,
    FORCE_PRESET_WATER_VORTEX
} ForceFieldPreset;

#include "core/force_field.h"
ForceField ForceField_CreatePreset(ForceFieldPreset preset);

// 10. Skill Builder
typedef struct {
    Vector3 target;
    float scale;
    bool hasExplosion;
    EffectPresetType explosionEffect;
    bool hasDecal;
    DecalPresetType decalType;
    float decalRadius;
    float decalDuration;
    bool hasDamageVolume;
    float damageRadius;
    float damageDps;
    float damageDuration;
} SkillBuildContext;

void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale);
void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx);
void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration);
void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration);
void SkillBuilder_Build(SkillBuildContext *ctx);

#endif // SKILL_HELPER_H
