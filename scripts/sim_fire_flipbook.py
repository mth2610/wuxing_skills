#!/usr/bin/env python3
"""Fire flipbook from a real fluid simulation — no Blender, no ray tracing.

    python3 scripts/sim_fire_flipbook.py --quick    # ~11 s, 512px sheet — the iteration loop
    python3 scripts/sim_fire_flipbook.py            # ~2.5 min, 2048px sheet — the one that ships

    Shape is swept from the command line, no editing:
    python3 scripts/sim_fire_flipbook.py --quick --buoyancy 19 --cooling 2.0 --swirl 4.2
      --buoyancy  taller / thinner as it rises
      --cooling   shorter flame, turns to soot sooner
      --swirl     more licking tongues, less smooth column
    Every run prints cell coverage and height/width and warns if the result is
    still puff-shaped, so a bad sheet is caught before it reaches the engine.

WHY THIS REPLACES gen_fire_flipbook.py (Blender)
    The Blender sheet was audited in E4 and failed structurally, not on tuning:
    cell coverage 4.1% against smoke's 19.6%, and height/width 1.00 — it was
    rendering the same SPHERICAL puff as the smoke sheet, only smaller. Flame
    morphology is the opposite: buoyancy stretches it vertically (h/w well above
    1.3) with tongues that lick up and detach. No parameter change gets there
    from a spherical domain; it needs a different simulation.

    The second reason is noise. Cycles volume rendering at low sample counts is
    stochastic, and E4's "viền lăn quăn" (writhing rim at dissipation) was traced
    to exactly that. A grid solver is deterministic: same seed, same frames, and
    the edge is as smooth as the density field is.

WHAT IT SIMULATES
    A 3D incompressible fluid on a uniform grid (semi-Lagrangian advection +
    Jacobi pressure projection), carrying temperature and soot:
      - a fuel disc at the floor injects hot, dense gas with a wandering offset,
      - buoyancy lifts hot gas and pulls cold soot down,
      - vorticity confinement re-injects the swirl that semi-Lagrangian
        advection damps out — this is what makes flame tongues instead of a
        smooth plume,
      - radiative cooling (T^4) turns the top of the plume into soot, which is
        what makes fire read as fire rather than as orange smoke.
    The fuel cuts off partway through the sheet, so the last frames are the
    flame dying into smoke rather than a hard loop.

OUTPUT
    RGB = normalised TEMPERATURE (greyscale), A = opacity.
    No colour is baked in: per F3 the black-body ramp is applied at the call
    site, so one sheet serves every element that needs a flame. Straight alpha.
"""

import argparse
import os
import sys
import time

import numpy as np
from PIL import Image

OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")


# ── grid helpers ─────────────────────────────────────────────────────────────
def trilerp(field, x, y, z):
    """Sample `field` at fractional coordinates (backwards trace).

    Written out rather than using scipy.ndimage.map_coordinates because scipy is
    not installed here and this is the only interpolation the solver needs.
    """
    nx, ny, nz = field.shape
    x = np.clip(x, 0.0, nx - 1.001)
    y = np.clip(y, 0.0, ny - 1.001)
    z = np.clip(z, 0.0, nz - 1.001)
    i0 = x.astype(np.int32); j0 = y.astype(np.int32); k0 = z.astype(np.int32)
    i1 = i0 + 1; j1 = j0 + 1; k1 = k0 + 1
    fx = (x - i0)[..., None][..., 0]
    fy = (y - j0)[..., None][..., 0]
    fz = (z - k0)[..., None][..., 0]

    c000 = field[i0, j0, k0]; c100 = field[i1, j0, k0]
    c010 = field[i0, j1, k0]; c110 = field[i1, j1, k0]
    c001 = field[i0, j0, k1]; c101 = field[i1, j0, k1]
    c011 = field[i0, j1, k1]; c111 = field[i1, j1, k1]

    c00 = c000 * (1 - fx) + c100 * fx
    c10 = c010 * (1 - fx) + c110 * fx
    c01 = c001 * (1 - fx) + c101 * fx
    c11 = c011 * (1 - fx) + c111 * fx
    c0 = c00 * (1 - fy) + c10 * fy
    c1 = c01 * (1 - fy) + c11 * fy
    return c0 * (1 - fz) + c1 * fz


class FireSim:
    def __init__(self, n, seed=7):
        # A cube: the CELL is square, so the flame has to be tall and narrow
        # inside a square frame — which is exactly the shape the audit asked for.
        self.n = n
        self.d = np.zeros((n, n, n), np.float32)   # soot / smoke density
        self.T = np.zeros((n, n, n), np.float32)   # temperature, 0..1
        self.u = np.zeros((n, n, n), np.float32)
        self.v = np.zeros((n, n, n), np.float32)
        self.w = np.zeros((n, n, n), np.float32)
        self.rng = np.random.default_rng(seed)
        ii, jj, kk = np.meshgrid(np.arange(n), np.arange(n), np.arange(n), indexing="ij")
        self.ii = ii.astype(np.float32)
        self.jj = jj.astype(np.float32)
        self.kk = kk.astype(np.float32)

        # Static per-cell jitter for the fuel disc: without it the base is a
        # perfect circle and the flame comes out rotationally symmetric, which
        # is the "spherical puff" failure in another guise.
        self.fuel_noise = self.rng.random((n, n, n)).astype(np.float32)

    # -- sources -------------------------------------------------------------
    def add_fuel(self, t01, dt):
        n = self.n
        cx = cz = (n - 1) * 0.5
        # The base wanders: a fixed source burns as a static column.
        wob = 0.055 * n
        cx += wob * np.sin(t01 * 11.0) * np.sin(t01 * 4.3)
        cz += wob * np.cos(t01 * 9.0 + 1.2)
        r = 0.25 * n   # wider base: the first pass made a candle wick, not a fire
        floor = slice(1, max(2, int(0.07 * n)))

        dist = np.sqrt((self.ii[:, floor, :] - cx) ** 2 + (self.kk[:, floor, :] - cz) ** 2)
        mask = np.clip(1.0 - dist / r, 0.0, 1.0) ** 0.6
        mask = mask * (0.55 + 0.45 * self.fuel_noise[:, floor, :])

        self.T[:, floor, :] += mask * (5.5 * dt)
        self.d[:, floor, :] += mask * (1.6 * dt)
        # A little upward kick so the fuel leaves the floor as a jet.
        self.v[:, floor, :] += mask * (11.0 * dt)
        np.clip(self.T, 0.0, 1.4, out=self.T)

    # -- forces --------------------------------------------------------------
    def clamp_velocity(self, limit):
        """Semi-Lagrangian advection is unconditionally stable, which is exactly
        why an unclamped solver fails SILENTLY here: instead of blowing up it
        keeps running while every parcel is traced right off the grid. Measured
        on the first run — |v| went 26 → 146 grid cells/step in eight steps and
        the sheet came out empty (coverage 0.0%)."""
        mag = np.sqrt(self.u * self.u + self.v * self.v + self.w * self.w)
        over = mag > limit
        if over.any():
            scale = np.ones_like(mag)
            scale[over] = limit / mag[over]
            self.u *= scale; self.v *= scale; self.w *= scale

    def buoyancy(self, dt, alpha=15.5, beta=5.0):
        # Hot gas rises, soot weighs it down. beta is what makes the plume neck
        # in and the top curl over instead of rising forever.
        self.v += dt * (alpha * self.T - beta * self.d)

    def vorticity_confinement(self, dt, eps=3.6):
        """Re-inject the small-scale swirl semi-Lagrangian advection destroys.

        This is the single most important term for FLAME rather than plume: the
        licking tongues are vortices, and without confinement they are smoothed
        away within a few steps and the result is a soft column.
        """
        u, v, w = self.u, self.v, self.w
        wx = (np.gradient(w, axis=1) - np.gradient(v, axis=2))
        wy = (np.gradient(u, axis=2) - np.gradient(w, axis=0))
        wz = (np.gradient(v, axis=0) - np.gradient(u, axis=1))
        mag = np.sqrt(wx * wx + wy * wy + wz * wz) + 1e-5
        gx = np.gradient(mag, axis=0)
        gy = np.gradient(mag, axis=1)
        gz = np.gradient(mag, axis=2)
        gl = np.sqrt(gx * gx + gy * gy + gz * gz) + 1e-5
        nx_, ny_, nz_ = gx / gl, gy / gl, gz / gl
        self.u += dt * eps * (ny_ * wz - nz_ * wy)
        self.v += dt * eps * (nz_ * wx - nx_ * wz)
        self.w += dt * eps * (nx_ * wy - ny_ * wx)

    def cool(self, dt, rate=1.55, soot_gain=0.5):
        # Radiative cooling goes as T^4 — the reason a flame's tip darkens
        # abruptly instead of fading linearly, and where the soot comes from.
        loss = rate * (self.T ** 4) * dt
        self.T -= loss
        self.d += loss * soot_gain
        np.clip(self.T, 0.0, None, out=self.T)
        self.d *= (1.0 - 0.35 * dt)

    # -- projection ----------------------------------------------------------
    def project(self, iters):
        div = 0.5 * (
            np.roll(self.u, -1, 0) - np.roll(self.u, 1, 0)
            + np.roll(self.v, -1, 1) - np.roll(self.v, 1, 1)
            + np.roll(self.w, -1, 2) - np.roll(self.w, 1, 2)
        )
        p = np.zeros_like(div)
        for _ in range(iters):
            p = (
                np.roll(p, -1, 0) + np.roll(p, 1, 0)
                + np.roll(p, -1, 1) + np.roll(p, 1, 1)
                + np.roll(p, -1, 2) + np.roll(p, 1, 2)
                - div
            ) / 6.0
        self.u -= 0.5 * (np.roll(p, -1, 0) - np.roll(p, 1, 0))
        self.v -= 0.5 * (np.roll(p, -1, 1) - np.roll(p, 1, 1))
        self.w -= 0.5 * (np.roll(p, -1, 2) - np.roll(p, 1, 2))
        # Walls: no flow through the floor, open at the top.
        self.v[:, 0, :] = np.maximum(self.v[:, 0, :], 0.0)

    def advect_all(self, dt):
        x = self.ii - self.u * dt
        y = self.jj - self.v * dt
        z = self.kk - self.w * dt
        nu = trilerp(self.u, x, y, z)
        nv = trilerp(self.v, x, y, z)
        nw = trilerp(self.w, x, y, z)
        self.d = trilerp(self.d, x, y, z)
        self.T = trilerp(self.T, x, y, z)
        self.u, self.v, self.w = nu, nv, nw

    def step(self, t01, dt, fuel_on, jacobi):
        if fuel_on:
            self.add_fuel(t01, dt)
        self.buoyancy(dt, alpha=getattr(self, 'k_buoy', 15.5))
        self.vorticity_confinement(dt, eps=getattr(self, 'k_swirl', 3.6))
        # Air drag, and the CFL clamp above. Together they keep the plume inside
        # the box for the whole sheet instead of evacuating it.
        self.u *= 0.985; self.v *= 0.985; self.w *= 0.985
        self.clamp_velocity(0.30 * self.n / max(dt, 1e-3) * dt)
        self.project(jacobi)
        self.advect_all(dt)
        self.cool(dt, rate=getattr(self, 'k_cool', 1.55))
        # Open top. A hard cut at the last few rows made gas pile against the
        # ceiling and draw a FLAT BRIGHT LID across the cell — visible in the
        # first sheet as a straight horizontal edge on every frame. A graded
        # outflow over the top eighth removes it without a visible boundary.
        h = max(3, self.n // 8)
        ramp = np.linspace(1.0, 0.0, h, dtype=np.float32)[None, :, None]
        self.d[:, -h:, :] *= ramp
        self.T[:, -h:, :] *= ramp


# ── rendering ────────────────────────────────────────────────────────────────
def render_cell_raw(sim):
    """Project the volume along Z into one cell, as raw float planes.

    Normalisation is deliberately NOT done here. Per-frame normalisation would
    rescale every frame to its own maximum, so a dying flame would look exactly
    as bright as a roaring one and the sheet would carry no intensity arc at
    all — the whole point of an authored flipbook. The scale is taken once,
    across the entire sheet, in main().
    """
    # The injection layer itself is not drawn: it is a boundary condition, not
    # part of the flame, and leaving it in put a hard bright bar along the
    # bottom of every cell.
    d = sim.d.copy(); T = sim.T.copy()
    cut = max(3, sim.n // 8)
    d[:, :cut, :] *= np.linspace(0.0, 1.0, cut, dtype=np.float32)[None, :, None]
    T[:, :cut, :] *= np.linspace(0.0, 1.0, cut, dtype=np.float32)[None, :, None]
    dens = d + T * 0.9                    # hot gas is visible even before sooting
    opacity = 1.0 - np.exp(-2.6 * dens.sum(axis=2) / sim.n * 8.0)
    # Emission-weighted temperature, so the value written is what the pixel
    # actually radiates rather than a plain column average.
    wgt = dens * (T ** 2)
    tsum = wgt.sum(axis=2)
    temp = np.where(tsum > 1e-6, (wgt * T).sum(axis=2) / np.maximum(tsum, 1e-6), 0.0)

    # Image rows run top-down, the grid's Y runs up: flip.
    return temp.T[::-1].copy(), opacity.T[::-1].copy()


def measure(sheet, grid, cell):
    """The audit numbers from E4, computed here so a bad sheet is caught before
    it ever reaches the engine: coverage vs smoke's 19.6%, and height/width,
    which must be well above 1.3 or the sim is still making a puff."""
    a = np.asarray(sheet)[..., 3].astype(np.float32) / 255.0
    covs, ratios = [], []
    for f in range(grid * grid):
        r, c = divmod(f, grid)
        sub = a[r * cell:(r + 1) * cell, c * cell:(c + 1) * cell]
        m = sub > 0.06
        if m.sum() < 32:
            continue
        covs.append(m.mean())
        ys, xs = np.nonzero(m)
        ratios.append((ys.max() - ys.min() + 1) / max(1, (xs.max() - xs.min() + 1)))
    return (float(np.mean(covs)) if covs else 0.0,
            float(np.mean(ratios)) if ratios else 0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true",
                    help="small grid + small cells: ~15 s, for iterating on look")
    ap.add_argument("--out", default="fire_atlas_8x8.png")
    ap.add_argument("--grid", type=int, default=8)
    ap.add_argument("--cell", type=int, default=None)
    ap.add_argument("--res", type=int, default=None, help="simulation grid size")
    ap.add_argument("--substeps", type=int, default=None)
    ap.add_argument("--jacobi", type=int, default=None)
    ap.add_argument("--seed", type=int, default=7)
    # The three knobs that decide the SHAPE. Exposed because this is the loop an
    # artist actually runs: --quick takes ~11 s, so a sweep is cheap.
    ap.add_argument("--buoyancy", type=float, default=12.0,
                    help="lift per unit temperature; higher = taller, thinner")
    ap.add_argument("--cooling", type=float, default=2.6,
                    help="radiative cooling; higher = shorter flame, more soot")
    ap.add_argument("--swirl", type=float, default=3.6,
                    help="vorticity confinement; higher = more licking tongues")
    args = ap.parse_args()

    res = args.res or (28 if args.quick else 56)
    cell = args.cell or (64 if args.quick else 256)
    substeps = args.substeps or (4 if args.quick else 6)
    jacobi = args.jacobi or (10 if args.quick else 22)
    frames = args.grid * args.grid

    sim = FireSim(res, args.seed)
    sim.k_buoy, sim.k_cool, sim.k_swirl = args.buoyancy, args.cooling, args.swirl
    dt = 0.12   # grid cells per step; see clamp_velocity for why this is small
    # Warm-up so frame 0 already has a formed flame — the smoke sheet's blank
    # first frames were harmless only because its alpha curve faded in.
    for i in range(int(res * 0.7)):
        sim.step(i * 0.004, dt, True, jacobi)

    raw = []
    t0 = time.time()
    for f in range(frames):
        t01 = f / (frames - 1)
        # Fuel cuts off at 62%: the tail of the sheet is the flame dying into
        # smoke, which is what a one-shot flipbook needs (ANIM_ONCE).
        fuel = t01 < 0.62
        for _ in range(substeps):
            sim.step(t01, dt / substeps, fuel, jacobi)
        raw.append(render_cell_raw(sim))
        if f % 8 == 0:
            print("  frame %2d/%d  %.1fs" % (f, frames, time.time() - t0), flush=True)

    # One scale for the whole sheet, from the 99.5th percentile so a couple of
    # hot voxels cannot crush everything else into the noise floor.
    tmax = max(1e-4, float(np.percentile(np.stack([t for t, _ in raw]), 99.5)))
    omax = max(1e-4, float(np.percentile(np.stack([o for _, o in raw]), 99.5)))
    sheet = Image.new("RGBA", (args.grid * cell, args.grid * cell), (0, 0, 0, 0))
    for f, (t, o) in enumerate(raw):
        img = np.zeros((t.shape[0], t.shape[1], 4), np.float32)
        img[..., 0] = img[..., 1] = img[..., 2] = np.clip(t / tmax, 0, 1)
        img[..., 3] = np.clip(o / omax, 0, 1)
        cellimg = Image.fromarray((img * 255).astype(np.uint8), "RGBA").resize(
            (cell, cell), Image.LANCZOS)
        r, c = divmod(f, args.grid)
        sheet.paste(cellimg, (c * cell, r * cell))

    path = os.path.join(OUT_DIR, args.out)
    sheet.save(path)
    cov, ratio = measure(sheet, args.grid, cell)
    print("wrote %s  %dx%d  in %.1fs" % (path, sheet.size[0], sheet.size[1], time.time() - t0))
    print("  cell coverage %.1f%%   (smoke sheet: 19.6%%)" % (cov * 100))
    print("  height/width  %.2f    (must exceed 1.30 — flame, not puff)" % ratio)
    if ratio < 1.3:
        print("  WARNING: still puff-shaped. Raise buoyancy alpha or narrow the fuel disc.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
