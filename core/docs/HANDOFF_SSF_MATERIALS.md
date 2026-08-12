# Handoff — SSF liquid materials + the cost gates

> Rewritten at the end of the 2026-08-12 (second) session. The five tasks the
> previous version of this file listed are **executed**; what follows records
> what landed, what it cost, and what is genuinely left.
> Method notes are at the bottom and are the most reusable part of this file.

## What landed this session

| what | where |
|---|---|
| **Liquid table**: `FluidLiquidDesc` + 4 content-addressed slots, LRU eviction | `core/fluid/fluid_surface.{h,c}` |
| **Per-pixel material id**: front capture RGBA32F, slot in `.b`, composite looks it up | `fluid_capture*.fs`, `fluid_surface.fs` |
| **Three optical classes**: dielectric / emissive / conductor | `fluid_surface.fs` |
| **IOR unified**: the three hardcoded water constants are gone | `fluid_surface.fs` |
| **Cost gates**: priority ownership + projected-size cull + frame budget | `FluidSurface_RequestBody` |
| **LIQUID BENCH fixture** (NEW FX 40): water, lava, liquid metal in ONE capture | `core/composition/water/liquid_bench.inl` |
| 3 new headless suites | `core/tests/fluid_{capture_projection,liquid_material,cost_gate}_test.c` |

### 1. Poison — confirmed, zero code
`FluidSurface_SetMaterialColors` with `VFX_Material(VC_MAT_POISON)` renders a
green ring with correct thickness-driven absorption. Beer-Lambert already derives
everything from the body colour. Note for authoring: poison's `glow` **equals**
its `body`, so its Fresnel rim is green-on-green and it reads flatter than water.
That is an authoring consequence, not a defect.

### 2. Lava — emission by thickness
`FLUID_LIQUID_EMISSIVE` adds `emissionColor * (1 - exp(-k·path)) * strength`,
with `k = ln(2)/FLUID_REFERENCE_DEPTH_M` — one reference depth emits half its
source radiance. Crust is the foam term **re-read**: the same patchy field
subtracts emission instead of adding a white cap, at its own frequency (7.0, a
0.14 m cell) because `surfaceNoise`'s 0.37 m cell is most of a body.
`opacityPerMetre` kills the refracted background; the body colour alone cannot,
because a saturated orange transmits ~100% of red.

### 3. Per-pixel material id — the unlock
The slot rides the **same fragment that wins the depth test**, so the composite
can never shade a surface with another surface's material. Read from the RAW
capture, never from the filtered depth: the narrow-range filter averages its
taps, and averaging slot 0 with slot 2 yields slot 1 — a third liquid that is not
there. Known limit: at MED/LOW the composite and the capture differ in size, so a
tap can land between texels and round to a neighbourless slot. That is a
one-texel band at a boundary between two liquids, and the tests deliberately do
**not** claim otherwise.

### 4. Liquid metal — conductor branch
Coloured F0 (the material's `body` **is** its F0), no transmission, no
in-scatter, no foam, and the dielectric rim hack gated off — it double-counts the
Fresnel and paints it METAL's blue `glow`. The un-normalized Blinn glint lobes
are gated off too: they were authored against water's 0.02 F0 and the GGX term
alone is already ~45x brighter once F0 is a metal's.

### 5. Cost gates
`FluidSurface_RequestBody(priority, center, worldRadius)` — ask before building.
`MINION` never; `BASIC` only joins a surface already running; `CAST` may switch
one on; `ULTIMATE` survives an over-budget frame. Below a 16 px projected radius
nothing gets SSF. The reconstruction radius (still single-owner) goes through
`FluidSurface_SetReconstructionRadiusFor`, highest priority wins.
Wired into `VFX_ComposeWaterRing` (rejected -> spawns the same torus with
**visible** particle colours instead of the SSF path's alpha-0) and
`FluidImpact_SpawnWater` (rejected -> skips the PBD solve; the ballistic droplets
and residue still render as ordinary particles).

## Two engine defects found on the way — both in the CPU ellipsoid path

`FluidSurface_RegisterParticle`/`RegisterEllipsoid` is public API and it rendered
**nothing**. Two independent bugs, both promoted to root `ENGINE_LANDMINES.md`:

1. **`rlPushMatrix()` does not hand back an identity.** It redirects writes to a
   persistent global and saves whatever that already held; it held a leftover
   VIEW matrix, so every sphere was view-transformed twice.
2. **The capture rasterized through `BeginMode3D` (near = 0.01) while the
   composite inverts near = 1.0.** A body at 7.5 m decoded to 428 m and every
   pixel failed the scene-occlusion discard.

Both were invisible because every *other* SSF input is an immediate-mode
billboard that computes its own depth, and this path had no fixture. It has one
now, and the fixture found both on its first run.

## Measured

**Per-pixel material id is free.** Interleaved in ONE process, variant chosen at
random per frame (the only method that works here — see below):

| run | RGBA32F capture | R32F capture |
|---|---|---|
| 1 | 19.974 ms (447 f) | 20.333 ms (440 f) |
| 2 | 20.183 ms (450 f) | 20.178 ms (442 f) |
| 3 | 21.136 ms (441 f) | 21.194 ms (438 f) |

Deltas −0.36 / +0.01 / −0.06 ms: the sign flips and every one is inside the
1.2 ms drift *between* runs. Widening the front capture costs nothing measurable
on this rig. The rendered image was checked on every run — the ring was present,
so these are not empty frames.

**Cross-process wall clock could not resolve this at all**: the same fixture
measured 19.2 / 27.1 / 20.4 ms per frame across three process pairs. Do not
bother with it.

## What is genuinely left

- **The gates have never run in a populated match scene.** Every number here and
  in the previous handoff is from an empty tester with one fixture. The budget
  constant (26 ms) and the 16 px cull are reasoned, not tuned against real play.
- **`FLUID_SURFACE_MATERIAL_SLOTS` is 4 and eviction is LRU.** Nothing has yet
  put more than three liquids on screen. The failure mode is a body changing
  colour, never a crash.
- **The water ring's rejected fallback is a bead ring**, not an authored
  particle effect. It is honest but it is not pretty; a real fallback is the
  composer author's job.
- **Mobile.** The whole liquid table is untested on Mali. `GfxQuality_Default()`
  returns GFX_MED on Android, which takes the separable filter path; the material
  id path is tier-independent and its one risk is the MED/LOW size mismatch noted
  above. One device run decides it.
- Everything in the previous handoff's §6 (half/adaptive depth resolution) and §7
  (PCA anisotropic kernels, the half-sunk ring's waterline, `capFade`, the 2D
  filter on mobile) is untouched and still stands.

## Method — this session's evidence for the rules

1. **Build the observation before the fix.** Four temporary composite views
   (raw depth, raw thickness, per-term subtraction, material slot) found three
   defects in one build each. Reasoning from mechanism had produced four wrong
   hypotheses about the CPU path before the first view was written.
2. **Subtract, do not display.** `water - emission` showed a rich crimson lava
   body underneath a peach one, which redirected the fix from *magnitude* to
   *colour* — halving the magnitude first had changed almost nothing.
3. **Two runs of the same fixture are NOT the same image.** The baseline
   run-to-run difference on WATER RING is **2.17% of pixels, mean |dRGB| 0.35**.
   Every regression check here is quoted against that floor; without it the
   material-table refactor's 2.13% would have read as a regression.
4. **A number that moves is worth a screenshot.** The cost gate's first version
   showed 2.21% differing pixels — inside the floor — but mean |dRGB| 3.02,
   7x the floor. The image showed the ring had been **deleted**. The pixel count
   alone would have passed it.
5. **Assert only what the algorithm promises.** The material-id test explicitly
   does not claim a tap between two liquids resolves to either of them, because
   it does not.
6. **Ask what refreshes a gate's input.** The first cost gate latched shut
   permanently because the state it read was only updated on the path it
   blocked.

## Tooling

```bash
# Headless capture. Without the ICD env the binary dies at instance creation.
V=~/VulkanSDK/1.3.296.0/macOS
VK_ICD_FILENAMES="$V/share/vulkan/icd.d/MoltenVK_icd.json" DYLD_LIBRARY_PATH="$V/lib" \
  ./build/wuxing --render-vfx 40 --warmup 60 --out autotest_output/shot.png
```
- NEW FX indices come from `s_newFxNames[]` in `sandbox/vfx_test.c`, 0-based.
  **They shifted this session**: FLUID IMPACT 38, **LIQUID BENCH 40**,
  WATER ORB 41, **WATER RING 42** (was 41). The list is alphabetical within a
  category, so any new water entry renumbers the ones after it.
- `glslangValidator` **rejects the project's `#include`** — resolve includes into
  a temp file first.
- `./scripts/run_core_tests.sh` — 63 suites, **4 red and have been for weeks**
  (`energy_burst_semantic_layers`, `tube_frame`, `vfx_layered_field_contract`,
  `volume_trail`). Not yours.
- **`RLVK_GPU_TRACE` does not work on macOS** — every query returns zero.
