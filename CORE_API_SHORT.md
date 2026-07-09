# WUXING SKILLS CORE API — SHORT FORM
> Compact AI-reference condensation of `CORE_API.md`. Section numbers mirror the full doc. Manual-only, not auto-synced — regenerate on explicit request only.

---

## 1. COMPILATION & ARCHITECTURE RULES

### 1.1 Language & Standards
- Strict **C99**, Raylib **6.0**, OpenGL 3.3 Core, 3D Isometric Night-time Arena.
- Every skill `.c` MUST `#include <stddef.h>`, `<stdlib.h>`, `<stdio.h>` explicitly (NULL/snprintf not implicitly available).
- Include paths relative from root: `#include "core/particle_system.h"`.
- `PI`: raylib already defines it (`3.14159265358979323846f`). **NEVER** bare `#define PI`. Guard:
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

### Scaling rules
Engine is fully meter-scaled (1 unit = 1 meter) — conversion complete across `entities/`, `sandbox/`, `main.c`, `maps/*`, and every skill under `skills/`. DO NOT use the old 100x scale for any new skill.
| Quantity | Range |
|---|---|
| Base mesh/tube radii | 0.10f–0.20f (10–20 cm) |
| Impact burst/light radius | 0.5f–1.5f |
| Particle speed | 1.0f–3.0f (m/s) |
| Gravity/force | 3.0f–9.8f (realistic physical defaults). **Do not** use the old 300–700f ranges. |

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
Font      ResourceManager_LoadFont(const char *filePath, int baseSize); // cached by (path,baseSize); falls back to GetFontDefault() if missing (LOG_WARNING, never fails)
```
- **NEVER** call `UnloadTexture`/`UnloadShader`/`UnloadSound` in `Unload[Name]Skill` — leave empty/commented. Global ResourceManager frees all on app shutdown.
- `ResourceManager_LoadFont` is bilinear-filtered (scales smoothly via `DrawTextEx` at other sizes); used by `sandbox/ui_panel.c` debug UI. `assets/fonts/` convention.

---

## 3b. DATA-DRIVEN TUNING (`core/tuning.h`)
Register a `float` as tunable via `tuning.cfg` (project root) — edit while game runs, no rebuild.
```c
void Tuning_Init(const char *configPath);   // main.c only, once at startup
bool Tuning_RegisterFloat(const char *key, float *value, float defaultValue);
void Tuning_Update(void);                   // main.c only, once per frame
void Tuning_Reload(void);                   // force immediate reload (debug hotkey)
```
- Skill only ever calls `Tuning_RegisterFloat` in `Init[Name]Skill` — sets `*value=defaultValue` immediately, then overwrites if key exists in `tuning.cfg`.
- Format: `key = value` per line, `#` comments, floats only. Missing key keeps current value (no reset-to-default).
- `Tuning_Update()` overwrites the registered float in place but can't fix values already copied elsewhere (e.g. baked into a `ParticleConfig` at Cast time) — re-read the float each frame if a live edit must reach an on-screen effect.
- Desktop-only hot-reload; `tuning.cfg` ships in the Android bundle but no live-reload story there.

**Per-path save/load (sandbox live-tuning UI, separate from `tuning.cfg` hot-reload):**
```c
bool Tuning_LoadFloatsFromPath(const char *path, const char *const *keys, float *outValues, int count);
bool Tuning_SaveFloats(const char *path, const char *const *keys, const float *values, int count);
```
- One-shot, not registered into per-frame hot-reload — for a UI-driven "pick a skill → edit sliders → Save" flow. Missing key leaves `outValues[i]` untouched (pre-fill defaults first). Returns `false` if file doesn't exist.
- Backs `RegisterSkillTunables`' persisted `.tuning` files, e.g. `skills/fire/fire_ball/fire_ball.tuning`.

---

## 3c. SOFT PARTICLES (Depth Blending)
Global linearized depth buffer for effects that should fade smoothly into solid geometry instead of clipping.
1. Include `soft_particle.glsl` in `.fs`.
2. Manually declare `uniform sampler2D u_cameraDepthTex;` — not auto-declared.
3. In `main()`: `SoftParticle_Factor(u_cameraDepthTex, fragTexCoord, gl_FragCoord.z, fadeDistance)` → `0.0` (occluded) .. `1.0` (unoccluded); multiply into final alpha. `fadeDistance` = world-unit fade distance.
4. C-side, in `Draw[Name]Skill`, bind BEFORE drawing and unbind after (use a texture unit not used by other textures, e.g. 3):
```c
ScreenDistort_BindDepthForSoftParticles(myShader, 3);
// ... draw mesh/particles ...
ScreenDistort_UnbindSoftParticleDepth(3);
```
> [!IMPORTANT] If disabling depth write/test (`rlDisableDepthMask()`) for a soft-particle pass, call `rlDrawRenderBatchActive()` **first** to flush the batch — otherwise the state change retroactively disables depth-writing for previously queued geometry (e.g. the ground plane).

---

## 3d. CAMERA SHAKE & SCREEN DISTORT
- `CameraFX_Shake`: defaults to **off (0.0f)**. Never add shake to a default skill without explicit approval — always expose it as a tunable defaulting to `0.0f` so it's opt-in via Sandbox.
- `ScreenDistort_Add`: avoid overuse (visually noisy) — reserve for Water-element skills (e.g. Hydro Cleave) or on explicit request.

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
void Cast[Name]Skill(int agentId, Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);
bool Is[Name]SkillCoiling(void);
int  Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);
```
`agentId` = caster's agent-pool slot (0..255), auto-forwarded by `CastSkill()`. Store as `int ownerAgentId;` in the per-instance struct at cast time — ownership tracking for `AbortSkill(skillIndex, agentId)`, nothing else. `Update[Name]Skill` has no agentId; skills can't read the live `Agent` array directly (see §16 Agent Providers for the sanctioned indirect path).

> [!NOTE] The 4 skeleton templates (Generic Projectile, Ground-Rising, Anchored-Along-Path, Entity-Attached) now live in a dedicated **`SKILL_SKELETONS.md`** — CORE_API.md itself only points there, no longer inlines the state-machine breakdowns. Consult that file for the per-state code shapes; this doc only guarantees the lifecycle prototypes above.

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
| `FORCE_VECTOR_TEXTURE` | box center (xz) | box half-extent (xz) | multiplier | 0 (must be) | 0 (must be) | tex slot 0/1 (int) | 0 (must be) |

- `RADIAL_AXIS`/`VORTEX_AXIS` ignore struct `origin`/`direction` — use per-frame `axisOrigin`/`axisDir` (e.g. via `SetFollowerAxis()`).
- Falloff: 0=constant, 1=linear-to-zero at radius, 2=quadratic.
- `ForceField_GetViscosityDamping(&s_forceField, dt)` → scale velocity manually in custom update loops.
- `FORCE_VECTOR_TEXTURE` (GPU-only): samples a world-space flow texture. RG channels = XZ flow dir remapped `[-1,1]→[0,1]`. **CPU path (`particle_system.c`/`trail_system.c`) treats this as a no-op** — only `GpuParticleSystem` (COMPUTE path) samples it via `GpuParticleSystem_SetVectorFieldTexture()`. Unverified on real GPU hardware (macOS caps at GL 4.1, never exercises COMPUTE path).

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
    const SkillCurve *radiusCurve;        // multiplies drawn radius, sampled at t01=1-lifeRatio
    const SkillCurve *speedCurve;         // multiplies this frame's position step from velocity (velocity itself untouched)
    const SkillCurve *alphaCurve;         // multiplies colorStart.a, overrides colorStart/End/gradient alpha (RGB unaffected)
    const ParticleConfig *onDeathEmit; int onDeathEmitCount;  // sub-emitter on death
    const ParticleConfig *onLiveEmit;  float onLiveEmitRate;  // sub-emitter along path
} ParticleConfig;
```
- `gradient` non-NULL → `colorStart`/`colorEnd` ignored.
- Sub-emitters inherit position, NOT velocity. Configs passed to sub-emitters MUST be `static`.
- `radiusCurve`/`speedCurve`/`alphaCurve` default `NULL` (today's legacy behavior). Each sampled fresh every frame via `SkillCurve_Eval` at `t01 = 1.0 - lifeRatio` (0 at spawn, 1 at death — same convention `gradient` uses). Mechanism for per-phase "size/speed/opacity over lifetime" tunables — see `fire_skill.c`/`thunder_orb_skill.c`: one `static SkillCurve` per phase/property, seeded flat via `SkillCurve_SetConstant`, wired as a curve-kind `SkillTunableEntry` (§8 Tunable Parameters).

---

## 7. TRAIL & RIBBON SYSTEM (`core/trail_system.h`)
`TrailConfig` init with `{0}`. `int SpawnTrailEntity(TrailConfig config);`
> `MAX_TRAIL_PARTICLES = 500` — single pool shared project-wide. When full, `SpawnTrailEntity` evicts the lowest-`priority` active trail (ties → shortest remaining life) instead of rejecting; returns `-1` only if every active trail already has strictly higher priority.

```c
typedef enum { TRAIL_TYPE_PROJECTILE, TRAIL_TYPE_WISP, TRAIL_TYPE_PORTAL, TRAIL_TYPE_FOLLOWER } TrailType;
typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);

typedef struct {
    TrailType type;
    Vector3 pos, vel, target;
    float len, thick, trailLength, life;       // life = total duration (s); MUST be > 0 — life<=0 dies on next UpdateTrailSystem tick, does NOT persist
    float initialAngle, wobblePhase, scale;
    Texture2D tex; Color tint; Shader shader;
    TrailUpdateCallback onUpdate; TrailDeathCallback onDeath;
    int ownerTag;                              // caller-defined, not read internally
    float wobbleAmplitudeOverride, curveRangeOverride; // >0 overrides global; <=0 default
    const ForceField *forceField; const ColorGradient *gradient; const SpriteAnim *spriteAnim;
    VFXPriority priority;                      // core/vfx_light.h enum; default VFX_PRIORITY_LOW from {0}
} TrailConfig;
```
- `priority`: additive field, backward compatible (`TrailConfig cfg={0}` still defaults to LOW). Set `VFX_PRIORITY_HIGH_ULTIMATE` for a cast that must not lose its trail to pool pressure.
- `TRAIL_TYPE_FOLLOWER`: three ways to drive the tip — (1) manual: `UpdateFollowerPosition(trailId, tipPos)` each frame before `UpdateTrailSystem`; (2) matrix attachment: `Trail_AttachToTransform(trailId, &myMatrix, localOffset)` once — `UpdateTrailSystem` reads `*myMatrix` each frame, `tip = Vector3Transform(localOffset, *myMatrix)`; `Matrix` must stay valid for the trail's lifetime (typically `static Matrix`); detach with `Trail_AttachToTransform(id, NULL, (Vector3){0})`; (3) `Trail_SetFollowerOrbit(trailId, radius, speed, axis, phase)` — makes a matrix-attached trail auto-orbit its `localOffset` point around `axis` (normalized) at `radius`, `speed*dt` rad/frame, starting at `phase`; `radius`/`speed`=0 disables. `SetFollowerAxis(trailId, basePos, normalizedDir)` sets radial-axis orientation for `FORCE_RADIAL_AXIS`, unrelated to tip position.
- **FOLLOWER's `trailLength` = integer history-node count** (e.g. `20.0f` = 20 nodes), not a fractional ratio — `(int)trailLength` taken directly. Renders only when `historyCount > 1` (values < 2.0f = no visible trail).
- `KillTrail(trailId)` to free when complete.
- `onDeath` fires once on `life` expiry OR (PROJECTILE only) auto hit-detect on `target` — spawn impact VFX at exact last position.
- `onUpdate` fires every frame post-physics; call `GetTrail(trailId)` inside for live pos/vel.
- `ownerTag`: tag caster/instance, read via `GetTrail(id)->ownerTag`.
- Per-instance PROJECTILE overrides: `wobbleAmplitudeOverride=0.001f` + `curveRangeOverride=1.0f` → near-straight, snap-to-target trail (e.g. sword qi). Leave `0` for default global-macro behavior.

---

## 8. GRAPHICS & VFX API

### Ground Decals (`core/decal_system.h`)
```c
void DecalSystem_Init(void);
void DecalSystem_Add(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);
void DecalSystem_AddEx(Vector3 pos, float rot, float rotSpeed, float scaleStart, float scaleEnd, Texture2D tex, float life, Color tint, BlendMode blendMode, float yOffset);
void DecalSystem_AddFlowEx(Vector3 pos, float rot, float rotSpeed, float scaleStart, float scaleEnd, Texture2D tex, float life, Color tint, BlendMode blendMode, float yOffset, float flowSpeed, float flowStrength);
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
- `AddFlowEx`: texture radially scrolls outward from decal center over time (`core/shaders/decal_flow.fs`) instead of static — lava-crack-crawl/ripple-spreading visuals. `flowSpeed` ~0.3–1.0 (radial units/sec), `flowStrength` ~0.5–1.0 (0=identical to static, 1=fully scrolled). Separate shader pass, doesn't affect `Add`/`AddEx`.

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

### Metaballs / Screen-Space Fluid (`core/metaball_fx.h`)
```c
void MetaballFX_RegisterBlob(Vector3 worldPos, float radius); // skill API — only this one
// Init/Unload/DrawRegistered are engine-internal (main.c), never call from skill code
```
- Register every frame per blob (projectile head, lava droplet...) — blob lives exactly 1 frame, must re-register continuously. `MAX = 32` shared engine-wide.
- **Never call `MetaballFX_DrawRegistered`** — runs raw GL (BeginTextureMode/EndTextureMode) and must run outside `BeginMode3D`/`EndMode3D`; already wired in `main.c` after `PostFX_Draw()`.
- Pure screen-space 2D — always draws on top, no depth-test vs 3D scene.
- Tint is one engine-wide fixed color (`main.c` passes `ELEMENT_COLOR_WATER` for every blob of every skill) — not per-skill/per-element yet.
- `threshold`/`smoothness`: lower threshold + higher smoothness = blobs merge more easily.

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

### Float Curve (`core/float_curve.h`)
Scalar equivalent of `ColorGradient` — same shape (`AddStop`/`Sample`, same 8-stop cap, LERP-between-adjacent-stops).
```c
typedef struct { float t, value; } FloatCurveStop;                                  // t in [0,1]
typedef struct { FloatCurveStop stops[FLOAT_CURVE_MAX_STOPS]; int count; } FloatCurve; // max 8 stops
bool  FloatCurve_AddStop(FloatCurve *c, float t, float value);  // caller must add in increasing t order
float FloatCurve_Sample(const FloatCurve *c, float t);          // clamps outside registered range
```
Use for any plain `float` shaping itself over a skill's lifetime. Maps to `WUXING_ART_DIRECTION.md` §4.3 "Four Curves" (Intensity/Density/Motion/Lighting).
- **`core/skill_curve.h`'s `SkillCurve`** (`typedef FloatCurve SkillCurve`) is the sandbox-tunable-wired specialization — fixed 5-stop convention (`SKILL_CURVE_KEYS`, t=0/25/50/75/100%) so it renders as 5 sliders, not a free-form stop editor. Use `SkillCurve` (not a raw `FloatCurve`) for anything registered via `SkillTunableEntry.curve`; use a raw `FloatCurve` only for curves internal to a skill, never sandbox-exposed.

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
> [!IMPORTANT] The skill's `.fs` **MUST declare the flow sampler with the exact name `flowTex`** (`uniform sampler2D flowTex;`) — `FlowMap_Create*` resolves `flowTexLoc` via `GetShaderLocation(shader, "flowTex")`. A different name (e.g. `texture0`) → `flowTexLoc == -1` → `FlowMap_Apply` silently no-ops the texture bind, **no error/warning**. Sampler then reads raylib's default fallback (solid white) → `flowBlend(...)` returns ~1.0 luminance everywhere → shader math blows out to solid white. Confirmed root cause of a real `tsunami_skill` bug (2026-06-30) — same failure shape as §10's `matModel` silent-no-op gotcha.

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

### Particle Radial Burst (`core/particle_system.h`)
```c
typedef struct {
    int countMin, countMax; float speedMin, speedMax, radiusMin, radiusMax, lifetimeMin, lifetimeMax;
    float pitchRange, upwardBias; Color colorStart, colorEnd;
    const ColorGradient *gradient; const ForceField *forceField;
} ParticleRadialBurstConfig;
void ParticleSystem_SpawnRadialBurst(Vector3 origin, float sizeScale, const ParticleRadialBurstConfig *cfg);
```

### Impact Burst (`core/composition/visual_composer.h`)
Full `ImpactBurstConfig` struct, `VFX_ImpactPreset`, and `VFX_TriggerImpactBurst` now live in **`COMPOSITION_API.md`** — not inlined here. Key invariants (2026-07-07 rewrite):
- `particles.speedMin/Max` are **direct m/s** — no internal throttle factor (old 0.3×/0.4× multiplier removed).
- `colorStart` auto-resolved from `gradient` at t=0 inside `TriggerImpactBurst` — gradient-only configs (colorStart.a==0) render correctly.
- `VFX_ImpactPreset.decalTint` defaults to WHITE when `{0,0,0,0}`; set a dark color to avoid a bright decal competing with the particle cloud.
- Keep `lightRadius` ≤ 0.4m (before sizeScale) so the flash doesn't bleach particles.

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
typedef enum { VFX_PRIORITY_LOW = 0, VFX_PRIORITY_HIGH_ULTIMATE } VFXPriority;
void VFXLight_Init(void); void VFXLight_Reset(void);
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime, VFXPriority priority); // auto-expires, no manual kill
void VFXLight_Update(float dt);
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount); // maxCount <= 16, call before drawing lit skill
```
- `priority`: **breaking signature change** — when pool full, evicts lowest-priority active light (ties → shortest remaining life) if its priority is `<=` incoming; only drops the new spawn if every slot is strictly higher priority. Same eviction pattern reused by `TrailConfig.priority` (§7).
- Keep lights alive for skill's full active phase, not spawn+kill same frame.

### Procedural Ray VFX (`core/vfx_proc_ray.h`)
Managed pools (32 rays + 32 bolts) for lightning/energy strands. Spawn→Update+Draw each frame→Kill.
```c
typedef struct { Color colorCore, colorGlow; float glowWidthMult, waveSpeed, amplitudeRatio,
  jitterStrength, thickness, envelopePow; bool sharpKinks; float taperTip; int branchCount; float branchScale; } ProcRayConfig;
ProcRayConfig ProcRay_LightningConfig(void);     // violet/white, high jitter — free-end rays
ProcRayConfig ProcRay_BoltLightningConfig(void); // low amplitude, branchCount=3 — fixed sky→ground bolts
ProcRayConfig ProcRay_EnergyConfig(void);        // cyan/gold smooth   | ProcRay_WindConfig(): white/teal, low amp
// Free-end ray (origin fixed, far end whips):
int  SpawnProcRay(ProcRayConfig cfg, float scale);
void ProcRay_SetPhase(int id, float phase); void ProcRay_SetBrightness(int id, float b);
void ProcRay_Update(int id, Vector3 origin, Vector3 dir, float length, float scale, float dt);
void ProcRay_Draw(int id, Camera3D cam); void ProcRay_Kill(int id);
// Fixed bolt (both ends pinned, jagged middle re-flickers every 50ms):
int  SpawnProcBolt(ProcRayConfig cfg, float scale);
void ProcBolt_SetBrightness(int id, float b);
void ProcBolt_Update(int id, Vector3 from, Vector3 to, float scale, float dt);
void ProcBolt_Draw(int id, Camera3D cam); void ProcBolt_Kill(int id);
// Unmanaged immediate-mode bolt (caller owns 9 waypoints, regenerate+draw per frame, wrap in BLEND_ADDITIVE):
void RegenerateLightningWaypoints(Vector3 *waypoints9, Vector3 from, Vector3 to, float scale);
void DrawLightningBolt(const Vector3 *waypoints9, float thickness, Camera3D cam); // legacy violet/white
void DrawLightningBoltEx(const Vector3 *waypoints9, float thickness, Camera3D cam, Color colorGlow, Color colorCore);
```
Key rules: always `Kill` on deactivate (IDs are pool slots, not auto-freed); call `ProcRay_SetPhase` per ray when spawning N concurrent rays or they share phase=0 and look identical; `waveSpeed=0` is correct for bolts (both ends fixed); rendering is 3-pass per channel (outer haze 2.4× → glow → hot core), branches thinner/dimmer (`branchScale`, 0.7× brightness); a freshly spawned bolt has no waypoints until its first `ProcBolt_Update` — call it with `dt=0` right after spawn if drawing the same frame; strike lifecycle: `SetBrightness(1.9f)` for ~70ms leader flash, then decay toward ~0.45 with small flicker for afterglow.

### Combat (`core/skill_manager.h`)
```c
void ApplyAoEDamage(Vector3 position, float radius, float damage, float knockback);
```

### Cooldown / Resource Gating State (`core/skill_manager.h`)
`Skill_CalculateCooldown()`/`Skill_CalculateManaCost()` only compute numbers — these hold/check actual elapsed-time state:
```c
bool SkillManager_CanCast(int skillIndex, int agentId);
void SkillManager_TriggerCooldown(int skillIndex, int agentId, float cooldownSeconds);
int  Skill_GetIndexByName(const char *name); // -1 if no exact match
```
- Keyed by `(skillIndex, agentId)` — `agentId` = caster's `entities/entities.h` agent-pool slot. Independent cooldown per caster per skill. Internal static `float[MAX_SKILLS][256]` (256 duplicated from `entities.h`'s `MAX_AGENTS` — `core/` can't `#include entities/`).
- `TriggerCooldown` ticks down via `UpdateSkillManager(dt,...)`; call it yourself where a skill actually casts (not auto-wired).
- `CanCast` returns `true` when remaining cooldown `<= 0`. Out-of-range args → `false`.
- `Skill_GetIndexByName`: call once in `Init[Name]Skill` (registry fully populated by then), cache in `static int s_skillIndex`.

### Abort / Interrupt (`core/skill_manager.h`)
Optional, additive — doesn't change the mandatory lifecycle contract.
```c
void RegisterSkillAbort(int skillIndex, void (*abort)(int agentId));
void AbortSkill(int skillIndex, int agentId);
```
- A skill opts in via `RegisterSkillAbort`. `AbortSkill` invokes the callback if present, else `LOG_WARNING` no-op — safe to call unconditionally. A skill tracking per-caster ownership can abort just that caster's instance; one that ignores `agentId` aborts everything.

### Lifecycle-End Query (`core/skill_manager.h`)
Optional, additive.
```c
void RegisterSkillLifecycleQuery(int skillIndex, bool (*hasActiveInstance)(int agentId));
bool Skill_HasActiveInstance(int skillIndex, int agentId);
```
- "Is there still an active instance of this skill owned by agentId X" — for gameplay code that needs to know a zone effect (Earth wall, Wood root-zone) is truly gone. Skills that never register report `false` unconditionally (safe default, no warning — harmless query not a command). Adopted by `STONE_PRISON`/`WOOD_THORNS`; transient skills (fire_ball, tube, water_sphere) don't need it.

### Tunable Parameters (`core/skill_manager.h`)
Optional, additive — exposes a skill's magic numbers as named, min/max-bounded sandbox sliders (`sandbox/ui_panel.c`).
```c
#define MAX_SKILL_TUNABLES 200
typedef struct {
    char label[32]; // slider label AND key= name in the skill's .tuning file
    float *value;
    float min, max, defaultValue;
    const char *phase; // NULL=ungrouped; free-form tag for sandbox tab grouping (e.g. "cast"/"fly"/"impact"/"rain")
    SkillCurve *curve;  // NULL=plain constant via `value`. Non-NULL=curve-kind; `value` must be NULL; 5 keyframes (t=0/25/50/75/100%) are the storage
} SkillTunableEntry;

void RegisterSkillTunables(int skillIndex, const SkillTunableEntry *entries, int count);
int  Skill_GetTunables(int skillIndex, SkillTunableEntry *outEntries, int maxEntries);

#define SKILL_TUNABLES_MAX_FLAT_KEYS (MAX_SKILL_TUNABLES * SKILL_CURVE_KEYS)
int  SkillTunables_Flatten(const SkillTunableEntry *entries, int count, char outKeys[][TUNING_MAX_KEY_LEN], float *outValues, int maxKeys);
void SkillTunables_Unflatten(const SkillTunableEntry *entries, int count, const char *const *keys, const float *values, int keyCount);
bool SkillTunables_LoadPersisted(const char *path, SkillTunableEntry *entries, int count);
```
- Call `RegisterSkillTunables` in `Init[Name]Skill` after `Skill_GetIndexByName`. `entries` must point at storage kept alive for the skill's lifetime (`static SkillTunableEntry s_tunables[N]` whose `.value`/`.curve` point at your own `static` state) — registry copies pointers by value; sandbox UI writes through them directly.
- **Phase grouping**: sandbox renders one tab per distinct `.phase` tag (registration order); `NULL`-phase entries share a "GENERAL" tab.
- **Curve-kind entries**: `.curve = &s_myCurve` (seeded via `SkillCurve_SetConstant`), `.value = NULL`. Read fresh each frame via `SkillCurve_Eval(&s_myCurve, t01)`; `t01` is always caller-defined progress (e.g. `Timeline_LayerProgress`), **never** fraction-of-distance-traveled (see `SkillHelper_StepCurveFlight` below).
- **Persisting curve entries**: call `SkillTunables_LoadPersisted("skills/<el>/<skill>/<skill>.tuning", entries, count)` right before `RegisterSkillTunables` — flattens (curve → 5 keys), loads persisted values (missing keys untouched), unflattens back.
- **`.inl` split (mandatory pattern)**: separate a skill's params and tunable-assignment block into co-located `[skill]_params.inl` (file-scope statics, `#include`d at file scope) and `[skill]_tunables.inl` (`#include`d **inside** `Init[Name]Skill`, right after the `SkillTunableEntry` array + counter declarations — refers to local vars, never file-scope). See `hoa_long_phong_ba_skill.c`/`magma_fissure_skill.c`.
- `Skill_GetTunables` is how `sandbox/ui_panel.c` discovers a skill's entries (0 if never registered); its Save button uses `Flatten` + `Tuning_SaveFloats`.

### Curve-Driven Flight & Extra Force (`core/skill_helper.h`)
```c
void SkillHelper_StepCurveFlight(const SkillCurve *speedCurve, float elapsed, float dt,
                                  float maxDuration, float maxRange, float targetDistance,
                                  float *traveled, bool *arrived);
Vector3 SkillHelper_EvaluateForceLayer(const ForceLayer *layer, Vector3 pos, Vector3 vel,
                                        float time, Vector3 axisOrigin, Vector3 axisDir);
```
- `StepCurveFlight`: one frame's advance for a projectile whose speed follows a `SkillCurve` over **elapsed time**, not fraction-of-distance (indexing by distance stretches the curve for far targets and leaves no hard cap). `t01 = clamp(elapsed/maxDuration, 0, 1)` indexes `speedCurve`; `*traveled += SkillCurve_Eval(t01)*dt`, clamped to `maxRange`; `*arrived=true` the moment `*traveled` reaches `min(targetDistance, maxRange)` OR `elapsed+dt >= maxDuration` (treated as impact-at-that-point). `maxDuration`/`maxRange` should themselves be `SkillTunableEntry` rows. Fits a straight-line projectile (`pos = startPos + dir*traveled`); a Bezier-path-anchored flight should sample `SkillCurve_Eval` at its own progress instead — see `fire_skill.c`'s `UpdateFireSkill`.
- `EvaluateForceLayer`: evaluates a single `ForceLayer` without building a whole `ForceField`. `axisOrigin`/`axisDir` only matter for `RADIAL_AXIS`/`VORTEX_AXIS` — pass `{0}` otherwise.

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
int SkillForceMix_MakeTunables(SkillForceMix *mix, const char *labelPrefix, const char *phase, SkillTunableEntry *outEntries);
```
- All 8 curated `ForceType`s (excludes `RADIAL_AXIS`/`VORTEX_AXIS`/`VECTOR_TEXTURE`) are **simultaneously available**, each with its own strength (0=inert) + full param set — dial up several at once (curl + gravity-point together), not a "pick one type" switch.
- One `static SkillForceMix` per phase; call `MakeTunables` once at Init, append the 29 entries after that phase's other tunables (contiguous group).
- Call `SkillForceMix_AddLayers(&mix, &yourForceField)` right before each real use (rebuild-from-current-values, don't bake once). `ForceField_AddLayer`'s 8-layer cap silently drops layers past it if base+mix exceeds it.
- **Rebuild pattern**: `static void RebuildXxxField(void)` = `ForceField_Clear` → re-add base layers from tunable statics → `SkillForceMix_AddLayers`. Call at Init AND top of every `Update` frame — calling only at Init bakes a stale snapshot.

### Shader Binding (`core/skill_manager.h`)
```c
void SkillManager_BeginShader(Shader shader);
void SkillManager_EndShader(void);
```
Auto-binds `u_time`, `viewPos`, `u_resolution`. (Also auto-sets `matModel=identity` — see §10.)

### Debug Draw (`core/debug_draw.h`)
Thin wireframe overlay for visually tuning hitbox/AoE radii.
```c
void DebugDraw_SetEnabled(bool enabled);
bool DebugDraw_IsEnabled(void);
void DebugDraw_Sphere(Vector3 pos, float radius, Color color);
void DebugDraw_Circle(Vector3 center, float radius, Color color);
```
- Gated behind one global toggle, **default disabled** — call sites can be left in unconditionally.
- `DebugDraw_Circle` draws on the ground plane at `center.y`.
- Built on raw `DrawSphereWires`/`DrawCircle3D` — exempt from "no raylib primitives" (internal dev tooling, not shipped VFX).

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
Mesh ProceduralMesh_BuildRockTemplateMesh(float radius, float jitterAmount, int seed, int subdivisions); // GPU-resident, tier-3 instancing
```
- Subdivided icosahedron (subdivisions clamped 0-2, level 2≈162 verts=ceiling), radial jitter ±`jitterAmount` via seeded hash PRNG. Flat-shaded (per-face normals). Same `seed` = same shape.
- Small rubble: keep using squished `DrawCoreCube`/`DrawCoreSphere` with §12.3 randomization, NOT this function.
- **Build once at cast time, cache** — doesn't animate.
- `BuildRockTemplateMesh`: GPU-resident single-rock template, build once ever (`UploadMesh`), draw N copies via `DrawMeshInstanced` with per-instance transforms — same convention as `BuildCrystalTemplateMesh` below. See GPU Instancing decision tree and `VFX_ComposeFloatingStones` reference. Expanded to 3 unique verts/face (no shared indices) since instancing needs a plain vertex buffer.

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

**Crystal / Crystal Cluster** (faceted crystal spike + multi-crystal cluster):
```c
typedef struct { float height, radius, taper, twist, noise, bevel, split; int sides, segments; } CrystalDesc; // sides<3 or segments<2 => no-op
void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color); // immediate-mode, sides<=16, segments<=16

#define CRYSTAL_CLUSTER_MAX_CRYSTALS 8
#define CRYSTAL_CLUSTER_MAX_TRIS 1024
typedef struct { Vector3 pos[...*3]; Vector3 normal[...*3]; Vector2 uv[...*3]; int triCount; } CrystalClusterMeshData;
void ProceduralMesh_BuildCrystalCluster(CrystalClusterMeshData *out, Vector3 center, const CrystalDesc *desc, int count, int seed, float progress);
void ProceduralMesh_DrawCrystalClusterMesh(const CrystalClusterMeshData *data, Color color);
void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color); // build+draw convenience wrapper, unchanged signature
```
- `DrawCrystalCluster` batches all children into one `CrystalClusterMeshData` (one `rlBegin(RL_TRIANGLES)/rlEnd()`) instead of one draw call per crystal — no call-site changes needed. Children LOD-capped `sides<=8, segments<=8` regardless of parent `desc`; `count` capped to 8.
- Immediate-mode path is for **small ambient clusters only** (3-8 low-detail, e.g. micro-debris). For a **hero burst** (e.g. 10 crystals, full detail, alive several seconds), rebuilding via `rlVertex3f` every frame is a measured CPU bottleneck — use the GPU-resident API below instead.

**Crystal Cluster — GPU-resident mesh (hero bursts):**
```c
Mesh ProceduralMesh_BuildCrystalClusterMesh(const CrystalDesc *desc, int count, int seed); // rare/static use — see perf trap below
void ProceduralMesh_DrawBakedCrystalCluster(Mesh mesh, Material material, Matrix transform);
Material ProceduralMesh_GetPassthroughMaterial(Shader shader);
void CrystalMaterial_SetGrowProgress(CrystalMaterial mat, float progress); // 0..1, default 1.0 (core/material/material_system.h)
Mesh ProceduralMesh_BuildCrystalTemplateMesh(const CrystalDesc *desc); // RECOMMENDED for burst/repeated casts
```
- **`BuildCrystalTemplateMesh` (recommended default)**: builds **one** crystal (local space, origin-centered, upright, no jitter). Call **once ever** (lazy static) — never rebuild/unload for process lifetime. Draw N "different-looking" crystals by looping `DrawBakedCrystalCluster(templateMesh, material, transform)` with a different per-instance `transform` (CPU-computed from a deterministic hash) — no new `Mesh`/`UploadMesh` after the first build. See `VFX_DrawIceCrystalBurst` (`vc_water.inl`).
- **`BuildCrystalClusterMesh` (rare/static only)**: bakes a whole pre-scattered cluster (jitter baked into geometry via `seed`) into one `Mesh`. **Perf trap**: calls `UploadMesh` — a real GPU-driver sync point. Fine once (static prop at level load); building a **new** one per cast stutters when several casts land in the same window. Use the template approach for anything cast-repeated.
- Both: baked at `progress=1.0` always — grow-reveal is **not** CPU-baked; `crystal.vs`'s `u_growProgress` uniform (`vertexPosition.y *= u_growProgress`) is set via `CrystalMaterial_SetGrowProgress`; `CrystalMaterial_Begin` always resets it to `1.0` first. Draw between `CrystalMaterial_Begin`/`End` (or any `BeginShaderMode` block); get the `Material` via `ProceduralMesh_GetPassthroughMaterial(mat.shader)` (a passthrough vehicle for `DrawMesh`, doesn't own `u_baseColor`/texture uniforms). Mesh built in local space centered at origin — position/orient/scale via `transform`, not baked geometry.
- **Ready-made ice wrapper**: `void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress);` — call every frame, no Build/Unload needed. Uses template mesh + GPU instancing internally (**one** `DrawMeshInstanced` call for the whole burst, not one `DrawMesh`/crystal). Pass a different `seed` per cast for shape variety (same seed = same layout, by design). Trade-off: `growProgress` is one uniform shared by the whole batch — no per-crystal grow stagger (possible with the old per-`DrawMesh` loop, not with instancing).

**Creating a new procedural mesh — decision tree (3 tiers, pick lowest sufficient):**
1. **Single shape, immediate-mode (`rlBegin`/`rlEnd`)** — fine when the effect draws **one instance** per frame (one tube/funnel/pillar/puddle), even if complex. Consolidate to fewest `rlBegin`/`rlEnd` calls the shape allows. Animated shapes *should* rebuild their vertex buffer every frame by design. Reference: `pm_tube.inl`, `pm_magic_effects.inl` (Fissure, VortexFunnel), `pm_organic.inl`, `pm_water_waves.inl`.
2. **Build-once template + N × `DrawMesh`** — shape is **cast/spawned repeatedly** but not enough simultaneous copies to justify instancing, or per-instance variation isn't purely a transform (per-instance grow-stagger, per-instance color). `UploadMesh` **exactly once ever** — never per cast (real GPU-driver sync point, the exact stutter the crystal-template fix addressed).
3. **Build-once template + `DrawMeshInstanced`** — N copies drawn **every frame while the effect is alive**, per-instance variation expressible as a transform. Soft threshold: N≳10 for a one-shot burst, or as low as ~4-5 if it repeats every frame for the effect's lifetime. Two reference material twins:
   - `CrystalMaterial`-backed shapes → `CrystalMaterialInstanced` + `crystal_instanced.vs` (`VFX_DrawIceCrystalBurst` N≤32 one-shot; `VFX_ComposeMetalShardCluster` N=4 per-frame).
   - `EffectMaterial`-backed shapes (`Material_Get`/`Material_LoadCustom`) → `EffectMaterialInstanced` + `effect_material_instanced.vs` (`VFX_ComposeFloatingStones` N=5 per-frame, `vc_earth.inl`).
   Don't invent a third instanced-material variant — reuse one of these two, loaded with different params.

**Tier-3 implementation checklist** (mirror `crystal_instanced.vs`/`effect_material_instanced.vs`):
1. New `*_instanced.vs` file — never retrofit `vs_header.glsl` (its `VS_FinalOutput()` assumes one `matModel` uniform per draw; instancing reads a per-instance transform *attribute*).
2. Fixed attribute name: `in mat4 instanceTransform;` — raylib auto-binds it like `vertexPosition`, no extra C-side plumbing.
3. Compute transform manually, don't call `VS_FinalOutput()`:
```glsl
mat4 instanceModel = matModel * instanceTransform; // matModel identity by default
fragPosition = vec3(instanceModel * vec4(pos, 1.0));
fragNormal   = normalize(vec3(instanceModel * vec4(vertexNormal, 0.0)));
gl_Position  = mvp * instanceTransform * vec4(pos, 1.0); // raylib sets mvp = matProjection*matView only when instancing
```
4. `.fs` needs zero changes (instancing is a vertex/transform concern only).
5. Duplicate the material wrapper (loc-cache struct can't be shared across separately-compiled programs) — see `CrystalMaterialInstanced`/`EffectMaterialInstanced` in `core/material/material_system.h/.c`.
6. CPU: build `Matrix transforms[N]` (standard TRS composition), one `DrawMeshInstanced(templateMesh, material, transforms, N)` call.
7. Per-instance geometry variation that isn't a rigid transform (e.g. per-blade `twist`) must be dropped or approximated by scale — instancing shares one template topology.

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

### GPU Vertex Displacement (`core/procedural_mesh_utils.h` + `core/shaders/common/displacement.glsl`)
```c
Mesh ProceduralMesh_CreateBaseGrid(float width, float length, int segmentsX, int segmentsZ);
Mesh ProceduralMesh_CreateBaseCylinder(int radialSegs, int heightSegs); // 2-end-open tube, local axis +Y in [0,1], local radius 1
typedef struct {
    float amplitude, frequency, speed;      // DisplaceVertex_Noise
    float twistAmount, taperStart, taperEnd; // radians/t=0..1, AlongPath/TwistAndTaper
    Vector3 pathP0, pathP1, pathP2, pathP3;  // world space, AlongPath only
} MeshDisplacementParams;
MeshDisplacementParams ProceduralMesh_DefaultDisplacementParams(void);
void ProceduralMesh_SetDisplacementUniforms(Shader shader, const MeshDisplacementParams *params); // call every frame after BeginShaderMode, before Draw
void ProceduralMesh_UnloadBase(Mesh *mesh); // call once at skill unload
```
```glsl
// displacement.glsl — include AFTER vs_header.glsl, opt-in, independent of noise.glsl
vec3 DisplaceVertex_Noise(vec3 localPos, vec3 localNormal, float noiseVal);
vec3 DisplaceVertex_AlongPath(vec3 localPos, vec2 texCoord);
vec3 DisplaceVertex_TwistAndTaper(vec3 localPos);
vec3 DisplaceVertex_AlongPathNormal(vec3 localNormal, vec2 texCoord);        // REQUIRED alongside AlongPath
vec3 DisplaceVertex_TwistAndTaperNormal(vec3 localPos, vec3 localNormal);   // REQUIRED alongside TwistAndTaper
```
- **`AlongPath`/`TwistAndTaper` REQUIRE their `*Normal()` counterpart** or lighting shows spiral banding — `VS_FinalOutput()` sets `fragNormal` from the un-rotated `vertexNormal`, unaware position was just rotated into a new frame:
  ```glsl
  vec3 displaced = DisplaceVertex_TwistAndTaper(vertexPosition);
  VS_FinalOutput(displaced);
  fragNormal = normalize(vec3(matModel * vec4(DisplaceVertex_TwistAndTaperNormal(vertexPosition, vertexNormal), 0.0)));
  ```
- `DisplaceVertex_Noise` has no normal counterpart (assumed small amplitude) — for higher-fidelity ripples perturb the normal in FS instead (`lighting.glsl`'s `perturbNormal()`).
- **Additive, not a replacement** for the CPU builders above — those rebuild CPU-side and let skill code read back positions (raycast/anchoring). This bakes ONE static mesh at cast time, displaces every frame via uniforms on GPU only — CPU never sees displaced positions. **Only for pure-visual effects needing no raycast/collision against the displaced shape.**
- Create base mesh once at cast time, cache in instance struct — never per frame.
- `ProceduralMesh_SetDisplacementUniforms` silently skips uniforms the shader doesn't declare (same safe pattern as `SkillManager_BeginShader`).
- `DrawMesh`/`DrawModel` auto-populate `matModel` via raylib — no identity-matModel workaround needed (unlike rlgl immediate-mode CPU builders, see §10).

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
- Frame order: `Begin()` → draw 3D scene → `End()` → `Draw(&config)`.
- Bloom uses a dual-filter pyramid (downsample 1/4→1/8→1/16 + upsample); recommended `bloomThreshold=0.5f, bloomIntensity=2.0f` for a dark arena.
- Skills don't control bloom params — they control emissive brightness of their own particles/shaders; bloom auto-picks up whatever exceeds threshold.
- Multi-texture binding uses `SetShaderValueTexture` inside `BeginShaderMode` — do not use `rlActiveTextureSlot`/`rlEnableTexture` for extra post-FX textures (confirmed silently broken).

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

### Impact / Cast / Flight Effect Presets
```c
typedef enum { EFFECT_PRESET_FIRE_EXPLOSION, EFFECT_PRESET_ICE_SHATTER, EFFECT_PRESET_WATER_SPLASH,
  EFFECT_PRESET_LIGHTNING_IMPACT, EFFECT_PRESET_EARTH_CRACK, EFFECT_PRESET_WOOD_BLOOM,
  EFFECT_PRESET_METAL_SHARD, EFFECT_PRESET_TAIJI_BURST } EffectPresetType;

void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale);
void SpawnCastEffect(Vector3 pos, EffectPresetType preset, float scale);
int  SpawnProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed);
```
- `SpawnImpactEffect`: all 6 elements covered (each pre-tinted, paired with a decal preset): `WOOD_BLOOM`→`DECAL_PRESET_WOOD_MOSS`, `METAL_SHARD`→`DECAL_PRESET_METAL_SLASH` (high pitch range), `TAIJI_BURST`→`DECAL_PRESET_TAIJI_RING` (stronger light, "no-element" ultimate). **`EARTH_CRACK` does NOT call `CameraFX_Shake`** (removed so Earth skills control shake via their own toggle) — call `CameraFX_Shake` explicitly if wanted.
- `SpawnCastEffect`: cast/windup equivalent — no knockback/decal. Ring-spawn particles pulled inward via `FORCE_GRAVITY_POINT` ("energy gathering") + light flash. Call at start of cast/windup. 8-slot static `ForceField` pool (`MAX_CONCURRENT_CAST_EFFECTS`), round-robin.
- `SpawnProjectileTrail`: flight-stage equivalent. Spawns `TRAIL_TYPE_PROJECTILE` + head particle w/ `onLiveEmit` tail dust. Returns trail ID — **caller MUST `KillTrail(id)` on impact**, before `SpawnImpactEffect`. Uses **sustained/flight force regime** (300-650f `FORCE_GRAVITY_DIR` + ~20f `FORCE_NOISE_PERLIN`), NOT the 5-60f ambient range §1 documents for cast/impact. 8-slot pool (`MAX_CONCURRENT_PROJECTILE_TRAILS`), round-robin.

### Lightning Trail Presets
```c
int SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed);
int SpawnLightningFollowerTrail(Vector3 startPos, float scale, float life);
```
Dedicated jagged/flicker profile — `SpawnProjectileTrail` can't reproduce a zigzag (its flight wobble is tuned for a smooth arc, and both `TRAIL_TYPE_PROJECTILE`'s homing-steer and `TRAIL_TYPE_WISP`'s distance-solver low-pass any jaggedness back to smooth).
- `SpawnLightningTrail`: flight-stage bolt along a **precomputed jagged 9-waypoint polyline** (`GenerateLightningWaypoints`, real geometric kinks, no `forceField`). A `TRAIL_TYPE_FOLLOWER` advances progress along it via `onUpdate`, pushing `LIGHTNING_BOLT_PUSH_COUNT` (50) points spread over travel duration, staying under `TRAIL_HISTORY_COUNT` (60) so the whole bolt stays visible tip-to-start. Self-terminates via `KillTrail` at progress=1.0 — caller doesn't need to kill it for normal flow.
- `SpawnLightningFollowerTrail` + `Lightning_UpdateFollowerTip(id, tipPos, scale)`: manually-driven, for an electric aura/bolt attached to a moving object at a fixed local point. **Use `Lightning_UpdateFollowerTip`, not raw `UpdateFollowerPosition`** — feeding a smooth per-frame path into `UpdateFollowerPosition` reads as a wiggly worm, not lightning. Only accepts a new point once real position moved `LIGHTNING_FOLLOWER_MIN_SEGMENT` (45f scaled) from the last, inserting one perpendicular kink per accepted segment. Caller **MUST call `KillTrail(id)`** when done.
- Both share a `LightningTrailFlicker` `onUpdate` (reads live state off the `TrailEntity`, spawns short `VFXLight_Spawn` bursts ~10/sec) and the same white→violet→dark-purple gradient `SpawnProjectileTrail(EFFECT_PRESET_LIGHTNING_IMPACT,...)` uses, for consistent cast/flight/impact color.

### Damage Volume
```c
typedef enum { SHAPE_CIRCLE, SHAPE_BOX, SHAPE_CONE } ShapeType;
typedef struct { ShapeType shape; Vector3 center; float radius; float damagePerSecond, tickInterval, duration; bool active; float timer, tickTimer; } DamageVolume;
void DamageVolume_Init(void); void DamageVolume_Update(float dt);
int  SpawnDamageVolume(DamageVolume config); // returns ID
void DamageVolume_Unload(void);
```

### Skill Timeline / Layered Timeline
```c
typedef struct { float current, duration; } SkillTimeline;
void Timeline_Start(SkillTimeline *t, float duration);
bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt); // true exactly 1 frame at trigger
bool Timeline_Finished(SkillTimeline *t);

#define TIMELINE_MAX_LAYERS 8
typedef struct { const char *tag; float start, duration; } TimelineLayer; // duration>0: continuous window; ~0: one-shot event
typedef struct { float current; TimelineLayer layers[TIMELINE_MAX_LAYERS]; int layerCount; } LayeredTimeline;
void  Timeline_LayeredStart(LayeredTimeline *t);
bool  Timeline_AddLayer(LayeredTimeline *t, const char *tag, float start, float duration); // false past TIMELINE_MAX_LAYERS
bool  Timeline_IsLayerActive(const LayeredTimeline *t, int layerIndex);
float Timeline_LayerProgress(const LayeredTimeline *t, int layerIndex); // 0..1 within window, clamped outside
bool  Timeline_LayerEvent(const LayeredTimeline *t, int layerIndex, float dt); // one-shot, same edge-detect as Timeline_Event
```
One declarative `{tag, start, duration}` table for staggering N visual layers (Trail/Light/Smoke/Decal) instead of hand-written `if (t>X && t<Y)` per layer — see `WUXING_ART_DIRECTION.md` §4.4. Caller advances `t->current += dt` itself, same convention as `SkillTimeline`. `LayerProgress` feeds `FloatCurve_Sample` for that layer's envelope.

### Particle Emitter / Mesh Presets
```c
typedef enum { EMITTER_FIRE, EMITTER_SNOW, EMITTER_WATER_SPURT, EMITTER_SHOCKED_SPARKS,
  EMITTER_WOOD_LEAVES, EMITTER_EARTH_DUST, EMITTER_METAL_SPARKS, EMITTER_TAIJI_MOTES } EmitterPreset;
void EmitterSystem_Init(void); void EmitterSystem_Update(float dt);
int  Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration);
void Emitter_Stop(int emitterId); void EmitterSystem_Unload(void);

typedef enum { MESH_PRESET_DISC, MESH_PRESET_RING, MESH_PRESET_CONE, MESH_PRESET_TORNADO,
  MESH_PRESET_CYLINDER, MESH_PRESET_SPHERE, MESH_PRESET_SHOCKWAVE, MESH_PRESET_PYRAMID, MESH_PRESET_TETRAHEDRON } MeshPresetType;
void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color);
```

### Shader Material Preset (`EffectMaterial`)
```c
typedef enum { MAT_FIRE, MAT_ICE, MAT_WATER, MAT_PORTAL, MAT_ROCK, MAT_METAL, MAT_GLASS, MAT_CUSTOM } MaterialPreset;
// Legacy aliases MATERIAL_FIRE/ICE/WATER/PORTAL/CUSTOM still #defined; Material_Load == Material_Get. CUSTOM set by Material_LoadCustom().
typedef struct {
    Color    baseColor;          // primary tint; also drives rim glow + dissolve edge glow
    float    rimStrength;        // 0..~2, rim/edge glow brightness (Fresnel-weighted, light-facing biased)
    float    fresnelPower;       // 1..8, rim sharpness (higher = thinner edge)
    float    emissiveIntensity;  // 0..~3, self-illumination boost added to base color
    float    distortionStrength; // 0..1, vertex wobble amount
    float    translucency;       // 0=opaque (alpha=baseColor.a), 1=glass/tube-style fresnel-driven alpha
    Texture2D texture1;          // optional secondary detail/mask texture; id==0 = unused
} EffectMaterialParams;
typedef struct {
    Shader shader; MaterialPreset preset;
    int uTimeLoc, uDissolveLoc, uBaseColorLoc, uTranslucencyLoc, uRimStrengthLoc,
        uFresnelPowerLoc, uEmissiveIntensityLoc, uDistortionStrengthLoc, uHasTexture1Loc, uTexture1Loc;
    EffectMaterialParams params;
} EffectMaterial;
EffectMaterial Material_Load(MaterialPreset preset);              // 4 hardcoded presets, effect_material-backed
EffectMaterial Material_LoadCustom(EffectMaterialParams params);  // parametrized shared shader, no new GLSL needed
void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val);
void Material_Begin(EffectMaterial mat); void Material_End(void);
```
- `Material_Load` (4 presets): each is a hardcoded `EffectMaterialParams` routed through `Material_LoadCustom` (shared `core/shaders/effect_material.vs/.fs`) — no per-skill shader files, no `shader.id==0` path.
- Rim glow weighted by light-facing direction: `rim = fresnel * mix(0.3, 1.0, max(dot(normal, lightDir), 0.0))` — dimmed not zeroed on backlit side.
- `translucency=1.0` for "center see-through, edges more solid" (`tube.fs` look) — **caller must wrap draw in `BeginBlendMode(BLEND_ALPHA)`/`EndBlendMode()`**, `Material_Begin`/`End` don't manage blend mode.
- Shader ignores per-vertex `Color` — tint comes only from `u_baseColor`. `texture1.id==0` skips the sample (guarded by `u_hasTexture1`), sampled as luminance mask (`.r` only) when present.
- `Material_SetFloat` still works on `Material_LoadCustom` materials for any uniform, e.g. animating `u_dissolve`. `fx.glsl`'s `dissolveCalc()` gives nonzero `edgeFactor` for ~8% of fragments even at `dissolve==0.0` — avoids speckle the instant material appears.

**`EffectMaterialInstanced`** — GPU-instancing twin of `EffectMaterial` (separate shader program → separate uniform-location cache, can't share the struct):
```c
EffectMaterialInstanced EffectMaterialInstanced_Load(EffectMaterialParams params);
void EffectMaterialInstanced_Begin(EffectMaterialInstanced mat);
void EffectMaterialInstanced_End(void);
```
Backed by `core/shaders/effect_material_instanced.vs` (+ unchanged `effect_material.fs`) — same wobble/distortion math, reading `instanceTransform` instead of a per-draw `matModel`. Use with `DrawMeshInstanced` for N transform-only copies (see §8 GPU Instancing decision tree; `VFX_ComposeFloatingStones` reference). No `SetFloat`/`SetGrowProgress`-equivalent yet.

> [!NOTE] `CrystalMaterial`'s own `CrystalMaterialParams`/`CrystalMaterial_Load`/`_Begin`/`_End` signatures are referenced throughout §8 (Crystal Cluster GPU-resident mesh) but their full field list isn't enumerated in the current CORE_API.md text — only `CrystalMaterial_SetGrowProgress(CrystalMaterial mat, float progress)` appears with an explicit signature. Consult `core/material/material_system.h` directly for the complete `CrystalMaterialParams` struct.

### Plasma Material (`core/material/material_system.h` — `plasma_shell.vs/.fs`)
Wispy energy membrane: alpha = fresnel × animated fbm ⇒ **fully transparent center** (`EffectMaterialParams.translucency` can't do this — 0.3 alpha floor face-on). Draw spheres under `BLEND_ADDITIVE`; disable backface culling to get the far-side membrane layer free. VS undulates the surface itself.
```c
typedef struct { Color baseColor /*membrane body, alpha=master*/, wispColor /*bright crests*/;
  float noiseScale /*2.5-4*/, noiseSpeed /*0.3-0.8, neg=reverse*/, fresnelPower /*higher=emptier center*/,
  rimStrength, emissive, opacity, displaceAmp /*world units, ~8% of sphere radius*/; } PlasmaMaterialParams;
PlasmaMaterial PlasmaMaterial_Load(PlasmaMaterialParams params);
void PlasmaMaterial_Begin(PlasmaMaterial mat); // runtime changes: edit mat.params before Begin
void PlasmaMaterial_End(void);
// Reference use: VFX_ComposePlasmaOrb (vc_plasma.inl).
```

### Ground Decal Preset
```c
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
- `FIRE_LAVA`/`WATER_RIPPLE` use `DecalSystem_AddFlowEx` internally (radial outward scroll, `flowSpeed=0.6, flowStrength=0.8`). Every other preset still calls plain `DecalSystem_Add` (static).

### ForceField Preset & Skill Builder (chainable context)
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
`AddCastEffect` fires `SpawnCastEffect` immediately (cast time), unlike other `Add*` which defer to `Build()` (impact time).

### SkillBuilder — One-line Archetype Spawns
Immediate, duration-based, no malloc. Static internal pools (see §17 for sizes).
```c
// Beam: element-tinted managed ray + VFXLight at endpoints
int  SkillBuilder_SpawnBeam(Vector3 from, Vector3 to, EffectPresetType element, float width, float duration);
void SkillBuilder_KillBeam(int handle);  // early termination

// GroundWave: expanding shockwave mesh + decal scroll emitter marching along dir. Static pool of 8.
void SkillBuilder_SpawnGroundWave(Vector3 origin, Vector3 dir, EffectPresetType element, float range, float speed);

// Orbitals: N tetrahedra orbiting center with random phase/scale (anti-robotic law). Static pool: 8 groups x 8 orbitals.
int  SkillBuilder_SpawnOrbitals(Vector3 center, EffectPresetType element, int count, float radius, float duration);

// AuraRing: looping emitter ring (K points on circle) + glow decal. Static pool of 8.
int  SkillBuilder_SpawnAuraRing(Vector3 center, EffectPresetType element, float radius, float duration);
void SkillBuilder_KillAuraRing(int handle);

// Drive all internal pools — called by SkillHelper_Update, not directly by skills.
void SkillBuilder_Update(float dt);
void SkillBuilder_DrawWorld(Camera3D cam);
```
- Beams wrap `SpawnProcRay` + `VFXLight_Spawn` at endpoints.
- `SkillBuilder_Update`/`DrawWorld` are driven by `SkillHelper_Update` (wired in `main.c`) — skills must **not** call them directly.

### Chain-Targeting Helper
```c
// Returns count of chain points (0 = no targets in range). outPoints[0]=origin, outPoints[1..]=jump targets (nearest not already hit). Damage is the skill's job — visuals only.
int  SkillHelper_ChainTargets(Vector3 origin, float jumpRadius, int maxJumps, Vector3 *outPoints, int maxOut);
// Fires SpawnLightningTrail per hop, staggered by hopDelay seconds. Internal static queue (32), driven by SkillHelper_Update.
void SpawnChainLightning(const Vector3 *points, int count, float scale, float hopDelay);
// Drives chain queue + SkillBuilder pools. Already wired in main.c — skills do not call this directly.
void SkillHelper_Update(float dt);
```
Depends on `SkillManager_SetNearbyTargetsProvider` being registered (done automatically by `Entity_Init`).

---

## 10. GLSL SHADER GUIDELINES & 3D RENDERING BEST PRACTICES

> [!IMPORTANT] As of this revision, CORE_API.md no longer inlines the GLSL reference — it points to a dedicated **`SHADER_API.md`** for: common headers (`vs_header.glsl`/`fs_header.glsl`/`lighting.glsl`/`noise.glsl`/`fx.glsl`/`triplanar.glsl`), built-in variables/functions, custom uniforms, Android/GLES compatibility Rules A–E, the `matModel` landmine, 3D lighting rules, and procedural-noise guidelines. Consult `SHADER_API.md` directly for the full reference — this SHORT doc no longer duplicates it (the previous version's detailed GLSL section here has drifted from CORE_API.md's actual current content and has been dropped rather than carried forward stale).

Load-bearing facts still surfaced elsewhere in this doc: `#include` in shaders is engine preprocessing (`ShaderPreprocessor_Load`, wired into `ResourceManager_LoadShader`), not native GLSL — raw `glCompileShader`/standalone linters fail on `#include` lines by design. `SkillManager_BeginShader` auto-sets `matModel=identity`; raw `BeginShaderMode()` callers must set it manually via `GetShaderLocation(shader, "matModel")` (name-based lookup — never `shader.locs[SHADER_LOC_MATRIX_MODEL]`, which is not actually `matModel`'s slot and silently corrupts an unrelated uniform, e.g. `texture0`, if used).

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

---

## 13. MOTION CONTROLLER (`core/motion_controller.h`)
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
    Vector3 pos, vel, target, origin;
    float elapsed, orbitAngle;
    bool returning;    // BOOMERANG: true when heading back toward origin
} MotionState;
void Motion_Init(MotionState *s, MotionParams params, Vector3 startPos, Vector3 target);
void Motion_Step(MotionState *s, float dt);
bool Motion_Arrived(const MotionState *s);
```
> [!NOTE] Scale: `speed` in m/s (typical projectile: 8–20 m/s), `gravity` in m/s² (real gravity=9.81; use 4.9 for a floaty arc). `arrivalRadius` 0.2f fits a 0.15–0.20f mesh radius per §1.

```c
// Skill struct: MotionState motion;
// On cast:
Motion_Init(&motion, (MotionParams){ .type = MOTION_HOMING, .speed = 12.0f,
    .turnRateRad = 2.5f, .arrivalRadius = 0.3f }, startPos, target);
// Each Update frame:
Motion_Step(&motion, dt);
if (Motion_Arrived(&motion)) { /* trigger impact */ }
```

---

## 14. STATUS VFX & AFTERIMAGE (`core/status_vfx.h` / `core/afterimage.h`)

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
- When agent dies (provider returns false) or duration expires → 0.5s fade-out, then auto-free.
- `Update`/`Draw` already wired in `main.c`.

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
- Typical spawn cadence: one ghost every 0.04s while dash/blade is active (caller-side timer).

---

## 15. SKILLBUILDER ARCHETYPES & CHAIN-TARGETING
Documented inline in §9b. Quick-reference: `SkillBuilder_SpawnBeam`/`KillBeam`, `SkillBuilder_SpawnGroundWave`, `SkillBuilder_SpawnOrbitals`, `SkillBuilder_SpawnAuraRing`/`KillAuraRing`, `SkillHelper_ChainTargets`/`SpawnChainLightning`, `SkillHelper_Update` (already wired in `main.c` — **do not call from skills**).

---

## 16. AGENT PROVIDERS (`core/skill_manager.h`)
Inversion-of-control: core queries agent positions and nearby targets without depending on `entities/`.
```c
typedef bool (*AgentPosProviderFn)(int agentId, Vector3 *outPos);
void SkillManager_SetAgentPosProvider(AgentPosProviderFn fn);
bool SkillManager_GetAgentPos(int agentId, Vector3 *outPos); // false if agentId invalid or no provider registered

typedef int (*NearbyTargetsProviderFn)(Vector3 center, float radius, int *outIds, int maxIds);
void SkillManager_SetNearbyTargetsProvider(NearbyTargetsProviderFn fn);
int  SkillManager_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds); // returns count found, 0 if no provider
```
Both registered automatically by `Entity_Init`. Skills call them **indirectly** via `StatusVFX_Update` or `SkillHelper_ChainTargets` — do not call these functions directly.

---

## 17. POOL STATS & SANDBOX TOOLS

### Pool Stats `GetStats()` (respective `core/*.h` headers)
Each core pool exposes active/max counts — call once per frame in autotest scenarios to detect silent pool overflow.
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
Displayed by `sandbox/pool_stats.h` — hold **F3** in-game. Row turns red when peak == max (overflow occurred this session). `TraceLog(LOG_WARNING)` fires once per pool per session on first drop.

### Sandbox: Pool Stats Overlay (`sandbox/pool_stats.h`)
```c
void PoolStats_Init(void);
void PoolStats_DrawOverlay(void);  // hold F3 in-game
```
Shows 8 pools: active/max + peak high-water mark. Red row = at least one item silently dropped this session.

### Sandbox: Visual Verify Harness (`sandbox/visual_verify.h`)
Headless regression capture. Usage: `WUXING_VERIFY=<skill_name> ./wuxing`
```c
bool        VisualVerify_IsEnabled(void);
const char *VisualVerify_GetSkillName(void);
void        VisualVerify_Init(int skillIndex);
void        VisualVerify_RunFrame(float elapsed);
bool        VisualVerify_IsFinished(void);
int         VisualVerify_GetExitCode(void);  // 0 = ok, 1 = unknown skill
```
- Casts the skill at a fixed position, saves 5 PNGs at 0.15/0.5/1.0/2.0/3.5s into `autotest_output/verify_<skill>_<time>s.png`, then exits.
- `FLAG_WINDOW_HIDDEN` — no display required.
- Skill name must match registry exactly (e.g. `FIRE_BALL`).

> [!NOTE] When autotest reports PASS but visual output looks wrong, trust the screenshot over the numeric result.

---

## 19. VISUAL COMPOSITION & PROCEDURAL MESHES
See **[COMPOSITION_API.md](COMPOSITION_API.md)** for the full composition reference: `VFX_Compose*`, `VFX_GroundPattern`, `VFX_PathWave`, `VFX_TriggerExplosion`, `VC_MaterialId` table, motion library, beauty primitives, archetype groups 3–5, `.inl` include order, and sync script.

> [!IMPORTANT] Both this file and `COMPOSITION_API.md` are mechanical references (what exists, what it's called, what parameters it takes) — neither teaches what makes a composition look good. Before authoring a new skill/VFX, read **[`WUXING_ART_DIRECTION.md`](WUXING_ART_DIRECTION.md)** (AI-oriented design rules, per-element visual language, timeline design, a cookbook of reusable layer recipes, and a 10-step workflow) — then use `COMPOSITION_API.md` §0's table to translate the cookbook pattern you pick into concrete function calls from this API.
