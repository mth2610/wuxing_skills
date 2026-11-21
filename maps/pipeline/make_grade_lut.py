#!/usr/bin/env python3
"""
Generate Ghost of Tsushima cinematic color grade LUT strip for Wuxing Skills.
Strip format: 256x16 RGBA8 (16 slices of 16x16, U=Red, V=Green, Slice=Blue).
Pure Python standard library (no external PIL / Pillow dependency).
"""

import math
import os
import struct
import zlib

LUT_SIZE = 16
LUT_WIDTH = LUT_SIZE * LUT_SIZE
LUT_HEIGHT = LUT_SIZE

def clamp01(x):
    return max(0.0, min(1.0, x))

def srgb_to_linear(c):
    if c <= 0.04045:
        return c / 12.92
    return math.pow((c + 0.055) / 1.055, 2.4)

def linear_to_srgb(c):
    if c <= 0.0031308:
        return c * 12.92
    return 1.055 * math.pow(max(0.0, c), 1.0 / 2.4) - 0.055

def rgb_to_hsl(r, g, b):
    max_c = max(r, g, b)
    min_c = min(r, g, b)
    l = (max_c + min_c) * 0.5
    if max_c == min_c:
        return 0.0, 0.0, l
    d = max_c - min_c
    s = d / (2.0 - max_c - min_c) if l > 0.5 else d / (max_c + min_c)
    if max_c == r:
        h = (g - b) / d + (6.0 if g < b else 0.0)
    elif max_c == g:
        h = (b - r) / d + 2.0
    else:
        h = (r - g) / d + 4.0
    h /= 6.0
    return h, s, l

def hue_to_rgb(p, q, t):
    if t < 0.0: t += 1.0
    if t > 1.0: t -= 1.0
    if t < 1.0 / 6.0: return p + (q - p) * 6.0 * t
    if t < 1.0 / 2.0: return q
    if t < 2.0 / 3.0: return p + (q - p) * (2.0 / 3.0 - t) * 6.0
    return p

def hsl_to_rgb(h, s, l):
    if s == 0.0:
        return l, l, l
    q = l * (1.0 + s) if l < 0.5 else l + s - l * s
    p = 2.0 * l - q
    r = hue_to_rgb(p, q, h + 1.0 / 3.0)
    g = hue_to_rgb(p, q, h)
    b = hue_to_rgb(p, q, h - 1.0 / 3.0)
    return r, g, b

def grade_pixel(r01, g01, b01):
    r_lin = srgb_to_linear(r01)
    g_lin = srgb_to_linear(g01)
    b_lin = srgb_to_linear(b01)

    luma = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin

    # 1. Subtle S-Curve Contrast in linear space
    luma_curved = luma * luma * (3.0 - 2.0 * luma)
    contrast_factor = luma_curved / max(1e-5, luma)
    r_lin = clamp01(r_lin * (0.35 + 0.65 * contrast_factor))
    g_lin = clamp01(g_lin * (0.35 + 0.65 * contrast_factor))
    b_lin = clamp01(b_lin * (0.35 + 0.65 * contrast_factor))

    # Lift deep blacks by +0.018 to preserve open shadow details (avoiding black crush)
    black_lift = 0.018 * math.pow(clamp01(1.0 - luma * 2.5), 2.0)
    r_lin += black_lift * 0.95
    g_lin += black_lift * 1.05
    b_lin += black_lift * 1.15

    # 2. Split Toning:
    # Shadow bias: cool teal / navy (preserve open shadows without clipping R)
    shadow_weight = math.pow(clamp01(1.0 - luma * 1.8), 1.5)
    r_lin += shadow_weight * -0.006
    g_lin += shadow_weight * 0.005
    b_lin += shadow_weight * 0.018

    # Highlight bias: gentle natural sun warmth without yellow-tinting grass foliage
    highlight_weight = math.pow(clamp01((luma - 0.38) / 0.62), 1.5)
    r_lin += highlight_weight * 0.010
    g_lin += highlight_weight * 0.007
    b_lin += highlight_weight * -0.003

    r_srgb = clamp01(linear_to_srgb(clamp01(r_lin)))
    g_srgb = clamp01(linear_to_srgb(clamp01(g_lin)))
    b_srgb = clamp01(linear_to_srgb(clamp01(b_lin)))

    # 3. HSL selective tuning (Ghost of Tsushima style):
    h, s, l = rgb_to_hsl(r_srgb, g_srgb, b_srgb)

    # Foliage green tuning (Tsushima lush emerald/jade clarity):
    # Organic olive-sage tone: prevent neon oversaturation and enhance natural depth
    is_green = math.exp(-math.pow((h - 0.29) / 0.08, 2.0))
    s = clamp01(s * (1.0 - 0.04 * is_green))
    h = h + 0.008 * is_green

    # Red/Orange/Gold detection (centered around hue 0.05 = 18 deg)
    dist_red = min(abs(h - 0.05), abs(h - 1.05))
    is_red = math.exp(-math.pow(dist_red / 0.10, 2.0))
    s = clamp01(s * (1.0 + 0.22 * is_red))

    r_out, g_out, b_out = hsl_to_rgb(h, s, l)

    return (
        int(clamp01(r_out) * 255.0 + 0.5),
        int(clamp01(g_out) * 255.0 + 0.5),
        int(clamp01(b_out) * 255.0 + 0.5),
        255
    )

def save_png_rgba(filename, width, height, pixels):
    """Write uncompressed 32-bit RGBA PNG without PIL."""
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    header = b'\x89PNG\r\n\x1a\n'
    ihdr = chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) # 8-bit RGBA
    
    raw_lines = []
    for y in range(height):
        raw_lines.append(b'\x00') # Filter type 0 (None)
        for x in range(width):
            r, g, b, a = pixels[y * width + x]
            raw_lines.append(bytes([r, g, b, a]))
    
    compressed = zlib.compress(b''.join(raw_lines), 9)
    idat = chunk(b'IDAT', compressed)
    iend = chunk(b'IEND', b'')

    with open(filename, 'wb') as f:
        f.write(header + ihdr + idat + iend)

def grade_desert_pixel(r01, g01, b01):
    r_lin = srgb_to_linear(r01)
    g_lin = srgb_to_linear(g01)
    b_lin = srgb_to_linear(b01)

    luma = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin

    # Gentle filmic S-curve + slight black lift for open granite cavities
    luma_curved = luma * luma * (3.0 - 2.0 * luma)
    contrast_factor = luma_curved / max(1e-5, luma)
    r_lin = clamp01(r_lin * (0.55 + 0.45 * contrast_factor))
    g_lin = clamp01(g_lin * (0.55 + 0.45 * contrast_factor))
    b_lin = clamp01(b_lin * (0.55 + 0.45 * contrast_factor))

    black_lift = 0.015 * math.pow(clamp01(1.0 - luma * 2.5), 2.0)
    r_lin += black_lift * 0.90
    g_lin += black_lift * 1.00
    b_lin += black_lift * 1.18

    # Desert split-tone: cool slate-blue shadows, warm cream highlights
    shadow_weight = math.pow(clamp01(1.0 - luma * 1.8), 1.5)
    r_lin += shadow_weight * -0.008
    g_lin += shadow_weight * 0.002
    b_lin += shadow_weight * 0.018

    highlight_weight = math.pow(clamp01((luma - 0.42) / 0.58), 1.4)
    r_lin += highlight_weight * 0.010
    g_lin += highlight_weight * 0.007
    b_lin += highlight_weight * -0.004

    r_srgb = clamp01(linear_to_srgb(clamp01(r_lin)))
    g_srgb = clamp01(linear_to_srgb(clamp01(g_lin)))
    b_srgb = clamp01(linear_to_srgb(clamp01(b_lin)))

    # HSL selective tuning: tame orange/yellow oversaturation (-13%) for pale quartz sand
    h, s, l = rgb_to_hsl(r_srgb, g_srgb, b_srgb)
    dist_orange = min(abs(h - 0.09), abs(h - 1.09))
    is_orange = math.exp(-math.pow(dist_orange / 0.11, 2.0))
    s = clamp01(s * (1.0 - 0.13 * is_orange))

    # Keep sky/shadow blues clean
    is_blue = math.exp(-math.pow((h - 0.60) / 0.10, 2.0))
    s = clamp01(s * (1.0 + 0.08 * is_blue))

    r_out, g_out, b_out = hsl_to_rgb(h, s, l)
    return (
        int(clamp01(r_out) * 255.0 + 0.5),
        int(clamp01(g_out) * 255.0 + 0.5),
        int(clamp01(b_out) * 255.0 + 0.5),
        255
    )


def grade_volcanic_pixel(r01, g01, b01):
    """Volcanic Fire-Element LUT: cool slate-indigo shadows, crisp basalt separation, blazing ember highlights."""
    r_lin = srgb_to_linear(r01)
    g_lin = srgb_to_linear(g01)
    b_lin = srgb_to_linear(b01)

    luma = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin

    # Crisp cinematic S-curve with open charcoal shadow separation (prevents pitch-black crush)
    luma_curved = luma * luma * (3.0 - 2.0 * luma)
    contrast_factor = luma_curved / max(1e-5, luma)
    r_lin = clamp01(r_lin * (0.45 + 0.55 * contrast_factor))
    g_lin = clamp01(g_lin * (0.45 + 0.55 * contrast_factor))
    b_lin = clamp01(b_lin * (0.45 + 0.55 * contrast_factor))

    black_lift = 0.014 * math.pow(clamp01(1.0 - luma * 2.8), 2.0)
    r_lin += black_lift * 0.86
    g_lin += black_lift * 0.94
    b_lin += black_lift * 1.24

    # Cold-vs-Hot Split Toning: cool smoky slate-blue shadows vs warm ember-gold highlights
    shadow_weight = math.pow(clamp01(1.0 - luma * 1.75), 1.5)
    r_lin += shadow_weight * -0.010
    g_lin += shadow_weight * 0.002
    b_lin += shadow_weight * 0.024

    highlight_weight = math.pow(clamp01((luma - 0.42) / 0.58), 1.45)
    r_lin += highlight_weight * 0.012
    g_lin += highlight_weight * 0.004
    b_lin += highlight_weight * -0.008

    r_srgb = clamp01(linear_to_srgb(clamp01(r_lin)))
    g_srgb = clamp01(linear_to_srgb(clamp01(g_lin)))
    b_srgb = clamp01(linear_to_srgb(clamp01(b_lin)))

    h, s, l = rgb_to_hsl(r_srgb, g_srgb, b_srgb)
    # Keep fiery magma & oxidized scoria crisp without clipping
    dist_fire = min(abs(h - 0.04), abs(h - 1.04))
    is_fire = math.exp(-math.pow(dist_fire / 0.09, 2.0))
    s = clamp01(s * (1.0 + 0.12 * is_fire))

    # Cool smoky blue-teal clarity in shadows & coastal sea
    is_cool = math.exp(-math.pow((h - 0.58) / 0.12, 2.0))
    s = clamp01(s * (1.0 + 0.10 * is_cool))

    r_out, g_out, b_out = hsl_to_rgb(h, s, l)
    return (
        int(clamp01(r_out) * 255.0 + 0.5),
        int(clamp01(g_out) * 255.0 + 0.5),
        int(clamp01(b_out) * 255.0 + 0.5),
        255
    )


def grade_alpine_pixel(r01, g01, b01):
    """Alpine Water-Element LUT: crisp cobalt-blue shadows, silk-soft snow highlights, vivid glacier cyan."""
    r_lin = srgb_to_linear(r01)
    g_lin = srgb_to_linear(g01)
    b_lin = srgb_to_linear(b01)

    luma = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin

    # Gentle S-curve with highlight shoulder protection so snowpack keeps sastrugi detail
    luma_curved = luma * luma * (3.0 - 2.0 * luma)
    contrast_factor = luma_curved / max(1e-5, luma)
    shoulder = clamp01((luma - 0.68) / 0.32)
    curve_blend = 0.48 * (1.0 - 0.65 * shoulder)
    r_lin = clamp01(r_lin * ((1.0 - curve_blend) + curve_blend * contrast_factor))
    g_lin = clamp01(g_lin * ((1.0 - curve_blend) + curve_blend * contrast_factor))
    b_lin = clamp01(b_lin * ((1.0 - curve_blend) + curve_blend * contrast_factor))

    # Prussian-blue shadow lift preventing dark slate crags from crushing to black
    black_lift = 0.016 * math.pow(clamp01(1.0 - luma * 2.6), 2.0)
    r_lin += black_lift * 0.82
    g_lin += black_lift * 0.96
    b_lin += black_lift * 1.26

    # Alpine split-toning: icy cobalt-blue shadows, crisp crystal-white / pale-gold highlights
    shadow_weight = math.pow(clamp01(1.0 - luma * 1.7), 1.5)
    r_lin += shadow_weight * -0.012
    g_lin += shadow_weight * 0.004
    b_lin += shadow_weight * 0.026

    highlight_weight = math.pow(clamp01((luma - 0.48) / 0.52), 1.5)
    r_lin += highlight_weight * 0.006
    g_lin += highlight_weight * 0.006
    b_lin += highlight_weight * 0.004

    r_srgb = clamp01(linear_to_srgb(clamp01(r_lin)))
    g_srgb = clamp01(linear_to_srgb(clamp01(g_lin)))
    b_srgb = clamp01(linear_to_srgb(clamp01(b_lin)))

    h, s, l = rgb_to_hsl(r_srgb, g_srgb, b_srgb)
    # Boost glacial cyan/azure clarity (+14%)
    is_cyan_blue = math.exp(-math.pow((h - 0.56) / 0.11, 2.0))
    s = clamp01(s * (1.0 + 0.14 * is_cyan_blue))

    r_out, g_out, b_out = hsl_to_rgb(h, s, l)
    return (
        int(clamp01(r_out) * 255.0 + 0.5),
        int(clamp01(g_out) * 255.0 + 0.5),
        int(clamp01(b_out) * 255.0 + 0.5),
        255
    )


def main():
    import sys
    desert_mode = "--desert" in sys.argv
    volcanic_mode = "--volcanic" in sys.argv
    alpine_mode = "--alpine" in sys.argv
    out_arg = next((a for a in sys.argv[1:] if not a.startswith("--")), None)
    if alpine_mode:
        fn = grade_alpine_pixel
    elif volcanic_mode:
        fn = grade_volcanic_pixel
    elif desert_mode:
        fn = grade_desert_pixel
    else:
        fn = grade_pixel

    denom = float(LUT_SIZE - 1)
    pixels = [(0, 0, 0, 255)] * (LUT_WIDTH * LUT_HEIGHT)

    for slice_b in range(LUT_SIZE):
        for g in range(LUT_SIZE):
            for r in range(LUT_SIZE):
                r01 = float(r) / denom
                g01 = float(g) / denom
                b01 = float(slice_b) / denom
                rgba = fn(r01, g01, b01)
                x = slice_b * LUT_SIZE + r
                y = g
                pixels[y * LUT_WIDTH + x] = rgba

    if out_arg:
        out_path = out_arg
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(os.path.dirname(script_dir))
        out_path = os.path.join(project_root, "assets", "luts", "grade.png")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    save_png_rgba(out_path, LUT_WIDTH, LUT_HEIGHT, pixels)
    mode_label = "alpine" if alpine_mode else ("volcanic" if volcanic_mode else ("desert" if desert_mode else "wuxia"))
    print(f"Generated {mode_label} LUT: {out_path} ({LUT_WIDTH}x{LUT_HEIGHT})")

    if not out_arg:
        out_path = "assets/maps/song_quao/song_quao_grade.png"
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        save_png_rgba(out_path, LUT_WIDTH, LUT_HEIGHT, pixels)
        print(f"Generated {out_path} ({LUT_WIDTH}x{LUT_HEIGHT}) successfully with pure Python.")

if __name__ == "__main__":
    main()
