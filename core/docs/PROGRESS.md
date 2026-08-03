# Core Engine — Progress

> **Rebuilt 28/07/2026.** This file had grown to ~3000 lines of session log, and
> reading it whole was the most expensive mistake available in this repo. It is
> now *state*, not *diary*. Nothing that mattered was lost:
>
> - **The lessons** were promoted to `docs/LANDMINES.md` (module-local) and
>   `ENGINE_LANDMINES.md` (cross-cutting) — where people actually look for them.
> - **The plan** is `docs/VFX_PLAN.md` (Đợt H), superseding `docs/ELDEN_VFX_SPEC.md`.
> - **The narrative** is in git history if anyone ever needs it.
>
> Keep it this way. Add an entry only for something *still true and still
> actionable*; when a thing is finished, delete its entry and make sure the lesson
> it taught is in LANDMINES.

---

## Current state (28/07/2026)

**Composition layer:** eleven survivors after the F0 purge — SmokePuff,
FlameVolume, CharacterAura, GlintSparkle, RuneCircle, ChargeConverge,
DissolveExit, SweepSlash, EnergyBurst, ImpactPackage, LightShaft.
`core/composition/visual_composer.h` documents each one and is the index.

**Đợt H:** H1 `VFX_ComposeSweptTrail` has landed (BLADE / RIBBON / FILAMENT,
`core/composition/common/vc_ribbon_trail.inl`) — the first consumer of
`core/trail_system.h`, which the purge left at zero users. Bench entry
`SWEPT TRAIL` drives a figure-eight and cycles the three styles every 4 s;
arithmetic covered by `core/tests/swept_trail_test.c`.

**Restored scaffold (NOT survivors):** the owner brought back a set of
pre-Đợt-E effects to rewrite the two water skills against — water stream and
on-path, ice crystal + burst, stone pillar, fissure streak, black hole, shard
debris. They sit in the `@gen:vc_declarations` block at the bottom of
`visual_composer.h`, marked there as temporary, with a planned end date at H8.
Do not build new work on them.

**Engine layers, in and working:** F1 lit particles, E1 post-FX (radial blur +
streak bloom, tier-gated), E2 VFX light bleed onto surfaces, E3 `VFX_Sequence`,
E8 quality tiers + `postfx_perf_log`.

**Bright-background VFX contrast:** the renderer now separates coloured VFX
bodies from additive emission. Every new VFX must follow the body/emission
contract in `API_GUIDE.md`; the renderer-independent guard is
`core/tests/bright_vfx_isolation_test.c` and wiring is covered by
`core/tests/vfx_render_layers_contract_test.c`.

**Skills:** deliberately bare wherever the purge took their visuals — logic,
timing, damage and clash untouched. The two water skills are rebuilt
(`tube_skill.c`, `glacial_cannon_skill.c`); the rest wait for H8.

**Verification available to an agent:** `./scripts/run_core_tests.sh` (10 suites),
`scripts/check_rlvk_compile.sh`, `scripts/run_rlvk_runtime_test.sh` (20 headless
scenarios). **Not available:** anything needing a window — `./build/wuxing` dies
with `FATAL: RLVK: instance creation failed` outside the owner's graphics
session, while headless Vulkan works fine. Every runtime question therefore has
to go through a log line, a tunable, or arithmetic.

---

## Open items

### 0. VOLUMETRIC TRAIL — the geometry half now exists (30/07)

`TRAIL_SHAPE_TUBE` landed: the trail system sweeps a volume, not only a strip.
Full account, the specs for what comes next, and a ready-to-paste prompt for a
fresh session are in **`docs/HANDOFF_VOLUME_VFX.md`** — read that rather than
this entry if you are picking the work up.

**Verified on screen 30/07.** Four independent causes of one symptom ("renders
flat") had to be fixed in sequence; the last was the transported frame collapsing
onto the tangent, where normalising a ~zero vector flattens the whole section into
a line — silent, no NaN, no log. The general lesson is in `docs/LANDMINES.md`.

### 0. H1 swept trail — REBUILT against the owner's ribbon-trail guide (29/07)

The draw was replaced wholesale: the composition now renders its own three-layer
ribbon from its own history ring instead of handing the job to a TrailEntity, and
the four things the owner's reference guide lists that we simply did not have are
in — a tiled + scrolled flow sheet with interior fibres, a lens width envelope
tapered at BOTH ends, flow-noise displacement, and sparkles along the ribbon.
Dials: `swept_flow`, `swept_wobble`, `swept_spark`, `swept_alpha`, `swept_width`.

**Two fixes on 29/07 for the owner's last two complaints, both now arithmetic
rather than opinion (`docs/LANDMINES.md` has the write-ups):**

- *"it twists itself"* was the polyline FOLDING, not the side vector. The stray
  bound was a flat 0.30 m against a 0.12 m node spacing at bench speed, so a node
  could pass its own leader; the rope constraints cannot see that, because
  distance is a scalar. The deviation is now split — along the path, bounded by
  `SWEPT_ORDER_FRAC` (0.45) of the node spacing; across it, the old metre bound.
  The lateral flutter is untouched, so the fix costs no motion.
- *"the energy does not move"* was the KIND of pattern. Continuous fibre lanes
  spanning the sheet can only translate when `v` scrolls. Replaced with 16 finite
  streaks (raised-cosine envelope, circular distance in `v` so the tiling stays
  seamless), which appear and fade as they pass.

- *"the energy still does not surge"* — the fold and the sheet were both fixed and
  it was still the flow. The owner called it: the UV was scrolling **with** the
  motion. It was `arc from the tail / tile`, and a full history ring's tail
  retreats at the head's speed, so 6.5 of the 8.8 tiles/sec was the swing leaking
  in — locked to the blade, and far too fast to track. The UV now comes from
  `nuv[]`, a material stamp written once when a node is laid, so the rate is the
  scroll term alone. Body sheet is `assets/textures/energy_flow.png`, rotated a
  quarter turn, cropped to the band that carries filaments and cross-faded so it
  tiles. New dials: `swept_sheet` (asset vs procedural), `swept_tile` (metres per
  repeat); `swept_flow` now means tiles/sec over the cloth, and negative flows the
  other way.

Still unverified on screen — all of it needs the owner's eye.

**Audit that changed the plan (29/07).** The owner asked whether H1 reused the
engine. It did not: `vc_ribbon_trail.inl` used `DrawRibbonStripEx`,
`Ribbon_ConstrainSegment`, `ForceField_*` and the curve types — but **zero** of
`core/trail_system.h`, whose empty consumer list was H1's stated reason for
existing. 849 lines of code (plus 637 of comment) had grown a private history
ring, sample clock, cloth and layered draw next to a system that should have
owned all four. The force field was hard-coded per style instead of coming from
`VFX_Material`, against `core/CLAUDE.md`'s composition rule.

**Step 1 — DONE: `core/trail_system.c` now owns the mechanism.** All additions
are inert at 0, so every existing consumer is byte-identical:

| Added to `TrailConfig` | What it fixes |
|---|---|
| `layers` / `layerCount` (`TrailLayer`) | replaces the hard-coded outer+inner pair; per-layer width, alpha, whiten, scroll rate, head burn, and its own texture |
| `uvMetresPerTile` | material-coordinate UV — the legacy `segRatio * uvTiling` both stretches with the trail's length AND is anchored to the moving head |
| `nodeHomeSpring` / `nodeHomeMaxDev` / `nodeOrderFrac` | cloth: FOLLOWER had NO constraint at all, so any force field replaced the swept path instead of perturbing it |
| `sampleHz` | FOLLOWER laid one node per FRAME — trail length in metres was a function of frame rate |
| `teleportSpeed` | a jump no longer draws a bridge through space the emitter never crossed |

Plus `Trail_SetLateralOffset` (per-frame world offset, for a bundle spreading
along an axis the caller derives) and `Trail_SetFrozen` (hold the shape, keep the
flow — the instrument that settles "is it actually flowing"). Arithmetic covered
by `core/tests/trail_cloth_test.c` (40 assertions). 13/13 suites green.

**Step 2 — DONE: `vc_ribbon_trail.inl` ported onto it.** 849 lines of code down
to 625, and what is left is authoring: styles, aspect cap, width/alpha curves,
the two sheets, the layer table, the swing-plane normal, the sparkles. The ring,
`Push`, `Cut`, `Simulate`, `BuildPoints`, the sample clock, the teleport check
and the three-pass draw are all gone into the engine. Tunables: 17 down to 11.

Two things were dropped on purpose, and the owner made the call that unblocked
both by confirming the dashing was the self-twist:

- **The screen-space width floor.** It existed to stop the blade rendering as a
  dotted line and never did; the dashing was the FOLD, fixed at the source by
  `nodeOrderFrac`. Keeping it would have dragged a camera dependency into an
  update path with no business knowing about one.
- **The FILAMENT lag schedule.** Each strand is now its own entity at its own
  lateral offset (`Trail_SetLateralOffset`), sampling the cloth field at its own
  position — so strands diverge because they sit in different places in a moving
  air field, not because they are copies of one path in the past.

One safety change the port forced: the entity now holds the CALLER's Matrix, so
`VFX_KillSweptTrail` must **detach** rather than just release the slot. It still
wind-downs (detaching stops the feed and the idle fade drains the tail), but a
strand left attached after the caller's storage goes out of scope would be a read
after free every frame. Pinned in the mirror test.

Dropping the TrailEntity also removed the inner-core strip, which was the last
unruled-out suspect for the dashing. The history below is kept because it is four
measurements, and if dashes return, the ruled-out list is where to start.

#### Prior investigation — BLADE dashed, four causes ruled out

**Status: parked at the owner's call (29/07). RIBBON and FILAMENT are correct;
the BLADE style renders as a dotted line and four rounds failed to fix it.**
Everything below is what is now KNOWN, so the next attempt starts from evidence
rather than from zero.

**Ruled OUT, each by a measurement, not an opinion:**
1. *Mask frequency.* The blade first reused `VFX_ComposeSweepSlash`'s sheet,
   authored for a band 6x wider (11.5 cycles across it). Replaced with a
   dedicated low-frequency sheet — no change.
2. *Sub-pixel geometry / distance.* A screen-space width floor with alpha
   compensation was added — no change. And FILAMENT, whose geometry is the
   THINNEST of the three (1:40 vs the blade's 1:20), never dashes at all, which
   this theory predicts backwards.
3. *Mask energy distribution.* The blade sheet packed 66% of its alpha into the
   outer 20% of the band (measured) vs 10% for the sheet the two working styles
   use. Broadened to ~48% at mid-band — no change.
4. *Foreshortening of the plane-pinned strip.* Modelled numerically against the
   real bench path and the real sandbox camera: the projection factor runs 1.00
   to 0.08 WITHIN one strip (13x), worst 0.069 over all camera angles, while
   camera-facing is 0.99 everywhere. `core/ribbon_strip.c` now blends `side`
   toward camera-facing below 0.35 (worst becomes 0.342, verified in
   `Test_Foreshortening`) — **and the blade still dashes.**

5. *The inner core strip* — **acted on 29/07, unverified.** `disableInnerCore`
   was false for BLADE alone: a second strip at 0.267x the outer half-width,
   drawn pure white at alpha 255 with the alpha curve ignored
   (`trail_system.c:961`), i.e. ~4x thinner and ~1.5x brighter than the band
   around it. The distance gate added earlier could not save it: the gate
   measures the band at the HEAD once per frame while the width envelope tapers
   it along its length, so at segRatio 0.4 a band the gate read as 9 px is
   really 5.4 px and the core is 1.4 px. **`swept_core` now defaults to 0** —
   RIBBON and FILAMENT have always had the core off and have never dashed, and
   the hot line BLADE wanted from it is already in its own mask.

**That was the last structural difference.** If BLADE still dashes with the core
off, then it differs from the working styles only in sheet and aspect, both of
which have been addressed, and the next step is not another guess but the
`swept_camfacing = 1` dial — which settles the swing plane's involvement in one
run and has still never been used.

**Also unfinished, and owned elsewhere:** no skill consumes the API yet (Skills
Agent, folded into H8). Dials: `swept_width`, `swept_aspect`, `swept_lag`,
`swept_spread`, `swept_core`, `swept_minpx`, `swept_blade_flat`,
`swept_camfacing`.

**Carry-over risk:** `core/ribbon_strip.c`'s foreshortening blend is shared by
`VFX_ComposeRuneCircle` and `VFX_ComposeSweepSlash`. It is a no-op above 70
degrees of foreshortening and measured correct on its own terms, but it has
never been seen on screen. If either of those two changes appearance, that is
where to look first.

### 1. Soft particles — fixed (2026-08-01)

rlvk now resolves the draw-call texture by reflected sampler name rather than
assuming descriptor binding 0 (`third_party/vulkan/docs/HANDOFF.md` §7.30).
`particle_lit.fs` again declares and samples `u_cameraDepthTex`; the existing
depth-bind path is live with a conservative default `particle_soft_fade = 0.35`
metres. GPU compute billboards use the same 0.35 m fade; fluid capture/thickness
passes remain unmodified. The `sampler_pair` visual scenario guards the backend contract.

### HDR composite contrast — fixed (2026-08-01)

Ordinary bright surfaces were entering the bloom extractor at the old 0.8 threshold,
then receiving the extractor's 2.2x gain before bloom was mixed back into the entire
frame and tone-mapped. Default bloom now begins at 1.25 (HDR emitters still qualify),
mixes at 0.12, and uses neutral exposure 1.00; this preserves local VFX/material contrast
on light maps. Bright-background VFX hue is protected by the separate VFX body/emission
compositor, not by this post-processing budget.

### 2. ImpactPackage cost at high severity

Down from 4.70x to 1.91x the cost of a bare burst, still dropping frames when
spammed at severity 1.0. The residual is intended — a maximum-severity impact
*should* be bigger than the bench default — so this is a budget question with an
obvious dial: severity 0.5 is exactly the bench burst. Per-beat measurement
switches: `impact_light`, `impact_distort`, `impact_decal`, `impact_hitstop`.

### 3. "Spirit" read as energy, not as a SOUL

The composition was deleted with the purge; the *analysis* is kept so the next
attempt does not restart from zero. Ranked cheapest first: ghost-pale colour
instead of a saturated element hue; whole SECTIONS of the body dropping out and
returning (a soul is intermittent, ours was continuous); drifting rather than
ballistic motion; an inner shell at a different scroll rate; and — the expensive
answer — that a featureless tube cannot hint at a figure, i.e. the primitive
itself is wrong.

### 4. Water tube's mist emits per frame, not per second

`tube_skill.c` spawns mist on a `GetRandomValue(0,100) < 60` roll each frame, so
its density moves with the frame rate (the FlameVolume bug in another skin).
One-line fix with a `dt` accumulator; left alone only because it changes the
skill's current look, which is the owner's call.

### 5. Asset library — only two files actually missing

Corrected 28/07 after counting what is on disk: flipbooks and the rune glyph
atlas/ring are DONE; slash masks, glints and erosion are missing but have working
generated substitutes in the consumers. The only group with no procedural
substitute is the tangent-space distortion normal maps
(`distort_normal_swirl`, `_shock`). See `VFX_PLAN.md` H6 — it is no longer the
bottleneck the old spec called it.


## Energy trail — the sin-wave band (03/08/2026)

Target: the RzFX "sin wave trail" look (a glowing ribbon that snakes as it
flows, tapering to a needle tail, HDR into bloom). The previous attempt was a
flat SMOKE_WIDEN plume with vertex waves switched off, so it read as neither.

**What changed, and why each one mattered.**

1. **The wave moved from the VERTEX stage to the FRAGMENT stage**
   (`trail_deform.fs` material mode 2, `TrailMaterialConfig.wave*`/`band*`).
   Vertex displacement folds the strip through itself on a turn and — the real
   killer — keyed off NORMALIZED segment, so the whole waveform stretched every
   time the trail grew or the head changed speed. That stretch is what "the
   waves read as rigid" actually was. In UV space the band snakes inside a flat
   quad and the geometry is untouchable.
2. **The wave is anchored in METRES of laid path** (`u_pathArc`, fed from
   `nodeUV[headNode]` and the head-minus-tail span). Crests now stand still on
   the trail the emitter laid; only `waveTravel` walks them. This is the single
   change that separates "flowing energy" from "a swinging rope".
3. **`TRAIL_WIDTH_ENVELOPE_ENERGY_BLADE`** — compact head, widest just behind
   it, long taper to a needle. The old SMOKE_WIDEN is its exact opposite and is
   why the effect read as a plume. The taper also pinches the wave's excursion
   toward the tail for free.
4. **HDR heat ramp** (`hotColor`, `hdrGain`): element hue at the fringe, burning
   to white-hot at the core, gain > 1 so bloom has something to catch. A single
   flat tint is what made the old band read as a neon sticker.
5. **Its own `onUpdate`.** It had been sharing `SmokeTrail_OnUpdate`, which was
   re-installing the fire updraft force field and the cloth spring on it every
   frame. See `LANDMINES.md`.

### Round 2 — one band was never going to be enough

Motion was accepted; the texture was not ("biên và đuôi liền mạch" — the edge and
tail stayed smooth and continuous instead of breaking into strands). Read the
original article (ryanzengvfx.blogspot.com, 2019-04) rather than guessing again,
and it names the thing the first pass got wrong: **Sin01/02/03 are three
independent wave fields, each sampling its own trail-pattern channel.** The
trail is a braid of overlapping bundles. A single analytic band, however well
eroded, can only ever be one smooth-edged ribbon.

Two halves, and the asset half was the bigger one:

- **The sheet.** `energy_wisp.png` was isotropic cloud noise in every channel.
  Cloud noise modulates a shape's brightness; it cannot split it into hairs.
  `gen_energy_wisp_texture.py` now builds actual filaments — narrow Gaussian
  ridges whose centres wander on integer-frequency sines in V (seamless), broken
  up lengthwise, combined **brightest-wins** (summing fills the gaps between
  hairs, and the gaps are the effect). Channel layout follows the article:
  R/G = trail patterns 1/2, B = flow distortion, A = dissolve. Measured: 5
  separate bright runs across the width in R, 17 in G, 42% of R near-black.
- **The shader.** Mode 2 has NO band function any more. Three detuned wave
  fields, three strand-bundle samples, combined with `max`, each with a wrap
  window so a crest cannot smear its strands back in from the far edge. Then the
  article's Phase 3 (zero-mean B-channel flow warp), Phase 4 (edge mask) and
  Phase 5 (A-channel dissolve). **Everything that frays is multiplied by
  `ramp`** — the along-trail coordinate — which is the article's "multiply by
  the U coordinate": tight coherent head, fanning frayed tail.

Also fixed: the A channel was generated into [0.5, 1.0], so no sane dissolve
threshold could bite it.

**Verified:** `trail_deform_test.c` (111 checks, 0 failures) — the bundle fits
inside the quad, the three fields diverge AND their spacing opens and closes
(a braid, not two rails), the disorder ramp is monotonic and zero at the
emitter, the arc anchor holds, plus source mirrors on the max-combine, the wrap
windows and the generator's filament construction. Both stages compile and LINK
clean under `glslangValidator` (the cross-stage `u_wavePhase`/`u_waveEnv`
sharing is deliberate — one program, one location).
**Not verified on screen:** this machine cannot bring up the Vulkan instance
(`FATAL: RLVK: instance creation failed`), so the look itself is unjudged. Bench
it at NEW FX -> ENERGY TRAIL.

**A test-writing note worth keeping:** "the crests travel with time" first
failed because it probed a single later time, and the two samples happened to
land either side of a trough. A moving wave and a frozen one are
indistinguishable at one sample pair — sweep and measure the swing.

**Live knobs** (`tuning.cfg`, hot-reload). Most useful first:
`energytrail_flow_str` (how hard the tail frays), `_strand_gain` (>1 thins the
hairs and opens the gaps), `_wave_spread` (0 stacks the three bundles into one
thick band, higher = wider braid), `_dissolve` / `_dissolve_soft`,
`_bundle_width`, `_bundle_wt`, `_wisp_mix` (weight of the fine-strand layer),
`_wave_amp` / `_wave_freq` (cycles per metre) / `_wave_travel`, `_tiling_x`
(sheet tiles per metre), `_pan_coarse` / `_pan_fine`, `_hdr_gain`, `_edge_soft`,
`_env_head`. `_wave_amp + _bundle_width` must stay under 1.0 or the outermost
bundle is cut flat by the edge mask.


### Round 3 — generalised, plus the bright-background bug

Three things, in the order they matter:

**1. The render split was wrong, and it is a cross-cutting bug.** The trail
looked faded over anything bright. Cause: `trail_deform.fs` emitted ONE colour
for both VFX passes, asserting in its own header that "both consume
`src.rgb * src.a`". Only additive does — `BLEND_ALPHA` is
`glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)`, not premultiplied — so the body
pass multiplied by alpha a second time, collapsed to ~intensity², contributed no
coverage, and the effect survived only in the additive pass, which by blend law
cannot create contrast over a bright destination. Fixed with `u_renderPass` +
one `ResolvePass()` resolver both material modes route through, and an authored
`TrailMaterialConfig.bodyOpacity`. **Promoted to `ENGINE_LANDMINES.md`** — any
module using `ScreenDistort_BeginVFXBody/Emission` can hit this.

Note the shape of the failure: the wrong belief was written down as a comment,
so every later reader confirmed it instead of checking it.

**2. Generalised into `VFX_ComposeStrandTrail(..., VFX_StrandStyle)`.** Styles
are a DATA table (`s_strandStyles`), not copied composers — a second composer
would re-acquire the render-split and arc-anchor bugs this one has been through.
`VFX_ComposeEnergyTrail` is now a thin alias; `VFX_ComposeSmokeStrandTrail` is
the other row. Split into its own `core/composition/common/vc_strand_trail.inl`:
it shares nothing with `VFX_ComposeSmokeTrail` (puffs on the classic CPU-cloth
pipeline) but a name, and living in that file is how it ended up calling the
smoke's shared init and wearing the smoke's updraft force field.

**3. A SMOKE style.** Same shader, three levers: `strandGain` **below** 1
(fattens each hair until neighbours merge into mass instead of reading as wire),
`bundleWidth` roughly doubled (magnifies the sheet's filaments in world space),
and `additive` off with `bodyOpacity` 0.92 — smoke must occlude; an additive
plume is a glow whatever colour you give it. Everything else slows down.

**Bench.** One entry per source `.inl` is the VFX bench's invariant, so both
styles share the NEW FX -> STRAND TRAIL button and are A/B'd live with the
`strandtrail_style` tunable (-1 = as spawned, 0 = energy, 1 = smoke). The
per-frame callback re-pushes blend mode, width envelope and tint as well as the
material, so the swap is complete without a respawn.

**Verified:** `trail_deform_test.c` 118 checks / 0 failures; full core suite
28/31 (the 3 failures are pre-existing, confirmed against a stashed tree).
Shader stages compile and LINK clean under `glslangValidator`.
**Not verified on screen** — no Vulkan instance on this machine.


### Round 4 — re-read the reference, found two real gaps

Went back to the algorithm write-up step by step instead of trusting the earlier
reading. Two genuine contradictions with the implementation, one deliberate
divergence.

**Gap 1 — Step 12, `frac()`, was missing entirely.** The reference folds its
noise UVs because `time * speed` grows without bound and float32 stops resolving
a fraction of a cycle. This implementation had TWO unbounded inputs: `u_time`
(`GetTime()`) and `metres` (the trail's cumulative `laidDist`). At one hour of
uptime `u_time * pan` is in the tens of thousands, where a float32 step is
~0.004 — visible stepping in the panned sheet and a juddering wave. Folded in
three places now: `fmod(GetTime(), 4096)` on the C side, `fract()` on every
time-driven pan, and `fract()` on the sine phase before it is scaled to radians.
The last one is EXACT, not an approximation: `sin(2pi(n+x)) == sin(2pi x)`.
Latent, not yet observed — bench trails are far too short-lived to reach it.

**Gap 2 — Step 13, the ALONG-trail colour ramp, was missing.** The reference
lerps head colour -> tail colour by `u`. This had only an intensity ramp (dense
strand centre -> hot core), which is a different axis and leaves the whole
ribbon one flat hue down its length. Both now stack: `tailColor` on
`TrailMaterialConfig`, authored per style as a blend toward the material's own
body tone (never toward black — a trail ramping to black reads as dirt, not as
cooling).

**Deliberate divergence — Step 4.** The reference NORMALISES the offset V back
into [-1,1] (compress). This clamps with a per-bundle window instead (cut).
Compressing squeezes the strand bundle as the wave peaks, which visibly thins
the trail at its crests; cutting keeps the bundle's shape and loses only what
leaves the quad, which the edge mask would remove anyway. Kept the cut, on
purpose.

Everything else lines up: centred V, sine offset, amplitude x U, three separate
sample sets (not three meshes), two-sample zero-mean flow noise, distortion x U,
edge mask, dissolve threshold rising with U.

**Verified:** `trail_deform_test.c` 125 checks / 0 failures — Steps 12 and 13
now have their own source mirrors. Full core suite 28/31 (same 3 pre-existing).

**Bench note:** after the file split, the old button index 6 is `SMOKE TRAIL` —
the ORIGINAL CPU-cloth puff trail, not this one. The strand trail is
`STRAND TRAIL`. Anyone reporting "it went back to the old ugly version" is
looking at the wrong button.


### Round 5 — two regressions I introduced, both found by the user

**Regression A: I moved the HDR gain into a branch nothing runs.** `main.c`
calls `DrawTrailEntitiesBody()` and never `DrawTrailEntitiesEmission()` — by
design, ribbons are HDR-lifted inside the body pass and picked up by ordinary
bloom, because a second full-res emission target duplicates the geometry per
trail. Round 3's "correct" split put the gain in the emission branch, i.e. in
dead code, and every trail went pale over dark and bright backgrounds alike.
The body pass has to carry BOTH jobs. Fixed, and `bodyOpacity` defaults raised
(0.45 -> 0.90 energy, 0.96 smoke): under a one-pass subsystem that knob is plain
visibility, not a body-vs-glow balance. Promoted to `ENGINE_LANDMINES.md`.

**Regression B: nested `fract()`.** Round 4's precision folds wrote
`fract(vs * 1.60)` where `vs` was already folded. `fract(fract(x)*k) != fract(x*k)`
— it changes each bundle's tiling RATE and chops the strands into short
mismatched runs, so the filaments read as fine mush. Now one shared unfolded
base with each consumer folding its own product once. Also promoted.

The arc length is now bounded on the C side instead (`fmodf(nodeUV, 8192)`),
where the modulus is a deliberate choice: ~30 min of continuous movement, and
the wrap re-phases the waves once — the honest cost of not letting the
arithmetic quietly decay.

`strandtrail_style` now logs on change (and warns on an out-of-range value),
per the "silent paths must announce themselves" rule — a knob that appears to do
nothing and a knob that is doing nothing look identical from the outside.

**Verified:** 130 checks / 0 failures, including mirrors pinning that `main.c`
runs the body pass and NOT the emission one, and that no fold sits on top of an
already-folded coordinate. Full suite 28/31 (same 3 pre-existing).


### Round 6 — deleted the old smoke trail, and found a real reason edits could vanish

**`VFX_ComposeSmokeTrail` is gone** (with `VFX_SmokeTrail_SetTexture`;
`VFX_SmokeTrail_Stop` became `VFX_StrandTrail_Stop` on the new file). It was the
classic CPU-cloth puff plume, and after the file split it occupied the VFX bench
slot the strand trail used to sit in — so the wrong effect kept getting judged.
Deleted rather than deprecated: two similar-looking buttons IS the bug.
`VFX_SURFACE_SMOKE_RIBBON` is now orphaned-but-kept (sheet is authored and
reusable); its provenance says so and the registry test no longer asserts a live
consumer for it.

**CMakeLists was missing `trail_deform.vs/.fs`.** Every shader the game loads at
runtime is `configure_file`'d into the build tree, and these two never were
(nor `trail_body.fs`). Whether that bites depends on the working directory the
binary is launched from — but the failure mode is nasty either way, and worth
recording: a missing deform shader does NOT report as a shader problem.
`TrailUsesDeformShader()` returns false, the trail silently renders through the
classic pipeline, and every edit to `trail_deform.fs` appears to do nothing.
That is indistinguishable from "the change had no effect", which is exactly how
it was reported. Now copied.

Also: `strandtrail_style` logs on change and warns on an out-of-range value, so
"the knob does nothing" and "the knob is doing nothing" stop looking alike.

**Verified:** 131 checks / 0 failures on the trail suite; full core suite back to
28/31 after fixing the registry test the deletion broke (same 3 pre-existing
failures). Still not verified on screen — no Vulkan instance on this machine,
which is why these last rounds have leaned on structural mirrors instead.


### Round 7 — the smoke style needed its own ASSET, not its own numbers

**Why `strandtrail_style=1` still looked like the energy trail.** Two reasons,
and the first one is the interesting one.

1. **Strand density lives in the asset.** `energy_wisp.png` has 34 coarse + 96
   fine hairs, authored thick and strong. Reference smoke is the opposite
   shape of problem: MANY MORE, THINNER, FAINTER strands piling up until the
   overlap reads as mass. No shader parameter turns 34 hairs into 300. The
   smoke row's `strandGain = 0.55` was not the wrong direction (below 1 lifts
   each hair's faint Gaussian skirt until neighbours touch — exactly right for
   smoke); it was the right operation applied to the wrong sheet, where it just
   fattened 34 thick hairs into glowing worms.
   New `scripts/gen_smoke_strand_texture.py` -> `assets/textures/smoke_strand.png`
   (registered as `VFX_SURFACE_SMOKE_STRAND`): 110 coarse + 260 fine hairs at
   about half the width, brightness skewed hard toward the faint end
   (`gain**2.2`), softer and wider density falloff, stronger wander. `surface`
   is now a per-style field — the two styles are NOT interchangeable on one
   sheet, and the struct says so.

2. **A live style swap never changed the texture.** The layer (and therefore
   the sheet) was chosen at spawn, so `strandtrail_style` swapped every number
   and left the other style's texture bound — producing neither look, which is
   indistinguishable from "the knob does nothing". `StrandTrail_EnsureSheet()`
   is split out for exactly this, and the per-frame callback now re-points
   `trail->layers` too.

Also clarified for the next reader: `-1` and `0` are IDENTICAL on the VFX bench,
because the bench spawns with `VFX_STRAND_ENERGY`. Only `1` changes anything
there. That is not a bug, but it looks like one.

**Verified:** 136 checks / 0 failures on the trail suite (new mirrors on the
per-style surface field and on the live layer re-point); full core suite 28/31
after updating the registry test, which asserted a literal profile id at a call
site that now resolves `st->surface`.


### Round 8 — the reference's R/G are a SHAPE, not a material

User posted the actual reference sheet. R and G are each **one complete smoke
streak**: a soft band, thick through the middle, tapering to nothing at both
ends, with a few curls rolling through it. The label is 拖尾紋理 — "trail
texture", singular. Its U axis maps ONCE across the whole trail.

Both previous sheets were built as tileable filament MATERIALS and the shader
tiled them by metres of laid path. That can only ever be a rope: uniform density
end to end, no head, no tail, no silhouette of its own. And three sin-offset
samples of a rope are three ropes — which is precisely what every version so far
rendered. The wave machinery, the flow warp, the disorder ramp and the dissolve
were all correct; they were being applied to the wrong KIND of asset.

- `gen_smoke_strand_texture.py` rewritten: R = 4 broad curls, G = 6 narrower
  ones, each a whole wisp running along +V, tapered at both ends and at both
  sides. Non-tiling by construction (drift frequencies are non-integer). B and A
  stay seamless — those two really are panned every frame.
- New per-style `stretchUV`. `false` = tile by metres (energy, a filament
  material). `true` = stretch once over the trail (smoke, a shape). This is
  decided by how the sheet was AUTHORED, not by taste: a tiled shape sheet is a
  rope, a stretched material sheet is a smear.
- Smoke row retuned for a shape sheet: `tailFadeA` 0.80 -> 0.94 (the sheet
  already tapers; fading twice thins the plume away), `wispMix` 0.60 (R and G
  are two DIFFERENT wisps now — blend them rather than favour one),
  `strandGain` 0.75, `flowStr` 0.65.

**The pattern worth naming:** four rounds of tuning maths that was already
right, because nobody checked what the source asset actually WAS. When an
effect's structure refuses to appear no matter how the parameters move, the
question is not "which knob" — it is "is the input the kind of thing this
algorithm consumes".

**Verified:** 141 checks / 0 failures (new mirrors on tile-vs-stretch and on the
generator building a wisp rather than hairs); full suite 28/31, same 3
pre-existing.


### Round 9 — the tail

Accepted look; the tail was the remaining coarse part. It ended on ONE segment:
all three bundles, the width envelope and the material fade all stopped at the
same place, so however soft the ramp over it, the eye still read a cut. Softness
blurs an edge, it does not remove one.

Three mechanisms, all per-style data (`tailStagger` / `tailDissolve` /
`tailNarrow`, packed into `u_tailShape`):

1. **Staggered ends.** The three bundles finish at `tailFadeA - stagger`,
   `tailFadeA`, `tailFadeA + stagger`. Applied PER BUNDLE, before the `max` —
   after it, a single cut would just clip the combined result again. The trail
   now unravels across a stretch: the earliest bundle thins out first and the
   last carries a few surviving wisps past it.
2. **Dissolve bites harder while dying.** The threshold already climbed with
   `ramp`; it now gets `+ tailDissolve * dying` over the final stretch, so the
   noise eats through the thin parts first and leaves the dense ones a moment
   longer — holes and islands instead of uniform dimming.
3. **Bundles narrow as they die.** A band that only fades keeps full width to
   the end and reads as a rectangle going transparent. Energy narrows hard
   (0.55 — ends as a few threads), smoke barely (0.78 — a plume stays broad).

All four tail knobs are live-tunable per style (`*_tail_stagger`,
`*_tail_dissolve`, `*_tail_narrow`, `*_tail_fade`).

**Verified:** 146 checks / 0 failures; full suite 28/31, same 3 pre-existing.
Not verified on screen — no Vulkan instance here.

---

## Pointers

| Question | Doc |
|---|---|
| What is the plan now? | `docs/VFX_PLAN.md` (Đợt H) |
| Why did Đợt E/F do what it did? | `docs/ELDEN_VFX_SPEC.md` (history) |
| What trap will I step on? | `docs/LANDMINES.md`, root `ENGINE_LANDMINES.md` |
| What does this API do? | `docs/API.md` (generated), `docs/API_GUIDE.md` (prose) |
| How do I add a VFX to the bench? | manifest entry by hand, then `scripts/sync_vfx_test.py` |
