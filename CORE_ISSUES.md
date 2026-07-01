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

## Item 6 — Generic FloatCurve (envelope) primitive — DONE

Identified while cross-checking `WUXING_ART_DIRECTION.md` Chapter 4.3
("The Four Curves": Intensity/Density/Motion/Lighting, each shaped
low → build → peak → fall → zero over a skill's lifetime) against the
actual Core API surface. `core/color_gradient.h`'s `ColorGradient`
already provides stops + `ColorGradient_Sample(t)` for **color**, but
there is no equivalent for a plain scalar float — every skill hand-rolls
its own lerp/easing for particle rate, light intensity, motion speed,
etc. This is the direct cause of the anti-patterns Chapter 4.6 warns
about ("constant particle emission — no rhythm", "maximum brightness
lasts too long — impact feels weak").

**Implemented:** `FloatCurve` in `core/float_curve.h/.c`, mirroring
`ColorGradient`'s API 1:1 — `FloatCurve_AddStop(t, value)`,
`FloatCurve_Sample(t)`, `FLOAT_CURVE_MAX_STOPS 8` (same cap style as
`COLOR_GRADIENT_MAX_STOPS`), linear interpolation matching
`ColorGradient`'s existing behavior — no extra easing-curve machinery,
since no real skill has needed it yet. Registered in `CMakeLists.txt`
and `Makefile.Android` (both platforms build clean, no regressions).
Documented in `CORE_API.md` § Color Gradient's neighboring section. Not
yet wired into any shipping skill — that's for a skill author to adopt
per-curve when replacing a hand-rolled lerp.

---

## Item 7 — Multi-layer Timeline (stagger schedule) — DONE

Identified against `WUXING_ART_DIRECTION.md` Chapter 4.4 ("Layer
Activation Timeline") — an explicit per-layer start/duration Gantt
(Core Body / Energy / Trail / Flash / Light / Smoke / Dust / Decal /
Distortion) that staggers when each visual layer turns on/off — plus
the "Fade" rule in 4.2 (every layer must fade independently, never all
at once). `core/skill_helper.h`'s `SkillTimeline` only exposes
`current/duration` plus a single-shot `Timeline_Event(triggerTime)`
boolean check — there's no way to declare N named layers with their own
start/duration in one place. Skills currently hand-write scattered
`if (t > X && t < Y)` blocks per layer, which is exactly how Chapter
4.6's "all particles disappear together — artificial" mistake happens
in practice.

**Implemented:** a sibling `LayeredTimeline` in `core/skill_helper.h/.c`
(kept separate from `SkillTimeline` rather than extending it — different
struct shape, and no existing call site to migrate) with a fixed-size
table of `{tag, start, duration}` entries (`TIMELINE_MAX_LAYERS 8`,
`Timeline_AddLayer`), plus `Timeline_IsLayerActive(t, layerIndex)` /
`Timeline_LayerProgress(t, layerIndex)` (0..1 within that layer's own
window, feeds directly into Item 6's `FloatCurve_Sample`). Static array,
no malloc. Same "caller advances `current += dt`" convention as
`SkillTimeline` — nothing ticks time internally.

Covers both authoring needs with the same table:
- **Continuous windows** (Chapter 4.4's Gantt layers — Trail/Light/Smoke
  each active over a span): `duration > 0`, sampled via
  `Timeline_IsLayerActive`/`Timeline_LayerProgress`.
- **Discrete tagged events** (Chapter 4.1's phase transitions — windup →
  cast → travel → impact → aftermath, each a one-shot fire): `duration`
  ~0, fired once via `Timeline_LayerEvent`, same edge-detection as the
  existing `Timeline_Event`'s crossing check.

Not implemented: the optional function-pointer callback per tag
(mirroring `RegisterSkill`'s callback pointer) — no skill has needed it
yet; `Timeline_IsLayerActive`/`Timeline_LayerEvent` polled per-frame in
`Update[Name]Skill` covers every case seen so far. Add it later if a
real skill's call-site pattern justifies it. Builds clean (desktop),
documented in `CORE_API.md` next to Skill Timeline. Not yet wired into
any shipping skill or `core_test` — adoption is a per-skill choice when
replacing hand-written `if (t > X && t < Y)` staggering.

---

## Item 8 — Material system: parametrize `EffectMaterial` beyond 4 hardcoded presets — DONE

`core/skill_helper.h`'s `EffectMaterial`/`Material_Load`/`Material_SetFloat`/
`Material_Begin`/`Material_End` currently only cover 4 hardcoded presets
(`MATERIAL_FIRE`/`ICE`/`WATER`/`PORTAL`), each backed by 1 shader with 2
fixed uniform slots (`uTimeLoc`, `uDissolveLoc`). Any skill wanting a
different combination of surface effects (rim/fresnel strength, emissive
intensity, distortion amount, a second texture slot) has to bypass
`EffectMaterial` entirely and hand-roll `BeginShaderMode`/
`SetShaderValue` calls itself — inconsistent with the rest of Core's
"orchestrate via one struct" pattern (`TubeMeshConfig`, `ColorGradient`,
etc.), and this is the module the codebase is objectively strongest at
(shader support) without a matching authoring struct.

**Implemented:** `EffectMaterialParams` (`core/skill_helper.h`) —
`baseColor`, `rimStrength`, `fresnelPower`, `emissiveIntensity`,
`distortionStrength`, plus a generic `texture1` secondary-texture slot
(`id==0` = unused, guarded in-shader via `u_hasTexture1` rather than
sampling an unbound unit). `Material_Load` keeps its exact original
signature/behavior for the 4 hardcoded presets — zero call sites to
break, confirmed none exist (see note below). Added
`Material_LoadCustom(EffectMaterialParams)` alongside it, backed by one
new shared shader family: `core/shaders/effect_material.vs/.fs`
(`#include`-based, GLES 3.0 runtime path per the Android two-path
shader architecture — no build-script changes needed). `Material_Begin`
re-uploads all param uniforms every call (not just once at Load), since
the shared shader is a single cached `Shader` object — if it were set
once at Load time, two different `Material_LoadCustom` instances using
the same shader would silently stomp each other's uniform values the
next time either one draws. `baseColor` uses the same
`ColorNormalize()` → `SHADER_UNIFORM_VEC4` pattern already established
in `core/metaball_fx.c`.

**Note — this shader ignores per-vertex color.** The existing
`vs_header.glsl`/`fs_header.glsl` 3D-lighting convention (used by
`tube.fs` etc.) never carries a `fragColor` varying from VS to FS — so
whatever `Color` you pass to the mesh-draw call between
`Material_Begin`/`Material_End` has no effect; tint comes only from
`EffectMaterialParams.baseColor`. Documented in `CORE_API.md` so a
future skill author doesn't lose time debugging why their mesh color
argument is ignored.

**Verified via `core_test`, 4 real bugs found and fixed during that
verification (not guessed — each traced to a specific line):**
1. **VS wobble used raw world-space `vertexPosition`** (`sin(vertexPosition.x * 3.0 + ...)`)
   instead of `vertexNormal` — immediate-mode draws bake world coordinates
   directly into `vertexPosition` (matModel is identity per Rule D), and the
   arena is centered around `(600, 0, 440)`, so `sin()`/`cos()` of an
   argument around 1800+ loses precision on GPU, producing noisy/unstable
   displacement instead of a smooth wobble. Fixed: phase now driven by
   `vertexNormal` (always `[-1..1]`), position-independent.
2. **Dissolve edge-glow noise used `hash3(floor(fragPosition * 6.0))`** —
   same world-position-magnitude problem feeding into `hash3`'s internal
   `sin(dot(p, largeWeights))`. Fixed: uses `normal` instead, also making
   the dissolve pattern identical regardless of where in the arena the
   material is drawn.
3. **`fx.glsl`'s `dissolveCalc()` computes a nonzero `edgeFactor` for ~8%
   of fragments even at `u_dissolve == 0.0`** (its check is `noiseVal <
   dissolve + edgeWidth`, not gated on `dissolve > 0`) — this showed up as
   scattered bright speckle across the whole surface the instant the
   material appeared, well before anything was meant to be dissolving.
   Fixed by gating the whole dissolve/edge-glow block on `u_dissolve > 0.0`
   in `effect_material.fs` (not touching the shared `dissolveCalc()` itself,
   since other callers may rely on its exact current signature/behavior).
4. **`texture1` sampled as full `detail.rgb` at 2x UV tiling** —
   `assets/textures/crack.png` was authored for a flat ground-decal quad,
   not a sphere (whose UV pinches hard at the poles); tiling it 2x and
   importing its own hue produced visible rainbow-ish color noise. Fixed:
   sampled as a luminance-only mask (`.r` channel, 1x UV, no `* 2.0`
   brightness boost).

Also added (not in the original proposed param list, but needed once a
real reference — the existing `tube.fs`/`MATERIAL_WATER` preset — was
used as the visual bar to match): **`translucency`** param + fresnel-driven
alpha (`mix(0.3, 0.9, fresnel)`, same formula as `tube.fs`), since the
initial all-opaque-alpha implementation had no way to reproduce a
glass/water "see-through center, more solid edges" look at all. Default
`0.0` keeps existing opaque behavior; caller must additionally wrap the
draw in `BeginBlendMode(BLEND_ALPHA)`/`EndBlendMode()`. Rim glow was also
reweighted by light-facing direction (`mix(0.3, 1.0, dot(normal, lightDir))`)
since pure view-angle Fresnel glowed evenly around the whole silhouette
regardless of actual light position, which read as "wrong direction".

Final verified test: a sphere using `Material_LoadCustom` (Taiji tint,
`rimStrength=0.7`, `fresnelPower=4.0`, `emissiveIntensity=0.15`,
`distortionStrength=0.4`, `translucency=1.0`, `texture1` =
`assets/textures/crack.png`) holds solid 1.5s then dissolves out over 1s
via `Material_SetFloat(&mat, "u_dissolve", t)` — confirming the existing
generic name-based uniform setter still works unmodified against the new
parametrized shader.

**Found in passing, NOT fixed (out of scope for this item):** the 4
existing `MaterialPreset` shader paths
(`skills/fire/fire_wildfire/...`, `skills/water/frost_blossom_rain_skill/...`,
`skills/taiji/yin_yang_orb/...`) reference files that no longer exist
anywhere in the repo. `Material_Load` has zero callers currently, so
this was never exercised/noticed at runtime — `ResourceManager_LoadShader`
just returns `shader.id == 0` (Rule C guard: silently renders nothing,
no crash). Whoever first actually wires up `Material_Load` with one of
these 4 presets needs to fix/repoint the shader paths first.

**Found in passing, NOT fixed (bigger than this item — see Item 10):**
comparing the rim glow against the test character's actual shadow showed
the rim's light direction doesn't match the environment's real sun
direction. Root cause: `lightDir` is hardcoded as `vec3(0.5, 0.8, 0.5)` in
`effect_material.fs`, matching `lighting.glsl`'s documented project-wide
convention — every other skill shader using `calcFresnel`/`calcDiffuse`
(e.g. `tube.fs`) hardcodes the same fake value. `Environment_GetSunDirection()`
exists but is never actually wired into any shader uniform anywhere in
the codebase. User explicitly chose not to special-case this one
material (would make it inconsistent with every other skill) — see Item
10 for the real, engine-wide fix.

---

## Item 9 — Data-driven tuning (lower priority, higher effort — do separately) — NOT STARTED

Every tunable VFX number (radius, speed, color-gradient stops, curve
stops once Item 6 exists, etc.) is currently a C literal — changing one
requires a full rebuild. A simple key-value config file, loaded once at
startup and hot-reloaded on file-change (mtime poll, no filesystem-watch
dependency needed for a single dev machine), would let a skill author
iterate on feel without recompiling. Real win for iteration speed, but
meaningfully more effort than Items 6–8 (needs a parser, a registry of
"tunable" values skills opt into, and a reload path that doesn't
clobber runtime state) — **don't bundle with Items 6–8**, pick up as
its own pass once those land and their new knobs (curve stops, layer
timings, material params) are stable enough to be worth exposing.

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