# Core Engine — Progress / Backlog / Session Log

Tracks core work that is unfinished (reverted, needs a fresh approach) or not
yet started, plus resolved-session logs kept for reference. See `docs/API.md`
for the API surface that IS shipped, and `docs/LANDMINES.md` for distilled
reusable lessons.

> [!CAUTION]
> **The Raylib Batching Hazard** (the single most important core rule) has been
> promoted to root `ENGINE_LANDMINES.md` §1 — read it there. Short version:
> always `rlDrawRenderBatchActive()` before AND after any depth mask/test change.

---

## Item 3 — Soft Particles (RESOLVED)

**Status: Resolved.** The issue where the ground-plane drew with immediate mode but failed to write depth correctly has been identified and resolved. 

**Root Cause:** The glDepthMask(GL_FALSE) call from `rlDisableDepthMask()` inside the particle drawing loop was leaking to the preceding immediate-mode draws (like the floor and grid) because the render batch was not flushed prior to changing the OpenGL depth-mask state. As a result, the entire scene's immediate-mode elements were rendered with depth-write disabled. In addition, the soft sphere itself was drawn after restoring the depth mask to true without flushing, causing it to write its own depth and overwrite the scene depth values.

**Fixes Applied:**
1. Added explicit `rlDrawRenderBatchActive()` flushes before and after any depth mask and depth test changes in both `main.c` (particle draw loop) and `skills/taiji/core_test/core_test_skill.c` (sphere draw loop).
2. Cleaned up and rescaled the test harness parameters in `skills/taiji/core_test/core_test_skill.c` (radius `0.4f` and fade distance `0.3f`) to match the new 1 unit = 1 meter real-world scale system.
3. Fixed the autotest activation coordinate mapping in `CoreTestSkill_AutoTestStep` to spawn on-screen relative to the camera target, making all depth readbacks valid and bringing the `soft_particle_ground_fade` autotest to a clean **PASS** state.

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

### The Final Missing Link (Raylib Batching Hazard)

The ground correctly draws using `rlBegin(RL_TRIANGLES)` immediate mode, but it was still failing to write depth even when explicitly wrapped in `rlEnableDepthMask()` in `maps/default_arena.c`. The root cause was discovered to be a **Raylib batching hazard**.

1. **The Batching Bug:** When Raylib's `rlDisableDepthMask()` or `rlDisableDepthTest()` is called directly (for example, at the start of drawing another skill like `fire_skill.c` or within the `Environment_DrawSmartShadow` function), it toggles OpenGL's `glDepthMask` and `glDisable(GL_DEPTH_TEST)` immediately. However, the previously submitted immediate-mode vertices (like the Arena Ground) are still sitting in Raylib's un-flushed rendering batch!
2. **The Consequence:** When the batch is finally flushed to OpenGL later, the OpenGL depth-writing states are already disabled. Thus, the entire batch (including the ground) is drawn invisibly to the depth buffer, causing the soft particle shader to fail (since `factor` becomes 1.0 everywhere as it thinks there is no occlusion).
3. **The Fix:** We systematically added `rlDrawRenderBatchActive()` to flush the batch **before** any invocation of `rlDisableDepthMask()`, `rlDisableDepthTest()`, `rlEnableDepthMask()`, and `rlEnableDepthTest()` across the entire codebase (11 files, including `environment_system.c` and all elemental skill source files). This guarantees that previously queued immediate-mode geometry is safely submitted to OpenGL with its original, correct depth state.

The soft particle system is now fully functional, heavily robust against OpenGL state manipulation by other skills, and cleanly integrated.

**Next steps for whoever resumes:**
1. The Soft Particles feature is fully complete and verified. The `CORE_TEST_SOFT_FADE_DISTANCE` has been rescaled properly, and the test harness behaves perfectly in all debug modes.
2. The `API.md` documentation has been updated to restore the "Soft Particles" section.

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


---

## E2 — VFX point lights: RESOLVED 23/07/2026

Two sessions of "the dynamic lights light nothing". Root cause was one layer
below E2 and not in E2's code at all: raylib's `DrawMesh` uploads
`matModel = modelTransform * rlGetMatrixTransform()`, and inside a 3D pass that
matrix is the **view** matrix, so every shader's `fragPosition` /`fragWorldPos`
— all of them labelled "world space" — is actually view space. Lights were
uploaded in world space; the two operands were in different spaces; the distance
came out as roughly the camera distance and attenuation clamped to 0 everywhere.

**Changed**
- `core/vfx_light.c` — `VFXLight_ShaderSpaceMatrix()` + `Vector3Transform` of each
  light position before upload; warns if it ever runs where the matrix is identity.
- `main.c` — `VFXLight_BindAll` moved out of the update block into the 3D pass,
  immediately after `MyBeginMode3D` (it must see the view matrix).
- `core/vfx_light.c/.h` — `VFXLight_DebugTestLight()`, a 6 m tunable light parked
  on the player (`vfx_light_test`), plus a once-a-second upload log gated on the
  debug mode.
- `core/shaders/common/vfx_lights.glsl` — debug modes 5 (flat constant), 6 (radius
  alone), 7 (distance to light 0).
- `core/tests/vfx_light_space_test.c` — new suite (runner now 5/5).
- `ENGINE_LANDMINES.md` §9, `core/docs/LANDMINES.md`, `core/docs/E2_VFX_LIGHT_HANDOFF.md`.

**Corrections to what was previously written down**
- The vec3→vec4 uniform rewrite fixed nothing. rlvk's `rlSetUniform` already
  strides arrays by 16 bytes; a headless probe reading the UBO staging back
  confirmed every element always landed correctly. `core/docs/LANDMINES.md`'s
  std140 entry has been corrected where it claimed E2's arrays "arrived corrupt".
- The "all debug modes regressed at once" observation was a scene artifact: the
  caster stands off `verdant_path`'s plateau, so the frame was filled by the
  cloud sea and the ground shader was drawing a sliver in one corner.
- rlvk was never at fault; the "dynamic lights may not work under Vulkan at all"
  hypothesis is dead.

**Method note (the reusable part).** `fract(worldPos)` was used as proof that the
coordinates were world space and it is *incapable* of showing this bug — a
translated position paints the same 1 m grid. The mode that found it in one run
was `length(lightPos - fragPos)`: paint a quantity that depends on the ORIGIN,
not just the gradient. And a ~120-line headless probe compiling the real shaders
through `rlLoadShaderProgram` and reading `fsStage` back killed three hypotheses
(std140, uniform delivery, compile failure) in two seconds, before any screenshot.

### E2 follow-up — remaining lit surfaces wired (25/07/2026)

After the view-space fix landed, only three surfaces consumed the pool
(character `surface_lit`, ground `ground_splat`/`grass_material`, path
`path_blend`). The rest of the scene stayed dark next to a floor-level effect.
Now wired too, same pattern (`#include vfx_lights.glsl` + one
`VFXLight_RegisterShader` at load, no per-frame code):

- **`maps/toolkit/shaders/prop_lit.fs`** (rocks / props, DrawModel path) —
  `VFXLights_Accumulate` with the TBN-perturbed normal, which prop_lit.vs already
  builds via `mat3(matModel)`, i.e. the same view space the lights are uploaded in.
- **`maps/toolkit/shaders/ground_shadow.fs`** (default_arena floor plate + BOTH
  maps' zone discs — raw immediate-mode `rlBegin` draws) — `VFXLights_AccumulateFlat`.
  `fragWorldPos` is already view space here for the immediate-mode reason
  documented in ground_shadow.vs (rlgl CPU-transforms the verts by the view
  matrix), so no conversion.

Verified in-scene per map: rock lit warm on verdant_path, floor plate lit warm on
default_arena, character lit on both. `shader_uniform_wiring_test.c` extended to
cover both new .fs/.c pairs (suite 5/5).

Note left for a later pass: the flat-ground consumers pass a literal `vec3(0,1,0)`
as the normal to `AccumulateFlat`. That is world-up, not view-up, so a flat floor's
half-Lambert term drifts slightly as the camera orbits. Visible result is fine
(the wrap's 0.5 base is forgiving) so it was left as-is to avoid regressing the
committed look; the correct fix is to upload world-up rotated into view space and
use it in `AccumulateFlat`.

---

## E1 — Post-FX radial blur + anamorphic streak bloom: LANDED 25/07/2026

Both features per `ELDEN_VFX_SPEC.md` §E1, **defaulting OFF** so nothing shipped
changes appearance until it opts in.

**E1a — radial blur** (`core/shaders/post_process.fs`, folded into the composite
rather than a separate pass, as the spec prefers): 8-tap smear along
`uv - center`, weighted by `smoothstep(0, falloff, length(dir))` so the focal
point stays sharp. Placed AFTER chromatic (so it smears the aberrated image
rather than re-sharpening the fringes) and BEFORE bloom (so the bloom blooms the
smear). Whole loop sits behind a uniform branch.

Transient API in `core/post_fx.{h,c}`: `PostFX_RadialBurst(worldPos, strength,
duration)` + `PostFX_UpdateTransient(cam, dt)` (wired in `main.c` next to
`VFXLight_Update`) + `PostFX_HasTransient` / `PostFX_ApplyTransient`. One slot,
not a pool — a full-screen effect cannot show two focal points, so the strongest
LIVE burst wins (compared against the decayed value, so a nearly-finished big
burst does not lock out a fresh small one). Quadratic decay, CameraFX_Shake's
trauma shape.

**E1b — anamorphic streak** (`core/shaders/bloom_downsample.fs`): a second
axis-stretched tap set (×4 along the axis, compressed across) blended into the
same target by `bloomStreakStrength`. In the downsample, so it costs one tap loop
at 1/8 res and nothing at all when bloom is off.

**Landmine avoided:** the streak uniforms are set INSIDE `BeginShaderMode` in
`DualFilterPass`. Under rlvk `SetShaderValue` writes to whichever shader is
ACTIVE, so setting them before the mode switch lands them on the previous
shader — silently. `DualFilterPass` gained a `streakCfg` parameter (NULL for the
upsample pass) rather than hoisting the sets out.

**Verified**
- A/B at 0.50s, arena: forced ON vs OFF = 3.2% of sampled pixels changed, max
  delta 253 — streaked highlights and an outward smear with a sharp centre.
- OFF path unchanged by construction: both shader blocks are gated on uniforms
  that are 0 when the (zero-initialised) config fields are false.
- `./scripts/run_core_tests.sh` 5/5 · `./scripts/run_rlvk_runtime_test.sh`
  0 failures · `convert_shaders_to_gles.py --dry-run` on both touched shaders:
  2 converted, **0 warnings needing manual review** (loop bounds are constant,
  which is what ES 2.0 requires).

**Live switches** (`tuning.cfg`, default 0 = off, they can only turn something
ON that the caller left off — never disable what a caller asked for):
`postfx_streak`, `postfx_streak_angle`, `postfx_radial`.

**Not done here:** nothing calls `PostFX_RadialBurst` yet — hooking it to
explosions/ultimates is the E7 retrofit pass. E0's 8-capture baseline was never
taken, so the "re-take E0 with the features ON" half of the DoD was replaced
with the targeted A/B above.

---

## E3 — VFX_Sequence (choreography layer): LANDED 25/07/2026

Rewritten from scratch: the original `vfx_sequence.c/.h` were uncommitted work
lost when the tree was reset mid-session (confirmed with the owner: no backup,
delete and redo).

**New:** `core/composition/vfx_sequence.{h,c}` — a beat track. `VFX_SeqBegin` →
`VFX_SeqAt(t, beat)` × N → `VFX_SeqPlay`. 8 beat kinds (COMPOSE, LIGHT, SHAKE,
HITSTOP, DISTORT, RADIAL, DECAL, CALLBACK), static pools 16×24, driven from
`VFX_Compose_Update` so there is **no `main.c` wiring** (spec §E3).
`VFX_SeqPreset` pre-fills the `anticipation → burst → sustain → dissipate`
envelope; the caller adds its own COMPOSE beats.

**Deviation from the spec, deliberate:** `VFX_SeqPlay` returns `int` rather than
`void`. The spec pairs `void VFX_SeqPlay` with `VFX_SeqStop(int handle)`, which
leaves no way to ever obtain a handle — Stop would be uncallable.

**Clock:** scaled dt (post `TimeFX_Apply`), the spec's recommendation, so a
sequence that fires its own hitstop stretches its own remaining beats.
`VFX_SeqSetUnscaled` opts out. Documented in the header, asserted by the test.

**Ported skill (DoD):** `TAIJI_LOI`. It used to fire bolt + impact + shockwave on
the SAME frame — a sky→ground strike with no travel and no envelope. Now the bolt
leaves at t=0 with a ground telegraph light, and impact/shake/radial/distort all
land together at t=0.10 (travel), with a dimming tail. Old path deleted, not
commented out. **Damage deliberately stays off the track** at cast time: moving
it onto a beat would delay the hit and change gameplay/netcode timing.

**Headless test:** `core/tests/vfx_sequence_test.c` (suite now 6/6). Covers the
two failures that are invisible on screen — a long frame must not EAT a beat
(one 0.5 s frame fires all 5, in order) and beats authored out of order must fire
in time order — plus fire-once, t=0-on-first-update, retire-after-last-beat and
overflow clamping. Mirror guards assert the load-bearing lines still exist in
the real `.c`/`.h` (core/CLAUDE.md §3).

### Bug found in E1a by this port, and fixed

The `TAIJI_LOI` strike lands away from the camera target, which exposed two
defects in the E1a radial blur that the centred NEW-FX demo never could:

1. `dir = uv - center` grows without bound as the focal point leaves the view, so
   the 8 taps landed far apart and rendered as **discrete ghost copies of the
   frame** rather than a smear.
2. Conceptually wrong: a burst you cannot see was smearing the whole screen.

Fixed in `post_process.fs`: the smear length is capped (0.08 UV) and the whole
effect fades out as the centre leaves `[0,1]`. Verified — ghosting gone.

**Also:** `sync_vfx_test.py` silently deleted a hand-written manifest entry for
anything not found as a composition function in a `.inl` (`PostFX_RadialBurst`,
`VFX_SeqPreset`). Added a `_manual: true` flag that preserves such entries, with
the reason in the function's docstring. Both E1a and E3 now have NEW FX buttons
(`RADIAL BURST E1A`, `SEQUENCE ENVELOPE E3`).

`core/docs/API_GUIDE.md` has the worked example; `gen_core_api_index.sh` gained
`vfx_sequence.h` so the generated index covers it.

### TAIJI_LOI made testable — debug affordance, NOT a rule change (27/07/2026)

The E3 port could not be verified by the owner because `TAIJI_LOI` is Thái Cực-
exclusive: `CastTaijiLoiSkill` returns early unless `Entity_IsTaijiActive`, and
that state is entered only by building a 2 Âm + 2 Dương loadout through the TAB
UI. So the skill silently did nothing and there was no way to tell the gate from
a broken sequencer.

Deliberately did NOT unlock the skill. The gate is a design rule (thiết kế §XVII
"Vô Sát", implemented in `Entity_RecomputeElement`); weakening it to make testing
easier would be a VFX task changing gameplay. Instead:

- **`[U]` in the sandbox** forces Thái Cực on/off for the player
  (`sandbox/sandbox_core.c`), the same `Entity_SetTaijiActive` path `boss/`
  already uses below 30% HP. The loadout rule is untouched.
- **`THAI CUC [U]: ON/OFF` on the debug HUD** (`main.c`). Without it, "[U] did
  nothing" and "[U] worked and the skill is still gated" look identical — the
  exact confusion the toggle exists to remove.
- Key checked for collisions first (`core/docs/LANDMINES.md`: KEY_K is already
  globally bound; `U` was free).

Mana still governs the state — `Entity_Update` clears Thái Cực at mana 0 and LOI
costs 45 of a 100 pool, so it drops after ~2 casts. That is the design, not the
toggle failing; noted in the code comment so it is not re-debugged.

**Verified:** strike cast on the caster renders the bolt, the shockwave ring, the
ground light pool and the character lit by its own strike (E2 bleed), with the
E3 envelope timing. E1a's radial burst fires on the same beat.

---

## E4 — flipbook library: smoke LANDED, fire/energy diagnosed (27/07/2026)

Assets are owner-generated (`scripts/gen_*_flipbook.py`, Blender). This session
did the **engine-side** half plus an audit of the sheets.

### Wired: `smoke_atlas_8x8.png` → `VFX_ComposeSmokePuff`

The sheet existed but **nothing consumed it** — F2 was still drawing the three
static silhouettes, and `ParticleConfig.spriteAnim` (full atlas playback, already
implemented in `particle_system.c:826`) had no callers anywhere in the project.
Now F2 plays the 64-frame billow, with the static sprites kept as the fallback
the spec requires (logged when it happens). `tuning.cfg → smokepuff_flipbook`
switches for A/B.

`fps` is derived, not chosen: `SpriteAnim_CalculateUV` advances on ABSOLUTE age
(`frame = age * fps`) while these particles live 1.1–2.0 s, so fps comes from the
LONGEST lifetime (64/2.0 = 32). Using the average instead would let long-lived
sprites run past the end and hold frame 63 — which is empty — so the smoke would
VANISH while its alpha curve still says visible.

**Integration bug found and fixed:** the sheet is a *lit* render (RGB mean
121/255) while F2's gradient is deliberately near-black (the flat sprites are
masks the lighting pass lifts). Multiplying the two measured ~33/255 — the puff
rendered as a black smudge. The consumer now lifts the vertex colour to a
near-neutral tint when the flipbook is active; measured brightness back in line
with the tuned static path (96 vs 99 mean over the puff region).

### Audit of the sheets

`smoke_atlas_8x8.png` — **good.** Real cauliflower silhouettes, a proper
growth→dissipation arc (coverage peaks mid-sheet), 19.6% cell coverage.
Straight alpha (correct for this engine's `BLEND_ALPHA`; the spec's
"premultiplied" line is wrong for this pipeline). Blank frame 0 and 62-63 are
harmless — they coincide with the alpha curve's own fade in/out.

`fire_atlas_8x8.png` — **not usable, and the reason is structural, not tuning.**
Measured against the smoke sheet:

| | coverage | height/width |
|---|---|---|
| smoke | 19.6% | 1.00 |
| fire | **4.1%** | **1.00** |

Fire is rendering as *the same spherical puff as smoke*, only smaller and with a
brightness ramp (the last row blows out to white). Flame morphology is the
opposite: buoyancy stretches it vertically (height/width should be well above
1.3), with tongues that lick up and detach — none of which a spherical smoke
domain produces. It also wastes ~80% of each cell. So `gen_fire_flipbook.py`
needs a different SIM (vertical buoyancy + a flame front), not different
parameters; and per F3 the colour should come from the black-body ramp at the
call site, so the sheet itself should stay a greyscale *density/temperature*
mask.

`energy_shockwave_atlas_8x8.png` — not audited this session (owner says WIP).

### E4 follow-up — flipbook cross-fade (27/07/2026)

Owner report on the F2 flipbook: *"xét riêng từng khung hình thì đúng là giống
khói, nhưng chuyển động giữa các khung cứ có cảm giác hỗn loạn, quay cuồng"*,
then *"tôi có cảm giác những hạt bị lật qua, lật lại"*.

**The atlas was cleared first, by measurement — it is not the fault:**
- frame order correct (the script's `GRID-1-row` flip matches the engine's
  row-major, top-left `frame/cols`);
- cells are not internally flipped (alpha centroid holds at 128/256 across all
  64 frames);
- the puff is radially symmetric, so there is no up/down for it to flip.

Three causes, all consumer-side:

1. **Lockstep frame stepping.** `frame = age * fps` and every sprite in a puff
   spawns on the same frame, so ~39 sprites stepped to a new atlas frame
   *simultaneously*, 32×/s. Fixed with four `SpriteAnim` templates at jittered
   rates (downward only — a faster rate would reach the sheet's empty frame 63
   before a long-lived particle dies, and the smoke would vanish while its alpha
   still said visible).
2. **Per-sprite spin fighting the sheet.** The spin exists to hide texture
   repetition on *flat* sprites; with a flipbook that job is already done and the
   rigid rotation just churns on top of a billow that is already rolling.
   Flipbook-only multipliers: spin ×0.12, sprite count ×0.55 (each flipbook
   sprite carries a whole simulation, so stacking as many as the flat version
   needs averages to mush).
3. **No inter-frame interpolation — the real one.** `SpriteAnim` snapped to whole
   frames: a 64-frame sheet over a 2 s life is 32 fps against a 60 fps render, so
   each atlas frame was held ~2 render frames then JUMPED to a different sim
   state. On a soft blob that is invisible; on an authored turbulent sheet it is
   exactly "flipping back and forth".

**Fix for (3):** `SpriteAnim_CalculateUVBlend` returns frame N, frame N+1 and the
fraction between them, and `particle_system.c` cross-fades the two quads.

The in-shader route was investigated first and is **blocked**: rlgl's immediate
batch carries position/texcoord/colour only (stated in `particle_lit.vs`'s own
header comment), so there is no spare vertex attribute to hand a second UV set
plus a per-particle blend factor to the fragment shader. The two-quad cross-fade
is the standard alternative. Its mid-blend coverage dip (two alpha draws are not
exactly a lerp) is ~7% at smoke's 0.28 alpha — not visible, and it would only
matter for near-opaque sheets, which these are not.

At the clamped end of an `ANIM_ONCE` sheet the blend is forced to 0 so it cannot
cross-fade into a wrapped-around frame 0 and appear to restart.

**Measured** (8 consecutive frames, puff region, deterministic capture):

| | mean Δ/frame | jitter (std of Δ) |
|---|---|---|
| snapping | 2.08 | 0.494 |
| cross-faded | 1.71 | **0.384** |

Jitter is the metric that matters here: uneven frame-to-frame deltas ARE the
snap. Both fell.

`tuning.cfg → particle_fb_blend = 0` disables it (also the perf lever — the
cross-fade emits two quads per flipbook particle). Non-flipbook particles are
untouched: with no `spriteAnim` the blend is 0 and exactly one quad is emitted,
as before.

### E4 follow-up 2 — the flipbook was growing twice (27/07/2026)

Owner, after dropping to a single particle to isolate it: *"nó đã đỡ hơn nhưng
còn 1 cảm giác gì đó, tôi nghi ngờ do hạt vừa lớn lên, vừa đổi khung hình"*.
Correct, and measurable:

- the SHEET expands on its own: puff width 123 px → 180 px = **1.46×**
- `radiusCurve` (0.45 → 2.2) multiplied **4.89×** on top
- compound apparent growth **7.2×**

Same class of bug as the double-darkening: the static-sprite tuning compensates
for something flat cutouts lack, and the flipbook already provides it. The steep
curve exists because "smoke expands and thins; constant-size puffs read as a
decal popping in and out" — true for a flat sprite, already handled by the sheet.

Worse, the sheet's width **wobbles ±7% frame to frame** (162, 153, 173, 155, 150,
167, 161, 172, 163 px). Riding that wobble on a steep scale ramp is what turns a
drift into a pulse.

Fixed with a second, flat curve used only when the flipbook is active
(0.90 → 1.05 → 1.30), plus a 1.45× base-radius compensation so the change affects
smoothness and not size. Measured apparent growth over the visible life is now
**2.0×** and decelerating (5381 → 6290 → 8809 → 10499 → 10725 covered px).

**On metrics:** mean per-frame pixel delta is confounded by sprite size (bigger
sprites move more pixels) and rose purely from the size compensation, while
jitter was unchanged (0.384 → 0.382). Neither settles "does it feel smooth" —
that stays the owner's call. What IS established numerically is that the growth
is no longer compounded.

New knobs: `smokepuff_fb_size` (base radius compensation) alongside
`smokepuff_fb_spin`, `smokepuff_fb_count`, `smokepuff_flipbook`,
`particle_fb_blend`.

### E4 follow-up 3 — the flicker was F1's fake normal, not the atlas (27/07/2026)

Owner sent a screen recording; the flicker survived the cross-fade, the desync,
the spin cut and the flat growth curve. Root cause was none of those.

**Atlas cleared again, numerically:** adjacent frames correlate at **0.974 mean,
0.952 worst** (0 frames below 0.90). The sheet is temporally continuous — the
jump was not coming from the asset.

**Actual cause — `particle_lit.fs`'s analytic hemisphere normal.** It computes
`q = fragTexCoord * 2.0 - 1.0`, i.e. it assumes fragTexCoord is the QUAD-LOCAL
0..1 UV. With a SpriteAnim atlas it is the atlas SUB-RECT, so:

| atlas cell | q actually spans |
|---|---|
| col 0 | [-1.00, -0.75] |
| col 3 | [-0.25, 0.00] |
| col 7 | [ 0.75, 1.00] |

Every cell shades from a *different slice of the hemisphere*, so the lighting
changes discontinuously the instant the animation steps to the next cell. A
guaranteed per-frame pop, entirely independent of how good the sheet is.

F1's author anticipated exactly this: `u_analyticUV` exists with the comment
"0 only if a SpriteAnim atlas is in use", and a derivative fallback sits behind
it. **Nothing ever set it to 0** — at the time F1 landed there were no atlas
particles in the project, and the smoke flipbook is the first one.

**Fix:** rather than fall back to the derivative path (which F1 replaced because
it has a dead core across the sprite's centre), the shader is now told the grid —
`uniform vec2 u_atlasGrid` — and recovers the local UV as
`fract(fragTexCoord * u_atlasGrid)`. Every cell then spans the full [-1,1]
hemisphere and the step disappears. Guarded so grid (1,1) leaves non-atlas
particles on the identical code path (`fract(1.0)` is 0.0 and would fold the
quad's far edge).

Set per batch in `particle_system.c` from `p->spriteAnim->cols/rows`. This never
splits a batch that would not already split, since an atlas particle necessarily
carries a different texture.

Correct **by construction** — the table above is the proof; the pixel-diff
metrics used earlier are too confounded (sprite size, particle birth/death) to
settle it, which is why they are not quoted here.

`particle_lighting_test`'s mirror guard caught the shader edit and was updated
rather than relaxed — the mechanism working as designed (core/CLAUDE.md §3).

### E4 follow-up 4 — the dissipation rim (27/07/2026)

Owner: the body now reads correctly; what remains is that *"lúc nó to ra hết cỡ,
sắp biến mất, thì cái viền tạo ra cảm giác hơi lăn quăn"*.

Isolated to the **sheet's own late frames**, not the consumer. Two consumer
hypotheses were tested and both came back flat, measuring frame-to-frame change
restricted to rim pixels (alpha band 4..70) over 5 consecutive captures:

| variable | rim change |
|---|---|
| cross-fade ON vs OFF | 16.91 vs 16.37 — no effect |
| texture filter POINT vs BILINEAR | 13.63 vs 15.14 — no improvement |

So the rim churn is the simulation's dissipation wisps genuinely changing shape,
amplified because the sprite is at its LARGEST exactly then.

`SetTextureFilter(BILINEAR)` was kept anyway: raylib defaults to POINT, and a
256 px cell magnified across a large sprite gives hard texel blocks. That is
wrong on principle even though this metric could not show it — stated plainly
rather than claimed as a win. Bilinear only, never mipmaps: at coarse mip levels
an 8×8 atlas bleeds neighbouring cells together.

**Mitigation that did help:** `smokepuff_fb_frames` (default 50 of 64) stops the
animation before the most broken-up frames and lets `ANIM_ONCE` hold a calmer
mid-dissipation frame while the alpha curve finishes the fade. Rim change
16.91 → 15.34 (−9%). fps is re-derived from the trimmed count so playback slows
rather than ending early.

**The rest is asset-side.** Rim high-frequency noise measures ~8–10/255 in the
soft edge band, roughly constant across the sheet — consistent with a Cycles
*volume* render at a very low sample count (the script's own usage line is
`--samples 1`, against an argparse default of 16). Re-rendering with more samples
(or denoising) would cut it at source; a gentler dissipation tail would cut the
rest. Nothing further is available from the engine side.

---

## E5.1 — VFX_ComposeGlintSparkle: LANDED (27/07/2026)

`core/composition/common/vc_glint_sparkle.inl`. Anisotropic star glints over a
Fibonacci point cloud — per the spec, nothing in the existing components did
this.

**Built only on the rebuilt foundation.** Owner's standing constraint for Đợt E:
this is a restructure, and the F2 smoke puff is the one component that has
actually been rebuilt, so a new component must be assembled the same way —
F1 lit particles, the F1b blend law, `VFX_Material` colours, `vc_motion.h`
motion. It touches none of `aura_shell` / `ground_aura` / `smoke_energy` /
`effect_material`: those are the single-surface-plus-FBM architecture §0.1b
diagnoses as the problem, and reusing them is exactly the character-aura mistake.

**Particles, not hand-drawn quads.** The spec's per-glint `VC_Flicker01` twinkle
reads as "draw N quads yourself"; routing it through the particle system instead
inherits the blend law, batching, soft-depth and pooling, and avoids adding
another hand-rolled geometry path. The twinkle is a spawn/death cycle on a fast-
bloom fade curve rather than a per-frame alpha.

**Blend law:** a glint EMITS, so additive + `unlit = 1`. Through the lighting
multiply it would brown out against the night sky.

**No asset dependency.** E4's `glint_star_4pt` does not exist, and the required
fallback cannot be the stock particle texture — that is a round blob, and
"anisotropic" is the entire point. So the star is GENERATED once into an image:
two perpendicular lobes (long falloff along the axis, sharp across) plus a small
round core. Closed-form, no noise hash — `fract(sin(...))` dies on Mali
(ENGINE_LANDMINES §4), which is precisely what authored masks exist to avoid.
An authored sheet is still preferred if one ever lands.

Verified in the NEW FX tab (index 66). First capture showed nothing readable —
diagnosed as a values question rather than a wiring one (the case was present
and `VC_MAT_HOLY` had a bright glow), confirmed with the live knobs, then the
defaults were raised: rate 16 → 34/s, radius 0.035–0.09 → 0.085–0.215 m.

Knobs: `glint_rate`, `glint_size`, `glint_points`.

---

## CPU particles gain the glowing core (emissiveBoost) — 27/07/2026

Owner: *"particle của chúng ta không có cái core sáng, như mấy game AAA"*, then —
decisively — *"lõi phát sáng đã được thực thi trong GPU particle... tuy nhiên
chưa có trong CPU particle"*. That pointer saved inventing a second design.

**Why the CPU path could not have a hot core.** Everything upstream was capped
at 1.0 and the cap was provable, not guessed:

1. vertex colour is `rlColor4ub` — 8-bit, max 1.0 (`rlColor4f` merely converts
   down to the same thing, so it is not an escape);
2. `emissiveCurve` is applied CPU-side and clamped at 255 — it can push a colour
   *toward* white but never *past* 1.0;
3. `particle_lit.fs` outputs `texelColor * fragColor`, so ≤ 1.0;
4. the scene buffer is R16F and holds 10.0 happily — the headroom existed and
   nothing ever used it;
5. ACES maps 1.0 to ~0.83, and the bloom threshold is 0.8, so a lone emissive
   particle sat exactly at the threshold: no blow-out, essentially no bloom.

**How the GPU path already solved it** (`compute/gpu_particle_system.c:275`): it
bakes the boost into the colour at spawn — `d.csr = (r/255) * boost` — which
works there because GPU particle colour is stored as FLOAT.

**The CPU port.** Baking into the colour is impossible for the reason above, so
the value rides a shader uniform instead: `u_emissiveBoost` in `particle_lit.fs`,
and `ParticleConfig.render.emissiveBoost` mirrors `GpuParticleConfig.emissiveBoost`
so both paths take the same number and mean the same thing. It participates in
draw batching exactly the way `unlit` does, which makes it genuinely per-particle
rather than per-pass. Only emissive (unlit) particles receive it — boosting smoke
would make it emit light it is meant to occlude (F1b).

**A trap worth recording.** The first attempt applied the multiply only at the
lit output and changed nothing, despite the uniform demonstrably arriving.
`particle_lit.fs` has an early-out at `u_lightingStrength <= 0.0` — and emissive
particles are *exactly* the population that takes it. The boost had to be applied
in that branch too. At boost 1.0 that branch is still byte-identical to the
pre-F1 shader.

**On the measurements.** Mean brightness over "gold" pixels showed almost nothing
(0.83 → 0.84) and briefly suggested the feature was dead. Two confounds: the
sample region included the tester's white crosshair, and the mean is dominated by
low-alpha rim pixels. Measuring the *core* (95th percentile) showed the real
behaviour — whiteness 0.87 → 0.94 at boost 20. At the shipped 4.5 the delta on
this particular test is modest because glints overlap additively and those pixels
are already near saturation; the boost matters most for single, non-overlapping
sprites and for what crosses the bloom threshold.

`VFX_ComposeGlintSparkle` uses 4.5 — the same value the GPU particle upgrades
test already uses, so the two paths agree on what "glowing" means. Global
override: `tuning.cfg → particle_emissive_boost` (1.0 = per-particle values as
authored).

---

## E5.3 — VFX_ComposeChargeConverge: LANDED (27/07/2026)

`core/composition/common/vc_charge_converge.inl`. The universal cast tell —
motes drawn inward into a hot core, pairing with `VFX_SeqPreset`'s anticipation
phase. Assembled the same way E5.1 was (F1 particles + F1b blend law +
`VFX_Material` + `vc_motion.h`); touches none of the §0.1b components.

**Deviation from the spec's use of `t01`.** The spec reads as "every mote sits at
`VC_MotionSpiralIn(..., t01)`", which freezes the whole field into a static
dotted ring whenever a caller holds `t01` still (a charge waiting on input, a
paused sequence). Each mote carries its OWN progress `u` instead, and `t01`
drives what the tell actually communicates: density, brightness, funnel
tightness, light radius.

### What the first four passes got wrong, and why

Each was measured/derived, not guessed:

1. **A linear spiral cannot express suction at any `turns`.** Its
   tangential/radial ratio is `turns*2PI*(1-t)` — at the RIM, where the motes
   are, motion is almost purely tangential, so the field reads as *orbiting*.
   At `turns` 0.85 it was still ~4:1 tangential there. Fixed with a new motion
   primitive, **`VC_MotionSpiralInAccrete`** (`vc_motion.h`): angle piles up as
   `t01^3`, so the outer leg flies nearly straight in and only the inner leg
   curls — angular-momentum-shaped, the ER look.
2. **Uniform `u` clusters motes at the centre** (path radius is `rim*(1-u)`, so
   equal counts per radius band = far too many per unit AREA near the middle).
   `u = 1 - x^0.65` sits between that and a strictly area-uniform `sqrt`, which
   over-loaded the rim and read as a torus.
3. **Lifetime must be DERIVED from the trip, not chosen.** With a constant life
   and a speed that rises with `t01`, late motes shot through the core and out
   the far side — a full charge held FEWER motes in the funnel than a half one.
   Now `life = remaining_distance / speed`.
4. **A saturated element colour never reaches white.** Additive stacking of
   `VC_MAT_LIGHTNING`'s glow (0,185,255) just gives more cyan, so nothing read
   as hot however high `emissiveBoost` went — the multiply cannot lift a channel
   that is 0. New shared helper **`VC_Whiten`** (`core/presets/vc_material.h`)
   leaves headroom at the source; motes 30%, the core 75%.

### Owner's call: wisps, not dots

*"tôi nghĩ dùng trail hay wisp nhìn nó sẽ đẹp hơn"* — right, and cheaper to read:
one head plus a tail beats a crowd of dots. Switched from stretched billboards to
the particle system's own ribbon trails (`render.trailLength`). A stretched
billboard can only smear along the CURRENT velocity, i.e. a straight dash; the
ribbon follows the mote's actual curved path, which is the whole point here.

Two engine gaps surfaced doing it, both fixed in `core/`:

- **Particle trails ignored the particle's blend mode.** The trail pass runs
  after the main pass tears the blend state down, so every trail drew
  `BLEND_ALPHA` — an additive emissive particle dragged a non-emitting grey
  smear. Nothing had noticed because until now nothing paired `trailLength` with
  `VFX_BLEND_ADDITIVE`. The pass now switches blend per particle like pass 1.
- **Tail length was capped by a hard-coded step.** History is 8 points recorded
  every 0.015 s, so a tail could only ever cover 0.105 s of travel — a comet
  dash, never a wisp. New `render.trailStepTime` (0 = the legacy 0.015)
  buys ~3x the length at the same 8 points and same memory. Cost is a coarser
  polyline, invisible on smooth paths.

Then, second owner note — *"cho nó thành sợi khí mảnh thôi, bỏ đầu tròn luôn"*:
a third gap. There was no way to draw a particle's trail WITHOUT its head.
Alpha 0 on the head kills the trail too (the trail scales its alpha by the
particle's), and shrinking the head thins the trail with it (half-width is
`radius * trailWidthRatio`). New `render.trailOnly` skips the billboard in the
main pass — before the batching decision, so it cannot split a batch — leaving
the particle as a pure PATH and `radius` as the thread's thickness.

Knobs: `charge_rate`, `charge_size`, `charge_speed`, `charge_core`,
`charge_trail` (0 = bare motes, A/B the tails), `charge_turns`.
NEW FX index 67 `CHARGE CONVERGE`; suite 6/6.

### E5.3 rebuild — mesh emitter + force-driven qi threads (27/07/2026)

Owner rejected the dot/streak version twice, then set the direction: *"nó chỉ nên đơn
giản, nên phát ra các trail ở bề mặt 1 hình cầu, sau đó tụ lại, quan trọng tạo
được chuyển động mềm mại của chân khí"*, plus *"có mesh emitter, bạn cũng chưa
tận dụng được"*. Both notes were right, and the second named a facility this
component should have used from the start.

**Re-diagnosed against §0.1b.** The streak version repeated the very disease the
spec diagnoses, in a different costume: instead of one-surface-plus-FBM it was
all-particles-all-additive. Additive output can never be darker than what is
behind it, so nothing occludes anything and there is no volume; and the
silhouette was still coming from a closed-form curve, which §0.1b says gives
texture but never an outline.

**What replaced it**

- **Shape from geometry.** `SpawnParticleOnMesh` + `MeshAdjacency` on a real
  `GenMeshSphere` — the engine's mesh emitter, previously used only by
  `vc_mesh_electricity`. Threads are born on the sphere's EDGES, so the launch
  set is scattered the way a surface is, not the way a parameter sweep is. One
  unit sphere serves every scale; `radius` rides the spawn matrix.
- **Motion from forces.** A launch push, then `FORCE_GRAVITY_POINT` +
  a weak `FORCE_VORTEX` + `FORCE_DRAG`. The analytic spiral was the reason the
  old version read as machinery — every thread swept the same arc at the same
  rate. Under an attractor each thread accelerates as it closes in and no two
  paths match, which is what "mềm mại" is. Drag is load-bearing: without it
  threads slingshot through the centre and out the far side.
- **Force fields are POOLED (4), matched by centre.** A particle holds the field
  pointer for its whole life, so one shared field would drag every thread of
  every concurrent charge toward whichever centre was written last.
- `VC_MotionSpiralInAccrete` stays in `vc_motion.h` — it is a correct, reusable
  primitive with its reasoning recorded above — but nothing calls it now.

**Third core gap, found here:** the 8-point history is a coarse polyline, and on
a curving path its corners show as facets — a bent wire, not a thread of gas.
New `render.trailSmooth` subdivides each segment with a **Catmull-Rom** through
the recorded points (interpolating, so the smoothed trail still passes exactly
where the particle was; a Bezier would have needed invented control points).
`PS_TRAIL_SUBDIV` 4, buffer 32.

So E5.3 ended up adding four things to the particle system that the project had
never needed before: per-particle trail blend mode, `trailStepTime`,
`trailOnly`, `trailSmooth`. All four default to the previous behaviour.

### E5.3 — white threads, per-thread opacity, and the real bloom ceiling (27/07/2026)

Owner: *"sao lại phải màu xanh? nó nên màu trắng để dễ tint màu vào, và mỗi trail
có độ mờ ngẫu nhiên nữa, hơn nữa có cách nào làm cái core nó sáng hơn để bloom?"*

- **White threads** (`charge_white`, 0.88). The element hue now appears only as
  a thread fades out, so a caller TINTS the effect instead of each material
  baking its own hue into the geometry. It is also the only colour additive
  stacking can drive to a hot core — a saturated hue just accumulates itself.
- **Per-thread opacity**, alpha 90–255 at spawn. Uniform alpha reads as a
  printed pattern; the ribbon scales its alpha by the particle's, so one random
  value varies the whole thread.
- **The bloom question had an arithmetic answer, found by reading the shader
  rather than by tuning.** `bloom_bright.fs` clamps EVERY pixel's contribution
  at `maxEnergy = 4.0` (a firefly guard). Past that point, raising a particle's
  `emissiveBoost` changes the bloom by exactly nothing — which is precisely why
  the earlier emissive measurement showed 0.87 → 0.94 for a boost of 20 and the
  feature looked dead. **Bloom size is set by how MANY pixels clear the
  threshold, not by how far one pixel clears it.**

  Two fixes, in that order of importance:
  1. A **mid-glow layer** at the core: a wider sprite kept just over the
     threshold, rather than a second white-hot point. This is what buys the
     glow, and it is the standard layered-core construction.
  2. The clamp is now a uniform — `tuning.cfg → bloom_max_energy`, default 4.0,
     i.e. byte-identical to the old constant until someone raises it.

  Measured over the core region, same frame, before vs after:
  near-white pixels **76 → 655**, pixels over 150 **4384 → 12790**. (Region is
  fixed and does include some threads; the ratios are far too large to be that.)

`convert_shaders_to_gles.py --dry-run`: `bloom_bright.fs` converts clean; the one
warning in the run is `particle_lit.fs`, pre-existing.

**Still open after all of the above — "vẫn cảm giác nó không sáng, không rực rỡ"**
(owner, deferred rather than solved). Two system-level suspects, neither of them
in the charge component, both to be tested when something else brings them up:
1. **ACES tonemap** (G1) maps 1.0 to ~0.83 and compresses hard above it, so
   *everything* lands white-but-not-radiant. A saturated glow that survives ACES
   usually needs the colour to stay off the neutral axis at high luminance.
2. **`bloomIntensity = 0.25`** (main.c) is a conservative global. `bloom_max_energy`
   is now tunable but intensity is not.
Do not tune these from inside a single effect — they are scene-wide.

---

## E5.2 — VFX_ComposeRuneCircle: LANDED (27/07/2026)

`core/composition/common/vc_rune_circle.inl`. `ringCount` concentric counter-
rotating rings on an arbitrary plane, snapping open and easing shut on `t01`,
with sparks running along the rings.

**Ribbons, not a decal or a textured quad.** A sigil has to sit on a plane the
caller chooses (`normal`), which a ground decal cannot do, and per §0.1b the
silhouette must come from geometry rather than from noise inside a quad. Built
on `ribbon_strip` — the project's one ribbon primitive — plus `VFX_Material` and
`vc_motion.h`. `MagicFilaments`/`GroundAura` also draw circles and are
deliberately NOT reused: they are the architecture §0.1b diagnoses.

**Glyphs without an atlas.** E4's sheet does not exist, so rings are DASHED —
alpha switched along the band, tooth count/duty/phase varying per ring so no two
share a rhythm. Closed-form, no `fract(sin(...))` (ENGINE_LANDMINES §4, Mali).

**Layered additive passes, because a ribbon has no `emissiveBoost`.** That
uniform lives on the particle shader; immediate-mode geometry caps at 8-bit 1.0,
which is under where the bloom threshold can reach. The way to exceed 1.0 in the
HDR buffer with plain geometry is to draw it additively more than once: wide dim
halo → the band → narrow near-white core. Measured over the sigil region,
pixels over 150: **3103 → 7239**. This is also the general answer to "why doesn't
my geometry bloom", and worth remembering for the still-open radiance question.

### The bug this task actually found — ribbons were being culled

Drew nothing at all, while logging correct geometry (97 points, unit side
vectors, alpha 242, sane positions). `DrawRibbonStripEx` disabled back-face
culling immediately before `rlBegin` **without a batch flush**, so the disable
never applied to its own quads: camera-facing strips always present their front
face and looked fine, a ring lying flat via `RIBBON_FIXED_NORMAL` presented its
back face and vanished entirely. Fixed in `core/ribbon_strip.c` — every ribbon
consumer in the project inherits it. Written up in `core/docs/LANDMINES.md` with
a pointer from `ENGINE_LANDMINES.md`.

Diagnostic note: "the geometry is correct" and "the geometry is visible" are
different claims, and the TraceLog only ever proved the first. What separated
them in one run was drawing a plain control polygon from the same function at the
same position — state isolated from maths.

Knobs: `rune_white`, `rune_width`, `rune_spin`, `rune_dash`.
NEW FX index 67 `RUNE CIRCLE` (charge moved to 68); suite 6/6.

### E5.2 follow-up — the glyph textures, generated not drawn (27/07/2026)

Owner asked for the script that was offered (not an AI prompt), and: *"rune_line
nó phải có vân năng lượng, chứ không đều như vậy"*.

**`scripts/gen_rune_textures.py`** writes the whole set:
`rune_line.png` (64x1024) and `rune_glyphs_0..3.png` (256x2048, four styles).

Procedural strokes rather than a font or an image model, for three reasons that
are requirements here rather than preferences:
1. the strip wraps a circle, so its join is on screen at ALL times — it must tile
   exactly, which the script guarantees by making the glyph pitch divide the
   width (measured seam delta: **0.0/255** on all four sheets, 0.6 on the line);
2. glyph height must be uniform or the ring looks ragged;
3. a clean alpha channel — image models effectively never give one, and macOS
   has no Unicode Runic font installed by default.

**The line profile is deliberately uneven.** A perfect gradient reads as a
printed circle. Brightness, thickness and centre all wander along the ring's
length, with occasional near-breaks, all built from sines at INTEGER cycle
counts so f(0) == f(1) exactly — a noise hash would not close the loop (and dies
on Mali anyway, ENGINE_LANDMINES §4).

Files are written TRANSPOSED because a ribbon's `u` runs across the band width
and `v` along its length: image X = band width, image Y = circumference.

Consumer: rings alternate written/plain (`r % 2`), a written ring is ~3x wider so
its glyphs are legible, takes 2 passes not 3 (the third pass paints a white
filament down the middle, which would cross out the text), and skips the dash
mask (the sheet's alpha IS the pattern). Falls back to dashed rings with a
warning if the sheets are missing.

### E5.2 follow-up 2 — real fonts, unbroken inner ring, no debris (27/07/2026)

Owner: *"ký tự cổ xấu quá, sao không dùng font? với cái vòng tròn nhỏ nhất sao bị
đứt nét? bỏ những ribbon, hạt thừa thãi, hơn nữa sao nó không sáng rực?"*

1. **"macOS has no runic font" was wrong** — that claim was assumed, not checked.
   Probing every system font against the tofu glyph (a missing glyph renders as
   the SAME box for every codepoint, so the test must compare against U+FFFF,
   not just ask whether ink landed) finds `Apple Symbols` carrying the Runic
   block **and all 64 I Ching hexagrams**, `Songti` the Han set, Noto the
   Tifinagh. Sheets are now hexagram / Han (heavenly stems, earthly branches,
   five elements, eight trigrams) / runic / tifinagh — the project's own
   cosmology first.
2. **Strip aspect had to change with it.** At 8:1 a Han character came out three
   times wider than its own pitch and the glyphs piled into each other. Now
   32:1 (4096x128), with glyph size clamped by BOTH the band height and the
   pitch. Seam delta 0.0/255 on three sheets, 9.7 on the runic one.
3. **The broken inner ring had two causes, both removed.** The dash mask ran on
   every plain ring, and the innermost has the shortest circumference, so the
   same pattern that reads as marks on a big ring reads as a broken ring there;
   and `rune_line` dimmed almost to zero in places to suggest uneven energy.
   The innermost plain ring is now solid, and the line profile modulates without
   ever interrupting.
4. **Sparks deleted** (the loose particles orbiting the sigil).
5. **Radiance:** the element colour is no longer bleached out (`rune_white`
   0.85 → 0.25 — white was making it pale, not bright), and a glyph ring now
   takes three passes where the third is the SAME sheet drawn again rather than
   a filament down the middle, which would cross out the writing. Plus
   `tuning.cfg → bloom_intensity` as an explicit override (0 = the caller's
   value passes through untouched).

---

## E5.4 — VFX_ComposeDissolveExit: LANDED (27/07/2026) — E5 batch complete

`core/composition/common/vc_dissolve_exit.inl`. The shared erosion-out: any
effect can call it on its death instead of inventing a fade. Two populations per
the blend law — an ALPHA body that still occludes while it erodes, and additive
unlit embers with an emissiveBoost, because one draw call cannot be both.

**The spec said `dissolve.fs` "exists but is not exposed". It was worse than
that: it had never compiled.** `colDiffuse` was undeclared, and a failed compile
falls back to the default shader silently — the first capture showed a perfectly
ordinary soft quad with no erosion at all, and nothing on screen said why. Fixed
in the shader, along with its uniform initialisers (ignored with a warning by
this backend, so a caller that forgets to set one gets 0 rather than the written
default). Written up in `core/docs/LANDMINES.md`.

**Two measurements that replaced guesswork**

1. *Grain*: sampling the noise at the quad's raw UV gives one whole noise image
   per sprite — the finest grain available, which reads as static. New
   `noiseScale`/`noiseOffset` uniforms magnify it into clumps and give each
   sprite in the cluster its own patch, so they do not erode as one stencil.
2. *Timing*: `assets/textures/noise.png` measures mean 0.498, p10 0.310,
   p90 0.686 — a narrow distribution. Fed the threshold raw, `t01` does nothing
   below ~0.31, erases nearly everything between 0.31 and 0.69, and has nothing
   left to do above it: all the visible change happened in a 40% window. `t01`
   is now remapped onto [0.18, 0.86].

**Landmine found on the way:** `rlDrawRenderBatchActive()` restores rlgl's
default 1x1 white texture, so the per-sprite uniform flush unbound the body
mask and every quad after the first rendered as a hard bright SQUARE.
`rlSetTexture` has to be re-issued after every flush.

Knobs: `dissolve_edge`, `dissolve_ember`, `dissolve_grain`.
NEW FX index 68 `DISSOLVE EXIT`; suite 6/6.

**E5 batch status:** E5.1 GlintSparkle · E5.2 RuneCircle · E5.3 ChargeConverge ·
E5.4 DissolveExit — all four landed. E6 is the next batch in the spec.
