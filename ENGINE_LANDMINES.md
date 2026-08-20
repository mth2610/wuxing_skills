# ENGINE_LANDMINES.md — Cross-cutting traps every module can hit

> Lessons that are **not** owned by one module — any agent touching GL, shaders, rendering, or the device build can repeat them. Read this before touching GL/shaders or shipping to Android.
>
> Format per entry: **Symptom → Cause → Rule** (see `DOC_ARCHITECTURE.md` §6). Module-local lessons stay in `module/docs/LANDMINES.md`; only promote here when another module could hit it.

## Index

| # | Trap | Bites |
|---|---|---|
| 1 | Raylib batching hazard (depth-state leak) | Anyone drawing with immediate mode + depth state changes |
| 2 | `rlFrustum` / camera near < 1.0 → blank render | Anyone tuning camera clip planes / custom projections |
| 3 | Lit material invisible in the dark night arena | Anyone drawing lit ground/geometry |
| 4 | Mali: `fract(sin(...))` noise hash dies | Any shader using hash noise, on-device only |
| 5 | Mali: top-84px is a mandatory gesture zone | Any on-screen UI/touch target |
| 6 | Android: black screen / crash on device | Anyone changing the Android build flags |
| 7 | `CreateEmitter` needs per-frame target update; name collision | Anyone using the emitter systems |
| 8 | Per-instance uniform changes → stale-UBO scrambling (rlvk) | Any VFX that calls `SetShaderValue` between instances |
| 9 | `matModel` is model×**view** → `fragPosition` is NOT world space | Any shader doing positional lighting/effects, incl. `viewPos - fragPosition` fresnel |
| 10 | `SetShaderValue` writes to the **active** shader under rlvk | Anyone setting uniforms outside `BeginShaderMode` |
| 11 | Colour-only VFX layer clear must preserve shared depth | Anyone adding a render layer over scene depth |
| 12 | Nonlinear shared body alpha reveals soft VFX edges | Every BODY producer: smoke, trail, particle, decal |
| 13 | A stale `tuning.cfg` A/B knob substitutes a whole CODE PATH | Anyone debugging a visual against a preset/style/variant knob |
| 14 | A float render target that is blended into / bilinearly sampled is OPTIONAL hardware | Anyone creating an R32F render target: postFX, particles, fluid, shadow |
| 15 | Sampling the scene texture while drawing INTO it (refraction/distortion taps) | Any screen-space effect that reads the scene inside a pass that binds it |
| 16 | `rlDisableColorBlend()` is FLUSH-scoped — re-enabling before the flush un-does it | Anyone wrapping a batch draw in a blend-state toggle (postFX composites, blits) |
| 17 | A shader redefining a function its `#include` exports renders as the DEFAULT shader, silently | Anyone adding a shared `#include` to an existing shader |
| 18 | Shaders hot-load from disk, C does not — a measurement after a `.inl` edit describes the OLD binary | Anyone measuring a C-side fix through the game |
| 19 | The MSAA window hint anti-aliases nothing when the scene renders into an FBO; and MSAA cannot touch a shader-decided edge | Anyone judging edge quality, adding a render target, or authoring a `step()`/`discard` silhouette |

---

## 19. Window MSAA never reaches an offscreen scene target — and MSAA fixes only ONE kind of edge (18/08/2026)

**Symptom.** Every geometric silhouette in the game arrived with binary coverage — "the colour
transition at the boundary is not smooth". On `--render-vfx 21`, column x=640: a smooth bloom ramp
on both sides of a **+64/255 luma jump across ONE pixel**. `main.c` never set
`FLAG_MSAA_4X_HINT`, and setting it would have changed nothing.

**Cause.** `FLAG_MSAA_4X_HINT` (and rlvk's `rlvkSetMsaaSamples`) configure the **swapchain**. This
engine rasterizes the entire 3D world into `ScreenDistort`'s offscreen HDR render target and only
composites a full-screen quad to the swapchain, so the hint was multisampling the one surface no
geometry is ever drawn into. Offscreen render targets are single-sampled unless the backend is
explicitly asked otherwise — under rlvk, `rlvkSetFramebufferSamples(fbId, 4)` (Vulkan only; GL 3.3
/ GLES have no equivalent here and keep FXAA).

**Rule.** Before believing any anti-aliasing setting, ask which surface the geometry actually
rasterizes into. Then, before concluding MSAA is broken or worthless, classify the edge you are
measuring — it fixes exactly one of these three:
- **Triangle silhouette on opaque geometry** — this is what MSAA is for. Measured: luma steps >50
  fell 325 → 97 across the map scene; in the ellipse region above, steps >20 fell 78 → 25.
- **Bright emissive HDR silhouette** — largely eaten by the tone curve. The resolve averages in
  linear HDR, then ACES compresses: 25% and 100% coverage landed 5 luma apart, so the +64 step
  above merely moved one pixel. Not a bug, and not fixable by more samples.
- **A feature thinner than a pixel, or an edge decided inside the fragment shader** (a 1–2 px rim
  line, a specular streak, a `step()`/`discard` cutoff) — **untouched; MSAA shades once per
  pixel.** Those need `smoothstep` over ~one pixel (`fwidth`), alpha-to-coverage, or supersampling.

**Corollary rule, learned the hard way here: never judge anti-aliasing from the single hardest
pixel or from one scan axis.** The ellipse first measured as "not one byte changed" — the
max-gradient pixel happened to sit where the boundary runs nearly horizontal, so a horizontal scan
across it can never show gradation at any sample count. A vertical scan four rows away had a clean
`7.9 → 29.1 → 29.1 → 50.2` half-coverage pair. Count hard steps over a REGION, on both axes.

Full chain, the depth-resolve constraint it hits under Vulkan, and the measured cost:
`third_party/vulkan/docs/HANDOFF.md` §7.34; guard `run_rlvk_visual_test.sh msaa_rt`.

## 18. Shaders reload from disk; C does not — and that asymmetry produces a false negative (17/08/2026)

**Symptom.** You fix something in a `.inl` or `.c`, run a headless capture to measure it, and
the numbers are unchanged. The natural reading — "the fix was wrong" — is itself wrong: the
binary never contained the fix.

**Cause.** `core/shaders/*.fs` are loaded by path at run time (e.g. `core/post_fx.c:193`), so a
shader edit takes effect on the next launch with no rebuild. That is genuinely useful, and it
teaches you that edits "just work". C sources have no such property. In a session that had just
fixed three shaders and a post-process curve with no rebuild, a one-line surface change in
`vc_energy_orb.inl` was measured immediately and read as "the fix changed nothing"; the binary
was 37 minutes older than the edit and `strings` showed the new symbol absent.

**Rule.** Before trusting any measurement of a C-side change, check the binary is newer than the
source. `scripts/render_vfx_matrix.sh` now refuses to run when anything under `core/`, `skills/`
or `sandbox/` is newer than `./build/wuxing`, and says so. Note the guard has to be written as
`find -newer`, not a pipeline: under `set -euo pipefail` an `xargs` test returning non-zero kills
the script before it can print the reason, which is a guard that fails silently — the exact
failure mode it exists to prevent.

---

## 17. A duplicate function definition makes a shader render as the DEFAULT one, silently (17/08/2026)

**Symptom.** An effect renders — as something else. No crash, no missing draw, and the
only clue is a `WARNING: RLVK: GLSL compile failed: 'fbm3' : function already has a body`
buried in the log. Worse, measurements taken through the game keep working and keep
lying: a survey of every VFX ranked `BLACK HOLE` the largest effect in the game at
**7.35%** of the frame; after the fix it measured **0.03%**. The 7.35% was the fallback
shader's output.

**Cause.** GLSL has no scoping that lets a local `float fbm3(...)` coexist with one from
an `#include`; two bodies for one name is a compile error. raylib then answers the failed
compile with the **default shader and a valid non-zero id**, so every `id != 0` guard
passes. Adding a shared include to an existing shader is exactly how this gets
introduced, and the 2026-08-16 shared-compositor migration did it to three shaders
(`black_hole_swirl.fs`, `ground_aura.fs`, `plasma_shell.fs`). The same collision was
found and fixed in `aura_shell.fs` on that same day, and nothing caught the other three.

**Rule.** Rename the LOCAL function with a file-specific prefix (`bh_fbm3`, `ga_fbm3`) —
**do not delete it.** The local copies are usually tuned differently from the shared one
(4 octaves vs 2 here), so deleting silently changes the look instead of the compile.
`scripts/validate_shader_includes.py` enforces this at CMake configure time and fails the
build; run it standalone any time you add an `#include` to a shader.

---

## 16. `rlDisableColorBlend()` only takes effect when the batch FLUSHES (17/08/2026)

**Symptom.** A full-screen composite that is explicitly wrapped in
`rlDisableColorBlend()` / `rlEnableColorBlend()` comes out blended anyway. In
`core/post_fx.c` the final tone-mapped composite was being multiplied by the HDR
scene target's **accumulated alpha** — which additive VFX push above 1.0 — so every
VFX region rendered ~1.5x too bright and clipped to white on the 8-bit swapchain.
That is the "my effects blow out to white" symptom that
`third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md` was written to chase, and it
was not in any shader.

**Cause.** `DrawTexturePro` and friends only QUEUE vertices; the draw happens when
the batch flushes (`EndShaderMode`, `EndTextureMode`, `EndDrawing`, a state change,
or an explicit `rlDrawRenderBatchActive()`). Blend state is applied at that flush, not
at call time. This is not an rlvk quirk — it is exactly how `glDisable(GL_BLEND)`
behaves under GL, so **both backends** are affected. The natural-looking

```c
rlDisableColorBlend();
DrawTexturePro(...);
rlEnableColorBlend();   // ← runs BEFORE the draw ever flushes
```

therefore restores blending in time for the very draw it was meant to protect.

**Rule.** Flush inside the window:

```c
rlDisableColorBlend();
DrawTexturePro(...);
rlDrawRenderBatchActive();   // the toggle only lands here
rlEnableColorBlend();
```

The same applies to any `rl*` state toggle wrapped around batched draws. Pinned by
`run_rlvk_visual_test.sh colorblend_flush`, which asserts both halves: flushed inside
the window the draw overwrites, re-enabled before the flush it blends.

---

## 15. A screen-space effect must not sample the target it is drawing into (11/08/2026)

**Symptom.** A refractive/distorting effect stops showing the background and
collapses to its own opaque colour — water reads as plastic, glass as a flat
shell. No error, no log line, and every tuning knob (absorption, thickness,
scattering) fails to bring the background back, because the background was never
being read.

**Cause.** The effect samples `ScreenDistort_GetSceneTexture()` inside a pass
whose bound colour attachment IS that texture. GL calls the result undefined;
Vulkan calls it a read/write hazard. This became true for `FluidSurface` the day
the split VFX layers were retired (`b03b7b6`): the body pass had always bound a
separate `vfxBodyTex`, and afterwards it bound `renderTex` itself.

**Rule.** If a pass both reads and writes the scene, snapshot the scene first and
sample the snapshot — taken at a point where the scene is still only a source. A
render-layer refactor is never local: whatever changes which target a pass binds
must be checked against every consumer that samples the scene inside it, because
the failure looks like an art problem, not a bug. Core detail + guard in
`core/docs/LANDMINES.md`. Shared utility for new effects:
`ScreenDistort_RequestSceneSnapshot()` (request while alive) +
`ScreenDistort_SnapshotScene()` (main.c, at 2D time after `MyEndMode3D`) +
`ScreenDistort_GetSceneSnapshotTexture()` (the sample-safe copy — the glass
shield refracts through it, drawn there in the dedicated
`VFX_ShieldShell_DrawRefraction` post-pass so it sees the complete scene).
Never sample `ScreenDistort_GetSceneTexture()` inside a VFX body/emission draw;
that IS `renderTex`, the bound target.

**Second trap in the same story (16/08/2026):** the snapshot itself must NOT be
taken inside a 3D pass (`MyBeginMode3D`). `ScreenDistort_SnapshotScene()` is a
`BeginTextureMode`/`EndTextureMode` blit, and raylib's `EndTextureMode()`
**hard-resets projection AND modelview to screen-space ortho without restoring
the caller's matrices** — a copy made mid-3D-pass silently corrupts every draw
after it (the whole composition pass). The shield once vanished entirely this
way ("nó tàng hình luôn rồi"): the copy looked harmless because it sat under a
blank-`rlLoadIdentity` guard that only restored the modelview stack, not the
projection.

---

## 14. R32F render targets: blending and LINEAR filtering are optional, and the absence is silent (11/08/2026)

**Symptom.** A screen-space pass that additively accumulates into a 32-bit float
target, or samples one bilinearly, is correct on desktop and produces wrong
pixels (flat, black, or unaccumulated) on another device. No error, no warning,
no validation message — the code path that "obviously works" simply does not.

**Cause.** Neither API guarantees those two features for full-precision float.
Vulkan's Mandatory Format Support tables require `COLOR_ATTACHMENT_BLEND` and
`SAMPLED_IMAGE_FILTER_LINEAR` for `VK_FORMAT_R16_SFLOAT`, and require only
`SAMPLED_IMAGE` + `COLOR_ATTACHMENT` for `VK_FORMAT_R32_SFLOAT`. The GLES story
is the same limit with a louder failure: blending while any draw buffer has
32-bit float components is `INVALID_OPERATION` without `EXT_float_blend`, and
LINEAR filtering of 32-bit float needs `OES_texture_float_linear` (ES 3.0
guarantees filterable float only at 16 bits). Desktop drivers and MoltenVK
provide all of it, so nothing in the normal dev loop can catch the assumption.

**Rule.** Pick the *narrowest* format that meets the precision need — R16F and
unorm both guarantee blend + linear everywhere, and 8-bit-packed linear depth is
filterable and blendable when precision allows. Reach for R32F only where the
precision is genuinely required, and then ask before relying on the extras: under
rlvk, `rlvkFormatSupportsBlend()` / `rlvkFormatSupportsLinearFilter()`
(`Caps.floatBlendR32` / `Caps.floatFilterR32` are detected at init and warn once).
Note that a separable screen-space filter sampling exact texel offsets needs no
linear filtering at all — `NEAREST` is both correct and portable there. Full
entry, with the repro scenario `float_blend_rt`, in
`third_party/vulkan/docs/LANDMINES.md`.

---

## 13. A persisted A/B knob that selects a VARIANT is not a tuning value — it silently runs different code (10/08/2026)

**Symptom.** An effect renders as the wrong effect. Two sessions and roughly a
dozen edits went into `trail_deform.fs` mode 2 chasing "the Fire energy trail is
a flat red band with no yellow core": the hot-core colour source was rewritten,
a geometric centreline was added independent of the sheet, the discard condition
was fixed, BODY/EMISSION were split across two passes, and the user still
reported "không được" after every one. Every change was correct. None of them
could be seen, because the shader branch being edited was running with the
**other style's** parameters, texture, tint source and blend mode.

**Cause.** `tuning.cfg` carried `strandtrail_style = 1.0`, left over from an
earlier A/B comparison. That knob does not scale a value — it **replaces the
style row** for every live strand trail: SMOKE instead of ENERGY, i.e.
`hotWhiten 0` (no hot core at all), body tint instead of glow tint, `BLEND_ALPHA`
instead of additive, and a different sheet. The code asked for ENERGY and the
file overruled it. The system did log which row won, at `LOG_INFO`, phrased as a
normal selection (`STRAND TRAIL: style -> smoketrailfx`) — indistinguishable from
a correct startup line in a wall of raylib INFO output, and it never said that
something had been *overridden*.

**Rule.** Separate the two kinds of knob and treat them differently.
- A knob that scales a value is a tuning value. The existing corollary applies:
  read the effect's `tuning.cfg` block before any visual A/B and state the
  multipliers (`core/docs/LANDMINES.md`, "Corollary on tuning.cfg").
- A knob that **selects a variant** — a style row, a preset id, a mode enum, a
  quality tier, a shader path — changes which code runs. It must announce itself
  at **`LOG_WARNING`, on change, naming the file, the forced variant AND the
  variant the caller asked for**, whenever the two differ. A line that reports
  only the winner is not a diagnostic: it reads identically whether the override
  is on or off. Pattern to copy: `StrandTrail_OnUpdate` in
  `core/composition/common/vc_strand_trail.inl`.
- Diagnostically: before editing a shader branch because an effect looks wrong,
  prove that branch is the one running — a headless render (`--render-vfx <n>`)
  plus the selection log costs one command and rules out the entire class.

---

## 11. Colour-only VFX layer clear must preserve shared depth

- **Symptom:** VFX rendered into a transparent layer appears through geometry, although it was depth-tested correctly before the layer clear.
- **Cause:** clearing a framebuffer that shares scene depth clears both attachments unless depth writes are disabled and the backend honours that mask.
- **Rule:** flush, call `rlDisableDepthMask()`, clear the colour layer, then restore the mask. The rlvk guard is `depth_mask_clear`; do not replace it with an unconditional depth clear.

---

## 10. `SetShaderValue` targets the ACTIVE shader under rlvk, not the one you passed

- **Symptom:** a uniform you set on shader A never arrives — or worse, silently
  lands on shader B and corrupts one of ITS uniforms. No error, no warning. The
  feature driven by that uniform behaves as if pinned to whatever was there.
- **Cause:** rlvk's `rlSetUniform` writes into
  `RLVK.shaderSlots[RLVK.State.activeShaderSlot]` — the currently **bound**
  shader. raylib's `SetShaderValue(shader, ...)` looks like it addresses the
  `shader` you hand it, and under desktop GL it effectively does (the location is
  program-scoped). Under rlvk the `shader` argument only supplies the *location*;
  the *destination* is whatever is active. Setting a uniform before its
  `BeginShaderMode` therefore writes it into the previous shader.
- **Rule:** every `SetShaderValue` must sit **between** `BeginShaderMode(sh)` and
  `EndShaderMode()` for that same `sh`. Never hoist uniform sets out of a shader
  scope "to avoid repeating them" — that is exactly the mistake. When a helper
  performs the `BeginShaderMode` for you, pass the values INTO the helper rather
  than setting them at the call site (worked example: `DualFilterPass`'s
  `streakCfg` parameter in `core/post_fx.c`, added for Đợt E1b — the streak
  uniforms would otherwise have landed on the bright-pass shader).
- Same family as §9: an API whose signature implies one target while the backend
  uses another.

---

## 9. `matModel` is model×view — `fragPosition` is view space, not world space

- **Symptom:** a positional effect (point light, radial falloff, distance fade) does nothing at all — not dim, *nothing* — on every surface at once, while directional sun/ambient on the same shaders looks perfectly correct. Everything checkable checks out: the uniforms reflect, the values reach the UBO, the debug marker lands on the effect.
- **Cause:** raylib's `DrawMesh` uploads `matModel = modelTransform * rlGetMatrixTransform()`, and inside a 3D pass `rlGetMatrixTransform()` **is the view matrix** — `rlPushMatrix()` in `RL_MODELVIEW` mode flips rlgl into `transformRequired` and redirects the current matrix into `RLGL.State.transform`, which is where `MyBeginMode3D`'s `rlMultMatrixf(matView)` then lands. So the near-universal line
  ```glsl
  fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));   // "world space"
  ```
  produces a **view-space** position. Compare it against anything genuinely in world space and the distance is roughly the camera distance, so every falloff clamps to zero. Directional lighting never notices, because it uses no position.
- **Rule:** never assume `matModel` gives world space. Put both operands in the *same* space and say which one in a comment. Two working precedents: `core/vfx_light.c` converts light positions with `rlGetMatrixTransform()` before upload (so lights meet the surfaces in view space), and `maps/toolkit/ground_shadow.c` folds `MatrixInvert(rlGetMatrixTransform())` into its light-VP matrix (so the shader gets back to world). Anything reading that matrix must run **inside** the 3D pass — outside it the matrix is identity and the correction silently becomes a no-op.
- **The decoy — do not repeat it:** `fract(fragPosition)` was used to "prove" the coordinates were world space, and it painted a clean 1 m grid. It always will: `fract()` of a *translated* position is an identical grid. A debug view that cannot distinguish the failure from success is worse than none (core/`CLAUDE.md` §6). Paint a quantity that depends on the ORIGIN — the distance to a known point — or assert it numerically. Full chain + regression test: `core/tests/vfx_light_space_test.c`, `core/docs/LANDMINES.md`.

### §9 also breaks fresnel, not just positional lighting (04/08/2026)

- **Symptom:** `normalize(viewPos - fragPosition)` — the near-universal fresnel/rim-light term in `plasma_shell`, `crystal`, `aura_shell`, `effect_material`, `water_splash` — looks *plausible* on screen (a bright/dark gradient that does move with the camera) but is not the fresnel it appears to be.
- **Confirmed empirically**, not just by re-reading §9's cause: `sandbox/fresnel_probe.c` (key **I** on the VFX Tester screen) draws a cylinder through the SAME mechanism these materials use — `DrawMesh` with a real `Mesh`/`Material`, not immediate-mode `rlBegin/rlVertex3f` (an earlier version of the probe used `DrawCoreCylinder`, immediate-mode, and got the *opposite*, wrong answer — see the probe's own file-header postmortem for why that draw path doesn't represent `DrawMesh`-based materials at all). Three readings, same frame, same camera:
  - `viewPos - fragPosition` (the shipped convention): a monotonic ramp across the visible face, no symmetric falloff — not a valid fresnel shape at all.
  - `-fragPosition` alone: a clean, symmetric dome — 214/255 at centre, 12–14/255 at both true silhouette edges, peak within 1% of the geometric centre.
  - `length(fragPosition)` at the centre vertex, judged against two literal reference markers (not a hand-inverted tonemap guess): read 15x closer to "distance to camera" than to "distance to world origin".
- **Conclusion:** `fragPosition` is view space here too, exactly as §9 says, and the shipped `viewPos - fragPosition` line is wrong for the same reason positional lighting was — it subtracts a world-space `viewPos` from a view-space `fragPosition`. The fix in view space needs no uniform at all: `normalize(-fragPosition)`. **Not yet applied to the 5 shaders** — this entry is the recorded evidence; the shader edits are a separate, deliberate change against shipped effects, not sandbox code.
- **Rule:** if reproducing this on a NEW immediate-mode probe/test, draw with `DrawMesh` (`GenMesh*` + `Material`), not `rlBegin/rlVertex3f` — the two code paths populate `matModel` differently and only one matches how the real shaders draw.

---

## 1. Raylib batching hazard (depth-state leak)

- **Symptom:** ground/environment drawn just before your effect renders with the wrong GL state — depth-write disabled, Z-buffer corrupted, soft particles broken.
- **Cause:** raylib's `rlgl` batches immediate-mode draws. State calls (`rlDisableDepthMask/Test`, `rlEnableDepthMask/Test`) change GL **immediately**, but vertices already queued via `rlBegin/rlEnd` are still un-flushed and get drawn under your new state. (Root-caused in `core/docs/PROGRESS.md` Item 3 / soft particles.)
- **Rule:** always flush the batch with `rlDrawRenderBatchActive()` **before and after** any depth mask/test change:
  ```c
  rlDrawRenderBatchActive();   // flush first
  rlDisableDepthMask(); rlDisableDepthTest();
  // ... your draw ...
  rlDrawRenderBatchActive();   // flush before restoring
  rlEnableDepthMask(); rlEnableDepthTest();
  ```

## 2. `rlFrustum` / camera near < 1.0 → blank render

- **Symptom:** whole scene renders blank/black after a camera or projection change.
- **Cause:** a near clip plane below `1.0` with the custom frustum path produces a broken projection under this engine's scale. (Logged in the meter-rescale work.)
- **Rule:** keep camera near clip ≥ `1.0`. If a change to clip planes / custom projection blanks the scene, suspect near-plane first.

## 3. Lit material invisible in the dark night arena

- **Symptom:** ground/geometry built with `EffectMaterial` (lit) renders black-on-black and disappears in the night arena.
- **Cause:** the lit shader has almost no light to work with in the dark scene, so lit surfaces collapse to black.
- **Rule:** for ground/large geometry in the night arena, use self-shaded vertex-color gradients instead of a lit material. Don't debug it as a "missing geometry" bug.

## 4. Mali: `fract(sin(...))` noise hash dies

- **Symptom:** on-device (Mali GPU, e.g. Samsung A33) only — an aura/effect turns into an invisible black hole or static; fine on desktop.
- **Cause:** `fract(sin(x))` hash noise loses precision at large domains on Mali. Note: **invisible ≠ shader failure** — a failed shader draws WHITE; invisible means the math degenerated.
- **Rule:** use a non-`sin` hash in `noise.glsl`. When something is invisible (not white) only on device, suspect a precision-degenerate hash before anything else.

## 5. Mali: top-84px is a mandatory OS gesture zone

- **Symptom:** finger taps on UI near the top of the screen do nothing on device, yet `adb shell input tap` at the same spot "works".
- **Cause:** the top 84px is a mandatory OS gesture zone that eats touches; `adb` bypasses the OS so it masks the bug.
- **Rule:** keep interactive UI below `y = 90`. Never trust `adb tap` to validate touch reachability.

## 6. Android: black screen / crash on device

- **Symptom (black screen):** app runs but shows only black; **Symptom (crash):** hard crash on launch.
- **Cause:** black screen = raylib 6.0 `CUSTOMIZE_BUILD` landmine (`EndDrawing` never swaps buffers). Crash = `-DGRAPHICS` is ignored on Android; ES2 instancing pointers are NULL on Mali.
- **Rule:** build Android with `-DOPENGL_VERSION="ES 3.0"` (not `-DGRAPHICS`, not ES2). After changing build flags, delete the raylib build cache. (Verified on Samsung A33 / Mali.)
- **STALE-DOC WARNING (2026-07-22):** the rule above belongs to the **GLES fallback** path. Since 2026-07-17 the Android build runs the **Vulkan backend (rlvk)** on real Mali hardware (`third_party/vulkan/docs/PROGRESS.md`, HANDOFF §7.11–7.23). Do not read this entry as "Android = GLES" — it cost one wrong conclusion already. What that means for renderer work: rlvk fixes DO reach Android, **except** anything gated on `Caps.noSampledDepth`, which is detected as *portability-subset AND vendorID 0x8086* (`rlvk_frame.inl:322`) — i.e. MoltenVK-on-Intel only, never Mali. The depth-twin machinery (§7.10/§7.27/§7.29) therefore does not exist on device.

## 7. Emitter systems: per-frame target + name collision

- **Symptom:** an emitter created with `CreateEmitter` doesn't follow its target; or edits to "EmitterSystem" affect the wrong thing.
- **Cause:** `core/emitter_system.h`'s `CreateEmitter` requires per-frame `UpdateEmitterTarget` calls. Separately, `main.c`'s `EmitterSystem_Update(dt)` is an **unrelated same-named system** in `skill_helper.c`.
- **Rule:** call `UpdateEmitterTarget` every frame for managed emitters. Don't assume the two `EmitterSystem` names are the same system — verify which one you're editing.

## 8. Per-instance uniform changes → stale-UBO scrambling (rlvk/Vulkan)

- **Symptom:** an effect renders correctly most of the time, then intermittently comes out as **small rectangles in scrambled positions or wrong colors**. Worse with several instances on screen at once, or while the camera moves; the next frame can look fine.
- **Cause:** changing a uniform between instances forces one batch flush — and one UBO snapshot — per instance. When rlvk's per-frame bump arena filled up, the UBO push was silently **skipped** and the draw inherited the *previous* push: stale `mvp` (wrong transform → scrambled quads) plus stale uniform values. Fixed in the backend (reserve-before-record, `third_party/vulkan/docs/HANDOFF.md` §7.28, guard scenario `ubo_arena`); the pattern is still the one that stresses the arena hardest.
- **Rule:** don't flush per instance when you can avoid it — carry per-instance variation in a **vertex attribute** (color/normal/UV channel) instead of a uniform, and keep uniforms to values shared by the whole batch. If you must change a uniform per instance, keep the instance count bounded. Also: never let a shader's noise domain depend on an unbounded value — wrap it with `fract()` first, or a bad frame degenerates into flat blocks (see `core/shaders/smoke_column.fs`).


## Raster-state flush also covers CULLING (27/07/2026)

§1's flush rule was written about depth state. It applies to back-face culling
just as literally: `core/ribbon_strip.c` disabled culling immediately before
`rlBegin` without a flush, so the disable never reached its own quads and any
ribbon presenting its back face — e.g. a ring lying flat on a plane via
`RIBBON_FIXED_NORMAL` — rendered NOTHING, while camera-facing ribbons looked
fine. Fixed in core; details in `core/docs/LANDMINES.md`.

## rlvk: a SECOND sampler in a shader unbinds the first (28/07/2026)

**Symptom.** Every particle draws as a flat, uniformly bright SQUARE — soft
interior gone, quad edges hard. Anything textured through the affected shader
loses its texture; a 1x1 white texel across a quad is a square.

**Cause.** `core/shaders/particle_lit.fs` gained a second `uniform sampler2D`
(`u_cameraDepthTex`, pulled in by `common/soft_particle.glsl`). rlvk's shaderc
pass rebases binding indices, and going from one sampler to two moved
`texture0`, so nothing was bound to it. The shader COMPILES cleanly — rlvk
reported "shader program compiled (26 uniforms)" — so there is no error to find.

**Rule.** Under the Vulkan backend, treat "how many samplers this shader
declares" as an interface that other things depend on. Before adding a second
sampler to a shader that has one, verify on-device; if the texture goes flat,
that is this. The mere DECLARATION is enough — the squares appear with the new
sampler's feature switched off and never read, which is the cleanest way to tell
this apart from a sampling bug.

**Diagnosis shortcut.** Set the new feature's uniform to 0 so its sampler is
never read. Still square = binding (this landmine). Square only when the feature
is on = the sampling maths.

Related, same session: a shader that FAILS to compile does not come back with
id 0 — raylib hands over the default shader and rlvk logs the GLSL error and
continues, so `shader.id != 0` answers "did something get bound", not "did mine
compile". Check that a uniform you know the shader declares resolves; see
`ParticleLighting_Begin` in `core/particle_system.c`.

## A plane-pinned ribbon dashes where it CURVES (29/07/2026)

- **Symptom:** a `RIBBON_FIXED_NORMAL` / `RIBBON_WORLD_UP` strip renders as a row
  of dashes — but only where the path bends, worse the further the camera is, and
  never in `RIBBON_CAMERA_FACING` on the same path with the same code.
- **Cause:** the width vector `side = cross(tangent, planeNormal)` rotates with
  the tangent, so the fraction of it that survives projection changes ALONG the
  strip. Measured on a real bench path (lemniscate, sandbox camera at 8.4 m):
  **1.00 at one end of the band and 0.08 at the other — 13x within a single
  strip.** The thin end is sub-pixel and dashes; the wide end is solid. A
  straight path holds the tangent constant and therefore the factor, which is why
  the artefact tracks curvature. A camera-facing strip is immune by construction:
  its side vector is built perpendicular to the view, factor 1 everywhere.
- **Rule:** fixed in `core/ribbon_strip.c` for every consumer — below
  `RIBBON_MIN_PROJECTION` (0.35, ~70 degrees of foreshortening) `side` is blended
  toward the camera-facing side vector, reaching it at true edge-on. **Rotate it,
  do not widen it.** The first attempt held the projected width by widening the
  band and paying in alpha (conserving brightness x width, which is the right law
  for a DISTANCE floor) — that converts a thin stretch into a DIM stretch, and a
  dim stretch between two bright ones still reads as a break. Measured worst
  factor over every camera angle and phase: 0.069 raw, 0.342 blended.
- **Diagnostic that cost four rounds to learn.** When one variant of an effect
  breaks and another does not, enumerate what actually DIFFERS between them and
  rule the list out one item at a time — and when a hypothesis cannot be tested
  by eye, MODEL IT NUMERICALLY rather than shipping the next plausible fix. Three
  rounds went to candidates that each looked obvious (mask frequency, sub-pixel
  geometry, mask energy distribution); twenty lines of arithmetic against the
  real path and the real camera settled it and produced the regression test
  (`Test_Foreshortening`, `core/tests/swept_trail_test.c`). The decisive fact was
  available the whole time and came from the owner: the THINNEST geometry of the
  three (a 1:40 filament) never dashed while a 1:20 blade did.

## The anti-bowtie check must run on the side vector you actually DRAW (29/07/2026)

- **Symptom:** a ribbon pinches to nothing somewhere along its length and crosses
  over itself — a dark wedge in the middle of an otherwise clean band.
- **Cause:** `DrawRibbonStripEx` keeps `side` continuous point-to-point by
  flipping it when it opposes the previous point's. That check stored `prevSide`
  **before** the foreshortening blend that runs a few lines later, so continuity
  was enforced on the RAW side vectors while the quads were built from the
  BLENDED ones — and two blended sides are free to point opposite ways even when
  their raw versions agree.
- **Rule:** record continuity as the LAST thing before the geometry is emitted.
  Anything that modifies `side` — a blend, a twist, a per-point rotation — has to
  happen before that store, and the ordering is asserted in
  `core/tests/swept_trail_test.c`.
- **Related, same symptom from the other end:** a cloth/rope solver whose
  constraint is stretch-only lets adjacent nodes collapse onto each other or pass
  THROUGH each other. A zero-length or reversed segment gives a garbage tangent,
  which flips the side vector, which is the same bowtie. Constrain from both
  sides — a floor at ~1/3 of the rest spacing lets the ribbon bunch without ever
  degenerating.

## A boolean that means "exactly" when you read it as "at least" (29/07/2026)

- **Symptom:** a simulated ribbon 6 m long rendered as a short, stiff spindle
  stuck near its emitter — it stopped following the path at all, and the flow
  animating along it had almost no length left to travel.
- **Cause:** `Ribbon_ConstrainSegment(a, b, rest, pinned, stretchOnly)` was called
  twice per segment: once with `true` for the inextensibility CEILING, then once
  with a shorter length and `false`, intended as a FLOOR against nodes collapsing
  into each other. `stretchOnly = false` does not mean "also enforce a minimum" —
  it means "force the distance to be EXACTLY restLen". The second call therefore
  overwrote the first and pinned every segment to a third of its rest spacing.
- **Rule:** when a constraint can be a ceiling, a floor, or an equality, give it
  a **named mode**, never a bool. `RIBBON_CONSTRAIN_MAX / MIN / EXACT` makes the
  wrong call unspellable; `stretchOnly = false` reads like the opposite of what
  it does. More generally: a boolean parameter whose `false` branch is not the
  negation of its `true` branch is a mis-named enum.
- **How it was found:** by pulling one frame out of the owner's screen recording
  with `qlmanage -t` and looking at it. The trail's rendered length against the
  path it was supposed to trace was obvious in a single still — after several
  rounds of reasoning had not found it.

## A ribbon pinches where the path points AT the camera — and a sign flip cannot fix it (29/07/2026)

- **Symptom:** a camera-facing strip closes to a single point somewhere along its
  length and re-opens rotated — two wedges meeting at a vertex. Once or twice per
  loop on a path that curves through the view direction.
- **Cause:** `side = cross(tangent, primaryNormal)` carries no direction when the
  tangent is parallel to `primaryNormal` — for a camera-facing strip, wherever the
  path runs straight at or away from the viewer. `ComputeSideVector` then fell
  back to an unrelated reference vector, so `side` did not flip, it **jumped to a
  different direction entirely**.
- **Rule:** the anti-bowtie continuity check only ever NEGATES `side`, which
  cannot undo a 90-degree jump — do not reach for it here. Detect the degenerate
  cross product by its LENGTH and **parallel-transport** the previous side vector
  instead: re-orthogonalise it against the new tangent. The strip then narrows
  through the degenerate stretch, which is correct (it is being seen end-on),
  rather than tearing. Fixed in `core/ribbon_strip.c` for every consumer.
- **Diagnostic:** a pinch that recurs at the same points of a repeating path is
  geometric degeneracy, not shading and not the mask. Ask what the cross product
  is doing where the path aligns with the reference direction.

### Postscript, 30/07/2026 — do not over-generalise the above

This entry was later cited as "shaders cannot have two samplers" and used to
block a design. That is not what it says, and the tree contradicts the general
form: `core/shaders/surface_lit.fs` declares **four** samplers and ships
(`core/surface_material.c`), `decal_flow.fs` declares two and ships.

The mechanism is narrower: adding a *second* sampler to a shader that had *one*
made shaderc rebase the binding indices, which moved `texture0` out from under
the implicit binding raylib sets up for it. A shader authored with several
samplers from the start, binding each explicitly, never depends on that implicit
slot. **Re-read the mechanism before letting a landmine veto a design, and check
whether something shipping already disproves the general form.**

### The batch-flush rule applies to BACKFACE CULLING too (30/07/2026)

The rule above is written for depth mask/test. It is the same for
`rlDisableBackfaceCulling` / `rlEnableBackfaceCulling`, and forgetting it there
produces a shape that looks deliberate:

**Symptom.** A swept TUBE rendered as half a shell — the owner described it three
times, most precisely as "a pipe split lengthwise, one half rotated 180 degrees
and stacked on the other". Two other causes were found and fixed first (a ribbon
sheet wrapped around a tube leaves a transparent seam; the fix was applied to one
layer and not the other), and it still looked the same.

**Cause.** `rlDisableBackfaceCulling(); DrawTube(); rlEnableBackfaceCulling();`
The tube's vertices go into the rlgl batch and are drawn LATER — after culling
has been switched back on — so exactly one wall of every ring survived.

**Rule.** `rlDrawRenderBatchActive()` before AND after **any** rlgl state change
that the queued geometry depends on: depth mask, depth test, blend mode, and
culling. If the state matters at draw time, it must be flushed at change time.

**And the diagnostic worth keeping:** the same visible symptom had THREE
independent causes here, and fixing each one in turn produced no change, which
read each time as "the fix did not work". When a symptom survives a correct fix,
consider that the pipeline has more than one way to produce it rather than that
the fix was wrong.

## BLEND_ALPHA is NOT premultiplied — one colour cannot serve both VFX passes

**Symptom.** A VFX looks right over the night arena and washes out to nothing
over a bright sky, a lit floor, or a light-coloured wall. Tuning its colour,
alpha or HDR gain moves the brightness but never restores the contrast.

**Cause.** The body/emission split (`ScreenDistort_BeginVFXBody()` /
`ScreenDistort_BeginVFXEmission()`) exists precisely so an effect keeps its hue
over a bright destination: the BODY pass occludes, the EMISSION pass glows. But
raylib's `BLEND_ALPHA` is `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` —
**not** premultiplied — so the hardware multiplies RGB by alpha itself. A shader
that emits one already-intensity-scaled colour for both passes gets it scaled a
SECOND time in the body pass, which collapses to ~intensity² and contributes
almost no coverage. The effect then exists only in the additive pass, and
additive over a bright destination can only add: it cannot darken, and it cannot
create contrast. `trail_deform.fs` shipped with a header comment asserting that
"both passes consume src.rgb * src.a" — true of additive, false of alpha, and
the assertion is what kept the bug invisible.

**Rule.** A shader used by both passes must know which one it is in, and emit
different things:

- BODY (`BLEND_ALPHA`): **unpremultiplied** colour + a coverage alpha.
- EMISSION (`BLEND_ADDITIVE`): straight colour × HDR gain, with intensity in
  source alpha so the blend unit applies it exactly once.

Route every mode of a multi-mode shader through ONE resolver function (see
`ResolvePass` in `core/trails/shaders/trail_deform.fs`) so the two passes cannot
drift apart per mode, and give the effect an authored `bodyOpacity`: 0 = pure
emissive (correct only for sparks and glints, which may legitimately vanish over
bright ground), ~0.5 = a coloured core inside a glow, ~0.9 = smoke, which must
occlude. An effect with no body-pass contribution is not "tuned for a dark
scene"; it is invisible in a bright one.

## A render pass that nothing calls is dead code — check the call site before you split work across passes

**Symptom.** A VFX goes pale over dark AND bright backgrounds after a change
that was supposed to *improve* its bright-background legibility. Every uniform
arrives, the maths is right, the shader compiles.

**Cause.** The body/emission split is an API, not a guarantee that both halves
run. At the time of this failure `main.c` called `DrawTrailEntitiesBody()` but
not `DrawTrailEntitiesEmission()`. Moving gain into emission therefore moved it
into dead code. The later attempt to keep both jobs in BODY combined with a
global alpha expansion and produced solid red energy bands plus hard smoke
edges. As of 10/08/2026 both trail passes run: BODY owns coverage, EMISSION owns
HDR.

**Rule.** Before dividing output, confirm both pass functions are called. Do not
silently compensate for a dead pass by mixing semantic jobs: wire the missing
pass or explicitly document a truly one-pass subsystem. Body opacity remains
coverage; it is never a substitute for emission strength.

## 12. A global nonlinear body-alpha curve reveals every producer's soft edge

**Symptom.** Smoke trail, smoke column, particles and decals simultaneously
develop visible silhouettes; energy ribbons become solid material bands. Local
edge-soft/dissolve knobs appear ineffective.

**Cause.** The shared compositor changed stored body alpha with
`1-(1-a)^6`. An authored edge at `a=0.10` became `0.47` after every producer had
already finished shaping it. No local mask can compensate reliably because the
same post-curve is applied to unrelated material classes.

**Rule.** Composite VFXBody with its stored alpha linearly. Structure/edge masks
belong to each producer or material profile. Use BODY/EMISSION separation to
retain hue on bright backgrounds; never seek background invariance by globally
inflating coverage.

## `fract()` for float precision: fold each product ONCE, never nest

**Symptom.** A layered texture effect loses its structure — distinct features
turn into fine uniform mush — after adding `fract()` folds for numerical
precision.

**Cause.** `fract(fract(x) * k) != fract(x * k)`. Folding a coordinate and then
scaling it changes the effective TILING RATE of every layer that reuses the
folded value, and chops each layer into short runs that no longer line up with
the others.

**Rule.** Keep the unfolded value as a shared base and fold each consumer's own
final product exactly once (`fract(base * 1.6)`, not `fract(fract(base) * 1.6)`).
Fold the sources that actually grow without bound — accumulated clocks and
cumulative distances — as close to their origin as possible, on the C side where
you can pick the modulus deliberately. Folding is exact for a sine (period 2pi)
and for a REPEAT-wrapped sampler; it is not exact for anything you then scale.

## An in-shader `fract()` cannot fix a stutter — only folding at the ORIGIN can

**Symptom.** An animation driven by `u_time * speed` visibly steps rather than
moving smoothly after a long session. Wrapping the argument with `fract()` in the
shader does not help, which then reads as "the fold didn't work" and invites a
second, wronger fix.

**Cause.** Two different problems wear the same fix. Folding a product that was
*already computed* at full magnitude improves the accuracy of `sin`'s argument
reduction — real, and measurable against a double-precision reference — but it
cannot recover bits the product has already lost. For frame-to-frame resolution
the phase step is `speed*dt` and the quantum at the argument is
`(t*speed)*2^-23`, so **the speed cancels out of their ratio**: no downstream
fold changes it, and only the magnitude of `t` itself matters.

**Rule.** Fold the unbounded source where it is *born*, on the C side, where the
modulus is a deliberate choice — `SurfaceFlow_PackGPU` folds every pan before
upload, `trail_system.c` folds cumulative arc length with `fmodf(..., 8192)`. Use
an in-shader fold (`UVDeform_SinePhase`, `UVDeform_FoldAngle`) for argument
accuracy, and never reach for one to fix a stutter. Also: an aperiodic consumer
must not be folded at all — folding the time term of an `fbm` domain makes the
whole field jump once per cycle (see the two adjacent, deliberately different
lines in `core/shaders/aura_shell.fs`). Proof and both measurements:
`core/tests/uv_deform_test.c` `Test_FoldingASineIsExact`.

## A ForceField with no VISCOSITY layer accelerates forever

**Symptom.** A VFX driven by a force field drifts off the map. It often looks
like three separate bugs: it flies away, then it "stops", then its head stretches
into a long thin spike. Nothing errors, and the spawn log looks healthy.

**Cause.** `ForceField_GetViscosityDamping` (`core/force_field.c`) multiplies a
factor only for `FORCE_VISCOSITY` layers and returns **1.0** for a field that
declares none. Node integration is `v = (v + a*dt) * viscDamp`, so with no
viscosity layer nothing damps anything and `v = a*t` without bound. A modest
2.2 m/s^2 updraft puts a node 10 m out by t=3s and 27 m by t=5s — past the
18 m arena radius. The "stop" and the "stretch" are the same event seen twice:
the history ring fills, the oldest node is recycled, and the geometry is then
strung between a stationary emitter and a departing front.

**Rule.** Any force field whose consumer lives longer than a second or two must
declare a `FORCE_VISCOSITY` layer. Pick it from the terminal speed you actually
want — `terminal = strength_of_wind / strength_of_viscosity` — and LOG that
derived number rather than the two inputs: "rise 2.2 m/s2" does not say whether
the effect stays on screen, and "terminal 1.30 m/s" does. Worked example and the
arithmetic: `core/composition/common/vc_smoke_column.inl`,
`core/tests/smoke_column_test.c`.

**Related, same file:** a parameter that is logged but never reaches the
geometry is worse than an absent one. `height` was printed on every spawn while
node spacing was a constant, so the column was capped near 1 m whatever the
caller asked for, and the log actively argued against the symptom.

## A setter that early-returns on a precondition you did not set

**Symptom.** A composition built entirely around one API call renders as a flat
sliver floating in space. The call is right there in the source, it compiled, it
executed, and the spawn log looks healthy.

**Cause.** The setter validated a precondition and returned in silence.
`Trail_SetStaticPath` opens with `if (t->type != TRAIL_TYPE_FOLLOWER) return;`,
and `TrailConfig cfg = {0}` means `TRAIL_TYPE_PROJECTILE` — zero is a valid enum
member, not an "unset" marker. So the path was never seeded, the trail laid
coincident nodes at one point, the tangent between two identical points is
garbage, and the tube's cross-section collapsed to a line: a plane, drawn where
a volume should be. No NaN, no warning, nothing in the log.

It survived a rewrite because the FIRST version happened to call
`Trail_AttachToTransform`, which sets the type as a side effect. Removing the
attach removed the only thing that made the later call legal.

**Rule.** Two parts, and the second is the one that generalises:

1. A `= {0}` config gets its enums set EXPLICITLY when zero is a real member.
   Grep the setter you are about to call for its early returns first.
2. **Log the EFFECT, not the inputs.** A spawn line listing radius, height and
   speed cannot distinguish a correct tube from a collapsed one — every input
   was fine in all three failed builds of this effect. Read the state back after
   the call (`GetTrail(id)->historyCount`) and warn when it disagrees with what
   was asked. Worked example: `core/composition/common/vc_smoke_column.inl`,
   which now prints `nodes N` and raises a WARNING naming the likely cause when
   `N` is wrong.

Same family as the missing-shader trap below: a precondition failure that
degrades into a plausible-looking wrong result instead of an absent one.

## A missing shader file does not report as a shader problem

**Symptom.** Edits to a `.vs`/`.fs` appear to do nothing at all. The effect still
renders — as its older, simpler self — and no error or warning stands out.

**Cause.** Every runtime-loaded shader must be `configure_file`'d into the build
tree by `CMakeLists.txt`. When one is missing, `ResourceManager_LoadShader`
returns id 0, and the consumer's capability check (e.g.
`TrailUsesDeformShader()`, which requires `s_deformShader.id != 0`) quietly
returns false. The subsystem then takes its fallback path and keeps drawing.
Nothing crashes, nothing is blank — which is why this reads as "my change had no
effect" rather than as a missing asset. Whether it bites at all depends on the
working directory the binary is launched from, so it can also work on one
machine and not another.

**Rule.** When you add a shader, add its `configure_file` line in the same
change. When an edit to a shader seems to do nothing, check that the file is
copied and that the load succeeded BEFORE re-reading the maths — and make every
capability fallback log why it opted out, on change rather than once at startup
(`core/CLAUDE.md` §4).

## Writing an include directive INSIDE A COMMENT still includes the file (05/08/2026)

**Symptom.** A `.vs`/`.fs` that compiled fine suddenly fails with a confusing
GLSL-compiler error — "Missing entry point" (no `void main()` found) or
`'#endif' : mismatched statements` — right after an edit that, read as C or
any real preprocessor would read it, changed nothing executable: a comment
was added, or an existing `#include` line was removed and replaced with prose
*describing* what used to be there.

**Cause.** `core/shader_preprocessor.c`'s `ProcessIncludes` resolves
`#include` because raylib's GLSL loader has no `#include` support of its own —
but it finds its target with a bare `strstr(cursor, "#include")` over the RAW
file text, with **zero comment-awareness** and **no per-path deduplication**.
It does not run a real preprocessor pass first; it does not know `//` or
`/* */` exist. Writing the eight-character token `#include` followed later in
the file by a quoted string — even inside a `//` line explaining *why not* to
include something, even as a "don't write this" example — makes the loader
splice that file's full text in for real, exactly as if the directive were
live code. If the named file is already included elsewhere (directly, or
transitively through another include), its body now appears twice in the
flattened output. GLSL's own `#ifndef` guards are supposed to make a second
textual copy of a header a no-op — and often do — but this project has now
observed two DIFFERENT ways the specific combination broke a real rlvk/
shaderc compile instead: a `void main()` that stops resolving, and a
`#ifndef`/`#endif` pair that comes out unbalanced. Do not assume the guards
save you; the real bug is that the include happened in the first place.

**Rule.** Never write the literal token `#include` in a shader comment,
including to name a file you are deliberately NOT including, even inside
backticks or quotes. Describe it in prose instead ("pulls in the uv_field
module", not `` `#include "core/uv/shaders/uv_field.glsl"` ``). This applies
to every `.vs`/`.fs`/`.glsl` in the tree — the preprocessor runs on all of
them identically, comment or not. Separately: before pulling a bundling
header (like `core/uv/shaders/uv_field.glsl`) into a file that already
includes pieces of what it bundles directly, check whether it re-includes
those same pieces — if so, declare only the specific uniforms/functions you
are missing instead of the whole bundle (see
`core/trails/shaders/trail_deform.fs`'s `u_uvField`/`u_uvMeta` declarations
for a worked example). No compiler was available in the session that hit
this twice — a `TraceLog` printing the actual uniform LOCATION after load
(not just whether the shader "loaded") is what surfaced it; see
`trail_volume.fs`'s load-site log for the pattern to copy.

## `rlPushMatrix()` does NOT hand back an identity matrix (12/08/2026)

**Symptom.** Three fluid blobs registered at provably correct world positions
(printed straight out of the capture: `(4.98, 0.16, 4.40)`, `(6.07, …)`,
`(7.16, …)`, camera looking at `(6.00, 0.20, 4.40)`) rasterized at the very top
and very bottom EDGES of the screen instead of in a row across the middle. Their
on-screen SIZE was right, so it was not a scale problem — only their placement.

**Cause.** In both rlgl and rlvk, `rlPushMatrix()` on the MODELVIEW stack does
two things: it redirects subsequent matrix writes to the persistent global
`State.transform`, and it saves whatever that global **already held**. Nothing
ever clears it. The invariant people assume — "pop restores the pre-push value,
so it recursively returns to identity" — only holds while pushes are strictly
LIFO, and PROJECTION and MODELVIEW pushes share ONE stack and ONE counter. Any
interleaving breaks it. Measured at the fluid capture, `State.transform` held a
leftover VIEW matrix (translation `-13.25` on Z = the camera distance), so
`rlPushMatrix(); rlTranslatef(...); DrawSphereEx(...)` drew every sphere
view-transformed **twice**.

**Fix.** State the identity instead of assuming it:

```c
rlPushMatrix();
rlLoadIdentity();          /* <- the transform global is NOT clean */
rlTranslatef(x, y, z);
```

**Rule.** Any immediate-mode 3D draw that builds its own transform with
`rlPushMatrix` + `rlTranslatef`/`rlScalef`/`rlRotatef` must `rlLoadIdentity()`
first. Most call sites get away without it only because whoever last used the
transform stack happened to leave identity behind — that is luck, not a
guarantee. Guarded by `core/tests/fluid_capture_projection_test.c`.

## A screen-space pass must rasterize through the SAME frustum it later inverts (12/08/2026)

**Symptom.** A fluid body that captured correctly (its depth and thickness
targets both showed clean, correctly-placed geometry) rendered **nothing at all**
in the composite. Every pixel was discarded. The few debug views that returned
before the discards showed the body; every view after them was empty.

**Cause.** `FluidSurface_Capture` rasterized through raylib's `BeginMode3D`,
whose projection is built from `RL_CULL_DISTANCE_NEAR = 0.01`, while the
composite reconstructs view positions by inverting a frustum with **near = 1.0**
(matching `main.c::MyBeginMode3D`, which uses 1.0 because this project's
`rlFrustum` renders blank below it). Device depth is wildly nonlinear in the near
plane: a body 7.5 m away writes `0.99868` under near=0.01, and inverting that
under near=1.0 places it at **428 m**. It then failed the composite's
scene-occlusion test (`sceneDistance - fluidDistance <= -0.002`) everywhere.

Only paths that let the RASTERIZER produce depth (`gl_FragCoord.z`) were
affected. The GPU splat paths compute depth from their own near=1.0 projection
and write `gl_FragDepth`, so they were correct throughout — which is why the
defect survived: the only affected path had no test fixture.

**Rule.** Any pass that writes depth for a later screen-space decode must build
its projection from the same constants the decoder inverts — never from
`BeginMode3D`, whose near plane is raylib's, not yours. And note the second half:
a rendering path with no fixture is a path where this class of bug is invisible.
Guarded by `core/tests/fluid_capture_projection_test.c`.

## A gate whose input is refreshed BEHIND the gate latches shut (12/08/2026)

**Symptom.** A newly-added SSF cost gate deleted the water ring outright — not
intermittently, permanently, from the first frame onward.

**Cause.** The gate rejected work when the previous frame ran over budget, and it
read that frame time from a variable updated inside `FluidSurface_Composite`. But
`main.c` only calls `Composite` when something was actually submitted. One slow
start-up frame closed the gate; with the gate closed nothing was submitted;
`Composite` was therefore never called; the frame time was never updated; the
gate could never reopen.

**Fix.** Read live (`GetFrameTime()`) and express "is it running" as an ageing
wall-clock **stamp** rather than a per-frame flag, so the state decays on its own
whether or not the function that sets it is ever reached.

**Rule.** When adding a gate, ask what refreshes the state it reads and whether
that refresh happens on the rejected path too. If it does not, the gate is a
one-way latch. Guarded by `core/tests/fluid_cost_gate_test.c`.

## A gate that measures a cost its own decision controls will STROBE (12/08/2026)

**Symptom.** The water surface flickered on and off continuously, every frame,
in normal play. Reported by the user; the headless fixture never showed it,
because a tester rendering one fixture is too cheap to reach the threshold.

**Cause.** The SSF cost gate refused new bodies when the previous frame ran over
a budget. But whether the surface ran IS most of what the frame costs. Logged at
a forced threshold, the mechanism is unmistakable:

```
GATE 184 frame=17.79ms -> admit      # cheap frame, let the water in
GATE 185 frame=18.62ms -> REJECT     # water made it expensive
GATE 186 frame=25.13ms -> REJECT
GATE 190 frame=16.50ms -> admit      # no water, frame got cheap again
GATE 192 frame=25.74ms -> REJECT
```

Admitted, the ring costs 23–27 ms; rejected, 16–17 ms. The threshold sat between
the two, so the loop flipped at frame rate. **No threshold value fixes this** —
any number between the two costs oscillates, and a number outside them makes the
gate either useless or permanent. Hysteresis and smoothing only change the
oscillation's period; a 0.5 s strobe is still a strobe.

**Fix.** Break the loop instead of tuning it: judge a body's affordability ONCE,
when it starts, and exempt a running body from the budget test. Gate inputs that
are *not* self-referential (here, projected screen size — camera distance does
not depend on whether SSF ran) can keep being evaluated every frame; they only
need ordinary hysteresis.

**Rule.** Before adding a load-shedding gate, ask whether the quantity it
measures is one the gate's own decision changes. If it is, the gate is a
feedback oscillator, and the only stable designs are decide-once-per-body or
measuring something the decision cannot move. Guarded by
`core/tests/fluid_cost_gate_test.c` (the anti-strobe block).

## A fixed timestep that only pins the LOOP does not pin the SIMULATION (14/08/2026)

**Symptom.** `--render-vfx` produced a different image every run at identical
settings. Sweeping `bloom_intensity` 0.12 → 0.60 gave a NON-MONOTONIC series —
0.60 dimmer than 0.40 — and four consecutive captures at one fixed setting
differed more from each other than the whole swept range did. The sweep read as
a plausible, interpretable result. It was noise.

**Cause.** `main.c` pins `dt = 1/60` on the headless paths (`--render-vfx`,
`--autotest`, `--visual-verify`) so a capture is reproducible — but the pin only
ever covered `main.c`'s own local variable. Roughly a dozen VFX systems called
raylib's `GetFrameTime()` directly (`sandbox/vfx_test.c`, which drives EVERY
tester fixture, plus ~10 accumulators under `core/composition/`). Headless also
skips `SetTargetFPS(60)`, so `GetFrameTime()` there is free-running wall clock:
frame cost varies run to run — a cold vs warm rlvk pipeline cache alone is
enough — and integrates into a different animation phase by capture time.

**The trap inside the trap.** A fixture can look perfectly deterministic because
it has ALREADY FINISHED. LIGHTNING IMPACT gave byte-identical captures at
`--warmup 30` and divergent ones at `--warmup 8`, where it still had content.
The first result was the determinism of an empty frame, and it briefly "proved"
a working instrument that did not exist.

**Fix.** One publisher, one accessor: `TimeFX_SetRawDelta()` in `main.c` next to
the pin, `TimeFX_RawDelta()` everywhere that advances VFX state. Deliberately
NOT hitstop-scaled — a determinism fix must not also change what hitstop does.

**Rule.** A fixed-timestep harness is only as good as its narrowest reader. When
you pin time, pin it at a source every simulator reads, and treat a direct
`GetFrameTime()` in simulation code as a bug. Wall clock stays correct for perf
counters and frame-budget gates — pinning THOSE makes them measure a constant
and report a healthy frame forever, so both directions are guarded by
`core/tests/frame_delta_determinism_test.c`.

**Before trusting any headless A/B:** capture the same fixture twice at the
warmup you intend to measure at and compare checksums. An instrument that has
not been shown to repeat cannot support a conclusion in either direction —
including a null result. Related: `core/docs/LANDMINES.md`, and the standing
rule to check `tuning.cfg` for persisted overrides before any visual comparison.

## Blitting a SUB-RECTANGLE between render textures needs the mirrored destination (18/08/2026)

**Symptom.** `ScreenDistort`'s soft-depth snapshot reported a scene 0.35–1.5 m
further away than it was, but only where the copied region was smaller than the
frame. Downstream the ShieldShell's ground contact could not draw a band on the
far wall: at the exact pixel where the depth TEST had already cut the geometry
away, the depth TEXTURE still claimed a metre of clearance. The near wall's band
looked plausible, so the whole path read as working-but-badly-tuned, and three
sessions of shader tuning went at the wrong layer.

**Cause.** The blit is `DrawTexturePro(depth, {x, y, w, -h}, {x/D, y/D, w/D, h/D})`.
The negative source height is the standard RT→RT convention: it makes raylib
read the block bottom-to-top, which converts FBO storage order back into screen
order. But it mirrors WITHIN THE BLOCK, so the block must also be written at its
MIRRORED position for the composition to come out as the identity every sampler
assumes when it reads with `gl_FragCoord.xy / u_resolution`. The correct
destination top edge is `(H - y - h) / D`, not `y / D`; the two agree only when
`y = 0, h = H`. The displacement is exactly `H - 2y - h` screen rows.

**Why it hid for so long.** A full-frame region is its own mirror. The snapshot
had only ever been armed full-frame (`ScreenDistort_RequestSceneSnapshot` does
exactly that), so the one case that was exercised was the one case the formula
got right. The first partial region to reach it — a shell's bounding box —
produced a smoothly wrong depth map: monotone, plausible, no seam, no flip, and
therefore nothing that looks like a bug in a screenshot.

**Rule.** A negative source height flips the block, not the image. Any partial
RT→RT copy that uses it must place the block at the mirrored destination, and
the only honest check is a round-trip: compose the blit's row mapping with the
sampler's row mapping and assert the identity. Guarded by
`core/tests/soft_depth_region_test.c`, which also asserts the pre-fix formula
FAILS that round-trip — a guard that cannot fail on the old code is not a guard.

**And the diagnostic that found it:** swap the partial region for a full-frame
one. If the picture changes, the bug is in the region plumbing and not in
anything the consumer computes from what it is handed.

## A term that decides HUE at a silhouette must ride a quantity that SATURATES there (18/08/2026)

**Symptom.** A rim that had been smooth came back blotchy: along the silhouette the colour
swung pixel to pixel between white and a deep saturated hue, with no pattern to it.
Measured as the standard deviation of peak luminance sampled every 2° around the arc, 10.6
against 3.9 before.

**Cause.** The white-hot core was keyed on `(1 - |N.V|)^8`. Near a silhouette `|N.V| -> 0`
is exactly where the rasterised edge is uncertain at the sub-pixel level — the interpolated
normal, the facet the fragment landed on and the coverage of that pixel all disagree
slightly — and an eighth power AMPLIFIES that disagreement. Feeding it into a
`mix(hue, white, ...)` turns a sub-pixel wobble into a visible hue swing.

The form it replaced was stable for one reason that is easy to miss: `wallDensity` is built
on `1 / max(|N.V|, 0.10)`, and that **clamp** makes it saturate a few pixels BEFORE the
silhouette. Across the uncertain band it is simply pinned, so the white core has nothing to
chase.

**Why this is worth an entry rather than a fix note.** The change that caused it was the
appealing one: "both rims should be built the same way, each taking its white core from its
own band's peak." Symmetry between two terms is not a reason to move one of them onto a
noisier variable, and the noise only exists in the last few pixels, which is precisely where
a rim lives and precisely where a still frame at 1:1 does not show it.

**Four other explanations were measured and rejected first**, and they are the ones anyone
will reach for: mesh tessellation (40 -> 96 slices changed nothing, even though the band had
become as narrow as the polygon's sagitta — a coincidence that looked like a smoking gun);
an additive noise term; a matcap; and drawing both faces so the two silhouettes overlap
(about a fifth of the effect, no more). Also rejected: "the old version only looked smooth
because it was CLIPPED" — the old shader at a non-clipping strength still scored 3.9.

**Rule.** Any term that selects COLOUR (as opposed to intensity) as a function of view angle
must be driven by a quantity that is clamped or saturating at grazing angles. Raising an
exponent to narrow a band is fine for the band's own falloff; it is not fine for anything
that decides hue. And when a silhouette artifact appears, measure the ANGULAR spread of the
rim, not a single scanline — one scanline through a wobble reports a value, not a variance.

**Guard.** `core/tests/shield_shell_test.c` asserts the white core rides `wallDensity`, that
the path-length clamp is what makes it saturate, and that the `rimBand` form does not return.

## Colour RINGS on a bright ramp are a PLATEAU, not a palette (18/08/2026)

**Symptom.** ShieldShell's ground line read as two or three concentric bands of
different colour with visible boundaries, instead of one soft gradient. Nothing
in the authored colour ramp has a boundary in it.

**Cause, in two parts that only bite together.**
1. The brightest channel is **pinned**. Across the band `R` sat at 1.0 for 13
   consecutive pixels, so there is no luminance gradient left there — the only
   quantity that can still vary across those pixels is HUE.
2. §12.1's hue-preserving highlight restoration blends per-channel ACES against a
   hue-keeping curve with weight `hueRestore * smoothstep(1,2,peak) * (1 - smoothstep(5,9,peak))`.
   That weight RISES AND FALLS along an intensity ramp. Where the top channel is
   pinned, the other channels therefore go down and then back up: measured
   `G = 185 → 170 → 231` across the band. A non-monotone channel on a monotone
   ramp is a ring, by construction.

Measured with the knob: at `postfx_hue_restore = 0` G rises monotonically
197 → 255 and the band is a clean gradient; at 0.5 it dips; at 1.0 it dips
harder. The tone map is behaving exactly as designed — it just has nothing left
to work with once a channel is pinned.

**Fix — at the plateau, not at the tone map.** The band came from
`1.0 - smoothstep(0, thickness, gap)`, and **smoothstep has zero derivative at
its lower edge**, so the profile holds ~1.0 for the first ~15% of its width. A
cubic `(1 - gap/thickness)^3` falls away immediately: flat top 13 px → 5 px,
clipped pixels 10924 → 7029, and the dip is gone. Turning hue restoration down
would have "fixed" it too — for every effect in the game, to buy back one
effect's authoring mistake.

**Rule.** Any falloff whose brightest end can pin a channel must be PEAKED, not
plateaued. `1 - smoothstep(a, b, x)` is a plateau: reach for it for a soft
*edge*, never for the *hot core* of an additive term. And when you see colour
banding on a smooth ramp, measure the channels along a scanline before touching
any colour: rings mean one channel stopped moving, and the cure is upstream of
the tone map. Brightness is the wrong lever — halving the strengths here moved
the clipped area 10924 → 6940 and kept the dip exactly where it was.

**Guard.** `core/tests/shield_shell_test.c` asserts the contact profile has no
flat top, and asserts the contrast against the old plateau form (0.729 vs 0.972
at one tenth of the band) so the check is shown to discriminate.

## Compositing a QUARTER-RES buffer with one bilinear tap reconstructs as blocks (18/08/2026)

**Symptom.** Once the bloom pyramid was folding properly again, every bright
curved silhouette grew visible stair-steps — square patches roughly 4 screen
pixels across, hugging the halo. Reported as "hạt hạt pixel".

**Cause.** `bloomTex` is a quarter-resolution target, and the composite read it
with a single `texture(u_bloomTex, uv)`. A bilinear fetch magnifying 4x
reconstructs the signal as piecewise-linear patches with a KINK at every source
texel boundary — the gradient is discontinuous on a 4-pixel grid, and the eye
finds that instantly along a high-contrast curve. Bilinear is an interpolation
filter, not a reconstruction filter; it is fine for magnifying something already
band-limited and wrong for anything carrying detail at its own Nyquist.

**Why it appeared only now.** While `bloom_scatter` was pinned at 1.0 the
pyramid collapsed to its coarsest mip, so `bloomTex` held nothing but smooth low
frequencies — there was no detail to alias. Restoring the near halo put real
quarter-res detail in that buffer and the reconstruction filter's inadequacy
became visible the same frame. **A latent quality bug can be revealed by fixing
something else; that does not make the fix wrong.**

**Fix.** Run the same 3x3 tent the upsample chain already uses
(`bloom_upsample.fs`) in the final composite, with the bloom target's texel size
passed as a uniform and computed from the LIVE texture, so a resize or a quality
tier that changes bloom resolution cannot desync it. Eight extra taps in one
fullscreen pass, no new render targets. Energy is unchanged — the kernel is
normalised: FLAME VOLUME moved 1 pixel by more than 4/255, and the SHIELD SHELL
matrix shifted by ~1 point on every plate.

**Rule.** The final upsample is part of the pyramid, not a free `+=`. Any buffer
composited back at a different resolution than it was rendered needs a
reconstruction filter that matches the one used to build it.

**And the measurement lesson, which cost a wrong "it's fine" here:** a single
scanline is not a proof of smoothness, and a scanline placed where the artifact
ISN'T proves nothing at all. The first check ran 18 rows OUTSIDE the rim, through
the smooth far halo, came back monotone, and was reported as "smooth at 1:1" —
while the stair-steps were on the bright edge a few pixels away. Sample where the
artifact is, and when the eye and a metric disagree, the metric is on trial.

## "Bounded change" and "no colour banding" are the SAME knob (18/08/2026)

**Symptom.** A bright rim reads as a rainbow — distinct colour patches with
visible boundaries — where the authored colour is one hue at rising intensity.
Blamed on the effect's shader three times before it was isolated.

**Isolate it with the simplest possible gradient.** Replace the effect's emission
with a pure linear ramp of ONE colour, `rimColor * t * 12`, turn bloom off, and
read the channels along a scanline. Everything else — geometry, profiles, term
stacking — is gone, so whatever structure survives belongs to the pipeline. Here,
on a strictly rising input, G came out `169 → 151 → …` : a REVERSAL, then a flat
plateau, then a fast sweep, then a frozen hue. Three patches, from a clean ramp.

**Cause.** §12.1's hue restoration blends per-channel ACES against a hue-keeping
curve with weight `w = hueRestore · smoothstep(1,2,peak) · (1 - smoothstep(5,9,peak))`.
Hue keeping means *lowering* the non-peak channels (that is what restores chroma).
`w` rises and then falls, so along a rising ramp the non-peak channels are pulled
down and then released: a trough with two edges. Measured G reversals on the pure
ramp: 0 at `hueRestore = 0`, 1 at 0.5, 5 at 1.0.

**The part that makes it structural — and it is the LOWER bound, not the upper one.**
That correction cost two sessions. The obvious reading is that the weight ramping back
OUT above peak 5 is what releases the channels, so the upper bound is the culprit. It is
not. Any weight that transitions from zero to non-zero **while the input is still
climbing** pulls the non-peak channels down and produces the trough; whether it later
ramps out is irrelevant. Proved by search over the whole family (18/08/2026):

* widening the rise `smoothstep(1,2)` → `(1,7)` makes it **worse**, 15 → 30 reversals —
  the drop is `~max(w · (perChannel − hueKept))` and does not care how gently `w` got
  there;
* bolting a bottom gate back onto the monotone candidate restores the banding **exactly**
  (0 reversals → 59), while the candidate without it has none.

So "bit-identical below `peak` 1" and "monotone through the shoulder" are the two
properties that cannot coexist. `tonemap_shoulder` used to require the first.

**RESOLVED 18/08/2026: the monotone alternative is now the shipping curve.** Constant
weight, whitening moved out of the weight and into a monotone desaturation of the
hue-kept colour (`core/shaders/post_process.fs`, `toneMapScene`). Measured on the
ShieldShell across the five-background matrix, chroma is UP on **every** plate —
dark 0.301 → 0.316, mid 0.230 → 0.255, white 0.316 → 0.345, warm 0.351 → 0.410,
cool 0.138 → 0.212 — and the gradient probe's one-hue band goes from
`+26 → +9 → -10 → +9` to a clean `+17 +18 +18 +13 +9`, 0 reversals.

**The price, stated in full**, because the handoff's headline number (d = 0.03 at
peak 0.2) is the FIRST failing sample, not the worst. The real shape:

| surface below the shoulder | shift at `u_hueRestore` 1.0 | at the shipping 0.5 |
|---|---|---|
| achromatic grey | **0.000, exactly, at every level** | 0.000 |
| mildly warm (1, .85, .7) | 0.141 | 0.071 |
| saturated (1, .35, .08) @ peak 0.98 | 0.206 (analytic max 0.212 as peak → 1) | 0.103 |

The grey row is why this is affordable: hue keeping is `(x/peak) · f(peak)`, which for an
achromatic input IS the per-channel result, so the change **cannot** touch a neutral
surface. It is confined to saturated content by construction. On a real capture the
whole-scene cost is 0.758 % of the frame moving more than 2/255 (mean 1.03/255) against a
0.043 % A/A noise floor — `scripts/run_tonemap_ab.sh --verify FIRE`.

`tonemap_shoulder` was **rewritten**, not disabled: it now asserts the achromatic row is
bit-identical, the saturated shift stays under a stated ceiling, chroma still improves in
the shoulder, and — the property the trade actually bought — **no channel goes backwards
across a dense rising ramp**. That last check was confirmed RED on the pre-fix shader
first ("channel 1 goes BACKWARDS: peak 1.32 → 1.45 drops 0.0332"). Suite: 28/28.

**Confirmed with NO effect present at all (18/08/2026), and there is now a tool for
it.** The isolation above still replaced an effect's emission, so "the shell" was
still in the frame. `sandbox/gradient_probe.c` (+ `core/shaders/probe_gradient.fs`)
removes even that: a RECTANGLE whose colour is an analytic function of x, drawn into
the HDR scene target so it takes the whole chain. Press **G** in the VFX tester, or
`WUXING_GRADIENT_PROBE=1` for a headless run; it prints per-channel numbers and writes
`autotest_output/gradient_probe.png`. Five bands, and the last two are what make it
decisive:

| band | content | result |
|---|---|---|
| 1 | ONE hue, rising level (`rimColor * level`) | G slope `+26 → +9 → -10 → +9`: dips and recovers |
| 3 | same hues at a CONSTANT level | perfectly smooth, `dG` constant |
| 4 | one flat colour, no ramp | flat — proves vignette/chroma are not the cause |
| — | same ramp on plain per-channel ACES, drawn AFTER post | 0 reversals, monotone |

Band 3 is the discriminator: the hues are innocent, the LEVEL dependence is the whole
mechanism. Two measurement traps this probe had to fix first, both of which produced a
wrong answer on its first run: **measure the middle of the screen**, because chromatic
aberration and the vignette are radial and scored worse than any real banding; and
**slope, not sign** — at the shipping strength the dip is spread over enough pixels that
no single-pixel step is negative, so a reversal count alone reports zero.

**Dose–response on the shipping knob** (band 1, minimum G slope over the shoulder):

| `postfx_hue_restore` | 0.0 | 0.15 | 0.25 | 0.35 | 0.5 (this machine) |
|---|---|---|---|---|---|
| min dG | +6 | +10 | +4 | **-1** | **-10** |
| worst G drop | 0 | 0 | 0 | 2 | 12 |

The channel first goes BACKWARDS between 0.25 and 0.35. That is a live, no-rebuild knob:
0.25 removes the reversal, 0.0 removes the plateau too — at the chroma cost §12.1 exists
to avoid.

**Rule.** Before attributing colour banding to an effect, run the one-hue ramp
through the real pipeline — `gradient_probe` does it in one keypress and needs no
effect at all. And when a tone-map bump is gated as "bit-identical below the shoulder",
know that you have just bought a non-monotone channel response on every smooth ramp:
turning the correction ON across a rising input is itself the band. If you need both,
the bound you give up is the LOWER one, and the thing that makes that affordable is
proving the change cannot move an achromatic surface.

## Selecting BLEND_ALPHA_PREMULTIPLY does not make a source premultiplied (20/08/2026)

**Symptom.** An effect is switched from additive to premultiplied to make it cut a
silhouette out of a bright background. It comes back *brighter* instead: `cover%` grows,
soft edges bloom out, and `darken%` — the number the change was made for — drops to
**0.0 on every background**, including the ones where it used to be non-zero. On the trail
presets, TRAIL BACKDROP went 98.3% → 0.0% on white, i.e. exactly backwards.

**Cause.** The three blends the engine uses are not three flavours of the same law:

| the blend | GL function | who multiplies RGB by alpha |
|---|---|---|
| `BLEND_ALPHA` | `(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` | the hardware |
| `BLEND_ADDITIVE` | `(SRC_ALPHA, ONE)` | the hardware |
| `BLEND_ALPHA_PREMULTIPLY` | `(ONE, ONE_MINUS_SRC_ALPHA)` | **nobody — you do** |

Every producer in this engine authors STRAIGHT colour with coverage in alpha, because the
other two blends apply the alpha for it. Hand that source to the premultiplied blend and
every fragment is scaled by `1/alpha`: opaque cores are unchanged, and the soft edge — the
majority of a VFX's area — is multiplied by up to 255. The added light then swamps whatever
body pass was providing the coverage, which is why the metric moves the wrong way.

**The tell that separates this from a genuine blend-law result.** Look at the DARK
background. There `dst ~ 0.02`, so `src + dst*(1-a)` and `src + dst` are the same picture —
the premultiplied law itself can barely change anything. Measured, `cover%` rose **~4x on
dark**. A large change on dark is proof the change was not the blend law.

**Rule. The blend state and the source's own output formula are ONE decision, never two.**
Whatever selects the blend must also tell the source which law it is under:

- **shader producer** — carry it in a uniform (`u_renderPass` in `trail_deform.fs` grew a
  third value; `shock_ring.fs` got `u_premultiply`) and pick the resolver:
  `VFX_ResolvePremultiplied` for premultiplied, `VFX_ResolveBody` for alpha,
  `VFX_ResolveEmission` for additive (`core/shaders/common/vfx_composite.glsl`).
- **fixed-function producer** (immediate-mode ribbon, raw `rlBegin` quad) — there are
  **three** halves, not two: the blend, the vertex tint (`VC_Premultiply`, next to
  `VC_WithAlpha` in `core/presets/vc_material.h`), **and the sheet**. The fixed-function
  path multiplies vertex colour by the texel, so a white-RGB alpha mask scales A without
  scaling RGB and hands the blend a straight source across the whole gradient. A generated
  mask must be written `(a, a, a, a)`, never `(255, 255, 255, a)`.
- **particle producer** — one field, `.render.blendMode = VFX_BLEND_PREMULTIPLIED`. This is
  the only case where the swap really is one line, because `particle_system.c` premultiplies
  for you. Do not generalise from it.

**Two corollaries worth knowing before the next migration:**

1. **A MULTI-PASS ADDITIVE STACK DOES NOT SURVIVE THE SWAP.** Additive passes SUM;
   premultiplied passes OCCLUDE EACH OTHER. `VFX_ComposeSweepSlash` draws its band three
   times sharing one outer edge — migrated, its white footprint over threshold halved
   (1501 px → 639) while the peak delta barely moved, and it was reverted.
   `VFX_ComposeLightShaft` is two passes and paid the same tax in the same direction (dark
   `|d|` −10%, against the 2–7% every single-draw migration cost).
2. **ALPHA OVER A STRAIGHT SOURCE ALREADY IS THE PREMULTIPLIED LAW.**
   `(SRC_ALPHA, 1-SRC_ALPHA)` over `col` and `(ONE, 1-SRC_ALPHA)` over `col*a` are both
   `col*a + dst*(1-a)`. Swapping an already-alpha effect (ENERGY ORB) is a no-op. What
   premultiplied buys is emission ABOVE coverage — an authoring decision, not a migration.

**And one rule the framework used to enforce and no longer does (20/08/2026).**
`VFXRender_BeginAppearance` forced EMISSION to additive for every named appearance, exactly to
stop corollary 1 from happening across a body/emission pair. That blanket was removed after
measuring — SHIELD SHELL's white `structure` tripled (0.035 → 0.105) and its white body area
went 0.26% → 1.12% without it — because the predicate was wrong: what matters is not "is this
the emission pass" but "does this emission cover the same area the body already covered", and
only the effect knows that. **So: an appearance whose EMISSION is a second FULL-COVERAGE copy
of its BODY must not declare a premultiplied surface.** Nothing will catch it for you; the
symptom is corollary 1's — the effect gets SMALLER, not more present.

Measured before/after for all of it: `BRIGHT_BACKGROUND_VFX_SPEC.md` §7.6d, "Second
migration, 20/08/2026".
