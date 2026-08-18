# ShieldShell — session handoff (17/08/2026)

Branch `bright-background-vfx-foundation`, tree clean, gates green: core 68/73 (the five
baseline failures are pre-existing), rlvk visual 27/27 normal and under validation, three
configure-time validators green.

## The open bug, in the owner's words

Latest report against the current build:

1. **The contact rim appears only on the FRONT face. On the rear it is drawn as a BLACK rim.**
2. **All of the rear face's colour renders as black.**
3. **The front contact band looks unnatural** — a uniform stripe with no white zone and no
   gradient, so it does not match the treatment of the sphere's own silhouette rim.

## What is already established (do not re-derive)

**The rear face's wash is entirely the EMISSION pass.** Bisected by killing one pass at a time
and measuring inside the shell against a flat backdrop (`WUXING_VFX_BG=0x4A4E70FF`):

| | luma inside / bg | chroma inside / bg |
|---|---|---|
| emission killed | 110.2 / 112.6 — darkens, correct | **60.5 / 59.8** — hue kept |
| body killed | 123.2 / 114.2 — **+9** | **49.8 / 58.3** — **-8.5** |

The body pass takes light out and leaves hue behind. The emission pass adds light with no
angular falloff and walks a coloured destination toward neutral. Every body-side and alpha-side
change tried in the last session failed for this reason — they could not reach the pass the
defect lives in. **Start at the emission mask:**

    emissionAlpha = max(fresnel * 0.92, max(contact * 0.90, ripple))

plus `u_emissionGain`. A floor in there that is not angular spreads emission across the
transparent middle by construction. Symptoms 1 and 2 are most likely the same mask evaluated on
the rear wall, where `contact` and `ripple` are angle-independent.

**The hard curved boundary across the shell is the SCENE, not the shell.** Luminance step on a
vertical scan: 11.3 with the map drawn, 1.2 with `WUXING_VFX_BG` skipping it and nothing else
changed. Do not chase it as a shader artifact.

**rlvk is innocent on the depth path.** A scenario reproduced ScreenDistort's exact manual FBO
(depth as a real TEXTURE via `rlLoadTextureDepth(..., false)`) and sampled it both to a plain
target and from inside another FBO scope; both read correct values, so the `Caps.noSampledDepth`
twin serves this shape. The scenario was removed because it inherited depth state from whatever
ran before it in a full suite run — it passed standalone and failed in the suite, which is worse
than not having it.

## Fixed and verified this session — do not regress

* Ground contact works at all. Three causes, each hiding the next: radial-vs-axial distance
  comparison; no `ScreenDistort_RequestSoftDepthRegion()` call; and the request had to move
  INTO the 3D pass, because `ScreenDistort_Begin()` clears `s_softDepthRegionValid` so that
  flag's lifetime is exactly Begin..SnapshotDepth, and the shell draws after the 3D pass.
  It lives in `VC_ShieldShell_Draw3D` now. `shield_shell_depth_enabled` defaults to 1.
* `|N·V|` via `abs()`, not `clamp(dot, 0, 1)`. The rear wall is back-facing, so clamping pinned
  it to 0 across the whole rear hemisphere: wall density saturated, the rim-hot blend went white
  everywhere, and the rear dimming turned that grey.
* Beer-Lambert wall density on path length `1/|N·V|` replaced a `(1-|N·V|)^4` rim band.
* Tessellation is tier-gated AND scales with the live shield count inside the same aggregate
  budget (`activeCount * rings^2 * 2 <= 6400`). LOW stays 14x14 for the mobile per-shell budget.
* `bottomGlow` deleted — it was a band in NORMAL space, which projects to a hard elliptical seam.
* An analytic ground contact was tried and REVERTED: "contact" means touching something, while
  it meant crossing the plane y=0, so a floating shell still drew a band.

## Method that worked, and the traps that cost the most time

* `scripts/render_vfx_matrix.sh "SHIELD SHELL" 40 90 140` — five backgrounds, identical framing,
  plate-referenced. Read `darken%` for glass; `body%` is misleading for a transparent shell.
* **Bisect by PASS before touching terms.** Four term-level ablations found nothing; one
  pass-level bisection found it immediately.
* **A change that does not move a measurement is not a fix.** Three plausible changes were
  reverted rather than left in, because keeping them makes the next person believe that ground
  is covered.
* Shaders hot-load from disk; C does not. A measurement after a `.inl` edit describes the OLD
  binary — `render_vfx_matrix.sh` now refuses to run when C sources are newer than the binary.
* Several tests pinned implementation SHAPE rather than contract and made legitimate changes look
  like regressions (`SHIELD_SHELL_RINGS 14`, `fresnelX2 * fresnelX2`, the literal text of an empty
  `Draw3D` stub). All three were loosened to contracts. Expect more of these.

## Files

`core/shaders/glass_shell.fs` and `.vs`, `core/composition/common/vc_shield_shell.inl`,
`core/tests/shield_shell_test.c`. Live knobs in `tuning.cfg`: `shield_shell_contact`,
`shield_shell_contact_thickness`, `shield_shell_contact_alpha`, `shield_shell_base_alpha`,
`shield_shell_depth_enabled`, `shield_shell_opacity`, `shield_shell_rim`.

Full narrative: `third_party/vulkan/docs/PROGRESS.md` (newest entries first) and
`third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md` §5 (authoring laws), §11b (the harness
and measured baselines), §12.1 (the hue-preserving tone map, shipped at 0.6; gate 5 / Mali still
outstanding).
