# AGENT_CODE_STANDARD.md — Self-Check Rules (Agent-Maintained)

> Not API docs (see `core/docs/API.md`, `core/particles/docs/GPU_BACKEND_API.md`, `environment/docs/API.md`, `maps/docs/API.md`). This is a terse mistake-prevention checklist for ALL code layers: skills (`skills/`), core engine (`core/`, `environment/`, `maps/`), common shaders (`core/shaders/common/*.glsl`).
> Sections 1-9 = skill code. Section 10 = core layer (incl. common shader functions).
> **Update rule:** any time an API doc changes, or you learn a new lesson from a bug, edit this file in the same turn. Keep entries terse — bullet + one-line reason max.

## 0. Before writing a skill
- Read relevant `core/docs/API.md` section. Grep `core/*.h` to confirm function still exists (docs may lag).
- Never edit `core/` files directly (Core Agent owns it) — read `.h` only. If you ARE Core Agent, go to §10.
- **Forbidden directories:** Never read, list, or touch `build/`, `_deps/`, `android.wuxing_skills/`, or `unreal-engine-starter-content-main/` (strictly off-limits unless explicitly permitted by user).
- **Auto-registered on build** by `scripts/generate_registry.py` — no manual registration step. Folder `skills/[element]/[skill_name]_skill/` holds `[skill]_skill.h` (lifecycle protos), `[skill]_skill.c` (logic + rlgl render), optional `.vs`/`.fs`/`.png` (auto-copied). Include your own header with the FULL path matching the folder exactly, incl. the `_skill` suffix (e.g. `#include "skills/wood/jade_burst_skill/jade_burst_skill.h"`).

## 1. C99 / Compile
- Strict **C99**; Raylib 6.0. Rendering backend: **Vulkan 1.1 via `rlvk` is the priority** (see `third_party/vulkan/docs/HANDOFF.md` / `third_party/vulkan/`); OpenGL 3.3 Core (desktop) and GLES 3.x (Android) are the fallback/legacy paths. Skill/draw code stays backend-agnostic — use `rlgl`/raylib calls, never raw GL or raw Vulkan.
- Relative include paths from repo root: `#include "core/particles/particle_system.h"`.
- `.c` skill files: must `#include <stddef.h> <stdlib.h> <stdio.h>` (for `NULL`/`snprintf` — not implicitly included).
- Never bare `#define PI` — always `#ifndef PI / #define PI ... / #endif` (bare redef = `-Wmacro-redefined`, a hard error in strict builds).
- No `malloc/calloc/realloc/free` anywhere in skill code — static arrays + flags only (stack vars/structs OK).
- `onDeathEmit`/`onLiveEmit` configs must be `static`.

## 2. Scale — meter-scale (1 unit = 1 m; NEVER the old ×100 numbers)
- Mesh/tube radius: 0.10–0.20f. Impact-burst/light radius: 0.5–1.5f.
- Gravity/force: 3.0–9.8f (judge against real gravity 9.81; never the old 300–700f).
- Particle speed: 1.0–3.0f (m/s).

## 3. Color
- No raw `Color{...}` literals — use `ELEMENT_COLOR_*` from `skill_manager.h`.
- Shade/fade via `ColorAlpha`/`ColorLerp`, not manual channel math.
- Multi-stage color → `ColorGradient`, not plain `colorStart/colorEnd`.
- A texture used with straight alpha must keep neutral white RGB even in its
  transparent border; additive-only dark RGB fringes become visible halos.

## 4. Resource Manager
- Use `ResourceManager_LoadTextureVariant` for different mipmap/sampler options; do not mutate a borrowed shared texture ID or its copied mipmap metadata.
- Load via `ResourceManager_LoadTexture/LoadShader` only, never raw raylib load calls.
- `Unload[Name]Skill` must NOT call `UnloadTexture`/`UnloadShader` — leave empty.

## 5. ForceField / Particle / Trail
- `ForceField` instances must be `static`.
- `FORCE_RADIAL_AXIS`/`FORCE_VORTEX_AXIS` ignore static origin/direction — feed axis per-frame via `SetFollowerAxis`.
- `TRAIL_TYPE_FOLLOWER`: call `SetFollowerAxis` AND `UpdateFollowerPosition` every frame — missing either breaks orientation.
- Non-persistent trails/fields (`life==0`) must `KillTrail` when done — no leaked slots.

## 6. Mesh / Geometry
- Index packing must preserve every authored attribute seam and triangle order; retain the original mesh when index limits or allocation fail.
- Never `DrawCylinder/DrawCone/DrawCube/DrawSphere`(+wireframe) for real meshes — use `procedural_mesh_utils.h`, `DrawRibbonStrip`, `ProceduralMesh_DrawTube`.
- Never hand-roll Bezier/Frenet/path-sampling — use `path_spline.h` + `procedural_mesh_utils.h`.
- `DrawRibbonStrip`/`ProceduralMesh_DrawTube` are low-level geometry-only APIs for manager batching. Standalone VFX must use `VFXRender_BeginAppearance`/`VFXRender_BeginDraw`; standalone ribbons should use `DrawRibbonStripAppearanceEx`.
- Before `rlBegin()` custom geometry: `rlColor4ub(255,255,255,255)` to reset vertex color.

## 7. Shaders

- When capping fog opacity, bound interval extinction before accumulating in-scattering so premultiplied radiance and opacity remain consistent.
- Opaque cloud seas write native surface depth to hide submerged cliffs consistently with fog; transparent rim mist preserves depth. Draw sky before clouds and flush both sides of depth-mask changes.
- Linear scene depth is view-axis Z: convert it to ray distance before reconstructing volumetric samples.
- Fog reconstruction filters premultiplied radiance and opacity together, normalizes weights, and rejects unrelated depth layers; validate silhouettes as well as sky grain.
- Distant volumetric framing uses focus-relative ground depth and projected span; keep local fog sampling independent of the distant haze fade.
- Include order: `fs_header.glsl` → `noise.glsl` (if needed) → `lighting.glsl` → `fx.glsl` → `triplanar.glsl` (if needed; depends on `noise.glsl` for `triplanarNoise`).
- VS must end with `VS_FinalOutput(vec3 finalPos)` — exactly one vec3 arg.
- Never redeclare built-ins: `fragPosition`, `fragNormal`, `u_time`, `viewPos`, `u_resolution`, `finalColor`.
- Standard `lightDir`, hardcode everywhere: `normalize(vec3(0.5, 0.8, 0.5))`.
- VS displaces position via height-field → `fragNormal` does NOT auto-update → re-derive via `perturbNormal()` in FS using the SAME height formula as VS.
- Never reimplement hash/noise/fbm/dissolve/flowBlend/emissiveMask — use `noise.glsl`/`fx.glsl`.
- Custom uniforms (`u_uvLength`, `u_dissolve`...): cache location once in `Init[Name]Skill` via `GetShaderLocation`, never per-frame. Set value after `SkillManager_BeginShader()`, before draw.
- Same uniform name in both VS+FS → one `SetShaderValue()` call covers both (one linked program).

### 7a. Android/GLES checklist (mandatory before calling a skill done)
- No `f` suffix on float literals anywhere (`1.25f` → `1.25`).
- Standalone `.vs` (no `#include vs_header.glsl`) must add `#ifdef GL_ES / precision highp float / #endif` itself.
- rlgl immediate mode (`rlBegin/rlEnd`, `ProceduralMesh_DrawTube`) bypassing `SkillManager_BeginShader` → must manually set `matModel = identity`, else `fragNormal` → NaN → white mesh on Android.
- Uniform shared by VS+FS must have matching precision (default `highp` via common headers — don't introduce mismatched `mediump`).
- White-screen mesh on Android → check logcat for `SHADER: compile failed` or `Link error...precision does not match` first, fix via rules above, rebuild.

## 8. Aesthetic (Anti-Robotic) Laws
- Any NEW FX fixture with more than one authored variant must expose the active
  variant and wrap deterministic `>` next / `<` previous controls in NEW FX.
- No raylib primitives for real meshes (see §6).
- Straight layouts need perpendicular jitter (`perp` + `GetRandomValue` pattern, CORE_API §12.2).
- Every spawned instance: random scale 85-115%, yaw 0-360°, pitch/roll ±10°.
- One shader for the skill's whole lifecycle (rising→active→dissolve) — no shader swap mid-skill. `u_dissolve` stays 0.0 until dissolve phase.
- Emissive area ≤ ~20-30% via `smoothstep`; rest uses diffuse+Fresnel to keep volume — never fully emissive.

## 9. Definition of done (skill)
- `make` builds clean — don't just eyeball code.
- Re-check §1-8 as a literal checklist; don't skip §7a if the skill has a custom shader.
- New core API found in `.h` but missing from `core/docs/API.md` → report it, don't guess behavior.

## 10. Core layer (`core/`, `environment/`, `maps/`, common shaders)

### 10.1 General
- Typed fields keep Newton force, acceleration and medium velocity separate. Direct Motion receivers sample ordinary Wind excluding Motion publication; authored drag/buoyancy replace automatic material approximations. Legacy packed force IDs and units never change.
- Rooted objects consume Motion Newton forces through their own mass/spring/damping; stateless queries must not capture free-particle lanes or fire arrival callbacks.
- Anchored vegetation samples airflow near terrain/canopy height, not at the airborne source height; a horizontal-axis swirl has different bending directions at those heights.
- Guide airflow for anchored vegetation must use the shared Wind source snapshot; do not spawn replacement vorticles every frame or confuse plant bending with free-body capture.
- Liquid code uses `core/liquid/` and `Liquid*`; `core/fluid/` contains source-compatibility headers only.
- New/changed core API must stay backward compatible with every skill caller — don't change existing signatures; add new functions or append-only struct fields instead.
- Under rlvk, the `Shader` argument to `SetShaderValue` does not select the target program: activate it with `BeginShaderMode`, keep uniform upload plus dependent draws in that scope, then end it.
- In this engine `matModel * vertexPosition` is shader/view space, not world space; positional map effects must inverse-transform it before comparing against world-space centres.
- Transform world light directions with rotation only and camera points with translation; ground-height AO must use recovered world Y.
- Never reduce a position-dependent Vortex/Turbulence field to one shared direction; keep it spatial, and derive Radial Blast direction independently at each receiver.
- A vegetation Radial Blast must read briefly as a moving pressure front before a stronger Turbulence wake takes over. Ramp impact Turbulence in with a short attack instead of applying full strength on the spawn frame; mirror the envelope and authored budgets across CPU/GPU and lock them with tests.
- Vegetation mesh LOD uses full camera distance and FOV scaling; orbit-radius compensation belongs only to visibility range. Apply blade-normal rounding in one stage and filter unresolved highlights by projected width.
- Core Wind owns vegetation forcing, including world-space grass-wave noise; vegetation shaders may only filter that field through species-specific lag, compliance, flutter, and bend limits. Do not add standalone sine/noise motion that remains active when the sampled wind is zero, and keep visible/shadow deformation identical.
- Cloud visibility is one shared world-space field transported by macro wind; attenuate direct diffuse, specular and transmission only, preserving sky ambient and local VFX lights.
- Large-map directional shadows keep static casters in a world-fixed cached layer and dynamic casters in the camera-following layer; bind both samplers explicitly, and invalidate/rebuild the static cache when the sun direction changes.
- Before adding foliage shadow casters, check receiver filter width and ambient fill: a valid shadow can disappear under nearly unattenuated sky lighting.
- Distant volumetric fog starts beyond the camera focus across zoom levels, fades in spatially, and scales beam haze with god-ray intensity; a fixed near-plane cutoff can wash out the player.
- Before changing a public function's behavior: `grep -r` across `skills/` for callers. Breaking changes must be documented (`core/docs/API.md` etc.) BEFORE landing, per `CLAUDE.md` cross-module rule.
- No dynamic allocation in core runtime paths — static pools matching existing patterns (`MAX_DECALS`, `MAX_VFX_LIGHTS`, `MAX_DISTORTION_SOURCES`).
- Skip texture uploads only when quantized bytes and resource identity match; keep simulation running and allocate/release upload snapshots at initialization/unload.
- Reset GPU timestamp queries outside render passes; report independent windows of available, ordered samples and reject zero spans or host-drained command splits.
- New modules follow existing Init/Update/Draw/Unload lifecycle shape (see `decal_system.h`, `vfx_light.h`).
- Update `core/docs/API.md` (or relevant doc) in the same turn as the code change — docs must never lag code.

### 10.2 Adding functions to common shaders (`core/shaders/common/*.glsl`)
- A material's `.mat output`, GLSL resolver, render pass and runtime blend are one contract: fixed outputs use the matching `VFX_Resolve*`; surface-aware EffectMaterial uses `VFX_ResolveOutput` plus `Material_BeginVFX/EndVFX` (ADDITIVE→EMISSION, ALPHA/PREMULTIPLIED→BODY). Legacy `Material_Begin/End` remains caller-managed.
- Tone-map-safe colour is explicit per producer: custom trail/particle shaders do not inherit EffectMaterial's permutation. For structured emitters preserve the sub-Bloom carrier and correct only HDR excess after coverage; if additive submits that completed value, use unit source alpha so coverage is not applied twice.
- File by domain: hash/noise/fbm → `noise.glsl`; lighting (diffuse/specular/fresnel/normal) → `lighting.glsl`; generic effects (dissolve/flow/emissive) → `fx.glsl`; world-space/no-UV projection → `triplanar.glsl`. Don't mix domains.
- Treat imported channel-packed textures as data contracts, not display RGB. Audit
  every channel and companion map before choosing a decoder; never infer six-way
  lighting from a smoke flipbook or discard non-alpha structure.
- Select mutually exclusive decoders with one numeric material-mode uniform;
  do not accumulate parallel boolean mode uniforms whose default fallthrough can
  reinterpret packed data as display colour.
- Check name doesn't collide with GLSL builtins (lesson: `noise2` clashed with builtin `noise()` → renamed `vnoise`).
- No `f` float suffixes in new GLSL — affects every skill that includes the file.
- Don't redeclare existing `vs_header.glsl`/`fs_header.glsl` vars/uniforms.
- A new common-header function is public API for every skill — keep its signature stable; if it must change, do the §10.1 caller-grep and update the GLSL Shader Guidelines section of `core/docs/API.md`.
- Keep `highp` precision for any uniform shared between VS+FS (lesson: `mediump`→`highp` fix was required for Android, see commit "fs_header.glsl — đổi precision mediump float → precision highp float"). Don't introduce `mediump`/`lowp` regressions.
- New common code only runs through the `#include` path (runtime-rewritten to `#version 300 es`) — must stay valid under both `#version 330` (desktop) and `#version 300 es` (Android) syntax.
- Lightning is not a broad ribbon: use `core/lightning/`'s endpoint-pinned canvas, FBM-warped distance-field centreline, and one continuous core→corona→field colour profile. Reveal it with the shared `travelDuration`/`u_travel` phase, taper body/core/halo at both endpoints, and never leave the canvas edge visible.
- Volume raymarches must decorrelate the first step in both screen axes; a shared fixed phase turns finite-step error into stripes after upsampling. Use world-space noise, never `sin(dot(position, k))`; keep multi-octave `fbm3` HIGH-only and use `vnoise3` on mobile tiers.
- Volumetric fire must not reuse smoke/body opacity as emission alpha: gate full-density emission transport with reaction+heat, and keep reaction persistence measured against density decay.
- After lowering shared volume coverage, compare each composition's per-second density/reaction budget; restore undersized primaries at the `GasKind`-gated caller, not with another global shader gain.
- Volume bloom must come from visible, front-to-back-integrated HDR core radiance above the global bright-pass threshold; never use an unattenuated max-along-ray seed or raise the whole carrier to manufacture a halo.
- Bright-background gas must pair negative body/extinction structure with attenuation of broad emission; sample pre-VFX luma once per output pixel and preserve only a bounded reaction+density HDR core.

### 10.3 GPU particle backend (`core/particles/gpu/`)
- Read `core/particles/docs/GPU_BACKEND_API.md` first. New VFX code uses `core/particles/particle_manager.h`.
- SSBO/buffer layout changes must stay in sync with C-side structs — verify std140/std430 alignment before committing.

- New spatial guidance uses Newton forces and the receiver's actual mass. Derive damping from stiffness/mass; do not expose redundant frequency, steering, wind-response and reference-mass knobs in compositions. Derive volume and spherical drag area from mass/density; keep visual size separate. Density=0 retains old gravity-only profiles. Existing acceleration-field APIs keep their declared m/s² units.
- Guide arrival must never spawn an implicit blast or turbulence wake. Configure independent target fields/impulses explicitly and trigger cast-level fields once on actual swept arrival, with no global cooldown.

- Shared procedural motion uses m/s turbulence and signed swirl amplitudes; zero disables each. Derive eddy scale from field geometry, window the vector potential before its curl, and keep target flow independent of travel flow. SSF is a surface renderer; density/cohesion constraints belong to a liquid solver.

### 10.4 Definition of done (core change)
- Ambient wind receivers retain velocity, integrate drag displacement, and sample changing airflow along the trajectory with bounded timesteps; constant-air tests alone cannot verify vortex motion. Guard: `core/tests/atmosphere_motion_test.c`.
- `make` builds clean.
- Grepped `skills/` (+ `environment/`, `maps/` if relevant) — no caller broken.
- Relevant API doc updated to match.
- This file updated if the change produced a new rule or lesson.

## 11. Tester UI

- Use the same responsive rectangles for UI drawing and pointer capture. Hidden controls must not keep invisible click targets; capture wheel input before camera zoom and keep parameter selection separate from debug toggles.
- Bounds-check indexed HUD positions against the actual array length; a fourth status label needs a fourth entry.

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-06 | Codex | §10.1 Typed field units and adapter response ownership | core/motion/physical_field.h; core/motion/motion_body.h; core/wind/wind_system.h | Ground-truth |
| 2026-10-05 | Codex | Tester UI capture and indexed HUD positions | main.c; sandbox/vfx_test.h | Ground-truth |
| 2026-10-05 | Codex | Shared flow amplitude and SSF boundary | core/motion/motion_flow.h; core/liquid/liquid_surface.h | Ground-truth |
| 2026-10-05 | Codex | §10.3 Mass-aware spatial forces and arrival ownership | core/motion/motion_fields.h; core/particles/particle_dynamics.h | Ground-truth |
| 2026-10-05 | Codex | §7 Cloud background depth policy | maps/toolkit/map_props_cloud.inl; maps/tests/test_map_background_state.py | Ground-truth |
| 2026-10-05 | Codex | §4 Isolated texture sampler variants | core/resource_manager.h; core/tests/resource_manager_texture_variant_test.c | Ground-truth |
| 2026-10-04 | Codex | §7 Bounded fog radiance and opacity | core/volumetric/shaders/volumetric_fog.fs; core/tests/volumetric_fog_transport_test.c | Ground-truth |
| 2026-10-04 | Codex | §10.1 Liquid names and legacy compatibility | core/liquid/liquid_surface.h; core/fluid/fluid_surface.h | Ground-truth |
| 2026-10-03 | Codex | §10.1 Texture upload identity and timestamp validation | maps/toolkit/map_props_nature.inl; third_party/vulkan/rlvk/rlvk_platform.inl; third_party/vulkan/rlvk/rlvk_renderpass.inl | Ground-truth |
| 2026-10-03 | Codex | §7 Fog reconstruction | core/volumetric/shaders/volumetric_composite.fs; core/tests/volumetric_fog_composite_test.c | Ground-truth |
| 2026-10-03 | Codex | §10.1 Prop lighting spaces | maps/toolkit/prop_lit.c; maps/toolkit/shaders/prop_lit.fs; maps/tests/test_prop_lighting_space.py | Ground-truth |
| 2026-10-06 | Codex | §10.1 Guide/Wind snapshot ownership | core/motion/motion_fields.c; core/wind/wind_system.h | Ground-truth |
| 2026-10-06 | Codex | §10.1 Vegetation airflow sampling height | maps/toolkit/map_props_nature.inl; maps/tests/test_nature_local_wind.py | Ground-truth |
