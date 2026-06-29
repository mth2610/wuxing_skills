# WUXING SKILLS DEVELOPMENT & CORE ENGINE API SPECIFICATION
> This document is the definitive technical API reference and architectural specification for creating skills in the Wuxing elements engine. 

---

## 1. PROJECT COMPILATION & ARCHITECTURE RULES

### 1.1 Language & Standards
* **Dialect:** Strict C99.
* **Environment:** Raylib 5.5, OpenGL 3.3 (Core Profile), 3D Isometric Night-time Arena.
* **Standard Headers:** Every `.c` skill file **MUST** explicitly `#include <stddef.h>`, `#include <stdlib.h>`, and `#include <stdio.h>` to avoid compilation errors regarding `NULL` and `snprintf`. Do not assume they are implicitly included.
* **Include Paths:** Use relative paths from root: `#include "core/particle_system.h"`, etc.
* **`PI` Macro Guard:** `raylib.h` already defines `PI` as `3.14159265358979323846f`. **Never** write a bare `#define PI`. Always guard with:
  ```c
  #ifndef PI
  #define PI 3.1415926535f
  #endif
  ```
  A bare redefinition triggers `-Wmacro-redefined` on Clang and is treated as a hard error in strict project builds.

### 1.2 Memory Constraints (Crucial)
* **Strictly NO Dynamic Allocation:** Calling `malloc`, `calloc`, `realloc`, or `free` is **PROHIBITED** inside skill code.
* **Static Allocation Pattern:** All dynamic entities (e.g. projectiles, active spikes, trail lists) must be managed using fixed-size static arrays (`static MyStruct s_entities[MAX_ENTITIES];`) and state flags (`bool active;` or state enums).
* **Stack Allocations:** Standard stack variables and structs are allowed.

### 1.3 Automatic Directory Scanning & Registry
New skills are automatically scanned and registered on compilation by `scripts/generate_registry.py`. Place your files in:
```
skills/[element]/[skill_name]_skill/
    ├── [skill_name]_skill.h  # Lifecycle prototypes
    ├── [skill_name]_skill.c  # Physics, logic & rlgl rendering
    ├── [skill_name].vs        # Vertex shader (Optional, automatically copied)
    ├── [skill_name].fs        # Fragment shader (Optional, automatically copied)
    └── [texture].png          # Texture assets (Optional, automatically copied)
```

> [!IMPORTANT]
> **Include Path Folder Matching Rule:** When including your skill's own header file within its `.c` source file, the path **MUST EXACTLY match** the directory structure where it is saved. For example, if you place your files in `skills/wood/jade_burst_skill/`, you MUST include it as `#include "skills/wood/jade_burst_skill/jade_burst_skill.h"`. Beware of typo errors or omitting suffix markers like `_skill` from the folder name.

---

## 2. CENTRALIZED ELEMENT COLORS & STYLING
The engine defines six customizable global base colors in [core/skill_manager.h](file:///Users/mth2610/Desktop/c_games/wuxing_skills/core/skill_manager.h):
* `ELEMENT_COLOR_WATER` : Cyan-Blue `(Color){ 41, 128, 185, 255 }`
* `ELEMENT_COLOR_WOOD`  : Emerald Green `(Color){ 46, 204, 113, 255 }`
* `ELEMENT_COLOR_FIRE`  : Crimson Red `(Color){ 231, 76, 60, 255 }`
* `ELEMENT_COLOR_EARTH` : Ochre Brown/Orange `(Color){ 230, 126, 34, 255 }`
* `ELEMENT_COLOR_METAL` : Silver Gray `(Color){ 149, 165, 166, 255 }`
* `ELEMENT_COLOR_TAIJI` : Amethyst Purple `(Color){ 155, 89, 182, 255 }`

### Rules for Color Usage
* **No Hardcoded Raw Colors:** Skill visual components (particles, meshes, decals, lights, and floating text) **MUST** derive their colors from these macro definitions.
* **Shading & Opacity Adjustments:** To create highlights, shadows, or fading trails, blend the base color using Raylib's helper functions:
  - `ColorAlpha(Color color, float alpha)` to apply opacity.
  - `ColorLerp(Color color1, Color color2, float factor)` to mix colors (e.g., mix with `BLACK` for dark roots/bark, or with `WHITE` for glowing energy).

---

## 3. CORE RESOURCE MANAGER (ASSET CACHING)
To optimize VRAM and prevent duplicate file loadings, skills must load textures and shaders through the Resource Manager (`#include "core/resource_manager.h"`):

### APIs
* `Texture2D ResourceManager_LoadTexture(const char *filePath);`
  - Loads a texture or retrieves it from cache if already loaded (e.g. sharing `crack.png` across skills).
* `Shader ResourceManager_LoadShader(const char *vsFilePath, const char *fsFilePath);`
  - Loads/compiles custom vertex and fragment shaders. Pass `NULL` for `vsFilePath` only for shaders that do not require custom vertex processing. Skills using 3D lighting must always provide both `.vs` and `.fs`.
### Mandatory Teardown Rule
* **DO NOT** call Raylib's `UnloadTexture` or `UnloadShader` inside your skill's `Unload[Name]Skill` callback. Leave the callback empty or commented; the global Resource Manager automatically unloads and frees all cached resources when the application shuts down.

---

## 4. STANDARD LIFECYCLE API (`[skill_name]_skill.h`)

For automatic detection, your header file must declare these exact prototypes (replace `[Name]` with your unique CamelCase skill name):

## SkillParams;
```c
typedef struct {
    int level;
    int milestone;
    int quantity;
    float sizeScale;
    float damage;
    CastAnchorType anchorType;
    CastPathType pathType;
    bool showPortal;
    
    // Path Drawing Data
    int pathPointCount;
    Vector3 pathPoints[32]; // Max 32 points for a drag-to-cast path
} SkillParams;

```c
#ifndef SKILL_[NAME]_H
#define SKILL_[NAME]_H

#include "raylib.h"
#include "core/skill_manager.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Main lifecycle
void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);

// Engine ↔ Skill communication
bool Is[Name]SkillCoiling(void);
int Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);

#endif // SKILL_[NAME]_H
```
---

## 5. DYNAMIC FORCE FIELD SYSTEM (`#include "core/force_field.h"`)
A `ForceField` contains up to **8 cumulative layers** that dynamically modify particle and trail velocities in 3D space.

### Common Setup Pattern
```c
static ForceField s_forceField; // MUST be static
ForceField_Clear(&s_forceField);
ForceField_AddLayer(&s_forceField, (ForceLayer){
    .type      = FORCE_VORTEX,
    .origin    = centerPoint,
    .direction = (Vector3){0.0f, 1.0f, 0.0f}, // Normalized axis of rotation
    .strength  = 5.0f,
    .radius    = 15.0f,
    .falloff   = 1.0f                      // Linear drop-off
});
```

### ForceLayer Types & Parameters Reference
| ForceType | `origin` | `direction` | `strength` | `radius` | `falloff` | `noiseScale` | `noiseSpeed` |
|---|---|---|---|---|---|---|---|
| `FORCE_GRAVITY_DIR` | Unused | Gravity vector (normalized) | Magnitude of acceleration | Unused | Unused | Unused | Unused |
| `FORCE_GRAVITY_POINT` | Center of attraction | Unused | Positive = attract, Negative = repel | Active range | 0=Constant, 1=Linear, 2=Quadratic | Unused | Unused |
| `FORCE_VORTEX` | Center of vortex | Axis of rotation (normalized) | Angular speed (ccw/cw) | Active range | 0=Constant, 1=Linear | Unused | Unused |
| `FORCE_WIND` | Unused | Wind vector (normalized) | Acceleration magnitude | Unused (Global) | Unused | Unused | Unused |
| `FORCE_NOISE_PERLIN` | Offset seed | Unused | Noise amplitude | Unused | Unused | Frequency of noise | Speed of animation |
| `FORCE_NOISE_CURL` | Offset seed | Unused | Noise amplitude | Unused | Unused | Frequency of noise | Speed of animation |
| `FORCE_DRAG` | Unused | Unused | Linear drag coefficient (0..1) | Unused | Unused | Unused | Unused |
| `FORCE_VISCOSITY` | Unused | Unused | Viscous damping coefficient | Unused | Unused | Unused | Unused |
| `FORCE_RADIAL_AXIS` | Unused (Dynamic) | Unused (Dynamic) | Positive = push, Negative = pull | Active range | 1=Linear | Unused | Unused |
| `FORCE_VORTEX_AXIS` | Unused (Dynamic) | Unused (Dynamic) | Rotation speed around axis | Active range | 1=Linear | Unused | Unused |

* **Dynamic Axis (RADIAL_AXIS / VORTEX_AXIS):** These forces ignore the `origin` and `direction` in the `ForceLayer` struct. Instead, they dynamically use the `axisOrigin` and `axisDir` passed each frame during evaluation (e.g. via `SetFollowerAxis()` for trails).

* **Falloff Semantic:** `0.0` = constant force throughout, `1.0` = linear decrease to zero at radius boundary, `2.0` = quadratic decrease (natural gravitational/magnetic falloff).
* **Viscosity Damping:** Use `ForceField_GetViscosityDamping(&s_forceField, dt)` inside manual update loops to damp velocity: `myVel = Vector3Scale(myVel, dampFactor);`.

---

## 6. PARTICLE SYSTEM (`#include "core/particle_system.h"`)
ParticleConfig should be initialized with {0}.
`void SpawnParticle(ParticleConfig config);` triggers particle emission in the engine.
### Configuration API
```c
typedef struct {
    Vector3 position, velocity;
    Color colorStart, colorEnd;
    float radius, lifetime;
    const ForceField *forceField;        // Dynamic steering
    const ColorGradient *gradient;       // Overrides colorStart/colorEnd
    const SpriteAnim *spriteAnim;        // Optional sprite animation
    const ParticleConfig *onDeathEmit;   // [Sub-Emitter] Spawned on death
    int onDeathEmitCount;                // Quantity to spawn on death
    const ParticleConfig *onLiveEmit;    // [Trail-Emitter] Spawned along path
    float onLiveEmitRate;                // Spawn rate (particles per second)
} ParticleConfig;
```
* **Color Priority:** If `gradient` is not `NULL`, `colorStart` and `colorEnd` are ignored. Always prefer `ColorGradient` for multi-stage color shifts (e.g. fire core white -> orange -> dark ash).
* **Sub-Emitter Lifecycle:** Sub-emitters (`onDeathEmit` and `onLiveEmit`) inherit the parent position but **do not** inherit velocity. Configs passed to sub-emitters **MUST** be declared static (persistent scope).

---

## 7. TRAIL & RIBBON SYSTEM (`#include "core/trail_system.h"`)
TrailConfig should be initialized with {0}.
`int SpawnTrailEntity(TrailConfig config);` spawns ribbon-based trail components.

### Configurations
```c
typedef enum {
    TRAIL_TYPE_PROJECTILE,  // Automatically flies towards target using vel + forceField
    TRAIL_TYPE_WISP,        // Drifts randomly in wind/noise fields
    TRAIL_TYPE_PORTAL,      // Static position, rotates in place (summoning circles)
    TRAIL_TYPE_FOLLOWER     // Manually driven. Follows coordinates bound by skill code
} TrailType;

typedef struct {
    TrailType type;
    Vector3 pos, vel, target;
    float len;              // Segment/blade length (perpendicular to movement direction)
    float thick;            // Thickness of ribbon at head
    float trailLength;      // Absolute trail decay length (world units)
    float life;             // Total duration in seconds (0 = persistent until KillTrail)
    float scale;
    Texture2D tex;
    Color tint;
    Shader shader;
    const ForceField *forceField;
    const ColorGradient *gradient;
    const SpriteAnim *spriteAnim;
} TrailConfig;
```
* **Follower Trails:** For sword swings or aura attachments, set type to `TRAIL_TYPE_FOLLOWER`. Every frame, you must call:
  - `SetFollowerAxis(trailId, basePos, normalizedDir);` (Sets orientation)
  - `UpdateFollowerPosition(trailId, tipPos);` (Updates head position)
* **Lifecycle:** Free active trails when complete by calling `KillTrail(trailId);`.

---

## 8. Graphics & VFX API

### Ground Decals (`core/decal_system.h`)
```c
void DecalSystem_Add(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);
```
Rules:
- Prevents Z-fighting automatically.
- Draw before 3D meshes.
- Recommended scale: 4–5.5× structure radius.

### Screen Distortion (`core/screen_distort.h`)
```c
void ScreenDistort_Add(Vector3 pos, float radius, float strength, float life, float speed);
```

### Standard Gradient (`core/color_gradient_ext.h`)
```c
void ColorGradient_StandardFade(ColorGradient *grad, Color baseColor, float midT, float brightenAmount);
```

### Particle Radial Burst (`core/particle_radial_burst.h`)
```c
typedef struct {
    int countMin, countMax;
    float speedMin, speedMax;
    float radiusMin, radiusMax;
    float lifetimeMin, lifetimeMax;
    float pitchRange, upwardBias;
    Color colorStart, colorEnd;
    const ColorGradient *gradient;
    const ForceField *forceField;
} ParticleRadialBurstConfig;

void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg);
```

### Impact Burst (`core/impact_burst.h`)
```c
typedef struct {
    /* --- Step 1: screen distortion --- */
    bool  distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;

    /* --- Step 2: ground decal --- */
    bool     decalEnabled;
    Texture2D decalTex;
    float     decalScale;   /* multiplied by sizeScale at call time */
    float     decalLife;
    Color     decalTint;
    bool      decalRandomRotation; /* true = GetRandomValue(0,360), false = use decalFixedRotation */
    float     decalFixedRotation;

    /* --- Step 3: point light flash --- */
    bool  lightEnabled;
    Color lightColor;
    float lightRadius;  /* multiplied by sizeScale at call time */
    float lightLife;

    /* --- Step 4: radial particle burst --- */
    bool particlesEnabled;
    ParticleRadialBurstConfig particles;
} ImpactBurstConfig;

void VFX_TriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);
```

### Combat (`core/skill_manager.h`)
```c
void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
```

### Shader Binding (`core/skill_manager.h`)
```c
void SkillManager_BeginShader(Shader shader);
void SkillManager_EndShader(void);
```
Automatically binds `u_time`, `viewPos`, `u_resolution`.

### VFX Standards
- Keep dark diffuse materials; avoid fully emissive meshes.
- Spawn particles continuously while active.
- Keep point lights alive during the active phase.

### Procedural Mesh (`core/procedural_mesh_utils.h`)
```c
void DrawCoreSphere(
    Vector3 center,
    float radius,
    int rings,
    int slices,
    Color color
);

void DrawCoreCylinder(
    Vector3 bottom,
    Vector3 top,
    float radiusBottom,
    float radiusTop,
    int slices,
    Color color
);

void DrawCoreCone(
    Vector3 bottom,
    float radius,
    float height,
    int slices,
    Color color
);

void DrawCorePlaneRect(
    Vector3 center,
    Vector2 size,
    Color color
);

void DrawCorePlanePolygon(
    Vector3 center,
    float radius,
    int sides,
    Color color
);

void DrawCoreCube(
    Vector3 position,
    float width,
    float height,
    float length,
    Color color
);

void DrawCoreTorus(
    Vector3 center,
    float innerRadius,
    float outerRadius,
    int sides,
    int rings,
    Color color
);

void DrawCorePrism(
    Vector3 bottom,
    Vector3 top,
    float radius,
    int sides,
    Color color
);

typedef struct {
    /* Radius profile */
    float capsuleTailExp;
    float tailTaperMin;
    float tailTaperMax;
    float headGrowth;

    /* Frame wobble */
    float wobbleAmplitude;
    float wobbleFrequency;
    float wobbleSpeed;

    /* Surface deformation */
    float deform1Amp, deform1FreqT, deform1FreqPhi, deform1Speed;
    float deform2Amp, deform2FreqT, deform2FreqPhi, deform2Speed;

    /* End-cap shape */
    float tailApexFactor;
    float headApexFactor;
} TubeMeshConfig;

typedef struct {
    Vector3 rings[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
    Vector3 normals[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];

    Vector3 tailCenter, headCenter;
    Vector3 tailTangent, headTangent;

    float tailRadius, headRadius;

    int segments;
    int radialSegs;

    float tailApexFactor;
    float headApexFactor;
} TubeMeshData;

TubeMeshConfig ProceduralMesh_DefaultTubeConfig(void);

void ProceduralMesh_BuildTube(
    TubeMeshData *out,
    Vector3 p0, Vector3 p1,
    Vector3 p2, Vector3 p3,
    float baseRadius,
    float flowProgress,
    float time,
    int segments,
    int radialSegs,
    const TubeMeshConfig *cfg
);

void ProceduralMesh_DrawTube(
    const TubeMeshData *data,
    float uvLengthScale
);
```
Rules:
- Use for circular Bezier tubes.
- Never implement Bezier, Frenet frame or tube generation inside skills.
- Start from ProceduralMesh_DefaultTubeConfig() and override only the fields you need.

### Post FX (`core/post_fx.h`)

```c
typedef struct {
    /* Bloom */
    bool bloomEnabled;
    float bloomThreshold;
    float bloomIntensity;

    /* Chromatic Aberration */
    bool chromaticEnabled;
    float chromaticStrength;

    /* Vignette */
    bool vignetteEnabled;
    float vignetteRadius;
    float vignetteSoftness;

    /* Color Grading */
    bool colorGradeEnabled;
    float contrast;
    float saturation;
    Vector3 colorTint;
} PostFXConfig;
```

### Camera FX (`core/camera_fx.h`)
```c
void CameraFX_Shake(float trauma);
```
`0.25` = Light, `0.5` = Medium, `0.75–1.0` = Heavy.

### Audio
```c
PlaySound(Sound sound);
```
Load sounds in `InitSkill()`.

## 10. GLSL Shader Guidelines

### Required Includes

Vertex Shader

```glsl
#version 330
#include "core/shaders/common/vs_header.glsl"
```

Fragment Shader

```glsl
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
```

### Shader Loading

Skills using custom 3D lighting must always load both vertex and fragment shaders.

```c
Shader shader = ResourceManager_LoadShader(
    "skill.vs",
    "skill.fs"
);
```

Only unlit shaders may pass `NULL` as the vertex shader.

Do not use `NULL` as the vertex shader when using lighting.

### Built-in Variables

Provided automatically by the common headers.

Do not redeclare:

- `u_time`
- `viewPos`
- `u_resolution`
- `fragPosition`
- `fragNormal`
- `fragTexCoord`
- `finalColor`

### Built-in Functions

Provided by the engine:

```glsl
VS_FinalOutput(...)
perturbNormal(...)
calcFresnel(...)
calcSpecular(...)
```

Do not reimplement these functions.

### Rules

- Always use both `.vs` and `.fs` for 3D shaders.
- Include `fs_header.glsl` before `lighting.glsl`.
- Call `VS_FinalOutput()` as the final step of every vertex shader.
- Declare only skill-specific uniforms.
- Keep shader logic focused on the visual behavior of the element.

---

---
## 11 3D Rendering & Shader Best Practices

### 11.1 Vertex Color Reset

Before drawing custom geometry with `rlBegin()`, always reset the vertex color:
```c
rlColor4ub(255, 255, 255, 255);
```
Otherwise the mesh may inherit colors from previous draw calls.

### 11.2 Procedural Noise
When using world-space procedural noise:
- Use low world-coordinate scales (e.g. `fragPosition.xz * 0.05`).
- Avoid high frequencies that produce TV-static artifacts.
- Stretch individual axes when directional patterns are desired.

### 11.3 3D Lighting

The default Raylib vertex shader cannot be used for custom 3D lighting.

Rules:

- Always provide both `.vs` and `.fs`.
- Load both with `ResourceManager_LoadShader()`.
- Never pass `NULL` as the vertex shader.
- Use `core/shaders/common/vs_header.glsl` and `VS_FinalOutput()`.

---

---
## 12 Critical Aesthetic Laws (Anti-Robotic Design)

### 12.1 No Raylib Primitives

Do not use `DrawCylinder()`, `DrawCone()`, `DrawCube()`, `DrawSphere()`, or wireframe variants for core skill meshes.

Use procedural meshes or engine mesh APIs.

---

### 12.2 Perpendicular Jitter

Avoid perfectly straight layouts.

```c
Vector3 perp = { -dir.z, 0.0f, dir.x };
float jitter = (float)GetRandomValue(-120, 120) / 10.0f;
Vector3 organicPos = Vector3Add(pos, Vector3Scale(perp, jitter));
```

---

### 12.3 Instance Randomization

Randomize every spawned instance.

- Scale: 85–115%
- Random yaw: 0–360°
- Pitch/Roll: ±10°

---

### 12.4 No Visual Popping

Keep the same shader active throughout the skill.

- Rising
- Active
- Dissolve

Keep `u_dissolve = 0.0` until dissolve begins, then smoothly animate it to `1.0`.

---

### 12.5 Preserve 3D Volume

To maintain solid 3D appearance:

- Restrict emissive regions with `smoothstep()` (≈20–30% coverage).
- Shade the remaining surface using diffuse lighting and Fresnel.
- Add emissive after base lighting to preserve brightness.