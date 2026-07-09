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
| 33 | Looping audio handles (flight/aura sound) | P3 | Core | 25 assets |
| 36 | `prop_lit` shader — PBR-lite material for map terrain/props (Map Agent request) | P1 | Core | none |
| 37 | `grass_material` shader — procedural noise-blended ground material (Map Agent request) — **SUPERSEDED by 38** | P1 | Core | none |
| 38 | `grass_material` v2 — texture-blend hybrid, replacing Item 37's pure-procedural approach (Map Agent request) | P1 | Core | none |

Remaining open: Item 33 (blocked on audio assets). **Item 34 COMPLETE** (2026-07-04): all shared infrastructure rescaled; all 7 unconverted skills' params.inl now in meter-scale; final 5 remaining raw ScreenDistort speed values (180/250/190→1.8/2.5/1.9) and 1 knockback (140→1.4) fixed in stone_prison/dia_long/thuy_kinh.

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

## Item 34 — COMPLETE (2026-07-04)

All shared infrastructure (CastSkill offsets, enemyRadius, Skill_CalculateKnockback, AddKnockbackToEnemy, SpawnImpactEffect 8 presets, vfx_proc_ray thickness) rescaled. All 7 remaining skills converted with 5-pass checklist (_params.inl in meter-scale, comprehensive tunables, SkillCurves, SkillForceMix). Final raw magic numbers fixed: ScreenDistort speed 180→1.8, 250→2.5, 190→1.9 (stone_prison/dia_long/thuy_kinh); knockback 140→1.4 (dia_long).

_(Full rescale methodology preserved below for reference — real-world-meter rescale: shared-infrastructure sweep + per-skill refactor checklist, P0, Core + Skills)_

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

---

## Item 35 — Post-FX "premium glow" pass (bloom/HDR upgrade) — REVERTED, do not re-attempt without a fresh isolated test rig

**Goal.** Trail/particle cores looked dim/flat compared to an Unreal-style
"sparkle" look. Two attempts this session, both reverted; `core/post_fx.*`,
`core/screen_distort.c`, `main.c`, `core/shaders/post_process.fs`,
`core/shaders/bloom_bright.fs` are all back to their pre-session content
(byte-identical, verified via `git diff`/`git checkout HEAD`).

**Attempt 1 — genuine HDR via RGBA16F render targets.** Switched
`post_fx.c`'s `mainRenderTex`/`bloomTex`/`dfTex[]` and
`screen_distort.c`'s scene `renderTex` from `RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8`
to `RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16` (half-float), so emissive
values >1.0 (e.g. `trail_glow.fs`'s `vec3(3.6)` core) would survive into the
bloom bright-pass uncapped, plus an ACES tonemap in the composite shader.
**Result: broken rendering** — particles/trails rendered as solid black
blobs with bright white edges.

**Root cause, confirmed.** The test machine's GPU is an **Intel HD Graphics
6000** (2015 Broadwell integrated GPU, OpenGL 4.1 via macOS's legacy GL
driver — logged at `INFO: GL: Renderer: Intel(R) HD Graphics 6000` on
startup). `RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16` was verified correct at
the raylib level (fetched `raylib` `5.5` tag's `rlgl.h` directly:
`RLGL.ExtSupported.texFloat16 = true` on `GRAPHICS_API_OPENGL_33`,
`rlGetGlTextureFormats` resolves it to `GL_RGBA16F`/`GL_RGBA`/`GL_HALF_FLOAT`
correctly) — so this is **not a raylib bug or a code logic bug**.
`glCheckFramebufferStatus` reported the FBO complete, but blending
(additive/alpha) into it was visibly corrupted — a documented class of
float-framebuffer driver bug on old Intel integrated GPUs. Completeness
checks cannot catch this; only an actual rendered-pixel comparison can.

**Attempt 2 — LDR-safe compensating tricks (no float framebuffers).** Added
a 3rd dual-filter bloom mip (1/32 res, for a wider soft halo) and a new
4-direction anamorphic streak pass (`core/shaders/bloom_streak.fs`, new
file, deleted on revert) reading the bright-pass output. **Result: also
broken** — the 1/32 mip's box/tent filter aliased against pre-existing
regularly-patterned VFX (water/ice ripple/sparkle dither), producing a
visible checkerboard/moiré blob instead of a smooth glow; the streak pass
(first tried at 1/8 res, intensity 0.5, stretch 4.0) was far too strong and
too low-res, turning individual ice-spike tips into blown-out blocky
starbursts that merged into one "quái đảng" (bizarre) mass. A toned-down
retry (streak at 1/4 res, intensity 0.15, stretch 2.0, bloom back to 2
levels) was prepared but the user asked to revert the whole session before
it could be visually confirmed — so it's unverified, not merely rejected.

**For whoever resumes this:**
- **Do not retry RGBA16F/float framebuffers on this class of hardware**
  (old Intel integrated GPUs) without a documented fallback that's verified
  to actually *render correctly*, not just pass `glCheckFramebufferStatus`
  — e.g. render one frame, read back a known pixel via `rlReadTexturePixels`
  and assert its value, not just check completeness.
- The self-verification loop this session used (`WUXING_VERIFY=<SKILL>
  ./wuxing`, headless screenshot harness, `sandbox/visual_verify.h`) was
  **unreliable in this environment** — the process hung/never produced
  screenshots when launched from a background shell (likely window
  focus/vsync throttling on an unfocused GLFW window on this machine). All
  visual confirmation this session came from the user's own manually-run
  `./build/wuxing` instance and screenshots they shared — budget for that
  turnaround instead of assuming headless self-verification will work.
- If retried, test the 3rd bloom mip and streak pass **separately and in
  isolation** against a VFX that has regular per-pixel noise/dither (e.g.
  `skills/water/glacial_cannon` or similar ice/water skill) specifically,
  since that's what exposed the moiré aliasing — a test against a single
  fireball wouldn't have caught it.
- A more promising LDR-safe direction not yet tried: per-particle "glint"
  sub-effects (tiny, high-contrast, short-lived additive highlights) rather
  than tuning the shared post-process bloom/streak pipeline — concentrates
  the "sparkle" in small screen areas instead of risking aliasing/blowout
  across whole VFX silhouettes made of many overlapping thin shapes (ice
  spikes, lightning bolts, etc.).

---

## Item 36 — `prop_lit` shader: PBR-lite material for map terrain/props (requested by Map Agent)

**Problem.** Module 2 (Map Virtual Trigger Zones, see `MODULES_ROADMAP.md` §2)
is moving map art direction from pure flat-shaded `rlgl` immediate-mode
(current `maps/default_arena.c` — no shader, no normals at all) to
"moderate realism" for terrain/props: diffuse+normal+roughness textures via
raylib's `Material` map slots. Every existing shader under `core/shaders/`
(`effect_material.fs`, `crystal.fs`, `plasma_shell.fs`, `aura_shell.fs`,
`ground_aura.fs`) is VFX-only, bound exclusively via `skill_manager.c`'s
shader dispatch — none of them are usable from a map's `DrawModel()` call,
and there is currently **no lit material shader for static geometry at
all**. Maps need one shared shader they can assign to a `Material` and
reuse across every prop (ground mesh, rocks, tree trunks, bushes) — one
asset, many draw calls, per the project's existing "load once, draw many"
convention (`MAP_API.md` §11).

**Assets already prepared and ready to consume** (Map Agent, this session):
`assets/textures/{stone_path,grass_ground,rock}_{diffuse,normal,roughness}.png`
— 3 full diffuse+normal+roughness sets, neutral tone (no baked lighting/mood,
so the same texture reacts correctly to whatever `Environment_Set*` preset
is active — see Item 3's design intent below), tileable, normal maps
verified blue-dominant (correct tangent-space encoding, not accidentally a
grayscale height map — see `scripts/generate_pbr_maps.py`, a new general
utility script that derives normal+roughness from a diffuse/height source
via seamless wrap-around Sobel gradients, added this session and reusable
for future ground textures too).

**Proposed API** (new files `core/shaders/prop_lit.vs` / `prop_lit.fs`,
loaded once via the existing `ResourceManager_LoadShader()` cache):

```c
// Suggested helper in core/ (exact home — resource_manager.h or a new
// small header — is Core's call):
Shader   PropLit_GetShader(void);  // lazy-load via ResourceManager_LoadShader, cached
Material PropLit_MakeMaterial(Texture2D diffuse, Texture2D normal, Texture2D roughness);
```

Fragment shader responsibilities:
1. Sample diffuse (albedo), normal (decode tangent-space via TBN, standard
   `rgb*2.0-1.0`), roughness (single channel) maps.
2. Lambertian diffuse + a roughness-scaled Blinn-Phong specular term (reuse
   `core/shaders/common/lighting.glsl`'s `calcDiffuse`/`calcSpecular` —
   don't reimplement, this file already exists exactly for this).
3. Light direction/color/ambient sourced from `Environment_GetSunDirection()`/
   `GetSunColor()`/`GetAmbientColor()`, pushed in as uniforms the same way
   `skill_manager.c:1060` already does for `u_lightDir` — same source of
   truth `environment_system.h` already exposes, no new Environment API
   needed for this part.
4. Multiply final color by raylib's standard `colDiffuse` uniform (tint) —
   this is what lets a single white/neutral texture (e.g. `petal_card.png`,
   any future foliage card) be recolored per-instance via `DrawModelEx`'s
   `tint` parameter, matching the project's existing white+tint convention
   for decals (`assets/INDEX.md`).

**Known raylib gotchas to check while implementing** (don't re-discover from
scratch):
- Normal mapping needs a `tangent` vertex attribute. Procedurally generated
  meshes (`GenMeshCube`/`GenMeshCylinder`/`GenMeshHeightmap`/a hand-built
  `rlgl` mesh converted to `Mesh`) do **not** have tangents populated by
  default — call `GenMeshTangents(&mesh)` before `LoadModelFromMesh()`, or
  the normal map will read as flat/zero. glTF-imported models usually ship
  tangents already; verify, don't assume.
- `skill_manager.c:1068`'s comment on `shader.locs[SHADER_LOC_MATRIX_MODEL]`
  documents a raylib model-matrix gotcha already hit once in this codebase —
  read it before wiring the model matrix uniform here.
- `core/flow_map.c`'s "bug cũ" (documented inline) and `CORE_ISSUES.md` Item
  3's root cause #3 both hit the same class of bug: manual
  `rlActiveTextureSlot`/`rlEnableTexture` binding silently not reaching the
  shader. Use `SetShaderValueTexture()` for the normal/roughness texture
  units, not raw `rlgl` binding calls.

**Related, NOT part of this item (flag only):** `EnvFogConfig` in
`environment/environment_system.c` (`s_fogConfig`, `Environment_GetFogConfig`/
`SetFogConfig`) is fully wired as data but **currently read by zero shaders
in the codebase** — fog is effectively dead code today, `MAP_API.md` §4's
`Environment_SetFogConfig` documentation notwithstanding. Whether `prop_lit`
should be the first shader to actually apply it (simple per-fragment linear
distance fog, reading camera position + `Environment_GetFogConfig()`) or
whether fog belongs in a depth-aware post-process pass instead is an open
design choice for whoever implements this — flagging so it isn't silently
rediscovered as "wait, does fog even do anything?" mid-implementation.

**Acceptance:** a map can call `PropLit_MakeMaterial(...)` once per prop
type in `Init`, assign it to a `Model`'s material slot, and `DrawModel`/
`DrawModelEx` it many times in `Draw` — diffuse+normal+roughness all
visibly affect shading, lighting responds correctly when
`Environment_SetSunColor`/`SetAmbientColor` change (verifies the "one asset,
reused across any lighting preset" requirement driving Module 2's dynamic
time-of-day plan), and `DrawModelEx`'s `tint` recolors a white/neutral
texture (test with `petal_card.png` on a simple quad) without touching RGB
elsewhere.

---

## Item 37 — `grass_material` shader: procedural noise-blended ground (requested by Map Agent)

**Problem.** `maps/verdant_path`'s ground currently uses `prop_lit`
(Item 36) with a photo-sourced `grass_ground_diffuse.png` tiled every 1.5m.
Two rounds of visual review (user screenshots) found this fundamentally the
wrong technique for this game's stylized look:
1. The reference art direction (user-provided screenshot of a wuxia mobile
   MMO) shows ground as a soft **blend of light green, dark green, and dirt
   tones** — no individual blade detail at all, closer to a painterly wash
   than a photo.
2. The photo diffuse texture has strong directional lighting/shadow **baked
   into its own pixels** (individual grass blades already show highlight/
   shadow contrast from whatever lit the original photo). A normal map
   derived from that texture's luminance (`scripts/generate_pbr_maps.py`,
   Item 36's companion tool) then re-applies a **second** round of dynamic
   lighting on top of the same baked pattern — the two compound into an
   exaggerated, fake-looking bumpy/"thô" (coarse) result that no amount of
   tile-size or normal-strength retuning fixed, because the root cause is
   architectural (photo-diffuse + derived-normal is the wrong pairing for a
   painterly, non-photoreal target look), not a tuning problem. Confirmed
   fixed for now by replacing the ground's normal map with a flat neutral
   (128,128,255) texture — but that's a stopgap, not the intended final
   material.

**Proposed fix: drop the photo texture entirely for ground colour, replace
with a procedural noise-blended material.** No diffuse/normal/roughness
textures at all — colour comes from world-space noise, so there is no
tiling seam, no UV-tile-size decision, and no baked-lighting-vs-live-
lighting conflict possible (nothing is baked; noise recomputes every
fragment).

**Proposed API** (new files `core/shaders/grass_material.vs`/`.fs` + a small
`core/grass_material.h`/`.c`, mirroring `prop_lit`'s shape at Item 36):

```c
typedef struct {
    Color colorDirt;        // low end of the noise blend
    Color colorGrassDark;   // mid
    Color colorGrassLight;  // high end / patch highlights
    float noiseScale;       // world units per noise cell — controls patch size
} GrassMaterialConfig;

Shader   GrassMaterial_GetShader(void);            // lazy-load via ResourceManager_LoadShader, cached
Material GrassMaterial_Make(GrassMaterialConfig config); // sets the 3 colors + noiseScale as shader uniforms ONCE at creation (they don't change per-frame, unlike lighting)
void     GrassMaterial_UpdateLighting(void);        // same per-frame contract as PropLit_UpdateLighting — push Environment sun/ambient + camera pos
```

Fragment shader responsibilities:
1. `#include "core/shaders/common/noise.glsl"` — reuse `fbm2(fragPosition.xz * noiseScale)` (world-space, NOT UV — this is what makes it seamless across any ground size with zero tiling decisions). Do not reimplement noise/hash functions; this file already exists exactly for this.
2. Blend `colorDirt -> colorGrassDark -> colorGrassLight` across 2 noise thresholds (e.g. two `smoothstep` bands, or a second higher-frequency `fbm2` octave layered in for fine patch variation) — tune so it reads as soft irregular patches, not a sharp 3-color cutout.
3. Light via Lambertian only (reuse `lighting.glsl`'s `calcDiffuse`, same `u_lightDir`/`u_lightColor`/`u_ambientColor` uniform names/convention as `prop_lit.fs` for consistency) — matte grass, no specular term needed (this is simpler than `prop_lit`, not a variant of it).
4. Multiply by `colDiffuse` for the standard tint convention, same as every other material in this codebase.

**Performance note (why this is reasonable, not a regression):** a single
ground plane, one draw call, ~2-3 octaves of `fbm2` per fragment (a handful
of hash+mix ops) — cheaper than `prop_lit`'s 3-texture-sample + tangent-
space-decode path, since there's no texture bandwidth/cache cost at all.
Standard technique, not exotic; safe for the mobile target.

**Relationship to `prop_lit` (Item 36):** NOT a replacement — `prop_lit`
stays the right tool for anything that should read as a distinct textured
surface (rock, stone path, props). This item is specifically for organic
ground-color blending where the reference art wants soft painterly variation
instead of a legible material texture.

**Acceptance:** `maps/verdant_path`'s ground swaps from
`PropLit_MakeMaterial(grass...)` to `GrassMaterial_Make(...)`; visually
reads as smooth blended green/dirt patches (matching the reference
screenshot's ground), no repeating tile pattern visible at any camera
distance, lighting responds to `Environment_SetSunColor`/`SetAmbientColor`
changes same as `prop_lit` does.

---

## Item 38 — `grass_material` v2: texture-blend hybrid (supersedes Item 37)

**Problem.** Item 37's pure-procedural approach (color blended purely via
`fbm2` noise, no textures at all) shipped and was visually reviewed against
a real screenshot. Two problems, both from the same root cause:
1. **"Looks fake"** — smooth color-only blobs with no fine surface grain
   read as an airbrushed gradient, not ground.
2. **Perceived to move/slide when the camera pans** — near-certainly a
   perceptual illusion (the shader math is world-space-anchored and
   verified correct, no bug found), but a real one: a low-frequency,
   feature-less color field gives the eye no fixed high-contrast reference
   point, so camera motion parallax on it reads as "the pattern is
   drifting." Both problems share one fix: the surface needs actual
   fine-grained detail for the eye to anchor to.

User feedback (an experienced walkthrough of how mobile MOBA/MMORPG ground
is actually done) confirms the right ratio is **~80% texture, ~20%
shader** — don't try to fake real surface detail with pure procedural
noise; use real textures for anything that needs to read as a material,
and use the shader only for blending layers together and adding broad
color variation. This matches how `prop_lit` (Item 36) already treats rock/
stone-path — Item 37 was the outlier in trying to go 100% shader for
ground specifically, and that's what didn't work.

**New design — replace `GrassMaterialConfig` entirely** (breaking change,
fine — the only consumer, `maps/verdant_path`, will be updated by the Map
Agent right after this lands):

```c
typedef struct {
    Texture2D grassBase;     // real grass photo texture (assets/textures/grass_ground_diffuse.png already exists — reuse it)
    Texture2D grassDetail;   // fine grayscale grain/speckle overlay, ~0.5-centered for symmetric multiply (assets/textures/grass_detail.png already exists — procedurally generated this session, tileable)
    Texture2D dirt;          // dirt patch photo texture (NOT YET PROVIDED — Map Agent is sourcing assets/textures/dirt_diffuse.png; wire the sampler/uniform now, swap the actual file in later, no API change needed when it arrives)
    float     baseTileSize;    // world meters per grassBase/dirt tile
    float     detailTileSize;  // world meters per grassDetail tile — much smaller than baseTileSize (denser repeat) for fine grain
    float     maskNoiseScale;  // world-space fbm2 frequency controlling where dirt shows through grass (procedural — NO separate mask texture)
    float     colorVarScale;   // world-space fbm2 frequency for broad brightness/tint variation across the whole ground (procedural — NO separate noise texture)
} GrassMaterialConfig;

Shader   GrassMaterial_GetShader(void);           // unchanged shape
Material GrassMaterial_Make(GrassMaterialConfig config);
void     GrassMaterial_UpdateLighting(void);       // unchanged shape/contract
```

Keep the mask and color-variation as **procedural** `fbm2` calls in the
fragment shader (reuse `noise.glsl`, do not add mask/noise texture files —
this keeps the "shader = blend + variation only" principle without extra
asset overhead, since a computed low-frequency noise value is visually
equivalent to sampling a baked noise texture for this specific purpose).

**Fragment shader logic** (`core/shaders/grass_material.fs` — rewrite, not
patch):
```glsl
vec2 worldXZ  = fragPosition.xz;
vec2 baseUV   = worldXZ / u_baseTileSize;
vec2 detailUV = worldXZ / u_detailTileSize;

vec3 grass  = texture(u_grassBase, baseUV).rgb;
vec3 detail = texture(u_grassDetail, detailUV).rgb;   // ~0.5-centered grayscale
vec3 dirt   = texture(u_dirtTex, baseUV).rgb;

vec3 grassDetailed = grass * mix(0.85, 1.15, detail.r);   // fine grain modulates brightness only, doesn't shift hue

float mask = smoothstep(0.35, 0.55, fbm2(worldXZ * u_maskNoiseScale));
vec3 blended = mix(grassDetailed, dirt, mask);

float colorVar = fbm2(worldXZ * u_colorVarScale);          // broad, sparse — large regions, not small speckle
blended *= mix(0.85, 1.15, colorVar);

// lighting: same Lambertian-only (calcDiffuse) + u_ambientColor/u_lightColor as Item 37 — no change here
```
Bind all 3 textures via `SetTextureWrap(tex, TEXTURE_WRAP_REPEAT)` (same
convention `prop_lit.c`'s callers already use) — the Map Agent will do this
at call time in `verdant_path.c`, but double-check `GrassMaterial_Make`
doesn't need to do it internally (prop_lit doesn't, so match that
precedent: wrap-mode is the caller's responsibility).

Reuse the existing 3-texture material-map-slot pattern `prop_lit.c`
established (`MATERIAL_MAP_DIFFUSE`/`MATERIAL_MAP_NORMAL`/
`MATERIAL_MAP_ROUGHNESS` as generic texture-slot carriers, not for their
raylib-semantic meaning) — same `shader.locs[]` fixup gotcha applies
(`texture0`/`texture2`/`texture3` uniform names, or pick your own 3 sampler
names consistently between `.fs` and the `.c` loc-fixup, just match
whichever you choose).

**Acceptance:** ground shows visible fine grain up close (not a flat
color), dirt patches blend in softly via the procedural mask, broad color
variation reads as gentle regional tint shift (not a repeating tile), no
change to the lighting/tint contract vs Item 37.

**Follow-up — found and worked around a suspected `fragPosition`/`matModel`
bug, root cause NOT confirmed.** After shipping, user testing found the
ground's dirt-patch pattern stayed at a **constant screen-relative offset
from the player regardless of true distance traveled** (walked from right
next to a rock to far away from it — rock correctly grew/shrank with
distance as expected, dirt patch did not move at all relative to the
player). Both `grass_material.fs` and `prop_lit.fs` compute
`fragPosition = matModel * vertexPosition` identically in their vertex
shaders; `prop_lit` only consumes `fragPosition` for a minor specular
view-direction term (a bug there would be easy to miss), while
`grass_material` used it as the PRIMARY color-driving coordinate (any bug
there is immediately, obviously visible) — so this doesn't rule out the
same issue silently affecting `prop_lit` too.

**Workaround applied (Map Agent), not a root-cause fix:** switched
`grass_material.fs`'s `worldXZ` from `fragPosition.xz` to `fragTexCoord`,
with `maps/verdant_path.c` baking `fragTexCoord` = world meters at
mesh-creation time (`TilePlaneUVs(&groundMesh, MAP_WIDTH, MAP_DEPTH,
1.0f)`, reusing the same UV-baking helper the stone path already used
successfully). `fragTexCoord` has zero per-frame matrix dependency, so it
can't carry this bug regardless of cause. Confirmed working after the
switch (not yet re-verified with a fresh user screenshot as of this
writing, but the mechanism is sound either way).

**Still open, for whoever picks this up:** the actual root cause of why
`matModel`-derived `fragPosition` might be camera/player-coupled was never
identified — static code review of `GrassMaterial_GetShader`'s
`shader.locs[]` fixup, `grass_material.vs`, and raylib's expected
`DrawMesh()`/`SHADER_LOC_MATRIX_MODEL` upload behavior all looked correct
on paper, and the workaround above was chosen over continued guessing. If
`prop_lit`'s specular highlight ever looks subtly wrong (e.g. doesn't track
camera movement correctly), start here. A real diagnostic (not more static
reading) would settle it: temporarily output `fragPosition` directly as
`finalColor` in either shader and screenshot it from two different camera/
player positions — if the color pattern shifts on screen by anything other
than the expected camera-relative reprojection of a truly static world
pattern, the bug is confirmed and localized.

---

## Item 39 — Procedural geometry / VFX composition audit checklist (found via Crystal Cluster, not yet applied to the rest of `core/geometry`/`core/composition`)

**Status: RESOLVED (2026-07-09).** Both audits below were run against every
file listed under "Not yet touched" — findings:
- **Audit A (draw-cost):** `Rock`/`ShardCluster`/`Tube`/`WavePlane`/
  `CurlingWave`/`VortexFunnel`/`Fissure` (`core/geometry/pm_*.inl`) checked.
  Every `Draw*` other than Rock's is a **single-shape-per-effect** draw
  (one tube, one funnel, one pillar/puddle), already consolidated to one/two
  `rlBegin`/`rlEnd` per call — no instancing applies to a single instance,
  no changes needed. `ProceduralMesh_DrawShardCluster`/`DrawWavePlane`/
  `DrawCurlingWave`/`DrawFissure` have **zero call sites** anywhere in
  `core/composition`/`skills/` (repo-wide grep) — left alone as unused-but-
  valid public API, not deleted.
  Two real N-copies-per-frame candidates were found and converted to GPU
  instancing (Item 40 follow-up, see below): `VFX_ComposeFloatingStones`
  (`vc_earth.inl`, 5 rocks) and `VFX_ComposeMetalShardCluster`'s 4-blade loop
  (`vc_metal.inl`, explicitly flagged by name in this item's original text).
- **Audit B (seed/variety):** `vc_fire.inl`, `vc_wood.inl`, `vc_earth.inl`,
  `vc_taiji.inl` grepped for `seed` — **zero matches in all four files**
  (none of their `VFX_Compose*` call a seed-taking `ProceduralMesh_Build*`/
  `Draw*`). Nothing to fix; audit closes clean.

The two-audit checklist itself remains valid and reusable for any future
procedural shape — re-run it before assuming a new `Draw*`/`VFX_Compose*` is
already optimal, per the original findings below.

Original finding: four distinct, unrelated-looking bugs were found and fixed
across one skill (`glacial_cannon_skill`'s ice-crystal effects) in the same
session, and all four turned out to be **instances of two repeatable
patterns** rather than one-off mistakes. Filed so the same two audits get
run against every other procedural shape in `core/geometry/*.inl` and every
`VFX_Compose*` in `core/composition/vc_*.inl` — most of them have never been
looked at through this lens.

### What actually happened (reference case)

1. **`ProceduralMesh_DrawCrystalCluster`** looped `rlPushMatrix` +
   `rlBegin`/`rlEnd` per crystal (N GL state pushes + N immediate-mode
   submissions for N crystals, every frame) → merged into building the whole
   cluster into one flat buffer, tilt applied via CPU `Vector3RotateByAxisAngle`
   instead of the GL matrix stack, submitted with exactly one `rlBegin`/`rlEnd`.
   (`core/geometry/pm_crystal.inl`)
2. That still rebuilt + resubmitted thousands of vertices through
   `rlVertex3f` every single frame for geometry that wasn't changing — for a
   "hero burst" (10 crystals, cast repeatedly) this measurably dropped FPS to
   ~30. First fix attempt: `ProceduralMesh_BuildCrystalClusterMesh` — bake
   once into a real `Mesh`, `UploadMesh` once, `DrawMesh` every frame. Better,
   but still wrong: it built (and `UploadMesh`'d) a **new** `Mesh` on every
   single cast.
3. `UploadMesh` (`glGenBuffers`/`glBufferData`) is a real GPU-driver
   synchronization point, not a cheap CPU op like `DrawMesh`. Rebuilding one
   per cast is fine for a single character casting occasionally, but
   **stutters** the moment several casts land in the same short window
   (rapid clicking, multiple casters) — several `UploadMesh` calls bunching
   up in one/few frames, not the cheap per-frame `DrawMesh` cost. Fix:
   `ProceduralMesh_BuildCrystalTemplateMesh` — build **one** crystal once,
   ever (same lifetime as a loaded shader/texture — never rebuilt, never
   unloaded), then draw N "different-looking" crystals via N `DrawMesh` calls
   with a **different transform** (translate/rotate/non-uniform-scale,
   computed cheaply on the CPU from a per-instance seeded hash) instead of N
   different meshes. Zero `UploadMesh` after the first ever call, regardless
   of cast frequency. (`VFX_DrawIceCrystalBurst`, `core/composition/vc_water.inl`)
4. Even with variety fixed, the burst still looked identical every cast: all
   N crystals shared one `growProgress` uniform, so they always grew in
   perfect lockstep — the *timing* was deterministic-in-the-wrong-way even
   though spatial layout wasn't. Fix: stagger each crystal's local grow
   progress by a per-instance seeded delay (`localGrow = clamp((growProgress
   - startT) / (1 - startT), 0, 1)` where `startT` comes from the same RNG
   draw as position/tilt) so the *sequence* of growth also varies per cast.
5. Separately, `VFX_PathWave`'s `PATH_ICE_SPIKE` case (the ice spikes drawn
   along the projectile's flight path, a **different** code path from the
   impact burst above) called `VFX_ComposeIceCrystal(pos, i)` — using the
   **loop index over a fixed 16-point path** as the seed. Since the path
   always has the same 16 points with the same indices every cast, this
   looked pixel-identical on every single cast regardless of anything fixed
   in items 1-4 above — a *different* function with the exact same seed-
   misuse bug. Fixed by threading a real per-cast `seed` (`GetTime()*10000 ^
   GetRandomValue(...)`, generated once in `CastGlacialCannonSkill`) through
   `VFX_PathWave`'s new `seed` parameter, mixed with the point index via an
   LCG (`rng = seed*747796405u + i*2891336453u; rng = (rng^(rng>>16))*1664525u
   + 1013904223u`) into a per-point `pointSeed`.

### The two audits to run against the rest of the codebase

**A. Draw-cost audit** (`core/geometry/pm_*.inl` — Rock, ShardCluster, Tube,
WavePlane, CurlingWave, VortexFunnel, Fissure all unaudited as of this
writing):
- Does the `Draw*` function loop `rlBegin`/`rlEnd` (or `rlPushMatrix`) per
  sub-element (per rock, per shard, per segment)? If the underlying geometry
  is static within one call, that should be one `rlBegin`/`rlEnd` for the
  whole shape (pattern: item 1 above), not one per sub-element.
- Is a `Build*`/`Draw*` pair (or a raw immediate-mode `Draw*`) getting
  called **fresh every cast** for a VFX that (a) has many vertices and (b) is
  redrawn every frame while alive, or (c) can be cast repeatedly/by multiple
  casters in a short window? If (a)+(b): batch into one `Mesh`, build once
  per VFX instance (existing `ProceduralMesh_CreateBaseGrid`-style
  convention, already fine as long as it's really "build once per instance,
  not per frame"). If (c) on top of that: don't build a **new** `Mesh` per
  cast — build **one reusable template once, ever**, vary appearance via
  per-instance `transform` (pattern: item 3 above). `ProceduralMesh_BuildCrystalClusterMesh`
  is kept in the API specifically as the "wrong for bursty casts, fine for a
  genuinely-static one-off prop" cautionary example — its doc comment in
  `procedural_mesh_utils.h` spells out when each of the two approaches applies.
- `vc_metal.inl`'s `VFX_ComposeMetalShardCluster` has its own hand-rolled
  4-blade `rlPushMatrix`+`ProceduralMesh_DrawCrystal` loop (flagged, not yet
  fixed — see prior session note) that has the exact same shape as pattern 1.

**B. Seed/variety audit** (`core/composition/vc_*.inl` and every skill `.c`
that calls a `VFX_Compose*`/`ProceduralMesh_Draw*`/`ProceduralMesh_Build*`
taking a `seed`/`int` randomness parameter):
- `grep -rn "seed" core/composition/*.inl skills/*/*/*.c` and eyeball every
  call site's seed **expression**, not just whether a seed parameter exists.
  Red flags: a raw loop/array index (`i`) used as the seed with a fixed-size
  loop (same indices every call — item 5's exact bug); `agentId` alone with
  no time/counter component (same caster always gets the same look);  a
  hardcoded constant.
- Any multi-object burst/cluster effect that shares **one** progress/grow
  uniform across all sub-objects reads as mechanically synchronized even
  once spatial variety is fixed (item 4's bug) — stagger per-instance using
  the same seeded hash already driving position/tilt, don't add a second
  unrelated RNG source.

### Not yet touched, likely worth the same pass

~~`vc_fire.inl`, `vc_wood.inl`, `vc_earth.inl`, `vc_taiji.inl` (composition
layer — audit B), and Rock/ShardCluster/Tube/WavePlane/CurlingWave/
VortexFunnel/Fissure (`core/geometry/pm_*.inl` — audit A). None of these were
inspected this session; this item exists so the next pass through them
starts from a checklist instead of rediscovering the same two bug shapes
one skill at a time.~~ **Done — see the RESOLVED note at the top of this
item (2026-07-09).** Nothing left in this list; any *new* shape added later
should still run through this same checklist before being assumed correct.

---

## Item 40 — Crystal Cluster GPU instancing (DONE, verified in-game — canonical reference implementation)

**Status: RESOLVED.** Follow-up to Item 39: `VFX_DrawIceCrystalBurst` was
looping `DrawMesh` once per crystal (N draw calls sharing one template mesh
— fine, but not the ceiling). Converted to a single `DrawMeshInstanced` call
per burst. User confirmed correct in-game rendering after the change.

**This is now the canonical GPU-instancing reference implementation** — the
full standard/checklist for applying this pattern elsewhere lives in
`CORE_API.md` under "GPU Instancing — standard pattern for 'many copies of
the same mesh, same frame'" (Procedural Mesh section, right after Crystal
Cluster). Read that before instancing any other shape — don't re-derive the
approach from scratch. Summary of what changed:

- **`core/shaders/crystal_instanced.vs`** (new file, does not touch the
  shared `vs_header.glsl`): declares `in mat4 instanceTransform;` (raylib's
  reserved instancing attribute name, auto-bound by `LoadShader` — no extra
  C-side setup needed) and manually computes `matModel * instanceTransform`
  for `fragPosition`/`fragNormal` and `mvp * instanceTransform` for
  `gl_Position`, instead of the shared `VS_FinalOutput()` helper (which only
  knows about a single per-draw-call `matModel` uniform, not a per-instance
  attribute). `crystal.fs` is reused completely unchanged — instancing is a
  vertex/transform-only concern.
- **`CrystalMaterialInstanced`** (`core/material/material_system.h/.c`) —
  a duplicate of `CrystalMaterial`'s struct/`_Load`/`_Begin`/`_End` shape,
  pointed at `crystal_instanced.vs` instead of `crystal.vs`. Necessary
  duplication, not sloppiness: a separately-linked shader program can have
  different uniform *locations* for the same-named uniform, so the loc-cache
  fields have to be re-queried against the new program — they can't be
  shared with the non-instanced `CrystalMaterial`.
- **`VFX_DrawIceCrystalBurst`** (`core/composition/vc_water.inl`) — same
  per-instance transform math as before (position/tilt/scale from a seeded
  LCG), but now collects all `N` transforms into a `Matrix[]` array and
  submits them with one `DrawMeshInstanced` call instead of `N` `DrawMesh`
  calls.

**Known trade-off, accepted:** the per-crystal grow-progress stagger added
earlier in the same session (each crystal starting its "grow up" animation
at a slightly different time, so a burst doesn't look mechanically
synchronized) had to be **removed** — instancing gives the whole batch one
shared set of uniforms per draw call, so `u_growProgress` can't vary
per-instance without a second custom instance-attribute buffer (real extra
engineering, not built here — nothing currently needs it enough to justify
it; see the "bad fit" note in the CORE_API.md standard). All crystals in one
burst now grow in lockstep again. Positional/shape variety (the thing that
actually matters for "doesn't look identical every cast" — see Item 39) is
untouched.

**If a future shape needs true per-instance uniform variation** (not just
transform): that's the actual scenario the standard's "bad fit" callout is
for. It needs a second `rlLoadVertexBuffer`-backed instance attribute
alongside `instanceTransform` (not covered by `DrawMeshInstanced`'s
convenience wrapper, which only manages the transform buffer) — no existing
code in this engine does this yet; whoever picks it up is establishing new
precedent, not following one.

### Follow-up conversions #2 and #3 (2026-07-09, Item 39's audit A findings)

Both use the exact checklist above, no deviations:

- **`VFX_ComposeMetalShardCluster`'s 4-blade loop** (`core/composition/vc_metal.inl`)
  — was explicitly flagged by name in Item 39's original text as having "the
  exact same shape as pattern 1" (per-blade `rlPushMatrix`+`ProceduralMesh_DrawCrystal`,
  every frame). Converted to `GetMetalBladeTemplateMesh()` (one crystal
  template, built once via `ProceduralMesh_BuildCrystalTemplateMesh`) +
  `CrystalMaterialInstanced` (reused directly — this effect already used
  `CrystalMaterial`, no new material system needed) + one `DrawMeshInstanced`
  for the 4 blades. **Trade-off:** each blade's `twist` used to vary per-blade
  (`desc.twist * (0.5 + r02)`) — twist changes mesh topology, not
  representable as a rigid transform, so the template now uses one fixed
  `twist` for all 4 blades (`GetMetalBladeDesc`, same file). Height/radius
  variety is preserved via non-uniform `Matrix` scale instead. The
  micro-crystal cluster drawn right after (`ProceduralMesh_DrawCrystalCluster`,
  5 tiny stubs) was left untouched — it already goes through the
  batched-cluster path from this item's original fix, not a per-instance loop.
- **`VFX_ComposeFloatingStones`** (`core/composition/vc_earth.inl`) — was 5
  `rlPushMatrix`+`ProceduralMesh_DrawRock` calls per frame (an ongoing
  levitation effect, not a one-shot burst — drawn every frame for the
  effect's whole lifetime). N=5 is below this item's original "N≳10" soft
  threshold for a one-shot burst, but justified here because the draw
  repeats every frame rather than once per cast (see the softened criterion
  added to the CORE_API.md decision tree). This effect used `EffectMaterial`
  (`Material_Get(MAT_ROCK)`), not `CrystalMaterial` — no instanced twin
  existed yet, so this conversion **added new reusable infra**:
  `core/shaders/effect_material_instanced.vs` + `EffectMaterialInstanced`
  (`core/material/material_system.h/.c`), a duplicate of `EffectMaterial`'s
  struct/`_Load`/`_Begin`/`_End` shape pointed at the new shader — exactly
  mirroring `CrystalMaterialInstanced`'s relationship to `CrystalMaterial`.
  Also added `ProceduralMesh_BuildRockTemplateMesh` (`core/geometry/pm_rocks.inl`)
  — one GPU-resident rock template, mirroring `ProceduralMesh_BuildCrystalTemplateMesh`.
  **Trade-off:** all 5 stones now share one rock silhouette instead of 5
  independently-seeded shapes (`MeshCache_GetRock(i*733+17, ...)` per stone)
  — variety comes from transform (orbit position/tumble/scale) only, same
  precedent as the ice-crystal burst. `MeshCache_GetRock`/`ProceduralMesh_DrawRock`
  are untouched and still used elsewhere (e.g. `VFX_ComposeBoulder`, a
  single-instance draw where instancing doesn't apply).

`EffectMaterialInstanced` is now available for any future `EffectMaterial`-
backed effect that needs N transform-only copies — see the two-material
decision guidance (Crystal-backed vs EffectMaterial-backed) in CORE_API.md's
GPU Instancing decision tree.

Both verified: `cmake --build build -j4` compiles clean. Visual in-game
confirmation of both effects (Floating Stones, Metal Shard Cluster) is
pending the user's own check — no headless harness covers this.

---

## Item 41 — Raylib 5.5 → 6.0 upgrade (DONE)

**Status: RESOLVED.** `CMakeLists.txt`'s `FetchContent_Declare(raylib ...
GIT_TAG 6.0)` (was `5.5`). Motivation: keep the dependency current while the
codebase is still small enough that a version bump is cheap — deferred
further gets more expensive as more code accumulates API-specific
assumptions.

**What was checked:** full clean rebuild (`_deps`/`build` cleared first —
`FetchContent` does not re-fetch on a changed `GIT_TAG` alone if the old
checkout is still present) compiled with **zero errors across every
module** (`core/`, `compute/`, `environment/`, `entities/`, `game/`,
`sandbox/`, `skills/`, `maps/`) — no breaking API changes from 5.5 hit any
call site in this codebase. Binary launches, logs confirm `Initializing
raylib 6.0`, no shader/texture/asset load errors in the startup log.

**Not exhaustively re-verified:** this confirms *compiles and boots
cleanly*, not full gameplay/visual regression across every skill/map — deep
runtime behavior changes (if any exist between 5.5 and 6.0) wouldn't
necessarily show up as a compile error or a boot-time log line. If anything
looks visually different after this point that isn't explained by other
changes in the same session, suspect this first.

**Docs updated to match:** root `CLAUDE.md`, `core/CLAUDE.md`,
`CORE_API.md`, `EXTERNAL_API.md`. **`CORE_API_SHORT.md` NOT updated** (still
says 5.5 on its line 9) — per its own stated rule it's manual-only, not
auto-synced with routine edits; regenerate it on request, not proactively.

**Follow-up bug (also RESOLVED): window invisible after upgrade.** After the
6.0 bump, `./build/wuxing` produced no visible window on this macOS +
Intel HD 6000 machine (Dock icon present, `InitWindow` logged success, but
nothing rendered on screen). Root cause found by bisection with an isolated
minimal raylib-6.0 app: `CMakeLists.txt` had
`set(CUSTOMIZE_BUILD ON CACHE BOOL ... FORCE)`, which changes raylib's own
internal `SUPPORT_*` build defaults on 6.0 (the project never actually set
any custom `SUPPORT_*` flags, so the line was doing nothing useful). With
`CUSTOMIZE_BUILD ON`, the mini app's window also failed to appear; removing
it fixed both the mini app and the real project. Fix applied: removed the
`CUSTOMIZE_BUILD ON` line from `CMakeLists.txt` (see comment there). Do not
re-enable it without also setting matching `SUPPORT_*` flags and re-testing
window visibility on this hardware. Confirmed 2026-07-09: full rebuild from
clean `_deps`/`build`, window renders, whole app runs normally.