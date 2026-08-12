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

    # The packed VOLUME flame is one directionless combustion parcel.  It must
    # stay free of lift, fall, and any baked tongue silhouette: those are
    # emitter/force-field decisions.  Its job is a continuous local gas parcel
    # with a hot porous interior and soot that appears during cooling, never a
    # collection of visibly separate sub-puffs.
    #
    # This is intentionally separate from fire_puff.  The latter also feeds the
    # legacy split FLIPBOOK, whose existing look must not change when the volume
    # path is tuned.
    "fire_volume_puff": dict(
        # 64 cells cover a short, actively burning interval, not 57.6 seconds
        # of a steady combustor.  The previous dt=0.9 let the gas converge to
        # a stationary volume from about frame 16 onward.
        dt=0.2, gravity=0.0, flat=1.0, shell=0.0, impulse=0.22,
        # Fuel is separate from smoke density and temperature.  Re-injecting
        # density directly made the steady source converge to one frozen blob.
        # A broad, low-density source gives the renderer a real soft envelope.
        # Dense fuel made one opaque hot nugget per card; that cannot stack
        # gracefully because overlapping billboards immediately read as tiles.
        fuel_dens=0.42, burn=6.0, heat_yield=6.0, smoke_yield=0.15,
        fuel_radius=0.085,
        # This is a reusable particle texture, not a one-shot explosion.
        # Particle lifetime supplies the global fade; keeping the tiny central
        # combustor alive gives every later frame a hot, evolving gas envelope
        # instead of a frozen soot tail after frame 16.
        fuel_frames=1.0,
        # One connected ignition source.  Five explicit source lobes survived
        # packing as five bright islands, so a dense emitter read as glued-together
        # chunks instead of one turbulent volume.
        source_lobes=1,
        source_variation=0.55,
        radial=4.5, sustain_pressure=0.32, contain=0.8, curl=29.0, swirl=32.0,
        shape_noise=0.38,
        # Fine eddies and low viscosity break the density into gas parcels.
        # They do not pick an up direction; random billboard rotation preserves
        # this primitive's use for any emitter orientation.
        # Large, continually changing eddies. The prior fine 52-cell field
        # wrinkled the ignition edge, then the parcel settled into the same
        # cauliflower shape for most of frames 16..64.
        eddy=24.0, diffuse=0.018, viscosity=0.035, buoyancy=0.0,
        # Cooling is deliberately moderate: the flipbook must retain flame in
        # late cells, while the runtime particle owns the event-level fade.
        cool=0.8, soot=0.38,
        # Blend between two centred turbulence fields. This turns the gas over
        # temporally without sliding the parcel across its own cell.
        noise_phase_speed=0.9,
        # A combustion source cannot be perfectly constant if the clip is meant
        # to animate: constant fuel plus constant boundary conditions converges
        # to a static fluid equilibrium. This is an isotropic flow-rate pulse,
        # not a directional source-shape animation.
        source_pulse_rate=1.2,
        # Runtime particles appear continuously. Starting their animation from
        # an un-stirred ignition sphere made new particles pile up as bright
        # round marbles at the emitter foot. Warm the physical solver first;
        # saved frame 1 is already a living gas parcel, with no image-space
        # crop, time remap, or directional shape edit.
        warmup_frames=10, lock_center=1),

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

    # A direction-bearing flame tongue.  Unlike fire_volume_puff this sheet is
    # intentionally authored with +Z as up: it is one complete torch/jet lick,
    # not a reusable gas parcel.  A larger physical domain leaves headroom for
    # the plume; render.py later crops it to the cell without turning the roof
    # of the solver into a hard silhouette edge.
    "fire_tongue": dict(
        dt=0.16, gravity=0.0, flat=1.0, shell=0.0, impulse=0.22,
        fuel_dens=0.35, burn=6.0, heat_yield=6.0, smoke_yield=0.15,
        fuel_radius=0.08, fuel_frames=0.90,
        radial=0.70, sustain_pressure=0.12, contain=0.22,
        curl=18.0, swirl=18.0, shape_noise=0.60,
        diffuse=0.020, eddy=20.0, viscosity=0.10, buoyancy=12.0,
        cool=0.55, soot=0.20,
        source_variation=0.45, source_pulse_rate=1.40,
        noise_phase_speed=0.85, warmup_frames=0,
        domain=1.4),

    # Legacy rising-flame baseline. Kept reproducible for comparison; new work
    # must use fire_tongue above, whose shorter timeline and changing source do
    # not settle into the old column's near-static late frames.
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
    ap.add_argument("--warmup", type=int, default=None,
                    help="physical frames to solve before writing f001; useful for "
                         "continuous emitters whose first card must not be an "
                         "un-stirred ignition ball")
    ap.add_argument("--substeps", type=int, default=3)
    ap.add_argument("--jacobi", type=int, default=40)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--arch", default="gpu", choices=["gpu", "cpu"],
                    help="Taichi backend; use cpu when Metal shader/cache setup fails")
    # Every physical term is a flag: this is the loop an artist runs.
    ap.add_argument("--radial", type=float, default=None, help="outward push from the centre")
    ap.add_argument("--contain", type=float, default=None,
                    help="isotropic radial restoring pressure; keeps a reusable puff "
                         "from developing a baked long axis")
    ap.add_argument("--sustain-pressure", type=float, default=None,
                    help="fraction of radial pressure sustained by a continuous fuel "
                         "source after the ignition impulse")
    ap.add_argument("--shape-noise", type=float, default=None,
                    help="fraction of centred curl turbulence allowed to puff/indent "
                         "the boundary; 0 keeps it purely tangential")
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
    ap.add_argument("--domain", type=float, default=None,
                    help="widen the simulated box by this factor at the SAME "
                         "grid resolution. Use it when a run warns about the "
                         "wall: >1 gives the gas room without weakening the "
                         "turbulence that shapes its edge")
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
    ap.add_argument("--source-variation", type=float, default=None,
                    help="amount of smooth, directionless fuel-flow variation inside "
                         "one connected source (0 = uniform ball)")
    ap.add_argument("--noise-phase-speed", type=float, default=None,
                    help="radians per simulated second for blending two centred "
                         "turbulence fields (0 = static field)")
    ap.add_argument("--source-pulse-rate", type=float, default=None,
                    help="radians per simulated second for isotropic fuel-flow "
                         "pulses (0 = a constant combustor)")
    ap.add_argument("--lock-center", type=int, choices=[0, 1], default=None,
                    help="remove density-weighted bulk velocity each solver step. "
                         "Use 1 for directionless parcel assets so turbulence churns "
                         "inside the cell instead of becoming baked travel.")
    args = ap.parse_args()

    p = dict(PRESETS[args.preset])
    # Every knob is quoted at res 64 (see the scaling below), so an override
    # means the same shape whatever --res it is applied at.
    for k in ("radial", "contain", "sustain_pressure", "shape_noise", "curl", "viscosity", "buoyancy", "cool", "swirl",
              "diffuse", "soot", "fuel_frames", "eddy", "gravity", "flat",
              "shell", "impulse", "fuel_dens", "dt", "dissipate",
              "fuel_radius", "noise_phase_speed", "source_pulse_rate", "source_variation", "lock_center"):
        if getattr(args, k) is not None:
            p[k] = getattr(args, k)
    if args.source_lobes is not None:
        p["source_lobes"] = args.source_lobes
    p.setdefault("source_lobes", 1)
    p.setdefault("noise_phase_speed", 0.0)
    p.setdefault("source_pulse_rate", 0.0)
    p.setdefault("source_variation", 0.0)
    p.setdefault("lock_center", 0)
    p.setdefault("warmup_frames", 0)
    if args.warmup is not None:
        p["warmup_frames"] = max(0, args.warmup)
    p.setdefault("contain", 0.0)
    p.setdefault("sustain_pressure", 0.0)
    p.setdefault("shape_noise", 0.0)
    p.setdefault("domain", 1.0)
    domain = args.domain if args.domain is not None else p["domain"]
    p.setdefault("burn", 6.0)
    p.setdefault("heat_yield", 6.0)
    p.setdefault("smoke_yield", 0.15)

    # RESOLUTION-INDEPENDENT FORCES. Velocities are stored in VOXELS per step.
    # A fixed velocity therefore travels a *smaller* fraction of a finer grid,
    # not a larger one.  Physical advection must scale as N/REF_RES.  The
    # former inverse factor (REF_RES/N) made the 96³ production bake almost
    # motionless while a 32³ probe looked lively, hiding a frozen atlas tail.
    res_k = args.res / REF_RES

    # A BIGGER DOMAIN, WITHOUT A BIGGER GRID (--domain).
    #
    # The puff overruns the box: 90th-percentile reach 1.26-1.30 of the domain
    # half-width, ~6% wall shell, so the boundary clamp starts creating mass and
    # the silhouette becomes partly the box. No crop repairs that — zooming in
    # hides the clamped region, zooming out frames it.
    #
    # The knobs that should have fixed it do not: --radial 8.0 -> 3.0 moved the
    # reach 1.28 -> 1.30, and --fuel-radius 0.05 -> 0.028 moved it 1.28 -> 1.26.
    # What drives the expansion is --curl, which is also the only thing that
    # makes the edge wispy rather than a cutout — one variable, two opposed
    # consequences, impossible to tune apart.
    #
    # This separates them. `--domain D` means "the box is D times wider in
    # physical terms at the same grid", so the SAME curl produces the same
    # filaments while the gas covers a smaller fraction of the box. It is
    # exactly the res normalisation above with one more factor, and it scales
    # the same three quantities the same three ways:
    #   advective forces are voxels/step        -> 1/D
    #   diffusion smooths ~sqrt(k) voxels       -> 1/D^2 (one power apart, as
    #                                              the note below already says)
    #   fuel_radius is a fraction of the domain -> 1/D, so the ignition stays
    #                                              the same PHYSICAL size
    dom_k = 1.0 / max(domain, 1e-3)
    for k in ("radial", "contain", "sustain_pressure", "curl", "buoyancy", "swirl", "gravity"):
        p[k] *= res_k * dom_k
    p["fuel_radius"] *= dom_k

    # DIFFUSION SCALES BY THE SQUARE, not linearly, and it was not scaled at all.
    # It is a per-step fraction of the 6-neighbour average, so over a fixed number
    # of steps it smooths a length of ~sqrt(k) VOXELS — a length that shrinks
    # relative to the domain as the grid gets finer. Holding the smoothed length
    # at a constant fraction of the domain therefore needs k * (N/64)^2, whereas
    # a velocity (voxels per step) only needs 64/N. Same trap as the forces, one
    # power apart: before this, a preset dialled in at res 64 came back at res 112
    # with different lobes and there was no way to tune cheaply.
    diff_k = p["diffuse"] * (res_k * res_k) * (dom_k * dom_k)
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
    # The transport velocity stays cell-centred because semi-Lagrangian
    # backtracing samples it at arbitrary positions.  Pressure projection does
    # not: its divergence/gradient pair must live on a MAC grid.  Applying a
    # forward/backward pair to all three components at cell centres made a
    # directionless radial puff creep towards negative xyz even with every
    # directional force disabled.  These are the three face-centred components
    # used only while projecting, then averaged back into `u` for transport.
    u_x = ti.field(ti.f32, shape=(N + 1, N, N))
    u_y = ti.field(ti.f32, shape=(N, N + 1, N))
    u_z = ti.field(ti.f32, shape=(N, N, N + 1))
    dens = ti.field(ti.f32, shape=(N, N, N))
    temp = ti.field(ti.f32, shape=(N, N, N))
    fuel = ti.field(ti.f32, shape=(N, N, N))
    fld_tmp = ti.field(ti.f32, shape=(N, N, N))
    fuel_tmp = ti.field(ti.f32, shape=(N, N, N))
    div = ti.field(ti.f32, shape=(N, N, N))
    pre = ti.field(ti.f32, shape=(N, N, N))
    pre2 = ti.field(ti.f32, shape=(N, N, N))
    omega = ti.Vector.field(3, ti.f32, shape=(N, N, N))
    omega_mag = ti.field(ti.f32, shape=(N, N, N))
    # Directionless parcels need turbulent RELATIVE motion, never a net push.
    # These scalar reductions remove the density/heat-weighted bulk velocity
    # before advection, making the solver's own centre a conserved frame.
    bulk_mass = ti.field(ti.f32, shape=())
    bulk_vx = ti.field(ti.f32, shape=())
    bulk_vy = ti.field(ti.f32, shape=())
    bulk_vz = ti.field(ti.f32, shape=())
    # A real random field, generated once on the CPU and uploaded. The first
    # version built "noise" from sums of sin(x), sin(y), sin(z) — which is a
    # periodic LATTICE, and it stamped a visible grid pattern into the puff. The
    # Mali ban on fract(sin(...)) is a rule for SHADERS on that device; this is
    # an offline script and can simply use randomness.
    # It is a vector potential, not a direct force.  We curl it below so the
    # injected turbulence is divergence-free before pressure projection.
    noise = ti.Vector.field(3, ti.f32, shape=(N, N, N))
    noise_alt = ti.Vector.field(3, ti.f32, shape=(N, N, N))
    noise_alt2 = ti.Vector.field(3, ti.f32, shape=(N, N, N))
    noise_alt3 = ti.Vector.field(3, ti.f32, shape=(N, N, N))

    def build_noise(seed):
        rng = np.random.default_rng(seed)
        # Generate the vector potential at the solver resolution in Fourier
        # space, then band-limit it isotropically.  The old coarse-grid /
        # nearest-neighbour upsample placed fixed cubical force cells into the
        # bake.  Those grid seams survived as one diagonal plume even though
        # the average force was zero.  A radial spectral filter has no chosen
        # x/y/z axis and keeps the eddy scale stable in DOMAIN units.
        white = rng.standard_normal((N, N, N, 3)).astype(np.float32)
        wave = np.fft.fftn(white, axes=(0, 1, 2))
        freq = np.fft.fftfreq(N) * N
        kx, ky, kz = np.meshgrid(freq, freq, freq, indexing="ij")
        kmag = np.sqrt(kx * kx + ky * ky + kz * kz)
        cutoff = max(3.0, float(p["eddy"]) * 0.5)
        # Reject DC / one-cell domain modes: they are coherent wind/shear, not
        # local turbulence.  The soft cutoff avoids ringing at a hard spectral
        # boundary and spans several wavelengths rather than one giant lobe.
        band = (1.0 - np.exp(-(kmag / 2.0) ** 4)) * np.exp(-(kmag / cutoff) ** 4)
        potential = np.fft.ifftn(wave * band[..., None], axes=(0, 1, 2)).real.astype(np.float32)
        # Central periodic differences keep div(curl(A)) zero in the discrete
        # stencil used here.  Do not force A/F into mirror parity: that made
        # each random sample into one persistent two-ended axis.  Directionless
        # means no preferred *world* direction, not an artificial requirement
        # that every turbulent parcel is centrally symmetric.  The zero-mean
        # force and bulk-velocity removal below keep this stochastic field from
        # becoming baked travel.
        d = lambda a, axis: 0.5 * (np.roll(a, -1, axis) - np.roll(a, 1, axis))
        big = np.empty_like(potential)
        big[..., 0] = d(potential[..., 2], 1) - d(potential[..., 1], 2)
        big[..., 1] = d(potential[..., 0], 2) - d(potential[..., 2], 0)
        big[..., 2] = d(potential[..., 1], 0) - d(potential[..., 0], 1)
        # Remove the DC wind, then project the three linear basis functions out
        # of every vector channel.  A finite random field otherwise contains a
        # local F(r)=A*r shear that makes a persistent diagonal head-and-tail.
        big -= big.mean(axis=(0, 1, 2), keepdims=True)
        # The bases are orthogonal on this centred grid, so the scalar least
        # squares projection is exact and preserves F(-r)=-F(r).
        coord = np.linspace(-1.0, 1.0, N, dtype=np.float32)
        for basis in (coord[:, None, None], coord[None, :, None],
                      coord[None, None, :]):
            denom = float(np.sum(basis * basis)) * N * N
            coeff = (big * basis[..., None]).sum(axis=(0, 1, 2)) / max(denom, 1e-6)
            big -= basis[..., None] * coeff
        # A finite random realization can still put most of its energy on one
        # component (for example Fz).  That is neither gravity nor a net wind,
        # but it bakes a tall local axis into a supposedly directionless card.
        # Whiten the vector covariance so all three force components carry the
        # same energy before the solver ever sees them.  This is a property of
        # the stochastic force ensemble, not a frame-space shape correction.
        samples = big.reshape(-1, 3).astype(np.float64)
        cov = samples.T @ samples / max(1, len(samples))
        values, vectors = np.linalg.eigh(cov)
        inv_sqrt = vectors @ np.diag(1.0 / np.sqrt(np.maximum(values, 1e-8))) @ vectors.T
        big = np.einsum("...j,ij->...i", big, inv_sqrt.astype(np.float32))
        big /= (np.abs(big).max() + 1e-6)
        return np.ascontiguousarray(big, np.float32)

    noise.from_numpy(build_noise(args.seed))
    noise_alt.from_numpy(build_noise(args.seed + 0x51A7))
    noise_alt2.from_numpy(build_noise(args.seed + 0xB529))
    noise_alt3.from_numpy(build_noise(args.seed + 0x68E3))

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
    def add_fuel(t: ti.f32, dt: ti.f32, rad: ti.f32, shell: ti.f32,
                 kfuel: ti.f32, source_lobes: ti.i32, pulse_rate: ti.f32,
                 noise_phase_speed: ti.f32, source_variation: ti.f32):
        # Voxel centres run 0..N-1. `N*0.5` chooses the upper of the two
        # middle voxels for even grids, while every mirrored field is symmetric
        # about (N-1)*0.5; that half-voxel disagreement was a permanent source
        # of diagonal parcel drift.
        c = ti.Vector([(N - 1) * 0.5, (N - 1) * 0.5, (N - 1) * 0.5])
        r = rad * N
        # A source with fixed mass flow has a static solution under a fixed
        # force field. Modulating only its scalar flow rate lets combustion keep
        # rebuilding the gas surface without introducing a preferred direction.
        pulse = 0.675 + 0.325 * ti.sin(t * pulse_rate)
        for I in ti.grouped(fuel):
            p_local = ti.cast(I, ti.f32) - c
            d = p_local.norm()
            source_r = r
            m = 0.0
            if source_lobes <= 1:
                if d < source_r:
                    m = (1.0 - d / source_r) ** 0.7
            else:
                # Up to five overlapping seed clumps. Their locations are fixed
                # in local space so the event evolves coherently frame-to-frame;
                # only the existing cell jitter is random.  They create local
                # porous structure, not an effect-level direction: this is a
                # directionless parcel and the emitter owns its silhouette.
                for l in ti.static(range(5)):
                    off = ti.Vector([0.0, 0.0, 0.0])
                    if ti.static(l == 0): off = ti.Vector([-0.48, 0.10, -0.12])
                    if ti.static(l == 1): off = ti.Vector([ 0.42,-0.18,  0.16])
                    if ti.static(l == 2): off = ti.Vector([ 0.08, 0.44, -0.28])
                    if ti.static(l == 3): off = ti.Vector([-0.16,-0.36,  0.40])
                    if ti.static(l == 4): off = ti.Vector([ 0.34, 0.22,  0.36])
                    if l < source_lobes:
                        dl = (ti.cast(I, ti.f32) - (c + off * r)).norm()
                        m = ti.max(m, ti.max(0.0, 1.0 - dl / (source_r * 0.68)) ** 0.7)
            if m > 0.0:
                # A uniform burning sphere remains a uniform bright nugget
                # until it has travelled far beyond the source.  Modulate the
                # scalar fuel flow by the *magnitude* of a time-blended
                # divergence-free turbulence field. Magnitude has no selected
                # coordinate axis; the source remains one connected volume and
                # its changing hot/cool pockets are authored by the emitter,
                # never painted into the projected sprite after simulation.
                phase = t * noise_phase_speed
                source_eddy = (noise[I] * ti.cos(phase)
                               + noise_alt[I] * ti.sin(phase)).norm()
                source_eddy = ti.min(source_eddy, 1.0)
                m *= 1.0 - source_variation * (1.0 - source_eddy)
                # SOLID BALL (shell = 0) or a HOLLOW SHELL. A detonation ignites
                # a surface, not a volume: the reference the owner gave is dark
                # in the middle with a bright, filamented rim, and that hole is
                # not the flame dying — it is where the fuel never was.
                if shell > 0.0:
                    w = r * (1.0 - shell)
                    m = ti.max(0.0, 1.0 - ti.abs(d - r * shell) / ti.max(w, 1e-3))
                fuel[I] += m * kfuel * pulse * dt

    @ti.kernel
    def combust(dt: ti.f32, burn: ti.f32, heat_yield: ti.f32, smoke_yield: ti.f32):
        """Consume fuel into heat and a small soot yield.

        Fuel is the only continuously sourced quantity.  Temperature and smoke
        therefore have independent creation/destruction paths, preventing a
        stationary source from continually stamping the same density ball.
        """
        for I in ti.grouped(fuel):
            consumed = ti.min(fuel[I], burn * fuel[I] * dt)
            fuel[I] -= consumed
            temp[I] += consumed * heat_yield
            dens[I] += consumed * smoke_yield

    @ti.kernel
    def forces(dt: ti.f32, buoy: ti.f32, curl: ti.f32, visc: ti.f32,
               t: ti.f32, radial: ti.f32, decay: ti.f32,
               grav: ti.f32, flat: ti.f32, noise_phase_speed: ti.f32,
               contain: ti.f32, sustain_pressure: ti.f32, shape_noise: ti.f32,
               pulse_rate: ti.f32):
        c = ti.Vector([(N - 1) * 0.5, (N - 1) * 0.5, (N - 1) * 0.5])
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
                # An explosion takes one radial impulse. A continuously fed
                # flame puff does not: after ignition it keeps breathing as its
                # fuel flow rises/falls. Holding a small isotropic pressure is
                # what lets the visible gas boundary keep evolving after the
                # warm-up frames, rather than settling into one static blob.
                breath = 1.0 + 0.30 * ti.sin(t * pulse_rate)
                pressure = decay + sustain_pressure * (1.0 - decay)
                v += dt * radial * pressure * breath * (temp[I] + 0.35 * dens[I]) * dir / dist
                # A local, directionless puff has no exterior wind or buoyant
                # axis baked into it.  This is the opposing isotropic pressure:
                # it grows linearly with radius, so a stray turbulent lobe gets
                # a stronger restoring force than the hot core.  It is part of
                # the fluid force field, never a render-space squash/crop.
                v -= dt * contain * (0.2 + dens[I] + temp[I]) * d / N
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
            # Four incommensurate odd-symmetric modes make a genuine temporal
            # turbulence field. A single sin/cos blend of two fields only walks
            # around one ellipse in force-space, so a continuous fuel source
            # repeatedly found the same gas silhouette. Every summand is still
            # a centred divergence-free curl: this increases temporal degrees
            # of freedom without moving the parcel or baking a preferred axis.
            phase = t * noise_phase_speed
            turbulence = 0.5 * (
                noise[I] * ti.cos(phase)
                + noise_alt[I] * ti.sin(phase)
                + noise_alt2[I] * ti.cos(1.618 * phase + 0.73)
                + noise_alt3[I] * ti.sin(1.414 * phase + 1.91))
            # Most eddies shear around the local radius, keeping the parcel
            # round.  A bounded share of the same centred curl is allowed to
            # push locally in/out: without it only the interior turns over and
            # the rendered silhouette never changes.  This is still a
            # zero-mean, divergence-free stochastic field — no gravity, wind,
            # or baked up/down/side direction — and containment owns the
            # overall radius.
            if dist > 0.5:
                tangent = (d / dist).cross(turbulence)
                turbulence = tangent * (1.0 - shape_noise) + turbulence * shape_noise
            v += dt * curl * turbulence * (0.4 + dens[I] + temp[I])
            # Viscosity: plain velocity damping, the owner's third term.
            v *= ti.exp(-visc * dt)
            u_tmp[I] = v
        for I in ti.grouped(u):
            u[I] = u_tmp[I]

    @ti.kernel
    def calculate_vorticity():
        """omega = curl(u), evaluated before pressure projection."""
        for I in ti.grouped(u):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                dx = ti.Vector([1, 0, 0])
                dy = ti.Vector([0, 1, 0])
                dz = ti.Vector([0, 0, 1])
                w = 0.5 * ti.Vector([
                    u[I + dy].z - u[I - dy].z - u[I + dz].y + u[I - dz].y,
                    u[I + dz].x - u[I - dz].x - u[I + dx].z + u[I - dx].z,
                    u[I + dx].y - u[I - dx].y - u[I + dy].x + u[I - dy].x])
                omega[I] = w
                omega_mag[I] = w.norm()
            else:
                omega[I] = ti.Vector([0.0, 0.0, 0.0])
                omega_mag[I] = 0.0

    @ti.kernel
    def confine_vorticity(dt: ti.f32, strength: ti.f32):
        """Add epsilon * (normalize(grad |omega|) cross omega)."""
        for I in ti.grouped(u):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                dx = ti.Vector([1, 0, 0])
                dy = ti.Vector([0, 1, 0])
                dz = ti.Vector([0, 0, 1])
                grad = 0.5 * ti.Vector([
                    omega_mag[I + dx] - omega_mag[I - dx],
                    omega_mag[I + dy] - omega_mag[I - dy],
                    omega_mag[I + dz] - omega_mag[I - dz]])
                n = grad.normalized(1e-4)
                u_tmp[I] = u[I] + dt * strength * 0.02 * n.cross(omega[I])
            else:
                u_tmp[I] = u[I]
        for I in ti.grouped(u):
            u[I] = u_tmp[I]

    @ti.kernel
    def velocity_to_faces():
        """Interpolate centred transport velocity onto MAC cell faces."""
        for I in ti.grouped(u_x):
            if I.x == 0 or I.x == N:
                u_x[I] = 0.0
            else:
                u_x[I] = 0.5 * (u[I.x - 1, I.y, I.z].x + u[I.x, I.y, I.z].x)
        for I in ti.grouped(u_y):
            if I.y == 0 or I.y == N:
                u_y[I] = 0.0
            else:
                u_y[I] = 0.5 * (u[I.x, I.y - 1, I.z].y + u[I.x, I.y, I.z].y)
        for I in ti.grouped(u_z):
            if I.z == 0 or I.z == N:
                u_z[I] = 0.0
            else:
                u_z[I] = 0.5 * (u[I.x, I.y, I.z - 1].z + u[I.x, I.y, I.z].z)

    @ti.kernel
    def divergence():
        for I in ti.grouped(div):
            if 1 <= I.x < N - 1 and 1 <= I.y < N - 1 and 1 <= I.z < N - 1:
                # MAC divergence at cell centre. `subtract_gradient` applies
                # the exact adjoint pressure differences to these same faces,
                # so the six-neighbour Jacobi Poisson solve matches the
                # discrete operator instead of injecting a one-cell bias.
                div[I] = (u_x[I.x + 1, I.y, I.z] - u_x[I.x, I.y, I.z]
                          + u_y[I.x, I.y + 1, I.z] - u_y[I.x, I.y, I.z]
                          + u_z[I.x, I.y, I.z + 1] - u_z[I.x, I.y, I.z])
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
        for I in ti.grouped(u_x):
            if 1 <= I.x < N:
                u_x[I] -= pre[I.x, I.y, I.z] - pre[I.x - 1, I.y, I.z]
        for I in ti.grouped(u_y):
            if 1 <= I.y < N:
                u_y[I] -= pre[I.x, I.y, I.z] - pre[I.x, I.y - 1, I.z]
        for I in ti.grouped(u_z):
            if 1 <= I.z < N:
                u_z[I] -= pre[I.x, I.y, I.z] - pre[I.x, I.y, I.z - 1]
        for I in ti.grouped(u):
            u[I] = ti.Vector([
                0.5 * (u_x[I.x, I.y, I.z] + u_x[I.x + 1, I.y, I.z]),
                0.5 * (u_y[I.x, I.y, I.z] + u_y[I.x, I.y + 1, I.z]),
                0.5 * (u_z[I.x, I.y, I.z] + u_z[I.x, I.y, I.z + 1])])

    @ti.kernel
    def clear_bulk_velocity():
        bulk_mass[None] = 0.0
        bulk_vx[None] = 0.0
        bulk_vy[None] = 0.0
        bulk_vz[None] = 0.0

    @ti.kernel
    def measure_bulk_velocity():
        for I in ti.grouped(dens):
            # Hot gas has to count before soot density has accumulated. This
            # keeps the first bright frames centered too, not only their tail.
            w = dens[I] + temp[I]
            ti.atomic_add(bulk_mass[None], w)
            ti.atomic_add(bulk_vx[None], u[I].x * w)
            ti.atomic_add(bulk_vy[None], u[I].y * w)
            ti.atomic_add(bulk_vz[None], u[I].z * w)

    @ti.kernel
    def remove_bulk_velocity():
        inv_mass = 1.0 / ti.max(bulk_mass[None], 1e-6)
        mean = ti.Vector([bulk_vx[None], bulk_vy[None], bulk_vz[None]]) * inv_mass
        for I in ti.grouped(u):
            u[I] -= mean

    @ti.kernel
    def advect_velocity(dt: ti.f32):
        for I in ti.grouped(u):
            back = ti.cast(I, ti.f32) - u[I] * dt
            u_tmp[I] = sampv(u, back)
        for I in ti.grouped(u):
            u[I] = u_tmp[I]

    @ti.kernel
    def advect_scalars(dt: ti.f32):
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
        for I in ti.grouped(fuel):
            back = ti.cast(I, ti.f32) - u[I] * dt
            fuel_tmp[I] = samp(fuel, back)
        for I in ti.grouped(fuel):
            fuel[I] = fuel_tmp[I]

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
    max_divergence = 0.0
    # Radius of every voxel, as a fraction of the domain HALF-width: the one
    # extent measurement that does not go through the renderer, so it compares
    # two resolutions (or a sim against a sheet) without the framing, the
    # extinction scale or the alpha threshold in the way.
    ax = (np.arange(N, dtype=np.float32) - (N - 1) / 2.0) / ((N - 1) / 2.0)
    rad = np.sqrt(ax[:, None, None] ** 2 + ax[None, :, None] ** 2 + ax[None, None, :] ** 2)
    rad_flat = rad.ravel()
    rad_order = np.argsort(rad_flat)
    rad_sorted = rad_flat[rad_order]
    total_frames = args.frames + p["warmup_frames"]
    for f in range(total_frames):
        for substep in range(args.substeps):
            frac = f / max(1, total_frames - 1)
            step_dt = dt / args.substeps
            force_time = (f + (substep + 0.5) / args.substeps) * dt
            # Stable-fluid ordering: transport velocity first, then apply
            # sources/forces, restore lost vorticity, project to divergence-free,
            # and only then transport the scalar material fields with that
            # projected velocity.  Keeping all three advections in one pass made
            # force injection depend on its own unprojected backtrace.
            advect_velocity(step_dt)
            if frac < p["fuel_frames"]:
                add_fuel(force_time, step_dt, p["fuel_radius"], p["shell"],
                         p["fuel_dens"], p["source_lobes"], p["source_pulse_rate"],
                         p["noise_phase_speed"], p["source_variation"])
            combust(step_dt, p["burn"], p["heat_yield"], p["smoke_yield"])
            # Impulse envelope: full push while the fuel burns, then off.
            # How long the radial impulse lasts, as a fraction of the sheet.
            # A puff coasts after a gentle shove (0.22); a DETONATION is over in
            # a few frames and everything after is momentum.
            decay = max(0.0, 1.0 - (f / max(1, args.frames - 1))
                        / max(p["impulse"], 1e-3)) ** 2
            # The turbulence field is time-dependent.  Freezing it for all
            # substeps made each atlas cell an impulse from a static field; the
            # parcel then settled into the same cauliflower outline for the
            # remainder of the sheet.  Sampling physical substep time makes
            # that field continuously turn over while retaining its centred,
            # odd-symmetric construction.
            forces(step_dt, p["buoyancy"], p["curl"], p["viscosity"],
                   force_time, p["radial"], decay, p["gravity"],
                   p["flat"], p["noise_phase_speed"], p["contain"],
                   p["sustain_pressure"], p["shape_noise"], p["source_pulse_rate"])
            calculate_vorticity()
            confine_vorticity(step_dt, p["swirl"])
            velocity_to_faces()
            divergence()
            for k in range(jacobi_iters // 2):
                jacobi(pre, pre2)
                jacobi(pre2, pre)
            subtract_gradient()
            if p["lock_center"]:
                clear_bulk_velocity()
                measure_bulk_velocity()
                remove_bulk_velocity()
            advect_scalars(step_dt)
            cool(step_dt, p["cool"], p["soot"], p.get("dissipate", 0.06))
            diffuse(p["diffuse"])

        if f < p["warmup_frames"]:
            continue

        d = dens.to_numpy()
        tp = temp.to_numpy()
        divergence()
        max_divergence = max(max_divergence, float(np.max(np.abs(div.to_numpy()))))
        tot = float(d.sum())
        if tot > 1e-6:
            shell = tot - float(d[2:-2, 2:-2, 2:-2].sum())
            wall_max = max(wall_max, shell / tot)
            w = d.ravel()[rad_order]
            r90 = float(rad_sorted[np.searchsorted(np.cumsum(w), 0.9 * tot)])
        # Layout [z][y][x], which is what render.py indexes.
        np.savez_compressed(
            os.path.join(out_dir, "f%03d.npz" % (f - p["warmup_frames"] + 1)),
            density=np.ascontiguousarray(d.transpose(2, 1, 0), np.float16),
            flame=np.ascontiguousarray(tp.transpose(2, 1, 0), np.float16),
            temperature=np.ascontiguousarray(tp.transpose(2, 1, 0), np.float16),
            res=np.array([N, N, N], np.int32))
        written = f - p["warmup_frames"]
        if written % 8 == 0:
            print("SIM %2d/%d  %.1fs  d.max=%.3f T.max=%.3f div.max=%.4f"
                  % (written, args.frames, time.time() - t0, float(d.max()), float(tp.max()),
                     max_divergence),
                  flush=True)

    print("SIM: %d frames in %.1fs -> %s" % (args.frames, time.time() - t0, out_dir))
    print("SIM: r90 %.2f of the domain half-width (last frame), wall shell %.1f%% (peak)"
          % (r90, wall_max * 100))
    print("SIM: peak post-projection divergence %.5f" % max_divergence)
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
