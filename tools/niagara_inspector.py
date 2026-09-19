#!/usr/bin/env python3
"""
niagara_inspector.py - Trích xuất tổng quát cấu trúc Niagara System & Emitter từ tệp .uasset của Unreal Engine.
Hỗ trợ UE 4 / UE 5.0 - 5.5+
Có thể dùng để phân tích bất kỳ Niagara System hoặc Emitter nào trong tương lai.
"""

import sys
import os
import struct
import json
import re
from pathlib import Path

# Tìm và nạp package uasset nếu có
try:
    from uasset.package import Package
    HAS_UASSET = True
except ImportError:
    HAS_UASSET = False

def read_fstring(f):
    """Đọc FString chuẩn Unreal: int32 length, chuỗi utf-8 hoặc utf-16."""
    try:
        raw_len = f.read(4)
        if len(raw_len) < 4:
            return ""
        length = struct.unpack("<i", raw_len)[0]
        if length > 0:
            s = f.read(length)
            return s.decode("utf-8", errors="ignore").rstrip("\x00")
        elif length < 0:
            utf16_len = -length * 2
            s = f.read(utf16_len)
            return s.decode("utf-16le", errors="ignore").rstrip("\x00")
        return ""
    except Exception:
        return ""

def scan_strings_and_symbols(file_path):
    """
    Parser dự phòng cực mạnh (Regex + Binary Heuristics) quét:
    - Danh sách NameTable / SymbolTable
    - Emitters (NE_..., EmitterHandle)
    - Renderers (NiagaraSpriteRendererProperties, NiagaraRibbonRendererProperties, NiagaraMeshRendererProperties, NiagaraLightRendererProperties)
    - Textures & Materials (T_..., M_..., MI_...)
    - Các Modules (SpawnBurst, CurlNoise, Drag, Gravity, Collision, SubUV, SizeBySpeed, FloatCurve...)
    - Giá trị tham số số thực (Floats, Vectors, Curves)
    """
    with open(file_path, "rb") as f:
        data = f.read()

    # Quét tất cả chuỗi in được độ dài >= 3
    ascii_strings = [m.decode("ascii", errors="ignore") for m in re.findall(b"[A-Za-z0-9_./-]{3,}", data)]
    
    # Gom nhóm thông tin phân tích
    info = {
        "file": os.path.basename(file_path),
        "path": file_path,
        "emitters": [],
        "renderers": [],
        "textures": [],
        "materials": [],
        "static_meshes": [],
        "modules": [],
        "variables_and_curves": [],
        "raw_names": []
    }

    # Phân loại
    seen = set()
    for s in ascii_strings:
        if s in seen:
            continue
        seen.add(s)

        # Emitter Handles / Emitters
        if "Emitter" in s or s.startswith("NE_"):
            if not any(k in s for k in ["Renderer", "Script", "Module"]):
                info["emitters"].append(s)

        # Renderers
        if "Renderer" in s:
            info["renderers"].append(s)

        # Textures
        if s.startswith("T_") or "/Textures/" in s or "/Sprites/" in s or "/BakedAtlas/" in s:
            info["textures"].append(s)

        # Materials
        if s.startswith("M_") or s.startswith("MI_") or "/Materials/" in s:
            info["materials"].append(s)

        # Meshes
        if s.startswith("SM_") or "/Meshes/" in s:
            info["static_meshes"].append(s)

        # Modules & Solvers
        modules_keywords = [
            "SpawnRate", "SpawnBurst", "SpawnPerUnit", "AddVelocity", "ConeVelocity",
            "Gravity", "PointAttractor", "Drag", "CurlNoise", "Vortex", "Collision",
            "Color", "ScaleColor", "SpriteSize", "ScaleSpriteSize", "SubUV",
            "RibbonWidth", "Orient", "CameraOffset", "KillParticles", "InitializeParticle",
            "SolveForcesAndVelocity", "UpdateMeshOrientation", "MeshRotationRate"
        ]
        if any(k.lower() in s.lower() for k in modules_keywords):
            info["modules"].append(s)

    return info

def inspect_with_uasset(file_path):
    """Phân tích sâu bằng cấu trúc Package của uasset nếu khả dụng."""
    try:
        pkg = Package(file_path)
        exports_data = []
        for i, exp in enumerate(pkg.exports):
            exp_name = str(exp.object_name)
            class_name = "Unknown"
            if exp.class_index < 0:
                class_name = str(pkg.imports[-exp.class_index - 1].object_name)
            elif exp.class_index > 0:
                class_name = str(pkg.exports[exp.class_index - 1].object_name)
            exports_data.append({
                "index": i,
                "name": exp_name,
                "class": class_name
            })
        return exports_data
    except Exception as e:
        return None

def analyze_vfx(file_path):
    """Phân tích toàn diện 1 Niagara System hoặc Emitter."""
    heuristic_info = scan_strings_and_symbols(file_path)
    exports_info = inspect_with_uasset(file_path) if HAS_UASSET else None
    
    result = {
        "file": os.path.basename(file_path),
        "path": file_path,
        "type": "NiagaraSystem" if "/NS_" in file_path or os.path.basename(file_path).startswith("NS_") else "NiagaraEmitter",
        "heuristic": heuristic_info,
        "exports": exports_info
    }
    return result

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 niagara_inspector.py <path_to_uasset_or_directory>")
        sys.exit(1)

    target = sys.argv[1]
    if os.path.isfile(target):
        res = analyze_vfx(target)
        print(json.dumps(res, indent=2))
    elif os.path.isdir(target):
        all_uassets = list(Path(target).rglob("*.uasset"))
        print(f"Scanning {len(all_uassets)} files in {target}...")
        summary = {}
        for p in all_uassets:
            name = p.stem
            if name.startswith("NS_") or name.startswith("NE_"):
                summary[name] = analyze_vfx(str(p))
        print(f"Found and analyzed {len(summary)} Niagara assets.")
        out_json = "tools/niagara_analysis_cache.json"
        with open(out_json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        print(f"Saved cache to {out_json}")
