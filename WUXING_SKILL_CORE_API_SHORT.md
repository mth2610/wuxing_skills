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
  - Loads/compiles custom vertex and fragment shaders. Pass `NULL` for `vsFilePath` to use Raylib's default vertex shader.

### Mandatory Teardown Rule
* **DO NOT** call Raylib's `UnloadTexture` or `UnloadShader` inside your skill's `Unload[Name]Skill` callback. Leave the callback empty or commented; the global Resource Manager automatically unloads and frees all cached resources when the application shuts down.

---

## 4. STANDARD LIFECYCLE API (`[skill_name]_skill.h`)
For automatic detection, your header file must declare these exact prototypes (replace `[Name]` with your unique CamelCase skill name):

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

// Main lifecycle endpoints
void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);

// Engine-to-Skill communication APIs
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

## 8. GRAPHICS, SOUND & POST-PROCESSING APIs

### Ground Decals (`#include "core/decal_system.h"`)
* `void DecalSystem_Add(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);`
  - Renders a ground decal (like cracked earth, water pools, or ash marks). `DecalSystem_Add` is the preferred unified prefix wrapper. The engine automatically resolves depth offset to prevent Z-fighting.
  - **Circular Shader Masking:** Ground decals render via `decal.fs` which applies a smooth radial falloff from the center, fading out the corners. The result is a soft-edged circle — ideal for single clean marks like scorch burns, frost rings, or impact craters where a circular shape is *intentional*.
  - **When the circle looks artificial:** A single centered decal becomes noticeably geometric when the intended effect is irregular — e.g. a water pool spreading outward, a mud splash, ash scatter. In those cases, spawn **2–4 decals at randomized offsets and sizes** so the overlapping falloff circles merge into an organic blob:
    ```c
    // Example: water pool with organic silhouette
    float jitterR = baseRadius * 0.30f;
    for (int k = 0; k < 3; k++) {
        float a    = (float)k * (2.0f * PI / 3.0f) + GetRandomValue(0, 40) * DEG2RAD;
        float r    = jitterR * ((float)GetRandomValue(55, 100) / 100.0f);
        Vector3 dp = { center.x + cosf(a)*r, center.y, center.z + sinf(a)*r };
        DecalSystem_Add(dp, GetRandomValue(0, 360),
                        baseRadius * ((float)GetRandomValue(32, 45) / 10.0f),
                        tex, life, ColorAlpha(tint, 0.35f + k * 0.06f));
    }
    ```
  - **Drawing Order Rule:** Decals **MUST** be drawn in the render loop *before* opaque 3D skill/projectile meshes (e.g. `DecalSystem_Draw()` runs *before* `DrawSkillManagerWorld3D()`). This guarantees that emerging structures or characters naturally overlap the decals rather than drawing over them.
  - **Aesthetic Scale Rule:** Never make ground decals tiny or exactly the same size as the structure's base. Decals must look impactful. Always scale the decal size to **`4.0x` to `5.5x`** the base radius of the emerging structure (e.g. `baseRadius * scale * 5.2f`).

### Screen Distortion (`#include "core/screen_distort.h"`)
* `void ScreenDistort_AddSource(Vector3 pos, float rad, float str, float life, float speed);`
* `void ScreenDistort_Add(Vector3 pos, float rad, float str, float life, float speed);`
  - Triggers a radial shockwave/heat-refraction distortion on screen. `ScreenDistort_Add` is the preferred unified prefix wrapper. Best used at collision/impact points.

### Standardized Combat API (`#include "core/skill_manager.h"`)
* `void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);`
  - Applies area-of-effect damage and knockback to the enemy character.
  - The Core System automatically tracks the enemy's coordinates and radius internally, eliminating the need to pass `enemyPos` or `enemyRadius` manually.
  - Shows floating text displays (damage value and action text) automatically and adds a localized 3D knockback vector to the target.

### Automatic Shader Uniform Binding (`#include "core/skill_manager.h"`)
* `void SkillManager_BeginShader(Shader shader);`
* `void SkillManager_EndShader(void);`
  - Wraps Raylib's `BeginShaderMode` and `EndShaderMode` but automatically binds essential environment uniforms (`u_time` as a float, `u_viewPos` as a Vector3 representing the camera position, and `u_resolution` as a Vector2 of the screen size) to prevent boilerplate code in individual skills.

### VFX & Emission Standards (Critical Quality Rules)
* **Emissive Shading Contrast:** To make meshes pop in the night arena without looking flat/artificial, keep organic dark textures (e.g. deep forest green, gnarled bark brown) and multiply the diffuse output by an animated breathing light multiplier (e.g. `diffuse.rgb * (1.35f + 0.1f * sin(u_time * 3.5f))`). Avoid flat bright neon colors.
* **Continuous Particle Aura:** Do not only spawn particles at birth or impact. During the active holding phase of a skill (e.g. `THORN_HOLDING`), continuously spawn themed particles (like rising poison gas, drifting steam, or floating sparks) along the height and radius of the mesh at a controlled rate (e.g. 20-30% chance per frame) to make the skill look alive.
* **Persistent Lights:** Source point lights spawned during emergence should match the active holding phase's lifetime (e.g. rise + hold time = `1.4f` seconds) so the scene remains illuminated while the skill is active.

### Procedural Mesh Utilities (`#include "core/procedural_mesh_utils.h"`)
Avoid raw Raylib primitives (which are strictly prohibited). Use these raw `rlgl`-based procedural helpers to draw standard primitives with correct normal mapping and texture coordinates:
* `void DrawCoreSphere(Vector3 center, float radius, int rings, int slices, Color color);`
* `void DrawCoreCylinder(Vector3 bottom, Vector3 top, float radiusBottom, float radiusTop, int slices, Color color);`
* `void DrawCoreCone(Vector3 bottom, float radius, float height, int slices, Color color);`
* `void DrawCorePlaneRect(Vector3 center, Vector2 size, Color color);`
* `void DrawCorePlanePolygon(Vector3 center, float radius, int sides, Color color);`
* `void DrawCoreCube(Vector3 position, float width, float height, float length, Color color);`
* `void DrawCoreTorus(Vector3 center, float innerRadius, float outerRadius, int sides, int rings, Color color);`
* `void DrawCorePrism(Vector3 bottom, Vector3 top, float radius, int sides, Color color);`

### Post-Processing & Bloom (`#include "core/post_fx.h"`)
Adjust screen bloom, vignette, or chromatic aberration dynamically during casting:
```c
PostFXConfig cfg = {
    .bloomEnabled = true,
    .bloomThreshold = 0.65f,
    .bloomIntensity = 1.2f,
    .chromaticEnabled = true,
    .chromaticStrength = 0.15f
};
// Update engine config
// (Engine reads active skill state and composites via post_process.fs)
```

### Camera Effects (`#include "core/camera_fx.h"`)
* `void CameraFX_Shake(float trauma);`
  - Triggers a screen-shake effect. `trauma` is a **single float in the range `0.0..1.0`** representing shake intensity (not duration — the engine derives duration and magnitude internally from the trauma value).
  - **Common values:** `0.25f` = light rumble (skill activation), `0.5f` = medium hit, `0.75f–1.0f` = heavy impact / explosion.
  - **Common Mistake:** Do **NOT** pass two arguments `(duration, magnitude)`. The function signature is exactly `void CameraFX_Shake(float trauma)` — one argument only.

### Audio Triggers
Play sound effects using standard Raylib audio APIs:
* `PlaySound(Sound sound);` (Ensure sound assets are preloaded via `LoadSound` in `Init`).

---

## 9. CRITICAL AESTHETIC LAWS (ANTI-ROBOTIC DESIGN)
Skills must strictly adhere to these visual guidelines to ensure organic, fluid movement:

1. **NO Raylib Primitives:** Calling high-level drawing functions like `DrawCylinder()`, `DrawCone()`, `DrawCube()`, `DrawSphere()`, or their wireframe variants is **STRICTLY PROHIBITED** for core skill meshes. Primitives lack proper texture coordinate mapping and normal alignment, and wireframe meshes generate generic grid lines that break visual immersion. You must build your meshes procedurally using `rlgl` vertex buffers.
2. **Perpendicular Jitter:** Never align sequential elements (e.g. sprouting roots, falling lightning, rock formations) in a straight mathematical line. Calculate a perpendicular offset vector and add randomized offset displacement:
   ```c
   Vector3 perp = { -dir.z, 0.0f, dir.x };
   float jitter = (float)GetRandomValue(-120, 120) / 10.0f;
   Vector3 organicPos = Vector3Add(pos, Vector3Scale(perp, jitter));
   ```
3. **Instance Randomization:** Vary the scale, yaw rotation, and pitch/roll tilt parameters slightly for each spawned entity to create an organic, hand-crafted aesthetic:
   - Scale range: `scale = baseScale * (float)GetRandomValue(85, 115) / 100.0f;`
   - Yaw range: `yaw = (float)GetRandomValue(0, 360) * (DEG2RAD);`
   - Tilt (Pitch/Roll): Add `±10 degrees` tilt to vertical elements so they emerge pointing in organic directions.
4. **No Visual Popping:** Always bind the custom shader (`BeginShaderMode`) during **all active phases** (rising, holding, and dissolving) of the skill. Set the uniform `u_dissolve = 0.0f` while the skill is fully intact, and transition it smoothly from `0.0f` to `1.0f` during the dissolution phase. This maintains consistent lighting and prevents sudden visual jumps when the dissolve phase begins.

---

## 10. PROCEDURAL 3D MESH GENERATION (THE TUBE METHODOLOGY)
To construct organic elements (water tubes, branches, fire streams, rocky pillars) along a path, use the mathematical pipeline of the **Tube Skill**:

### Step 1: Evaluate Path Coordinates (Cubic Bezier)
Evaluate coordinates along the trajectory from `p0` to `p3` using a parametric percentage `t (0.0..1.0)`:
```c
Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(p0, u * u * u),
        Vector3Add(
            Vector3Scale(p1, 3.0f * u * u * t),
            Vector3Add(Vector3Scale(p2, 3.0f * u * t * t), Vector3Scale(p3, t * t * t))
        )
    );
}

// Calculate the first derivative (tangent) to align rings:
Vector3 GetBezierDerivative(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u),
        Vector3Add(
            Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t),
            Vector3Scale(Vector3Subtract(p3, p2), 3.0f * t * t)
        )
    );
}
```

### Step 2: Establish the Local Frenet Frame
Compute orthogonal `Right` and `Up` orientation vectors relative to the curve's direction at each point:
```c
Vector3 tangent = Vector3Normalize(GetBezierDerivative(p0, p1, p2, p3, t));
Vector3 upTemp = (fabsf(tangent.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
Vector3 right = Vector3Normalize(Vector3CrossProduct(upTemp, tangent));
Vector3 up = Vector3CrossProduct(tangent, right);
```

### Step 3: Apply Spiral Twist & Wobble
To animate flowing liquid or twist wooden bark, rotate the local coordinate frame axes along the path over time:
```c
float wobble = wobbleAmplitude * sinf(t * PI * wobbleFrequency + time * wobbleSpeed);
Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)), Vector3Scale(right, sinf(wobble)));
Vector3 twistedRight = Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));
```

### Step 4: Taper & Perturb Ring Vertices
Calculate final ring vertex positions, adjusting the radius to taper at the ends and adding noise waves:
```c
float baseCapsule = 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI))); // Taper ends
float finalRadius = baseRadius * baseCapsule * scale;

for (int j = 0; j < RADIAL_SEGS; j++) {
    float phi = (float)j * (2.0f * PI) / RADIAL_SEGS;
    Vector3 normal = Vector3Add(Vector3Scale(twistedRight, cosf(phi)), Vector3Scale(twistedUp, sinf(phi)));
    
    // Wave perturbation (overlapping sine waves)
    float wave1 = sinf(t * 18.0f + phi * 3.0f + time * 10.0f);
    float wave2 = sinf(t * 9.0f - phi * 5.0f - time * 6.0f);
    float deform = 1.0f + wave1 * 0.12f + wave2 * 0.08f;
    
    Vector3 vertexPos = Vector3Add(pathPos, Vector3Scale(normal, finalRadius * deform));
}
```

### Step 5: Render Quads and Closed End-Caps
1. **Quad Strip:** Render faces using a quad loop, calculating vertex normals (`rlNormal3f`) and mapping UV texture coordinates (`rlTexCoord2f`). Set U based on the radial angle `phi / (2 * PI)` and V based on the path progress `t * tiling + time * speed` to animate flow.
2. **Solid Capping:** To prevent hollow openings, close both ends of the tube using triangle fans (`rlBegin(RL_TRIANGLES)`):
   - **Tail Cap:** Connect the vertices of the first ring (`t = 0`) to a center apex point: `apex = tailCenter - tangent * radius * 0.25f`.
   - **Head Cap:** Connect the vertices of the last ring (`t = 1`) to a center apex point: `apex = headCenter + tangent * radius * 0.25f`.

---

## 11. COMPLETE IMPLEMENTATION TEMPLATE
Below is a complete, self-contained reference implementation for a skill (**"Wood Thorns"**). It demonstrates static array allocation, `ResourceManager` usage, global element color integration, physics state logic, dynamic lights, camera shaking, decals, and `rlgl` vertex drawing.

### Header File (`wood_thorns_skill.h`)
```c
#ifndef WOOD_THORNS_SKILL_H
#define WOOD_THORNS_SKILL_H

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

void InitWoodThornsSkill(int screenWidth, int screenHeight);
void CastWoodThornsSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateWoodThornsSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawWoodThornsSkill(void);
void UnloadWoodThornsSkill(void);

bool IsWoodThornsSkillCoiling(void);
int GetWoodThornsSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateWoodThornsProjectile(int index);

## 11. COMPLETE IMPLEMENTATION TEMPLATE: WATER STREAM (TUBE)
This template demonstrates the creation of the **Water Stream (Tube)** skill. It showcases dynamic 3D Bezier tube generation, Frenet-frame orientation, organic vertex perturbation, and custom shader rendering.

> [!WARNING]
> * **3D Mesh Generation (Bezier/Frenet Math):** This code is the gold standard reference for procedurally extruding 3D organic meshes along curves.
> * **VFX & Particle Quality:** The particle bursts, splash animations, and collision VFX implemented in this template are basic, simplified placeholding mechanics. They **MUST NOT** be used as a quality reference. For premium artistic execution, you must design custom, highly-organic, element-specific particle systems and shaders following the guidelines in [WUXING_ART_DIRECTION.md](file:///Users/mth2610/Desktop/c_games/wuxing_skills/WUXING_ART_DIRECTION.md).

### Header File (`water_stream_skill.h`)
```c
#ifndef WATER_STREAM_SKILL_H
#define WATER_STREAM_SKILL_H

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

void InitWaterStreamSkill(int screenWidth, int screenHeight);
void CastWaterStreamSkill(Vector3 startPos, Vector3 target, SkillParams params);
void UpdateWaterStreamSkill(float dt, Vector3 enemyPos, float enemyRadius);
void DrawWaterStreamSkill(void);
void UnloadWaterStreamSkill(void);

bool IsWaterStreamSkillCoiling(void);
int GetWaterStreamSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void DeactivateWaterStreamProjectile(int index);

#endif // WATER_STREAM_SKILL_H
```

### Implementation File (`water_stream_skill.c`)
```c
#include "skills/water/water_stream/water_stream_skill.h"
#include "core/resource_manager.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#define PI 3.1415926535f

#define MAX_STREAMS          4
#define TUBE_SEGMENTS        30
#define RADIAL_SEGS          16
#define BASE_RADIUS          8.0f
#define FLOW_SPEED           1.5f
#define STREAM_LIFETIME      1.8f
#define DUST_PARTICLES       15

typedef struct {
    Vector3 p0, p1, p2, p3;  // Cubic Bezier control points
    float   progress;        // 0.0 (cast) -> 1.0 (reached target)
    float   scale;
    float   life;            // Remaining duration
    Vector3 headPos;         // Current tip coordinate
    bool    active;
    bool    impactTriggered;
    float   damage;
    float   knockback;
} WaterStream;

static WaterStream   s_streams[MAX_STREAMS];
static Texture2D     s_causticsTex;
static Shader        s_shader;
static int           s_uDissolveLoc;
static int           s_uTimeLoc;
static ColorGradient s_splashGrad;

static int FindFreeSlot(void) {
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!s_streams[i].active) return i;
    }
    return -1;
}

// Evaluation helpers for cubic Bezier
static Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(p0, u * u * u),
        Vector3Add(
            Vector3Scale(p1, 3.0f * u * u * t),
            Vector3Add(Vector3Scale(p2, 3.0f * u * t * t), Vector3Scale(p3, t * t * t))
        )
    );
}

static Vector3 GetBezierDerivative(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    return Vector3Add(
        Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u),
        Vector3Add(
            Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t),
            Vector3Scale(Vector3Subtract(p3, p2), 3.0f * t * t)
        )
    );
}

static void TriggerImpactVFX(Vector3 pos, float scale) {
    ScreenDistort_AddSource(pos, 85.0f, 0.7f, 0.6f, 150.0f);
    Decal_Spawn(
        pos, 
        (float)GetRandomValue(0, 360), 
        BASE_RADIUS * scale * 2.5f, 
        s_causticsTex, 
        4.0f, 
        ColorAlpha(ELEMENT_COLOR_WATER, 0.7f)
    );
    VFXLight_Spawn(pos, ELEMENT_COLOR_WATER, 55.0f * scale, 0.5f);

    // Dynamic splash explosion particles
    for (int i = 0; i < DUST_PARTICLES; i++) {
        float angle = (float)i / DUST_PARTICLES * 2.0f * PI;
        Vector3 vel = {
            cosf(angle) * (float)GetRandomValue(55, 110) * scale,
            (float)GetRandomValue(60, 140) * scale,
            sinf(angle) * (float)GetRandomValue(55, 110) * scale
        };

        SpawnParticle((ParticleConfig){
            .position         = pos,
            .velocity         = vel,
            .colorStart       = ELEMENT_COLOR_WATER,
            .colorEnd         = ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.3f), 0.0f),
            .radius           = (float)GetRandomValue(25, 55) / 10.0f,
            .lifetime         = 0.6f + (float)GetRandomValue(0, 4) * 0.1f,
            .forceField       = NULL,
            .gradient         = &s_splashGrad,
            .spriteAnim       = NULL,
            .onDeathEmit      = NULL,
            .onDeathEmitCount = 0,
            .onLiveEmit       = NULL,
            .onLiveEmitRate   = 0.0f
        });
    }
}

void InitWaterStreamSkill(int w, int h) {
    (void)w; (void)h;
    for (int i = 0; i < MAX_STREAMS; i++) s_streams[i].active = false;

    // Load texture & custom shader using ResourceManager (VRAM caching)
    s_causticsTex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
    s_shader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs", "skills/water/water_stream/tube.fs");
    s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");
    s_uTimeLoc     = GetShaderLocation(s_shader, "u_time");

    // Clear and configure the color gradient based on ELEMENT_COLOR_WATER
    ColorGradient_AddStop(&s_splashGrad, 0.00f, ELEMENT_COLOR_WATER);
    ColorGradient_AddStop(&s_splashGrad, 0.40f, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.2f));
    ColorGradient_AddStop(&s_splashGrad, 1.00f, (Color){ 0, 0, 0, 0 });
}

void CastWaterStreamSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = FindFreeSlot();
    if (slot < 0) return;

    // Setup Bezier control points to create an arching/sweeping path
    Vector3 toTarget = Vector3Subtract(target, startPos);
    Vector3 control1 = Vector3Add(startPos, (Vector3){ toTarget.x * 0.25f, 25.0f, toTarget.z * 0.25f });
    Vector3 control2 = Vector3Add(startPos, (Vector3){ toTarget.x * 0.75f, 25.0f, toTarget.z * 0.75f });

    s_streams[slot] = (WaterStream){
        .p0              = startPos,
        .p1              = control1,
        .p2              = control2,
        .p3              = target,
        .progress        = 0.0f,
        .scale           = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f,
        .life            = STREAM_LIFETIME,
        .headPos         = startPos,
        .active          = true,
        .impactTriggered = false,
        .damage          = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params),
        .knockback       = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params)
    };
}

void UpdateWaterStreamSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!s_streams[i].active) continue;
        WaterStream *s = &s_streams[i];

        s->life -= dt;
        if (s->life <= 0.0f) {
            s->active = false;
            continue;
        }

        // Advance flow progress along the Bezier curve
        s->progress = Clamp(s->progress + FLOW_SPEED * dt, 0.0f, 1.0f);
        s->headPos = GetBezierPoint(s->p0, s->p1, s->p2, s->p3, s->progress);

        // Spawn head mist particles (flowing effect)
        if (s->progress < 1.0f) {
            SpawnParticle((ParticleConfig){
                .position         = s->headPos,
                .velocity         = Vector3Scale(Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, s->progress)), 12.0f),
                .colorStart       = ColorAlpha(ELEMENT_COLOR_WATER, 0.7f),
                .colorEnd         = (Color){ 255, 255, 255, 0 },
                .radius           = 2.2f * s->scale,
                .lifetime         = 0.35f,
                .forceField       = NULL,
                .gradient         = &s_splashGrad,
                .spriteAnim       = NULL,
                .onDeathEmit      = NULL,
                .onDeathEmitCount = 0,
                .onLiveEmit       = NULL,
                .onLiveEmitRate   = 0.0f
            });
        }

        // Collision detection at front tip of the stream
        if (!s->impactTriggered && s->progress >= 0.95f) {
            float distSq = Vector3DistanceSqr(s->headPos, enemyPos);
            float hitRad = (BASE_RADIUS * s->scale) + enemyRadius;
            if (distSq <= hitRad * hitRad) {
                TriggerImpactVFX(s->headPos, s->scale);
                
                char dmgText[32];
                snprintf(dmgText, sizeof(dmgText), "-%.0f", s->damage);
                AddFloatingText(enemyPos, dmgText, RED, 3.0f, 1.0f);
                
                // ApplySlow(2.5f, 0.4f); // Not available in engine yet
                AddKnockbackToEnemy(Vector3Scale(Vector3Normalize(Vector3Subtract(enemyPos, s->pos)), s->knockback));
                s->impactTriggered = true;
            }
        }
    }
}

void DrawWaterStreamSkill(void) {
    float time = (float)GetTime();
    
    // Check if any stream is active
    bool active = false;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (s_streams[i].active) { active = true; break; }
    }
    if (!active) return;

    rlDisableDepthMask();
    BeginShaderMode(s_shader);
    SetShaderValue(s_shader, s_uTimeLoc, &time, SHADER_UNIFORM_FLOAT);

    for (int idx = 0; idx < MAX_STREAMS; idx++) {
        if (!s_streams[idx].active) continue;
        WaterStream *s = &s_streams[idx];

        // Fade dissolve at end of life
        float dissolve = 0.0f;
        if (s->life < 0.4f) {
            dissolve = 1.0f - (s->life / 0.4f);
        }
        SetShaderValue(s_shader, s_uDissolveLoc, &dissolve, SHADER_UNIFORM_FLOAT);

        Vector3 rings[TUBE_SEGMENTS + 1][RADIAL_SEGS];
        Vector3 normals[TUBE_SEGMENTS + 1][RADIAL_SEGS];

        // Frenet frame math & vertex mesh generation
        for (int i = 0; i <= TUBE_SEGMENTS; i++) {
            float t = (float)i / TUBE_SEGMENTS;
            float currT = t * s->progress;
            
            Vector3 pathPos = GetBezierPoint(s->p0, s->p1, s->p2, s->p3, currT);
            Vector3 tangent = Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, currT));
            
            Vector3 upTemp = (fabsf(tangent.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
            Vector3 right = Vector3Normalize(Vector3CrossProduct(upTemp, tangent));
            Vector3 up = Vector3CrossProduct(tangent, right);
            
            // Twist / Wobble
            float wobble = 0.25f * sinf(t * PI * 4.0f + time * 6.0f);
            Vector3 twistedUp = Vector3Add(Vector3Scale(up, cosf(wobble)), Vector3Scale(right, sinf(wobble)));
            Vector3 twistedRight = Vector3Normalize(Vector3CrossProduct(twistedUp, tangent));
            
            // Capsule Tapering
            float baseCapsule = 0.3f + 0.7f * sqrtf(fmaxf(0.0f, sinf(t * PI)));
            float tailTaper = 0.15f + 0.85f * t;
            float r = BASE_RADIUS * s->scale * baseCapsule * tailTaper;

            for (int j = 0; j < RADIAL_SEGS; j++) {
                float phi = (float)j * (2.0f * PI) / RADIAL_SEGS;
                Vector3 normal = Vector3Add(Vector3Scale(twistedRight, cosf(phi)), Vector3Scale(twistedUp, sinf(phi)));
                
                // Ring vertex deformation
                float wave1 = sinf(t * 18.0f + phi * 3.0f + time * 10.0f);
                float wave2 = sinf(t * 9.0f - phi * 5.0f - time * 6.0f);
                float deform = 1.0f + wave1 * 0.12f + wave2 * 0.08f;
                
                normals[i][j] = normal;
                rings[i][j] = Vector3Add(pathPos, Vector3Scale(normal, r * deform));
            }
        }

        // Draw quads using low-level rlgl
        rlPushMatrix();
        rlBegin(RL_QUADS);
        for (int i = 0; i < TUBE_SEGMENTS; i++) {
            float v1 = (float)i / TUBE_SEGMENTS * 3.0f;
            float v2 = (float)(i + 1) / TUBE_SEGMENTS * 3.0f;
            
            for (int j = 0; j < RADIAL_SEGS; j++) {
                int next = (j + 1) % RADIAL_SEGS;
                float u1 = (float)j / RADIAL_SEGS;
                float u2 = (float)(j + 1) / RADIAL_SEGS;
                
                rlNormal3f(normals[i][j].x, normals[i][j].y, normals[i][j].z);
                rlTexCoord2f(u1, v1);
                rlVertex3f(rings[i][j].x, rings[i][j].y, rings[i][j].z);

                rlNormal3f(normals[i][next].x, normals[i][next].y, normals[i][next].z);
                rlTexCoord2f(u2, v1);
                rlVertex3f(rings[i][next].x, rings[i][next].y, rings[i][next].z);

                rlNormal3f(normals[i + 1][next].x, normals[i + 1][next].y, normals[i + 1][next].z);
                rlTexCoord2f(u2, v2);
                rlVertex3f(rings[i + 1][next].x, rings[i + 1][next].y, rings[i + 1][next].z);

                rlNormal3f(normals[i + 1][j].x, normals[i + 1][j].y, normals[i + 1][j].z);
                rlTexCoord2f(u1, v2);
                rlVertex3f(rings[i + 1][j].x, rings[i + 1][j].y, rings[i + 1][j].z);
            }
        }
        rlEnd();

        // Closed End-Caps (Anti-Robotic Rule)
        float rTail = BASE_RADIUS * s->scale * 0.15f;
        float rHead = BASE_RADIUS * s->scale * s->progress;
        Vector3 tangentTail = Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, 0.0f));
        Vector3 tangentHead = Vector3Normalize(GetBezierDerivative(s->p0, s->p1, s->p2, s->p3, s->progress));
        
        Vector3 tailCenter = GetBezierPoint(s->p0, s->p1, s->p2, s->p3, 0.0f);
        Vector3 headCenter = GetBezierPoint(s->p0, s->p1, s->p2, s->p3, s->progress);

        Vector3 tailApex = Vector3Subtract(tailCenter, Vector3Scale(tangentTail, rTail * 0.25f));
        Vector3 headApex = Vector3Add(headCenter, Vector3Scale(tangentHead, rHead * 0.25f));

        rlBegin(RL_TRIANGLES);
        // Tail Cap
        for (int j = 0; j < RADIAL_SEGS; j++) {
            int next = (j + 1) % RADIAL_SEGS;
            rlNormal3f(-tangentTail.x, -tangentTail.y, -tangentTail.z);
            rlTexCoord2f((float)j / RADIAL_SEGS, -0.1f);
            rlVertex3f(tailApex.x, tailApex.y, tailApex.z);

            rlNormal3f(normals[0][next].x, normals[0][next].y, normals[0][next].z);
            rlTexCoord2f((float)(next) / RADIAL_SEGS, 0.0f);
            rlVertex3f(rings[0][next].x, rings[0][next].y, rings[0][next].z);

            rlNormal3f(normals[0][j].x, normals[0][j].y, normals[0][j].z);
            rlTexCoord2f((float)j / RADIAL_SEGS, 0.0f);
            rlVertex3f(rings[0][j].x, rings[0][j].y, rings[0][j].z);
        }
        // Head Cap
        for (int j = 0; j < RADIAL_SEGS; j++) {
            int next = (j + 1) % RADIAL_SEGS;
            rlNormal3f(tangentHead.x, tangentHead.y, tangentHead.z);
            rlTexCoord2f((float)j / RADIAL_SEGS, 3.1f);
            rlVertex3f(headApex.x, headApex.y, headApex.z);

            rlNormal3f(normals[TUBE_SEGMENTS][j].x, normals[TUBE_SEGMENTS][j].y, normals[TUBE_SEGMENTS][j].z);
            rlTexCoord2f((float)j / RADIAL_SEGS, 3.0f);
            rlVertex3f(rings[TUBE_SEGMENTS][j].x, rings[TUBE_SEGMENTS][j].y, rings[TUBE_SEGMENTS][j].z);

            rlNormal3f(normals[TUBE_SEGMENTS][next].x, normals[TUBE_SEGMENTS][next].y, normals[TUBE_SEGMENTS][next].z);
            rlTexCoord2f((float)(next) / RADIAL_SEGS, 3.0f);
            rlVertex3f(rings[TUBE_SEGMENTS][next].x, rings[TUBE_SEGMENTS][next].y, rings[TUBE_SEGMENTS][next].z);
        }
        rlEnd();
        rlPopMatrix();
    }
    EndShaderMode();
    rlEnableDepthMask();
}

void UnloadWaterStreamSkill(void) {
    // Cached assets are managed and freed globally by the Resource Manager
}

bool IsWaterStreamSkillCoiling(void) {
    return false; // Straight projectile sweep, no active pinning
}

int GetWaterStreamSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (s_streams[i].active && count < maxProjectiles) {
            outProjectiles[count].position = s_streams[i].headPos;
            outProjectiles[count].radius = BASE_RADIUS * s_streams[i].scale;
            outProjectiles[count].active = true;
            count++;
        }
    }
    return count;
}

void DeactivateWaterStreamProjectile(int index) {
    if (index >= 0 && index < MAX_STREAMS && s_streams[index].active) {
        TriggerImpactVFX(s_streams[index].headPos, s_streams[index].scale);
        s_streams[index].active = false;
    }
}
```

---

## 9. 3D RENDERING & SHADER BEST PRACTICES (CRITICAL LESSONS)

When rendering custom 3D primitives (like tubes, pillars, or crystals) using `rlgl` and custom shaders, you MUST follow these crucial rules to avoid visual bugs (e.g., flat 2D look, TV static noise, random colored tints):

### 9.1 Vertex Color Initialization (`rlColor4ub`)
When drawing raw geometry via `rlBegin(RL_QUADS)` / `rlVertex3f`, the `fragColor` in the shader inherits the **last active color state** set by Raylib. If a UI element or another skill drew something green just before your skill, your 3D mesh will be tinted green!
* **Rule:** ALWAYS call `rlColor4ub(255, 255, 255, 255);` (White, full alpha) immediately before `rlBegin()` for 3D meshes that use custom shader textures/colors, to reset the vertex color state.

### 9.2 Avoiding "TV Static" in Procedural Noise
When generating procedural textures (like magma veins, ice cracks) in shaders using `noise` or `fbm` (Fractional Brownian Motion):
* **Do NOT use high frequencies on World UVs:** If `worldUV` is calculated from `fragPosition` (e.g., `fragPosition.xz * 4.0`), the noise will sample extremely high frequencies and turn into random noisy dots (TV static).
* **Rule:** Use very small multipliers for world coordinates (e.g., `fragPosition.xz * 0.05`) to create large, organic, continuous patterns. Stretch the noise on specific axes (e.g., multiplying X by 0.1 but Y by 0.01) to create directional fissures (like vertical magma cracks).

### 9.3 Custom Vertex Shader Requirement for 3D Lighting
Raylib's default vertex shader (loaded when passing `NULL` as the vs path) **does NOT pass `fragNormal` or `fragPosition`** to the fragment shader.
* **The Trap:** If you try to calculate 3D lighting (like `NdotL`, `NdotV`, `normalize(fragNormal)`) in your fragment shader while using the default vertex shader, the uninitialized variables will cause a `NaN` (Not a Number) chain reaction. Your beautiful 3D mesh will instantly break into flat, black, broken triangles.
* **Rule:** If your fragment shader needs `fragNormal` or `fragPosition` for 3D lighting, you **MUST** create and load a custom vertex shader (e.g., `my_skill.vs`) that explicitly passes them using `matModel`. Do not pass `NULL` to `ResourceManager_LoadShader()`.

### 9.4 Preserving 3D Volume (Avoiding the "Flat 2D" look)
If a glowing emissive color covers too much of the object (e.g., > 60% coverage) without any shading, the object will lose all its dark/shadow areas and appear as a flat 2D sprite.
* **Rule 1 - Sparse Emissive Masks:** Use `smoothstep` (e.g., `smoothstep(0.6, 0.8, noiseValue)`) to tightly restrict glowing areas (like magma/cracks) to only 20-30% of the surface area. The remaining 70% must be the base shaded material.
* **Rule 2 - Wrap Lighting & Fresnel:** For the non-emissive base material, always calculate `NdotL` (Lambertian diffuse) and `NdotV` (Fresnel). Use Fresnel (`pow(1.0 - NdotV, 3.0)`) to add Rim Lighting to the edges of the 3D volume, emphasizing its curvature.
* **Rule 3 - Emissive Order:** Always add your `emissive` color **AFTER** multiplying the base color by textures/shadows, otherwise the glow will be incorrectly darkened by the shadow/texture.