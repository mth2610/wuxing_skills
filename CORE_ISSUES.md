# Core Engine — Open Issues / Unfinished Work

Tracks work from the "Core API update" task list that is either unfinished
(reverted, needs a fresh approach) or not yet started. See `CORE_API.md` for
the API surface that IS shipped and documented.

---

> [!CAUTION]
> ## CRITICAL ENGINE RULE: The Raylib Batching Hazard
> **BẮT BUỘC PHẢI ĐỌC (MUST READ)** trước khi code bất kỳ Skill hoặc hiệu ứng đồ họa nào!
> 
> Raylib sử dụng cơ chế gộp nhóm vẽ (`rlgl` batching). Khi bạn gọi các hàm đổi trạng thái OpenGL như `rlDisableDepthMask()`, `rlDisableDepthTest()`, `rlEnableDepthMask()`, `rlEnableDepthTest()`, trạng thái OpenGL sẽ bị thay đổi **ngay lập tức**, nhưng các đỉnh đồ họa (vertices) đã được gọi trước đó bằng `rlBegin/rlEnd` (ví dụ: vẽ mặt đất) có thể vẫn đang kẹt trong batch chờ xả (un-flushed batch)!
> 
> **Hậu quả:** Nếu bạn đổi state mà không xả batch, toàn bộ mặt đất/môi trường xếp hàng phía trước sẽ bị vẽ sai state (ví dụ: bị tắt ghi độ sâu), làm hỏng Soft Particles hoặc Z-Buffer của toàn bộ game.
> 
> **QUY TẮC:** Mọi lệnh thay đổi trạng thái Depth (Mask/Test) **BẮT BUỘC** phải được đặt sau một lệnh xả batch tường minh:
> ```c
> rlDrawRenderBatchActive(); // Luôn luôn xả batch trước!
> rlDisableDepthMask();
> rlDisableDepthTest();
> // Vẽ đồ họa của bạn...
> rlDrawRenderBatchActive(); // Nhớ xả batch trước khi trả lại state cũ!
> rlEnableDepthMask();
> rlEnableDepthTest();
> ```

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
2. The `CORE_API.md` documentation has been updated to restore the "Soft Particles" section.

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
| 20 | Sandbox visual verification harness | P1 | Sandbox | — |
| 21 | `SKILL_RECIPE.md` — single-entry doc for one-prompt generation | P1 | Skills + Core | 20 |
| 22 | Preset symmetry: ForceField + Material for all 6 elements | P2 | Core | — |
| 23 | SkillBuilder archetypes: Beam / GroundWave / Orbitals / AuraRing | P2 | Core | — |
| 26 | Agent position provider — VFX that follows a moving caster/target | P2 | Core + Entities | — |
| 27 | `core/motion_controller.h` — reusable projectile motion library | P2 | Core | — |
| 28 | Chain-targeting helper (chain lightning et al.) | P2 | Core + Entities | 26 |
| 29 | Status/aura VFX attached to agents (burning, shocked, frozen) | P3 | Core | 26 |
| 31 | Mesh afterimage / ghost trail | P3 | Core | — |
| 32 | Pool stats overlay (P3, Core + Sandbox) | P3 | Core + Sandbox | — |
| 33 | Looping audio handles (flight/aura sound) | P3 | Core | 25 assets |

Recommended order: 20 → 21, then 22/23/26/27 in any order, then 28/29/31–33.

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

## Item 34 — Real-world-meter rescale: shared-infrastructure sweep + per-skill refactor checklist (P0, Core + Skills)

**Background.** 2026-07-03: the project moved from a de facto 1 unit = 1cm
scale to 1 unit = 1 meter (see root `CLAUDE.md` "Standard coordinates &
scale"). Arena center `(6.0f, 0.0f, 4.4f)`, radius `18.0f`, real gravity
`9.81f m/s²` is now the reference every force should be judged against. Two
pilot skills (`skills/fire/fire_ball`, `skills/metal/thunder_orb_skill`) were
converted and a sandbox live-tuning UI was built (`RegisterSkillTunables`/
`Skill_GetTunables` in `core/skill_manager.h`, `Tuning_SaveFloats`/
`Tuning_LoadFloatsFromPath` in `core/tuning.h` — pick a skill in the sandbox,
drag sliders, Save writes a per-skill `.tuning` file).

**Problem — it's not just the two pilot skills' own files.** Every
"disconnected from character" / "huge" bug reported after the two pilots were
converted turned out to live in **shared core infrastructure** the pilots
call into, not in the pilots' own code:
- `core/skill_manager.c`'s `CastSkill()` dispatcher applies a position offset
  (`CAST_PATH_PROJECTILE`: `startPos.y + 25.0f` then `+ aimDir*22.0f` —
  "cast from shoulder height, slightly forward") *before* any skill's own
  Cast function runs. Un-rescaled, this was a 25m/22m displacement instead
  of 25cm/22cm. Same file: `main.c`'s `UpdateSkillManager(dt, enemy.position,
  35.0f)` passed an un-rescaled `enemyRadius` (should be `0.35f`), making the
  generic per-frame projectile-vs-enemy hit check trigger on frame 1 for
  every skill (fire/wood/electric/metal all share this code path) regardless
  of real distance. Also un-rescaled in the same file: 5 `AddKnockbackToEnemy`
  strengths, `Skill_CalculateKnockback`'s base, `AddFloatingText`'s position
  jitter, `AddCastPortal`'s size.
- `core/vfx_proc_ray.c`'s shared lightning-bolt presets
  (`ProcRay_LightningConfig`/`BoltLightningConfig`/`EnergyConfig`/
  `WindConfig`) had a `thickness` field (1.1–1.8, meant as meters) never
  rescaled — a 1-2 *meter* thick ray. Any skill using these presets inherits
  the bug.
- `core/skill_helper.c`'s `SpawnImpactEffect()` — a single function with 8
  `EFFECT_PRESET_*` cases shared across all 6 elements — has the same
  un-rescaled-constants pattern. Only `EFFECT_PRESET_LIGHTNING_IMPACT` has
  been fixed so far (ground decal radius, screen-distort radius/speed, VFX
  light radius, particle velocity/radius — all were old-scale). **The other
  7 cases (`FIRE_EXPLOSION`, `ICE_SHATTER`, `WATER_SPLASH`, `EARTH_CRACK`,
  `WOOD_BLOOM`, `METAL_SHARD`, `TAIJI_BURST`) were spot-checked and confirmed
  to have the identical pattern (e.g. `FIRE_EXPLOSION`'s `ScreenDistort_Add`
  radius 55.0f, decal radius 22.0f, particle speed `rand()%35+20`) but are
  **not yet fixed** — any skill that calls `SpawnImpactEffect()` with one of
  these 7 presets will show the same oversized-decal/light/particle symptom
  once converted.

**Why this was missed the first time**: the original rescale pass (and every
debugging round after it) read individual skill files (`fire_skill.c`,
`thunder_orb_skill.c`) end to end, but never traced *into* the shared
functions those files call (`CastSkill`, `SpawnImpactEffect`,
`ProcRay_LightningConfig`) to check whether those functions' own internals
were rescaled. A skill file can be 100% correctly converted and still
produce wrong-scale visuals if it calls into un-converted shared code.

**Refactor checklist for converting the next skill** (do all four passes,
not just the first):
1. **The skill's own file**: grep for spatial magic numbers (radii, speeds,
   forces, position offsets — anything in the tens-to-hundreds range is
   almost certainly old-scale) and ÷100. Non-spatial values (particle
   counts, colors, alpha, time/lifetime, unitless ratios/multipliers) do
   NOT get scaled — see the "what NOT to scale" examples throughout Item 34's
   sibling commits (`fire_skill.c`, `thunder_orb_skill.c` diffs) for the
   distinction in practice.
2. **Every shared function it calls into** — trace `SpawnImpactEffect`,
   `ProcRay_*Config`, `SpawnGroundDecal` call sites, `ScreenDistort_Add`,
   `VFXLight_Spawn`, any `core/skill_helper.h` convenience wrapper — and
   check whether *that function's own internals* (not just the skill's call
   site arguments) have already been rescaled. Don't assume a shared
   function is safe just because its signature looks normal.
3. **The dispatch path**: confirm `CastSkill()`'s `pathType` switch (now
   fixed for all 3 branches) and `UpdateSkillManager`'s collision-check
   `enemyRadius` (now fixed) actually apply — these are global fixes, should
   already be correct for every skill, but verify once per new skill as a
   sanity check (cast near the character, confirm the effect visibly
   originates there, not from a corner).
4. **Distance-proportional VFX parameters**: anything computed as
   `distance * ratio` (wave wobble, spread cones, etc.) needs a floor/cap —
   see `fire_skill.c`'s `GetDragonPathPos` wave-amplitude fix
   (`fminf(fmaxf(dist*0.18f, 0.2f), dist*0.6f)`) as the pattern. Short-range
   casts are now the norm at 1/100th scale; a pure ratio silently produces
   an imperceptible effect instead of an obviously-broken one, which is
   harder to notice and debug.

**Verification technique that worked reliably** (headless, no interactive
screen access needed): temporarily register an `AutoTest_*` case in
`skills/taiji/core_test/core_test_skill.c` that calls `CastSkill()` with
`camera.target` as start and `camera.target + small offset (1.5–2.5 units)`
as target — this exercises the exact same dispatch path a real click does.
Screenshot at a frame timed to the skill's own travel duration (cast
distance ÷ known speed) via `AutoTest_SaveScreenshot`, `Read` the PNG
directly. Remove the temp case once confirmed — don't leave debug
scaffolding in `core_test_skill.c`.

---

**Follow-up (2026-07-03 session) — comprehensive sandbox-tunable system now
exists and is PART of the per-skill conversion pass, not a separate task.**
Before sweeping the remaining 11 skills, the user asked to first make the
sandbox-tunable system itself comprehensive: every parameter that affects
look/feel (size, speed, opacity, force) adjustable at every phase, ideally
as a value that changes *over time* rather than a flat constant, plus a
general "add extra force" capability. That system is now built and proven
end-to-end on both pilots (`fire_ball`, `thunder_orb_skill`). **Converting a
skill for Item 34 is now a 5-pass job — passes 1-4 above are unchanged
(meter-scale), pass 5 below is new (comprehensive tunability). Do both in
the same pass; don't convert a skill's scale and leave it under-tunable for
a later touch.**

*What now exists (all documented in `CORE_API.md` — read there for full
signatures, this is just a map of what to reach for):*
- `core/skill_curve.h`'s `SkillCurve` — 5 fixed keyframes (t = 0/25/50/75/100%
  of caller-defined progress), `SkillCurve_Eval`/`SkillCurve_SetConstant`.
  Built on the pre-existing (previously unused anywhere) `core/float_curve.h`.
- `core/skill_manager.h`'s `SkillTunableEntry` gained `.phase` (sandbox
  groups entries into one **tab** per distinct tag — was inline scroll
  headers, changed this session because a long scroll list across 4+ phases
  wasn't navigable) and `.curve` (curve-kind entry: 5 sliders instead of 1).
  `MAX_SKILL_TUNABLES` raised 16→200. `SkillTunables_Flatten/Unflatten/
  LoadPersisted` persist curve entries through the same `.tuning` file
  format, no format changes needed.
- `core/skill_helper.h`'s `SkillHelper_StepCurveFlight` — curve-driven
  flight speed indexed by **elapsed time, never distance-to-target** (the
  original design mistake this fixed: indexing by distance-progress makes a
  far cast silently stretch the whole speed ramp, and caps nothing). Hard-
  capped by tunable `maxDuration`/`maxRange` so a cast can never fly
  farther/longer than its own limit regardless of target distance. Fits a
  straight-line projectile only — see thunder_orb_skill.c.
- `core/skill_helper.h`'s `SkillForceMix` — all 8 curated `ForceType`s
  simultaneously tunable (each with its own full param set: strength/
  direction/origin/noise as applicable), always-additive — no "pick one
  type" step, dial up as many at once as wanted. (An earlier per-slot
  "pick a type from a button row, shared field storage" design was tried
  and rejected mid-session — confusing, and couldn't run two types at
  once; don't reintroduce it.) `SkillForceMix_MakeTunables` builds all 29
  tunable entries for one phase in one call; `SkillForceMix_AddLayers`
  composes the nonzero-strength ones into a `ForceField`, called fresh
  right before each real use (same "read live, don't bake at Init" rule as
  every other tunable-driven `ForceField` in this codebase).
- `core/particle_system.h`'s `ParticleConfig` gained `radiusCurve`/
  `speedCurve`/`alphaCurve` (all `const SkillCurve *`, default `NULL` =
  today's exact legacy behavior) — a particle's size/velocity/opacity can
  now genuinely change **over its own short lifetime** (multiplicative,
  sampled at age-fraction 0→1), not just be randomized once at spawn.
- `sandbox/ui_panel.c` — tuning panel is tabbed by phase, visually merged
  into one panel with the cast-params controls above it, and slider width
  is dynamic to the actual window size (was a cramped fixed 380px).

*Pass 5 checklist — comprehensive tunability:*
1. Identify the skill's own phases (reuse whatever state-machine names it
   already has — cast/fly/impact/whatever — don't invent new ones).
2. Tag every magic number that affects look/feel with `.phase = "<phase>"`:
   existing physics constants, plus newly-exposed count/speed-min-max/
   lifetime-min-max/radius-min-max for every particle spawn site in that
   phase. Pure structural constants (array-sizing `#define`s) stay
   `#define`, not tunable — making those runtime-adjustable would need
   dynamic allocation, which this project's static-array convention
   doesn't use. See `thunder_orb_skill.c`'s "Pool-size constants... stay
   #define" comment for the exact line to draw.
3. If the skill has projectile-style flight, add one `SkillCurve` for
   speed — **match the curve-drive model to how the skill's motion
   actually works**, don't force one pattern onto both: a straight-line
   projectile uses `SkillHelper_StepCurveFlight` (thunder_orb_skill.c); a
   path-parameter flight (e.g. a Bezier curve already anchored between
   start/target, inherently spatially bounded) should sample
   `SkillCurve_Eval` directly at the skill's own progress instead, plus add
   its own `maxDuration`-only safety cap (fire_skill.c's `UpdateFireSkill`).
4. Add one `SkillForceMix` per phase, rebuilt fresh before each real use.
   **If a phase's motion isn't purely particle-velocity-driven** — e.g. a
   Bezier-path dragon whose visible silhouette doesn't read particle
   forces at all, only its short-lived decorative trail embers do — the
   force must additionally perturb the actual visible path/position, not
   just be attached to particles that die too fast to show it. This was a
   real bug found and fixed this session, not a hypothetical: see
   `fire_skill.c`'s `forceOffset`/`forceVel` accumulator on `FireEmitter`,
   added to `GetDragonPathPos`'s return, driven by `s_flyForce` each frame.
5. Add `radiusCurve`/`speedCurve`/`alphaCurve` per phase (3 `SkillCurve`s,
   seeded flat at `1.0` via `SkillCurve_SetConstant` — a no-op multiplier
   until shaped), pointed to by every `ParticleConfig` spawned in that phase.
6. Seed every curve/force BEFORE building the `SkillTunableEntry` array.
   Build the array as a **sequence of assignments**, not one static
   literal — `SkillForceMix_MakeTunables` returns a variable count, so a
   fixed-size positional initializer can't interleave it with a phase's
   named entries while keeping the phase's tab contiguous. Call
   `SkillTunables_LoadPersisted` right before `RegisterSkillTunables`. See
   `fire_skill.c`'s `InitFireSkill` for the exact call order.
7. Watch the `MAX_SKILL_TUNABLES` (200) budget: named entries + one
   `SkillForceMix` (29) + 3 curves, per phase, adds up fast — fire_ball
   landed at 185, thunder_orb at 104. A skill with 5+ phases may need to
   skip the force mix for a phase with no meaningful local force to begin
   with, rather than raising the cap further.

**Reference implementations (both fully done — meter-scale AND
comprehensive tunability):** `skills/fire/fire_ball/fire_skill.c`,
`skills/metal/thunder_orb_skill/thunder_orb_skill.c`. Read these end-to-end
before starting a new skill, not just `CORE_API.md` — the *pattern* (how
phases/curves/force-mix compose, where they're seeded, how the
`RebuildFire*Field`/`RebuildFlightParticleField`-style functions work)
matters more than any single function signature.

**Acceptance**: the remaining 11 unconverted skills each get the 5-pass
checklist above (4 meter-scale + 1 comprehensive-tunability) when they're
next touched; the 7 remaining `SpawnImpactEffect` presets get rescaled
either as a dedicated `core/` sweep (fixes all 7 at once, benefits every
skill that uses them) or incrementally as each skill that calls them gets
converted — either is fine, just don't convert a skill's own file and
assume the shared presets it calls are already safe.

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