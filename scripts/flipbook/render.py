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
    ap.add_argument("--flame-projection", default="integral",
                    choices=["peak", "integral"],
                    help="how the hot volume becomes the flame mask. 'integral' "
                         "is the physical emission/absorption ray integral and "
                         "the shipping default; 'peak' is a silhouette-debug "
                         "view, not a volume render")
    ap.add_argument("--emission-gamma", type=float, default=1.0,
                    help="shape the normalised flame emission before packing. "
                         "< 1 lifts the weak shoulder relative to the centre, "
                         "preventing a white-solid disc; 1 keeps legacy linear output")
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
    ap.add_argument("--profile", default="volume", choices=["volume", "dust", "eoo"],
                    help="volume keeps the smoke/fire lighting channels. dust writes a "
                         "cold, eroded alpha parcel. eoo writes Niagara EOO fire (R=Planck emission, "
                         "G=Transmittance, B=0, A=Coverage) + BC5 tangent normals.")
    ap.add_argument("--bake-normals", type=int, default=1,
                    help="1 = bake 3D volumetric tangent space normal map to normals/ folder, 0 = off")
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
    # flame envelope/emission, smoke, opacity, shaded value
    out = ti.Vector.field(4, ti.f32, shape=(S, S))
    # 3D Volumetric Tangent Normal: Nx, Ny, Nz (facing camera), Alpha
    out_norm = ti.Vector.field(4, ti.f32, shape=(S, S))

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

    @ti.func
    def border_fade(u: ti.f32, v: ti.f32, m: ti.f32) -> ti.f32:
        fx = ti.math.clamp(u / m, 0.0, 1.0) * ti.math.clamp((1.0 - u) / m, 0.0, 1.0)
        fy = ti.math.clamp(v / m, 0.0, 1.0) * ti.math.clamp((1.0 - v) / m, 0.0, 1.0)
        return fx * fx * (3.0 - 2.0 * fx) * fy * fy * (3.0 - 2.0 * fy)

    @ti.kernel
    def march(ks: ti.f32, kf: ti.f32, kfe: ti.f32, fw: ti.f32, fh: ti.f32,
              peak_flame: ti.i32):
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
            hot_envelope = 0.0
            smoke = 0.0
            shade = 0.0
            accum_norm = ti.Vector([0.0, 0.0, 0.0])
            inside = (u >= 0.0) and (u <= 1.0) and (v >= 0.0) and (v <= 1.0)
            fade = border_fade(u, v, 0.05) if inside else 0.0
            steps = ry * 2 if inside else 0
            dstep = ti.cast(ry - 1, ti.f32) / steps
            for s in range(steps):
                gy = s * dstep
                d = sample(dens, gx, gy, gz)
                f = sample(flame, gx, gy, gz)
                ext = (d * ks + f * kfe) * dstep * fade
                emis += f * kf * trans * dstep * fade
                hot_envelope = ti.max(hot_envelope, f * kf * fade)
                smoke += d * ks * trans * dstep * fade
                shade += d * ks * trans * sample(shad, gx, gy, gz) * dstep * fade

                # Central differences 3D density/flame gradient
                dx = sample(dens, gx + 1.0, gy, gz) - sample(dens, gx - 1.0, gy, gz)
                dy = sample(dens, gx, gy + 1.0, gz) - sample(dens, gx, gy - 1.0, gz)
                dz = sample(dens, gx, gy, gz + 1.0) - sample(dens, gx, gy, gz - 1.0)
                if kfe > 0.0:
                    dx += (sample(flame, gx + 1.0, gy, gz) - sample(flame, gx - 1.0, gy, gz)) * 0.3
                    dy += (sample(flame, gx, gy + 1.0, gz) - sample(flame, gx, gy - 1.0, gz)) * 0.3
                    dz += (sample(flame, gx, gy, gz + 1.0) - sample(flame, gx, gy, gz - 1.0)) * 0.3

                # Tangent space normal pointing outward:
                # Right (+X) = -dx
                # Up (+Y) = -dz
                # Front/Camera (+Z) = dy
                local_n = ti.Vector([-0.5 * dx, -0.5 * dz, 0.5 * dy])
                n_len = local_n.norm()
                if n_len > 1e-4:
                    local_n = local_n / n_len
                else:
                    local_n = ti.Vector([0.0, 0.0, 1.0])

                step_alpha = 1.0 - ti.exp(-ext)
                accum_norm += local_n * trans * step_alpha

                trans *= ti.exp(-ext)
                if trans < 0.004:      # the rest cannot contribute a visible level
                    break
            flame_out = hot_envelope if peak_flame != 0 else emis
            out[px, py] = ti.Vector([flame_out, smoke, 1.0 - trans, shade])

            total_alpha = 1.0 - trans
            if total_alpha > 0.005:
                an_len = accum_norm.norm()
                n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
                n_unit.z = ti.max(0.0, n_unit.z)
                n_unit = n_unit.normalized(1e-4)
                out_norm[px, py] = ti.Vector([
                    0.5 + 0.5 * n_unit.x,
                    0.5 + 0.5 * n_unit.y,
                    0.5 + 0.5 * n_unit.z,
                    total_alpha
                ])
            else:
                out_norm[px, py] = ti.Vector([0.5, 0.5, 1.0, 0.0])

    @ti.func
    def smoothstep_f(e0: ti.f32, e1: ti.f32, x: ti.f32) -> ti.f32:
        t = ti.math.clamp((x - e0) / (e1 - e0), 0.0, 1.0)
        return t * t * (3.0 - 2.0 * t)

    @ti.func
    def snoise3(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
        val = 0.0
        val += ti.sin(p.x * 2.8 + t * 1.1) * ti.cos(p.y * 2.5 - t * 0.9) * ti.sin(p.z * 3.1 + t * 1.2)
        val += 0.55 * ti.sin(p.y * 6.2 + t * 1.7) * ti.cos(p.z * 5.8 - t * 1.5) * ti.sin(p.x * 6.5 + t * 1.8)
        val += 0.32 * ti.sin(p.z * 13.5 - t * 2.9) * ti.cos(p.x * 12.8 + t * 2.6) * ti.sin(p.y * 14.2 - t * 3.1)
        val += 0.18 * ti.sin(p.x * 28.4 + t * 4.5) * ti.cos(p.y * 27.1 - t * 4.2) * ti.sin(p.z * 29.7 + t * 4.8)
        val += 0.09 * ti.sin(p.y * 58.2 + t * 6.8) * ti.cos(p.z * 55.4 - t * 6.1) * ti.sin(p.x * 61.3 + t * 7.2)
        return val

    @ti.func
    def billow_noise(p: ti.types.vector(3, ti.f32), t: ti.f32) -> ti.f32:
        b1 = 1.0 - 2.0 * ti.abs(ti.sin(p.x * 3.5 + t * 1.2) * ti.cos(p.y * 3.1 - t) * ti.sin(p.z * 3.8 + t * 1.1))
        b2 = 1.0 - 2.0 * ti.abs(ti.sin(p.y * 7.8 + t * 1.8) * ti.cos(p.z * 7.2 - t * 1.6) * ti.sin(p.x * 8.1 + t * 1.7))
        b3 = 1.0 - 2.0 * ti.abs(ti.sin(p.z * 17.5 - t * 3.0) * ti.cos(p.x * 16.2 + t * 2.8) * ti.sin(p.y * 18.1 - t * 3.2))
        b4 = 1.0 - 2.0 * ti.abs(ti.sin(p.x * 36.0 + t * 4.6) * ti.cos(p.y * 34.0 - t * 4.4) * ti.sin(p.z * 37.0 + t * 4.9))
        return 0.45 * b1 + 0.30 * b2 + 0.18 * b3 + 0.10 * b4

    @ti.kernel
    def march_eoo(kf: ti.f32, kfe: ti.f32, fw: ti.f32, fh: ti.f32, e_norm: ti.f32, time_val: ti.f32):
        for px, py in out:
            u = ((px + 0.5) / S - 0.5) / fw + 0.5
            v = ((py + 0.5) / S - 0.5) / fh + 0.5
            gx_base = u * (rx - 1)
            gz_base = (1.0 - v) * (rz - 1)

            trans = 1.0
            emis_accum = 0.0
            body_accum = 0.0
            shadow_accum = 0.0
            accum_norm = ti.Vector([0.0, 0.0, 0.0])

            inside = (u >= 0.0) and (u <= 1.0) and (v >= 0.0) and (v <= 1.0)
            fade = border_fade(u, v, 0.04) if inside else 0.0

            steps = ry * 2 if inside else 0
            dstep = ti.cast(ry - 1, ti.f32) / ti.max(steps, 1)

            if inside:
                for s in range(steps):
                    gy = s * dstep
                    p_norm = ti.Vector([gx_base / rx, gy / ry, gz_base / rz])
                    p_stretch = ti.Vector([p_norm.x * 1.25, p_norm.y * 1.25, p_norm.z * 0.85])

                    w_x = snoise3(p_stretch * 5.0 + ti.Vector([12.3, 0, 0]), time_val * 1.4)
                    w_y = snoise3(p_stretch * 5.0 + ti.Vector([0, 45.6, 0]), time_val * 1.4)
                    w_z = snoise3(p_stretch * 5.0 + ti.Vector([0, 0, 78.9]), time_val * 1.4)

                    gx = gx_base + w_x * 3.5
                    gz = gz_base + w_z * 3.5
                    gy_w = gy + w_y * 3.5

                    f_raw = sample(flame, gx, gy_w, gz)
                    if f_raw > 0.002:
                        b_noise = billow_noise(p_stretch * 4.2, time_val * 1.6)
                        fine_wisp = snoise3(p_stretch * 18.0, time_val * 2.8)
                        micro_grain = snoise3(p_stretch * 40.0, time_val * 4.2)

                        modulation = ti.max(0.0, 0.68 + 0.90 * b_noise + 0.32 * fine_wisp + 0.12 * micro_grain)
                        f_mod = f_raw * modulation

                        core_heat = ti.pow(ti.max(0.0, f_mod - 0.08) * 1.7, 1.5)
                        core_heat *= (0.60 + 0.80 * ti.max(0.0, fine_wisp + 0.3))

                        ext = (f_mod * kfe) * (dstep / rz) * 24.0 * fade
                        step_alpha = 1.0 - ti.exp(-ext)

                        emis_accum += core_heat * kf * trans * (dstep / rz) * 20.0 * fade
                        body_accum += f_mod * trans * (dstep / rz) * 16.0 * fade
                        shadow_accum += f_mod * (1.0 - trans) * (dstep / rz) * 14.0 * fade

                        delta = 1.0
                        dx = sample(flame, gx + delta, gy_w, gz) - sample(flame, gx - delta, gy_w, gz)
                        dy = sample(flame, gx, gy_w + delta, gz) - sample(flame, gx, gy_w - delta, gz)
                        dz = sample(flame, gx, gy_w, gz + delta) - sample(flame, gx, gy_w, gz - delta)

                        n_noise_x = snoise3(p_stretch * 12.0, time_val)
                        n_noise_y = snoise3(p_stretch * 12.0 + ti.Vector([7, 7, 7]), time_val)

                        local_n = ti.Vector([-0.5 * dx + 0.12 * n_noise_x, -0.5 * dz + 0.12 * n_noise_y, 0.5 * dy])
                        n_len = local_n.norm()
                        if n_len > 1e-4:
                            local_n /= n_len
                        else:
                            local_n = ti.Vector([0.0, 0.0, 1.0])

                        accum_norm += local_n * trans * step_alpha
                        trans *= ti.exp(-ext)
                        if trans < 0.003:
                            break

            r_raw = emis_accum / ti.max(e_norm, 1e-4)
            r_val = ti.pow(ti.math.clamp(r_raw, 0.0, 1.0), 1.12)

            cov_raw = 1.0 - trans
            edge_wisp = snoise3(ti.Vector([u * 22.0, v * 22.0, 0.5]), time_val * 2.2)
            coverage = smoothstep_f(0.012 + 0.025 * edge_wisp, 0.12, cov_raw) * fade

            body_norm = ti.math.clamp(body_accum * 0.85, 0.0, 1.0)
            crevice_shadow = ti.math.clamp(shadow_accum * 1.4, 0.0, 0.45)
            g_val = ti.math.clamp(1.0 - 0.65 * r_val - 0.20 * body_norm - crevice_shadow, 0.32, 1.0)

            if coverage < 0.005:
                r_val = 0.0
                g_val = 1.0
                coverage = 0.0

            out[px, py] = ti.Vector([r_val, g_val, 0.0, coverage])

            an_len = accum_norm.norm()
            n_unit = accum_norm / an_len if an_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])
            edge_blend = smoothstep_f(0.005, 0.30, coverage)
            n_smooth = n_unit * edge_blend + ti.Vector([0.0, 0.0, 1.0]) * (1.0 - edge_blend)
            ns_len = n_smooth.norm()
            n_final = n_smooth / ns_len if ns_len > 1e-4 else ti.Vector([0.0, 0.0, 1.0])

            out_norm[px, py] = ti.Vector([
                0.5 + 0.5 * n_final.x,
                0.5 + 0.5 * n_final.y,
                0.0,
                1.0
            ])

    t0 = time.time()

    def render_all(fw, fh, quiet=False):
        frames = []
        norm_frames = []
        for i, p in enumerate(files):
            z = np.load(p)
            d = np.ascontiguousarray(z["density"], np.float32)
            dens.from_numpy(d)
            flame.from_numpy(np.ascontiguousarray(z["flame"], np.float32))
            if args.profile == "eoo":
                time_val = float(i) * 0.08
                march_eoo(args.flame_scale, args.flame_extinction, fw, fh, 1.0, time_val)
            else:
                above = np.cumsum(d[::-1], axis=0)[::-1] - d
                shad.from_numpy(np.ascontiguousarray(
                    args.ambient + (1.0 - args.ambient)
                    * np.exp(-args.light * args.density_scale * above), np.float32))
                march(args.density_scale, args.flame_scale, args.flame_extinction,
                      fw, fh, 1 if args.flame_projection == "peak" else 0)
            img = out.to_numpy().transpose(1, 0, 2)
            img_norm = out_norm.to_numpy().transpose(1, 0, 2)
            if args.supersample > 1:
                k = args.supersample
                img = img.reshape(args.cell, k, args.cell, k, 4).mean(axis=(1, 3))
                norm_down = img_norm.reshape(args.cell, k, args.cell, k, 4).mean(axis=(1, 3))
                rgb = norm_down[..., :3] * 2.0 - 1.0
                norm_len = np.maximum(np.linalg.norm(rgb, axis=-1, keepdims=True), 1e-4)
                norm_down[..., :3] = 0.5 + 0.5 * (rgb / norm_len)
                img_norm = norm_down
            frames.append(img)
            norm_frames.append(img_norm)
            if i % 8 == 0 and not quiet:
                print("RENDER: %d/%d  %.1fs" % (i, len(files), time.time() - t0),
                      flush=True)
        return frames, norm_frames

    if autofit:
        # Measure on the UNCROPPED render, where nothing can be cut off
        alpha_ch = 3 if args.profile == "eoo" else 2
        probe = np.stack(render_all(fit_w, fit_h, quiet=True)[0])[..., alpha_ch]
        lit = probe > 0.005
        half = args.cell / 2.0
        ys, xs = np.nonzero(lit.any(axis=0))
        if len(ys):
            reach = max(abs(ys.max() + 0.5 - half), abs(ys.min() + 0.5 - half),
                        abs(xs.max() + 0.5 - half), abs(xs.min() + 0.5 - half)) / half
            zoom = 0.82 / max(reach, 1e-3)
            fit_w, fit_h = min(1.0, aspect) * zoom, min(1.0, 1.0 / aspect) * zoom
            print("RENDER: autofit reach %.3f of the domain half-width -> zoom %.2f"
                  % (reach, zoom))
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

    frames, norm_frames = render_all(fit_w, fit_h)
    stack = np.stack(frames)
    e_max = max(1e-5, float(np.percentile(stack[..., 0], 99.5)))
    s_max = max(1e-5, float(np.percentile(stack[..., 1], 99.5)))
    print("RENDER: normalising emission/%.4f smoke/%.4f" % (e_max, s_max))

    out_dir = os.path.join(args.cache_dir, "frames")
    os.makedirs(out_dir, exist_ok=True)
    normals_dir = os.path.join(args.cache_dir, "normals")
    if args.bake_normals:
        os.makedirs(normals_dir, exist_ok=True)

    for i, (img, n_img) in enumerate(zip(frames, norm_frames)):
        rgba = np.zeros((args.cell, args.cell, 4), np.float32)
        if args.profile == "eoo":
            emission = np.clip(img[..., 0] / e_max, 0, 1)
            if args.emission_gamma != 1.0:
                emission = emission ** max(args.emission_gamma, 0.05)
            rgba[..., 0] = emission                               # flame emission
            rgba[..., 1] = np.clip(img[..., 1], 0, 1)             # transmittance G
            rgba[..., 2] = 0.0                                    # B is 0
            rgba[..., 3] = np.clip(img[..., 3], 0, 1)             # coverage A
        else:
            emission = np.clip(img[..., 0] / e_max, 0, 1)
            if args.emission_gamma != 1.0:
                emission = emission ** max(args.emission_gamma, 0.05)
            rgba[..., 0] = emission                            # flame emission
            rgba[..., 1] = np.clip(img[..., 1] / s_max, 0, 1)     # smoke
            rgba[..., 2] = np.clip(img[..., 3] / s_max, 0, 1)      # self-shadowed value
            rgba[..., 3] = np.clip(img[..., 2], 0, 1)              # true opacity
            if args.profile == "dust":
                base = rgba[..., 1]
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
                soft = np.clip((base - 0.16 + (grain - 0.5) * 0.23) / 0.66, 0.0, 1.0)
                rgba[..., 1] = soft
                shadow = np.asarray(Image.fromarray((volume_value * 255).astype(np.uint8)).filter(
                    ImageFilter.GaussianBlur(radius=max(1.0, args.cell * 0.006))),
                    np.float32) / 255.0
                local = np.asarray(Image.fromarray((base * 255).astype(np.uint8)).filter(
                    ImageFilter.GaussianBlur(radius=max(1.0, args.cell * 0.010))),
                    np.float32) / 255.0
                value = np.clip(0.04 + 0.86 * shadow + 0.10 * local, 0.0, 1.0)
                rgba[..., 2] = soft * value
                rgba[..., 3] = soft * np.clip(0.46 + grain * 0.50, 0.0, 1.0)

        # Rows: image Y already runs down from the grid's top, so no flip here.
        Image.fromarray((rgba * 255).astype(np.uint8), "RGBA").save(
            os.path.join(out_dir, "f%03d.png" % (i + 1)))

        if args.bake_normals:
            if args.profile == "eoo":
                n_rgba = np.clip(n_img, 0.0, 1.0)
                zero_alpha = rgba[..., 3] < 0.005
                n_rgba[zero_alpha, 0] = 0.5
                n_rgba[zero_alpha, 1] = 0.5
                n_rgba[zero_alpha, 2] = 0.0
                n_rgba[..., 3] = 1.0
            else:
                n_rgba = np.clip(n_img, 0.0, 1.0)
                zero_alpha = n_rgba[..., 3] < 0.005
                n_rgba[zero_alpha, 0] = 0.5
                n_rgba[zero_alpha, 1] = 0.5
                n_rgba[zero_alpha, 2] = 1.0
                n_rgba[zero_alpha, 3] = 0.0
            Image.fromarray((n_rgba * 255).astype(np.uint8), "RGBA").save(
                os.path.join(normals_dir, "f%03d.png" % (i + 1)))

    print("RENDER: %d frames -> %s and %s (%.1fs)" % (len(frames), out_dir, normals_dir, time.time() - t0))
    print("RENDER: next  python3 scripts/flipbook/pack.py %s --grid 8 --alpha-from-luma 0 --out <name>_8x8.png" % out_dir)
    if args.bake_normals:
        print("RENDER: normal pack -> python3 scripts/flipbook/pack.py %s --grid 8 --alpha-from-luma 0 --out <name>_normals_8x8.png" % normals_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
