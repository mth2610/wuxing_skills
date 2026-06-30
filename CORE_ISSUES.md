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

## Item 4 — Material/Trail/Decal/Bloom upgrades (4a DONE, 4b–4d NOT STARTED)

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

### 4b. Flow-mapped decals (`core/decal_system.h`)
`DecalSystem_Add`/`AddEx`/`AddStreak` exist but have no flow/scroll concept
— decals are static images. Goal: let `DECAL_PRESET_FIRE_LAVA` /
`DECAL_PRESET_WATER_RIPPLE`-style decals sample a flow map and scroll
outward from center over time instead of being a static stamped texture.

### 4c. Dynamic trail attachment (`core/trail_system.h`)
Existing API (`UpdateFollowerPosition`, `SetFollowerAxis`) supports a
trail following a *point*, not a bone/joint on a skinned IQM model. Goal:
`Trail_AttachToTransform(TrailConfig, const Matrix *targetTransform,
Vector3 localOffset)` so a sword-light/afterimage trail can track an
animated bone matrix. **Needs investigation first**: confirm bone matrices
are actually exposed anywhere accessible to Core (entities/skills modules
own the animated models) before committing to this signature.

### 4d. Dual-filtering bloom (`core/post_fx.h`)
Current bloom (`PostFX_Draw`'s bright/blur passes in `core/post_fx.c`) uses
a simple separable Gaussian (`bloom_blur.fs`), not a dual-filter
(downsample/upsample pyramid) approach. Goal: replace with dual-filtering
for a wider, cheaper glow more suitable for mobile — this is a quality/perf
upgrade to an already-working system, lowest priority of the four.

**4b–4d have no code written yet.** Recommend treating each as its
own scoped task with a visual test (sandbox skill or similar) before
merging, given the Item 3 experience above.
