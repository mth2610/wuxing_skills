#!/usr/bin/env python3
"""
Generates maps/toolkit/textures/reed_plume_atlas.png
Dedicated high-resolution (512x1024) fluffy reed plume (Phragmites / Pampas grass) texture.
Features:
- Thousands of procedural curved hair filaments branching at botanical angles.
- Density envelope R(t) = R_max * sin(pi * t^0.8)^0.6.
- Soft volumetric core with delicate wispy hair tips in the alpha channel.
- Warm straw base gradient transitioning to pale silvery-cream fluff.
"""

import math
import random
import zlib
import struct
import os

WIDTH = 512
HEIGHT = 1024

def generate_reed_plume():
    img_data = bytearray(WIDTH * HEIGHT * 4)

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
                img_data[idx + 0] = int(min(255, max(0, out_r)))
                img_data[idx + 1] = int(min(255, max(0, out_g)))
                img_data[idx + 2] = int(min(255, max(0, out_b)))
                img_data[idx + 3] = int(min(255, max(0, out_a * 255)))

    # Seed random for determinism
    rng = random.Random(42918)

    cx = WIDTH // 2  # 256
    top_y = 60
    bottom_y = 980
    plume_h = bottom_y - top_y

    # 1. Base volumetric soft mist layer following botanical envelope
    for y in range(top_y, bottom_y):
        t = (y - top_y) / plume_h  # 0 at top, 1 at base
        u = 1.0 - t  # 1 at top, 0 at base
        # Density envelope R(u) = sin(pi * u^0.8)^0.6
        s = math.sin(math.pi * math.pow(max(0.0, min(1.0, u)), 0.78))
        if s < 0.0: s = 0.0
        max_w = 175.0 * math.pow(s, 0.62) + 6.0

        # Color gradient: base straw-tan -> tip silvery cream
        cr = int(218 + 37 * (1.0 - t * 0.8))
        cg = int(206 + 46 * (1.0 - t * 0.8))
        cb = int(182 + 68 * (1.0 - t * 0.8))

        for x in range(int(cx - max_w - 20), int(cx + max_w + 20)):
            dx = abs(x - cx)
            if dx < max_w:
                norm_d = dx / max_w
                # Soft Gaussian-like core falloff
                core_alpha = math.exp(-3.2 * norm_d * norm_d)
                # Add micro-fiber vertical noise
                noise = 0.85 + 0.15 * math.sin(y * 0.7 + dx * 0.4) + 0.10 * math.cos(y * 1.8 - dx * 0.8)
                a = int(185 * core_alpha * noise)
                if a > 0:
                    blend_pixel(x, y, cr, cg, cb, a)

    # 2. Draw 2400 individual procedural hair filaments
    hair_count = 2400
    for h in range(hair_count):
        # Position along plume axis
        t = rng.uniform(0.03, 0.95)  # 0: top, 1: bottom
        u = 1.0 - t
        y0 = top_y + t * plume_h
        x0 = cx + rng.gauss(0, 5.0)

        # Envelope width at this height
        s = math.sin(math.pi * math.pow(max(0.0, min(1.0, u)), 0.78))
        if s < 0.0: s = 0.0
        env_w = 175.0 * math.pow(s, 0.62) + 6.0

        side = -1.0 if rng.random() < 0.5 else 1.0
        # Hair branches upward at 25 to 55 degrees
        angle_deg = rng.uniform(25.0, 55.0)
        angle_rad = angle_deg * math.pi / 180.0

        hair_len = env_w * rng.uniform(0.75, 1.25)
        # Bezier control points for the hair
        p0 = (x0, y0)
        # Mid point: shoots out and up
        mid_x = x0 + side * math.cos(angle_rad) * hair_len * 0.55
        mid_y = y0 - math.sin(angle_rad) * hair_len * 0.55
        # Tip point: sags slightly under gravity and wind
        tip_x = x0 + side * math.cos(angle_rad) * hair_len
        tip_y = y0 - math.sin(angle_rad) * hair_len * 0.85 + rng.uniform(4.0, 18.0)

        # Hair color
        h_cr = int(222 + 33 * u + rng.uniform(-10, 10))
        h_cg = int(212 + 42 * u + rng.uniform(-10, 10))
        h_cb = int(190 + 64 * u + rng.uniform(-10, 10))
        h_alpha = int(rng.uniform(140, 240))

        # Rasterize hair curve using 28 samples
        samples = 28
        for step in range(1, samples + 1):
            tau = step / float(samples)
            omtau = 1.0 - tau
            hx = omtau * omtau * p0[0] + 2.0 * omtau * tau * mid_x + tau * tau * tip_x
            hy = omtau * omtau * p0[1] + 2.0 * omtau * tau * mid_y + tau * tau * tip_y

            # Hair tapers towards tip
            step_alpha = int(h_alpha * (1.0 - tau * 0.45))
            ix, iy = int(round(hx)), int(round(hy))
            blend_pixel(ix, iy, h_cr, h_cg, h_cb, step_alpha)
            # 1-pixel thickness antialiasing
            blend_pixel(ix + 1, iy, h_cr, h_cg, h_cb, step_alpha // 2)
            blend_pixel(ix, iy + 1, h_cr, h_cg, h_cb, step_alpha // 2)

    # 3. Slender central cane stem through plume
    for y in range(int(top_y + plume_h * 0.35), bottom_y):
        frac = (y - bottom_y) / plume_h
        sw = max(1, int(3.5 - frac * 1.5))
        for sx in range(-sw, sw + 1):
            dist = abs(sx) / float(sw + 1)
            a = int(255 * (1.0 - dist * 0.4))
            blend_pixel(cx + sx, y, 142, 168, 92, a)

    # Encode as PNG using zlib
    def make_png(width, height, rgba_data):
        raw = bytearray()
        for y in range(height):
            raw.append(0)  # Filter type 0 (None)
            raw.extend(rgba_data[y * width * 4 : (y + 1) * width * 4])

        def chunk(tag, data):
            crc = zlib.crc32(tag + data) & 0xffffffff
            return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', crc)

        png = bytearray(b'\x89PNG\r\n\x1a\n')
        ihdr = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
        png.extend(chunk(b'IHDR', ihdr))
        png.extend(chunk(b'IDAT', zlib.compress(bytes(raw), 9)))
        png.extend(chunk(b'IEND', b''))
        return bytes(png)

    out_path = "maps/toolkit/textures/reed_plume_atlas.png"
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(make_png(WIDTH, HEIGHT, img_data))
    print(f"Generated {out_path} ({os.path.getsize(out_path)} bytes)")

if __name__ == "__main__":
    generate_reed_plume()
