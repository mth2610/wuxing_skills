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

### 1. Soft particles — the cut between particles and geometry is still visible

Cause is known and proven: `particle_lit.fs` may declare exactly ONE sampler
under rlvk; a second silently unbinds `texture0` (`ENGINE_LANDMINES.md`). The
C-side machinery is still there and inert (`s_locSoftFade == -1`).

Two routes, in order of value: fix the binding in `third_party/vulkan/` (Renderer
Agent — it also unblocks flow maps, `VFX_PLAN.md` H4), or ship a separate shader
variant used only where the binding behaves.

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

---

## Pointers

| Question | Doc |
|---|---|
| What is the plan now? | `docs/VFX_PLAN.md` (Đợt H) |
| Why did Đợt E/F do what it did? | `docs/ELDEN_VFX_SPEC.md` (history) |
| What trap will I step on? | `docs/LANDMINES.md`, root `ENGINE_LANDMINES.md` |
| What does this API do? | `docs/API.md` (generated), `docs/API_GUIDE.md` (prose) |
| How do I add a VFX to the bench? | manifest entry by hand, then `scripts/sync_vfx_test.py` |
