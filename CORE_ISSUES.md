# Core Engine — Open Issues / Unfinished Work

Tracks work from the "Core API update" task list that is either unfinished
(reverted, needs a fresh approach) or not yet started. See `CORE_API.md` for
the API surface that IS shipped and documented.

---

## Item 3 — Soft Particles (REBUILD IN PROGRESS, paused — 3 real bugs fixed, ground occlusion confirmed still broken in isolation)

**Status: `core/screen_distort.c/.h` infrastructure is still in the tree and
untouched.** Builds clean, no regressions. Paused again this session after a
long screenshot-driven debugging loop — see "Session 2 findings" below for
what's now confirmed vs. still open. Picking this up again should be faster
than starting over: 3 real bugs are fixed, real test infrastructure now
exists, and the remaining problem is characterized much more precisely than
before (isolated to ground-plane occlusion specifically, not "some occluder
somewhere").

> [!NOTE]
> **Test harness re-added this session, present and working.**
> `core_test` (`skills/taiji/core_test/`) currently hosts the soft-particle
> test shape again — see "Test infrastructure now in place" below for exact
> file list and controls. Whoever resumes does NOT need to re-add anything;
> start by reading that section and re-running the existing test.

### What's implemented (same architecture as the first attempt)
- `core/screen_distort.c/.h`: `renderTex` rebuilt with a sampleable depth
  **texture** attachment (`LoadRenderTextureWithDepthTexture`, via manual
  `rlLoadFramebuffer`/`rlLoadTextureDepth`), a `prevDepthTex` R32F snapshot
  target (`LoadLinearDepthTarget`), `ScreenDistort_SnapshotDepth()` (copies
  + linearizes scene depth once/frame, called from `main.c` right after
  `ScreenDistort_End()`), `ScreenDistort_BindDepthForSoftParticles`/
  `UnbindSoftParticleDepth`.
- `core/shaders/depth_copy.fs`, `core/shaders/common/soft_particle.glsl`:
  unchanged in spirit from the first attempt (linearize NDC depth; expose
  `SoftParticle_LinearDepth`/`SoftParticle_Factor`).

### Three confirmed, fixed root causes (none of these were identified last time)

1. **GL depth TEST was never disabled, only depth WRITE.** The draw call
   only had `rlDisableDepthMask()`. With `GL_DEPTH_TEST` still enabled, the
   buried half of the sphere fails the hardware depth test against the
   ground plane and never reaches the fragment shader at all — no shader
   fix can matter if the fragment is discarded before it runs. **Fix:**
   wrap the draw in `rlDisableDepthTest()`/`rlEnableDepthTest()` too, not
   just the depth-mask pair. Confirmed via an isolated flat-shader test
   (Step 0): the full sphere, including the buried portion, became visible.

2. **Near-plane mismatch in the depth linearization.** `ScreenDistort_SnapshotDepth()`
   (and `BindDepthForSoftParticles`) used `rlGetCullDistanceNear()`/`Far()`
   (reflects `rlSetClipPlanes(0.1f, 15000.0f)` in `main.c`) to linearize
   depth — but the ACTUAL projection matrix used during rendering
   (`MyBeginMode3D`'s `rlFrustum(...)` in `main.c`) hardcodes **near=10.0**,
   not 0.1. These are two different, unrelated globals. Using the wrong
   near value silently crushed every real depth sample to near-zero
   (confirmed numerically: a CPU readback of the snapshot texture showed
   `0.17`–`4.4` world units for content that was actually 17–426 units
   away once the correct near value was used). **Fix:** added explicit
   `SOFT_PARTICLE_SCENE_NEAR`/`_FAR` constants in `core/screen_distort.c`
   matching `MyBeginMode3D`'s real values, with a comment cross-referencing
   `main.c` so they don't drift apart silently again. This coupling is
   fragile — if `MyBeginMode3D`'s near/far ever changes, these must be
   updated too; there's no shared single source of truth for it currently.

3. **Manual `rlActiveTextureSlot()`/`rlEnableTexture()` binding silently
   didn't reach the shader**, regardless of which slot (tried 1 and 8) or
   texture format (tried both the R32F depth snapshot and a normal RGBA8
   texture) — `texture(u_cameraDepthTex, ...)` read back as 0 every time.
   `core/flow_map.c` already hit and documented this **exact** class of bug
   for its own multi-texture shader (`FlowMap_Apply`'s "bug cũ" comment) and
   fixed it by switching to `SetShaderValueTexture()` instead, which lets
   raylib manage the texture unit itself. Applied the same fix to
   `ScreenDistort_BindDepthForSoftParticles` — confirmed via an unclamped
   debug-output test (sampled value let through raw instead of divided down
   for display, so it visibly clamped to solid red instead of being an
   indistinguishable near-black sliver) that texture sampling then worked
   correctly, for both a fixed UV and the real per-pixel computed UV.

### Session 2 findings — occlusion from real props works; ground occlusion does not, even in total isolation

**Confirmed working (positive result):** in `maps/bamboo_valley`, with the
test sphere near bamboo stalks and the player character, the fade correctly
and precisely traces the **silhouette** of whichever opaque object (bamboo
leaves, player model) sat between the camera and the sphere in the previous
frame — pixel-accurate cutout shapes, not noise. This reconfirms bugs #1–3
above are genuinely fixed and the mechanism works for ordinary opaque props.

**Confirmed broken (the real remaining problem):** built a brand new,
deliberately empty test map (`maps/soft_test_ground/` — see below) with
*only* one flat opaque floor at exactly `Y = 0.0f`, nothing else in the
scene, camera unobstructed. Test sphere (radius 40) centered at `Y = 0.0f`,
so genuinely half-buried, half-exposed. Even with `CORE_TEST_SOFT_FADE_DISTANCE`
raised from `30.0f` to `200.0f` (5–6× the sphere's radius) as a diagnostic:
- Debug Mode 1 (`SoftParticle_Factor` as grayscale) shows **uniform 1.0
  (fully opaque) across the entire visible sphere**, including its
  lowest/most-buried visible pixels.
- Debug Mode 2 (raw, **unclamped** `diff = sceneLinear - fragLinear`,
  sign-coded, with an explicit yellow flag for any `|diff| < 10` pixel)
  confirms this isn't a clamping artifact: `diff` reads strongly positive
  (≥ ~120 world units) everywhere, and **not a single pixel** anywhere on
  the sphere's surface ever lit up the near-zero yellow flag.

So: the exact same mechanism that correctly detects a bamboo stalk or the
player model as an occluder **never once detects the ground plane itself**,
even when the test sphere is unambiguously, geometrically buried inside it.
This is a materially more precise problem statement than the original "open
question" above (which blamed an occluder-in-front, i.e. bamboo) — this
time there is categorically nothing else in the scene, and the fade still
never triggers, not even a thin sliver.

**Leading hypothesis, NOT yet confirmed:** everything that correctly
registers occlusion (bamboo, player) is drawn via `DrawModelEx`/normal mesh
draws. The one thing that never registers (the floor, in both
`maps/default_arena` and the new `maps/soft_test_ground`) is drawn via raw
`rlBegin(RL_TRIANGLES)`/`rlVertex3f`/`rlColor4ub` immediate mode with no
explicit shader bound. Worth testing directly whether this immediate-mode
path actually writes real depth into the framebuffer `ScreenDistort`
snapshots from — has NOT been confirmed either way this session, just
flagged as the most concrete remaining lead given the clean A/B result
(mesh-drawn props: works: raw-immediate floor: never works, in every test
location tried, on two different maps).

**Next steps for whoever resumes:**
1. Confirm or rule out the immediate-mode-floor hypothesis above — the
   fastest way is probably a GPU frame capture (RenderDoc or similar; same
   tooling gap already flagged in Item 5 below) inspecting the actual FBO
   depth attachment right after the floor draw call, rather than more
   screenshot/shader-color inference.
2. If confirmed: either switch floor drawing to a real mesh
   (`DrawMesh`/`DrawModel`) across the affected maps, or find whatever
   depth-state difference immediate-mode triangles have vs. mesh draws in
   this specific rlgl/GL setup and fix it at the source.
3. Re-verify `SOFT_PARTICLE_SCENE_NEAR`/`_FAR` (`core/screen_distort.c`)
   still match `MyBeginMode3D`'s real `rlFrustum` near/far in `main.c` —
   flagged as a fragile coupling when bug #2 was fixed, not re-checked
   this session.
4. Once truly fixed and visually confirmed (top of sphere opaque, buried
   bottom fades smoothly, on the clean `SOFT_TEST_GROUND` map): re-add the
   "Soft Particles" section to `CORE_API.md` (removed in the original
   revert, commit `54a9c4c`), re-tune `CORE_TEST_SOFT_FADE_DISTANCE` back
   down from the `200.0f` diagnostic value to a sane default, and decide
   intentionally (not by formula-accident) whether "fade when occluded by
   something in front" is the desired semantic for a hard occluder vs. a
   hard clip/discard.

### Test infrastructure now in place (for whoever resumes — nothing needs re-adding)

- `skills/taiji/core_test/core_test_skill.c` + `.h`: `CastCoreTestSkill`
  spawns a radius-40 sphere centered at `Y=0` (startPos X/Z) using
  `SkillManager_BeginShader` (auto `matModel`/`u_lightDir`, sidesteps Item
  11's bug), `ScreenDistort_BindDepthForSoftParticles`, and
  `rlDisableDepthTest()`/`rlDisableDepthMask()` around the draw.
  - **L** — CPU-side 3-point (top/mid/bottom) numeric depth readback,
    logged via `TraceLog` AND drawn on-screen (`DrawCoreTestSkillDebugHUD`,
    wired into `main.c`'s 2D pass). **Turned out to be unreliable/misleading**
    — the sample points are literal world-space positions on the sphere's
    vertical center axis, which do NOT correspond to the actual visible
    front-surface fragment at that screen pixel from an oblique camera
    angle, so its `diff` values run substantially more negative than the
    real per-pixel GPU value (Mode 2 below). Trust the shader-side debug
    modes over this readback; consider removing or reworking it before
    reusing this harness for anything else.
  - **H** — cycles 3 shader debug modes (`u_debugShowFade` uniform in
    `core_test_soft.fs`): 0 = normal shaded look, 1 = `SoftParticle_Factor()`
    as grayscale (max luma 0.35, kept under `main.c`'s `bloomThreshold=0.5f`
    so PostFX bloom can't add a false glow halo), 2 = raw unclamped `diff`
    (green=positive/unoccluded, red=negative/occluded, explicit yellow flag
    for any `|diff| < 10` pixel so a thin transition band can't hide inside
    an over-coarse color scale).
  - **`CORE_TEST_SOFT_FADE_DISTANCE`** currently `200.0f` (bumped up from
    `30.0f` as a diagnostic — see findings above). Reset this once the real
    bug is found and fixed.
  - **IMPORTANT key-binding gotcha hit this session:** `KEY_K` is already
    bound globally in `main.c` to cycle maps. An earlier version of this
    harness also bound `KEY_K` to toggle the debug view — raylib doesn't
    give one system exclusive ownership of a key, so every debug-view
    toggle silently ALSO cycled the map, which looked exactly like "the
    fade result depends on the player's position" for several test rounds
    before the real cause (wrong map, not player position) was found.
    Debug view is now on `KEY_H` specifically to avoid this. Grep
    `IsKeyPressed(KEY_` across `main.c`/`sandbox/*.c` before binding any
    new key in a test harness.
- `skills/taiji/core_test/core_test_soft.vs`/`.fs`: minimal shader,
  `#include`s `vs_header.glsl`/`fs_header.glsl`/`soft_particle.glsl`.
- `maps/soft_test_ground/` (new): registered as map name `SOFT_TEST_GROUND`
  — a single flat opaque floor at exactly `Y=0.0f`, nothing else at all.
  Built because neither existing map was clean enough to isolate this bug:
  `maps/bamboo_valley` draws no floor of its own at all (the visible ground
  there comes from elsewhere, likely fog/background, not a real depth-writing
  mesh), and `maps/default_arena`'s floor sits at `Y=-0.05f` and is drawn
  alongside grid lines that added noise to the comparison. Default active
  map at startup is still `BAMBOO_VALLEY` (`core/map_manager.c`'s
  `MapManager_Init`); cycle to `SOFT_TEST_GROUND` with **K** (currently 3
  presses from the default map — check `core/maps_generated.h`'s
  registration order if this ever changes).

### Process note for next time
A numeric CPU readback of the actual texture/uniform values (`LoadImageFromTexture`
+ inspect specific pixels, or log resolved `GetShaderLocation` results) caught
two of the three bugs above almost immediately, where screenshot-color
guessing previously produced repeated false "it's broken" / false "it's
fixed" conclusions. **But "numeric" alone isn't sufficient** — this
session's CPU 3-point readback (see "L" above) was itself numeric yet still
misleading, because the sample points didn't correspond to the real visible
fragment. A numeric check is only as good as whether it queries the actual
thing being rendered; prefer a shader-side debug view (computes the real
per-fragment value directly) over a CPU-side approximation whenever
possible. Also: an already-**clamped** debug view (Mode 1's `[0,1]` factor,
or the original `/500`-divided color swatch from bug #3's investigation)
can hide a real but narrow signal just as easily as guessing from an
unprocessed color — Mode 2's fix (explicit near-zero flag color instead of
a wider linear scale) was needed specifically because a `diff/300` scale
saturated long before revealing whether anything ever approached zero.

### Session 3 (Map Agent) — immediate-mode-floor hypothesis CONFIRMED and fixed; blocked on verification by an unrelated build break

**Hypothesis confirmed.** Read `maps/soft_test_ground/soft_test_ground.c`
directly: `DrawSoftTestGroundMap()` was exactly as suspected — no mesh, no
shader, just `rlDisableBackfaceCulling()` + `rlBegin(RL_TRIANGLES)` +
per-vertex `rlColor4ub`/`rlVertex3f` + `rlEnd()`, building a 64-segment
triangle fan by hand. Zero `DrawModel`/`DrawMesh` calls anywhere in the
file. This matches the "raw immediate mode, no explicit shader bound"
description exactly.

**Fix applied** (`maps/soft_test_ground/soft_test_ground.c`,
`maps/soft_test_ground/soft_test_ground.h`): replaced the immediate-mode
fan with a real mesh draw.
- `InitSoftTestGroundMap()` now calls `GenMeshPlane(3610.0f, 3610.0f, 4, 4)`
  + `LoadModelFromMesh()` once, stored in a file-static `Model floorModel`
  (per the Map Agent's "load once in Init" rule). 3610×3610 is a square
  superset of the old radius-1805 circular fan — same footprint, nothing
  near the edges in this deliberately empty test map.
- `DrawSoftTestGroundMap()` now just calls
  `DrawModel(floorModel, center, 1.0f, GetColor(0x22331FFF))` — same
  center `(600, 0, 440)`, same flat color `0x22331F`, same `Y=0.0f`
  elevation.
- Added `UnloadSoftTestGroundMap()` (new, optional per the map lifecycle
  API) that calls `UnloadModel(floorModel)`; not wired into a central
  unload call anywhere else in the engine as of this session, but present
  and correct for whenever map unload hooks are invoked.

**Verification BLOCKED — could not run the autotest this session.** `make`
fails before reaching link, in `core/skill_helper.c`, unrelated to any
`maps/` file:
```
core/skill_helper.c:162:82: error: too few arguments to function call, expected 5, have 4
            VFXLight_Spawn(pos, (Color){ 255, 120, 20, 255 }, 65.0f * scale, 0.5f);
./core/vfx_light.h:34:6: note: 'VFXLight_Spawn' declared here
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime,
```
(9 identical-shape errors, all in `core/skill_helper.c`.) `git status`
shows `core/vfx_light.c`/`core/vfx_light.h` modified but uncommitted —
another agent's in-flight signature change to `VFXLight_Spawn` (added a
5th parameter) that `core/skill_helper.c`'s call sites haven't been
updated for yet. Confirmed this is not a transient/shared-object-file
race: retried `make` twice, several seconds apart, identical error both
times, and `core/vfx_light.*`'s modified state never changed in between.
Compiled `maps/soft_test_ground/soft_test_ground.c` in isolation via
`make -f CMakeFiles/wuxing.dir/build.make CMakeFiles/wuxing.dir/maps/soft_test_ground/soft_test_ground.c.o`
to confirm the new code itself is not the problem — it compiles clean on
its own. This is squarely a `core/` module concern (Core Agent's
`VFXLight_Spawn` call-site migration), out of Map Agent scope to fix.

**Net status:** hypothesis confirmed, fix applied and compiles cleanly in
isolation, but **not yet verified end-to-end** — no `[CORE_TEST SOFT]`
numeric readback or `[AUTOTEST]` result was obtained this session because
the full binary wouldn't link. Whoever resumes: once `core/skill_helper.c`'s
`VFXLight_Spawn` call sites are updated to match the new 5-arg signature
(Core Agent's job), rebuild and run
`WUXING_AUTOTEST=1 ./wuxing 2>&1 | grep -E "\[AUTOTEST\]|CORE_TEST SOFT"`
— watch for real `scene=X frag=Y diff=Z` numbers on the "bottom" sample
(not "off-screen or invalid"; that's a separate, still-unresolved
projection/`GetWorldToScreen` bug in the autotest harness itself, flagged
in an earlier session, NOT fixed by this floor change and NOT expected to
be fixed by it — don't conflate the two). If the bottom sample still comes
back off-screen even after this fix, that confounder is still blocking
the autotest signal and a real PASS/FAIL there says nothing either way;
check the raw `[CORE_TEST SOFT]` log lines directly as instructed in
Item 3's test infrastructure section above.

---

## Item 11 — `water_sphere_skill.c` never sets `matModel`, unlike its sibling raw-`BeginShaderMode()` skills — RESOLVED (Skills Agent)

Found while chasing Item 10's `water_sphere.fs` follow-up: even after
fixing `u_lightDir` wiring and adding real diffuse, the user still saw
the sphere's bottom brighter than its top (opposite of what a
downward-pointing sun should produce). Investigating the actual draw
call (`DrawWaterSphereSkill` in `water_sphere_skill.c`) found this
skill **never sets `matModel` at all** — no `SetShaderValueMatrix` call
anywhere, no `GetShaderLocation(shader, "matModel")` fetch.

**Why this matters:** `water_sphere.vs` computes `fragNormal =
normalize(vec3(matModel * vec4(vertexNormal, 0.0)))` directly (not via
`VS_FinalOutput()`, but the same math). `DrawCoreSphere()` draws via raw
rlgl immediate mode, which Raylib does **not** auto-upload `matModel`
for (only `DrawMesh`/`DrawModel` get that automatically) — so the GLSL
uniform sits at its zero-initialized default, a zero matrix, and
`normalize(vec3(0))` is NaN. This is the exact Rule D pattern already
documented in this file's Android shader pipeline notes, and exactly
what `SkillManager_BeginShader()` auto-fixes for skills that use it —
but this skill bypasses that entirely via raw `BeginShaderMode()`.

**Inconsistent with its sibling skills:**
- `tube_skill.c` *does* attempt a `matModel` fix, but via
  `tubeShader.locs[SHADER_LOC_MATRIX_MODEL] >= 0`, which `CORE_API.md`'s
  own bug note already flags as the **wrong** check (raylib never
  populates that array slot for a name not in its fixed default-uniform
  list, so it stays `0` from `RL_CALLOC` — `0` passes `>= 0` but isn't a
  real location, risking silently overwriting whatever uniform actually
  lives at GL location 0, e.g. a `sampler2D texture0`). Whether this
  produces a *visible* bug in `tube.fs` specifically hasn't been
  checked.
- `stone_prison_skill.c` also never sets `matModel` — same gap as
  `water_sphere_skill.c`, not yet confirmed whether it's visible there
  too.

### Session 4 (Skills Agent) — confirmed via shader-math reasoning, fixed in all 3 affected skills

**Root cause confirmed per-file (no build/visual access used — reasoned
directly from the GLSL):**
- `skills/water/water_sphere_skill/water_sphere.vs:26` —
  `fragNormal = normalize(vec3(matModel * vec4(vertexNormal, 0.0)))`.
  `matModel` is declared as a plain `uniform mat4` (not routed through
  `VS_FinalOutput()`), and `DrawWaterSphereSkill()` drew via raw
  `BeginShaderMode()` + `DrawCoreSphere()` (rlgl immediate mode) with zero
  `matModel` upload anywhere. Raylib only auto-uploads `matModel` for
  `DrawMesh`/`DrawModel`, so the uniform sat at its zero-initialized
  default (`RL_CALLOC`'d zero matrix) → `vec3(mat4(0) * vec4(n,0))` = zero
  vector → `normalize(vec3(0))` = `0/0` = NaN for every fragment's normal.
  Confirmed real, not speculative.
- `skills/earth/stone_prison_skill/stone_prison.vs:28` — identical pattern,
  identical bug: `fragNormal = normalize(vec3(matModel * vec4(vertexNormal,
  0.0)))`, drawn via raw `BeginShaderMode(s_shader)` +
  `rlBegin(RL_QUADS)`/`DrawPerturbedPillar()` (rlgl immediate mode), no
  `matModel` upload anywhere in `stone_prison_skill.c`. Same NaN-normal
  risk, now confirmed present (was previously only suspected).
- `skills/water/water_stream/tube_skill.c` — `tube.vs` uses
  `VS_FinalOutput()` (`core/shaders/common/vs_header.glsl`), which also
  computes `fragNormal` from `matModel` the same way, so `matModel`
  *does* matter for this skill's lighting (not a case where it's unused).
  The existing guard `tubeShader.locs[SHADER_LOC_MATRIX_MODEL] >= 0` is
  confirmed **broken**, per `CORE_API.md`'s own documented bug (fixed
  2026-06-30, root-caused via `tsunami_skill`): raylib's `locs[]` array
  only gets populated for its fixed default-uniform names; `matModel` is
  not one of them, so that slot stays `0` from `RL_CALLOC` — `0 >= 0`
  always passes even though `0` isn't `matModel`'s real GL location. Two
  compounding effects: (1) the real `matModel` uniform is never written,
  same NaN-normal risk as the other two skills; (2) `SetShaderValueMatrix`
  writes an identity matrix into whatever uniform actually happens to link
  to GL location `0` in `tube`'s program — `tube.fs` declares no
  `sampler2D`/`texture0` (so no texture-binding corruption like
  `tsunami_skill`'s confirmed case), but location 0 is still some other
  real uniform (e.g. `u_time`/`u_uvLength`, whichever the GLSL linker
  assigned first) that gets silently stomped every frame. Not "harmless by
  luck" — actively wrong on both ends, just with cosmetic-looking rather
  than texture-breaking symptoms so far.

**Fix applied (same pattern in all 3 files) — replaced/added
`SkillManager_BeginShader(shader)` right after `BeginShaderMode(shader)`,
and `SkillManager_EndShader()` right before/after the matching
`EndShaderMode()`, per the proven pattern already in
`skills/taiji/core_test/core_test_skill.c`:**
- `skills/water/water_sphere_skill/water_sphere_skill.c`:
  `DrawWaterSphereSkill()` — added `SkillManager_BeginShader(s_sphereShader)`
  after `BeginShaderMode(s_sphereShader)`, added
  `SkillManager_EndShader()` before `EndShaderMode()`. `core/skill_manager.h`
  was already included.
- `skills/earth/stone_prison_skill/stone_prison_skill.c`: added
  `#include "core/skill_manager.h"`; `DrawStonePrisonSkill()` — added
  `SkillManager_BeginShader(s_shader)` after `BeginShaderMode(s_shader)`,
  added `SkillManager_EndShader()` before `EndShaderMode()` (only for the
  main pillar-shader block; the separate crack-decal shader block
  (`s_crackShader`) doesn't use `matModel`-based normals, left untouched).
- `skills/water/water_stream/tube_skill.c`: replaced the
  `tubeShader.locs[SHADER_LOC_MATRIX_MODEL] >= 0` / manual
  `SetShaderValueMatrix` block with `SkillManager_BeginShader(tubeShader)`;
  added matching `SkillManager_EndShader()` before `EndShaderMode()`.

`SkillManager_BeginShader()` (per `CORE_API.md`) looks up `matModel` via
`GetShaderLocation(shader, "matModel")` (real `-1` when absent, safe) and
sets it to identity, plus auto-binds `u_time`/`viewPos`/`u_resolution` —
strictly more correct than, and a superset of, all 3 skills' prior manual
uniform wiring, so nothing regresses.

Not build-verified this session (per task instructions — another process
builds/verifies centrally to avoid a race with parallel work elsewhere in
the repo).

---

## Item 5 — GPU Particle Force Field Integration (`compute/gpu_particle_system.*`, `core/force_field.*`) — PAUSED: particles invisible on Android on BOTH draw paths (CPU/VBO and COMPUTE), root cause still unknown

**Status:** Force field math + GPU registry/pack code is implemented and
believed correct (verified via desktop logic/build, and a passing visual
check on macOS after the immediate-mode rewrite below). The COMPUTE (GPU
dispatch) draw path does not work on the one Mali device tested (vertex-stage
SSBO unsupported) and falls back to CPU/VBO by design — but CPU/VBO particles
are **still invisible on that same Android device** despite three real, fixed
bugs and despite the draw code now matching `core/particle_system.c`'s
`DrawParticles()` (proven working — skill particles render fine there)
almost exactly. Root cause of the remaining Android-only invisibility is
**not found**. Paused here per user request; do not re-guess blindly — see
"Next steps" for what's actually worth trying with real GPU debug tooling.

### What's implemented
- `core/force_field.h/.c`: `ForceField_PackGPU()` signature grew two params
  (`Vector3 axisOrigin, Vector3 axisDir`) to bake `FORCE_RADIAL_AXIS`/
  `FORCE_VORTEX_AXIS` layers' dynamic axis into `origin`/`direction` at pack
  time (previously undocumented as unsupported). Zero existing call sites
  broke — the function had no callers anywhere in the repo before this
  (confirmed by a full clean rebuild), so this was safe dead-code-first work.
- `compute/gpu_particle_system.h/.c`: `GpuParticleConfig` gained
  `forceField`/`axisOrigin`/`axisDir`. Internal pointer→slot registry
  (`MAX_GPU_FORCE_FIELDS 8`, keyed by `ForceField*`) re-packs every frame via
  `ForceField_PackGPU` into a second SSBO (`binding=1`, `ForceFieldBuffer`) —
  used by the COMPUTE path only.
- `compute/shaders/gpu_particles.comp`: full `evalForceField()` GLSL port of
  `ForceField_Evaluate()` (all 8 force types except `FORCE_VISCOSITY`, which
  is intentionally a no-op — `core/particle_system.c`'s CPU particle path
  doesn't call `ForceField_GetViscosityDamping` either, so this keeps
  CPU/GPU parity rather than "fixing" an asymmetry that already exists
  upstream). Formulas cross-checked line-by-line against `core/force_field.c`
  (not guessed) after an earlier draft got the `GRAVITY_POINT`/`VORTEX`
  `1/(dist+1)` falloff and `FORCE_DRAG` semantics wrong.
- `sandbox/vfx_test.c`: touch-accessible "FF TEST" button (screen coords now
  `(70, 400)`, moved off the top-left corner after it turned out to be
  hidden behind the pre-existing "[X] ẩn bảng điều khiển" debug-panel toggle)
  spawns a 40-particle burst with a real `FORCE_VORTEX` field attached — the
  only live caller of `GpuParticleSystem_Spawn()` in the whole repo.
- `Makefile.Android`: fixed two **pre-existing, unrelated** breakages
  surfaced only because this was the first real Android build/deploy attempt
  exercised in this repo this session:
  - `PROJECT_SOURCE_FILES` was missing `core/metaball_fx.c` and
    `entities/entities.c` (both present in `CMakeLists.txt`, both referenced
    by `main.c`/`sandbox_core.c` — desktop linked fine, Android didn't).
  - `generate_apk_keystore`'s `keytool` (JDK 17, defaults to PKCS12) and
    `apksigner` (runs under whatever `java` is on `PATH` — JDK 8 Corretto on
    this machine) disagreed on keystore format → `apksigner` failed with
    "Invalid keystore format" on every build. Fixed by forcing
    `-storetype JKS` on the `keytool` call.

### Confirmed, fixed root causes (found via real device logcat/screencap, not guessing)
1. **`DrawSandboxTouchControls` (joystick/dash/jump/fly/cam) invisible on
   Android** — unrelated to particles, found while trying to reach the FF
   TEST button. Root cause: manual `rlMatrixMode`/`rlPushMatrix`/`rlOrtho`
   2D-overlay technique instead of raylib's `BeginMode2D()`. Fixed in
   `sandbox/sandbox_core.c` by swapping to `BeginMode2D(screenOverlayCamera)`/
   `EndMode2D()` — confirmed visually fixed via `adb exec-out screencap`.
2. **GPU compute draw shader (`gpu_particles_ssbo.vs`) fails to compile on
   this Mali device**: `SHADER: [ID 9] Compile error: 0:17: S0059: Expected
   layout qualifier identifier, got 'std430'` — the compute dispatch shader
   (`gpu_particles.comp`, also SSBO-based) compiles fine, only the
   **vertex-stage** SSBO read fails. Likely `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS
   == 0` on this Mali driver (a known real-world GLES limitation, not a
   syntax bug — pulled the actual packaged shader out of the built APK and
   confirmed `#version 310 es` + `layout(std430,...)` is byte-identical to
   source, ruling out the GLES conversion script). Added a runtime fallback
   in `GpuParticleSystem_Init()`: if `s_draw_shader_gpu.id == 0` after load,
   tear down the compute-path GL resources and fall through to `cpu_path:`
   instead of leaving `s_use_compute=true` with a broken shader.
   **CORRECTION (see "Session 2" below): this hypothesis was wrong.** The
   packaged asset file was correctly `#version 310 es`, but a *separate*
   runtime step (`ShaderPreprocessor_Load()` → `RewriteVersionForGLES()` in
   `core/shader_preprocessor.c`, which every `ResourceManager_LoadShader()`
   call goes through) unconditionally rewrote `#version 310 es` down to
   `#version 300 es` **after** the file was read from disk/APK — invisible to
   a byte-compare of the packaged asset, which is why it wasn't caught then.
   Not a hardware/driver limitation at all. Fixed in Session 2.
3. **CPU/VBO path's original hand-rolled VAO/VBO/shader draw
   (`SetupCpuVAO` + `gpu_particles_vbo.vs` + `rlDrawVertexArray`) never
   rendered anything, on any platform** — this code path had never been
   exercised before (no caller existed until the FF TEST button above).
   Two real bugs were found and fixed in it (vertex-color attribute location
   guessed as `2`, actually `3` per raylib's fixed `vertexPosition/TexCoord/
   Normal/Color→0/1/2/3` binding, confirmed by reading back
   `shader.locs[SHADER_LOC_VERTEX_COLOR]` at runtime instead of guessing;
   missing `mvp` uniform, never set because `rlDrawVertexArray()` bypasses
   raylib's render-batch pipeline that normally uploads it automatically) —
   but particles **still didn't render on Android** after both fixes, and
   confirmed later still didn't render on **macOS either** at that point.
   Abandoned this whole approach rather than keep patching it — rewrote the
   CPU/VBO draw to use `rlBegin(RL_QUADS)`/`rlVertex3f()`/`rlColor4ub()`
   immediate-mode, identical in structure to `core/particle_system.c`'s
   `DrawParticles()` (proven working — skill particles render fine on the
   same Android device). This fixed it **on macOS** (confirmed by the user:
   particles visible, flying straight — correct, since CPU/VBO intentionally
   doesn't apply force field, only the COMPUTE path does).
4. **Applied the `ANDROID_NOTICES.md` §A "Geometry Batch Limit" fix** (single
   `rlBegin(RL_QUADS)` wrapping the whole particle loop → per-particle
   `rlBegin`/`rlEnd`, matching `DrawParticles()`'s exact pattern, since the
   doc explicitly warns mobile GPU drivers silently corrupt/drop an
   oversized `rlBegin`/`rlEnd` batch where desktop GL tolerates it) — did
   **not** fix the Android invisibility (see open question below). Kept the
   fix anyway since it's still correct/safer and matches the documented
   convention, even though it wasn't the actual cause here.

### Open question — not resolved, do not re-guess without real GPU tooling
After fix #3+#4 above, the CPU/VBO draw code is now near-byte-identical to
`DrawParticles()`: same `rlBegin(RL_QUADS)`/`rlColor4ub`/`rlTexCoord2f`/
`rlVertex3f`/`rlEnd()` per-particle pattern, same texture (`globalParticleTex`),
same blend mode (`BLEND_ADDITIVE`), same camera, called back-to-back in
`main.c`. Confirmed via logcat that `GpuParticleSystem_Draw()` is reached
every frame with valid data (`Pool: 80/8192 active`, particle position near
camera, color `(0.31,0.78,1.00)` non-zero alpha) — yet **on the tested Android
device only**, nothing appears on screen, while the same code renders
correctly on macOS and while `DrawParticles()`'s particles are confirmed
visible on the same Android device for every skill tested. No further
difference between the two draw functions was found by code reading.

**What's been ruled out:** shader compile errors (none — CPU/VBO path uses
raylib's default shader now, no custom shader at all), attribute binding
(N/A now — immediate mode doesn't use custom VAOs), `mvp` uniform (N/A —
raylib's batch pipeline handles it), geometry batch overflow (fixed, no
change), spawn logic (`Pool` count increments correctly), GL errors in
logcat (none present).

**What's NOT been ruled out / worth trying next, in likely-usefulness order:**
1. **Numeric, not visual, verification** — per Item 3's process note above,
   screenshot-based "is it visible" checks have a history of false negatives
   in this codebase. Before assuming the draw is truly failing, do a raw
   pixel readback (`glReadPixels` at the particle's known screen-projected
   position, or a debug `DrawText` of `GetWorldToScreen(particlePos, camera)`
   to confirm the projected coordinate is actually on-screen and not e.g.
   behind the player mesh / outside the viewport due to a camera/aspect
   difference between the two screenshot sessions).
2. **Depth test/mask state**: `rlDisableDepthMask()` is called before both
   `DrawParticles()` and `GpuParticleSystem_Draw()` in `main.c`, but confirm
   `GL_DEPTH_TEST` itself (not just depth mask) isn't somehow re-enabled
   between the two calls on this specific device/driver — Item 3 above hit
   exactly this class of bug (depth test vs. depth mask are independent
   states) for an unrelated feature.
3. **Real GPU debug tooling** (RenderDoc for Android, or Arm Mobile Studio /
   Mali Graphics Debugger, since this is confirmed to be a Mali GPU via
   `GL_VERSION` string `v1.r32p1-...`) to actually capture the frame and see
   whether the draw call executes with zero-size/degenerate geometry, is
   clipped, or is being drawn and then immediately overpainted by something
   later in the frame. This is likely the fastest real path to a root cause
   — everything crash/log/screenshot-based has been exhausted.
4. Try a single, large (radius ~60+), fully opaque, non-additive-blend test
   particle at a fixed known-visible world position (e.g. directly above the
   player, matching an existing skill effect's exact spawn pattern) to rule
   out a scale/blend/visibility issue rather than a "nothing draws at all"
   issue.

### Files touched this session (for whoever resumes)
`core/force_field.h`, `core/force_field.c`, `compute/gpu_particle_system.h`,
`compute/gpu_particle_system.c`, `compute/shaders/gpu_particles.comp`,
`compute/shaders/gpu_particles_ssbo.vs`, `sandbox/vfx_test.c`,
`sandbox/sandbox_core.c`, `Makefile.Android`. All build clean (desktop +
Android), no regressions in existing skills/systems observed.

---

### Session 2 — added `FORCE_VECTOR_TEXTURE`, fixed the COMPUTE-path-never-compiles bug, hit the SAME invisibility bug on COMPUTE too — paused again per user request

**New feature (unrelated to the bug below, implemented cleanly):** added
`FORCE_VECTOR_TEXTURE` — particles sample a world-space flow texture instead
of a procedural formula (for geometry-authored fields: smoke hugging a wall,
fire wrapping a body). No `ForceLayer`/SSBO layout change — reuses existing
fields (`origin.xz`/`direction.xz` = sample-box center/half-extent,
`noiseScale` = texture slot 0/1). CPU path (`ForceField_Evaluate`) treats it
as a documented no-op, same precedent as `FORCE_VISCOSITY` — GPU-only by
design. New `GpuParticleSystem_SetVectorFieldTexture(slot, tex)` binds the
texture to a unit right before compute dispatch via raw
`rlActiveTextureSlot`/`rlEnableTexture` (not `SetShaderValueTexture`, since
the compute program isn't a raylib `Shader` struct — this is a self-contained
raw-GL block with no raylib draw call interleaved, so the slot-conflict class
of bug documented elsewhere in this file for that API pair shouldn't apply,
but this was **not** confirmed on real hardware — see below). Full design in
`CORE_API.md` §5 and `COMPUTE_API.md` §3. Added a `sandbox/vfx_test.c` "VF
TEST" touch button to test it, following the existing "FF TEST" pattern.

**Bug #1 — found and FIXED:** the `RewriteVersionForGLES` hypothesis
correction noted above (root cause item 2). `core/shader_preprocessor.c`
unconditionally downgraded any `#version 310 es` to `#version 300 es` at
runtime load, breaking `gpu_particles_ssbo.vs`'s SSBO syntax (`std430`,
`binding`, `readonly` are ES-3.1-only, invalid in 300 es). Fix: only
downgrade when the shader does **not** contain `std430` (i.e. genuinely
"accidentally included" 310 header text, the scenario the rewrite was
originally meant for — real SSBO shaders keep 310 es). **Confirmed fixed on
device via logcat**: `gpu_particles_ssbo.vs` now compiles, `GPU_PARTICLES:
COMPUTE path active (8192 particles)` — the fallback to CPU/VBO in
`GpuParticleSystem_Init()` (root cause item 2's workaround) no longer
triggers on this device at all.

**Bug #2 — hit immediately after, NOT fixed, PAUSED again:** with COMPUTE
path now genuinely active (not falling back), particles are **still
completely invisible** on the same Android device (Mali, `GL_VERSION`
`OpenGL ES 3.2 v1.r32p1-...` — same device as the original CPU/VBO
investigation above). Confirmed via the "VF TEST"/"FF TEST" buttons: `Pool:
N/8192` count increments correctly on every press (spawn + touch input both
verified working, including a real UI bug found and fixed along the way —
"VF TEST" button's Y position landed inside the virtual joystick's activation
radius on-device despite looking clear in the desktop preview; moved above
"FF TEST" instead of below), draw path is reached, but zero visible particles
on screen — for **both** buttons, i.e. both `FORCE_VECTOR_TEXTURE` (new) and
plain `FORCE_VORTEX` (pre-existing, previously never actually confirmed
visible on this device either, only assumed once compute path was expected
to activate).

**Why this matters for root-causing:** this is a genuinely new data point
the original investigation (root cause item 4 / "Open question" above) did
not have — it only ever tested CPU/VBO immediate-mode draw. Now that the
*entirely different* COMPUTE draw path (SSBO + vertex-shader billboard
construction via `gl_VertexID`, no `rlBegin`/`rlEnd` at all) exhibits the
**same symptom**, whatever is hiding the particles is almost certainly
**not** specific to either draw method (rules out anything about
immediate-mode batching, VAO/VBO attribute binding, or the SSBO vertex-stage
support question) — it's something common to both: camera/projection,
depth/culling state, or particle world position ending up somewhere
off-screen/occluded for both paths alike. Points more strongly at "Not been
ruled out" items 1–2 in the original investigation (numeric
`GetWorldToScreen` / depth-test-vs-depth-mask check) than at anything
draw-method-specific.

**Stopped here per explicit user instruction** ("thử sửa 1 lần, không được
thì ngưng" — try one fix attempt, stop if it doesn't work) rather than
continuing to guess. `FORCE_VECTOR_TEXTURE` itself is implemented, documented,
and builds clean on both desktop and Android — but is **unverified on real
hardware** and will remain so until this pre-existing invisibility bug is
resolved. Do not re-attempt blindly; needs the numeric/GPU-tooling
verification already prescribed in the original "what's NOT been ruled out"
list.

---

## Item 12 — VFX Light / Trail pools have no priority-based eviction — RESOLVED

Built: `VFXPriority` enum (`VFX_PRIORITY_LOW` / `VFX_PRIORITY_HIGH_ULTIMATE`)
in `core/vfx_light.h`. `VFXLight_Spawn()` gained a required `priority` param
(**breaking signature change** — 7 existing call sites in
`skills/water/water_sphere_skill/water_sphere_skill.c`,
`skills/wood/wood_thorns/wood_thorns_skill.c`,
`skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba_skill.c`,
`skills/earth/stone_prison_skill/stone_prison_skill.c` need a Skills-Agent
follow-up to add the new arg). `core/trail_system.h`'s `TrailConfig` gained
an additive `priority` field (backward compatible, defaults to
`VFX_PRIORITY_LOW` via `{0}` init — no existing `TrailConfig` call site
found in `skills/` at the time of this change). Both `VFXLight_Spawn` and
`SpawnTrailEntity` now scan the full-pool case for the lowest-priority slot
(ties broken by shortest remaining lifetime) and evict it instead of
rejecting the new spawn, as long as the victim's priority is `<=` the
incoming one — an equal-or-lower priority spawn can still lose to a fully
saturated higher-priority pool, same as the old silent-reject behavior in
that case. See `CORE_API.md`'s "VFX Lights" and "Trail & Ribbon" sections.

Raised by an external code review of `CORE_API.md`, verified against the actual
headers before filing. Confirmed: `core/vfx_light.h:7`
(`MAX_VFX_LIGHTS 16`) and `core/trail_system.h:10`
(`MAX_TRAIL_PARTICLES 500`) are flat static pools shared by every skill/caster
in the scene, with no priority field and no eviction logic anywhere in either
header. `VFXLight_Spawn()`/`SpawnTrailEntity()` simply no-op/return `-1` once
full.

**Why it matters:** `nguhanhtyvo_kehoach.md` (§XI, the PvP MVP phase) confirms
this project is headed toward concurrent multi-caster PvP (1v1 now, 2v2/3v3
planned). An Ultimate-tier skill cast can land on a pool already saturated by
smaller concurrent skills and silently lose its entire VFX presentation — no
crash, just missing lights/trails, which undercuts `WUXING_ART_DIRECTION.md`'s
intent for Ultimates to read as impactful.

**Suggested API surface (not yet designed/built):**
- Add a priority param/field (e.g. `VFXPriority`: `PRIORITY_LOW` /
  `PRIORITY_HIGH_ULTIMATE`) to `VFXLight_Spawn()` and `TrailConfig`.
- When the pool is full, evict the lowest-priority (or shortest-remaining-life)
  slot instead of rejecting the new spawn outright.

---

## Item 13 — No lifecycle-end signal for gameplay code that depends on a skill's dissolve finishing — NOT STARTED (external review finding, confirmed valid)

Confirmed: `core/skill_manager.h` has no callback/event/`IsExpired`-style API.
A skill's own `active = false;` on dissolve-complete is entirely internal to
its `.c` file — nothing external (`main.c`, `entities/entities.h`) can
currently learn that a specific cast has actually finished dissolving.

**Why it matters:** any gameplay effect that needs to persist exactly as long
as a skill's visual is on screen (e.g. an Earth wall blocking movement, a Wood
root-zone) has no way to know when to release itself except by duplicating
that skill's own timer at the gameplay layer.

**Needs a design decision before implementation**, not just a straight
build — options include (a) a generic `bool Skill_IsInstanceExpired(...)`
poll, or (b) a callback registered at cast time. Core Agent should propose
the shape; flagging here rather than committing to one now.

---

## Item 14 — No interrupt/abort path for a coiling or in-flight skill — RESOLVED (Core-only scope)

Built the Core-only half explicitly, per instruction to avoid the
cross-module lifecycle-contract change: `void RegisterSkillAbort(int
skillIndex, void (*abort)(int agentId));` — an **optional**, additive
registration separate from `RegisterSkill()` (backward compatible; skills
that don't call it simply can't be force-aborted) — and `void
AbortSkill(int skillIndex, int agentId);` which invokes the registered
callback with that `agentId` if present, otherwise logs `LOG_WARNING` and
no-ops. `skills/CLAUDE.md`'s mandatory lifecycle contract
(`Init/Cast/Update/Draw/Unload/IsCoiling/GetProjectiles/DeactivateProjectile`)
is untouched. **Not done, deliberately out of scope:** no skill anywhere
actually calls `RegisterSkillAbort()` yet — that's a per-skill adoption task
for the Skills Agent, one skill at a time, wherever an abort makes gameplay
sense. See `CORE_API.md`'s "Abort / Interrupt" section.

**Session update (confirmed real target scale is 6 players + a mixed
minion/mid-tier/2-boss monster pool, not single-attacker):** upgraded the
callback shape from `void (*abort)(void)` to `void (*abort)(int agentId)` —
done at zero migration cost since there were still zero adopters at the
time (confirmed via grep). A skill that later wants caster-specific abort
(only abort the instance owned by the stunned caster, not every instance of
that skill type) can filter on the `agentId` it receives once it tracks
per-instance ownership internally (still opt-in, still deferred — see Item
15). A skill that ignores the parameter aborts everything, identical to the
original behavior — no forced changes to any existing skill file.

Confirmed via `grep -rn "Abort" skills/ core/ entities/` → zero hits, and
`entities/entities.h` has no stun/interrupt entry point either. Note the
original review claim that cooldown management is "completely empty" is
**wrong** — `Skill_CalculateCooldown()` already exists at
`core/skill_manager.h:57` — but the **interrupt** half of the claim holds: if
a caster is crowd-controlled while `Is[Name]SkillCoiling()` is `true`, nothing
can force that instance to skip straight to its dissolve state; it always
finishes charging and fires.

**Suggested API surface (not yet designed/built):** a generic
`Skill_AbortInstance(...)` in `core/skill_manager.h`, or a per-skill
`Abort[Name]Skill(void)` added to the required lifecycle contract in
`skills/CLAUDE.md`. The latter is **cross-module** — it changes the mandatory
signature every skill file implements, so it needs Skills Agent sign-off, not
just a Core-side change.

---

## Item 15 — No caster/owner identity on skill instances (multiplayer state) — RESOLVED

**Session update:** confirmed real target scale is 6 real players (peer-hosted
networking, separate ceiling from anything below) + a mixed monster pool
(minions + a few mid-tier + 2 bosses) — not a hypothetical. Also confirmed
`entities/entities.h` already has the caster-identity primitive this item was
asking for (`agentId`, `Entity_SpawnAgent`/`Entity_GetAgent`), and
`PlayerEntity`/`EnemyEntity` (`sandbox/sandbox_core.h`) already carry an
`agentId` field. `entities/entities.h`'s `MAX_AGENTS` bumped `8 -> 256` to
actually fit the target scale (static array, ~20KB, negligible cost).

`CastSkill()` takes `agentId` as its 2nd parameter (all 3 call sites —
`main.c:259`, `sandbox_core.c:400,409` — updated to pass
`player`/`enemy .agentId`), and Item 14/16's APIs are keyed by
`(skillIndex, agentId)` — see their sections above.

**Full per-instance completion (this session, follow-up pass):**
`skills/CLAUDE.md`'s required lifecycle contract changed —
`Cast[Name]Skill` now takes a leading `int agentId`, forwarded all the way
from `CastSkill()` through `RegisterSkill()`'s `cast` function-pointer type.
Every skill in the repo migrated to the new contract and now stores
`int ownerAgentId;` on its own per-instance struct, set at cast/allocation
time:
- `skills/taiji/core_test/core_test_skill.c` (`s_ownerAgentId`, single-instance)
- `skills/fire/fire_ball/fire_skill.c` (`FireEmitter.ownerAgentId`) +
  `skills/water/water_stream/tube_skill.c` (`TubeEmitter.ownerAgentId`) — the
  2 legacy skills, threaded through `core/skill_manager.c`'s
  `CastFireWrapper`/`CastTubeWrapper` adapters (orchestrator-edited, Core's
  file, not Skills')
- `skills/earth/stone_prison_skill/stone_prison_skill.c` (`EarthPrison.ownerAgentId`)
- `skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba_skill.c` (`HoaLongPhongBaVortex.ownerAgentId`)
- `skills/water/water_sphere_skill/water_sphere_skill.c` (`WaterSphere.ownerAgentId`)
- `skills/wood/wood_thorns/wood_thorns_skill.c` (`Thorn.ownerAgentId`)

Done as one coordinated pass (core contract change + 3 parallel Skills Agent
sessions, 2 skills each) rather than the earlier-considered "hide all
skills, migrate later" approach — turned out unnecessary once the actual
technical requirement was understood precisely; zero skills were ever
excluded from the build. Verified via `make` (clean, first try, no
patch-up needed) and `WUXING_AUTOTEST=1 ./wuxing` (no regressions in
Item 12/14/16/17's autotest cases).

**Bonus finds during the pass:** `wood_thorns_skill.c` and
`hoa_long_phong_ba_skill.c` both had the *exact same* NaN-normal `matModel`
bug as Item 11 (raw `BeginShaderMode()` immediate-mode draw, never uploads
`matModel`) — neither was in Item 11's original fix batch. Both fixed the
same way (`SkillManager_BeginShader`/`EndShader`).

**Minor gap surfaced during the pass — FIXED:** all 3 migration sessions
independently found the same thing — no skill had any way to learn its own
registered `skillIndex` (`RegisterSkill()`'s return value wasn't captured
anywhere), which is why none of the 6 migrated skills adopted Item 16's
`SkillManager_CanCast`/`TriggerCooldown` in that pass. Added
`int Skill_GetIndexByName(const char *name)` to `core/skill_manager.h`/`.c`
(reverse lookup by name, `-1` if not found) — a skill can now call this once
in `Init[Name]Skill`, cache the result, and use it to actually call
`SkillManager_CanCast`/`TriggerCooldown` about itself. See `CORE_API.md`'s
"Cooldown / Resource Gating State" section. **Still not done:** no skill
actually calls it yet — real cooldown adoption across the 7 skills remains
a separate follow-up task, now technically unblocked.

---

<details>
<summary>Original filing (before the session update above)</summary>


External review correctly flagged that no skill instance struct anywhere
carries a caster/owner id (spot-checked `Thorn` in
`skills/wood/wood_thorns/wood_thorns_skill.c`) and that `nguhanhtyvo_kehoach.md`
(§XI) confirms this is a genuine peer-hosted PvP project (1v1 now, 2v2/3v3
planned) — so this is a real architectural question, not a hypothetical.

**However, the review's specific failure mechanism is wrong**: static pool
slots don't cross-contaminate between casters (each `FindFreeSlot()`-style
allocation is independent) — the actual risk when the pool is shared and
fills up is silent pool **exhaustion**, i.e. the same root issue already
filed as Item 12, not state overwrite.

**Why this isn't filed as a normal to-do:** the proposed fix (adding
`casterId`/`CasterContext` to every skill instance) implies changing the
mandatory `Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius)`
signature that **every** skill file must implement per `skills/CLAUDE.md` —
a cross-module contract change with the widest blast radius of anything in
this review, not something Core Agent can decide or build alone. Recording it
here as an open question for a Core+Skills design discussion, not a
ready-to-implement item.

</details>

---

## Item 16 — No cooldown/resource *gating* state, only calculation — RESOLVED (per-agent scope)

Built `bool SkillManager_CanCast(int skillIndex, int agentId)` and `void
SkillManager_TriggerCooldown(int skillIndex, int agentId, float
cooldownSeconds)` in `core/skill_manager.h`/`.c`. Ticks down automatically
inside `UpdateSkillManager(dt, ...)`, same idiom as the existing `slowTimer`/
`rootTimer` fields. **Session update:** originally keyed only by
`skillIndex` (single-caster scope, see below) — upgraded to key by
`(skillIndex, agentId)` once the real target scale (6 players + a mixed
monster pool, not one attacker) was confirmed and `entities/entities.h`'s
existing `agentId` primitive was found (see Item 15). Internal storage is
now a static `float[MAX_SKILLS][256]` table (256 duplicated from
`entities/entities.h`'s `MAX_AGENTS`, since core/ must not `#include
entities/` — same fragile-coupling pattern as Item 3's
`SOFT_PARTICLE_SCENE_NEAR`/`_FAR`, flag if `MAX_AGENTS` ever changes).
Verified via `skills/taiji/core_test/core_test_skill.c`'s
`skill_cooldown_gating` autotest case, which now specifically asserts that
triggering agent 0's cooldown does **not** affect agent 1's `CanCast` result
— the actual bug this upgrade fixes. **Still not done:**
`SkillManager_TriggerCooldown` is not wired into any skill's
`Cast[Name]Skill` call site — nobody calls it yet, that's a Skills Agent
call-site adoption task, unchanged from before. See `CORE_API.md`'s
"Cooldown / Resource Gating State" section.

<details>
<summary>Original filing (single-caster scope, before the session update above)</summary>

Deliberately keyed only by `skillIndex`, NOT per-caster — per instruction,
since there was no caster/owner identity concept anywhere in this engine yet
(that was Item 15, an open cross-module design question, explicitly out of
scope at the time). This meant it was one global cooldown timer per skill
slot shared by whoever cast it — correct for the assumed single-attacker
setup at the time, needing a caster-keyed table once Item 15 was resolved
(now done, see above).

`core/skill_manager.h:57-59` already has `Skill_CalculateCooldown()` and
`Skill_CalculateManaCost()` — so the review's claim that cooldown/mana is
"completely absent" is **wrong**. What's actually missing, confirmed by
grep, is any function that holds or checks *state*: no `SkillManager_CanCast()`,
no `SkillManager_TriggerCooldown()`, nothing that remembers "this caster used
this skill N seconds ago." The Calculate* functions return a value; nothing
persists or enforces it. A skill can currently be cast back-to-back with zero
gating even though the numbers to prevent that already exist.

**Suggested API surface (not yet designed/built):** something like
`bool SkillManager_CanCast(int skillIndex, int casterId)` /
`void SkillManager_TriggerCooldown(int skillIndex, int casterId, float cooldown)`.
Note this likely wants a caster/owner identity to key the cooldown table per
caster — ties into the open design question in Item 15, not fully independent
of it.

</details>

---

## Item 17 — No debug-draw API for tuning hitboxes/AoE radii visually — RESOLVED

Built new `core/debug_draw.h`/`.c`: `DebugDraw_Sphere(Vector3 pos, float
radius, Color color)` and `DebugDraw_Circle(Vector3 center, float radius,
Color color)` (ground-plane, for AoE shapes), wireframe only, backed by
raylib's `DrawSphereWires`/`DrawCircle3D` directly (exempt from skills'
"no raylib primitives" rule — this is core-internal dev tooling, not a
shipped VFX mesh). Gated behind `DebugDraw_SetEnabled(bool)` /
`DebugDraw_IsEnabled(void)`, default **disabled** — all `Draw*` calls no-op
when disabled so call sites can leave them in unconditionally. No
`core/procedural_mesh_utils.h` wireframe helpers existed to reuse (checked
first, confirmed only solid-mesh draws there). See `CORE_API.md`'s "Debug
Draw" section.

---

**Not filed (reviewed and rejected):**
- *"`PathSpline_CalculateLength` helper missing"* — premise doesn't hold
  against current code. Checked both real `u_uvLength` call sites
  (`skills/water/water_stream/tube_skill.c:255`,
  `skills/fire/hoa_long_phong_ba_skill/hoa_long_phong_ba_skill.c:578`) — both
  use a fixed constant, neither has the manual arc-length loop the review
  describes. No actual duplication to fix.
- *"`GetRandomValue` called inside `Draw()`"* — false. Checked every
  `Draw*()` function body across all skills with a `GetRandomValue` call
  (46 call sites total); none are inside a `Draw` function. Jitter is
  already generated once at cast/spawn time and stored on the instance
  struct, per `WUXING_ART_DIRECTION.md`'s anti-robotic rule. Already correct,
  no action needed.
- *"Cancel/Interrupt API missing"* — same gap as Item 14 above (missing
  `Abort[Name]Skill`/interrupt path); not filed twice.
- *"`Entity_GetNearbyTargets` undocumented — only a comment mention"* — false.
  Full signature, semantics, and usage are documented in `ENTITIES_API.md:112`
  §7 (and its AoE-composition callers in §8/§9). Correctly owned/documented by
  the Entities Agent, not a `CORE_API.md` gap — the reviewer only checked
  `CORE_API.md`.
- *"No terrain-height query API / flat-ground assumption undocumented"* —
  false. `MAP_API.md:294` explicitly documents the default arena as flat at
  `Y = 0.0f`, and `MAP_API.md:450` already ships a `GetHeightmapHeight()`
  helper for non-flat terrain. Map Agent's territory, already handled.
- *"No consolidated performance-budget table"* — minor point, not filed as an
  issue. Every individual pool already documents its own limit and overflow
  behavior inline (e.g. `CORE_API.md:778` trail pool, `:857` `MAX_DECALS`) —
  behavior is consistent (silent no-op / return `-1`) and already spelled out
  per system, just not tabulated in one place. Cosmetic, low value; not worth
  a tracked item unless requested.
- *"Self-acknowledged gaps (SFX assets, `Material_Load` broken preset paths,
  `Entity_ApplyAoEBuff` no team filter) should be tracked as TODOs"* — they
  already are, inline: `CORE_API.md:1580` (SFX), `CORE_API.md:1756` (`Material_Load`
  paths, explicitly marked "known pre-existing issue, out of scope"), and
  `CORE_API.md:649`/`ENTITIES_API.md` §9 (AoEBuff team filter). Nothing new to
  add; the last one is Entities Agent's territory, not Core's.
- *"Skill metadata registry for UI (name/icon/description/unlock)"* — false
  premise. `nguhanhtyvo_kehoach.md:12` explicitly states the game is designed
  to need **no UI or text at all** — wuxing interactions are read entirely
  through in-world visual cues. Building this would contradict the design
  doc, not fill a gap in it.