#!/usr/bin/env python3
"""
Export all VFX flipbooks and particle textures from Unreal Engine Starter Content (.uasset) to PNG.
"""

import os
import subprocess

TARGETS = [
    ("T_Smoke_SubUV", "png"),
    ("T_Explosion_SubUV", "png"),
    ("T_Dust_Particle_D", "png"),
    ("T_Fire_Tiled_D", "png"),
    ("T_Smoke_Tiled_D", "png"),
    ("T_Burst_M", "png"),
    ("T_Fire_SubUV", "thumb"),
    ("T_Spark_Core", "thumb"),
]

SRC_DIR = "unreal-engine-starter-content-main/Samples/StarterContent/Content/StarterContent/Textures"
OUT_DIR = "assets/vfx_starter_content"

def export_direct_png(name, src_path, out_path):
    with open(src_path, "rb") as fp:
        data = fp.read()
    png_start = data.find(b"\x89PNG")
    if png_start == -1:
        return False
    iend_pos = data.find(b"IEND", png_start)
    if iend_pos == -1:
        return False
    png_data = data[png_start : iend_pos + 8]
    with open(out_path, "wb") as fp:
        fp.write(png_data)
    return True

def export_thumb_png(name, src_path, out_path):
    with open(src_path, "rb") as fp:
        data = fp.read()
    jpg_start = data.find(b"\xff\xd8\xff")
    if jpg_start == -1:
        return False
    jpg_end = data.find(b"\xff\xd9", jpg_start) + 2
    tmp_jpg = f"/tmp/{name}_thumb.jpg"
    with open(tmp_jpg, "wb") as fp:
        fp.write(data[jpg_start:jpg_end])
    subprocess.run(["sips", "-s", "format", "png", tmp_jpg, "--out", out_path],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if os.path.exists(tmp_jpg):
        os.remove(tmp_jpg)
    return True

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Exporting all Starter Content VFX textures from {SRC_DIR} to {OUT_DIR}/...\n")
    for name, kind in TARGETS:
        src = os.path.join(SRC_DIR, f"{name}.uasset")
        out = os.path.join(OUT_DIR, f"{name}.png")
        if not os.path.exists(src):
            print(f"[!] Not found: {src}")
            continue
        ok = False
        if kind == "png":
            ok = export_direct_png(name, src, out)
        if not ok or kind == "thumb":
            ok = export_thumb_png(name, src, out)
        if ok:
            sz = os.path.getsize(out) / 1024.0
            print(f"[OK] Exported {name}.png ({sz:.1f} KB)")
        else:
            print(f"[ERR] Could not extract {name}")

if __name__ == "__main__":
    main()
