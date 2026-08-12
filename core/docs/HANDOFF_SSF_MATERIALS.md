# Handoff — SSF liquid materials (poison / lava / liquid metal) + the cost gates

> Written at the end of the 2026-08-12 session. Successor to
> `HANDOFF_SSF_UPGRADE.md`, which is now executed.
> **Assigned to the rlvk / Renderer agent persona**, deliberately crossing the
> ownership split in the root `CLAUDE.md` (this work lives in `core/fluid`, which
> the table gives to Core). Take the work; do not stall on the boundary. Follow
> the rlvk methodology (`third_party/vulkan/docs/LANDMINES.md` §"Debugging
> methodology") — it is what cracked every defect in the last two sessions.

## What already landed (do not redo)

| commit | what |
|---|---|
| `a333eaf` | dual-depth thickness `T = z_back − z_front`; velocity-aligned anisotropic splats |
| `0903620` | specular antialiasing by normal variance (Kaplanyan/Filament) |
| `8128911` | `surfaceNoise` was a plane wave drawing bands → `fbm3`; `vnoise3`/`fbm3` added to `core/shaders/common/noise.glsl` |
| `8b6ebe9` | thickness pipeline at half resolution; `--render-vfx` FPS uncapped; `perf_ssf_filter` repaired |
| `30f53b8` | `RLVK_GPU_TRACE` now reports missing timestamps instead of averaging zeros |
| `489790d` | silhouette antialiasing from the depth mask |
| `4c2b0f6` | the depth filter runs as a **true 2D kernel**, not two 1D passes |
| `6408c03` | the 2D reach is capped — uncapped it was 9 fps at close range |
| `a26998b` | why the ring outcosts the PBD crown at half the particles |

The user has confirmed by eye: the stripes are gone.

## Architecture facts that constrain everything below (all verified in source)

1. **PBD is a SINGLE SLOT.** `s_particleCount`, `s_impactAge`, `s_receiverPoint`
   are file statics in `fluid_pbd_gpu.c`; `FluidPBDGPU_SpawnImpact` overwrites
   them. A second impact **deletes** the first mid-animation. 2048 particles at
   HIGH, 2.5 s lifetime. There is no pool.
2. **The SSF surface is a singleton with 16 input streams** (`s_gpuStreams[16]`
   in `fluid_surface.c`). All streams rasterize into ONE capture, are filtered
   once and composited once.
3. **ONE material at a time.** `FluidSurface_SetMaterialColors` is global. Every
   liquid on screen shares one colour. This is the single biggest blocker for the
   material work below.
4. **The capture target is `R32F` — one channel.** `fluid_capture_particle.fs`
   computes a per-splat `coverage` into `.g` and it is discarded at the write,
   because there is no green channel. Any per-pixel material id needs RG32F or a
   second target.
5. **Cost is mostly per-frame FIXED**, not per-body. Measured: the ring has 6.3x
   the splat area and 3.2x the screen coverage of the crown and costs only 1.5x.
   The second fluid body is nearly free; the first pays the whole bill.
6. `MAX_GPU_PARTICLES` is **8192** shared across all emitters;
   `WATER_RING_MAX_SPAWN` is 48/frame, which caps the ring at ~2700 alive at
   60 fps regardless of what `alive` asks for.

## Measured cost (VFX tester, 1280x720, HIGH, Intel Mac / MoltenVK)

| | frame | SSF share |
|---|---|---|
| no fluid (NEW FX 0) | 9.8 ms — 102 fps | — |
| FLUID IMPACT | 16.6 ms @f100, 14.6 @f200 (it decays at 2.5 s) | ~6.8 ms |
| WATER RING | 20.1 ms, flat | ~10.3 ms |

Close range (camera 2 m) before the 2D cap: **112 ms — 9 fps**. After the cap:
26.8-31.4 ms, i.e. at or below the old separable path.

**These numbers are from an EMPTY tester scene with one fixture.** Nobody has
measured a populated match scene. Do not quote them as if they were.

## The work, in order

### 1. Poison water — free, do it first as a path check
Nothing to write. Call `FluidSurface_SetMaterialColors` with the poison material
from `VFX_Material(VC_MAT_*)`. Beer-Lambert absorption is already derived from
`u_materialBody`, so "thicker = deeper colour" is already correct for any
dielectric. Confirm on a fixture, then move on.

### 2. Lava — one missing term
The composite's result is `dielectricBase + specular + foam + rimLight`. There is
**no emission term at all**. Lava needs emission driven by thickness (thick =
glowing core, thin = cooled crust), and the refraction of the background turned
down hard. Thickness is now a real measured path in metres, which is exactly the
input this wants. `foam` must become crust, or be gated off.

### 3. Per-pixel material id — the unlock
Until this exists, poison and lava cannot coexist on screen (fact 3). The capture
is single-channel (fact 4), so this means RG32F (depth + material id) or a small
second target, plus a material table in the composite. Everything downstream is
already parameterized by `u_materialBody/Glow/Soft`; what is missing is per-pixel
selection.

### 4. Liquid metal — the biggest job, and probably the best-looking
Metal is a **conductor**: high, coloured F0 and **no transmission**. Today the
water IOR is hardcoded in three places — `FresnelSchlick`'s
`waterF0 = 0.02037`, the same literal again inside `WaterSpecularBRDF`, and
`refract(incident, N, 1.0 / 1.333)`. The whole transmission + Beer-Lambert branch
must be switched off for it. The payoff: metal reads almost entirely through
reflection, the SSR path already exists, and SSF's normals are good at it.

### 5. Cost gates (needed before ANY of this ships into the arena)
There is **no gate today** — nothing stops N skills submitting streams and
nothing skips SSF when the frame is over budget.
- **Ownership by priority**: a boss ultimate outranks a player cast; minions never
  get SSF.
- **Cull by projected size**: below N pixels, fall back to ordinary particles. The
  fixed cost is absurd for a small splash.
- The design that makes basic attacks affordable: they **contribute splats to an
  already-active surface** rather than switching SSF on. Marginal cost is splat
  area only (fact 5). If no hero-scale water is present, they fall back to
  particles.

### 6. Perf, blocked on measurement
- **Half/adaptive resolution for the depth chain** is the biggest remaining lever
  (thickness is already half-res). `perf_ssf_filter` measures 1.2-2.0 ms saved
  over 8 passes; the game runs 4.
- **Dropping the redundant clears**: the filter/blur/resolve passes assign
  `finalColor` on every path, so the `ClearBackground` before them is dead work —
  but omitting it makes rlvk use `loadOp LOAD`, a full-target read, plausibly
  worse on a tiler. Untested, and untestable here.
- **Scissoring measured 0.72 ms SLOWER** — `ClearBackground` is a `loadOp CLEAR`
  over the whole target and scissor cannot shrink it. Do not retry; a smaller
  VIEWPORT or smaller targets would be a different experiment.

### 7. Parked, with reasons
- **PCA anisotropic kernels** for the PBD crown. The old reason for parking
  ("needs a neighbour search the architecture avoids") is **wrong for PBD**:
  `fluid_pbd_gpu.comp` already keeps `heads[]`/`next[]` over 32³ cells, rebuilt
  each Jacobi pass, so a covariance can ride the loop the density constraint
  already walks. Only reachable for FLUID IMPACT, never for the ring.
- **The half-sunk ring's waterline.** `VFX_ComposeWaterRing` spawns a torus
  centred at the cast position, so with a 0.108 m tube at y=0 the lower half is
  under the ground and the foam line is a genuine waterline. An authoring call,
  not a bug. Three mechanism "fixes" were tried and reverted — see LANDMINES.
- **`capFade`** in `WaterMultiOctaveWaves` is still a plane wave. Amplitude 0.004;
  disabling the whole term changed nothing visible.
- **2D filter on mobile.** HIGH only today. `GfxQuality_Default()` returns
  **GFX_MED on Android**, and a several-hundred-tap dependent-fetch loop on a Mali
  tiler cannot be judged from a desktop. One device run decides it.

## Method — earned the hard way, twice. Read this before touching anything.

1. **Build the observation before the fix.** It settled three defects in one build
   each, after reasoning-from-mechanism had missed repeatedly: a 1 cm *ruler* over
   thickness, *subtracting* one additive term at a time, and running each filter
   pass *alone*. A stripe/step colour code survives the HDR tonemap; a grey ramp
   does not.
2. **Isolate additive terms by SUBTRACTION, not by display.** Every term rendered
   alone looked innocent — foam is a dim teal fringe on black and white on top of
   blue. `water - foam` found it in one look.
3. **Check WHICH VARIANT of an algorithm you are running.** The filter was a
   faithful narrow-range implementation — of the paper's `filter1D`, run twice,
   which is the approximate separation the paper itself warns about. Five invented
   boundary conditions had been spent before anyone checked.
4. **A mirror test can assert a property the algorithm does not have.** It
   happened again: a guard demanded a splat's chord close to zero at its rim, but
   the capture profile deliberately keeps a floor there (the real value is 28% of
   the diameter). Fix the guard, not the shader.
5. **Verify the picture before trusting a perf number.** A half-resolution change
   measured as a clean win because it had broken the surface and was rendering
   nothing.
6. **Runs are NOT deterministic.** Two captures of the same fixture differ. Compare
   silhouette-scale detail across several runs before attributing it.
7. **Do not ship a change the user cannot see.** The silhouette AA is real at 7x
   and invisible at 1x; it was oversold. Compare at the framing the user actually
   plays at.
8. **Check the fixture's geometry before deciding a term misfires.** Three
   mechanism fixes were spent on a "silhouette artifact" that was the real
   waterline of a half-sunk ring.

## Tooling

```bash
# Headless capture. Without the ICD env the binary dies at instance creation.
V=~/VulkanSDK/1.3.296.0/macOS
VK_ICD_FILENAMES="$V/share/vulkan/icd.d/MoltenVK_icd.json" DYLD_LIBRARY_PATH="$V/lib" \
  ./build/wuxing --render-vfx 41 --warmup 60 --out autotest_output/shot.png
```
- NEW FX indices come from `s_newFxNames[]` in `sandbox/vfx_test.c`, 0-based:
  **FLUID IMPACT 38, WATER RING 41**. Pick the warmup per effect — the PBD crown
  peaks near frame 16 and is gone by 45; the ring is steady.
- The camera is fixed at `vfxCamDist = 6.0` with no CLI knob, and
  `GfxQuality_Default()` has no override. Both are worth a TEMPORARY env in
  `main.c` / `core/gfx_quality.c` when a close-up or a LOW-tier check is needed —
  add, use, remove.
- `glslangValidator` **rejects the project's `#include`**. Resolve the includes
  into a temp file first, then validate; `fluid_surface.fs` now includes
  `noise.glsl`.
- `./scripts/run_core_tests.sh` is the headless tier. **Four suites are red and
  have been for weeks** — `energy_burst_semantic_layers`, `tube_frame`,
  `vfx_layered_field_contract`, `volume_trail`. Not yours.
- rlvk perf scenarios need `UNCAPPED=1`, or they measure the display refresh and
  report plausible nonsense.
- **`RLVK_GPU_TRACE` does not work on macOS.** The queue family advertises
  `timestampValidBits=64` and every query returns available with a value of zero.
  Use wall clock sampled INSIDE the render loop, with variants interleaved
  **randomly** per frame in ONE process — a fixed rotation puts the GPU's
  pipelining bias on one bucket and the deltas flip sign between runs.
