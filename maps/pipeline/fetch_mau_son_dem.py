#!/usr/bin/env python3
"""
Acquire or synthesize a projected UTM Zone 48N (EPSG:32648) GeoTIFF DEM for
Mẫu Sơn Massif (Lạng Sơn, Vietnam — 21.825°N..21.860°N, 106.935°E..106.985°E).

Outputs a strict uncompressed, north-up, single-band classic GeoTIFF with
metric pixel scale and NoData = -32768.0 compatible with maps/pipeline/georaster.py.
"""

import argparse
import os
import struct
import sys
import numpy as np

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from georaster import read_dem


def write_geotiff_utm48n(path, dem_f32, origin_x, origin_y, pixel_size_m=15.0, nodata=-32768.0):
    """Write a strict uncompressed north-up single-band GeoTIFF (EPSG:32648)."""
    h, w = dem_f32.shape
    arr = np.asarray(dem_f32, dtype="<f4")
    pixel_bytes = arr.tobytes()

    # GeoKeys: GTModelTypeGeoKey(1024)=1 (Projected), GTRasterTypeGeoKey(1025)=1 (Area),
    # ProjectedCSTypeGeoKey(3072)=32648 (WGS 84 / UTM zone 48N), ProjLinearUnitsGeoKey(3076)=9001 (Metre)
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
        f.write(struct.pack("<I", 0))
        f.write(extras)
        f.write(pixel_bytes)


def synthesize_mau_son_dem(width_px=320, height_px=240, pixel_size_m=15.0):
    """
    Build a high-resolution geomorphological DEM of the Mẫu Sơn Massif (Lạng Sơn):
    - ~4.8 km (E-W) x 3.6 km (N-S) at 15 m/px
    - Đỉnh Mẫu Sơn / Phặt Chỉ (1,541 m) eastern main summit with upper glacial cirque tarn
    - Đỉnh Cha Sơn (1,380 m) western twin peak with wind-scoured slate ridges
    - Đỉnh Công Sơn (1,260 m) northern spur
    - Central Alpine Saddle Plateau (~1,150 m..1,210 m) hosting Thiên Trì Hàn Đàm glacial lake
    - Radial nivational gullies & cascading Khuổi Mới / Bản Khoai stream valleys
    """
    x = np.arange(width_px, dtype=np.float64) * pixel_size_m
    z = np.arange(height_px, dtype=np.float64) * pixel_size_m
    X, Z = np.meshgrid(x, z)

    cx, cz = width_px * pixel_size_m * 0.50, height_px * pixel_size_m * 0.50
    nx = (X - cx) / (width_px * pixel_size_m * 0.44)
    nz = (Z - cz) / (height_px * pixel_size_m * 0.41)

    # Organic mountain-massif plateau footprint (E-W ridge spurs + N-S buttresses)
    ang = np.arctan2(nz, nx)
    ridge_warp = (
        0.13 * np.cos(2.0 * ang + 0.3)
        + 0.08 * np.sin(3.0 * ang - 0.5)
        + 0.05 * np.cos(5.0 * ang + 1.1)
        + 0.04 * np.sin(X * 0.0026 - Z * 0.0022)
    )
    nw_spur = 0.14 * np.exp(-((nx + 0.66) ** 2 / 0.06 + (nz + 0.44) ** 2 / 0.05))
    se_spur = 0.13 * np.exp(-((nx - 0.68) ** 2 / 0.06 + (nz - 0.36) ** 2 / 0.05))
    r_massif = np.hypot(nx * 0.94, nz * 1.08) - ridge_warp - nw_spur - se_spur

    valid = r_massif <= 0.92
    edge_dist = np.maximum(0.0, 0.92 - r_massif)

    # Base alpine massif highland platform (~580m foothills rising to ~1160m central saddle)
    elev = 580.0 + 560.0 * np.tanh(edge_dist * 3.0)

    # 1. Đỉnh Mẫu Sơn / Phặt Chỉ (Eastern Main Peak: 1,541 m, with upper cirque tarn bowl & WSW spillway)
    ms_x, ms_z = cx + 1020.0, cz - 180.0
    dx_ms, dz_ms = X - ms_x, Z - ms_z
    r_ms = np.hypot(dx_ms, dz_ms)
    ang_ms = np.arctan2(dz_ms, dx_ms)
    ms_rim_r = 420.0 + 44.0 * np.cos(3.0 * ang_ms + 0.5) + 26.0 * np.sin(5.0 * ang_ms)
    ms_gully = np.sin(ang_ms * 10.0 + np.sin(r_ms * 0.007) * 1.2) * 0.5 + np.cos(ang_ms * 15.0 - r_ms * 0.005) * 0.3
    ms_flank_gully = 36.0 * ms_gully * np.exp(-((np.maximum(0.0, r_ms - ms_rim_r * 0.90)) / 360.0) ** 2)
    ms_peak = 415.0 * np.exp(-((np.maximum(0.0, r_ms - ms_rim_r * 0.66)) / 480.0) ** 1.58)
    # Upper glacial cirque bowl on the WSW shoulder of Phặt Chỉ
    cirque_x, cirque_z = ms_x - 190.0, ms_z + 110.0
    r_cirque = np.hypot(X - cirque_x, Z - cirque_z)
    cirque_bowl = 135.0 * np.exp(-((r_cirque / 215.0) ** 2.4))
    breach_cirque = np.exp(-((ang_ms - 2.65) / 0.36) ** 2) * np.exp(-((r_ms - ms_rim_r) / 210.0) ** 2)
    elev += ms_peak + ms_flank_gully - cirque_bowl - 95.0 * breach_cirque

    # 2. Đỉnh Cha Sơn (Western Twin Peak: ~1,380 m)
    cs_x, cs_z = cx - 960.0, cz + 120.0
    dx_cs, dz_cs = X - cs_x, Z - cs_z
    r_cs = np.hypot(dx_cs, dz_cs)
    ang_cs = np.arctan2(dz_cs, dx_cs)
    cs_gully = 26.0 * np.sin(ang_cs * 8.0 + r_cs * 0.006) * np.exp(-((np.maximum(0.0, r_cs - 280.0)) / 320.0) ** 2)
    cs_peak = 275.0 * np.exp(-((r_cs / 450.0) ** 1.65))
    elev += cs_peak + cs_gully

    # 3. Đỉnh Công Sơn (Northern Ridge Spur: ~1,260 m)
    cn_x, cn_z = cx - 180.0, cz - 720.0
    r_cn = np.hypot(X - cn_x, Z - cn_z)
    elev += 175.0 * np.exp(-((r_cn / 360.0) ** 1.75))

    # 4. Central Glacial Tarn Depression (Thiên Trì Hàn Đàm) adjacent to the saddle combat plateau
    lake_x, lake_z = cx + 240.0, cz + 180.0
    r_lake = np.hypot((X - lake_x) * 0.92, (Z - lake_z) * 1.10)
    tarn_bowl = 92.0 * np.exp(-((r_lake / 310.0) ** 2.5))
    elev -= tarn_bowl

    # 5. Multi-scale alpine ridges & metamorphic slate strata ribs
    ridge_noise = (
        28.0 * np.sin(X * 0.0042 + np.cos(Z * 0.0036) * 1.3)
        + 15.0 * np.cos(Z * 0.0078 - X * 0.0058)
        + 7.5 * np.sin(X * 0.016 + Z * 0.014)
    )
    elev += ridge_noise * np.clip(edge_dist * 3.0, 0.18, 1.0)

    # Calibrate to Mẫu Sơn real elevation envelope (580.0 m .. 1,541.0 m)
    elev_valid = elev[valid]
    e_lo, e_hi = elev_valid.min(), elev_valid.max()
    elev = 580.0 + (elev - e_lo) / max(e_hi - e_lo, 1e-6) * (1541.0 - 580.0)
    elev[~valid] = -32768.0
    return elev.astype(np.float32)


def main():
    parser = argparse.ArgumentParser(description="Acquire/synthesize Mẫu Sơn UTM 48N GeoTIFF DEM")
    parser.add_argument("--input-tif", default=None, help="Optional existing UTM 48N GeoTIFF to validate/copy")
    parser.add_argument("--out", default=os.path.join(ROOT_DIR, "assets/maps/mau_son/DEM_MauSon_UTM48N.tif"),
                        help="Output GeoTIFF path")
    parser.add_argument("--pixel-size", type=float, default=15.0, help="Metric pixel size (m)")
    args = parser.parse_args()

    if args.input_tif and os.path.exists(args.input_tif):
        dem, valid, meta = read_dem(args.input_tif)
        print(f"[MauSon DEM] Validated input GeoTIFF: {meta['shape']} EPSG:{meta['crs_epsg']} px={meta['pixel_size_m']}")
        if os.path.abspath(args.input_tif) != os.path.abspath(args.out):
            write_geotiff_utm48n(args.out, dem, meta["origin"][0], meta["origin"][1], meta["pixel_size_m"][0])
    else:
        dem = synthesize_mau_son_dem(width_px=320, height_px=240, pixel_size_m=args.pixel_size)
        # Mẫu Sơn (Lạng Sơn) UTM 48N approximate NW corner (Eastings ~700000 m, Northings ~2418500 m)
        write_geotiff_utm48n(args.out, dem, origin_x=700000.0, origin_y=2418500.0, pixel_size_m=args.pixel_size)
        _, valid_check, meta_check = read_dem(args.out)
        print(f"[MauSon DEM] Synthesized & verified {args.out}: "
              f"shape={meta_check['shape']}, EPSG:{meta_check['crs_epsg']}, "
              f"valid={int(valid_check.sum())} px, range=[{dem[valid_check].min():.1f}m, {dem[valid_check].max():.1f}m]")


if __name__ == "__main__":
    main()
