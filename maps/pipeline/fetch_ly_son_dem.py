#!/usr/bin/env python3
"""
Acquire or synthesize a projected UTM Zone 49N (EPSG:32649) GeoTIFF DEM for
Lý Sơn Island (Quảng Ngãi, Vietnam — 15.365°N..15.398°N, 109.080°E..109.150°E).

Outputs a strict uncompressed, north-up, single-band classic GeoTIFF with
metric pixel scale and NoData = -32768.0 compatible with maps/pipeline/georaster.py.
"""

import argparse
import os
import struct
import sys
import urllib.request
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from georaster import read_dem


def write_geotiff_utm49n(path, dem_f32, origin_x, origin_y, pixel_size_m=15.0, nodata=-32768.0):
    """Write a strict uncompressed north-up single-band GeoTIFF (EPSG:32649)."""
    h, w = dem_f32.shape
    arr = np.asarray(dem_f32, dtype="<f4")
    pixel_bytes = arr.tobytes()

    # GeoKeys: GTModelTypeGeoKey(1024)=1 (Projected), GTRasterTypeGeoKey(1025)=1 (Area),
    # ProjectedCSTypeGeoKey(3072)=32649 (WGS 84 / UTM zone 49N), ProjLinearUnitsGeoKey(3076)=9001 (Metre)
    geo_keys = [
        1, 1, 0, 4,
        1024, 0, 1, 1,
        1025, 0, 1, 1,
        3072, 0, 1, 32649,
        3076, 0, 1, 9001,
    ]
    pixel_scale = [float(pixel_size_m), float(pixel_size_m), 0.0]
    tie_point = [0.0, 0.0, 0.0, float(origin_x), float(origin_y), 0.0]
    nodata_ascii = f"{nodata:g}\0".encode("ascii")

    # Tag definitions: (tag, type, count, raw_bytes_or_None)
    # Types: 2=ASCII, 3=SHORT, 4=LONG, 12=DOUBLE
    entries_spec = [
        (256, 4, 1, struct.pack("<I", w)),
        (257, 4, 1, struct.pack("<I", h)),
        (258, 3, 1, struct.pack("<H", 32) + b"\x00\x00"),
        (259, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),      # Compression = 1 (None)
        (262, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),      # Photometric = MinIsBlack
        (273, 4, 1, None),                                    # StripOffsets (patched)
        (274, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),      # Orientation = 1 (Top-Left)
        (277, 3, 1, struct.pack("<H", 1) + b"\x00\x00"),      # SamplesPerPixel = 1
        (278, 4, 1, struct.pack("<I", h)),                    # RowsPerStrip = h
        (279, 4, 1, struct.pack("<I", len(pixel_bytes))),     # StripByteCounts
        (339, 3, 1, struct.pack("<H", 3) + b"\x00\x00"),      # SampleFormat = 3 (IEEE float)
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
        f.write(struct.pack("<I", 0))  # Next IFD = 0
        f.write(extras)
        f.write(pixel_bytes)


def synthesize_ly_son_dem(width_px=340, height_px=240, pixel_size_m=15.0):
    """
    Build a high-resolution geomorphological DEM of Lý Sơn Island (Lớn Island):
    - ~5.1 km (E-W) x 3.6 km (N-S) at 15 m/px
    - Núi Thới Lới (169.3 m) eastern caldera with breached SSW magma spillway
    - Núi Giếng Tiền (86.4 m) western volcanic crater bowl with SW spillway
    - Hòn Tai / Hòn Sỏi southern volcanic spur (68 m)
    - Hang Câu NE basalt cliff & Cổng Tò Vò NW coastal basalt headland
    - Central saddle combat plain (~18 m - 26 m)
    """
    x = np.arange(width_px, dtype=np.float64) * pixel_size_m
    z = np.arange(height_px, dtype=np.float64) * pixel_size_m
    X, Z = np.meshgrid(x, z)

    cx, cz = width_px * pixel_size_m * 0.50, height_px * pixel_size_m * 0.50
    nx = (X - cx) / (width_px * pixel_size_m * 0.44)
    nz = (Z - cz) / (height_px * pixel_size_m * 0.40)

    # Organic volcanic island coastline (elongated E-W crescent with NW Cổng Tò Vò spit & NE Hang Câu headland)
    ang = np.arctan2(nz, nx)
    coast_warp = (
        0.14 * np.cos(2.0 * ang - 0.5)
        + 0.09 * np.sin(3.0 * ang + 0.8)
        + 0.06 * np.cos(5.0 * ang - 1.2)
        + 0.05 * np.sin(X * 0.0028 + Z * 0.0021)
    )
    # Cổng Tò Vò NW promontory
    tovo_lobe = 0.16 * np.exp(-((nx + 0.68) ** 2 / 0.06 + (nz + 0.42) ** 2 / 0.04))
    # Hang Câu NE cliff lobe
    hangcau_lobe = 0.14 * np.exp(-((nx - 0.72) ** 2 / 0.05 + (nz + 0.35) ** 2 / 0.05))
    r_island = np.hypot(nx * 0.92, nz * 1.12) - coast_warp - tovo_lobe - hangcau_lobe

    valid = r_island <= 0.92
    coast_dist = np.maximum(0.0, 0.92 - r_island)

    # Base island shield platform (gentle coastal bench rising to 16-24m interior saddle)
    elev = 4.0 + 22.0 * np.tanh(coast_dist * 3.2)

    # 1. Núi Thới Lới (Eastern Major Caldera: 169.3 m rim, radial pyroclastic gullies, 108 m inner crater floor)
    tl_x, tl_z = cx + 1080.0, cz - 140.0
    dx_tl, dz_tl = X - tl_x, Z - tl_z
    r_tl = np.hypot(dx_tl, dz_tl)
    ang_tl = np.arctan2(dz_tl, dx_tl)
    tl_rim_r = 450.0 + 48.0 * np.cos(3.0 * ang_tl + 0.4) + 28.0 * np.sin(5.0 * ang_tl)
    # Radial pyroclastic erosion gullies on the outer flank
    tl_gully = np.sin(ang_tl * 11.0 + np.sin(r_tl * 0.008) * 1.4) * 0.5 + np.cos(ang_tl * 17.0 - r_tl * 0.005) * 0.3
    tl_flank_gully = 11.5 * tl_gully * np.exp(-((np.maximum(0.0, r_tl - tl_rim_r * 0.92)) / 360.0) ** 2)
    # Outer cone skirt (steeper, craggier volcanic silhouette)
    tl_cone = 156.0 * np.exp(-((np.maximum(0.0, r_tl - tl_rim_r * 0.72)) / 460.0) ** 1.62)
    # Caldera bowl depression inside rim
    tl_crater = 76.0 * np.exp(-((r_tl / (tl_rim_r * 0.62)) ** 2.6))
    # Breached SSW magma spillway (angle ~ 2.25 rad, facing central saddle & sea)
    breach_tl = np.exp(-((ang_tl - 2.25) / 0.34) ** 2) * np.exp(-((r_tl - tl_rim_r) / 205.0) ** 2)
    elev += tl_cone + tl_flank_gully - tl_crater - 52.0 * breach_tl

    # 2. Núi Giếng Tiền (Western Volcanic Cinder Cone: ~94 m rim, ~52 m floor)
    gt_x, gt_z = cx - 940.0, cz + 40.0
    dx_gt, dz_gt = X - gt_x, Z - gt_z
    r_gt = np.hypot(dx_gt, dz_gt)
    ang_gt = np.arctan2(dz_gt, dx_gt)
    gt_rim_r = 335.0 + 34.0 * np.sin(3.0 * ang_gt - 0.6)
    gt_gully = 6.5 * np.sin(ang_gt * 9.0 + r_gt * 0.006) * np.exp(-((np.maximum(0.0, r_gt - gt_rim_r * 0.90)) / 280.0) ** 2)
    gt_cone = 76.0 * np.exp(-((np.maximum(0.0, r_gt - gt_rim_r * 0.70)) / 375.0) ** 1.60)
    gt_crater = 38.0 * np.exp(-((r_gt / (gt_rim_r * 0.62)) ** 2.4))
    breach_gt = np.exp(-((ang_gt - 1.35) / 0.40) ** 2) * np.exp(-((r_gt - gt_rim_r) / 175.0) ** 2)
    elev += gt_cone + gt_gully - gt_crater - 28.0 * breach_gt

    # 3. Hòn Tai / Southern Volcanic Spur (~66 m)
    ht_x, ht_z = cx + 220.0, cz + 680.0
    r_ht = np.hypot(X - ht_x, Z - ht_z)
    elev += 44.0 * np.exp(-((r_ht / 320.0) ** 1.85))

    # 4. Hang Câu NE Basalt Cliff Wall (steep outer slope)
    hc_x, hc_z = cx + 1480.0, cz - 520.0
    r_hc = np.hypot(X - hc_x, Z - hc_z)
    elev += 36.0 * np.exp(-((r_hc / 380.0) ** 2.0))

    # 5. Multi-scale volcanic ridges & basalt lava flow ribs
    ridge_noise = (
        6.5 * np.sin(X * 0.0045 + np.cos(Z * 0.0038) * 1.4)
        + 3.2 * np.cos(Z * 0.0082 - X * 0.0061)
        + 1.6 * np.sin(X * 0.017 + Z * 0.015)
    )
    elev += ridge_noise * np.clip(coast_dist * 3.0, 0.15, 1.0)

    # Calibrate to Lý Sơn real elevation envelope (0.5 m .. 169.3 m)
    elev_valid = elev[valid]
    e_lo, e_hi = elev_valid.min(), elev_valid.max()
    elev = 0.8 + (elev - e_lo) / max(e_hi - e_lo, 1e-6) * (169.3 - 0.8)
    elev[~valid] = -32768.0
    return elev.astype(np.float32)


def main():
    parser = argparse.ArgumentParser(description="Acquire/synthesize Lý Sơn UTM 49N GeoTIFF DEM")
    parser.add_argument("--input-tif", default=None, help="Optional existing UTM 49N GeoTIFF to validate/copy")
    parser.add_argument("--out", default=os.path.join(ROOT_DIR, "assets/maps/ly_son/DEM_LySon_UTM49N.tif"),
                        help="Output GeoTIFF path")
    parser.add_argument("--pixel-size", type=float, default=15.0, help="Metric pixel size (m)")
    args = parser.parse_args()

    if args.input_tif and os.path.exists(args.input_tif):
        dem, valid, meta = read_dem(args.input_tif)
        print(f"[LySon DEM] Validated input GeoTIFF: {meta['shape']} EPSG:{meta['crs_epsg']} px={meta['pixel_size_m']}")
        if os.path.abspath(args.input_tif) != os.path.abspath(args.out):
            write_geotiff_utm49n(args.out, dem, meta["origin"][0], meta["origin"][1], meta["pixel_size_m"][0])
    else:
        dem = synthesize_ly_son_dem(width_px=340, height_px=240, pixel_size_m=args.pixel_size)
        # Lý Sơn Island UTM 49N approximate NW corner (Eastings ~294000 m, Northings ~1704500 m)
        write_geotiff_utm49n(args.out, dem, origin_x=294000.0, origin_y=1704500.0, pixel_size_m=args.pixel_size)
        _, valid_check, meta_check = read_dem(args.out)
        print(f"[LySon DEM] Synthesized & verified {args.out}: "
              f"shape={meta_check['shape']}, EPSG:{meta_check['crs_epsg']}, "
              f"valid={int(valid_check.sum())} px, range=[{dem[valid_check].min():.1f}m, {dem[valid_check].max():.1f}m]")


if __name__ == "__main__":
    main()
