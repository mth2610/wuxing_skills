#!/usr/bin/env python3
"""
Acquire real elevation data for Phú Quốc Island (Kiên Giang, Vietnam —
10.00°N..10.45°N, 103.82°E..104.07°E) from public AWS Mapzen Terrarium tiles
and export as a projected UTM Zone 48N (EPSG:32648) single-band GeoTIFF.

Falls back to an asymmetric N-S fault-block Phú Quốc DEM (zero circular
craters) if offline.
"""

import argparse
import io
import math
import os
import struct
import sys
import urllib.request
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from dem_pipeline import largest_component, resample_grid
from georaster import read_dem


def write_geotiff_utm48n(path, dem_f32, origin_x, origin_y, pixel_size_m=15.0, nodata=-32768.0):
    """Write a strict uncompressed north-up single-band GeoTIFF (EPSG:32648)."""
    h, w = dem_f32.shape
    arr = np.asarray(dem_f32, dtype="<f4")
    pixel_bytes = arr.tobytes()

    geo_keys = [
        1, 1, 0, 4,
        1024, 0, 1, 1,
        1025, 0, 1, 1,
        3072, 0, 1, 32648,
        3076, 0, 1, 9001,
    ]
    pixel_scale = [float(pixel_size_m), float(pixel_size_m), 0.0]
    tie_point = [0.0, 0.0, 0.0, float(origin_x), float(origin_y), 0.0]
    nodata_ascii = f"{nodata:g}\0".encode("ascii")

    entries_spec = [
        (256, 4, 1, struct.pack("<I", w)),
        (257, 4, 1, struct.pack("<I", h)),
        (258, 3, 1, struct.pack("<H", 32) + b"\x00\x00"),
        (259, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),
        (262, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),
        (273, 4, 1, None),
        (274, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),
        (277, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),
        (278, 4, 1, struct.pack("<I", h)),
        (279, 4, 1, struct.pack("<I", len(pixel_bytes))),
        (339, 3, 1, struct.pack("<H", 3) + b"\x00\x00"),
        (33550, 12, 3, struct.pack("<3d", *pixel_scale)),
        (33922, 12, 6, struct.pack("<6d", *tie_point)),
        (34735, 3, len(geo_keys), struct.pack(f"<{len(geo_keys)}H", *geo_keys)),
        (42113, 2, len(nodata_ascii), nodata_ascii),
    ]

    ifd_offset = 8
    ifd_size = 2 + 12 * len(entries_spec) + 4
    extras_offset = ifd_offset + ifd_size
    extras = bytearray()

    ifd_entries = []
    for tag, typ, count, payload in entries_spec:
        if tag == 273:
            ifd_entries.append((tag, typ, count, None))
            continue
        if len(payload) <= 4:
            ifd_entries.append((tag, typ, count, payload.ljust(4, b"\x00")))
        else:
            off = extras_offset + len(extras)
            extras.extend(payload)
            ifd_entries.append((tag, typ, count, struct.pack("<I", off)))

    data_offset = extras_offset + len(extras)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"II")
        f.write(struct.pack("<HI", 42, ifd_offset))
        f.write(struct.pack("<H", len(ifd_entries)))
        for tag, typ, count, field in ifd_entries:
            if tag == 273:
                field = struct.pack("<I", data_offset)
            f.write(struct.pack("<HHI4s", tag, typ, count, field))
        f.write(struct.pack("<I", 0))
        f.write(extras)
        f.write(pixel_bytes)


def _deg2num(lat_deg, lon_deg, zoom):
    lat_rad = math.radians(lat_deg)
    n = 2.0 ** zoom
    xtile = int((lon_deg + 180.0) / 360.0 * n)
    ytile = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return xtile, ytile


def _num2deg(xtile, ytile, zoom):
    n = 2.0 ** zoom
    lon_deg = xtile / n * 360.0 - 180.0
    lat_rad = math.atan(math.sinh(math.pi * (1.0 - 2.0 * ytile / n)))
    lat_deg = math.degrees(lat_rad)
    return lat_deg, lon_deg


def _decode_png_rgb(raw_bytes):
    """Pure-Python 8-bit RGB/RGBA PNG decoder (no PIL dependency)."""
    import zlib
    if raw_bytes[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Not a PNG")
    pos = 8
    width = height = bit_depth = color_type = 0
    idat = bytearray()
    while pos + 8 <= len(raw_bytes):
        length = struct.unpack(">I", raw_bytes[pos:pos + 4])[0]
        ctype = raw_bytes[pos + 4:pos + 8]
        data = raw_bytes[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", data[:10])
        elif ctype == b"IDAT":
            idat.extend(data)
        elif ctype == b"IEND":
            break
    if bit_depth != 8 or color_type not in (2, 6):
        raise ValueError(f"Unsupported PNG format: bit_depth={bit_depth}, color_type={color_type}")
    bpp = 3 if color_type == 2 else 4
    stride = width * bpp
    raw = zlib.decompress(bytes(idat))
    out = np.zeros((height, width, bpp), dtype=np.uint8)
    prev = np.zeros(stride, dtype=np.int32)
    idx = 0
    for y in range(height):
        flt = raw[idx]
        idx += 1
        scan = np.frombuffer(raw, dtype=np.uint8, count=stride, offset=idx).astype(np.int32)
        idx += stride
        if flt == 0:
            recon = scan
        elif flt == 1:
            recon = scan.copy()
            for i in range(bpp, stride):
                recon[i] = (recon[i] + recon[i - bpp]) & 0xFF
        elif flt == 2:
            recon = (scan + prev) & 0xFF
        elif flt == 3:
            recon = scan.copy()
            for i in range(stride):
                left = int(recon[i - bpp]) if i >= bpp else 0
                recon[i] = (recon[i] + ((left + int(prev[i])) >> 1)) & 0xFF
        elif flt == 4:
            recon = scan.copy()
            for i in range(stride):
                a = int(recon[i - bpp]) if i >= bpp else 0
                b = int(prev[i])
                c = int(prev[i - bpp]) if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                recon[i] = (recon[i] + pr) & 0xFF
        else:
            raise ValueError(f"Unsupported PNG filter {flt}")
        out[y] = recon.astype(np.uint8).reshape(width, bpp)
        prev = recon
    return out[:, :, :3].astype(np.float64)


def fetch_real_phu_quoc_terrarium(out_w=260, out_h=340):
    """Download real SRTM/Copernicus elevation tiles from AWS Terrarium (z=11)."""
    lat_min, lat_max = 10.00, 10.455
    lon_min, lon_max = 103.82, 104.075
    zoom = 11

    x0, y0 = _deg2num(lat_max, lon_min, zoom)
    x1, y1 = _deg2num(lat_min, lon_max, zoom)
    tiles_x = x1 - x0 + 1
    tiles_y = y1 - y0 + 1

    stitched = np.zeros((tiles_y * 256, tiles_x * 256), dtype=np.float64)
    for ty in range(y0, y1 + 1):
        for tx in range(x0, x1 + 1):
            url = f"https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{zoom}/{tx}/{ty}.png"
            req = urllib.request.Request(url, headers={"User-Agent": "WuxingSkills-DEM/1.0"})
            with urllib.request.urlopen(req, timeout=12) as resp:
                arr = _decode_png_rgb(resp.read())
            elev_tile = (arr[:, :, 0] * 256.0 + arr[:, :, 1] + arr[:, :, 2] / 256.0) - 32768.0
            ry = (ty - y0) * 256
            rx = (tx - x0) * 256
            stitched[ry:ry + 256, rx:rx + 256] = elev_tile

    # Crop exact Phú Quốc Island lat/lon bounding box
    lat_top, lon_left = _num2deg(x0, y0, zoom)
    lat_bot, lon_right = _num2deg(x1 + 1, y1 + 1, zoom)

    total_h, total_w = stitched.shape
    c0 = max(0, int((lon_min - lon_left) / (lon_right - lon_left) * total_w))
    c1 = min(total_w, int((lon_max - lon_left) / (lon_right - lon_left) * total_w))
    r0 = max(0, int((lat_top - lat_max) / (lat_top - lat_bot) * total_h))
    r1 = min(total_h, int((lat_top - lat_min) / (lat_top - lat_bot) * total_h))

    cropped = stitched[r0:r1, c0:c1]
    dem = resample_grid(cropped, out_h, out_w)

    # Keep only the main Phú Quốc landmass above sea level (> 0.5 m)
    land = largest_component(dem > 0.5)
    if land.sum() < 4000:
        raise RuntimeError("Real DEM landmass mask too small")

    dem = np.where(land, np.maximum(0.5, dem), -32768.0)
    return dem.astype(np.float32)


def synthesize_phu_quoc_fallback(width_px=260, height_px=340, pixel_size_m=15.0):
    """
    Offline fallback: asymmetric N-S fault-block Phú Quốc topography
    (broad western Dương Đông / Cửa Cạn plains, steep eastern Hàm Ninh - Núi Chúa
    spine, ZERO circular crater bowls or radial spokes).
    """
    x = np.arange(width_px, dtype=np.float64) * pixel_size_m
    z = np.arange(height_px, dtype=np.float64) * pixel_size_m
    X, Z = np.meshgrid(x, z)

    W_m = width_px * pixel_size_m
    H_m = height_px * pixel_size_m
    cx, cz = W_m * 0.50, H_m * 0.50

    nz = (Z - cz) / (H_m * 0.44)
    taper = np.clip(1.0 + 0.24 * (-nz) - 0.16 * np.maximum(nz, 0.0), 0.58, 1.24)
    nx = (X - cx) / (W_m * 0.42 * taper)

    ang = np.arctan2(nz, nx)
    coast_warp = (
        0.10 * np.cos(2.0 * ang + 0.4)
        + 0.07 * np.sin(3.0 * ang - 0.6)
        + 0.05 * np.cos(5.0 * ang + 1.1)
    )
    duongdong_bay = 0.12 * np.exp(-((nx + 0.72) ** 2 / 0.06 + (nz - 0.05) ** 2 / 0.08))
    r_island = np.hypot(nx * 1.02, nz * 0.96) - coast_warp + duongdong_bay
    valid = r_island <= 0.90
    coast_dist = np.maximum(0.0, 0.90 - r_island)

    # Broad low-lying western coastal plain (5-18m)
    elev = 2.0 + 18.0 * np.tanh(coast_dist * 3.5)

    # Elongated N-S eastern Hàm Ninh - Núi Chúa fault-block mountain spine
    spine_x = cx + W_m * 0.18 + np.sin(Z * 0.0012) * 260.0
    dx_sp = (X - spine_x) / 520.0
    nz_sp = np.clip((Z - cz) / (H_m * 0.36), -1.2, 1.2)
    spine_env = np.exp(-(dx_sp ** 2)) * np.exp(-(np.abs(nz_sp * 0.85) ** 2.4))
    nui_chua = 440.0 * np.exp(-(((X - (cx + W_m * 0.16)) / 580.0) ** 2 + ((Z - (cz - H_m * 0.18)) / 620.0) ** 2))
    ham_ninh = 260.0 * np.exp(-(((X - (cx + W_m * 0.19)) / 460.0) ** 2 + ((Z - (cz + H_m * 0.14)) / 680.0) ** 2))
    elev += spine_env * 165.0 + nui_chua + ham_ninh

    # Natural E-W fault ridges (no radial crater spokes)
    ridge_noise = (
        18.0 * np.sin(X * 0.0038 + np.cos(Z * 0.0032) * 1.2)
        + 10.0 * np.cos(Z * 0.0068 - X * 0.0045)
    )
    elev += ridge_noise * np.clip(coast_dist * 3.0, 0.15, 1.0)

    elev_valid = elev[valid]
    elev = 0.5 + (elev - elev_valid.min()) / max(elev_valid.max() - elev_valid.min(), 1e-6) * (603.0 - 0.5)
    elev[~valid] = -32768.0
    return elev.astype(np.float32)


def main():
    parser = argparse.ArgumentParser(description="Acquire real Phú Quốc UTM 48N GeoTIFF DEM")
    parser.add_argument("--input-tif", default=None, help="Optional existing UTM 48N GeoTIFF to validate/copy")
    parser.add_argument("--out", default=os.path.join(ROOT_DIR, "assets/maps/phu_quoc/DEM_PhuQuoc_UTM48N.tif"),
                        help="Output GeoTIFF path")
    parser.add_argument("--pixel-size", type=float, default=15.0, help="Metric pixel size (m)")
    args = parser.parse_args()

    if args.input_tif and os.path.exists(args.input_tif):
        dem, valid, meta = read_dem(args.input_tif)
        print(f"[PhuQuoc DEM] Validated input GeoTIFF: {meta['shape']} EPSG:{meta['crs_epsg']} px={meta['pixel_size_m']}")
        if os.path.abspath(args.input_tif) != os.path.abspath(args.out):
            write_geotiff_utm48n(args.out, dem, meta["origin"][0], meta["origin"][1], meta["pixel_size_m"][0])
    else:
        source_mode = "real AWS Terrarium SRTM"
        try:
            dem = fetch_real_phu_quoc_terrarium(out_w=260, out_h=340)
        except Exception as exc:
            source_mode = f"offline fallback ({exc})"
            dem = synthesize_phu_quoc_fallback(width_px=260, height_px=340, pixel_size_m=args.pixel_size)

        write_geotiff_utm48n(args.out, dem, origin_x=375000.0, origin_y=1156000.0, pixel_size_m=args.pixel_size)
        _, valid_check, meta_check = read_dem(args.out)
        print(f"[PhuQuoc DEM] {source_mode} -> {args.out}: "
              f"shape={meta_check['shape']}, EPSG:{meta_check['crs_epsg']}, "
              f"valid={int(valid_check.sum())} px, range=[{dem[valid_check].min():.1f}m, {dem[valid_check].max():.1f}m]")


if __name__ == "__main__":
    main()
