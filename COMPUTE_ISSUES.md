## Item 5 — GPU Particle Force Field Integration (`compute/gpu_particle_system.*`, `core/force_field.*`) — PAUSED: CPU/VBO draw invisible on Android only

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