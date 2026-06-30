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
This engine uses a large coordinate scale. DO NOT use physical defaults (1.0 or 9.8).
Radii: Base mesh/tube radii should be around 10.0f to 20.0f. Impact bursts/lights should range from 50.0f to 100.0f.
Velocity/Speed: Particle speeds for bursts should range from 100.0f to 300.0f.

Gravity/Force splits into two regimes depending on the particle's lifetime/travel distance — pick the one that matches the case, don't apply one blanket number everywhere:
- **Sustained/flight force** (long-lived, far-traveling particles — e.g. a projectile trail): MUST be strictly between 300.0f and 700.0f. Real precedent: `skills/water/water_stream/tube_skill.c` uses `FORCE_GRAVITY_DIR strength = 650.0f`/`325.0f`.
- **Ambient/burst force** (short-lived particles that stay near their spawn point — e.g. Cast/Impact preset particles): 5.0f to 60.0f. Using 300-700f here flings burst particles off-screen instantly. Real precedent: the Cast/Impact preset `ForceField`s in `core/skill_helper.c`.

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
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

void InitExampleSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExampleSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        s_instances[i] = (SkillInstance){
            .state = STATE_CASTING,
            .position = startPos,
            .target = target,
            .velocity = (Vector3){0},
            .stateTimer = 0.0f,
            .radius = 8.0f * params.sizeScale,
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
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#define RISING_DURATION 0.4f
#define ACTIVE_DURATION 2.0f
#define DISSOLVE_DURATION 0.5f

void InitExampleRisingSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExampleRisingSkill(Vector3 spawnPos, SkillParams params) {
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (s_instances[i].active) continue;
        s_instances[i] = (SkillInstance){
            .state = STATE_CASTING,
            .position = spawnPos,
            .stateTimer = 0.0f,
            .radius = 14.0f * params.sizeScale,
            .maxHeight = 80.0f * params.sizeScale,
            .currentHeight = 0.0f,
            .active = true
        };
        SpawnCastEffect(spawnPos, EFFECT_PRESET_EARTH_CRACK, params.sizeScale * 0.6f);
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
        Vector3 top = { s->position.x, s->position.y + s->currentHeight, s->position.z };
        DrawCoreCylinder(s->position, top, s->radius, s->radius * 0.6f, 12, ELEMENT_COLOR_EARTH);
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
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#define RISING_DURATION 0.25f
#define ACTIVE_DURATION 1.5f
#define DISSOLVE_DURATION 0.4f

void InitExamplePathAnchorSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExamplePathAnchorSkill(Vector3 startPos, Vector3 target, SkillParams params) {
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

The documented Skill lifecycle (`Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params)` / `Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius)`) doesn't pass an `agentId` — don't invent a new parameter or change `Update[Name]Skill`'s signature to get one (that's a bigger cross-module lifecycle decision, tracked separately). Instead the skill caches the owner's `Vector3` position at cast time (`startPos`) and re-applies the buff via the radius-based `Entity_ApplyAoEBuff(casterPos, smallRadius, speedMult, duration)` entry point — no `agentId` needed. Visually following the *exact* agent (not just its cast-time position) requires `entities/entities.h`'s `Agent` array, which Skills doesn't currently have read access to (see note below); this skeleton instead re-centers on `casterPos` once at cast time, consistent with what `Entity_ApplyAoEBuff` itself can target without an agent ID.

Two options for the attached visual: (1) re-spawn a small particle burst and/or `VFXLight_Spawn` at the tracked position once per frame while `ATTACHED` — simplest, no persistent trail object to manage; (2) drive a `core/trail_system.h` `TRAIL_TYPE_FOLLOWER` trail with `SetFollowerAxis`/`UpdateFollowerPosition` every frame — better when the buff needs ribbon/aura geometry, not just a glow. The skeleton below uses option (1); reach for `TRAIL_TYPE_FOLLOWER` only if the visual genuinely needs ribbon geometry.

`DISSOLVE` fires when the skill's own local timer reaches the `duration` it requested from `Entity_ApplyAoEBuff` — the skill does not query `entities/` for the live modifier state (Core/Skills never read `entities/` internals); it just times its visual to roughly match the buff duration it asked for.

```c
#include "skill_example_attached.h"
#include "core/skill_helper.h"
#include "core/vfx_light.h"
#include "core/particle_radial_burst.h"
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
    bool active;
} SkillInstance;

static SkillInstance s_instances[MAX_INSTANCES];

#define CASTING_DURATION 0.15f
#define DISSOLVE_DURATION 0.3f
#define VISUAL_TICK_INTERVAL 0.1f  // re-spawn particle/light every N seconds, not every frame

void InitExampleAttachedSkill(int screenWidth, int screenHeight) {
    for (int i = 0; i < MAX_INSTANCES; i++) s_instances[i].active = false;
}

void CastExampleAttachedSkill(Vector3 startPos, Vector3 target, SkillParams params) {
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
> Requires `#include "entities/entities.h"` to call `Entity_ApplyAoEBuff`. As of this writing, `skills/CLAUDE.md`'s allowed read list covers `core/` headers and `environment/environment_system.h` only — `entities/` is **not** listed. Skills Agent: confirm with whoever owns `skills/CLAUDE.md` whether `entities/entities.h` should be added to that list before shipping a real Buff skill against this skeleton.

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

> **Pool budget:** `MAX_TRAIL_PARTICLES = 500` is a single static pool shared across **all active trails project-wide**, not per-skill. Several concurrent heavy-trail skills (e.g. multiple wisp/projectile-heavy casts at once) can exhaust it — `SpawnTrailEntity` returns `-1` if the pool is full. Same pattern as `MAX_DECALS` in §8.

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
    float life;             // Total duration in seconds (0 = persistent until KillTrail)
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
} TrailConfig;
```
* **Follower Trails:** For sword swings or aura attachments, set type to `TRAIL_TYPE_FOLLOWER`. Every frame, you must call:
  - `SetFollowerAxis(trailId, basePos, normalizedDir);` (Sets orientation)
  - `UpdateFollowerPosition(trailId, tipPos);` (Updates head position)
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
void DecalSystem_AddStreak(const Vector3 *points, int count, float rot, float scale, Texture2D tex, float life, Color tint);
void DecalSystem_Update(float dt);
void DecalSystem_Draw(void);
void DecalSystem_Unload(void);
```
* `rot`: yaw around Y axis (degrees). Alpha fades internally as `lifetime / maxLifetime` decays to 0.
* Static pool, `MAX_DECALS = 64`, no malloc.
* `DecalSystem_AddStreak`: thin wrapper that calls `DecalSystem_Add` once per point in `points[0..count-1]` — for path-shaped effects (thorn lines, scorch trails) instead of hand-rolling a loop. Caller's responsibility to pass a reasonable `count` (e.g. up to 32, matching `SkillParams.pathPoints[32]`); not auto-clamped against `MAX_DECALS` headroom, same convention as `SamplePath`'s `maxSegments` in `core/path_spline.h`.

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

void VFXLight_Init(void);
void VFXLight_Reset(void);
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime);
void VFXLight_Update(float dt);
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);
```
* **`Spawn`:** Light expires automatically after `lifetime` seconds via `Update` — no manual kill needed.
* **`GetActive`:** Call every frame before drawing a lit skill, to fetch the active list and upload it via `SetShaderValueV`. `maxCount` should be ≤ `MAX_VFX_LIGHTS` (16).
* Rule: VFX lights supplement, not replace, scene lighting — keep them alive for the skill's full active phase rather than spawning and killing within one frame.

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
- **Submits geometry only** — like `DrawRibbonStrip`, `ProceduralMesh_DrawTube` does not change shader or blend state. `BeginShaderMode()` and, if the shader outputs alpha < 1 (e.g. for translucent water/energy tubes), `BeginBlendMode(BLEND_ALPHA)` or `BLEND_ADDITIVE` must be set by the calling skill before the draw call.

> [!NOTE]
> **UV convention (inferred — confirm against `procedural_mesh_utils.c` source):** Based on how `tube.vs`/`tube.fs` consume `vertexTexCoord`, the tube mesh appears to map `vertexTexCoord.x ∈ [0, 1]` around the **radial/circumferential** direction (so `phi = vertexTexCoord.x * 2π` sweeps once around the tube's cross-section) and `vertexTexCoord.y` along the **length** of the tube in world units, normalized by dividing by `u_uvLength` (set from C-side to the tube's actual Bezier arc length) to get `t ∈ [0, 1]` along the spline. This document doesn't yet have a confirmed, authoritative statement of this convention from the mesh-generation source itself — treat the above as the working assumption validated by the Water Stream sample, and update this note if `procedural_mesh_utils.c` defines it differently (e.g. if `uvLengthScale` in `ProceduralMesh_DrawTube` already pre-divides UVs, in which case skill shaders should *not* divide by `u_uvLength` again).

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
typedef enum { MATERIAL_FIRE, MATERIAL_ICE, MATERIAL_WATER, MATERIAL_PORTAL } MaterialPreset;
typedef struct { Shader shader; MaterialPreset preset; int uTimeLoc; int uDissolveLoc; } EffectMaterial;
EffectMaterial Material_Load(MaterialPreset preset);
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
void Material_Begin(EffectMaterial mat);
void Material_End(void);
```

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

**Quy tắc include:**
- Luôn include theo thứ tự: `fs_header.glsl` → `noise.glsl` (nếu cần) → `lighting.glsl` → `fx.glsl`
- `fx.glsl` không phụ thuộc `noise.glsl` — có thể include riêng lẻ hoặc cùng nhau
- Không tái implement hash/noise/fbm/dissolve/flow blend trong skill code

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

> [!NOTE]
> **`lightDir` is not provided by the engine.** Both `lighting.glsl` functions take `lightDir` as a plain parameter — there is no auto-bound global light direction uniform. Existing skills (e.g. `tube.fs`) hard-code a fixed directional light as `normalize(vec3(0.5, 0.8, 0.5))` to match the project's single static "Isometric Night-time Arena" key light. **New skills should reuse this exact vector** rather than inventing a different light direction, to keep lighting consistent across all elements in the same scene. If the engine later exposes a shared `u_lightDir` uniform, this section will be updated — until then, hard-coding this specific vector is the project convention.

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

Nếu skill gọi `BeginShaderMode` trực tiếp (bypass `SkillManager_BeginShader`), PHẢI set matModel thủ công:

```c
// Trước draw call, sau BeginShaderMode():
if (s_shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0) {
    Matrix identity = MatrixIdentity();
    SetShaderValueMatrix(s_shader, s_shader.locs[SHADER_LOC_MATRIX_MODEL], identity);
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