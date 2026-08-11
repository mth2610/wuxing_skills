# Handoff — SSF upgrade: dual-depth thickness + anisotropic splats

> Written at the end of the 2026-08-11 session, for a fresh session to execute.
> **The user has assigned this to the rlvk / Renderer agent persona**, deliberately
> crossing the ownership split in the root `CLAUDE.md` (this work lives in
> `core/fluid`, which the table gives to Core). Their reason: the Core persona was
> not effective on this class of problem in that session. Do not stall on the
> boundary — take the work, and follow the rlvk methodology rules
> (`third_party/vulkan/docs/LANDMINES.md` §"Debugging methodology"), which are what
> finally cracked the previous defects.

## Order of work

1. **Dual-depth thickness** (the user's call, promoted over the cheaper
   calibration fix: dense splash effects are coming and a shell-only model cannot
   serve them).
2. **Anisotropic splats along velocity** — the only visible defect left.

Everything else is parked with reasons at the bottom.

---

## 1. Dual-depth thickness

### What exists now

`core/fluid/shaders/fluid_surface_thickness.fs` accumulates one sphere chord per
splat (`2*r*sqrt(1-r²)`, scaled ×16) with additive blending into an R32F target.
`DecodeOpticalThickness` in `fluid_surface.fs` then maps that sum to metres:

```glsl
accumulatedPath = enc / 16.0;
traversedPath   = accumulatedPath / 1.5;        // "kernel overlap" — INVENTED
return 0.16 * (1.0 - exp(-traversedPath / 1.20)); // knee + cap — INVENTED
```

Both constants were hand-tuned twice in one session and are behind two separate
user complaints: the body reading as opaque plastic (an earlier knee saturated
every interior pixel) and later "it lost its mass" (the next knee still
saturated). The METHOD is standard — additive accumulation is what NVIDIA FleX,
Obi, VTK and Narrow-Band SSFR (CGF 2022 §2.4) all do — but this calibration is not.

### What to build

Thickness as a measured geometric path: `T = z_back − z_front`.

- Point sprites have **no back faces to cull**. The capture fragment shader
  already computes the sphere's near root; the back pass is the same shader with
  the far root (`centre − sphereZ*r` in view space, where +Z is toward the eye)
  and a **max**-depth reduction instead of min.
- Front depth already exists: `core/particles/shaders/gpu/fluid_surface_capture.vs`
  + `core/fluid/shaders/fluid_capture_particle.fs`. Mirror it for the back.
- Feed `T` straight into Beer-Lambert. `absorption` in `fluid_surface.fs` is
  currently `-log(materialTransmission) * 2.60 + 0.06`; with a real path length in
  metres this scale should be re-derived, not re-tuned by eye.

### The trap that must be handled, not discovered

Dual-depth measures the **envelope of the splat cloud**, not the water. For a
DENSE body (fluid impact, PBD crown) that is correct. For a **shell** emitter it
is not: `core/composition/water/water_ring.inl` spawns particles on the SURFACE
of a torus, so the tube is hollow, and back−front reports the full tube diameter.

Decide this by measurement, not by argument — both fixtures already exist:

- `NEW FX` tab → **WATER RING** (shell, hollow tube)
- `NEW FX` tab → **FLUID IMPACT** (dense, PBD crown)

If dual-depth wins on both, delete the accumulation path. If it inflates the ring,
keep both and let the emitter choose (a flag on the surface stream, defaulting to
accumulation for shells). Do not decide this from a screenshot of one fixture.

### Acceptance

- The two invented constants (`/1.5`, `/1.20`) are gone or replaced by something
  derived, and `core/tests/fluid_surface_optics_test.c` no longer needs to assert
  "the densest authored body must sit clear of the cap" — that guard exists only
  because the decode saturates.
- A new headless guard covering the back-depth arithmetic (the far root, the max
  reduction, `T = back − front` non-negative and zero where the cloud is one splat
  thick).
- Visual check on BOTH fixtures above, at HIGH and LOW tiers.

---

## 2. Anisotropic splats along velocity

### Why it is worth doing and why it is cheap

Splats are isotropic spheres, so thin films and rims render as strings of beads —
the last visible defect in the session's screenshots. The full fix in the
literature (Yu & Turk 2013 anisotropic kernels; the screen-space variant in
Computers & Graphics 2022/2023) needs a PCA over each particle's neighbours,
i.e. a neighbour search the force-field path deliberately does not have.

The velocity-aligned approximation needs no neighbour search, and both halves are
already in place:

- the GPU particle struct already carries `vel_drag`, so
  `core/particles/shaders/gpu/fluid_surface_capture.vs` can build a stretched
  quad from it with **no struct change**;
- `FluidSurface_RegisterEllipsoid(pos, radii)` is already in the public header —
  but the implementation averages the three radii back into one scalar
  (`core/fluid/fluid_surface.c:271` and `:285`). The API promises anisotropy and
  the code throws it away. Honour it.

### Care

The fragment stage reconstructs an analytic sphere (`r2 = dot(corner, corner)`,
`sphereZ = sqrt(1 - r2*0.9) * (1 - r2*0.1)`). Stretching the quad without
stretching that reconstruction gives a clipped ellipse, not an ellipsoid. Both
must change together, and the thickness pass's chord length must use the
stretched extent too or thickness and depth will disagree.

Cap the stretch (roughly 3:1) — an unbounded aspect ratio at high speed turns
splats into streaks and reopens the coverage question that
`core/tests/water_ring_coverage_test.c` guards.

---

## What NOT to redo

The silhouette streaking hunt cost most of a session. **Five** boundary
conditions were tried in the depth filter; the record is in
`core/docs/PROGRESS.md` and `core/docs/LANDMINES.md`:

| tried | result |
|---|---|
| drop the missing sample | biased — the streak |
| `break` at the first missing side | unbiased, but stops at every interior gap in a sparse field: worse |
| clamp around the centre depth | terraces a sloped surface into steps |
| clamp around a tangent-plane prediction | unbiased but dilutes, edge stiffer than interior |
| even reflection | unbiased and undiluted, and visibly WORSE on screen |

The actual cause was **not in the filter**: the normal reconstruction substituted
the centre's own depth for a missing neighbour, which gave the fabricated sample a
zero z-difference so it won the minimum-gradient pick every time, forcing
view-facing normals at every silhouette pixel. Fixed in `3aad985`. The filter is
now the published narrow-range algorithm (Truong & Yuksel 2018) implemented from
the paper, with two local additions the reference lacks: sigma continuous in depth
(the reference's integer `filterSize` is a step function and draws contour lines)
and a per-tier radius ceiling.

Note also: NB-SSFR (CGF 2022) is **not** applicable here — it needs a spatial grid
and neighbour classification, and its win only appears when interior particles
dominate. At 1–2k particles nearly all of ours are surface particles.

## Method that worked, after several that did not

1. **Build the observation before the fix.** A `u_debugView` uniform splitting the
   composite into stages attributed the contour bands in ONE build, after four
   fixes reasoned from mechanism had all missed. Views 1 (normal) and 12
   (composite off the unfiltered capture, host-side) are still in
   `fluid_surface.fs` / `fluid_surface.c`, driven by `WUXING_FLUID_DEBUG`; remove
   them when this work is signed off.
2. **Read the reference implementation** before inventing a sixth variant.
3. **A mirror test can assert a property the method does not have.** One guard here
   demanded that an edge pixel be smoothed as hard as an interior one; that is not
   a property of the narrow-range filter, and chasing it produced a change that
   looked worse. Assert what the algorithm promises, not what you wish it did.
4. `glslangValidator -S frag <file>` compiles every shader here headlessly — use it
   after every edit. `./scripts/run_core_tests.sh` is the headless tier.

## Performance context

Measured in-game: fluid costs ~1.7 ms of a 16.7 ms frame (surface ~1.2 ms, PBD
solver ~0.5 ms after the compute-dispatch batching landed in `bb7caf1`). Neither
upgrade above should be judged on frame time; if perf work is wanted later,
`perf_ssf_filter` in the rlvk visual suite is written and has never been run.

## Parked, with reasons

- **Half-resolution + bilateral upsample** — ceiling on the win is ~0.6 ms. Measure
  with `perf_ssf_filter` first.
- **Temporal accumulation** — nobody has observed the surface boiling. Wait for an
  observation, not an argument.
- **Curvature flow** — the streaks are now sub-visible; marginal value.
- **Narrow-band (CGF 2022)** — see above, wrong particle-count regime.
- **Full PCA anisotropic kernels** — needs the neighbour search the architecture
  avoids; item 2 is the 80% without it.
- **One Android run** — cheap and answers three open questions (R32F blend, R32F
  linear filtering, Mali early-Z cost of `gl_FragDepth` + `discard` in the capture
  pass). rlvk's runtime test prints the format caps. Android runs rlvk/Vulkan
  already (`third_party/vulkan/docs/PROGRESS.md:170`) — do not repeat the mistake
  of assuming a GLES 3.0 build.
- **Four red core suites** (`energy_burst_semantic_layers`, `tube_frame`,
  `vfx_layered_field_contract`, `volume_trail`) were red before that session and
  nobody has looked.
