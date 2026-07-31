#!/usr/bin/env python3
"""Step 2 of 3 — ray-march the simulated grids on the GPU (Taichi).

    python3 scripts/flipbook/render.py build_cache/smoke_puff --cell 256 \
        --supersample 2 --density-scale 3 --zoom auto

Reads build_cache/<name>/f###.npz (from ti_sim.py) and writes
build_cache/<name>/frames/f###.png, ready for pack.py.

WHY A HAND-WRITTEN RAY-MARCHER RATHER THAN EEVEE
    Eevee's alpha comes from EXTINCTION, so an emissive flame renders bright and
    almost transparent — measured on the previous pipeline at rgb 255 / alpha 10,
    which forced alpha to be faked from luminance and collapsed the sheet to one
    channel of information. Marching the grid ourselves decides exactly what
    lands in each channel, so "thick but cool" (smoke) and "hot" stay separable.

CHANNEL LAYOUT (what the engine gets)
    R = emission / flame        → multiply by the black-body ramp at the call
                                  site (F3); this is the additive population.
    G = smoke density           → the alpha-blended, LIT population (F1b).
    B = self-shadow             → the same integral weighted by the light that
                                  reaches each sample (--light). pack.py --split
                                  writes B/G into the smoke sheet's RGB: an
                                  UNSHADED smoke mask stacks into flat cards.
    A = 1 - transmittance       → a real opacity, not a luminance guess.

    One draw of this sheet can therefore feed both populations the blend law
    requires, instead of one greyscale mask doing duty for both.
"""

import argparse
import glob
import os
import sys
import time

import numpy as np
from PIL import Image, ImageFilter

import taichi as ti


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cache_dir")
    ap.add_argument("--cell", type=int, default=128, help="output pixels per frame")
    ap.add_argument("--supersample", type=int, default=2,
                    help="render at N x cell then box-filter down; 1 = off")
    ap.add_argument("--density-scale", type=float, default=28.0,
                    help="extinction per unit of simulated density. The grids peak "
                         "near 0.1 for smoke, so raw values integrate to almost "
                         "nothing over a short ray. Raising it also SATURATES "
                         "the puff's interior into a plateau — measured, the "
                         "alpha median moved only 0.54->0.72 across 1.5..7, so "
                         "this is not the knob for a card-looking sprite.")
    ap.add_argument("--flame-scale", type=float, default=3.0)
    ap.add_argument("--flame-extinction", type=float, default=6.0,
                    help="how much the flame itself blocks light; 0 makes fire "
                         "purely additive and it stops occluding its own smoke")
    ap.add_argument("--zoom", default="1.0",
                    help="a number, or 'auto' to FIT the sheet: render once "
                         "uncropped, measure how far the lit alpha actually "
                         "reaches, and set the crop so it sits 2%% inside the "
                         "cell border. Dialling this by hand cannot converge, "
                         "because once the effect is clipped the measurement "
                         "saturates — a puff twice too big and one 1%% too big "
                         "both report 'touching the border'. "
                         "Crops toward the domain centre. The SIM needs room so "
                         "the plume never touches a wall (a wall makes the "
                         "silhouette a box), but the SHEET wants the effect to "
                         "fill its cell. Those are different requirements, so "
                         "framing belongs here and not in the solver — raising "
                         "the radial force to fill the frame just runs the puff "
                         "into the boundary.")
    ap.add_argument("--light", type=float, default=1.0,
                    help="self-shadow strength, as a multiple of --density-scale "
                         "(0 = off, flat). Written to channel B, the one the "
                         "layout reserved for lighting. A smoke sheet needs it: "
                         "the engine lights a BILLBOARD, so nothing at the call "
                         "site can shade the inside of the puff, and an unshaded "
                         "mask stacks into flat cards.")
    ap.add_argument("--ambient", type=float, default=0.22,
                    help="floor under the self-shadow, so the underside of a "
                         "thick puff goes dark rather than black")
    ap.add_argument("--arch", default="gpu", choices=["gpu", "cpu"])
    ap.add_argument("--profile", default="volume", choices=["volume", "dust"],
                    help="volume keeps the smoke/fire lighting channels. dust writes a "
                         "cold, eroded alpha parcel: it deliberately has no volume "
                         "self-shadow, which otherwise shows up as horizontal bands.")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.cache_dir, "f*.npz")))
    if not files:
        print("no f###.npz in %s — run scripts/flipbook/ti_sim.py first" % args.cache_dir)
        return 1

    ti.init(arch=ti.gpu if args.arch == "gpu" else ti.cpu)

    first = np.load(files[0])
    rz, ry, rx = first["density"].shape
    S = args.cell * max(1, args.supersample)

    # The cell is SQUARE but the domain is not. Fit the grid inside the cell at
    # its true aspect and leave the rest transparent, instead of stretching each
    # axis to the full cell — which is what the first version did: a 34x34x96
    # grid came out smeared 2.8x horizontally, which is why the flame looked
    # wrong, why the smoke reached the cell edges and got clipped, and why the
    # height/width audit read 0.60 for a plume that is actually tall.
    aspect = rx / rz                      # width / height of the domain
    autofit = str(args.zoom).lower() == "auto"
    zoom = 1.0 if autofit else float(args.zoom)
    fit_w = min(1.0, aspect) * zoom        # fraction of the cell used, per axis
    fit_h = min(1.0, 1.0 / aspect) * zoom

    dens = ti.field(ti.f32, shape=(rz, ry, rx))
    flame = ti.field(ti.f32, shape=(rz, ry, rx))
    # Transmittance from the LIGHT to each voxel — the volume's own shadow.
    shad = ti.field(ti.f32, shape=(rz, ry, rx))
    # emission, smoke, opacity, shaded value
    out = ti.Vector.field(4, ti.f32, shape=(S, S))

    @ti.func
    def sample(fld, gx, gy, gz):
        # Trilinear, clamped. Clamping (rather than wrapping) matters: the plume
        # touches the domain wall and a wrap would smear it to the far side.
        x = ti.math.clamp(gx, 0.0, rx - 1.001)
        y = ti.math.clamp(gy, 0.0, ry - 1.001)
        z = ti.math.clamp(gz, 0.0, rz - 1.001)
        i, j, k = int(x), int(y), int(z)
        fx, fy, fz = x - i, y - j, z - k
        c00 = fld[k, j, i] * (1 - fx) + fld[k, j, i + 1] * fx
        c10 = fld[k, j + 1, i] * (1 - fx) + fld[k, j + 1, i + 1] * fx
        c01 = fld[k + 1, j, i] * (1 - fx) + fld[k + 1, j, i + 1] * fx
        c11 = fld[k + 1, j + 1, i] * (1 - fx) + fld[k + 1, j + 1, i + 1] * fx
        c0 = c00 * (1 - fy) + c10 * fy
        c1 = c01 * (1 - fy) + c11 * fy
        return c0 * (1 - fz) + c1 * fz

    @ti.kernel
    def march(ks: ti.f32, kf: ti.f32, kfe: ti.f32, fw: ti.f32, fh: ti.f32):
        for px, py in out:
            # Orthographic side view: image X is the grid's X, image Y is the
            # grid's Z (up in Blender), and the ray runs along Y.
            # Normalised cell coords, then remapped into the fitted rectangle.
            # Outside it there is no domain at all, so the ray contributes
            # nothing and the pixel stays transparent.
            u = ((px + 0.5) / S - 0.5) / fw + 0.5
            v = ((py + 0.5) / S - 0.5) / fh + 0.5
            gx = u * (rx - 1)
            gz = (1.0 - v) * (rz - 1)

            trans = 1.0
            emis = 0.0
            smoke = 0.0
            shade = 0.0
            inside = (u >= 0.0) and (u <= 1.0) and (v >= 0.0) and (v <= 1.0)
            steps = ry * 2 if inside else 0
            dstep = ti.cast(ry - 1, ti.f32) / steps
            for s in range(steps):
                gy = s * dstep
                d = sample(dens, gx, gy, gz)
                f = sample(flame, gx, gy, gz)
                ext = (d * ks + f * kfe) * dstep
                emis += f * kf * trans * dstep
                smoke += d * ks * trans * dstep
                # The same integral WEIGHTED by how much light reaches each
                # sample. Without it every sprite is a flat plate of one value,
                # and a stack of flat plates reads as overlapping cards no
                # matter how faint each one is made — the lighting pass at the
                # call site cannot supply this, because it lights a BILLBOARD
                # and knows nothing about the depth inside it.
                shade += d * ks * trans * sample(shad, gx, gy, gz) * dstep
                trans *= ti.exp(-ext)
                if trans < 0.004:      # the rest cannot contribute a visible level
                    break
            out[px, py] = ti.Vector([emis, smoke, 1.0 - trans, shade])

    t0 = time.time()

    def render_all(fw, fh, quiet=False):
        frames = []
        for i, p in enumerate(files):
            z = np.load(p)
            d = np.ascontiguousarray(z["density"], np.float32)
            dens.from_numpy(d)
            flame.from_numpy(np.ascontiguousarray(z["flame"], np.float32))
            # SELF-SHADOW, computed by a prefix sum rather than a second ray per
            # sample: with the key light straight overhead the light ray IS the
            # grid's z axis, so the optical depth above every voxel is one
            # cumulative sum (grid index rz-1 is the TOP — see gz above). An
            # arbitrary light direction would cost a march per sample; a fixed
            # overhead one costs O(N^3) once per frame and buys the same cue.
            above = np.cumsum(d[::-1], axis=0)[::-1] - d
            shad.from_numpy(np.ascontiguousarray(
                args.ambient + (1.0 - args.ambient)
                * np.exp(-args.light * args.density_scale * above), np.float32))
            march(args.density_scale, args.flame_scale, args.flame_extinction,
                  fw, fh)
        # transpose: the Taichi field is indexed [px, py] (x first), but numpy
        # and PIL read axis 0 as the ROW. Without this the sheet comes out
        # rotated 90 degrees — the flame rises along the image's X axis, which
        # measured as a row-centroid that never moved (128 in every frame) while
        # the column-centroid drifted 236 -> 198.
            img = out.to_numpy().transpose(1, 0, 2)
            if args.supersample > 1:
                k = args.supersample
                img = img.reshape(args.cell, k, args.cell, k, 4).mean(axis=(1, 3))
            frames.append(img)
            if i % 8 == 0 and not quiet:
                print("RENDER: %d/%d  %.1fs" % (i, len(files), time.time() - t0),
                      flush=True)
        return frames

    if autofit:
        # Measure on the UNCROPPED render, where nothing can be cut off, so the
        # reach is a true extent and not a saturated one. Alpha is 1 - trans,
        # an absolute quantity (unlike R/G, which are percentile-normalised
        # later), so the 0.06 threshold here is the same one pack.py audits with.
        probe = np.stack(render_all(fit_w, fit_h, quiet=True))[..., 2]
        lit = probe > 0.06
        half = args.cell / 2.0
        ys, xs = np.nonzero(lit.any(axis=0))
        if len(ys):
            reach = max(abs(ys.max() + 0.5 - half), abs(ys.min() + 0.5 - half),
                        abs(xs.max() + 0.5 - half), abs(xs.min() + 0.5 - half)) / half
            # Pack audits after filtering/quantising, which can expand the
            # measured alpha by one or two pixels. Keep a real 6% margin here;
            # 2% repeatedly produced "auto-fit" sheets that still clipped.
            zoom = 0.94 / max(reach, 1e-3)
            fit_w, fit_h = min(1.0, aspect) * zoom, min(1.0, 1.0 / aspect) * zoom
            print("RENDER: autofit reach %.3f of the domain half-width -> zoom %.2f"
                  % (reach, zoom))
            # The "silhouette is the box" test needs a SOLID threshold, not the
            # visibility one. A ray crossing the whole domain accumulates enough
            # optical depth from haze alone to pass alpha 0.06 at the very edge,
            # so the faint reach is ~1.0 on a perfectly healthy puff (measured:
            # reach 0.996 on a sim whose mass audit said r90 0.79, wall 0.1%).
            # Warning on that number cries wolf on every good sheet.
            solid = np.nonzero((probe > 0.5).any(axis=0))
            if len(solid[0]):
                sreach = max(abs(solid[0].max() + 0.5 - half),
                             abs(solid[0].min() + 0.5 - half),
                             abs(solid[1].max() + 0.5 - half),
                             abs(solid[1].min() + 0.5 - half)) / half
                if sreach > 0.95:
                    print("RENDER: WARNING opaque material reaches the domain "
                          "wall (%.2f) — the silhouette is partly the box, and "
                          "no crop repairs that" % sreach)
        else:
            print("RENDER: autofit found nothing lit; keeping zoom 1.0")

    frames = render_all(fit_w, fit_h)
    stack = np.stack(frames)
    # One scale for the WHOLE sheet, from a high percentile. Per-frame
    # normalisation would rescale a dying flame to look as bright as a roaring
    # one, which deletes the intensity arc the flipbook exists to carry; the
    # percentile keeps a few hot voxels from crushing everything else.
    e_max = max(1e-5, float(np.percentile(stack[..., 0], 99.5)))
    s_max = max(1e-5, float(np.percentile(stack[..., 1], 99.5)))
    print("RENDER: normalising emission/%.4f smoke/%.4f" % (e_max, s_max))

    out_dir = os.path.join(args.cache_dir, "frames")
    os.makedirs(out_dir, exist_ok=True)
    for i, img in enumerate(frames):
        rgba = np.zeros((args.cell, args.cell, 4), np.float32)
        rgba[..., 0] = np.clip(img[..., 0] / e_max, 0, 1)     # emission
        rgba[..., 1] = np.clip(img[..., 1] / s_max, 0, 1)     # smoke
        # B = the SHADED smoke value, normalised by the same scale as G so the
        # two are directly comparable: B/G is exactly the fraction of light that
        # survived to each pixel, which is what makes B usable as a shading term
        # rather than as a second, differently-scaled density.
        rgba[..., 2] = np.clip(img[..., 3] / s_max, 0, 1)      # self-shadowed value
        rgba[..., 3] = np.clip(img[..., 2], 0, 1)              # true opacity
        if args.profile == "dust":
            # Dust is a cold particulate card, not a mini lit smoke volume.
            # Keeping B/G's volume-light ratio exposes ray-step/self-shadow
            # bands in the first dense cells.  Shape it from the integrated
            # density instead, then erode it with stable coarse grain so the
            # parcel tears as it expands without temporal glitter.
            base = rgba[..., 1]
            # B/G before the dust rewrite is the marched internal light just
            # like SmokePuff's contract. The old dust path threw it away and
            # replaced it with an 11px density blur, flattening every parcel.
            volume_value = np.clip(rgba[..., 2] / np.maximum(base, 1e-3), 0.0, 1.0)
            rng = np.random.default_rng(0xD057 + i)
            coarse_size = max(5, args.cell // 24)
            fine_size = max(9, args.cell // 11)
            coarse = rng.random((coarse_size, coarse_size), dtype=np.float32)
            fine = rng.random((fine_size, fine_size), dtype=np.float32)
            coarse = np.asarray(Image.fromarray((coarse * 255).astype(np.uint8)).resize(
                (args.cell, args.cell), Image.Resampling.BICUBIC), np.float32) / 255.0
            fine = np.asarray(Image.fromarray((fine * 255).astype(np.uint8)).resize(
                (args.cell, args.cell), Image.Resampling.BICUBIC), np.float32) / 255.0
            grain = coarse * 0.68 + fine * 0.32
            # A dust parcel must never begin as an opaque white stamp.  Preserve
            # its dense core, but keep headroom for the material tint and let
            # the per-card alpha stack build the impact cloud.
            # Put the grain into the THRESHOLD, not only opacity. That breaks
            # the silhouette into particulate lobes instead of painting noise
            # over one smooth smoke blob.
            soft = np.clip((base - 0.16 + (grain - 0.5) * 0.23) / 0.66, 0.0, 1.0)
            rgba[..., 1] = soft
            # Same B/G contract consumed by pack.py and SmokePuff: B is the
            # density integral with a VALUE term, so B/G becomes the sprite's
            # internal light. Do not use the ray-marched overhead shadow here —
            # it banded in dense early dust frames. A broad density blur gives
            # a stable bright body / darker broken rim instead.
            shadow = np.asarray(Image.fromarray((volume_value * 255).astype(np.uint8)).filter(
                ImageFilter.GaussianBlur(radius=max(1.0, args.cell * 0.006))),
                np.float32) / 255.0
            local = np.asarray(Image.fromarray((base * 255).astype(np.uint8)).filter(
                ImageFilter.GaussianBlur(radius=max(1.0, args.cell * 0.010))),
                np.float32) / 255.0
            # Preserve the broad dynamic range that makes SmokePuff read as a
            # volume (measured p10≈0.25). The previous 0.18 + 0.64 floor made
            # Dust p10≈0.55, mathematically flattening every card before the
            # particle renderer ever saw it.
            value = np.clip(0.04 + 0.86 * shadow + 0.10 * local, 0.0, 1.0)
            rgba[..., 2] = soft * value
            rgba[..., 3] = soft * np.clip(0.46 + grain * 0.50, 0.0, 1.0)
        # Rows: image Y already runs down from the grid's top, so no flip here.
        Image.fromarray((rgba * 255).astype(np.uint8), "RGBA").save(
            os.path.join(out_dir, "f%03d.png" % (i + 1)))

    print("RENDER: %d frames -> %s  (%.1fs)" % (len(frames), out_dir, time.time() - t0))
    print("RENDER: next  python3 scripts/flipbook/pack.py %s --grid 8 "
          "--alpha-from-luma 0 --out <name>.png" % out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
