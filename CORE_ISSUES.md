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

# Skill-Creation Pipeline Upgrade — Items 16–33 (filed 2026-07-02)

Source: full audit of `core/` + `CORE_API.md` against the goal **"an AI
creates a complete, beautiful, creative skill from a single prompt."**
Numbering starts at 16 because Items ≤15 are referenced elsewhere
(`skills/CLAUDE.md` cites Items 14/15). Each item is self-contained; pick
up any one independently unless a dependency is stated.

| # | Title | Priority | Owner | Depends on |
|---|---|---|---|---|
| 16 | CORE_API.md §4 lifecycle signature stale + broken markdown | **P0 bug** | Core | — |
| 17 | `Material_Load` presets point at deleted shader files | **P0 bug** | Core | — |
| 18 | `scripts/new_skill.py` skill scaffolder | P1 | Core | 16 |
| 19 | `scripts/lint_skill.py` mechanical rule checker | P1 | Core | — |
| 20 | Sandbox visual verification harness | P1 | Sandbox | — |
| 21 | `SKILL_RECIPE.md` — single-entry doc for one-prompt generation | P1 | Skills + Core | 16, 18, 19, 20 |
| 22 | Preset symmetry: ForceField + Material for all 6 elements | P2 | Core | 17 |
| 23 | SkillBuilder archetypes: Beam / GroundWave / Orbitals / AuraRing | P2 | Core | — |
| 24 | Lifecycle boilerplate macros for non-projectile skills | P2 | Core | — |
| 25 | `assets/INDEX.md` + per-element SFX assets | P2 | Skills (+ user) | — |
| 26 | Agent position provider — VFX that follows a moving caster/target | P2 | Core + Entities | — |
| 27 | `core/motion_controller.h` — reusable projectile motion library | P2 | Core | — |
| 28 | Chain-targeting helper (chain lightning et al.) | P2 | Core + Entities | 26 |
| 29 | Status/aura VFX attached to agents (burning, shocked, frozen) | P3 | Core | 26 |
| 30 | Hitstop / local time-scale for impact juice | P3 | Core | — |
| 31 | Mesh afterimage / ghost trail | P3 | Core | — |
| 32 | Pool stats overlay (catch silent overflow) | P3 | Core + Sandbox | — |
| 33 | Looping audio handles (flight/aura sound) | P3 | Core | 25 |

Recommended order: 16 → 17 → 18 → 19 → 21 → 20, then 22–28 in any order,
then 29–33. Items 16 + 18 + 21 alone deliver ~80% of the one-prompt goal.

---

## Item 16 — CORE_API.md §4 documents a lifecycle signature that no longer compiles (P0)

**Problem.** `CORE_API.md` §4 (line ~135) and all 4 skeletons document
`Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params)` —
**without `int agentId`**. Reality: `core/skill_manager.h:76`'s
`RegisterSkill` cast callback is `void (*cast)(int agentId, Vector3, Vector3,
SkillParams)`, every real skill uses it (`thunder_orb_skill.h:18`,
`fire_skill.h:16`), and `skills/CLAUDE.md:38` mandates it (Item 15
migration). An AI reading §4 generates a skill whose cast fn fails the
registry's function-pointer type → compile error. Prose at `CORE_API.md`
~line 587 ("the documented lifecycle doesn't pass an agentId") is stale for
the same reason. Additionally §4's markdown is corrupted: line ~100 has a
heading typo `## SkillParams;` and the ` ```c ` fence opened at ~101 is
never closed before the next fence at ~117, so the whole section renders
scrambled.

**How to fix.**
1. `grep -n "Cast\[Name\]Skill(Vector3\|CastExampleSkill(Vector3\|Cast.*Skill(Vector3" CORE_API.md`
   — find every stale signature in §4 and inside all 4 skeletons
   (§4 headers ~line 96–147, skeletons at ~148, ~278, ~428, ~583).
2. Update each to `void Cast[Name]Skill(int agentId, Vector3 startPos,
   Vector3 target, SkillParams params)`. In each skeleton's instance struct
   add `int ownerAgentId;`, set it at cast time, per `skills/CLAUDE.md`'s
   ownership-tracking rule.
3. Fix the ~line 587 prose: `agentId` IS now passed to Cast (Item 15);
   what's still true is that `Update[Name]Skill` has no agentId and skills
   can't read the live `Agent` array — keep that half, and cross-reference
   Item 26 below as the planned fix for "follow a moving agent".
4. Fix the markdown: `## SkillParams;` → `### SkillParams`, close the
   SkillParams code fence properly before the header-template fence opens.
5. **Verify:** copy the corrected Generic Projectile skeleton verbatim into
   a scratch dir under `skills/taiji/`, run `python3
   scripts/generate_registry.py && make` — must build with zero edits.
   Delete the scratch skill afterwards.
6. Regenerate the §4-equivalent part of `CORE_API_SHORT.md`. (Normally
   SHORT is manual-only; regeneration is explicitly authorized as part of
   this item — the filing session's user asked for the pipeline to be made
   one-prompt-reliable, and a stale SHORT defeats that.)

**Acceptance:** step 5 passes; `grep "Cast.*Skill(Vector3" CORE_API.md
CORE_API_SHORT.md` returns nothing.

---

## Item 17 — `Material_Load`'s 4 presets reference deleted shader files (P0)

**Problem.** Documented at `CORE_API.md:1881`: `MATERIAL_FIRE/ICE/WATER/
PORTAL` load shaders from `skills/fire/fire_wildfire/`,
`skills/water/frost_blossom_rain_skill/`, `skills/taiji/yin_yang_orb/` —
none exist anymore. `ResourceManager_LoadShader` returns `shader.id == 0`
(Rule C guard) → mesh silently invisible, no error. Zero current callers,
but any AI that picks a preset from the enum hits an invisible-mesh dead
end with no diagnostic.

**How to fix.**
1. In `core/skill_helper.c`'s `Material_Load`, stop loading per-skill
   shader files. Route all 4 presets through the same path as
   `Material_LoadCustom` (shared `core/shaders/effect_material.vs/.fs`)
   with a hardcoded `EffectMaterialParams` per preset. Suggested starting
   values (tune visually in `core_test`):
   - `MATERIAL_FIRE`:  baseColor `ELEMENT_COLOR_FIRE`, rim 1.2, fresnel 3,
     emissive 1.5, distortion 0.4, translucency 0.
   - `MATERIAL_ICE`:   baseColor pale blue `(170,220,255,255)`, rim 1.5,
     fresnel 5, emissive 0.5, distortion 0.05, translucency 0.6.
   - `MATERIAL_WATER`: baseColor `ELEMENT_COLOR_WATER`, rim 1.0, fresnel 4,
     emissive 0.6, distortion 0.25, translucency 0.85.
   - `MATERIAL_PORTAL`: baseColor `ELEMENT_COLOR_TAIJI`, rim 2.0, fresnel 2,
     emissive 2.0, distortion 0.6, translucency 0.3.
2. Keep signature and enum unchanged (no call sites to migrate).
3. Update `CORE_API.md` "Shader Material Preset" section: delete the
   "known pre-existing issue" paragraph (line ~1881), document that all
   presets are now `effect_material`-backed.
4. **Verify** in `core_test`: draw one sphere per preset, screenshot, all 4
   visible.

**Acceptance:** no `shader.id == 0` path reachable from `Material_Load`;
4 visible spheres in core_test.

---

## Item 18 — `scripts/new_skill.py` scaffolder (P1, depends on Item 16)

**Problem.** `scripts/` only generates registries. The biggest one-prompt
failure class is naming/signature errors in hand-written boilerplate
(exact file naming for the registry scanner, exact lifecycle prototypes,
PI guard, includes). A scaffolder makes those errors structurally
impossible.

**How to build.**
1. CLI: `python3 scripts/new_skill.py <element> <snake_name>
   --archetype projectile|ground|path|attached [--shader]`.
   Validate `<element>` ∈ {water, wood, fire, earth, metal, taiji};
   derive CamelCase `[Name]` from `<snake_name>`.
2. Create `skills/<element>/<snake_name>_skill/` with:
   - `<snake_name>_skill.h` — exact lifecycle prototypes from the
     (Item-16-corrected) §4 header template, include guards filled in.
   - `<snake_name>_skill.c` — the chosen archetype skeleton from §4 with
     `[Name]`/`Example` substituted, `ownerAgentId` wiring included.
   - With `--shader`: minimal `<snake_name>.vs/.fs` pair that `#include`s
     `vs_header.glsl`/`fs_header.glsl` and compiles as-is (flat lit color),
     plus the `ResourceManager_LoadShader` call pre-wired in `Init`.
3. Template source: keep templates as separate files in
   `scripts/templates/` (NOT embedded strings) so Core Agent can keep them
   in sync with CORE_API.md §4 with a plain diff.
4. After writing files, run `scripts/generate_registry.py` automatically
   and print: skill index, how to cast it in sandbox, next steps
   (`make && ./wuxing`).
5. Refuse to overwrite an existing skill dir.

**Acceptance:** `python3 scripts/new_skill.py fire test_scaffold
--archetype projectile && make` succeeds with zero manual edits and the
skill is castable in the sandbox. (Delete the test skill after verifying.)

---

## Item 19 — `scripts/lint_skill.py` mechanical rule checker (P1)

**Problem.** Every hard rule (no malloc, no raylib primitives,
`ELEMENT_COLOR_*` only, PI guard, no Unload of cached resources, GLES
shader rules) lives in prose. An AI can't self-verify; violations surface
as visual bugs weeks later.

**How to build.** `python3 scripts/lint_skill.py <skill_dir>|--all`,
regex/text-based (no compiler needed), reports `file:line: rule: message`,
exits non-zero on any violation. Checks, in order of value:
1. `\b(malloc|calloc|realloc|free)\s*\(` in `.c/.h` → forbidden.
2. `\bDraw(Cylinder|Sphere|Cube|Capsule|Plane)\w*\(` → forbidden raylib
   primitive; point at `core/procedural_mesh_utils.h` / `DrawEffectMesh`.
3. `(Color)\s*\{` literals in `.c` → warn unless the line also contains
   `ELEMENT_COLOR_` or a `// lint: allow-color` opt-out comment (gradients
   legitimately need raw stops; warn, don't fail, for those files that
   define `ColorGradient` stops).
4. `#define PI ` without a preceding `#ifndef PI` on the line above →
   fail.
5. `Unload(Shader|Texture)\s*\(` inside the file that also defines
   `Unload[A-Z]\w*Skill` → fail (ResourceManager owns those).
6. `SpawnProjectileTrail|SpawnLightningFollowerTrail` present but no
   `KillTrail` anywhere in the file → fail (documented MUST in
   `skill_helper.h`).
7. `rlDisableDepthMask` count ≠ `rlEnableDepthMask` count (same for
   DepthTest) → warn.
8. Shader files: flag `#version` in any `.vs/.fs` that also uses
   `#include` (the preprocessor injects the version — see `CORE_API.md`
   §10 Android/GLES rules ~line 2177); flag `texture2D(` (GLES1-ism).
9. Wire a `lint` target into the Makefile: `make lint` runs `--all` over
   `skills/`.

**Acceptance:** all 8 existing skills pass, or each violation is either
fixed or given an explicit opt-out comment with justification. CI-style:
`make lint` exit 0.

---

## Item 20 — Sandbox visual verification harness (P1, Sandbox Agent)

**Problem.** Numeric autotest PASS has produced false positives three
times (Item 3 history: camera followed player, props occluded the test
shape, character shadows read as the effect). The trustworthy check is a
standardized set of screenshots a human/AI actually looks at — currently
every session improvises this.

**How to build** (in `sandbox/`, e.g. new `sandbox/visual_verify.c/.h`):
1. Trigger: env var `WUXING_VERIFY=<skill_name>` (resolve via
   `Skill_GetIndexByName`). On startup: switch to the empty flat map
   `SOFT_TEST_GROUND` (already exists from Item 3), disable player/AI
   movement, fixed camera at a documented offset from arena center
   `(600, 0, 440)` — do NOT use the player-follow camera (that's exactly
   what caused false positive #1).
2. Cast the skill once from a fixed `startPos` toward a fixed `target`
   (e.g. center → center + (200, 0, 0)) with default `SkillParams`
   (`sizeScale = 1`).
3. Capture screenshots at fixed wall-clock offsets after cast — 0.15s
   (cast/windup), 0.5s, 1.0s (flight/mid), 2.0s (impact/active), 3.5s
   (dissolve/end) — into the scratchpad or `verify_out/` as
   `verify_<skill>_<t>.png` (reuse `AutoTest_SaveScreenshot`).
4. Then exit with code 0. No numeric PASS/FAIL judgment — the output IS
   the screenshots; judgment belongs to the user/AI reviewing them.
5. Optional second camera angle (top-down) per timestamp if cheap.
6. Grep `IsKeyPressed(KEY_` in `main.c`/`sandbox/*.c` before binding any
   new debug key (Item 3's KEY_K collision lesson).

**Acceptance:** `WUXING_VERIFY=FireBall ./wuxing` (exact name per registry)
produces 5 comparable PNGs unattended in <15s.

---

## Item 21 — `SKILL_RECIPE.md`: the one document a generating AI reads (P1)

**Problem.** Knowledge needed for one-prompt skill creation is spread
across CORE_API.md (2373 lines), CORE_API_SHORT.md, WUXING_ART_DIRECTION
(+SHORT), skills/CLAUDE.md, and folklore. Token cost and contradiction
risk are both high.

**How to write** (Skills Agent drafts, Core Agent reviews; ≤300 lines,
root of repo):
1. **Decision tree**: element (given by prompt) → archetype (projectile /
   ground-rising / path-anchored / entity-attached — one line on when each
   fits) → which §4 skeleton / `new_skill.py --archetype` flag.
2. **Command sequence**: `new_skill.py` → edit the marked TODO blocks →
   `make` → `make lint` → `WUXING_VERIFY=<Name> ./wuxing` → review PNGs.
3. **Per-element preset table** (6 rows × columns): `ELEMENT_COLOR_*`,
   `EFFECT_PRESET_*` (cast/flight/impact), `EMITTER_*`, best
   `DECAL_PRESET_*` choices, `FORCE_PRESET_*` (after Item 22),
   suggested gradient stops.
4. **Aesthetic checklist** (10 checkboxes distilled from
   `WUXING_ART_DIRECTION_SHORT.md`): perpendicular jitter, 85–115% scale
   randomization, no popping (same shader across states), emissive ≤30%,
   no raylib primitives, depth rules, scale bands (radii 10–20f, force
   300–700f, speed 100–300f).
5. **Signature lookups**: link to `CORE_API_SHORT.md` sections by heading —
   do not duplicate signatures (single source of truth stays SHORT/LONG).
6. Add a pointer to it from the root `CLAUDE.md` reference-docs list.

**Acceptance:** a fresh AI session given ONLY `SKILL_RECIPE.md` plus a
one-line prompt ("create a metal skill: spinning blade vortex") produces a
compiling, registered, lint-clean, visually verified skill without reading
any other doc except the linked SHORT sections.

---

## Item 22 — Preset symmetry: ForceField + Material presets for all 6 elements (P2)

**Problem.** `EffectPresetType`, `EmitterPreset`, decals cover all 6
elements; `ForceFieldPreset` covers 3 (`FIRE_UPDRAFT`, `SNOW_BLIZZARD`,
`WATER_VORTEX`) and there are no per-element materials. Wood/Earth/Metal/
Taiji skills get systematically less "free beauty" from presets.

**How to fix** (all in `core/skill_helper.h/.c`, mirroring the existing
3 presets' construction pattern in `ForceField_CreatePreset`):
1. Add enum values + layer recipes (magnitudes in the documented 300–700f
   force regime; see `CORE_API.md` §5 for layer types):
   - `FORCE_PRESET_EARTH_RUMBLE` — weak `FORCE_GRAVITY_POINT` pull +
     low-frequency `FORCE_NOISE_PERLIN` (~40f, slow) for heavy dust drift,
     plus mild downward `FORCE_GRAVITY_DIR` (~350f) so debris falls.
   - `FORCE_PRESET_WOOD_GROWTH` — upward `FORCE_GRAVITY_DIR` (~400f) +
     gentle `FORCE_VORTEX` around +Y (~300f) — spiraling rising leaves.
   - `FORCE_PRESET_METAL_IMPLOSION` — strong inward `FORCE_GRAVITY_POINT`
     (~650f) + `FORCE_DRAG` so shards gather sharply then hang.
   - `FORCE_PRESET_TAIJI_ORBIT` — `FORCE_VORTEX_AXIS` around +Y (~450f) +
     weak point gravity: stable circular orbit for motes.
2. Add `EffectMaterial Material_LoadElement(EffectPresetType element);`
   keyed by the same enum the sound presets reuse (one enum = one element
   convention). Internally `Material_LoadCustom` with a tuned
   per-element `EffectMaterialParams` table — baseColor from
   `ELEMENT_COLOR_*`, emissive respecting the ≤30% coverage law
   (emissiveIntensity ≤ ~1.0 except Taiji), water/taiji translucent,
   earth/metal opaque with strong fresnel.
3. Demo each new preset in `core_test` (one shape per material, one
   emitter burst per force preset), screenshot, then remove per the
   core_test cleanup rule (wait for user confirm).
4. Document both tables in `CORE_API.md` §9b (surgical edit per the
   shared-write workflow).

**Acceptance:** every element has ≥1 force preset and exactly 1 material
preset; screenshots reviewed.

---

## Item 23 — SkillBuilder archetype extensions (P2)

**Problem.** `SkillBuilder` composes exactly one archetype (impact
explosion + decal + damage volume). Beams, ground waves, orbitals, and
auras get hand-rolled per skill, inconsistently.

**How to build** (in `core/skill_helper.h/.c`; each new call composes
ONLY existing systems — no new rendering code):
1. `int SkillBuilder_SpawnBeam(Vector3 from, Vector3 to,
   EffectPresetType element, float width, float duration);`
   — wraps `core/vfx_proc_ray.h`'s managed ray pool (element-tinted) +
   a `VFXLight_Spawn` at both ends + optional `SpawnDamageVolume` along
   the line (CAPSULE approximated by 3 overlapping circles). **Immediate**
   (like `SkillBuilder_AddCastEffect`), returns handle for early kill.
2. `void SkillBuilder_SpawnGroundWave(Vector3 origin, Vector3 dir,
   EffectPresetType element, float range, float speed);`
   — expanding ring/line: `DecalSystem_AddFlowEx` scroll decal + a
   `SHOCKWAVE` mesh preset scaled over time + `EARTH_DUST`-class emitter
   marching along `dir`. Internally a small static pool (8) updated from
   `DamageVolume_Update`-style tick registered in the helper's update.
3. `int SkillBuilder_SpawnOrbitals(Vector3 center, EffectPresetType
   element, int count, float radius, float duration);`
   — N small `DrawEffectMesh` tetrahedra orbiting `center` (static pool,
   updated per frame), each with a thin follower trail; per-instance
   random phase/scale per the anti-robotic law.
4. `int SkillBuilder_SpawnAuraRing(Vector3 center, EffectPresetType
   element, float radius, float duration);`
   — looping emitter ring (reuse `Emitter_AttachToPoint` at K points on
   the circle) + `DECAL_PRESET_GENERIC_GLOW` tinted + low-priority light.
5. All Spawn* are **immediate with duration/self-expiry** (unlike the
   deferred `Add*`/`Build` pattern) — document the distinction in the
   header exactly like `SkillBuilder_AddCastEffect` already does.
6. Demo each in `core_test`; document in `CORE_API.md` §9b.

**Acceptance:** each archetype callable in one line from a skill; demoed +
screenshotted; pools are static, no malloc.

---

## Item 24 — Lifecycle boilerplate macros for non-projectile skills (P2)

**Problem.** Every skill must define `Is[Name]SkillCoiling`,
`Get[Name]SkillProjectiles`, `Deactivate[Name]Projectile` even when
meaningless (ground/aura skills) — ~25 dead lines per skill and a chance
for an AI to get the signatures wrong.

**How to build.**
1. New `core/skill_boilerplate.h` (or a section in `skill_manager.h`):
   `#define SKILL_EMPTY_PROJECTILE_API(Name)` expanding to the three
   definitions returning `false` / `0` / no-op.
2. **Constraint:** `scripts/generate_registry.py` scans headers textually
   — it cannot see through macros. Keep explicit prototypes in the
   skill's `.h` (scaffolder emits them anyway); the macro is used only in
   the `.c` to provide definitions. Verify the registry scanner only
   needs the `.h` prototypes before shipping (read the script first).
3. Retrofit one existing non-projectile skill (e.g. `stone_prison`) as
   the demo; leave others until touched for other reasons.
4. Document in `CORE_API.md` §4 (one paragraph + macro name) and in the
   Item-18 templates for `ground`/`path`/`attached` archetypes.

**Acceptance:** a scaffolded ground skill's `.c` contains zero hand-written
projectile-API stubs; build + registry still work.

---

## Item 25 — `assets/INDEX.md` + per-element SFX assets (P2, Skills Agent + user)

**Problem.** An AI cannot create a PNG or WAV from a prompt. Existing
reusable textures aren't indexed (so AIs invent filenames), and
`PlayCastSound`/`PlayImpactSound` are warn-only stubs — the SFX asset gap
is already documented in `skill_helper.h:74`.

**How to fix.**
1. **Skills Agent:** walk `assets/textures/` (and any other art dirs);
   write `assets/INDEX.md` — one line per file: path, rough size, visual
   description, tint expectation (pre-tinted vs white/tintable), which
   preset(s) reference it. Group by decals / generic / noise / skill-
   specific. Link it from `SKILL_RECIPE.md` (Item 21).
2. **User task (blocked on assets):** source 12+ files
   (`assets/sounds/<element>_cast.ogg`, `<element>_impact.ogg` × 6).
3. **Core Agent (after 2):** wire paths into the switch in
   `skill_helper.c` per the existing NOTE; remove the one-time warning.
4. Rule for the recipe doc: **skills must only reference textures listed
   in INDEX.md** or ship their own PNG in the skill dir (which the AI
   can't create — so in practice: INDEX.md only).

**Acceptance:** INDEX.md covers 100% of preset-referenced textures; sounds
play for all 6 elements (or item stays open marked "blocked on user
assets" with steps 1+4 done).

---

## Item 26 — Agent position provider: VFX that follows a moving caster/target (P2, Core + Entities)

**Problem.** Skills cache the caster's position at cast time (see
`CORE_API.md` ~587) — auras/buffs/attached effects cannot track a moving
agent. Core must not `#include entities/` (documented constraint at
`CORE_API.md:1184`), so direct access is off the table.

**How to build** (inversion of control — same duplicated-constant pattern
the cooldown table already uses):
1. `core/skill_manager.h`:
   ```c
   typedef bool (*AgentPosProviderFn)(int agentId, Vector3 *outPos);
   void SkillManager_SetAgentPosProvider(AgentPosProviderFn fn);
   // false if no provider, id out of range, or agent inactive/dead.
   bool SkillManager_GetAgentPos(int agentId, Vector3 *outPos);
   ```
   Implementation in `skill_manager.c`: one static fn pointer, NULL-safe.
2. **Entities Agent:** in `Entities_Init`, register a provider that reads
   the agent pool (active check + position). Document in
   `ENTITIES_API.md`. This is the only entities-side change.
3. Skill usage pattern (document in `CORE_API.md` §4 + fix the ~587
   skeleton note per Item 16):
   ```c
   Vector3 p;
   if (SkillManager_GetAgentPos(inst->ownerAgentId, &p)) inst->anchor = p;
   // else: keep last known anchor (agent died) and start dissolve.
   ```
4. **Verify** in `core_test`: attach a glow ring to the player's agentId;
   walk around; ring follows; kill/respawn doesn't crash.

**Acceptance:** core builds without any entities include; ring-follow demo
confirmed visually.

---

## Item 27 — `core/motion_controller.h`: reusable projectile motion (P2)

**Problem.** Every projectile skill hand-rolls velocity integration —
harder for an AI to get right, and "creative motion" (spiral, boomerang,
orbit) is where hand-rolled math usually breaks scale conventions.

**How to build** (new `core/motion_controller.h/.c`; plain value struct
embedded in the skill's instance — no pool, no malloc):
```c
typedef enum { MOTION_LINEAR, MOTION_HOMING, MOTION_BALLISTIC,
               MOTION_SPIRAL, MOTION_ORBIT, MOTION_BOOMERANG } MotionType;
typedef struct {
    MotionType type;
    Vector3 pos, vel, origin, target;
    float speed;        // 100–300f per project scale rules
    float turnRate;     // homing: max radians/s steer
    float arcHeight;    // ballistic apex above the chord
    float spiralRadius, spiralRate, phase;
    float elapsed;
} MotionController;

void    Motion_Init(MotionController *m, MotionType type,
                    Vector3 start, Vector3 target, float speed);
Vector3 Motion_Step(MotionController *m, float dt, Vector3 liveTarget);
bool    Motion_Arrived(const MotionController *m, float epsilon);
```
Per-type notes:
- LINEAR: constant velocity toward target.
- HOMING: steer `vel` toward `liveTarget` clamped by `turnRate`
  (default ~3.0 rad/s); never overshoot-jitter — cap turn per frame.
- BALLISTIC: solve initial vy from `arcHeight` (default 0.35 × chord
  length) with gravity in the 300–700f regime; land exactly at target.
- SPIRAL: advance along the chord + rotate a perpendicular offset
  (`spiralRadius` default 25f, `spiralRate` ~6 rad/s, random `phase` at
  init per the anti-robotic law).
- ORBIT: circle `liveTarget` at `spiralRadius`, for orbital/satellite
  skills; `Motion_Arrived` always false (caller uses duration).
- BOOMERANG: fly to target, then re-target `origin`; arrived = returned.

Then: update the Generic Projectile skeleton in `CORE_API.md` §4 to use
`MotionController` instead of raw velocity math (one surgical edit), add
§ to CORE_API.md, demo SPIRAL + BOOMERANG in `core_test`.

**Acceptance:** fire_ball-style flight reproducible in ≤5 lines; spiral
demo screenshot; no skill needs raymath integration code for standard
motion.

---

## Item 28 — Chain-targeting helper (P2, depends on Item 26's provider pattern)

**Problem.** "Chain lightning"-class skills need: find nearest target,
jump N times, draw a bolt per hop with staggered timing. Entities owns
target queries; core owns bolts; nobody owns the composition.

**How to build.**
1. Extend the Item-26 provider pattern with a second callback in
   `core/skill_manager.h`:
   ```c
   typedef int (*NearbyTargetsProviderFn)(Vector3 center, float radius,
                                          Vector3 *outPos, int *outAgentIds,
                                          int maxOut);
   void SkillManager_SetNearbyTargetsProvider(NearbyTargetsProviderFn fn);
   ```
   Entities Agent registers it backed by `Entity_GetNearbyTargets`
   (`ENTITIES_API.md` §7).
2. `core/skill_helper.h`:
   ```c
   // Builds the jump list: from origin, nearest target within jumpRadius,
   // then nearest-to-that not already hit, up to maxJumps. Returns count.
   int SkillHelper_ChainTargets(Vector3 origin, float jumpRadius,
                                int maxJumps, Vector3 *outPoints,
                                int *outAgentIds, int maxOut);
   // Fire-and-forget visual: one SpawnLightningTrail per hop, each hop
   // delayed hopDelay (LayeredTimeline-style stagger, static pool).
   void SpawnChainLightning(const Vector3 *points, int count,
                            float scale, float hopDelay);
   ```
3. Damage stays the skill's job (call `Entity_ApplyAoEDamage` with a small
   radius at each point when its hop fires) — helper does visuals only,
   consistent with the sandbox-damage-fallback pattern.
4. Demo: sandbox dummies in a line, one call chains 3 hops.

**Acceptance:** 3-hop chain visually staggered; no-targets case degrades
to zero hops gracefully (returns 0, spawns nothing).

---

## Item 29 — Status/aura VFX attached to agents (P3, depends on Item 26)

**Problem.** "Burning / shocked / frozen / blessed" looping effects on an
agent are the visual language of wuxing counters (design doc: no UI, no
text — states must be readable in-world), and currently every skill would
have to reimplement follow-the-agent particles.

**How to build** (`core/status_vfx.h/.c`, static pool `MAX_STATUS_VFX 32`):
```c
int  StatusVFX_Attach(int agentId, EffectPresetType element, float duration);
void StatusVFX_Detach(int handle);        // early removal (cleanse)
void StatusVFX_Update(float dt);          // main.c, next to EmitterSystem_Update
void StatusVFX_Draw(void);                // transparent pass
```
Each slot owns: one looping emitter (element's `EmitterPreset` at low
rate), one low-priority `VFXLight`, optional small generic glow decal
under the agent. Every frame: `SkillManager_GetAgentPos(agentId, &p)`
→ reposition all three; provider returns false (agent died) → start a
0.5s fade-out then free the slot. Re-attaching the same element to the
same agent refreshes duration instead of stacking (search pool first).
Wire Update/Draw into `main.c`; document in `CORE_API.md`.

**Acceptance:** burning status follows a moving enemy dummy, expires
cleanly, refresh-not-stack confirmed.

---

## Item 30 — Hitstop / local time-scale (P3)

**Problem.** Big impacts read flat: camera shake exists
(`CameraFX_AddImpulse`) but there's no frame-freeze "juice", the cheapest
high-impact polish trick available.

**How to build** (`core/time_fx.h/.c`, tiny):
```c
void  TimeFX_Hitstop(float duration, float timeScale); // e.g. (0.09f, 0.05f)
float TimeFX_Apply(float rawDt); // returns rawDt * currentScale, ticks state
```
1. `main.c`: `float dt = TimeFX_Apply(GetFrameTime());` feed the scaled
   `dt` to skills/particles/entities; keep RAW dt for camera + PostFX so
   the freeze doesn't stall camera shake (that combination — frozen world
   + live shake — is the intended effect).
2. Clamp: duration ≤ 0.25s, scale ≥ 0.05; new calls extend, never stack
   multiplicatively.
3. Call it from `SpawnImpactEffect` automatically when `scale >= 1.5f`
   (big hits only), plus expose for manual use. Document in §9b.
4. Coordination note: `main.c` is root-owned — Core Agent edits it here
   (one line) but must state the change in the session report.

**Acceptance:** thunder_orb impact with hitstop vs without, two captures;
no physics explosion after resume (dt discontinuity handled since scale
is applied, not skipped frames).

---

## Item 31 — Mesh afterimage / ghost trail (P3)

**Problem.** Fast motion (dashes, blade sweeps, thrown meshes) has no
cheap motion-trail solution for MESHES — trails/ribbons cover points and
strips, not full silhouettes.

**How to build** (`core/afterimage.h/.c`, static pool 64):
```c
void Afterimage_Spawn(Model model, Matrix transform, Color tint, float life);
void Afterimage_Update(float dt);
void Afterimage_Draw(void); // BLEND_ALPHA pass, depth-write off
```
- Stores model REFERENCE + transform snapshot (no mesh copy — models are
  ResourceManager-cached and long-lived; document that the caller must
  not unload the model while ghosts live, which is already guaranteed by
  the no-Unload rule).
- Draw with the shared `effect_material` shader: translucency 1.0,
  `u_dissolve` ramped from 0→1 over `life` (dissolve-out, no popping),
  tint = element color at ~40% alpha.
- Typical use: spawn one ghost every 0.04s while a blade/dash is active
  (caller-side timer).
- Demo in `core_test` with a swinging tetrahedron.

**Acceptance:** 4–6 fading ghosts behind a fast-moving mesh, correct
draw order against ground/props, zero malloc.

---

## Item 32 — Pool stats overlay (P3, Core + Sandbox)

**Problem.** Every pool overflows silently (return -1 / no-op) — correct
for shipping, but during authoring an AI can't tell "effect looks weak"
from "80% of my particles were dropped".

**How to build.**
1. Each core system gains a 2-int getter (trivial, no state added):
   `void X_GetStats(int *active, int *max);` for: particle system, trail
   system, decal system, vfx lights, emitter system, damage volumes,
   afterimage (Item 31), status vfx (Item 29), and `skill_helper.c`'s
   internal cast/projectile/lightning force pools. Track a high-water
   mark (`maxSeen`) inside each — reset on Init.
2. Sandbox Agent: overlay panel (grep key conflicts first — Item 3's
   KEY_K lesson; suggest `KEY_F3`) listing `name active/max (peak)` —
   red when peak == max (something WAS dropped this session).
3. Also `TraceLog(LOG_WARNING)` ONCE per system per session on first
   drop, so headless/autotest runs surface it in logs.

**Acceptance:** deliberately over-spawn in core_test → overlay row turns
red + one warning line in the log; normal skills show comfortable
headroom.

---

## Item 33 — Looping audio handles (P3, blocked on Item 25's assets)

**Problem.** `PlayCastSound`/`PlayImpactSound` cover one-shots only; the
flight/aura/beam stages have no sound story (explicitly documented as out
of scope in `skill_helper.h:70` — this item is that scope).

**How to build** (`core/skill_helper.h/.c`, static pool 16):
```c
int  Audio_StartLoop(EffectPresetType element, float volume); // returns handle
void Audio_SetLoopVolume(int handle, float volume);           // e.g. distance fade
void Audio_StopLoop(int handle);                              // 0.15s fade-out, then stop
void Audio_UpdateLoops(float dt);                             // main.c once/frame
```
- Backed by raylib `Music` streams (`assets/sounds/<element>_loop.ogg`,
  6 more user-sourced files added to Item 25's list) —
  `UpdateMusicStream` called from `Audio_UpdateLoops`.
- Fade-in 0.15s on start, fade-out 0.15s on stop (no clicks).
- Missing asset → same warn-once-and-noop contract as the existing sound
  presets.
- Typical wiring: start in `Cast`, stop on impact right next to the
  mandatory `KillTrail` call — add this to the Item-19 linter as a warn
  (StartLoop without StopLoop in the same file).

**Acceptance:** fire projectile hums during flight, stops cleanly at
impact; no click; missing-asset path silent after one warning.

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