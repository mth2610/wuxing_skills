# Real Shading — Implementation Spec (P1–P6)

Companion to `REAL_SHADING_PLAN.md`. Per-phase, implement-ready: exact files, signatures, uniforms,
shader insert points, and acceptance checks. Grounded in the current code:
`core/surface_material.{h,c}`, `core/shaders/surface_lit.{vs,fs}`, `environment/environment_system.h`.

**Golden rules for every phase**
- One shader (`surface_lit`), gated by `u_qualityTier` (int: 0 UNLIT / 1 LOW / 2 MED / 3 HIGH) +
  per-material feature flags (`u_hasMatcap`, `u_hasNormalMap`, ...). No shader-variant matrix unless
  §Perf forces it.
- Fully runtime-switchable: changing quality flips a uniform, never reloads a shader or re-applies
  materials.
- Every shader edit is verified **on real Mali via rlvk** before "done" (see §Perf/rlvk). Desktop
  green ≠ device correct.
- Keep writing to the **HDR float buffer** — never clamp; spec/rim/emissive must be free to exceed 1.

---

## P0 — Shared foundation: the quality toggle (Core Agent) — do first, tiny

**New files:** `core/gfx_quality.h`, `core/gfx_quality.c`.

```c
// gfx_quality.h
typedef enum { GFX_UNLIT = 0, GFX_LOW = 1, GFX_MED = 2, GFX_HIGH = 3 } GfxQuality;
void       GfxQuality_Set(GfxQuality q);   // runtime switch (sandbox UI / options menu)
GfxQuality GfxQuality_Get(void);           // read at material bind + anywhere else
GfxQuality GfxQuality_Default(void);       // platform default (see below)
```

```c
// gfx_quality.c
static GfxQuality s_q = GFX_MED;
void       GfxQuality_Set(GfxQuality q) { s_q = q; }
GfxQuality GfxQuality_Get(void)         { return s_q; }
GfxQuality GfxQuality_Default(void) {
#if defined(__ANDROID__)
    return GFX_MED;      // A33/Mali class; drop to GFX_LOW if perf demands
#else
    return GFX_HIGH;     // desktop / Vulkan-Mac
#endif
}
```
`main.c` calls `GfxQuality_Set(GfxQuality_Default())` once at startup, after window init, before the
first frame. Add `core/gfx_quality.c` to the build (CMake + `Makefile.Android` `PROJECT_SOURCE_FILES`
— the Android list is separate, don't forget it, see `RLVK_HANDOFF.md` §7.24).

**Acceptance:** builds on desktop + Android; `GfxQuality_Get()` returns the platform default.

---

## P1 — LOW-tier look: tier branch + hemispheric ambient + emissive (Core + Environment)

Delivers the "easy ON/OFF for weak machines" ask AND visibly improves every device (LOW items are
~free ALU). Rim + half-Lambert are already in `surface_lit.fs`; this phase adds the tier gate,
hemispheric ambient, and emissive plumbing.

### P1a — push the tier uniform (Core, `surface_material.c` + `surface_lit.fs`)
- `surface_lit.fs`: add `uniform int u_qualityTier;`
- `surface_material.c`:
  - add `static int s_locQualityTier;` and in `Init`: `s_locQualityTier = GetShaderLocation(s_shader, "u_qualityTier");`
  - in `UpdateFrame`, push it: `int tier = (int)GfxQuality_Get(); SetShaderValue(s_shader, s_locQualityTier, &tier, SHADER_UNIFORM_INT);`
  - `#include "core/gfx_quality.h"` at top.

### P1b — restructure `surface_lit.fs` main() around tiers
Replace the flat body with the gated version (unlit early-out, then LOW base, then MED/HIGH adds):
```glsl
void main() {
    vec4 tex = texture(texture0, fragTexCoord);
    vec3 albedo = tex.rgb * colDiffuse.rgb * fragColor.rgb;
    float alpha = tex.a * colDiffuse.a;

    if (u_qualityTier == 0) { finalColor = vec4(albedo, alpha); return; } // UNLIT: cheap passthrough

    vec3 N = normalize(fragNormal);
    // (HIGH normal-map perturbation of N is inserted here in P5, gated by u_hasNormalMap)
    vec3 L = normalize(u_sunToLight);
    vec3 V = normalize(u_viewPos - fragWorldPos);
    vec3 H = normalize(L + V);
    float ndl = dot(N, L);

    // --- LOW base (all tiers >= 1) ---
    float wrap = ndl * 0.5 + 0.5; wrap *= wrap;           // half-Lambert
    vec3 diffuse = albedo * u_sunColor * wrap;
    float hemi = N.y * 0.5 + 0.5;                          // hemispheric ambient (P1c)
    vec3 ambient = albedo * mix(u_groundColor, u_skyColor, hemi);
    float fres = pow(1.0 - max(dot(N, V), 0.0), u_rimPower);
    vec3 rim = u_rimColor * (fres * u_rimStrength);        // MED adds directional tint (P3)
    vec3 emissive = u_emissiveColor;                      // P1d; + map sample when u_hasEmission
    vec3 color = ambient + diffuse + rim + emissive;

    // --- MED adds (tier >= 2): spec sheen, matcap, directional rim --- (P3)
    // --- HIGH adds (tier >= 3): aniso sheen, fake SSS --- (P5)

    if (u_fogEnabled > 0.5) { /* existing distance fog, unchanged */ }
    finalColor = vec4(color, alpha);
}
```
Keep the existing fog block verbatim (deferred, not gated on a tier).

### P1c — hemispheric ambient (Environment + Core)
- `surface_lit.fs`: `uniform vec3 u_skyColor; uniform vec3 u_groundColor;` (used above). Remove the
  old flat `u_ambientColor` use (keep the uniform for back-compat or delete + rewire).
- `environment_system.h`: add getters
  ```c
  Color Environment_GetSkyAmbient(void);    // upper-hemisphere tint
  Color Environment_GetGroundAmbient(void); // lower-hemisphere bounce
  ```
  Minimal impl (no preset-struct change needed yet): derive from the existing ambient —
  `sky = ambient * 1.25` (clamped), `ground = ambient * 0.5` with a slight warm/cool shift. Better
  later: add `skyAmbient`/`groundAmbient` to `EnvLightingPreset` so time-of-day drives them.
- `surface_material.c` `UpdateFrame`: push both (replace the single ambient push):
  ```c
  Vector3 sky = ColorToVec3(Environment_GetSkyAmbient());
  Vector3 grd = ColorToVec3(Environment_GetGroundAmbient());
  SetShaderValue(s_shader, s_locSky,    &sky, SHADER_UNIFORM_VEC3);
  SetShaderValue(s_shader, s_locGround, &grd, SHADER_UNIFORM_VEC3);
  ```
  (add `s_locSky`, `s_locGround` locations in `Init`).

### P1d — emissive plumbing (Core)
- `surface_lit.fs`: `uniform vec3 u_emissiveColor; // default (0,0,0)`. Add its value into `color`
  (done above). Optional map: `uniform sampler2D emissionMap; uniform float u_hasEmission;` and
  `emissive += texture(emissionMap, fragTexCoord).rgb * u_hasEmission;` — wire
  `s_shader.locs[SHADER_LOC_MAP_EMISSION]` so raylib binds `MATERIAL_MAP_EMISSION` when a model has
  one. For P1, uniform-only is fine (default 0 → no visual change until content sets it).
- `surface_material.c`: default-push `u_emissiveColor = 0` in `Init`; expose a setter later if
  per-model emissive tint is needed.

**Acceptance (P1):** flip `GfxQuality_Set(GFX_UNLIT/LOW/MED/HIGH)` at runtime → UNLIT shows flat
albedo, LOW+ shows moonlit shading with hemispheric ambient (dark scenes read better, no flat-black
undersides). On-device Mali: no precision artifacts, no perf cliff between UNLIT↔LOW. Verify with the
P2 sandbox toggle.

---

## P2 — Sandbox live toggle + lit test scene (Sandbox Agent)

**Files:** `sandbox/ui_panel.c` (or `sandbox_core.c`), reusing the existing panel.
- Add a 4-state control (buttons or a cycler) that calls `GfxQuality_Set(GFX_UNLIT..HIGH)` and shows
  the current tier. Place it OUTSIDE the top-84px gesture zone (memory `project_mali_device_landmines`).
- Add/point a test scene that draws a `SurfaceMaterial`-applied model (the existing CharacterModel is
  enough) under the arena's environment lighting, plus a couple of props with different materials
  (one plain, one with a matcap once P3 lands, one with an emission map) so every tier is visible on
  real content.
- Optional: a key/adb hotkey to cycle tiers for fast A/B while screenshotting.

**Acceptance:** can switch all four tiers live on device and see each technique appear/disappear.

---

## P3 — MED materials: spec sheen (present) + matcap + directional rim (Core)

All inside `surface_lit.fs`, gated `if (u_qualityTier >= 2)`.

### P3a — Blinn spec sheen (already implemented) — move it under the MED gate
```glsl
if (u_qualityTier >= 2) {
    float spec = pow(max(dot(N, H), 0.0), u_shininess) * u_specStrength;
    spec *= smoothstep(0.0, 0.15, ndl);
    color += u_sunColor * spec;
}
```

### P3b — directional-moon rim tint (upgrade the LOW rim when MED+)
Multiply the rim by moon-facing so it favours the back-lit silhouette:
```glsl
if (u_qualityTier >= 2) {
    float moonFacing = smoothstep(-0.2, 0.6, dot(N, -L));
    rim *= moonFacing;   // rim was computed in the LOW block; re-scale here before it enters color
}
```
(Reorder so `rim` is scaled before being added to `color`, or fold the whole rim into the tier
branch.)

### P3c — matcap material path (Core) — the big cheap win
- **VS** (`surface_lit.vs`): output a view-space normal. Add `uniform mat4 matView;` and
  `out vec3 fragViewNormal;` with `fragViewNormal = normalize(mat3(matView) * fragNormal);`
  In `surface_material.c` `Init`, wire it so raylib auto-updates it:
  `s_shader.locs[SHADER_LOC_MATRIX_VIEW] = GetShaderLocation(s_shader, "matView");`
- **FS**: `uniform sampler2D matcapTex; uniform float u_hasMatcap; uniform float u_matcapAmount; in vec3 fragViewNormal;`
  ```glsl
  if (u_qualityTier >= 2 && u_hasMatcap > 0.5) {
      vec2 muv = normalize(fragViewNormal).xy * 0.5 + 0.5;
      vec3 matcap = texture(matcapTex, muv).rgb;
      color = mix(color, color * matcap * 2.0, u_matcapAmount); // multiply=material; swap to += for metal/energy sheen
  }
  ```
- **Per-material binding:** matcap is per-MATERIAL, not global. Provide a small setter so a specific
  model/material opts in:
  `void SurfaceMaterial_SetMatcap(Model *model, int materialIndex, Texture2D matcap, float amount);`
  which binds the texture to a spare map slot and sets `u_hasMatcap=1`, `u_matcapAmount=amount` for
  that draw. (Because raylib binds one shader instance's uniforms globally per-frame, either (a) draw
  matcap models in their own pass setting/clearing `u_hasMatcap`, or (b) give matcap materials their
  own Shader instance cloned from surface_lit. Recommend (b): a `SurfaceMaterial_GetMatcapVariant()`
  that shares the program but carries per-material sampler bindings — simplest correctness on rlvk's
  pool-ring path.)
- Provide 2–3 authored matcap textures in `assets/` (jade, polished metal, energy) — Art task.

**Acceptance:** a weapon/jade prop with a matcap reads as "expensive" metal/jade at MED with no
measurable Mali cost; toggling `u_hasMatcap` off returns it to plain sheen.

---

## P4 — Wider deployment (Map, Boss, Character, docs)

`SurfaceMaterial_Apply(&model)` is the deploy primitive — call it once after each model loads.
- **Map Agent:** after loading static map/prop meshes in `maps/*`, call `SurfaceMaterial_Apply` on
  them so the whole scene shares one lighting model. Ensure `SurfaceMaterial_UpdateFrame(camera)` is
  called once per frame in the 3D pass (likely already for characters — confirm it covers map draws).
- **Boss Agent:** in each boss `*_def.c`, `SurfaceMaterial_Apply(&bossModel)` after load (mirror
  CharacterModel). Boss VFX are unaffected (they use their own materials).
- **Character Agent:** already applies it; for P5 ensure exported models carry tangents + normal maps.
- **Docs:** add a short `SURFACE_MATERIAL.md` (or a section in `ENVIRONMENT_API.md`) with the 3-line
  recipe: `SurfaceMaterial_Init()` once at startup → `SurfaceMaterial_Apply(&model)` per model →
  `SurfaceMaterial_UpdateFrame(camera)` per frame before drawing lit models. Note the matcap opt-in
  and the quality toggle.

**Acceptance:** characters, bosses, and map props are all lit by the same sun/ambient and respond to
the quality toggle together; no model is left on the raylib default unless intentionally UNLIT.

---

## P5 — HIGH upgrades: normal map + anisotropic sheen + fake SSS (Core + Character)

All gated `if (u_qualityTier >= 3)`; each also has a per-material/uniform flag so it's free when
absent.

### P5a — normal mapping (Core + Character)
- **Mesh requirement:** models need tangents. Character/Map export must include them, or call
  `GenMeshTangents` on load. Character Agent owns making sure hero models carry a `MATERIAL_MAP_NORMAL`
  texture + tangents.
- **VS:** `in vec4 vertexTangent; out mat3 fragTBN;`
  ```glsl
  vec3 T = normalize(vec3(matModel * vec4(vertexTangent.xyz, 0.0)));
  vec3 Nw = fragNormal; // world normal already computed
  T = normalize(T - dot(T, Nw) * Nw);          // Gram-Schmidt
  vec3 B = cross(Nw, T) * vertexTangent.w;
  fragTBN = mat3(T, B, Nw);
  ```
  Wire `s_shader.locs[SHADER_LOC_VERTEX_TANGENT] = GetShaderLocation(s_shader, "vertexTangent");`
- **FS:** `uniform sampler2D normalMap; uniform float u_hasNormalMap; in mat3 fragTBN;`
  Insert at the "normal-map perturbation" point (right after `vec3 N = normalize(fragNormal);`):
  ```glsl
  if (u_qualityTier >= 3 && u_hasNormalMap > 0.5) {
      vec3 nTex = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
      N = normalize(fragTBN * nTex);
  }
  ```
  Wire `s_shader.locs[SHADER_LOC_MAP_NORMAL]` so raylib binds `MATERIAL_MAP_NORMAL`; set
  `u_hasNormalMap=1` per material that has one.

### P5b — anisotropic sheen (Core, optional) — hair/silk
Replace/augment the MED Blinn spec for materials flagged anisotropic (`u_aniso > 0.5`), using the
tangent:
```glsl
if (u_qualityTier >= 3 && u_aniso > 0.5) {
    vec3 Tw = fragTBN[0];
    float ToH = dot(Tw, H);
    float a = sqrt(max(0.0, 1.0 - ToH * ToH));
    color += u_sunColor * pow(a, u_anisoShininess) * u_specStrength;
}
```

### P5c — fake jade/skin SSS (Core) — back-scatter
```glsl
if (u_qualityTier >= 3 && u_sssStrength > 0.0) {
    float back = pow(max(dot(V, -L), 0.0), u_sssPower) * u_sssStrength;
    color += albedo * u_sunColor * back;
}
```
Per-material `u_sssStrength`/`u_sssPower` (default 0 → off). Use for jade artifacts, skin, thin robes.

**Acceptance:** on HIGH desktop, a normal-mapped hero shows surface detail; toggling to MED drops it
with a clear perf gain; hair/jade materials read distinctly. Verify HIGH shader still compiles + runs
on strong Mali (it's the tier with the most texture/TBN cost).

---

## P6 — Real directional shadow map (Environment Agent) — heavy, last, optional

Only after P1–P5 look good and there's perf headroom. Fake blob shadows stay default until then.
- **Owner:** Environment Agent (owns shadow/lighting). New `environment/env_shadow.{c}` or extend the
  existing shadow file.
- **Approach (single directional, no cascades):**
  1. Depth-only pass from the sun's POV into a `RenderTexture2D` depth target (e.g. 1024² desktop /
     512² Mali), covering the arena bounds with an orthographic light matrix.
  2. Draw the same lit models (reuse a depth-only shader) into it once per frame.
  3. `surface_lit.fs` (HIGH+Shadow only): `uniform sampler2D shadowMap; uniform mat4 u_lightVP; uniform float u_shadowEnabled;`
     sample with a 3×3 PCF + slope bias; multiply `diffuse`/`spec` (not ambient) by the shadow factor.
- **Toggle:** a 5th quality state `+Shadow` on top of HIGH (or a separate `GfxShadows_Set(bool)`), so
  it's independently switchable and OFF on Mali by default.
- **Mali-cheaper alternative to try FIRST:** short-range **screen-space contact shadows** (ray-march
  a few steps in the existing depth buffer) — grounds feet/contact without a second pass. Decide
  after profiling.

**Acceptance:** on desktop HIGH+Shadow, characters cast a soft grounded shadow that tracks the sun;
disabling it (or dropping below HIGH) reverts to fake blob shadows with no artifacts. Do NOT ship it
enabled on Mali until profiled.

---

## Perf / rlvk verification (Renderer agent — every phase touching `surface_lit.*`)
Run after ANY shader edit, before calling a phase done:
1. `./scripts/check_rlvk_compile.sh` — the shader itself isn't here, but keep the backend green.
2. Build + run on **real Mali** (`make -f Makefile.Android USE_VULKAN=1`), toggle every tier, watch
   `adb logcat` for `RLVK: ... pipeline ... SKIPPED` (a failed pipeline → invisible draw) and for the
   custom-shader compile line. **Invisible geometry = pipeline skipped or a Mali precision death; a
   WHITE model = shader fell back to default** (memory `project_mali_device_landmines`).
3. Avoid `sin`/`fract`-of-large-value math in any new shader code (the sin-hash landmine). Match
   precision, `f`-suffix literals, don't assume `matModel` identity (memory `android_shader_pipeline`).
4. New per-material samplers (matcap, normal, emission) must bind correctly on the **pool-ring
   descriptor path** (Mali has no push descriptor) — re-check texture bindings on device, not just
   desktop/MoltenVK.

## Uniform reference (final `surface_lit` set)
| Uniform | Type | Source | Tier |
|---|---|---|---|
| `u_qualityTier` | int | `GfxQuality_Get()` | all |
| `u_sunToLight`,`u_sunColor` | vec3 | Environment sun | LOW+ |
| `u_skyColor`,`u_groundColor` | vec3 | Environment hemispheric ambient | LOW+ |
| `u_viewPos` | vec3 | camera | LOW+ |
| `u_rimColor`,`u_rimPower`,`u_rimStrength` | vec3/float | material const | LOW+ |
| `u_emissiveColor` (+`emissionMap`,`u_hasEmission`) | vec3/tex | material | LOW+ |
| `u_specStrength`,`u_shininess` | float | material const | MED+ |
| `matcapTex`,`u_hasMatcap`,`u_matcapAmount` | tex/float | per-material | MED+ |
| `matView` (→`fragViewNormal`) | mat4 | raylib auto | MED+ (matcap) |
| `normalMap`,`u_hasNormalMap`,`vertexTangent`,`fragTBN` | tex/float/attr | per-material | HIGH |
| `u_aniso`,`u_anisoShininess` | float | per-material | HIGH |
| `u_sssStrength`,`u_sssPower` | float | per-material | HIGH |
| `shadowMap`,`u_lightVP`,`u_shadowEnabled` | tex/mat4/float | Environment | HIGH+Shadow |
| `u_fogColor/Start/End/Enabled` | vec3/float | Environment (existing, unchanged) | LOW+ |
