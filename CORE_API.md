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

### CRITICAL SCALING RULES FOR AI:
This engine is now fully meter-scaled (1 unit = 1 meter). DO NOT use the old 100x scale.
Radii: Base mesh/tube radii should be around 0.10f to 0.20f (10-20 cm). Impact bursts/lights should range from 0.5f to 1.5f.
Velocity/Speed: Particle speeds should be around 1.0f to 3.0f (m/s).
Gravity/Force: Use realistic physical defaults (e.g. 3.0f - 9.8f for gravity). Do not use the old 300-700f ranges.

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
* `Font ResourceManager_LoadFont(const char *filePath, int baseSize);`
  - Loads a TTF/OTF font at `baseSize`, cached by (path, baseSize) — a different `baseSize` for the same file builds a separate atlas, matching raw `LoadFontEx`. Bilinear-filtered so it scales smoothly at other draw sizes via `DrawTextEx`. Falls back to `GetFontDefault()` if `filePath` doesn't exist yet — never fails outright, safe to call before an asset is provided (logs a `LOG_WARNING`). Used by `sandbox/ui_panel.c` for its debug-UI text; `assets/fonts/` is the convention for where a UI font file should live.
### Mandatory Teardown Rule
* **DO NOT** call Raylib's `UnloadTexture` or `UnloadShader` inside your skill's `Unload[Name]Skill` callback. Leave the callback empty or commented; the global Resource Manager automatically unloads and frees all cached resources when the application shuts down.

---

## 3b. Data-Driven Tuning (`#include "core/tuning.h"`)
Lets a skill register a `float` as tunable via a plain-text config file (`tuning.cfg` at the project root) instead of hardcoding it — edit the file while the game is running and see it change with no rebuild/restart.
```c
void Tuning_Init(const char *configPath);                                // main.c calls this once at startup
bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue);
void Tuning_Update(void);                                                // main.c calls this once per frame
void Tuning_Reload(void);                                                // force an immediate reload (e.g. a debug hotkey)
```
* **Where to call from a skill:** register in `Init[Name]Skill` (`Tuning_RegisterFloat("my_skill_radius", &s_radius, 40.0f)`) — `*value` is set to `defaultValue` immediately, then overwritten if the key is already in `tuning.cfg`. `main.c` already calls `Tuning_Init`/`Tuning_Update` globally; a skill only ever calls `Tuning_RegisterFloat`.
* **Config format:** `key = value` per line in `tuning.cfg`, `#` for comments, blank lines OK. Only floats. A key not listed in the file simply keeps whatever value the registering code already set (its default, or whatever it was before) — hot-reload never resets a value back to default just because its line is temporarily missing or malformed.
* **Read live values fresh, don't bake them once.** `Tuning_Update()` (mtime poll, no filesystem-watch dependency) overwrites the registered `float` in place, but has no way to retroactively fix anything a skill already copied that value into (e.g. a `ParticleConfig` baked at `Cast` time). If a value needs to react to a live edit while an effect is on screen, re-read the registered float each frame/draw and re-apply it — see `core_test`'s `EffectMaterial.params.rimStrength/fresnelPower` for the pattern.
* **Desktop dev-tool, not shipped Android hot-reload:** `tuning.cfg` is copied into the Android asset bundle (`Makefile.Android`) so the packaged defaults still apply, but editing a file inside an installed APK isn't meaningful, so there's no live-reload story on Android — only on desktop.

**Per-path save/load (sandbox live-tuning UI, separate from the `tuning.cfg` hot-reload path above):**
```c
bool Tuning_LoadFloatsFromPath(const char *path, const char *const *keys, float *outValues, int count);
bool Tuning_SaveFloats(const char *path, const char *const *keys, const float *values, int count);
```
* One-shot, not registered into the per-frame hot-reload table — for a UI-driven flow (pick a skill in sandbox → edit sliders → click Save) rather than hand-editing a file while the game runs.
* `Tuning_LoadFloatsFromPath`: like `Tuning_RegisterFloat`, a key missing from the file leaves that `outValues[i]` untouched — pre-fill `outValues` with your defaults before calling. Returns `false` if the file doesn't exist (nothing touched).
* `Tuning_SaveFloats`: overwrites `path` fresh with a `key = value` list (same textual format as `tuning.cfg`). Used by `RegisterSkillTunables` consumers (below) to persist to a skill's co-located `.tuning` file, e.g. `skills/fire/fire_ball/fire_ball.tuning`.

---

## 3c. Soft Particles (Depth Blending)
The core engine provides a global linearized depth buffer for effects that need to smoothly fade when intersecting solid geometry (e.g., ground planes, walls, props) instead of clipping harshly.

### How to use Soft Particles in a Skill:
1. **Include the GLSL Header:** In your fragment shader (`.fs`), include `soft_particle.glsl`.
2. **Access the Depth Uniform:** The engine provides a global `sampler2D u_cameraDepthTex`. You MUST manually define this uniform in your shader if you want to use it.
3. **Calculate the Fade Factor:** Inside your `main()` fragment function, call `SoftParticle_Factor(u_cameraDepthTex, fragTexCoord, gl_FragCoord.z, fadeDistance)`. 
   - `fadeDistance`: The distance in world units over which the object will fade out as it approaches an intersection.
   - The returned factor is `0.0` (fully occluded/transparent) to `1.0` (unoccluded/opaque). Multiply your final output alpha by this factor.
4. **Bind the Depth Texture in C:** In your `Draw[Name]Skill()` function, you must explicitly bind the depth texture **BEFORE** drawing your mesh, and unbind it after:
```c
   // Note: Bind to a texture unit that is NOT used by your other textures (e.g., unit 3)
   ScreenDistort_BindDepthForSoftParticles(myShader, 3);
   
   // ... [Draw your mesh or particles here] ...
   
   ScreenDistort_UnbindSoftParticleDepth(3);
```
5. **State Management Warning:** If you disable depth writing/testing (e.g. `rlDisableDepthMask()`) for your soft particle pass, **you MUST flush the batch first** by calling `rlDrawRenderBatchActive();` right before the state change. Failure to do so will retroactively disable depth-writing for previously queued geometry (like the ground plane)!

---

## 3d. Camera Shake & Screen Distort
* **Camera Shake (`CameraFX_Shake`):** Mặc định là KHÔNG RUNG (defaults to off/0.0f). Không bao giờ tự ý thêm hiệu ứng rung camera vào các chiêu thức mặc định nếu không có sự đồng ý của người dùng. Nếu chiêu thức có hiệu ứng rung, luôn phải biến nó thành một tham số có thể điều chỉnh (tunable) với giá trị mặc định là 0.0f để người dùng có thể tùy ý bật lên trong Sandbox nếu muốn.
* **Screen Distort (`ScreenDistort_Add`):** Hạn chế lạm dụng gây rối mắt, chỉ dùng cho các chiêu hệ Thủy (Hydro Cleave) hoặc khi được yêu cầu.

---

## 4. SKILL LIFECYCLE & INTEGRATION (`[skill_name]_skill.h`)

For automatic detection, your header file must declare these exact prototypes (replace `[Name]` with your unique CamelCase skill name):

### SkillParams
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
```

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
void Cast[Name]Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);

// Engine ↔ Skill communication
bool Is[Name]SkillCoiling(void);
int Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);

#endif // SKILL_[NAME]_H
```

`agentId` is the caster's agent-pool slot (0..255), forwarded automatically by `CastSkill()` (CORE_ISSUES.md Item 15). Store it in your per-instance struct as `int ownerAgentId;` at cast time — it's what lets `AbortSkill(skillIndex, agentId)` target only the caster's own instances. Ownership tracking is its only job right now; see `skills/CLAUDE.md` for the full rule.

### Minimal Complete `.c` Skeleton (Generic Projectile Skill)

A minimal, generic skill `.c` showing the full state machine (`CASTING → FLYING → IMPACT → DISSOLVE`), a static fixed-size array of instances (no malloc), and correct lifecycle wiring. Copy and adapt — not tied to any element.

```c
#include "skill_example.h"
#include "core/particle_system.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "raymath.h"
#include <math.h>

#define MAX_INSTANCES 16

typedef enum {
    STATE_CASTING,
    STATE_FLYING,
    STATE_IMPACT,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;
    Vector3 target;
    Vector3 velocity;
    float stateTimer;
    float radius;
    int ownerAgentId;   // caster's agent slot, set at cast time (ownership tracking)
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

void InitExampleSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExampleSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        s_instances[i] = (SkillInstance){
            .state = STATE_CASTING,
            .position = startPos,
            .target = target,
            .velocity = (Vector3){0},
            .stateTimer = 0.0f,
            .radius = 8.0f * params.sizeScale,
            .ownerAgentId = agentId,
            .active = true
        };
        SpawnCastEffect(startPos, EFFECT_PRESET_FIRE_EXPLOSION, params.sizeScale * 0.6f);
        return;
    }
}

void UpdateExampleSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= 0.35f) {
                    s->state = STATE_FLYING;
                    s->stateTimer = 0.0f;
                    s->velocity = Vector3Scale(Vector3Normalize(Vector3Subtract(s->target, s->position)), 220.0f);
                }
                break;

            case STATE_FLYING:
                s->position = Vector3Add(s->position, Vector3Scale(s->velocity, dt));
                if (Vector3Distance(s->position, s->target) < s->radius + enemyRadius) {
                    s->state = STATE_IMPACT;
                    s->stateTimer = 0.0f;
                    SpawnImpactEffect(s->position, EFFECT_PRESET_FIRE_EXPLOSION, 1.0f);
                    ApplyAoEDamage(s->position, 30.0f, 25.0f, 0.0f);
                }
                break;

            case STATE_IMPACT:
                if (s->stateTimer >= 0.2f) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE:
                if (s->stateTimer >= 0.5f) {
                    s->active = false;
                }
                break;
        }
    }
}

void DrawExampleSkill(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        if (s->state != STATE_FLYING) continue; // only draw the flying projectile body
        DrawEffectMesh(MESH_PRESET_SPHERE, s->position, (Vector3){ s->radius, s->radius, s->radius }, ELEMENT_COLOR_FIRE);
    }
}

void UnloadExampleSkill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}

bool IsExampleSkillCoiling(void) {
    return false; // override if this skill has a charge-up/coiling phase
}

int GetExampleSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_INSTANCES && count < maxProjectiles; i++) {
        if (!s_instances[i].active || s_instances[i].state != STATE_FLYING) continue;
        outProjectiles[count].position = s_instances[i].position;
        outProjectiles[count].radius = s_instances[i].radius;
        outProjectiles[count].active = true;
        count++;
    }
    return count;
}

void DeactivateExampleProjectile(int index) {
    if (index < 0 || index >= MAX_INSTANCES) return;
    s_instances[index].active = false;
}
```

### Minimal Complete `.c` Skeleton (Non-Projectile / Ground-Rising Skill)

For skills with no flight stage — the effect happens at a fixed spawn point instead of traveling there (e.g. a stone spike rising from the ground). State machine: `CASTING → RISING (mesh height animates 0→full via SmoothStep01) → ACTIVE → DISSOLVE`. Static instance array keyed by spawn position, not velocity/target — nothing flies, so no `ProceduralMesh_BuildTube`/path-spline is needed; draw a simple procedural primitive (`DrawCoreCylinder`/`DrawCorePrism`) whose height is animated directly.

The mesh is drawn as several short, jittered `DrawCoreCylinder` segments instead of one straight tapered shape, per §12.2 (Perpendicular Jitter) and §12.3 (Instance Randomization) below — a single undecorated frustum reads as robotic even though `DrawCoreCylinder` itself is an approved procedural mesh function.

```c
#include "skill_example_rising.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "raymath.h"

#define MAX_INSTANCES 16

typedef enum {
    STATE_CASTING,
    STATE_RISING,
    STATE_ACTIVE,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position; // fixed spawn point — nothing flies, no velocity/target
    float stateTimer;
    float radius;
    float maxHeight;
    float currentHeight; // animated 0 -> maxHeight during RISING
    float yawOffset;     // per-instance random yaw (§12.3), applied to the jitter axis
    float scaleVariance; // per-instance 85-115% scale (§12.3)
    int ownerAgentId;    // caster's agent slot, set at cast time (ownership tracking)
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#define RISING_DURATION 0.4f
#define ACTIVE_DURATION 2.0f
#define DISSOLVE_DURATION 0.5f
#define MESH_SEGMENTS 5 // stacked segments forming the rising shape, see §12.2

void InitExampleRisingSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExampleRisingSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        s_instances[i] = (SkillInstance){
            .state = STATE_CASTING,
            .position = target, // ground-rising mesh spawns at the target, not the caster
            .stateTimer = 0.0f,
            .radius = 14.0f * params.sizeScale,
            .maxHeight = 80.0f * params.sizeScale,
            .currentHeight = 0.0f,
            .yawOffset = (float)GetRandomValue(0, 360), // §12.3 instance randomization
            .scaleVariance = (float)GetRandomValue(85, 115) / 100.0f,
            .ownerAgentId = agentId,
            .active = true
        };
        SpawnCastEffect(startPos, EFFECT_PRESET_EARTH_CRACK, params.sizeScale * 0.6f); // windup plays at the caster
        return;
    }
}

void UpdateExampleRisingSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= 0.3f) {
                    s->state = STATE_RISING;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_RISING: {
                float t = SmoothStep01(s->stateTimer / RISING_DURATION);
                s->currentHeight = Math_Mix(0.0f, s->maxHeight, t);
                if (s->stateTimer >= RISING_DURATION) {
                    s->state = STATE_ACTIVE;
                    s->stateTimer = 0.0f;
                    s->currentHeight = s->maxHeight;
                    SpawnImpactEffect(s->position, EFFECT_PRESET_EARTH_CRACK, 1.0f);
                    ApplyAoEDamage(s->position, s->radius * 1.5f, 25.0f, 0.0f);
                }
                break;
            }

            case STATE_ACTIVE:
                if (s->stateTimer >= ACTIVE_DURATION) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE: {
                float t = SmoothStep01(s->stateTimer / DISSOLVE_DURATION);
                s->currentHeight = Math_Mix(s->maxHeight, 0.0f, t);
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
            }
        }
    }
}

void DrawExampleRisingSkill(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active || s->currentHeight <= 0.01f) continue;

        // §12.2/§12.3: build the rising shape out of several short jittered
        // segments + per-instance scale, instead of one straight tapered cylinder.
        float dirYaw = s->yawOffset * DEG2RAD;
        Vector3 dir = { cosf(dirYaw), 0.0f, sinf(dirYaw) };       // per-instance random jitter axis
        Vector3 perp = { -dir.z, 0.0f, dir.x };
        float segHeight = (s->currentHeight / MESH_SEGMENTS) * s->scaleVariance;
        float baseRadius = s->radius * s->scaleVariance;

        Vector3 segBottom = s->position;
        for (int seg = 0; seg < MESH_SEGMENTS; seg++) {
            float segT = (float)seg / (float)(MESH_SEGMENTS - 1); // 0 at base, 1 at tip
            // Smaller-scale jitter than the §12.2 reference (tuned for a ~80f tall mesh,
            // not a long path layout): +-3.0f instead of +-12.0f.
            float jitter = (float)GetRandomValue(-30, 30) / 10.0f;
            Vector3 jitteredBottom = Vector3Add(segBottom, Vector3Scale(perp, jitter));

            Vector3 segTop = { jitteredBottom.x, segBottom.y + segHeight, jitteredBottom.z };
            float jitterTop = (float)GetRandomValue(-30, 30) / 10.0f;
            segTop = Vector3Add(segTop, Vector3Scale(perp, jitterTop - jitter));

            float radiusBottom = Math_Mix(baseRadius, baseRadius * 0.6f, segT);
            float radiusTop = Math_Mix(baseRadius, baseRadius * 0.6f, segT + 1.0f / MESH_SEGMENTS);
            DrawCoreCylinder(jitteredBottom, segTop, radiusBottom, radiusTop, 12, ELEMENT_COLOR_EARTH);

            segBottom = segTop;
        }
    }
}

void UnloadExampleRisingSkill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}
```

### Minimal Complete `.c` Skeleton (Anchored-Along-Path Skill)

For skills that spawn **multiple anchored instances along a drawn path** (`SkillParams.pathPoints[]`), rather than one instance at a single point. This is a technical/structural pattern, independent of range tier (Tầm xa/Trung/Cận) — range is just a max-distance check at cast time and has no bearing on which skeleton a skill uses; do not name or theme this skeleton around "melee" or any specific range. State machine per instance: `CASTING → WAITING (staggered) → RISING → ACTIVE/HOLDING → DISSOLVE`. Use `SamplePath()` from `core/path_spline.h` to resample the cast path at even spacing instead of hand-rolling cumulative-distance math.

```c
#include "skill_example_pathanchor.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/path_spline.h"
#include "core/procedural_mesh_utils.h"
#include "core/utils_math.h"
#include "raymath.h"

#define MAX_INSTANCES 16
#define INSTANCE_SPACING 35.0f   // world units between sampled points
#define STAGGER_DURATION 0.5f    // total time for the last instance to start rising

typedef enum {
    STATE_CASTING,
    STATE_WAITING,   // staggered delay before this instance starts rising
    STATE_RISING,
    STATE_ACTIVE,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 position;   // sampled along SkillParams.pathPoints[] via SamplePath()
    float stateTimer;
    float waitTime;     // per-instance stagger, set at cast time
    float radius;
    float maxHeight;
    float currentHeight; // animated 0 -> maxHeight during RISING
    int ownerAgentId;    // caster's agent slot, set at cast time (ownership tracking)
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#define RISING_DURATION 0.25f
#define ACTIVE_DURATION 1.5f
#define DISSOLVE_DURATION 0.4f

void InitExamplePathAnchorSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExamplePathAnchorSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    // Resample the drawn path at even spacing — never hand-roll cumulative-distance
    // sampling in skill code (see core/path_spline.h). Falls back to a straight
    // startPos->target line if the caller didn't draw a multi-point path.
    Vector3 rawPath[33];
    int rawCount = 0;
    if (params.pathPointCount > 1) {
        for (int i = 0; i < params.pathPointCount && i < 32; i++) rawPath[rawCount++] = params.pathPoints[i];
    } else {
        rawPath[rawCount++] = startPos;
        rawPath[rawCount++] = target;
    }

    Vector3 sampled[MAX_INSTANCES];
    int sampledCount = SamplePath(rawPath, rawCount, INSTANCE_SPACING, sampled, MAX_INSTANCES);
    if (sampledCount <= 0) return;

    for (int p = 0; p < sampledCount; p++) {
        int slot = -1;
        for (int i = 0; i < MAX_INSTANCES; i++) {
            if (!s_instances[i].active) { slot = i; break; }
        }
        if (slot < 0) break;

        s_instances[slot] = (SkillInstance){
            .state = STATE_CASTING,
            .position = sampled[p],
            .stateTimer = 0.0f,
            // Stagger so instances rise in sequence along the line instead of all at once.
            .waitTime = (float)p / (float)sampledCount * STAGGER_DURATION,
            .radius = 12.0f * params.sizeScale,
            .maxHeight = 70.0f * params.sizeScale,
            .currentHeight = 0.0f,
            .ownerAgentId = agentId,
            .active = true
        };
    }
    SpawnCastEffect(startPos, EFFECT_PRESET_WOOD_BLOOM, params.sizeScale * 0.6f);
}

void UpdateExamplePathAnchorSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= 0.05f) {
                    s->state = STATE_WAITING;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_WAITING:
                if (s->stateTimer >= s->waitTime) {
                    s->state = STATE_RISING;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_RISING: {
                float t = SmoothStep01(s->stateTimer / RISING_DURATION);
                s->currentHeight = Math_Mix(0.0f, s->maxHeight, t);
                if (s->stateTimer >= RISING_DURATION) {
                    s->state = STATE_ACTIVE;
                    s->stateTimer = 0.0f;
                    s->currentHeight = s->maxHeight;
                    ApplyAoEDamage(s->position, s->radius * 1.3f, 18.0f, 0.0f);
                }
                break;
            }

            case STATE_ACTIVE:
                if (s->stateTimer >= ACTIVE_DURATION) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_DISSOLVE: {
                float t = SmoothStep01(s->stateTimer / DISSOLVE_DURATION);
                s->currentHeight = Math_Mix(s->maxHeight, 0.0f, t);
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
            }
        }
    }
}

void DrawExamplePathAnchorSkill(void) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active || s->currentHeight <= 0.01f) continue;
        Vector3 top = { s->position.x, s->position.y + s->currentHeight, s->position.z };
        DrawCoreCylinder(s->position, top, s->radius, s->radius * 0.5f, 10, ELEMENT_COLOR_WOOD);
    }
}

void UnloadExamplePathAnchorSkill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}
```
> [!NOTE]
> `skills/wood/wood_thorns/wood_thorns_skill.c` implements this same pattern today with a hand-rolled cumulative-distance loop (predates `SamplePath`). It could be refactored to call `SamplePath()` directly — out of scope here, left for the Skills Agent.

---

### Minimal Complete `.c` Skeleton (Entity-Attached Skill)

For skills whose visual must follow a *moving* `Agent` rather than stay at a fixed world position or path — primarily **Buff** skills (and future khinh công dash afterimages). State machine: `STATE_CASTING → STATE_ATTACHED (visual follows the tracked position every frame, buff active on the target) → STATE_DISSOLVE`.

`Cast[Name]Skill` DOES receive the caster's `int agentId` (CORE_ISSUES.md Item 15) — store it as `ownerAgentId` like every skeleton. What the lifecycle still does NOT provide: `Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius)` carries no agentId, and skills can't read the live `Agent` array (`entities/entities.h` internals are off-limits to Skills), so there is no way yet to track a *moving* agent's position frame-to-frame — don't invent a new Update parameter to get one (CORE_ISSUES.md Item 26 is the planned fix: an agent-position provider callback). Until then the skill caches the owner's `Vector3` position at cast time (`startPos`) and re-applies the buff via the radius-based `Entity_ApplyAoEBuff(casterPos, smallRadius, speedMult, duration)` entry point; this skeleton re-centers on `casterPos` once at cast time, consistent with what `Entity_ApplyAoEBuff` itself can target without live agent tracking.

Two options for the attached visual: (1) re-spawn a small particle burst and/or `VFXLight_Spawn` at the tracked position once per frame while `ATTACHED` — simplest, no persistent trail object to manage; (2) drive a `core/trail_system.h` `TRAIL_TYPE_FOLLOWER` trail with `SetFollowerAxis`/`UpdateFollowerPosition` every frame — better when the buff needs ribbon/aura geometry, not just a glow. The skeleton below uses option (1); reach for `TRAIL_TYPE_FOLLOWER` only if the visual genuinely needs ribbon geometry.

`DISSOLVE` fires when the skill's own local timer reaches the `duration` it requested from `Entity_ApplyAoEBuff` — the skill does not query `entities/` for the live modifier state (Core/Skills never read `entities/` internals); it just times its visual to roughly match the buff duration it asked for.

```c
#include "skill_example_attached.h"
#include "core/skill_helper.h"
#include "core/vfx_light.h"
#include "core/particle_system.h"
#include "core/utils_math.h"
#include "entities/entities.h"   // for Entity_ApplyAoEBuff — see note below
#include "raymath.h"

#define MAX_INSTANCES 4

typedef enum {
    STATE_CASTING,
    STATE_ATTACHED,
    STATE_DISSOLVE
} SkillState;

typedef struct {
    SkillState state;
    Vector3 casterPos;   // cached owner position, re-used as the visual anchor each frame
    float stateTimer;
    float buffDuration;  // mirrors the duration passed to Entity_ApplyAoEBuff
    float buffRadius;
    int ownerAgentId;    // caster's agent slot, set at cast time (ownership tracking)
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#define CASTING_DURATION 0.15f
#define DISSOLVE_DURATION 0.3f
#define VISUAL_TICK_INTERVAL 0.1f  // re-spawn particle/light every N seconds, not every frame

void InitExampleAttachedSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExampleAttachedSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    int slot = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!s_instances[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    float duration = 5.0f * params.sizeScale;
    float radius = 40.0f * params.sizeScale;

    s_instances[slot] = (SkillInstance){
        .state = STATE_CASTING,
        .casterPos = startPos,
        .stateTimer = 0.0f,
        .buffDuration = duration,
        .buffRadius = radius,
        .ownerAgentId = agentId,
        .active = true
    };

    // Radius-based — no agentId required. NOTE: buffs every agent currently inside
    // buffRadius of casterPos, ally or enemy (Entity_ApplyAoEBuff has no team
    // filtering yet, see ENTITIES_API.md §9).
    Entity_ApplyAoEBuff(startPos, radius, 1.5f /* speedMult */, duration);

    SpawnCastEffect(startPos, EFFECT_PRESET_WOOD_BLOOM, params.sizeScale * 0.5f);
}

void UpdateExampleAttachedSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        SkillInstance *s = &s_instances[i];
        if (!s->active) continue;
        s->stateTimer += dt;

        switch (s->state) {
            case STATE_CASTING:
                if (s->stateTimer >= CASTING_DURATION) {
                    s->state = STATE_ATTACHED;
                    s->stateTimer = 0.0f;
                }
                break;

            case STATE_ATTACHED: {
                // Re-spawn a light/particle pulse at the tracked position on an
                // interval, rather than every single frame, to avoid flooding the
                // VFXLight/particle pools. The position itself is only as fresh as
                // casterPos (cached at cast time) — see note above re: agentId.
                static float tickAccum = 0.0f; // illustrative; real code keys this per-instance
                if (fmodf(s->stateTimer, VISUAL_TICK_INTERVAL) < dt) {
                    VFXLight_Spawn(s->casterPos, ELEMENT_COLOR_WOOD, 60.0f, VISUAL_TICK_INTERVAL * 1.5f);
                }
                if (s->stateTimer >= s->buffDuration) {
                    s->state = STATE_DISSOLVE;
                    s->stateTimer = 0.0f;
                }
                break;
            }

            case STATE_DISSOLVE:
                if (s->stateTimer >= DISSOLVE_DURATION) {
                    s->active = false;
                }
                break;
        }
    }
}

void DrawExampleAttachedSkill(void) {
    // VFXLight/particle draws are handled by their owning systems each frame
    // (VFXLight_GetActive, etc.) — nothing to draw here directly in this skeleton.
}

void UnloadExampleAttachedSkill(void) {
    // No-op: textures/shaders are owned by ResourceManager, never unload here.
}
```
> [!NOTE]
> Requires `#include "entities/entities.h"` to call `Entity_ApplyAoEBuff`. `skills/CLAUDE.md`'s allowed read list now includes `entities/entities.h` (for `Entity_ApplyAoEDamage`/`Entity_ApplyAoEBuff`/`Entity_GetNearbyTargets`) — this is resolved, no further confirmation needed.
>
> **Any skill calling `Entity_ApplyAoEDamage` or `Entity_ApplyAoEBuff`** (not just this skeleton) must `#include "entities/entities.h"` — this is easy to miss since these functions live in `entities/`, not `core/`, unlike everything else a skill normally includes. A missing include here fails with `implicit declaration of function` at compile time, not a clear error pointing at the missing header.

### Agent Position & Nearby Targets Providers (`core/skill_manager.h`)

Inversion-of-control callbacks — core queries agent positions without depending on `entities/`. Both registered automatically by `Entity_Init`; skills call them indirectly via `StatusVFX_Update` or `SkillHelper_ChainTargets`, never directly.

```c
typedef bool (*AgentPosProviderFn)(int agentId, Vector3 *outPos);
void SkillManager_SetAgentPosProvider(AgentPosProviderFn fn);
bool SkillManager_GetAgentPos(int agentId, Vector3 *outPos);
// Returns false if agentId is invalid or no provider is registered.

typedef int (*NearbyTargetsProviderFn)(Vector3 center, float radius,
                                       int *outIds, int maxIds);
void SkillManager_SetNearbyTargetsProvider(NearbyTargetsProviderFn fn);
int  SkillManager_GetNearbyTargets(Vector3 center, float radius,
                                   int *outIds, int maxIds);
// Returns count of agents found (0 if no provider registered).
```

Full API documentation in §16 below.

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
| `FORCE_VECTOR_TEXTURE` | Box center (xz) | Box half-extent (xz) | Multiplier on sampled vector | Unused (must be 0) | Unused (must be 0) | (int) texture slot 0/1 | Unused (must be 0) |

* **Dynamic Axis (RADIAL_AXIS / VORTEX_AXIS):** These forces ignore the `origin` and `direction` in the `ForceLayer` struct. Instead, they dynamically use the `axisOrigin` and `axisDir` passed each frame during evaluation (e.g. via `SetFollowerAxis()` for trails).

* **Falloff Semantic:** `0.0` = constant force throughout, `1.0` = linear decrease to zero at radius boundary, `2.0` = quadratic decrease (natural gravitational/magnetic falloff).
* **Viscosity Damping:** Use `ForceField_GetViscosityDamping(&s_forceField, dt)` inside manual update loops to damp velocity: `myVel = Vector3Scale(myVel, dampFactor);`.
* **`FORCE_VECTOR_TEXTURE` (GPU-only):** Samples a world-space flow texture instead of a procedural formula — for geometry-authored vector fields (smoke hugging a wall, fire wrapping a body) that noise/vortex layers can't express. `origin.xz`/`direction.xz` define a world-space sample box (`direction.xz` = half-extent, `origin.y`/`direction.y` ignored). Texture RG channels = XZ flow direction remapped `[-1,1] -> [0,1]`. Particles outside the box get zero acceleration (hard cutoff, no edge-clamp). **CPU path (`ForceField_Evaluate`, `particle_system.c`, `trail_system.c`) treats this as a no-op** — only `GpuParticleSystem` (COMPUTE path) actually samples the texture, via `GpuParticleSystem_SetVectorFieldTexture()` (see COMPUTE_API.md §3). Not yet verified on real GPU hardware (macOS caps at GL 4.1 and never exercises the COMPUTE path) — treat as unverified until confirmed on an Android/GL4.3+ device.

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
    const SkillCurve *radiusCurve;       // Optional: multiplies `radius` when drawn
    const SkillCurve *speedCurve;        // Optional: multiplies velocity's contribution to position each Update frame
    const SkillCurve *alphaCurve;        // Optional: multiplies colorStart.a, overriding the colorStart/colorEnd/gradient alpha
    const ParticleConfig *onDeathEmit;   // [Sub-Emitter] Spawned on death
    int onDeathEmitCount;                // Quantity to spawn on death
    const ParticleConfig *onLiveEmit;    // [Trail-Emitter] Spawned along path
    float onLiveEmitRate;                // Spawn rate (particles per second)
} ParticleConfig;
```
* **Color Priority:** If `gradient` is not `NULL`, `colorStart` and `colorEnd` are ignored. Always prefer `ColorGradient` for multi-stage color shifts (e.g. fire core white -> orange -> dark ash).
* **Sub-Emitter Lifecycle:** Sub-emitters (`onDeathEmit` and `onLiveEmit`) inherit the parent position but **do not** inherit velocity. Configs passed to sub-emitters **MUST** be declared static (persistent scope).
* **Over-lifetime curves (`radiusCurve`/`speedCurve`/`alphaCurve`, `core/skill_curve.h`):** all three are `NULL` by default (today's exact legacy behavior — fixed radius, physics-only velocity, colorStart/colorEnd/gradient's own alpha). When set, each is sampled fresh every frame at `t01 = 1.0 - lifeRatio` (0 at spawn, 1 at death — same "age fraction" convention `gradient` already uses) via `SkillCurve_Eval`, and **multiplies** the corresponding base value: `radiusCurve` scales the drawn radius, `speedCurve` scales only this frame's position step from `velocity` (the stored velocity itself is untouched, so it composes cleanly with `forceField`/`WindZone` physics instead of compounding), `alphaCurve` scales `colorStart.a` and overrides whatever alpha `colorStart`/`colorEnd`/`gradient` would have produced (RGB is unaffected). This is the mechanism for a skill's per-phase "particle size/speed/opacity over its own short lifetime" tunables — see `fire_skill.c`/`thunder_orb_skill.c` for the pattern: one `static SkillCurve` per phase per property, seeded flat at `1.0` via `SkillCurve_SetConstant` (a no-op multiplier), registered as a curve-kind `SkillTunableEntry`, and pointed to by every `ParticleConfig` spawned in that phase.

---

## 7. TRAIL & RIBBON SYSTEM (`#include "core/trail_system.h"`)
TrailConfig should be initialized with {0}.
`int SpawnTrailEntity(TrailConfig config);` spawns ribbon-based trail components.

> **Pool budget:** `MAX_TRAIL_PARTICLES = 500` is a single static pool shared across **all active trails project-wide**, not per-skill. Several concurrent heavy-trail skills (e.g. multiple wisp/projectile-heavy casts at once) can exhaust it. (CORE_ISSUES.md Item 12) When full, `SpawnTrailEntity` now scans for the lowest-`priority` active trail (ties broken by shortest remaining lifetime) and evicts it instead of rejecting outright — it only returns `-1` if every active trail already has strictly higher priority than the incoming `config.priority`. Same eviction pattern as `core/vfx_light.h`'s `VFXLight_Spawn`.

### Configurations
```c
typedef enum {
    TRAIL_TYPE_PROJECTILE,  // Automatically flies towards target using vel + forceField
    TRAIL_TYPE_WISP,        // Drifts randomly in wind/noise fields
    TRAIL_TYPE_PORTAL,      // Static position, rotates in place (summoning circles)
    TRAIL_TYPE_FOLLOWER     // Manually driven. Follows coordinates bound by skill code
} TrailType;

typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);

typedef struct {
    TrailType type;
    Vector3 pos, vel, target;
    float len;              // Segment/blade length (perpendicular to movement direction)
    float thick;            // Thickness of ribbon at head
    float trailLength;      // Absolute trail decay length (world units)
    float life;             // Total duration in seconds. MUST be > 0 — life<=0 dies on the very next UpdateTrailSystem tick, it does NOT persist until KillTrail
    float initialAngle;     // Starting rotation (degrees), mainly for TRAIL_TYPE_PORTAL spin start
    float wobblePhase;      // Starting phase offset for TRAIL_TYPE_PROJECTILE's sine wobble
    float scale;
    Texture2D tex;
    Color tint;
    Shader shader;
    TrailUpdateCallback onUpdate;   // Called every UpdateTrailSystem() frame for this trail, while active
    TrailDeathCallback onDeath;     // Called once when the trail dies (KillTrail or auto hit-detect)
    int ownerTag;            // Caller-defined ID (e.g. caster entity ID); not read by trail_system itself
    float wobbleAmplitudeOverride; // >0: overrides TRAIL_PROJECTILE_WOBBLE_AMPLITUDE for this trail. <=0 (default): use global
    float curveRangeOverride;      // >0: overrides TRAIL_PROJECTILE_CURVE_RANGE for this trail. <=0 (default): use global
    const ForceField *forceField;
    const ColorGradient *gradient;
    const SpriteAnim *spriteAnim;
    VFXPriority priority;   // core/vfx_light.h enum. Default 0 (VFX_PRIORITY_LOW) from {0} init.
} TrailConfig;
```
* **`priority` (CORE_ISSUES.md Item 12):** additive field — `TrailConfig cfg = {0};` still compiles and defaults to `VFX_PRIORITY_LOW`, so this is backward compatible (unlike `VFXLight_Spawn`'s signature change above). Set `cfg.priority = VFX_PRIORITY_HIGH_ULTIMATE;` for a cast that must not silently lose its trail to pool pressure.
* **Follower Trails:** For sword swings or aura attachments, set type to `TRAIL_TYPE_FOLLOWER`. Two ways to drive the tip:
  - **Manual (per-frame):** call `UpdateFollowerPosition(trailId, tipPos);` each frame before `UpdateTrailSystem`.
  - **Matrix attachment:** call `Trail_AttachToTransform(trailId, &myMatrix, localOffset);` once — `UpdateTrailSystem` reads `*myMatrix` automatically each frame and computes `tip = Vector3Transform(localOffset, *myMatrix)`. Pass `localOffset={0,0,0}` to track the matrix origin. The `Matrix` must stay valid for the trail's lifetime (typically a `static Matrix` field on the owning skill). Detach with `Trail_AttachToTransform(id, NULL, (Vector3){0})`.
  - **Dynamic Orbit:** call `Trail_SetFollowerOrbit(trailId, radius, speed, axis, phase);` to make a matrix-attached trail automatically orbit its `localOffset` point! Orbit rotates around `axis` (must be normalized) at distance `radius`, advancing `speed * dt` radians per frame. Starts at angle `phase`. Set `radius` or `speed` to `0.0f` to disable.
  - `SetFollowerAxis(trailId, basePos, normalizedDir);` sets the optional radial-axis orientation for `FORCE_RADIAL_AXIS` in `forceField` — unrelated to tip position.
  - **`trailLength` for FOLLOWER = integer node count** (e.g. `20.0f` = 20 history nodes). Not a fractional ratio — `(int)trailLength` is taken directly. Trail only renders when `historyCount > 1`, so values < 2.0f result in no visible trail.
* **Lifecycle:** Free active trails when complete by calling `KillTrail(trailId);`.
* **`onDeath`:** Fired once when a trail dies — either its `life` timer expires, or (for `TRAIL_TYPE_PROJECTILE`) it auto-detects a hit on `target`. Use it to spawn an impact effect exactly at the trail's last position without separately tracking when the projectile arrived:
  ```c
  static void OnQiBladeDeath(Vector3 pos, float scale) {
      SpawnImpactEffect(pos, PRESET_METAL, scale);
  }
  // ...
  TrailConfig cfg = {0};
  cfg.onDeath = OnQiBladeDeath;
  ```
* **`onUpdate`:** Fired every frame the trail is active, after physics update — use for custom per-trail logic (e.g. periodic sub-emits) that doesn't fit the built-in `forceField`/`gradient` model. Receives the trail's own ID, so call `GetTrail(trailId)` inside to read live position/velocity.
* **`ownerTag`:** Caller-defined integer, stored but never interpreted by `trail_system.c` itself — use it to tag which caster/skill instance owns a trail (e.g. for multi-caster scenarios where you need to distinguish "my projectile" from another player's via `GetTrail(id)->ownerTag`).
* **Per-instance `TRAIL_TYPE_PROJECTILE` overrides:** `wobbleAmplitudeOverride` and `curveRangeOverride` let a single trail opt out of the global homing/wobble macros without affecting other skills. Example — a dead-straight "sword qi" trail:
  ```c
  TrailConfig cfg = {0};
  cfg.type = TRAIL_TYPE_PROJECTILE;
  cfg.wobbleAmplitudeOverride = 0.001f; // near-zero wobble
  cfg.curveRangeOverride = 1.0f;        // snaps to target direction almost immediately
  ```
  Leave both at `0` (default from `{0}` zero-init) to keep the existing global-macro behavior — fully backward compatible.

---

## 8. Graphics & VFX API

### Ground Decals (`core/decal_system.h`)
```c
void DecalSystem_Init(void);
void DecalSystem_Add(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);
void DecalSystem_AddEx(Vector3 pos, float rot, float rotSpeed, float scaleStart, float scaleEnd, Texture2D tex, float life, Color tint, BlendMode blendMode, float yOffset);
void DecalSystem_AddFlowEx(Vector3 pos, float rot, float rotSpeed, float scaleStart, float scaleEnd, Texture2D tex, float life, Color tint, BlendMode blendMode, float yOffset, float flowSpeed, float flowStrength);
void DecalSystem_AddStreak(const Vector3 *points, int count, float rot, float scale, Texture2D tex, float life, Color tint);
void DecalSystem_Update(float dt);
void DecalSystem_Draw(void);
void DecalSystem_Unload(void);
```
* `rot`: yaw around Y axis (degrees). Alpha fades internally as `lifetime / maxLifetime` decays to 0.
* Static pool, `MAX_DECALS = 64`, no malloc.
* `DecalSystem_AddStreak`: thin wrapper that calls `DecalSystem_Add` once per point in `points[0..count-1]` — for path-shaped effects (thorn lines, scorch trails) instead of hand-rolling a loop. Caller's responsibility to pass a reasonable `count` (e.g. up to 32, matching `SkillParams.pathPoints[32]`); not auto-clamped against `MAX_DECALS` headroom, same convention as `SamplePath`'s `maxSegments` in `core/path_spline.h`.
* **`DecalSystem_AddFlowEx`** (CORE_ISSUES.md Item 4b): same params as `AddEx` plus `flowSpeed`/`flowStrength`. Texture radially scrolls outward from the decal center over time (`core/shaders/decal_flow.fs`) instead of staying static — for lava-crack-crawl / ripple-spreading visuals. `flowSpeed` ~0.3–1.0 (radial units/sec), `flowStrength` ~0.5–1.0 (0 = looks identical to a static decal, 1 = fully replaced by the scrolled sample). Draws via a separate shader pass from static decals — does not affect `Add`/`AddEx` behavior or performance. Already wired into `SpawnGroundDecal` for `DECAL_PRESET_FIRE_LAVA`/`DECAL_PRESET_WATER_RIPPLE` (see Ground Decal Preset section); every other preset is unaffected (static).

Rules:
- Call `DecalSystem_Init()` once at startup, `DecalSystem_Update(dt)` every frame to age out decals.
- Prevents Z-fighting automatically (internal Y offset, do not add your own).
- Draw before 3D meshes, using `BLEND_ALPHA`.
- Recommended scale: 4–5.5× structure radius.
- Do not call `DecalSystem_Unload()` from skill code — global system, owned by the engine shutdown sequence only.

### Screen Distortion (`core/screen_distort.h`)
Static pool of radial shockwave/heatwave distortions. `MAX_DISTORTION_SOURCES = 16`.

**Lifecycle (global — skill code chỉ gọi Add):**
```c
void ScreenDistort_Init(int width, int height);  // Gọi một lần lúc khởi động
void ScreenDistort_Begin(void);                  // Bắt đầu render cảnh 3D vào buffer phụ
void ScreenDistort_End(void);                    // Kết thúc render cảnh 3D
void ScreenDistort_Update(float dt);             // Cập nhật lifetime các nguồn
void ScreenDistort_Draw(Camera3D camera);        // Vẽ kết quả kèm distortion lên màn hình
void ScreenDistort_Unload(void);                 // Giải phóng (engine shutdown, không gọi từ skill)
```

**Skill API — chỉ cần gọi Add:**
```c
void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed);
```
* `radius`: bán kính sóng xung kích tối đa (world units).
* `strength`: biên độ méo UV (0.01–0.05 cho heatwave nhẹ, 0.1–0.3 cho shockwave mạnh).
* `speed`: tốc độ lan tỏa sóng ra ngoài.
* Distortion tự tắt sau `lifetime` giây — không cần kill thủ công.

### Metaballs / Screen-Space Fluid (`core/metaball_fx.h`)
```c
void MetaballFX_Init(int width, int height);   // engine-internal
void MetaballFX_Unload(void);                  // engine-internal
void MetaballFX_RegisterBlob(Vector3 worldPos, float radius);
void MetaballFX_DrawRegistered(Camera3D camera, Color tint, float threshold, float smoothness); // engine-internal, main.c gọi 1 lần/frame
```
Rules:
- **Skill API — chỉ cần gọi `MetaballFX_RegisterBlob`** mỗi frame cho mỗi blob muốn hiện (đầu đạn nước, giọt dung nham...) — blob tồn tại đúng 1 frame, phải register lại liên tục. `MAX = METABALL_MAX_BLOBS = 32` toàn engine (registry dùng chung mọi skill, không phải pool riêng từng skill).
- **KHÔNG gọi `MetaballFX_DrawRegistered` từ skill code** — nó chạy GL trực tiếp (BeginTextureMode/EndTextureMode) và **PHẢI** chạy ngoài `BeginMode3D`/`EndMode3D` (raylib's render-texture Begin/End không nest — gọi giữa lúc skill đang vẽ vào `screen_distort.c`'s 3D buffer sẽ phá binding). Đã wire sẵn trong `main.c`, sau `PostFX_Draw()`.
- Hiệu ứng **screen-space thuần 2D** — blob luôn vẽ đè lên trên cùng màn hình, không depth-test với scene 3D (không bị địa hình/thực thể che khuất).
- **Tint hiện tại là 1 màu cố định toàn engine** (`main.c` truyền `ELEMENT_COLOR_WATER` cho mọi blob của mọi skill) — không phải per-skill/per-element. Nếu cần nhiều màu cùng lúc, cần mở rộng registry để mang theo `Color` riêng từng blob (chưa làm — ngoài phạm vi bản hiện tại).
- `threshold`/`smoothness` ảnh hưởng độ "dính" của các blob khi hoà vào nhau — threshold thấp + smoothness cao = dễ dính/mượt hơn.

### Color Gradient (`core/color_gradient.h`)
```c
typedef struct {
    float t;       // [0.0 .. 1.0]
    Color color;
} GradientStop;

typedef struct {
    GradientStop stops[COLOR_GRADIENT_MAX_STOPS]; // max 8
    int count;
} ColorGradient;

bool  ColorGradient_AddStop(ColorGradient *g, float t, Color color);
Color ColorGradient_Sample(const ColorGradient *g, float t);
ColorGradient ColorGradient_MakeElectric(void);
void  ColorGradient_StandardFade(ColorGradient *grad, Color baseColor, float midT, float brightenAmount);
```
* **`AddStop`:** Caller must add stops in increasing `t` order (no internal sort).
* **`Sample`:** Linear interpolation (LERP) between adjacent stops. Prefer `ColorGradient` over `colorStart/colorEnd` for multi-stage color shifts (e.g. fire core white → orange → ash gray).
* **`MakeElectric`:** Built-in preset for the Lightning element.
* **`StandardFade`:** Quick 3-stop gradient (dark → `baseColor` → brighter via `brightenAmount` blended toward `WHITE`); `midT` sets the middle stop position.

### Float Curve (`core/float_curve.h`)
```c
typedef struct {
    float t;       // [0.0 .. 1.0]
    float value;
} FloatCurveStop;

typedef struct {
    FloatCurveStop stops[FLOAT_CURVE_MAX_STOPS]; // max 8
    int count;
} FloatCurve;

bool  FloatCurve_AddStop(FloatCurve *c, float t, float value);
float FloatCurve_Sample(const FloatCurve *c, float t);
```
* Scalar-value equivalent of `ColorGradient` — same API shape (`AddStop`/`Sample`, same stop cap, same LERP-between-adjacent-stops semantics), for any plain `float` that needs to shape itself over a skill's lifetime (particle emission rate, light intensity, motion speed, etc.) instead of a hand-rolled per-skill lerp/easing.
* **`AddStop`:** Caller must add stops in increasing `t` order (no internal sort), same as `ColorGradient_AddStop`.
* **`Sample`:** Linear interpolation between adjacent stops; clamps to the first/last stop's value outside the registered `t` range.
* Maps directly onto `WUXING_ART_DIRECTION.md` Chapter 4.3's "Four Curves" (Intensity/Density/Motion/Lighting) — declare one `FloatCurve` per curve at cast time, sample it each frame in `Update[Name]Skill` instead of scattering manual lerp math.
* **`core/skill_curve.h`'s `SkillCurve`** (`typedef FloatCurve SkillCurve`) is the sandbox-tunable-wired specialization of this same type — a fixed 5-stop convention (`SKILL_CURVE_KEYS`, t = 0/25/50/75/100%) so it renders as 5 plain sliders instead of a free-form stop editor. Use `SkillCurve` (not a raw `FloatCurve`) for anything registered via `SkillTunableEntry.curve` (see "Tunable Parameters" below); reach for a raw `FloatCurve` directly only for curves that stay internal to a skill and are never sandbox-exposed.

### Ribbon Strip (`core/ribbon_strip.h`)
Standard geometry for any continuous long body (dragon, vine, lightning bolt, water stream), replacing stacked billboard chains (heavy overdraw, wrong silhouette when viewed along the path). Technique: **camera-facing ribbon** — at each path point, offset left/right by a vector perpendicular to both the path tangent and the camera view direction, forming a continuous triangle strip (rlgl immediate-mode, no VBO, no malloc).
```c
typedef struct {
    Vector3 position;  // World-space point on the path
    float   halfWidth; // Half-width of the body at this point
    Color   tint;       // Color + alpha at this point
    float   v;          // UV along strip length, caller-computed (e.g. normDist 0..1)
} RibbonPoint;

void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture, Camera3D camera);
```
Rules:
- Module does not manage memory — caller supplies a static `RibbonPoint` array; `count >= 2` required.
- Submits geometry only — does **not** change shader/blend state; `BeginShaderMode()`/`BeginBlendMode()` must be set from outside, so calls interleave with `DrawBillboard` in the same batch.
- Mandatory for any long-body mesh in the project — do not hand-roll a billboard chain (see `SKILL_STANDARD.md`).

### Flow Map (`core/flow_map.h`)
Shared module for UV flow effects (shield, fire, water, tornado...). Each skill owns its own `FlowMap` instance (location cache + config + texture) — no global state, so multiple skills can use flow maps concurrently with different shaders/textures.
```c
typedef struct {
    float speed;    // UV scroll speed over time
    float strength; // UV distortion amplitude
    float tiling;   // Tiling of main/caustics texture
} FlowMapConfig;

typedef struct {
    FlowMapConfig cfg; // plus internal per-instance uniform location cache + texture
} FlowMap;

FlowMap FlowMap_Create(Shader shader, Texture2D flowTex, const char *timeUniformName);
FlowMap FlowMap_CreateWithVortexTexture(Shader shader, int texSize, const char *timeUniformName);
void    FlowMap_Apply(const FlowMap *fm, Shader shader, float time);
void    FlowMap_Unload(FlowMap *fm);
```
* **`Create`:** Binds to one already-loaded shader. `flowTex` (RG = flow direction) is **not** owned by `FlowMap` — caller must `UnloadTexture` it. Pass `NULL` for `timeUniformName` if the skill sets time another way.
* **`CreateWithVortexTexture`:** Generates a procedural vortex flow texture; `FlowMap` **owns** it and frees it in `FlowMap_Unload`.
* **`Apply`:** Call **after** `BeginShaderMode(shader)`, with the same `shader` used in `Create` (raylib needs the matching `shader.id`). Only sets its own flow-texture uniform via `SetShaderValueTexture` — does not bind texture slots manually, so other textures (caustics, main tex...) stay unaffected.

> [!IMPORTANT] **The skill's `.fs` MUST declare the flow texture sampler with the exact name `flowTex`** (i.e. `uniform sampler2D flowTex;`), since `FlowMap_Create`/`FlowMap_CreateWithVortexTexture` resolve their internal `flowTexLoc` via `GetShaderLocation(shader, "flowTex")`. If the `.fs` instead declares it under a different name (e.g. `texture0`), `flowTexLoc == -1` and `FlowMap_Apply` silently no-ops on the texture bind — **no error, no warning**. The sampler then reads raylib's default fallback texture (solid white), and any `flowBlend(...)` call using it returns ~1.0 luminance everywhere, typically blowing out the shader's lighting math to solid white. Confirmed root cause of a real bug in `tsunami_skill` (2026-06-30) — see also §10 Rule D for the related `matModel` silent-no-op gotcha (same failure shape: wrong/unbound uniform → washed-out white render with correct geometry).

### Path Spline (`core/path_spline.h`)
```c
Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target, float t);
int     SamplePath(const Vector3 *path, int pathCount, float spacing, Vector3 *outSegments, int maxSegments);
```
* **`GetBezierPoint`/`GetBezierTangent`:** Standard cubic Bezier — use for cast paths, curved projectile flight, ribbon/tube spline generation.
* **`SamplePath`:** Resamples any `Vector3` chain at even `spacing` (world units), e.g. to feed `RibbonPoint[]` or tube meshes. Returns actual point count (≤ `maxSegments`).
- **Rule:** Never hand-roll Bezier interpolation or point sampling in skill code — use this module.

### Sprite Animation (`core/sprite_anim.h`)
```c
typedef enum {
    ANIM_ONCE = 0,
    ANIM_LOOP,
    ANIM_RANDOM_START,
    ANIM_PING_PONG,
} AnimPlayMode;

typedef struct {
    int rows, cols;       // Atlas grid layout
    int frameCount;       // Valid frame count (<= rows * cols)
    float fps;
    AnimPlayMode playMode;
    // Remaining fields are internal playback state
} SpriteAnim;

void      SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode);
void      SpriteAnim_Update(SpriteAnim *anim, float dt);
Rectangle SpriteAnim_GetUVRect(const SpriteAnim *anim);
bool      SpriteAnim_IsFinished(const SpriteAnim *anim);
void      SpriteAnim_Reset(SpriteAnim *anim);

// Stateless UV lookup by age, for particles that don't keep their own playback state
Rectangle SpriteAnim_CalculateUV(const SpriteAnim *template, float age, int *outFrame);
```
* **Stateless (particles):** Particle/Trail structs hold only `const SpriteAnim *spriteAnim` (a shared template), then call `SpriteAnim_CalculateUV(template, particle->age, &frame)` each frame — avoids bloating the per-particle struct (see `spriteAnim` field in `ParticleConfig`/`TrailConfig`, sections 6–7).
* **Stateful (instances):** For standalone UI/decal/billboard, call `SpriteAnim_Init` then `Update` every frame, read UV via `GetUVRect`.
* `ANIM_RANDOM_START`: starts at a random frame, useful so particles sharing one atlas don't animate in visible lockstep.

### Particle Radial Burst (`core/particle_system.h`)
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

### Impact Burst (`core/composition/visual_composer.h`)
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

### Math Utils (`core/utils_math.h`)
```c
float Math_Mix(float x, float y, float a);  // LERP: x + (y - x) * a
float SmoothStep01(float x);                // Standard smoothstep, clamps input to [0,1]
float Random01(void);                       // Random float in [0.0 .. 1.0]
```
- Use `Math_Mix`/`SmoothStep01` instead of re-implementing lerp/smoothstep in skill code.

### VFX Lights (`core/vfx_light.h`)
Static pool of dynamic point lights (explosion flash, lightning, skill auras), fed into the lighting shader's uniform array.
```c
#define MAX_VFX_LIGHTS 16

typedef struct {
    Vector3 position;
    float   radius;
    Color   color;
} VFXLightData;

typedef enum {
    VFX_PRIORITY_LOW = 0,
    VFX_PRIORITY_HIGH_ULTIMATE
} VFXPriority;

void VFXLight_Init(void);
void VFXLight_Reset(void);
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime,
                    VFXPriority priority);
void VFXLight_Update(float dt);
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);
```
* **`Spawn`:** Light expires automatically after `lifetime` seconds via `Update` — no manual kill needed.
* **`priority` (CORE_ISSUES.md Item 12):** `[!NOTE]` **breaking signature change** — `VFXLight_Spawn` gained a required `VFXPriority` parameter. When `MAX_VFX_LIGHTS` (16) is full, spawn no longer silently rejects: it scans for the lowest-priority active light (ties broken by shortest remaining lifetime) and evicts it, as long as that slot's priority is `<=` the incoming one. Only if every active slot is strictly higher priority does the new spawn get dropped (same as the old full-pool behavior). Existing call sites must add a priority arg (e.g. `VFX_PRIORITY_LOW` for routine VFX, `VFX_PRIORITY_HIGH_ULTIMATE` for Ultimate-tier casts).
* **`GetActive`:** Call every frame before drawing a lit skill, to fetch the active list and upload it via `SetShaderValueV`. `maxCount` should be ≤ `MAX_VFX_LIGHTS` (16).
* Rule: VFX lights supplement, not replace, scene lighting — keep them alive for the skill's full active phase rather than spawning and killing within one frame.

### Procedural Ray VFX (`core/vfx_proc_ray.h`)
Managed pools for lightning-style rays and fixed sky→ground bolts. Skills call Spawn/Update/Draw/Kill — the module owns waypoint generation, multi-harmonic wave math, and glow ribbon drawing.

```c
typedef struct {
    Color colorCore, colorGlow;
    float glowWidthMult;   // glow width = thickness * glowWidthMult (1.4–2.0)
    float waveSpeed;       // rad/s phase advance (3–6; 0 for bolts)
    float amplitudeRatio;  // max disp = length * amplitudeRatio (0.25–0.45)
    float jitterStrength;  // 0=smooth, 1=full electric crackle
    float thickness;       // ribbon half-width in world units (1–3)
    float envelopePow;     // displacement = t^envelopePow (0.7=fast bloom, 1.5=slow whip)
    bool  sharpKinks;      // true=linear (jagged lightning), false=Catmull-Rom (smooth)
    float taperTip;        // width mult at far end: 0.1=needle tip, 1.0=uniform; <=0 → 1.0
    int   branchCount;     // bolts only: forks off main channel (0–4), regenerated per flicker
    float branchScale;     // branch width/alpha vs main channel (0.4–0.6)
} ProcRayConfig;

// Named presets
ProcRayConfig ProcRay_LightningConfig(void);      // violet/white, high jitter, taperTip=0.12 needle tendrils
ProcRayConfig ProcRay_BoltLightningConfig(void);  // amplitudeRatio=0.10, branchCount=3 — sky→ground bolts
ProcRayConfig ProcRay_EnergyConfig(void);         // cyan/gold, smooth Catmull-Rom
ProcRayConfig ProcRay_WindConfig(void);           // white/teal, very low amplitude

// Free-end ray — origin fixed, free end whips via traveling wave. Pool: 32 slots.
int  SpawnProcRay(ProcRayConfig config, float scale);
void ProcRay_SetPhase(int id, float phase);   // offset concurrent rays so they differ
void ProcRay_SetBrightness(int id, float b);  // alpha mult, 1=normal, >1 saturates to flash
void ProcRay_Update(int id, Vector3 origin, Vector3 dir, float length, float scale, float dt);
void ProcRay_Draw(int id, Camera3D cam);
void ProcRay_Kill(int id);

// Fixed bolt — both endpoints clamped, jagged middle flickers at 50ms interval. Pool: 32 slots.
int  SpawnProcBolt(ProcRayConfig config, float scale);
void ProcBolt_SetBrightness(int id, float b); // drive strike flash→afterglow lifecycle
void ProcBolt_Update(int id, Vector3 from, Vector3 to, float scale, float dt);
void ProcBolt_Draw(int id, Camera3D cam);
void ProcBolt_Kill(int id);
```

**Usage pattern (free-end ray):**
```c
int id = SpawnProcRay(ProcRay_LightningConfig(), 1.0f);
ProcRay_SetPhase(id, i * 2.0f * PI / N);  // offset when spawning N concurrent rays
// each frame:
ProcRay_Update(id, origin, dir, length, scale, dt);
ProcRay_Draw(id, camera);
// on expire:
ProcRay_Kill(id);
```

**Usage pattern (sky→ground bolt):**
```c
int id = SpawnProcBolt(ProcRay_BoltLightningConfig(), 1.2f);
// each frame:
ProcBolt_Update(id, skyOrigin, groundPoint, scale, dt);
ProcBolt_Draw(id, camera);
// on expire:
ProcBolt_Kill(id);
```

**Key rules:**
* Always call `ProcRay_Kill`/`ProcBolt_Kill` on deactivate — IDs are pool slots, not auto-freed.
* Call `ProcRay_SetPhase` on each ray after spawn when spawning N concurrent rays — without it, all rays share phase=0 and look identical.
* `SpawnProcRay` assigns a random `refHint` per slot to prevent all horizontal rays from sharing the same perpendicular plane.
* `waveSpeed = 0.0f` is correct for bolts (both endpoints fixed, no traveling wave needed).
* Rendering is 3-pass per channel (outer haze 2.4× → glow → hot core); branches render thinner (`branchScale`) and at 0.7× brightness.
* A freshly spawned bolt has no waypoints until its first `ProcBolt_Update` — call `ProcBolt_Update(id, from, to, scale, 0.0f)` right after `SpawnProcBolt` if you draw it the same frame (the first Update always regenerates immediately).
* Strike lifecycle pattern: `ProcBolt_SetBrightness(id, 1.9f)` for the first ~70 ms (leader flash), then decay toward ~0.45 with small random flicker for the afterglow.

### Combat (`core/skill_manager.h`)
```c
void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
```

### Cooldown / Resource Gating State (`core/skill_manager.h`)
CORE_ISSUES.md Item 16. `Skill_CalculateCooldown()`/`Skill_CalculateManaCost()` only compute numbers — these two hold and check actual elapsed-time state:
```c
bool SkillManager_CanCast(int skillIndex, int agentId);
void SkillManager_TriggerCooldown(int skillIndex, int agentId, float cooldownSeconds);
```
* Keyed by **`(skillIndex, agentId)`** — `agentId` is the caster's `entities/entities.h` agent pool slot (see `PlayerEntity`/`EnemyEntity`'s `agentId` field). Each caster gets an independent cooldown per skill; one caster's Fireball cooldown never blocks another caster's Fireball. Internally a static `float[MAX_SKILLS][256]` table (256 must stay in sync with `entities/entities.h`'s `MAX_AGENTS` — duplicated constant, core/ must not `#include entities/`).
* `SkillManager_TriggerCooldown` ticks down automatically via `UpdateSkillManager(dt, ...)`. Call it yourself at the point a skill actually casts (not wired into any skill's `Cast[Name]Skill` automatically — call-sites need to adopt it).
* `SkillManager_CanCast` returns `true` when remaining cooldown is `<= 0`. Out-of-range `skillIndex`/`agentId` returns `false`.
* **`int Skill_GetIndexByName(const char *name)`** — reverse lookup so a skill can learn its own `skillIndex` (the `RegisterSkill()` return value isn't captured anywhere by convention) in order to actually call `SkillManager_CanCast`/`TriggerCooldown` about itself. Call once in `Init[Name]Skill` (the full registry is already populated by the time any skill's `Init` runs — `InitSkillManager()` registers everything before looping over `init()` callbacks) and cache the result in a `static int s_skillIndex`. Returns `-1` if `name` doesn't exactly match any registered skill's name string (the same string passed to your own `RegisterSkill()` call).

### Abort / Interrupt (`core/skill_manager.h`)
CORE_ISSUES.md Item 14. Optional, additive — does **not** change the mandatory skill lifecycle contract in `skills/CLAUDE.md`:
```c
void RegisterSkillAbort(int skillIndex, void (*abort)(int agentId));
void AbortSkill(int skillIndex, int agentId);
```
* A skill calls `RegisterSkillAbort(index, MyAbortFn)` in addition to `RegisterSkill()` if it wants to support being force-aborted (e.g. future crowd-control). Skills that never call this simply can't be force-aborted.
* `AbortSkill(index, agentId)` invokes the registered callback with that `agentId` if present; otherwise logs `LOG_WARNING` and no-ops. Safe to call unconditionally. A skill that tracks per-caster instance ownership (opt-in, not required) can use the `agentId` to abort only that caster's instance instead of every active instance of the skill type; a skill that ignores the parameter aborts everything, same as before.

### Lifecycle-End Query (`core/skill_manager.h`)
CORE_ISSUES.md Item 13. Optional, additive — does **not** change the mandatory skill lifecycle contract in `skills/CLAUDE.md`:
```c
void RegisterSkillLifecycleQuery(int skillIndex, bool (*hasActiveInstance)(int agentId));
bool Skill_HasActiveInstance(int skillIndex, int agentId);
```
* Lets gameplay code ask "is there still an active instance of this skill owned by agentId X" — e.g. an Earth wall / Wood root-zone effect that gameplay logic needs to know is truly gone before releasing whatever it's blocking.
* Skills that never call `RegisterSkillLifecycleQuery` report `false` unconditionally (safe default — a caller never waits forever on a skill that never opted in; unlike `AbortSkill`, this doesn't `LOG_WARNING` since it's a harmless query, not a command).
* Adopted by `STONE_PRISON` (`StonePrisonSkill_HasActiveInstance`) and `WOOD_THORNS` (`WoodThornsSkill_HasActiveInstance`) — both scan their instance pool for `ownerAgentId == agentId && state != INACTIVE`. Projectile/vortex-style skills (fire_ball, tube, water_sphere, hoa_long_phong_ba) don't need this — they're transient, not "zones" gameplay code needs to track.

### Tunable Parameters (`core/skill_manager.h`)
Optional, additive — lets a skill expose its physics/visual magic numbers as named, min/max-bounded sliders in the sandbox UI (`sandbox/ui_panel.c`) instead of only being editable by recompiling. Not part of the mandatory skill lifecycle contract.
```c
#define MAX_SKILL_TUNABLES 200
typedef struct {
    char label[32]; // slider label AND the key= name in the skill's .tuning file
    float *value;
    float min, max, defaultValue;

    // Optional, appended after defaultValue — existing positional-init call
    // sites ({"label", &val, min, max, def}) keep compiling unchanged, these
    // trailing fields zero-init to NULL/0 (today's legacy behavior).
    const char *phase; // NULL = ungrouped. Free-form tag for sandbox grouping,
                        // e.g. "cast"/"fly"/"impact"/"rain" — by convention
                        // matches a LayeredTimeline layer's tag when the skill
                        // uses one (core/skill_helper.h), not enforced.
    SkillCurve *curve;  // NULL = plain constant via `value`. Non-NULL =
                         // curve-kind entry; `value` must be NULL in this
                         // case — the curve's 5 keyframes are the storage,
                         // sandbox draws 5 sliders (one per keyframe: t =
                         // 0/25/50/75/100%) instead of 1.
} SkillTunableEntry;

void RegisterSkillTunables(int skillIndex, const SkillTunableEntry *entries, int count);
int  Skill_GetTunables(int skillIndex, SkillTunableEntry *outEntries, int maxEntries);

// Flatten/unflatten a possibly-curve-kind entry list to/from core/tuning.c's
// plain "key = value" text format — a curve entry labeled "foo" becomes 5
// ordinary keys "foo_t0".."foo_t4"; a constant entry stays a single "foo"
// key. No changes to tuning.c's format/parser.
#define SKILL_TUNABLES_MAX_FLAT_KEYS (MAX_SKILL_TUNABLES * SKILL_CURVE_KEYS)
int  SkillTunables_Flatten(const SkillTunableEntry *entries, int count,
                            char outKeys[][TUNING_MAX_KEY_LEN], float *outValues, int maxKeys);
void SkillTunables_Unflatten(const SkillTunableEntry *entries, int count,
                              const char *const *keys, const float *values, int keyCount);
bool SkillTunables_LoadPersisted(const char *path, SkillTunableEntry *entries, int count);
```
* Call `RegisterSkillTunables` in `Init[Name]Skill`, after `Skill_GetIndexByName` resolves your own `skillIndex`. `entries` must point at storage you keep alive for your skill's lifetime — a `static SkillTunableEntry s_tunables[N]` whose `.value`/`.curve` fields point at your own `static float`/`static SkillCurve` state — the registry copies the entries (including the pointers) by value, so your static storage stays the single source of truth; the sandbox UI writes through `value` (or `curve->stops[k].value`) directly when a slider moves.
* **Grouping by phase**: set `.phase` on entries that belong to a specific stage of the skill (cast/fly/impact/...) — the sandbox renders one **tab** per distinct tag (in registration order), showing only the active tab's entries at a time instead of one long scrolled list. Entries with `phase == NULL` share a single "GENERAL" tab — the legacy/default look for a skill that doesn't opt into phases at all.
* **Curve-kind entries**: set `.curve = &s_myCurve` (a `static SkillCurve`, seeded via `SkillCurve_SetConstant(&s_myCurve, defaultValue)` before first use) and leave `.value = NULL`. Read it fresh each frame via `SkillCurve_Eval(&s_myCurve, t01)` at the point of use — `t01` is always caller-defined progress (e.g. a `LayeredTimeline`'s `Timeline_LayerProgress`, or a skill's own normalized progress), **never** "fraction of distance already traveled toward a target" — see `SkillHelper_StepCurveFlight` below for why that specific indexing is wrong for a flight-speed curve.
* **Persisting curve-kind entries**: call `SkillTunables_LoadPersisted("skills/<element>/<skill>/<skill>.tuning", entries, count)` in `Init[Name]Skill` right before `RegisterSkillTunables` — it flattens the entry list (curve entries become 5 keys), loads any persisted values from the file (missing keys leave the entry's current value untouched, same semantics as `Tuning_LoadFloatsFromPath`), and unflattens back into `value`/`curve->stops[k].value`. This is a drop-in replacement for the old "build a flat key/value array by hand, call `Tuning_LoadFloatsFromPath`" pattern — needed once any entry is curve-kind, but safe to use even for all-constant entry lists.
* **Separating tables into `.inl` files**: Always separate a skill's parameters and tunable assignment block into co-located `[skill]_params.inl` and `[skill]_tunables.inl` files. `#include "[skill]_params.inl"` at file scope for static variables, and `#include "[skill]_tunables.inl"` inside `Init[Name]Skill` right after the `static SkillTunableEntry` and counter declarations. The preprocessor pastes the file verbatim — it sees all the enclosing function's locals and file-statics without `extern`. Do **not** `#include` the `_tunables.inl` at file scope; it refers to local variables (`tn`, the array name). See `hoa_long_phong_ba_skill.c` or `magma_fissure_skill.c` for reference.
* `Skill_GetTunables` is how `sandbox/ui_panel.c` discovers what to draw for the currently-selected skill — returns 0 for any skill that never called `RegisterSkillTunables`. Its Save button uses `SkillTunables_Flatten` + `Tuning_SaveFloats` to persist, mirroring `SkillTunables_LoadPersisted`.

### Curve-Driven Flight & Extra Force (`core/skill_helper.h`)
```c
void SkillHelper_StepCurveFlight(const SkillCurve *speedCurve, float elapsed, float dt,
                                  float maxDuration, float maxRange, float targetDistance,
                                  float *traveled, bool *arrived);

Vector3 SkillHelper_EvaluateForceLayer(const ForceLayer *layer, Vector3 pos, Vector3 vel,
                                        float time, Vector3 axisOrigin, Vector3 axisDir);
```
* **`SkillHelper_StepCurveFlight`** — one frame's advance for a projectile whose speed follows a `SkillCurve` over **elapsed time**, not over fraction-of-distance-to-target. Indexing a speed curve by distance-progress makes a far-away target silently stretch the whole curve over a longer path, and leaves nothing capping how far a cast can travel — a skill must always have a hard cap on how far/long it can fly, independent of where the target is. `t01 = clamp(elapsed / maxDuration, 0, 1)` indexes `speedCurve`; `*traveled` accumulates `SkillCurve_Eval(speedCurve, t01) * dt` and is clamped to `maxRange`; `*arrived` becomes true the moment `*traveled` reaches `min(targetDistance, maxRange)`, or `elapsed + dt >= maxDuration` — whichever happens first, treated as impact-at-that-point (not vanish), same as reaching the real target early. `maxDuration`/`maxRange` are meant to be ordinary `SkillTunableEntry` rows themselves (tag them with the flight phase), so both the ramp shape and the hard cap are sandbox-adjustable. Fits a straight-line/fixed-direction Euclidean projectile (position = `startPos + dir * *traveled`); a skill whose "flight" is a normalized path-parameter (e.g. a Bezier-curve path already anchored between start/target, inherently bounded) should sample `SkillCurve_Eval` directly at its own progress instead and add its own `maxDuration`-only safety cap — see `fire_skill.c`'s `UpdateFireSkill` for that variant.
* **`SkillHelper_EvaluateForceLayer`** — evaluates a single `ForceLayer` (`core/force_field.h`) without building a whole `ForceField` container just to hold it. Useful for a one-off, author-fixed-shape extra force. If several force types should be simultaneously tunable together, use `SkillForceMix` below instead. `axisOrigin`/`axisDir` only matter for `FORCE_RADIAL_AXIS`/`FORCE_VORTEX_AXIS` layers — pass `(Vector3){0}` for anything else, same convention as `ForceField_Evaluate`.

### Configurable, Always-Additive Force Mix (`core/skill_helper.h`)
```c
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

void SkillForceMix_AddLayers(const SkillForceMix *mix, ForceField *ff);

#define SKILL_FORCE_MIX_TUNABLE_COUNT 29
int SkillForceMix_MakeTunables(SkillForceMix *mix, const char *labelPrefix,
                                const char *phase, SkillTunableEntry *outEntries);
```
* All 8 curated `ForceType`s (excludes `FORCE_RADIAL_AXIS`/`FORCE_VORTEX_AXIS`/`FORCE_VECTOR_TEXTURE`, which need axis/GPU context a generic per-phase mix doesn't have) are **simultaneously available** — each with its own strength (0 = that type contributes nothing) AND its own full relevant parameter set (direction/origin/noise as applicable), not a shared number reused across types. There's no "pick one type" step: dial up as many as you want at once (e.g. curl + gravity-point together) and they all compose into the same `ForceField`. An earlier design let the sandbox switch between types one at a time on a shared field set — that turned out to be confusing (the same displayed number meant different, unrelated things depending on which type was selected) and didn't let two types run together, so it was replaced with this always-additive mix.
* A skill wanting an independent mix per phase declares one `static SkillForceMix` per phase and calls `SkillForceMix_MakeTunables` once at `Init` time, appending the 29 returned entries into its own combined `SkillTunableEntry` array (right after that phase's other tunables, so the phase group stays contiguous — see `fire_skill.c`'s `InitFireSkill` for the pattern of building the array as a sequence of assignments rather than one static literal, needed once any per-phase block mixes fixed named entries with a variable-count helper call).
* Call `SkillForceMix_AddLayers(&mix, &yourForceField)` right before each real use (same "rebuild from current tunable values, don't bake once at Init" rule as any other tunable-driven `ForceField`) — it adds one `ForceLayer` per component whose strength is nonzero, skipping the rest. If a phase's base physics already uses several layers AND the user dials up many mix components at once, the total can exceed `FORCE_FIELD_MAX_LAYERS` (8, `core/force_field.h`) — `ForceField_AddLayer` silently drops layers past that cap rather than crashing, so in that unlikely all-at-once case the least-recently-added mix components stop applying.
* **Rebuild pattern when base field has hardcoded layers + a mix overlay**: create a `static void RebuildXxxField(void)` that (1) `ForceField_Clear`, (2) re-adds the base layers from tunable statics, (3) calls `SkillForceMix_AddLayers`. Call it once at Init AND at the top of every `UpdateXxxSkill` frame. Calling `SkillForceMix_AddLayers` only at Init bakes a snapshot — sandbox mix changes won't take effect until the next cast. Confirmed in `hoa_long_phong_ba_skill.c`'s `RebuildGeyserField`/`RebuildTravelField`.

### Shader Binding (`core/skill_manager.h`)
```c
void SkillManager_BeginShader(Shader shader);
void SkillManager_EndShader(void);
```
Automatically binds `u_time`, `viewPos`, `u_resolution`.

### Debug Draw (`core/debug_draw.h`)
CORE_ISSUES.md Item 17. Thin wireframe overlay for visually tuning hitbox/AoE radii — no equivalent existed before (`core/tuning.h`'s hot-reload let you edit a radius number but not see it in-world).
```c
void DebugDraw_SetEnabled(bool enabled);
bool DebugDraw_IsEnabled(void);
void DebugDraw_Sphere(Vector3 pos, float radius, Color color);
void DebugDraw_Circle(Vector3 center, float radius, Color color);
```
* Gated behind a single global toggle, **default disabled** — all `Draw*` calls no-op when disabled, so call sites can be left in unconditionally.
* `DebugDraw_Circle` draws on the ground plane at `center.y` (for AoE/ground-target shapes).
* Wireframe only, built on raylib's `DrawSphereWires`/`DrawCircle3D` directly — exempt from skills' "no raylib primitives" rule since this is internal core dev tooling, not a shipped VFX mesh.

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
- **Submits geometry only** — like `DrawRibbonStrip`, `ProceduralMesh_DrawTube` does not change shader or blend state. `BeginShaderMode()` and, if the shader outputs alpha < 1 (e.g. for translucent water/energy tubes), `BeginBlendMode(BLEND_ALPHA)` or `BLEND_ADDITIVE` must be set by the calling skill before the draw call.

> [!NOTE]
> **UV convention (inferred — confirm against `procedural_mesh_utils.c` source):** Based on how `tube.vs`/`tube.fs` consume `vertexTexCoord`, the tube mesh appears to map `vertexTexCoord.x ∈ [0, 1]` around the **radial/circumferential** direction (so `phi = vertexTexCoord.x * 2π` sweeps once around the tube's cross-section) and `vertexTexCoord.y` along the **length** of the tube in world units, normalized by dividing by `u_uvLength` (set from C-side to the tube's actual Bezier arc length) to get `t ∈ [0, 1]` along the spline. This document doesn't yet have a confirmed, authoritative statement of this convention from the mesh-generation source itself — treat the above as the working assumption validated by the Water Stream sample, and update this note if `procedural_mesh_utils.c` defines it differently (e.g. if `uvLengthScale` in `ProceduralMesh_DrawTube` already pre-divides UVs, in which case skill shaders should *not* divide by `u_uvLength` again).

#### Wave Plane (rippling water surface)

```c
#define WAVE_PLANE_MAX_SEGMENTS_X 24
#define WAVE_PLANE_MAX_SEGMENTS_Z 24

typedef struct {
    float wavelength;
    float amplitude;
    Vector3 direction;     // propagation direction, XZ-plane, normalized internally
    float crestSharpness;  // 0 = smooth sine, higher = sharper peaked crests
} WavePlaneConfig;

typedef struct {
    Vector3 verts[WAVE_PLANE_MAX_SEGMENTS_X + 1][WAVE_PLANE_MAX_SEGMENTS_Z + 1];
    Vector3 normals[WAVE_PLANE_MAX_SEGMENTS_X + 1][WAVE_PLANE_MAX_SEGMENTS_Z + 1];
    int segmentsX;
    int segmentsZ;
} WavePlaneMeshData;

WavePlaneConfig ProceduralMesh_DefaultWavePlaneConfig(void);

void ProceduralMesh_BuildWavePlane(
    WavePlaneMeshData *out,
    Vector3 center,
    float width, float length,
    int segmentsX, int segmentsZ,
    float time,
    const WavePlaneConfig *cfg
);

void ProceduralMesh_DrawWavePlane(const WavePlaneMeshData *data, Color color);
```
Rules:
- Flat-ish rippling water surface — a subdivided grid with per-vertex Y displacement from 3 layered sine waves (main directional + secondary cross-direction at ~2.3x freq + slow low-frequency) plus a deterministic per-vertex hash-noise term, all computed CPU-side at Build time (geometry, not GPU shader displacement) — same CPU/GPU split as `BuildTube`.
- `crestSharpness` reshapes the main wave via `sign(s) * |s|^(1/(1+sharpness))` — 0 leaves it a plain sine, higher values peak the crests.
- Moderate segment counts only (`WAVE_PLANE_MAX_SEGMENTS_X/Z` = 24) — low-poly per mobile/Android performance discipline, not a dense smooth mesh.
- Build every frame like `BuildTube` (animates via `time`), then `Draw`. `Draw*` takes an explicit `Color` (unlike `DrawTube`, which gets color from a dedicated shader) to support plain vertex-color rendering too.
- Normals computed via finite-difference between neighboring displaced verts (not analytic).

#### Curling Wave (cresting/curling wave wall — tsunami silhouette)

```c
#define CURLING_WAVE_MAX_WIDTH_SEGS 32
#define CURLING_WAVE_MAX_PROFILE_SEGS 16

typedef struct {
    float curlAmount;  // 0 = flat wall, higher = more overhang at the top lip
    float height;
    float archWidth;   // total width of the wave wall along widthDirection
} CurlingWaveConfig;

typedef struct {
    // verts[w][p]: w = slice along width, p = point along the "C" profile (0=base, profileSegs=outer lip)
    Vector3 verts[CURLING_WAVE_MAX_WIDTH_SEGS + 1][CURLING_WAVE_MAX_PROFILE_SEGS + 1];
    Vector3 normals[CURLING_WAVE_MAX_WIDTH_SEGS + 1][CURLING_WAVE_MAX_PROFILE_SEGS + 1];
    int widthSegs;
    int profileSegs;
} CurlingWaveMeshData;

CurlingWaveConfig ProceduralMesh_DefaultCurlingWaveConfig(void);

void ProceduralMesh_BuildCurlingWave(
    CurlingWaveMeshData *out,
    Vector3 baseCenter,
    Vector3 widthDirection,
    const CurlingWaveConfig *cfg,
    int profileSegs, int widthSegs
);

void ProceduralMesh_DrawCurlingWave(const CurlingWaveMeshData *data, Color color);
```
Rules:
- Distinct from Wave Plane — this is the actual cresting/curling wave wall, not a flat rippling surface. Sweeps an **open "C"-shaped cross-section** (base → rising face → overhanging lip) sideways along `widthDirection`, reusing `BuildTube`'s sweep-along-path technique (Frenet-style `up`/`depth` frame) adapted to an open arc instead of a closed circle.
- Profile arc: starts at -90° (base), smoothstep-eased sweep up to `90° + 90°*curlAmount` (lip). `curlAmount = 0` → flat wall; higher → more overhang.
- Small deterministic per-vertex jitter applied near the lip to avoid a perfectly smooth "cast mold" look.
- Same Build-every-frame-then-Draw convention as `BuildTube`/`BuildWavePlane`.

#### Rock (low-poly faceted rock — prominent/large rocks only)

```c
#define ROCK_MESH_MAX_VERTS 162
#define ROCK_MESH_MAX_FACES 320

typedef struct {
    Vector3 verts[ROCK_MESH_MAX_VERTS];
    Vector3 faceNormals[ROCK_MESH_MAX_FACES];  // flat-shaded: 1 normal per face
    int faceVertIdx[ROCK_MESH_MAX_FACES][3];
    int vertCount;
    int faceCount;
} RockMeshData;

void ProceduralMesh_BuildRock(
    RockMeshData *out,
    Vector3 center, float radius,
    float jitterAmount,
    int seed,
    int subdivisions
);

void ProceduralMesh_DrawRock(const RockMeshData *data, Color color);
```
Rules:
- For **prominent/large rocks only**. Small background rubble should keep using squished `DrawCoreCube`/`DrawCoreSphere` with per-instance randomization (§12.3) — don't switch those over to this function.
- Built from a base icosahedron (12 verts/20 faces), recursively subdivided (`subdivisions`, clamped to 0-2 — level 2 ≈ 162 verts, at the `ROCK_MESH_MAX_VERTS` ceiling), then each vertex's radial distance from `center` is jittered within `±jitterAmount` via a deterministic hash PRNG keyed on `seed` + vertex index. Same `seed` always produces the same rock shape.
- Flat-shaded (per-face normals, not per-vertex averaged) so facets read as angular/natural, not as a smoothed sphere — this is the key difference from squishing `DrawCoreSphere`.
- **Build once at cast time and cache in the skill's instance struct** — unlike `BuildTube`/`BuildWavePlane`, rocks don't animate their shape, so there's no reason to rebuild every frame.

#### Shard Cluster (radiating crystal/shard cluster)

```c
#define SHARD_CLUSTER_MAX_SHARDS 16
#define SHARD_MAX_SIDES 6

typedef struct {
    float spreadAngle;       // half-angle of the cone shards radiate within (radians)
    float thicknessMin, thicknessMax; // cross-section radius as a ratio of each shard's own length
    float tipSharpness;      // 0 = flat-cut tip (full cross-section), 1 = sharp point
    int   sides;              // polygon sides per shard cross-section, <= SHARD_MAX_SIDES
} ShardClusterConfig;

typedef struct {
    Vector3 baseRing[SHARD_CLUSTER_MAX_SHARDS][SHARD_MAX_SIDES];
    Vector3 tipRing[SHARD_CLUSTER_MAX_SHARDS][SHARD_MAX_SIDES];
    Vector3 baseNormal[SHARD_CLUSTER_MAX_SHARDS][SHARD_MAX_SIDES];
    Vector3 tipCenter[SHARD_CLUSTER_MAX_SHARDS];
    Vector3 baseCenter[SHARD_CLUSTER_MAX_SHARDS];
    int sides;
    int shardCount;
} ShardClusterMeshData;

ShardClusterConfig ProceduralMesh_DefaultShardClusterConfig(void);

void ProceduralMesh_BuildShardCluster(
    ShardClusterMeshData *out,
    Vector3 origin,
    Vector3 mainDirection,
    int shardCount,
    float minLength, float maxLength,
    int seed,
    const ShardClusterConfig *cfg
);

void ProceduralMesh_DrawShardCluster(const ShardClusterMeshData *data, Color color);
```
Rules:
- Each shard is a tapered prism (low-poly polygon cross-section, `cfg->sides`) radiating from `origin`, tilted off `mainDirection` by a random angle within `cfg->spreadAngle` (cone spread), with randomized length (`minLength..maxLength`), thickness ratio (`thicknessMin..thicknessMax`), and cross-section twist — all deterministic per `seed` (same PRNG helper as `BuildRock`'s jitter). Shards are never evenly spaced/sized — same anti-robotic discipline as Rock/CurlingWave jitter.
- Use case: Metal sword-qi/shard skills, Water ice-shard skills.
- `tipSharpness` controls `tipRadius = baseRadius * (1 - tipSharpness)` — 1.0 gives a near-point tip, 0.0 a flat-cut prism end.
- **Build once at cast time and cache** — shards don't animate shape, same convention as Rock.

#### Vortex Funnel (tapered, twisting wind/tornado funnel)

```c
#define VORTEX_FUNNEL_MAX_HEIGHT_SEGS 32
#define VORTEX_FUNNEL_MAX_RADIAL_SEGS 24

typedef struct {
    float topRadius;
    float bottomRadius;
    float height;
    float twistAmount;  // total rotation in degrees from bottom to top
    int   ridgeCount;   // number of visible spiral ridges
    float ridgeAmount;  // ridge protrusion, as a ratio of local radius (0 = no ridge, ~0.15 = moderate)
} VortexFunnelConfig;

typedef struct {
    // rings[i][j]: i = along height (0=bottom, heightSegs=top), j = around circumference
    Vector3 rings[VORTEX_FUNNEL_MAX_HEIGHT_SEGS + 1][VORTEX_FUNNEL_MAX_RADIAL_SEGS];
    Vector3 normals[VORTEX_FUNNEL_MAX_HEIGHT_SEGS + 1][VORTEX_FUNNEL_MAX_RADIAL_SEGS];
    int heightSegs;
    int radialSegs;
} VortexFunnelMeshData;

VortexFunnelConfig ProceduralMesh_DefaultVortexFunnelConfig(void);

void ProceduralMesh_BuildVortexFunnel(
    VortexFunnelMeshData *out,
    Vector3 center,
    const VortexFunnelConfig *cfg,
    int heightSegs, int radialSegs,
    float time
);

void ProceduralMesh_DrawVortexFunnel(const VortexFunnelMeshData *data, Color color);
```
Rules:
- For Phong (wind) skills, tornado/cyclone visuals, the Taiji ultimate.
- Conceptually the same sweep-along-path technique `BuildTube`/`BuildCurlingWave` use, specialized for a **straight vertical path**: instead of a Bezier + Frenet frame, the path is fixed +Y, so the function builds rings directly rather than calling `BuildTube` — but the `rings[height][radial]`/`normals[height][radial]` data layout and the two-level (height, then radial) build loop intentionally mirror `TubeMeshData`/`BuildTube`'s convention.
- Cross-section radius lerps `bottomRadius -> topRadius` over height, rotates by `twistAmount` degrees total (plus continuous `time`-based spin for an animated vortex — pass `time=0` for a static/cached funnel), and gets a `cos(phi * ridgeCount)` bump (`ridgeAmount`) that follows the twist angle so ridges read as spiraling along the surface, not static rings.
- **No end caps** — the funnel is open at both ends (tornado silhouette shows through), unlike Tube's capped ends.
- Animated (spinning) funnels should rebuild every frame like `BuildTube`/`BuildWavePlane`; static use can build once and cache with `time=0`.

#### Fissure (raised/sunken jagged 3D ground crack)

```c
#define FISSURE_MAX_SEGMENTS 48
#define FISSURE_CROSS_VERTS 5  // left edge, left shoulder, bottom, right shoulder, right edge

typedef struct {
    Vector3 verts[FISSURE_MAX_SEGMENTS + 1][FISSURE_CROSS_VERTS];
    Vector3 normals[FISSURE_MAX_SEGMENTS + 1][FISSURE_CROSS_VERTS];
    int segments;
} FissureMeshData;

void ProceduralMesh_BuildFissure(
    FissureMeshData *out,
    const Vector3 *pathPoints, int pathPointCount,
    float width, float depth,
    float jaggedness,
    int seed
);

void ProceduralMesh_DrawFissure(const FissureMeshData *data, Color color);
```
Rules:
- Distinct from the existing **flat 2D crack decals** — this is real 3D geometry, for Earth skills (Địa chấn, Thạch shatter-type effects) needing more presence than a decal.
- Centerline is rasterized from `pathPoints` (a polyline, not Bezier control points) via `SamplePath` (`core/path_spline.h`) — reuses the same path-sampling function the Anchored-Along-Path skill skeleton already uses, no hand-rolled sampling. `spacing` passed to `SamplePath` is `max(width*0.5, 1.0)`, clamped to `FISSURE_MAX_SEGMENTS` samples.
- Each cross-section is a 5-vertex jagged "V" (left edge at y=0 → left shoulder → bottom at y=-depth → right shoulder → right edge at y=0), with `jaggedness` (0..1) scaling deterministic per-segment jitter (seed-keyed, same PRNG helper as Rock) on edge width, shoulder depth, bottom depth, and lateral centerline offset — avoids a perfectly straight/regular crack.
- Pass a negative `depth` for a raised crack instead of a sunken one.
- **Build once at cast time and cache** — fissures don't animate shape, same convention as Rock/ShardCluster.

### GPU Vertex Displacement (`core/procedural_mesh_utils.h` + `core/shaders/common/displacement.glsl`)
```c
Mesh ProceduralMesh_CreateBaseGrid(float width, float length, int segmentsX, int segmentsZ);
Mesh ProceduralMesh_CreateBaseCylinder(int radialSegs, int heightSegs);

typedef struct {
    float amplitude;   // DisplaceVertex_Noise normal-offset magnitude
    float frequency;
    float speed;
    float twistAmount; // radians, t=0..1 — AlongPath/TwistAndTaper
    float taperStart;
    float taperEnd;
    Vector3 pathP0, pathP1, pathP2, pathP3; // world space, AlongPath only
} MeshDisplacementParams;

MeshDisplacementParams ProceduralMesh_DefaultDisplacementParams(void);
void ProceduralMesh_SetDisplacementUniforms(Shader shader, const MeshDisplacementParams *params);
void ProceduralMesh_UnloadBase(Mesh *mesh);
```
```glsl
// core/shaders/common/displacement.glsl — include AFTER vs_header.glsl, opt-in
vec3 DisplaceVertex_Noise(vec3 localPos, vec3 localNormal, float noiseVal);
vec3 DisplaceVertex_AlongPath(vec3 localPos, vec2 texCoord);
vec3 DisplaceVertex_TwistAndTaper(vec3 localPos);

// Normal counterparts — REQUIRED alongside AlongPath/TwistAndTaper (see rules below)
vec3 DisplaceVertex_AlongPathNormal(vec3 localNormal, vec2 texCoord);
vec3 DisplaceVertex_TwistAndTaperNormal(vec3 localPos, vec3 localNormal);
```
Rules:
- **`AlongPath`/`TwistAndTaper` REQUIRE the matching `*Normal()` call, or lighting will be visibly wrong** (spiral banding artifacts, confirmed in testing). `VS_FinalOutput()` (in `vs_header.glsl`) sets `fragNormal` from the **un-rotated** `vertexNormal` — it has no idea the position function just rotated the vertex into a new frame (path frame, or twist angle). Always call the displacement function, then `VS_FinalOutput(displaced)`, then **override `fragNormal`** with the `*Normal()` counterpart, e.g.:
  ```glsl
  vec3 displaced = DisplaceVertex_TwistAndTaper(vertexPosition);
  VS_FinalOutput(displaced);
  fragNormal = normalize(vec3(matModel * vec4(
      DisplaceVertex_TwistAndTaperNormal(vertexPosition, vertexNormal), 0.0)));
  ```
- `DisplaceVertex_Noise` has no normal counterpart — its displacement amplitude is assumed small enough that the un-rotated normal is an acceptable approximation. For higher-fidelity ripples, perturb the normal in the fragment shader instead (see `lighting.glsl`'s `perturbNormal()`).
- **Additive, not a replacement** for the CPU builders above (Tube/WavePlane/CurlingWave/Rock/ShardCluster/VortexFunnel/Fissure). Those rebuild CPU-side every frame and let skill code read back vertex positions (e.g. for raycast/anchoring). This system bakes ONE static mesh on the GPU at cast time and lets the vertex shader displace it every frame via uniforms — CPU never sees the displaced positions. **Only use this for pure-visual effects that need no raycast/collision against the displaced shape.**
- Cast-time only: call `ProceduralMesh_CreateBaseGrid`/`CreateBaseCylinder` once, cache the returned `Mesh` in the skill's instance struct. Do **not** call it per frame.
- `ProceduralMesh_CreateBaseCylinder` returns a 2-end-open tube (no caps), local axis +Y in `[0,1]`, local radius 1 — `vertexTexCoord.y` is the `t` parameter consumed by `DisplaceVertex_AlongPath`/`TwistAndTaper`.
- `displacement.glsl` does **not** depend on `noise.glsl` (same independence convention as `fx.glsl`'s `dissolveCalc`) — `DisplaceVertex_Noise` takes a precomputed `noiseVal`; compute it with `fbm2`/`vnoise` from `noise.glsl` if needed, include `noise.glsl` before `displacement.glsl` in that case.
- `ProceduralMesh_SetDisplacementUniforms` silently skips uniforms the shader doesn't declare (same safe pattern as `SkillManager_BeginShader`) — call every frame after `BeginShaderMode(shader)`, before `DrawMesh`/`DrawModel`.
- `DrawMesh`/`DrawModel` (unlike rlgl immediate-mode draws used by the CPU builders above) auto-populate `matModel` correctly via raylib — no need for the `SkillManager_BeginShader` identity-matModel workaround documented for immediate-mode skills.
- Call `ProceduralMesh_UnloadBase` exactly once at skill unload — not per frame.

### Post FX (`core/post_fx.h`)

```c
void PostFX_Init(int width, int height);
void PostFX_Unload(void);
void PostFX_Begin(void);   // Begin rendering the main 3D scene into the PostFX buffer
void PostFX_End(void);     // End main scene rendering
void PostFX_Draw(const PostFXConfig *config); // Runs Bloom -> CA -> Grade -> Vignette, draws to screen

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
Rules:
- Call order each frame: `PostFX_Begin()` → draw 3D scene → `PostFX_End()` → `PostFX_Draw(&config)`.
- `Init`/`Unload` belong to the application lifecycle (global) — skill code does not call them.
- **Bloom uses dual-filter pyramid** (downsample 1/4→1/8→1/16 + upsample back), replaced the old separable Gaussian. Produces a wider, softer glow at the same pass count. Recommended values for a dark arena scene: `bloomThreshold=0.5f`, `bloomIntensity=2.0f`.
- **Skills do not control bloom parameters.** Skills control emissive brightness of their own particles/shaders — the global bloom picks up whatever exceeds `bloomThreshold` automatically. Brighter emissive = more bloom, no per-skill config needed.
- **Multi-texture binding:** `u_bloomTex` uses `SetShaderValueTexture` (called inside `BeginShaderMode`). Do not use `rlActiveTextureSlot`/`rlEnableTexture` for extra textures in post-FX passes — confirmed silently broken (same root cause as Item 3 soft-particle depth tex).

### Camera FX (`core/camera_fx.h`)
```c
void CameraFX_Shake(float trauma);          // Thêm chấn động rung lắc (tích lũy, giới hạn 1.0)
void CameraFX_Update(Camera3D *camera, float dt); // Cập nhật + áp dụng offset vào camera — gọi mỗi frame
```
* `trauma`: `0.25` = nhẹ, `0.5` = trung bình, `0.75–1.0` = nặng. Giá trị cộng dồn và tự giảm dần theo thời gian.
* `CameraFX_Update` phải được gọi sau khi update game logic, trước khi `BeginMode3D`. Skill code chỉ gọi `CameraFX_Shake`; `Update` thuộc engine loop.

**Skill Helper impulse (tuỳ chọn — `core/skill_helper.h`):**
```c
typedef struct { float magnitude; float duration; float frequency; float falloff; } CameraImpulse;
void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse);
```

### Audio

**Centralized presets (`core/skill_helper.h`, `core/resource_manager.h`) — use these for Cast/Impact:**
```c
Sound ResourceManager_LoadSound(const char *filePath); // cached, same dedup-by-path pattern as LoadTexture/LoadShader

void PlayCastSound(EffectPresetType preset);
void PlayImpactSound(EffectPresetType preset);
```
Reuse the same `EffectPresetType` enum as `SpawnCastEffect`/`SpawnImpactEffect`, so a skill calls one enum value for both image and sound. Each preset loads (via `ResourceManager_LoadSound`, cached) and plays a per-element `Sound` on first use, then just `PlaySound()`s the cached handle on subsequent calls — callers don't need their own cache layer. No Flight-stage sound preset yet (looping/ambient audio during flight is a different mechanism than one-shot `PlaySound`).

> [!NOTE] As of 2026-06-30 no per-element SFX assets exist under `assets/` (no `.wav`/`.ogg` files anywhere in the repo). `PlayCastSound`/`PlayImpactSound` currently `TraceLog(LOG_WARNING, ...)` once per missing preset and return without playing or crashing — this is a content gap, not a stub Core Agent will silently fix. Once real asset files land (e.g. `assets/sounds/fire_cast.wav`), wire the paths into the switch in `skill_helper.c`.

**Skill-owned one-off sound (still valid):**
```c
PlaySound(Sound sound);
```
For a skill's own *unique* one-off sound not covered by an element preset, load via `ResourceManager_LoadSound()` in `InitSkill()` (cached, don't `LoadSound`/`UnloadSound` directly) and `PlaySound()` it yourself. Cast/Impact sounds that map to an element should go through `PlayCastSound`/`PlayImpactSound` instead, not be hand-rolled per skill.

---

## 9. Wind Zone Global (`core/force_field.h`)

`WindZone` là một `ForceField` toàn cục tự động áp dụng cho **mọi particle** trong `UpdateParticles()` — skill code không cần gán per-`ParticleConfig`. Dùng để mô phỏng gió môi trường, bão, hoặc lực nền suốt trận đấu.

```c
void    WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq);
void    WindZone_Clear(void);
bool    WindZone_IsActive(void);
// WindZone_Evaluate() chỉ dùng nội bộ bởi particle_system — skill code không gọi trực tiếp.
```

**Tham số:**
| Param | Ý nghĩa | Giá trị gợi ý |
|---|---|---|
| `direction` | Hướng gió chính (sẽ normalize tự động) | `(Vector3){1,0,0}` = gió đông |
| `strength` | Gia tốc cơ bản (m/s²) | `80–250` cho gió nhẹ–bão |
| `noiseAmp` | Biên độ nhiễu Curl chồng (0 = gió thẳng) | `30–80` |
| `noiseFreq` | Tần số không gian của nhiễu | `0.005–0.03` |

**Ví dụ:**
```c
// Thiết lập gió đông bắc nhẹ có nhiễu — gọi khi bắt đầu map hoặc thời tiết thay đổi
WindZone_Set((Vector3){0.7f, 0.0f, 0.3f}, 120.0f, 40.0f, 0.015f);

// Tắt khi vào vùng trong nhà hoặc kết thúc hiệu ứng thời tiết
WindZone_Clear();
```

---

## 9b. Skill Helper (`core/skill_helper.h`)

Các wrapper tiện ích cao cấp — dùng để giảm boilerplate. Không bắt buộc; skill phức tạp thường gọi thẳng core API.

### Impact Effect Preset
```c
typedef enum { EFFECT_PRESET_FIRE_EXPLOSION, EFFECT_PRESET_ICE_SHATTER,
               EFFECT_PRESET_WATER_SPLASH, EFFECT_PRESET_LIGHTNING_IMPACT,
               EFFECT_PRESET_EARTH_CRACK, EFFECT_PRESET_WOOD_BLOOM,
               EFFECT_PRESET_METAL_SHARD, EFFECT_PRESET_TAIJI_BURST } EffectPresetType;
void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale);
```
All 6 elements now covered: `WOOD_BLOOM` (leaf/vine burst, upward-biased, `ELEMENT_COLOR_WOOD`, `DECAL_PRESET_WOOD_MOSS`), `METAL_SHARD` (sharp shards, high pitch range, `ELEMENT_COLOR_METAL`, `DECAL_PRESET_METAL_SLASH`), `TAIJI_BURST` (amethyst-purple radial burst + stronger light flash for the "no-element" ultimate state, `ELEMENT_COLOR_TAIJI`, `DECAL_PRESET_TAIJI_RING`).

**`EFFECT_PRESET_EARTH_CRACK` does NOT call `CameraFX_Shake`** — it was removed from `SpawnImpactEffect` so that Earth skills with a per-skill `s_shakeEnable` toggle can control shake themselves. Any skill that uses `EARTH_CRACK` and wants shake must call `CameraFX_Shake(…)` explicitly, guarded by its own toggle.

### Cast Effect Preset (windup/energy-gathering)
```c
void SpawnCastEffect(Vector3 pos, EffectPresetType preset, float scale);
```
Reuses `EffectPresetType` — the cast/windup equivalent of `SpawnImpactEffect`. No knockback, no ground decal. Particles spawn on a ring around `pos` and get pulled inward via a `FORCE_GRAVITY_POINT` field (reads as "energy gathering" rather than an outward explosion), plus a light flash (`VFXLight_Spawn`). Call at the start of a cast/windup phase, e.g. inside `Cast[Name]Skill` or the `STATE_CASTING` branch of `Update[Name]Skill`. Internally backed by an 8-slot static `ForceField` pool (`MAX_CONCURRENT_CAST_EFFECTS`), claimed round-robin per call, so concurrent casts at different positions (multi-player) pull correctly without interfering with each other.

### Flight Effect Preset (projectile trail)
```c
int SpawnProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed);
```
Reuses `EffectPresetType` — the flight-stage equivalent of Cast/Impact, for the middle of a skill's lifecycle (while it's traveling from `start` to `target`). Spawns a `TRAIL_TYPE_PROJECTILE` `TrailConfig` (per-element tint/gradient from the same static `ColorGradient`s Cast/Impact use) plus a head `ParticleConfig` with an `onLiveEmit` sub-emitter for continuous tail dust. Returns the trail ID — **caller MUST call `KillTrail(id)` on impact**, typically right before calling `SpawnImpactEffect` at the target.

**Force regime differs from Cast/Impact**: uses the sustained/flight range (300-650f primary directional pull via `FORCE_GRAVITY_DIR`, ~20f `FORCE_NOISE_PERLIN` wobble on top) per CORE_API.md §1 and the `water_stream/tube_skill.c` precedent — NOT the 5-60f ambient/burst range used by `SpawnCastEffect`/`SpawnImpactEffect`. Internally backed by an 8-slot static `ForceField` pool (`MAX_CONCURRENT_PROJECTILE_TRAILS`), claimed round-robin per call, same pattern as `SpawnCastEffect`'s pool, so concurrent flying projectiles don't interfere with each other's direction.

### Lightning Trail Presets
```c
int SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed);
int SpawnLightningFollowerTrail(Vector3 startPos, float scale, float life);
```
Dedicated jagged/flicker profile for electric visuals (bolts, electric blades, electric projectiles, teleport streaks) — `SpawnProjectileTrail`'s `EffectPresetType` path only swaps color/gradient, it can't reproduce lightning's signature zigzag because its flight wobble (20f/0.08f `FORCE_NOISE_PERLIN`) is tuned for a smooth arc, not a jagged bolt.

* **`SpawnLightningTrail`**: flight-stage bolt (tia điện / đạn điện / teleport streak) that travels from `start` to `target` along a **precomputed jagged polyline**, not a physics/noise-driven path. Two earlier passes tried physics instead — `TRAIL_TYPE_PROJECTILE` (with and without `FORCE_GRAVITY_DIR`, with `wobbleAmplitudeOverride` cranked up) rendered as a visually straight line because its built-in homing steer damps any deviation back toward straight every frame; `TRAIL_TYPE_WISP` with a strong noise `forceField` rendered as a smooth "silk ribbon" sag because `ConstrainRibbonSegment`'s distance-solver (needed to keep the strand rope-coherent) low-pass-filters per-node jaggedness into a flowing curve. Neither can hold a real sharp-angle zigzag — both are built to stay smooth by design. The fix: `GenerateLightningWaypoints` (internal to `skill_helper.c`) builds `LIGHTNING_BOLT_WAYPOINTS` (9) points along `start`→`target`, offsetting each interior point sideways (alternating sign, two perpendicular axes relative to the travel direction) by `jaggedAmount` — a real geometric kink, with no `forceField` involved at all, so no gravity-like sag is possible. A `TRAIL_TYPE_FOLLOWER` trail is spawned with `onUpdate = LightningBoltAdvance`, which each frame advances progress along the waypoint polyline (`SampleLightningPath`) and pushes the interpolated point via `UpdateFollowerPosition` — `LIGHTNING_BOLT_PUSH_COUNT` (50) total pushes spread proportionally over the travel duration (`boltLen / speed`), staying under `TRAIL_HISTORY_COUNT` (60) so no earlier point of the bolt gets evicted from history — the whole bolt stays visible from start to current tip, not just a trailing window. `LightningBoltAdvance` also spawns a small short-lived particle (the visible "hạt") at the tip on every push (exactly on the path, not a separately physics-simulated dot that could diverge) and an occasional `VFXLight_Spawn` flicker. Progress reaching 1.0 self-terminates via `KillTrail` — the caller does **not** need to call `KillTrail(id)` itself for the normal flow (only if cancelling early). Per-bolt state (waypoints, elapsed time, push count) lives in a `MAX_CONCURRENT_LIGHTNING_TRAILS`-slot round-robin pool (`s_lightningBolts`) keyed by trail ID, looked up inside the callback since `TrailUpdateCallback` carries no userdata pointer.
* **`SpawnLightningFollowerTrail`** + **`Lightning_UpdateFollowerTip`**: manually-driven variant for an electric aura/bolt **attached to a moving object at a fixed local point** (kiếm điện, a spark pinned to a point on a moving object) — `TRAIL_TYPE_FOLLOWER`, thin (`thick = 1.0f*scale`). Drive the tip with **`Lightning_UpdateFollowerTip(id, tipPos, scale)`, not the raw `UpdateFollowerPosition`**: feeding a smooth per-frame path straight into `UpdateFollowerPosition` records one history node per frame — a dense, smooth curve that reads as a wiggly worm, not lightning (confirmed visually testing an orbiting anchor this way). `Lightning_UpdateFollowerTip` only accepts a new point once the caller's real position has moved `LIGHTNING_FOLLOWER_MIN_SEGMENT` (45f, scaled) away from the last recorded one — few, far-apart points — and inserts one perpendicular-offset kink at the midpoint of each accepted segment (real geometric displacement, same philosophy as `GenerateLightningWaypoints`, just applied incrementally instead of precomputed). This turns *any* caller-driven path into a sparse zigzag automatically, regardless of how often the caller calls it (e.g. every frame). Per-trail filter state (last recorded position, alternating kink sign) lives in a small round-robin pool keyed by trail ID, same lookup-by-ID pattern as `SpawnLightningTrail`'s bolt state. The trail's own `forceField` (`FORCE_NOISE_PERLIN` 100f/0.6/14 + `FORCE_VISCOSITY` 4.0f — lighter than before, since it now only adds crackle texture on top of the filter's real kinks instead of having to invent jaggedness from scratch) is unrelated to gravity, so dragging behind a moving anchor comes purely from the trail's own history-lag. Caller **MUST call `KillTrail(id)`** when the effect ends. `Trail_AttachToTransform()` still works for a smooth (non-electric) FOLLOWER use if the zigzag filtering isn't wanted.
* Both share a `LightningTrailFlicker` `onUpdate` callback — reads live `position`/`scale`/`tint` off the `TrailEntity` itself (not captured per-call state, so one function pointer serves every concurrent bolt) and spawns a short `VFXLight_Spawn` burst (`35f * scale` radius, 0.08s life, `VFX_PRIORITY_LOW`) at ~10/sec average, framerate-independent via `dt`-scaled probability, for the flicker.
* Both use the existing `s_lightningGrad` (white → violet → dark purple fade-out), same gradient `SpawnProjectileTrail(EFFECT_PRESET_LIGHTNING_IMPACT, ...)` uses, so cast/impact/flight-stage lightning colors stay consistent across a skill's lifecycle.

### Damage Volume
```c
typedef enum { SHAPE_CIRCLE, SHAPE_BOX, SHAPE_CONE } ShapeType;
typedef struct {
    ShapeType shape; Vector3 center; float radius;
    float damagePerSecond; float tickInterval; float duration;
    bool active; float timer; float tickTimer;
} DamageVolume;

void DamageVolume_Init(void);
void DamageVolume_Update(float dt);
int  SpawnDamageVolume(DamageVolume config); // Trả về ID
void DamageVolume_Unload(void);
```

### Skill Timeline
```c
typedef struct { float current; float duration; } SkillTimeline;
void Timeline_Start(SkillTimeline *t, float duration);
bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt); // true đúng 1 frame khi đến giờ
bool Timeline_Finished(SkillTimeline *t);
```
Dùng để orchestrate chuỗi sự kiện nhiều bước mà không cần state machine thủ công.

### Layered Timeline (staggered multi-layer schedule)
```c
#define TIMELINE_MAX_LAYERS 8

typedef struct {
    const char *tag;
    float start;
    float duration; // >0: continuous window. ~0: one-shot event.
} TimelineLayer;

typedef struct {
    float current;
    TimelineLayer layers[TIMELINE_MAX_LAYERS];
    int layerCount;
} LayeredTimeline;

void  Timeline_LayeredStart(LayeredTimeline *t);
bool  Timeline_AddLayer(LayeredTimeline *t, const char *tag, float start, float duration);
bool  Timeline_IsLayerActive(const LayeredTimeline *t, int layerIndex);
float Timeline_LayerProgress(const LayeredTimeline *t, int layerIndex);
bool  Timeline_LayerEvent(const LayeredTimeline *t, int layerIndex, float dt);
```
One declarative `{tag, start, duration}` table for staggering N visual layers (Trail/Light/Smoke/Decal/...) instead of hand-written `if (t > X && t < Y)` blocks per layer — see `WUXING_ART_DIRECTION.md` Chapter 4.4 ("Layer Activation Timeline").
* **Same convention as `SkillTimeline`:** caller advances `t->current += dt` themselves each frame — nothing here ticks time internally.
* **Continuous windows** (`duration > 0`): `Timeline_IsLayerActive` returns true while `current` is inside `[start, start+duration)`; `Timeline_LayerProgress` returns 0..1 progress within that window (feeds into `FloatCurve_Sample` for that layer's own envelope). Clamped outside the window (0 before start, 1 after end).
* **One-shot events** (`duration ~0`): use `Timeline_LayerEvent` instead — fires true for exactly one frame when `current` crosses `start`, same edge-detection as `Timeline_Event(t, triggerTime, dt)`.
* **`AddLayer`:** returns `false` past `TIMELINE_MAX_LAYERS` (same fixed-cap pattern as `ColorGradient_AddStop`/`FloatCurve_AddStop`). No malloc, static array.

### Particle Emitter Preset
```c
typedef enum { EMITTER_FIRE, EMITTER_SNOW, EMITTER_WATER_SPURT, EMITTER_SHOCKED_SPARKS,
               EMITTER_WOOD_LEAVES, EMITTER_EARTH_DUST, EMITTER_METAL_SPARKS, EMITTER_TAIJI_MOTES } EmitterPreset;
void EmitterSystem_Init(void);
void EmitterSystem_Update(float dt);
int  Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration);
void Emitter_Stop(int emitterId);
void EmitterSystem_Unload(void);
```

### Mesh Preset
```c
typedef enum { MESH_PRESET_DISC, MESH_PRESET_RING, MESH_PRESET_CONE, MESH_PRESET_TORNADO,
               MESH_PRESET_CYLINDER, MESH_PRESET_SPHERE, MESH_PRESET_SHOCKWAVE,
               MESH_PRESET_PYRAMID, MESH_PRESET_TETRAHEDRON } MeshPresetType;
void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color);
```

### Shader Material Preset
```c
typedef enum { MATERIAL_FIRE, MATERIAL_ICE, MATERIAL_WATER, MATERIAL_PORTAL,
               MATERIAL_CUSTOM } MaterialPreset; // MATERIAL_CUSTOM set by Material_LoadCustom()

typedef struct {
    Color    baseColor;          // primary tint; also drives rim glow + dissolve edge glow
    float    rimStrength;        // 0..~2, rim/edge glow brightness (Fresnel-weighted, light-facing biased)
    float    fresnelPower;       // 1..8, rim sharpness (higher = thinner edge)
    float    emissiveIntensity;  // 0..~3, self-illumination boost added to base color
    float    distortionStrength; // 0..1, vertex wobble amount
    float    translucency;       // 0..1: 0 = opaque (alpha = baseColor.a), 1 = glass/tube-style
                                  // fresnel-driven alpha (center see-through, edges more solid)
    Texture2D texture1;          // optional secondary detail/mask texture; id==0 = unused
} EffectMaterialParams;

typedef struct {
    Shader shader;
    MaterialPreset preset;
    int uTimeLoc, uDissolveLoc, uBaseColorLoc, uTranslucencyLoc, uRimStrengthLoc,
        uFresnelPowerLoc, uEmissiveIntensityLoc, uDistortionStrengthLoc, uHasTexture1Loc, uTexture1Loc;
    EffectMaterialParams params;
} EffectMaterial;

EffectMaterial Material_Load(MaterialPreset preset);       // 4 hardcoded presets, effect_material-backed
EffectMaterial Material_LoadCustom(EffectMaterialParams params); // parametrized shared shader
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
void Material_Begin(EffectMaterial mat);
void Material_End(void);
```
* **`Material_Load` (4 presets):** all presets are `effect_material`-backed (Item 17) — each is a hardcoded `EffectMaterialParams` over the same shared `core/shaders/effect_material.vs/.fs` that `Material_LoadCustom` uses (the old per-skill shaders these presets borrowed were deleted from the repo). Signature and enum unchanged. Preset params: FIRE = `ELEMENT_COLOR_FIRE`, rim 1.2/fresnel 3/emissive 1.5/distortion 0.4/translucency 0; ICE = pale blue `(170,220,255)`, rim 1.5/fresnel 5/emissive 0.5/distortion 0.05/translucency 0.6; WATER = `ELEMENT_COLOR_WATER`, rim 1.0/fresnel 4/emissive 0.6/distortion 0.25/translucency 0.85; PORTAL = `ELEMENT_COLOR_TAIJI`, rim 2.0/fresnel 2/emissive 2.0/distortion 0.6/translucency 0.3.
* **`Material_LoadCustom` (new):** always backed by the shared `core/shaders/effect_material.vs/.fs` — one shader, look configured entirely via `EffectMaterialParams` uniforms (`u_baseColor`, `u_rimStrength`, `u_fresnelPower`, `u_emissiveIntensity`, `u_distortionStrength`, `u_translucency`, optional `texture1`). No new GLSL needed per combination.
* **Rim glow is weighted by light-facing direction**, not view angle alone: plain Fresnel glows evenly around the whole silhouette regardless of where the light is, which reads as "rim doesn't match the light". `rim = fresnel * mix(0.3, 1.0, max(dot(normal, lightDir), 0.0))` — dimmed (not zeroed) on the backlit side.
* **`translucency`** (default 0 = opaque, unchanged from initial implementation): set to `1.0` for the same "center see-through, edges more solid" look as `tube.fs` (`alpha = mix(0.3, 0.9, fresnel)`), driven by the same fresnel term as the rim. **Caller must wrap the draw in `BeginBlendMode(BLEND_ALPHA)`/`EndBlendMode()`** for alpha < 1 to actually blend — `Material_Begin`/`Material_End` do not manage blend mode themselves.
* **This shader ignores per-vertex color** (`vs_header.glsl`/`fs_header.glsl`'s 3D-lighting convention doesn't carry a `fragColor` varying) — tint comes only from `u_baseColor`. The `Color` argument passed to whatever mesh-draw call you use inside `Material_Begin`/`Material_End` has no visual effect with this material.
* **`texture1` is optional** — `EffectMaterialParams.texture1.id == 0` skips the sample entirely (guarded by `u_hasTexture1` in the shader) rather than sampling an unbound/stale texture unit. Sampled as a luminance mask (`.r` channel only, not `.rgb`) — importing the texture's own hue directly onto a mesh with very different UV density than what it was authored for (e.g. a flat ground-decal crack texture on a sphere, which pinches hard at the poles) produces visible color noise.
* **`Material_SetFloat`** still works unmodified on `Material_LoadCustom` materials for any uniform name, including animating `u_dissolve` frame-to-frame (see `core_test`'s usage: solid hold, then dissolve out over the last second). Dissolve's edge-glow only evaluates once `u_dissolve > 0.0` — `fx.glsl`'s `dissolveCalc()` computes a nonzero `edgeFactor` for ~8% of fragments even at `dissolve == 0.0`, which would otherwise show as speckle the instant the material appears.

### Ground Decal Preset
```c
typedef enum {
    // Earth
    DECAL_PRESET_CRACK, DECAL_PRESET_EARTH_SHATTER, DECAL_PRESET_EARTH_RUNE,
    // Fire
    DECAL_PRESET_BURN, DECAL_PRESET_FIRE_LAVA,
    // Water
    DECAL_PRESET_WATER, DECAL_PRESET_WATER_SPLASH, DECAL_PRESET_WATER_RIPPLE, DECAL_PRESET_ICE,
    // Wood
    DECAL_PRESET_WOOD_ROOT, DECAL_PRESET_WOOD_MOSS,
    // Metal
    DECAL_PRESET_METAL_SLASH, DECAL_PRESET_METAL_CRATER, DECAL_PRESET_METAL_RUNE,
    // Taiji
    DECAL_PRESET_TAIJI_RING, DECAL_PRESET_TAIJI_LIGHTNING, DECAL_PRESET_TAIJI_WIND,
    // Generic — untinted, caller may apply its own Color
    DECAL_PRESET_GENERIC_IMPACT_RING, DECAL_PRESET_GENERIC_GLOW, DECAL_PRESET_GENERIC_SHADOW
} DecalPresetType;
void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration);
```
* All 6 elements now have at least 2 ground-mark presets; each (except GENERIC_*) is pre-tinted via its `ELEMENT_COLOR_*` macro inside `SpawnGroundDecal` — caller does not pass a `Color`.
* Backing textures live under `assets/textures/decals/` (per-element marks) and `assets/textures/generic/` (untinted, reusable across elements: `impact_ring.png`, `glow_circle.png`, `shadow_blob.png`).
* `DECAL_PRESET_CRACK`/`BURN`/`ICE`/`WATER` are the original 4 presets, kept for call-site compatibility with existing skills — `ICE` now points to a real frost texture (`decal_frost_ring.png`) instead of the old `dust_wind.png` placeholder.
* `DECAL_PRESET_FIRE_LAVA` and `DECAL_PRESET_WATER_RIPPLE` use `DecalSystem_AddFlowEx` internally (radial outward scroll, `flowSpeed=0.6, flowStrength=0.8`) — see "Ground Decals" section above. Every other preset still calls plain `DecalSystem_Add` (static).

### ForceField Preset
```c
typedef enum { FORCE_PRESET_FIRE_UPDRAFT, FORCE_PRESET_SNOW_BLIZZARD, FORCE_PRESET_WATER_VORTEX } ForceFieldPreset;
ForceField ForceField_CreatePreset(ForceFieldPreset preset);
```

### Skill Builder (chainable context)
```c
typedef struct { Vector3 target; float scale; bool hasExplosion; EffectPresetType explosionEffect;
                 bool hasDecal; DecalPresetType decalType; float decalRadius; float decalDuration;
                 bool hasDamageVolume; float damageRadius; float damageDps; float damageDuration; } SkillBuildContext;
void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale);
void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx);
void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration);
void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration);
void SkillBuilder_Build(SkillBuildContext *ctx); // Gọi cuối để kích hoạt tất cả

void SkillBuilder_AddCastEffect(SkillBuildContext *ctx, EffectPresetType preset); // Cast-stage hook, own trigger point
```
`SkillBuilder_AddCastEffect` has its own trigger point — call it at **cast time** (after `SkillBuilder_Start`, which sets `ctx->target`/`ctx->scale`), separately from `SkillBuilder_Build()` which fires at **impact time**. It fires `SpawnCastEffect` immediately rather than deferring into `ctx`, since cast and impact happen at different points in a skill's lifecycle and the builder's other `Add*` calls all defer to `Build()`.

### SkillBuilder — One-line Archetype Spawns

Immediate, duration-based, no malloc. Static internal pools — see §17 for pool sizes.

```c
// Beam: element-tinted managed ray + VFXLight at endpoints
int  SkillBuilder_SpawnBeam(Vector3 from, Vector3 to, EffectPresetType element,
                             float width, float duration);
void SkillBuilder_KillBeam(int handle);  // early termination

// GroundWave: expanding shockwave mesh + decal scroll emitter marching along dir
// Static pool of 8.
void SkillBuilder_SpawnGroundWave(Vector3 origin, Vector3 dir, EffectPresetType element,
                                  float range, float speed);

// Orbitals: N tetrahedra orbiting center with random phase/scale (anti-robotic law)
// Static pool: 8 groups × 8 orbitals.
int  SkillBuilder_SpawnOrbitals(Vector3 center, EffectPresetType element,
                                int count, float radius, float duration);

// AuraRing: looping emitter ring (K points on circle) + glow decal
// Static pool of 8.
int  SkillBuilder_SpawnAuraRing(Vector3 center, EffectPresetType element,
                                float radius, float duration);
void SkillBuilder_KillAuraRing(int handle);

// Drive all internal pools — called by SkillHelper_Update, not directly by skills.
void SkillBuilder_Update(float dt);
void SkillBuilder_DrawWorld(Camera3D cam);
```

- Beams wrap `SpawnProcRay` + `VFXLight_Spawn` at endpoints.
- `SkillBuilder_Update`/`DrawWorld` are driven by `SkillHelper_Update` (wired in `main.c`) — skills must **not** call them directly.

### Chain-Targeting Helper

```c
// Returns count of chain points (0 = no targets in range).
// outPoints[0] = origin, outPoints[1..] = jump targets (nearest not already hit).
// Damage is the skill's job — this helper handles visuals only.
int  SkillHelper_ChainTargets(Vector3 origin, float jumpRadius, int maxJumps,
                               Vector3 *outPoints, int maxOut);

// Fires SpawnLightningTrail per hop, staggered by hopDelay seconds.
// Internal static queue (32 entries); driven by SkillHelper_Update.
void SpawnChainLightning(const Vector3 *points, int count, float scale, float hopDelay);

// Drives chain queue + SkillBuilder pools. Call once per frame in main update.
// Already wired in main.c — skills do not call this directly.
void SkillHelper_Update(float dt);
```

Depends on `SkillManager_SetNearbyTargetsProvider` being registered (done automatically by `Entity_Init`).

---

## 10. GLSL Shader Guidelines

### `#include` Is an Engine Preprocessing Step, Not Native GLSL

> [!IMPORTANT]
> GLSL has no native `#include` directive. The `#include "core/shaders/common/..."` lines below are resolved by **`shader_preprocessor.h/.c`** (`ShaderPreprocessor_Load()`), which is wired into `ResourceManager_LoadShader()`. It recursively reads the file, textually substitutes every `#include "..."` line with the target file's contents (up to `MAX_INCLUDE_DEPTH`), and only then hands the fully-expanded source to `LoadShaderFromMemory()`. The resulting buffer is heap-allocated with `RL_MALLOC`/freed with `RL_FREE` internally — skill code never touches this buffer and never calls `ShaderPreprocessor_Load()` directly; it is invoked automatically by `ResourceManager_LoadShader()`.
>
> Practical implication: a raw `glCompileShader` call (or any tool that lints `.vs`/`.fs` files standalone, e.g. an online GLSL validator) will fail on the `#include` line because it isn't valid core GLSL — this is expected and not a project bug. Only `ResourceManager_LoadShader()` produces compilable output.

### Common Shader Files — Tổng quan

| File | Dùng trong | Cung cấp |
|---|---|---|
| `vs_header.glsl` | Mọi `.vs` | Attributes, uniforms, varyings, `VS_FinalOutput()` |
| `fs_header.glsl` | Mọi `.fs` | Varyings nhận, uniforms môi trường, `finalColor` |
| `lighting.glsl` | `.fs` cần chiếu sáng | `perturbNormal`, `calcFresnel`, `calcSpecular`, `calcDiffuse` |
| `noise.glsl` | `.vs` / `.fs` cần nhiễu | `hash2`, `hash3`, `vnoise`, `fbm2`, `fbm2N` |
| `fx.glsl` | `.fs` cần hiệu ứng | `dissolveCalc`, `flowBlend`, `emissiveMask` |
| `triplanar.glsl` | `.fs` cho mesh không có UV ổn định | `triplanarWeights`, `triplanarNoise`, `triplanarSample` |

**Quy tắc include:**
- Luôn include theo thứ tự: `fs_header.glsl` → `noise.glsl` (nếu cần) → `lighting.glsl` → `fx.glsl` → `triplanar.glsl` (nếu cần, phụ thuộc `noise.glsl` cho `triplanarNoise`)
- `fx.glsl` không phụ thuộc `noise.glsl` — có thể include riêng lẻ hoặc cùng nhau
- Không tái implement hash/noise/fbm/dissolve/flow blend/triplanar trong skill code

### Triplanar Mapping (`core/shaders/common/triplanar.glsl`)

Giải quyết Item 4a (`CORE_ISSUES.md`): các `ProceduralMesh_Draw*` (Rock, ShardCluster, Fissure, VortexFunnel) vẽ qua `rlBegin`/`rlEnd` immediate-mode — chỉ có position + normal, **không có texcoord** — nên UV-based texturing sẽ stretch/streak trên facet jagged. Triplanar chiếu texture/pattern từ 3 mặt phẳng trục world-space (X/Y/Z) và blend theo world normal thay vì dùng UV.

```glsl
vec3 triplanarWeights(vec3 worldNormal, float sharpness);              // sharpness 2.0-6.0
float triplanarNoise(vec3 worldPos, vec3 weights, float scale);        // procedural, không cần texture asset
vec4 triplanarSample(sampler2D tex, vec3 worldPos, vec3 weights, float scale); // texture asset thật
```

Pattern dùng trong `main()`:
```glsl
vec3 w = triplanarWeights(fragNormal, 4.0);
float pattern = triplanarNoise(fragPosition, w, 0.05); // hoặc triplanarSample(myTex, fragPosition, w, 0.02)
```

> [!NOTE]
> `scale` là tần số chiếu world-space (không phải UV [0,1]) — giá trị nhỏ (0.01-0.05) cho mesh lớn, lớn hơn (0.05-0.1) cho mesh nhỏ/chi tiết. Tune bằng mắt theo kích thước thực tế của mesh.

### Required Includes

Vertex Shader

```glsl
#version 330
#include "core/shaders/common/vs_header.glsl"
```

Fragment Shader — 3D mesh cần chiếu sáng đầy đủ:

```glsl
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"    // nếu cần hash/fbm
#include "core/shaders/common/lighting.glsl"  // perturbNormal, calcFresnel, calcSpecular, calcDiffuse
#include "core/shaders/common/fx.glsl"        // dissolveCalc, flowBlend, emissiveMask
```

Fragment Shader — tối giản (chỉ dissolve, không cần lighting 3D):

```glsl
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"
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

Provided automatically by the common headers — **do not redeclare** any of these in a skill `.vs`/`.fs`.

**From `vs_header.glsl` (vertex shader only):**

| Variable | Direction | Type | Space / Notes |
|---|---|---|---|
| `vertexPosition` | `in` (attribute) | `vec3` | Object/local space — raw mesh vertex |
| `vertexTexCoord` | `in` (attribute) | `vec2` | Raw mesh UV |
| `vertexNormal` | `in` (attribute) | `vec3` | Object/local space — raw mesh normal, **not yet normalized or transformed** |
| `mvp` | `uniform` | `mat4` | Model-View-Projection — used internally by `VS_FinalOutput()` |
| `matModel` | `uniform` | `mat4` | Model matrix — used internally by `VS_FinalOutput()` |
| `u_time` | `uniform` | `float` | Auto-bound by `SkillManager_BeginShader()` — do not set manually |
| `viewPos` | `uniform` | `vec3` | Camera world-space position — auto-bound |
| `u_resolution` | `uniform` | `vec2` | Screen resolution — auto-bound |
| `fragPosition` | `out` (varying) | `vec3` | **World-space.** Written only by `VS_FinalOutput()` |
| `fragTexCoord` | `out` (varying) | `vec2` | Passthrough of `vertexTexCoord`, written by `VS_FinalOutput()` |
| `fragNormal` | `out` (varying) | `vec3` | **World-space, normalized.** Written by `VS_FinalOutput()` |

**From `fs_header.glsl` (fragment shader only):**

| Variable | Direction | Type | Space / Notes |
|---|---|---|---|
| `fragPosition` | `in` (varying) | `vec3` | **World-space** — matches VS output exactly |
| `fragTexCoord` | `in` (varying) | `vec2` | UV, passed through unchanged from VS |
| `fragNormal` | `in` (varying) | `vec3` | **World-space, normalized** |
| `u_time` | `uniform` | `float` | Auto-bound — do not set manually |
| `viewPos` | `uniform` | `vec3` | Camera world-space position — auto-bound |
| `u_resolution` | `uniform` | `vec2` | Auto-bound |
| `u_lightDir` | `uniform` | `vec3` | Real environment sun direction, pre-negated to point *toward* the light — auto-bound **only if the skill uses `SkillManager_BeginShader()`**; skills calling raw `BeginShaderMode()` must set it manually (see note below) |
| `finalColor` | `out` | `vec4` | Final pixel output — write exactly once per `main()` |

> [!NOTE]
> **`fragNormal` caveat:** `VS_FinalOutput()` computes `fragNormal` from the **original** `vertexNormal` (`normalize(matModel * vec4(vertexNormal, 0.0))`) — it does **not** recompute the normal from a displaced surface. If your vertex shader displaces position (e.g. `tube.vs`'s `getDisplacement()`), the outgoing `fragNormal` will *not* reflect that displacement. This is why skills like the Water Stream tube re-derive a perturbed normal in the **fragment** shader via `perturbNormal()` using a matching height-field gradient, rather than relying on a geometrically displaced normal from the VS. If a skill needs a true displaced-geometry normal, it must compute it manually in the VS (e.g. via finite-difference neighboring vertices) — `VS_FinalOutput()` will not do this automatically.

### Built-in Functions

#### `lighting.glsl` — Chiếu sáng 3D

```glsl
vec3  perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength);
float calcFresnel(vec3 normal, vec3 viewDir, float power);
float calcSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess);
float calcDiffuse(vec3 normal, vec3 lightDir, float ambient);
```

* **`perturbNormal(baseNormal, heightDelta, strength)`** — Perturbs a base normal using the gradient of a skill-supplied height field, to fake surface roughness (water ripples, lava bubbling, bark texture...) without extra geometry.
  - `baseNormal`: the mesh normal to perturb — typically `fragNormal` (already world-space, normalized).
  - `heightDelta`: `vec2(h(u-eps) - h(u+eps), h(v-eps) - h(v+eps))` — the **gradient** of your own height function, sampled at `±eps` around the current `fragTexCoord` in U and V respectively. The skill must implement its own height function (e.g. `tube.fs`'s `getIrregularity()`) and **must reuse the exact same formula as the vertex shader's displacement function**, or lighting and physical displacement will visually mismatch.
  - `strength`: deformation intensity, **typical range 0.3 – 0.8**.
* **`calcFresnel(normal, viewDir, power)`** — Schlick-approximated rim term, returns `[0..1]` (`0` = surface viewed face-on, `1` = viewed edge-on). **Typical power: 2.0 – 5.0.**
* **`calcSpecular(normal, lightDir, viewDir, shininess)`** — Blinn-Phong specular highlight, returns `[0..1]` — caller scales bằng intensity (e.g. `* 5.0`). **Typical shininess: 32 – 512.**
* **`calcDiffuse(normal, lightDir, ambient)`** — Lambertian diffuse với ambient floor, trả về `[ambient..1.0]`.
  - `ambient`: ánh sáng nền tối thiểu, thường `0.10 – 0.25`.
  - Nhân trực tiếp vào baseColor: `baseColor *= calcDiffuse(normal, lightDir, 0.15);`

**`lightDir` chuẩn của project** (hard-code trong mọi skill):
```glsl
vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
```

---

#### `noise.glsl` — Nhiễu ngẫu nhiên

```glsl
float hash2(vec2 p);                    // 2D hash → [0, 1]
float hash3(vec3 p);                    // 3D hash → [0, 1]
float vnoise(vec2 p);                   // 2D value noise → [0, 1]  (tên "vnoise" tránh conflict GLSL built-in noise2)
float fbm2(vec2 p);                     // 3-octave FBM → [0, ~1]
float fbm2N(vec2 p, int octaves);       // N-octave FBM, 1–6 → [0, 1] normalized
```

* **`hash2 / hash3`** — Pseudo-random hash. `hash3` dùng cho dissolve theo world-space: `hash3(floor(fragPosition * scale))`.
* **`vnoise`** — Value noise, nhanh hơn Perlin. Dùng làm base cho FBM hoặc UV warp trực tiếp. (Không dùng tên `noise2` — GLSL built-in conflict.)
* **`fbm2`** — 3-octave FBM, dùng trong đa số VFX (lửa, plasma, vân sóng). Có built-in rotation để tránh axis-aligned artifacts.
* **`fbm2N`** — Khi cần kiểm soát chi tiết: 1–2 octave cho gió mềm/hào quang, 5–6 cho vỏ cây/đá.

```glsl
// Ví dụ: UV warp theo FBM để làm gió uốn xoắn
vec2 flow = vec2(u_time * 0.4, -u_time * 0.6);
float distort = fbm2(vec2(localU, localV) * 8.0 + flow);
vec2 warpedUV = uv + (distort - 0.5) * 0.008;
```

---

#### `fx.glsl` — Hiệu ứng VFX

```glsl
float dissolveCalc(float noiseVal, float dissolve, float edgeWidth, out float edgeFactor);
float flowBlend(sampler2D tex, vec2 uv, vec2 flowDir, float speed, float strength, float time);
float emissiveMask(vec3 worldPos, float freq, float threshold);
```

* **`dissolveCalc`** — Noise-based dissolve + viền cháy sáng. Trả về `1.0` nếu pixel bị xóa, `0.0` nếu giữ lại. `edgeFactor` (out) là mức độ viền để mix màu element.
  ```glsl
  // Pattern chuẩn — include noise.glsl trước:
  float n = hash3(floor(fragPosition * 10.0));
  float edgeFactor;
  if (dissolveCalc(n, u_dissolve, 0.08, edgeFactor) >= 1.0) discard;
  baseColor = mix(baseColor, vec3(1.0, 0.5, 0.1), edgeFactor); // viền lửa ví dụ
  ```

* **`flowBlend`** — Flow map 2-phase blend chống giật (không seam khi phase reset). Trả về `float` luminance của texture sau blend.
  ```glsl
  vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
  float intensity = flowBlend(causticsTex, fragTexCoord * 2.0, flowDir, 1.2, 0.05, u_time);
  baseColor += waterColor * intensity * 1.5;
  ```

* **`emissiveMask`** — Sine-based emissive từ world position — không bị kéo méo theo UV. Dùng cho nhựa cây, mạch năng lượng, rạn nứt phát sáng.
  ```glsl
  float mask = emissiveMask(fragPosition, 1.5, 0.88);
  baseColor += elementGlowColor * mask * 2.5;
  ```

Do not reimplement these functions.

> [!NOTE] **Resolved (CORE_ISSUES.md Item 10).** `u_lightDir` is now a real
> auto-bound uniform (added to `fs_header.glsl`, table above) —
> `SkillManager_BeginShader()` sets it to `-Environment_GetSunDirection()`
> (negated: the environment API returns the direction light *travels*, Y
> negative; shaders' `dot(normal, lightDir)` convention needs the direction
> *toward* the light, Y positive). **Do not hard-code
> `normalize(vec3(0.5, 0.8, 0.5))` in new skills** — use `normalize(u_lightDir)`
> instead so rim/diffuse lighting actually matches the environment's real
> sun direction (confirmed previously mismatched by comparing against a
> character's cast shadow). `tube.fs`, `stone_prison.fs`, `water_sphere.fs`,
> and `effect_material.fs` were migrated as part of this fix.
>
> **If your skill calls raw `BeginShaderMode()` instead of
> `SkillManager_BeginShader()`** (check your skill's `Draw` function —
> several existing skills do, e.g. `tube_skill.c`, `stone_prison_skill.c`),
> the auto-bind does **not** reach your shader. You must fetch
> `GetShaderLocation(shader, "u_lightDir")` yourself in `Init[Name]Skill`
> and call `SetShaderValue(shader, loc, &lightDir, SHADER_UNIFORM_VEC3)`
> with `lightDir = Vector3Negate(Environment_GetSunDirection())` each frame
> you draw — same pattern as `viewPos`/`u_camPos` in those two files.

### Custom Per-Skill Uniforms (e.g. `u_uvLength`, `u_dissolve`)

Skill-specific uniforms (anything not in the built-in tables above) are **not** handled by `SkillManager_BeginShader()` — the skill's own C code is responsible for sending them.

* **Lookup:** Cache the uniform location once, typically as a `static int` next to the shader, fetched in `Init[Name]Skill()` via `GetShaderLocation(shader, "u_uvLength")`. Do not call `GetShaderLocation` every frame — it's a string-hash lookup the engine does not cache for you.
* **Set timing:** Call `SetShaderValue()` for skill-specific uniforms **after** `SkillManager_BeginShader(shader)` (so the shader is bound) and **before** the draw call that uses them, every frame the value changes (e.g. `u_dissolve` ramping toward `1.0`) or once if constant for the skill's lifetime (e.g. `u_uvLength`, fixed at cast-time from the Bezier path length).
* **VS/FS synchronization:** If the same uniform name (e.g. `u_uvLength`) is declared in **both** `.vs` and `.fs` (as in the Water Stream sample), `SetShaderValue()` must be called **once** with that uniform's location for the shader program as a whole — raylib's `Shader.id` is one linked GL program covering both stages, so one `SetShaderValue()` call updates the value for both VS and FS reads of the same uniform name. There is no need (and no mechanism) to set it "twice, once per stage."
* **Declaration:** Declare these uniforms only in the `.vs`/`.fs` file(s) that read them — e.g. `u_uvLength` appears in both `tube.vs` and `tube.fs` because both need it; `u_dissolve` appears only in `tube.fs` because only the fragment shader uses it for fade-out.

### Rules

- Always use both `.vs` and `.fs` for 3D shaders.
- Include `fs_header.glsl` before `lighting.glsl`.
- Call `VS_FinalOutput()` as the final step of every vertex shader.
- Declare only skill-specific uniforms.
- Keep shader logic focused on the visual behavior of the element.
- Strict Parameter Requirement: The core engine's final vertex output function MUST receive exactly one vec3 argument representing the final processed or displaced vertex position.

### Android / GLES Compatibility Rules

Build Android chạy trên OpenGL ES. Pipeline dùng **hai path** tùy shader có dùng `#include` hay không.

#### Path 1 — Standalone shaders (không có `#include`)

`scripts/convert_shaders_to_gles.py` chạy lúc build APK, convert sang **GLES 1.00 (`#version 100`)**:

| Desktop GLSL 3.3 | GLES 1.00 (sau build script) |
|---|---|
| `in vec3 pos` (VS) | `attribute vec3 pos` |
| `out vec3 vary` (VS) | `varying vec3 vary` |
| `in vec3 vary` (FS) | `varying vec3 vary` |
| `out vec4 finalColor` + mọi dùng `finalColor` | xóa khai báo + đổi thành `gl_FragColor` |
| `texture(sampler, uv)` | `texture2D(sampler, uv)` |
| precision (FS) | tự inject `precision highp float;` nếu chưa có |

Build script **KHÔNG** tự sửa: `f` suffix trên float literal, precision cho `.vs`, nội dung `#include`.

> Yêu cầu GLES 2.0+ (Android 2.2+, tất cả thiết bị target).

#### Path 2 — Shaders dùng `#include` common headers

Build script **BỎ QUA** — các file này giữ nguyên `#version 330` trong APK.

Ở runtime, `ResourceManager_LoadShader` → `ShaderPreprocessor_Load`:
1. Mở rộng đệ quy mọi `#include "..."` (ví dụ `vs_header.glsl`, `lighting.glsl`)
2. `RewriteVersionForGLES()` đổi `#version 330` → `#version 300 es`
3. Kết quả: source GLES 3.0 với `in`/`out`/`texture()` — hợp lệ

Common headers (`vs_header.glsl`, `fs_header.glsl`, `lighting.glsl`, `noise.glsl`, `fx.glsl`) **đã có** `#ifdef GL_ES precision highp float; #endif` — không cần khai báo thêm trong skill shader. Cả VS lẫn FS đều dùng `highp float` (quan trọng — xem Rule E).

> Yêu cầu GLES 3.0+ (Android 4.3+, toàn bộ thiết bị hiện đại).

---

**Rule A — Không dùng `f` suffix trên float literal (áp dụng cho CẢ HAI path):**

```glsl
// SAI — Android GLES compiler từ chối, build script KHÔNG tự sửa:
float breathe = 1.25f + 0.12f * sin(u_time * 5.5);

// ĐÚNG:
float breathe = 1.25 + 0.12 * sin(u_time * 5.5);
```

`f` suffix là cú pháp C. Desktop driver bỏ qua; Android GLES strict compiler từ chối → `shader.id = 0`.

**Rule B — Standalone VS phải tự khai báo precision:**

Build script tự inject precision cho standalone `.fs`, nhưng **không** làm với `.vs`. Mọi standalone vertex shader (không có `#include "core/shaders/common/vs_header.glsl"`) phải thêm:

```glsl
#version 330

#ifdef GL_ES
precision highp float;
#endif
```

Shader dùng common headers → precision đã có trong `vs_header.glsl`/`fs_header.glsl`, không cần khai báo lại.

**Rule C — Behavior khi shader compile thất bại trên Android:**

`ResourceManager_LoadShader` **không crash** khi shader compile fail — trả về `shader.id = 0` và log:
```
SHADER: compile failed, not caching (vs=... fs=...)
```
`SkillManager_BeginShader` guard `id == 0` → no-op (bỏ qua `BeginShaderMode`). Skill vẫn chạy nhưng render với default flat shader → mesh trông **trắng toát / không có hiệu ứng**.

Khi thấy chiêu render trắng toát trên Android: kiểm tra logcat dòng trên, sửa theo Rule A/B, rebuild APK.

**Rule D — `matModel` phải được set thủ công khi dùng rlgl immediate mode:**

`VS_FinalOutput()` trong `vs_header.glsl` tính `fragNormal = normalize(matModel * vertexNormal)`. Raylib chỉ upload `matModel` khi dùng `DrawMesh`/`DrawModel` — **không** upload khi dùng rlgl immediate mode (`rlBegin`/`rlEnd`/`ProceduralMesh_DrawTube`...).

Trên Android GLES 3.0, `matModel` giữ giá trị **all-zeros** → `normalize(vec3(0,0,0))` = undefined (NaN trên Adreno/Mali) → `fragNormal = NaN` → `clamp(NaN, 0, 1) = 1.0` → màu trắng. Trên Mac desktop, OpenGL driver xử lý normalize(zero) khác (trả về identity-ish) nên không thấy lỗi.

**`SkillManager_BeginShader` tự động set `matModel = identity` trước `BeginShaderMode`.** Skill code không cần làm gì thêm nếu dùng `SkillManager_BeginShader`.

> [!IMPORTANT] **Bug đã sửa (2026-06-30):** bản fix trước đây dùng `shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0` để kiểm tra location hợp lệ — đây là cách kiểm tra SAI. Raylib's `LoadShaderFromMemory` chỉ auto-bind một danh sách uniform mặc định cố định (`mvp`, `colDiffuse`, `texture0`, vertex attribs...); `matModel` không nằm trong danh sách đó, nên slot `shader.locs[SHADER_LOC_MATRIX_MODEL]` không bao giờ được ghi và giữ giá trị `0` từ `RL_CALLOC` ban đầu. `0` vẫn pass `>= 0` dù **không phải vị trí thật của `matModel`** → `SetShaderValueMatrix` ghi đè nhầm vào bất kỳ uniform nào khác thực sự nằm ở location 0 trong chương trình GLSL đã link (ví dụ một `sampler2D texture0` khai báo riêng) → vỡ texture binding / giá trị uniform đó → mesh có thể hiện toàn màu trắng dù hình dạng vẫn đúng. Đã xác nhận qua `tsunami_skill` (FlowMap's `texture0` bị ghi đè bởi identity matrix). `core/skill_manager.c`'s `SkillManager_BeginShader` đã được sửa để dùng `GetShaderLocation(shader, "matModel")` (tra theo tên, trả về `-1` thật nếu không tồn tại) thay vì đọc `shader.locs[SHADER_LOC_MATRIX_MODEL]`.

Nếu skill gọi `BeginShaderMode` trực tiếp (bypass `SkillManager_BeginShader`), PHẢI set matModel thủ công — và PHẢI tra location bằng tên, không dùng `shader.locs[SHADER_LOC_MATRIX_MODEL]`:

```c
// Trước draw call, sau BeginShaderMode():
int matModelLoc = GetShaderLocation(s_shader, "matModel");
if (matModelLoc >= 0) {
    Matrix identity = MatrixIdentity();
    SetShaderValueMatrix(s_shader, matModelLoc, identity);
}
```

**Rule E — VS và FS phải dùng cùng precision cho mọi shared uniform (GLES 3.x strict):**

Trên GLES 3.x strict implementations (Mali-G68, GLES 3.2), nếu một uniform xuất hiện ở cả VS lẫn FS, cả hai phải có **cùng precision qualifier**. Nếu không khớp → link failure → `shader.id = 0` → màu trắng.

```
// Lỗi điển hình trong logcat:
// SHADER: [ID 14] Link error: L0001 The fragment floating-point variable u_time
//         does not match the vertex variable u_time. The precision does not match.
```

Common headers đã xử lý vấn đề này: cả `vs_header.glsl` và `fs_header.glsl` đều dùng `precision highp float` — do đó `u_time`, `viewPos`, `u_resolution` và mọi uniform khai báo theo default đều là `highp` ở cả hai stage.

Nếu skill tự khai báo uniform riêng (ví dụ `uniform float u_uvLength;`) trong cả `.vs` lẫn `.fs`, uniform đó sẽ inherit default precision — `highp` từ `vs_header.glsl` cho VS và `highp` từ `fs_header.glsl` cho FS → khớp, không có vấn đề.

Nếu skill tự khai báo precision mặc định thấp hơn (vd `precision mediump float;`) ở FS standalone, phải đảm bảo VS cũng dùng `mediump` — hoặc tốt hơn là dùng `highp` nhất quán ở cả hai.

> [!NOTE]
> Desktop OpenGL driver thường compile thành công kể cả khi có `f` suffix hay thiếu precision, và xử lý `normalize(zero)` khác mobile — lỗi thường chỉ xuất hiện khi test trên thiết bị Android thật (GLES strict mode).

---

---
## 11 3D Rendering & Shader Best Practices

### 11.1 Vertex Color Reset

Before drawing custom geometry with `rlBegin()`, always reset the vertex color:
```c
#include "rlgl.h"   // required for rlBegin/rlColor4ub/rlVertex3f/rlEnd — not implicitly pulled in by raylib.h
// ...
rlColor4ub(255, 255, 255, 255);
```
Otherwise the mesh may inherit colors from previous draw calls. `rlColor4ub` (and the rest of the `rl*` immediate-mode API) lives in `rlgl.h`, a separate header from `raylib.h` — a skill that only includes `raylib.h` will fail with `implicit declaration of function` at compile time, not an obvious "missing header" error.

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

---

## 13. Motion Controller (`#include "core/motion_controller.h"`)

Stateless projectile kinematics — no alloc, no global pool. Caller owns a `MotionState` (stack or skill-struct field).

```c
typedef enum {
    MOTION_LINEAR,     // constant velocity toward target
    MOTION_HOMING,     // steers toward target each frame (turnRateRad)
    MOTION_BALLISTIC,  // projectile arc with gravity
    MOTION_SPIRAL,     // spirals around the origin→target axis
    MOTION_ORBIT,      // circles a fixed orbitCenter point
    MOTION_BOOMERANG   // flies out to boomerangRange then returns to origin
} MotionType;

typedef struct {
    MotionType type;
    float speed;             // m/s travel speed
    float arrivalRadius;     // detonation threshold (m), default 0.2f
    float turnRateRad;       // HOMING: max turn per second (rad/s), e.g. 2.5f
    float gravity;           // BALLISTIC: downward accel (m/s²), e.g. 4.9f (half real gravity)
    float spiralRadius;      // SPIRAL: lateral displacement (m)
    float spiralFreq;        // SPIRAL: rotations per second
    Vector3 orbitCenter;     // ORBIT: center point
    float orbitRadiusXZ;     // ORBIT: radius in XZ plane (m)
    float orbitAngularSpeed; // ORBIT: rad/s
    float boomerangRange;    // BOOMERANG: outbound distance before return (m)
} MotionParams;

typedef struct {
    MotionParams params;
    Vector3 pos;       // current position — read each frame
    Vector3 vel;       // current velocity
    Vector3 target;    // destination
    Vector3 origin;    // initial position
    float elapsed;
    float orbitAngle;
    bool returning;    // BOOMERANG: true when heading back toward origin
} MotionState;

void Motion_Init(MotionState *s, MotionParams params, Vector3 startPos, Vector3 target);
void Motion_Step(MotionState *s, float dt);
bool Motion_Arrived(const MotionState *s);
```

> [!NOTE]
> Scale: `speed` in m/s (typical projectile: 8–20 m/s), `gravity` in m/s² (real gravity = 9.81; use 4.9 for floaty arc). `arrivalRadius` 0.2f fits a 0.15–0.20f mesh radius per §1 scale rules.

Typical usage pattern:
```c
// Skill struct:
MotionState motion;
// On cast:
Motion_Init(&motion, (MotionParams){ .type = MOTION_HOMING, .speed = 12.0f,
    .turnRateRad = 2.5f, .arrivalRadius = 0.3f }, startPos, target);
// Each Update frame:
Motion_Step(&motion, dt);
if (Motion_Arrived(&motion)) { /* trigger impact */ }
```

---

## 14. Status VFX & Afterimage (`#include "core/status_vfx.h"` / `#include "core/afterimage.h"`)

### Status/Aura VFX (`core/status_vfx.h`)

Looping element-tinted emitter + low-priority `VFXLight` that follow an agent each frame.

```c
#define MAX_STATUS_VFX 32
int  StatusVFX_Attach(int agentId, EffectPresetType element, float duration);
void StatusVFX_Detach(int handle);   // early removal (cleanse)
void StatusVFX_Update(float dt);     // call in main update loop
void StatusVFX_Draw(void);           // call in transparent draw pass
void StatusVFX_GetStats(int *active, int *max);
```

- Agent position queried each frame via `SkillManager_GetAgentPos(agentId)` — no entity dependency.
- Re-attaching the same element to the same agent **refreshes duration, does not stack**.
- When agent dies (provider returns false) or duration expires → 0.5 s fade-out, then auto-free.

> [!NOTE]
> Wire `StatusVFX_Update(dt)` in the main update loop and `StatusVFX_Draw()` in the transparent draw pass. Both are already wired in `main.c`.

### Mesh Afterimage (`core/afterimage.h`)

Ghost dissolve of a model at a frozen transform — for dash/blade trails.

```c
#define MAX_AFTERIMAGES 64
void Afterimage_Init(void);
void Afterimage_Spawn(Model model, Matrix transform, Color tint, float life);
void Afterimage_Update(float dt);
void Afterimage_Draw(void);   // BLEND_ALPHA, depth-write off
void Afterimage_GetStats(int *active, int *max);
```

- Stores a **reference** to `model` (not a copy) + frozen `Matrix` snapshot.
- Dissolves via `u_dissolve` ramp 0→1 over `life` seconds using the `effect_material` shader.
- Caller must **not** unload the model while any ghost referencing it is alive.
- Typical spawn cadence: one ghost every 0.04 s while dash/blade is active (caller-side timer).

---

## 15. SkillBuilder Archetypes & Chain-Targeting

Documented inline in §9b above. Quick-reference signatures:

- `SkillBuilder_SpawnBeam` / `SkillBuilder_KillBeam`
- `SkillBuilder_SpawnGroundWave`
- `SkillBuilder_SpawnOrbitals`
- `SkillBuilder_SpawnAuraRing` / `SkillBuilder_KillAuraRing`
- `SkillHelper_ChainTargets` / `SpawnChainLightning`
- `SkillHelper_Update` — already wired in `main.c`; **do not call from skills**.

---

## 16. Agent Providers (`#include "core/skill_manager.h"`)

Inversion-of-control: core queries agent positions and nearby targets without depending on `entities/`.

```c
typedef bool (*AgentPosProviderFn)(int agentId, Vector3 *outPos);
void SkillManager_SetAgentPosProvider(AgentPosProviderFn fn);
bool SkillManager_GetAgentPos(int agentId, Vector3 *outPos);
// Returns false if agentId invalid or no provider registered.

typedef int (*NearbyTargetsProviderFn)(Vector3 center, float radius,
                                       int *outIds, int maxIds);
void SkillManager_SetNearbyTargetsProvider(NearbyTargetsProviderFn fn);
int  SkillManager_GetNearbyTargets(Vector3 center, float radius,
                                   int *outIds, int maxIds);
// Returns count found. Returns 0 if no provider registered.
```

Both are registered automatically by `Entity_Init`. Skills call them **indirectly** via `StatusVFX_Update` or `SkillHelper_ChainTargets` — they should not call these functions directly.

---

## 17. Pool Stats & Sandbox Tools

### Pool Stats `GetStats()` (`#include "core/*.h"` respective headers)

Each core pool exposes active/max counts. Call once per frame in autotest scenarios to detect silent pool overflow.

```c
void ParticleSystem_GetStats(int *active, int *max);  // pool: 2000
void TrailSystem_GetStats(int *active, int *max);      // pool: 500
void DecalSystem_GetStats(int *active, int *max);      // pool: 64
void VFXLight_GetStats(int *active, int *max);         // pool: 16
void EmitterSystem_GetStats(int *active, int *max);    // pool: 256
void DamageVolume_GetStats(int *active, int *max);     // pool: 32
void StatusVFX_GetStats(int *active, int *max);        // pool: 32
void Afterimage_GetStats(int *active, int *max);       // pool: 64
```

Displayed by `sandbox/pool_stats.h` — hold **F3** in-game. Row turns red when peak == max (pool overflow occurred this session). `TraceLog(LOG_WARNING)` fires once per pool per session on first drop.

### Sandbox: Pool Stats Overlay (`#include "sandbox/pool_stats.h"`)

```c
void PoolStats_Init(void);
void PoolStats_DrawOverlay(void);  // hold F3 in-game
```

Shows 8 pools: active/max + peak high-water mark. Red row = at least one item was silently dropped this session.

### Sandbox: Visual Verify Harness (`#include "sandbox/visual_verify.h"`)

Headless regression capture. Usage: `WUXING_VERIFY=<skill_name> ./wuxing`

```c
bool        VisualVerify_IsEnabled(void);
const char *VisualVerify_GetSkillName(void);
void        VisualVerify_Init(int skillIndex);
void        VisualVerify_RunFrame(float elapsed);
bool        VisualVerify_IsFinished(void);
int         VisualVerify_GetExitCode(void);  // 0 = ok, 1 = unknown skill
```

- Casts the skill at a fixed position, saves 5 PNGs at 0.15 / 0.5 / 1.0 / 2.0 / 3.5 s into `autotest_output/verify_<skill>_<time>s.png`, then exits.
- `FLAG_WINDOW_HIDDEN` — no display required.
- Skill name must match registry exactly (e.g. `FIRE_BALL`).

> [!NOTE]
> When autotest reports PASS but visual output looks wrong, trust the screenshot over the numeric result — see memory entry "Trust Visual Over Numeric PASS".
## 19. Visual Composition & Procedural Meshes (core/composition/visual_composer.h & core/geometry/procedural_mesh_utils.h)

Cung cấp các hàm dựng hình học thô tĩnh nằm trong `procedural_mesh_utils.h`, và các bộ phối cảnh hiệu ứng hoàn chỉnh (gắn vật liệu, shader, hoạt ảnh) nằm trong `visual_composer.h`.

### Nhóm 1: Mesh & Hình khối tĩnh (Trong core/geometry/procedural_mesh_utils.h, gọi trong Draw)
Tập hợp các hàm vẽ hình học thô ráp, không tự gán vật liệu hay blend mode:
- `ProceduralMesh_DrawOrganicStonePillar`: Vẽ cột đá lăng trụ thô ráp với nắp phẳng ở đỉnh lăng trụ bát giác phẳng đầu.
- `ProceduralMesh_DrawOrganicPuddle`: Vẽ vũng nước phẳng thô dạng đa giác nhấp nhô hữu cơ.
- `ProceduralMesh_DrawRock`: Vẽ tảng đá răng cưa lởm chởm theo cấu trúc `RockMeshData`.
- `ProceduralMesh_DrawShardCluster`: Vẽ chùm tinh thể nhọn nhấp nhô bát diện theo cấu trúc `ShardClusterMeshData`.

### Nhóm 2: Bộ phối cảnh hiệu ứng hoàn chỉnh (Trong core/composition/visual_composer.h)
Tự gán shader, texture, vật liệu và quản lý blend mode / Z-buffer phù hợp để tạo hiệu ứng hoàn chỉnh:
- `VFX_ComposeStonePillar`: Dựng cột đá nhô lên theo `progress` sử dụng vật liệu `MAT_ROCK`.
- `VFX_ComposeBoulder`: Dựng tảng đá răng cưa lởm chởm kết hợp tâm cầu trơn mịn sử dụng vật liệu `MAT_ROCK`.
- `VFX_ComposeIceCrystal`: Dựng tinh thể băng lăng trụ kết chùm phát sáng trong suốt với vật liệu `MAT_ICE` (blend alpha + tắt ghi độ sâu).
- `VFX_ComposeMagicPuddle`: Dựng vũng nước ma thuật cuộn chảy động (flow map) sử dụng shader `puddle.fs` kết nối slot đa cấu hình `water_caustics.png` (slot 0) và `water_flow.png` (slot 1) dạng lặp `REPEAT`.
- `VFX_ComposeFireball`: Dựng quả cầu lửa hai lớp (lõi phát xạ mạnh sáng rực và lớp vỏ bập bùng biến dạng) vẽ qua blending cộng màu `BLEND_ADDITIVE`.
- `VFX_ComposeSmokePuff`: Bùng khói đặc tại một điểm bằng `ParticleSystem_SpawnRadialBurst`.
- `VFX_ComposeSmokeTrail`: Rải một đường hạt khói bay bay.
- `VFX_ComposeFissureStreak`: Tạo vệt rạn nứt đất dài liền mạch dưới dạng Quad 3D phẳng được map kết cấu `tex_crack_mask.png` (đã loại bỏ culling để hiển thị ổn định trên mọi góc quay camera).
- `VFX_ComposeLightningBolt`: Bắn một tia sét giật (proc bolt) từ điểm đầu đến điểm cuối, trả về ID thực thể tia sét để quản lý.
- `VFX_ComposeImpact`: Sinh hiệu ứng va chạm theo ElementPresetType.
- `VFX_ComposeCast`: Sinh hiệu ứng tụ khí theo ElementPresetType.
- `VFX_ComposeProjectileTrail`: Sinh vệt đạn bay theo ElementPresetType.
- `VFX_ComposeWaterStream`: Dựng dòng nước cuộn trào dạng ống Bezier mềm mại uốn lượn sử dụng shader `tube.fs` và texture `water_caustics.png` trong chế độ `BLEND_ALPHA`.
- `VFX_ComposeGlowingVine`: Dựng dải dây leo phát sáng ngọc bích tự động bò và xoắn ốc quấn chặt lấy mục tiêu. Thực hiện vẽ 2-pass (pass 1 ngọc bích trong suốt phát quang viền Fresnel qua `Material_LoadCustom`, pass 2 lõi sáng trắng tăng cường chế độ cộng màu `BLEND_ADDITIVE`).
- `VFX_ComposeProjectile`: Vẽ một loại đạn bay (PROJECTILE_FIREBALL, PROJECTILE_ICE, PROJECTILE_LIGHTNING, PROJECTILE_WOOD_SEED, PROJECTILE_ROCK, PROJECTILE_YINYANG) với đầy đủ hiệu ứng tích hợp: lõi cầu, vệt đuôi hạt, ánh sáng tỏa và tự động xoay lật.
- `VFX_GroundPattern`: Tạo hoa văn pháp trận trên mặt đất dạng Quad ngang tắt Culling (đất nứt, vòng ma thuật xoay, nham thạch sủi bọt, sương băng, gai mọc, chữ rune cổ).
- `VFX_ComposeBeam`: Vẽ tia laser/chùm sáng 3D đa hướng (crossed-quads) cuốn chảy kết cấu (lửa, sét, băng, ánh sáng, hư không).
- `VFX_PathWave`: Sinh đợt hiệu ứng mọc tuần tự dọc theo một danh sách điểm (cột đá nhô, gai băng mọc, gai mộc bò, lửa phun, sét truyền), phù hợp với kỹ năng vẽ đường casting kéo chuột.
- `VFX_SummonCircle`: Tạo vòng tròn triệu hồi với hai lớp pháp trận xoay ngược chiều nhau, hút các luồng hạt năng lượng vào tâm.
- `VFX_TriggerExplosion`: Kích nổ theo công thức chuẩn (lửa, băng, sét, đất, độc, thánh quang, hư không), tự động kết hợp Screen Distortion, Point Light flash, Decal, hạt nổ tỏa tròn và rung camera tùy chọn.
- `VFX_ComposeAura`: Tạo hào quang/vòng buff lơ lửng quanh chân và tỏa các hạt năng lượng hướng lên trên (lửa, băng, gió, sét, thái cực).
- `VFX_ComposeQiAura` / `VFX_AttachQiAura` / `VFX_DetachQiAura` / `VFX_UpdateQiAuras`: Hào quang khí công quấn quanh nhân vật theo `casterAgentId` (cột khí xoáy ngẫu nhiên bốc lên, sparkle rải rác) — `Attach` gắn/khởi tạo theo agent, `Update` chạy mỗi frame cho toàn bộ pool, `Detach` gỡ khi kết thúc.

### Nhóm 3: Beauty Primitives (`core/composition/vc_common.inl`) — mảnh trang trí tái sử dụng
Thuần particle/decal/light, **không đụng post-process pipeline** (xem `CORE_ISSUES.md` Item 35 — chỉnh sửa bloom/streak dùng chung rất dễ vỡ trên GPU cũ, tập trung "lấp lánh" vào các primitive nhỏ gọn/ngắn hạn như dưới đây mới an toàn):
- `VFX_ComposeShockwaveRing(pos, radius, life, tint)`: Vòng sóng xung kích mặt đất — decal ring giãn nở (`assets/textures/generic/impact_ring.png`, `BLEND_ADDITIVE`) + flash light.
- `VFX_ComposeGlintBurst(pos, count, spread, tint)`: Chùm tia lấp lánh nhỏ, bung nhanh rồi tắt (~0.12-0.22s/hạt) — dùng làm điểm nhấn "sparkle" cho bất kỳ hiệu ứng nào, kể cả gắn vào các archetype khác.
- `VFX_ComposeEmberDrift(pos, radius, count, tint)`: Hạt tàn lửa/bụi trôi lơ lửng (noise-curl + trọng lực nhẹ hướng lên), lifetime dài (~1.2-2.2s) — dùng cho hào quang/môi trường liên tục.
- `VFX_ComposeStreakFlare(pos, scale, tint)`: Chớp sáng bùng nổ tại điểm (particle tròn cực ngắn + flash light) — đọc là "flash" chứ không phải hình ngôi sao (particle hệ thống chỉ dùng chung 1 texture toàn cục, xem `core/particle_system.h`).

### Nhóm 4: Element Parity Additions (Phase 1 — `vc_metal.inl` / `vc_fire.inl`)
- `VFX_ComposeMetalShardCluster(basePos, seed)`: Cụm mảnh kim loại sắc nhọn dùng chung hệ crystal-mesh với băng nhưng đục/sáng bóng/không refract (`CrystalMaterialParams`: `refraction=0`, `crack=0`, `sparkle` cao).
- `VFX_ComposeMetalOrb(pos, time)`: Quả cầu chrome + viền điện xanh, cùng khuôn 2 lớp (lõi phát xạ + vỏ Fresnel) với `VFX_ComposeFireball`; thỉnh thoảng tự bắn `VFX_ComposeGlintBurst` làm tia điện lẹt xẹt.
- `VFX_ComposeBladeRing(pos, radius, bladeCount, rotationDeg)`: Vòng lưỡi kim loại chĩa ra ngoài quanh tâm, dùng vật liệu `MAT_METAL` có sẵn.
- `VFX_ComposeFlameWisp(pos, time)`: Đốm lửa nhỏ lập lờ, lệch pha theo vị trí spawn để nhiều đốm không nhấp nháy đồng bộ.
- `VFX_ComposeFirePillar(basePos, progress)`: Cột lửa trồi lên theo `progress`, cùng công thức smoothstep-rise với `VFX_ComposeStonePillar`.

### Nhóm 5: Phase 3 Archetypes — shield/chain/zone/slash/charge
Cùng quy ước tham số `(style, pos, ..., progress, time)` như các archetype ở Nhóm 2 (`VFX_ComposeBeam`, `VFX_GroundPattern`...), để AI dựng skill mới dễ đoán chữ ký hàm:
- `VFX_ComposeShield(ShieldStyle, pos, radius, progress, time)` (`vc_shield.inl`): Khiên/vòm chắn — scale-in 0..0.3, giữ nguyên, fade-out ở 0.85..1.0 (gọi liên tục mỗi frame trong lúc khiên còn tồn tại). Sphere lõm nửa dưới đất tạo hiệu ứng dome mà không cần mesh hemisphere riêng, cộng vòng rune xoay ở chân + glint bề mặt ngẫu nhiên. Style: `SHIELD_METAL/WOOD/WATER/EARTH/TAIJI`.
- `VFX_ComposeChain(ChainStyle, const Vector3 *targets, count, progress, time)` (`vc_chain.inl`): Nối tuần tự các điểm mục tiêu (bounce/jump targeting) — đoạn `i` chỉ hiện khi `progress` vượt qua `i/(count-1)` (giống `VFX_PathWave`); mỗi target mới chạm tới nổ `VFX_ComposeGlintBurst`. Style: `CHAIN_LIGHTNING/VINE/WATER/FIRE/TAIJI`.
- `VFX_ComposeZone(ZoneStyle, pos, radius, progress, time)` (`vc_zone.inl`): Vùng AoE tồn tại lâu dài (lava/frost/poison/holy/void) — tái dùng `VFX_GroundPattern` cho nền + hạt/light rải theo xác suất mỗi lần gọi (không phải burst cố định), gọi mỗi frame suốt thời gian zone active. Style: `ZONE_LAVA/FROST/POISON/HOLY/VOID`.
- `VFX_ComposeSlashArc(SlashStyle, pos, dir, radius, arcDegrees, progress, time)` (`vc_slash.inl`): Vệt chém cận chiến dạng ribbon cong, mỏng ở đuôi/dày ở đỉnh sweep, chỉ hiện phần cung đã quét tới `progress`; glint ở mép đang chém. Style: `SLASH_METAL/WOOD/FIRE/ICE/EARTH`.
- `VFX_ComposeChargeUp(ChargeStyle, pos, radius, progress, time)` (`vc_charge.inl`): Hiệu ứng tích khí/kênh phép — lõi cầu lớn dần + hạt hội tụ từ vòng ngoài co lại theo `progress`, glint bùng khi gần release (`progress > 0.7`). Style: `CHARGE_FIRE/METAL/WATER/WOOD/EARTH/TAIJI`.

Tất cả Nhóm 3-5 đã gắn sẵn vào tab **"NEW FX"** trong `sandbox/vfx_test.c` (14 mục) để xem trực quan — không cần viết skill thật mới xem được.



