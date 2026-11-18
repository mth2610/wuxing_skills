"""
blender_giao_linh_mixamo_engine.py

Procedural Traditional Cross-Collar Wide-Sleeve Robe (Áo Giao Lĩnh)
with Full Mixamo Hierarchy + Secondary Cloth Bones (Skirt Front/Back, Sleeves L/R, Ribbons).

Engine-Ready Architecture:
- True 2-sided solidified shells (no fake doubleSided culling masks)
- Low-poly smooth topology (~11k-14k tris, zero subsurf bloat) for fast CPU skinning
- Sleeve tube locked to Arm/ForeArm around the limb; only the hanging bell drapes via cloth_sleeve_*
- Tailored A-line skirt with subtle silk pleats, clean side slits, and normalized 4-bone weights
"""

import bpy
import bmesh
import math
from mathutils import Vector


# ==============================================================================
# 1. CONFIGURATION & PALETTE
# ==============================================================================

COLOR_PALETTE = {
    "primary_indigo_teal": (0.215, 0.375, 0.475, 1.0),   # #376079
    "shadow_indigo":       (0.118, 0.218, 0.285, 1.0),   # #1E3849
    "highlight_steel":     (0.357, 0.525, 0.620, 1.0),   # #5B869E
    "collar_lining_ivory": (0.855, 0.832, 0.780, 1.0),   # #DAD4C7
    "inner_shirt_cream":   (0.910, 0.892, 0.850, 1.0),   # #E8E3D9
    "gold_embroidery":     (0.790, 0.718, 0.555, 1.0),   # #C9B78E
}

PROPORTIONS = {
    "hem_z": 0.040,
    "slit_top_z": 0.995,
    "waist_z": 1.020,
    "chest_z": 1.270,
    "collar_cross_z": 1.390,
    "shoulder_z": 1.425,
    "neck_z": 1.490,
    "inner_neck_z": 1.504,
}

ALL_BONE_NAMES = [
    # Core Mixamo Hierarchy (20 bones)
    "mixamorig:Hips",
    "mixamorig:Spine",
    "mixamorig:Spine1",
    "mixamorig:Spine2",
    "mixamorig:Neck",
    "mixamorig:Head",
    "mixamorig:LeftShoulder",
    "mixamorig:LeftArm",
    "mixamorig:LeftForeArm",
    "mixamorig:LeftHand",
    "mixamorig:RightShoulder",
    "mixamorig:RightArm",
    "mixamorig:RightForeArm",
    "mixamorig:RightHand",
    "mixamorig:LeftUpLeg",
    "mixamorig:LeftLeg",
    "mixamorig:LeftFoot",
    "mixamorig:RightUpLeg",
    "mixamorig:RightLeg",
    "mixamorig:RightFoot",
    # Secondary Bones - Sleeves
    "cloth_sleeve_l_01",
    "cloth_sleeve_l_02",
    "cloth_sleeve_r_01",
    "cloth_sleeve_r_02",
    # Secondary Bones - Front Skirt
    "cloth_skirt_f_l",
    "cloth_skirt_f_l_02",
    "cloth_skirt_f_r",
    "cloth_skirt_f_r_02",
    "cloth_skirt_f_c_01",
    "cloth_skirt_f_c_02",
    # Secondary Bones - Back Skirt
    "cloth_skirt_b_l",
    "cloth_skirt_b_l_02",
    "cloth_skirt_b_r",
    "cloth_skirt_b_r_02",
    "cloth_skirt_b_c_01",
    "cloth_skirt_b_c_02",
    # Secondary Bones - Waist Ribbons
    "cloth_ribbon_l",
    "cloth_ribbon_l_02",
    "cloth_ribbon_r",
    "cloth_ribbon_r_02",
]


# ==============================================================================
# 2. SCENE & MESH HELPERS
# ==============================================================================

def clean_existing_setup():
    """Remove previous Armature and Mesh if re-running the script."""
    for obj_name in ["RefBodyArmature", "Wuxia_AoTienHiep_Mesh"]:
        if obj_name in bpy.data.objects:
            obj = bpy.data.objects[obj_name]
            data = obj.data
            bpy.data.objects.remove(obj, do_unlink=True)
            if data and data.users == 0:
                if isinstance(data, bpy.types.Mesh):
                    bpy.data.meshes.remove(data)
                elif isinstance(data, bpy.types.Armature):
                    bpy.data.armatures.remove(data)

    if "Giao_Linh_Garment_Architecture" in bpy.data.collections:
        col = bpy.data.collections["Giao_Linh_Garment_Architecture"]
        for obj in list(col.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
        bpy.data.collections.remove(col)


def set_bsdf_input(bsdf: bpy.types.Node, names: list, value):
    """Principled BSDF compatibility across Blender 3.6 and 4.x."""
    for name in names:
        if name in bsdf.inputs:
            sock = bsdf.inputs[name]
            if sock.type == "VALUE" and isinstance(value, (tuple, list)):
                sock.default_value = float(sum(value[:3]) / 3.0)
            elif sock.type == "RGBA" and isinstance(value, (int, float)):
                v = float(value)
                sock.default_value = (v, v, v, 1.0)
            else:
                sock.default_value = value
            return


def add_vertex_weight(obj: bpy.types.Object, bone_name: str, v_idx: int, weight: float):
    if weight <= 0.0001:
        return
    vg = obj.vertex_groups.get(bone_name)
    if vg is None:
        vg = obj.vertex_groups.new(name=bone_name)
    vg.add([v_idx], float(weight), 'REPLACE')


def ensure_uv_and_smooth(mesh: bpy.types.Mesh, recalc_normals: bool = True):
    """Ensure valid UVMap (for glTF tangent export), smooth shading, and outward normals."""
    bm = bmesh.new()
    bm.from_mesh(mesh)
    if recalc_normals:
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    uv_layer = bm.loops.layers.uv.active or bm.loops.layers.uv.new("UVMap")
    for face in bm.faces:
        face.smooth = True
        for loop in face.loops:
            co = loop.vert.co
            u = math.atan2(co.x, -co.y) / (2.0 * math.pi) + 0.5
            v = (co.z - PROPORTIONS["hem_z"]) / 1.50
            loop[uv_layer].uv = (u, v)
    bm.to_mesh(mesh)
    bm.free()


def finalize_garment_part(
    obj: bpy.types.Object,
    thickness: float = 0.0024,
    solidify_offset: float = 0.0,
    recalc_normals: bool = True,
):
    """
    Finalize a garment part without Subdivision bloat:
    - Recalculate consistent outward normals and UVs
    - Apply Solidify only when the part is a single-walled sheet (thickness > 0)
    """
    ensure_uv_and_smooth(obj.data, recalc_normals=recalc_normals)

    if thickness > 0.0:
        solid = obj.modifiers.new(name="Solidify", type="SOLIDIFY")
        solid.thickness = thickness
        solid.offset = solidify_offset
        solid.use_even_offset = True
        solid.use_quality_normals = True
        solid.use_rim = True
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=solid.name)


# ==============================================================================
# 3. ANATOMICAL TORSO & A-LINE SURFACE MATH
# ==============================================================================

def get_body_profile(z: float):
    hem_z = PROPORTIONS["hem_z"]
    waist_z = PROPORTIONS["waist_z"]
    chest_z = PROPORTIONS["chest_z"]
    shoulder_z = PROPORTIONS["shoulder_z"]
    neck_z = PROPORTIONS["neck_z"]

    if z <= waist_z:
        s = max(0.0, (z - hem_z) / (waist_z - hem_z))
        flare = (1.0 - s) ** 1.08
        rx = 0.148 + 0.275 * flare
        ry = 0.116 + 0.188 * flare
        y_shift = 0.0
    elif z <= chest_z:
        s = (z - waist_z) / (chest_z - waist_z)
        smooth_blouse = math.sin(s * math.pi) * 0.008
        rx = 0.148 + 0.064 * (s ** 0.88) + smooth_blouse * 0.5
        ry = 0.116 + 0.026 * math.sin(s * math.pi * 0.75) + smooth_blouse
        y_shift = -0.003 * s
    elif z <= shoulder_z:
        s = (z - chest_z) / (shoulder_z - chest_z)
        rx = 0.212 - 0.008 * (s ** 1.4)
        ry = 0.138 - 0.016 * (s ** 1.3)
        y_shift = -0.003 * (1.0 - s)
    else:
        s = min(1.0, max(0.0, (z - shoulder_z) / (neck_z - shoulder_z)))
        smooth_s = 0.5 * (1.0 - math.cos(math.pi * s))
        rx = 0.196 * (1.0 - smooth_s) + 0.068 * smooth_s
        ry = 0.116 * (1.0 - smooth_s) + 0.060 * smooth_s
        y_shift = 0.004 * smooth_s

    return rx, ry, y_shift


def eval_body_point(z: float, phi: float, radial_offset: float = 0.0, x_flare_extra: float = 0.0):
    rx, ry, y_shift = get_body_profile(z)
    eff_rx = max(0.035, rx + radial_offset + x_flare_extra)
    eff_ry = max(0.035, ry + radial_offset)
    x = eff_rx * math.sin(phi)
    y = -eff_ry * math.cos(phi) + y_shift
    return (x, y, z)


# ==============================================================================
# 4. MIXAMO ARMATURE + SECONDARY CLOTH BONES BUILDER
# ==============================================================================

def build_mixamo_and_cloth_armature() -> bpy.types.Object:
    """Build RefBodyArmature aligned to the robe geometry and Mixamo bone names."""
    arm_data = bpy.data.armatures.new("RefBodyArmature")
    arm_obj = bpy.data.objects.new("RefBodyArmature", arm_data)
    arm_data.display_type = 'OCTAHEDRAL'
    arm_obj.show_in_front = True
    bpy.context.scene.collection.objects.link(arm_obj)

    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='EDIT')
    eb = arm_data.edit_bones

    def make_bone(name, head, tail, parent_name=None, connect=False, roll=0.0):
        b = eb.new(name)
        b.head = Vector(head)
        b.tail = Vector(tail)
        b.roll = roll
        if parent_name and parent_name in eb:
            b.parent = eb[parent_name]
            b.use_connect = connect
        return b

    # 1. Spine & Head
    make_bone("mixamorig:Hips",   (0.0, 0.0, 0.92), (0.0, 0.0, 1.02))
    make_bone("mixamorig:Spine",  (0.0, 0.0, 1.02), (0.0, 0.0, 1.15), "mixamorig:Hips", True)
    make_bone("mixamorig:Spine1", (0.0, 0.0, 1.15), (0.0, 0.0, 1.28), "mixamorig:Spine", True)
    make_bone("mixamorig:Spine2", (0.0, 0.0, 1.28), (0.0, 0.0, 1.40), "mixamorig:Spine1", True)
    make_bone("mixamorig:Neck",   (0.0, 0.0, 1.40), (0.0, 0.0, 1.50), "mixamorig:Spine2", True)
    make_bone("mixamorig:Head",   (0.0, 0.0, 1.50), (0.0, 0.0, 1.70), "mixamorig:Neck", True)

    # 2. Arms (centered inside the upper sleeve tube, covering deltoid height)
    make_bone("mixamorig:LeftShoulder", (0.06, 0.0, 1.42), (0.16, 0.0, 1.38), "mixamorig:Spine2", False)
    make_bone("mixamorig:LeftArm",      (0.16, 0.0, 1.38), (0.33, 0.0, 1.14), "mixamorig:LeftShoulder", True)
    make_bone("mixamorig:LeftForeArm",  (0.33, 0.0, 1.14), (0.48, 0.0, 0.92), "mixamorig:LeftArm", True)
    make_bone("mixamorig:LeftHand",     (0.48, 0.0, 0.92), (0.58, 0.0, 0.80), "mixamorig:LeftForeArm", True)

    make_bone("mixamorig:RightShoulder", (-0.06, 0.0, 1.42), (-0.16, 0.0, 1.38), "mixamorig:Spine2", False)
    make_bone("mixamorig:RightArm",      (-0.16, 0.0, 1.38), (-0.33, 0.0, 1.14), "mixamorig:RightShoulder", True)
    make_bone("mixamorig:RightForeArm",  (-0.33, 0.0, 1.14), (-0.48, 0.0, 0.92), "mixamorig:RightArm", True)
    make_bone("mixamorig:RightHand",     (-0.48, 0.0, 0.92), (-0.58, 0.0, 0.80), "mixamorig:RightForeArm", True)

    # 3. Legs
    make_bone("mixamorig:LeftUpLeg", (0.09, 0.0, 0.90), (0.09, 0.0, 0.48), "mixamorig:Hips", False)
    make_bone("mixamorig:LeftLeg",   (0.09, 0.0, 0.48), (0.09, 0.0, 0.08), "mixamorig:LeftUpLeg", True)
    make_bone("mixamorig:LeftFoot",  (0.09, 0.0, 0.08), (0.09, -0.13, 0.02), "mixamorig:LeftLeg", True)

    make_bone("mixamorig:RightUpLeg", (-0.09, 0.0, 0.90), (-0.09, 0.0, 0.48), "mixamorig:Hips", False)
    make_bone("mixamorig:RightLeg",   (-0.09, 0.0, 0.48), (-0.09, 0.0, 0.08), "mixamorig:RightUpLeg", True)
    make_bone("mixamorig:RightFoot",  (-0.09, 0.0, 0.08), (-0.09, -0.13, 0.02), "mixamorig:RightLeg", True)

    # 4. Bell Sleeve Secondary Bones (centered in the hanging silk drape below ForeArm)
    make_bone("cloth_sleeve_l_01", (0.33, 0.0, 1.00), (0.36, 0.0, 0.76), "mixamorig:LeftForeArm", False)
    make_bone("cloth_sleeve_l_02", (0.36, 0.0, 0.76), (0.39, 0.0, 0.54), "cloth_sleeve_l_01", True)

    make_bone("cloth_sleeve_r_01", (-0.33, 0.0, 1.00), (-0.36, 0.0, 0.76), "mixamorig:RightForeArm", False)
    make_bone("cloth_sleeve_r_02", (-0.36, 0.0, 0.76), (-0.39, 0.0, 0.54), "cloth_sleeve_r_01", True)

    # 5. Front Skirt Secondary Bones
    for b_top, b_bot, phi in [
        ("cloth_skirt_f_l",    "cloth_skirt_f_l_02",  0.28 * math.pi),
        ("cloth_skirt_f_c_01", "cloth_skirt_f_c_02",  0.00),
        ("cloth_skirt_f_r",    "cloth_skirt_f_r_02", -0.28 * math.pi),
    ]:
        p0 = eval_body_point(0.96, phi, radial_offset=0.004)
        p1 = eval_body_point(0.50, phi, radial_offset=0.004)
        p2 = eval_body_point(0.05, phi, radial_offset=0.004)
        make_bone(b_top, p0, p1, "mixamorig:Hips", False)
        make_bone(b_bot, p1, p2, b_top, True)

    # 6. Back Skirt Secondary Bones
    for b_top, b_bot, phi in [
        ("cloth_skirt_b_l",    "cloth_skirt_b_l_02",  0.72 * math.pi),
        ("cloth_skirt_b_c_01", "cloth_skirt_b_c_02",  1.00 * math.pi),
        ("cloth_skirt_b_r",    "cloth_skirt_b_r_02",  1.28 * math.pi),
    ]:
        p0 = eval_body_point(0.96, phi, radial_offset=-0.001)
        p1 = eval_body_point(0.50, phi, radial_offset=-0.001)
        p2 = eval_body_point(0.05, phi, radial_offset=-0.001)
        make_bone(b_top, p0, p1, "mixamorig:Hips", False)
        make_bone(b_bot, p1, p2, b_top, True)

    # 7. Waist Ribbon Secondary Bones
    for b_top, b_bot, phi_base, z_bot, r_off in [
        ("cloth_ribbon_r", "cloth_ribbon_r_02", -0.465, 0.42, 0.011),
        ("cloth_ribbon_l", "cloth_ribbon_l_02", -0.405, 0.46, 0.015),
    ]:
        z_top = PROPORTIONS["waist_z"] - 0.008
        z_mid = 0.5 * (z_top + z_bot)
        p0 = eval_body_point(z_top, phi_base, radial_offset=r_off)
        p1 = eval_body_point(z_mid, phi_base, radial_offset=r_off)
        p2 = eval_body_point(z_bot, phi_base, radial_offset=r_off)
        make_bone(b_top, p0, p1, "mixamorig:Hips", False)
        make_bone(b_bot, p1, p2, b_top, True)

    bpy.ops.object.mode_set(mode='OBJECT')
    return arm_obj


# ==============================================================================
# 5. COMPACT TRIM-AWARE LOOP SCHEDULES (LOW-POLY, CLEAN TRIM BANDS)
# ==============================================================================

def make_collar_u_schedule(body_segs: int = 24) -> list:
    left_collar = [0.000, 0.022, 0.044]
    left_gold = [0.050]
    right_gold = [0.950]
    right_collar = [0.956, 0.978, 1.000]
    u0, u1 = 0.054, 0.946
    body = [u0 + (u1 - u0) * (i / body_segs) for i in range(body_segs + 1)]
    return left_collar + left_gold + body + right_gold + right_collar


def make_panel_u_schedule(mid_segs: int = 18) -> list:
    left_edge = [0.000, 0.018, 0.024]
    right_edge = [0.976, 0.982, 1.000]
    u0, u1 = 0.030, 0.970
    mid = [u0 + (u1 - u0) * (i / mid_segs) for i in range(mid_segs + 1)]
    return left_edge + mid + right_edge


def make_skirt_v_schedule(mid_rings: int = 12) -> list:
    hem_edge = [0.000, 0.014, 0.024]
    v0, v1 = 0.032, 0.992
    mid = [v0 + (v1 - v0) * (r / mid_rings) for r in range(mid_rings + 1)]
    return hem_edge + mid + [1.000]


# ==============================================================================
# 6. PBR MATERIALS (TRUE BACKFACE CULLING ENABLED)
# ==============================================================================

def create_garment_materials() -> dict:
    mats = {}

    mat_teal = bpy.data.materials.get("MAT_Teal_Cloud_Damask") or bpy.data.materials.new("MAT_Teal_Cloud_Damask")
    mat_teal.use_nodes = True
    mat_teal.use_backface_culling = True
    nodes = mat_teal.node_tree.nodes
    links = mat_teal.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    set_bsdf_input(bsdf, ["Base Color"], COLOR_PALETTE["primary_indigo_teal"])
    set_bsdf_input(bsdf, ["Roughness"], 0.38)
    set_bsdf_input(bsdf, ["Sheen Weight", "Sheen"], 0.60)
    set_bsdf_input(bsdf, ["Sheen Tint"], COLOR_PALETTE["highlight_steel"])
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    mats["teal"] = mat_teal

    mat_ivory = bpy.data.materials.get("MAT_Ivory_Silk_Trim") or bpy.data.materials.new("MAT_Ivory_Silk_Trim")
    mat_ivory.use_nodes = True
    mat_ivory.use_backface_culling = True
    bsdf_ivory = mat_ivory.node_tree.nodes.get("Principled BSDF")
    if bsdf_ivory:
        set_bsdf_input(bsdf_ivory, ["Base Color"], COLOR_PALETTE["collar_lining_ivory"])
        set_bsdf_input(bsdf_ivory, ["Roughness"], 0.32)
        set_bsdf_input(bsdf_ivory, ["Sheen Weight", "Sheen"], 0.45)
    mats["ivory"] = mat_ivory

    mat_inner = bpy.data.materials.get("MAT_Inner_Trung_Y_Silk") or bpy.data.materials.new("MAT_Inner_Trung_Y_Silk")
    mat_inner.use_nodes = True
    mat_inner.use_backface_culling = True
    bsdf_inner = mat_inner.node_tree.nodes.get("Principled BSDF")
    if bsdf_inner:
        set_bsdf_input(bsdf_inner, ["Base Color"], COLOR_PALETTE["inner_shirt_cream"])
        set_bsdf_input(bsdf_inner, ["Roughness"], 0.36)
        set_bsdf_input(bsdf_inner, ["Sheen Weight", "Sheen"], 0.35)
    mats["inner_cream"] = mat_inner

    mat_gold = bpy.data.materials.get("MAT_Gold_Brocade_Accent") or bpy.data.materials.new("MAT_Gold_Brocade_Accent")
    mat_gold.use_nodes = True
    mat_gold.use_backface_culling = True
    bsdf_gold = mat_gold.node_tree.nodes.get("Principled BSDF")
    if bsdf_gold:
        set_bsdf_input(bsdf_gold, ["Base Color"], COLOR_PALETTE["gold_embroidery"])
        set_bsdf_input(bsdf_gold, ["Metallic"], 0.62)
        set_bsdf_input(bsdf_gold, ["Roughness"], 0.26)
    mats["gold"] = mat_gold

    return mats


# ==============================================================================
# 7. WEIGHT PAINTING HELPERS
# ==============================================================================

def assign_torso_weights(obj: bpy.types.Object):
    for v in obj.data.vertices:
        x, y, z = v.co
        weights = {}
        if z < 1.03:
            w_hips = max(0.0, (1.03 - z) / 0.06)
            weights["mixamorig:Hips"] = w_hips
            weights["mixamorig:Spine"] = 1.0 - w_hips
        elif z < 1.15:
            t = (z - 1.03) / 0.12
            weights["mixamorig:Spine"] = 1.0 - t
            weights["mixamorig:Spine1"] = t
        elif z < 1.28:
            t = (z - 1.15) / 0.13
            weights["mixamorig:Spine1"] = 1.0 - t
            weights["mixamorig:Spine2"] = t
        else:
            t_neck = max(0.0, min(1.0, (z - 1.38) / 0.10))
            weights["mixamorig:Spine2"] = 1.0 - 0.70 * t_neck
            weights["mixamorig:Neck"] = 0.70 * t_neck

        if z > 1.20 and abs(x) > 0.07:
            w_sh = min(0.55, ((abs(x) - 0.07) / 0.12) * max(0.0, min(1.0, (z - 1.20) / 0.15)))
            sh_bone = "mixamorig:LeftShoulder" if x > 0 else "mixamorig:RightShoulder"
            for k in list(weights.keys()):
                weights[k] *= (1.0 - w_sh)
            weights[sh_bone] = w_sh

        for bname, w in weights.items():
            add_vertex_weight(obj, bname, v.index, w)


def assign_skirt_panel_weights(obj: bpy.types.Object, is_front: bool):
    b_left_1 = "cloth_skirt_f_l" if is_front else "cloth_skirt_b_l"
    b_left_2 = "cloth_skirt_f_l_02" if is_front else "cloth_skirt_b_l_02"
    b_right_1 = "cloth_skirt_f_r" if is_front else "cloth_skirt_b_r"
    b_right_2 = "cloth_skirt_f_r_02" if is_front else "cloth_skirt_b_r_02"
    b_center_1 = "cloth_skirt_f_c_01" if is_front else "cloth_skirt_b_c_01"
    b_center_2 = "cloth_skirt_f_c_02" if is_front else "cloth_skirt_b_c_02"

    z_hem = PROPORTIONS["hem_z"]
    z_waist = PROPORTIONS["waist_z"]

    for v in obj.data.vertices:
        x, y, z = v.co
        t_down = max(0.0, min(1.0, (z_waist - z) / (z_waist - z_hem)))

        w_hips = max(0.0, 1.0 - max(0.0, t_down - 0.06) * 4.8)
        w_leg_follow = 0.10 * math.sin(min(1.0, t_down * 1.4) * math.pi) if t_down > 0.08 else 0.0
        w_cloth = max(0.0, 1.0 - w_hips - w_leg_follow)

        if w_hips > 0.0:
            add_vertex_weight(obj, "mixamorig:Hips", v.index, w_hips)
        if w_leg_follow > 0.005:
            leg_bone = "mixamorig:LeftUpLeg" if x >= 0 else "mixamorig:RightUpLeg"
            add_vertex_weight(obj, leg_bone, v.index, w_leg_follow)

        if w_cloth > 0.0:
            if t_down < 0.50:
                w_seg1 = 1.0 - max(0.0, (t_down - 0.32) / 0.36)
                w_seg2 = 1.0 - w_seg1
            else:
                w_seg2 = min(1.0, 0.5 + (t_down - 0.50) / 0.36)
                w_seg1 = 1.0 - w_seg2

            rx, _, _ = get_body_profile(z)
            nx = max(-1.0, min(1.0, x / max(0.12, rx)))

            if nx >= 0.0:
                w_side = min(1.0, nx * 1.15)
                w_mid = 1.0 - w_side
                add_vertex_weight(obj, b_left_1, v.index, w_cloth * w_side * w_seg1)
                add_vertex_weight(obj, b_left_2, v.index, w_cloth * w_side * w_seg2)
            else:
                w_side = min(1.0, (-nx) * 1.15)
                w_mid = 1.0 - w_side
                add_vertex_weight(obj, b_right_1, v.index, w_cloth * w_side * w_seg1)
                add_vertex_weight(obj, b_right_2, v.index, w_cloth * w_side * w_seg2)

            add_vertex_weight(obj, b_center_1, v.index, w_cloth * w_mid * w_seg1)
            add_vertex_weight(obj, b_center_2, v.index, w_cloth * w_mid * w_seg2)


# ==============================================================================
# 8. GARMENT PART BUILDERS
# ==============================================================================

def build_inner_trung_y_collar(mats: dict) -> bpy.types.Object:
    mesh = bpy.data.meshes.new("01_Co_Ao_Lot_Trung_Y_Trong_Mesh")
    obj = bpy.data.objects.new("01_Co_Ao_Lot_Trung_Y_Trong", mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    rings = 8
    segs = 26
    z_bot = 1.280
    z_top = PROPORTIONS["inner_neck_z"]

    ring_verts = []
    for r in range(rings + 1):
        v = r / rings
        z = z_bot + v * (z_top - z_bot)
        t_top = 1.0 - v
        phi_start = 0.14 - 0.52 * (t_top ** 0.88)
        phi_end = (2.0 * math.pi - 0.14) + 0.52 * (t_top ** 0.88)
        base_inset = -0.015 * (1.0 - 0.45 * (v ** 2))

        row = []
        for i in range(segs + 1):
            u = i / segs
            phi = phi_start + u * (phi_end - phi_start)
            overlap_shift = 0.0018 * math.cos(math.pi * u)
            pt = eval_body_point(
                min(z, PROPORTIONS["neck_z"]),
                phi,
                radial_offset=base_inset + overlap_shift,
            )
            row.append(bm.verts.new((pt[0], pt[1], z)))
        ring_verts.append(row)

    for r in range(rings):
        for i in range(segs):
            bm.faces.new((
                ring_verts[r][i],
                ring_verts[r][i + 1],
                ring_verts[r + 1][i + 1],
                ring_verts[r + 1][i],
            ))

    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)
    for poly in obj.data.polygons:
        poly.material_index = 3

    assign_torso_weights(obj)
    finalize_garment_part(obj, thickness=0.0024)
    return obj


def build_upper_giao_linh_bodice(mats: dict) -> bpy.types.Object:
    mesh = bpy.data.meshes.new("02_Than_Tren_Giao_Linh_Mesh")
    obj = bpy.data.objects.new("02_Than_Tren_Giao_Linh", mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    u_list = make_collar_u_schedule(body_segs=24)
    segs = len(u_list) - 1

    mid_rings = 10
    v_mid = [0.0] + [0.02 + 0.905 * (r / mid_rings) for r in range(mid_rings + 1)]
    v_list = v_mid + [0.935, 0.945, 0.975, 1.000]
    rings = len(v_list) - 1

    z_min = 1.000
    z_max = PROPORTIONS["neck_z"]

    ring_verts = []
    for r, v in enumerate(v_list):
        z = z_min + v * (z_max - z_min)
        t_top = 1.0 - v
        phi_start = 0.23 - 0.98 * t_top + 0.30 * (t_top ** 2)
        phi_end = (2.0 * math.pi - 0.23) + 0.92 * t_top - 0.28 * (t_top ** 2)

        row = []
        for i, u in enumerate(u_list):
            phi = phi_start + u * (phi_end - phi_start)
            layer_offset = 0.0048 * math.cos(math.pi * u)
            is_collar_zone = (u <= 0.050) or (u >= 0.950) or (v >= 0.935)
            waist_taper = min(1.0, max(0.0, (z - 1.015) / 0.05))
            collar_emboss = (0.0018 * waist_taper) if is_collar_zone else 0.0

            pt = eval_body_point(z, phi, radial_offset=layer_offset + collar_emboss)
            row.append(bm.verts.new(pt))
        ring_verts.append(row)

    for r in range(rings):
        v_mid_face = 0.5 * (v_list[r] + v_list[r + 1])
        for i in range(segs):
            u_mid_face = 0.5 * (u_list[i] + u_list[i + 1])
            face = bm.faces.new((
                ring_verts[r][i],
                ring_verts[r][i + 1],
                ring_verts[r + 1][i + 1],
                ring_verts[r + 1][i],
            ))
            in_ivory = (u_mid_face < 0.044) or (u_mid_face > 0.956) or (v_mid_face > 0.945)
            in_gold = (0.044 <= u_mid_face <= 0.050) or (0.950 <= u_mid_face <= 0.956) or (0.935 <= v_mid_face <= 0.945)
            face.material_index = 1 if in_ivory else (2 if in_gold else 0)

    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)

    assign_torso_weights(obj)
    finalize_garment_part(obj, thickness=0.0026)
    return obj


def build_split_skirt_panel(
    name: str,
    mats: dict,
    phi_left_waist: float,
    phi_right_waist: float,
    phi_left_hem: float,
    phi_right_hem: float,
    base_radial_offset: float,
    is_front: bool,
    arch_camber: float = 0.003,
    pleat_amp: float = 0.004,
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    u_list = make_panel_u_schedule(mid_segs=18)
    v_list = make_skirt_v_schedule(mid_rings=12)
    segs = len(u_list) - 1
    rings = len(v_list) - 1

    z_hem = PROPORTIONS["hem_z"]
    z_top = PROPORTIONS["waist_z"] + 0.010

    ring_verts = []
    for r, v in enumerate(v_list):
        z = z_hem + v * (z_top - z_hem)
        t_down = 1.0 - v
        phi_0 = phi_left_waist * (1.0 - t_down) + phi_left_hem * t_down
        phi_1 = phi_right_waist * (1.0 - t_down) + phi_right_hem * t_down

        row = []
        for i, u in enumerate(u_list):
            phi = phi_0 + u * (phi_1 - phi_0)
            camber = arch_camber * math.sin(math.pi * u) * (0.3 + 0.7 * t_down)
            # Subtle vertical silk pleats fading toward waist
            pleat = pleat_amp * math.cos(5.0 * math.pi * u) * math.sin(math.pi * u) * (t_down ** 0.85)
            pt = eval_body_point(z, phi, radial_offset=base_radial_offset + camber + pleat)
            row.append(bm.verts.new(pt))
        ring_verts.append(row)

    for r in range(rings):
        v_mid = 0.5 * (v_list[r] + v_list[r + 1])
        for i in range(segs):
            u_mid = 0.5 * (u_list[i] + u_list[i + 1])
            face = bm.faces.new((
                ring_verts[r][i],
                ring_verts[r][i + 1],
                ring_verts[r + 1][i + 1],
                ring_verts[r + 1][i],
            ))
            in_ivory = (v_mid < 0.014) or (u_mid < 0.018) or (u_mid > 0.982)
            in_gold = (0.014 <= v_mid <= 0.024) or (0.018 <= u_mid <= 0.024) or (0.976 <= u_mid <= 0.982)
            face.material_index = 1 if in_ivory else (2 if in_gold else 0)

    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)

    assign_skirt_panel_weights(obj, is_front=is_front)
    finalize_garment_part(obj, thickness=0.0025)
    return obj


def build_all_split_skirt_panels(mats: dict) -> list:
    panels = []
    # 1. Front Outer Panel (overlaps slightly at hip side-slit to prevent waist-level gaps)
    panels.append(build_split_skirt_panel(
        name="03_Ta_Truoc_Ao",
        mats=mats,
        phi_left_waist=-0.535 * math.pi,
        phi_right_waist=0.535 * math.pi,
        phi_left_hem=-0.515 * math.pi,
        phi_right_hem=0.515 * math.pi,
        base_radial_offset=0.0065,
        is_front=True,
        arch_camber=0.0030,
        pleat_amp=0.0045,
    ))
    # 2. Front Inner Underlap (traditional cross-collar under-skirt)
    panels.append(build_split_skirt_panel(
        name="03b_Ta_Phu_Ben_Trong",
        mats=mats,
        phi_left_waist=-0.520 * math.pi,
        phi_right_waist=-0.120 * math.pi,
        phi_left_hem=-0.510 * math.pi,
        phi_right_hem=-0.150 * math.pi,
        base_radial_offset=-0.0020,
        is_front=True,
        arch_camber=0.0010,
        pleat_amp=0.0015,
    ))
    # 3. Back Panel (overlaps cleanly underneath front panel at hip)
    panels.append(build_split_skirt_panel(
        name="04_Ta_Sau_Ao",
        mats=mats,
        phi_left_waist=0.470 * math.pi,
        phi_right_waist=1.530 * math.pi,
        phi_left_hem=0.495 * math.pi,
        phi_right_hem=1.505 * math.pi,
        base_radial_offset=0.0010,
        is_front=False,
        arch_camber=0.0030,
        pleat_amp=0.0045,
    ))
    return panels


def build_refined_wide_sleeves(mats: dict) -> list:
    """
    Build 2-sided wide bell sleeves (Quảng Tay) directly without double-Solidify:
    - Outer wall + cuff rim + deep inner lining (normals properly flipped inward)
    - Upper/front/back tube around the arm stays 100% locked to Arm/ForeArm
    - Only the hanging lower silk bell (s_vert < -0.15) drapes with cloth_sleeve_*
    """
    created = []
    t_body = [0.895 * (r / 12) for r in range(13)]
    t_list = t_body + [0.908, 0.925, 0.975, 1.000]
    rings = len(t_list) - 1
    segs = 24

    for side, sign in [("Left", 1.0), ("Right", -1.0)]:
        mesh = bpy.data.meshes.new(f"05_Tay_Ao_Quang_Tay_{side}_Mesh")
        obj = bpy.data.objects.new(f"05_Tay_Ao_Quang_Tay_{side}", mesh)
        bpy.context.scene.collection.objects.link(obj)

        bm = bmesh.new()
        vert_meta = []

        def eval_sleeve_ring(t_val: float, inset: float = 0.0):
            # Cover the full 1.48m Mixamo deltoid/shoulder cap and give generous bicep clearance
            x_top = sign * (0.125 + 0.462 * t_val) - sign * inset * 0.6
            z_top = 1.472 - 0.505 * (t_val ** 0.95) - inset

            x_bot = sign * (0.152 + 0.246 * (t_val ** 0.88)) - sign * inset * 0.6
            z_bot = 1.155 - 0.605 * (t_val ** 1.05) - 0.042 * math.sin(math.pi * t_val) + inset

            cx = 0.5 * (x_top + x_bot)
            cz = 0.5 * (z_top + z_bot)
            dx = 0.5 * (x_top - x_bot)
            dz = 0.5 * (z_top - z_bot)
            ry = max(0.025, (0.128 * (1.0 - t_val) + 0.145 * (t_val ** 0.90)) - inset)
            y_bias = -0.012 * max(0.0, 1.0 - t_val * 2.0)

            pts = []
            for i in range(segs):
                theta = (i / segs) * 2.0 * math.pi
                s_vert = math.sin(theta)
                c_horiz = math.cos(theta)
                width_mod = 1.0 - 0.03 * s_vert
                vx = cx + dx * s_vert
                vz = cz + dz * s_vert
                vy = y_bias + ry * width_mod * c_horiz
                v = bm.verts.new((vx, vy, vz))
                pts.append(v)
                vert_meta.append((t_val, s_vert))
            return pts

        outer_rings = [eval_sleeve_ring(t, inset=0.0) for t in t_list]
        t_inner_list = [1.000, 0.992, 0.930, 0.820, 0.680, 0.520]
        inner_rings = [eval_sleeve_ring(t, inset=0.0028) for t in t_inner_list]

        for r in range(rings):
            t_mid = 0.5 * (t_list[r] + t_list[r + 1])
            for i in range(segs):
                i_next = (i + 1) % segs
                v1, v2 = outer_rings[r][i], outer_rings[r][i_next]
                v3, v4 = outer_rings[r + 1][i_next], outer_rings[r + 1][i]
                verts = (v1, v2, v3, v4) if sign > 0 else (v2, v1, v4, v3)
                face = bm.faces.new(verts)
                face.material_index = 1 if t_mid > 0.908 else (2 if t_mid >= 0.895 else 0)

        for i in range(segs):
            i_next = (i + 1) % segs
            v1, v2 = outer_rings[-1][i], outer_rings[-1][i_next]
            v3, v4 = inner_rings[0][i_next], inner_rings[0][i]
            verts = (v1, v2, v3, v4) if sign > 0 else (v2, v1, v4, v3)
            face = bm.faces.new(verts)
            face.material_index = 1

        for r in range(len(inner_rings) - 1):
            for i in range(segs):
                i_next = (i + 1) % segs
                v1, v2 = inner_rings[r][i], inner_rings[r][i_next]
                v3, v4 = inner_rings[r + 1][i_next], inner_rings[r + 1][i]
                verts = (v1, v2, v3, v4) if sign > 0 else (v2, v1, v4, v3)
                face = bm.faces.new(verts)
                face.material_index = 1

        bm.to_mesh(mesh)
        bm.free()

        for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
            obj.data.materials.append(m)

        b_sh = f"mixamorig:{side}Shoulder"
        b_arm = f"mixamorig:{side}Arm"
        b_fore = f"mixamorig:{side}ForeArm"
        b_hand = f"mixamorig:{side}Hand"
        s_tag = "l" if side == "Left" else "r"
        b_slv1 = f"cloth_sleeve_{s_tag}_01"
        b_slv2 = f"cloth_sleeve_{s_tag}_02"

        for v_idx, (t_val, s_vert) in enumerate(vert_meta):
            # Only the hanging lower bell (s_vert < -0.15) drapes with cloth_sleeve_*;
            # the sleeve tube around the arm stays locked to Arm/ForeArm.
            hang_factor = max(0.0, min(1.0, (-0.15 - s_vert) / 0.75)) * max(0.0, min(1.0, (t_val - 0.18) / 0.28))
            w_cloth = 0.88 * hang_factor
            w_main = 1.0 - w_cloth

            if t_val < 0.20:
                u = t_val / 0.20
                w_sp2 = 0.60 * (1.0 - u)
                w_sh = 0.40 * (1.0 - u) + 0.35 * u * (1.0 - u)
                w_ar = max(0.0, 1.0 - w_sp2 - w_sh)
                add_vertex_weight(obj, "mixamorig:Spine2", v_idx, w_main * w_sp2)
                add_vertex_weight(obj, b_sh, v_idx, w_main * w_sh)
                add_vertex_weight(obj, b_arm, v_idx, w_main * w_ar)
            elif t_val < 0.55:
                u = (t_val - 0.20) / 0.35
                add_vertex_weight(obj, b_arm, v_idx, w_main * (1.0 - u))
                add_vertex_weight(obj, b_fore, v_idx, w_main * u)
            else:
                u = min(1.0, (t_val - 0.55) / 0.45)
                add_vertex_weight(obj, b_fore, v_idx, w_main * (1.0 - 0.25 * u))
                add_vertex_weight(obj, b_hand, v_idx, w_main * (0.25 * u))

            if w_cloth > 0.0:
                w_s2 = max(0.0, min(1.0, (-s_vert - 0.25) / 0.65))
                w_s1 = 1.0 - w_s2
                add_vertex_weight(obj, b_slv1, v_idx, w_cloth * w_s1)
                add_vertex_weight(obj, b_slv2, v_idx, w_cloth * w_s2)

        # Already a closed 2-sided cuff shell -> do NOT Solidify again; keep exact analytical normals
        finalize_garment_part(obj, thickness=0.0, recalc_normals=False)
        created.append(obj)

    return created


def build_refined_waist_sash(mats: dict) -> bpy.types.Object:
    mesh = bpy.data.meshes.new("06_Dai_Lung_Va_No_Mesh")
    obj = bpy.data.objects.new("06_Dai_Lung_Va_No", mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    vert_weights_info = []

    # 1. Main Waist Belt
    v_belt = [0.000, 0.120, 0.500, 0.880, 1.000]
    belt_segs = 32
    z_c = PROPORTIONS["waist_z"]
    belt_half_h = 0.027

    b_verts = []
    for v in v_belt:
        z = (z_c - belt_half_h) + v * (2.0 * belt_half_h)
        arch = 0.0018 * math.sin(v * math.pi)
        row = []
        for i in range(belt_segs):
            phi = (i / belt_segs) * 2.0 * math.pi
            pt = eval_body_point(z_c, phi, radial_offset=0.0085 + arch)
            row.append(bm.verts.new((pt[0], pt[1], z)))
            vert_weights_info.append(("belt", v))
        b_verts.append(row)

    for r in range(len(v_belt) - 1):
        v_mid = 0.5 * (v_belt[r] + v_belt[r + 1])
        for i in range(belt_segs):
            i_next = (i + 1) % belt_segs
            face = bm.faces.new((
                b_verts[r][i],
                b_verts[r][i_next],
                b_verts[r + 1][i_next],
                b_verts[r + 1][i],
            ))
            face.material_index = 2 if (v_mid < 0.12 or v_mid > 0.88) else 0

    # 2. Two Flowing Waist Ribbons
    knot_phi = -0.44
    knot_pt = eval_body_point(z_c, knot_phi, radial_offset=0.013)
    u_ribbon = [0.000, 0.120, 0.500, 0.880, 1.000]

    for tail_idx, (phi_base, z_bot) in enumerate([(-0.465, 0.42), (-0.405, 0.46)]):
        rows_len = 12
        ribbon_half_w = 0.019
        tail_grid = []

        for r in range(rows_len + 1):
            t = r / rows_len
            z = (z_c - 0.008) * (1.0 - t) + z_bot * t
            base_pt = eval_body_point(z, phi_base, radial_offset=0.011 + 0.004 * tail_idx)
            wid_scale = 1.0 + 0.18 * t

            row = []
            for u in u_ribbon:
                centered_u = u - 0.5
                offset_x = centered_u * 2.0 * ribbon_half_w * wid_scale
                vx = base_pt[0] + offset_x * math.cos(phi_base)
                vy = base_pt[1] + offset_x * math.sin(phi_base)
                row.append(bm.verts.new((vx, vy, z)))
                vert_weights_info.append((f"tail_{tail_idx}", t))
            tail_grid.append(row)

        for r in range(rows_len):
            for c in range(len(u_ribbon) - 1):
                u_mid = 0.5 * (u_ribbon[c] + u_ribbon[c + 1])
                face = bm.faces.new((
                    tail_grid[r][c],
                    tail_grid[r][c + 1],
                    tail_grid[r + 1][c + 1],
                    tail_grid[r + 1][c],
                ))
                is_trim = (u_mid < 0.125) or (u_mid > 0.875) or (r == rows_len - 1)
                face.material_index = 2 if is_trim else 0

    # 3. Sculpted Waist Bow
    for loop_sign in (-1.0, 1.0):
        loop_u = 8
        loop_grid = []
        for r in range(loop_u + 1):
            t = r / loop_u
            angle = t * math.pi
            reach = 0.048 * math.sin(angle)
            drop = -0.012 * math.sin(angle) - 0.008 * (t ** 2)
            thick_y = -0.007 * math.sin(angle)

            row = []
            for u in u_ribbon:
                w = (u - 0.5) * 0.022 * (0.50 + 0.50 * math.sin(angle))
                vx = knot_pt[0] + loop_sign * reach
                vy = knot_pt[1] + thick_y - 0.005
                vz = knot_pt[2] + drop + w
                row.append(bm.verts.new((vx, vy, vz)))
                vert_weights_info.append(("bow", t))
            loop_grid.append(row)

        for r in range(loop_u):
            for c in range(len(u_ribbon) - 1):
                u_mid = 0.5 * (u_ribbon[c] + u_ribbon[c + 1])
                face = bm.faces.new((
                    loop_grid[r][c],
                    loop_grid[r][c + 1],
                    loop_grid[r + 1][c + 1],
                    loop_grid[r + 1][c],
                ))
                face.material_index = 2 if (u_mid < 0.125 or u_mid > 0.875) else 0

    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)

    for v_idx, (part_type, param_t) in enumerate(vert_weights_info):
        if part_type in ("belt", "bow"):
            add_vertex_weight(obj, "mixamorig:Hips", v_idx, 0.65)
            add_vertex_weight(obj, "mixamorig:Spine", v_idx, 0.35)
        elif part_type in ("tail_0", "tail_1"):
            b1 = "cloth_ribbon_r" if part_type == "tail_0" else "cloth_ribbon_l"
            b2 = "cloth_ribbon_r_02" if part_type == "tail_0" else "cloth_ribbon_l_02"
            w_hips = max(0.0, 1.0 - param_t * 5.0)
            w_rem = 1.0 - w_hips
            add_vertex_weight(obj, "mixamorig:Hips", v_idx, w_hips)
            if param_t < 0.5:
                w1 = 1.0 - max(0.0, (param_t - 0.32) / 0.36)
                add_vertex_weight(obj, b1, v_idx, w_rem * w1)
                add_vertex_weight(obj, b2, v_idx, w_rem * (1.0 - w1))
            else:
                w2 = min(1.0, 0.5 + (param_t - 0.50) / 0.36)
                add_vertex_weight(obj, b1, v_idx, w_rem * (1.0 - w2))
                add_vertex_weight(obj, b2, v_idx, w_rem * w2)

    finalize_garment_part(obj, thickness=0.0022)
    return obj


# ==============================================================================
# 9. MAIN BUILDER: MERGE & RIG TO ENGINE-READY MIXAMO STRUCTURE
# ==============================================================================

def build_engine_ready_giao_linh():
    clean_existing_setup()

    arm_obj = build_mixamo_and_cloth_armature()
    mats = create_garment_materials()

    parts = []
    parts.append(build_inner_trung_y_collar(mats))
    parts.append(build_upper_giao_linh_bodice(mats))
    parts.extend(build_all_split_skirt_panels(mats))
    parts.extend(build_refined_wide_sleeves(mats))
    parts.append(build_refined_waist_sash(mats))

    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[1]
    bpy.ops.object.join()

    robe_obj = bpy.context.view_layer.objects.active
    robe_obj.name = "Wuxia_AoTienHiep_Mesh"
    robe_obj.data.name = "OuterRobeMesh"

    for bone_name in ALL_BONE_NAMES:
        if bone_name not in robe_obj.vertex_groups:
            robe_obj.vertex_groups.new(name=bone_name)

    bpy.ops.object.mode_set(mode='WEIGHT_PAINT')
    bpy.ops.object.vertex_group_normalize_all(lock_active=False)
    bpy.ops.object.vertex_group_limit_total(group_select_mode='ALL', limit=4)
    bpy.ops.object.mode_set(mode='OBJECT')

    robe_obj.parent = arm_obj
    arm_mod = robe_obj.modifiers.new(name="Armature", type='ARMATURE')
    arm_mod.object = arm_obj
    arm_mod.use_vertex_groups = True

    robe_obj.data.calc_loop_triangles()
    if hasattr(robe_obj.data, "calc_tangents"):
        try:
            robe_obj.data.calc_tangents()
        except Exception:
            pass

    bpy.ops.object.select_all(action='DESELECT')
    arm_obj.select_set(True)
    robe_obj.select_set(True)
    bpy.context.view_layer.objects.active = arm_obj

    tri_count = len(robe_obj.data.loop_triangles)
    print("==================================================================")
    print("HOÀN TẤT: Áo Giao Lĩnh tối ưu Low-Poly + Chuẩn 2 mặt & Mixamo!")
    print(f"- Armature gốc : {arm_obj.name} ({len(arm_obj.data.bones)} bones)")
    print(f"- Mesh đầu ra  : {robe_obj.name} (Data: {robe_obj.data.name})")
    print(f"- Tổng số đỉnh : {len(robe_obj.data.vertices)} vertices | {tri_count} triangles")
    print("==================================================================")


if __name__ == "__main__":
    build_engine_ready_giao_linh()
