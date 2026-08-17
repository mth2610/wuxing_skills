# rlvk — Progress / Backlog

## ShieldShell ground contact: an analytic fallback was tried and REVERTED (2026-08-17)

With the depth path blocked, the shell's contact band was reimplemented analytically: a shell
knows its centre and the ground plane's height, so where it crosses `y = 0` is closed-form and
needs no depth texture. It drew the intersection ellipse on both walls and looked right in the
one fixture it was built against.

**Reverted — the model was wrong, not the tuning.** "Contact" means *touching something*; the
analytic version meant *crossing the plane y = 0*. Those coincide only on flat ground at that
exact height, so a shell floating in mid-air still drew a full contact band, and it would draw
one over a pit or a slope too. It also fed `u_contactColor` into `glow` strongly enough to swamp
the white silhouette rim, which measured back at RGB(255,250,212) once reverted.

The lesson worth keeping: routing around a broken dependency with a geometric approximation
requires checking that the approximation's assumptions match the FEATURE'S MEANING, not just its
appearance in the fixture in front of you. Depth contact stays the only correct mechanism, and
unblocking it is an rlvk depth-twin task.

## ShieldShell ground contact is dead — three causes found, two fixed (2026-08-17)

`depthContact()` in `glass_shell.fs` returns 0 for every fragment, so toggling
`shield_shell_depth_enabled` changes **0.000% of pixels**. Instrumented rather than guessed:
a probe shader wrote the sampled values out and read them back — `hasDepth=1`,
`u_depthEnabled=1`, fragment axial depth ~11.9 units, **sampled scene depth exactly 0**.

Fixed:
1. It compared `sceneDepth` against `length(fragPosition)`. `depth_copy.fs` stores the standard
   perspective linearisation — distance along the VIEW AXIS — while `length()` is RADIAL, which
   off-axis is always the larger. `gap` went negative across most of the shell. Now `-fragPosition.z`.
2. The shield sampled `ScreenDistort_GetDepthTexture()` without ever calling
   `ScreenDistort_RequestSoftDepthRegion()`. `ScreenDistort_SnapshotDepth()` is IDLE-GATED and
   starts idle, so with no registered consumer the snapshot never runs. A perf gate had silently
   switched a feature off — the general lesson for any idle-gated shared resource.

NOT fixed: with both of those corrected the depth texture still reads 0. The remaining suspect is
rlvk's `Caps.noSampledDepth` twin (`HANDOFF.md` §7.10 / the twin keep-alive window): the R32F
shadow copy that serves `renderTex.depth` may not be refilled on the path
`ScreenDistort_SnapshotDepth` reads it from, or the keep-alive gate closes it. Next step is an
rlvk-side repro that binds an FBO depth twin and reads it back, not more shield-side work. This
may be MoltenVK-specific and want re-checking on Mali.

Until it lands the shell draws no ground contact at all, deliberately — the previous
`smoothstep(-normal.y)` stand-in was a band in normal space, which projects to a hard elliptical
seam with no relationship to where the shell meets anything.

## Particle + decal surface contract (2026-08-17)

Extending the surface/resolver audit past the composition layer. Both systems pick blend from
DATA rather than at a call site, so the static validator cannot see them; the contract is now
asserted in `core/tests/vfx_appearance_test.c` (verified to fail on the pre-fix shape).

- **Particles** derive BOTH the shader branch and the blend from one `VFXResolvedAppearance`:
  `unlit` selects `VFX_ResolveEmission` vs `VFX_ResolveBody`, `surface` selects the blend. The
  guard that keeps straight RGB out of a premultiplied blend law was keyed on
  `appearance == VFX_APPEARANCE_FIRE`, written when FIRE was the only premultiplied entry. MAGIC
  became premultiplied in the 2026-08-16 migration and did not inherit it. **Latent, not live** -
  no particle spawns MAGIC today (it is used only by ShieldShell geometry) - so generalising the
  condition to `surface == VFX_SURFACE_PREMULTIPLIED` changes no current behaviour and closes the
  trap for the next author.
- **Decals** are self-consistent: the pass structure drives both branch and blend (body groups
  ALPHA/MULTIPLIED, emissive group ADDITIVE). But they RESOLVED `appearance.surface` and silently
  discarded it, which is how a caller comes to believe it asked for something. Now warns once.
- New assertion covering both: for every appearance, `unlit == false` implies
  `surface == VFX_SURFACE_ALPHA`, because the lit branch returns straight RGB.

## Foundation closeout (2026-08-17)

Three foundation items closed; gate 4 of the tone map remains the owner's.

- **Surface/resolver contract is now enforced at configure time.** `ResolveBody` must be drawn
  ALPHA, `ResolvePremultiplied` PREMULTIPLIED, `ResolveEmission` ADDITIVE - break it and the
  coverage term is silently discarded, which is invisible on the night arena and fatal in
  daylight. `scripts/validate_vfx_surface_contract.py` (wired into CMakeLists) found ENERGY ORB
  and BLACK HOLE; both fixed. BLACK HOLE is the sharper case: an effect named for ABSORBING light
  was bound so it could only ever add it. Coverage is honest about its reach - 17 composition draw
  sites, 2 bind a `VFX_Resolve*` shader (both checked), 15 draw immediate-mode through the ribbon
  primitive with no shader of their own, so the contract does not apply. Particle/trail/decal
  systems pick blend state internally and are NOT covered; that is remaining work.
- **The spec was teaching numbers its own metrics reject.** §6.1 said fire coverage 0.35-0.70 and
  §6.2 said magic 0.20-0.55, while §8.2 needs ~0.68 at the body sample to clear white at EV2.
  Corrected to 0.60-0.80 and 0.55-0.80 with the measurement quoted. Any effect authored against
  the old ranges is under-covered by construction.
- **The RGBA8 fallback has an oracle** (`bright_vfx_ldr`): the whole §8 chart through an 8-bit
  scene target. §7.1's claim holds - source readability passes on coverage alone. Finding: bloom
  is INERT on that path below exposure 1.25, because an RGBA8 scene clips at 1.0 and nothing can
  cross a 1.25 exposed threshold.
  **CORRECTION to the first version of this entry: this is NOT "the mobile path".**
  `ScreenDistort_Init` takes RGBA8 only on `WUXING_NO_HDR` or a failed RGBA16F framebuffer, and
  RGBA16F colour-attachment support is MANDATORY in Vulkan 1.1 - so every conformant Vulkan
  device, Android included, gets HDR. The scenario guards the `WUXING_NO_HDR` override and
  GL/GLES builds. Which backend the shipping Android build uses cannot be checked from here.

## ENERGY ORB fixed on measurement (2026-08-17)

Body area on a white background went **0.07% -> 9.89%**, parity with its 10.08% on dark, and
`darken%` 0.0% -> 97.9%. Two causes, measured separately: the draw bound `VFX_SURFACE_ADDITIVE`
while its shader returns `VFX_ResolveBody` (contract violation - coverage was being discarded),
and `aura_shell.fs` applies a bottom-to-top alpha fade because it was written for a CYLINDER,
which on a sphere deletes the upper half's coverage. The second was the dominant one, worth 8x
alone. Both fixes are exact identities at their defaults so every other consumer of that shader is
bit-identical; `orb_cover_floor` is a live tuning knob.

Metric lesson: `structure` (CV of luminance) counts a smooth gradient the same as fine texture, so
removing the wrong fade LOOKED like a 3x structure regression. Added `detail` (luminance minus a
blurred copy) which sees only short-scale contrast - by it the cost is 0.069 -> 0.060. Do not read
`structure` alone after changing anything with a large-scale gradient.

## Real-effect bright-background harness (2026-08-17)

`scripts/render_vfx_matrix.sh <idx> [frames...]` renders one VFX fixture across the §8.1
backgrounds at IDENTICAL camera/frame/resolution (via the existing `WUXING_VFX_BG`, which also
skips map+skybox), plus a background plate per colour, and `analyze_vfx_matrix.py` reports
footprint / darkening / internal structure / chroma. Spec §11b.

First result, VOLUME TRAIL (35): coverage and silhouette PASS (darkens 99.6% of its footprint on
white - it is a real body), but **internal structure collapses 4x** dark->white (0.327 -> 0.085;
absolute luminance std 40.9 -> 16.5 over the same 56k pixels, so not a metric artifact) and chroma
halves. Cause is structural: for an alpha body, internal contrast is proportional to |C - B|, so
filament and gap converge as the background approaches the effect's own brightness. Fix is §5.5's
split - emission is the one term that does not scale with |C - B|.

The three are drawn by three DIFFERENT paths (trail tube / particle system / primitive score), so
this is the law, not a shader bug - there is no single file to fix, it is per-effect authoring.
CORRECTION to an earlier note here: "fix it in trail_volume.fs and measure on SMOKE COLUMN" was
wrong twice over. Smoke is a dark body, so |C - B| is LARGEST against bright scenery - smoke reads
better in daylight and loses structure against black, the mirror of fire. Never generalise this
table to a dark-bodied effect.

Extended to FLAME VOLUME (38) and PROJECTILE (20). Structure collapses on ALL THREE (4x / 8x /
10x dark->white) - it is the most general bright-background defect in the project and coverage does
not fix it. The ordering is by how much of each effect is additive rather than body: VOLUME TRAIL
keeps its silhouette exactly, FLAME VOLUME loses 56% of its body area on white, PROJECTILE loses
79% and darkens only 28.7% of its footprint. Harness is now in the working protocol of root,
`core/`, `skills/` and `third_party/vulkan/` CLAUDE.md - `core/CLAUDE.md`'s "Core has no automated
visual tier" entry is replaced by it for this class of check.

`VFX_ComposeProjectile` was DELETED (17/08/2026) on the strength of its row — no gameplay consumer,
worst scores on every axis. `sync_vfx_test.py` propagated the removal from deleting the one `.inl`;
only the hand-written declarations in `visual_composer.h` needed removing by hand (the generated
block is below them). NEWFX indices shifted as a result, which is why tooling and docs now cite
fixtures by NAME (`scripts/vfx_fixture_index.py`). Note the defect did NOT go with it, though not because of a shared
shader (see the correction above) - it is the law and it survives in every bright-bodied effect. Also: PROJECTILE was the only fixture reaching
exposed peak 9, so nothing now exercises the §12.1 candidate's ramp-out.

THIRD probe mistake, caught the same way: `darken%` was measured per channel, and the colour
grade's saturation stage is not per-channel monotonic - a provably pure-additive effect measured
"97.9% darkened" on the cool background. It is measured on LUMA now. Corrected figures: VOLUME
TRAIL still darkens 99.2% on white (a real body), FLAME VOLUME 57.2% (less body-driven than first
reported), ENERGY ORB 0.0-0.8% everywhere (purely additive, confirmed in code).

Two earlier probe mistakes worth remembering, both caught before they were reported: comparing screenshots
at different apparent SIZE confounds background with scale, and deriving the background reference
from a radial median of the frame is broken by the background's own bloom (reported 39% footprint
where the truth is 6%).

## Hue-preserving tone map SHIPPED at 0.6 (2026-08-17)

Owner chose 0.6 from a blind A/B after gates 0-3. `core/post_fx.c`'s `s_hueRestore` now defaults
to `0.6f`; `postfx_hue_restore = 0` restores the old per-channel ACES curve bit-identically. The
shoulder-view diagnostic was promoted to its own knob (`postfx_shoulder_view`) rather than deleted,
because §11b gate 3 expires the moment a brighter map or auto-exposure lands.

**Gate 5 remains OUTSTANDING**: Mali frame cost and `mediump` behaviour of the `x / peak` rescale
have not been measured. This ships desktop-verified and Android-unverified.

## Hue-preserving tone map — gates 0-1 PASS, gate 2 is the human's (2026-08-17)

Candidate lives behind `postfx_hue_restore` in tuning.cfg, **default 0 = shipping curve**. It is a
bump over the ACES fit, not a replacement, so the approval surface is the highlight band only.

- **Gate 0** (`tonemap_shoulder`): `d = 0.00000` below the shoulder and above it — bounded, proven
  through the shipping shader, not asserted.
- **Gate 1** (`BRIGHT_HUEFIX=<0..1> bright_vfx`): full chart PASSES at 0.35/0.6/1.0. Mean chroma
  gain `+0.074 / +0.127 / +0.211`; worst `rgbDistance` change anywhere `-0.038 / -0.021 / -0.035`.
- **Gate 2** (`scripts/run_tonemap_ab.sh --vfx 0 <strength>`): **PASS.** A/A floor 0.000% (byte
  identical); A/B changes **0.494-0.495%** of the frame at every strength, max delta 35/61/103 of
  255 at 0.35/0.6/1.0. The difference map is black everywhere except the emissive beam - ground,
  sky, portal, stars and fog are untouched. The changed pixel COUNT is strength-independent (it is
  set by which pixels are above the shoulder); only the magnitude scales. This is what turns a
  tone-mapper change into an emissive-only approval.
- **Gate 3** (shoulder view, `postfx_hue_restore = -1`): **PASS for the current art direction.**
  A single capture paints the candidate's active band, so the `--verify` nondeterminism is
  irrelevant. Whole-scene captures (character + ground + sky + fog + stars, FIRE and TUBE, 5 timed
  shots each) put **0.0000%** of the frame in the band - one single pixel, once. The material
  regression list is empty: the candidate is the exact identity on every non-emissive surface.
  Cross-validated: the shoulder view reads 0.4964% of `--render-vfx 0` in band, the gate-2 A/B read
  0.495% changed on the same fixture. NOTE this gate EXPIRES - every map is night-time and exposure
  is pinned at 1.00; a daylight arena or auto-exposure pushes ground and sky over 1.0 and regrows
  the approval surface. Also: only FIRE and TUBE are compiled in (`core/skills_config.h`), and
  nothing reached the `>=9` band so the ramp-out is unexercised.
- **Capture-path limitation found by the A/A floor:** `WUXING_VERIFY=<skill>` (whole scene) is NOT
  reproducible - two identical runs differ on 0.05-0.17% of pixels, and the >2 / >8 / >32 buckets
  are all the same percentage, so the differing pixels are wholly different rather than shifted
  (sparse-particle or frame-phase leak). Its A/A floor exceeded the A/B signal, so it cannot carry
  a pixel A/B until fixed. Worth a separate look by whoever owns `sandbox/visual_verify.c`.

Two caveats that decide this, both in `BRIGHT_BACKGROUND_VFX_SPEC.md` §12.1: strength 1.0 makes
cores markedly darker (non-peak channels drop hard) so expect to ship nearer 0.35-0.6; and the
candidate breaks per-channel monotonicity, so §5.7's darkening budget stops proving "this has
coverage" and would need re-deriving in scene-linear space.

## Bright-background VFX oracle — rebuilt as a real chart (2026-08-17)

`bright_vfx` v1 was green for the wrong reason: it tone mapped through a Reinhard probe
(`x/(1+x)`) while the game runs the ACES fit in `post_process.fs`, it drew a flat constant-colour
quad so none of the §5.4 core/body/halo metrics could be measured at all, it implemented 1 of
§8.2's 8 metrics, and it used a Euclidean `rgbDistance` where the spec defines max-abs-channel.

It now loads the SHIPPING `post_process.fs` and the three `bloom_*.fs` instead of
re-implementing them (deliberately non-hermetic: a Core change to the post chain can fail this
scenario — that is what an acceptance oracle is for), draws a fixture with the real §5.4 pixel
hierarchy, and runs six element hues x five backgrounds x three exposures x bloom off/on, plus a
blend-law check at BOTH draw sites. **PASS**; suite 25/25 normal and under validation.

Building it produced findings, not just a test — all written up in
[`BRIGHT_BACKGROUND_VFX_SPEC.md`](BRIGHT_BACKGROUND_VFX_SPEC.md) §5.5–5.7, §7.2, §7.3b, §12:

- **A game bug, in the postFX composite, not in any shader.** `rlDisableColorBlend()` is
  flush-scoped, so `core/post_fx.c`'s `disable / draw / enable` wrapper handed the draw back to
  the blender. The final composite was therefore multiplied by the HDR scene target's accumulated
  alpha — additive VFX push it above 1.0 — so every VFX region rendered ~1.5x too bright and
  clipped to white on the 8-bit swapchain. Fixed at both sites; pinned by the new
  `colorblend_flush` scenario; promoted to `ENGINE_LANDMINES.md` #16.
- **Coverage + a white core is not enough** — the 3–9 px corona needs its own saturated radiance,
  or a translucent body over a complementary background mixes toward neutral (measured
  `chroma 0.05`).
- **The halo must fade out inside the body radius and carry the body hue, not the emission hue**
  (measured `rgbDistance 0.06` vs `0.21` on the same fixture).
- **Per-channel ACES is the binding constraint at EV2 on bright ground**, and §6.1/§6.2's
  coverage ranges are below what §8.2 needs. Both are open items in §12.

Also landed: output dither in `post_process.fs` (sin-free hash, Mali) and NaN/Inf containment in
`core/shaders/common/vfx_composite.glsl`. Core suites 68/73 — the same five baseline failures.

## Bright-background VFX oracle (2026-08-16)

`rlvk_format.inl` now reports byte sizes for R16/R16G16B16/RGBA16F readback. The visual suite
contains a `bright_vfx` scenario that renders premultiplied and additive equations over dark,
white, warm, and cool backgrounds into RGBA16F, checks HDR values before tone mapping, and
measures post-tone-map contrast. The scenario is present but its first run is pending the local
Vulkan-Headers cache (the bootstrap cannot resolve GitHub in the current environment).

The local Vulkan SDK and visual raylib cache are present. With those cache paths wired locally,
`check_rlvk_compile.sh` passes (`rlvk.h compile check: OK`). The headless/runtime harness reaches
MoltenVK but reports `VK_ERROR_INCOMPATIBLE_DRIVER` because this agent environment has no Metal
device; its capability failures are therefore environmental, not a backend regression.

With an escalated GPU-capable process, the headless runtime now passes all checks, and the full
visual suite passes `24/24` both normally and with `VALIDATE=1`. Validation prints two existing
MoltenVK portability warnings for zero-stride instanced probes, but no scenario fails.

The shared bloom prefilter now receives the configured exposure: threshold and soft-knee weight
are evaluated in camera space, while raw scene-linear HDR is written into the pyramid and exposed
exactly once by `post_process.fs`. Core bloom contracts cover the binding and ordering.

> Status + remaining work for the rlvk Vulkan 1.1 backend. Full narrative and per-item evidence in [`HANDOFF.md`](HANDOFF.md) §8; the debugging log is §7 (indexed in `LANDMINES.md`).

## Perf measurement — HOW, and two traps that produce confident nonsense (2026-07-22)

`UNCAPPED=1 ./scripts/run_rlvk_visual_test.sh perf_<name>` (IMMEDIATE present; under FIFO every
number is just a vsync bucket, and the game additionally caps with `SetTargetFPS(60)` — build it
with `-DWUXING_PERF_CAPTURE=ON` and read the **ms** HUD, never FPS).

**Trap 1 — never time variant A as one block and variant B as the next.** Presentation throttling
changes phase during a run: the same `perf_dynmesh` work measured **10 ms in whichever block ran
first and 1.8 ms in whichever ran second**. Swapping the order swapped the numbers. Any A/B taken
as consecutive blocks on this platform is worthless.

**Trap 2 — do not alternate variants with `f & 1`.** `RLVK_FRAME_INDEX_COUNT` is **2**, so
alternating pins each variant to its own frame-ring slot. That bias reported "1 upload costs
4.2 ms, 2 uploads cost 1.8 ms" — an impossibility that looked like a real measurement.

**The method that works**: interleave the variants within one run and pick each frame's variant
from an LCG, so neither presentation phase nor the frame ring can correlate with the variant. Two
independent runs then agree to ~0.3 ms. `sc_perf_dynmesh` is the worked example — copy its shape.

### Numbers that were taken correctly
- **A dynamic mesh re-upload costs ~0.5–0.65 ms per `UpdateMeshBuffer` CALL** (1681-vertex mesh,
  40 KB per buffer; two calls = 1.0–1.3 ms/mesh/frame). 40 KB cannot cost that — the cost is
  `rlvkUploadBuffer` tearing down and rebuilding the render pass **per call**
  (`rlDisableFramebuffer` + `vkCmdEndRenderPass` + copy + barrier + resume). It is per-CALL, not
  per-vertex. Anything doing raylib's `UpdateModelAnimation` (CPU skinning + full vertex re-upload,
  2–3 buffers) pays this per character per frame. **Open optimization**: coalesce consecutive
  uploads into a single pass split (lazy resume) — and, game-side, GPU skinning removes it entirely.
- **The §7.29 twin-fix delta**: `perf_rt2048` 13.4 → 8.9 ms, corroborated in-game (33 → 21 ms).

- **Extra full-resolution offscreen passes are free.** `perf_fullres_ab` (1 vs 3 × 1280×720
  RGBA16F, LCG-interleaved): −0.80 and +0.28 ms per extra pass across two runs. Collapsing the
  postFX/distort chain is not worth doing.
- **An upload inside an FBO scope costs no more than one on the swapchain.** `perf_upload_fbo`:
  −0.39 / +1.08 ms across two runs, i.e. noise. The theory that every `rlvkUploadBuffer` suspend
  re-runs the depth-twin bounce for the active render target is **tested and rejected** — do not
  re-chase it.
- **The step that IS large: rendering into a 1280×720 offscreen target at all.** Drawing the same
  mesh to the swapchain is ~0.9 ms/frame (`perf_dynmesh` draw-only); the moment the scene goes
  through a full-res target the frame is ~9 ms, reproduced across `perf_fullres*`,
  `perf_hdr_main` and `perf_upload_fbo` in separate sessions. Since *extra* passes are free, this
  is fill/resolution-bound, not pass-count bound — the lever is internal resolution (and the 2048²
  shadow capture, which is 4.5× the scene's pixel count).

### The shadow system: no single hotspot (2026-07-22, user-confirmed `J` off ⇒ >60 FPS)
In-game the whole shadow subsystem is worth ~5 ms (20 ms → under 16.6 ms when toggled off). Probing
its parts found **no dominant one** — it is the sum:

| part | A/B probe | cost |
|---|---|---|
| capture map 2048² vs 1024² | `perf_shadow_ab` | **0.6 – 1.1 ms** |
| ground receiver 16 vs 4 PCF taps @1280×720 | `perf_pcf_ab` | **0.16 – 0.89 ms** (full-screen; the ground covers less) |
| the rest (~3 ms) | not isolable in the harness | the **second full scene traversal** the capture pass costs — draw calls, binds, vertex work, all doubled |

So the lever is not resolution and not the filter: it is **how often the scene is re-drawn into the
map**. Capturing on alternate frames halves the whole capture cost, CPU draw calls included.
NOTE: `perf_shadow_ab`'s first version drew one small cube and reported resolution as free — a
capture probe must make the casters FILL the map, as the fitted light frustum does in-game.

### Session outcome (2026-07-22, in-game, Intel Iris 6000)
33 ms → 21 ms (§7.29 twin elision) → 17 ms (half-rate shadow capture + 8 PCF taps) → **16 ms /
62 FPS** (twin keep-alive window + idle-gated `ScreenDistort_SnapshotDepth`).

**Half-rate shadow capture was REVERTED (same day).** It bought ~1.5 ms but produced visible
shimmer on moving casters (30 Hz shadow vs 60 Hz motion, worse wherever the frame rate is lower)
*and* the alternating frame cost below. Temporal tricks on the shadow update rate are not free
here; take the milliseconds from resolution (spatial) instead. Confirmed on device (Mali-G68).

**Watch frame PACING, not just the average.** Half-rate shadow capture makes frame cost alternate
heavy/light — a 16 ms average with a 19 ms worst is capture frames at ~19 and skipped frames at
~14. Under vsync every heavy frame misses the 16.6 ms deadline, which can feel like judder even
though the average clears 60. If that shows up, prefer an evenly-distributed cost (e.g. a 1024²
capture EVERY frame) over an alternating one at the same average.

### Numbers that are NOT trustworthy — retake them with the LCG method before citing
`perf_base`, `perf_switch*`, `perf_rt256`, `perf_fullres*`, `perf_hdr_*`, `perf_ldr_bloom` were all
taken one-config-per-process (trap 1). They suggested that scope switches, extra full-res passes,
the bloom pyramid and the HDR format were all free — **the dynmesh result contradicts at least the
"pass splits are free" part of that**, since the upload cost IS a pass split. Treat those as
unmeasured.

## Format capability query added — R32F blend/filter are no longer assumed (2026-08-11)

rlvk called `vkGetPhysicalDeviceFormatProperties` **nowhere**: `rlvkGetVkTextureFormat`
mapped `RL_PIXELFORMAT_UNCOMPRESSED_R32` to `VK_FORMAT_R32_SFLOAT` and every caller took
the remaining features on faith. Per the spec's Mandatory Format Support tables that faith
is misplaced for exactly the two features screen-space passes lean on: `R32_SFLOAT`
guarantees `SAMPLED_IMAGE` + `COLOR_ATTACHMENT` but **not** `COLOR_ATTACHMENT_BLEND` and
**not** `SAMPLED_IMAGE_FILTER_LINEAR`; `R16_SFLOAT` guarantees both.

Added: `rlvkFormatSupportsColorAttachment/Blend/LinearFilter(int rlFormat)` (public,
`rlvk_format.inl`), `Caps.floatBlendR32` / `Caps.floatFilterR32` cached at init with a
one-time warning, 6 runtime-test checks, and visual scenario `float_blend_rt` asserting the
cap matches observed behaviour (three additive writes must read back 3.0).

Measured on MoltenVK/Metal here: `R32F blend=1 linearFilter=1`, additive x3 → 3.000. So the
desktop path is genuinely fine — this is instrumentation for the driver that is not, and
the caller-side decision (which format a pass should use) still belongs to the caller.
Known consumer that relies on both today: `core/fluid/fluid_surface.c` (four R32F targets,
additive thickness pass, BILINEAR filter) — Core's call.

## Compute dispatch costs ~0.6-0.9 ms per CALL — and it is none of the obvious three (2026-08-11)

core/fluid's PBD solver measured **4.4 ms in-game** for 2,048 particles across 9
dispatches — 72 workgroups of real work. `perf_dispatch_count` (visual suite,
LCG-interleaved) reproduces the shape with a kernel that does nothing but add a
float: **1 dispatch 1.316 ms, 9 dispatches 6.240 ms, i.e. 0.615 ms per extra
dispatch.**

Three hypotheses were tested and **all three are wrong**, so do not re-chase them:

| suspect | test | result |
|---|---|---|
| one-shot submit + `vkQueueWaitIdle` (out-of-frame path) | 9 dispatches in-frame vs outside | 0.02 ms/call difference |
| render-pass split per dispatch (in-frame path) | same measurement, other direction | same |
| the full `CmdPipelineBarrier2` after every dispatch | `RLVK_EXP_NO_COMPUTE_BARRIER=1` (gate since removed) | within run-to-run noise (±0.5 ms) |

**The descriptor snapshot is ~0.20 ms of it — measured.** `RLVK_EXP_REUSE_COMPUTE_SET=1`
(gate since removed) re-bound the previous set instead of allocating and
rewriting one, reuse scoped to within a frame because the compute pool is reset
at frame begin and a set kept across that boundary segfaults promptly:

| | 9 dispatches, in frame | outside |
|---|---|---|
| snapshot per dispatch | 7.70 ms | 7.18 ms |
| ONE snapshot per frame | 5.80 ms | 5.62 ms |

i.e. **~0.20-0.24 ms per `vkAllocateDescriptorSets` + 15 `vkUpdateDescriptorSets`
writes**. That leaves ~0.4 ms per dispatch still unattributed, and the remaining
candidate is Metal's render-encoder/compute-encoder switch inside MoltenVK, which
rlvk cannot avoid per dispatch.

**Not implemented, and here is the honest reason.** The fix would be push
descriptors for the compute set (the graphics path already uses them, so the
`Caps.pushDescriptor` plumbing exists) or a pre-allocated per-frame set ring. But
after the batching landed, the in-game PBD solve is ~0.5 ms TOTAL across nine
dispatches — the consumers that were paying this already stopped. A naive
signature cache would also miss on every PBD dispatch anyway, because each one
rewrites `u_phase`, so the uniform block changes by construction. Worth doing
when a consumer that dispatches heavily IN FRAME shows up; not worth the risk to
the compute descriptor layout (see the MoltenVK storage-image quirk above) for a
cost nobody is currently paying.

**CORRECTION (same day, from the game): batching is worth ~3.9 ms and the
synthetic scenario was WRONG about it.** In-game with the PBD fixture:
5.6 ms of fluid before, **1.7 ms after** (60 -> 45 FPS became 60 -> 54-55).
`perf_dispatch_count` reported the change as noise, so its "outside the frame"
variant cannot be reaching the path the game reaches — `rlEnableShader` and
friends call `rlvkBeginFrame` lazily ("ensure a frame is active"), so a scenario
that sets up its dispatch state inside a render loop is probably in-frame no
matter where the call sits. **Do not trust that scenario's out-of-frame column
until it proves which path it took** (methodology rule 4: distrust your own
probes — this is a worked example of it). The three ruled-out suspects above
still stand: they were measured on the in-frame path, which the scenario does
reach.

**Landed:** out-of-frame dispatches now batch into ONE command buffer
(`RLVK.computeBatch`) instead of each creating a command pool, submitting,
waiting on the queue and destroying the pool. Correct by construction — the
per-dispatch memory barriers still order the work — and flushed before anything
that must observe it: any other one-shot submission (`rlvkOneShotBegin`), frame
begin (**before** the compute descriptor-pool reset, which would otherwise free
sets the pending buffer references), and `rlglClose`. Worth ~3.9 ms in the
game, which is where it was finally measured correctly.

## State
Retarget 1.3→1.1-core complete. Headless suite runtime-verified on MoltenVK (20/20, zero validation errors); visual suite 14/14. **In-game confirmed on desktop** (character self-occlusion, black-hole occlusion, soft-particle fade). **Runs on real Android/Mali hardware** (2026-07-17); the Android bring-up bugs (HANDOFF §7.11–7.23) are fixed.

## Immediate-mode normals verified faithful (`imm_normal`, 2026-08-06)
Closed the volume-tube `|N·V|` investigation from the backend side: the handoff's
conclusion ("rlNormal3f does not deliver per-vertex normals") is a **false alarm**. The
`imm_normal` scenario sends a known normal down the game's exact immediate-mode draw path
and reads it back numerically: raw attribute = `view*N` (d 0.002), `matModel*` =
`view*view*N` (d 0.005). The backend is provably faithful — `transformRequired` CPU-rotates
attributes into view space and the flush uploads `matModel = State.transform`, so the old
`matModel * vertexNormal` double-rotated. Core-side fix (attributes passed through, frag
stage takes `V = normalize(-fragPosition)`) landed and is guarded by
`imm_normal` + `core/tests/volume_space_contract_test.c`. → HANDOFF §7.8.

## Done
- **Depth / occlusion in render textures** (§7.1) + **soft-particle soft-cut** (§7.10) — user-confirmed in-game.
- **Graphics-stage SSBO** (§8.2) — `ssbo_vs` 11/11, zero validation errors; read-only graphics SSBOs on weak 1.1 devices via `Caps.graphicsSsboStores`.
- **Port `compute/gpu_particle_system.c`** (§8.3) — nothing to do; already pure `rl*` API, lights up under Vulkan with §8.2 done.
- **Android platform glue** landed; swapchain/letterbox/touch/shaderc/descriptor-path bugs fixed.
- **Descriptor snapshot cache** (§8.4b) landed — per-frame set0 cache to cut per-draw `vkAllocateDescriptorSets` on the pool-ring path.

## Open / next
- **Confirm GPU particles in the actual game** (human-run build) — the one unverified end-to-end path after §8.2.
- **Descriptor cache correctness on real Mali** — it's a no-op on the healthy push-descriptor desktop, so 14/14 proves zero-regression, not the cache working. Needs an on-device run.
- **Desktop pool-ring guard is RED on this MoltenVK/Intel-Iris host** (§8.4b) — `RLVK_FORCE_POOL_RING` can't validate pool-ring changes here (a Metal argument-limit artifact, not a Mali regression). Decide: different guard host (Android emulator / non-Intel MoltenVK) or a documented "skip on Intel-Iris" caveat.
- **Perf**: ~30 FPS on Mali with the full HDR + ScreenDistort + PostFX(+bloom) + GPU-particle pipeline; not profiled/optimized. Descriptor cache is the first lever (unverified on Mali).

## Deliberate known gaps (§8.5)
- `rlLoadShaderProgramEx` unimplemented (unused by raylib's normal flow).
- `rlBindImageTexture` records but plain textures lack STORAGE usage/GENERAL layout — dedicated path when a real consumer appears (blanket STORAGE kills mobile framebuffer compression).
- Compute samplers limited to 2 (bindings 12–13).
- Cache limits warn when exhausted (`RLVK_MAX_RENDER_PASSES` 32, framebuffers 64, desc sets 1024/256) — tune against real content.
- Shadow-copy twin bandwidth (§7.10): the depth→R32F bounce runs at every `rlDisableFramebuffer` of a depth FBO even when never sampled (quirk drivers only) — future opt = lazy first-sample creation.
- Validation noise: `01211` ×30 fixed → suite 0; only the intentional §7.5 stride-0 `04457` ×3 remains.

## Long-term (§8.6, standalone engine)
Tiler-aware VFX (load/store ops as real levers), then extract `core/` VFX + `compute/` + `environment/` into a library whose only downward interface is rlgl + platform hooks. GL-vs-Vulkan stays a build-time choice per binary; runtime selection needs a shared-library split (deferred).

## Perf tooling repairs (2026-08-12)

Found while measuring the SSF surface's frame cost for `core/fluid`.

- `perf_ssf_filter` had **never produced a valid number**. It loaded its shader by
  a path relative to the repo while the harness `cd`s to its cache directory, and
  raylib answers a missing shader file with the default shader and a non-zero id,
  so the scenario's `id == 0` guard never fired. Now resolved through
  `RLVK_REPO_ROOT` (exported by `run_rlvk_visual_test.sh`) and guarded against
  `rlGetShaderIdDefault()`.
- Its third variant now varies `u_kernelRadius`, not `u_filterRadius`. The filter
  derives its own per-pixel reach from the kernel's projected size and treats
  `u_filterRadius` as a ceiling only, so the original "r=10 vs r=28" comparison
  measured nothing (-1.1 ms, i.e. noise).
- **Result, uncapped, three runs:** halving the resolution saves 1.2–2.0 ms across
  8 filter passes; tripling the kernel width changes nothing measurable
  (-0.27 / +0.73 / -1.34 ms). The narrow-range filter is bound by the pixels it
  touches, not by taps per pixel. That is the measurement the parked
  "half-resolution + bilateral upsample" item was waiting for, and it confirms the
  guessed ~0.6 ms ceiling for the game's 4 passes.
- `RLVK_GPU_TRACE` is broken on MoltenVK — see the new row in `docs/LANDMINES.md`.
  Worth fixing: it is the only GPU-side timing this backend has.
### ShieldShell visual QA follow-up (2026-08-16)

- ShieldShell remains the focused visual fixture (`VFX_APPEARANCE_MAGIC`); its
  body carrier was made darker/sparser and its rim/emission was strengthened so
  bright backgrounds do not become a milky salmon film.
- Updated defaults: opacity `0.78`, rim `2.15`, base alpha `0.025`, Fresnel
  alpha `0.34`, contact alpha `0.70`; shader body luminance curve reduced.
- Post-change gates: `check_rlvk_compile.sh` **OK**; `bright_vfx` **PASS**.

## Runtime teardown validation cleanup (2026-08-16)

`rlglClose()` released graphics shader modules but omitted linked compute programs that
`rlUnloadShader()` correctly retains for GL-compatible stage-handle semantics. The validated
runtime harness therefore completed functionally while reporting four
`VUID-vkDestroyDevice-device-05137` leaks (two modules, two pipelines).

`rlvk_core.inl` now destroys `compMod` and `computePipeline` during shutdown after the existing
device-idle drain. `check_rlvk_compile.sh`, `run_rlvk_runtime_test.sh`, and the full
`VALIDATE=1 run_rlvk_visual_test.sh` pass; the only remaining validation output is the known
intentional stride-0 portability `04457` warning. Full debugging chain: `HANDOFF.md` §7.33;
rule: `LANDMINES.md` “Shutdown cleanup”.

### Session handoff — ShieldShell visual work (2026-08-16)

The owner restored the best-known code state at session end. Do not assume the
experimental ShieldShell scene-through/rear-layer iterations are retained.

Remaining work:

- ShieldShell is still a visual fixture, not a final glass-volume reference.
- Final target: front scene-through glass, readable rear/inner volume, sparse
  internal filaments, and contact glow that does not muddy the body.
- The unresolved issue was rear/front compositing: scalar alpha/gain tuning
  oscillated between a faint rear wall and an opaque/overexposed sphere.
- Validate the actual checked-out state before further edits; use the runtime
  marker and fresh dark/bright world captures, not old screenshots.
- Do not treat `bright_vfx` synthetic gate as visual sign-off for the reference
  glass look; it only verifies the Vulkan HDR/composite path.

Known gate context from this session:

- `rlvk` compile: PASS.
- `rlvk` runtime: PASS.
- Vulkan visual suite: 24/24 normal and 24/24 validation PASS.
- Core suites: 68/73 PASS; five baseline failures remain documented elsewhere.

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-08-16 | Codex | Runtime teardown validation cleanup | `rlvk_core.inl`, `tests/rlvk_runtime_test.c` | Ground-truth |
| 2026-08-17 | Claude | Bright-background oracle rebuilt; postFX blend-toggle bug fixed; dither + NaN guard | `tests/rlvk_visual_test.c`, `core/post_fx.c`, `core/shaders/*` | Ground-truth (25/25 normal + validation, core 68/73) |
| 2026-08-17 | Claude | Hue-preserving tone-map candidate + gates 0-1 + gate-2 tooling | `core/shaders/post_process.fs`, `core/post_fx.c`, `scripts/{run_tonemap_ab.sh,diff_captures.py}` | Ground-truth (26/26 normal + validation, core 68/73) |
