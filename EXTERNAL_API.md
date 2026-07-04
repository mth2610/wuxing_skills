# EXTERNAL_API.md — Wuxing Skills: Standalone Skill Creation Reference

**Purpose**: Self-contained API reference for creating new skills without access to the project directory.
**Who**: AI assistants, designers, or programmers working outside the repo.
**How to use**: Paste this entire file into your context. All types, constants, and examples are fully spelled out here — no "see file X" references.

---

## 1. Game Context

```
Project:  Wuxing Skills (C / Raylib 5.5 / OpenGL 3.3, isometric night arena)
Scale:    1 unit = 1 meter. Arena center (6.0f, 0.0f, 4.4f), radius 18m. Y=0 is ground.
Gravity:  PHYSICS_GRAVITY_MPS2 = 9.81f  (reference for tuning force/gravity values)
Elements: Water, Wood, Fire, Earth, Metal, Taiji

Element color macros (always use these — never hardcode RGB):
  ELEMENT_COLOR_WATER   ELEMENT_COLOR_WOOD    ELEMENT_COLOR_FIRE
  ELEMENT_COLOR_EARTH   ELEMENT_COLOR_METAL   ELEMENT_COLOR_TAIJI

Typical scales:
  Mesh radii:      0.10 – 0.20 m
  Forces/gravity:  3.0  – 7.0  m/s²  (compare: real gravity = 9.81 m/s²)
  Particle speeds: 1.0  – 3.0  m/s
  Travel speeds:   2.0  – 6.0  m/s

Memory model: NO malloc/free anywhere. Static arrays only.
              Pool overflow = silent no-op (never crashes, just drops).
```

---

## 2. Required Skill File Structure

```
skills/[element]/[skill_name]/
    [skill_name].h              # lifecycle prototypes
    [skill_name].c              # logic + rendering
    [skill_name]_params.inl     # file-scope static tunable state  (included once at file scope)
    [skill_name]_tunables.inl   # tunable registration             (included inside Init)
    [shader].vs / [shader].fs   # optional custom shaders
```

### `_params.inl` — file-scope statics

Included ONCE at file scope in `.c`, before any function. Holds all mutable tuning parameters:

```c
// Real-world-scaled: 1 unit = 1 meter.
static float s_speed       = 3.0f;   // projectile travel speed (m/s)
static float s_radius      = 0.15f;  // mesh / hit radius (m)
static float s_damage      = 25.0f;  // base damage (game units)
static float s_lifetime    = 2.5f;   // particle/effect lifetime (s)
static float s_jitter      = 0.05f;  // perpendicular spawn jitter (m)

static SkillCurve s_radiusCurve;     // radius multiplier over phase [0..1]
static SkillCurve s_speedCurve;      // speed multiplier over phase [0..1]
static SkillCurve s_alphaCurve;      // alpha multiplier over phase [0..1]
static SkillCurve s_emissiveCurve;   // emissive multiplier over phase [0..1]
static SkillForceMix s_flyForce;     // composite force applied during flight

#define MY_TUNABLE_COUNT 45
```

### `_tunables.inl` — registration (inside Init)

```c
// Included inside Init[Name]Skill, after `int tn = 0;`
s_tunables[tn++] = (SkillTunableEntry){
    .name = "Speed", .ptr = &s_speed,
    .min = 0.5f, .max = 8.0f, .phase = "flight"
};
s_tunables[tn++] = (SkillTunableEntry){
    .name = "Radius", .ptr = &s_radius,
    .min = 0.05f, .max = 0.5f, .phase = "flight"
};
s_tunables[tn++] = (SkillTunableEntry){
    .name = "Damage", .ptr = &s_damage,
    .min = 1.0f, .max = 200.0f, .phase = "impact"
};
// For SkillForceMix, use the bulk helper:
SkillForceMix_MakeTunables(&s_flyForce, s_tunables, &tn, "flight");
```

### `[skill_name].h` — prototype block

```c
#pragma once
#include "raylib.h"
#include "core/skill_manager.h"   // SkillParams, SkillProjectile

void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);
bool Is[Name]SkillCoiling(void);
int  Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);
```

---

## 3. Skill Lifecycle API

Every skill must implement exactly these 8 entry points.

```c
// Types from core/skill_manager.h:

typedef struct {
    float sizeScale;         // visual/hitbox scale multiplier (1.0 = normal)
    float damageMultiplier;  // damage scale (1.0 = base damage)
    int   level;             // skill level (1–5 typical)
} SkillParams;

typedef struct {
    Vector3 position;        // current world position of projectile
    float   radius;          // hitbox radius (m)
    bool    active;          // false = already detonated
} SkillProjectile;
```

| Function | Called by | Purpose |
|---|---|---|
| `Init[Name]Skill` | Once at startup | Load shaders, meshes, register tunables |
| `Cast[Name]Skill` | On cast input | Start skill state, spawn initial effects |
| `Update[Name]Skill` | Every frame | Move projectiles, check hits, spawn particles |
| `Draw[Name]Skill` | Every frame (draw phase) | Render meshes/particles |
| `Unload[Name]Skill` | At shutdown | Release meshes (NOT shaders/textures — ResourceManager owns those) |
| `Is[Name]SkillCoiling` | Query | Return true while skill is active (suppresses caster movement in some modes) |
| `Get[Name]SkillProjectiles` | Collision query | Fill `outProjectiles`, return count |
| `Deactivate[Name]Projectile` | On external hit | Mark projectile inactive (e.g. blocked by shield) |

---

## 4. Core Systems

### A. Particle System (`core/particle_system.h`)

Pool size: **2000** particles. Overflow = silent drop.

```c
typedef struct {
    Vector3       position;
    Vector3       velocity;
    float         radius;         // m, typical 0.01–0.15
    float         lifetime;       // seconds
    Color         colorStart;     // RGBA (used when gradient == NULL)
    Color         colorEnd;       // RGBA (used when gradient == NULL)
    ColorGradient *gradient;      // if non-NULL, overrides colorStart/End
    ForceField    *forceField;    // NULL = no external force
    const SkillCurve *radiusCurve;   // NULL = constant 1.0x
    const SkillCurve *speedCurve;    // NULL = constant 1.0x
    const SkillCurve *alphaCurve;    // NULL = constant 1.0x
    const SkillCurve *emissiveCurve; // NULL = constant 1.0x
} ParticleConfig;

void SpawnParticle(ParticleConfig cfg);
void ParticleSystem_GetStats(int *active, int *max);
```

**Minimal spawn example:**
```c
SpawnParticle((ParticleConfig){
    .position   = ms.pos,
    .velocity   = (Vector3){ 0.0f, 1.5f, 0.0f },
    .radius     = 0.04f,
    .lifetime   = 0.8f,
    .colorStart = ELEMENT_COLOR_WATER,
    .colorEnd   = ColorAlpha(ELEMENT_COLOR_WATER, 0),
});
```

**Full example with curves and force field:**
```c
SpawnParticle((ParticleConfig){
    .position     = spawnPos,
    .velocity     = vel,
    .radius       = s_radius * 0.5f,
    .lifetime     = s_lifetime,
    .gradient     = &s_gradient,
    .forceField   = &s_forceField,
    .radiusCurve  = &s_radiusCurve,
    .alphaCurve   = &s_alphaCurve,
    .emissiveCurve = &s_emissiveCurve,
});
```

---

### B. Force Field (`core/force_field.h`)

Up to **8 layers** per ForceField. `ForceField` is opaque — use the API only.

```c
typedef enum {
    FORCE_GRAVITY_DIR,    // uniform directional force (e.g. downward gravity)
    FORCE_GRAVITY_POINT,  // attract/repel from a point (sign of strength)
    FORCE_DRAG,           // velocity damping (reduces speed each frame)
    FORCE_NOISE_PERLIN,   // 3D turbulence/wind
    FORCE_VORTEX,         // spin particles around an axis
    FORCE_RADIAL_AXIS,    // push particles outward/inward from an axis
    FORCE_VISCOSITY,      // GPU-only: drag (ignored by CPU particle system)
    FORCE_VECTOR_TEXTURE  // GPU-only: flow field from texture
} ForceType;

typedef struct {
    ForceType type;
    float     strength;    // m/s² for physics types; compare real gravity = 9.81
    Vector3   direction;   // GRAVITY_DIR: force direction (normalize first)
                           // RADIAL_AXIS: axis direction
                           // VORTEX: spin axis
    Vector3   origin;      // GRAVITY_POINT: the attract/repel point
    float     noiseScale;  // NOISE_PERLIN: spatial frequency, typical 0.01–0.05
    float     noiseSpeed;  // NOISE_PERLIN: time multiplier, typical 0.5–2.0
} ForceLayer;

typedef struct { /* opaque — up to 8 layers */ } ForceField;

void ForceField_Clear(ForceField *ff);
void ForceField_AddLayer(ForceField *ff, ForceLayer layer);
```

**Example: downward gravity + turbulence:**
```c
ForceField_Clear(&s_forceField);
ForceField_AddLayer(&s_forceField, (ForceLayer){
    .type      = FORCE_GRAVITY_DIR,
    .direction = {0.0f, -1.0f, 0.0f},
    .strength  = 4.9f
});
ForceField_AddLayer(&s_forceField, (ForceLayer){
    .type       = FORCE_NOISE_PERLIN,
    .strength   = 1.2f,
    .noiseScale = 0.03f,
    .noiseSpeed = 1.0f
});
```

**Example: attract toward a point:**
```c
ForceField_Clear(&s_forceField);
ForceField_AddLayer(&s_forceField, (ForceLayer){
    .type     = FORCE_GRAVITY_POINT,
    .origin   = targetPos,
    .strength = 5.0f   // positive = attract
});
```

**SkillForceMix** (sandbox-tunable composite force):
```c
// SkillForceMix wraps multiple ForceLayer definitions with tunable weights.
// Never build layers by hand when using ForceMix — let the tunable system own them.
typedef struct { /* opaque */ } SkillForceMix;

void SkillForceMix_AddLayers(const SkillForceMix *mix, ForceField *ff);
// Call immediately before using the ForceField — NOT at Init — so sandbox edits apply.

void SkillForceMix_MakeTunables(SkillForceMix *mix, SkillTunableEntry *t,
                                 int *tn, const char *phase);
// Call inside Init to register all ForceMix weights as tunables.
```

---

### C. Color Gradient (`core/color_gradient.h`)

```c
typedef struct { /* opaque */ } ColorGradient;

// Two-stop linear gradient:
void ColorGradient_Init(ColorGradient *g, Color c0, Color c1);

// Convenience: fade from full alpha to zero using the element color:
void ColorGradient_StandardFade(ColorGradient *g, Color elementColor,
                                float alphaStart, float alphaEnd);
// alphaStart/alphaEnd: 0.0–255.0
```

**Example:**
```c
ColorGradient_StandardFade(&s_gradient, ELEMENT_COLOR_WATER, 220.0f, 0.0f);
```

---

### D. VFX Light (`core/vfx_light.h`)

Pool size: **16** lights. Overflow = silent drop.

```c
typedef enum {
    VFX_PRIORITY_LOW,           // dropped first when pool full
    VFX_PRIORITY_HIGH_ULTIMATE  // retained when pool full (use sparingly)
} VFXLightPriority;

void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime,
                    VFXLightPriority priority);
void VFXLight_GetStats(int *active, int *max);
```

**Example:**
```c
VFXLight_Spawn(impactPos, ELEMENT_COLOR_FIRE, 3.0f, 0.4f, VFX_PRIORITY_LOW);
```

---

### E. Decal System (`core/decal_system.h`)

Pool size: **64** decals.

```c
typedef enum {
    DECAL_PRESET_BURN,
    DECAL_PRESET_ICE,
    DECAL_PRESET_WATER,
    DECAL_PRESET_CRACK,
    DECAL_PRESET_WOOD_MOSS,
    DECAL_PRESET_METAL_SLASH,
    DECAL_PRESET_TAIJI_RING,
    DECAL_PRESET_TAIJI_LIGHTNING,
    DECAL_PRESET_GENERIC_GLOW
} DecalPresetType;

void SpawnGroundDecal(DecalPresetType preset, Vector3 pos, float radius, float lifetime);
void DecalSystem_GetStats(int *active, int *max);
```

**Example:**
```c
SpawnGroundDecal(DECAL_PRESET_WATER, impactPos, 1.5f, 4.0f);
```

---

### F. Screen Distort (`core/screen_distort.h`)

Full-screen post-process ripple effect.

```c
void ScreenDistort_Add(Vector3 worldPos, float radius, float strength,
                       float lifetime, float speed);
// worldPos:  epicenter in world space
// radius:    world-space effect radius (m), typical 0.3–1.5
// strength:  distort intensity 0..1
// lifetime:  seconds the ripple lasts
// speed:     wave expansion speed (m/s), typical 1.5–3.0
```

**Example:**
```c
ScreenDistort_Add(impactPos, 1.0f, 0.4f, 0.5f, 2.0f);
```

---

### G. Camera FX (`core/camera_fx.h`)

```c
void CameraFX_Shake(float trauma);
// trauma: 0..1. Typical values: 0.2 (small hit), 0.4 (medium), 0.7 (heavy impact)
```

---

### H. Procedural Mesh (`core/procedural_mesh_utils.h`)

**Do NOT use Raylib's `DrawSphere`/`DrawCylinder` directly** — always use these:

```c
void DrawCoreSphere(Vector3 center, float radius, Color color);
void DrawCoreCylinder(Vector3 base, float radiusTop, float radiusBottom,
                      float height, Color color);
// Additional mesh draw functions follow the same DrawCore* naming convention.
```

---

## 5. Skill Helper Presets (`core/skill_helper.h`)

One-line cast/impact effects, trail projectiles, and audio. **`SkillHelper_Update` is called by `main.c` — skills must NOT call it.**

```c
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

// Spawn a burst effect at cast origin:
void SpawnCastEffect(Vector3 pos, EffectPresetType preset, float scale);

// Spawn a burst effect at impact point:
void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale);

// Spawn a moving trail from start→target. Returns trail ID.
// MUST call KillTrail(id) on impact or cancellation.
int  SpawnProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset,
                          float scale, float speed);
void KillTrail(int id);

// Lightning bolt trail from start→target. Self-terminates — no KillTrail needed.
int  SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed);

// Audio:
void PlayCastSound(EffectPresetType preset);
void PlayImpactSound(EffectPresetType preset);

// Internal — do NOT call from skills:
void SkillHelper_Update(float dt);
```

---

## 6. Skill Builder Archetypes (`core/skill_helper.h`, §11)

High-level one-liner spawns. All are driven internally by `SkillHelper_Update` (wired in `main.c`).
No manual Update/Draw calls from skill code. Static pools — overflow = silent no-op.

```c
// BEAM — persistent light ray between two world points.
// Pool: 8 beams. Returns handle for early kill.
int  SkillBuilder_SpawnBeam(Vector3 from, Vector3 to, EffectPresetType element,
                             float width, float duration);
void SkillBuilder_KillBeam(int handle);
// Internally wraps ProcRay + VFXLight at both endpoints.

// GROUND WAVE — expanding shockwave ring along the ground.
// Pool: 8 waves. No handle — fires and forgets.
void SkillBuilder_SpawnGroundWave(Vector3 origin, Vector3 dir, EffectPresetType element,
                                  float range, float speed);
// dir: direction of propagation (normalized XZ vector)
// range: max radius before wave dies (m)
// speed: expansion rate (m/s)

// ORBITALS — N element-tinted tetrahedra orbiting a center point.
// Pool: 8 groups × 8 orbitals each. Returns group handle.
// Orbitals self-expire after duration — no explicit kill function.
int  SkillBuilder_SpawnOrbitals(Vector3 center, EffectPresetType element,
                                int count, float radius, float duration);
// count: number of orbitals (1–8)
// radius: orbit radius (m)
// duration: lifetime (s)

// AURA RING — looping particle emitter ring + glow decal.
// Pool: 8 rings. Returns handle for early kill.
int  SkillBuilder_SpawnAuraRing(Vector3 center, EffectPresetType element,
                                float radius, float duration);
void SkillBuilder_KillAuraRing(int handle);
```

---

## 7. Motion Controller (`core/motion_controller.h`)

Controls projectile movement without allocating memory. One `MotionState` per projectile (store on static array or struct).

```c
typedef enum {
    MOTION_LINEAR,     // constant velocity straight toward target
    MOTION_HOMING,     // steers toward moving target (turnRateRad rad/s)
    MOTION_BALLISTIC,  // projectile arc: launched with initial velocity, falls under gravity
    MOTION_SPIRAL,     // helical path along origin→target axis
    MOTION_ORBIT,      // circles orbitCenter in the XZ plane
    MOTION_BOOMERANG   // flies to boomerangRange then returns toward origin
} MotionType;

typedef struct {
    MotionType type;
    float speed;             // travel speed (m/s)
    float arrivalRadius;     // detonation/arrival threshold (m), default 0.2f
    float turnRateRad;       // HOMING: max turn per second (rad/s), e.g. 2.5f
    float gravity;           // BALLISTIC: downward acceleration (m/s²), e.g. 4.9f
    float spiralRadius;      // SPIRAL: lateral displacement from axis (m)
    float spiralFreq;        // SPIRAL: rotations per second
    Vector3 orbitCenter;     // ORBIT: center point in world space
    float orbitRadiusXZ;     // ORBIT: radius in XZ plane (m)
    float orbitAngularSpeed; // ORBIT: angular speed (rad/s)
    float boomerangRange;    // BOOMERANG: max outbound distance before returning (m)
} MotionParams;

typedef struct {
    MotionParams params;
    Vector3 pos;        // READ this each frame — current world position
    Vector3 vel;        // current velocity vector
    Vector3 target;     // target world position (update externally for tracking targets)
    Vector3 origin;     // spawn position (used by BOOMERANG)
    float   elapsed;    // time since Motion_Init (seconds)
    float   orbitAngle; // ORBIT: current angle (radians)
    bool    returning;  // BOOMERANG: true when heading back toward origin
} MotionState;

void Motion_Init(MotionState *s, MotionParams params, Vector3 startPos, Vector3 target);
void Motion_Step(MotionState *s, float dt);  // call every frame in Update
bool Motion_Arrived(const MotionState *s);   // true when within arrivalRadius of target
```

**Example — homing projectile:**
```c
static MotionState s_ms;

// In Cast:
Motion_Init(&s_ms, (MotionParams){
    .type          = MOTION_HOMING,
    .speed         = 4.0f,
    .turnRateRad   = 2.5f,
    .arrivalRadius = 0.2f,
}, startPos, target);

// In Update:
Motion_Step(&s_ms, dt);
// s_ms.pos is current world position — use for trail, collision, particles
if (Motion_Arrived(&s_ms)) { /* handle impact */ }
```

**Example — ballistic arc:**
```c
Motion_Init(&s_ms, (MotionParams){
    .type          = MOTION_BALLISTIC,
    .speed         = 6.0f,
    .gravity       = 5.0f,
    .arrivalRadius = 0.3f,
}, startPos, target);
```

**Example — spiral:**
```c
Motion_Init(&s_ms, (MotionParams){
    .type         = MOTION_SPIRAL,
    .speed        = 3.0f,
    .spiralRadius = 0.4f,
    .spiralFreq   = 2.0f,
    .arrivalRadius = 0.2f,
}, startPos, target);
```

---

## 8. Chain-Targeting (`core/skill_helper.h`, §12)

Handles chain-jump target selection and the lightning visuals for the chain.
**Visuals only** — damage must be applied separately per point.

```c
// Find up to maxJumps enemies within jumpRadius of each successive jump point.
// outPoints[0] = origin (cast/impact point), [1..n-1] = jump destinations.
// Returns total point count (including origin). Returns 0 if no targets in range.
// Requires entities registered via SkillManager_SetNearbyTargetsProvider (auto in Entity_Init).
int SkillHelper_ChainTargets(Vector3 origin, float jumpRadius, int maxJumps,
                              Vector3 *outPoints, int maxOut);

// Spawn chain lightning visuals between the points.
// One SpawnLightningTrail per hop, staggered by hopDelay seconds.
// Driven by SkillHelper_Update (main.c) — no manual update needed.
void SpawnChainLightning(const Vector3 *points, int count, float scale, float hopDelay);
```

**Full chain usage:**
```c
// After impact at ms.pos:
Vector3 chainPoints[8];
int n = SkillHelper_ChainTargets(ms.pos, 3.5f, 6, chainPoints, 8);
if (n > 1) {
    SpawnChainLightning(chainPoints, n, 1.0f, 0.10f);
    // Apply damage per hop:
    for (int i = 1; i < n; i++) {
        Entity_ApplyAoEDamage(chainPoints[i], 0.6f, s_damage * 0.6f, 1.0f);
    }
}
```

---

## 9. Status VFX & Afterimage

### M. Status VFX (`core/status_vfx.h`)

Looping element aura attached to an agent — follows the agent automatically.

```c
// Attach an aura. Re-attaching same element+agentId refreshes duration (no stacking).
// Returns handle for early removal.
int  StatusVFX_Attach(int agentId, EffectPresetType element, float duration);

// Early removal (e.g. cleanse effect):
void StatusVFX_Detach(int handle);

// Update/Draw wired in main.c — skills do NOT call these.
```

**Example:**
```c
// On hit — apply burning aura for 3 seconds:
int burnHandle = StatusVFX_Attach(targetAgentId, EFFECT_PRESET_FIRE_EXPLOSION, 3.0f);

// If the target is cleansed:
StatusVFX_Detach(burnHandle);
```

---

### N. Mesh Afterimage (`core/afterimage.h`)

Ghost copies of a mesh that dissolve over time. Used for dash/dash-strike trails.

```c
void Afterimage_Spawn(Model model, Matrix transform, Color tint, float life);
// model:     reference only — do NOT unload the model while ghost copies live
// transform: world matrix of the mesh at the moment of spawning
// tint:      color + alpha (use ColorAlpha to set transparency)
// life:      dissolve duration (s), typical 0.2–0.5
```

**Example — afterimage trail during dash:**
```c
static float s_afterimageTimer = 0.0f;

// In Update, while dashing:
s_afterimageTimer += dt;
if (s_afterimageTimer >= 0.04f) {
    s_afterimageTimer = 0.0f;
    Afterimage_Spawn(s_dashModel, s_dashTransform,
                     ColorAlpha(ELEMENT_COLOR_METAL, 180), 0.3f);
}
```

---

## 10. Comprehensive Tunability

### SkillCurve — value multiplier over a phase

5-keyframe curve sampled over phase progress `t ∈ [0, 1]`. Applied as a multiplier on top of the base value (e.g. particle radius, speed).

```c
typedef struct {
    float v[5];  // values at t=0, 0.25, 0.5, 0.75, 1.0
} SkillCurve;

void  SkillCurve_SetConstant(SkillCurve *c, float value);
// Sets all 5 keyframes to `value` — flat/no-op multiplier when value=1.0.

float SkillCurve_Eval(const SkillCurve *c, float t);
// t in [0, 1]. Linearly interpolates between keyframes.
```

**Example curves:**
```c
// Grow then shrink (pulse):
s_radiusCurve = (SkillCurve){ .v = {0.0f, 1.2f, 1.0f, 0.8f, 0.0f} };

// Start fast, ease out:
s_speedCurve  = (SkillCurve){ .v = {1.5f, 1.2f, 1.0f, 0.8f, 0.5f} };

// Fade out alpha:
s_alphaCurve  = (SkillCurve){ .v = {1.0f, 0.9f, 0.7f, 0.4f, 0.0f} };

// Flash emissive on spawn:
s_emissiveCurve = (SkillCurve){ .v = {2.0f, 1.5f, 1.0f, 0.8f, 0.5f} };

// Constant (no effect):
SkillCurve_SetConstant(&s_speedCurve, 1.0f);
```

**Using a curve in Update:**
```c
float t = projectileAge / s_lifetime;  // normalize to [0, 1]
float currentRadius = s_radius * SkillCurve_Eval(&s_radiusCurve, t);
```

---

### SkillForceMix — sandbox-tunable composite force

Wraps multiple ForceLayer definitions with sandbox-adjustable weights. Preferred over raw `ForceField_AddLayer` when you want live tuning.

```c
typedef struct { /* opaque */ } SkillForceMix;

// Apply the mix's layers into a ForceField.
// Call immediately before using the ForceField — NOT at Init.
// This ensures sandbox edits are reflected every frame.
void SkillForceMix_AddLayers(const SkillForceMix *mix, ForceField *ff);

// Register all ForceMix weights as sandbox tunables.
// Call inside Init[Name]Skill, after SkillForceMix is configured.
void SkillForceMix_MakeTunables(SkillForceMix *mix, SkillTunableEntry *t,
                                 int *tn, const char *phase);
```

**Pattern for RebuildField (must call each frame):**
```c
// In Update[Name]Skill each frame (not just at Init):
ForceField_Clear(&s_flyField);
SkillForceMix_AddLayers(&s_flyForce, &s_flyField);
// Now use &s_flyField in ParticleConfig
```

---

## 11. Damage & Knockback

### Entity_ApplyAoEDamage (real gameplay — use in shipped skills)

```c
// From entities/entities.h
// Damages real agents in the entity pool. Respects HP, team membership, etc.
void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage,
                            float knockbackStrength);
// center:            world position of AoE origin
// radius:            damage radius (m)
// damage:            raw damage before entity resistances
// knockbackStrength: impulse magnitude (m/s), typical 1.0–4.0
```

### ApplyAoEDamage (sandbox fallback)

```c
// From core/skill_manager.h
// Use in sandbox/test harnesses when no real entity pool is available.
// No HP bookkeeping — just triggers hit reactions and VFX.
void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
```

### Knockback Helper

```c
typedef enum {
    SKILL_CAT_PROJECTILE,    // single-target projectile
    SKILL_CAT_AOE_CONTROL,   // area control (waves, fields)
    SKILL_CAT_MELEE,         // melee strike
    SKILL_CAT_TRAP_UTILITY,  // traps, delayed effects
    SKILL_CAT_BUFF_SUPPORT   // buffs, heals
} SkillCategory;

// From core/skill_manager.h
// Returns a knockback value pre-scaled by category and params.
float Skill_CalculateKnockback(SkillCategory cat, SkillParams params);
```

---

## 12. Tunable Registration Pattern

Enables live sandbox editing of skill parameters during development.

```c
// Full type from core/skill_manager.h:
typedef struct {
    const char *name;   // display name in sandbox UI
    float      *ptr;    // pointer to the static float to tune
    float       min;    // slider minimum
    float       max;    // slider maximum
    const char *phase;  // phase label: "flight", "impact", "cast", "idle", etc.
} SkillTunableEntry;

// Registration functions (call in Init):
void RegisterSkillTunables(int skillIndex, SkillTunableEntry *entries, int count);
void SkillTunables_LoadPersisted(const char *path, SkillTunableEntry *entries, int count);
// path: "skills/[element]/[skill_name]/[skill_name].tuning"
```

**Complete Init pattern:**
```c
void InitMyWaterSkill(int screenWidth, int screenHeight) {
    // 1. Load resources via ResourceManager (NOT LoadShader/LoadTexture directly):
    s_shader = ResourceManager_LoadShader("skills/water/my_skill/my_skill.vs",
                                          "skills/water/my_skill/my_skill.fs");
    s_mesh   = ResourceManager_LoadModel("assets/mesh/orb.glb");

    // 2. Initialize curves:
    s_radiusCurve  = (SkillCurve){ .v = {0.0f, 1.0f, 1.0f, 0.8f, 0.0f} };
    s_alphaCurve   = (SkillCurve){ .v = {1.0f, 1.0f, 0.9f, 0.5f, 0.0f} };
    SkillCurve_SetConstant(&s_speedCurve, 1.0f);

    // 3. Initialize gradient:
    ColorGradient_StandardFade(&s_gradient, ELEMENT_COLOR_WATER, 220.0f, 0.0f);

    // 4. Register tunables:
    static SkillTunableEntry s_tunables[MY_TUNABLE_COUNT];
    int tn = 0;
    #include "my_skill_tunables.inl"
    SkillTunables_LoadPersisted(
        "skills/water/my_skill/my_skill.tuning", s_tunables, tn);
    RegisterSkillTunables(s_skillIndex, s_tunables, tn);
}
```

**Important**: `s_skillIndex` is assigned by `SkillManager_Register` during game startup — the skill's `.c` file receives it as a static set at registration time.

---

## 13. Cookbook Examples

### Example 1 — Homing water orb with chain lightning on impact

```c
// _params.inl (file scope):
static float s_speed         = 4.0f;
static float s_radius        = 0.12f;
static float s_damage        = 30.0f;
static float s_chainRadius   = 3.5f;
static int   s_chainJumps    = 4;
static bool  s_active        = false;
static MotionState s_ms;
static int   s_trailId       = -1;
static ColorGradient s_gradient;
static ForceField    s_field;
static SkillCurve    s_alphaCurve;

// In Cast:
void CastWaterChainSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams p) {
    s_active = true;
    Motion_Init(&s_ms, (MotionParams){
        .type          = MOTION_HOMING,
        .speed         = s_speed * p.sizeScale,
        .turnRateRad   = 2.5f,
        .arrivalRadius = 0.2f,
    }, startPos, target);
    s_trailId = SpawnProjectileTrail(startPos, target,
                                     EFFECT_PRESET_WATER_SPLASH, 1.0f, s_speed);
    SpawnCastEffect(startPos, EFFECT_PRESET_WATER_SPLASH, 1.0f);
    PlayCastSound(EFFECT_PRESET_WATER_SPLASH);
}

// In Update:
void UpdateWaterChainSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    if (!s_active) return;

    Motion_Step(&s_ms, dt);

    // Spawn trail particles (perpendicular jitter on Y axis):
    SpawnParticle((ParticleConfig){
        .position   = s_ms.pos,
        .velocity   = (Vector3){ 
            GetRandomValue(-100, 100) * 0.005f,
            0.5f,
            GetRandomValue(-100, 100) * 0.005f },
        .radius     = s_radius * 0.4f,
        .lifetime   = 0.3f,
        .gradient   = &s_gradient,
        .alphaCurve = &s_alphaCurve,
    });

    if (Motion_Arrived(&s_ms)) {
        s_active = false;
        KillTrail(s_trailId);

        SpawnImpactEffect(s_ms.pos, EFFECT_PRESET_WATER_SPLASH, 1.2f);
        SpawnGroundDecal(DECAL_PRESET_WATER, s_ms.pos, 1.2f, 5.0f);
        VFXLight_Spawn(s_ms.pos, ELEMENT_COLOR_WATER, 3.0f, 0.5f, VFX_PRIORITY_LOW);
        ScreenDistort_Add(s_ms.pos, 1.0f, 0.35f, 0.4f, 2.0f);
        CameraFX_Shake(0.2f);
        PlayImpactSound(EFFECT_PRESET_WATER_SPLASH);

        // Primary AoE damage:
        Entity_ApplyAoEDamage(s_ms.pos, 0.6f, s_damage, 1.5f);

        // Chain to nearby enemies:
        Vector3 points[8];
        int n = SkillHelper_ChainTargets(s_ms.pos, s_chainRadius, s_chainJumps,
                                          points, 8);
        if (n > 1) {
            SpawnChainLightning(points, n, 1.0f, 0.10f);
            for (int i = 1; i < n; i++) {
                Entity_ApplyAoEDamage(points[i], 0.5f, s_damage * 0.5f, 0.8f);
            }
        }
    }
}
```

---

### Example 2 — Orbiting fire orbitals (cast-and-forget)

```c
static int s_orbHandle = -1;

// In Cast:
void CastFireOrbitalSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams p) {
    SpawnCastEffect(startPos, EFFECT_PRESET_FIRE_EXPLOSION, p.sizeScale);
    s_orbHandle = SkillBuilder_SpawnOrbitals(
        startPos, EFFECT_PRESET_FIRE_EXPLOSION,
        3,      // count
        1.2f,   // orbit radius (m)
        4.0f    // duration (s)
    );
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
    // No update needed — SkillHelper_Update (main.c) drives this
}
```

---

### Example 3 — Mesh afterimage during dash (Metal element)

```c
static float s_afterimageTimer = 0.0f;
static bool  s_dashing         = false;

// In Update, while dashing:
void UpdateMetalDashSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    if (!s_dashing) return;

    s_afterimageTimer += dt;
    if (s_afterimageTimer >= 0.04f) {
        s_afterimageTimer = 0.0f;
        Afterimage_Spawn(s_dashModel, s_dashTransform,
                         ColorAlpha(ELEMENT_COLOR_METAL, 180), 0.3f);
    }
}
```

---

### Example 4 — Status aura (burning DoT)

```c
static int s_burnHandle = -1;

// On hit:
void ApplyBurn(int targetAgentId) {
    // Re-attaching same element+agent refreshes duration (no stacking):
    s_burnHandle = StatusVFX_Attach(targetAgentId, EFFECT_PRESET_FIRE_EXPLOSION, 3.0f);
}

// On cleanse:
void CleanseBurn(void) {
    if (s_burnHandle >= 0) {
        StatusVFX_Detach(s_burnHandle);
        s_burnHandle = -1;
    }
}
```

---

### Example 5 — Ground shockwave (Earth element)

```c
// In Cast:
void CastEarthWaveSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams p) {
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
    dir.y = 0.0f;  // keep in XZ plane

    SkillBuilder_SpawnGroundWave(startPos, dir, EFFECT_PRESET_EARTH_CRACK,
                                  8.0f,  // range (m)
                                  5.0f); // speed (m/s)
    SpawnGroundDecal(DECAL_PRESET_CRACK, startPos, 2.0f, 6.0f);
    CameraFX_Shake(0.4f);
    PlayCastSound(EFFECT_PRESET_EARTH_CRACK);
}
```

---

### Example 6 — Beam skill (Wood vine)

```c
static int s_beamHandle = -1;

// In Cast:
s_beamHandle = SkillBuilder_SpawnBeam(
    casterPos, targetPos, EFFECT_PRESET_WOOD_BLOOM,
    0.15f,  // beam width (m)
    2.5f    // duration (s)
);
// Beam auto-kills after duration. Kill early:
// SkillBuilder_KillBeam(s_beamHandle);
```

---

### Example 7 — Spiral Water projectile

```c
static MotionState s_ms;
static bool s_active = false;

// In Cast:
Motion_Init(&s_ms, (MotionParams){
    .type         = MOTION_SPIRAL,
    .speed        = 3.5f,
    .spiralRadius = 0.35f,
    .spiralFreq   = 3.0f,
    .arrivalRadius = 0.25f,
}, startPos, target);
s_active = true;

// In Update:
if (s_active) {
    Motion_Step(&s_ms, dt);
    // Emit particles at current position (s_ms.pos)
    if (Motion_Arrived(&s_ms)) {
        s_active = false;
        Entity_ApplyAoEDamage(s_ms.pos, 0.8f, s_damage, 2.0f);
        SpawnImpactEffect(s_ms.pos, EFFECT_PRESET_WATER_SPLASH, 1.0f);
    }
}
```

---

## 14. Conventions & Rules

### Memory
- **No malloc/free.** Static arrays everywhere. Pool overflow = silent no-op.
- Store per-projectile state in fixed-size static arrays (e.g. `#define MAX_PROJ 8`).

### Colors
- **Always use `ELEMENT_COLOR_*` macros.** Never hardcode RGB values for element identity.
- Use `ColorAlpha(ELEMENT_COLOR_X, alpha)` to add transparency (alpha 0–255).

### Scale
- All spatial values in **meters**. 1 unit = 1 meter.
- Mesh radii: 0.10–0.20 m
- Forces/gravity: 3.0–7.0 m/s² (real gravity = 9.81 m/s²)
- Particle speeds: 1.0–3.0 m/s
- Projectile speeds: 2.0–6.0 m/s

### Drawing
- **No `DrawSphere`/`DrawCylinder`** — use `DrawCoreSphere`, `DrawCoreCylinder` from `core/procedural_mesh_utils.h`.
- Call `rlDrawRenderBatchActive()` before any `rlDisableDepthMask()` or `rlDisableDepthTest()`.
- **No RNG in Draw** — all randomness at spawn/cast time, stored on instance structs.

### Resource Lifetime
- **No `UnloadShader`/`UnloadTexture`** inside `Unload[Name]Skill` — ResourceManager owns shader/texture lifetime.
- Unloading meshes (`Model`, `Mesh`) in Unload is fine.
- Do not unload Models while Afterimage ghosts referencing them are still alive.

### Randomness
- Use Raylib's `GetRandomValue(min, max)` — returns int. Divide to get float jitter.
- All jitter/randomness must be computed at spawn time, not in Draw.

### Perpendicular jitter
- When spawning particles along a path, add `±s_jitter` on an axis perpendicular to the travel direction to avoid perfectly straight/flat layouts.

### ForceMix rebuild rule
- Never build ForceMix layers once at Init and cache forever. Rebuild the ForceField each frame from `SkillForceMix_AddLayers` so sandbox changes take effect immediately.

### Trail cleanup
- `SpawnProjectileTrail` returns an ID that **must** be passed to `KillTrail(id)` on impact or cancellation.
- `SpawnLightningTrail` self-terminates — no `KillTrail` needed for the normal code path.

### Sandbox vs. shipped skills
- Use `ApplyAoEDamage` (core/skill_manager.h) in sandbox/test-only code paths.
- Use `Entity_ApplyAoEDamage` (entities/entities.h) in all shipped skill logic.
- When both modes must work: call `Entity_ApplyAoEDamage` first; fall back to `ApplyAoEDamage` if the entity system is unavailable (check your skill's registration context).

### Is[Name]SkillCoiling
- Return `true` while the skill is actively running (projectile in flight, effect ongoing).
- Return `false` once the skill has fully resolved (idle state).

### Get[Name]SkillProjectiles
- Fill `outProjectiles` with all currently active projectiles (position + radius + active flag).
- Return count written (0 if nothing active).
- `Deactivate[Name]Projectile(index)` sets the indexed projectile's `active = false`.

### Header includes (minimal required set)
```c
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"                          // if using rlDisableDepthMask etc.
#include "core/skill_manager.h"            // SkillParams, SkillProjectile, tunables
#include "core/particle_system.h"
#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/vfx_light.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/camera_fx.h"
#include "core/skill_helper.h"
#include "core/motion_controller.h"
#include "core/status_vfx.h"               // if using status auras
#include "core/afterimage.h"               // if using afterimages
#include "core/procedural_mesh_utils.h"    // DrawCoreSphere, DrawCoreCylinder
#include "entities/entities.h"             // Entity_ApplyAoEDamage
```
