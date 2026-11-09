#!/usr/bin/env python3
"""
scripts/reorganize_modular_assets.py

Separates composite assets into clean, decoupled modular components:
- Base Body
- Torso clothing (Áo Giao Lĩnh)
- Legs clothing (Quần Wuxia)
- Headgear socket (Nón Lá Hiệp Khách)
- Weapon socket (Kiếm Thuận Thiên)
- Back socket (Vỏ Kiếm Sơn Mài)
"""

import sys
import os

try:
    import bpy
    from mathutils import Vector
except ImportError:
    sys.exit("Must be run inside Blender's python.")

SRC_GLB = "assets/characters/models/nhanvat1.glb"

OUT_DIRS = {
    "bodies": "assets/characters/bodies",
    "weapons": "assets/characters/equipment/weapons",
    "headgear": "assets/characters/equipment/headgear",
    "back": "assets/characters/equipment/back",
    "torso": "assets/characters/equipment/torso",
    "legs": "assets/characters/equipment/legs",
    "defs": "assets/characters/definitions"
}

for d in OUT_DIRS.values():
    os.makedirs(d, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC_GLB)

mesh_obj = bpy.data.objects["nhanvat1_mesh"]
arm_obj = bpy.data.objects["PlayerArmature"]

# Separate by material
bpy.context.view_layer.objects.active = mesh_obj
mesh_obj.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.separate(type="MATERIAL")
bpy.ops.object.mode_set(mode="OBJECT")

submeshes = [o for o in bpy.data.objects if o.type == "MESH"]
mat_map = {}
for sm in submeshes:
    mat_name = sm.material_slots[0].name if sm.material_slots else ""
    mat_map[mat_name] = sm

def export_isolated(objs, filepath, is_skinned=False):
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    if is_skinned:
        arm_obj.select_set(True)
        bpy.context.view_layer.objects.active = arm_obj
    else:
        bpy.context.view_layer.objects.active = objs[0]
        # For rigid sockets: remove armature modifiers so skins: 0
        for o in objs:
            for mod in list(o.modifiers):
                if mod.type == 'ARMATURE':
                    o.modifiers.remove(mod)

    bpy.ops.export_scene.gltf(
        filepath=filepath,
        export_format='GLB',
        use_selection=True,
        export_apply=not is_skinned,
        export_animations=False,
        export_skins=is_skinned
    )
    print(f"Exported: {filepath}")

# 1. Headgear: Nón Lá
non_la = mat_map.get("Mat_NonLa_WovenBamboo")
if non_la:
    export_isolated([non_la], os.path.join(OUT_DIRS["headgear"], "non_la_hiep_khach.glb"), is_skinned=False)

# 2. Back Scabbard: Vỏ Kiếm Sơn Mài
vo_kiem = mat_map.get("Mat_SonMai_LacquerArmor")
if vo_kiem:
    export_isolated([vo_kiem], os.path.join(OUT_DIRS["back"], "vo_kiem_son_mai.glb"), is_skinned=False)

# 3. Weapon: Kiếm Thuận Thiên
kiem = mat_map.get("Mat_ThuanThien_Steel")
if kiem:
    export_isolated([kiem], os.path.join(OUT_DIRS["weapons"], "kiem_thuan_thien.glb"), is_skinned=False)

# 4. Torso Outfit: Áo Giao Lĩnh
torso_mats = ["Mat_GiaoLinh_PrimaryBrocade", "Mat_LacViet_CrimsonSilk", "Mat_DongSon_GoldBronze", "Mat_Inner_IvorySilk"]
torso_objs = [mat_map[m] for m in torso_mats if m in mat_map]
if torso_objs:
    export_isolated(torso_objs, os.path.join(OUT_DIRS["torso"], "ao_giao_linh.glb"), is_skinned=True)

# 5. Legs Outfit: Quần Wuxia
legs_obj = mat_map.get("Mat_DarkIndigo_MartialCloth")
if legs_obj:
    export_isolated([legs_obj], os.path.join(OUT_DIRS["legs"], "quan_wuxia.glb"), is_skinned=True)

# 6. Base Body: Body Skin
body_mats = ["Mat_Stylized_ShadowedSkin", "Mat_HeroSkin_Hands", "Mat_Elemental_QiJade"]
body_objs = [mat_map[m] for m in body_mats if m in mat_map]
if body_objs:
    export_isolated(body_objs, os.path.join(OUT_DIRS["bodies"], "human_male_base.glb"), is_skinned=True)

print("All modular assets extracted successfully!")
