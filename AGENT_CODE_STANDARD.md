# AGENT_CODE_STANDARD.md — Self-Check Rules (Agent-Maintained)

> Not API docs (see `CORE_API.md`, `COMPUTE_API.md`, `ENVIRONMENT_API.md`, `MAP_API.md`). This is a terse mistake-prevention checklist for ALL code layers: skills (`skills/`), core engine (`core/`, `compute/`, `environment/`, `maps/`), common shaders (`core/shaders/common/*.glsl`).
> Sections 1-9 = skill code. Section 10 = core layer (incl. common shader functions).
> **Update rule:** any time an API doc changes, or you learn a new lesson from a bug, edit this file in the same turn. Keep entries terse — bullet + one-line reason max.

## 0. Before writing a skill
- Read relevant `CORE_API.md` section. Grep `core/*.h` to confirm function still exists (docs may lag).
- Folder: `skills/[element]/[skill_name]_skill/` — include path must match exactly incl. `_skill` suffix.
- Never edit `core/` files directly (Core Agent owns it) — read `.h` only. If you ARE Core Agent, go to §10.

## 1. C99 / Compile
- `.c` skill files: must `#include <stddef.h> <stdlib.h> <stdio.h>`.
- Never bare `#define PI` — always `#ifndef PI / #define PI ... / #endif`.
- No `malloc/calloc/realloc/free` anywhere in skill code — static arrays + flags only.
- `onDeathEmit`/`onLiveEmit` configs must be `static`.

## 2. Scale (do not use "normal physics" numbers)
- Mesh/tube radius: 10-20f. Impact/light radius: 50-100f.
- Gravity/force: 300-700f (never 9.8 or 1.0).
- Particle speed: 100-300f.

## 3. Color
- No raw `Color{...}` literals — use `ELEMENT_COLOR_*` from `skill_manager.h`.
- Shade/fade via `ColorAlpha`/`ColorLerp`, not manual channel math.
- Multi-stage color → `ColorGradient`, not plain `colorStart/colorEnd`.

## 4. Resource Manager
- Load via `ResourceManager_LoadTexture/LoadShader` only, never raw raylib load calls.
- `Unload[Name]Skill` must NOT call `UnloadTexture`/`UnloadShader` — leave empty.

## 5. ForceField / Particle / Trail
- `ForceField` instances must be `static`.
- `FORCE_RADIAL_AXIS`/`FORCE_VORTEX_AXIS` ignore static origin/direction — feed axis per-frame via `SetFollowerAxis`.
- `TRAIL_TYPE_FOLLOWER`: call `SetFollowerAxis` AND `UpdateFollowerPosition` every frame — missing either breaks orientation.
- Non-persistent trails/fields (`life==0`) must `KillTrail` when done — no leaked slots.

## 6. Mesh / Geometry
- Never `DrawCylinder/DrawCone/DrawCube/DrawSphere`(+wireframe) for real meshes — use `procedural_mesh_utils.h`, `DrawRibbonStrip`, `ProceduralMesh_DrawTube`.
- Never hand-roll Bezier/Frenet/path-sampling — use `path_spline.h` + `procedural_mesh_utils.h`.
- `DrawRibbonStrip`/`ProceduralMesh_DrawTube` only submit geometry — caller must `BeginShaderMode()` (+`BeginBlendMode()` if alpha<1) first.
- Before `rlBegin()` custom geometry: `rlColor4ub(255,255,255,255)` to reset vertex color.

## 7. Shaders
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
- No raylib primitives for real meshes (see §6).
- Straight layouts need perpendicular jitter (`perp` + `GetRandomValue` pattern, CORE_API §12.2).
- Every spawned instance: random scale 85-115%, yaw 0-360°, pitch/roll ±10°.
- One shader for the skill's whole lifecycle (rising→active→dissolve) — no shader swap mid-skill. `u_dissolve` stays 0.0 until dissolve phase.
- Emissive area ≤ ~20-30% via `smoothstep`; rest uses diffuse+Fresnel to keep volume — never fully emissive.

## 9. Definition of done (skill)
- `make` builds clean — don't just eyeball code.
- Re-check §1-8 as a literal checklist; don't skip §7a if the skill has a custom shader.
- New core API found in `.h` but missing from `CORE_API.md` → report it, don't guess behavior.

## 10. Core layer (`core/`, `compute/`, `environment/`, `maps/`, common shaders)

### 10.1 General
- New/changed core API must stay backward compatible with every skill caller — don't change existing signatures; add new functions or append-only struct fields instead.
- Before changing a public function's behavior: `grep -r` across `skills/` for callers. Breaking changes must be documented (`CORE_API.md` etc.) BEFORE landing, per `CLAUDE.md` cross-module rule.
- No dynamic allocation in core runtime paths — static pools matching existing patterns (`MAX_DECALS`, `MAX_VFX_LIGHTS`, `MAX_DISTORTION_SOURCES`).
- New modules follow existing Init/Update/Draw/Unload lifecycle shape (see `decal_system.h`, `vfx_light.h`).
- Update `CORE_API.md` (or relevant doc) in the same turn as the code change — docs must never lag code.

### 10.2 Adding functions to common shaders (`core/shaders/common/*.glsl`)
- File by domain: hash/noise/fbm → `noise.glsl`; lighting (diffuse/specular/fresnel/normal) → `lighting.glsl`; generic effects (dissolve/flow/emissive) → `fx.glsl`; world-space/no-UV projection → `triplanar.glsl`. Don't mix domains.
- Check name doesn't collide with GLSL builtins (lesson: `noise2` clashed with builtin `noise()` → renamed `vnoise`).
- No `f` float suffixes in new GLSL — affects every skill that includes the file.
- Don't redeclare existing `vs_header.glsl`/`fs_header.glsl` vars/uniforms.
- A new common-header function is public API for every skill — keep its signature stable; if it must change, do the §10.1 caller-grep and update the GLSL Shader Guidelines section of `CORE_API.md`.
- Keep `highp` precision for any uniform shared between VS+FS (lesson: `mediump`→`highp` fix was required for Android, see commit "fs_header.glsl — đổi precision mediump float → precision highp float"). Don't introduce `mediump`/`lowp` regressions.
- New common code only runs through the `#include` path (runtime-rewritten to `#version 300 es`) — must stay valid under both `#version 330` (desktop) and `#version 300 es` (Android) syntax.

### 10.3 Compute (`compute/`)
- Read `COMPUTE_API.md` first — GPU particle system is shared by skills AND environment; bugs here hit both.
- SSBO/buffer layout changes must stay in sync with C-side structs — verify std140/std430 alignment before committing.

### 10.4 Definition of done (core change)
- `make` builds clean.
- Grepped `skills/` (+ `environment/`, `maps/` if relevant) — no caller broken.
- Relevant API doc updated to match.
- This file updated if the change produced a new rule or lesson.
