# Flipbook pipeline — Mantaflow sim → Taichi render → atlas

Everything needed to author a volumetric flipbook (fire, smoke, explosion,
dust…) lives in this folder. Adding a new effect should be **one entry in
`fb_presets.py`**, not a new script.

```bash
# one command, end to end
python3 scripts/flipbook/make.py fire --quick          # ~15 s, 1024px sheet
python3 scripts/flipbook/make.py fire --res 96 --cell 256   # the one that ships

# only the LOOK changed? skip the slow half — the grids are still on disk
python3 scripts/flipbook/make.py fire --no-bake --density-scale 6
```

Output lands in `assets/textures/<name>_atlas_8x8.png`.

## Why three stages

| stage | file | tool | cost |
|---|---|---|---|
| 1. bake | `bake.py` | Blender/**Mantaflow** | slow (seconds → minutes) |
| 2. render | `render.py` | **Taichi** (GPU) | seconds |
| 3. pack + audit | `pack.py` | numpy/PIL | instant |

Baking is the slow part and **nothing about the sheet's appearance depends on
it**. Keeping them apart means a look iteration costs seconds, and one bake
feeds any number of different sheets.

**Mantaflow does the sim** because it is a production solver — MacCormack
advection, a calibrated fuel → flame → soot combustion model, obstacles, wavelet
turbulence. There is no standalone build to install (`pip` has no mantaflow);
Blender's Fluid modifier *is* Mantaflow.

**Taichi does the render** because Eevee cannot give us the channels we need:
its alpha comes from *extinction*, so an emissive flame renders bright and
almost transparent (measured: rgb 255 / alpha 10). That forces alpha to be faked
from luminance, which collapses the sheet to one channel — it can no longer tell
"thick but cool" (smoke) from "hot". Marching the grids ourselves decides
exactly what lands in each channel. Verified on Metal at 2.86 ms/kernel for 128³.

## Channel layout

| channel | contents | consumed as |
|---|---|---|
| **R** | flame emission | multiply by the black-body ramp at the call site (F3) — the ADDITIVE population |
| **G** | smoke density | the alpha-blended, LIT population (F1b) |
| **B** | *reserved* | motion vectors / rim / 6-way lighting |
| **A** | true opacity (1 − transmittance) | the mask |

One sheet therefore feeds **both** populations the blend law requires, instead
of one greyscale mask doing duty for both.

## The audit is part of the pipeline

Every pack prints cell coverage, height/width, and per-channel coverage, and
warns when the result is still puff-shaped. E4's original fire sheet reached the
engine before anyone measured it at 4.1% coverage and height/width 1.00 — a
spherical puff. That cannot happen silently now.

Targets: **height/width > 1.3** for anything that rises (a cubic domain cannot
exceed ~1, so the fix is `domain_scale`, never the render).

**Audit numbers are only as good as the orientation.** The renderer briefly
wrote its frames transposed — Taichi fields index `[x, y]` while numpy and PIL
read axis 0 as the row — and the audit dutifully reported height/width 2.13 for
a flame that was actually WIDER than it was tall (0.60 once fixed). If a number
looks too good, check that the motion runs down the axis you think it does: a
rising plume must move the row-centroid, not the column-centroid.

## Self-test (run this after touching render.py)

```bash
python3 scripts/flipbook/selftest.py     # ~5 s, no Blender, no bake
```

Renders a SYNTHETIC column whose shape is known exactly and asserts the sheet
comes back with it: tall, unclipped, rising up the rows. Both renderer bugs so
far were invisible on a real bake — nobody knows what a flame *should* look
like, so a stretched or transposed one just looks "wrong somehow" — and obvious
on a shape we authored ourselves.

## Landmines already paid for

- **`cache_type` must stay `REPLAY`.** Setting it to `'ALL'` makes
  `bpy.ops.fluid.bake_all()` die with
  `AttributeError: 'NoneType' object has no attribute 'getDataPointer'` — and
  `'ALL'` is a perfectly valid enum value, so nothing warns. This cost a long
  detour: it was blamed on a stale cache (wiping `build_cache` did make one run
  succeed, by coincidence), then on the environment, and a Blender restart did
  not fix it. What found it was bisecting ONE setting at a time against a
  minimal scene that baked cleanly with Blender's defaults: cache dir OK,
  resolution OK, adaptive-domain OK, `cache_type='ALL'` FAILED. Under REPLAY the
  solver steps as frames are set, which is what the dump loop walks anyway.
- **A failed bake used to be invisible**: the pipeline rendered the OLD grids
  happily and the sheet looked like the change did nothing — every measurement
  after that described the previous sheet. `bake.py` now returns non-zero on a
  bake exception AND when fewer frames land than were asked for, and `make.py`
  stops on it.
- **The cell is square; the domain usually is not.** Stretching each axis to
  the full cell smeared a 34x34x96 grid 2.8x horizontally. That single bug
  produced three separate-looking complaints: the fire did not read as fire, the
  smoke reached the cell edge and was clipped there, and the height/width audit
  said 0.60 for a plume that is actually tall. The renderer now fits the domain
  at its true aspect and leaves the rest transparent.
- **`argparse.parse_args()` with no argument** reads `sys.argv`, which under
  Blender still holds Blender's own flags — always `parse_args(argv)`.
- **The grids must come from the EVALUATED object** (`obj.evaluated_get(dg)`);
  the original datablock's modifier holds no simulation state and dumps zeros.
- **The dumped grids are BASE resolution.** Mantaflow's wavelet upres lives in a
  grid Python does not expose, so raise `--res` instead of `--noise`; the GPU
  marcher can afford it.
- **Soot is conserved unless `dissolve` is set** — it fills the domain within a
  dozen frames and the silhouette becomes the box wall (measured 66.8% coverage
  with a rectangular outline).

## Superseded

`scripts/gen_fire_flipbook.py` (Blender + Cycles) and
`scripts/manta_fire_flipbook.py` (Mantaflow + Eevee) are replaced by this
folder. `scripts/sim_fire_flipbook.py` is a pure-numpy solver kept only as a
no-Blender fallback. All three can be deleted once this pipeline has produced
the sheets that ship.
