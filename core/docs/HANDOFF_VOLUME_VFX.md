# Handoff — volumetric VFX, session of 30/07/2026

Paste the block at the bottom into a new session. Everything above it is context
for a human deciding whether the handoff is honest.

---

## What landed, and what is unverified

**Landed and tested (16/16 headless suites green, build clean):**

- `TRAIL_SHAPE_TUBE` — the trail system can sweep a volume, not only a strip.
  Same history, width curve, layers, material UV and cloth; only the
  cross-section changes. `TRAIL_SHAPE_RIBBON = 0`, so every existing caller is
  byte-identical.
- **Parallel-transport frame** in `core/geometry/pm_tube.inl`, opt-in via
  `TubeMeshConfig.useTransportFrame`. The old per-slice world-up frame shears the
  texture by 14% of a wrap on a curving path and snaps a quarter turn when the
  tangent passes vertical; measured in `core/tests/tube_frame_test.c`.
- `ProceduralMesh_DrawTubeEx(data, uvLengthScale, uvOffset)` — the tube can
  finally SCROLL. The original computed `v = i/segments` with no offset, so a
  tube could never be seen to flow.
- Vertex deformation sampled from `assets/textures/volume_noise.png`.
- `core/shaders/flow_map.fs` — displacement is now **centred** on the undisplaced
  phase. It was not, which inverted the technique: the layer at full weight was
  always the one at maximum stretch. **Still zero consumers.**
- `FlowMap_CreateWithTrailTexture` — a tiling along-axis flow field. The only
  previous generator was a vortex, which cannot tile.
- `scripts/gen_volume_trail_textures.py` — smoke / fire / energy sheets, RGB
  noise, three flow maps, a gradient ramp. Seamless on both axes **by
  construction** (wrapping lattice), with the seam error measured against each
  texture's own local variation and printed.
- Primaries extracted: `VFX_ComposeCoreGlow`, `VFX_ComposeEnergyOrb`.
  Composite: `VFX_ComposeProjectile`.

**VERIFIED ON SCREEN 30/07: the tube renders correctly.** Getting there took
FOUR independent causes of one symptom ("the tube renders flat"), each found and
fixed in sequence, and the effect looked identical after the first three:

1. a ribbon sheet wrapped around a tube leaves a transparent seam down its whole
   length — on a cylinder, `u = 0` and `u = 1` are the same line;
2. that fix was applied to one layer and not the other, so a complete inner tube
   sat inside a split outer one;
3. `rlDisableBackfaceCulling()` was not batch-flushed, so the tube was queued
   with culling off and drawn after it was switched back on — exactly one wall of
   every ring survived;
4. **the one that actually did it:** re-orthogonalising the transported frame
   subtracted a vector from itself whenever the carried frame drifted parallel to
   the tangent, and `Vector3Normalize` of ~zero is garbage. The whole cross
   section collapsed to a LINE. No NaN, no crash, no log — just a tube drawn as a
   plane. Guarded in `core/geometry/pm_tube.inl`; degenerate slices fall back to
   a reference frame, which is less stable but is always a real section.

The roundness instrument added while hunting (4) is still in `trail_system.c` and
still worth keeping: it prints the built section's aspect once per trail, which
separates "the section collapsed" from "the tube branch never ran" — two causes
that are indistinguishable on screen.

## The pattern worth carrying, stated plainly

Almost every defect this session was one of three shapes, and each cost multiple
rounds because it was guessed at rather than measured:

- **A moving origin.** Anything anchored to an end that itself moves reads as a
  painted-on pattern being dragged. Hit in the UV (`arc from the tail`), then
  again in the vertex noise (`t = i/segments`). The fix is always a MATERIAL
  coordinate — a label stamped once and never revisited.
- **A closed section needs wrapping, not cross-fading.** Sheets, flow maps and
  noise all had to be periodic *by construction*; cross-fading a seam blurs the
  detail, and for a direction field it averages opposing vectors into no flow.
- **Silent clamps and silent early-outs.** A style clamped to a stale enum bound,
  a guard reading the wrong end of an array, a generator whose idempotence check
  looked at its trigger instead of its output. Each produced a *plausible* wrong
  result, which is far more expensive than an absent one.

When a symptom survives a correct fix, suspect a second cause rather than a bad
fix. That happened three times.

---

## PROMPT FOR THE NEW SESSION

```
You are the Core Agent for this C/Raylib (Vulkan via rlvk) game.

READ EXACTLY THIS, IN ORDER, AND STOP:
  1. core/CLAUDE.md
  2. core/docs/VFX_PLAN.md — §4 only (the PRIMARY catalogue)
  3. core/docs/LANDMINES.md and ENGINE_LANDMINES.md — skim headings, read any
     entry you are about to touch the subject of
Do NOT read core/docs/PROGRESS.md, ELDEN_VFX_SPEC.md, build/, _deps/, or
android.wuxing_skills/.

CONSTRAINT THAT SHAPES EVERYTHING: I cannot run the game for you.
./build/wuxing dies with "FATAL: RLVK: instance creation failed" outside my
graphics session; headless tests run fine. Every runtime question must go
through a log line, a tunable, or arithmetic. If you need me to look at
something, say so directly and tell me exactly what to look for.

Commands: `cmake --build build -j4` and `./scripts/run_core_tests.sh`
(all suites must be green before you report anything done).

THE TUBE WORKS. It was verified on screen on 30/07 after four separate causes of
one symptom; do not go looking for a fifth. If it ever looks flat again, the
roundness line already in trail_system.c separates "the section collapsed" from
"the tube branch never ran" in one run — those are the only two possibilities and
they need different fixes.

TASK — work Part 4 §4.3 of VFX_PLAN.md in order: P1 VolumeTrail,
P2 ConvergeMotes, P3 DebrisShards, P4 Beam, P5 ShockRing, P6 PortalDisc.
Each spec there names its API, its build notes and its definition of done.

RULES THAT ARE NOT NEGOTIABLE (each was paid for):
  - Blend law: occludes -> BLEND_ALPHA + lit; emits -> BLEND_ADDITIVE + unlit.
    Glowing smoke is TWO draws.
  - Emission is a RATE with a carried fraction, never a count per frame call.
  - Thickness is a ratio against the thing's OWN length; spacing in METRES.
  - Colours and force fields from VFX_Material; motion from vc_motion.h.
  - C99, static pools, no malloc. 1 unit = 1 metre. rlgl/raylib only.
  - rlDrawRenderBatchActive() before AND after ANY rlgl state change the queued
    geometry depends on — depth mask, depth test, blend mode, AND culling.
  - Tier gates may only ever clamp DOWN. A gate that can only turn a thing off
    is not a tier; do not add one.
  - No camera shake unless I ask.
  - Every new VFX: a manifest entry added BY HAND to
    scripts/vfx_test_manifest.json, then `python3 scripts/sync_vfx_test.py`.
    NEVER hand-edit a @gen: block — the generator's early-out is computed from
    its trigger, and a hand-written include silently suppresses the dispatch.
  - Every new VFX: a headless test for whatever part of it is arithmetic. A
    mirror needle must pin CODE, not FORMATTING (collapse whitespace both
    sides), and a negative needle must carry punctuation only code can have or
    it matches the comment explaining the fix.

BEFORE WRITING A SECOND IMPLEMENTATION OF ANYTHING, GREP FOR THE FIRST.
This happened twice in one session: a composition grew its own history ring,
sample clock, cloth and layered draw beside core/trail_system.h; then the trail
system grew its own tube beside ProceduralMesh_BuildTubeAlongPath. Both times
the existing one was better and had one fixable defect.

Answer in Vietnamese, terse, lead with the result, cite path:line.
```
