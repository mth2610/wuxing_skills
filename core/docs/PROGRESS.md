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

---

## E4 — fire flipbook rewritten as a fluid SIM, not a render (27/07/2026)

`scripts/sim_fire_flipbook.py`. Replaces the Blender path for fire
(`gen_fire_flipbook.py` is left in place but is no longer the source of the
shipped sheet).

**Why a rewrite rather than a re-tune.** The E4 audit measured the Blender sheet
at 4.1% cell coverage and height/width 1.00 — it was rendering the same
SPHERICAL puff as the smoke sheet, only smaller. No parameter reaches flame
morphology from a spherical domain. Second reason: Cycles volume rendering is
stochastic, and E4 follow-up 4 traced the writhing dissipation rim to exactly
that. A grid solver is deterministic — same seed, same frames.

**What it is:** a 3D incompressible fluid on a uniform grid — semi-Lagrangian
advection, Jacobi pressure projection, buoyancy, vorticity confinement, T^4
radiative cooling that converts flame into soot, fuel cutting off at 62% so the
tail of the sheet is the flame dying rather than a hard loop. numpy only
(trilinear interpolation is hand-written; scipy is not installed here).

**Two commands, as asked:** `--quick` (~11 s, 512px) for iterating, bare (~2.5
min, 2048px) for the sheet that ships. `--buoyancy` / `--cooling` / `--swirl` are
on the command line, so shape is swept without editing code, and every run
prints coverage + height/width and WARNS when the result is still puff-shaped.

**Three failures, each caught by a number rather than by eye**

1. **Empty sheet (coverage 0.0%).** Semi-Lagrangian advection is unconditionally
   stable, which is exactly why it failed silently: instead of blowing up, the
   solver kept running while every parcel was traced off the grid. Probing the
   fields showed |v| going 26 → 146 cells/step in eight steps. Fixed with a CFL
   clamp plus drag; `dt` dropped 0.55 → 0.12.
2. **A flat bright LID across every cell** — gas piling against the ceiling. The
   hard 3-row damp was replaced with a graded outflow over the top eighth.
3. **A hard bright BAR along the bottom** — the injection layer is a boundary
   condition, not part of the flame; it is now faded out of the projection.

Also: normalisation is done ONCE across the whole sheet, not per frame.
Per-frame would rescale a dying flame to look exactly as bright as a roaring
one, i.e. delete the intensity arc that an authored flipbook exists to carry.

Current numbers at `--buoyancy 19 --cooling 2.0 --swirl 4.2`: coverage 41.3%,
height/width 1.18. Above the 1.0 the Blender sheet managed, still below the
1.3 target — the remaining gap is a look sweep, which is what `--quick` is for.

### E4 fire — Mantaflow + Eevee pipeline (27/07/2026)

Owner: *"nếu thấy xài qua blender không hiệu quả có thể dùng Mantaflow trực tiếp
mà"*. Checked rather than argued, and the check changed the conclusion:

- **There is no standalone Mantaflow to install** — `pip` has no such package;
  using the C++ library directly means building it from source.
- **Blender 3.6 IS Mantaflow.** The Fluid modifier is Mantaflow embedded.

So the earlier failure was never "Blender is not good enough". It was two other
things: a cubic/spherical setup that cannot produce flame morphology, and
**Cycles** — path-traced volumes are stochastic, which is what E4 follow-up 4
traced the writhing rim to. Mantaflow for the SIM, **Eevee** for the render
(rasterised volumes, so the noise is gone by construction, not by sample count).

`scripts/manta_fire_flipbook.py` (bake + render) and `scripts/pack_flipbook.py`
(pack + audit). Split because the bake needs Blender's interpreter and the pack
needs numpy/PIL — and because a re-pack then costs seconds instead of a re-bake.

    blender --background --python scripts/manta_fire_flipbook.py -- --quick
    python3 scripts/pack_flipbook.py build_cache_manta/frames --grid 8 --out fire_atlas_manta_8x8.png

Quick bake is ~5 s, render ~15 s. Knobs on the command line: `--buoyancy`,
`--vorticity`, `--burn`, `--flame-smoke`, `--emission`.

**Measured against the audit that failed the old sheet:**

| | old (Cycles) | numpy solver | Mantaflow + Eevee |
|---|---|---|---|
| height/width | 1.00 | 1.18 | **1.43** |
| cell coverage | 4.1% | 41% | 15.6% (smoke: 19.6%) |

**Four bugs, each found by a measurement rather than by eye**
1. `argparse.parse_args()` with no argument reads `sys.argv`, which under
   Blender still holds Blender's own flags — must be `parse_args(argv)`.
2. The fuel emitter rendered as a solid white blob in every cell: it is a
   boundary condition, `hide_render = True`.
3. **Emission-only volumes render bright but nearly TRANSPARENT** — Eevee's
   alpha comes from extinction, not emission, so the flame measured rgb 255 /
   alpha 10 and the audit read the sheet as 64/64 empty frames. `pack_flipbook`
   now folds luminance into alpha (`--alpha-from-luma`).
4. Emission gain 25 clipped every flame pixel to white and threw away the
   temperature gradient; exposed as `--emission`.

**Known limitation, stated rather than hidden:** with alpha derived from
luminance the sheet carries ONE channel of information — it cannot express
"thick but cool" (smoke). The fix is a two-pass render (density-only pass → A,
flame-only pass → RGB), roughly 20 lines, not yet done. Also note the grey
histogram inside the mask is self-referential while alpha comes from luma: the
region selected by alpha is by definition the bright region, so that particular
measurement cannot judge the gradient.

`scripts/sim_fire_flipbook.py` (pure numpy) stays as the no-Blender fallback.

### E4 — the flipbook pipeline settles: Mantaflow sim + Taichi render (27/07/2026)

Owner asked which to standardise on for the remaining flipbooks. Answer, with
the checks behind it: **both, split by what each is actually good at.**

- **Mantaflow does the SIM.** Production solver (MacCormack, calibrated
  fuel → flame → soot combustion, obstacles, wavelet turbulence). Reproducing it
  by hand is thousands of lines, and every future sheet — explosion, shockwave,
  dust, steam — needs a *different* physical model that it already has.
  There is no standalone build to install (`pip` has no mantaflow); Blender's
  Fluid modifier IS Mantaflow.
- **Taichi does the RENDER.** Verified installed and running on Metal:
  128³, 2.86 ms/kernel — about 1000x per voxel over the numpy solver.
- **The link that makes the split possible, verified before committing to it:**
  `density_grid`, `flame_grid`, `temperature_grid`, `velocity_grid` are readable
  straight from Blender's Python. No OpenVDB, no build step.

**Why not just render in Eevee.** Its alpha comes from EXTINCTION, so an
emissive flame renders bright and nearly transparent (measured: rgb 255 /
alpha 10). That forced alpha to be faked from luminance, which collapses the
sheet to ONE channel — it can no longer express "thick but cool" (smoke) apart
from "hot". That is not a tuning problem; it is how raster volumes work.

**Three scripts, deliberately separate**

    blender --background --python scripts/manta_bake.py -- --preset fire --quick
    python3 scripts/ti_render.py build_cache/fire --cell 128 --supersample 2
    python3 scripts/pack_flipbook.py build_cache/fire/frames --grid 8 \
        --alpha-from-luma 0 --out fire_atlas_8x8_v2.png

Bake ~8 s, render ~5 s, pack instant (at --quick). The split means a look change
never re-bakes, and one bake feeds any number of sheets. New effects are a
PRESETS entry in `manta_bake.py`, not a new script (`fire`, `smoke`, `explosion`
are in already).

**Channel layout** — R = flame emission (multiply by the black-body ramp at the
call site, F3; the additive population), G = smoke density (the alpha-blended,
LIT population, F1b), B = reserved (motion vectors / rim / 6-way lighting),
A = true opacity from transmittance. One sheet now feeds BOTH populations the
blend law requires.

**Result**: height/width **2.02** (Cycles sheet: 1.00), flame covers 26.9% of the
sheet and smoke 35.7%. The audit now reports the two channels separately — the
old 19.6% target was measured on a sheet where alpha *was* the smoke, so
comparing a two-channel sheet against it is meaningless.

**One sim fix found by the render:** soot was conserved, so it filled the domain
within a dozen frames and the silhouette became the box (66.8% coverage, a
rectangular outline). `dissolve` is now on in the fire preset.

**Known limit:** the dumped grids are BASE resolution — Mantaflow's wavelet
upres lives in a grid Python does not expose. Raise `--res` instead; the GPU
ray-marcher can afford it.

### E4 flipbook pipeline — packaged, and two renderer bugs the audit could not see (27/07/2026)

Pipeline now lives in `scripts/flipbook/` (`make.py` runs all three stages;
`bake.py` / `render.py` / `pack.py` / `fb_presets.py` / `selftest.py` / README).
Adding an effect is a `fb_presets.py` entry — `fire`, `smoke`, `explosion`,
`dust` are in. `make.py --no-bake` re-renders from grids already on disk, which
is the loop that matters: the look never needs a re-bake.

**Two bugs in the renderer, both invisible to the audit** because the audit
measures the sheet and the sheet was wrong in the same direction as the
measurement:

1. **Frames written transposed** — Taichi fields index `[x, y]`, numpy/PIL read
   axis 0 as the row. The flame "rose" along the image's X axis. Caught by
   measuring centroids: row-centroid sat at 128 in every frame while the
   column-centroid drifted 236 → 198. **Every height/width number reported
   before this fix was meaningless** (the 2.02 and 2.13 figures included).
2. **Each axis stretched to the full square cell**, ignoring the domain's
   aspect. A 34x34x96 grid came out smeared 2.8x horizontally. This one bug
   produced three complaints that sounded unrelated — "nhìn đâu giống lửa",
   "khói hình dạng kỳ", "lan hết ô → bị cắt hình".

**`selftest.py`** now renders a synthetic column of known shape and asserts the
result is tall, unclipped, and rising. No Blender, ~5 s. A real bake cannot test
this: nobody knows what the flame *should* look like, so a distorted one just
looks vaguely wrong.

**Correction to what was written earlier.** The `bake_all()` failure
(`'NoneType' object has no attribute 'getDataPointer'`) was blamed on a stale
Mantaflow cache, and that went into the README. It is wrong: a 20-line minimal
repro with `--factory-startup` and a fresh cache fails identically, on a machine
where the same script worked half an hour earlier. It is an environment fault
(restart Blender, then the machine), not a pipeline bug. The README now says so.

**Also fixed:** a failed bake used to be silent — the next stage rendered the
OLD grids and every measurement afterwards described the previous sheet.
`bake.py` returns non-zero on a bake exception and on a short frame count, and
`make.py` stops.

**Blocked:** the fire sheet cannot be regenerated until Mantaflow bakes again.

### E4 fire sheet — SHIPPED (27/07/2026)

`assets/textures/fire_atlas_8x8.png`, 2048x2048, produced entirely by
`scripts/flipbook/` (Mantaflow bake 512 s at res 112 → grid 70x70x112 → Taichi
render → pack).

| | Cycles sheet (E4 audit) | this one |
|---|---|---|
| height/width | 1.00 (a spherical puff) | **1.73** |
| cell coverage | 4.1% | 18.1% (smoke sheet, which works: 19.6%) |
| channels | 1 (greyscale) | R flame 9.1% · G smoke 9.9% · true A |

**The bake failure was `cache_type = 'ALL'`, not the environment.** It is a
valid enum value so nothing warns, but with it set `bpy.ops.fluid.bake_all()`
dies with `'NoneType' object has no attribute 'getDataPointer'`. Blender's
default REPLAY works, and under REPLAY the solver steps as frames are set —
exactly what the dump loop walks anyway.

**Method note, because two wrong diagnoses came first.** It was blamed on a
stale cache (wiping `build_cache` did make one run succeed — coincidence), then
on a broken Blender install (restarting Blender changed nothing). What actually
found it: build a MINIMAL scene that bakes cleanly with Blender's defaults, then
re-enable one setting at a time — cachedir OK, resolution OK, adaptive-domain
OK, `cache_type='ALL'` FAILED. When "the same script worked half an hour ago",
the temptation is to reach for environment state; the reliable move is to walk
from a working configuration toward the broken one, one variable per step.

### F3 consumes the E4 fire sheet (27/07/2026)

`VFX_ComposeFlameVolume`'s BODY layer now plays the simulated flipbook
(`assets/textures/fire_atlas_8x8_flame.png`) instead of F2's round sprites.
Owner asked whether a COLUMN sheet fits this component: it does — FlameVolume is
already a tall, narrow, rising flame with core/body/smoke layers, which is the
same shape the sim produces.

Two things the wiring needed:

1. **The sheet had to be split per population.** `particle_lit.fs` multiplies the
   WHOLE of `texelColor.rgb` by the vertex colour, so feeding it a two-channel
   sheet (R flame, G smoke) would tint the fire with its own smoke. `pack.py
   --split` writes `<name>_flame.png` and `<name>_smoke.png`, each white-RGB with
   the channel in alpha, so the black-body ramp at the call site still owns the
   colour. One bake, two sheets, no shader change.
2. **Rotation had to be turned OFF for the atlas.** A round puff can spin freely
   — that is how F2 hides sprite repetition — but a simulated flame column has an
   UP, and spinning it renders the fire upside down.

`fps` is derived from the LONGEST body lifetime (64 / 1.7 s), not the average:
SpriteAnim advances on absolute age, so a faster rate runs a long-lived particle
past the sheet's empty last frames and the flame vanishes while its alpha curve
still says visible — the same landmine the smoke puff hit in E4.

`tuning.cfg → flame_atlas` (0 = the old sprites) for A/B. Falls back with a
warning if the sheet is missing.

### F3 atlas follow-up — UV flip, fill-rate, and why it still is not the reference (28/07/2026)

Owner: *"những particle bị ngược thì phải, hơn nữa fps tụt thê thảm, còn 22 chỉ
với ngọn lửa này"* + a reference photo of a fire bed made of many separate licks.

**1. The particles WERE upside down, and it is an engine bug, not the sheet.**
`PS_EMIT_QUAD` paired texcoord `(x, y)` — the atlas cell's TOP-left — with the
`-up` vertex, i.e. the BOTTOM of the quad. Every particle sprite this engine has
ever drawn was flipped vertically. It went unnoticed for the same reason the
atlas-UV hemisphere bug did: every sprite was round and symmetric. The E4 flame
flipbook is the first sprite with an UP. Fixed in `particle_system.c`; suite 6/6.

**2. Frame rate: 22 fps on one flame is OVERDRAW, not particle count.** Each
atlas sprite is a large alpha-blended quad, and with the flipbook cross-fade it
draws TWICE. Twenty-two of them stacked on one axis is a wall of full-screen
alpha. The atlas path now spawns 6 body sprites instead of 22, each ~2.5x larger,
with `flame_body_count` as the lever. Same lesson E4 recorded for smoke: a
flipbook sprite carries a whole simulation — do not stack it like a flat puff.

**3. The look still is not the reference, and the reason is structural.** The
`fire` preset bakes an ENTIRE fire into one sprite. Right for a lone campfire,
wrong as a building block: a few of them merge into one mass, while the
reference is many separate tongues with dark gaps. Added a `flame_tongue`
preset (narrow domain, high vorticity) — the composition then scatters tongues
along a base instead of stacking columns.

**Resolution trap found doing that:** `resolution_max` applies to the LONGEST
axis, so a 0.32 x 0.32 x 1.9 domain at res 32 bakes a **5x5x32** grid — five
voxels across the tongue. A narrow preset needs res 128+ to be worth baking.

### F3 — the body blend was the reason fire read as PATCHES (28/07/2026)

Owner: *"tôi nghĩ do chế độ blend màu nữa, lửa mới có độ trong nhất định như ảnh
mẫu. còn cái này màu giống theo từng mảng."* Correct, and the contradiction was
sitting in the file: the comment above the body layer says "FIRE EMITS LIGHT —
it must not be multiplied by the scene's", and the layer was then drawn with
`BLEND_ALPHA`.

F3 chose alpha deliberately, and for a good reason at the time: the old
`fire_funnel` was ONE additive draw and read as glowing gas, so the body was made
able to be darker than its background. That is right for a hand-tuned gradient on
a round sprite. It is wrong for a SIMULATED sheet, which already carries its own
density falloff — alpha then stacks the sprites into opaque patches, while real
flame is translucent and accumulates. The layer that genuinely needs to occlude
is the SMOKE, which stays alpha and stays lit (F1b intact).

`flame_body_blend` (1 = additive, default with the atlas; 0 = the original) and
`flame_body_alpha` (per-sprite contribution). Additive accumulates, so that
second knob is what decides whether overlapping tongues keep their orange or clip
to white — and it depends on the BACKGROUND: the test arena's sky sits near 0.35,
so a value that reads as fire in the night scene blows out there. Still blowing
out in the test scene at the time of writing; it needs tuning where it ships.

**Owner's direction for the next sheet:** bake fire as a PUFF (like the smoke
sheet) rather than a column — a column sprite is right for one isolated flame or
a round fire bed, but puffs are what compose into a larger fire.

### E4 — `fire_puff` preset, and a resolution trap that invalidates quick-tuning (28/07/2026)

Owner specified the physics: a puff has **no buoyancy and no gravity** — only
radial expansion, curl noise and viscosity. In Mantaflow that maps to
`beta = 0`, `alpha = 0` (the whole difference from the `fire` preset; leave them
on and every sheet drifts upward into a column again), radial push from the
inflow's `velocity_normal` along the emitter sphere's NORMALS, `vorticity 0.95`
for the curl, and `dissolve` standing in for viscosity, which a gas domain has
no parameter for.

Two things the first attempt got wrong:
- **`burn` is fuel CONSUMPTION rate.** At 1.4 the flame was gone by frame 10 and
  two thirds of the sheet was empty. 0.45 keeps fire alive through the first half.
- **The emitter was flattened for every preset** (`flow.scale.z = 0.45`), which
  is right for something rising off a floor and wrong for a puff — a flat source
  biases the expansion wide before any physics runs. Now `fuel_flat` per preset.

**The trap: parameters tuned at `--quick` do not transfer to full resolution.**
The quick sheet (res 32) had a clear life cycle with visible curl structure. The
same preset at res 112 baked a smooth, near-solid SPHERE — 17 minutes to find
out. Vorticity and burn are resolution-relative: at 112 the same vorticity acts
on features eight times smaller, and the same burn rate consumes far less fuel
per voxel per step. Tune at a MIDDLE resolution (~64) before committing to a
full bake, or treat quick output as composition-only (timing, extent) and never
as a preview of the look.

`fire_puff_8x8.png` + `_flame`/`_smoke` splits are written but are NOT usable
yet for that reason.

### E4 — Taichi GPU solver as the fast path (28/07/2026)

Owner asked whether writing the sim in Taichi would be faster. Measured, same
config (res 64, 24 frames): **Mantaflow 99.9 s → Taichi 3.7 s**, ~27x. At res
112/64 frames Mantaflow took 1037 s, so the iteration loop goes from 17 minutes
to well under one. `scripts/flipbook/ti_sim.py` writes the same `.npz` grids
`bake.py` does, so `render.py` and `pack.py` are untouched — the two solvers are
interchangeable stage-1s.

It also expresses the physics the owner specified, which Mantaflow's gas domain
cannot: a **radial** force, **curl noise**, **viscosity**, buoyancy at zero. The
Mantaflow preset had to fake the first with inflow-along-normals and the third
with dissolve speed.

**Four bugs, each one a mis-modelled force rather than a coding slip:**
1. *Lattice noise.* The first "curl noise" was sums of `sin(x)+sin(y)+sin(z)` —
   a periodic LATTICE, and it stamped a visible grid into the puff. Replaced by
   a real smoothed random field. Note the reasoning error behind it: the ban on
   `fract(sin(...))` is a **Mali shader** landmine (ENGINE_LANDMINES §4) and has
   nothing to do with an offline script.
2. *Noise frequency below feature size.* At 6 cycles across the domain the field
   is a large-scale flow that bends the whole puff into lobes.
3. *Viscosity as a brake.* `exp(-1.7 * dt)` applied EVERY substep killed the
   radial push within three steps and the puff never left its ignition volume.
   Viscosity is a slow drag; 0.22 works.
4. *Non-zero-mean noise = wind.* A smoothed random field keeps a large-scale
   bias, and a constant force applied every substep blew the puff sideways into
   a comet. Zero-meaning it per channel helps but is not sufficient — see below.

**Open:** the noise field's cell (N/8 = 8 voxels) is LARGER than the ignition
volume the owner asked for (radius 0.05N ≈ 3 voxels), so within the puff the
"turbulence" is still effectively uniform and pushes it off-centre. Fix is to
raise the noise field's resolution relative to the feature it perturbs, not to
lower its gain.

### Taichi solver — the radial force was in the wrong place (28/07/2026)

Raising `--radial` from 13 to 26 changed the sheet by nothing measurable, which
was the tell. The force was applied inside `add_fuel`, i.e. only within the
ignition volume (3 voxels) and only for the first 10% of the sheet. An expansion
needs a force on the EXPANDING MATERIAL, not on its source; it now lives in
`forces()`, scaled by `temp + 0.35*dens` so only hot gas is pushed and the
surrounding air is not. The source keeps a 15% launch kick.

Sequence of mis-modelled forces in this solver, all of the same species — the
term existed but acted on the wrong thing, at the wrong scale, or for the wrong
duration:
- viscosity applied per substep as `exp(-1.7*dt)` = a brake, not a drag;
- noise cell (8 voxels) larger than the feature it stirred (3) = uniform wind;
- noise with non-zero mean = wind again, at the domain scale;
- `sin`-lattice standing in for noise = a printed grid;
- radial force on the source instead of on the gas.

Puff now expands from a small ignition volume, stays centred (centroid 59,66 vs
centre 64) and keeps its structure. Cell coverage 6.3% against the smoke sheet's
19.6% — still small in frame, which is the next thing to tune (`--radial` and
frame count together, since expansion is now sustained rather than impulsive).

### Taichi puff — framing, resolution-independence, and the cooling law (28/07/2026)

Three more findings, each one a term that was right in spirit and wrong in form:

1. **Framing belongs to the RENDERER, not the solver.** Raising `--radial`
   barely moved cell coverage (6.3% → 6.8% → 7.4% for radial 3 → 6 → 10) because
   the sim needs a wide domain — a plume that touches a wall gets a box-shaped
   silhouette — while the SHEET wants the effect to fill its cell. New
   `render.py --zoom` crops toward the centre: coverage 7.4% → 24.1% with no
   change to the physics.
2. **Forces are measured in VOXELS per step, so a preset does not survive a
   resolution change.** The preset that filled 24% of the cell at res 64 filled
   85% at res 112 — the same numbers push gas further, relatively, on a finer
   grid. `ti_sim.py` now scales the advective terms by `64/N`, which is what
   makes quick-tuning transferable and kills the trap that cost a 17-minute
   Mantaflow run.
3. **`T^4` cooling stalls.** At T=0.3 it is 0.008, so once a puff drops below
   ~0.5 it essentially stops cooling and the sheet burns to the last frame
   regardless of the rate. Real cooling is radiative (T^4) PLUS convective
   (linear); the linear term is what finishes it.

**Current state:** puff expands from a small ignition volume, stays centred,
keeps its structure, and now hands fire over to smoke — but it dissipates by
about frame 40, leaving the last three rows of the sheet empty. The next lever
is the soot decay inside `cool()` (`dens *= 1 - 0.25*dt`, too aggressive for a
64-frame sheet) together with `fuel_frames`; both are sim-side, ~90 s per trial.

### Taichi puff — cauliflower (28/07/2026)

Owner: *"nó bắt đầu phân tán ra nhiều hơn rồi đó, phải làm cho nó hơi tròn tròn
giống súp lơ thì mới giống khói puff"*. Three changes, in order of effect:

1. **Radial became an IMPULSE.** Held on as a constant force it keeps
   accelerating every parcel outward and shreds the puff into filaments. A real
   puff takes its momentum at ignition and then coasts — which is what lets the
   lobes round back up. Envelope: full for the first 22% of the sheet, then off.
2. **Diffusion added** (`diffuse`). This is the term that makes billows convex:
   without it the density field keeps every filament the velocity field ever
   drew, so the silhouette is spiky. It is also the knob that trades "one smooth
   mass" (0.16 welded the lobes together) against "separable billows" (0.09).
3. **Curl halved** (2.2 → 1.1). Turbulence should add detail TO a shape; past a
   point it becomes the shape, and the result is a shredded cloud.

At res 112 the lobes are unmistakably cauliflower. Two things left:
- **`diffuse` is not resolution-scaled** (the advective forces are). It is a
  per-step fraction on the grid, so at res 112 it smooths relatively less and
  the lobes come out larger and fewer than at res 64 — the same class of trap
  as the force scaling, and the same fix.
- Flame is now only 4.8% of the sheet against smoke's 55.5%; the puff reads as
  smoke with a hot core rather than as fire. `cool` and `fuel_frames` are the
  levers.

### E4 puff — the framing, and three more terms measured in voxels (28/07/2026)

**Framing cannot be dialled by hand, because the measurement saturates.** At
`--zoom 1.4` the puff was clipped at 63% coverage, and every correction guessed
from a clipped sheet is blind: a puff twice too big and one 1% too big both
report "touching the border". `render.py --zoom auto` renders one uncropped
probe pass — where nothing can be cut — measures how far alpha 0.06 actually
reaches, and sets the crop from it. Exact arithmetic, one extra pass (~1.5 s).
`pack.py` now also reports `reach`, how many frames touch the border, and the
factor to multiply a manual `--zoom` by.

**`diffuse` needed the SQUARE of the resolution ratio, not `64/N`.** It is a
per-step fraction of the 6-neighbour average, so over a fixed step count it
smooths a length of ~`sqrt(k)` VOXELS: holding that length at a constant
fraction of the domain needs `k*(N/64)^2`, whereas a velocity in voxels/step
needs `64/N`. Same trap, one power apart.

**But diffusion was not the reason presets did not transfer.** Measured, res 112
vs res 64, same preset, 64 frames: scaled `diffuse` 44.2% coverage / lobes 1.08,
unscaled 40.7% / 1.15 — while the res-64 reference sat at 81.4% / 1.41. Two more
quantities were in voxels: the noise field's eddy was `N//3` cells, i.e. **3
voxels wide at any resolution**, so res 112 stirred features 1.8x smaller
relative to the puff; and the Jacobi count was fixed at 40, so the projection
reached a shorter fraction of a finer domain. Both now scale from `REF_RES`.

**The dominant cause was neither: at res 64 the puff was hitting the WALL.**
Measured in sim space (the one extent that does not pass through the renderer):
r90 = 1.27 of the domain half-width at res 64 versus 0.79 at res 112 — past the
inscribed sphere, i.e. mass in the box CORNERS. Advection samples are clamped at
the boundary, so material pressed against a wall is re-sampled from itself and
the solver **manufactures density**. A run in that state is not a smaller
version of the same effect and cannot be compared with anything. `ti_sim.py` now
prints `r90` and the wall-shell mass fraction every run and warns. With
`--radial 8`, where neither resolution touches the wall, the preset transfers:
lobes 1.08 vs 1.06, coverage 27.5% vs 21.5% (was 2.3x apart).

**Frame count is a physics axis, not a sampling axis.** `dt` is per frame, so
`--frames` changes how long the sim runs: identical preset at res 64, 24 frames
→ 64 frames took coverage 18.1% → 89.5%, with 57/64 cells clipped. A 24-frame
probe shows an EARLIER MOMENT of the effect, not a cheap version of it — the
frame count must match the sheet being tuned for.

**Lobe count is `--eddy`, not `curl`.** Curl is the amplitude, eddy the scale;
raising curl to get more billows mostly transports the puff (curl 2.4 pushed
r90 from 0.86 to 1.00, into the wall) while `--eddy` 21 → 44 changed lobes by
0.04 at curl 1.1 — turbulence too weak to imprint at any scale. Both knobs
exist now, and `lobes` (isoperimetric perimeter/(2*sqrt(pi*area)), 1.0 = one
round blob) is the number that reads cauliflower, independent of size on screen.

**`fire_puff` shipped** — `assets/textures/fire_puff_8x8.png` + `_flame`/`_smoke`
splits, 2048x2048, res 112 / 64 frames / 57.6 s:

| | before | now |
|---|---|---|
| cell coverage | 63% (clipped) | 22.3% (target ~20%) |
| frames touching the border | most | 0/64 |
| flame : smoke | 4.8% : 55.5% (1:11.6) | 4.5% : 18.1% (1:4) |
| sim wall-shell mass | — | 0.1% (clean) |

Fire came up via `cool` 0.5 → 0.22 and `fuel_frames` 0.16 → 0.35: at 0.5 the
flame was out by frame 20 of 64, so the sheet was smoke with a hot core.

**Two warnings were firing on a correct sheet, and that is a defect of its own.**
`height/width > 1.3` is a check for a rising COLUMN and is nonsense for a puff
(`pack.py --shape puff`), and the autofit's "silhouette is the box" test used the
visibility threshold — a ray crossing the whole domain accumulates alpha 0.06
from haze alone, so it fired at reach 0.996 on a sim whose mass audit said
r90 0.79 / wall 0.1%. It now tests opaque material (alpha > 0.5).

### F3 — FlameVolume takes the puff sheet as its building block (28/07/2026)

`flame_atlas` is now three-valued: **0** = the F2 round sprites, **1** = the new
`fire_puff_8x8_flame` (default), **2** = `fire_atlas_8x8_flame`, the column. The
two sheets are different SCALES of the same effect, not two qualities of it —
the column bakes an entire fire into one sprite, so several of them merge into
one mass, while a puff is one billow and several of them scatter into the bed of
separate tongues the owner's reference photo shows. A missing file falls through
to the other sheet, not to the F2 sprites: an effect that silently changes scale
is harder to diagnose than one that silently changes look.

Three things the puff sheet changes, each for a reason in the sheet itself:

- **Rotation is legal again.** It was disabled for the whole atlas path because
  the column has an UP and spinning it renders the fire upside down. The puff
  was simulated with buoyancy AND gravity at zero, so it is radially symmetric
  by construction — spinning it is what stops ten sprites off one sheet from
  reading as ten copies of one billow. Rotation is a property of the SHEET, not
  of the atlas path.
- **Ten sprites instead of six, each smaller** (0.20–0.32 vs 0.30–0.46). The
  overdraw budget that capped the column at six is unchanged; a billow simply
  does not need to be read at whole-flame size.
- **The flipbook rate is derived, not written down.** `FVOL_BODY_LIFE_MAX` is
  now the constant the body lifetime and the sheet's fps both come from
  (64 / 1.40 s), so a sprite plays the sheet exactly once over the longest life
  it can be given. The column's hand-written 1.7 could not drift into a bug
  because it was slower than the longest life; deriving it removes the class.

Builds clean, core suites 6/6.

### E4 — `smoke_puff`, and the retirement of the 19.6% "sheet that works" (28/07/2026)

Owner: *"smoke_atlas_8x8.png không chuẩn"*. Re-auditing it says how, and the
audit could not have said so before today (`pack.py <sheet.png>` now re-audits a
shipped sheet in place — a sheet that shipped before a measurement existed has
never been held to it):

| | smoke_atlas_8x8 (old) | smoke_puff_8x8 (new) |
|---|---|---|
| lobes | 2.31 — shredded, not billowed | 1.08 |
| channels | R = G = 21.0%, i.e. ONE greyscale channel tripled | flame 0.7% · smoke 18.4% |
| framing | reach 0.75 — a quarter of the cell wasted | 0.98, 0/64 clipped |
| sim wall contact | unknown, predates the audit | 0.0% |

Identical R and G is the tell that it predates the channel layout entirely: it
is a Cycles greyscale render, so it cannot express "thick but cool" apart from
"hot", which is the whole reason the pipeline marches the grids itself.

**The 19.6% target is deleted from `pack.py`.** Every coverage figure in E4 was
printed against "the smoke sheet, which works: 19.6%" — a calibration taken from
the sheet the owner has now rejected. A constant that came from a rejected
artifact is worse than no constant: it made 22% look like a pass. Coverage is
still reported; what it should be is a judgement about the effect.

**`smoke_puff` preset** — the fire_puff skeleton with combustion turned into an
instant hand-off (`cool` 3.0, `soot` 1.0: heat becomes density within a few
frames, so the sheet is smoke from frame one instead of fire that fades), more
diffusion, and **buoyancy near zero even though smoke rises**. The rise belongs
to the PARTICLE: bake it into the sheet and the engine's own upward velocity
double-counts it, while the puff drifts off the cell centre and the autofit crop
— symmetric about that centre — pays for the empty half.

**Two sheets, two contracts, and the wiring must know which.** The new sheet is
a MASK (white RGB, density in alpha) whose value comes from the lighting pass —
the F1b split, same as the flame. The old one is pre-shaded, which is why
`vc_smoke_puff.inl` lifts the vertex colour to ~160 for it; applying that lift
to a mask lights it twice, and skipping it on a pre-shaded sheet is the measured
33/255 black smudge. `s_smokeFbMask` now carries the distinction, and the old
sheet stays as the fallback.

`assets/textures/smoke_puff_8x8.png` + `_flame`/`_smoke`, res 112 / 64 frames /
67.3 s. Builds clean, core suites 6/6.

### E4 — "những mảng màu riêng biệt": a flat mask cannot be a volume (28/07/2026)

Owner asked whether the patchy smoke was the blend mode. It was not, and the
measurement says so without a rebuild: the sheet's RGB **value spread (p10..p90)
was 0.00** — literally flat white. A stack of flat plates reads as overlapping
cards no matter how faint each one is made, and the per-sprite alpha was already
0.28.

The plateau in the alpha channel is real but is not the cause: 61.6% of lit
pixels sat above 0.6, and dropping `--density-scale` from 7 to 1.5 only moved
the median 0.72 → 0.54. It cannot be tuned away either, because it is physical —
the column density through a fat cauliflower puff IS near-constant across its
middle. Real smoke reads as volume for a different reason: it is SHADED.

**`render.py --light`: the volume's own shadow, from a prefix sum.** With the key
light straight overhead, the light ray IS the grid's z axis, so the optical depth
above every voxel is one cumulative sum — O(N^3) once per frame, instead of a
second march per sample. The marcher accumulates the same integral weighted by
that transmittance and writes it to **B, the channel the layout reserved for
lighting**. `pack.py --split` then puts **B/G** in the smoke sheet's RGB: B and G
share a normalisation, so their ratio is exactly the fraction of light that
survived, with the density falloff divided back out. Multiplying density in twice
(RGB and alpha) is what turns a thick puff into a silhouette.

| sheet | value spread p10..p90 |
|---|---|
| smoke_puff, unshaded mask (what shipped this morning) | 0.00 |
| smoke_atlas_8x8 (the rejected sheet) | 0.31 |
| smoke_puff, self-shadowed | **0.69** |

**The flame sheet deliberately does NOT get this.** Fire emits; it is not
shadowed by anything, and its colour comes from the black-body ramp at the call
site — so its split stays white-RGB-plus-alpha. Two sheets from one pipeline with
two different contracts, and `vc_smoke_puff.inl`'s existing "the sheet is already
lit" path is the correct one for the smoke sheet (its near-black gradient exists
to be lifted by the lighting pass for FLAT sprites; a shaded sheet needs the
vertex colour to step back and only tint — measured 33/255 black smudge when
that is got backwards).

Re-shipped: `smoke_puff_8x8.png` + splits, coverage 22.5%, lobes 1.08, 0/64
clipped. Builds clean, core suites 6/6, `selftest.py` PASS.

### Đợt E — where the plan stands, and what should come next (28/07/2026)

**Part A is complete.** F1 (lit particles + blend law), F2 (smoke), F3 (fire),
F4 (character aura, three layers) are all in. **Part B**: E1 post-FX LANDED,
E2 VFX light bleed RESOLVED, E3 `VFX_Sequence` LANDED, E5 batch 1 complete
(GlintSparkle, RuneCircle, ChargeConverge, DissolveExit). E4 now ships real
sheets rather than a plan (`fire_puff_8x8`, `smoke_puff_8x8`, both accepted).

**F0 (the purge) is the owner's, and is happening now** — the survivor set is
`VFX_ComposeSmokePuff` and `VFX_ComposeFlameVolume`, the two just rebuilt.

That leaves, from the spec: **E6 batch 2** — and every one of its dependencies
is now satisfied (SweepSlash needed E4, ImpactPackage needed E3, LightShaft
needed E1) — then **E7**, the retrofit stop-gate, then **E8** platform/perf.

**Ranked by how many future moments each unlocks, not by spec order:**

1. **`VFX_ComposeImpactPackage` (E6 #6).** The one effect every skill, minion
   death and boss hit routes through; it is also what tells a player their hit
   LANDED. `vc_impact.inl` exists but predates F1/E3 — 175 lines, no sequence,
   no lit population.
2. **A `dust_puff` sheet.** The pipeline's next sheet should be dust, not
   another fire: it is what an impact package, a footfall, a landing, a fissure
   and any Earth skill all need, and it is the same `smoke_puff` skeleton with
   a shorter life and no soot hand-off. ~90 s of sim + the audit.
3. **Clash VFX.** Đấu Pháp resolves a 5x5 element matrix (`combat/docs/API.md`)
   and the moment two projectiles meet — the signature gameplay beat — has no
   composition of its own. It needs E3's envelope more than it needs new
   particles.
4. **Telegraph / anticipation.** E3 gives the envelope but nothing draws the
   *anticipation* phase, which is the half of ER readability the spec's own
   §0.1 names. A ground-decal wind-up per element is small and reusable.
5. **`VFX_ComposeSweepSlash` (E6 #5)** — was blocked on E4, now unblocked; the
   sheet it wants is a thin arc, which the pipeline does not yet author (its
   domain is cubic and its physics radial). Costs a new sim shape, not a preset.
6. **E7 stop-gate** once 1–3 are in: retrofit three pilot skills and A/B them.
   The spec is explicit that this is where the plan is proven or re-scoped.

**Deliberately NOT doing** (owner's call, 28/07/2026): re-enabling FlameVolume's
own smoke layer (`flame_volume.inl:345`, still commented) — fire reads well
enough as it is; and retrofitting the other smoke emitters (`vc_smoke_energy`,
`burning_ground`, aura) onto the flipbook, since F0 may delete them. Both would
otherwise be correct: the flat-mask defect measured this session applies to
every one of them.

**Pipeline housekeeping done with this entry:** `scripts/flipbook/README.md`
rewritten around the Taichi solver, and `bake.py` / `make.py` / `fb_presets.py`
deleted — Mantaflow measured 1037 s where this takes ~60 s, and could not
express a radial force, viscosity, or buoyancy at zero. `git log` has them.

### E4 — `dust_puff` shipped, and the vertical axis was the wrong one (28/07/2026)

`assets/textures/dust_puff_4x4.png` + `_smoke`, 1024x1024, res 112 / 64 frames.
Coverage 18.1%, lobes 1.02, 0/16 clipped, wall-shell 0.0%, value spread 0.73.

**The solver's UP was the camera's DEPTH axis.** `forces()` applied buoyancy to
`v.y`, while the renderer's image-up is the grid's **z** (`gz = (1-v)*(rz-1)`)
and its ray marches along y — so buoyancy pushed the puff straight away from the
camera. Measured before the fix, buoyancy 40: centroid 27.8 → 43.8 along the
view ray, 24.1 → 22.5 along the render's up. After: 27.7 → 43.2 up, view ray
flat. It survived this long only because both shipped puffs have buoyancy ~0 —
`fire_puff` 0.0 and `smoke_puff` 1.5, whose "slight rise" was in fact a slight
drift into the screen. The `fire` and `smoke` column presets were nonsense and
had never been baked from this solver.

**Two terms dust needs that no gas preset has:**
- `flat` squashes the vertical component of the radial push. Impact dust spreads
  along the ground rather than inflating a ball, and at 0.20 the sheet measures
  **height/width 0.87** — wider than tall, which is the shape a viewer reads as
  "something hit the floor".
- `gravity`, on DENSITY rather than temperature. Buoyancy cannot express
  settling: it scales with `temp`, and dust is cold, so a negative buoyancy does
  nothing at all.

**Gravity had to stay SMALL, for the same reason buoyancy does.** At 4.0 the dust
piled onto the domain floor and the boundary clamp manufactured **16.4%** of the
total mass there. The fall belongs to the PARTICLE; what a sheet can carry that
a rigidly-moving sprite cannot is the internal asymmetry. Known limit: even at
1.0 the lateral spread grazes the SIDE walls at res 64 (1.4%) — the real fix is
a wide, short domain, and the solver is cubic.

**A smaller grid is a PACK decision, not a sim one.** The spec asks for
`fb_dust_4x4`; simulating 16 frames would have produced an earlier moment of the
event rather than a coarser sampling of it, so the sim runs the full 64 and
`pack.py --stride 4` subsamples. `--split` now also skips a channel whose
coverage is under 1% — a cold effect has no usable flame channel, and writing it
puts a file in `assets/` that looks like an asset and is not one.

**E4's DoD, honestly:** flipbooks 3/4 (`fire_puff`, `smoke_puff`, `dust_puff`;
`spark_burst` is a PARTICLE effect, not a fluid, so it belongs in a composition
rather than in this pipeline). Still absent: glint/streak masks, `arc_slash_*`,
`sigil_ring_*`, distortion normal maps, erosion masks — all with working
procedural fallbacks in their consumers, as the spec requires. `assets/INDEX.md`
now has an entry per sheet, including why `smoke_atlas_8x8` is superseded.

### E6 #6 — `VFX_ComposeImpactPackage`, and where a cloud ends and a puff begins (28/07/2026)

Owner, on the dust preset: *"chúng ta đang làm flipbook cho 1 đám bụi nhỏ trong
1 đám bụi lớn, nên trọng lực hút xuống là việc của mô hình đám bụi lớn quyết chứ
không phải việc của flipbook"*. Correct, and it invalidated more than the gravity
term: the FLATTENING went with it. A wide, low, ground-hugging shape is a
property of the CLOUD — of where the composition puts its sprites — not of a
parcel a few centimetres across, which expands isotropically. Both terms were
solving the cloud's problem inside the sheet, where they would then be applied a
second time per sprite.

`dust_puff` re-baked with `gravity 0`, `flat 1.0`: height/width **0.99** (a
round parcel, as it should be), lobes 1.04, 0/16 clipped, wall-shell 0.0%. What
still separates dust from smoke at this scale is small and honest — dust is
GRAINIER (eddy 48 vs 34, diffuse 0.035 vs 0.06) and its shape stops evolving
sooner (viscosity 0.85 vs 0.30, a shorter fuel window), because heavy particles
lose their momentum quickly while smoke keeps rolling for the whole sheet.

**The package** (`core/composition/common/vc_impact_package.inl`) is authored as
a `VFX_Sequence`, which is what E3 was built for — the beats and their offsets
ARE the effect:

    0.00  LIGHT     flash — the beat that says the hit registered; never delayed
    0.00  HITSTOP   gated at severity 0.45 (a light hit that freezes reads as a dropped frame)
    0.00  COMPOSE   element debris — additive, unlit, ballistic (its own 9.81 field)
    0.02  COMPOSE   the dust cloud — one frame behind, so the eye sees cause then effect
    0.03  DISTORT   gated at 0.35
    0.04  DECAL     the only beat that outlives the moment; lifetime scales with severity
    0.05  SHAKE     gated at 0.90

The cloud carries what the sheet gave up: a RING (not a disc — a filled disc
puts most of its sprites where they only occlude each other), velocity outward
ACROSS the surface rather than up (dust thrown up reads as an explosion, dust
thrown out reads as an impact), and a gentle 1.1 m/s² sink against a 2.4 drag,
which is what makes it spread and then stop. `normal` builds a tangent basis, so
a hit on a wall throws dust off the wall — hard-coding the ground plane is what
makes every impact in a game look like it happened on a floor.

**Shake is gated at 0.90 rather than removed.** The project decided against
per-skill screen shake (reserved for a boss ultimate), but a package that cannot
express the beat at all cannot express a boss ult either; the gate says "not for
ordinary hits" in a way a deleted line cannot.

Wired into `sandbox/vfx_test.c` (NEW FX → "IMPACT PKG", severity 1.0 so every
gate is open). Builds clean, core suites 6/6.

**Known gap:** the DECAL beat uses the sequence's default texture — the old
`vc_impact.inl` had its decal and light calls commented out, so there is no
established dust-scuff decal to point at. `assets/textures/decals/` has burn and
crack only.

### E6 — energy burst: a particle SYSTEM, not one big flipbook (28/07/2026)

Owner: the impact's important half is the ENERGY EXPLOSION, dust is only
accompaniment — and *"làm 1 flipbook lớn ko thực tế"*, with a proposed design: a
low capless cylinder emitter, centrifugal launch, the outward push giving way to
viscosity and curl past a set radius, the burst existing as the superposition.
That design is right, and it is what shipping games use for repeatable gameplay
VFX (baked hero explosions exist, but they are one fixed silhouette, one scale,
~16 MB, blind to the scene). The engine already had all four terms.

**The one thing the design was missing, and the screenshot had already proved
it:** a round puff sprite cannot make a radial filament however it moves. The
dust ring made exactly the lumpy necklace that predicts. The fix is
`ParticleConfig.stretchStrength` — velocity-stretched billboards, so each
particle IS a streak along its own motion and the superposition reads as a
starburst. Every other term decides where the streaks go; this one decides that
they are streaks.

**"Viscosity increases past a radius" is expressible as "the push runs out".**
`FORCE_GRAVITY_POINT` with a negative strength, a `radius` and `falloff 1.0` is
full at the centre and absent beyond `reach`; the curl and drag layers are
constant, so they simply become the whole story once it has faded. Nothing has
to ramp up, and nothing needs a term the force field cannot express.

`VFX_ComposeEnergyBurst(pos, mat, scale, intensity)`, wired as the LEAD beat of
`VFX_ComposeImpactPackage` at t=0.005 (a hair after the light, so the flash is
what the eye catches first). Dust dropped to 8 sprites at alpha 135 with much
wider angular and radial jitter — an evenly spaced ring reads as a necklace, and
the giveaway is that you can count the sprites. Borrows the smoke flipbook as a
placeholder sheet by the owner's decision; a dedicated sheet should be a WISP
(thin, elongated, sharp ends), since stretching a round puff only goes so far.

**What the flipbook route measured, before it was abandoned.** An `energy_burst`
sim preset is in `ti_sim.py` and inverts the smoke presets term by term
(`diffuse` ~0 — diffusion is what rounds filaments into billows; `shell 0.72` —
a detonation ignites a SURFACE, and the reference's dark core is where the fuel
never was; fine `eddy`; a violent brief `impulse`). Three findings worth keeping:

1. **The promising number was an artifact.** The first run measured lobes 2.19
   (filaments!) — with 38.2% of its mass pressed against the domain wall. The
   jagged silhouette was the BOX. Clean runs measured 1.04–1.25, i.e. a smooth
   ball, and no combination of `radial`/`dt`/`impulse` reached the reference.
2. **Filaments need total advected DISTANCE, which is what hits the wall.**
   Cutting `dt` to fit the event inside the domain removes the very stretching
   that draws the streaks: radial 22 → 12 took lobes 2.19 → 1.20.
3. **A fine-eddy preset cannot be probed at low resolution.** `eddy 60` across
   the domain at res 64 is ~1 voxel per eddy — below the grid, so the
   turbulence is unresolved noise. The parameter is resolution-INDEPENDENT and
   still unusable below a minimum grid; those are different properties.

New solver knobs from this round: `shell` (hollow ignition), `impulse` (how long
the radial impulse lasts, replacing a hard-coded 0.22), `fuel_dens` (density
injected with the heat — low = energy, high = fire that becomes soot), `dt`
(simulated time per frame: how much of an EVENT the sheet covers).

**Camera shake removed from ImpactPackage**, and the rule recorded: shake is
never added on a VFX's own initiative — a severity gate is not permission.

### E6 energy burst — the smoke sheet stays; growth is the effect (28/07/2026)

Owner: the smoke flipbook already reads well, so the planned energy-WISP bake is
dropped — more particles instead, born tiny and growing, with size and opacity
randomised per particle.

Why the smoke sheet works here, worth keeping since it decided against a bake:
its RGB carries the puff's own self-shadow, so every streak has internal
brightness variation a flat mask cannot have, while velocity-stretching plus
additive overlap supply the direction its round silhouette does not.

- **Growth curve 0.14 → 2.40** (was 0.55 → 1.70). The eye reads the RATE of
  growth; a sprite that starts near its final size only translates.
- **Lifetime 0.38–0.78 s** (was 0.30–0.55). At 0.30 s that growth happens faster
  than it can be resolved and the burst goes back to looking like sprites
  appearing at size — the curve and the lifetime are one parameter, not two.
- **Count 40–80** (was 18–40), sizes `Mix(0.07, 0.34)` weighted to the small end
  (pow 2.2) and alpha randomised 0.45–1.0 per particle, independently of size.
  A cloud whose sprites share a size reads as one object cut into pieces.

**Fill rate is the thing to watch**, and `count` is the lever: additive flipbook
sprites draw TWICE (the atlas cross-fade), and F3 measured 22 fps from
twenty-two LARGE ones. These are a third of that size and shorter-lived, which
is what buys the count — it is not headroom that was always there.

### ImpactPackage — the cloud takes the SMOKE sheet, not the dust one (28/07/2026)

Owner, on the impact still looking wrong: the smoke flipbook was the one to use.
The lead energy burst was already on it; the trailing CLOUD was still bound to
`dust_puff_4x4_smoke`, and that sheet is baked grainy on purpose — fine eddies
(48 across the domain), almost no diffusion (0.035) — because that is what
separates dust from smoke AS A FLUID. On a sprite, that grain reads as a torn,
stringy edge, and a dozen torn edges overlapping is what looked bizarre. The
smoke sheet's rounder, self-shadowed billows carry the same motion without the
fray.

The lesson generalises past this case: **a property that distinguishes two
fluids in a simulation is not automatically a property worth having on a
sprite.** Grain is real dust physics and it is the wrong thing to magnify onto a
half-metre billboard.

`dust_puff_4x4` stays on disk with no consumer — it is the sheet to come back to
if a gritty rock impact ever wants exactly that quality. The cloud now also uses
the smoke sheet's four-rate table, so the sprites do not step to each new billow
in lockstep.

### ImpactPackage stripped to one population (28/07/2026)

Owner: *"giờ impact chỉ cần khói, di chuyển theo tôi mô tả là đủ, ko cần gì
hết"*. Removed, in order of how wrong each was:

- **Velocity stretch on the burst sprites** — the "trail". This file had argued
  it was the load-bearing term, on the grounds that a round sprite cannot make a
  radial filament however it moves. That argument was reasoned from the
  reference IMAGE rather than from the effect on screen: with the smoke sheet,
  enough particles and the size ramp, the burst already reads, and the stretched
  version read as a smear behind each sprite. The premise was not wrong about
  filaments — it was wrong that this effect needed them.
- **The separate dust cloud** — a second smoke population with its own sheet,
  field, gradient and curves, doing what the burst already does.
- **Ballistic debris sparks** — they said "what was hit"; nothing had asked for
  that to be said, and they competed with the burst inside a tenth of a second.
- **The motionless core-flash sprites** in the burst — a flash belongs to the
  LIGHT beat, which costs no overdraw and reads brighter than a sprite can.

The file went 300 → 120 lines. What is left is a light flash, the burst, a
distortion, a decal, and a gated hitstop — beats that own their own state, so
`ImpactPkg_InitShared` now builds nothing.

**The pattern behind all four removals, worth naming:** each was reasoned from a
reference image rather than from what was on screen, and each added a population
that competed with the one doing the work. A package is stronger with one
population and clear timing than with four inside a tenth of a second.

### Energy burst — two phases, and sprite size derived from the ring (28/07/2026)

Owner, on the first working build: the ring forms correctly, but the sprites are
too small to OVERLAP, and once the ring exists the centrifugal force should be
essentially zero — the smoke passes into a chaotic phase and dissipates, and
THAT is the explosion.

**Phase 1 builds the shape, phase 2 is the effect.** The force field already
expressed the hand-off (a repulsor with `radius` + `falloff 1.0` is absent
beyond `reach`), but phase 2 had nothing to take over with: curl 2.2–4.5 against
drag 1.5 left the ring coasting outward and thinning, which reads as a smoke
ring rather than an explosion. Now curl 5.0–9.5 in TWO layers — coarse
(noiseScale 0.55) to roll whole sections of the ring, fine (2.2) to break their
edges — against drag 1.1, enough to end the outward flight without damping the
churn. Lifetimes went 0.38–0.78 s → 0.85–1.45 s, since the flight to the ring
alone costs ~0.3 s: roughly two thirds of each life should now be phase 2. The
alpha curve was holding 0.30 by mid-life, i.e. the churn was happening behind a
fade that had already run; it now holds 0.80 to 55% of life.

**Sprite size is now DERIVED, not picked.** The burst exists only as the
superposition, and superposition needs overlap: on a ring of radius `reach`
carrying `count` sprites, neighbours sit `2*PI*reach/count` apart, so anything
smaller leaves gaps — the necklace of separate puffs the owner photographed. The
size is computed from that spacing with an overlap factor of 1.30, divided by
the growth curve's mid-life value because the number set is the radius at BIRTH
while the overlap has to hold on ARRIVAL. Measured at intensity 0.9, scale 1.0:
count 76, spacing 0.182 m, radius at the ring 0.15–0.37 m, **diameter 2.6x the
spacing, ring coverage 2.6x**. Randomisation is now around that derived mean
rather than around a constant, so the spread still breaks up the ring but cannot
move the mean off the overlap.

The point of deriving it: changing `count` or `reach` later cannot silently
break the overlap again.

### The flipbook was playing — every sprite was playing the SAME frame (28/07/2026)

Owner: *"sao nó như ko có đổi sprite, mà như 1 cái ảnh di chuyển vậy?"* The
animation was running. `SpriteAnim` derives its frame from the particle's
ABSOLUTE age, and a burst spawns every sprite in the same instant, so all of
them held the same frame for their whole lives. Under ADDITIVE with one tint,
three dozen identical frames overlapping is one image being moved around.

`vc_smoke_puff.inl` had already met this and worked around it with four
templates at different RATES. That only splits a burst into four identical
groups — nine sprites per frame here — and they all still start at frame 0
together. The comment there says as much ("SpriteAnim has no per-particle frame
offset"); the workaround was treated as the fix for long enough that the next
consumer inherited the bug.

**`ParticleConfig.spriteAnimPhase`** (seconds, default 0 = old behaviour) is the
actual fix, in the engine where the limitation is: the renderer now reads
`age + phase`. Every flipbook consumer can use it.

The rate then has to be re-derived, because phase spends sheet: the longest life
PLUS the largest phase must still land inside 64 frames, or the sprite runs into
the empty tail and vanishes while its alpha says visible (the E4 landmine, third
time). The burst uses its own templates at 64/2.4 s x {1.0, 0.92, 0.85, 0.78} —
slowest 20.8 fps reaches frame 48 at (1.7 s life + 0.6 s phase), fastest
26.7 fps reaches 61. Both inside the sheet.

Also added, per this module's own rule about silent paths: EnergyBurst now warns
once if the smoke flipbook is not loaded. A burst of static sprites and a burst
whose flipbook failed to load are the same picture, and only one of them is a
bug.

**`VFX_ComposeSmokePuff` still has the lockstep**, and could take the same
phase — left alone deliberately: the owner likes how it looks now, and this is
a change to a shipped effect, not a fix to a broken one.

### Energy burst — the motion the owner actually specified (28/07/2026)

Owner's correction, and they were right that their own earlier idea was wrong:
*"lực li tâm ban đầu hơi mạnh ... sau đó giảm dần về 0 + 1 xíu nhiễu ... chứ ko
cần curl noise mạnh làm nó bay lung tung"*.

**The impulse is a launch VELOCITY, not a force.** "Strong, then decaying to
zero" is exactly what a launch speed against drag does, so the repulsor field
went away entirely: with drag k, speed decays as exp(-k*t), and at k = 4.5 the
outward motion is a third of launch by ~0.35 s and gone by ~0.8 s — inside the
sprite's life, so the ring visibly SETTLES instead of drifting off. The ring's
final radius is therefore launch/drag (~0.9 m at scale 1): move the ring by
changing the SPEED, not by tuning a separate radius. Curl dropped 5.0-9.5 in two
layers to 0.6-1.3 in one — "một xíu nhiễu", not a force of its own.

Count 28 -> 60: SmokePuff spreads its 28 across a DISC, while a ring has to
cover a circumference, so the same density needs more of them. Rotation is
random at BIRTH with no spin after it — the angle is what stops one sheet read
sixty times from showing sixty copies of the same silhouette, while continuous
rotation on a billow that is already rolling reads as churning (SmokePuff turns
its own flipbook spin down to 0.12x for the same reason).

**A diagnostic gap closed, because "every sprite holds one frame from birth to
death" had two indistinguishable causes.** Both `SmokePuff_InitShared` and the
burst now log which path they took — which sheet loaded, or that none did and
the static sprites are in use. A silent fallback from the new sheet to the old
one, or from either to the three static cutouts, looked exactly like a working
flipbook on screen; the effect being static was the only symptom, and it is the
symptom of three different faults.

**Not verifiable from this side:** `./build/wuxing --render-vfx 57` dies with
`FATAL: RLVK: instance creation failed` outside the owner's graphical session,
so the burst cannot be run headless here the way the flipbook audit can. The log
line is the instrument instead.

### Soft particles reach the PARTICLES (28/07/2026)

Owner, on the burst: the long-standing problem is billboards being cut where
they meet scene geometry, and the bigger the sprite the more it shows. The
screenshot is a textbook case — dead-straight horizontal slices through every
large sprite where the quads cross the ground plane.

**The feature already existed and no particle had ever used it.** "Item 3 — Soft
Particles (RESOLVED)" at the top of this file is real: `ScreenDistort` snapshots
the previous frame's linearised scene depth, `soft_particle.glsl` exposes
`SoftParticle_Factor()`, and it was proven end-to-end — on a bespoke test shader
in `skills/taiji/core_test/`. The CPU particle path draws through
`particle_lit.fs`, which never included it. A capability can be finished,
documented and shipped, and still not be CONNECTED to the thing it was built
for; "resolved" meant the mechanism worked, not that anything used it.

Wired now in `particle_lit.fs`, and applied to `base.a` BEFORE the emissive
early-out — emissive sprites (this burst, sparks, glints) are exactly the
population that shows the cut worst, being large and bright, and they never
reach the lit path at the bottom of the shader.

**`u_softFade` is 0 = OFF, and the C side is what raises it.** The failure mode
otherwise is total: an unbound sampler reads 0, so the factor is 0 everywhere
and EVERY particle in the game vanishes. `ParticleLighting_Begin` only binds and
raises it when `ScreenDistort_GetDepthTexture()` is a real texture, and the
shader treats 0 as "feature off" rather than "fully occluded". It logs the state
on change, so "the cut is still there" stays separable from "the fade is on and
0.45 m is too small".

`particle_soft_fade` (metres, default 0.45) is a tunable — no rebuild to sweep
it. Depth is one frame stale by design (sampling the depth buffer you are
writing to is undefined); for a fade that is invisible.

**The wiring test caught the change**, which is the point of it: the four depth
uniforms are uploaded by `ScreenDistort_BindDepthForSoftParticles`, not by
`particle_system.c`, so `shader_uniform_wiring_test` reported them unwired. Fixed
by adding `core/screen_distort.c` to that shader's source list — the check stays
strict, it just knows where to look. Suites 6/6.

### Soft particles — a debug view, because "still cut" has three causes (28/07/2026)

Owner, after the wiring landed: the cut is still visible — is it simply
unavoidable when a billboard intersects geometry? No. That is precisely what
soft particles remove, so something is not working, and "still cut" has three
causes that look identical on screen:

1. the feature is OFF (no depth texture — the C side refuses to raise
   `u_softFade` without one, since an unbound sampler reads 0 and would erase
   every particle in the game);
2. it is ON but 0.45 m is too short to hide the cut on a 1 m sprite;
3. it is ON and sampling the WRONG depth — most plausibly a `u_resolution` that
   does not match the render target actually being drawn into, which sends
   `gl_FragCoord.xy / u_resolution` to the wrong texel.

`particle_soft_debug = 1` (tunable, no rebuild) paints the factor instead of the
particle: GREEN where nothing is behind the fragment, RED where it is fading.
The three cases are then distinguishable in one look — a red BAND along the
floor line means the fade is working and only its distance is wrong; a flat
green sprite means the factor is 1 everywhere and the depth being sampled is not
the scene's; and the log line already separates OFF from ON.

The view deliberately uses `max(u_softFade, 0.5)` so it still answers "what does
the depth buffer say here" when the fade itself is off — a debug view that goes
blank in the failure case it exists to diagnose is the one mistake this module's
notes keep recording (§6 of core/CLAUDE.md).

**Ruled out already:** particles writing depth and cutting each other.
`main.c:1845` disables the depth mask around the whole particle pass, with the
batch flushed either side.

### OPEN — soft-particle fade still shows the cut (28/07/2026)

Wired, builds, logs its state, has a debug view — and the owner still sees the
hard intersection line. Parked by their decision to do performance first. When
it resumes, the debug view (`particle_soft_debug = 1`) answers it in one look:
a red BAND along the floor line = working, distance too short; flat green =
factor 1 everywhere, so the sampled depth is not the scene's (suspect
`u_resolution` versus the render target actually bound — `gl_FragCoord.xy` is in
the CURRENT target's pixels, and the particle pass draws inside ScreenDistort's
`renderTex`, which under HDR may not be the screen's size). Already ruled out:
particles writing depth (main.c:1845 disables the mask around the whole pass).

### PERF — the flame was emitting per FRAME, not per second (28/07/2026)

Owner: ten energy bursts drop the game to 20 fps, and a single FlameVolume is
"tụt fps thê thảm" on its own. The flame's cause is a bug, not a budget.

**`VFX_ComposeFlameVolume` spawned a fixed count per CALL, and the caller calls
it every frame.** No `dt` anywhere in the file. Two consequences:

1. **Frame-rate dependent emission.** 13 sprites/frame is 780 per SECOND at
   60 fps and 260 at 20. The fire changed density with the frame rate — and it
   "settled" at ~20 fps because emitting less is what let the frame rate
   recover. That is a feedback loop, not a budget.
2. **The live count is rate x lifetime**: 600 body/s against a 1.075 s average
   life is ~645 live body sprites plus ~54 core, for ONE flame. Each is a large
   blended quad that the atlas cross-fade draws TWICE — about 1400 quads per
   frame from a single campfire.

Emission is now a RATE derived from a LIVE-COUNT target, which is the quantity
an artist can actually see: `rate = live / averageLifetime`, with an accumulator
carrying the fraction so 0.25 sprites per frame emits one every fourth frame
rather than rounding to zero. Target 14 live body + 4 core with the puff sheet.

| | before | after |
|---|---|---|
| emission | 13/frame (780/s at 60 fps) | 14 body + 4 core LIVE |
| live sprites | ~700 | ~18 |
| quads/frame | ~1400 | ~36 |

`FVOL_BODY_LIFE_AVG` / `FVOL_CORE_LIFE_AVG` sit next to the lifetime ranges they
average, because the rate is derived from them: change a lifetime without
changing these and the live count silently moves. They are two halves of one
number. A per-frame clamp (8 body, 4 core) keeps a long hitch from dumping a
hundred sprites in the frame after it.

**Second lever: the cross-fade now sheds under load.** It draws a second
full-size quad for every animated particle, to hide the ~2-render-frame step of
a 25 fps atlas. That is a subtle artifact; the second quad is a literal doubling
of the most expensive thing on screen. Past `particle_fb_blend_max` live
particles (default 220) the fade is dropped — an exact 2x fill saving on the
frames that need it, nothing on the frames that do not. Nobody sees the step in
a screen full of overlapping sprites; everybody sees 20 fps.

**Still to do, in the order they are likely to pay** (the burst is one-shot, so
it has no rate bug — its cost is 60 sprites x 10 bursts x 2 quads):
1. Measure before tuning further — `ParticleSystem_GetStats` already reports
   live/max, and the number to watch is live count x average screen area, not
   particle count.
2. The burst's `count` (60) and its sprite radius: fill scales with the SQUARE
   of the radius, so 0.8x radius is a 36% saving where 0.8x count is 20%.
3. `particle_soft_fade` adds a dependent texture fetch per fragment on a
   fill-bound pass; worth A/B-ing once it works.
4. Bloom over large bright additive areas (`post_fx.c`) is the other fullscreen
   cost that scales with exactly this content.

### PERF — two wrong guesses, then an instrument (28/07/2026)

Owner: the rate fix changed the frame rate very little, and it left FlameVolume
as "những hạt rời rạc, nhỏ xíu, ko đánh giá được gì".

**Both of my perf changes were guesses, and the second one broke the look.**
Cutting one flame from ~700 live sprites to ~18 barely moved the frame rate —
which is itself the most useful measurement of the session, because it FALSIFIES
the overdraw hypothesis. If 700 large blended quads were the binding constraint,
removing 97% of them would not have been a small improvement. The emission-rate
fix was still correct on its own terms (frame-rate-dependent emission is a bug
whatever the cost profile), but it was sold as a performance fix and it was not
one.

`flame_body_live` is now a tunable (default 90, was hard-coded 14). A correct
UNIT with a wrong VALUE is still wrong, and 14 was never justified by anything
measured — it came from the same guess.

**The instrument, so the third attempt is aimed.** `particle_perf_log = 1` prints
one line per second:

    PARTICLE perf: live=N quads=N batches=N vfxLights=N fps=N

Each number kills a different hypothesis:

- **live** small while fps is low → the cost is not particle count, and no
  amount of spawning less will fix it.
- **quads** versus live → how much the atlas cross-fade is really costing (it
  doubles), measured rather than assumed.
- **batches** → rlEnd/rlBegin splits. Particles draw in POOL order, so
  populations interleave, and every alternation of texture, blend mode, lit
  flag, atlas grid or emissive boost is a flush. Ten bursts plus a flame is
  exactly the case that would interleave badly, and it is the hypothesis that
  survives the flame result: the flame is ONE population and cutting it changed
  nothing, while ten mixed effects are hundreds of state changes.
- **vfxLights** → each one is a loop iteration in every particle fragment AND in
  the scene's lit surfaces; FlameVolume spawns lights continuously.

Next step is to read that line before touching anything else.

### The particle shader had not compiled since the soft-particle wiring (28/07/2026)

The owner's log, asked for to settle a performance question, answered a
different one:

    WARNING: RLVK: GLSL compile failed:
    rlvk:50: error: 'u_resolution' : undeclared identifier
    INFO: PARTICLE soft-fade: ON (depth tex 5, fade 0.45 m)

`#include "common/soft_particle.glsl"` went in right after `#version`, while
`uniform vec2 u_resolution` — which the included function uses — was declared
further down with the other soft-particle uniforms. GLSL is read top to bottom.
`particle_lit.fs` has not compiled since, so the F1 lit path, the emissive boost
and the fade itself were all dead, and every visual judgement made in between
was made on raylib's default shader.

**The two lines together are the real lesson.** The engine said the shader
failed; this file said the feature was ON. `ParticleLighting_Begin` gated on
`s_litShader.id == 0`, and a failed compile does not produce id 0 — raylib hands
back the DEFAULT shader and rlvk logs the error and continues. A non-zero id
answers "did something get bound", not "did my shader compile".

Now checked: if none of the uniforms this shader definitely declares resolve,
the bound shader is not ours — log an ERROR naming the id and fall back to the
documented unlit path instead of pretending. That is the difference between a
silent wrong render and a line that says which line of GLSL to look at.

**Every performance number from the last three rounds was measured on a broken
shader** and has to be taken again.

### Soft particles erased every particle the first frame they ran (28/07/2026)

With the compile fixed, `SoftParticle_Factor` ran for real for the first time
and the flame went invisible. The depth texture is bound (id 5) but reads back
~0, so `diff = sceneLinear - fragLinear` is negative everywhere, the factor
clamps to 0, and 0 multiplied into alpha is every particle in the game.

**A linear scene depth at or before the near plane is geometrically
impossible** — nothing can be solid at the camera's eye — so it is not
occlusion, it is "this texture has no data here": never written, cleared to
zero, or a backend where the snapshot did not land. `soft_particle.glsl` now
returns 1.0 for that case. For a term that MULTIPLIES alpha there is only one
safe direction to fail, and it is open.

The C-side guard was already there and was not enough: it checks that a depth
texture EXISTS, which is a different question from whether it contains depth.
Two guards, two distinct failure modes — a missing target, and a target that is
empty.

**What this says about the original complaint.** The cut was never being faded:
the shader had not compiled since the feature was wired, so every "still cut"
observation was of a build with no soft particles at all. Whether the fade now
works depends on whether that depth snapshot ever lands under rlvk, and the
answer is visible in one look with `particle_soft_debug = 1`: all-red means the
factor is 0 (the texture is empty and the new guard is what is keeping the
particles on screen), a red band along the floor line means it works.

### Soft particles, attempt parked: the second sampler (28/07/2026)

With the shader compiling and the fade failing open, every particle drew as a
flat bright SQUARE. That is the signature of `texture0` not being bound: an
unbound sampler reads a 1x1 white texel, and a quad of constant colour is a
square. Ruled out on the way: rlvk's `RL_BLEND_ADDITIVE` is
`(SRC_ALPHA, ONE)` — the standard pair — so the white-RGB flame split is
correctly modulated by its alpha and the blend was not the cause.

What changed at exactly that point is that `particle_lit.fs` gained a SECOND
sampler (`u_cameraDepthTex`). rlvk has previous form for binding-index
rebasing, and going from one sampler to two is precisely the change that would
shift `texture0`.

`particle_soft_fade` now defaults to **0** — the fade is polish, the particles
are the game. The default is also the experiment: turn it on and the squares
tell us which layer is at fault.

- squares only when the fade is ON → the sampling is wrong, keep the sampler;
- squares even at 0 → the DECLARATION shifts the binding, and the fix is a
  separate shader variant (one with the sampler, one without) rather than a
  uniform.

**Three separate faults in one feature, each hidden behind the previous one:**
the include order stopped it compiling; the C guard checked that a depth texture
exists rather than that it holds depth, so the first working frame erased every
particle; and now the sampler itself. Only the first was visible without
running the game, which is why the shader-compile check added earlier matters
more than the feature does.

### Soft particles reverted out of particle_lit.fs — rlvk sampler binding (28/07/2026)

The decisive test came back: the squares appear with `particle_soft_fade = 0`,
i.e. with the depth sampler declared but never read. So it is not the sampling —
the DECLARATION alone moves `texture0`'s binding under rlvk, and an unbound
sampler reads a 1x1 white texel, which across a quad is a flat square.

`particle_lit.fs` is back to exactly one sampler. The C-side machinery stays and
is inert: `s_locSoftFade` resolves to -1, the block is gated on that, and
nothing binds or uploads. Promoted to `ENGINE_LANDMINES.md` because it is not a
particle problem — ANY core shader that grows a second sampler will hit it, and
the shader compiles cleanly so there is no error to notice.

**What this cost, and the cheap thing that would have caught it.** Three rounds
of visual debugging, on a build where the particle shader had not compiled at
all for two of them. The instrument that finally worked was the owner pasting
the engine's own log — it had been saying `GLSL compile failed` the whole time.
The two checks added on the way (a shader that loads but does not compile; a
depth texture that exists but is empty) are worth more than the feature was.

**Soft particles remain unsolved**, and the cut is still there. Options when it
resumes, in order of cost: a separate shader variant used only where the binding
works; or the rlvk binding itself, which is the Renderer Agent's module.

### PERF — the burst is free, the package is not (28/07/2026)

Owner, measuring: `VFX_ComposeEnergyBurst` fired continuously holds ~60 fps;
`VFX_ComposeImpactPackage` drops it. The burst is the only beat made of
PARTICLES, so this rules the particle system out entirely — and with it the two
optimisations attempted earlier, which were both aimed there.

What is left is the same shape of problem twice over: a handful of objects whose
cost is paid per FRAGMENT across the whole screen.

- **VFX lights.** Each active light is a loop iteration in every particle
  fragment AND in every lit surface fragment in the scene. The package spawns
  one per impact; spam impacts and the whole screen pays for all of them.
- **Screen distortion.** The pass always runs — it IS the blit of the scene, so
  turning distortions off does not skip anything — but with sources active its
  shader loops over them per fragment, full screen.

Neither shows up as particle count, quads or batches, which is why the perf line
alone would not have found it.

Rather than argue about which, `impact_light` / `impact_distort` / `impact_decal`
/ `impact_hitstop` now gate the beats individually. Turn them off one at a time
and watch the frame rate; whichever restores it is the answer. The decal is in
the list not because it is suspected but so it can be ruled OUT by measurement
rather than by assumption. They stay afterwards as the per-effect budget
switches.

**Method note.** Three performance changes have now been made in this area; the
first two were guesses at the particle system and neither paid. The owner's
one-line comparison — one effect fast, a superset of it slow — narrowed it
further in a sentence than either guess did in a round trip. The subtraction
was available the whole time.

### PERF — severity was applied twice, and that was the whole difference (28/07/2026)

Owner: still dropping with the beats switched off. That narrowed it to the burst
itself — and the burst alone holds 60 fps, so the package was not firing the
same burst.

It was not. `VFX_Sequence` multiplies every beat's spatial `a` by the sequence's
scale (`s->scale * scale` for COMPOSE, `b->a * s->scale` for LIGHT / DISTORT /
DECAL). The package ramped with severity in BOTH places: once in `VFX_SeqBegin`
(`scale * Mix(0.7, 1.25, sev)`) and again in each beat's `a`
(`Mix(0.6, 1.35, sev)` for the burst). At severity 1.0 that is 1.69x scale:

| | bench burst | inside the package |
|---|---|---|
| scale | 1.00 | 1.69 |
| sprites | 75 | 123 |
| area each | x1 | x2.85 |
| **total fill** | x1 | **x4.70** |

Severity now lives in exactly one place — the sequence scale — and every beat's
`a` is a plain proportion in metres at scale 1. Times (hitstop duration, decal
and light lifetimes) keep their own severity ramps, because the sequence does
not scale those. **4.70x -> 1.91x.**

The residual 1.91x is real and intended: a maximum-severity impact SHOULD be
bigger than the bench's default burst (scale 1.25 and intensity 0.85 against
1.0/0.9). If spamming maximum-severity impacts is still too expensive, that is a
budget question with an obvious dial — at severity 0.5 the sequence scale is
0.975, i.e. exactly the bench burst.

**Method note, the third in this area.** Two guesses at the particle system paid
nothing; per-beat kill switches were about to blame the light or the distortion;
the answer was a multiplier applied twice, and it fell out of ARITHMETIC once
the owner's subtraction ("burst fast, package slow, beats off, still slow") left
only one thing standing. A scale that compounds silently through a layer is the
kind of bug that is invisible in a screenshot and obvious in a table.

### Handoff written: HANDOFF_E_F.md (28/07/2026)

`HANDOFF_E_F.md` at the repo root is the entry point for the next session on the
remaining E/F work. It exists because this file is now 2459 lines and reading it
whole is the most expensive mistake available in this codebase: the handoff
points at **line 2186 onward only**, plus the last entry of
`ENGINE_LANDMINES.md` and one section of the spec.

It carries the state table, the seven landmines that would otherwise be stepped
on again (rlvk's second sampler, a failed compile not returning id 0,
`spriteAnimPhase` and its budget against the sheet, the sequence's double
severity, emission as a rate, no camera shake on initiative, smoke sheets must
self-shadow), and the working rules this session paid for — chiefly that the
cost was never in the particle system, that the owner's own subtraction found in
one sentence what two guesses did not, and that the game cannot be run from the
agent's side (`FATAL: RLVK: instance creation failed`), so every runtime
question has to go through a log line or a tunable.

### E6 #5 — `VFX_ComposeSweepSlash` landed (28/07/2026)

The last blocked item in E6. The spec wants "an arc mesh driven by an authored
flipbook mask (`arc_slash_*`)"; that sheet does not exist and nothing in
`scripts/flipbook/` can bake one — that pipeline is a Taichi smoke SIMULATION,
which is the wrong instrument for a blade streak. So this took the route §0.2
authorises where an asset stalls and GENERATED the mask
(`core/composition/common/vc_sweep_slash.inl`, `SweepSlash_BuildMask`), the same
move `VFX_ComposeDissolveExit` made for its body texture. Swapping in an authored
sheet later is one line.

**What makes it read as a cut rather than as a glowing banana**, in the order
they matter:

1. **The head outruns the tail.** The band is the gap between two angles walking
   the same arc on different clocks — head ease-out (fastest at the start, a
   follow-through), tail smoothstep starting 0.16 in. It is never a whole arc
   that fades; a slash you can see all of at once is a decoration.
2. **The cross-section is asymmetric.** Near-white against the outer edge,
   smearing inward. Symmetric is a tube.
3. **The tip is a needle.** Half-width goes to zero at the head, so the leading
   end is a point rather than a cut-off rectangle.

**The bug that would have shipped, caught by arithmetic rather than by looking.**
Which side of the strip carries the hot rim is decided by `ribbon_strip.c`, not
by the mask: `DrawRibbonStripEx` emits u = 0 at `center + side*halfWidth` with
`side = normalize(cross(tangent, normal))`, and for this arc that cross product
is the radially OUTWARD direction (basis check: axU x axV = n, so
tangent x n = axU cos a + axV sin a = the radial). The mask had been authored
with u = 1 as the outer edge, which would have buried the rim against the pivot
and hung the smear outside the swing. On screen that does not look like a bug —
it looks "a bit soft", which is exactly the kind of wrongness that survives a
screenshot review. `core/tests/sweep_slash_test.c` now pins it.

Also in the file, each one a landmine already paid for: emission of both the
refraction sources and the sparks is a **rate** (`dt`-driven accumulator, ~9/s
and ~55/s) rather than a count per call, because this composition is re-called
every frame; the strip is `RIBBON_FIXED_NORMAL` + `Ribbon_ComputeArcLengthUV`, so
the mask keeps its texel density as the band shortens; depth state is flushed on
both sides; and there is no camera shake.

Tunables (lazy, on first use): `slash_width`, `slash_tilt` (plane tilt off
horizontal — at 0 the sweep is a disc on the ground and reads as a shockwave),
`slash_distort` (the refraction is per-fragment across the whole screen for every
live source — this is the perf switch), `slash_sparks`.

Bench: manifest entry added by hand, `scripts/sync_vfx_test.py` regenerated the
rest → NEW FX tab, "SWEEP SLASH". Build clean, `run_core_tests.sh` 7/7.

**Not verified on screen** — the agent cannot run the game
(`FATAL: RLVK: instance creation failed`). What the headless suite cannot answer:
whether the tilt reads as a diagonal from the game camera, whether the striation
frequency survives at gameplay distance, and whether three additive passes is the
right amount of bloom feed. Those are the eyeball questions left for the owner.

### SweepSlash, first screenshot: the sparks beat the blade (28/07/2026)

Owner's capture: a chain of round bright beads strung along the arc, with the
blade itself a faint blue smear behind them. It reads as fairy lights, not as a
cut. The beads are the SPARKS, and three mistakes stacked to make them the
effect:

| | first cut | why it failed | now |
|---|---|---|---|
| rate x life | 55/s x 0.25 s = ~14 live | they accumulate faster than the head moves, so they lie on the arc like beads on a wire | 24/s x 0.14 s = ~3 live |
| stretch | 0.10 → `1 + 4*0.10` = **1.4x** at 4 m/s | that is a round dot, not a streak | 1.10 → ~5x |
| emissiveBoost | 1.5 | over the bloom threshold, and bloom turns a 2 cm sprite into a 10 cm bead | 1.15 |

They also spawned across the leading THIRD of the band; now only the leading 8%,
so they come off the tip instead of decorating the arc.

The band had its own version of the same problem — too much soft, not enough
edge. Half-width 0.16 → 0.105 of the arc radius (at 1.8 m that was a 1.5 m wide
glow on a 1.8 m arc: a crescent moon, not a cut), the halo pass 2.7x → 1.8x, and
the mask's rim narrowed and weighted up against its smear (sigma 0.10 → 0.075,
0.55/0.95 → 0.45/0.80).

**The lesson worth keeping is about the stretch number.** `stretchStrength` is
not a 0..1 fraction — `stretchFactor = 1 + speed * strength`
(`core/particle_system.c:1003`), so at gameplay speeds a value like 0.10 is
indistinguishable from no stretch at all. The only other user in the tree
(`vc_particle_upgrades_test.inl`) uses 0.04, which is where the wrong intuition
came from. `core/tests/sweep_slash_test.c` now asserts the streak factor, the
live-spark count and the life-to-sweep ratio, so this exact picture cannot come
back silently.

### SweepSlash: three passes drew three edges (28/07/2026)

Second capture — the beads are gone and the blade reads — but the arc came out as
**two or three parallel wires** rather than one edge, clearest at the head.

The mask puts its hot rim at a fixed FRACTION of the strip's width (u ~ 0.05).
The three passes were centred on the same path at 1.8x / 1.0x / 0.34x width, so
each one placed its rim at a different radius: 1.8x as far out for the halo, a
third as far for the core. Three rims, three wires. A symmetric mask (the rune
circle's, which this layering was copied from) does not have this failure mode —
widening a symmetric band just blurs it, which is why the pattern transferred
without the problem transferring with it.

Passes are now aligned by their **outer edge**, not their centre:
`rr += halfW * env * (1 - passW[pass])`. Every rim lands on the same world
circle; the wider passes reach further INWARD, which is what a halo behind a
blade should do anyway. Asserted in `core/tests/sweep_slash_test.c`
(`Test_PassesShareOneOuterEdge`), including that the halo still reaches inward
past the core.

**Rule for reuse:** any multi-pass ribbon whose mask is ASYMMETRIC across the
strip must align its passes on the edge the mask is keyed to. Centre-aligning is
only safe for symmetric masks.

### SweepSlash: a lens, not a comet; and the AA was erasing the edge (28/07/2026)

Owner, on the third capture: *"nó có 1 đầu bự 1 đầu nhọn, đầu bự đi trước đầu
nhỏ theo sau — sao ko làm 2 đầu nhọn luôn?"* Correct, and the envelope's own
arithmetic said so all along.

`powf(s, 0.5f) * (1 - smoothstep((s-0.86)/0.14))` is zero at both endpoints —
which is the property the first test asserted, and it is the WRONG property.
`sqrt` reaches half width by s = 0.25, so the band was fat across most of its
length and pinched only in the last percent, while the head's taper was squeezed
into 14%. A blunt club leading a thin streamer: a comet. A blade leaves a LENS,
pointed at both ends, because both ends are the same edge at different times.
Now `powf(sinf(PI*s), 0.85f)`, and the test asserts **symmetry** —
`env(s) == env(1-s)` — instead of the endpoint values, because that is the
property that was actually wanted. Direction is still readable: it lives in the
brightness and hue ramp toward the head, not in the silhouette.

**Two more, both found while fixing that:**

1. **The halo pass was washing out the edge it was supposed to sit behind.** All
   three passes share one outer edge, so a halo carrying the rim lays a rim
   1.8x thicker in metres on exactly the same line — a broad soft gradient
   instead of a line, i.e. the "crescent moon, not a cut" of capture three. The
   halo now draws from a SECOND generated sheet with the smear alone
   (`SweepSlash_Profile(u, withRim)`), asserted monotone by
   `Test_HaloSheetHasNoRim`.
2. **The antialiasing shoulder was erasing the rim it protects.** The shoulder
   spanned the outer 4% of the strip while the rim's sigma had been narrowed to
   0.05 — they overlapped, and the shoulder was halving the rim's peak. The
   shoulder must be narrower than the rim's distance from the border: 1.5%,
   which at the mask's new 192-texel width is ~3 texels, all AA needs. The mask
   also went 64 → 192 across, because a 5%-wide rim on a 64-texel sheet is three
   texels magnified across metres of screen — bilinear cannot invent an edge the
   texture does not resolve.

Mask also gained a shallow lengthwise swell (`pulse`, 0.8-1.0) so the band is not
a uniform sheet of light; a perfectly even arc reads as painted glass.

7/7 suites, 46 checks in this one. Still not eyeballed by me — capture four is
the owner's.

### SweepSlash: width belongs to the arc's LENGTH, not its radius (28/07/2026)

Fourth capture: pointed at both ends now, and reading as a LEAF — far too fat.

Fatness is an aspect ratio against the distance the head travels, and the width
was keyed to the arc's RADIUS (`length * 0.105`). That makes the ratio move with
`arcRad`: at the bench's 2.2 rad the head covers 3.96 m and the band was 0.38 m
across — 1:10, a leaf. The same constant at a 0.6 rad flick would have been about
1:3, a paddle. The number was only ever right at one sweep angle, and nothing
said which one.

Now `halfW = length * clamp(arcRad, 0.5, 3.2) * 0.022`, i.e. **1:23 against the
arc it actually travelled**, and the look holds at any sweep angle. Halo pass
trimmed 1.8x -> 1.5x and 0.26 -> 0.18 alpha with it, since the wide dim smear was
the other half of the leaf.

`Test_BandIsThinAgainstItsOwnArc` pins both the ratio and its invariance to
`arcRad` — the second assertion is the one that matters, because the first would
have passed on the old formula too, at exactly one angle.
