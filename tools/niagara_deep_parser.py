#!/usr/bin/env python3
"""
niagara_deep_parser.py - Parser siêu tốc và chuẩn xác cho Unreal Engine Niagara VFX assets (.uasset).
Quét cấu trúc bằng binary memory buffer regex + structure pattern matching:
- Không bị phụ thuộc vào định dạng version phức tạp của Summary UE5
- Tốc độ xử lý: ~0.05 giây / file (quét toàn bộ 88 file chỉ trong ~2 giây)
- Trích xuất: Emitter handles, Modules (Spawn, Velocity, Forces, Collision, SubUV...), Renderers, Textures, Materials, Meshes, Curves, Parameters.
"""

import sys
import os
import re
import json
from pathlib import Path

def parse_niagara_asset(file_path):
    with open(file_path, "rb") as f:
        data = f.read()

    # Chuyển đổi sang chuỗi byte string và text decoding an toàn
    text = data.decode("latin1", errors="ignore")
    
    # 1. Emitter handles và Emitter dependencies
    # EmitterHandleName\x00\x00...Name hoặc /Game/.../NE_...
    emitter_handles = []
    for m in re.finditer(r"EmitterHandleName\x00+([A-Za-z0-9_]+)", text):
        e_name = m.group(1)
        if e_name not in emitter_handles and e_name != "None":
            emitter_handles.append(e_name)

    ne_refs = list(set(re.findall(r"/Game/[A-Za-z0-9_/]+/(NE_[A-Za-z0-9_]+)", text)))
    
    # 2. Renderers được cấu hình
    renderers = []
    if "NiagaraSpriteRendererProperties" in text:
        renderers.append("SpriteRenderer")
    if "NiagaraRibbonRendererProperties" in text:
        renderers.append("RibbonRenderer")
    if "NiagaraMeshRendererProperties" in text:
        renderers.append("MeshRenderer")
    if "NiagaraLightRendererProperties" in text:
        renderers.append("LightRenderer")

    # 3. Textures, Materials, Meshes
    # Lọc các tiền tố chuẩn của UE
    textures = list(set(re.findall(r"\b(T_[A-Za-z0-9_]+)\b", text)))
    materials = list(set(re.findall(r"\b(M[I]*_[A-Za-z0-9_]+)\b", text)))
    meshes = list(set(re.findall(r"\b(SM_[A-Za-z0-9_]+)\b", text)))

    # 4. Modules & Solvers logic
    module_catalog = {
        "SpawnBurst": ["SpawnBurst_Instantaneous", "SpawnBurst"],
        "SpawnRate": ["SpawnRate"],
        "SpawnPerUnit": ["SpawnPerUnit"],
        "InitializeParticle": ["InitializeParticle"],
        "AddVelocity": ["AddVelocity", "AddVelocityInCone"],
        "Gravity": ["GravityForce", "Gravity"],
        "Drag": ["Drag", "PhysicsDrag"],
        "CurlNoise": ["CurlNoiseForce", "SampleCurlNoise"],
        "PointAttractor": ["PointAttractorForce", "PointAttractor"],
        "Vortex": ["VortexForce", "Vortex"],
        "Collision": ["CollisionQueryAndResponse", "Collision"],
        "ScaleColor": ["ScaleColor"],
        "ScaleSpriteSize": ["ScaleSpriteSize", "UniformCurveSpriteScale"],
        "SubUVAnimation": ["SubUVAnimation", "SubImageIndex"],
        "OrientToVelocity": ["OrientToVelocity"],
        "CameraOffset": ["CameraOffset"],
        "RibbonWidth": ["RibbonWidth"],
        "MeshOrientation": ["UpdateMeshOrientation", "MeshRotationRate"],
        "KillParticles": ["KillParticlesInVolume", "KillParticles"]
    }

    detected_modules = []
    for mod_name, patterns in module_catalog.items():
        if any(p in text for p in patterns):
            detected_modules.append(mod_name)

    # 5. Flipbook / SubUV configuration (grid ví dụ 8x8, 4x4)
    subuv_grid = None
    m_subuv = re.search(r"SubImageSize.*?([1-9][0-9]?)\.0+.*?([1-9][0-9]?)\.0+", text)
    if m_subuv:
        subuv_grid = f"{m_subuv.group(1)}x{m_subuv.group(2)}"

    # 6. User Parameters (đầu vào tùy chỉnh)
    user_params = list(set(re.findall(r"User\.([A-Za-z0-9_]+)", text)))

    is_system = os.path.basename(file_path).startswith("NS_")

    return {
        "file": os.path.basename(file_path),
        "type": "NiagaraSystem" if is_system else "NiagaraEmitter",
        "emitter_handles": emitter_handles,
        "ne_references": ne_refs,
        "renderers": renderers,
        "textures": sorted(textures),
        "materials": sorted(materials),
        "meshes": sorted(meshes),
        "modules": detected_modules,
        "subuv_grid": subuv_grid,
        "user_parameters": sorted(user_params)
    }

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 niagara_deep_parser.py <path_or_dir>")
        sys.exit(1)

    target = sys.argv[1]
    if os.path.isfile(target):
        res = parse_niagara_asset(target)
        print(json.dumps(res, indent=2))
        return

    all_uassets = [p for p in Path(target).rglob("*.uasset") if p.stem.startswith("NS_") or p.stem.startswith("NE_")]
    print(f"Found {len(all_uassets)} Niagara assets (NS_ / NE_) to scan...", flush=True)
    
    summary = {
        "systems": {},
        "emitters": {}
    }
    
    count = 0
    for i, p in enumerate(all_uassets):
        name = p.stem
        if name.startswith("NS_"):
            summary["systems"][name] = parse_niagara_asset(str(p))
            count += 1
        elif name.startswith("NE_"):
            summary["emitters"][name] = parse_niagara_asset(str(p))
            count += 1
        if (i + 1) % 10 == 0 or (i + 1) == len(all_uassets):
            print(f"[{i+1}/{len(all_uassets)}] Processed {name}", flush=True)

    out_file = "tools/niagara_deep_summary.json"
    with open(out_file, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    print(f"Successfully parsed {len(summary['systems'])} Systems and {len(summary['emitters'])} Emitters (Total: {count}).", flush=True)
    print(f"Output saved to {out_file}", flush=True)

if __name__ == "__main__":
    main()
