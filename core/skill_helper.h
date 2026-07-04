#ifndef SKILL_HELPER_H
#define SKILL_HELPER_H

#include "raylib.h"
#include "core/skill_curve.h"
#include "core/force_field.h"
#include "core/skill_manager.h"
#include <stdbool.h>

// 1. Effect Preset
typedef enum {
    EFFECT_PRESET_FIRE_EXPLOSION,
    EFFECT_PRESET_ICE_SHATTER,
    EFFECT_PRESET_WATER_SPLASH,
    EFFECT_PRESET_LIGHTNING_IMPACT,
    EFFECT_PRESET_EARTH_CRACK,
    EFFECT_PRESET_WOOD_BLOOM,
    EFFECT_PRESET_METAL_SHARD,
    EFFECT_PRESET_TAIJI_BURST
} EffectPresetType;

void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale);

// Cast/windup variant — sustained "energy gathering" effect at the caster,
// no knockback/decal. Reuses EffectPresetType.
void SpawnCastEffect(Vector3 pos, EffectPresetType preset, float scale);

// Flight-stage variant — projectile trail + tail particles while a skill
// is flying from start to target. Reuses EffectPresetType. Returns the
// trail ID; caller MUST call KillTrail(id) on impact (e.g. right before
// SpawnImpactEffect at the target).
int SpawnProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed);

// Lightning Trail Presets — dedicated jagged/flicker profile for electric
// visuals (bolts, electric blades, electric projectiles, teleport streaks).
// Unlike SpawnProjectileTrail's EffectPresetType color-swap, lightning needs
// its own high-frequency zigzag jitter + periodic glow flicker, which the
// generic preset's smooth flight wobble can't reproduce.

// Flight-stage variant — electric bolt flying start->target (tia điện, đạn
// điện, teleport streak). Returns trail ID; caller MUST call KillTrail(id)
// on impact, e.g. right before SpawnImpactEffect(EFFECT_PRESET_LIGHTNING_IMPACT).
int SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed);

// Manually-driven variant — for weapon-attached lightning (kiếm điện,
// electric aura) where the caller drives the tip every frame. Returns
// trail ID; caller MUST call KillTrail(id) when the effect ends.
//
// Drive the tip with Lightning_UpdateFollowerTip(id, tipPos, scale) below,
// NOT the raw UpdateFollowerPosition() — feeding a smooth per-frame path
// straight into UpdateFollowerPosition records one history node per frame
// (a dense smooth curve — looks like a wiggly worm, not lightning).
// Lightning_UpdateFollowerTip only records a new point once the caller's
// position has moved a real distance from the last one (few, far-apart
// points) and inserts one kinked midpoint per accepted segment, so it stays
// a proper sparse zigzag no matter how often you call it. Falls back to
// Trail_AttachToTransform() only if you don't need the zigzag filtering
// (e.g. a smooth non-electric FOLLOWER use).
int SpawnLightningFollowerTrail(Vector3 startPos, float scale, float life);
void Lightning_UpdateFollowerTip(int id, Vector3 tipPos, float scale);

// ── Low-level lightning building blocks (use vfx_proc_ray.h for the managed API) ──
// Stateless per-frame bolt renderer. waypoints9 is a caller-owned Vector3[9].
// Must be called inside BeginBlendMode(BLEND_ADDITIVE) / EndBlendMode.
void RegenerateLightningWaypoints(Vector3 *waypoints9, Vector3 from, Vector3 to, float scale);
void RegenerateLightningRay(Vector3 *waypoints9, Vector3 origin, Vector3 direction,
                             float length, float phase, float amplitude, float scale);
void DrawLightningBolt(const Vector3 *waypoints9, float thickness, Camera3D cam);

// Audio presets — reuse EffectPresetType so skill authors call the same enum
// value for both image and sound. Each loads (via ResourceManager_LoadSound,
// cached) and plays a per-element Sound on first use, then just PlaySound()s
// the cached handle on subsequent calls. No Flight-stage preset (looping/
// ambient audio during flight is a different mechanism than one-shot
// PlaySound; out of scope here).
//
// NOTE: as of this writing no per-element SFX assets exist under assets/ yet
// (content gap, not a Core Agent decision) — these currently TraceLog a
// one-time LOG_WARNING per missing preset and return without playing
// anything or crashing. Once real .wav/.ogg assets are added under
// assets/sounds/, wire the paths into the switch in skill_helper.c.
void PlayCastSound(EffectPresetType preset);
void PlayImpactSound(EffectPresetType preset);

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

// 3b. Layered Timeline (staggered multi-layer schedule)
#define TIMELINE_MAX_LAYERS 8

typedef struct {
    const char *tag;
    float start;
    float duration; // >0: continuous window (IsLayerActive/LayerProgress). ~0: one-shot event (LayerEvent)
} TimelineLayer;

typedef struct {
    float current;
    TimelineLayer layers[TIMELINE_MAX_LAYERS];
    int layerCount;
} LayeredTimeline;

void Timeline_LayeredStart(LayeredTimeline *t);
bool Timeline_AddLayer(LayeredTimeline *t, const char *tag, float start, float duration);
bool Timeline_IsLayerActive(const LayeredTimeline *t, int layerIndex);
float Timeline_LayerProgress(const LayeredTimeline *t, int layerIndex);
bool Timeline_LayerEvent(const LayeredTimeline *t, int layerIndex, float dt);

// 3c. Curve-driven flight (CORE_ISSUES.md Item 34 follow-up: sandbox-tunable
// system upgrade). One frame's advance for a projectile whose speed follows
// a SkillCurve over time — NOT over fraction-of-distance-to-target. This
// matters: indexing a speed curve by distance-progress makes a far-away
// target silently stretch the whole curve over a longer path, and leaves
// nothing capping how far a cast can travel. Here, t01 = clamp(elapsed /
// maxDuration, 0, 1) indexes speedCurve; *traveled accumulates
// SkillCurve_Eval(speedCurve, t01) * dt and is clamped to maxRange.
// *arrived becomes true the moment *traveled reaches min(targetDistance,
// maxRange), or elapsed + dt >= maxDuration — whichever happens first. This
// guarantees a cast can never fly farther than maxRange or longer than
// maxDuration regardless of target distance or the curve's shape.
// maxDuration/maxRange are meant to be ordinary sandbox tunables themselves
// (phase = the flight phase's tag), so both the ramp shape and the hard cap
// are adjustable.
void SkillHelper_StepCurveFlight(const SkillCurve *speedCurve, float elapsed, float dt,
                                  float maxDuration, float maxRange, float targetDistance,
                                  float *traveled, bool *arrived);

// 3d. Single-force-layer evaluation. Evaluates one ForceLayer (core/force_field.h)
// without needing to build a whole ForceField container just to hold it — for
// a skill's tunable "extra force" applied only during one phase (register the
// layer's own float fields, e.g. .strength, as ordinary phase-tagged
// SkillTunableEntry rows). axisOrigin/axisDir only matter for
// FORCE_RADIAL_AXIS/FORCE_VORTEX_AXIS layers — pass (Vector3){0} for
// anything else, same convention as ForceField_Evaluate.
Vector3 SkillHelper_EvaluateForceLayer(const ForceLayer *layer, Vector3 pos, Vector3 vel,
                                        float time, Vector3 axisOrigin, Vector3 axisDir);

// 3e. Fully-configurable, always-additive tunable force mix. All 8 curated
// ForceTypes are simultaneously available — each with its own strength
// (0 = that type contributes nothing) AND its own full relevant parameter
// set (direction/origin/noise as applicable), not just a shared strength
// number. There's no "select one type" step: dial up as many types as you
// want at once (e.g. curl + gravity-point together), and they all compose
// into the same ForceField. A skill wanting an independent mix per phase
// declares one static SkillForceMix per phase and calls
// SkillForceMix_MakeTunables once. Excludes FORCE_RADIAL_AXIS/
// FORCE_VORTEX_AXIS/FORCE_VECTOR_TEXTURE (need axis/GPU context a generic
// per-phase mix doesn't have).
typedef struct {
    float windStrength, windDirX, windDirY, windDirZ, windNoiseScale, windNoiseSpeed;
    float perlinStrength, perlinNoiseScale, perlinNoiseSpeed;
    float curlStrength, curlNoiseScale, curlNoiseSpeed;
    float gravDirStrength, gravDirX, gravDirY, gravDirZ;
    float gravPtStrength, gravPtOriginX, gravPtOriginY, gravPtOriginZ;
    float vortexStrength, vortexOriginX, vortexOriginY, vortexOriginZ, vortexDirX, vortexDirY, vortexDirZ;
    float dragStrength;
    float viscosityStrength;
} SkillForceMix;

// Adds one ForceLayer per component whose strength != 0 (skips the rest —
// keeps ForceField's layer budget, FORCE_FIELD_MAX_LAYERS in
// core/force_field.h, from being spent on inactive components). Call this
// fresh right before each use (not just once at Init) so live sandbox edits
// take effect immediately, same as any other tunable-driven ForceField.
// NOTE: if a phase's own base physics already uses several layers AND the
// user dials up many of the 8 mix components at once, the total can exceed
// FORCE_FIELD_MAX_LAYERS (8) — ForceField_AddLayer silently drops layers
// past that cap rather than crashing, so the least-recently-added mix
// components would stop applying in that (unlikely) all-8-at-once case.
void SkillForceMix_AddLayers(const SkillForceMix *mix, ForceField *ff);

// Fills outEntries[0..28] (29 entries, one per field above) with this mix's
// tunables, all tagged with `phase` and labeled "<labelPrefix><fieldName>"
// (e.g. labelPrefix "cast_force_" -> "cast_force_wind_strength",
// "cast_force_curl_noise_scale", ...). Caller appends these into its own
// combined SkillTunableEntry array and calls RegisterSkillTunables itself —
// this only fills entries, doesn't register. Returns 29.
#define SKILL_FORCE_MIX_TUNABLE_COUNT 29
int SkillForceMix_MakeTunables(SkillForceMix *mix, const char *labelPrefix,
                                const char *phase, SkillTunableEntry *outEntries);

// 4. Particle Emitter System
typedef enum {
    EMITTER_FIRE,
    EMITTER_SNOW,
    EMITTER_WATER_SPURT,
    EMITTER_SHOCKED_SPARKS,
    EMITTER_WOOD_LEAVES,
    EMITTER_EARTH_DUST,
    EMITTER_METAL_SPARKS,
    EMITTER_TAIJI_MOTES
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
// All presets are backed by the shared core/shaders/effect_material.vs/.fs
// (Item 17): Material_Load(preset) = Material_LoadCustom with hardcoded params.
typedef enum {
    MATERIAL_FIRE,
    MATERIAL_ICE,
    MATERIAL_WATER,
    MATERIAL_PORTAL,
    MATERIAL_CUSTOM // set by Material_LoadCustom() — parametrized shared shader
} MaterialPreset;

typedef struct {
    Color baseColor;          // primary tint; also drives rim glow + dissolve edge glow color
    float rimStrength;        // 0..~2, rim/edge glow brightness (Fresnel-weighted)
    float fresnelPower;       // 1..8, rim sharpness (higher = thinner edge)
    float emissiveIntensity;  // 0..~3, self-illumination boost added to base color
    float distortionStrength; // 0..1, vertex wobble amount
    float translucency;       // 0..1: 0 = opaque (alpha = baseColor.a), 1 = glass/tube-style
                               // fresnel-driven alpha (center see-through, edges more solid,
                               // same formula as tube.fs). Draw call must use BLEND_ALPHA
                               // (BeginBlendMode/EndBlendMode) for this to actually blend.
    Texture2D texture1;       // optional secondary detail/mask texture; id==0 = unused
} EffectMaterialParams;

typedef struct {
    Shader shader;
    MaterialPreset preset;
    int uTimeLoc;
    int uDissolveLoc;
    int uBaseColorLoc;
    int uTranslucencyLoc;
    int uRimStrengthLoc;
    int uFresnelPowerLoc;
    int uEmissiveIntensityLoc;
    int uDistortionStrengthLoc;
    int uHasTexture1Loc;
    int uTexture1Loc;
    EffectMaterialParams params;
} EffectMaterial;

EffectMaterial Material_Load(MaterialPreset preset);
EffectMaterial Material_LoadCustom(EffectMaterialParams params);
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
void Material_Begin(EffectMaterial mat);
void Material_End(void);

// 7. Ground Decal
typedef enum {
    // Earth
    DECAL_PRESET_CRACK,            // crack.png
    DECAL_PRESET_EARTH_SHATTER,    // decals/decal_stone_shatter.png
    DECAL_PRESET_EARTH_RUNE,       // decals/decal_earth_rune.png

    // Fire
    DECAL_PRESET_BURN,             // scorch_mark.png
    DECAL_PRESET_FIRE_LAVA,        // decals/decal_lava_crack.png

    // Water
    DECAL_PRESET_WATER,            // water_caustics.png
    DECAL_PRESET_WATER_SPLASH,     // decals/decal_splash_ring.png
    DECAL_PRESET_WATER_RIPPLE,     // decals/decal_water_ripple.png
    DECAL_PRESET_ICE,              // decals/decal_frost_ring.png

    // Wood
    DECAL_PRESET_WOOD_ROOT,        // decals/decal_root_mark.png
    DECAL_PRESET_WOOD_MOSS,        // decals/decal_moss_stain.png

    // Metal
    DECAL_PRESET_METAL_SLASH,      // decals/decal_slash_mark.png
    DECAL_PRESET_METAL_CRATER,     // decals/decal_impact_crater.png
    DECAL_PRESET_METAL_RUNE,       // decals/decal_metal_rune.png

    // Taiji
    DECAL_PRESET_TAIJI_RING,       // decals/decal_taiji_ring.png
    DECAL_PRESET_TAIJI_LIGHTNING,  // decals/decal_lightning_char.png
    DECAL_PRESET_TAIJI_WIND,       // decals/decal_wind_groove.png

    // Generic (no element tint baked in — caller sets tint to taste)
    DECAL_PRESET_GENERIC_IMPACT_RING, // generic/impact_ring.png
    DECAL_PRESET_GENERIC_GLOW,        // generic/glow_circle.png
    DECAL_PRESET_GENERIC_SHADOW       // generic/shadow_blob.png
} DecalPresetType;

void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration);
// Full-control variant — exposes all AddEx params. scaleStart/scaleEnd let the decal
// grow or shrink over its lifetime; rotSpeed spins continuously (deg/s); yOffset lifts
// the decal off the ground plane (use ~0.02f to avoid z-fighting).
void SpawnGroundDecalEx(DecalPresetType type, Vector3 pos,
                        float scaleStart, float scaleEnd, float lifetime,
                        float rotSpeed, float yOffset);

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

// Cast-stage hook for the builder pattern. Has its own trigger point — call
// this at cast time (e.g. inside Cast[Name]Skill), separately from
// SkillBuilder_Build() which fires at impact. Fires SpawnCastEffect
// immediately rather than deferring into ctx, since cast and impact happen
// at different points in the skill's lifecycle.
void SkillBuilder_AddCastEffect(SkillBuildContext *ctx, EffectPresetType preset);

#endif // SKILL_HELPER_H
