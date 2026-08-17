#!/usr/bin/env python3
"""Compare two headless captures pixel by pixel.

Gate 2 of the hue-preserving tone-map approval
(third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md 12.1). The candidate curve is
provably the identity below exposed peak 1.0 (rlvk scenario `tonemap_shoulder`), so
*every* pixel that differs between the two captures is a pixel that was already above
the shoulder. That makes "what fraction of pixels changed" a direct measurement of the
approval surface -- no material classification needed.

Uses numpy + Pillow when they are importable and falls back to a pure-stdlib PNG reader
when they are not. On this machine that distinction is real: /usr/bin/python3 has both,
while the /usr/local/bin/python3 that is first on PATH does not. The fallback is what
makes the script run under whichever interpreter you happen to invoke; the fast path is
what makes it finish in well under a second on a 1280x720 frame instead of tens of
seconds of per-pixel Python.

    python3 scripts/diff_captures.py A.png B.png [--out diff.png] [--label "A/B"]
"""
import sys
import zlib
import struct

try:                      # fast path
    import numpy as _np
    from PIL import Image as _Image
except ImportError:       # portable path
    _np = None


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, w, h, chans = 8, [], 0, 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8:
                raise ValueError(f"{path}: only 8-bit PNGs are supported (got {depth})")
            if interlace:
                raise ValueError(f"{path}: interlaced PNGs are not supported")
            chans = {0: 1, 2: 3, 4: 2, 6: 4}.get(color)
            if chans is None:
                raise ValueError(f"{path}: palette PNGs are not supported")
        elif ctype == b"IDAT":
            idat.append(body)
        elif ctype == b"IEND":
            break
        pos += 12 + length

    raw = zlib.decompress(b"".join(idat))
    stride = w * chans
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(chans, stride):
                line[i] = (line[i] + line[i - chans]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                left = line[i - chans] if i >= chans else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - chans] if i >= chans else 0
                b = prev[i]
                c = prev[i - chans] if i >= chans else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise ValueError(f"{path}: bad filter type {ft}")
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return w, h, chans, out


def write_gray_png(path, w, h, pixels):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(pixels[y * w:(y + 1) * w])
    body = zlib.compress(bytes(raw), 9)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", body))
        f.write(chunk(b"IEND", b""))


def compare_numpy(path_a, path_b, out_path, label):
    a = _np.asarray(_Image.open(path_a).convert("RGB"), dtype=_np.int16)
    b = _np.asarray(_Image.open(path_b).convert("RGB"), dtype=_np.int16)
    if a.shape != b.shape:
        print(f"FAIL {label}: size mismatch {a.shape[1]}x{a.shape[0]} vs {b.shape[1]}x{b.shape[0]}")
        return 1
    d = _np.abs(a - b).max(axis=2)
    total = d.size
    pct = lambda n: 100.0 * n / total
    print(f"{label}: {a.shape[1]}x{a.shape[0]}  changed >2/255: {pct(int((d > 2).sum())):6.3f}%   "
          f">8/255: {pct(int((d > 8).sum())):6.3f}%   >32/255: {pct(int((d > 32).sum())):6.3f}%   "
          f"max {int(d.max())}/255   mean {d.mean():.3f}/255")
    if out_path:
        _Image.fromarray(_np.clip(d * 8, 0, 255).astype(_np.uint8)).save(out_path)
        print(f"  difference map (x8 gain) -> {out_path}")
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    out_path = None
    label = "A/B"
    for i, a in enumerate(opts):
        if a == "--out" and i + 1 < len(opts):
            out_path = opts[i + 1]
        if a == "--label" and i + 1 < len(opts):
            label = opts[i + 1]

    if _np is not None:
        return compare_numpy(args[0], args[1], out_path, label)

    w0, h0, c0, a = read_png(args[0])
    w1, h1, c1, b = read_png(args[1])
    if (w0, h0) != (w1, h1):
        print(f"FAIL {label}: size mismatch {w0}x{h0} vs {w1}x{h1}")
        return 1

    # Grayscale inputs are legitimate here: the difference maps this script writes are
    # themselves grayscale, and being able to re-diff one is how you check the writer.
    nc = min(3, c0, c1)
    total = w0 * h0
    over2 = over8 = over32 = 0
    worst = 0
    dsum = 0
    diff = bytearray(total)
    for i in range(total):
        pa, pb = i * c0, i * c1
        d = 0
        for k in range(nc):
            v = abs(a[pa + k] - b[pb + k])
            if v > d:
                d = v
        diff[i] = min(255, d * 8)
        dsum += d
        if d > worst:
            worst = d
        if d > 2:
            over2 += 1
        if d > 8:
            over8 += 1
        if d > 32:
            over32 += 1

    pct = lambda n: 100.0 * n / total
    print(f"{label}: {w0}x{h0}  changed >2/255: {pct(over2):6.3f}%   "
          f">8/255: {pct(over8):6.3f}%   >32/255: {pct(over32):6.3f}%   "
          f"max {worst}/255   mean {dsum/total:.3f}/255")
    if out_path:
        write_gray_png(out_path, w0, h0, diff)
        print(f"  difference map (x8 gain) -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
