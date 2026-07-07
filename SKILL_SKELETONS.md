# Skill Skeletons — Minimal Complete `.c` Templates

> Cross-reference: [`CORE_API.md`](CORE_API.md) §4 (Skill Lifecycle & SkillParams struct)
> Owned by: Skills Agent (copy/adapt for new skills)

---

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

