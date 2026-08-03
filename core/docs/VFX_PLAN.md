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
| **Flow map** | `core/uv/flow_map.h` | **0** |
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

### H1. `VFX_ComposeRibbonTrail` — the swept ribbon/body trail

**Owner:** Core Agent · **Size:** M · **Depends on:** nothing · **Do first**
> **LANDED 29/07/2026** — `core/composition/common/vc_ribbon_trail.inl`, bench
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
int  VFX_ComposeRibbonTrail(const Matrix *followTransform, VC_MaterialId mat,
                             float width, float lifetime, VFX_RibbonTrailKind kind);
void VFX_RibbonTrailSetWidth(int handle, float width01); // ramped, for wind-down
void VFX_KillRibbonTrail(int handle);
```

`VFX_RibbonTrailKind` is the point of the task: BLADE (thin, hard outer edge, the
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

`core/uv/flow_map.h` is unused. Real UV advection is what makes energy look like it
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

## Part 4 — The PRIMARY catalogue (rewritten 30/07/2026)

### 4.0 What changed: there is now a VOLUME primitive

Until 30/07 every visual this project owned was a camera-facing sprite or a flat
strip, and §0.1 named that as the whole diagnosis. That is no longer true. The
trail system can now sweep a **volume**, and the pieces are in and tested:

| Capability | Where | State |
|---|---|---|
| `TRAIL_SHAPE_TUBE` — swept cross-section, layers, material UV, cloth | `core/trail_system.h` | landed, 16/16 suites |
| Teardrop profile + apex caps + taper | `ProceduralMesh_DefaultTubeConfig` | pre-existing, now used |
| **Parallel-transport frame** (`useTransportFrame`) | `core/geometry/pm_tube.inl` | landed; opt-in so water stream is untouched |
| Scrolling UV along a tube (`ProceduralMesh_DrawTubeEx`) | same | landed |
| Vertex deform sampled from `volume_noise.png` | same | landed |
| Two-phase flow map, displacement **centred** | `core/shaders/flow_map.fs` | bug fixed, still **zero consumers** |
| Tiling flow field generator | `FlowMap_CreateWithTrailTexture` | landed |
| Seamless smoke / fire / energy / noise / flow / gradient sheets | `scripts/gen_volume_trail_textures.py` | landed, seams measured |

**VERIFIED ON SCREEN 30/07.** It took four independent causes of one symptom
("renders flat"), fixed in sequence, and it looked identical after the first
three: a ribbon sheet wrapped around a tube; that fix applied to one layer only;
a missing batch flush around the cull state; and finally the transported frame
collapsing onto the tangent, where `Vector3Normalize` of ~zero flattened the
whole cross-section into a line. The last one is the general lesson — see
`docs/LANDMINES.md`.

### 4.1 The two rules that decide what is a primary

1. **A variation that differs only in numbers is a PARAMETER.** Small/Medium/
   Large explosion, Fire/Void/Energy orb, Inner/Outer aura — one function with an
   argument. This is what keeps a 30-function library from becoming 120 without
   gaining a capability.
2. **Decompose before you invent.** A primary extracted from an approved
   composite costs no visual iteration. One invented from scratch costs 3-5
   rounds of write-blind → owner looks → guess again. This session spent most of
   its rounds on the second kind, and every one of the cheap wins was the first.

### 4.2 What exists (15 primaries + 2 composites)

**Primaries:** `SmokePuff` `FlameVolume` `CharacterAura` `GlintSparkle`
`RuneCircle` `DissolveExit` `SweepSlash` `EnergyBurst` `LightShaft`
`ImpactFlash` `ImpactDistort` `ImpactDecal` `SweptTrail` `GroundWave`
`SparkTrail` `CoreGlow` `EnergyOrb` `VolumeTrail` (P1, 30/07)

**Composites:** `ImpactPackage` `Projectile` (and `ChargeConverge`, which is one
primary short of being a pure score — the converge motes still need extracting).

#### Visual review hold — ShieldShell and Residue/Scorch (31/07)

- **ShieldShell:** authored flow-surface now has a separate body and RG flow
  map, alpha composition, strand-driven face alpha and triplanar sphere mapping.
  It still loses contrast on a bright environment. This is a visual-owner review
  item; do not compensate by reintroducing additive haze, a darkening blend, or
  a Fresnel half-sphere gradient.
- **Residue/Scorch:** blocked by `assets/DECAL_REWORK.md`. The current decal
  catalog is legacy and has no approved residue/scorch profile; `DecalSystem`
  also has no semantic profile or per-primary handle. Do not implement a new
  primary by selecting a legacy filename. Resume only with the Decal Rework
  surface registry (role, channels, provenance, wrap/filter, blend) and visual
  approval.
  - **Scorch render investigation hold (31/07):** the P4 review material has
    clean organic alpha, a conformal mesh, alpha and multiply A/B base passes,
    plus an independent HDR ember pass. It remains washed-out on both grass and
    bright ground; stacked instances become readable. This may be a blend/state
    or backend interaction (including Vulkan), but is not proven. Do not keep
    tuning source thresholds or ShieldShell by analogy. Resume with a minimal
    backend A/B capture of the decal pass and its blend/depth state.

### 4.3 What is MISSING, in build order

Each entry below is a spec. They are ordered so that nothing is blocked by
anything after it.

---

#### P1. `VFX_ComposeVolumeTrail` — promote the tube to a named primary — **LANDED 30/07**

**Why it was first:** the tube existed only as `VFX_TRAIL_HAZE`, a *style* of the
swept trail, reachable only through a trail handle. Smoke, fire, a dragon's
breath and a beam all want a swept volume without wanting a weapon trail's
cloth, aspect cap or spark layer.

`core/composition/common/vc_volume_trail.inl`, declared in `visual_composer.h`.

```c
typedef enum { VOL_ENERGY = 0, VOL_SMOKE, VOL_FIRE, VFX_VOLUME_KIND_COUNT } VFX_VolumeKind;

int  VFX_ComposeVolumeTrail(const Matrix *followTransform, VC_MaterialId mat,
                            float radius, float lifetime, VFX_VolumeKind kind);
void VFX_KillVolumeTrail(int handle);
```

- `kind` selects the sheet, the noise amplitude and the flow swirl. **Exactly
  three columns**, and `core/tests/volume_trail_test.c` counts the per-kind
  tables so a fourth cannot arrive quietly.
- `TRAIL_SHAPE_TUBE` reused wholesale — the test asserts the file contains no
  `ProceduralMesh_BuildTubeAlongPath` call and no `rlBegin` of its own.
- **Three departures from the spec as written, each recorded here so nobody
  re-litigates them:**
  1. **The blend is a fourth thing `kind` decides**, because the blend law is not
     optional: smoke OCCLUDES (`BLEND_ALPHA`, dark ramp), energy and fire EMIT
     (`BLEND_ADDITIVE`, body→glow ramp). Both are read off ONE predicate
     (`VolumeTrail_Emits`) so they cannot drift apart. Glowing smoke stays TWO
     draws.
  2. **The aspect cap stays**, as ONE ratio (1:2.5 full width against travelled
     length) shared by every kind — not the weapon trail's per-style table,
     which is the thing P1 sheds. Dropping it entirely would let a barely-moved
     emitter draw a ball, which is the exact landmine "Thickness is a ratio
     against the thing's OWN length".
  3. **The tier ladder scales the tube instead of dropping it.** `VFX_TRAIL_HAZE`
     falls back to a flat strip below `GFX_MED`; that is a switch, not a tier, so
     here radial segments / rings / layer count clamp down (8·24·2 → 5·10·1,
     ~9x cheaper) and it is still a closed tube at the lowest tier.
- Cloth, spark layer and the per-style aspect table are all gone; the surface's
  life comes from the noise deform travelling along the tube on the sheet's own
  scroll clock.
- Dials: `vol_radius vol_aspect vol_alpha vol_noise vol_flow vol_tile vol_layers`
  and `vol_solid` (the matte white cast, for judging silhouette alone).
- **DoD met:** bench entry `VOLUME TRAIL` (all three kinds flying the same curved
  path side by side, which is the only way "they differ in three things" is
  judgeable); `core/tests/volume_trail_test.c`, 59 checks.

#### P2. `VFX_ComposeConvergeMotes` — finish decomposing ChargeConverge

The one visual idea `ChargeConverge` still owns: motes peeling off a shell and
being drawn in along curved threads. Extract it verbatim; the composite then
becomes a pure score (motes + `CoreGlow` + light).

```c
void VFX_ComposeConvergeMotes(Vector3 center, VC_MaterialId mat, float radius,
                              float t01, int moteCount);
```

**DoD:** `ChargeConverge` shrinks to three calls; a headless test asserts every
authored number arrived intact (copy the shape of `core_glow_test.c`, which
exists precisely to make an extraction provable).

#### P3. `VFX_ComposeDebrisShards` — rewrite the restored scaffold

`VFX_ComposeShardDebris` is a pre-Đợt-E survivor, not a survivor of the purge.
The reference projectile needs chips catching light in its wake.

```c
void VFX_ComposeDebrisShards(Vector3 pos, Vector3 vel, VC_MaterialId mat,
                             float scale, int count);
```

- Angular flat-shaded chips (`DrawCoreCube` squashed, per-instance random), NOT
  sprites: a chip that is the same shape from every angle is a spark.
- Tumble on their own axis; the tumble is what catches light.
- **DoD:** bench entry; test pinning count-vs-tier and that emission is a count
  per CALL (one-shot) rather than a rate.

#### P4. `VFX_ComposeBeam` — the sustained line

Nothing in the library draws a held beam. `DrawRibbonEnergyField` exists with one
consumer and `vfx_proc_ray.h` has none.

```c
void VFX_ComposeBeam(Vector3 from, Vector3 to, VC_MaterialId mat,
                     float width, float t01);
```

- A swept tube between two points is the obvious build now that P1 exists.
- **DoD:** must hold up when `from`/`to` are nearly coincident, and when the beam
  is viewed end-on.

#### P5. `VFX_ComposeShockRing` — the expanding ring, off the ground

`GroundWave` conforms to terrain. A ring in mid-air (an impact in the air, a
parry, a barrier break) is a different primary and wants no height function.

#### P6. `VFX_ComposePortalDisc` — the flat disc with a rim

`TRAIL_TYPE_PORTAL` exists with no consumer.

---

### 4.4 If the tube ever looks flat again

There are exactly two causes and they need different fixes. The roundness line in
`core/trail_system.c` separates them in one run:

```
TRAIL tube: 24 rings x 8 radial, section 1.550 x 1.480 m (roundness 0.95 ...)
```

- **no line at all** → the tube branch never ran and a ribbon is drawing. Check
  `shape` in the `VFX_SWEPT` spawn line and the `GfxQuality_Get() >= GFX_MED`
  gate.
- **roundness < 0.2** → the section collapsed. The degenerate-frame guard in
  `pm_tube.inl` is missing a case.

Do not guess between them. Four rounds went on exactly that.

### 4.5 The dials that exist, so nobody re-derives them

`tuning.cfg`, all live, no rebuild:

```
haze_solid = 0    # 1 = opaque white — judge SHAPE with no additive confusion
haze_layers = 1   # 1 or 2
haze_tex = 0      # 0 bare / 1 energy / 2 smoke / 3 fire
haze_rim = 1      # 1 = both walls (free rim), 0 = near wall only
haze_noise = 0.18 # vertex deform amplitude, fraction of radius
swept_flow = -1.0 # tiles/sec over the cloth; NEGATIVE runs against travel
swept_tile = 1.10 # metres per texture repeat
swept_alpha = 1.0
orb_rim / orb_core / orb_size
proj_spiral_turns / proj_spiral_r / proj_scale
```

## What is deliberately out of scope

Skinned/animated VFX meshes (ER leans on them heavily; we have no pipeline and it
is a project of its own), volumetric fog/lighting, GPU-driven particles, and
depth-of-field pulses. Named here so they are not re-litigated every planning
round — they are real, they are big, and they are not next.
