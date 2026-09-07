#!/usr/bin/env python3
"""
Generates maps/toolkit/textures/foliage_atlas_v1.png
Master 4-quadrant AAA Foliage Atlas:
- Quadrant 0 (Top-Left): Radial Blooms (<= 8 petals: Daisy 8-petal, Poppy 5-petal, Bell 6-petal, Cup side-view)
- Quadrant 1 (Top-Right): Vertical Floral Spikes (Lavender/Larkspur floret clusters along stems)
- Quadrant 2 (Bottom-Left): Fluffy Reed Plumes (Phragmites feather tassels with micro-hair alpha)
- Quadrant 3 (Bottom-Right): Foliage Leaves & Basal Rosettes (Long reed blades, flower base rosettes)
"""

import math
import zlib
import struct
import os

WIDTH = 1024
HEIGHT = 1024

def create_atlas():
    img_data = bytearray(WIDTH * HEIGHT * 4)

    def set_pixel(x, y, r, g, b, a):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            idx = (y * WIDTH + x) * 4
            img_data[idx + 0] = max(0, min(255, int(r)))
            img_data[idx + 1] = max(0, min(255, int(g)))
            img_data[idx + 2] = max(0, min(255, int(b)))
            img_data[idx + 3] = max(0, min(255, int(a)))

    def blend_pixel(x, y, r, g, b, a):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT and a > 0:
            idx = (y * WIDTH + x) * 4
            src_a = a / 255.0
            dst_a = img_data[idx + 3] / 255.0
            out_a = src_a + dst_a * (1.0 - src_a)
            if out_a > 0.001:
                out_r = (r * src_a + img_data[idx + 0] * dst_a * (1.0 - src_a)) / out_a
                out_g = (g * src_a + img_data[idx + 1] * dst_a * (1.0 - src_a)) / out_a
                out_b = (b * src_a + img_data[idx + 2] * dst_a * (1.0 - src_a)) / out_a
                img_data[idx + 0] = int(out_r)
                img_data[idx + 1] = int(out_g)
                img_data[idx + 2] = int(out_b)
                img_data[idx + 3] = int(out_a * 255)

    # -------------------------------------------------------------
    # Quadrant 0: Radial Blooms (x: 0..512, y: 0..512)
    # -------------------------------------------------------------
    # Sub-slot 0 (0..256, 0..256): 8-petal Daisy / Cosmos
    cx0, cy0 = 128, 128
    radius0 = 110.0
    for y in range(0, 256):
        for x in range(0, 256):
            dx = x - cx0
            dy = y - cy0
            dist = math.hypot(dx, dy)
            if dist > radius0 + 2.0:
                continue
            angle = math.atan2(dy, dx)
            # 8 petals
            petal_wave = math.cos(8.0 * angle)
            # Petal shape: elongated elliptic lobe
            petal_r = radius0 * (0.40 + 0.60 * math.pow(max(0.0, (petal_wave + 1.0) * 0.5), 0.75))
            if dist < petal_r:
                edge = min(1.0, max(0.0, (petal_r - dist) / 2.0))
                # Micro veins along petal length
                vein = 0.92 + 0.08 * math.sin(angle * 64.0) * (dist / radius0)
                # Petal gradient: white to pale ivory
                albedo = int(245 * vein)
                set_pixel(x, y, albedo, albedo, albedo, int(255 * edge))
            # Center seed disc
            disc_r = 30.0
            if dist < disc_r:
                d_edge = min(1.0, max(0.0, (disc_r - dist) / 2.0))
                noise = 0.88 + 0.12 * math.sin(dist * 0.8 + angle * 16.0)
                cr = int(225 * noise)
                cg = int(180 * noise)
                cb = int(50 * noise)
                blend_pixel(x, y, cr, cg, cb, int(255 * d_edge))

    # Sub-slot 1 (256..512, 0..256): 5-petal Poppy / Buttercup
    cx1, cy1 = 384, 128
    radius1 = 112.0
    for y in range(0, 256):
        for x in range(256, 512):
            dx = x - cx1
            dy = y - cy1
            dist = math.hypot(dx, dy)
            if dist > radius1 + 2.0:
                continue
            angle = math.atan2(dy, dx)
            # 5 overlapping broad petals
            petal_wave = math.cos(5.0 * angle)
            petal_r = radius1 * (0.50 + 0.50 * math.pow(max(0.0, (petal_wave + 1.0) * 0.5), 0.45))
            if dist < petal_r:
                edge = min(1.0, max(0.0, (petal_r - dist) / 2.0))
                ripple = 0.90 + 0.10 * math.sin(dist * 0.25 + angle * 15.0)
                set_pixel(x, y, int(240 * ripple), int(235 * ripple), int(225 * ripple), int(255 * edge))
            # Dark poppy center
            center_r = 26.0
            if dist < center_r:
                c_edge = min(1.0, max(0.0, (center_r - dist) / 2.0))
                blend_pixel(x, y, 65, 45, 30, int(255 * c_edge))

    # Sub-slot 2 (0..256, 256..512): 6-petal Bell / Star bloom
    cx2, cy2 = 128, 384
    radius2 = 110.0
    for y in range(256, 512):
        for x in range(0, 256):
            dx = x - cx2
            dy = y - cy2
            dist = math.hypot(dx, dy)
            if dist > radius2 + 2.0:
                continue
            angle = math.atan2(dy, dx)
            petal_wave = math.cos(6.0 * angle)
            petal_r = radius2 * (0.35 + 0.65 * math.pow(max(0.0, (petal_wave + 1.0) * 0.5), 1.2))
            if dist < petal_r:
                edge = min(1.0, max(0.0, (petal_r - dist) / 2.0))
                groove = 0.88 + 0.12 * math.cos(angle * 6.0)
                set_pixel(x, y, int(235 * groove), int(230 * groove), int(240 * groove), int(255 * edge))
            if dist < 22.0:
                c_edge = min(1.0, max(0.0, (22.0 - dist) / 2.0))
                blend_pixel(x, y, 220, 200, 70, int(255 * c_edge))

    # Sub-slot 3 (256..512, 256..512): Side-profile 3D Cup Bloom
    cx3, cy3 = 384, 384
    for y in range(256, 512):
        for x in range(256, 512):
            nx = (x - cx3) / 105.0
            ny = (y - cy3) / 105.0
            # Cup profile: rounded bottom at ny=0.7, flaring rim at ny=-0.7
            if ny < -0.80 or ny > 0.80:
                continue
            half_w = 0.18 + 0.65 * math.pow(max(0.0, (0.80 - ny) / 1.60), 0.70)
            if abs(nx) < half_w:
                dist_to_edge = half_w - abs(nx)
                edge = min(1.0, max(0.0, dist_to_edge * 35.0))
                # Vertical folds on cup petals
                folds = 0.88 + 0.12 * math.cos(nx * 18.0)
                shade = max(0.70, min(1.10, 1.0 - ny * 0.25))
                set_pixel(x, y, int(240 * folds * shade), int(235 * folds * shade), int(230 * folds * shade), int(255 * edge))

    # -------------------------------------------------------------
    # Quadrant 1: Vertical Floral Spikes (Lavender / Larkspur) (x: 512..1024, y: 0..512)
    # -------------------------------------------------------------
    # Sub-slot 0 (512..768, 0..512): Lavender Spike Strip
    stem_x = 640
    for y in range(16, 496):
        t = (y - 16) / 480.0  # 0: top bud, 1: lower stem
        # Thin central green stem
        for sx in range(-3, 4):
            set_pixel(stem_x + sx, y, 85, 128, 55, 255)
        # Florets along the upper 70% of the strip (t from 0.05 to 0.75)
        if 0.05 < t < 0.75:
            floret_width = 48.0 * math.sin((t - 0.05) / 0.70 * 3.14159)
            density = 14.0
            tier = math.floor(y / density)
            cluster_y = tier * density + density * 0.5
            dy_c = (y - cluster_y) / (density * 0.48)
            if abs(dy_c) < 1.0:
                for x in range(int(stem_x - floret_width), int(stem_x + floret_width)):
                    dx_c = (x - stem_x) / max(1.0, floret_width)
                    floret_dist = math.hypot(dx_c, dy_c)
                    if floret_dist < 1.0:
                        alpha = min(1.0, max(0.0, (1.0 - floret_dist) * 3.5))
                        # Petal texture: soft lavender purple with edge highlights
                        v_shading = 0.85 + 0.15 * math.sin(dx_c * 12.0)
                        pr = int(185 * v_shading)
                        pg = int(160 * v_shading)
                        pb = int(228 * v_shading)
                        blend_pixel(x, y, pr, pg, pb, int(255 * alpha))

    # Sub-slot 1 (768..1024, 0..512): White/Rose Larkspur Spike
    stem_x2 = 896
    for y in range(16, 496):
        t = (y - 16) / 480.0
        for sx in range(-3, 4):
            set_pixel(stem_x2 + sx, y, 90, 132, 58, 255)
        if 0.06 < t < 0.78:
            floret_width = 54.0 * math.sin((t - 0.06) / 0.72 * 3.14159)
            density = 16.0
            tier = math.floor(y / density)
            cluster_y = tier * density + density * 0.5
            dy_c = (y - cluster_y) / (density * 0.50)
            if abs(dy_c) < 1.0:
                for x in range(int(stem_x2 - floret_width), int(stem_x2 + floret_width)):
                    dx_c = (x - stem_x2) / max(1.0, floret_width)
                    floret_dist = math.hypot(dx_c, dy_c)
                    if floret_dist < 1.0:
                        alpha = min(1.0, max(0.0, (1.0 - floret_dist) * 3.0))
                        sh = 0.90 + 0.10 * math.cos(dx_c * 10.0)
                        blend_pixel(x, y, int(245 * sh), int(230 * sh), int(225 * sh), int(255 * alpha))

    # -------------------------------------------------------------
    # Quadrant 2: Fluffy Reed Plumes (Phragmites) (x: 0..512, y: 512..1024)
    # -------------------------------------------------------------
    plume_cx = 256
    for y in range(520, 1000):
        t = (y - 520) / 480.0  # 0: plume tip (top), 1: plume base (bottom)
        # Plume profile: búp măng / spindle tapering at tip and base
        if t < 0.15:
            profile = t / 0.15
        elif t < 0.75:
            profile = 1.0 - (t - 0.15) * 0.25
        else:
            profile = 0.85 * (1.0 - (t - 0.75) / 0.25)

        half_w = max(4.0, 78.0 * profile)
        # Slight gentle lean curve to the right
        lean_x = plume_cx + math.pow(1.0 - t, 1.6) * 28.0

        for x in range(int(lean_x - half_w - 20), int(lean_x + half_w + 20)):
            dx = x - lean_x
            dist_norm = abs(dx) / max(1.0, half_w)
            if dist_norm < 1.25:
                # Micro-feather hair noise
                hair_noise = math.sin(y * 0.85 + dx * 0.6) * 0.25 \
                           + math.sin(y * 1.7 - dx * 0.9) * 0.18 \
                           + math.sin(dx * 2.2 + y * 0.4) * 0.12
                effective_dist = dist_norm + hair_noise * (0.20 + 0.30 * dist_norm)
                if effective_dist < 1.0:
                    alpha = min(1.0, max(0.0, (1.0 - effective_dist) * 2.5))
                    # Color: warm golden tan at bottom to pale ivory fluff at top
                    color_lerp = 1.0 - t
                    cr = int(228 + 22 * color_lerp)
                    cg = int(218 + 24 * color_lerp)
                    cb = int(196 + 36 * color_lerp)
                    # Subtle fibrous luminance
                    fib = 0.94 + 0.06 * math.sin(y * 2.5 + dx * 1.5)
                    blend_pixel(x, y, int(cr * fib), int(cg * fib), int(cb * fib), int(255 * alpha))

    # Central reed culm/stem through the bottom of the plume
    for y in range(800, 1020):
        for sx in range(-3, 4):
            set_pixel(plume_cx + sx, y, 105, 138, 62, 255)

    # -------------------------------------------------------------
    # Quadrant 3: Foliage Leaves & Basal Rosettes (x: 512..1024, y: 512..1024)
    # -------------------------------------------------------------
    # Sub-slot 0 (512..768, 512..1024): Long Arching Reed Leaves
    leaf_cx = 640
    for y in range(530, 1010):
        t = (y - 530) / 480.0
        # Leaf curves outwards then tapers to sharp point at y=530
        curve_x = leaf_cx + math.sin(t * 3.14159) * 35.0
        lw = 24.0 * math.sin(t * 3.14159)
        for x in range(int(curve_x - lw - 2), int(curve_x + lw + 2)):
            dx = x - curve_x
            if abs(dx) < lw:
                edge = min(1.0, max(0.0, (lw - abs(dx)) * 1.5))
                # Center midrib vein
                is_vein = abs(dx) < 2.0
                if is_vein:
                    cr, cg, cb = 135, 175, 78
                else:
                    shade = 0.92 + 0.08 * math.cos(dx / max(1.0, lw) * 3.14159)
                    cr, cg, cb = int(92 * shade), int(136 * shade), int(52 * shade)
                set_pixel(x, y, cr, cg, cb, int(255 * edge))

    # Sub-slot 1 (768..1024, 512..1024): Basal Leaf Rosette
    rcx, rcy = 896, 768
    for y in range(600, 936):
        for x in range(728, 1024):
            dx = x - rcx
            dy = y - rcy
            dist = math.hypot(dx, dy)
            if dist > 140.0:
                continue
            angle = math.atan2(dy, dx)
            # 5 rosette leaves
            leaf_wave = math.cos(5.0 * angle)
            lr = 135.0 * math.pow(max(0.0, (leaf_wave + 1.0) * 0.5), 0.70)
            if dist < lr:
                edge = min(1.0, max(0.0, (lr - dist) / 2.5))
                vein_line = abs(math.sin(angle * 2.5)) < 0.08
                if vein_line:
                    cr, cg, cb = 120, 160, 68
                else:
                    cr, cg, cb = 68, 108, 42
                set_pixel(x, y, cr, cg, cb, int(255 * edge))

    return img_data

def write_png(filepath, width, height, raw_rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    scanlines = bytearray()
    row_stride = width * 4
    for y in range(height):
        scanlines.append(0)  # filter type None
        scanlines.extend(raw_rgba[y * row_stride:(y + 1) * row_stride])
    compressed = zlib.compress(bytes(scanlines), 9)

    with open(filepath, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", compressed))
        f.write(chunk(b"IEND", b""))

if __name__ == "__main__":
    out_dir = "maps/toolkit/textures"
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "foliage_atlas_v1.png")
    print(f"Generating master foliage atlas -> {out_path}")
    raw_data = create_atlas()
    write_png(out_path, WIDTH, HEIGHT, raw_data)
    print("Done! Foliage atlas generated successfully.")
