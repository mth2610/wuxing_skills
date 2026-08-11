#!/usr/bin/env python3
"""Stage 1 of 3 — the fluid solver (Taichi, GPU).

    python3 scripts/flipbook/ti_sim.py fire_puff --res 96 --frames 64
    python3 scripts/flipbook/ti_sim.py fire_puff --res 96 --radial 9 --curl 3.5

Stage 1 of three: writes `build_cache/<name>/f###.npz`, which `render.py` then
marches and `pack.py` packs. See README.md for the whole loop and for the
landmines (framing, resolution transfer, wall contamination).

WHY IT REPLACED BLENDER/MANTAFLOW (28/07/2026)

  1. **Speed.** Same config, res 64 / 24 frames: Mantaflow 99.9 s, this 3.7 s.
     At res 112 / 64 frames it was 1037 s against ~60 s — the difference between
     tuning a look and guessing at it.
  2. **The physics the owner asked for.** A puff has "no buoyancy, no gravity —
     only radial expansion, curl noise and viscosity". Mantaflow's gas domain
     has no radial force and no viscosity, so its preset faked them with
     inflow-along-normals and dissolve speed. Here each is a term of its own.

  What was given up: Mantaflow's combustion model and wavelet turbulence. `git
  log` has `bake.py` if a future effect wants them back.
"""

import argparse
import os
import sys
import time

import numpy as np
import taichi as ti

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.abspath(os.path.join(HERE, "..", "..", "build_cache"))


# Every preset number is quoted AT THIS RESOLUTION; everything that is not
# already a pure fraction of the domain is rescaled from here (see main()).
REF_RES = 64.0

PRESETS = {
    # Radial burst: expands equally in every direction, no lift, no fall.
    # The ignition volume is SMALL: a puff starts as a point and expands. Seeding a
    # large sphere means the first frames are already a big ball and the growth
    # has nowhere to read from — the sim spends the sheet deforming a blob
    # instead of expanding one.
    "fire_puff": dict(dt=0.9, gravity=0.0, flat=1.0, shell=0.0, impulse=0.22, fuel_dens=1.0, 
        # Small ignition volume: a puff starts as a point and expands. Seeding a
        # large sphere means the first frames are already a big ball.
        # fuel_frames is what decides how much of the sheet is still FIRE: at
        # 0.16 the flame was 2% of the sheet against 17% smoke, which reads as
        # smoke with a hot core. 0.35 with the slower cooling below triples it.
        fuel_radius=0.05, fuel_frames=0.35,
        # RADIAL is an impulse (see the decay envelope in the step loop), so it
        # can be strong: held on as a constant force it shreds the puff into
        # filaments instead of inflating lobes. It came down from 26 because at
        # 26 the puff reached the domain WALL before the last frame (r90 1.27 of
        # the half-width, i.e. into the corners) and the boundary clamp then
        # manufactures density — see the wall warning ti_sim prints.
        radial=8.0,
        curl=2.4, swirl=4.0,
        # Diffusion rounds filaments into convex billows, and is the trade
        # between "one welded mass" (0.16) and separable lobes.
        diffuse=0.05,
        # Viscosity is a slow drag, NOT a brake: at 1.7 with dt 0.3 it multiplies
        # velocity by exp(-0.51) every substep and the puff never leaves its
        # ignition volume.
        viscosity=0.22, buoyancy=0.0,
        # Turbulence cells across the domain: the lobe COUNT knob.
        eddy=21.0,
        # Radiative + convective cooling; T^4 alone stalls below T~0.5 and the
        # sheet burns to the last frame. 0.5 put the flame out by frame 20 of 64.
        cool=0.22, soot=0.8),

    # A SMOKE puff, as a sprite. Same radial-burst skeleton as fire_puff, with
    # the combustion turned into an instant hand-off: cool is high and soot is
    # 1.0, so the little heat that is injected becomes density within a few
    # frames and the sheet is smoke from the start rather than fire that fades.
    #
    # BUOYANCY STAYS NEAR ZERO even though smoke rises. The rise belongs to the
    # PARTICLE, not to the sheet: bake it in and the engine's own upward
    # velocity double-counts it, while the puff also drifts off the cell centre
    # and the autofit crop (which is symmetric about that centre) pays for the
    # empty half. A small value is enough to break the symmetry.
    "smoke_puff": dict(dt=0.9, gravity=0.0, flat=1.0, shell=0.0, impulse=0.22, fuel_dens=1.0, 
        fuel_radius=0.06, fuel_frames=0.18,
        radial=4.0, curl=3.2, swirl=4.0,
        # Smoke is rounder than flame: it has no thin licking tongues, so it
        # takes more diffusion before the silhouette reads as billows.
        diffuse=0.06, eddy=34.0,
        viscosity=0.30, buoyancy=1.5,
        cool=3.0, soot=1.0),

    # DUST, as ONE SMALL PARCEL inside a larger cloud — which is what a sprite
    # in a flipbook actually is (owner, 28/07/2026). That framing decides two
    # terms by itself:
    #
    #   gravity 0 and flat 1.0. Settling and the wide, low shape of an impact
    #     cloud are properties of the CLOUD — where the composition places its
    #     sprites and what force field it moves them with. A parcel a few
    #     centimetres across expands isotropically; baking a fall or a squash
    #     into the sheet applies the cloud's behaviour a second time, per sprite.
    #     (Measured what happens if you do it anyway: gravity 4.0 piled 16.4% of
    #     the mass onto the domain floor, where the boundary clamp then
    #     manufactures more.)
    #
    # What is left to distinguish dust from smoke at THIS scale is small, and
    # honest: dust is GRAINIER (finer eddies, less diffusion) and its shape
    # stops evolving sooner (higher viscosity, shorter impulse) because the
    # particles are heavy and the parcel loses its momentum quickly. Smoke keeps
    # rolling for the whole sheet.
    "dust_puff": dict(dt=0.9, gravity=0.0, flat=1.0, shell=0.0, fuel_dens=1.0,
        # Smaller seed and earlier breakup: a dust card is one parcel, never a
        # self-contained smoke cloud. The cloud-scale flattening stays in the
        # emitter; this parcel only needs grain and a short rolling breakup.
        fuel_radius=0.040, fuel_frames=0.060, impulse=0.12,
        # A dust parcel starts as several touching clumps, not one perfectly
        # smooth fuel ball. The renderer can shade real lobes; it cannot invent
        # them from a uniform density field after the fact.
        source_lobes=5,
        radial=4.4, curl=3.8, swirl=3.8,
        diffuse=0.020, eddy=54.0,
        viscosity=0.95, buoyancy=0.0,
        # Dust loses visibility quickly; leaving smoke's 0.06 decay here is
        # why the final atlas rows became one static white cloud.
        # Leave a faint but usable tail at f064. The prior 0.11 made the last
        # three sampled atlas cells empty; a flipbook is a timed event, not a
        # 13-frame effect padded with transparent slots.
        dissipate=0.075, cool=3.0, soot=1.0),

    # ENERGY EXPLOSION — the one preset that is NOT composed from parcels.
    #
    # The reference (owner, 28/07/2026) is a single CORRELATED structure: each
    # filament runs continuously from the core to the rim and its shape depends
    # on its neighbours. That is why it must be one large sheet rather than many
    # small sprites — cut it up and the correlation, which IS the look, is gone.
    #
    # Almost every number is the OPPOSITE of the smoke/dust presets, and for the
    # same reasons stated there, inverted:
    #   diffuse ~0   — diffusion is what rounds filaments into convex billows.
    #                  Cauliflower wanted it; wisps are destroyed by it.
    #   shell 0.72   — a detonation ignites a SURFACE. The dark core in the
    #                  reference is where the fuel never was, not a flame dying.
    #   eddy 60      — fine turbulence, so the wisps are thin.
    #   radial 22    — a violent, brief impulse. Strong advection is what STRETCHES
    #                  the noise into radial streaks; without it the curl just
    #                  stirs a ball.
    #   soot 0.10    — energy, not fire: almost nothing is left behind as smoke.
    #   cool 0.30    — it must stay emissive across the sheet, since the FLAME
    #                  channel is what gets drawn (additive).
    "energy_burst": dict(dt=0.32, gravity=0.0, flat=1.0, impulse=0.07, fuel_dens=0.18,
        shell=0.72, fuel_radius=0.15, fuel_frames=0.06,
        radial=22.0, curl=4.5, swirl=6.5,
        diffuse=0.004, eddy=60.0,
        viscosity=0.12, buoyancy=0.0,
        cool=0.30, soot=0.10),

    # A rising flame: buoyancy on, radial off.
    "fire": dict(dt=0.9, gravity=0.0, flat=1.0, shell=0.0, impulse=0.22, fuel_dens=1.0, eddy=21.0, diffuse=0.10, radial=1.2, curl=2.4, viscosity=1.2, buoyancy=17.0,
                 cool=2.2, fuel_frames=0.75, fuel_radius=0.16,
                 soot=0.5, swirl=3.4),
    "smoke": dict(dt=0.9, gravity=0.0, flat=1.0, shell=0.0, impulse=0.22, fuel_dens=1.0, eddy=21.0, diffuse=0.18, radial=0.8, curl=2.0, viscosity=1.6, buoyancy=6.0,
                  cool=3.0, fuel_frames=0.30, fuel_radius=0.18,
                  soot=0.9, swirl=2.2),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("preset", choices=sorted(PRESETS))
    ap.add_argument("--name", default=None)
    ap.add_argument("--res", type=int, default=96)
    ap.add_argument("--frames", type=int, default=64)
    ap.add_argument("--substeps", type=int, default=3)
    ap.add_argument("--jacobi", type=int, default=40)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--arch", default="gpu", choices=["gpu", "cpu"],
                    help="Taichi backend; use cpu when Metal shader/cache setup fails")
    # Every physical term is a flag: this is the loop an artist runs.
    ap.add_argument("--radial", type=float, default=None, help="outward push from the centre")
    ap.add_argument("--curl", type=float, default=None, help="curl-noise forcing")
    ap.add_argument("--viscosity", type=float, default=None, help="velocity damping")
    ap.add_argument("--buoyancy", type=float, default=None, help="0 for a puff")
    ap.add_argument("--dt", type=float, default=None,
                    help="simulated time per frame — how much of the EVENT the "
                         "sheet covers. See the note at the step loop.")
    ap.add_argument("--impulse", type=float, default=None,
                    help="how long the radial impulse lasts, as a fraction of "
                         "the sheet. A puff coasts after a shove (0.22); a "
                         "detonation is over in a few frames")
    # THE DOMAIN IS FIXED, SO THE IGNITION IS WHAT SETS THE HEADROOM.
    # A puff that reaches the wall has its silhouette clamped into the box, and
    # no crop repairs that: zooming in hides it and zooming out frames it. The
    # only real remedy is to start smaller relative to the domain, which is
    # this. Lower it whenever the run warns about the wall — the fix is not
    # --radial, which was measured moving the 90th-percentile reach only
    # 1.28 -> 1.30 across 8.0 -> 3.0.
    ap.add_argument("--fuel-radius", type=float, default=None,
                    help="ignition ball radius as a fraction of the domain. "
                         "Smaller = more room to expand before hitting the wall")
    ap.add_argument("--fuel-dens", type=float, default=None,
                    help="density injected with the heat. Low = ENERGY (little "
                         "smoke left behind), high = fire that turns to soot")
    ap.add_argument("--shell", type=float, default=None,
                    help="0 = ignite a solid ball; 0..1 = ignite a hollow SHELL "
                         "at that fraction of --fuel-radius. A detonation lights "
                         "a surface, and the dark core is where the fuel was not")
    ap.add_argument("--flat", type=float, default=None,
                    help="vertical scale on the radial push. 1 = a ball, "
                         "<1 = spreads sideways like impact dust")
    ap.add_argument("--gravity", type=float, default=None,
                    help="settling force on DENSITY (dust), along the same "
                         "vertical axis buoyancy uses")
    ap.add_argument("--cool", type=float, default=None, help="T^4 cooling into soot")
    ap.add_argument("--swirl", type=float, default=None, help="vorticity confinement")
    ap.add_argument("--eddy", type=float, default=None,
                    help="turbulence cells across the DOMAIN. This is the lever "
                         "for how MANY lobes there are: curl sets how hard the "
                         "field stirs, --eddy sets at what size. Raising curl to "
                         "get more billows instead just transports the puff "
                         "further and runs it into the wall.")
    ap.add_argument("--diffuse", type=float, default=None,
                    help="smoothing that rounds filaments into convex billows. "
                         "LOW = many separate lobes, HIGH = one welded mass.")
    ap.add_argument("--soot", type=float, default=None,
                    help="how much of the heat lost to cooling becomes smoke")
    ap.add_argument("--fuel-frames", type=float, default=None,
                    help="fraction of the sheet over which fuel is injected; the "
                         "lever that decides how much of the sheet is still FIRE")
    ap.add_argument("--dissipate", type=float, default=None,
                    help="density loss per simulated second; dust needs a short visual tail")
    ap.add_argument("--source-lobes", type=int, default=None,
                    help="number of overlapping seed lobes (1 = legacy smooth ball)")
    args = ap.parse_args()

    p = dict(PRESETS[args.preset])
    # Every knob is quoted at res 64 (see the scaling below), so an override
    # means the same shape whatever --res it is applied at.
    for k in ("radial", "curl", "viscosity", "buoyancy", "cool", "swirl",
              "diffuse", "soot", "fuel_frames", "eddy", "gravity", "flat",
              "shell", "impulse", "fuel_dens", "dt", "dissipate",
              "fuel_radius"):
        if getattr(args, k) is not None:
            p[k] = getattr(args, k)
    if args.source_lobes is not None:
        p["source_lobes"] = args.source_lobes
    p.setdefault("source_lobes", 1)

    # RESOLUTION-INDEPENDENT FORCES. Velocities here are in VOXELS per step, so
    # the same number moves gas further in relative terms on a finer grid: the
    # preset tuned at res 64 filled 24% of the cell and the identical preset at
    # res 112 filled 85%. Scaling the advective forces by 64/N makes a preset
    # mean the same shape at any resolution — which is what stops quick-tuning
    # from lying about the full bake (the trap that cost a 17-minute Mantaflow
    # run earlier).
    res_k = REF_RES / args.res
    for k in ("radial", "curl", "buoyancy", "swirl", "gravity"):
        p[k] *= res_k

    # DIFFUSION SCALES BY THE SQUARE, not linearly, and it was not scaled at all.
    # It is a per-step fraction of the 6-neighbour average, so over a fixed number
    # of steps it smooths a length of ~sqrt(k) VOXELS — a length that shrinks
    # relative to the domain as the grid gets finer. Holding the smoothed length
    # at a constant fraction of the domain therefore needs k * (N/64)^2, whereas
    # a velocity (voxels per step) only needs 64/N. Same trap as the forces, one
    # power apart: before this, a preset dialled in at res 64 came back at res 112
    # with different lobes and there was no way to tune cheaply.
    diff_k = p["diffuse"] / (res_k * res_k)
    if diff_k > 0.9:
        # dens <- (1-k)*dens + k*avg is a convex blend only for k <= 1; past that
        # the explicit step oscillates and the field goes negative.
        print("SIM: diffuse %.2f clamped to 0.90 (res %d is past the explicit "
              "stability limit for this preset)" % (diff_k, args.res))
        diff_k = 0.9
    p["diffuse"] = diff_k

    # The pressure solve is the third quantity measured in voxels: a Jacobi
    # sweep moves information ONE cell, so a fixed iteration count reaches a
    # shorter fraction of a finer domain and leaves the projection more
    # under-converged there. Iterations therefore scale with N, not with nothing.
    jacobi_iters = max(8, int(round(args.jacobi * args.res / REF_RES)))

    N = args.res
    name = args.name or args.preset
    out_dir = os.path.join(CACHE, name)
    os.makedirs(out_dir, exist_ok=True)
    for f in os.listdir(out_dir):
        if f.endswith(".npz"):
            os.remove(os.path.join(out_dir, f))

    ti.init(arch=ti.gpu if args.arch == "gpu" else ti.cpu, random_seed=args.seed)

    u = ti.Vector.field(3, ti.f32, shape=(N, N, N))
    u_tmp = ti.Vector.field(3, ti.f32, shape=(N, N, N))
    dens = ti.field(ti.f32, shape=(N, N, N))
    temp = ti.field(ti.f32, shape=(N, N, N))
    fld_tmp = ti.field(ti.f32, shape=(N, N, N))
    div = ti.field(ti.f32, shape=(N, N, N))
    pre = ti.field(ti.f32, shape=(N, N, N))
    pre2 = ti.field(ti.f32, shape=(N, N, N))
    # A real random field, generated once on the CPU and uploaded. The first
    # version built "noise" from sums of sin(x), sin(y), sin(z) — which is a
    # periodic LATTICE, and it stamped a visible grid pattern into the puff. The
    # Mali ban on fract(sin(...)) is a rule for SHADERS on that device; this is
    # an offline script and can simply use randomness.
    # It does not need to be divergence-free either: the pressure projection two
    # steps later removes whatever divergence it introduces.
    noise = ti.Vector.field(3, ti.f32, shape=(N, N, N))

    def build_noise(seed):
        rng = np.random.default_rng(seed)
        # Coarse random, then smoothed: smoothing is what turns white noise into
        # something with EDDIES. Unsmoothed noise just jitters each cell.
        # Cell size matters relative to the FEATURE being stirred. At N//8 the
        # noise cell was 8 voxels while the ignition volume is ~3, so inside the
        # puff the field was effectively constant — turbulence that acts as a
        # uniform push is just wind, which is why it kept blowing sideways.
        # N//3 puts several cells across even the initial blob.
        # The eddy size is a fraction of the DOMAIN, not a count of voxels.
        # It used to be N//3 cells, i.e. an eddy 3 voxels wide at every
        # resolution — so at res 112 the turbulence stirred features 1.8x
        # smaller (relative to the puff) than the same preset did at res 64.
        # That is the largest remaining reason a preset did not survive a
        # resolution change; the force scaling and the diffusion coefficient
        # are both smaller effects than this one.
        M = max(4, int(round(p["eddy"])))
        low = rng.standard_normal((M + 2, M + 2, M + 2, 3)).astype(np.float32)
        for _ in range(1):
            low = (low + np.roll(low, 1, 0) + np.roll(low, -1, 0)
                   + np.roll(low, 1, 1) + np.roll(low, -1, 1)
                   + np.roll(low, 1, 2) + np.roll(low, -1, 2)) / 7.0
        idx = (np.arange(N) * (low.shape[0] - 1) / N).astype(np.int32)
        big = low[np.ix_(idx, idx, idx)]
        # ZERO-MEAN, per channel. A smoothed random field keeps a large-scale
        # bias, and a constant force applied every substep is not turbulence —
        # it is wind: the puff came out blown sideways into a comet.
        big -= big.mean(axis=(0, 1, 2), keepdims=True)
        big /= (np.abs(big).max() + 1e-6)
        noise.from_numpy(np.ascontiguousarray(big, np.float32))

    build_noise(args.seed)

    @ti.func
    def samp(f, pos):
        q = ti.math.clamp(pos, 0.0, N - 1.001)
        i = ti.cast(q, ti.i32)
        w = q - i
        c = 0.0
        for dx, dy, dz in ti.static(ti.ndrange(2, 2, 2)):
            wt = (w.x if dx else 1 - w.x) * (w.y if dy else 1 - w.y) * (w.z if dz else 1 - w.z)
            c += wt * f[i.x + dx, i.y + dy, i.z + dz]
        return c

    @ti.func
    def sampv(f, pos):
        q = ti.math.clamp(pos, 0.0, N - 1.001)
        i = ti.cast(q, ti.i32)
        w = q - i
        c = ti.Vector([0.0, 0.0, 0.0])
        for dx, dy, dz in ti.static(ti.ndrange(2, 2, 2)):
            wt = (w.x if dx else 1 - w.x) * (w.y if dy else 1 - w.y) * (w.z if dz else 1 - w.z)
            c += wt * f[i.x + dx, i.y + dy, i.z + dz]
        return c

    @ti.kernel
    def add_fuel(t: ti.f32, dt: ti.f32, rad: ti.f32, radial: ti.f32,
                 shell: ti.f32, kdens: ti.f32, source_lobes: ti.i32):
        c = ti.Vector([N * 0.5, N * 0.5, N * 0.5])
        r = rad * N
        for I in ti.grouped(dens):
            d = (ti.cast(I, ti.f32) - c).norm()
            m = 0.0
            if source_lobes <= 1:
                if d < r:
                    m = (1.0 - d / r) ** 0.7
            else:
                # Five overlapping, asymmetric seed clumps. Their locations
                # are fixed in local space so the event evolves coherently
                # frame-to-frame; only the existing cell jitter is random.
                for l in ti.static(range(5)):
                    off = ti.Vector([0.0, 0.0, 0.0])
                    if ti.static(l == 0): off = ti.Vector([-0.48, 0.10, -0.12])
                    if ti.static(l == 1): off = ti.Vector([ 0.42,-0.18,  0.16])
                    if ti.static(l == 2): off = ti.Vector([ 0.08, 0.44, -0.28])
                    if ti.static(l == 3): off = ti.Vector([-0.16,-0.36,  0.40])
                    if ti.static(l == 4): off = ti.Vector([ 0.34, 0.22,  0.36])
                    dl = (ti.cast(I, ti.f32) - (c + off * r)).norm()
                    m = ti.max(m, ti.max(0.0, 1.0 - dl / (r * 0.68)) ** 0.7)
            if m > 0.0:
                # SOLID BALL (shell = 0) or a HOLLOW SHELL. A detonation ignites
                # a surface, not a volume: the reference the owner gave is dark
                # in the middle with a bright, filamented rim, and that hole is
                # not the flame dying — it is where the fuel never was.
                if shell > 0.0:
                    w = r * (1.0 - shell)
                    m = ti.max(0.0, 1.0 - ti.abs(d - r * shell) / ti.max(w, 1e-3))
                # Slight per-cell jitter so the source is not a perfect ball —
                # a perfectly symmetric source produces a perfectly symmetric
                # puff, which is the "solid sphere" failure.
                m *= 0.6 + 0.4 * ti.random()
                temp[I] += m * 6.0 * dt
                dens[I] += m * 1.2 * kdens * dt
                if d > 0.001:
                    # A small launch kick only; the sustained expansion is a
                    # force on the gas itself, applied in forces().
                    u[I] += (ti.cast(I, ti.f32) - c).normalized() * (0.15 * radial * m * dt)

    @ti.kernel
    def forces(dt: ti.f32, buoy: ti.f32, curl: ti.f32, visc: ti.f32,
               swirl: ti.f32, t: ti.f32, radial: ti.f32, decay: ti.f32,
               grav: ti.f32, flat: ti.f32):
        c = ti.Vector([N * 0.5, N * 0.5, N * 0.5])
        for I in ti.grouped(u):
            v = u[I]
            # RADIAL pressure, applied to all HOT gas rather than only inside the
            # ignition volume. Putting it in add_fuel meant it only ever pushed
            # the first three voxels for the first 10% of the sheet, so raising
            # it from 13 to 26 changed nothing measurable — an expansion needs a
            # force on the expanding material, not on its source.
            d = ti.cast(I, ti.f32) - c
            dist = d.norm()
            if dist > 0.5:
                # DECAYING impulse, not a constant force. Held on, the radial
                # term keeps accelerating every parcel outward and the puff
                # shreds into filaments; a real puff gets its momentum at
                # ignition and then coasts, which is what lets surface tension —
                # here, diffusion — round the lobes back up. Cauliflower is
                # LOBES, and lobes need the flow to settle.
                # FLATTEN: impact dust spreads along the ground, it does not
                # inflate a ball. Scaling the vertical component of the push is
                # the only part of that a SPRITE can carry — the fall itself
                # belongs to the particle, exactly as buoyancy does for smoke.
                dir = ti.Vector([d.x, d.y, d.z * flat])
                v += dt * radial * decay * (temp[I] + 0.35 * dens[I]) * dir / dist
            # UP IS Z, not Y. The renderer's image Y is the grid's z
            # (`gz = (1-v)*(rz-1)`) and its ray marches along y, so a force on
            # v.y pushes the puff straight AWAY FROM THE CAMERA. Measured with
            # buoyancy 40: the centroid moved 27.8 -> 43.8 along the view ray
            # while the render's up axis went 24.1 -> 22.5. It was invisible
            # until now only because both shipped puffs have buoyancy ~0.
            v.z += dt * buoy * temp[I]
            # GRAVITY, on DENSITY rather than temperature: dust is heavy
            # particulate that settles, which is the whole difference between a
            # dust puff and a smoke puff. Buoyancy cannot express it — it scales
            # with temp, and dust is cold, so a negative buoyancy does nothing.
            v.z -= dt * grav * dens[I]
            # Turbulent forcing, scaled by how much material is here: empty
            # cells should not be stirred.
            v += dt * curl * noise[I] * (0.4 + dens[I] + temp[I])
            # Viscosity: plain velocity damping, the owner's third term.
            v *= ti.exp(-visc * dt)
            u_tmp[I] = v
        for I in ti.grouped(u):
            u[I] = u_tmp[I]
        # Vorticity confinement, re-injecting the swirl advection eats.
        for I in ti.grouped(u):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                wx = (u[I + ti.Vector([0, 1, 0])].z - u[I - ti.Vector([0, 1, 0])].z
                      - u[I + ti.Vector([0, 0, 1])].y + u[I - ti.Vector([0, 0, 1])].y)
                wy = (u[I + ti.Vector([0, 0, 1])].x - u[I - ti.Vector([0, 0, 1])].x
                      - u[I + ti.Vector([1, 0, 0])].z + u[I - ti.Vector([1, 0, 0])].z)
                wz = (u[I + ti.Vector([1, 0, 0])].y - u[I - ti.Vector([1, 0, 0])].y
                      - u[I + ti.Vector([0, 1, 0])].x + u[I - ti.Vector([0, 1, 0])].x)
                w = ti.Vector([wx, wy, wz]) * 0.5
                if w.norm() > 1e-4:
                    u_tmp[I] = u[I] + dt * swirl * 0.02 * w
                else:
                    u_tmp[I] = u[I]
            else:
                u_tmp[I] = u[I]
        for I in ti.grouped(u):
            u[I] = u_tmp[I]

    @ti.kernel
    def divergence():
        for I in ti.grouped(div):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                div[I] = 0.5 * (u[I + ti.Vector([1, 0, 0])].x - u[I - ti.Vector([1, 0, 0])].x
                                + u[I + ti.Vector([0, 1, 0])].y - u[I - ti.Vector([0, 1, 0])].y
                                + u[I + ti.Vector([0, 0, 1])].z - u[I - ti.Vector([0, 0, 1])].z)
            else:
                div[I] = 0.0
            pre[I] = 0.0

    @ti.kernel
    def jacobi(src: ti.template(), dst: ti.template()):
        for I in ti.grouped(dst):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                dst[I] = (src[I + ti.Vector([1, 0, 0])] + src[I - ti.Vector([1, 0, 0])]
                          + src[I + ti.Vector([0, 1, 0])] + src[I - ti.Vector([0, 1, 0])]
                          + src[I + ti.Vector([0, 0, 1])] + src[I - ti.Vector([0, 0, 1])]
                          - div[I]) / 6.0
            else:
                dst[I] = 0.0

    @ti.kernel
    def subtract_gradient():
        for I in ti.grouped(u):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                u[I] -= 0.5 * ti.Vector([
                    pre[I + ti.Vector([1, 0, 0])] - pre[I - ti.Vector([1, 0, 0])],
                    pre[I + ti.Vector([0, 1, 0])] - pre[I - ti.Vector([0, 1, 0])],
                    pre[I + ti.Vector([0, 0, 1])] - pre[I - ti.Vector([0, 0, 1])]])

    @ti.kernel
    def advect(dt: ti.f32):
        for I in ti.grouped(u):
            back = ti.cast(I, ti.f32) - u[I] * dt
            u_tmp[I] = sampv(u, back)
        for I in ti.grouped(dens):
            back = ti.cast(I, ti.f32) - u[I] * dt
            fld_tmp[I] = samp(dens, back)
        for I in ti.grouped(dens):
            dens[I] = fld_tmp[I]
        for I in ti.grouped(temp):
            back = ti.cast(I, ti.f32) - u[I] * dt
            fld_tmp[I] = samp(temp, back)
        for I in ti.grouped(temp):
            temp[I] = fld_tmp[I]
        for I in ti.grouped(u):
            u[I] = u_tmp[I]

    @ti.kernel
    def diffuse(k: ti.f32):
        """Smooth density and temperature toward their neighbours.

        This is what makes a puff look like CAULIFLOWER instead of a shredded
        cloud: without it the advected field keeps every filament the velocity
        field ever drew, so the silhouette is spiky. Diffusion rounds each lobe
        back into a convex blob while leaving the large-scale structure alone.
        """
        for I in ti.grouped(dens):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                avg = 0.0
                avt = 0.0
                for o in ti.static(range(3)):
                    e = ti.Vector([1 if o == 0 else 0, 1 if o == 1 else 0, 1 if o == 2 else 0])
                    avg += dens[I + e] + dens[I - e]
                    avt += temp[I + e] + temp[I - e]
                fld_tmp[I] = dens[I] + k * (avg / 6.0 - dens[I])
                div[I] = temp[I] + k * (avt / 6.0 - temp[I])
            else:
                fld_tmp[I] = dens[I]
                div[I] = temp[I]
        for I in ti.grouped(dens):
            dens[I] = fld_tmp[I]
            temp[I] = div[I]

    @ti.kernel
    def cool(dt: ti.f32, rate: ti.f32, soot: ti.f32, dissipate: ti.f32):
        for I in ti.grouped(temp):
            # T^4 radiative loss, and what it loses becomes soot: this is the
            # hand-off that makes fire read as fire rather than orange smoke.
            # T^4 alone stalls: at T=0.3 it is 0.008, so a puff that has cooled
            # below ~0.5 essentially stops cooling and the sheet stays on fire to
            # the last frame. Real cooling is radiative (T^4) PLUS convective
            # (linear); the linear term is what actually finishes the job.
            loss = rate * (temp[I] ** 4 + 0.45 * temp[I]) * dt
            temp[I] = ti.max(0.0, temp[I] - loss)
            # 0.25 emptied the last three rows of a 64-frame sheet; soot should
            # outlive the flame by a long way.
            dens[I] = (dens[I] + loss * soot) * ti.max(0.0, 1.0 - dissipate * dt)

    # Simulated time per FRAME. The sheet always has --frames cells, so this is
    # what decides how much of an EVENT they cover: a puff drifting for a second
    # (0.9) or a detonation that is over in a fifth of one (0.3). Lowering it is
    # how a violent impulse fits inside the domain — lowering the FORCE instead
    # also removes the strong advection that stretches the noise into filaments,
    # which is the entire look of an explosion (measured: radial 22 -> 12 took
    # lobes from 2.19 to 1.20).
    dt = p["dt"]
    t0 = time.time()
    wall_max = 0.0
    r90 = 0.0
    # Radius of every voxel, as a fraction of the domain HALF-width: the one
    # extent measurement that does not go through the renderer, so it compares
    # two resolutions (or a sim against a sheet) without the framing, the
    # extinction scale or the alpha threshold in the way.
    ax = (np.arange(N, dtype=np.float32) - (N - 1) / 2.0) / ((N - 1) / 2.0)
    rad = np.sqrt(ax[:, None, None] ** 2 + ax[None, :, None] ** 2 + ax[None, None, :] ** 2)
    rad_flat = rad.ravel()
    rad_order = np.argsort(rad_flat)
    rad_sorted = rad_flat[rad_order]
    for f in range(args.frames):
        for _ in range(args.substeps):
            frac = f / max(1, args.frames - 1)
            if frac < p["fuel_frames"]:
                add_fuel(frac, dt / args.substeps, p["fuel_radius"], p["radial"],
                         p["shell"], p["fuel_dens"], p["source_lobes"])
            # Impulse envelope: full push while the fuel burns, then off.
            # How long the radial impulse lasts, as a fraction of the sheet.
            # A puff coasts after a gentle shove (0.22); a DETONATION is over in
            # a few frames and everything after is momentum.
            decay = max(0.0, 1.0 - (f / max(1, args.frames - 1))
                        / max(p["impulse"], 1e-3)) ** 2
            forces(dt / args.substeps, p["buoyancy"], p["curl"], p["viscosity"],
                   p["swirl"], f * 0.1, p["radial"], decay, p["gravity"],
                   p["flat"])
            divergence()
            for k in range(jacobi_iters // 2):
                jacobi(pre, pre2)
                jacobi(pre2, pre)
            subtract_gradient()
            advect(dt / args.substeps)
            cool(dt / args.substeps, p["cool"], p["soot"], p.get("dissipate", 0.06))
            diffuse(p["diffuse"])

        d = dens.to_numpy()
        tp = temp.to_numpy()
        tot = float(d.sum())
        if tot > 1e-6:
            shell = tot - float(d[2:-2, 2:-2, 2:-2].sum())
            wall_max = max(wall_max, shell / tot)
            w = d.ravel()[rad_order]
            r90 = float(rad_sorted[np.searchsorted(np.cumsum(w), 0.9 * tot)])
        # Layout [z][y][x], which is what render.py indexes.
        np.savez_compressed(
            os.path.join(out_dir, "f%03d.npz" % (f + 1)),
            density=np.ascontiguousarray(d.transpose(2, 1, 0), np.float16),
            flame=np.ascontiguousarray(tp.transpose(2, 1, 0), np.float16),
            temperature=np.ascontiguousarray(tp.transpose(2, 1, 0), np.float16),
            res=np.array([N, N, N], np.int32))
        if f % 8 == 0:
            print("SIM %2d/%d  %.1fs  d.max=%.3f T.max=%.3f"
                  % (f, args.frames, time.time() - t0, float(d.max()), float(tp.max())),
                  flush=True)

    print("SIM: %d frames in %.1fs -> %s" % (args.frames, time.time() - t0, out_dir))
    print("SIM: r90 %.2f of the domain half-width (last frame), wall shell %.1f%% (peak)"
          % (r90, wall_max * 100))
    if wall_max > 0.02 or r90 > 1.0:
        # Advection samples are CLAMPED at the boundary, so material pressed
        # against a wall is re-sampled from itself and the solver manufactures
        # density there. A run in that state is not a smaller version of the
        # same effect — it is a different one, and it cannot be compared with a
        # run at another resolution. r90 > 1.0 means the mass has reached past
        # the inscribed sphere into the box CORNERS.
        print("SIM: WARNING the puff reached the domain wall — the silhouette is "
              "partly the box and the boundary clamp is creating mass. Lower "
              "--radial or shorten the run before comparing resolutions.")
    print("SIM: next  python3 scripts/flipbook/render.py %s --cell 256" % out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
