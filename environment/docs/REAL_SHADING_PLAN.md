# Real Shading Plan — toggleable stylized-realism scene lighting

> Goal (user, 2026-07-19): "real shading, but not too heavy, easy to turn ON/OFF for weak
> machines, and easy to deploy across the project." Vulkan (rlvk) now runs on Mac + Android/Mali.
> This is a **Core / Environment / Character / Map / Boss / Sandbox** effort. The **Renderer
> (rlvk) agent owns none of the shading design** — its only stake is "the shader variants compile
> and run correctly on Vulkan/Mali" (see §7). Assign the phases below to the owning agents.

## 0. Where we already are (do NOT rebuild this)
G2 already landed a real-shading foundation — build ON it:
- `core/surface_material.{h,c}` + `core/shaders/surface_lit.{vs,fs}` — a shared forward-lit
  material: **half-Lambert diffuse + Blinn "moonlight" sheen + cool Fresnel rim + distance fog**,
  driven by `environment_system`'s sun/ambient/fog, writing into the **HDR** scene buffer (bloom
  picks up spec/rim). Deploy is already one call: `SurfaceMaterial_Apply(&model)` after load +
  `SurfaceMaterial_UpdateFrame(camera)` once per frame.
- Applied to characters via `CharacterModel_Load` → `SurfaceMaterial_Apply`. (The old memory note
  "characters are unlit mannequins" is STALE — G2 replaced that.)
- `Environment_GetSunDirection/GetSunColor/GetAmbientColor` already expose the light for the
  material to consume.

**What's missing for the user's ask:** (1) a quality toggle / tiers, (2) conservative mobile
defaults, (3) wider deployment (props, map, bosses), (4) a few *optional* "more real" upgrades that
must stay behind the toggle so weak devices never pay for them.

## 1. Design decisions (agree these first)
1. **Stylized-realism, single forward directional light — NOT full PBR, NOT deferred.** Matches the
   Moonlight-Blade art pivot and stays cheap. One sun + ambient + fog is enough; metal/rough PBR
   textures are a non-goal.
2. **Forward, single pass, HDR.** Keep the current pipeline. No G-buffer, no light loop.
3. **Fake shadows stay the default** (`Environment_DrawSmartShadow`). A real shadow map is a
   HIGH-tier-only, later, optional add (§4 HIGH) — not required for "real shading".
4. **Quality is a single global switch** with a small number of tiers, read where the material
   binds. Weak devices pick a low tier (or OFF) and pay nothing for the features they skip.

## 2. The toggle — the core of "easy ON/OFF" (Core Agent owns)
Add one global setting (new tiny `core/gfx_quality.{h,c}`, or fold into an existing settings spot):
```c
typedef enum { GFX_UNLIT = 0, GFX_LOW = 1, GFX_MED = 2, GFX_HIGH = 3 } GfxQuality;
void       GfxQuality_Set(GfxQuality q);   // runtime switch (sandbox UI, options menu)
GfxQuality GfxQuality_Get(void);
```
Wire it two ways (use both):
- **OFF (`GFX_UNLIT`)** → `SurfaceMaterial_Apply` becomes a no-op / leaves raylib's default unlit
  shader. Weakest devices skip the lit shader entirely (zero added cost, mannequin look).
- **LOW/MED/HIGH** → the lit shader runs, gated by a `uniform int u_qualityTier` (cheap runtime
  branches) so ONE shader covers all three — no shader-variant matrix to manage. If profiling later
  shows the branches cost too much on the weakest Mali, split HIGH-only features into a
  `#define`-guarded variant then; start with runtime branches for simplicity.

**Defaults:** desktop/Vulkan-Mac → `GFX_HIGH`; Android/Mali → `GFX_MED` (or `GFX_LOW` on the A33
class), chosen at startup. Keep it overridable from the sandbox + a real options menu later.

## 3. Per-frame plumbing (Core + Environment)
`SurfaceMaterial_UpdateFrame` already pushes sun/ambient/fog/viewPos — extend it to also push
`u_qualityTier` (from `GfxQuality_Get`) and any HIGH-tier uniforms (normal-map presence, shadow
matrix/texture). Environment Agent keeps the sun/ambient/fog getters authoritative and drives them
from time-of-day.

## 4. Quality tiers (what each includes — Core Agent implements in `surface_lit.fs`)
Every ✅/⚠️ technique from the beauty-vs-cost review is in scope. The three dropped outright
(user 2026-07-19): **SSAO, full PBR metal/rough+IBL, deferred/multi-light** (see §8). **Fog is
deferred** — keep the *existing* cheap distance fog as-is; the height-fog+tint upgrade is postponed
(hard, low priority), so it's not a tier gate below.

| Tier | Technique set | Target | Added cost |
|---|---|---|---|
| **UNLIT** | raylib default shader (fallback) | weakest / off | 0 |
| **LOW** | half-Lambert diffuse **+ hemispheric ambient + emissive + Fresnel rim** | weak Mali | ~free (all cheap ALU) |
| **MED** *(default)* | LOW **+ Blinn spec sheen + matcap materials + directional-moon rim tint** + existing distance fog | mid Mali | a few `pow`/1 tex |
| **HIGH** | MED **+ normal mapping + anisotropic sheen + fake jade/skin SSS** | desktop / strong Mali | +TBN + 1–2 tex |
| **+Shadow** | HIGH **+ single real directional shadow map (PCF)** — separate later milestone | desktop | +1 depth pass |

Rationale for the split: the whole **signature wuxia-night look (LOW)** is cheap ALU — half-Lambert,
hemispheric ambient, emissive, and rim already read as "moonlit". MED adds material identity
(sheen/matcap) for a couple of `pow`s and one texture. HIGH is the only tier that pays real texture
+ TBN cost, and the real shadow map is isolated as its own milestone so nothing else waits on it.
One shader, gated by `u_qualityTier` branches (+ per-material feature flags like "has matcap"/"has
normal map"); split HIGH to a `#define` variant only if the weakest Mali stalls on the branches.

### 4a. Technique cookbook (drop-in GLSL — Core owns the shader, Environment feeds uniforms)
All operate in the existing forward pass, write to the HDR buffer, and cost near-nothing unless
noted. `N`,`L`,`V`,`H`,`albedo` as already defined in `surface_lit.fs`.

**Hemispheric ambient** (LOW) — replaces flat `u_ambientColor`. Near-free, biggest dark-scene win:
```glsl
// u_skyColor / u_groundColor from environment_system (sky tint above, bounce/ground below)
float hemi = N.y * 0.5 + 0.5;
vec3 ambient = albedo * mix(u_groundColor, u_skyColor, hemi);
```

**Emissive** (LOW) — glowing runes/eyes/weapon energy; additive into HDR so bloom nurses it:
```glsl
// u_emissiveTint + optional MATERIAL_MAP_EMISSION sample; u_emissiveStrength can exceed 1.0
vec3 emissive = texture(emissionMap, fragTexCoord).rgb * u_emissiveTint * u_emissiveStrength;
color += emissive;   // add AFTER lighting, BEFORE fog
```

**Directional-moon rim** (MED) — upgrade the uniform rim so it favours the anti-moon silhouette
(reads as real backlight, not a uniform halo):
```glsl
float fres = pow(1.0 - max(dot(N, V), 0.0), u_rimPower);
float moonFacing = smoothstep(-0.2, 0.6, dot(N, -L)); // stronger where the moon is behind the edge
vec3  rim = u_rimColor * (fres * moonFacing * u_rimStrength);
```

**Matcap / lit-sphere** (MED) — huge "expensive material" payoff on mobile for near-zero cost; use
for jade / metal weapons / pháp bảo. One texture sample keyed by the view-space normal:
```glsl
// matcap sampled in VIEW space (transform N by the view matrix in the VS → vViewNormal)
vec2 muv = vViewNormal.xy * 0.5 + 0.5;
vec3 matcap = texture(matcapTex, muv).rgb;
// combine: multiply for base material, or add for a metallic/energetic sheen
color = mix(color, color * matcap * 2.0, u_matcapAmount);
```
Per-material flag `u_hasMatcap`; materials without a matcap texture skip this entirely.

**Normal mapping** (HIGH) — best "looks real" per GPU-cost; identity when no map, so zero-cost off:
```glsl
// VS builds TBN from vertexNormal + vertexTangent (raylib MATERIAL_MAP_NORMAL provides the tangent)
vec3 nTex = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
N = normalize(TBN * nTex);   // replaces the geometric N before lighting
```

**Anisotropic sheen** (HIGH, optional) — long hair / silk robes; shift the Blinn highlight along the
tangent so it streaks instead of dotting:
```glsl
vec3  T = normalize(fragTangent);
float ToH = dot(T, H);
float aniso = sqrt(max(0.0, 1.0 - ToH * ToH));      // stretched highlight
float spec = pow(aniso, u_anisoShininess) * u_specStrength;
```

**Fake jade/skin SSS** (HIGH) — half-Lambert already wraps light; add cheap back-scatter so thin
edges glow with the moon behind them (jade artifacts, skin, translucent robes):
```glsl
float back = pow(max(dot(V, -L), 0.0), u_sssPower) * u_sssStrength;
color += albedo * u_sunColor * back;   // light bleeding through from behind
```

### 4b. Real directional shadow (separate milestone — Environment Agent owns shadow)
The one genuinely heavy item, HIGH-tier only, do LAST. Single directional depth pass from the sun +
a small PCF (3×3) lookup; fake blob shadows stay the default everywhere else and on all lower tiers.
Consider short-range **screen-space contact shadows** as a cheaper stand-in on Mali before
committing to a full shadow map. Keep entirely behind `+Shadow`.

## 5. Deployment across the project (make it one-liner everywhere)
`SurfaceMaterial_Apply` is already the deploy primitive. Extend coverage so the whole scene shares
one lighting model:
- **Map Agent** — apply to static map/prop meshes in `maps/*` after load.
- **Boss Agent** — apply to boss models (in `*_def.c`, mirroring characters).
- **Character Agent** — already done; add normal-map pickup for HIGH.
- Document the 3-line recipe (`Init` once, `Apply` per model, `UpdateFrame` per frame) in
  `environment/docs/API.md` or a short `SURFACE_MATERIAL.md` so any future model is lit consistently.

## 6. Phased rollout (suggested order)
1. **P1 — Toggle + LOW-tier look (Core + Environment).** `GfxQuality` switch + `u_qualityTier`
   branches, conservative Android default, AND the LOW cookbook items (hemispheric ambient +
   emissive + rim). These are ~free and already deliver the signature moonlit look, so P1 both ships
   the "easy ON/OFF for weak machines" ask AND visibly improves every device — lowest risk, highest
   ratio. Environment adds `sky/ground` ambient colors + emissive plumbing.
2. **P2 — Sandbox toggle + lit test scene (Sandbox).** A UI control to flip tiers live + a scene to
   eyeball each tier on real content. Enables iteration on the rest.
3. **P3 — MED materials (Core).** Blinn spec sheen (already present) + **matcap** material path +
   directional-moon rim. Matcap is the big cheap "expensive material" win for weapons/jade.
4. **P4 — Wider deploy (Map, Boss, Character).** `SurfaceMaterial_Apply` on props/map/bosses;
   document the 3-line recipe.
5. **P5 — HIGH upgrades (Core + Character).** Normal mapping (needs tangents from Character/model
   export), anisotropic sheen, fake jade/skin SSS. Each independently flag-gated.
6. **P6 — Real shadow map (Environment).** The one heavy milestone; do only if P1–P5 look good and
   there's perf headroom. Try screen-space contact shadows first on Mali.

## 7. Mobile / rlvk constraints (Renderer agent verifies each shader change)
- **Every `surface_lit` edit must compile & run on Mali via rlvk, verified ON DEVICE** — desktop
  green ≠ device correct (see `RLVK_HANDOFF.md`, and the sin-hash landmine: mobile precision kills
  things silently). Watch for: large-magnitude math (avoid `sin`/`fract` hashes),
  `mediump`/precision, `f`-suffix literals, `matModel` identity assumptions (memory
  `android-shader-pipeline`).
- Runtime `u_qualityTier` branching is fine on Vulkan; if the weakest Mali stalls on it, switch
  HIGH to a `#define` variant (Renderer confirms both variants build).
- Per-material uniforms must work on rlvk's **pool-ring descriptor path** (Mali has no
  push-descriptor) — already exercised, but re-check after adding HIGH uniforms.
- Keep writing to the **HDR float buffer** (G1) so spec/rim bloom; don't add an LDR clamp.

## 8. Non-goals & deferrals
**Dropped outright (user 2026-07-19 — do NOT do):**
- **SSAO** — too expensive/noisy on Mali; use baked vertex-AO or a cheap fake if occlusion is needed.
- **Full metal/roughness PBR + IBL** — heavy, needs HDR cubemaps, wrong art direction.
- **Deferred / clustered / multiple dynamic lights** — one sun/moon is the whole model.

**Deferred (later, not now):**
- **Fog upgrade** — the height-fog + moon-tint scatter is postponed (user finds it hard). Keep the
  *existing* cheap distance fog in `surface_lit.fs` exactly as-is; don't gate tiers on it.
- **Real shadow map** — P6, its own milestone; fake blob shadows stay default until then.

**Out of scope by design:**
- Per-skill custom character shading — VFX keep their own `EffectMaterial`/`AuraShellMaterial`; this
  plan covers opaque scene surfaces (characters/props/map/bosses) only.
- Cascaded shadow maps (single directional is enough if P6 happens at all).
