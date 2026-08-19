# ShieldShell — session handoff (18/08/2026)

Branch `bright-background-vfx-foundation`. Gates green: core **69/74** (the five baseline
failures are pre-existing and unchanged), rlvk visual 27/27, surface-registry and
`sync_vfx_test --check` clean.

> **Updated 18/08/2026, late.** Everything below still holds. Since it was written: the
> contact band's white core came back on its OWN window, the fresnel curve moved to the
> fragment stage, FXAA landed as the shipping anti-aliasing, and MSAA landed as a
> capability that is OFF by default. See "What landed after this doc was first written".

## Six symptoms CLOSED, and the seventh CLOSED TOO (18/08/2026)

Each closed one had a single cause; two of them shared a cause. Verified in a scene
render, not from numbers alone. The seventh — the rainbow rim — is a global tone-map
policy question with a measured answer and a ready patch, deliberately NOT applied; see
"The rainbow rim" below.

1. **Contact rim only on the front face, BLACK on the rear** — the emission scope ran a
   single draw and inherited `RL_CULL_FACE_BACK` from the body pass, so emission covered
   the near wall only.
2. **Rear face renders black** — same cause. The far wall received the body pass, which
   only takes light out, and no radiance at all.
3. **Front contact band is a flat stripe** — three things: the band was in the WRONG PLACE
   (see the depth blit below), it was one flat colour times a ramp, and its profile was
   PLATEAUED. It now uses the silhouette rim's own hot-core/corona treatment on a peaked
   falloff.

4. **(reported after the first fix) The bright bands read as concentric colour RINGS.**
   `1 - smoothstep` has zero derivative at its lower edge, so the band held ~1.0 for its
   first ~15% — 13 px where R is pinned at 1.0 and hue is the only thing left that can
   vary. §12.1's hue-restoration weight rises and falls along an intensity ramp, so the
   other channels went down and back up (`G = 185 → 170 → 231`): a ring. A cubic falloff
   takes the flat top 13 px → 5 px and removes the dip **on the contact band**.
   **Brightness was the wrong lever** — halving the strengths moved the clipped area
   10924 → 6940 and kept the dip. NOTE: this narrowed where the pipeline can band; it did
   NOT remove the banding mechanism, which is symptom 7 and is not the shell's.
5. **The silhouette cliffed from background to near-white in one pixel** — not a shell
   bug at all: `bloom_scatter` was pinned at 1.0, which discards the near halo for every
   effect in the game. See the section below; it is a global change.
6. **Blocky "hạt hạt pixel" graininess along the bright edges**, once the halo came back
   — the composite magnified the QUARTER-resolution `bloomTex` 4x with a single bilinear
   fetch, which reconstructs as piecewise-linear patches kinking on a 4-pixel grid. The
   composite now runs the same 3x3 tent the upsample chain uses. Latent all along: a
   collapsed pyramid had left no detail in that buffer to alias.

Fixes: `core/composition/common/vc_shield_shell.inl` (emission composites both
interfaces), `core/screen_distort.c` (the blit), `core/shaders/glass_shell.fs` (contact
band profile + shaping, rear emission weight), `tuning.cfg` (`bloom_scatter` back to its
shipped default), `core/shaders/post_process.fs` + `core/post_fx.c` (tent on the final
bloom upsample), `scripts/render_vfx_matrix.sh` (guard). Full narrative with the measured trade-off table:
`core/docs/PROGRESS.md`, entries 2026-08-18 through 2026-08-18e.

## The rainbow rim: it is the tone map, not the shell — and it is structural

7. **"The rim is like a rainbow, distinct colour patches."** **FIXED 18/08/2026 — at the
   tone map, which is where the probe pinned it.** `toneMapScene()` now uses the monotone
   form (constant weight, whitening moved into a monotone desaturation of the hue-kept
   colour); the shell's one-hue ramp measures 0 reversals and chroma is UP on all five
   background plates. `tonemap_shoulder` was REWRITTEN around the new contract and the
   suite is 28/28. See "The gradient probe" below and `core/docs/PROGRESS.md` 2026-08-18g.
   Originally settled with the effect removed entirely — Originally isolated as the
   owner suggested, with the simplest possible gradient: the shell's emission replaced by a
   pure linear ramp of ONE colour (`rimColor * t * 12`), body pass and far wall discarded,
   bloom off. No geometry, no profiles, no term stacking — whatever structure survives is
   the pipeline's. On a strictly RISING input, G came out
   `169 → 151 → 152 … 172 → 183 → 254 → 255 (frozen)`: a reversal, a slow plateau, a fast
   sweep, then a frozen hue. Four zones from a clean ramp.

**Attribution on that same probe:**

| `postfx_hue_restore` | G reversals | worst drop | max dHue/step |
|---|---|---|---|
| 0.0 | 0 | 0 | 7.74 |
| 0.5 (shipping) | 1 | 18 | 7.18 |
| 1.0 | 5 | 71 | 10.32 |

**Mechanism.** Hue keeping restores chroma by LOWERING the non-peak channels. Its weight
`hueRestore · smoothstep(1,2,peak) · (1 - smoothstep(5,9,peak))` rises and then falls, so
along a rising ramp those channels are pulled down and released — a trough with two edges,
which is what a colour band is.

**Why it cannot simply be tuned away.** `rlvk_visual_test`'s `tonemap_shoulder` requires
the change to be BOUNDED: bit-identical for `peak < 1` **and** for `peak > 9`. A weight
that is zero at both ends and non-zero between them cannot be monotone. Under this
blend-two-curves architecture, "bounded" and "no banding" are the same knob pointed in
opposite directions. §12.1 chose bounded, deliberately.

**The monotone alternative is measured, better on every other axis, and NOT applied.**
Candidate H — take the intensity dependence out of the weight entirely, move the whitening
into a monotone desaturation of the hue-kept colour — gives 0 reversals, max hue rate 4.75
vs 7.18, and chroma UP on every plate (white 0.320 → 0.347, warm 0.349 → 0.408, cool
0.140 → 0.215) at unchanged darkening, with `bright_vfx` and `bright_vfx_ldr` still green.
It fails `tonemap_shoulder` at peak 0.2 with d = 0.03: every material below the shoulder
shifts by up to ~8/255. That is the gate doing its job — a whole-scene approval, not a
bounded fix — and the standing instruction here is that rlvk stays 27/27. The exact
one-line patch is in **`HANDOFF_TONEMAP_CANDIDATE_H.md`**; it is shader-only and hot-loads,
so it can be tried and reverted with no rebuild.

Three other reformulations were measured and are dead: anything that starts whitening near
peak 1 destroys what §12.1 exists to protect (white chroma 0.320 → 0.073 / 0.107).

## What landed after this doc was first written

* **The contact band's white core is back, on a WIDER window** — `smoothstep(0.75, 1.0,
  contact)`, not the rim's `0.88..0.995`. Copying the rim's narrow window put the white on a
  shell of `contact` values that, after the profile went cubic, sits OFF the visible band's
  peak: it read as a separate pale stripe beside the orange one. And the band's luminance is
  already at the ceiling across its width, so white there cannot brighten anything, only shift
  hue. Term ablation isolated it — removing the matcap or `rimHot` changed nothing. Removing
  the core outright was tried and is ALSO wrong: the ground contact is the same event as the
  silhouette and has to look like it.
* **Fresnel and `|N.V|` are evaluated per FRAGMENT now.** They were per-vertex varyings, i.e. a
  QUARTIC sampled at the vertices and joined with straight lines across a 40-slice sphere, and
  every shading term hangs off them.
* **FXAA is the shipping anti-aliasing** (`core/shaders/fxaa.fs`, `postfx_fxaa`, default 1.0).
  The renderer had none at all: every geometric silhouette landed with binary coverage, which
  is what the remaining stair-stepping at the shell's boundary was. Not a shell bug — the map's
  own geometry staircased identically.
* **MSAA on the scene target exists but is OFF** (`WUXING_MSAA=4` opts in). It fixes opaque
  triangle silhouettes only. On the shell it measured 937 -> 933 luma steps — nothing — because
  an emissive HDR silhouette is eaten by the tone curve and a shader-decided rim is thinner
  than the pixel MSAA shades once. `ENGINE_LANDMINES.md` #19 has the three edge classes; read
  it before trying to sharpen any edge.

## The gradient probe — symptom 7, with no effect in the frame at all (18/08/2026)

The isolation above still ran inside the shell's own shader, so "the shell" was still on
the stand. `sandbox/gradient_probe.c` + `core/shaders/probe_gradient.fs` remove it: a
RECTANGLE whose colour is an analytic function of x, drawn into the HDR scene target
inside `PostFX_Begin/End` so it takes the whole chain, with the same ramps evaluated on
the CPU through plain per-channel ACES and drawn AFTER `PostFX_Draw` as the control.
Press **G** in the VFX tester, or `WUXING_GRADIENT_PROBE=1` for a headless run; it prints
the numbers and writes `autotest_output/gradient_probe.png`.

Result: **the shell is not the one banding.** Band 1 — one hue at a rising level, exactly
what `glass_shell.fs` computes — has a G slope of `+26 → +9 → -10 → +9`, a stall, a
reversal and a re-acceleration. Its control is monotone with zero reversals. Band 3, the
same hues at a CONSTANT level, is perfectly smooth at every knob setting, which pins the
mechanism on the LEVEL dependence of §12.1's weight and clears the hues. Band 4 (one flat
colour) is the positional reference that proves vignette and chromatic aberration are not
doing it.

Dose–response on the live knob, band 1's minimum G slope: `0.0 → +6`, `0.15 → +10`,
`0.25 → +4`, `0.35 → -1`, `0.5 → -10`. The reversal appears between 0.25 and 0.35, so
`postfx_hue_restore = 0.25` buys the reversal back with no rebuild and no gate change —
short of Candidate H, which is still the better answer and still needs a whole-scene
approval.

Two traps the probe hit on its FIRST run, recorded because they will bite anyone
extending it: measure the MIDDLE of the screen (chromatic aberration and the vignette are
radial, and scored the achromatic band worst of all five), and score the SLOPE, not the
sign (at the shipping strength no single-pixel step is negative while the band is plainly
visible). Full chain in `ENGINE_LANDMINES.md`, "Bounded change and no colour banding are
the SAME knob"; session narrative in `core/docs/PROGRESS.md` 2026-08-18f.

## The bug under the bug — read this before touching any depth consumer

`ScreenDistort_SnapshotDepth()` wrote **any region smaller than the full frame to the
wrong rows of its target**. The blit uses a negative source height, which mirrors the
block, so the block must also be placed at the mirrored destination `(H - y - h) / D`;
`y / D` is correct only when `y = 0, h = H` — the only region ever armed until a shell
bounding box reached it. Displacement is exactly `H - 2y - h` screen rows.

This is cross-cutting: every soft-depth consumer read it. `IMPACT DUST` and
`SMOKE COLUMN` were checked before/after and are pixel-identical, so nothing was tuned
against the misalignment, but check any new consumer against
`ENGINE_LANDMINES.md` — "Blitting a SUB-RECTANGLE between render textures".

**The diagnostic that found it, worth reusing:** swap the partial region for a full-frame
one. If the picture changes, the bug is in the region plumbing, not in anything the
consumer computes from what it is handed.

## Correction to the previous handoff's starting point

The recorded pass bisection (emission raises luma +9, drops chroma 8.5 inside the shell)
reproduces exactly and is correct — but it was taken on a **flat background with the map
skipped**, where there is no floor, no contact term and no occluded rear wall. It cannot
see symptoms 1–4 at all — and the matrix is byte-identical before and after the contact
profile change, which is the proof, not a hope. `render_vfx_matrix.sh` sets `WUXING_VFX_BG`, which skips the
map, so the ground line the owner is describing **does not exist in any of its five
backgrounds**.

- **Instrument for the ground-contact symptoms:** `./build/wuxing --render-vfx 21
  --warmup 90 --out <png>` with NO `WUXING_VFX_BG` (index via
  `scripts/vfx_fixture_index.py "SHIELD SHELL"`).
- **Instrument for what a fix costs elsewhere:** `scripts/render_vfx_matrix.sh
  "SHIELD SHELL" 40 90 140`.

Use both. The matrix alone would have called this fix a regression; the scene shot alone
would have missed the cost.

## Still established, still true (do not re-derive)

* `|N·V|` via `abs()`, not `clamp(dot, 0, 1)` — the rear wall is back-facing.
* Beer-Lambert wall density on path length `1/|N·V|`, not a `(1-|N·V|)^4` rim band.
* `bottomGlow` deleted: a band in NORMAL space projects to a hard elliptical seam.
* An analytic ground contact was tried and REVERTED — "contact" means touching something,
  not crossing `y = 0`.
* The hard curved boundary across the shell is the SCENE, not the shell. It is also,
  specifically, the far wall being occluded by the floor: correct geometry, and now the
  place the contact band lives.
* rlvk is innocent on the depth path.
* Shaders hot-load from disk; C does not. `render_vfx_matrix.sh` refuses to run when C
  sources are newer than the binary.

## Tried and REMOVED this session

A screen-space width floor `max(u_contactThickness, fwidth(gap) * 5.0)` in
`depthContact()`, as a cure for the missing rear band. With the snapshot aligned it moves
104 of 921600 pixels by more than 2/255, costs two derivatives, and lights up the
silhouette where `fwidth` explodes and a 30 m gap still scores as "touching". A note
remains at the site so it is not re-derived.

Unifying `rimHot` and `contactHot` into one `max()` hotness scalar, on the theory that two
white peaks straddling a saturated trough made the colour rings. 4 pixels of 921600. The
two ramps are not the mechanism.

## Method that keeps working

* **Bisect by PASS before touching terms** — still the highest-yield move. Here the next
  step after it was to bisect by WALL (`if (u_wallPass != 0) discard;`), which showed the
  rear wall's crescent immediately, and then to probe the raw depth gap as colour buckets,
  which is what exposed the blit.
* **A change that does not move a measurement is not a fix** — remove it rather than
  leaving it, or the next person believes that ground is covered.
* **Look at the image before reporting.** The rear-emission rebalance improved chroma on
  every plate while the cool plate's darken% fell; only the picture settles whether that
  is a shell made of glass or a shell made of fog.
* **A guard that cannot fail on the pre-fix code is not a guard.** Every new guard here
  was confirmed red on the pre-fix code first.
* **A single scanline is not a proof of smoothness — and one placed where the artifact
  ISN'T proves nothing.** "Smooth at 1:1" was reported here off a scan through the far
  halo while the stair-steps sat on the bright edge a few pixels away. Sample where the
  artifact is; when the eye and a metric disagree, the metric is on trial.
* **Fixing one thing can EXPOSE a latent defect downstream.** The blocky halo predates
  the scatter restore and was merely invisible while the pyramid was collapsed. Check
  whether a newly visible artifact is older than your change before reverting.

## The hard silhouette edge was bloom_scatter — and it is a GLOBAL change

`tuning.cfg` pinned `bloom_scatter = 1.0`. The upsample folds with
`dst = mix(dst, tent(src), scatter)`, so 1.0 means `dst = tent(src)` — the finer level is
REPLACED at every step, the pyramid collapses to its coarsest mip, and NO effect in the
game can have a near-field halo. The file's own comment says "Sweep 0.4 -> 0.8"; the
shipped default is 0.65; `git log -L` shows 0.65 → 1.0 in `b18e76d` ("update lut"),
apparently incidental. Restored to 0.65: the silhouette now ramps 87 → 203 over 20 px
instead of cliffing in one pixel.

Cost, measured, and it is the owner's call to keep or revert: the shield's cool-plate
darken% RECOVERS 18.6 → 50.8 (the one real cost of the rear-emission fix) and the
anomalous mid/cool footprints collapse from 36%/20% of frame to 10%/12%; warm darken%
drops 87.5 → 65.5. FLAME VOLUME moves 80677 pixels; IMPACT DUST and SMOKE COLUMN are
pixel-identical. **§11b's measured baselines were all taken at scatter 1.0 and are now
stale** — flagged rather than edited, since that spec belongs to the renderer module.

Diagnostic worth reusing: scan luminance OUTWARD from a silhouette. Flat means the
pyramid collapsed; a ramp means bloom is working. Raising intensity only makes a flat
veil brighter, which is exactly how this hid.

## Two instrument bugs found here — they will bite the next person too

* **`tuning.cfg` overrides appended at the end are DEAD TEXT.** `FindKeyValue` takes the
  first match, and the file already held `postfx_hue_restore = 0.5`. An A/B at 0.0 / 0.5
  / 1.0 came back byte-identical and would have exonerated the tone map. Grep the key and
  EDIT the existing line. Note this machine pins hue restore at **0.5** where §12.1
  documents 0.6 — every local measurement is at 0.5.
* **`render_vfx_matrix.sh`'s stale-binary guard counted `core/tests/`**, which links
  nothing from the game, so it refused to run whenever a regression test was added beside
  a fix. Excluded, and re-verified to still fire on a real source change.

## Open / next

* ~~The contact ring is bright enough to compete with the silhouette rim.~~ **DONE
  18/08/2026, the other way round**: the SILHOUETTE was the outsized one (16 px band /
  ~5 px clipped white core against the contact's 8 px / ~1 px). The rim now has its own
  authorable width, `shield_shell_rim_power` (default 8.0), and takes its white core from
  its own band's peak exactly as the contact does. 7 px / 1 px / peak 237 vs 9 px / 1 px /
  228. Cost: `darken%` up on the bright plates (mid 7.5 → 50.1, white 79.8 → 91.6) because
  the glass carrier is now the larger share of a thinner effect — chroma and detail are up
  everywhere else. See `core/docs/PROGRESS.md` 2026-08-18h.
* **The shell reads as two bright rings with a hole in the middle** — the emission mask is
  `max(fresnel * 0.92, ...)` and `fresnel` is `(1-|N.V|)^4`, which at the middle of the disc
  (`|N.V| = 0.71`) is 0.0067. That mask is A RING BY CONSTRUCTION, while the body pass carries
  the Beer-Lambert wall thickness — a real volume — but only takes light out, which is
  invisible on a night background. An emission term shaped by `wallDensity` was tried and
  REVERTED: it looked right, but the uniform never reached the shader (sweeping the knob
  changed 0 pixels) and removing its last use made the renderer abort — an rlvk UBO trap that
  was not chased. Anyone picking this up should start by finding out why that uniform did not
  arrive; the visual direction was correct.
* `BRIGHT_BACKGROUND_VFX_SPEC.md` §11b's measured baselines predate the `bloom_scatter`
  restore and need re-measuring. Renderer module's file — not edited here.
* ~~**The rainbow rim is unresolved by decision, not by ignorance.**~~ **DONE 18/08/2026** —
  the owner took the whole-scene approval and Candidate H is the shipping curve. The gate
  did not go red: it was rewritten around the contract that was actually chosen (achromatic
  bit-identity + a ceiling on the saturated shift + monotonicity), with the new check
  confirmed red on the pre-fix shader first. `HANDOFF_TONEMAP_CANDIDATE_H.md` is history
  now and its headline cost figure was understated — see `core/docs/PROGRESS.md`
  2026-08-18g for the full table.
* The cool-plate darkening trade is a one-line choice:
  `glow *= mix(1.0, 0.42, rearInterface)` in `glass_shell.fs`. The measured curve for
  0.00 / 0.28 / 0.42 / 0.68 is in `core/docs/PROGRESS.md`.

## Files

`core/shaders/glass_shell.fs` and `.vs`, `core/composition/common/vc_shield_shell.inl`,
`core/screen_distort.c`, `core/tests/shield_shell_test.c`,
`core/tests/soft_depth_region_test.c`, `core/shaders/post_process.fs`, `core/post_fx.c`,
`core/tests/bloom_pyramid_contract_test.c`, `tuning.cfg`, `scripts/render_vfx_matrix.sh`. Live knobs in `tuning.cfg`:
`shield_shell_rim_power` (rim WIDTH — the counterpart of contact_thickness; amplitude
cannot narrow a band), `shield_shell_contact`, `shield_shell_contact_thickness`, `shield_shell_contact_alpha`,
`shield_shell_base_alpha`, `shield_shell_depth_enabled`, `shield_shell_opacity`,
`shield_shell_rim`.

Also: `third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md` §5 (authoring laws), §11b
(the harness and measured baselines), §12.1 (hue-preserving tone map, shipped at 0.6;
gate 5 / Mali still outstanding).
