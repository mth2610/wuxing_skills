# Core Engine API — Usage Guide

> The **prose companion** to [`API.md`](API.md) (the generated signature index). API.md tells you *what exists*; this guide tells you *how to use it* — patterns, contracts, worked examples, and the "why". Look up a signature in the index; come here for how to wield it.
>
> - **Code standards** (C99, memory, auto-registry, scale, shaders, aesthetic laws): [`AGENT_CODE_STANDARD.md`](../../AGENT_CODE_STANDARD.md) — the authoritative checklist.
> - **Composition layer:** [`COMPOSITION_API.md`](COMPOSITION_API.md). **Skill skeletons/recipes:** [`RECIPE.md`](../../skills/docs/RECIPE.md), [`SKELETONS.md`](../../skills/docs/SKELETONS.md). **Traps:** [`LANDMINES.md`](LANDMINES.md), [`ENGINE_LANDMINES.md`](../../ENGINE_LANDMINES.md).

---

## 1. Compilation & scale — quick reminders
Full rules live in [`AGENT_CODE_STANDARD.md`](../../AGENT_CODE_STANDARD.md) (§0 auto-registry, §1 C99/compile, §2 scale). The essentials:
- Strict C99, Raylib 6.0. Backend: **Vulkan 1.1 via `rlvk` (priority)**; OpenGL 3.3 Core / GLES 3.x are fallback paths (see `../../third_party/vulkan/docs/HANDOFF.md`). Keep draw code backend-agnostic (`rlgl`/raylib, never raw GL/Vulkan). Skill `.c` files must `#include <stddef.h> <stdlib.h> <stdio.h>`; guard `PI` with `#ifndef`.
- No `malloc/calloc/realloc/free` in skill code — fixed-size static arrays + flags only.
- Skills auto-register on build via `scripts/generate_registry.py`; folder `skills/[element]/[name]_skill/` holds `.h`/`.c` (+ optional `.vs`/`.fs`/`.png`, auto-copied). Include your own header with the full path, incl. the `_skill` suffix.

> [!IMPORTANT]
> **Meter-scale (1 unit = 1 m) — never the old ×100 numbers.** Mesh/tube radii ~0.10–0.20f; impact bursts/lights 0.5–1.5f; particle speed 1.0–3.0f; gravity/force 3.0–9.8f (vs real gravity 9.81). Do NOT use the old 300–700f ranges.

---

## 2. CENTRALIZED ELEMENT COLORS & STYLING
The engine defines six customizable global base colors in [core/skill_manager.h](../../skill_manager.h):
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
* **Camera Shake (`CameraFX_Shake`):** Defaults to NO SHAKE (off/0.0f). Never add camera shake to a default skill without the user's consent. If a skill has a shake effect, always expose it as a tunable defaulting to 0.0f, so the user can opt into it in the Sandbox.
* **Screen Distort (`ScreenDistort_Add`):** Avoid overuse (visually noisy) — use only for Water-element skills (Hydro Cleave) or when requested.

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

`agentId` is the caster's agent-pool slot (0..255), forwarded automatically by `CastSkill()`. Store it in your per-instance struct as `int ownerAgentId;` at cast time — it's what lets `AbortSkill(skillIndex, agentId)` target only the caster's own instances. Ownership tracking is its only job right now; see `skills/CLAUDE.md` for the full rule.

See **[skills/docs/SKELETONS.md](../../skills/docs/SKELETONS.md)** for all 4 skeleton templates:
Generic Projectile, Ground-Rising, Anchored-Along-Path, Entity-Attached.

### Agent Position & Nearby Targets Providers (`core/skill_manager.h`)
See [skills/docs/SKELETONS.md](../../skills/docs/SKELETONS.md) end-of-file. Full API in §16 below.

---
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
* **`FORCE_VECTOR_TEXTURE` (GPU-only):** Samples a world-space flow texture instead of a procedural formula — for geometry-authored vector fields (smoke hugging a wall, fire wrapping a body) that noise/vortex layers can't express. `origin.xz`/`direction.xz` define a world-space sample box (`direction.xz` = half-extent, `origin.y`/`direction.y` ignored). Texture RG channels = XZ flow direction remapped `[-1,1] -> [0,1]`. Particles outside the box get zero acceleration (hard cutoff, no edge-clamp). **CPU path (`ForceField_Evaluate`, `particle_system.c`, `trail_system.c`) treats this as a no-op** — only the internal GPU backend samples the texture. New code declares this module through `ParticleEmitterDesc`; it does not call the backend directly.

---

## 6. PARTICLE SYSTEM (`#include "core/particles/particle_system.h"`)

### Hybrid particle façade (`#include "core/particles/particle_manager.h"`)

New gameplay and VFX code creates `ParticleEmitterDesc` and only calls
`ParticleManager_CreateEmitter`, `ParticleManager_Emit`, `ParticleManager_Update`,
and `ParticleManager_Draw`. It must not select or call a CPU/GPU implementation.
`ParticleSystem_GetGPUCaps()` is a read-only snapshot probed once during
`ParticleManager_Init`; never query graphics capability in an effect update.

`PARTICLE_SIM_AUTO` selects compute only when the probe and every declared module
permit it; otherwise it uses the CPU pool and increments the fallback statistic.
`PARTICLE_SIM_CPU_ONLY` is mandatory for gameplay callbacks, authoritative
collision, and bone-attached authoring. `PARTICLE_SIM_GPU_ONLY` rejects emission
when unavailable or unsupported: inspect `ParticleManager_GetEmitterStatus`; the
manager logs a warning once per emitter and exposes the rejection counter.

| Module | CPU | GPU |
| --- | --- | --- |
| Gravity, drag, colour/size over life, velocity stretch, basic force field | yes | yes |
| Vector-field texture | no | yes |
| Gameplay callback/collision | yes | no |
| Depth collision | fallback raycast/disable by policy | preferred |
| Ribbon/trail | authoring | stream consumer when supported |
| Fluid SSF input | surface stream | direct raster stream |

Descriptors are copied into a fixed 128-emitter pool. Their pointer members
(`ForceField`, gradients, curves, sprite animation, and textures) are borrowed:
they must outlive every emitted particle. No allocation or shader compilation is
performed by emit/update. `SpawnParticle` remains a compatibility AUTO burst and
currently carries the legacy-compat module, so existing CPU effects retain their
behaviour while their descriptors are migrated.

Fluid renderers consume `ParticleRenderStream` from
`ParticleManager_GetSurfaceStream`. The stream is opaque; consumers must never
map it or request GPU-to-CPU readback. `FluidSurface_SubmitParticleStream` is the
backend-neutral handoff point.

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
    float spriteAnimPhase;               // Per-particle time offset (seconds)
    float spriteAnimRate;                // 0 = legacy 1.0x; use <= 1.0 for ANIM_ONCE sheets
    bool spriteFlipX, spriteFlipY;       // UV variation; ideal for directionless puffs
    const Vector3 *followTarget;         // Optional stable emitter position
    const unsigned int *followTargetGeneration; // Optional recycled-slot guard
    unsigned int followGeneration;
    float followStrength;                // Target displacement inherited at birth
    const SkillCurve *followCurve;       // NULL = releases linearly by death
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
* **Flipbook variation:** A reusable, directionless flipbook should use a randomized `spriteAnimPhase`, a slightly slower `spriteAnimRate` (never make an `ANIM_ONCE` sheet overrun its last authored frame), and X/Y flips. Shape still comes from emitter distribution, velocity and force fields.
* **Emitter follow:** `followTarget` carries only source displacement, then releases the particle through `followCurve` (or a default linear release). Its target pointer must remain valid for the particle lifetime. For pooled emitters, provide `followTargetGeneration` and the captured `followGeneration`, otherwise particles can attach to an unrelated effect after the slot is reused.
* **Over-lifetime curves (`radiusCurve`/`speedCurve`/`alphaCurve`, `core/skill_curve.h`):** all three are `NULL` by default (today's exact legacy behavior — fixed radius, physics-only velocity, colorStart/colorEnd/gradient's own alpha). When set, each is sampled fresh every frame at `t01 = 1.0 - lifeRatio` (0 at spawn, 1 at death — same "age fraction" convention `gradient` already uses) via `SkillCurve_Eval`, and **multiplies** the corresponding base value: `radiusCurve` scales the drawn radius, `speedCurve` scales only this frame's position step from `velocity` (the stored velocity itself is untouched, so it composes cleanly with `forceField`/`WindZone` physics instead of compounding), `alphaCurve` scales `colorStart.a` and overrides whatever alpha `colorStart`/`colorEnd`/`gradient` would have produced (RGB is unaffected). This is the mechanism for a skill's per-phase "particle size/speed/opacity over its own short lifetime" tunables — see `fire_skill.c`/`thunder_orb_skill.c` for the pattern: one `static SkillCurve` per phase per property, seeded flat at `1.0` via `SkillCurve_SetConstant` (a no-op multiplier), registered as a curve-kind `SkillTunableEntry`, and pointed to by every `ParticleConfig` spawned in that phase.

### Shared VFX Contrast Profiles (`core/vfx_contrast.h`)

Đây là policy chống bệt màu dùng chung ở tầng renderer, không phải một post-effect.
VFX chỉ chọn profile theo bản chất vật liệu; Core áp dụng cùng luật cho màu thân
`BODY`, alpha che nền, màu lõi `EMISSION`, HDR gain và emissive threshold.

```c
particle.render.contrastProfile = VFX_CONTRAST_ENERGY;
trail.material.contrastProfile = VFX_CONTRAST_SMOKE;
decalMaterial.contrastProfile = VFX_CONTRAST_FIRE;
```

- `VFX_CONTRAST_NONE` bằng `0` và là identity chính xác; config `{0}` không đổi look cũ.
- Các profile có sẵn: `SMOKE`, `FIRE`, `ENERGY`, `MAGIC`, `DUST`.
- Profile không thay thế luật semantic layer: vật chất vẫn vẽ vào body/alpha,
  radiance vẫn vẽ vào emission/additive. Profile chỉ resolve hai lớp nhất quán.
- Particle CPU/GPU và particle-ribbon đọc `VFX_RenderConfig.contrastProfile`.
  Trail ribbon/tube/deform đọc `TrailMaterialConfig.contrastProfile`.
  Decal material đọc `DecalMaterialParams.contrastProfile`.
- Raw ribbon dùng `DrawRibbonStripProfiledEx` hoặc
  `DrawRibbonStripDeformedProfiledEx`; `RibbonEnergyFieldLayer` mang profile và
  `VFXContrastLayer` riêng cho từng layer.
- Với strand trail lẫn classic/swept ribbon, `edgeSharpness` định hình coverage
  ngay tại producer, nhờ vậy khe giữa filament không bị lấp thành một dải đặc.
  Compositor giữ alpha tuyến tính; không được tăng coverage toàn cục vì việc đó
  sẽ làm lộ biên của smoke, particle và decal. HDR/lõi nằm ở emission pass riêng,
  không nhân vào body.
- `SMOKE` và `DUST` giữ `alpha = 1`, `edgeSharpness = 1`: profile được phép đổi
  sắc độ/mật độ thân nhưng không được thay silhouette mềm đã author.
- Riêng strand mode 2 còn áp một cross-profile thuôn và center-core mảnh cho mỗi
  bundle. Texture R/G điều biến chi tiết, nhưng không được quyền biến một bundle
  thành hình chữ nhật đặc hoặc làm mất hot core khi sheet/fallback quá phẳng.
- Màu center-core lấy từ `VFX_ElementMaterial.hotGrad` (sample vùng nóng), rồi
  nội suy với `glow/body` bằng tuning `hot_whiten` cũ. Không whiten trực tiếp
  màu đỏ-cam: Fire phải đi về gold/yellow, không đi về pink-white.

### Mesh-based Particle Emission
* `void SpawnParticleOnMesh(const struct MeshAdjacency *adj, Matrix transform, ParticleConfig config);`
Spawns a particle at a random edge position on the mesh, transforming its position into world space using the given transform matrix.

### Mesh Adjacency Graph (`#include "core/mesh_adjacency.h"`)
Used to construct topological adjacency graphs of 3D meshes (welding vertices within a small threshold) to enable path walking and edge sampling.
* `void MeshAdjacency_Build(MeshAdjacency *out, Mesh mesh);` - Builds the adjacency graph.
* `Vector3 MeshAdjacency_SampleVertex(const MeshAdjacency *adj);` - Samples a random vertex.
* `Vector3 MeshAdjacency_SampleEdge(const MeshAdjacency *adj);` - Samples a random point on an edge.
* `int MeshAdjacency_GeneratePath(const MeshAdjacency *adj, int startVertex, int length, Vector3 *outPath);` - Generates a random non-backtracking walk path.

---

## 7. TRAIL & RIBBON SYSTEM (`#include "core/trails/trail_system.h"`)
TrailConfig should be initialized with {0}.
`int SpawnTrailEntity(TrailConfig config);` spawns ribbon-based trail components.

> **Pool budget:** `MAX_TRAIL_PARTICLES = 500` is a single static pool shared across **all active trails project-wide**, not per-skill. Several concurrent heavy-trail skills (e.g. multiple wisp/projectile-heavy casts at once) can exhaust it. When full, `SpawnTrailEntity` now scans for the lowest-`priority` active trail (ties broken by shortest remaining lifetime) and evicts it instead of rejecting outright — it only returns `-1` if every active trail already has strictly higher priority than the incoming `config.priority`. Same eviction pattern as `core/vfx_light.h`'s `VFXLight_Spawn`.

### Configurations
```c
typedef enum {
    TRAIL_TYPE_PROJECTILE,  // Automatically flies towards target using vel + forceField
    TRAIL_TYPE_WISP,        // Drifts randomly in wind/noise fields
    TRAIL_TYPE_PORTAL,      // Static position, rotates in place (summoning circles)
    TRAIL_TYPE_FOLLOWER     // Manually driven. Follows coordinates bound by skill code
} TrailType;

typedef enum {
    TRAIL_WIDTH_ENVELOPE_UNIFORM = 0, // Uniform width multiplier (1.0)
    TRAIL_WIDTH_ENVELOPE_TAPER_TAIL = 1, // Needle tail (segRatio ^ 1.2)
    TRAIL_WIDTH_ENVELOPE_TAPER_BOTH = 2, // Needle head & tail (leaf shape)
    TRAIL_WIDTH_ENVELOPE_PULSE       = 3, // Breathing wave along path
    TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE = 4 // small source -> body -> dissolve
} TrailWidthEnvelopeType;

typedef void (*TrailUpdateCallback)(int trailId, float dt);
typedef void (*TrailDeathCallback)(Vector3 pos, float scale);
typedef bool (*TrailCollisionCheckCallback)(int trailId, Vector3 currentPos);

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
    
    // 5 New Upgrades configuration
    TrailCollisionCheckCallback collisionCheck; // Custom dynamic collision test
    float uvTiling;          // Texture repeat count along path (defaults to 1.0f if 0)
    float uvScrollSpeed;     // UV offset shift speed (V-coordinate units/sec)
    float minVertexDistance; // Min distance before inserting new history node (0 = every frame)
    TrailWidthEnvelopeType widthEnvelope; // Shape envelope along path length
    bool smoothSpline;       // Enable Catmull-Rom spline interpolation (min 30 vertices)
} TrailConfig;
```
* **`priority`:** additive field — `TrailConfig cfg = {0};` still compiles and defaults to `VFX_PRIORITY_LOW`, so this is backward compatible (unlike `VFXLight_Spawn`'s signature change above). Set `cfg.priority = VFX_PRIORITY_HIGH_ULTIMATE;` for a cast that must not silently lose its trail to pool pressure.
* **Follower Trails:** For sword swings or aura attachments, set type to `TRAIL_TYPE_FOLLOWER`. Two ways to drive the tip:
  - **Manual (per-frame):** call `UpdateFollowerPosition(trailId, tipPos);` each frame before `UpdateTrailSystem`.
  - **Matrix attachment:** call `Trail_AttachToTransform(trailId, &myMatrix, localOffset);` once — `UpdateTrailSystem` reads `*myMatrix` automatically each frame and computes `tip = Vector3Transform(localOffset, *myMatrix)`. Pass `localOffset={0,0,0}` to track the matrix origin. The `Matrix` must stay valid for the trail's lifetime (typically a `static Matrix` field on the owning skill). Detach with `Trail_AttachToTransform(id, NULL, (Vector3){0})`.
  - **Dynamic Orbit:** call `Trail_SetFollowerOrbit(trailId, radius, speed, axis, phase);` to make a matrix-attached trail automatically orbit its `localOffset` point! Orbit rotates around `axis` (must be normalized) at distance `radius`, advancing `speed * dt` radians per frame. Starts at angle `phase`. Set `radius` or `speed` to `0.0f` to disable.
  - `SetFollowerAxis(trailId, basePos, normalizedDir);` sets the optional radial-axis orientation for `FORCE_RADIAL_AXIS` in `forceField` — unrelated to tip position.
  - **`trailLength` for FOLLOWER = integer node count** (e.g. `20.0f` = 20 history nodes). Not a fractional ratio — `(int)trailLength` is taken directly. Trail only renders when `historyCount > 1`, so values < 2.0f result in no visible trail.
* **Lifecycle:** Free active trails when complete by calling `KillTrail(trailId);`. For `VFX_ComposeStrandTrail` (and its `VFX_ComposeEnergyTrail` / `VFX_ComposeSmokeStrandTrail` aliases), prefer `VFX_StrandTrail_Stop(trailId)`: it detaches the emitter and lets the already-laid ribbon drift and dissolve. `KillTrail` remains the intentional immediate cut.
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
* **New 5 Upgrades Features:**
  - **`uvTiling` & `uvScrollSpeed`:** Enables automatic texture coordinates scrolling along the trail path length. E.g., `cfg.uvTiling = 2.0f; cfg.uvScrollSpeed = 1.5f;` tiles the texture 2 times and scrolls it at 1.5 units/sec.
  - **`widthEnvelope`:** Modifies the shape of the trail using `TrailWidthEnvelopeType`. E.g., `TRAIL_WIDTH_ENVELOPE_TAPER_BOTH` creates a double-pointed long leaf shape. `TRAIL_WIDTH_ENVELOPE_PULSE` generates breathing waves. `TRAIL_WIDTH_ENVELOPE_SMOKE_LIFECYCLE` grows from a narrow source into a broad body then transparently dissolves at the tail; it was the default for the deleted `VFX_ComposeSmokeTrail`; the strand trail's smoke style uses `TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN`.
  - **`minVertexDistance`:** Optimizes performance by filtering node insertion. A new node is only recorded in history if the head moves at least `minVertexDistance` (world units) away from the last node.
  - **`smoothSpline`:** Smooths out segments via Catmull-Rom spline interpolation if `historyCount >= 4`. Dense points (minimum 30) are interpolated dynamically to avoid angular corners at low FPS.
  - **`collisionCheck`:** Accepts a custom callback `collisionCheck(trailId, pos)`. For `TRAIL_TYPE_PROJECTILE`, if this callback returns `true`, it immediately triggers a collision impact hit (`onDeath`) and transitions to the follower decay stage.

--- wobble
  cfg.curveRangeOverride = 1.0f;        // snaps to target direction almost immediately
  ```
  Leave both at `0` (default from `{0}` zero-init) to keep the existing global-macro behavior — fully backward compatible.

---

## 8. Graphics & VFX API

### Ground Decals (`core/decals/decal_system.h`)
```c
void DecalSystem_Init(void);
void DecalSystem_Add(Vector3 pos, float rot, float scale, Texture2D tex, float life, Color tint);
void DecalSystem_AddEx(Vector3 pos, float rot, float rotSpeed, float scaleStart, float scaleEnd, Texture2D tex, float life, Color tint, BlendMode blendMode, float yOffset);
void DecalSystem_AddFlowEx(Vector3 pos, float rot, float rotSpeed, float scaleStart, float scaleEnd, Texture2D tex, float life, Color tint, BlendMode blendMode, float yOffset, float flowSpeed, float flowStrength);
void DecalSystem_AddOrientedEx(Vector3 pos, Vector3 normal, float rotation, float rotSpeed, float scaleStart, float scaleEnd, Texture2D texture, float lifetime, Color tint, BlendMode blendMode, float yOffset);
void DecalSystem_AddStreak(const Vector3 *points, int count, float rot, float scale, Texture2D tex, float life, Color tint);
void DecalSystem_Update(float dt);
void DecalSystem_DrawBody(void);
void DecalSystem_DrawEmission(void);
bool DecalSystem_HasEmission(void);
void DecalSystem_Draw(void);
void DecalSystem_Unload(void);
```
* `rot`: yaw around Y axis (degrees). Alpha fades internally as `lifetime / maxLifetime` decays to 0.
* Static pool, `MAX_DECALS = 64`, no malloc.
* `DecalSystem_AddStreak`: thin wrapper that calls `DecalSystem_Add` once per point in `points[0..count-1]` — for path-shaped effects (thorn lines, scorch trails) instead of hand-rolling a loop. Caller's responsibility to pass a reasonable `count` (e.g. up to 32, matching `SkillParams.pathPoints[32]`); not auto-clamped against `MAX_DECALS` headroom, same convention as `SamplePath`'s `maxSegments` in `core/path_spline.h`.
* **`DecalSystem_AddFlowEx`**: same params as `AddEx` plus `flowSpeed`/`flowStrength`. Texture radially scrolls outward from the decal center over time (`core/decals/shaders/decal_flow.fs`) instead of staying static — for lava-crack-crawl / ripple-spreading visuals. `flowSpeed` ~0.3–1.0 (radial units/sec), `flowStrength` ~0.5–1.0 (0 = looks identical to a static decal, 1 = fully replaced by the scrolled sample). Draws via a separate shader pass from static decals — does not affect `Add`/`AddEx` behavior or performance. Already wired into `SpawnGroundDecal` for `DECAL_PRESET_FIRE_LAVA`/`DECAL_PRESET_WATER_RIPPLE` (see Ground Decal Preset section); every other preset is unaffected (static).
* **`DecalSystem_AddOrientedEx`**: a surface-aligned static quad for a known hit normal. Use it for walls/ceilings; `rotation` rolls around `normal`, and `yOffset` lifts along the normal. Do not use it as an every-frame projector.
* **Semantic draw split (engine render graph):** call `DrawBody` inside
  `ScreenDistort_BeginVFXBody()` and, when `HasEmission` is true, call
  `DrawEmission` inside `ScreenDistort_BeginVFXEmission()`. `DrawBody` prepares
  the shared decal queue; `DrawEmission` reuses it. `DecalSystem_Draw()` remains
  a compatibility entry point, but it cannot choose two render targets for its
  caller and therefore is not suitable for the layered compositor.

### Fluid Impacts (`core/fluid_impact.h`)

```c
void FluidImpact_SpawnWater(const FluidImpactEvent *event);
void FluidImpact_SetCollisionQuery(FluidImpactCollisionQueryFn query, void *userData);
```

Gameplay submits `hitPoint`/`hitNormal`; hero droplets perform deterministic swept collision through `FluidImpact_SetCollisionQuery`, while compute/CPU-VBO particles are background density only. Without a provider, Core collides against the active map ground; walls/props require the world/physics owner to register its query. Full budget and wetness fallback contract: [`FLUID_IMPACT_SPEC.md`](FLUID_IMPACT_SPEC.md).

Rules:
- Call `DecalSystem_Init()` once at startup, `DecalSystem_Update(dt)` every frame to age out decals.
- Prevents Z-fighting automatically (internal Y offset, do not add your own).
- Draw before 3D meshes, using `BLEND_ALPHA`.
- Recommended scale: 4–5.5× structure radius.
- Do not call `DecalSystem_Unload()` from skill code — global system, owned by the engine shutdown sequence only.

### Screen Distortion (`core/screen_distort.h`)
Static pool of radial shockwave/heatwave distortions. `MAX_DISTORTION_SOURCES = 16`.

**Lifecycle (global — skill code only calls Add):**
```c
void ScreenDistort_Init(int width, int height);  // Call once at startup
void ScreenDistort_Begin(void);                  // Begin rendering the 3D scene into the aux buffer
void ScreenDistort_End(void);                    // End 3D scene render
void ScreenDistort_Update(float dt);             // Tick source lifetimes
void ScreenDistort_Draw(Camera3D camera);        // Draw the result with distortion to the screen
void ScreenDistort_Unload(void);                 // Free (engine shutdown; not called from a skill)
```

**VFX render-layer contract (required for new render code):** `main.c` owns
the scene target and shared manager passes. A self-contained skill or
composition must route its own draw through one of these pairs:

```c
ScreenDistort_BeginVFXBody();     // coloured, translucent material
/* draw with BLEND_ALPHA */
ScreenDistort_EndVFXLayer();

ScreenDistort_BeginVFXEmission(); // light/halo only
/* draw with BLEND_ADDITIVE */
ScreenDistort_EndVFXLayer();
```

- Put smoke, decal pigment, trail/particle cores, and any surface whose hue
  must survive a bright map in the **body** layer.
- Put only low-energy glow, sparks, and bloom halos in the **emission** layer.
  An effect needing both must draw its body first and halo separately.
- Do not add per-skill branches to `main.c`, render directly into the scene
  target, or assume an additive sprite can keep hue on a bright destination.
  The compositor alpha-overlays body, then adds emission.
- Flush pending rlgl batches before changing layers when mixing raylib batched
  drawing with immediate rendering. Set custom shader uniforms inside
  `BeginShaderMode`.
- Shared managers follow the same contract: particle and trail bodies are
  collected into the single frame-wide VFXBody target; particle emission and
  decal emission are collected into VFXEmission. A VFX that emits and has
  visible coloured mass must author both populations rather than marking its
  only population additive.

**Skill API — only call Add:**
```c
void ScreenDistort_Add(Vector3 worldPos, float radius, float strength, float lifetime, float speed);
```
* `radius`: max shockwave radius (world units).
* `strength`: UV-distortion amplitude (0.01–0.05 for a light heatwave, 0.1–0.3 for a strong shockwave).
* `speed`: how fast the wave propagates outward.
* Distortion auto-expires after `lifetime` seconds — no manual kill needed.

### Metaballs / Screen-Space Fluid (`core/metaball_fx.h`)
```c
void MetaballFX_Init(int width, int height);   // engine-internal
void MetaballFX_Unload(void);                  // engine-internal
void MetaballFX_RegisterBlob(Vector3 worldPos, float radius);
void MetaballFX_Prepare(Camera3D camera, Color tint, float threshold, float smoothness); // engine-internal, main.c calls once/frame
void MetaballFX_Composite(void); // engine-internal, into the VFX body target
```
Rules:
- **Skill API — only call `MetaballFX_RegisterBlob`** each frame for every blob you want visible (water projectile head, lava droplet...) — a blob lives exactly one frame and must be re-registered continuously. `MAX = METABALL_MAX_BLOBS = 32` engine-wide (one shared registry across all skills, not a per-skill pool).
- **Do NOT call `MetaballFX_Prepare` or `MetaballFX_Composite` from skill code** — the former runs GL directly and must stay outside `BeginMode3D`/`EndMode3D`; `main.c` prepares its mask/blur after the scene, then composites it into `ScreenDistort`'s VFX body before PostFX.
- The effect is **2D screen-space**, but its final alpha body now uses the shared VFX compositor, so bright scenery cannot bleach its tint. It still does not depth-test individual blobs.
- **Tint is currently one fixed engine-wide color** (`main.c` passes `ELEMENT_COLOR_WATER` for every blob of every skill) — not per-skill/per-element. Multiple colors at once would need the registry extended to carry a per-blob `Color`.
- `threshold`/`smoothness` control how "sticky" blobs are as they merge — low threshold + high smoothness = merges more easily/smoothly.

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
Standard geometry for any continuous long body (dragon, vine, lightning bolt, water stream, energy flow), replacing stacked billboard chains (heavy overdraw, wrong silhouette when viewed along the path) **and** replacing hand-rolled intersecting-plane hacks. Technique: at each path point, offset left/right by a vector perpendicular to both the path tangent and a chosen "right" mode, forming a continuous triangle strip (rlgl immediate-mode, no VBO, no malloc).
```c
typedef struct {
    Vector3 position;  // World-space point on the path
    float   halfWidth; // Half-width of the body at this point
    Color   tint;       // Color + alpha at this point
    float   v;          // UV along strip length, caller-computed (e.g. normDist 0..1) —
                        // or call Ribbon_ComputeArcLengthUV to fill this correctly (below)
} RibbonPoint;

typedef enum {
    RIBBON_CAMERA_FACING, // right = tangent × camera view dir (default — Trail Renderer style,
                          // silhouette always faces camera). Lightning, beams, projectile trails.
    RIBBON_WORLD_UP,      // right = tangent × (0,1,0) — does NOT billboard. Ground-anchored
                          // ribbons (rivers, vines lying on terrain) where camera-facing would
                          // flip the silhouette wrong as the camera orbits.
    RIBBON_FIXED_NORMAL,  // right = tangent × caller-supplied normal — ribbon pinned to an
                          // arbitrary plane (e.g. a skill-defined wall/slope), not just world-up.
} RibbonMode;

void DrawRibbonStripEx(const RibbonPoint *points, int count, Texture2D texture,
                       Camera3D camera, RibbonMode mode, Vector3 fixedNormal);
void DrawRibbonStripProfiledEx(const RibbonPoint *points, int count, Texture2D texture,
                               Camera3D camera, RibbonMode mode, Vector3 fixedNormal,
                               VFXContrastProfileId profile, VFXContrastLayer layer);
void DrawRibbonStrip(const RibbonPoint *points, int count, Texture2D texture, Camera3D camera);
// ^ convenience wrapper: DrawRibbonStripEx(..., RIBBON_CAMERA_FACING, unused)

void Ribbon_ComputeArcLengthUV(RibbonPoint *points, int count);

void Ribbon_ComputeCrossFrame(const Vector3 *points, int count,
                              RibbonMode mode, Vector3 fixedNormal, Camera3D camera,
                              Vector3 *outAxisA, Vector3 *outAxisB);

typedef struct {
    float widthRatio;  // half-width = width * widthRatio * breathe * (widthEnvelope[i] or 1)
    float breatheFreq; // pulsing width: 1 + breatheAmp*sin(time*breatheFreq). breatheAmp==0 disables.
    float breatheAmp;
    float scrollSpeed; // texcoord V scroll (V units/sec)
    float uvTiling;    // total texture repeats along the WHOLE path (e.g. pathLength/5.0f)
    bool  vFlip;       // flip V — 2 layers at different scrollSpeed + one vFlip = cheap woven look
    bool  useTexture;  // false = flat color, ignores `texture` (e.g. hot core)
    Color color;
    VFXContrastProfileId contrastProfile;
    VFXContrastLayer contrastLayer;
} RibbonEnergyFieldLayer;
// max RIBBON_ENERGY_FIELD_MAX_LAYERS=4 layers, RIBBON_ENERGY_FIELD_MAX_PTS=64 points

void DrawRibbonEnergyField(const Vector3 *points, int count, float width,
                           const float *widthEnvelope, // NULL = uniform 1.0
                           const RibbonEnergyFieldLayer *layers, int layerCount,
                           Texture2D texture, RibbonMode mode, Vector3 fixedNormal,
                           Camera3D camera, float time);
```
Rules:
- Module does not manage memory — caller supplies a static `RibbonPoint` array; `count >= 2` required.
- Submits geometry only — does **not** change shader/blend state; `BeginShaderMode()`/`BeginBlendMode()` must be set from outside, so calls interleave with `DrawBillboard` in the same batch.
- Mandatory for any long-body mesh in the project — do not hand-roll a billboard chain or an intersecting-plane beam (see `SKILL_STANDARD.md`).
- `Ribbon_ComputeArcLengthUV` fills `points[i].v` with normalized cumulative distance (0 at `points[0]`, 1 at the last point) instead of `v = index/count` — call it right after filling `position` for all points, before `Draw*`. `v = index/count` stretches texture unevenly on a curved/jagged path (a long straight stretch between two waypoints gets compressed the same as a short one); this bit the original `DrawLightningBoltEx`/`ProcRay`/`ProcBolt` shared `DrawChannel`, fixed 2026-07-10 (see `core/vfx_proc_ray.c`).
- `Ribbon_ComputeCrossFrame` (new 2026-07-10) computes a **pair** of continuous perpendicular axes (`axisA`, `axisB = tangent × axisA`) at every path point instead of `DrawRibbonStripEx`'s single side vector — generalizes the old "2 fixed perpendicular planes" beam trick (which only worked for a straight 2-point line) to any N-point path.
- `DrawRibbonEnergyField` (new 2026-07-10) is the N-configurable-layer "energy field" primitive built on `Ribbon_ComputeCrossFrame` — a "+" cross-section (2 perpendicular planes) instead of one billboard, so it reads as real 3D geometry from any camera angle. Lives here (not in `core/composition/`) because both `core/vfx_proc_ray.c`'s EnergyFlow and `core/composition/vc_beam.inl`'s `VFX_ComposeBeam` need it, and composition may depend on core but never the reverse. Uses an internally-inlined `1+amp*sin(time*freq)` breathe formula rather than including `core/composition/vc_motion.h`'s identical `VC_Breathe` — same reason.
- This is the **only** ribbon/long-body primitive in the engine. `VFX_ComposeBeam` migrated `DrawRibbonStripEx` → `DrawRibbonEnergyField` 2026-07-10 once the "+" cross-section technique needed to generalize beyond a straight 2-point line; `EnergyFlow` (`core/vfx_proc_ray.c`) migrated the same day for visual consistency with Beam (both now read as real 3D energy fields, not a flat camera-facing ribbon).
- `DrawRibbonStripEx` resets to `rlSetTexture(0)` before returning (fixed 2026-07-10) — matters now that real textures are bound, not just the `(Texture2D){0}` every earlier caller used.

### UV module (`core/uv/`)

`mesh + UVDeformField + SurfaceFlow = effect`. Warp the coordinate, then sample it.
`core/uv/uv_deform.h` is the warp, `core/uv/surface_flow.h` the sampling, `core/uv/uv_fx.h`
binds both in one call, and `core/uv/flow_map.h` (moved here 2026-08-03) stays as the
one-layer convenience. They are one module because they share the **envelope**: the
along-surface gate that weights a wave's amplitude is the same weight that blends a
texture layer.

**Two GLSL tiers, and the choice matters.**

* `core/uv/shaders/uv_deform.glsl` and `surface_flow.glsl` declare **no uniforms** — the
  `flow_map.glsl` contract. A shader with its own uniform naming calls them without
  renaming anything, which is how `trail_deform.fs` migrated onto the module with its
  output unchanged to the last bit.
* `core/uv/shaders/uv_field.glsl` declares the standard packed `vec4[]` blocks that
  `UVFx_Apply()` binds. Include this **or** the pure files, not as a matter of taste:
  including it costs the uniform blocks whether or not you fill them.

```c
UVDeformField field = UVDeform_CreatePreset(UV_DEFORM_PRESET_SIN_WAVE_TRAIL);
UVDeform_SetPhase(&field, myRandomPhase);   // preserves each layer's detuned phase
SurfaceFlow flow; SurfaceFlow_Clear(&flow);
SurfaceFlow_AddLayer(&flow, (SurfaceFlowLayer){ .tiling = {2,2}, .env = UV_ENV_NONE });
UVFx_SyncStretch(&field, &flow, false);     // one asset, one answer about SHAPE/MATERIAL

UVFxLocs locs = UVFx_CacheLocations(shader);   // once
BeginShaderMode(shader);
UVFx_Apply(&field, &flow, shader, &locs, (float)GetTime());
```

**Layers run summed OR in parallel, and they are not interchangeable.**
`UVDeform_Evaluate` / `UVDeform_ApplyField` sum every layer into one displaced coordinate —
the reference decomposition, and what a warped flat quad wants. `UVDeform_EvaluateLayer` /
`UVDeform_ApplyFieldLayer` give one layer alone, for effects where each layer carries its
own sample of the sheet and the samples are combined afterwards with `max`. That is what
makes a trail read as three braided strand bundles; summing those three gives back the one
smooth band the mode exists to avoid.

**Pass a material coordinate, not a uv.** Every entry point takes the driving coordinate
(`mat`) separately from the coordinate being warped. A scroll built on anything measured
from a *moving* end of the geometry — a trail's tail, the first live particle — reads as
frozen however fast you scroll it, because the geometry slides under the coordinate at the
scroll speed. Pass a label stamped on the geometry when it was created (metres of emitter
path at the moment a node was laid). A flat quad may pass its own uv.

**Folding.** `UVDeform_SinePhase` takes whole **turns**, not radians — that is what makes
the fold exact and leaves the multiply grouping with the caller, so an existing shader can
migrate onto it bit-identically. `UVDeform_FoldAngle` serves shaders already written in
radians. Neither fixes frame-to-frame stutter: folding a product already computed at full
magnitude cannot recover bits it has lost. Fold at the origin instead — `SurfaceFlow_PackGPU`
folds every pan on the C side before upload, which is where the modulus can be chosen
deliberately. And never nest folds: `fract(fract(x)*k) != fract(x*k)` changes the tiling
rate.

**`stretchUV` is an authoring fact, not an inferable one.** A sheet with head/tail taper
painted in is one complete SHAPE and must be stretched once; a repeating filament sheet is
a MATERIAL and must be tiled by metres. Tiling a shape sheet gives a rope with no
silhouette. Both halves carry the flag; `UVFx_SyncStretch` keeps them from disagreeing.

Adding a `.glsl` here means adding its `configure_file` line in `CMakeLists.txt` **and** its
`cp -f` in `Makefile.Android` — `Makefile.Android` globs `core/shaders/common/*.glsl` only,
and a missing shader file does not report as a shader problem.

Headless test: `core/tests/uv_deform_test.c`.

### Flow Map (`core/uv/flow_map.h`)
Shared module for UV flow effects (shield, fire, water, tornado...). Each skill owns its own `FlowMap` instance (location cache + config + texture) — no global state, so multiple skills can use flow maps concurrently with different shaders/textures. For more than one layer, or to reuse the envelope the deform half uses, reach for `SurfaceFlow` above instead — a one-layer `SurfaceFlow` with `twoPhase` on is exactly a `FlowMap`.
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

### Particle Radial Burst (`core/particles/particle_system.h`)
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
See [COMPOSITION_API.md](COMPOSITION_API.md) — full `ImpactBurstConfig` struct, `VFX_ImpactPreset`, and `VFX_TriggerImpactBurst`.

Key invariants (as of 2026-07-07 rewrite):
- `particles.speedMin/Max` are **direct m/s** — no internal throttle factor. Old presets used a 0.3×/0.4× multiplier that has been removed.
- `colorStart` is auto-resolved from `gradient` at t=0 inside `TriggerImpactBurst`, so gradient-only configs (colorStart.a==0) render correctly.
- `VFX_ImpactPreset.decalTint` defaults to WHITE when `{0,0,0,0}`; set a dark color to avoid a bright decal competing with the particle cloud.
- Keep `lightRadius` ≤ 0.4m (before sizeScale) so the flash doesn't bleach particles.

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
* **`priority`:** `[!NOTE]` **breaking signature change** — `VFXLight_Spawn` gained a required `VFXPriority` parameter. When `MAX_VFX_LIGHTS` (16) is full, spawn no longer silently rejects: it scans for the lowest-priority active light (ties broken by shortest remaining lifetime) and evicts it, as long as that slot's priority is `<=` the incoming one. Only if every active slot is strictly higher priority does the new spawn get dropped (same as the old full-pool behavior). Existing call sites must add a priority arg (e.g. `VFX_PRIORITY_LOW` for routine VFX, `VFX_PRIORITY_HIGH_ULTIMATE` for Ultimate-tier casts).
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
    float flowScrollSpeed; // EnergyFlow only — flow-texture scroll speed (arc-length units/sec).
                           // Ignored by Ray/Bolt. 0 = no scroll.
} ProcRayConfig;

// Named presets
ProcRayConfig ProcRay_LightningConfig(void);      // violet/white, high jitter, taperTip=0.12 needle tendrils
ProcRayConfig ProcRay_BoltLightningConfig(void);  // amplitudeRatio=0.10, branchCount=3 — sky→ground bolts
ProcRayConfig ProcRay_EnergyConfig(void);         // cyan/gold, smooth Catmull-Rom — used by VFX_SpawnProcBeam
ProcRayConfig ProcRay_EnergyFlowConfig(void);     // cyan-white/blue-purple, animated traveling wave + flow texture — EnergyFlow only
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

// Energy Flow — A→B energy stream (mana bridge / power conduit). Organic
// multi-harmonic wave that TRAVELS along the length (config.waveSpeed drives a
// wavePhase; both ends pinned) — NOT a static bow. Catmull-Rom-resampled to
// FLOW_RIBBON_PTS=48, then drawn via DrawRibbonEnergyField (core/ribbon_strip.h)
// — same "+" cross-section N-layer primitive VFX_ComposeBeam uses, so both read
// as real 3D energy fields rather than a flat camera-facing ribbon. Width bulges
// in the middle and tapers to needle ends (shared per-point envelope); 3 layers
// (soft glow, no texture / flow-texture body / thin hot-white core), each with
// its own scroll speed (config.flowScrollSpeed drives the body layer). Endpoints
// tracked smoothly (no ProcBolt flicker). Pool: 16 slots. See
// core/composition/vc_archetype.inl's VFX_ComposeEnergyFlow for the managed-pool
// wrapper (duration auto-expire).
int  SpawnEnergyFlow(ProcRayConfig config, float scale);
void EnergyFlow_SetBrightness(int id, float b);
void EnergyFlow_Update(int id, Vector3 from, Vector3 to, float scale, float dt);
void EnergyFlow_Draw(int id, Camera3D cam);
void EnergyFlow_Kill(int id);

// Unmanaged immediate-mode bolt draw (no pool) — caller owns the 9 waypoints,
// regenerates + draws per frame. Ex = caller-chosen colors (glow = wide outer
// pass, core = bright inner pass); DrawLightningBolt = Ex with the legacy
// violet/white palette.
void RegenerateLightningWaypoints(Vector3 *waypoints9, Vector3 from, Vector3 to, float scale);
void DrawLightningBolt(const Vector3 *waypoints9, float thickness, Camera3D cam);
void DrawLightningBoltEx(const Vector3 *waypoints9, float thickness, Camera3D cam,
                         Color colorGlow, Color colorCore);
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
`Skill_CalculateCooldown()`/`Skill_CalculateManaCost()` only compute numbers — these two hold and check actual elapsed-time state:
```c
bool SkillManager_CanCast(int skillIndex, int agentId);
void SkillManager_TriggerCooldown(int skillIndex, int agentId, float cooldownSeconds);
```
* Keyed by **`(skillIndex, agentId)`** — `agentId` is the caster's `entities/entities.h` agent pool slot (see `PlayerEntity`/`EnemyEntity`'s `agentId` field). Each caster gets an independent cooldown per skill; one caster's Fireball cooldown never blocks another caster's Fireball. Internally a static `float[MAX_SKILLS][256]` table (256 must stay in sync with `entities/entities.h`'s `MAX_AGENTS` — duplicated constant, core/ must not `#include entities/`).
* `SkillManager_TriggerCooldown` ticks down automatically via `UpdateSkillManager(dt, ...)`. Call it yourself at the point a skill actually casts (not wired into any skill's `Cast[Name]Skill` automatically — call-sites need to adopt it).
* `SkillManager_CanCast` returns `true` when remaining cooldown is `<= 0`. Out-of-range `skillIndex`/`agentId` returns `false`.
* **`int Skill_GetIndexByName(const char *name)`** — reverse lookup so a skill can learn its own `skillIndex` (the `RegisterSkill()` return value isn't captured anywhere by convention) in order to actually call `SkillManager_CanCast`/`TriggerCooldown` about itself. Call once in `Init[Name]Skill` (the full registry is already populated by the time any skill's `Init` runs — `InitSkillManager()` registers everything before looping over `init()` callbacks) and cache the result in a `static int s_skillIndex`. Returns `-1` if `name` doesn't exactly match any registered skill's name string (the same string passed to your own `RegisterSkill()` call).

### Abort / Interrupt (`core/skill_manager.h`)
Optional, additive — does **not** change the mandatory skill lifecycle contract in `skills/CLAUDE.md`:
```c
void RegisterSkillAbort(int skillIndex, void (*abort)(int agentId));
void AbortSkill(int skillIndex, int agentId);
```
* A skill calls `RegisterSkillAbort(index, MyAbortFn)` in addition to `RegisterSkill()` if it wants to support being force-aborted (e.g. future crowd-control). Skills that never call this simply can't be force-aborted.
* `AbortSkill(index, agentId)` invokes the registered callback with that `agentId` if present; otherwise logs `LOG_WARNING` and no-ops. Safe to call unconditionally. A skill that tracks per-caster instance ownership (opt-in, not required) can use the `agentId` to abort only that caster's instance instead of every active instance of the skill type; a skill that ignores the parameter aborts everything, same as before.

### Lifecycle-End Query (`core/skill_manager.h`)
Optional, additive — does **not** change the mandatory skill lifecycle contract in `skills/CLAUDE.md`:
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
Thin wireframe overlay for visually tuning hitbox/AoE radii — no equivalent existed before (`core/tuning.h`'s hot-reload let you edit a radius number but not see it in-world).
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

// Camera-facing quad (2 tris) — for shader-driven effects whose SHAPE should
// come entirely from the fragment shader's alpha (e.g. a radial-biased
// erosion/dissolve, see energy_smoke.fs), not from a mesh silhouette. A
// sphere's outline is always a hard geometric circle in screen space no
// matter what the surface shader does; a flat quad has none of that
// constraint. vertexNormal faces the camera (-forward) so shaders reading
// fragNormal (fresnel/rim terms) still get a sane value. Much cheaper than
// DrawCoreSphere too (2 tris vs. e.g. 800 for a 20x20 sphere).
void DrawCoreBillboardQuad(
    Vector3 center,
    float halfSize,
    Camera3D cam,
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

void ProceduralMesh_BuildTubeAlongPath(
    TubeMeshData *out,
    const Vector3 *pathPoints,
    int pathCount,
    float baseRadius,
    float startT,
    float endT,
    float time,
    int segments,
    int radialSegs,
    const TubeMeshConfig *cfg
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

Mesh ProceduralMesh_BuildRockTemplateMesh(float radius, float jitterAmount, int seed, int subdivisions);
```
Rules:
- For **prominent/large rocks only**. Small background rubble should keep using squished `DrawCoreCube`/`DrawCoreSphere` with per-instance randomization (§12.3) — don't switch those over to this function.
- Built from a base icosahedron (12 verts/20 faces), recursively subdivided (`subdivisions`, clamped to 0-2 — level 2 ≈ 162 verts, at the `ROCK_MESH_MAX_VERTS` ceiling), then each vertex's radial distance from `center` is jittered within `±jitterAmount` via a deterministic hash PRNG keyed on `seed` + vertex index. Same `seed` always produces the same rock shape.
- Flat-shaded (per-face normals, not per-vertex averaged) so facets read as angular/natural, not as a smoothed sphere — this is the key difference from squishing `DrawCoreSphere`.
- **Build once at cast time and cache in the skill's instance struct** — unlike `BuildTube`/`BuildWavePlane`, rocks don't animate their shape, so there's no reason to rebuild every frame.
- **`ProceduralMesh_BuildRockTemplateMesh`** — GPU-resident (`UploadMesh`) single-rock template, same "build once ever" convention as `ProceduralMesh_BuildCrystalTemplateMesh`. Use this instead of `ProceduralMesh_BuildRock`/`ProceduralMesh_DrawRock` when N rock copies need `DrawMeshInstanced` in one frame — see the GPU Instancing decision tree above and `VFX_ComposeFloatingStones` (`vc_earth.inl`) for the reference call site. Flat-shaded like `DrawRock`, expanded to 3 unique verts/face (no shared vertex indices) since instancing needs a plain vertex buffer, not an index+face-normal lookup.

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

#### Crystal / Crystal Cluster (faceted crystal spike + multi-crystal cluster)

```c
typedef struct {
    float height, radius, taper, twist, noise, bevel, split;
    int sides, segments;   // sides<3 or segments<2 => no-op
} CrystalDesc;

// Single crystal, immediate-mode draw (1 rlBegin/rlEnd per call).
void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color);

#define CRYSTAL_CLUSTER_MAX_CRYSTALS 8
#define CRYSTAL_CLUSTER_MAX_TRIS 1024

typedef struct {
    Vector3 pos[CRYSTAL_CLUSTER_MAX_TRIS * 3];
    Vector3 normal[CRYSTAL_CLUSTER_MAX_TRIS * 3];
    Vector2 uv[CRYSTAL_CLUSTER_MAX_TRIS * 3];
    int triCount;
} CrystalClusterMeshData;

void ProceduralMesh_BuildCrystalCluster(
    CrystalClusterMeshData *out,
    Vector3 center, const CrystalDesc *desc,
    int count, int seed, float progress
);
void ProceduralMesh_DrawCrystalClusterMesh(const CrystalClusterMeshData *data, Color color);

// Convenience build+draw wrapper — same signature as before the batching
// rewrite, no call-site changes needed.
void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color);
```
Rules:
- `ProceduralMesh_DrawCrystal` draws one faceted crystal spike (`sides`-gon cross-section, `segments` rings, taper/twist/noise/random-offset apex), clamped to `sides<=16`, `segments<=16`.
- `ProceduralMesh_DrawCrystalCluster` used to `rlPushMatrix`+draw each crystal separately (N draw calls for N crystals). It now builds the whole cluster (children positioned/tilted per deterministic `seed`, same layout as before) into one flat `CrystalClusterMeshData` buffer — tilt applied via CPU `Vector3RotateByAxisAngle` instead of the GL matrix stack — then submits it with exactly **one** `rlBegin(RL_TRIANGLES)/rlEnd()`. The function signature is unchanged; existing call sites (`vc_metal.inl`, `vc_water.inl`) get the win with no changes.
- Cluster children are LOD-capped to `sides<=8, segments<=8` regardless of the parent `desc` (cluster crystals are secondary/ambient detail, not the hero shape) — this bounds `CrystalClusterMeshData`'s static footprint. `count` is capped to `CRYSTAL_CLUSTER_MAX_CRYSTALS` (8).
- If you need to animate `progress` (grow-in reveal) call `ProceduralMesh_BuildCrystalCluster` yourself each frame with the current `progress` and cache the `CrystalClusterMeshData` in the skill's instance struct — same idea as `BuildTube`/`BuildWavePlane`'s per-frame rebuild convention, just batched instead of one-crystal-at-a-time. If `progress` is always `1.0` for your use case, building once at cast time and reusing the cached buffer is cheaper (matches Rock/ShardCluster's build-once convention).
- This immediate-mode path is for **small ambient clusters only** (3-8 low-detail crystals, e.g. micro-crystal debris at a skill's base). For a **hero burst** (e.g. 10 crystals at full `sides<=16, segments<=16` detail, spawned once and alive for several seconds) rebuilding + resubmitting thousands of vertices through `rlVertex3f` every frame is a measured CPU bottleneck (not a "draw call count" problem — `rlgl` already batches same-primitive submissions into one GL call; the cost is the sheer number of per-vertex function calls + trig/cross/normalize math redone every frame for geometry that isn't changing). Use the GPU-resident mesh API below instead.

##### Crystal Cluster — GPU-resident mesh (hero bursts, e.g. 10-crystal cast)

```c
Mesh ProceduralMesh_BuildCrystalClusterMesh(const CrystalDesc *desc, int count, int seed);
void ProceduralMesh_DrawBakedCrystalCluster(Mesh mesh, Material material, Matrix transform);
Material ProceduralMesh_GetPassthroughMaterial(Shader shader);

// core/material/material_system.h
void CrystalMaterial_SetGrowProgress(CrystalMaterial mat, float progress); // 0..1, default 1.0
```
Two GPU-resident approaches exist — **prefer the template one** (see perf warning below):

**`ProceduralMesh_BuildCrystalTemplateMesh` (recommended default for burst/repeated casts):**
```c
Mesh ProceduralMesh_BuildCrystalTemplateMesh(const CrystalDesc *desc);
```
- Builds **one** crystal (local space, centered at origin, upright — no position/tilt/scale jitter). Call **once ever** (lazy static, same lifetime convention as a loaded shader/texture) — never rebuild, never unload for the lifetime of the process.
- Draw N crystals that all "look different" by looping `ProceduralMesh_DrawBakedCrystalCluster(templateMesh, material, transform)` with a **different `transform` per crystal** (translate/rotate/non-uniform-scale computed on the CPU from a per-instance deterministic hash) — no new `Mesh`, no `UploadMesh`, ever, after the first build. See `VFX_DrawIceCrystalBurst` (`core/composition/vc_water.inl`) for the full worked example (LCG hash → position/tilt/height-radius-scale → `MatrixScale`×`MatrixRotateY/Z`×`MatrixTranslate` composed the standard raylib TRS way).

**`ProceduralMesh_BuildCrystalClusterMesh` (rare/static use only):**
```c
Mesh ProceduralMesh_BuildCrystalClusterMesh(const CrystalDesc *desc, int count, int seed);
```
- Bakes a **whole pre-scattered cluster** (position/tilt jitter baked into the geometry itself, driven by `seed`) into one `Mesh`.
- **Perf trap:** this calls `UploadMesh` — a real GPU-driver synchronization point (`glGenBuffers`/`glBufferData`), not a cheap CPU operation. Building it once (e.g. a static decorative prop baked at level load) is fine. Building a **new** one on every cast (the first version of this API did this) is fine for a single character casting occasionally, but **stutters** the moment several casts complete in the same short window (multiple characters, or rapid repeated clicks) — several `UploadMesh` calls bunching up in one/few frames is the actual bottleneck, not `DrawMesh` (which is cheap even called 30+ times/frame). If you need each cast to look different, use the template approach above instead — it gets the same "no two casts look identical" result via per-instance transform, with zero `UploadMesh` after the first ever call.

Shared rules for both:
- Baked at `progress = 1.0` (fully grown) always — the "grow up from the ground" reveal animation is **not** baked into CPU vertices. `core/shaders/crystal.vs` has a `u_growProgress` uniform (`vertexPosition.y *= u_growProgress` before MVP) set via `CrystalMaterial_SetGrowProgress`. `CrystalMaterial_Begin` always resets `u_growProgress` to `1.0` first (so old `ProceduralMesh_DrawCrystal`/`DrawCrystalCluster` immediate-mode call sites, which bake `progress` at the CPU level, are unaffected).
- Draw between `CrystalMaterial_Begin`/`CrystalMaterial_End` (or any other `BeginShaderMode`-wrapped block): `DrawMesh`/`ProceduralMesh_DrawBakedCrystalCluster` set `mvp`/`matModel` correctly via `shader.locs[]` — see the `matModel` identity-default comment in `SkillManager_BeginShader` (`core/skill_manager.c`), which exists specifically so `DrawMesh` composes safely with a shader already bound via `BeginShader`. Get the `Material` via `ProceduralMesh_GetPassthroughMaterial(mat.shader)` — a cached `LoadMaterialDefault()` with `.shader` swapped in, used purely as a vehicle for `DrawMesh`; it never touches `u_baseColor`/texture1/etc, those stay owned by `CrystalMaterial_Begin`.
- Mesh is built in **local space centered at the origin** — position/orient/scale via the `transform` matrix, not by baking world position into the geometry.
- **Ready-made ice/water wrapper** (`core/composition/visual_composer.h`, `vc_water.inl`) — skills don't need to touch `ProceduralMesh_*`/`CrystalMaterial_*` directly for the ice-crystal case:
  ```c
  void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress); // call every frame, no Build/Unload needed
  ```
  Uses the template mesh **+ GPU instancing** internally (see "GPU Instancing" standard below — `core/shaders/crystal_instanced.vs` + `CrystalMaterialInstanced`) — the whole burst is submitted with **one** `DrawMeshInstanced` call, not one `DrawMesh` per crystal. No `Mesh` to cache or unload in the skill's instance struct, just call this once per frame while the VFX is alive. Pass a **different `seed` per cast** (e.g. derived from `GetTime()`, a per-agent cast counter, or `agentId` mixed with cast index) if you want each cast to look different — `seed` is the only source of shape variety. The same `seed` always reproduces the same layout (by design, for reproducibility/testing). **Trade-off:** `growProgress` is one uniform shared by the whole instanced batch — all crystals in one burst grow in lockstep (no per-crystal stagger; that was possible with the older per-`DrawMesh` loop but isn't with instancing — see the standard below for why).

##### Creating a new procedural mesh — decision tree (read this first)

Three tiers, in order of increasing engineering cost. Pick the **lowest tier that's actually insufficient** — don't jump straight to instancing because a loop exists (PROGRESS.md Item 39's audit-before-optimizing principle: rlgl/GPU budgets handle dozens of `DrawMesh`/`rlBegin` calls per frame without issue). Every shape currently in `core/geometry/pm_*.inl` has been through this audit at least once (Item 39/40) — check the relevant `pm_*.inl`/`vc_*.inl` file's comments before assuming a shape needs converting.

1. **Single shape, immediate-mode (`rlBegin`/`rlEnd`)** — fine as-is when the effect draws **one instance** of the shape per frame (one tube, one funnel, one pillar, one puddle), even if the shape is complex (many segments/rings). Consolidate to the fewest `rlBegin`/`rlEnd` calls the shape's structure allows (one for the whole shape, or one per logical part like "body" + "caps" — never one per sub-element in a loop, that's Item 39 pattern 1). Animated shapes (spinning funnel, flowing tube, undulating wave plane) *should* rebuild their vertex buffer every frame by design — `DrawMeshInstanced`/a cached `Mesh` has nothing to offer a single instance, don't reach for it here. Reference: `pm_tube.inl`, `pm_magic_effects.inl` (Fissure, VortexFunnel), `pm_organic.inl`, `pm_water_waves.inl` — all audited, all correctly at this tier, no changes needed.
2. **Build-once template + N × `DrawMesh`** — when a shape needs to be **cast/spawned repeatedly** (not redrawn every frame from scratch) but doesn't have enough simultaneous copies to justify instancing, or per-instance variation isn't purely a transform (e.g. per-instance grow-stagger, per-instance color). Build the `Mesh` **once**, cache it (`MeshCache_*`-style seed-keyed cache, or a single lazy `static Mesh`), `UploadMesh` exactly once ever — never re-`UploadMesh` per cast, that's the stutter Item 40 fixed for crystals (`UploadMesh` is a real GPU-driver sync point, not cheap like `DrawMesh`). `ProceduralMesh_BuildCrystalClusterMesh` is the documented "wrong for bursty casts, fine for a genuinely-static one-off prop" example above — read its doc comment before reusing that specific function.
3. **Build-once template + `DrawMeshInstanced`** — when N copies of one shape are drawn **every frame while the effect is alive** (an ongoing/ambient effect, not a one-shot burst) and per-instance variation is expressible as a transform (position/rotation/scale). The soft threshold is **N≳10 for a one-shot burst**, but a smaller N (down to ~4-5) still justifies it when the draw repeats **every frame for the effect's lifetime** rather than once per cast — the win compounds over the effect's duration. Two reference implementations exist, pick based on which material system the effect already uses:
   - **`CrystalMaterial`-backed effects** (ice/metal/glass crystal shapes) → `CrystalMaterialInstanced` + `crystal_instanced.vs`. Reference: `VFX_DrawIceCrystalBurst` (`vc_water.inl`, N up to 32, one-shot burst) and `VFX_ComposeMetalShardCluster`'s 4-blade loop (`vc_metal.inl`, N=4, drawn every frame).
   - **`EffectMaterial`-backed effects** (`Material_Get`/`Material_LoadCustom` — the generic preset material used by most other composition code) → `EffectMaterialInstanced` + `effect_material_instanced.vs`. Reference: `VFX_ComposeFloatingStones`'s 5-rock orbit (`vc_earth.inl`, N=5, drawn every frame).
   Don't invent a third instanced-material variant for a new `EffectMaterial` preset or a new `CrystalMaterial` flavor (metal/ice/glass) — reuse the existing instanced twin, just load it with different `EffectMaterialParams`/`CrystalMaterialParams`, exactly like `GetMetalBladeMaterialInstanced` does.

**Implementation checklist for tier 3 (mirror `crystal_instanced.vs`/`CrystalMaterialInstanced` or `effect_material_instanced.vs`/`EffectMaterialInstanced`):**
1. **New `*_instanced.vs` file — never retrofit `core/shaders/common/vs_header.glsl`.** `vs_header.glsl`'s `VS_FinalOutput()` assumes one `matModel` uniform per draw call; instancing needs a per-instance transform read from a vertex *attribute*, not a uniform. Mixing both concerns in the shared header risks breaking every other shader that includes it, and reading an unbound `in mat4 instanceTransform` attribute when a shader ISN'T drawn via `DrawMeshInstanced` is undefined behavior across GPU drivers — keep it in its own dedicated file instead.
2. **Attribute name is fixed: `in mat4 instanceTransform;`.** This is raylib's reserved instancing attribute name — `LoadShader` auto-detects and binds it like `vertexPosition`/`vertexTexCoord`/`vertexNormal`, no extra C-side plumbing needed.
3. **Compute the transform manually, don't call `VS_FinalOutput()`:**
   ```glsl
   mat4 instanceModel = matModel * instanceTransform;      // matModel is identity by default (SkillManager_BeginShader) — instancing doesn't use the DrawMesh `transform` param at all
   fragPosition = vec3(instanceModel * vec4(pos, 1.0));
   fragNormal   = normalize(vec3(instanceModel * vec4(vertexNormal, 0.0)));
   gl_Position  = mvp * instanceTransform * vec4(pos, 1.0); // raylib sets `mvp` = matProjection*matView only (no per-object transform) when instancing
   ```
4. **The `.fs` needs zero changes** — reuse the existing fragment shader unchanged. Instancing is purely a vertex/transform concern; lighting/noise/color logic doesn't know or care how position was computed.
5. **Duplicate the material wrapper, don't try to share one.** A separately-compiled shader program can have different uniform *locations* for the same-named uniform, so the `XMaterial`-style loc-cache struct can't be reused as-is — copy `CrystalMaterial`'s (or `EffectMaterial`'s) struct/`_Load`/`_Begin`/`_End` shape into a second `XMaterialInstanced` pointed at the new shader (see `CrystalMaterialInstanced`/`EffectMaterialInstanced` in `core/material/material_system.h/.c` for the two reference copies). This is accepted, intentional duplication — not a place to build a shared-across-programs abstraction.
6. **CPU side:** build a `Matrix transforms[N]` array (standard raylib TRS composition — `MatrixMultiply(MatrixMultiply(scale, rotation), translation)`, one entry per instance), then exactly one `DrawMeshInstanced(templateMesh, material, transforms, N)` call. `material` still comes from `ProceduralMesh_GetPassthroughMaterial(shader)`.
7. **Per-blade/per-instance geometry variation that ISN'T a rigid transform (e.g. `twist` in `VFX_ComposeMetalShardCluster`'s original per-blade `CrystalDesc`) has to be dropped or approximated by scale** — instancing shares one template's topology across all instances. Document the trade-off at the template-desc definition (see `GetMetalBladeDesc`'s comment in `vc_metal.inl`) so it isn't "fixed" back into per-instance desc fields later without re-breaking instancing.

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
void PostFX_SetMonochrome(float intensity01); // Thái Cực overlay: blends composite saturation toward 0 (1 = full B&W); overrides the config's color grade while > 0, no extra render target. main.c fades it while any agent has taijiActive.

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
- **Multi-texture binding:** `u_bloomTex` uses `SetShaderValueTexture` (called inside `BeginShaderMode`). Do not use `rlActiveTextureSlot`/`rlEnableTexture` for extra textures in post-FX passes — confirmed silently broken (same root cause soft-particle depth tex).

### Camera FX (`core/camera_fx.h`)
```c
void CameraFX_Shake(float trauma);          // Add shake trauma (accumulates, clamped to 1.0)
void CameraFX_Update(Camera3D *camera, float dt); // Update + apply offset to the camera — call every frame
```
* `trauma`: `0.25` = light, `0.5` = medium, `0.75–1.0` = heavy. Accumulates and decays over time.
* `CameraFX_Update` must be called after game-logic update, before `BeginMode3D`. Skill code only calls `CameraFX_Shake`; `Update` belongs to the engine loop.

**Skill Helper impulse (optional — `core/skill_helper.h`):**
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

`WindZone` is a global `ForceField` auto-applied to **every particle** in `UpdateParticles()` — skill code does not assign it per-`ParticleConfig`. Use it to simulate ambient wind, storms, or a baseline force throughout a match.

```c
void    WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq);
void    WindZone_Clear(void);
bool    WindZone_IsActive(void);
// WindZone_Evaluate() is used internally by particle_system — skill code does not call it directly.
```

**Parameters:**
| Param | Meaning | Suggested value |
|---|---|---|
| `direction` | Main wind direction (auto-normalized) | `(Vector3){1,0,0}` = eastward wind |
| `strength` | Base acceleration (m/s²) | `80–250` for light wind to storm |
| `noiseAmp` | Amplitude of the overlaid Curl noise (0 = straight wind) | `30–80` |
| `noiseFreq` | Spatial frequency of the noise | `0.005–0.03` |

**Example:**
```c
// Set up a light north-easterly wind with noise — call at map start or on weather change
WindZone_Set((Vector3){0.7f, 0.0f, 0.3f}, 120.0f, 40.0f, 0.015f);

// Clear on entering an indoor area or when the weather effect ends
WindZone_Clear();
```

---

## 9b. Skill Helper (`core/skill_helper.h`)

High-level convenience wrappers to cut boilerplate. Optional; complex skills often call the core API directly.

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

**Force regime differs from Cast/Impact**: uses the sustained/flight range (300-650f primary directional pull via `FORCE_GRAVITY_DIR`, ~20f `FORCE_NOISE_PERLIN` wobble on top) per API.md §1 and the `water_stream/tube_skill.c` precedent — NOT the 5-60f ambient/burst range used by `SpawnCastEffect`/`SpawnImpactEffect`. Internally backed by an 8-slot static `ForceField` pool (`MAX_CONCURRENT_PROJECTILE_TRAILS`), claimed round-robin per call, same pattern as `SpawnCastEffect`'s pool, so concurrent flying projectiles don't interfere with each other's direction.

### Lightning Arc (instant source → target)

```c
VFX_ComposeLightningArc(casterPos, clickedWorldPos, VC_MAT_LIGHTNING, 0.075f);

// Advanced control: custom branches, post-impact arcing, or deterministic replay seed.
VFX_LightningArcConfig arc = VFX_LightningArc_DefaultConfig();
arc.material = VC_MAT_LIGHTNING;
arc.width = 0.075f;        // metres
arc.travelDuration = 0.10f; // seconds for the discharge head to reach the target
arc.postImpactDuration = 0.30f; // animated hold after impact; 0 = die on impact
arc.coreEmission = 4.5f;    // HDR ion channel; bloom responds to this value
arc.haloEmission = 0.42f;   // low-energy, soft surrounding field
arc.jaggedness = 0.80f;    // maximum metres of lateral kink
arc.branchCount = 2;       // 0..2
VFX_LightningArc_Spawn(casterPos, clickedWorldPos, &arc);
```

Use this for an instantaneous strike, tether hit, or click-to-target cast. It
does not create any `VFXLight` itself. A skill that needs environmental light
should explicitly spawn short contact lights at `casterPos` and/or
`clickedWorldPos`; never add a midpoint light to the reusable arc primitive.
owns a 32-slot pool. After the discharge head reaches the target it remains
alive for `postImpactDuration` seconds (zero means it dies on impact), advances
its FBM phase faster, and fades out only at the end of that hold. It re-seeds
its precomputed 3D polyline every
45 ms, while the endpoints remain exact. Its trunk uses Core's midpoint
displacement primitive for its optional branches (3–4 bounded subdivision
levels, amplitude decaying by 0.52 per level). The primary line is generated by
the dedicated `core/lightning/` shader: one camera-facing source→target canvas,
FBM domain warp, then bounded distance-to-centreline. The warp is pinned at the
two endpoints, so it remains exactly attached to the caster and clicked target.
`core/lightning/lightning_stroke.c` advances a 0→1 travel value over
`travelDuration`; `core/lightning/shaders/lightning_stroke.fs` uses it to reveal
the filament from `from` to `to` and brighten only the advancing ionisation
head. Its centreline uses the endpoint-pinned FBM profile. The near-white ion
channel has an HDR `coreEmission` multiplier; a saturated electric-blue corona
then bridges it continuously into a paler cubic-fade, low-energy outer field.
The corona is intentionally stronger close to the core without expanding its
SDF radius, so it reads as energy density rather than a second blue band.
The small alpha body is deliberately pale and only preserves hue over bright
destinations — it is not an opaque blue outline.
The same shader applies a rounded endpoint taper and a compact contact glint at
both exact endpoints, so the canvas is never visibly cut square at the source
or clicked target.
The electric-blue body is alpha-composited into the shared VFX body layer; its
compact halo and near-white core are emitted together in one additive pass.
The convenience call floors width at 0.075 m. Do not replace
this with a physics trail: it cannot preserve sharp electrical kinks. Call
`VFX_LightningArc_SetEndpoints` only when an active arc must follow a moving
socket; normally it self-expires. `seed = 0` derives a deterministic seed from
the endpoints, while a nonzero seed supports replay/network determinism.

### Moving Lightning Trail and Ground Impact

```c
VFX_LightningTrailConfig trail = VFX_LightningTrail_DefaultConfig();
trail.material = VC_MAT_LIGHTNING;
trail.width = 0.055f;
trail.pointLifetime = 0.26f;
int id = VFX_LightningTrail_Spawn(projectileHead, &trail);

// Each update while the head is alive:
VFX_LightningTrail_SetHead(id, projectileHead);
// On cancellation: VFX_LightningTrail_Kill(id);
// On a normal end: VFX_LightningTrail_Stop(id); // retained path fades itself

// One-shot reference composition used by the VFX tester:
VFX_ComposeLightningGroundRicochet(hitPos, VC_MAT_LIGHTNING, 1.0f, seed);
```

`LightningTrail` is the free-path counterpart to `LightningArc`. It records a
moving head and submits the resulting polyline through
`LightningStroke_SpawnPath` / `LightningStroke_SetPath`. The carrier is a
single connected ribbon with one normalized arc-length UV, while the exact same
distance-field shader used by the two-point arc supplies its narrow HDR ion
core, blue corona, soft field, animated flow and rounded endpoints. Therefore a
curved path remains one discharge — it never restarts a miniature bolt or halo
at a history node.

This is intentionally a dedicated lightning path, not a generic Trail material:
the ordinary Trail system owns smoke, energy ribbons and physical tails, while
lightning needs the stroke renderer's precise travel/reveal, endpoint contacts
and cinematic HDR colour profile. `Stop` begins the configured `pointLifetime`
post-impact hold while the filament keeps re-phasing; `Kill` removes it
immediately. The low-level multi-point `LightningStroke` API is reusable for
any authored or sampled curve.

`VFX_ComposeLightningGroundRicochet` is a reusable reference composition, not a
special renderer. Two short discharge heads leave `impactPos`; every 105 ms one
lands, fades, and a fresh discharge starts from that exact contact. This avoids
one long, projectile-shaped trail. There are five hops: horizontal distance
decays by 0.67 and apex height by 0.62 each time, with a small transverse snap
to keep the route electrically irregular. The function does not spawn contact
lights; a skill may opt into separate source/impact lights for its own gameplay
beat. Its eight-instance composition pool owns at most 16 active trail slots;
on pressure it first reclaims the oldest instance and its two child trails.

For another non-lightning irregular path, use the lower-level Core primitive
instead of copying the subdivision loop:

```c
Vector3 points[RIBBON_MIDPOINT_MAX_POINTS];
RibbonMidpointConfig path = Ribbon_MidpointDefaultConfig();
path.levels = 4;                 // 17 points; hard cap is 5 / 33 points
path.initialAmplitude = 0.10f;   // metres at the first split
path.amplitudeDecay = 0.5f;      // each later split is smaller
path.seed = seed;
int pointCount = Ribbon_GenerateMidpointDisplacement(a, b, &path, points,
                                                      RIBBON_MIDPOINT_MAX_POINTS);
```

It is allocation-free and deterministic; it only builds points. Draw the result
with the appropriate existing Ribbon/Tube primitive and own the blend/layer
state at that caller.

### Lightning Trail Presets
```c
int SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed);
int SpawnLightningFollowerTrail(Vector3 startPos, float scale, float life);
```
Dedicated jagged/flicker profile for electric visuals (bolts, electric blades, electric projectiles, teleport streaks) — `SpawnProjectileTrail`'s `EffectPresetType` path only swaps color/gradient, it can't reproduce lightning's signature zigzag because its flight wobble (20f/0.08f `FORCE_NOISE_PERLIN`) is tuned for a smooth arc, not a jagged bolt.

* **`SpawnLightningTrail`**: flight-stage bolt (electric bolt / electric projectile / teleport streak) that travels from `start` to `target` along a **precomputed jagged polyline**, not a physics/noise-driven path. Two earlier passes tried physics instead — `TRAIL_TYPE_PROJECTILE` (with and without `FORCE_GRAVITY_DIR`, with `wobbleAmplitudeOverride` cranked up) rendered as a visually straight line because its built-in homing steer damps any deviation back toward straight every frame; `TRAIL_TYPE_WISP` with a strong noise `forceField` rendered as a smooth "silk ribbon" sag because `ConstrainRibbonSegment`'s distance-solver (needed to keep the strand rope-coherent) low-pass-filters per-node jaggedness into a flowing curve. Neither can hold a real sharp-angle zigzag — both are built to stay smooth by design. The fix: `GenerateLightningWaypoints` (internal to `skill_helper.c`) builds `LIGHTNING_BOLT_WAYPOINTS` (9) points along `start`→`target`, offsetting each interior point sideways (alternating sign, two perpendicular axes relative to the travel direction) by `jaggedAmount` — a real geometric kink, with no `forceField` involved at all, so no gravity-like sag is possible. A `TRAIL_TYPE_FOLLOWER` trail is spawned with `onUpdate = LightningBoltAdvance`, which each frame advances progress along the waypoint polyline (`SampleLightningPath`) and pushes the interpolated point via `UpdateFollowerPosition` — `LIGHTNING_BOLT_PUSH_COUNT` (50) total pushes spread proportionally over the travel duration (`boltLen / speed`), staying under `TRAIL_HISTORY_COUNT` (60) so no earlier point of the bolt gets evicted from history — the whole bolt stays visible from start to current tip, not just a trailing window. `LightningBoltAdvance` also spawns a small short-lived particle (the visible spark) at the tip on every push (exactly on the path, not a separately physics-simulated dot that could diverge) and an occasional `VFXLight_Spawn` flicker. Progress reaching 1.0 self-terminates via `KillTrail` — the caller does **not** need to call `KillTrail(id)` itself for the normal flow (only if cancelling early). Per-bolt state (waypoints, elapsed time, push count) lives in a `MAX_CONCURRENT_LIGHTNING_TRAILS`-slot round-robin pool (`s_lightningBolts`) keyed by trail ID, looked up inside the callback since `TrailUpdateCallback` carries no userdata pointer.
* **`SpawnLightningFollowerTrail`** + **`Lightning_UpdateFollowerTip`**: manually-driven variant for an electric aura/bolt **attached to a moving object at a fixed local point** (an electric sword — a spark pinned to a point on a moving object) — `TRAIL_TYPE_FOLLOWER`, thin (`thick = 1.0f*scale`). Drive the tip with **`Lightning_UpdateFollowerTip(id, tipPos, scale)`, not the raw `UpdateFollowerPosition`**: feeding a smooth per-frame path straight into `UpdateFollowerPosition` records one history node per frame — a dense, smooth curve that reads as a wiggly worm, not lightning (confirmed visually testing an orbiting anchor this way). `Lightning_UpdateFollowerTip` only accepts a new point once the caller's real position has moved `LIGHTNING_FOLLOWER_MIN_SEGMENT` (45f, scaled) away from the last recorded one — few, far-apart points — and inserts one perpendicular-offset kink at the midpoint of each accepted segment (real geometric displacement, same philosophy as `GenerateLightningWaypoints`, just applied incrementally instead of precomputed). This turns *any* caller-driven path into a sparse zigzag automatically, regardless of how often the caller calls it (e.g. every frame). Per-trail filter state (last recorded position, alternating kink sign) lives in a small round-robin pool keyed by trail ID, same lookup-by-ID pattern as `SpawnLightningTrail`'s bolt state. The trail's own `forceField` (`FORCE_NOISE_PERLIN` 100f/0.6/14 + `FORCE_VISCOSITY` 4.0f — lighter than before, since it now only adds crackle texture on top of the filter's real kinks instead of having to invent jaggedness from scratch) is unrelated to gravity, so dragging behind a moving anchor comes purely from the trail's own history-lag. Caller **MUST call `KillTrail(id)`** when the effect ends. `Trail_AttachToTransform()` still works for a smooth (non-electric) FOLLOWER use if the zigzag filtering isn't wanted.
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
int  SpawnDamageVolume(DamageVolume config); // Returns ID
void DamageVolume_Unload(void);
```

### Skill Timeline
```c
typedef struct { float current; float duration; } SkillTimeline;
void Timeline_Start(SkillTimeline *t, float duration);
bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt); // true for exactly 1 frame when the time is reached
bool Timeline_Finished(SkillTimeline *t);
```
Use it to orchestrate a multi-step event sequence without a manual state machine.

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
* **`Material_Load` (4 presets):** all presets are `effect_material`-backed — each is a hardcoded `EffectMaterialParams` over the same shared `core/shaders/effect_material.vs/.fs` that `Material_LoadCustom` uses (the old per-skill shaders these presets borrowed were deleted from the repo). Signature and enum unchanged. Preset params: FIRE = `ELEMENT_COLOR_FIRE`, rim 1.2/fresnel 3/emissive 1.5/distortion 0.4/translucency 0; ICE = pale blue `(170,220,255)`, rim 1.5/fresnel 5/emissive 0.5/distortion 0.05/translucency 0.6; WATER = `ELEMENT_COLOR_WATER`, rim 1.0/fresnel 4/emissive 0.6/distortion 0.25/translucency 0.85; PORTAL = `ELEMENT_COLOR_TAIJI`, rim 2.0/fresnel 2/emissive 2.0/distortion 0.6/translucency 0.3.
* **`Material_LoadCustom` (new):** always backed by the shared `core/shaders/effect_material.vs/.fs` — one shader, look configured entirely via `EffectMaterialParams` uniforms (`u_baseColor`, `u_rimStrength`, `u_fresnelPower`, `u_emissiveIntensity`, `u_distortionStrength`, `u_translucency`, optional `texture1`). No new GLSL needed per combination.
* **Rim glow is weighted by light-facing direction**, not view angle alone: plain Fresnel glows evenly around the whole silhouette regardless of where the light is, which reads as "rim doesn't match the light". `rim = fresnel * mix(0.3, 1.0, max(dot(normal, lightDir), 0.0))` — dimmed (not zeroed) on the backlit side.
* **`translucency`** (default 0 = opaque, unchanged from initial implementation): set to `1.0` for the same "center see-through, edges more solid" look as `tube.fs` (`alpha = mix(0.3, 0.9, fresnel)`), driven by the same fresnel term as the rim. **Caller must wrap the draw in `BeginBlendMode(BLEND_ALPHA)`/`EndBlendMode()`** for alpha < 1 to actually blend — `Material_Begin`/`Material_End` do not manage blend mode themselves.
* **This shader ignores per-vertex color** (`vs_header.glsl`/`fs_header.glsl`'s 3D-lighting convention doesn't carry a `fragColor` varying) — tint comes only from `u_baseColor`. The `Color` argument passed to whatever mesh-draw call you use inside `Material_Begin`/`Material_End` has no visual effect with this material.
* **`texture1` is optional** — `EffectMaterialParams.texture1.id == 0` skips the sample entirely (guarded by `u_hasTexture1` in the shader) rather than sampling an unbound/stale texture unit. Sampled as a luminance mask (`.r` channel only, not `.rgb`) — importing the texture's own hue directly onto a mesh with very different UV density than what it was authored for (e.g. a flat ground-decal crack texture on a sphere, which pinches hard at the poles) produces visible color noise.
* **`Material_SetFloat`** still works unmodified on `Material_LoadCustom` materials for any uniform name, including animating `u_dissolve` frame-to-frame (see `core_test`'s usage: solid hold, then dissolve out over the last second). Dissolve's edge-glow only evaluates once `u_dissolve > 0.0` — `fx.glsl`'s `dissolveCalc()` computes a nonzero `edgeFactor` for ~8% of fragments even at `dissolve == 0.0`, which would otherwise show as speckle the instant the material appears.

**`EffectMaterialInstanced`** — GPU-instancing twin of `EffectMaterial`, same relationship as `CrystalMaterialInstanced` is to `CrystalMaterial` (separate shader program → separate uniform-location cache, can't share the struct):
```c
EffectMaterialInstanced EffectMaterialInstanced_Load(EffectMaterialParams params);
void EffectMaterialInstanced_Begin(EffectMaterialInstanced mat);
void EffectMaterialInstanced_End(void);
```
Backed by `core/shaders/effect_material_instanced.vs` (+ unchanged `effect_material.fs`) — same wobble/distortion math as `effect_material.vs`, reading `instanceTransform` instead of a per-draw `matModel`. Use with `DrawMeshInstanced` when an `EffectMaterial`-backed shape (i.e. anything drawn via `Material_Get`/`Material_LoadCustom`) needs N transform-only copies in one frame — see the GPU Instancing decision tree above and `VFX_ComposeFloatingStones` (`vc_earth.inl`) for the reference call site. No `SetFloat`/`SetGrowProgress`-equivalent yet — add one only if a concrete effect needs a per-frame-varying uniform on the instanced path (none has so far).

### Plasma Material (`core/material/material_system.h` — `plasma_shell.vs/.fs`)
Wispy energy membrane: alpha = fresnel × animated fbm noise → **fully transparent center** (which `EffectMaterialParams.translucency` can't do — it has a 0.3 alpha floor at the center). Draw the sphere under `BLEND_ADDITIVE`; disable backface culling to get a free back membrane layer. The VS displaces the surface itself (sines on `vertexNormal` — immediate-mode/Android-Rule-D safe).
```c
typedef struct {
    Color baseColor;    // membrane body tint (alpha = master alpha)
    Color wispColor;    // bright wisp-tip tint
    float noiseScale;   // wisp frequency over the sphere (2.5-4.0)
    float noiseSpeed;   // wisp drift speed (0.3-0.8, negative = reverse)
    float fresnelPower; // 1..8 — higher = hollower center
    float rimStrength;  // 0..~2 rim brightness boost
    float emissive;     // 0..~2 self-illumination
    float opacity;      // 0..1 master alpha
    float displaceAmp;  // surface displacement amplitude (world units, ~8% radius)
} PlasmaMaterialParams;

PlasmaMaterial PlasmaMaterial_Load(PlasmaMaterialParams params);
void PlasmaMaterial_Begin(PlasmaMaterial mat); // change params at runtime: set mat.params before Begin
void PlasmaMaterial_End(void);
```
Real usage example: `VFX_ComposePlasmaOrb` (`vc_plasma.inl`).

> Prop Lit / Grass Material moved to `maps/toolkit/` (Map Agent-owned — only
> `maps/` ever used them) — see `maps/docs/API.md`'s Toolkit API section, not here.

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
void SkillBuilder_Build(SkillBuildContext *ctx); // Call last to activate everything

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

## 10. GLSL Shader Guidelines & 3D Rendering Best Practices

See **[SHADER_API.md](SHADER_API.md)** for the full reference:
common headers (vs/fs/lighting/noise/fx/triplanar), built-in variables/functions,
custom uniforms, Android/GLES compatibility rules A–E, `matModel` landmine,
3D lighting rules, procedural noise guidelines.

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

## 18. Real Shading — Surface Material & Quality Toggle (`#include "core/surface_material.h"` / `#include "core/gfx_quality.h"`)

Stylized-realism forward lighting for opaque scene meshes (characters/props/map/bosses) — half-Lambert diffuse + hemispheric ambient + Blinn sheen + cool Fresnel rim + emissive + distance fog, all in one shader (`core/shaders/surface_lit.{vs,fs}`) gated by a single quality tier. See `REAL_SHADING_PLAN.md` / `REAL_SHADING_SPEC.md` for full design rationale.

```c
// core/gfx_quality.h
typedef enum { GFX_UNLIT = 0, GFX_LOW = 1, GFX_MED = 2, GFX_HIGH = 3 } GfxQuality;
void       GfxQuality_Set(GfxQuality q);
GfxQuality GfxQuality_Get(void);
GfxQuality GfxQuality_Default(void); // GFX_HIGH desktop, GFX_MED __ANDROID__

// core/surface_material.h
void   SurfaceMaterial_Init(void);              // once, after window/GL is up
Shader SurfaceMaterial_GetShader(void);
void   SurfaceMaterial_Apply(Model *model);      // once per model after loading
void   SurfaceMaterial_UpdateFrame(Camera3D camera); // once per frame before drawing lit models

// Matcap / lit-sphere material (P3c, MED+ only) — jade/metal/energy props.
// Shader is shared globally, so this is a per-call toggle, not per-material
// state: call SetMatcapActive right before the DrawModel(s) that should use
// it, ClearMatcap right after so later draws aren't affected.
void SurfaceMaterial_SetMatcapActive(Texture2D matcap, float amount); // amount in [0,1]
void SurfaceMaterial_ClearMatcap(void);
```

**Tiers** (one shader, `uniform int u_qualityTier`, runtime `>=` branches — no shader-variant matrix):
- `GFX_UNLIT` (0) — cheap passthrough: `albedo * colDiffuse * fragColor`, zero lighting cost.
- `GFX_LOW` (1) — half-Lambert diffuse + hemispheric ambient (`u_skyColor`/`u_groundColor`, from `Environment_GetSkyAmbient/GetGroundAmbient`) + Fresnel rim + emissive. All cheap ALU, the signature moonlit look.
- `GFX_MED` (2, default) — LOW + Blinn spec sheen + directional-moon-facing rim tint (`smoothstep(-0.2, 0.6, dot(N,-L))` narrows the rim to the anti-moon silhouette instead of a uniform halo) + optional matcap (below).
- `GFX_HIGH` (3) — MED + normal mapping + anisotropic sheen + fake jade/skin SSS (below).

**Matcap materials** (MED+, `SurfaceMaterial_SetMatcapActive`/`ClearMatcap`) — no authored matcap textures exist in `assets/` yet (Art task); the shader/API plumbing is done and inert (`u_hasMatcap` defaults to 0) until a texture is supplied. Because the shader instance is shared across all models, this is a **call-around-the-draw** toggle, not a per-material flag baked at `Apply` time — wrap the specific `DrawModel` call(s) that should use it.

**HIGH-tier extras** (P5, same call-around-the-draw pattern, no-op below `GFX_HIGH`):
```c
void SurfaceMaterial_SetNormalMapActive(Texture2D normalMap); void SurfaceMaterial_ClearNormalMap(void);
void SurfaceMaterial_SetAniso(float anisoShininess);          void SurfaceMaterial_ClearAniso(void);
void SurfaceMaterial_SetSSS(float strength, float power);     void SurfaceMaterial_ClearSSS(void); // strength=0 clears
```
- **Normal map** — `SurfaceMaterial_Apply` now calls `GenMeshTangents` on every mesh so tangents exist even without authored export data; the VS builds a world-space TBN (`fragTBN`), the FS perturbs `N` before any lighting term when `u_hasNormalMap > 0.5`.
- **Anisotropic sheen** — streaks the Blinn highlight along the tangent (hair/silk) instead of dotting it; `anisoShininess` reuses `u_specStrength` for intensity.
- **Fake SSS** — cheap back-scatter (`pow(dot(V,-L), power) * strength`) for jade/skin/thin-robe edges glowing with the moon behind them.
No authored normal-map textures exist yet either (Art/Character task) — the plumbing is done and inert until one is supplied.

**Real shadow map** (P6, HIGH+Shadow, Environment-owned — `#include "environment/env_shadow.h"`): single directional depth pass + 3×3 PCF, an opt-in layer on top of HIGH; fake blob shadows (`Environment_DrawSmartShadow`) remain the default everywhere else. **OFF by default on every platform** — not yet profiled on Mali.

> [!NOTE]
> **STATUS (2026-07-19, session 3): PARTIALLY working — paused, not a shipped feature.**
> Renders a coherent caster-following shadow on the ground, but its position/size drift with the
> caster's distance from the arena center (unresolved rlvk-side scale mismatch; plus several open
> contradictions — e.g. identical code shows no shadow at 1024² but a displaced one at 2048²).
> Full evidence log, the numeric debug instrument (**H** hotkey → `EnvShadow_DebugDump`), and the
> recommended Renderer-agent next steps are in **`environment/docs/REAL_SHADING_P6_NOTES.md`** —
> read it before touching this code again. Harmless as-is: `EnvShadow_SetEnabled` defaults `false`
> everywhere, so none of this runs unless a developer explicitly enables it (**J** in-game).
```c
void       EnvShadow_Init(void);           // once, after Environment_Init + SurfaceMaterial_Init
void       EnvShadow_SetEnabled(bool enabled);
bool       EnvShadow_IsEnabled(void);
void       EnvShadow_BeginCapture(void);   // begin the light-space depth pass
void       EnvShadow_EndCapture(void);
Shader     EnvShadow_GetDepthShader(void);
Matrix     EnvShadow_GetLightVP(void);
Texture2D  EnvShadow_GetShadowMap(void);

void SurfaceMaterial_BeginShadowCast(Model model, Shader depthShader); // swap a Model's materials to the depth shader
void SurfaceMaterial_EndShadowCast(Model model);                       // swap back to the lit shader
```
`main.c` wires it once per frame: if `EnvShadow_IsEnabled()`, wraps `EnvShadow_BeginCapture()`/`EndCapture()` around re-invoking the same draw calls the real scene uses (`DrawSandbox3D`/`GameScreen_Draw3D`+`Boss_Draw`+`Formation_Draw`), with the character model's shader swapped to the depth shader for that pass via `SurfaceMaterial_BeginShadowCast`/`EndShadowCast`. `SurfaceMaterial_UpdateFrame` then pushes `u_lightVP`/`shadowMap`/`u_shadowEnabled` automatically. In-game: **J** toggles it, shown in the corner HUD next to the GFX tier.
- Depth target: sampleable depth **texture** (not the renderbuffer `LoadRenderTexture` gives you) built via raw `rlgl` calls (`rlLoadTextureDepth` + `rlFramebufferAttach(..., RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D)` + `rlActiveDrawBuffers(0)`), the same recipe `core/screen_distort.c`'s `LoadRenderTextureWithDepthTexture` already uses successfully in this codebase (de-risks it against the rlvk Vulkan backend).
- Resolution: 1024² desktop, 512² `__ANDROID__`.
- Shadow multiplies **diffuse + spec only**, not ambient — shadowed areas stay lit by the hemispheric ambient instead of going pitch black.
- The pre-pass currently sweeps in whatever `DrawSandbox3D`/`GameScreen_Draw3D` draw (HP bars, decals, etc., not just meshes) since those aren't split into geometry-only vs. overlay layers — harmless (stray depth contribution), not visually wrong, but not a clean caster list either; a future pass could split that out.
- **Not verified on-device / not run through the Renderer Agent's rlvk test ladder yet** — treat as unverified until that happens (see `third_party/vulkan/CLAUDE.md`).

**Deploy recipe (3 lines, per model):**
```c
SurfaceMaterial_Init();               // once at startup, before InitSandbox/model loads
SurfaceMaterial_Apply(&model);        // once per model, right after loading
SurfaceMaterial_UpdateFrame(camera);  // once per frame, before drawing any lit model
```
Currently applied to `character/character_model.c` hero models. Map props and boss models are NOT yet wired — call `SurfaceMaterial_Apply` on them the same way to bring them into the same lighting model.

`GfxQuality_Set`/`Get` changes take effect immediately (just flips `u_qualityTier`, never reloads a shader or re-applies materials) — safe to call from a debug hotkey or options menu. In-game: **L** cycles UNLIT→LOW→MED→HIGH, current tier shown in the corner HUD text.

## 19. Visual Composition & Procedural Meshes

See **[COMPOSITION_API.md](COMPOSITION_API.md)** for the full composition reference:
`VFX_Compose*`, `VFX_GroundPattern`, `VFX_PathWave`, `VFX_TriggerExplosion`, `VC_MaterialId` table, motion library, beauty primitives, archetype group 3–5, `.inl` include order, and sync script.

> [!IMPORTANT]
> Both this file and `COMPOSITION_API.md` are mechanical references (what
> exists, what it's called, what parameters it takes) — neither teaches
> what makes a composition look good. Before authoring a new skill/VFX,
> read **[`WUXING_ART_DIRECTION.md`](WUXING_ART_DIRECTION.md)** (AI-oriented
> design rules, per-element visual language, timeline design, a cookbook of
> reusable layer recipes, and a 10-step workflow) — then use
> `COMPOSITION_API.md` §0's table to translate the cookbook pattern you pick
> into concrete function calls from this API.


## 20. Audio System (`#include "core/audio_system.h"`)

Game SFX + one music bed. Data-driven and **asset-optional**: each event
(`SfxId` / `MusicId`) maps to a fixed path under `assets/audio/`; a missing
file is silent (empty `Sound`, `IsSoundValid` gates playback), so the game
runs fully before any audio asset exists. See `assets/audio/README.md` for
the file→event table users drop assets into.

- `Audio_Init()` (after `InitWindow`, skip in headless) / `Audio_Shutdown()`
  (before `CloseWindow`) / `Audio_Update(dt)` (streams the music bed).
- `Audio_PlaySFX(id)` (2D, UI/stingers) / `Audio_PlaySFXAt(id, worldPos)`
  (3D — distance falloff to `Audio_SetListener(pos)` + stereo pan, ±6% pitch
  jitter). `Audio_CastSfxForElement(element)` maps 0..4 → `SFX_CAST_*`.
- `Audio_PlayMusic(MUS_ARENA_NIGHT)` (self-guards restart) / `Audio_StopMusic()`.
- **Layering rule**: this is a core service, but `entities/` and `combat/`
  must NOT call it (they forbid VFX/audio). Wiring lives in main.c / game/ /
  ui/, which poll those modules' events (`Combat_PeekEvents`,
  `AI_PollExplosions`, `Control_ConsumeCastFired`) and translate to Audio_*.
- Known gaps: `PlaySound` mono-voice (rapid casts cut each other off — add `LoadSoundAlias`
  pool later); online clients only hear cast (via mirror) + music, not
  host-side hit/clash/explosion yet.

---

## VFX_Sequence — the choreography layer (Đợt E / E3)

`core/composition/vfx_sequence.h`. A sequence is a **beat track**: you say what
happens and *when*, and the pool fires it. It exists because every skill used to
hand-code its own phase timing, so the
`anticipation → burst → sustain → dissipate` envelope that makes an effect
readable was accidental and drifted between skills.

### The shape of a call

```c
VFX_Sequence *s = VFX_SeqBegin(pos, VC_MAT_FIRE, 1.0f);
VFX_SeqAt(s, 0.00f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = MyWindup });
VFX_SeqAt(s, 0.20f, (VFX_Beat){ .kind = VFX_BEAT_COMPOSE, .cb = MyBoom });
VFX_SeqAt(s, 0.20f, (VFX_Beat){ .kind = VFX_BEAT_SHAKE,  .a = 0.4f });
VFX_SeqAt(s, 0.20f, (VFX_Beat){ .kind = VFX_BEAT_RADIAL, .a = 0.16f, .b = 0.45f });
VFX_SeqPlay(s);                 // returns a handle; keep it only to Stop early
```

Beats may be added in any order — they are sorted on `VFX_SeqPlay`. Adding after
Play is ignored (and warned): the track is fixed once it starts.

### A `VFX_Compose*` needs a 3-line adapter

A beat calls `fn(pos, scale, ud)`, so wrap the composition:

```c
static void MyBoom(Vector3 pos, float scale, void *ud) {
    (void)ud;
    VFX_ComposeImpact(pos, EFFECT_PRESET_FIRE_EXPLOSION, 1.2f * scale);
}
```

`scale` arrives pre-multiplied by the sequence's scale, so one sequence authored
at 1.0 scales as a whole.

### Take the envelope for free

```c
VFX_Sequence *s = VFX_SeqPreset(pos, VC_MAT_FIRE, 1.0f,
                                0.15f,   // anticipation
                                0.10f,   // burst
                                0.40f,   // sustain
                                0.50f);  // dissipate
// then add YOUR compose beats — the preset supplies light/shake/warp/smear only
```

The preset deliberately does **not** fill in hitstop (that stops the world — a
gameplay decision a preset must not impose) or COMPOSE beats (it cannot know
what your effect spawns, only when it should land).

### What goes on the track, and what must not

`VFX_BEAT_COMPOSE` and `VFX_BEAT_CALLBACK` run the same mechanism but mean
different things, and the split is load-bearing: a future quality-tier filter may
drop COMPOSE beats on a weak device, but must never drop CALLBACK — that is
where gameplay and audio hang.

**Keep damage off the track.** The `TAIJI_LOI` port (the E3 reference case) keeps
`Entity_ApplyAoEDamage` at cast time and puts only the visuals on beats. Moving
the hit onto a beat would delay it by the bolt's travel time and change gameplay
and netcode timing — a VFX change has no business doing that.

### The clock

Sequences advance on the **scaled** dt (post `TimeFX_Apply`), so a sequence that
fires its own hitstop also stretches its own remaining beats — that stretch is
the intended feel, and it keeps the sequence in sync with the particles it
spawned. `VFX_SeqSetUnscaled(s, true)` opts out, for UI flourishes and anything
that must keep wall-clock time through a hitstop.

### Pools

`VFX_SEQ_MAX = 16` sequences × `VFX_SEQ_MAX_BEATS = 24` beats, static. A full
pool recycles the oldest **playing** sequence (never one still being authored —
someone holds that pointer) and logs once. Frame spikes never drop beats: a
single long frame fires every beat it jumped over, in order.

Live demo: NEW FX tab → `SEQUENCE ENVELOPE E3`.

---

## Shared appearance across particles, trails, and decals

Use `VFXAppearanceId` when an effect wants a standard visual intent. The same
enum resolves surface/blend, contrast, body coverage, HDR emission, and lighting
for every migrated geometry provider. `VFX_APPEARANCE_INHERIT` is zero and keeps
all legacy fields exact.

```c
particle.render.appearance = VFX_APPEARANCE_GLOW;
trail.material.appearance = VFX_APPEARANCE_GLOW;
decalMaterial.appearance = VFX_APPEARANCE_GLOW;
```

Available intents:

- `NORMAL`: alpha, lit, no HDR emission.
- `SMOKE` / `DUST`: alpha body with their shared contrast profile.
- `GLOW` / `MAGIC`: unlit additive HDR emission, no body coverage.
- `FIRE`: body plus stronger HDR emission. Packed-volume particles use one
  premultiplied draw; ordinary sprites use an alpha/HDR fallback, while trails
  and decals use their existing body/emission split.

The caller still owns geometry, motion, colour, texture, and lifetime. It does
not repeat blend/bloom/contrast policy. Resolution happens once when the object
is spawned; draw loops consume already-resolved values. The GPU particle path
currently batches additive billboards only, so named alpha/premultiplied
appearances automatically use the CPU fallback rather than rendering with the
wrong blend law.

## Making a particle GLOW (manual override)

Prefer `.render.appearance = VFX_APPEARANCE_GLOW` for the standard look. The
fields below remain available when an effect deliberately needs authored values
outside the shared preset.

Four knobs, and the first two are mandatory — a boost on its own does nothing.

```c
SpawnParticle((ParticleConfig){
    .position   = p,
    .colorStart = VC_WithAlpha(mat->glow, 255),
    .render.blendMode     = VFX_BLEND_ADDITIVE, // 1. it EMITS -> additive
    .render.unlit         = 1,                  // 2. REQUIRED, see below
    .render.emissiveBoost = 4.5f,               // 3. the intensity
});
```

### 1–2. `blendMode` + `unlit` — the gate

`emissiveBoost` is applied **only to `unlit` particles**
(`particle_system.c`: `SetEmissive(p->unlit ? wantBoost : 1.0f)`). That is not a
quirk, it is the F1b blend law: smoke and dust OCCLUDE light and get lit, so
boosting them would make them emit light they are supposed to block. If you set
a boost and see nothing, check `unlit` first — that is the usual cause.

### 3. `emissiveBoost` — the intensity, and what the numbers mean

| value | result |
|---|---|
| `1.0` (default) | exactly the pre-HDR look: caps at 1.0, sits at the bloom threshold, no blow-out |
| `2–3` | visibly hotter core, light bloom |
| `4.5` | the house value — `VFX_ComposeGlintSparkle` and the GPU particle upgrades test both use it |
| `8+` | core goes fully white, strong bloom halo. Reserve for ultimates |

Why these numbers: the scene buffer is R16F, ACES maps 1.0 to ~0.83, and the
bloom threshold is `0.8`. So a particle at 1.0 sits *exactly* at the threshold —
it can never blow out. Values above 1.0 are what push into the tonemapper's
roll-off (which is what makes a core read as white-hot with a coloured rim) and
past the bloom threshold (which is what gives it a halo).

Same field, same meaning on both paths: `GpuParticleConfig.emissiveBoost` bakes
the value into its float colour, `ParticleConfig.render.emissiveBoost` rides a
shader uniform because CPU vertex colour is `rlColor4ub` — 8-bit, hard-capped at
1.0. Use the same number for either.

### 4. `emissiveCurve` — brightness over LIFETIME, not headroom

Different tool, common confusion. It is applied **CPU-side into the 8-bit colour
and clamps at 255**, so it can only push a colour *toward* white — it cannot
exceed 1.0 and cannot produce a hot core on its own. Use it for shape over time
(flare up then fade); use `emissiveBoost` for how hot the peak is. They compose.

### Global overrides

- `tuning.cfg → particle_emissive_boost` — multiplies every per-particle value
  (1.0 = respect what each call site authored). Hot-reload, for dialling a whole
  scene without editing call sites.
- `PostFXConfig.bloomThreshold` (1.25) / `.bloomIntensity` (0.12) in `main.c` —
  what counts as "bright enough to bloom" and how strong the halo is. Raising the
  boost and lowering the threshold both increase halo; prefer the boost, since
  the threshold is global and will drag every bright surface in with it.
  The shared bright prefilter gathers each quarter-resolution source cell before
  applying this threshold, so a thin HDR mesh core is eligible for bloom without
  being widened into a ribbon.
