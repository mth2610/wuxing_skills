# Shader API — GLSL Guidelines & 3D Rendering Best Practices

> Source: `core/shaders/common/` (vs_header, fs_header, lighting, noise, fx, triplanar)
> Cross-reference: [`API.md`](API.md) §10 stub

---

## 10. GLSL Shader Guidelines

### `#include` Is an Engine Preprocessing Step, Not Native GLSL

> [!IMPORTANT]
> GLSL has no native `#include` directive. The `#include "core/shaders/common/..."` lines below are resolved by **`shader_preprocessor.h/.c`** (`ShaderPreprocessor_Load()`), which is wired into `ResourceManager_LoadShader()`. It recursively reads the file, textually substitutes every `#include "..."` line with the target file's contents (up to `MAX_INCLUDE_DEPTH`), and only then hands the fully-expanded source to `LoadShaderFromMemory()`. The expansion buffer is 128 KiB so composed volume shaders can safely combine shared headers, UV helpers, and FX helpers. It is heap-allocated with `RL_MALLOC`/freed with `RL_FREE` internally — skill code never touches this buffer and never calls `ShaderPreprocessor_Load()` directly; it is invoked automatically by `ResourceManager_LoadShader()`.
>
> Practical implication: a raw `glCompileShader` call (or any tool that lints `.vs`/`.fs` files standalone, e.g. an online GLSL validator) will fail on the `#include` line because it isn't valid core GLSL — this is expected and not a project bug. Only `ResourceManager_LoadShader()` produces compilable output.

### Common Shader Files — Overview

| File | Used in | Provides |
|---|---|---|
| `vs_header.glsl` | Every `.vs` | Attributes, uniforms, varyings, `VS_FinalOutput()` |
| `fs_header.glsl` | Every `.fs` | Incoming varyings, environment uniforms, `finalColor` |
| `lighting.glsl` | `.fs` needing lighting | `perturbNormal`, one-/two-sided Fresnel, optical-depth body/rim, specular, diffuse |
| `noise.glsl` | `.vs` / `.fs` needing noise | `hash2`, `hash3`, `vnoise`, `fbm2`, `fbm2N` |
| `fx.glsl` | `.fs` needing effects | `dissolveCalc`, `flowBlend`, `emissiveMask`, edge-erosion macros |
| `triplanar.glsl` | `.fs` for meshes without stable UVs | `triplanarWeights`, `triplanarNoise`, `triplanarSample` |

**Include rules:**
- Always include in this order: `fs_header.glsl` → `noise.glsl` (if needed) → `lighting.glsl` → `fx.glsl` → `triplanar.glsl` (if needed, depends on `noise.glsl` for `triplanarNoise`)
- `fx.glsl` does not depend on `noise.glsl` — can be included alone or together
- Do not re-implement hash/noise/fbm/dissolve/flow-blend/triplanar in skill code

### Triplanar Mapping (`core/shaders/common/triplanar.glsl`)

Solves Item 4a (`PROGRESS.md`): the `ProceduralMesh_Draw*` functions (Rock, ShardCluster, Fissure, VortexFunnel) draw via `rlBegin`/`rlEnd` immediate mode — position + normal only, **no texcoord** — so UV-based texturing stretches/streaks across jagged facets. Triplanar projects a texture/pattern from 3 world-space axis planes (X/Y/Z) and blends by world normal instead of using UVs.

```glsl
vec3 triplanarWeights(vec3 worldNormal, float sharpness);              // sharpness 2.0-6.0
float triplanarNoise(vec3 worldPos, vec3 weights, float scale);        // procedural, no texture asset needed
vec4 triplanarSample(sampler2D tex, vec3 worldPos, vec3 weights, float scale); // real texture asset
```

Pattern used in `main()`:
```glsl
vec3 w = triplanarWeights(fragNormal, 4.0);
float pattern = triplanarNoise(fragPosition, w, 0.05); // or triplanarSample(myTex, fragPosition, w, 0.02)
```

> [!NOTE]
> `scale` is the world-space projection frequency (not UV [0,1]) — small values (0.01-0.05) for large meshes, larger (0.05-0.1) for small/detailed meshes. Tune by eye against the mesh's real size.

### Required Includes

Vertex Shader

```glsl
#version 330
#include "core/shaders/common/vs_header.glsl"
```

Fragment Shader — 3D mesh needing full lighting:

```glsl
#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"    // if hash/fbm needed
#include "core/shaders/common/lighting.glsl"  // normals, Fresnel, optical depth, specular, diffuse
#include "core/shaders/common/fx.glsl"        // dissolveCalc, flowBlend, emissiveMask
```

Fragment Shader — minimal (dissolve only, no 3D lighting needed):

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

#### `lighting.glsl` — 3D Lighting

```glsl
vec3  perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength);
float calcFresnel(vec3 normal, vec3 viewDir, float power);
float calcTwoSidedFresnel(vec3 normal, vec3 viewDir, float power);
float calcOpticalDepthBody(float absNdotV, float power);
float calcOpticalDepthRim(float absNdotV, float power);
float combineOpticalDepth(float body, float rim, float bodyWeight, float rimWeight);
float calcSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess);
float calcDiffuse(vec3 normal, vec3 lightDir, float ambient);
```

* **`perturbNormal(baseNormal, heightDelta, strength)`** — Perturbs a base normal using the gradient of a skill-supplied height field, to fake surface roughness (water ripples, lava bubbling, bark texture...) without extra geometry.
  - `baseNormal`: the mesh normal to perturb — typically `fragNormal` (already world-space, normalized).
  - `heightDelta`: `vec2(h(u-eps) - h(u+eps), h(v-eps) - h(v+eps))` — the **gradient** of your own height function, sampled at `±eps` around the current `fragTexCoord` in U and V respectively. The skill must implement its own height function (e.g. `tube.fs`'s `getIrregularity()`) and **must reuse the exact same formula as the vertex shader's displacement function**, or lighting and physical displacement will visually mismatch.
  - `strength`: deformation intensity, **typical range 0.3 – 0.8**.
* **`calcFresnel(normal, viewDir, power)`** — one-sided, Schlick-approximated rim term for a closed surface with outward normals and back faces culled. Returns `[0..1]` (`0` = surface viewed face-on, `1` = viewed edge-on). **Typical power: 2.0 – 5.0.**
* **`calcTwoSidedFresnel(normal, viewDir, power)`** — winding-independent rim for a genuinely two-sided sheet; uses `abs(N·V)`. It does not discard back-facing fragments.
* **`calcOpticalDepthBody(absNdotV, power)`** / **`calcOpticalDepthRim(absNdotV, power)`** — volume density terms from `abs(N·V)`: the body is strongest face-on; the rim is strongest at the silhouette. They are not interchangeable with Fresnel.
* **`combineOpticalDepth(body, rim, bodyWeight, rimWeight)`** — adds independently weighted body density and rim scattering, then clamps. Use this instead of interpolating the opposing terms.
* **`calcSpecular(normal, lightDir, viewDir, shininess)`** — Blinn-Phong specular highlight, returns `[0..1]` — caller scales by intensity (e.g. `* 5.0`). **Typical shininess: 32 – 512.**
* **`calcDiffuse(normal, lightDir, ambient)`** — Lambertian diffuse with an ambient floor, returns `[ambient..1.0]`.
  - `ambient`: minimum background light, typically `0.10 – 0.25`.
  - Multiply directly into baseColor: `baseColor *= calcDiffuse(normal, lightDir, 0.15);`

**Project-standard `lightDir`** (hard-coded in every skill):
```glsl
vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
```

---

#### `noise.glsl` — Procedural Noise

```glsl
float hash2(vec2 p);                    // 2D hash → [0, 1]
float hash3(vec3 p);                    // 3D hash → [0, 1]
float vnoise(vec2 p);                   // 2D value noise → [0, 1]  ("vnoise" avoids conflicting with GLSL's built-in noise2)
float fbm2(vec2 p);                     // 3-octave FBM → [0, ~1]
float fbm2N(vec2 p, int octaves);       // N-octave FBM, 1–6 → [0, 1] normalized
```

* **`hash2 / hash3`** — Pseudo-random hash. `hash3` is used for world-space dissolve: `hash3(floor(fragPosition * scale))`.
* **`vnoise`** — Value noise, faster than Perlin. Used as a base for FBM or direct UV warp. (Not named `noise2` — conflicts with a GLSL built-in.)
* **`fbm2`** — 3-octave FBM, used in most VFX (fire, plasma, wave patterns). Has built-in rotation to avoid axis-aligned artifacts.
* **`fbm2N`** — For finer control: 1–2 octaves for soft wind/halo effects, 5–6 for bark/rock texture.

```glsl
// Example: UV warp driven by FBM to make wind ripple/twist
vec2 flow = vec2(u_time * 0.4, -u_time * 0.6);
float distort = fbm2(vec2(localU, localV) * 8.0 + flow);
vec2 warpedUV = uv + (distort - 0.5) * 0.008;
```

---

#### `fx.glsl` — VFX Effects

```glsl
float dissolveCalc(float noiseVal, float dissolve, float edgeWidth, out float edgeFactor);
float flowBlend(sampler2D tex, vec2 uv, vec2 flowDir, float speed, float strength, float time);
float emissiveMask(vec3 worldPos, float freq, float threshold);
EDGE_EROSION_MASK(noiseVal, edgeWeight, strength);
EDGE_EROSION_THRESHOLD_JITTER(noiseVal, edgeWeight, strength);
```

* **`dissolveCalc`** — Noise-based dissolve + glowing burn edge. Returns `1.0` if the pixel should be discarded, `0.0` if it stays. `edgeFactor` (out) is how much edge to mix in the element color.
  ```glsl
  // Standard pattern — include noise.glsl first:
  float n = hash3(floor(fragPosition * 10.0));
  float edgeFactor;
  if (dissolveCalc(n, u_dissolve, 0.08, edgeFactor) >= 1.0) discard;
  baseColor = mix(baseColor, vec3(1.0, 0.5, 0.1), edgeFactor); // fire edge example
  ```

* **`flowBlend`** — 2-phase flow-map blend that avoids stutter (no seam on phase reset). Returns the blended texture's `float` luminance.
  ```glsl
  vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
  float intensity = flowBlend(causticsTex, fragTexCoord * 2.0, flowDir, 1.2, 0.05, u_time);
  baseColor += waterColor * intensity * 1.5;
  ```

* **`emissiveMask`** — Sine-based emissive derived from world position — not warped by UV. Used for tree sap, energy veins, glowing cracks.
  ```glsl
  float mask = emissiveMask(fragPosition, 1.5, 0.88);
  baseColor += elementGlowColor * mask * 2.5;
  ```

* **`EDGE_EROSION_MASK`** — Shared expression macro for silhouette erosion. It expands to the original smoke-tube arithmetic, including the fixed `0.15` mask-space transition and the final strength blend.
  ```glsl
  coverage *= EDGE_EROSION_MASK(noiseValue, silhouetteWeight, erosionStrength);
  ```

* **`EDGE_EROSION_THRESHOLD_JITTER`** — Shared expression macro for dissolve effects that own a threshold.
  ```glsl
  float threshold = dissolve + EDGE_EROSION_THRESHOLD_JITTER(noiseValue, silhouetteWeight, tearStrength);
  ```

Do not reimplement these functions.

> [!NOTE] **Resolved (PROGRESS.md Item 10).** `u_lightDir` is now a real
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

The Android build runs on OpenGL ES. The pipeline uses **two paths** depending on whether a shader uses `#include` or not.

#### Path 1 — Standalone shaders (no `#include`)

`scripts/convert_shaders_to_gles.py` runs at APK build time, converting to **GLES 1.00 (`#version 100`)**:

| Desktop GLSL 3.3 | GLES 1.00 (after build script) |
|---|---|
| `in vec3 pos` (VS) | `attribute vec3 pos` |
| `out vec3 vary` (VS) | `varying vec3 vary` |
| `in vec3 vary` (FS) | `varying vec3 vary` |
| `out vec4 finalColor` + every use of `finalColor` | declaration removed + replaced with `gl_FragColor` |
| `texture(sampler, uv)` | `texture2D(sampler, uv)` |
| precision (FS) | auto-injects `precision highp float;` if missing |

The build script does **NOT** auto-fix: the `f` suffix on float literals, precision for `.vs`, or `#include` contents.

> Requires GLES 2.0+ (Android 2.2+, all target devices).

#### Path 2 — Shaders using `#include` common headers

The build script **SKIPS** these — they keep `#version 330` unchanged in the APK.

At runtime, `ResourceManager_LoadShader` → `ShaderPreprocessor_Load`:
1. Recursively expands every `#include "..."` (e.g. `vs_header.glsl`, `lighting.glsl`)
2. `RewriteVersionForGLES()` changes `#version 330` → `#version 300 es`
3. Result: GLES 3.0 source with `in`/`out`/`texture()` — valid

The common headers (`vs_header.glsl`, `fs_header.glsl`, `lighting.glsl`, `noise.glsl`, `fx.glsl`) **already have** `#ifdef GL_ES precision highp float; #endif` — no need to declare it again in skill shaders. Both VS and FS use `highp float` (important — see Rule E).

> Requires GLES 3.0+ (Android 4.3+, all modern devices).

---

**Rule A — Do not use the `f` suffix on float literals (applies to BOTH paths):**

```glsl
// WRONG — Android GLES compiler rejects this, and the build script does NOT auto-fix it:
float breathe = 1.25f + 0.12f * sin(u_time * 5.5);

// CORRECT:
float breathe = 1.25 + 0.12 * sin(u_time * 5.5);
```

The `f` suffix is C syntax. The desktop driver ignores it; Android's strict GLES compiler rejects it → `shader.id = 0`.

**Rule B — A standalone VS must declare precision itself:**

The build script auto-injects precision for standalone `.fs` files, but **not** for `.vs`. Every standalone vertex shader (without `#include "core/shaders/common/vs_header.glsl"`) must add:

```glsl
#version 330

#ifdef GL_ES
precision highp float;
#endif
```

Shaders using the common headers already get precision from `vs_header.glsl`/`fs_header.glsl` — no need to redeclare it.

**Rule C — Behavior when shader compilation fails on Android:**

`ResourceManager_LoadShader` does **not crash** on a shader compile failure — it returns `shader.id = 0` and logs:
```
SHADER: compile failed, not caching (vs=... fs=...)
```
`SkillManager_BeginShader` guards on `id == 0` → no-op (skips `BeginShaderMode`). The skill still runs but renders with the default flat shader → the mesh looks **fully white / no effect**.

When a skill renders fully white on Android: check the logcat line above, fix per Rule A/B, rebuild the APK.

**Rule D — `matModel` must be set manually when using rlgl immediate mode:**

`VS_FinalOutput()` in `vs_header.glsl` computes `fragNormal = normalize(matModel * vertexNormal)`. Raylib only uploads `matModel` when using `DrawMesh`/`DrawModel` — it does **not** upload it when using rlgl immediate mode (`rlBegin`/`rlEnd`/`ProceduralMesh_DrawTube`...).

On Android GLES 3.0, `matModel` holds **all-zeros** → `normalize(vec3(0,0,0))` is undefined (NaN on Adreno/Mali) → `fragNormal = NaN` → `clamp(NaN, 0, 1) = 1.0` → white color. On Mac desktop, the OpenGL driver handles `normalize(zero)` differently (returns something identity-ish), so the bug doesn't show up there.

**`SkillManager_BeginShader` automatically sets `matModel = identity` before `BeginShaderMode`.** Skill code doesn't need to do anything extra if it uses `SkillManager_BeginShader`.

> [!IMPORTANT] **Bug fixed (2026-06-30):** the previous fix used `shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0` to check whether the location was valid — this check is WRONG. Raylib's `LoadShaderFromMemory` only auto-binds a fixed default set of uniforms (`mvp`, `colDiffuse`, `texture0`, vertex attribs...); `matModel` is not in that set, so `shader.locs[SHADER_LOC_MATRIX_MODEL]` is never written and keeps its initial `0` value from `RL_CALLOC`. `0` still passes `>= 0` even though it is **not `matModel`'s real location** → `SetShaderValueMatrix` mistakenly overwrites whatever other uniform actually sits at location 0 in the linked GLSL program (e.g. a separately declared `sampler2D texture0`) → breaks that uniform's texture binding/value → the mesh can render fully white even though the shape is still correct. Confirmed via `tsunami_skill` (FlowMap's `texture0` got overwritten by the identity matrix). `core/skill_manager.c`'s `SkillManager_BeginShader` was fixed to use `GetShaderLocation(shader, "matModel")` (looked up by name, returns a real `-1` if it doesn't exist) instead of reading `shader.locs[SHADER_LOC_MATRIX_MODEL]`.

If a skill calls `BeginShaderMode` directly (bypassing `SkillManager_BeginShader`), it MUST set matModel manually — and MUST look up the location by name, not via `shader.locs[SHADER_LOC_MATRIX_MODEL]`:

```c
// Before the draw call, after BeginShaderMode():
int matModelLoc = GetShaderLocation(s_shader, "matModel");
if (matModelLoc >= 0) {
    Matrix identity = MatrixIdentity();
    SetShaderValueMatrix(s_shader, matModelLoc, identity);
}
```

**Rule E — VS and FS must use the same precision for every shared uniform (GLES 3.x strict):**

On strict GLES 3.x implementations (Mali-G68, GLES 3.2), if a uniform appears in both VS and FS, both must have the **same precision qualifier**. A mismatch → link failure → `shader.id = 0` → white color.

```
// Typical logcat error:
// SHADER: [ID 14] Link error: L0001 The fragment floating-point variable u_time
//         does not match the vertex variable u_time. The precision does not match.
```

The common headers already handle this: both `vs_header.glsl` and `fs_header.glsl` use `precision highp float` — so `u_time`, `viewPos`, `u_resolution`, and every default-declared uniform are `highp` in both stages.

If a skill declares its own uniform (e.g. `uniform float u_uvLength;`) in both `.vs` and `.fs`, that uniform inherits the default precision — `highp` from `vs_header.glsl` for the VS and `highp` from `fs_header.glsl` for the FS → matching, no problem.

If a skill declares a lower default precision (e.g. `precision mediump float;`) in a standalone FS, it must ensure the VS also uses `mediump` — or, better, use `highp` consistently in both.

> [!NOTE]
> The desktop OpenGL driver usually compiles successfully even with the `f` suffix or missing precision, and handles `normalize(zero)` differently than mobile — the error usually only shows up when testing on a real Android device (GLES strict mode).

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

### 11.4 Core Custom VFX Shaders

The core engine provides specialized, high-performance standalone custom shaders under `core/shaders/` to render procedurally-detailed billboards and meshes:

- **`magic_filaments.fs`** (Magical Sparkling Filaments Shader):
  - **Inputs/Uniforms**: `u_color` (vec4), `u_progress` (float), `u_diffusion` (float), `u_noiseScale` (float), `u_driftSpeed` (float), `u_sourcePos` (vec2).
  - **Technique**: Uses a 1-octave value noise domain-warp for coordinate bending, a highly optimized 2-octave ridged FBM (`ridgedFBM2`) to isolate thin glowing fibers, an expanding shell-fresnel rim, and high-frequency time-varying value noise sparkle peaks.
  - **Performance Profile**: Optimized to use exactly 5 texture noise fetches per pixel (representing a 50% GPU fill rate footprint reduction).
- **`energy_smoke.fs`** (Gaseous Energy Smoke Shader):
  - **Inputs/Uniforms**: `u_color` (vec4), `u_progress` (float), `u_diffusion` (float), `u_noiseScale` (float), `u_driftSpeed` (float), `u_sourcePos` (vec2).
  - **Technique**: Simulates point-source gas expansion using a closed-form analytic diffusion solution:
    $$C(r,t) = \frac{C_0 \cdot e^{-\frac{r^2}{4Dt}}}{4\pi Dt}$$
    This models expanding, fading Gaussian profiles with built-in domain warping using 2D value noise.
  - **Performance Profile**: Uses 2D value noise instead of 3D FBM, reducing mathematically heavy hash overhead by 86% per pixel.
- **`smoke_column.fs`** (Continuous Smoke Column Shader):
  - **Technique**: Decodes position seeds packed into the model's normal vector coordinates:
    $$\text{seedVal} = u\_seed + \frac{\text{fragNormal.y}}{\|\text{fragNormal.xz}\|} \times 100.0$$
    This decodes CPU-generated random offsets on the GPU to generate continuous, un-pixelated noise fields across billboard planes with **zero batch flushes**.

---

---
