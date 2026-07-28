# Flipbook pipeline — Taichi sim → Taichi render → atlas

Everything needed to author a volumetric flipbook (fire, smoke, dust, steam…)
lives in this folder. Adding a new effect should be **one entry in `PRESETS`
inside `ti_sim.py`**, not a new script.

```bash
# 1. simulate     ~10 s at res 64, ~60 s at res 112 (64 frames, Metal GPU)
python3 scripts/flipbook/ti_sim.py smoke_puff --res 112 --frames 64 --name smoke_puff

# 2. render       seconds — this is the stage you re-run when only the LOOK changed
python3 scripts/flipbook/render.py build_cache/smoke_puff --cell 256 --supersample 2 \
    --density-scale 3 --zoom auto

# 3. pack + audit instant
python3 scripts/flipbook/pack.py build_cache/smoke_puff/frames --grid 8 \
    --alpha-from-luma 0 --split --shape puff --out smoke_puff_8x8.png
```

Output lands in `assets/textures/`. `--split` also writes `<name>_flame.png` and
`<name>_smoke.png`, the two single-population sheets the engine actually binds.

Shipping presets: `fire_puff`, `smoke_puff` (both in use), `fire` (column),
`smoke`.

## Why three stages

| stage | file | cost |
|---|---|---|
| 1. simulate | `ti_sim.py` | ~60 s at res 112 |
| 2. render | `render.py` | seconds |
| 3. pack + audit | `pack.py` | instant |

Nothing about the sheet's appearance depends on stage 1, so a look iteration
costs seconds and one sim feeds any number of sheets. Keep the `build_cache/`
grids until you are done with an effect — re-rendering is free, re-simulating is
not.

**Why we march the grids ourselves instead of rendering in Blender.** Eevee's
alpha comes from *extinction*, so an emissive flame renders bright and almost
transparent (measured: rgb 255 / alpha 10). That forces alpha to be faked from
luminance, which collapses the sheet to one channel — it can no longer tell
"thick but cool" (smoke) from "hot". Marching ourselves decides exactly what
lands in each channel.

## Channel layout

| channel | contents | consumed as |
|---|---|---|
| **R** | flame emission | multiplied by the black-body ramp at the call site — the ADDITIVE population |
| **G** | smoke density | the alpha-blended, LIT population |
| **B** | self-shadow: the same integral weighted by light reaching each sample (`--light`) | the smoke sheet's VALUE (`pack.py --split` writes B/G into RGB) |
| **A** | true opacity (1 − transmittance) | the mask |

One sim therefore feeds **both** populations the blend law requires.

## The two sheets have different contracts — do not swap them

- A **flame** sheet is white RGB + alpha. Fire EMITS: it is not shadowed by
  anything, and its colour comes from the ramp at the call site. Shading it
  would be wrong.
- A **smoke** sheet carries its own VALUE in RGB (the self-shadow) and the
  density mask in alpha. **A flat mask cannot read as a volume**: the engine
  lights a BILLBOARD and knows nothing about the depth inside the puff, so an
  unshaded smoke sheet stacks into flat overlapping cards. Measured, internal
  value spread (p10..p90): 0.00 = the owner's *"những mảng màu riêng biệt"*,
  0.69 = accepted. Lowering per-sprite alpha does not fix it; only shading does.

The call site must match: `vc_smoke_puff.inl` lifts the vertex colour to ~160 so
a pre-shaded sheet only gets TINTED. Feeding a flat mask down that path, or a
shaded sheet down the near-black-gradient path, is a measured 33/255 black
smudge either way.

## The audit is part of the pipeline

Every pack prints the numbers below, and `pack.py <sheet.png> --grid 8`
re-audits a sheet that already shipped (one that shipped before a measurement
existed has never been held to it).

| number | means | note |
|---|---|---|
| cell coverage | how much of the cell is lit | **no target** — see below |
| height/width | > 1.3 for anything that rises | meaningless for a puff: `--shape puff` |
| reach | farthest lit pixel, in half-cells | ≥ 1.0 with frames touching the border = CLIPPED |
| lobes | perimeter / (2·√(π·area)) | 1.0 = one round blob, higher = more billows. Size-independent, so it compares across resolutions |
| r90, wall shell | printed by `ti_sim.py` | extent of the MASS, measured without the renderer in the way |

**There is no coverage target.** The "19.6%, the sheet that works" figure printed
here for weeks came from `smoke_atlas_8x8.png`, which the owner rejected — a
constant calibrated against a rejected artifact made 22% look like a pass.

## Landmines already paid for

- **Never dial `--zoom` by hand.** Once the effect is clipped the measurement
  saturates: a puff twice too big and one 1% too big both report "touching the
  border". `--zoom auto` renders one uncropped probe pass, measures the true
  reach and computes the crop.
- **If the puff touches the domain wall, the run is contaminated.** Advection
  samples are CLAMPED at the boundary, so material pressed against a wall is
  re-sampled from itself and the solver manufactures density. `ti_sim.py` prints
  `r90` (mass radius, in domain half-widths — > 1.0 means it is in the box
  corners) and the wall-shell mass fraction, and warns. A contaminated run is
  not a smaller version of the same effect and cannot be compared with anything.
- **A preset only survives a resolution change if nothing is measured in
  VOXELS.** Velocities scale `64/N`; diffusion is a per-step fraction that
  smooths ~`sqrt(k)` voxels, so it scales `(N/64)²`; a Jacobi sweep moves
  information one cell, so the iteration count scales `N`; the noise eddy is a
  fraction of the DOMAIN (`--eddy` cells across it), not a count of voxels. All
  four are handled — quote every preset number at `REF_RES` = 64.
- **`--frames` is a PHYSICS axis, not a sampling one.** `dt` is per frame, so a
  24-frame probe simulates an earlier MOMENT of the effect, not a cheap version
  of the whole thing: measured, the same preset went from 18.1% coverage at 24
  frames to 89.5% (and 57/64 cells clipped) at 64. Probe at the frame count you
  will ship.
- **Lobe COUNT is `--eddy`, not `--curl`.** Curl is the amplitude, eddy the
  scale; raising curl to get more billows mostly transports the whole puff into
  the wall.
- **Buoyancy belongs to the PARTICLE, not to a puff sheet.** Bake the rise in
  and the engine's own upward velocity double-counts it, while the puff drifts
  off the cell centre and the (centre-symmetric) autofit crop pays for the empty
  half.
- **Derive the flipbook's fps from the LONGEST lifetime** the consuming
  particle can be given. `SpriteAnim` advances on absolute age, so a faster rate
  runs a long-lived particle past the sheet's empty tail and the effect vanishes
  while its alpha curve still says visible.
- **The cell is square; the domain usually is not.** The renderer fits the
  domain at its true aspect and leaves the rest transparent. Stretching each
  axis to the full cell once smeared a 34x34x96 grid 2.8x horizontally, which
  produced three complaints that sounded unrelated.
- **Audit numbers are only as good as the orientation.** The renderer briefly
  wrote frames transposed (Taichi indexes `[x, y]`, numpy/PIL read axis 0 as the
  row) and the audit reported height/width 2.13 for a flame that was WIDER than
  it was tall. If a number looks too good, check that the motion runs down the
  axis you think it does.

## Self-test (run this after touching render.py)

```bash
python3 scripts/flipbook/selftest.py     # ~5 s
```

Renders a SYNTHETIC column whose shape is known exactly and asserts the sheet
comes back with it: tall, unclipped, rising up the rows. Both renderer bugs so
far were invisible on a real bake — nobody knows what a flame *should* look
like, so a distorted one just looks "wrong somehow" — and obvious on a shape we
authored ourselves.

## Mantaflow is gone (28/07/2026)

Stage 1 used to be Blender's Fluid modifier. Measured on the same config
(res 64, 24 frames): **Mantaflow 99.9 s → Taichi 3.7 s**, ~27x; at res 112/64
frames it was 1037 s against ~60 s. It also could not express the physics these
puffs need — a radial force, curl noise and viscosity with buoyancy at zero —
so the Mantaflow presets faked the first with inflow-along-normals and the third
with dissolve speed. `bake.py`, `make.py` and `fb_presets.py` are deleted;
`git log` has them if a future effect ever wants Mantaflow's combustion model or
wavelet turbulence back.
