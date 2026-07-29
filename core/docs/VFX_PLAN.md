# Đợt H — Hình khối. The active VFX plan.

> Written 28/07/2026, replacing `ELDEN_VFX_SPEC.md` as the plan of record. That
> document is now history: read it only for context on why Đợt E/F did what it
> did. Everything still binding from it is repeated here.

---

## 0. Where we actually are

F0 (the purge) left **eleven** compositions. Here is what they are built from,
counted rather than estimated:

| Primitive | Public API | Survivor compositions using it |
|---|---|---|
| Particle system | `core/particle_system.h` | **10 of 11** |
| Ribbon strip | `core/ribbon_strip.h` | 3 (slash, shaft, rune ring) |
| Ribbon energy field (crossed planes + scroll) | `DrawRibbonEnergyField` | 1 |
| **Trail system** (18 entry points, attach-to-transform, orbit followers, ribbon modes) | `core/trail_system.h` | **0** |
| **Procedural mesh** — tube, wave plane, crystal (53 entry points) | `core/geometry/procedural_mesh_utils.h` | **0** |
| **Path spline** | `core/path_spline.h` | **0** |
| **Flow map** | `core/flow_map.h` | **0** |
| **Crystal / Plasma materials** | `core/material/material_system.h` | **0** |
| Mesh adjacency / on-mesh spawning | `core/mesh_adjacency.h` | 1 |

### 0.1 Why it does not look like Elden Ring

**Because almost everything we own is a camera-facing sprite.** That is the whole
diagnosis, and the table above is the evidence.

A sprite cannot do the four things that make an ER effect read as part of the
world:

1. **Hold a silhouette from any angle.** A billboard is the same shape from
   everywhere; a swept blade mesh is a thin edge from the side and a broad plane
   from the front. That change *is* the sense of a real object moving.
2. **Intersect the world.** Geometry cuts into terrain, wraps a body, follows a
   slope. Sprites float in front of it, which is why our effects read as *decals
   of* an effect rather than the effect.
3. **Be lit as a surface.** F1 gave particles a fake normal, and it helped. But a
   real mesh has real normals, takes the arena's light, and its bright side moves
   as it turns. That is most of what "expensive" looks like.
4. **Persist coherently.** A trail with history has a shape that records where
   something has been. Re-emitting sprites along a path only approximates it, and
   loses the moment the emitter turns hard.

Đợt E/F was the right work — the blend law, lit particles, the sequencer, the
material table, the tier budget are all load-bearing and none of it is wasted.
But it built the **particle** half of the toolkit and left the **geometry** half
untouched. Đợt H is that half.

### 0.2 What is actually left of E/F

| Task | State |
|---|---|
| F0 purge, F1 lit particles, F2 smoke, F3 fire, F4 aura | **done** |
| E1 post-FX, E2 VFX light, E3 sequencer, E5, E6 | **done** |
| **E0 baseline capture** | **dead — do not do it.** Its eight named subjects were deleted by F0. There is nothing left to baseline against. Đợt H sets its own acceptance per task instead. |
| **E4 asset library** | **mostly done — and it is no longer the bottleneck.** See H6 for the corrected accounting. |
| **E7 retrofit checkpoint** | **not started.** Carried into H8, and it is still a stop-gate. |
| E8 platform/perf | tier gate + instrument done; **device verification outstanding** (owner-only: rlvk visual tier, 60 fps PC with everything on, A33). |

### 0.3 Rules that bind every task here

Carried forward, unchanged, because each was paid for:

- **C99, static pools, no `malloc`/`free`.** Fixed arrays with a `MAX_*`; on
  overflow recycle the oldest.
- **Meter scale.** 1 unit = 1 m. Never write 1cm-scale numbers.
- **Backend-agnostic draws** — `rlgl`/raylib only, never raw GL or Vulkan.
- **The blend law** (`core/particle_system.h`): occludes → `BLEND_ALPHA` and lit;
  emits → `BLEND_ADDITIVE` and unlit. Glowing smoke is TWO draws.
- **Emission is a RATE, never a count per call**, in any function called per
  frame. Derive it from a live-count target and carry the fraction.
- **A thing's thickness is a ratio against its own length**, and spacing is in
  METRES, not array indices (`core/docs/LANDMINES.md`).
- **Colours and force fields come from `VFX_Material`**, motion from
  `vc_motion.h`. Hard-coded colour only for a deliberate identity break, with a
  comment saying so.
- **Every new VFX gets a bench entry** (`scripts/vfx_test_manifest.json` by hand,
  then `scripts/sync_vfx_test.py`) and a **headless test** for whatever part of it
  is arithmetic.
- **Tier budget**: anything screen-wide or fill-hungry is gated by
  `GfxQuality_Get()`, and the gate may only ever clamp DOWN.
- **No camera shake unless the owner asks.**

---

## Part 1 — The geometry half

### H1. `VFX_ComposeSweptTrail` — the swept weapon/body trail

**Owner:** Core Agent · **Size:** M · **Depends on:** nothing · **Do first**
> **LANDED 29/07/2026** — `core/composition/common/vc_swept_trail.inl`, bench
> entry `SWEPT TRAIL` (figure-eight, cycles the three styles), 57 assertions in
> `core/tests/swept_trail_test.c`. Two DoD items are outstanding and belong to
> other owners: no skill consumes it yet (Skills Agent, folded into H8).
> **The BLADE style renders as a dotted line and is PARKED unsolved** after four
> failed attempts — four causes are ruled out by measurement and the remaining
> suspect is named, in `docs/PROGRESS.md` §0. RIBBON and FILAMENT are correct.
>
> One design decision worth carrying into H2-H5: the caller's `width` is a
> CEILING and the drawn width is capped against the length the tip actually
> travelled (1:20 / 1:10 / 1:40). That is the thickness rule from LANDMINES
> turned into code rather than a comment, and it is what stops a hard turn from
> producing a blob.

The single largest gap between our look and ER's, and the primitive is already
written and shipping-quality: `core/trail_system.h` has attach-to-transform,
orbit followers, `RibbonMode`, arc-length UV, inner/outer double strips — and
**not one composition uses it**.

```c
// Attaches to a moving transform and owns a trail handle.
int  VFX_ComposeSweptTrail(const Matrix *followTransform, VC_MaterialId mat,
                           float width, float lifetime, VFX_TrailStyle style);
void VFX_TrailSetWidth(int handle, float width01);   // ramped, for wind-down
void VFX_KillSweptTrail(int handle);
```

`VFX_TrailStyle` is the point of the task: BLADE (thin, hard outer edge, the
`SweepSlash` mask reused as a scrolling sheet), RIBBON (soft, wide, cloth-like),
FILAMENT (several thin strands at slightly different lags — the "many threads"
look ER uses for magic).

**Why a composition and not just calling the trail system:** because the trail
system takes a texture and a config and knows nothing about elements, the blend
law, or the tier budget. Every skill wiring it by hand would re-derive those, and
they would each get it slightly wrong.

**DoD:** bench entry driving a transform on a figure-eight (a trail that looks
right on a straight line and breaks on a hard turn is the classic failure);
headless test for the width envelope and the lag schedule; one skill using it.

---

### H2. `VFX_ComposeGroundWave` — geometry that follows the terrain

**Owner:** Core Agent · **Size:** M · **Depends on:** H1 (shares the material work)

`ProceduralMesh_BuildWavePlane` exists and is unused. An expanding ring of
displaced ground-conforming geometry is the shockwave ER uses everywhere, and it
is exactly what a flat additive decal cannot do: it rises, it has a lip, it
catches the light on its inner face.

```c
void VFX_ComposeGroundWave(Vector3 center, VC_MaterialId mat, float radius,
                           float t01, GroundHeightSampleFn heightFn, void *ud);
```

The height callback already has a type in `procedural_mesh_utils.h` — the map
layer supplies it, so the wave follows slopes instead of clipping through them.

**DoD:** visibly conforms on a sloped map; headless test that the ring's radius
and lip height follow the curve and that the vertex count is bounded.

---

### H3. Element shells on real meshes — put the materials to work

**Owner:** Core Agent · **Size:** M

`CrystalMaterial`, `PlasmaMaterial` and the instanced crystal path are written,
documented, and used by **zero** survivors (the ice crystals the owner restored
are pre-E code, and are the proof this works: they are the best-looking thing in
the water skills right now).

Deliver three compositions over the existing mesh + material pair:

- `VFX_ComposeCrystalGrowth(pos, mat, count, seed, t01)` — the restored ice burst
  generalised to any element, with `growProgress` as the shader-side reveal.
- `VFX_ComposePlasmaShell(pos, mat, radius, t01)` — a membrane for wards and
  charge-ups, replacing the sphere-of-sprites approach.
- `VFX_ComposeShardStorm(...)` — instanced shards with per-instance transforms
  (`DrawMeshInstanced` already used by the crystal burst), not sprites.

**Landmine already known:** per-instance uniform changes stress rlvk's UBO arena
(`ENGINE_LANDMINES.md` §8). Carry per-instance variation in the transform and the
vertex colour, never in a uniform.

---

### H4. Flow-mapped surfaces

**Owner:** Core Agent · **Size:** S · **Depends on:** H2/H3 (needs a surface)

`core/flow_map.h` is unused. Real UV advection is what makes energy look like it
is *moving through* a surface rather than sliding across it.

**The constraint that decides the design:** under rlvk a second `sampler2D` in a
core shader unbinds `texture0` (`ENGINE_LANDMINES.md`). A classic flow map is two
samplers. Two ways out, in order of preference:

1. **Fix the binding** in `third_party/vulkan/` (Renderer Agent) — this unblocks
   soft particles too, which are parked on the same landmine. Ask first; it may
   be a small fix, and it is worth more than either feature.
2. **Compute the field** instead of sampling it (2 fbm octaves warping the UV
   before a single fetch). Proven — it is what the deleted spirit stream did.

**DoD:** whichever route, a headless test asserting the shader declares exactly
as many samplers as the backend can honour.

---

### H5. Projected decals that follow contour

**Owner:** Core Agent + Map Agent · **Size:** S

`DecalSystem_AddStreak` and `AddFlowEx` exist and no survivor uses them. Scorch,
frost, and rune marks that lie ON the ground — not a quad hovering above it — are
half of what sells an impact after the flash is gone.

**DoD:** a decal on a slope with no visible float or clip; the impact package's
decal beat routed through it.

---

## Part 2 — The half that is not code

### H6. The authored mask library (was E4) — **downgraded, and here is why**

**Owner:** owner / asset pipeline · **Size:** S (was L) · **Not a blocker**

The old spec called this "the single biggest quality gap". That was true when it
was written and it is **not true now**, so the plan is corrected rather than
repeated. Counting what is actually on disk:

| Group | State |
|---|---|
| Flipbooks | **done** — `fire_puff_8x8`, `fire_atlas_8x8`, `smoke_puff_8x8`, `smoke_atlas_8x8`, `dust_puff_4x4`, `flame_tongue_8x8` |
| Sigils / glyphs | **done** — `rune_glyphs_0..3` + `rune_line`, consumed by `VFX_ComposeRuneCircle` |
| Slash masks (`arc_slash_*`) | missing, **and generated instead** — `VFX_ComposeSweepSlash` bakes its own |
| Glints (`glint_star_*`, `streak_aniso`) | missing, **generated fallback in use** |
| Erosion masks | missing, `noise.png` used instead |
| Distortion normal maps | missing, **no generated substitute** |

**What changed the assessment:** Đợt E/F proved that a *generator script* is a
first-class way to make these sheets, not a stopgap. It can assert what a painted
file cannot — that the sheet tiles (measure the wrap seam against the typical
neighbour delta), that it is mostly empty (coverage percentile), that its axis
convention matches the mapping. `scripts/gen_rune_textures.py` and the slash mask
baked into `vc_sweep_slash.inl` both work.

**So the remaining ask is small and specific:**

1. **Tangent-space distortion normals** (`distort_normal_swirl`, `_shock`) — the
   one group with no procedural substitute, because `ScreenDistort` wants a real
   normal field, not a scalar mask.
2. **Hero-quality painted slash/glint art**, *if and only if* the owner judges the
   generated versions insufficient after H1 lands. That is a taste call to make
   with the trails on screen, not a task to schedule blind.

Everything else in the old E4 table is either done or deliberately generated.

**Rule that survives from it, and belongs in every INDEX entry:** state the axis
convention. A sheet authored for a flat beam produces rings when wrapped on a
tube, and no shader parameter can undo it.

---

## Part 3 — Proof

### H7. Platform close-out (was E8's remainder)

**Owner:** owner + Renderer Agent · **Size:** S

rlvk visual tier, 60 fps on PC with everything on, A33 at its tier, zero new
validation errors. `postfx_perf_log = 1` and `particle_perf_log = 1` are the
instruments. Everything headless is already green from the agent side.

### H8. The retrofit checkpoint (was E7) — **the stop-gate**

**Owner:** Skills Agent (Core supports) · **Size:** M · **Depends on:** H1–H5

Rebuild **three** skills — one Fire, one Water/Wood, one Taiji — entirely from
the Đợt E survivors plus the Đợt H geometry tools, choreographed through
`VFX_Sequence`.

This is where the plan is proven or falsified. **If the three do not clearly
out-read what they replaced, stop and re-scope before touching the rest.** The
purge removed the fallback on purpose: there is no old version to quietly keep.

**DoD:** side-by-side captures, and a written verdict — continue or re-scope, and
why — in `skills/docs/PROGRESS.md`.

---

## Order, and why

```
H1 (swept trail) ─┬─> H3 (mesh shells) ─┬─> H4 (flow) ──> H8 (checkpoint)
H2 (ground wave) ─┘   H5 (decals)  ─────┘
H6 (2 normal maps) ── small, parallel, no longer gating
H7 (platform) ── after H1-H5 land, before H8's verdict is trusted
```

**H1 first** because it is the biggest visual delta per line of new code: the
engine work is already done and shipping, and nothing consumes it. **H6 is no longer on the critical path** — it is two normal maps plus an
optional art pass, and the generator route covers the rest.
**H8 last and hard** — a checkpoint that runs before the tools exist measures the
wrong thing.

---

## Part 4 — The composition library: PRIMARY and COMPOSITE (29/07/2026)

The owner's direction, and it is right: split the library by **role**, not by
element, and make the two tiers explicit.

- **PRIMARY** — the smallest thing with a name. One visual idea, no timing
  beyond its own envelope, callable on its own, its own bench entry, its own
  tier gate and budget switch. `VFX_ComposeImpactFlash`, `VFX_ComposeGroundWave`.
- **COMPOSITE** — a SCORE over primaries, and nothing else. Its content is
  *which primaries, at what times, at what strength*. `VFX_ComposeImpactPackage`
  is now exactly this and nothing more.

`common/` is already the role-based folder; the element folders hold what is
genuinely element-specific. That part of the structure does not need changing.

### The rule that keeps the count honest

**A variation that differs only in numbers is a PARAMETER, not a function.**
Small/Medium/Large Explosion, Vertical/Horizontal/Cross Slash, Fire/Void/Energy
Orb, Inner/Outer/Breathing Aura — every one of those is one primary with an
argument. Turning parameters into functions is how a 20-function library becomes
120 without gaining a single new capability, and every one of them then costs
its own bench entry, its own test and its own rounds of visual iteration.

Judged that way, the proposed catalogue of ~120 collapses to roughly **30-35
primaries**, of which we already have 14. That is the real target.

### The rule that decides the ORDER, and it is the expensive lesson of this week

**Decompose before you invent.** A primary extracted from an approved composite
costs no visual iteration — the look is already signed off, only its address
changes. A primary invented from scratch costs 3-5 rounds of "write it blind,
owner looks, guess again": H1's swept trail took four and is still not right.

So the queue is: (1) extract the primaries that already exist inside composites,
(2) wire the primitives that exist and have no consumer (the VFX_PLAN §0 table:
trail system, flow map, crystal/plasma materials, path spline), (3) only then
invent.

**And extraction finds bugs, which is the second reason it comes first.** The
first extraction, done today, found that `VFX_ComposeImpactPackage`'s DECAL beat
had shipped with no texture in its `.ud`, so the sequencer's
`if (b->ud != NULL)` skipped it every time: **every impact in the game has been
leaving no mark at all**, while the `normal` the function takes was written into
a static ring and never read. It failed silently, logged nothing, and survived
every review — because a beat buried in a 169-line composite has no bench entry
and cannot be fired on its own. That is the case for the whole split, made by
the code rather than by argument.

### Where the trail sheet fits

The owner's reference sheet lists eight trail types. Five of the eight are the
same primary with different material and width — H1's `VFX_TrailStyle` already
covers BLADE / RIBBON / FILAMENT, and Beam / Projectile / Smoke / Flow /
Lightning are (in order) `DrawRibbonEnergyField`, the trail system's PROJECTILE
type, a particle emitter, a flow-mapped strip (H4), and `core/vfx_proc_ray.h` —
**all of which already exist and four of which have no consumer.** Almost none
of that sheet is new technology; it is wiring, and it belongs in step (2).

**H1's BLADE is still broken, so no new trail work starts before it is fixed.**
Adding five trail types on top of one that dashes multiplies the debt.

### First two pieces of step (2), landed 29/07

**`VFX_ComposeSparkTrail`** — one small moving thing with a CURVED tail, on
`TRAIL_TYPE_WISP` (another trail-system entry point with no consumer). This is
the piece the owner identified as blocking everything upstream: Charge Converge
and the deleted Spirit Swarm read as DOTS, and a charge is a COMPOSITE whose
quality is capped by the primaries it has to draw from. Not stretched sprites —
`stretchStrength` streaks along the velocity, which is a straight segment, and a
mote spiralling into a point is doing nothing but turning.

The immediate follow-on is one line: Charge Converge spawns these instead of
plain particles. It is deliberately NOT done in the same change — the trail has
to be judged on its own first, or a bad result has two candidate causes.

---

## What is deliberately out of scope

Skinned/animated VFX meshes (ER leans on them heavily; we have no pipeline and it
is a project of its own), volumetric fog/lighting, GPU-driven particles, and
depth-of-field pulses. Named here so they are not re-litigated every planning
round — they are real, they are big, and they are not next.
