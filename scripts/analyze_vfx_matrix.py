#!/usr/bin/env python3
"""Measure a VFX fixture's behaviour across backgrounds, from a render_vfx_matrix.sh run.

Answers, per background, the three questions eyeballing a screenshot cannot:

  cover%      how much of the frame the effect occupies (should be background-independent
              — if it is not, the effect is losing its silhouette, not just its colour)
  darken%     what fraction of the effect's own footprint pulls LUMINANCE below the
              background (per-channel would be wrong — see the note in measure()). This is §5.7's darkening budget applied to real content: an
              effect that scores ~0 here is riding entirely on added light and will fade
              out as scenes get brighter, whatever it looks like tonight.
  detail      short-length-scale contrast: luminance minus a blurred copy. This is the
              honest "is the internal texture still there" number — `structure` counts a
              smooth gradient the same as fine detail, and once mistook a wrong vertical
              alpha fade for internal structure.
  structure   coefficient of variation of luminance inside the footprint. This is the
              number for "the filament network turned into a flat blob": internal
              contrast, independent of overall brightness. A collapse from dark to bright
              backgrounds means the effect's depth was being carried by the BACKGROUND
              showing through its gaps, not by the effect itself.
  chroma      mean max-min channel spread inside the footprint (§8.2's chroma).
  |d|         mean max-abs-channel distance from the local background.

THE BACKGROUND IS MEASURED, NOT ASSUMED — from a PLATE, and the reason is a mistake this
script already made. The post chain applies a vignette and a colour grade, so a 0xFFFFFF
clear reads 63 at the corners and cream (238,222,199) at the centre, never 255. The first
version rebuilt the reference as a radial median of the frame itself; that assumption is
broken by the BACKGROUND'S OWN BLOOM, which lifts the frame near the effect, so the mask
counted 39% of the frame as effect where the true footprint is 6%. Every number derived
from it described the instrument. The reference is now `plate_<bg>.png`: the same
background rendered with no fixture, so the background's own bloom cancels exactly and
only the effect's contribution survives.

    python3 scripts/analyze_vfx_matrix.py autotest_output/vfx_matrix/idx35
"""
import glob
import os
import re
import sys

import numpy as np
from PIL import Image

LUMA = np.array([0.2126, 0.7152, 0.0722])


def box_blur(a, r):
    """Separable box blur via cumulative sums — no scipy dependency."""
    pad = np.pad(a, r, mode="edge")
    c = np.cumsum(np.cumsum(pad, axis=0), axis=1)
    c = np.pad(c, ((1, 0), (1, 0)))
    h, w = a.shape
    k = 2 * r + 1
    return (c[k:k + h, k:k + w] - c[0:h, k:k + w]
            - c[k:k + h, 0:w] + c[0:h, 0:w]) / float(k * k)


def measure(path, plate_path):
    img = np.asarray(Image.open(path).convert("RGB")).astype(np.float32)
    bg = np.asarray(Image.open(plate_path).convert("RGB")).astype(np.float32)
    if img.shape != bg.shape:
        return None
    delta = img - bg
    dist = np.abs(delta).max(axis=2)
    foot = dist > 8.0        # everything the effect touches, its own bloom included
    core = dist > 32.0       # the effect's body: structure must not be measured on veil
    if int(foot.sum()) < 200 or int(core.sum()) < 50:
        return None
    # DARKENING IS MEASURED ON LUMA, NOT PER CHANNEL. A per-channel test looks like the
    # obvious one and is wrong here: the colour grade's saturation stage,
    # mix(vec3(luma), col, sat) with sat > 1, is not per-channel monotonic. A PURELY
    # ADDITIVE warm effect over a cool background measured "97.9% of pixels darkened" —
    # R +77, G +9, B -5 — because the added warm light raised luma and the saturation
    # operator then pushed the already-below-luma blue further down. Nothing attenuated
    # anything. Luma is the honest test: additive can only ever add light, so a drop in
    # luminance means real coverage, and the saturation operator is luma-preserving by
    # construction. (Same assumption, per-channel monotonicity, that the §12.1 tone-map
    # candidate also breaks — see the spec.)
    lum_img = img[foot] @ LUMA
    lum_bg = bg[foot] @ LUMA
    darken = float((lum_img - lum_bg < -2.0).mean())
    lumfull = img @ LUMA
    lum = lumfull[core]
    structure = float(lum.std() / max(lum.mean(), 1e-6))
    # DETAIL separates fine internal texture from a smooth gradient, which `structure`
    # cannot: the coefficient of variation is just as high for a broad top-to-bottom fade
    # as for a filament network. That mattered the first time it was used — ENERGY ORB's
    # original 0.466 "structure" was largely its (wrong, cylinder-shaped) vertical alpha
    # fade, so removing that fade looked like a regression when it was a fix. Subtracting
    # a blurred copy leaves only what varies on a short length scale.
    detail = lumfull - box_blur(lumfull, 4)
    detail_cv = float(detail[core].std() / max(lum.mean(), 1e-6))
    chroma = float((img[core].max(axis=1) - img[core].min(axis=1)).mean())
    return dict(cover=100.0 * foot.mean(), body=100.0 * core.mean(),
                darken=100.0 * darken, structure=structure, detail=detail_cv,
                chroma=chroma / 255.0, dist=float(dist[foot].mean()) / 255.0)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else None
    if not d or not os.path.isdir(d):
        print(__doc__)
        return 2
    files = sorted(glob.glob(os.path.join(d, "*.png")))
    if not files:
        print(f"no PNGs in {d}")
        return 1
    order = {"dark": 0, "mid": 1, "white": 2, "warm": 3, "cool": 4}
    rows = []
    for f in files:
        if os.path.basename(f).startswith("plate_"):
            continue
        m = re.match(r"(\w+)_w(\d+)\.png", os.path.basename(f))
        if not m:
            continue
        plate = os.path.join(d, f"plate_{m.group(1)}.png")
        if not os.path.exists(plate):
            print(f"missing background plate {plate} — re-run render_vfx_matrix.sh")
            return 1
        r = measure(f, plate)
        rows.append((int(m.group(2)), order.get(m.group(1), 9), m.group(1), r))
    rows.sort()

    print(f"{'warmup':>6} {'background':10} {'cover%':>7} {'body%':>6} {'darken%':>8} "
          f"{'structure':>10} {'detail':>7} {'chroma':>7} {'|d|':>6}")
    last = None
    for w, _, name, r in rows:
        if last is not None and w != last:
            print()
        last = w
        if r is None:
            print(f"{w:6} {name:10}   (effect not visible at this frame)")
            continue
        print(f"{w:6} {name:10} {r['cover']:6.3f}% {r['body']:5.2f}% {r['darken']:7.1f}% "
              f"{r['structure']:10.3f} {r['detail']:7.3f} {r['chroma']:7.3f} "
              f"{r['dist']:6.3f}")
    print("\ncover% should be roughly background-independent; a big drop means the "
          "silhouette itself is failing.\ndarken% near 0 on bright backgrounds = the "
          "effect only adds light (§5.7).\nstructure collapsing from dark to bright = the "
          "internal depth was the background showing through.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
