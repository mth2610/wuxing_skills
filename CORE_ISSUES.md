# Core Engine — Open Issues / Unfinished Work

Tracks work from the "Core API update" task list that is either unfinished
(reverted, needs a fresh approach) or not yet started. See `CORE_API.md` for
the API surface that IS shipped and documented.

---

## Item 3 — Soft Particles (REBUILD IN PROGRESS, paused — 3 real bugs fixed, 1 open question)

**Status: `core/screen_distort.c/.h` infrastructure is still in the tree and
untouched.** Builds clean, no regressions. Paused mid-verification because
the debugging loop was taking too long — picking this up again should be
much faster than starting over, since 3 of probably ~4 real bugs are now
confirmed fixed.

> [!NOTE]
> **Test harness removed.** `core_test` (`skills/taiji/core_test/`) was
> repurposed as a single-purpose test skill for Item 4a (triplanar mapping)
> — `core_test_soft.vs/.fs` and `core_test_skill.c`'s `DrawSoftParticleShape()`
> described below were deleted, along with the dissolve/metaball test shapes.
> Whoever resumes Item 3 needs to re-add a soft-particle test shape (sphere +
> `ScreenDistort_BindDepthForSoftParticles`/`UnbindSoftParticleDepth` call
> sites, depth-test/mask disable pattern) before re-verifying — the engine
> side (`core/screen_distort.c/.h`, `core/shaders/depth_copy.fs`,
> `core/shaders/common/soft_particle.glsl`) is unaffected and still in place.
> The dissolve edge glow (`fx.glsl`'s `dissolveCalc`) and metaballs
> (`core/metaball_fx.h`) APIs themselves are also unaffected — only their
> `core_test` demo call sites were removed.

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

### Open question — not yet confirmed (where to pick this back up)

With all three bugs above fixed, the test sphere was *still* fully
invisible at the original test camera position. Investigation (numeric,
not screenshot-color-guessing) showed why: `SoftParticle_Factor`'s
`diff = sceneLinear - fragLinear` was strongly **negative** — the
previous-frame scene depth behind the sphere (read at screen-center) was
only `~17` world units, much *closer* than the sphere itself (`~175` units
away from that test camera). That means some opaque geometry (almost
certainly a bamboo stalk in the test map, given how dense/close they are
around the chosen test position) sat directly between the camera and the
sphere, in front of it, in the previous frame's depth buffer. The formula,
as written, correctly fades to 0 when the recorded "scene" is closer than
the particle — which is arguably correct behavior for an occluder *in
front*, just not the demo this test position was meant to show. Repositioning the camera to a steep top-down angle (avoiding the bamboo
line-of-sight) got as far as confirming non-black output before this task
was paused — **not yet confirmed as a clean, fully-faded-gradient pass.**

**Next steps for whoever resumes:**
1. Find/confirm a test camera + sphere position with clear open
   background behind the unburied top of the sphere (no intervening
   geometry), and verify visually that the top stays opaque while the
   buried bottom fades smoothly — the actual feature pass/fail criterion.
2. Decide intentionally whether "fade to 0 when occluded by something in
   front" (current formula behavior) is the desired semantic, or whether
   particles in front of closer occluders should hard-discard/clip instead
   of fade — currently accidental-by-formula, not a deliberate design call.
3. Once visually confirmed: re-add the "Soft Particles" section to
   `CORE_API.md` (removed in the original revert, commit `54a9c4c`),
   tune `CORE_TEST_SOFT_FADE_DISTANCE` (currently `30.0f`, unvalidated) to
   a sensible default, and flip `core_test_skill.c` back to a real
   `Cast`-triggered demo if useful for future regression checks.

### Process note for next time
A numeric CPU readback of the actual texture/uniform values (`LoadImageFromTexture`
+ inspect specific pixels, or log resolved `GetShaderLocation` results) caught
two of the three bugs above almost immediately, where screenshot-color
guessing previously produced repeated false "it's broken" / false "it's
fixed" conclusions (this session included — see bug #3's writeup; an early
attempt to verify the texture read via a `/500`-divided color swatch looked
like solid failure, but the real value was just too small to see against a
dominant channel, not actually zero). **Prefer an unclamped/raw numeric
check over a manually-scaled color visualization when verifying a shader
value is non-zero or correct** — scaling for human visibility and
correctness-checking are different goals and conflating them produced
exactly the kind of false reading this rebuild was meant to avoid.

---

## Item 4 — Material/Trail/Decal/Bloom upgrades (ALL DONE: 4a/4b/4c/4d)

From the original Core API update request. Assessed as legitimate, real
gaps (not redundant with existing code). Four independent sub-items — can
be picked up in any order or split across sessions:

### 4a. Triplanar mapping — DONE
`core/shaders/common/triplanar.glsl` (`triplanarWeights`, `triplanarNoise`,
`triplanarSample`) — see `CORE_API.md` § GLSL Shader Guidelines →
"Triplanar Mapping". Blends 3 world-space axis projections by world normal,
so jagged `ProceduralMesh_Draw*` shapes (Rock, ShardCluster, Fissure — all
rlBegin immediate-mode, position+normal only, no UV) don't stretch/streak.
Tested via `skills/taiji/core_test` (`core_test_triplanar.vs/.fs`): a
`ProceduralMesh_BuildRock` shape shaded with `triplanarNoise` + diffuse/
specular/Fresnel. `triplanarSample(sampler2D, ...)` is the texture-asset
variant for real Earth/Metal materials — not yet wired into a shipping
skill, only the procedural-noise path is exercised by the test.

### 4b. Flow-mapped decals — DONE
`DecalSystem_AddFlowEx` (`core/decal_system.h/.c`) + `core/shaders/decal_flow.fs`
— see `CORE_API.md` "Ground Decals" section. Radially scrolls the decal
texture outward from center over time (`fract(dist - u_time*flowSpeed)`)
instead of a static stamped image; renders via a separate shader pass per
blend-mode group so it doesn't touch `Add`/`AddEx`'s existing behavior or
cost for any other decal. Wired into `SpawnGroundDecal` (`core/skill_helper.c`)
for `DECAL_PRESET_FIRE_LAVA` and `DECAL_PRESET_WATER_RIPPLE`
(`flowSpeed=0.6, flowStrength=0.8`) — every other preset is unaffected
(still static). Tested via `skills/taiji/core_test`: casting spawns a
`DECAL_PRESET_FIRE_LAVA` decal under the triplanar rock; confirmed visually
— concentric rings scrolling outward from center, not a static stamp.

### 4c. Dynamic trail attachment (`core/trail_system.h`) — DONE
`Trail_AttachToTransform(int id, const Matrix *targetTransform, Vector3 localOffset)`
added to `core/trail_system.h/.c`. Stores the pointer on the `TrailEntity`
(`attachedTransform` + `attachLocalOffset` fields); `UpdateTrailSystem` reads
it each frame and drives tip position via `Vector3Transform(localOffset, *mat)`
→ `UpdateFollowerPosition`. Caller (skill/sandbox) owns the `Matrix` and updates
it each frame — same pattern as the existing `ForceField *` pointer on `TrailEntity`.
**Investigation finding:** `entities/entities.h` has no bone/skeleton exposure
(minimal gameplay module — position/velocity/health only). The pointer-based
design is correct for any matrix source (manual orbit, bone from `ModelAnimation.framePoses`,
procedural pivot, etc.) — Core stays decoupled from how the caller produces the matrix.
Tested via `skills/taiji/core_test`: a `TRAIL_TYPE_FOLLOWER` trail orbiting the
caster at radius 60 / Y 30 using `MatrixTranslate` updated each frame — trail
follows the orbit visibly.

### 4d. Dual-filtering bloom (`core/post_fx.h`) — DONE
Replaced separable Gaussian (`bloom_blur.fs` H+V passes) with a dual-filter
pyramid: bright pass (full→1/4) + downsample chain (1/4→1/8→1/16,
`bloom_downsample.fs`) + upsample chain (1/16→1/8→1/4, `bloom_upsample.fs`).
Produces a wider, softer glow. Recommended scene values: `bloomThreshold=0.5f`,
`bloomIntensity=2.0f` (dark arena).

**Three root-cause bugs fixed in the process (bloom was never working):**
1. **Bright pass used `DrawTextureRec` (1:1 scale)** into a 1/4-size target — only
   captured the top-left quarter of the scene; content anywhere else never triggered
   bloom. Fix: `DrawTexturePro` with explicit src→dst scaling.
2. **Bright pass missing Y-flip** (`-height`) — bloom appeared at the vertically-mirrored
   position of each bright pixel (looked like a shadow below the source). Fix: match
   the composite pass's `{0,0,w,-h}` source rect convention.
3. **`u_bloomTex` bound via `rlActiveTextureSlot`/`rlEnableTexture`** — silently never
   reached the shader (same root cause as Item 3 soft-particle depth tex). Fix:
   `SetShaderValueTexture` called inside `BeginShaderMode`.

---

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
