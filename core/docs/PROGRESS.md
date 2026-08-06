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

**`core/uv/` — UV deformation + surface flow (03/08/2026, landed).** `UV' = UV +
W(UV, t)` used to exist only inside `trail_deform.fs` mode 2. It is now a module:
`uv_deform.h` (4 layer kinds, 5 envelopes, presets, CPU mirror, `vec4[]` GPU
pack), `surface_flow.h` (N texture layers, tiling/pan/envelope/blend), `uv_fx.h`
(binds both), and `flow_map.h` **moved here from `core/`** with its API unchanged.
GLSL comes in two tiers — `shaders/uv_deform.glsl` and `shaders/surface_flow.glsl`
declare no uniforms, `shaders/uv_field.glsl` adds the packed blocks. Three
consumers: `trail_deform.fs` mode 2 (proved bit-identical over ~60k samples,
`core/tests/uv_deform_test.c`), `shield_shell.fs` (FlowMap → SurfaceFlow, its
`u_useFlow` branch is now a property of the flow), and `aura_shell.fs`.
Usage prose: `API_GUIDE.md` "UV module".

**`VFX_ComposeSmokeColumn` (03/08/2026)** — the consumer `core/deform` was built
for, and the answer to "is the reference technique reproducible here". It is
assembled entirely from parts that already existed: `TRAIL_SHAPE_TUBE` for the
volume, a `ForceField` (wind + curl) on the history nodes for buoyancy and
wander, `core/deform` via `tubeNoiseAmp` for the surface churn, the new
`volume_surface_*.png` sheets scrolled over it, and
`TRAIL_WIDTH_ENVELOPE_SMOKE_WIDEN` vs `UNIFORM` for funnel vs cylinder — the
shape question turned out to be one enum, not new code.

Not a variant of `VFX_ComposeVolumeTrail`: that one is what a MOVING emitter
leaves behind and sets `forceField = NULL` on purpose. A column's emitter does
not move, so the force field is the entire effect. Two archetypes, one primitive.

Bench: NEW FX tab, `SMOKE COLUMN`, index 23. Knobs `smokecolumn_rise/curl/
noise/scroll/alpha/tile` hot-reload from `tuning.cfg`.

**Three sandbox-generator gaps this surfaced**, all now fixed in
`scripts/sync_vfx_test.py`: a `trail` lifecycle was hard-coded to require a
`follower` fixture (a column is a trail with a STILL source); `infer_entry` had
no `static` branch, so it fell through to `persistent` and silently rewrote the
fixture declared in `LIFECYCLE_SPECS`; and `gen_draw_block` could only emit
`draw_call` or follower spawn blocks.

**And one landmine re-earned the hard way:** `VC_SmokeColumn_Update` was written
without `static`, so `ARCH_UPDATE_RE` did not match, `scan_archetypes` did not
see an archetype pair, and the generator moved the include to `common.inl` and
emitted NO update dispatch — the pool would never have ticked. It compiles
clean and the effect is simply absent. The `Update`/`Draw3D` pair IS the
declaration; `VC_SmokeColumn_Draw3D` is an empty stub for exactly that reason,
as `vc_volume_trail.inl`'s already was.

NOT VERIFIED ON SCREEN — no Vulkan instance on this machine. Specifically
unknown: whether a stationary emitter actually lays nodes (the design relies on
the last node RISING away from the source past `minVertexDistance = 0.045`), and
whether the result reads as smoke or as a churning pipe. The spawn log line
prints kind/shape/rise/curl/noise/scroll so the first question is answerable
from the log alone.

**`core/deform/` — mesh displacement (03/08/2026, landed).** The vertex-space
twin of `core/uv/`: `P' = P + D(P, N, mat, t)`. `MeshDeformField` with typed
layers (NOISE_CHANNEL / SINE / CURL), four direction modes — `NORMAL_SCALE`
(radius breathes, silhouette preserved) vs `NORMAL_OFFSET` (pushed off the
normal, wider and asymmetric) vs AXIS / TANGENT — and the envelope **shared with
`core/uv`** rather than re-declared, because the along-surface gate that weights
a wave, blends a texture layer and scales a displacement is one concept.

It is a MOVE, not a rewrite. The formula lived hard-coded inside
`core/geometry/pm_tube.inl` — two octaves, fixed channels, fixed weights, tubes
only, and the block was **copy-pasted into both builders**. Now one call site,
and any mesh can use it. Two shipped effects depend on those exact numbers
(trail tubes on the image source, the beam on the procedural lattice);
`core/tests/mesh_deform_test.c` proves both are bit-identical after the move,
plus a negative control showing the wrong grouping really would have differed.
`TubeMeshConfig.noiseField` is append-only, so every existing caller is untouched.

**No GLSL mirror, deliberately.** No vertex shader in this engine samples a
texture, so vertex texture fetch is unproven on rlvk; a mirror written now would
be a shader nobody can run and nobody can verify. The pack is GPU-shaped
(`vec4`-only, enum-as-float, location cache) so the move stays a port.

**Volume surface sheets (03/08/2026).** `scripts/gen_volume_surface.py` — one
parameterised script, three sheets (smoke / fire / steam), STRAND layout with
every channel TILE. They are a MATERIAL wrapped around a deformed volume, not a
SHAPE like `smoke_strand.png`: the silhouette comes from the geometry, the sheet
supplies detail only. Registered along with `volume_noise.png`, which had been
outside the registry entirely. The `NOISE` layout was added to
`TEXTURE_PACKING.md` for it — four decorrelated scalar fields, pure data.

All four are marked `orphaned` on purpose: their consumer, a composed volume
smoke/fire column, is not built. The validator prints them every configure until
one exists or they are deleted.

**A bug the seam instrument caught immediately:** the first generator scaled the
sample coordinate (`u * 1.7`), which breaks the integer-harmonic construction
that makes the sheet seamless — `u` is periodic with period 1, `u * 1.7` is not.
The wrap read 3.4x the interior variation. Channels are differentiated by
frequency RANGE now, and the measurement itself had to be fixed first: comparing
column 0 with column SIZE-1 for equality treats two *adjacent* columns as
duplicates and reports a healthy texture as broken. The honest test is the wrap
step against the texture's own local variation.

**Texture packing is now a hard rule (03/08/2026).** `assets/TEXTURE_PACKING.md`
defines what each RGBA channel of a VFX sheet may carry: four layouts (`STRAND`,
`FLOW`, `OPAQUE`, `FLIPBOOK`) plus an explicitly deprecated `SPLIT_LEGACY`
bucket, a machine-checkable channel grammar in every profile's `channels`
string, and nine numbered rules. `smoke_strand.png` is the reference sheet.

It is enforced, not merely written: `scripts/validate_vfx_surface_registry.py`
now parses the grammar and runs from `CMakeLists.txt` at configure time with
`FATAL_ERROR` on failure. Verified by deliberately breaking the manifest twice
(wrong slot; a packed profile regrowing a second file) — both blocked the
configure. `core/tests/texture_packing_test.c` pins the wiring itself.

**Why that mattered:** the validator already existed, was wired to nothing, and
had been failing on five profiles for an unknown length of time — two of them
being that `energy_wisp.png` and `smoke_strand.png`, the two best sheets in the
repo, were not catalogued in `assets/INDEX.md` at all. A rule nothing runs is
not a rule.

**Open debt, now counted:** the validator reports 11 constant channels across 5
`SPLIT_LEGACY` assets (`smoke_ribbon` flow+mask, `energy_tube`/`smoke_tube`/
`fire_tube` flow). Each is a standalone RG flow map wasting B and A — exactly
what the `FLOW` layout exists to reclaim, folding 9 files into 4. Not done here:
it changes live rendering and this machine cannot see the result.

**Consequence worth a decision:** `vc_shield_shell.inl` was `FlowMap`'s only C
consumer, so moving it to `SurfaceFlow` leaves the `FlowMap` *C* API
(`Create`/`Apply`/`Unload`, the two texture generators) with **zero callers**.
Kept anyway, because the brief chose "move, API unchanged" and deleting a
documented public API with its own test suite is a separate call. Its GLSL half
is very much alive — `FlowMap_SampleTwoPhase` is what `uv_field.glsl` calls for
layer 0. Delete or keep is the owner's; `core/docs/VFX_PLAN.md` §Flow map still
describes it as an unused module to build on, which is now more true, not less.

Two things deliberately left undone inside `trail_deform.fs`, both because that
shader's output had to be provably unchanged and neither is worth an optional
edit there: the `flow` variable at the Phase-3 block is computed from three
texture samples and **never used** (so the `flowStr` / `TrailMaterialConfig
.flowStrength` knob does nothing), and mode 1's pan at the bottom of the file is
still un-`fract()`ed while mode 2's is folded. Both are safe to fix in a change
that owns a visual check.

Not verified visually — this machine cannot create a Vulkan instance. The
"rendered result unchanged" claim for the trail is arithmetic (bit-identity plus
a negative control proving the test is not vacuous), not a look at the screen.

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

**Verification available to an agent:** `./scripts/run_core_tests.sh` (32 suites; `swept_trail`, `tube_frame`, `volume_trail` fail on main already),
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

## MỞ — Khói volume: mờ và loãng (06/08/2026)

**Trạng thái:** chốt tạm ở bộ cũ, chưa giải quyết. `tuning.cfg`:
`vol_depth_mode = 0 / vol_rim = 1 / vol_depth_pow = 2 / vol_density = 1.75`
— tức đúng công thức trước 06/08/2026. Người dùng thử qua cả ba mốc và bộ này
"vẫn là ổn nhất" về mặt *giống khói*.

**Còn nợ:** khói vẫn **mờ** và **loãng**, thỉnh thoảng dồn sang một hoặc hai
bên tuỳ góc camera.

**Đã LOẠI TRỪ được — đừng đo lại:**
- `viewPos` / không gian toạ độ: `volume_debug = 6` cho một màu V đồng nhất
  tuyệt đối trên toàn thân. `fragPosition` và `fragNormal` đi qua CÙNG
  `matModel` (`vs_header.glsl:34-35`) nên cùng không gian. Không phải
  ENGINE_LANDMINES §9.
- Attribute pháp tuyến thiếu (cái `silhouette_test.c` cảnh báo): `volume_debug
  = 5` cho hue quét trọn quanh thân ở đoạn dày. Attribute tới nơi.
- Biên độ churn: `floored` đã về vùng của cột khói (0-10%), không còn bão hoà.
- Neo (4 chỗ): bán kính / envelope deform / uốn trục / mặt nạ alpha đều đã
  cùng một `anchorAtTail`.
- Số lát lưới: đã tách khỏi số node lịch sử (`tubeGeomSegs = 24`), độ dốc giữa
  hai vành giảm đúng một nửa, xuống dưới mức của cột khói.

**Hai hướng chưa thử:**
1. **Số hạng độ dày không phải là chỗ sửa.** Cả `(1-|N·V|)^p`, `|N·V|^p` và
   tổng có trọng số của hai cái đều đã thử — không cái nào vừa đặc vừa tự
   nhiên. Nghi ngờ: cấu trúc phải đến từ `pattern` (tích hai sheet noise) chứ
   không phải từ số hạng hình học, và pattern hiện đang quá đều/quá mờ. Kiểm
   bằng `volume_debug = 3` (vẽ riêng `pattern`) — nếu nó gần như phẳng thì đó
   là chỗ sửa, và câu trả lời nằm ở tấm sheet / `uvMetresPerTile`, không nằm
   trong shader.
2. **Một vỏ không làm ra khối.** Ống hiện là MỘT lớp vỏ kín, cull mặt xa. Khói
   thật là nhiều lớp chồng. Rẻ nhất để thử: hai ống lồng nhau lệch pha churn
   (đã có sẵn `TrailLayer.widthMul` 1.0/0.72 nhưng cả hai dùng CHUNG một mesh,
   nên chúng không phải hai lớp thật — chỉ là hai lần vẽ cùng một hình).

**Sọc xen kẽ ở đuôi mảnh:** chưa kết luận. `volume_debug = 5` vẽ TRƯỚC
`discard` nên chồng cả hai mặt ống — nghi là artefact của chính debug view.
Đã thêm `volume_debug = 8` (cùng pháp tuyến, vẽ SAU `discard`) để phân định:
5 có sọc mà 8 không thì hình học sạch. Chưa ai chạy.

## MỞ — VolumeTrail bản "energy" vẽ ra vệt ĐEN ĐỤC (06/08/2026)

**Không sửa ở đây, và cố ý.** Đổi nó là đổi một hiệu ứng đang chạy mà không ai
yêu cầu đổi — đúng bài học của mục "một bản sửa đúng vật lý vẫn là hồi quy"
trong `docs/LANDMINES.md`. Ghi lại để lần sau không phải chẩn đoán lại.

**Triệu chứng:** thân ống có màu lai kỳ quặc, và những chỗ đáng lẽ trong suốt
lại ra ĐEN ĐỤC. Người dùng phát hiện trên beam bước 1 rồi nhận ra volume trail
cũng vậy.

**Nguyên nhân — vi phạm hợp đồng KÊNH TEXTURE, không phải blend.**
`core/trails/shaders/trail_volume.fs` khai hợp đồng ngay trong mã nguồn của nó:

> "A = coverage in the OPAQUE layout; RGB is grey luminance **kept grey so the
> caller's tint survives**"

rồi tính `colour = s1.rgb * vColor.rgb * colDiffuse.rgb`. Đối chiếu
`assets/vfx_surface_profiles.json`:

| sheet | khai báo kênh |
|---|---|
| `VOLUME_SMOKE` / `FIRE` / `STEAM` | "**GREY luminance in RGB** so the caller's VFX_Material tint survives" ✅ |
| `ENERGY_TUBE` (`energy_volume.png`) | "tintable energy filaments" — **không hứa gì về grey** ❌ |

`energy_volume.png` là sheet MÀU thật: sợi sáng trên nền tối, còn kênh A
(coverage) vẫn cao **giữa** các sợi. Nhân với tint thì hai chuyện xảy ra cùng
lúc: sợi ra màu lai giữa sheet và nguyên tố, và nền tối giữa các sợi ra **đen
đục** — vì alpha cao trong khi RGB gần 0. "Trong suốt → thành màu đen" không
phải lỗi blend; fragment đó thật sự đục và thật sự đen.

`consumers` của `ENERGY_TUBE` trong registry chỉ có một dòng:
`VFX_ComposeVolumeTrail energy`.

**Ba cách sửa, chưa chọn:**
1. Đổi consumer sang `VOLUME_FIRE` (grey, đúng hợp đồng) — rẻ nhất, mất "vân
   sợi năng lượng" của sheet cũ. Đây là cái beam đã làm.
2. Sinh một sheet năng lượng GREY-luminance mới (`scripts/gen_volume_surface.py`
   đã làm đúng việc đó cho smoke/fire/steam) — đúng nhất, tốn asset mới.
3. Cho `trail_volume.fs` tự lấy luminance của `s1.rgb` — chữa được màu lai
   nhưng **không** chữa được vệt đen (luminance ở đó cũng ~0 trong khi A vẫn
   cao), nên một mình nó không đủ.

**Đã chặn tái diễn:** `core/tests/beam_geometry_test.c` giờ đọc thẳng
`assets/vfx_surface_profiles.json` và bắt buộc sheet mà beam dùng phải khai
"GREY luminance in RGB". Hiệu ứng khối tiếp theo chọn nhầm sheet sẽ đỏ ngay ở
suite chứ không phải sau một vòng build-and-look.

## MỞ — Hai lỗi CŨ của trail, xác nhận lại trên beam (06/08/2026)

Chủ dự án nêu: "lỗi răng cưa, lỗi blend màu của trail (**trước giờ chưa sửa
được**)". Không phải hồi quy của P4 — beam chỉ là chỗ chúng lộ rõ, vì beam
mảnh, dài và nhìn xiên.

### A. Răng cưa ở mép — và số đo đã LOẠI hướng sửa hiển nhiên nhất

Phản xạ đầu tiên là tăng số lát quanh thân. `core/tests/silhouette_test.c`
(`Test_CombinedAndResolution`, cull + p=2) đo sẵn:

| radial | hardness |
|---|---|
| 8 | 0.130 |
| 16 | 0.125 |
| 24 | 0.119 |
| 32 | 0.120 |
| 48 | 0.117 |

Toàn dải đã dưới `HARD_LIMIT` 0.15, và 8 → 48 chỉ nhích 0.013. **Tăng radial
không phải câu trả lời** — đừng làm lại phép thử đó.

**NHƯNG biết giới hạn của phép đo này:** harness dựng một hình trụ NGẮN nhìn
NGANG (`BuildTube(&m, 0.55f, 1.1f, ...)` — bán kính 0.55 m, nửa chiều dài
1.1 m). Beam là trụ rất DÀI, rất MẢNH, nhìn XIÊN. Đó là một tỉ lệ khác hẳn và
harness chưa mô hình hoá nó, nên bảng trên chứng minh "radial không cứu được
ca ĐÃ ĐO", không chứng minh "răng cưa của beam không liên quan hình học". Bước
tiếp theo nếu ai truy tiếp: thêm một ca trụ dài/mảnh/xiên vào `silhouette_test.c`
TRƯỚC khi đụng vào bất cứ thứ gì — đây đúng là loại câu hỏi số học mà
`core/CLAUDE.md` §1 nói không được trả bằng ảnh chụp.

### B. Blend màu — nhiều lớp alpha không sort theo độ sâu

Trail vẽ từng layer nối tiếp không có depth sort giữa chúng, nên hai lớp alpha
trên CÙNG một mesh không thể composite đúng — chỉ chồng thêm một pass nữa.
Phạm vi thật của bản sửa là sorting trong `trail_system.c`, ảnh hưởng mọi
trail; chưa làm.

**Đã làm được phần không gây hồi quy:** beam bỏ lớp alpha thứ hai (còn 1 lớp
body + 1 lõi additive). Việc này KHÔNG sửa lỗi — nó chỉ khiến beam thôi đóng
góp vào lỗi. Ghi rõ để không ai đọc nhầm là đã xong.

### C. Lõi — đơn giản hoá theo yêu cầu chủ dự án

Lõi giờ: không sheet (`texture = NULL` → trail_system rơi về flat trắng),
không scroll, không volume shader, 8 radial x 8 lát. Nhiệm vụ duy nhất của nó
là sống sót cú nhìn dọc trục; mọi thứ thêm vào nó là thêm một pass alpha không
sort chồng lên thân mà không được thêm chút silhouette nào. Đã khoá bằng
assert trong `core/tests/beam_geometry_test.c`.

## Bằng chứng TRỰC TIẾP cho "một vỏ không làm ra khối" (06/08/2026)

Chủ dự án, khi nhìn beam bước 1: *"có những góc nhìn, chỉ thấy 1 nửa vỏ năng
lượng, có hay không đây là 1 lỗi từ lâu gây ra khói mỏng và loãng trong smoke
trail"*.

**Có, cùng một gốc** — và quan sát này là bằng chứng trực tiếp cho hướng 2 của
mục "Khói volume: mờ và loãng" ở trên, nên hướng đó được nâng lên thành hướng
CHÍNH. Nhưng phải tách hai cơ chế, vì chúng cộng lại chứ không phải một:

1. **Cull mặt xa** — `trail_volume.fs`'s `if (facing < 0.0) discard;`. CỐ Ý và
   bắt buộc: `silhouette_test.c` chứng minh không có nó thì tia sượt rìa cắt
   qua vô số facet và alpha ở rìa CAO hơn ở giữa. Cái giá là ta chỉ bao giờ vẽ
   NỬA GẦN của vỏ.
2. **Số hạng rim rỗng ruột** — với `vol_depth_mode = 0` (bộ đang chốt), độ đục
   ĐÚNG BẰNG 0 tại tâm thân và đạt đỉnh ở 0.9 bán kính
   (`volume_optical_depth_test.c`). Nửa vỏ gần đó vì thế chỉ hiện ra thành HAI
   VỆT RÌA, không phải một mặt.

Ghép lại: cả một khối khí đang được biểu diễn bằng **hai đường viền của một
nửa vỏ**. Đó chính xác là "mỏng và loãng", và nó giải thích luôn vì sao tăng
`vol_density`/`alpha` không bao giờ đủ — chúng làm hai đường viền đó đậm hơn
chứ không thêm vật chất vào giữa.

**Vì sao không sửa bằng `vol_depth_mode = 1`:** đã thử, cả ba mốc; nó lấp được
giữa nhưng đọc ra "như sáp" và mất tự nhiên. Cơ chế (2) không sửa được một
mình vì cơ chế (1) vẫn chỉ cho một lớp.

**Hướng còn lại, chưa thử:** nhiều vỏ LỒNG NHAU lệch pha. `MeshDeformField` là
struct rời nên hai ống cùng path với `timeOffset` khác nhau là hai lớp khói
THẬT — khác hẳn `TrailLayer` (nhiều lần vẽ cùng MỘT mesh, không thêm chiều sâu
nào, và còn nuôi lỗi blend không-sort). Đây là chỗ nên tiêu tiền tiếp theo cho
khói.


## MỞ — `|N·V|` của volume tube bị ĐẢO NGƯỢC → `docs/VOLUME_SHADING_HANDOFF.md`

Toàn bộ điều tra 06/08/2026 đã được viết thành một file handoff riêng:
**`core/docs/VOLUME_SHADING_HANDOFF.md`**. Đọc file đó trước khi đụng
`trail_volume.fs`, cách xử lý không gian toạ độ, hay pháp tuyến của `PMTube`.

Tóm tắt một dòng: trên một hình trụ trơn đứng yên, `|N·V|` đo ra **ngược** —
≈0 ở tâm silhouette, ≈1 ở rìa — trong khi CPU đọc thẳng cùng mesh và cùng
camera lại cho dải đúng (`0.049..0.990`). Mesh, pháp tuyến, attribute, uniform
`viewPos` và đường debug đều đã được loại trừ bằng phép đo. Không gian của
`fragPosition` đã xác định được là **VIEW space** (bác bỏ comment cũ trong
shader). Mâu thuẫn còn lại và ba ứng viên nằm ở §4 của file handoff.

Phần đắt nhất của file đó là **§5 — chín cái bẫy DỤNG CỤ ĐO** đã phát hiện
trong phiên, mỗi cái đều cho ra một ảnh hợp lý nhưng hoàn toàn sai. Đọc §5
trước khi viết bất kỳ debug view nào cho module này.
