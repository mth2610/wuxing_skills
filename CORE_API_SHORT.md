# WUXING SKILLS CORE API — SHORT FORM
> Compact AI-reference condensation of `CORE_API.md`. Section numbers mirror the full doc. Manual-only, not auto-synced — regenerate on explicit request only.

---

## 1. COMPILATION & ARCHITECTURE RULES

### 1.1 Language & Standards
- Strict **C99**, Raylib 5.5, OpenGL 3.3 Core, 3D Isometric Night-time Arena.
- Every skill `.c` MUST `#include <stddef.h>`, `<stdlib.h>`, `<stdio.h>` explicitly (NULL/snprintf not implicitly available).
- Include paths relative from root: `#include "core/particle_system.h"`.
- `PI`: raylib already defines it. **NEVER** bare `#define PI`. Guard:
```c
#ifndef PI
#define PI 3.1415926535f
#endif
```
Bare redefinition = hard error (`-Wmacro-redefined`).

### 1.2 Memory
- **NO malloc/calloc/realloc/free** in skill code. Use `static MyStruct s_entities[MAX_ENTITIES];` + active flags/state enums.
- Stack allocations OK.

### 1.3 Registry
`scripts/generate_registry.py` auto-scans skills at build. Layout:
```
skills/[element]/[skill_name]_skill/
    [skill_name]_skill.h   # lifecycle prototypes
    [skill_name]_skill.c   # logic + rlgl rendering
    [skill_name].vs/.fs    # optional, auto-copied
    [texture].png          # optional, auto-copied
```

### Scaling rules (MANDATORY — no physical defaults like 1.0/9.8)
| Quantity | Range |
|---|---|
| Mesh/tube radii | 10–20f |
| Impact burst/light radius | 50–100f |
| Particle burst speed | 100–300f |
| **Sustained/flight force** (long-lived, far-traveling, e.g. projectile trail) | **300–700f** strictly. Precedent: `tube_skill.c` `FORCE_GRAVITY_DIR` 650f/325f |
| **Ambient/burst force** (short-lived, near spawn, e.g. Cast/Impact presets) | **5–60f**. 300-700f here flings particles off-screen |

> [!IMPORTANT] Include path of a skill's own header in its `.c` MUST exactly match its folder, including `_skill` suffix, e.g. `skills/wood/jade_burst_skill/` → `#include "skills/wood/jade_burst_skill/jade_burst_skill.h"`.

---

## 2. ELEMENT COLORS (`core/skill_manager.h`)
| Macro | Color | RGBA |
|---|---|---|
| `ELEMENT_COLOR_WATER` | Cyan-Blue | 41,128,185,255 |
| `ELEMENT_COLOR_WOOD` | Emerald Green | 46,204,113,255 |
| `ELEMENT_COLOR_FIRE` | Crimson Red | 231,76,60,255 |
| `ELEMENT_COLOR_EARTH` | Ochre Brown | 230,126,34,255 |
| `ELEMENT_COLOR_METAL` | Silver Gray | 149,165,166,255 |
| `ELEMENT_COLOR_TAIJI` | Amethyst Purple | 155,89,182,255 |

- **No hardcoded raw colors** — derive from these macros.
- `ColorAlpha(color, alpha)` for opacity; `ColorLerp(c1, c2, factor)` to mix (BLACK for shadow, WHITE for glow).

---

## 3. RESOURCE MANAGER (`core/resource_manager.h`)
```c
Texture2D ResourceManager_LoadTexture(const char *filePath);  // cached
Shader    ResourceManager_LoadShader(const char *vsFilePath, const char *fsFilePath); // cached; NULL vs only for unlit
Sound     ResourceManager_LoadSound(const char *filePath);    // cached
```
- **NEVER** call `UnloadTexture`/`UnloadShader`/`UnloadSound` in `Unload[Name]Skill` — leave empty/commented. Global ResourceManager frees all on app shutdown.

---

## 4. STANDARD LIFECYCLE API (`[skill_name]_skill.h`)

```c
typedef struct {
    int level, milestone, quantity;
    float sizeScale, damage;
    CastAnchorType anchorType;
    CastPathType pathType;
    bool showPortal;
    int pathPointCount;
    Vector3 pathPoints[32]; // max 32, drag-to-cast path
} SkillParams;

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct { Vector3 position; float radius; bool active; } SkillProjectile;
#endif

void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);
bool Is[Name]SkillCoiling(void);
int  Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);
```
No `agentId` parameter exists anywhere in this lifecycle — don't invent one.

### 4 skill skeletons (state-machine shapes only — see `CORE_API.md` §4 for full code)

**(A) Generic Projectile** — `skills/.../skill_example.c` pattern. Static `SkillInstance s_instances[MAX_INSTANCES]`, no malloc.
States: `CASTING` (windup timer, `SpawnCastEffect`) → `FLYING` (move by velocity, `Vector3Distance` hit check → `SpawnImpactEffect` + `ApplyAoEDamage`) → `IMPACT` (brief hold) → `DISSOLVE` (timeout → `active=false`).
`Draw` only renders state `FLYING`. `GetProjectiles` reports active `FLYING` instances for engine collision feed.

**(B) Ground-Rising (non-projectile)** — effect spawns/animates at fixed point, nothing flies.
States: `CASTING` → `RISING` (`currentHeight` animates 0→`maxHeight` via `SmoothStep01`+`Math_Mix`) → `ACTIVE` (hold, `ApplyAoEDamage` fires once on RISING completion) → `DISSOLVE` (height animates back to 0).
Mesh: NOT a single `DrawCoreCylinder` — build from several short jittered segments (§12.2 Perpendicular Jitter) with per-instance yaw/scale variance (§12.3), via `DrawCoreCylinder` per segment, not `ProceduralMesh_BuildTube`/path-spline.

**(C) Anchored-Along-Path** — multiple instances spawned along `SkillParams.pathPoints[]`. Use `SamplePath()` (`core/path_spline.h`) to resample at even spacing — never hand-roll cumulative-distance math (existing offender: `wood_thorns_skill.c`, unrefactored).
States: `CASTING` (brief, per-instance) → `WAITING` (staggered delay, `waitTime = p/sampledCount * STAGGER_DURATION`, so instances rise in sequence) → `RISING` → `ACTIVE`/`HOLDING` → `DISSOLVE`. Falls back to straight `startPos→target` line if `pathPointCount <= 1`.

**(D) Entity-Attached** (Buff skills, dash afterimages) — visual follows a moving Agent. Lifecycle signature does NOT carry `agentId`; cache `casterPos = startPos` at cast time instead, re-apply via radius-based `Entity_ApplyAoEBuff(casterPos, radius, speedMult, duration)` (no team filtering yet — hits allies and enemies in radius).
States: `CASTING` → `ATTACHED` (re-spawn `VFXLight_Spawn`/particle pulse at `casterPos` on an interval, not every frame) → `DISSOLVE` (fires when local timer ≈ buff `duration`; skill doesn't query `entities/` for live buff state).
Two visual options: (1) re-spawn particle/light each tick — simplest; (2) drive `TRAIL_TYPE_FOLLOWER` via `SetFollowerAxis`/`UpdateFollowerPosition` — only if ribbon/aura geometry is genuinely needed.
**Requires `#include "entities/entities.h"`** for `Entity_ApplyAoEBuff`/`Entity_ApplyAoEDamage`/`Entity_GetNearbyTargets` — any skill calling these must include it (lives in `entities/`, not `core/`; missing include → `implicit declaration` error, not a clear missing-header error). Skills' allowed read list includes this header — resolved, no further confirmation needed.

---

## 5. DYNAMIC FORCE FIELD (`core/force_field.h`)
`ForceField` = up to **8 cumulative layers**. Must be `static`.
```c
static ForceField s_forceField;
ForceField_Clear(&s_forceField);
ForceField_AddLayer(&s_forceField, (ForceLayer){ .type=FORCE_VORTEX, .origin=p, .direction=(Vector3){0,1,0}, .strength=5.0f, .radius=15.0f, .falloff=1.0f });
```

| ForceType | origin | direction | strength | radius | falloff | noiseScale | noiseSpeed |
|---|---|---|---|---|---|---|---|
| `FORCE_GRAVITY_DIR` | — | gravity vec (norm) | accel magnitude | — | — | — | — |
| `FORCE_GRAVITY_POINT` | attract center | — | +attract/-repel | range | 0=const,1=lin,2=quad | — | — |
| `FORCE_VORTEX` | center | rotation axis (norm) | angular speed | range | 0=const,1=lin | — | — |
| `FORCE_WIND` | — | wind vec (norm) | accel magnitude | — (global) | — | — | — |
| `FORCE_NOISE_PERLIN` | offset seed | — | amplitude | — | — | freq | anim speed |
| `FORCE_NOISE_CURL` | offset seed | — | amplitude | — | — | freq | anim speed |
| `FORCE_DRAG` | — | — | linear drag coeff (0-1) | — | — | — | — |
| `FORCE_VISCOSITY` | — | — | viscous damping | — | — | — | — |
| `FORCE_RADIAL_AXIS` | dynamic | dynamic | +push/-pull | range | 1=lin | — | — |
| `FORCE_VORTEX_AXIS` | dynamic | dynamic | rotation speed | range | 1=lin | — | — |

- `RADIAL_AXIS`/`VORTEX_AXIS` ignore struct `origin`/`direction` — use per-frame `axisOrigin`/`axisDir` (e.g. via `SetFollowerAxis()`).
- Falloff: 0=constant, 1=linear-to-zero at radius, 2=quadratic.
- `ForceField_GetViscosityDamping(&s_forceField, dt)` → scale velocity manually in custom update loops.

---

## 6. PARTICLE SYSTEM (`core/particle_system.h`)
`ParticleConfig` init with `{0}`. `void SpawnParticle(ParticleConfig config);`
```c
typedef struct {
    Vector3 position, velocity;
    Color colorStart, colorEnd;
    float radius, lifetime;
    const ForceField *forceField;
    const ColorGradient *gradient;        // overrides colorStart/End if non-NULL
    const SpriteAnim *spriteAnim;
    const ParticleConfig *onDeathEmit; int onDeathEmitCount;  // sub-emitter on death
    const ParticleConfig *onLiveEmit;  float onLiveEmitRate;  // sub-emitter along path
} ParticleConfig;
```
- `gradient` non-NULL → `colorStart`/`colorEnd` ignored.
- Sub-emitters inherit position, NOT velocity. Configs passed to sub-emitters MUST be `static`.

---

## 7. TRAIL & RIBBON SYSTEM (`core/trail_system.h`)
`TrailConfig` init with `{0}`. `int SpawnTrailEntity(TrailConfig config);` returns `-1` if pool full.
> `MAX_TRAIL_PARTICLES = 500` — single pool shared project-wide, not per-skill.

```c
typedef enum { TRAIL_TYPE_PROJECTILE, TRAIL_TYPE_WISP, TRAIL_TYPE_PORTAL, TRAIL_TYPE_FOLLOWER } TrailType;
typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);

typedef struct {
    TrailType type;
    Vector3 pos, vel, target;
    float len, thick, trailLength, life;       // life=0 → persistent until KillTrail
    float initialAngle, wobblePhase, scale;
    Texture2D tex; Color tint; Shader shader;
    TrailUpdateCallback onUpdate; TrailDeathCallback onDeath;
    int ownerTag;                              // caller-defined, not read internally
    float wobbleAmplitudeOverride, curveRangeOverride; // >0 overrides global; <=0 default
    const ForceField *forceField; const ColorGradient *gradient; const SpriteAnim *spriteAnim;
} TrailConfig;
```
- `TRAIL_TYPE_FOLLOWER`: every frame call `SetFollowerAxis(trailId, basePos, normalizedDir)` + `UpdateFollowerPosition(trailId, tipPos)`.
- `KillTrail(trailId)` to free when complete.
- `onDeath` fires once on `life` expiry OR (PROJECTILE only) auto hit-detect on `target` — use to spawn impact VFX at exact last position.
- `onUpdate` fires every frame post-physics; call `GetTrail(trailId)` inside for live pos/vel.
- `ownerTag`: tag caster/instance, read via `GetTrail(id)->ownerTag`.
- Per-instance PROJECTILE overrides: `wobbleAmplitudeOverride=0.001f` + `curveRangeOverride=1.0f` → near-straight, snap-to-target trail (e.g. sword qi). Leave `0` for default global-macro behavior.

---

## 8. GRAPHICS & VFX API

### Ground Decals (`core/decal_system.h`)
```c
void DecalSystem_Init(void);
void DecalSystem_Add(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);
void DecalSystem_AddStreak(const Vector3 *points, int count, float rot, float scale, Texture2D tex, float life, Color tint); // wraps Add per-point
void DecalSystem_Update(float dt);
void DecalSystem_Draw(void);
void DecalSystem_Unload(void); // engine-only, never from skill code
```
- `rot`=yaw deg around Y. Alpha fades via `lifetime/maxLifetime`.
- `MAX_DECALS = 64`, static pool, no malloc.
- `Init()` once at startup; `Update(dt)` every frame.
- Internal Y offset prevents Z-fighting — don't add your own.
- Draw before 3D meshes, `BLEND_ALPHA`.
- Recommended scale: 4–5.5× structure radius.
- `AddStreak` count not auto-clamped to `MAX_DECALS` headroom — caller's responsibility (typically ≤32).

### Screen Distortion (`core/screen_distort.h`)
`MAX_DISTORTION_SOURCES = 16`. Lifecycle (engine-only, skill only calls `Add`):
```c
void ScreenDistort_Init(int width, int height);
void ScreenDistort_Begin(void); void ScreenDistort_End(void);
void ScreenDistort_Update(float dt);
void ScreenDistort_Draw(Camera3D camera);
void ScreenDistort_Unload(void); // engine shutdown only
void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed);
```
- `strength`: 0.01–0.05 light heatwave, 0.1–0.3 strong shockwave. Auto-expires after `lifetime` — no manual kill.

### Color Gradient (`core/color_gradient.h`)
```c
typedef struct { float t; Color color; } GradientStop;            // t in [0,1]
typedef struct { GradientStop stops[COLOR_GRADIENT_MAX_STOPS]; int count; } ColorGradient; // max 8 stops
bool  ColorGradient_AddStop(ColorGradient *g, float t, Color color);  // caller must add in increasing t order
Color ColorGradient_Sample(const ColorGradient *g, float t);          // LERP between adjacent stops
ColorGradient ColorGradient_MakeElectric(void);
void  ColorGradient_StandardFade(ColorGradient *grad, Color baseColor, float midT, float brightenAmount);
```
Prefer `ColorGradient` over `colorStart/colorEnd` for multi-stage shifts (fire white→orange→ash).

### Ribbon Strip (`core/ribbon_strip.h`)
Camera-facing ribbon for any continuous long body (dragon/vine/lightning/water stream) — replaces billboard chains.
```c
typedef struct { Vector3 position; float halfWidth; Color tint; float v; } RibbonPoint; // v = caller-computed UV along length
void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture, Camera3D camera);
```
- Caller supplies static array, `count >= 2`.
- Geometry-only — does NOT set shader/blend state; caller must `BeginShaderMode`/`BeginBlendMode` first.
- Mandatory for long-body meshes — never hand-roll billboard chains.

### Flow Map (`core/flow_map.h`)
Per-skill instance, no global state.
```c
typedef struct { float speed, strength, tiling; } FlowMapConfig;
typedef struct { FlowMapConfig cfg; /* + internal uniform loc cache + tex */ } FlowMap;
FlowMap FlowMap_Create(Shader shader, Texture2D flowTex, const char *timeUniformName); // flowTex NOT owned, caller unloads
FlowMap FlowMap_CreateWithVortexTexture(Shader shader, int texSize, const char *timeUniformName); // FlowMap owns generated tex
void    FlowMap_Apply(const FlowMap *fm, Shader shader, float time); // call AFTER BeginShaderMode, same shader as Create
void    FlowMap_Unload(FlowMap *fm);
```

### Path Spline (`core/path_spline.h`)
```c
Vector3 GetBezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
Vector3 GetBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 target, float t);
int     SamplePath(const Vector3 *path, int pathCount, float spacing, Vector3 *outSegments, int maxSegments); // returns actual count <= maxSegments
```
Never hand-roll Bezier or point sampling in skill code.

### Sprite Animation (`core/sprite_anim.h`)
```c
typedef enum { ANIM_ONCE=0, ANIM_LOOP, ANIM_RANDOM_START, ANIM_PING_PONG } AnimPlayMode;
typedef struct { int rows, cols, frameCount; float fps; AnimPlayMode playMode; /* + internal state */ } SpriteAnim;
void      SpriteAnim_Init(SpriteAnim *anim, int rows, int cols, int frameCount, float fps, AnimPlayMode mode);
void      SpriteAnim_Update(SpriteAnim *anim, float dt);
Rectangle SpriteAnim_GetUVRect(const SpriteAnim *anim);
bool      SpriteAnim_IsFinished(const SpriteAnim *anim);
void      SpriteAnim_Reset(SpriteAnim *anim);
Rectangle SpriteAnim_CalculateUV(const SpriteAnim *template, float age, int *outFrame); // stateless, for particles
```
- Particles/trails hold `const SpriteAnim*` template + use stateless `CalculateUV(template, age, &frame)`.
- Stateful instances (UI/decal/billboard): `Init` then `Update` per frame, read via `GetUVRect`.
- `ANIM_RANDOM_START`: avoids visible lockstep across particles sharing an atlas.

### Particle Radial Burst (`core/particle_radial_burst.h`)
```c
typedef struct {
    int countMin, countMax; float speedMin, speedMax, radiusMin, radiusMax, lifetimeMin, lifetimeMax;
    float pitchRange, upwardBias; Color colorStart, colorEnd;
    const ColorGradient *gradient; const ForceField *forceField;
} ParticleRadialBurstConfig;
void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg);
```

### Impact Burst (`core/impact_burst.h`)
```c
typedef struct {
    bool distortEnabled; float distortRadius, distortStrength, distortLife, distortSpeed;
    bool decalEnabled; Texture2D decalTex; float decalScale; float decalLife; Color decalTint;
    bool decalRandomRotation; float decalFixedRotation; // RandomRotation true → GetRandomValue(0,360)
    bool lightEnabled; Color lightColor; float lightRadius; float lightLife;
    bool particlesEnabled; ParticleRadialBurstConfig particles;
} ImpactBurstConfig;
void VFX_TriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);
```
4-step pipeline: screen distort → ground decal → point light flash → radial particle burst.

### Math Utils (`core/utils_math.h`)
```c
float Math_Mix(float x, float y, float a);  // LERP
float SmoothStep01(float x);                // clamped smoothstep
float Random01(void);                       // [0,1]
```
Use these instead of reimplementing lerp/smoothstep.

### VFX Lights (`core/vfx_light.h`)
`MAX_VFX_LIGHTS = 16`, static pool.
```c
typedef struct { Vector3 position; float radius; Color color; } VFXLightData;
void VFXLight_Init(void); void VFXLight_Reset(void);
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime); // auto-expires, no manual kill
void VFXLight_Update(float dt);
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount); // maxCount <= 16, call before drawing lit skill
```
Keep lights alive for skill's full active phase, not spawn+kill same frame.

### Combat (`core/skill_manager.h`)
```c
void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
```

### Shader Binding (`core/skill_manager.h`)
```c
void SkillManager_BeginShader(Shader shader);
void SkillManager_EndShader(void);
```
Auto-binds `u_time`, `viewPos`, `u_resolution`. (Also auto-sets `matModel=identity` — see §10 Rule D.)

### VFX Standards
- Dark diffuse materials, avoid fully emissive meshes.
- Spawn particles continuously while active.
- Keep point lights alive during active phase.

### Procedural Mesh (`core/procedural_mesh_utils.h`)
```c
void DrawCoreSphere(Vector3 center, float radius, int rings, int slices, Color color);
void DrawCoreCylinder(Vector3 bottom, Vector3 top, float radiusBottom, float radiusTop, int slices, Color color);
void DrawCoreCone(Vector3 bottom, float radius, float height, int slices, Color color);
void DrawCorePlaneRect(Vector3 center, Vector2 size, Color color);
void DrawCorePlanePolygon(Vector3 center, float radius, int sides, Color color);
void DrawCoreCube(Vector3 position, float width, float height, float length, Color color);
void DrawCoreTorus(Vector3 center, float innerRadius, float outerRadius, int sides, int rings, Color color);
void DrawCorePrism(Vector3 bottom, Vector3 top, float radius, int sides, Color color);
```

**Tube** (circular Bezier tube — water stream etc.):
```c
typedef struct {
    float capsuleTailExp, tailTaperMin, tailTaperMax, headGrowth;
    float wobbleAmplitude, wobbleFrequency, wobbleSpeed;
    float deform1Amp, deform1FreqT, deform1FreqPhi, deform1Speed;
    float deform2Amp, deform2FreqT, deform2FreqPhi, deform2Speed;
    float tailApexFactor, headApexFactor;
} TubeMeshConfig;
typedef struct {
    Vector3 rings[TUBE_MESH_MAX_SEGMENTS+1][TUBE_MESH_MAX_RADIAL];
    Vector3 normals[TUBE_MESH_MAX_SEGMENTS+1][TUBE_MESH_MAX_RADIAL];
    Vector3 tailCenter, headCenter, tailTangent, headTangent;
    float tailRadius, headRadius; int segments, radialSegs;
    float tailApexFactor, headApexFactor;
} TubeMeshData;
TubeMeshConfig ProceduralMesh_DefaultTubeConfig(void);
void ProceduralMesh_BuildTube(TubeMeshData *out, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float baseRadius, float flowProgress, float time, int segments, int radialSegs, const TubeMeshConfig *cfg);
void ProceduralMesh_DrawTube(const TubeMeshData *data, float uvLengthScale);
```
- Never hand-implement Bezier/Frenet/tube generation in skills. Start from `DefaultTubeConfig()`, override only needed fields.
- Geometry-only — set `BeginShaderMode()`/`BeginBlendMode()` (ALPHA or ADDITIVE for translucent) yourself.
- [!NOTE working assumption] UV: `vertexTexCoord.x ∈[0,1]` = radial/circumferential (`phi = x*2π`); `.y` = length, normalized by `u_uvLength` (C-side arc length). Unconfirmed against source — verify if behavior mismatches.

**Wave Plane** (rippling water surface):
```c
#define WAVE_PLANE_MAX_SEGMENTS_X 24
#define WAVE_PLANE_MAX_SEGMENTS_Z 24
typedef struct { float wavelength, amplitude; Vector3 direction; float crestSharpness; } WavePlaneConfig;
typedef struct { Vector3 verts[...][...]; Vector3 normals[...][...]; int segmentsX, segmentsZ; } WavePlaneMeshData;
WavePlaneConfig ProceduralMesh_DefaultWavePlaneConfig(void);
void ProceduralMesh_BuildWavePlane(WavePlaneMeshData *out, Vector3 center, float width, float length, int segmentsX, int segmentsZ, float time, const WavePlaneConfig *cfg);
void ProceduralMesh_DrawWavePlane(const WavePlaneMeshData *data, Color color);
```
- CPU-side: 3 layered sines (main + cross @~2.3x freq + slow low-freq) + per-vertex hash noise. `crestSharpness`: `sign(s)*|s|^(1/(1+sharpness))`, 0=plain sine.
- Build every frame (animates via `time`) then Draw. Normals via finite-difference.

**Curling Wave** (cresting wave wall):
```c
#define CURLING_WAVE_MAX_WIDTH_SEGS 32
#define CURLING_WAVE_MAX_PROFILE_SEGS 16
typedef struct { float curlAmount, height, archWidth; } CurlingWaveConfig; // curlAmount 0=flat
typedef struct { Vector3 verts[...][...]; Vector3 normals[...][...]; int widthSegs, profileSegs; } CurlingWaveMeshData;
CurlingWaveConfig ProceduralMesh_DefaultCurlingWaveConfig(void);
void ProceduralMesh_BuildCurlingWave(CurlingWaveMeshData *out, Vector3 baseCenter, Vector3 widthDirection, const CurlingWaveConfig *cfg, int profileSegs, int widthSegs);
void ProceduralMesh_DrawCurlingWave(const CurlingWaveMeshData *data, Color color);
```
- Open "C" cross-section swept along `widthDirection` (reuses `BuildTube`'s Frenet technique, open arc not closed circle). Profile arc -90°→`90+90*curlAmount`. Small jitter near lip. Build-every-frame like Tube.

**Rock** (faceted rock, prominent/large only):
```c
#define ROCK_MESH_MAX_VERTS 162
#define ROCK_MESH_MAX_FACES 320
typedef struct { Vector3 verts[...]; Vector3 faceNormals[...]; int faceVertIdx[...][3]; int vertCount, faceCount; } RockMeshData;
void ProceduralMesh_BuildRock(RockMeshData *out, Vector3 center, float radius, float jitterAmount, int seed, int subdivisions);
void ProceduralMesh_DrawRock(const RockMeshData *data, Color color);
```
- Subdivided icosahedron (subdivisions clamped 0-2, level 2≈162 verts=ceiling), radial jitter ±`jitterAmount` via seeded hash PRNG. Flat-shaded (per-face normals).
- Small rubble: keep using squished `DrawCoreCube`/`DrawCoreSphere` with §12.3 randomization, NOT this function.
- **Build once at cast time, cache** — doesn't animate.

**Shard Cluster** (radiating crystal/shard):
```c
#define SHARD_CLUSTER_MAX_SHARDS 16
#define SHARD_MAX_SIDES 6
typedef struct { float spreadAngle, thicknessMin, thicknessMax, tipSharpness; int sides; } ShardClusterConfig;
typedef struct { Vector3 baseRing[...][...], tipRing[...][...], baseNormal[...][...]; Vector3 tipCenter[...], baseCenter[...]; int sides, shardCount; } ShardClusterMeshData;
ShardClusterConfig ProceduralMesh_DefaultShardClusterConfig(void);
void ProceduralMesh_BuildShardCluster(ShardClusterMeshData *out, Vector3 origin, Vector3 mainDirection, int shardCount, float minLength, float maxLength, int seed, const ShardClusterConfig *cfg);
void ProceduralMesh_DrawShardCluster(const ShardClusterMeshData *data, Color color);
```
- Each shard: tapered prism (polygon cross-section `cfg->sides`), random tilt within `spreadAngle` cone, random length/thickness/twist, seeded PRNG (same as Rock). Use: Metal sword-qi, Water ice-shards. `tipSharpness`: `tipRadius=baseRadius*(1-tipSharpness)`. **Build once at cast time, cache.**

**Vortex Funnel** (tornado/wind funnel):
```c
#define VORTEX_FUNNEL_MAX_HEIGHT_SEGS 32
#define VORTEX_FUNNEL_MAX_RADIAL_SEGS 24
typedef struct { float topRadius, bottomRadius, height, twistAmount; int ridgeCount; float ridgeAmount; } VortexFunnelConfig;
typedef struct { Vector3 rings[...][...]; Vector3 normals[...][...]; int heightSegs, radialSegs; } VortexFunnelMeshData;
VortexFunnelConfig ProceduralMesh_DefaultVortexFunnelConfig(void);
void ProceduralMesh_BuildVortexFunnel(VortexFunnelMeshData *out, Vector3 center, const VortexFunnelConfig *cfg, int heightSegs, int radialSegs, float time);
void ProceduralMesh_DrawVortexFunnel(const VortexFunnelMeshData *data, Color color);
```
- For Phong (wind)/tornado/Taiji ultimate. Straight-vertical specialization of sweep-along-path (builds rings directly, no Bezier). Radius lerps bottom→top over height, rotates `twistAmount`° total + `time`-based spin (pass `time=0` for static/cached), ridge bump follows twist (spirals). **No end caps** (open both ends). Animated → rebuild every frame; static → build once, `time=0`.

**Fissure** (3D ground crack, Earth):
```c
#define FISSURE_MAX_SEGMENTS 48
#define FISSURE_CROSS_VERTS 5  // left edge, left shoulder, bottom, right shoulder, right edge
typedef struct { Vector3 verts[...][5]; Vector3 normals[...][5]; int segments; } FissureMeshData;
void ProceduralMesh_BuildFissure(FissureMeshData *out, const Vector3 *pathPoints, int pathPointCount, float width, float depth, float jaggedness, int seed);
void ProceduralMesh_DrawFissure(const FissureMeshData *data, Color color);
```
- Distinct from flat 2D crack decals — real 3D geometry. Centerline via `SamplePath` (polyline, not Bezier ctrl points), `spacing=max(width*0.5,1.0)`, clamped to `FISSURE_MAX_SEGMENTS`. Each cross-section: 5-vert jagged "V" with seeded jitter on width/depth/offset. Negative `depth` = raised crack. **Build once at cast time, cache.**

### Post FX (`core/post_fx.h`)
```c
void PostFX_Init(int width, int height); void PostFX_Unload(void); // app lifecycle only
void PostFX_Begin(void); void PostFX_End(void);
void PostFX_Draw(const PostFXConfig *config); // runs Bloom -> CA -> Grade -> Vignette
typedef struct {
    bool bloomEnabled; float bloomThreshold, bloomIntensity;
    bool chromaticEnabled; float chromaticStrength;
    bool vignetteEnabled; float vignetteRadius, vignetteSoftness;
    bool colorGradeEnabled; float contrast, saturation; Vector3 colorTint;
} PostFXConfig;
```
Frame order: `Begin()` → draw 3D scene → `End()` → `Draw(&config)`.

### Camera FX (`core/camera_fx.h`)
```c
void CameraFX_Shake(float trauma); // 0.25 light, 0.5 medium, 0.75-1.0 heavy; cumulative, capped 1.0, auto-decays
void CameraFX_Update(Camera3D *camera, float dt); // engine loop only, after game logic update, before BeginMode3D
```
Skill code only calls `Shake`.
```c
typedef struct { float magnitude, duration, frequency, falloff; } CameraImpulse;
void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse); // core/skill_helper.h
```

### Audio
```c
void PlayCastSound(EffectPresetType preset);   // core/skill_helper.h — same enum as SpawnCastEffect
void PlayImpactSound(EffectPresetType preset); // same enum as SpawnImpactEffect
PlaySound(Sound sound);                        // skill-owned one-off; load via ResourceManager_LoadSound in InitSkill, cache, never LoadSound/UnloadSound directly
```
- Cast/Impact presets cached+played automatically, no per-skill cache layer needed. No Flight-stage sound preset yet.
- [!NOTE 2026-06-30] No SFX asset files exist yet under `assets/` — `PlayCastSound`/`PlayImpactSound` `TraceLog(LOG_WARNING)` once per missing preset, no crash. Content gap, not a stub to silently fix.

---

## 9. WIND ZONE GLOBAL (`core/force_field.h`)
`WindZone` = global `ForceField` auto-applied to **every particle** in `UpdateParticles()` — no per-config assignment needed.
```c
void WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq);
void WindZone_Clear(void);
bool WindZone_IsActive(void);
// WindZone_Evaluate() internal only, skill code never calls directly
```
| Param | Meaning | Suggested |
|---|---|---|
| direction | main wind dir (auto-normalized) | (1,0,0)=east |
| strength | base accel (m/s²) | 80–250 |
| noiseAmp | curl-noise overlay amplitude (0=straight) | 30–80 |
| noiseFreq | spatial noise frequency | 0.005–0.03 |

---

## 9b. SKILL HELPER (`core/skill_helper.h`)
High-level wrappers, reduce boilerplate; not mandatory, complex skills may call core API directly.

```c
typedef enum { EFFECT_PRESET_FIRE_EXPLOSION, EFFECT_PRESET_ICE_SHATTER, EFFECT_PRESET_WATER_SPLASH,
  EFFECT_PRESET_LIGHTNING_IMPACT, EFFECT_PRESET_EARTH_CRACK, EFFECT_PRESET_WOOD_BLOOM,
  EFFECT_PRESET_METAL_SHARD, EFFECT_PRESET_TAIJI_BURST } EffectPresetType;

void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale);
```
All 6 elements covered (each pre-tinted, paired with a decal preset): `WOOD_BLOOM`→`DECAL_PRESET_WOOD_MOSS`, `METAL_SHARD`→`DECAL_PRESET_METAL_SLASH` (high pitch range), `TAIJI_BURST`→`DECAL_PRESET_TAIJI_RING` (stronger light, "no-element" ultimate).

```c
void SpawnCastEffect(Vector3 pos, EffectPresetType preset, float scale);
```
Cast/windup equivalent — no knockback/decal. Ring-spawn particles pulled inward via `FORCE_GRAVITY_POINT` ("energy gathering") + light flash. Call at start of cast/windup. Backed by 8-slot static `ForceField` pool (`MAX_CONCURRENT_CAST_EFFECTS`), round-robin — concurrent casts don't interfere.

```c
int SpawnProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed);
```
Flight-stage equivalent. Spawns `TRAIL_TYPE_PROJECTILE` + head particle w/ `onLiveEmit` tail dust. Returns trail ID — **caller MUST `KillTrail(id)` on impact**, before `SpawnImpactEffect`. Uses **sustained/flight force regime** (300-650f `FORCE_GRAVITY_DIR` + ~20f `FORCE_NOISE_PERLIN`), NOT the 5-60f ambient range. 8-slot pool (`MAX_CONCURRENT_PROJECTILE_TRAILS`), round-robin.

```c
typedef enum { SHAPE_CIRCLE, SHAPE_BOX, SHAPE_CONE } ShapeType;
typedef struct { ShapeType shape; Vector3 center; float radius; float damagePerSecond, tickInterval, duration; bool active; float timer, tickTimer; } DamageVolume;
void DamageVolume_Init(void); void DamageVolume_Update(float dt);
int  SpawnDamageVolume(DamageVolume config); // returns ID
void DamageVolume_Unload(void);

typedef struct { float current, duration; } SkillTimeline;
void Timeline_Start(SkillTimeline *t, float duration);
bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt); // true exactly 1 frame at trigger
bool Timeline_Finished(SkillTimeline *t);
// orchestrate multi-step event sequences w/o manual state machine

typedef enum { EMITTER_FIRE, EMITTER_SNOW, EMITTER_WATER_SPURT, EMITTER_SHOCKED_SPARKS,
  EMITTER_WOOD_LEAVES, EMITTER_EARTH_DUST, EMITTER_METAL_SPARKS, EMITTER_TAIJI_MOTES } EmitterPreset;
void EmitterSystem_Init(void); void EmitterSystem_Update(float dt);
int  Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration);
void Emitter_Stop(int emitterId); void EmitterSystem_Unload(void);

typedef enum { MESH_PRESET_DISC, MESH_PRESET_RING, MESH_PRESET_CONE, MESH_PRESET_TORNADO,
  MESH_PRESET_CYLINDER, MESH_PRESET_SPHERE, MESH_PRESET_SHOCKWAVE, MESH_PRESET_PYRAMID, MESH_PRESET_TETRAHEDRON } MeshPresetType;
void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color);

typedef enum { MATERIAL_FIRE, MATERIAL_ICE, MATERIAL_WATER, MATERIAL_PORTAL } MaterialPreset;
typedef struct { Shader shader; MaterialPreset preset; int uTimeLoc, uDissolveLoc; } EffectMaterial;
EffectMaterial Material_Load(MaterialPreset preset);
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
void Material_Begin(EffectMaterial mat); void Material_End(void);

typedef enum {
  DECAL_PRESET_CRACK, DECAL_PRESET_EARTH_SHATTER, DECAL_PRESET_EARTH_RUNE,
  DECAL_PRESET_BURN, DECAL_PRESET_FIRE_LAVA,
  DECAL_PRESET_WATER, DECAL_PRESET_WATER_SPLASH, DECAL_PRESET_WATER_RIPPLE, DECAL_PRESET_ICE,
  DECAL_PRESET_WOOD_ROOT, DECAL_PRESET_WOOD_MOSS,
  DECAL_PRESET_METAL_SLASH, DECAL_PRESET_METAL_CRATER, DECAL_PRESET_METAL_RUNE,
  DECAL_PRESET_TAIJI_RING, DECAL_PRESET_TAIJI_LIGHTNING, DECAL_PRESET_TAIJI_WIND,
  DECAL_PRESET_GENERIC_IMPACT_RING, DECAL_PRESET_GENERIC_GLOW, DECAL_PRESET_GENERIC_SHADOW // untinted, caller applies own Color
} DecalPresetType;
void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration);
```
- All 6 elements have ≥2 ground-mark presets, pre-tinted via `ELEMENT_COLOR_*` (except GENERIC_*) — no Color param needed.
- Textures: `assets/textures/decals/` (per-element), `assets/textures/generic/` (untinted reusable).
- `CRACK`/`BURN`/`ICE`/`WATER` = original 4, kept for compat. `ICE` now uses real frost texture (was `dust_wind.png` placeholder).

```c
typedef enum { FORCE_PRESET_FIRE_UPDRAFT, FORCE_PRESET_SNOW_BLIZZARD, FORCE_PRESET_WATER_VORTEX } ForceFieldPreset;
ForceField ForceField_CreatePreset(ForceFieldPreset preset);

typedef struct {
  Vector3 target; float scale;
  bool hasExplosion; EffectPresetType explosionEffect;
  bool hasDecal; DecalPresetType decalType; float decalRadius, decalDuration;
  bool hasDamageVolume; float damageRadius, damageDps, damageDuration;
} SkillBuildContext;
void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale);
void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx);
void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration);
void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration);
void SkillBuilder_Build(SkillBuildContext *ctx); // fires all at IMPACT time
void SkillBuilder_AddCastEffect(SkillBuildContext *ctx, EffectPresetType preset); // own trigger point, call at CAST time after Start
```
`AddCastEffect` fires `SpawnCastEffect` immediately (cast time), unlike other `Add*` which defer to `Build()` (impact time) — different lifecycle points.

---

## 10. GLSL SHADER GUIDELINES

### `#include` is engine preprocessing, not native GLSL
> [!IMPORTANT] Resolved by `ShaderPreprocessor_Load()` (in `shader_preprocessor.h/.c`), wired into `ResourceManager_LoadShader()`. Recursively textually substitutes `#include "..."` (up to `MAX_INCLUDE_DEPTH`) before `LoadShaderFromMemory()`. Heap buffer (`RL_MALLOC`/`RL_FREE`) handled internally — skill code never touches it, never calls `ShaderPreprocessor_Load()` directly.
> Raw `glCompileShader`/standalone GLSL linters will fail on `#include` lines — expected, not a bug. Only `ResourceManager_LoadShader()` produces compilable output.

| File | Used in | Provides |
|---|---|---|
| `vs_header.glsl` | every `.vs` | attributes, uniforms, varyings, `VS_FinalOutput()` |
| `fs_header.glsl` | every `.fs` | received varyings, env uniforms, `finalColor` |
| `lighting.glsl` | `.fs` needing lighting | `perturbNormal`, `calcFresnel`, `calcSpecular`, `calcDiffuse` |
| `noise.glsl` | `.vs`/`.fs` needing noise | `hash2`, `hash3`, `vnoise`, `fbm2`, `fbm2N` |
| `fx.glsl` | `.fs` needing effects | `dissolveCalc`, `flowBlend`, `emissiveMask` |

Include order: `fs_header.glsl` → `noise.glsl` (if needed) → `lighting.glsl` → `fx.glsl`. `fx.glsl` doesn't depend on `noise.glsl`. **Never reimplement hash/noise/fbm/dissolve/flowblend in skill code.**

Required includes:
```glsl
// VS
#version 330
#include "core/shaders/common/vs_header.glsl"

// FS — full 3D lighting
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"

// FS — minimal (dissolve only, no 3D lighting)
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"
```
- Custom 3D lighting skills MUST load both `.vs`+`.fs` via `ResourceManager_LoadShader()`. Only unlit shaders may pass `NULL` vs. Never `NULL` vs with lighting.

### Built-in variables — DO NOT redeclare in skill shaders

**`vs_header.glsl` (VS only):**
| Var | Dir | Type | Notes |
|---|---|---|---|
| `vertexPosition` | in | vec3 | object/local space, raw |
| `vertexTexCoord` | in | vec2 | raw mesh UV |
| `vertexNormal` | in | vec3 | object/local, NOT yet normalized/transformed |
| `mvp` | uniform | mat4 | used internally by `VS_FinalOutput()` |
| `matModel` | uniform | mat4 | used internally by `VS_FinalOutput()` |
| `u_time` | uniform | float | auto-bound by `SkillManager_BeginShader()` — never set manually |
| `viewPos` | uniform | vec3 | auto-bound |
| `u_resolution` | uniform | vec2 | auto-bound |
| `fragPosition` | out | vec3 | world-space, written only by `VS_FinalOutput()` |
| `fragTexCoord` | out | vec2 | passthrough |
| `fragNormal` | out | vec3 | world-space normalized, written by `VS_FinalOutput()` |

**`fs_header.glsl` (FS only):**
| Var | Dir | Type | Notes |
|---|---|---|---|
| `fragPosition` | in | vec3 | world-space |
| `fragTexCoord` | in | vec2 | UV passthrough |
| `fragNormal` | in | vec3 | world-space normalized |
| `u_time` | uniform | float | auto-bound, never set manually |
| `viewPos` | uniform | vec3 | auto-bound |
| `u_resolution` | uniform | vec2 | auto-bound |
| `finalColor` | out | vec4 | write exactly once per `main()` |

> [!NOTE] **`fragNormal` caveat**: computed from original `vertexNormal` only — NOT recomputed from displaced surface. If VS displaces position, `fragNormal` won't reflect it. Pattern: re-derive perturbed normal in FS via `perturbNormal()` with matching height-field gradient (see `tube.fs`/`getDisplacement()`). True displaced-geometry normal requires manual VS finite-difference — not automatic.

### Built-in functions

**`lighting.glsl`:**
```glsl
vec3  perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength); // strength typical 0.3-0.8
float calcFresnel(vec3 normal, vec3 viewDir, float power);              // power typical 2.0-5.0
float calcSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess); // shininess typical 32-512
float calcDiffuse(vec3 normal, vec3 lightDir, float ambient);           // ambient typical 0.10-0.25
```
- `perturbNormal`: `heightDelta = vec2(h(u-eps)-h(u+eps), h(v-eps)-h(v+eps))`, gradient of skill's own height fn — MUST reuse exact same formula as VS displacement, else lighting/displacement mismatch visually.
- Project-standard `lightDir` (hard-code in every skill, no auto-bound uniform exists):
```glsl
vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
```

**`noise.glsl`:**
```glsl
float hash2(vec2 p); float hash3(vec3 p);     // pseudo-random hash
float vnoise(vec2 p);                          // value noise [0,1], faster than Perlin, NOT named noise2 (GLSL builtin conflict)
float fbm2(vec2 p);                            // 3-octave FBM, has built-in rotation (avoids axis artifacts)
float fbm2N(vec2 p, int octaves);              // N-octave (1-6), 1-2 for soft wind/aura, 5-6 for bark/rock
```
`hash3(floor(fragPosition*scale))` for world-space dissolve.

**`fx.glsl`:**
```glsl
float dissolveCalc(float noiseVal, float dissolve, float edgeWidth, out float edgeFactor); // returns 1.0=discard pixel, 0.0=keep; edgeFactor=burn edge mix amount
float flowBlend(sampler2D tex, vec2 uv, vec2 flowDir, float speed, float strength, float time); // 2-phase blend, no seam on reset; returns luminance
float emissiveMask(vec3 worldPos, float freq, float threshold); // sine-based on world pos, not UV-distorted
```
Do not reimplement these.

> [!NOTE] No auto-bound `lightDir` uniform exists — both lighting fns take it as a param. Reuse `normalize(vec3(0.5,0.8,0.5))` for consistency (project's fixed key light). Update this section if engine later exposes shared `u_lightDir`.

### Custom per-skill uniforms (e.g. `u_uvLength`, `u_dissolve`)
Not handled by `SkillManager_BeginShader()` — skill C code responsible.
- Cache uniform location once (`static int`) via `GetShaderLocation()` in `Init[Name]Skill()` — never every frame (uncached string-hash lookup).
- `SetShaderValue()` AFTER `SkillManager_BeginShader(shader)`, BEFORE the draw call, every frame value changes (or once if constant).
- Same uniform name declared in both `.vs`+`.fs` → ONE `SetShaderValue()` call updates both (one linked GL program, `Shader.id`). No "set twice per stage" needed.
- Declare uniforms only in the stage(s) that read them.

### Rules
- Always both `.vs`+`.fs` for 3D shaders. Include `fs_header.glsl` before `lighting.glsl`. Call `VS_FinalOutput()` as VS's final step. Declare only skill-specific uniforms. `VS_FinalOutput()` MUST receive exactly one `vec3` (final processed/displaced vertex position).

### Android / GLES Compatibility Rules
Two build paths depending on whether shader uses `#include`:

**Path 1 — Standalone (no `#include`):** `scripts/convert_shaders_to_gles.py` converts to GLES 1.00 (`#version 100`) at APK build:
| Desktop GLSL 3.3 | GLES 1.00 |
|---|---|
| `in vec3 pos` (VS) | `attribute vec3 pos` |
| `out vec3 vary` (VS) | `varying vec3 vary` |
| `in vec3 vary` (FS) | `varying vec3 vary` |
| `out vec4 finalColor` + uses | removed, → `gl_FragColor` |
| `texture(sampler,uv)` | `texture2D(sampler,uv)` |
| precision (FS) | auto-injects `precision highp float;` if missing |

Script does NOT fix: `f` suffix on float literals, `.vs` precision, `#include` contents. Requires GLES 2.0+.

**Path 2 — Uses `#include` common headers:** Build script skips these (stay `#version 330` in APK). At runtime, `ResourceManager_LoadShader`→`ShaderPreprocessor_Load`: (1) recursively expand all `#include`, (2) `RewriteVersionForGLES()` converts `#version 330`→`#version 300 es`, (3) result = valid GLES 3.0 with `in`/`out`/`texture()`. Common headers already have `#ifdef GL_ES precision highp float; #endif` — no extra declaration needed; both VS+FS use `highp float` (important, see Rule E). Requires GLES 3.0+.

**Rule A — No `f` suffix on float literals (BOTH paths):**
```glsl
// WRONG — rejected by Android GLES strict compiler, build script does NOT fix:
float breathe = 1.25f + 0.12f * sin(u_time * 5.5);
// RIGHT:
float breathe = 1.25 + 0.12 * sin(u_time * 5.5);
```
`f` suffix is C syntax; desktop driver ignores it, Android GLES rejects → `shader.id = 0`.

**Rule B — Standalone VS must declare precision itself:**
Build script auto-injects precision for standalone `.fs` only, NOT `.vs`. Any standalone VS (no `#include vs_header.glsl`) needs:
```glsl
#version 330
#ifdef GL_ES
precision highp float;
#endif
```
Shaders using common headers already have this — no redeclaration.

**Rule C — Shader compile failure behavior on Android:**
`ResourceManager_LoadShader` does NOT crash on compile fail — returns `shader.id = 0`, logs `SHADER: compile failed, not caching (vs=... fs=...)`. `SkillManager_BeginShader` guards `id==0` → no-op (skips `BeginShaderMode`). Skill still runs but renders with default flat shader → mesh appears **flat white / no effect**. If a skill renders all-white on Android: check logcat for that line, fix per Rule A/B, rebuild APK.

**Rule D — `matModel` must be set manually with rlgl immediate mode:**
`VS_FinalOutput()` computes `fragNormal = normalize(matModel * vertexNormal)`. Raylib only uploads `matModel` via `DrawMesh`/`DrawModel` — NOT via rlgl immediate mode (`rlBegin`/`rlEnd`/`ProceduralMesh_DrawTube`...). On Android GLES 3.0, unset `matModel` = all-zeros → `normalize(vec3(0,0,0))` = NaN on Adreno/Mali → `fragNormal=NaN` → `clamp(NaN,0,1)=1.0` → solid white. Desktop GL driver handles `normalize(zero)` differently — bug invisible on Mac.
**`SkillManager_BeginShader` auto-sets `matModel=identity`** before `BeginShaderMode` — no action needed if using it.
If calling `BeginShaderMode` directly (bypassing `SkillManager_BeginShader`), MUST set manually:
```c
if (s_shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0) {
    Matrix identity = MatrixIdentity();
    SetShaderValueMatrix(s_shader, s_shader.locs[SHADER_LOC_MATRIX_MODEL], identity);
}
```

**Rule E — VS/FS must match precision for shared uniforms (GLES 3.x strict, e.g. Mali-G68, GLES 3.2):**
A uniform appearing in both VS+FS must have identical precision qualifier in both, else link failure → `shader.id=0` → white. Logcat: `Link error: L0001 ... precision does not match`.
Common headers already consistent: both use `highp float` default → `u_time`/`viewPos`/`u_resolution` and any default-precision uniform match across stages automatically.
Skill-declared shared uniforms (e.g. `uniform float u_uvLength;` in both `.vs`+`.fs`) inherit default precision from each header (`highp` both) → automatically match.
If a skill explicitly declares lower precision (e.g. `precision mediump float;`) in standalone FS, VS must match (`mediump`) — or better, use `highp` consistently in both.

> [!NOTE] Desktop OpenGL often compiles successfully despite `f` suffix or missing precision, and handles `normalize(zero)` differently than mobile — these bugs typically only surface on real Android device testing (GLES strict mode).

---

## 11. 3D RENDERING & SHADER BEST PRACTICES

### 11.1 Vertex Color Reset
Before `rlBegin()` custom geometry, always reset vertex color:
```c
#include "rlgl.h"   // required for rlBegin/rlColor4ub/rlVertex3f/rlEnd — NOT pulled in by raylib.h
rlColor4ub(255, 255, 255, 255);
```
Otherwise mesh may inherit color from previous draw calls. `rlgl.h` is separate from `raylib.h` — skills including only `raylib.h` fail with `implicit declaration of function`, not an obvious missing-header error.

### 11.2 Procedural Noise
Low world-coordinate scales (e.g. `fragPosition.xz * 0.05`). Avoid high frequencies (TV-static artifacts). Stretch axes for directional patterns.

### 11.3 3D Lighting
Default Raylib VS cannot be used for custom 3D lighting. Always: both `.vs`+`.fs`, load via `ResourceManager_LoadShader()`, never `NULL` vs, use `vs_header.glsl` + `VS_FinalOutput()`.

---

## 12. CRITICAL AESTHETIC LAWS (ANTI-ROBOTIC DESIGN)

### 12.1 No Raylib Primitives
Never `DrawCylinder()`/`DrawCone()`/`DrawCube()`/`DrawSphere()`/wireframe variants for core skill meshes. Use procedural meshes or engine mesh APIs (`DrawCore*`).

### 12.2 Perpendicular Jitter
Avoid perfectly straight layouts:
```c
Vector3 perp = { -dir.z, 0.0f, dir.x };
float jitter = (float)GetRandomValue(-120, 120) / 10.0f;
Vector3 organicPos = Vector3Add(pos, Vector3Scale(perp, jitter));
```

### 12.3 Instance Randomization
Randomize every spawned instance: Scale 85–115%, random yaw 0–360°, pitch/roll ±10°.

### 12.4 No Visual Popping
Keep same shader active throughout Rising/Active/Dissolve. `u_dissolve=0.0` until dissolve phase, then smoothly animate to `1.0`.

### 12.5 Preserve 3D Volume
Restrict emissive regions via `smoothstep()` (~20-30% coverage). Shade rest with diffuse + Fresnel. Add emissive AFTER base lighting to preserve brightness.
