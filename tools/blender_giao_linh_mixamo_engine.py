"""
blender_giao_linh_mixamo_engine.py

Procedural Traditional Cross-Collar Wide-Sleeve Robe (Áo Giao Lĩnh - Hữu Nhậm)
100% Compatible with Custom C99 / Raylib / Vulkan Engine (character_model.c + spring_bone.c).
- Zero arm/shoulder poke-through across full swing & wind range
- Overlapping side-slit hip panels to prevent thigh/leg clipping
- Exact 40-bone Mixamo + Secondary Cloth Hierarchy & Bind-Pose parity
"""

import bpy
import bmesh
import math
from mathutils import Vector


# ==============================================================================
# 1. CONFIGURATION & PALETTE
# ==============================================================================

COLOR_PALETTE = {
    "primary_indigo_teal": (0.215, 0.375, 0.475, 1.0),   # #376079 - Thanh Lam trầm
    "shadow_indigo":       (0.118, 0.218, 0.285, 1.0),   # #1E3849 - Chàm tối
    "highlight_steel":     (0.357, 0.525, 0.620, 1.0),   # #5B869E - Ánh lụa sáng
    "collar_lining_ivory": (0.855, 0.832, 0.780, 1.0),   # #DAD4C7 - Ngà cổ & viền
    "inner_shirt_cream":   (0.910, 0.892, 0.850, 1.0),   # #E8E3D9 - Trung Y kem
    "gold_embroidery":     (0.795, 0.720, 0.555, 1.0),   # #CBB88E - Kim tuyến
}

PROPORTIONS = {
    "hem_z": 0.040,
    "slit_top_z": 0.995,
    "waist_z": 1.020,
    "chest_z": 1.270,
    "collar_cross_z": 1.355,
    "shoulder_z": 1.380,
    "neck_z": 1.480,
    "inner_neck_z": 1.496,
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
    # Secondary Bones - Sleeves (Ống tay áo trái / phải)
    "cloth_sleeve_l_01",
    "cloth_sleeve_l_02",
    "cloth_sleeve_r_01",
    "cloth_sleeve_r_02",
    # Secondary Bones - Front Skirt (Tà áo trước)
    "cloth_skirt_f_l",
    "cloth_skirt_f_l_02",
    "cloth_skirt_f_r",
    "cloth_skirt_f_r_02",
    "cloth_skirt_f_c_01",
    "cloth_skirt_f_c_02",
    # Secondary Bones - Back Skirt (Tà áo sau)
    "cloth_skirt_b_l",
    "cloth_skirt_b_l_02",
    "cloth_skirt_b_r",
    "cloth_skirt_b_r_02",
    "cloth_skirt_b_c_01",
    "cloth_skirt_b_c_02",
    # Secondary Bones - Waist Ribbons (Dải lụa đai lưng)
    "cloth_ribbon_l",
    "cloth_ribbon_l_02",
    "cloth_ribbon_r",
    "cloth_ribbon_r_02",
]


# ==============================================================================
# 2. MATH, SCENE & SKINNING HELPERS
# ==============================================================================

def smoothstep(edge0: float, edge1: float, x: float) -> float:
    t = max(0.0, min(1.0, (x - edge0) / max(1e-6, edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def smootherstep(edge0: float, edge1: float, x: float) -> float:
    t = max(0.0, min(1.0, (x - edge0) / max(1e-6, edge1 - edge0)))
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def clean_existing_setup():
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


def init_all_bone_vertex_groups(obj: bpy.types.Object):
    for b_name in ALL_BONE_NAMES:
        if obj.vertex_groups.get(b_name) is None:
            obj.vertex_groups.new(name=b_name)


def assign_weights_strict4(obj: bpy.types.Object, v_idx: int, weight_pairs: list):
    merged = {}
    for b_name, w in weight_pairs:
        if w > 0.0001:
            merged[b_name] = merged.get(b_name, 0.0) + float(w)
    if not merged:
        return
    top4 = sorted(merged.items(), key=lambda kv: kv[1], reverse=True)[:4]
    total = sum(w for _, w in top4)
    if total <= 1e-6:
        return
    for b_name, w in top4:
        vg = obj.vertex_groups.get(b_name)
        if vg is None:
            vg = obj.vertex_groups.new(name=b_name)
        vg.add([v_idx], w / total, 'REPLACE')


def apply_subsurf_and_smooth(obj: bpy.types.Object, subsurf_levels: int = 1):
    for poly in obj.data.polygons:
        poly.use_smooth = True

    if subsurf_levels > 0:
        sub = obj.modifiers.new(name="Subdivision", type="SUBSURF")
        sub.levels = subsurf_levels
        sub.render_levels = subsurf_levels
        sub.uv_smooth = 'PRESERVE_CORNERS'
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=sub.name)


# ==============================================================================
# 3. C2-CONTINUOUS ANATOMICAL TORSO & A-LINE DRAPE SURFACE MATH
# ==============================================================================

def get_body_profile(z: float):
    hem_z = PROPORTIONS["hem_z"]
    waist_z = PROPORTIONS["waist_z"]
    chest_z = PROPORTIONS["chest_z"]
    shoulder_z = PROPORTIONS["shoulder_z"]
    neck_z = PROPORTIONS["neck_z"]

    rx_waist, ry_waist, y_waist = 0.148, 0.116, 0.000
    rx_chest, ry_chest, y_chest = 0.200, 0.130, -0.002
    rx_shldr, ry_shldr, y_shldr = 0.194, 0.116, 0.000
    rx_neck,  ry_neck,  y_neck  = 0.068, 0.060, 0.004

    if z <= waist_z:
        s = max(0.0, min(1.0, (waist_z - z) / (waist_z - hem_z)))
        flare = (s ** 1.08) * (1.0 - 0.06 * (1.0 - smootherstep(0.0, 0.25, s)))
        rx = rx_waist + 0.300 * flare
        ry = ry_waist + 0.210 * flare
        y_shift = y_waist
    elif z <= chest_z:
        t = smootherstep(waist_z, chest_z, z)
        smooth_blouse = math.sin(t * math.pi) * 0.0025
        rx = rx_waist + (rx_chest - rx_waist) * t + smooth_blouse * 0.5
        ry = ry_waist + (ry_chest - ry_waist) * t + smooth_blouse
        y_shift = y_waist + (y_chest - y_waist) * t
    elif z <= shoulder_z:
        t = smootherstep(chest_z, shoulder_z, z)
        rx = rx_chest + (rx_shldr - rx_chest) * t
        ry = ry_chest + (ry_shldr - ry_chest) * t
        y_shift = y_chest + (y_shldr - y_chest) * t
    else:
        t = smootherstep(shoulder_z, neck_z, z)
        rx = rx_shldr + (rx_neck - rx_shldr) * t
        ry = ry_shldr + (ry_neck - ry_shldr) * t
        y_shift = y_shldr + (y_neck - y_shldr) * t

    return rx, ry, y_shift


def eval_body_point(z: float, phi: float, radial_offset: float = 0.0, x_flare_extra: float = 0.0):
    rx, ry, y_shift = get_body_profile(z)
    eff_rx = max(0.035, rx + radial_offset + x_flare_extra)
    eff_ry = max(0.035, ry + radial_offset)
    x = eff_rx * math.sin(phi)
    y = -eff_ry * math.cos(phi) + y_shift
    return (x, y, z)


def eval_body_frame(z: float, phi: float, radial_offset: float = 0.0):
    p = Vector(eval_body_point(z, phi, radial_offset))
    rx, ry, _ = get_body_profile(z)
    t_phi = Vector((rx * math.cos(phi), ry * math.sin(phi), 0.0)).normalized()
    n_out = Vector((ry * math.sin(phi), -rx * math.cos(phi), 0.0)).normalized()
    return p, t_phi, n_out


# ==============================================================================
# 4. MIXAMO ARMATURE + SECONDARY CLOTH BONES (EXACT ENGINE BIND POSE)
# ==============================================================================

def build_mixamo_and_cloth_armature() -> bpy.types.Object:
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

    # 1. TRỤC XƯƠNG SỐNG & ĐẦU
    make_bone("mixamorig:Hips",   (0.0, 0.0, 0.92), (0.0, 0.0, 1.02))
    make_bone("mixamorig:Spine",  (0.0, 0.0, 1.02), (0.0, 0.0, 1.15), "mixamorig:Hips", True)
    make_bone("mixamorig:Spine1", (0.0, 0.0, 1.15), (0.0, 0.0, 1.28), "mixamorig:Spine", True)
    make_bone("mixamorig:Spine2", (0.0, 0.0, 1.28), (0.0, 0.0, 1.40), "mixamorig:Spine1", True)
    make_bone("mixamorig:Neck",   (0.0, 0.0, 1.40), (0.0, 0.0, 1.50), "mixamorig:Spine2", True)
    make_bone("mixamorig:Head",   (0.0, 0.0, 1.50), (0.0, 0.0, 1.70), "mixamorig:Neck", True)

    # 2. TAY TRÁI (+X) & TAY PHẢI (-X)
    make_bone("mixamorig:LeftShoulder", (0.04, 0.0, 1.34), (0.13, 0.0, 1.28), "mixamorig:Spine2", False)
    make_bone("mixamorig:LeftArm",      (0.13, 0.0, 1.28), (0.30, 0.0, 1.08), "mixamorig:LeftShoulder", True)
    make_bone("mixamorig:LeftForeArm",  (0.30, 0.0, 1.08), (0.45, 0.0, 0.88), "mixamorig:LeftArm", True)
    make_bone("mixamorig:LeftHand",     (0.45, 0.0, 0.88), (0.55, 0.0, 0.76), "mixamorig:LeftForeArm", True)

    make_bone("mixamorig:RightShoulder", (-0.04, 0.0, 1.34), (-0.13, 0.0, 1.28), "mixamorig:Spine2", False)
    make_bone("mixamorig:RightArm",      (-0.13, 0.0, 1.28), (-0.30, 0.0, 1.08), "mixamorig:RightShoulder", True)
    make_bone("mixamorig:RightForeArm",  (-0.30, 0.0, 1.08), (-0.45, 0.0, 0.88), "mixamorig:RightArm", True)
    make_bone("mixamorig:RightHand",     (-0.45, 0.0, 0.88), (-0.55, 0.0, 0.76), "mixamorig:RightForeArm", True)

    # 3. CHÂN TRÁI (+X) & CHÂN PHẢI (-X)
    make_bone("mixamorig:LeftUpLeg", (0.09, 0.0, 0.90), (0.09, 0.0, 0.48), "mixamorig:Hips", False)
    make_bone("mixamorig:LeftLeg",   (0.09, 0.0, 0.48), (0.09, 0.0, 0.08), "mixamorig:LeftUpLeg", True)
    make_bone("mixamorig:LeftFoot",  (0.09, 0.0, 0.08), (0.09, -0.13, 0.02), "mixamorig:LeftLeg", True)

    make_bone("mixamorig:RightUpLeg", (-0.09, 0.0, 0.90), (-0.09, 0.0, 0.48), "mixamorig:Hips", False)
    make_bone("mixamorig:RightLeg",   (-0.09, 0.0, 0.48), (-0.09, 0.0, 0.08), "mixamorig:RightUpLeg", True)
    make_bone("mixamorig:RightFoot",  (-0.09, 0.0, 0.08), (-0.09, -0.13, 0.02), "mixamorig:RightLeg", True)

    # 4. XƯƠNG PHỤ ỐNG TAY ÁO QUẢNG TỤ
    make_bone("cloth_sleeve_l_01", (0.30, 0.0, 1.04), (0.34, 0.0, 0.78), "mixamorig:LeftForeArm", False)
    make_bone("cloth_sleeve_l_02", (0.34, 0.0, 0.78), (0.38, 0.0, 0.55), "cloth_sleeve_l_01", True)

    make_bone("cloth_sleeve_r_01", (-0.30, 0.0, 1.04), (-0.34, 0.0, 0.78), "mixamorig:RightForeArm", False)
    make_bone("cloth_sleeve_r_02", (-0.34, 0.0, 0.78), (-0.38, 0.0, 0.55), "cloth_sleeve_r_01", True)

    # 5. XƯƠNG PHỤ TÀ ÁO TRƯỚC
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

    # 6. XƯƠNG PHỤ TÀ ÁO SAU
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

    # 7. XƯƠNG PHỤ DẢI LỤA ĐAI LƯNG
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
# 5. NON-UNIFORM SUPPORT LOOP SCHEDULES & SEAMLESS PARAMETRIC UV
# ==============================================================================

def make_collar_u_schedule(body_segs: int = 44) -> list:
    left_collar = [0.000, 0.004, 0.016, 0.030, 0.043]
    left_gold = [0.045, 0.049]
    right_gold = [1.0 - x for x in reversed(left_gold)]
    right_collar = [1.0 - x for x in reversed(left_collar)]

    u0, u1 = 0.053, 0.947
    body = [u0 + (u1 - u0) * (i / body_segs) for i in range(body_segs + 1)]
    return left_collar + left_gold + body + right_gold + right_collar


def make_panel_u_schedule(mid_segs: int = 28) -> list:
    left_edge = [0.000, 0.003, 0.010, 0.017, 0.019, 0.023]
    right_edge = [1.0 - x for x in reversed(left_edge)]
    u0, u1 = 0.027, 0.973
    mid = [u0 + (u1 - u0) * (i / mid_segs) for i in range(mid_segs + 1)]
    return left_edge + mid + right_edge


def make_skirt_v_schedule(mid_rings: int = 22) -> list:
    hem_edge = [0.000, 0.003, 0.010, 0.014, 0.016, 0.022]
    v0, v1 = 0.027, 0.992
    mid = [v0 + (v1 - v0) * (r / mid_rings) for r in range(mid_rings + 1)]
    return hem_edge + mid + [0.997, 1.000]


def build_grid_faces_with_uv(
    bm: bmesh.types.BMesh,
    ring_verts: list,
    u_list: list,
    v_list: list,
    uv_rect: tuple,
    mat_fn,
):
    uv_layer = bm.loops.layers.uv.verify()
    u0_uv, v0_uv, u1_uv, v1_uv = uv_rect
    rings = len(ring_verts) - 1
    segs = len(u_list) - 1

    for r in range(rings):
        va, vb = v_list[r], v_list[r + 1]
        v_mid = 0.5 * (va + vb)
        for i in range(segs):
            ua, ub = u_list[i], u_list[i + 1]
            u_mid = 0.5 * (ua + ub)
            face = bm.faces.new((
                ring_verts[r][i],
                ring_verts[r][i + 1],
                ring_verts[r + 1][i + 1],
                ring_verts[r + 1][i],
            ))
            face.smooth = True
            face.material_index = mat_fn(u_mid, v_mid)
            uvs = [
                (u0_uv + ua * (u1_uv - u0_uv), v0_uv + va * (v1_uv - v0_uv)),
                (u0_uv + ub * (u1_uv - u0_uv), v0_uv + va * (v1_uv - v0_uv)),
                (u0_uv + ub * (u1_uv - u0_uv), v0_uv + vb * (v1_uv - v0_uv)),
                (u0_uv + ua * (u1_uv - u0_uv), v0_uv + vb * (v1_uv - v0_uv)),
            ]
            for loop, uv in zip(face.loops, uvs):
                loop[uv_layer].uv = uv


# ==============================================================================
# 6. PBR MATERIALS
# ==============================================================================

def create_garment_materials() -> dict:
    mats = {}

    # 1. Teal Cloud-Damask Silk
    mat_teal = bpy.data.materials.get("MAT_Teal_Cloud_Damask") or bpy.data.materials.new("MAT_Teal_Cloud_Damask")
    mat_teal.use_nodes = True
    mat_teal.use_backface_culling = False
    nodes = mat_teal.node_tree.nodes
    links = mat_teal.node_tree.links
    nodes.clear()

    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")

    tex_coord = nodes.new("ShaderNodeTexCoord")
    wave = nodes.new("ShaderNodeTexWave")
    wave.wave_type = 'RINGS'
    wave.rings_direction = 'Z'
    wave.inputs["Scale"].default_value = 18.0
    wave.inputs["Distortion"].default_value = 6.0
    wave.inputs["Detail"].default_value = 2.5

    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.22
    ramp.color_ramp.elements[0].color = (0.165, 0.305, 0.395, 1.0)
    ramp.color_ramp.elements[1].position = 0.78
    ramp.color_ramp.elements[1].color = COLOR_PALETTE["primary_indigo_teal"]

    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.025
    bump.inputs["Distance"].default_value = 0.0015

    set_bsdf_input(bsdf, ["Base Color"], COLOR_PALETTE["primary_indigo_teal"])
    set_bsdf_input(bsdf, ["Roughness"], 0.38)
    set_bsdf_input(bsdf, ["Sheen Weight", "Sheen"], 0.60)
    set_bsdf_input(bsdf, ["Sheen Tint"], COLOR_PALETTE["highlight_steel"])

    links.new(tex_coord.outputs["UV"], wave.inputs["Vector"])
    links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    links.new(wave.outputs["Fac"], bump.inputs["Height"])
    links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    mats["teal"] = mat_teal

    # 2. Ivory Silk Collar & Cuff Trim
    mat_ivory = bpy.data.materials.get("MAT_Ivory_Silk_Trim") or bpy.data.materials.new("MAT_Ivory_Silk_Trim")
    mat_ivory.use_nodes = True
    mat_ivory.use_backface_culling = False
    bsdf_ivory = mat_ivory.node_tree.nodes.get("Principled BSDF")
    if bsdf_ivory:
        set_bsdf_input(bsdf_ivory, ["Base Color"], COLOR_PALETTE["collar_lining_ivory"])
        set_bsdf_input(bsdf_ivory, ["Roughness"], 0.32)
        set_bsdf_input(bsdf_ivory, ["Sheen Weight", "Sheen"], 0.45)
    mats["ivory"] = mat_ivory

    # 3. Inner Undershirt (Trung Y) Pure Soft Cream
    mat_inner = bpy.data.materials.get("MAT_Inner_Trung_Y_Silk") or bpy.data.materials.new("MAT_Inner_Trung_Y_Silk")
    mat_inner.use_nodes = True
    mat_inner.use_backface_culling = False
    bsdf_inner = mat_inner.node_tree.nodes.get("Principled BSDF")
    if bsdf_inner:
        set_bsdf_input(bsdf_inner, ["Base Color"], COLOR_PALETTE["inner_shirt_cream"])
        set_bsdf_input(bsdf_inner, ["Roughness"], 0.36)
        set_bsdf_input(bsdf_inner, ["Sheen Weight", "Sheen"], 0.35)
    mats["inner_cream"] = mat_inner

    # 4. Gold Brocade & Piping Accent
    mat_gold = bpy.data.materials.get("MAT_Gold_Brocade_Accent") or bpy.data.materials.new("MAT_Gold_Brocade_Accent")
    mat_gold.use_nodes = True
    mat_gold.use_backface_culling = False
    bsdf_gold = mat_gold.node_tree.nodes.get("Principled BSDF")
    if bsdf_gold:
        set_bsdf_input(bsdf_gold, ["Base Color"], COLOR_PALETTE["gold_embroidery"])
        set_bsdf_input(bsdf_gold, ["Metallic"], 0.62)
        set_bsdf_input(bsdf_gold, ["Roughness"], 0.26)
    mats["gold"] = mat_gold

    return mats


# ==============================================================================
# 7. WEIGHT PAINTING HELPER FUNCTIONS
# ==============================================================================

def assign_torso_weights(obj: bpy.types.Object):
    init_all_bone_vertex_groups(obj)
    for v in obj.data.vertices:
        x, y, z = v.co
        weights = []
        if z < 1.03:
            w_hips = max(0.0, (1.03 - z) / 0.06)
            weights.append(("mixamorig:Hips", w_hips))
            weights.append(("mixamorig:Spine", 1.0 - w_hips))
        elif z < 1.15:
            t = (z - 1.03) / 0.12
            weights.append(("mixamorig:Spine", 1.0 - t))
            weights.append(("mixamorig:Spine1", t))
        elif z < 1.28:
            t = (z - 1.15) / 0.13
            weights.append(("mixamorig:Spine1", 1.0 - t))
            weights.append(("mixamorig:Spine2", t))
        else:
            t_neck = max(0.0, min(1.0, (z - 1.38) / 0.10))
            weights.append(("mixamorig:Spine2", 1.0 - 0.70 * t_neck))
            weights.append(("mixamorig:Neck", 0.70 * t_neck))

        if z > 1.20 and abs(x) > 0.07:
            w_sh = min(0.55, ((abs(x) - 0.07) / 0.12) * max(0.0, min(1.0, (z - 1.20) / 0.15)))
            sh_bone = "mixamorig:LeftShoulder" if x > 0 else "mixamorig:RightShoulder"
            weights.append((sh_bone, w_sh))

        assign_weights_strict4(obj, v.index, weights)


def assign_skirt_panel_weights(obj: bpy.types.Object, is_front: bool):
    init_all_bone_vertex_groups(obj)
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

        w_hips = max(0.0, 1.0 - t_down * 4.5)
        w_cloth = 1.0 - w_hips
        weights = []

        if w_hips > 0.0:
            weights.append(("mixamorig:Hips", w_hips))

        if w_cloth > 0.0:
            w_seg2 = smoothstep(0.32, 0.72, t_down)
            w_seg1 = 1.0 - w_seg2

            rx, _, _ = get_body_profile(z)
            nx = max(-1.0, min(1.0, x / max(0.12, rx)))

            if nx >= 0.0:
                w_side = smoothstep(0.0, 0.85, nx)
                w_mid = 1.0 - w_side
                weights.append((b_left_1, w_cloth * w_side * w_seg1))
                weights.append((b_left_2, w_cloth * w_side * w_seg2))
            else:
                w_side = smoothstep(0.0, 0.85, -nx)
                w_mid = 1.0 - w_side
                weights.append((b_right_1, w_cloth * w_side * w_seg1))
                weights.append((b_right_2, w_cloth * w_side * w_seg2))

            weights.append((b_center_1, w_cloth * w_mid * w_seg1))
            weights.append((b_center_2, w_cloth * w_mid * w_seg2))

        assign_weights_strict4(obj, v.index, weights)


# ==============================================================================
# 8. GARMENT PART BUILDERS
# ==============================================================================

def build_inner_trung_y_collar(mats: dict) -> bpy.types.Object:
    mesh = bpy.data.meshes.new("01_Co_Ao_Lot_Trung_Y_Trong_Mesh")
    obj = bpy.data.objects.new("01_Co_Ao_Lot_Trung_Y_Trong", mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    rings = 14
    segs = 44
    z_bot = 1.275
    z_top = PROPORTIONS["inner_neck_z"]

    u_list = [i / segs for i in range(segs + 1)]
    v_list = [r / rings for r in range(rings + 1)]

    ring_verts = []
    for r, v in enumerate(v_list):
        t_top = 1.0 - v
        phi_start = 0.13 - 0.54 * (t_top ** 0.88)
        phi_end = (2.0 * math.pi - 0.13) + 0.54 * (t_top ** 0.88)
        base_inset = -0.0085 * (1.0 - 0.52 * (v ** 2))

        row = []
        for i, u in enumerate(u_list):
            phi = phi_start + u * (phi_end - phi_start)
            front_dip = 0.018 * (max(0.0, math.cos(phi)) ** 1.4) * (v ** 1.5)
            z = (z_bot + v * (z_top - z_bot)) - front_dip
            overlap_shift = 0.0018 * math.cos(math.pi * u)
            pt = eval_body_point(
                min(z, PROPORTIONS["neck_z"]),
                phi,
                radial_offset=base_inset + overlap_shift,
            )
            row.append(bm.verts.new((pt[0], pt[1], z)))
        ring_verts.append(row)

    build_grid_faces_with_uv(
        bm, ring_verts, u_list, v_list,
        uv_rect=(0.02, 0.78, 0.28, 0.98),
        mat_fn=lambda u, v: 3,
    )

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)

    assign_torso_weights(obj)
    apply_subsurf_and_smooth(obj, subsurf_levels=1)
    return obj


def build_upper_giao_linh_bodice(mats: dict) -> bpy.types.Object:
    mesh = bpy.data.meshes.new("02_Than_Tren_Giao_Linh_Mesh")
    obj = bpy.data.objects.new("02_Than_Tren_Giao_Linh", mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    u_list = make_collar_u_schedule(body_segs=44)

    mid_rings = 22
    v_mid = [0.0, 0.010] + [0.02 + 0.905 * (r / mid_rings) for r in range(mid_rings + 1)]
    v_top_collar = [0.928, 0.934, 0.938, 0.962, 0.992, 1.000]
    v_list = v_mid + v_top_collar

    z_min = 1.000
    z_max = PROPORTIONS["neck_z"]
    z_cross = PROPORTIONS["collar_cross_z"]

    ring_verts = []
    for r, v in enumerate(v_list):
        t_top = 1.0 - v
        phi_start = 0.20 - 0.95 * t_top + 0.28 * (t_top ** 2)
        phi_end = (2.0 * math.pi - 0.20) + 0.89 * t_top - 0.26 * (t_top ** 2)

        row = []
        for i, u in enumerate(u_list):
            phi = phi_start + u * (phi_end - phi_start)
            front_cos = max(0.0, math.cos(phi))
            neck_slope = (z_max - z_cross) * (front_cos ** 1.08) * (v ** 1.75)
            z = (z_min + v * (z_max - z_min)) - neck_slope

            is_turned_hem = (u <= 0.001) or (u >= 0.999) or (v >= 0.999)
            hem_tuck = -0.0022 if is_turned_hem else 0.0

            layer_offset = 0.0048 * math.cos(math.pi * u)
            is_collar_zone = (u <= 0.049) or (u >= 0.951) or (v >= 0.928)
            waist_taper = smoothstep(1.010, 1.065, z)
            collar_emboss = (0.0016 * waist_taper) if is_collar_zone else 0.0

            pt = eval_body_point(z, phi, radial_offset=layer_offset + collar_emboss + hem_tuck)
            row.append(bm.verts.new(pt))
        ring_verts.append(row)

    def bodice_mat(u_mid, v_mid):
        in_ivory = (u_mid < 0.044) or (u_mid > 0.956) or (v_mid > 0.936)
        in_gold = (0.044 <= u_mid <= 0.049) or (0.951 <= u_mid <= 0.956) or (0.928 <= v_mid <= 0.936)
        return 1 if in_ivory else (2 if in_gold else 0)

    build_grid_faces_with_uv(
        bm, ring_verts, u_list, v_list,
        uv_rect=(0.02, 0.42, 0.56, 0.76),
        mat_fn=bodice_mat,
    )

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)

    assign_torso_weights(obj)
    apply_subsurf_and_smooth(obj, subsurf_levels=1)
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
    uv_rect: tuple,
    arch_camber: float = 0.0030,
    drape_folds: int = 5,
) -> bpy.types.Object:
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)

    bm = bmesh.new()
    u_list = make_panel_u_schedule(mid_segs=28)
    v_list = make_skirt_v_schedule(mid_rings=22)

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
            fold_env = (math.sin(math.pi * u) ** 1.4) * (t_down ** 1.35)
            drape_wave = 0.0026 * math.sin(drape_folds * math.pi * u) * fold_env

            is_edge_tuck = (v <= 0.001) or (u <= 0.001) or (u >= 0.999)
            edge_tuck = -0.0022 if is_edge_tuck else 0.0

            pt = eval_body_point(z, phi, radial_offset=base_radial_offset + camber + drape_wave + edge_tuck)
            row.append(bm.verts.new(pt))
        ring_verts.append(row)

    def skirt_mat(u_mid, v_mid):
        in_ivory = (v_mid < 0.015) or (u_mid < 0.018) or (u_mid > 0.982)
        in_gold = (0.015 <= v_mid <= 0.023) or (0.018 <= u_mid <= 0.024) or (0.976 <= u_mid <= 0.982)
        return 1 if in_ivory else (2 if in_gold else 0)

    build_grid_faces_with_uv(
        bm, ring_verts, u_list, v_list,
        uv_rect=uv_rect,
        mat_fn=skirt_mat,
    )

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)

    assign_skirt_panel_weights(obj, is_front=is_front)
    apply_subsurf_and_smooth(obj, subsurf_levels=1)
    return obj


def build_all_split_skirt_panels(mats: dict) -> list:
    panels = []
    # 1. Tà Trước Ngoài (chồng lớp ~15 độ qua hông trái/phải để không lộ đùi khi bước)
    panels.append(build_split_skirt_panel(
        name="03_Ta_Truoc_Ao",
        mats=mats,
        phi_left_waist=-0.48 * math.pi,
        phi_right_waist=0.54 * math.pi,
        phi_left_hem=-0.50 * math.pi,
        phi_right_hem=0.55 * math.pi,
        base_radial_offset=0.0075,
        is_front=True,
        uv_rect=(0.02, 0.02, 0.36, 0.40),
        arch_camber=0.0035,
        drape_folds=5,
    ))
    # 2. Tà Phụ Bên Trong
    panels.append(build_split_skirt_panel(
        name="03b_Ta_Phu_Ben_Trong",
        mats=mats,
        phi_left_waist=-0.52 * math.pi,
        phi_right_waist=-0.10 * math.pi,
        phi_left_hem=-0.53 * math.pi,
        phi_right_hem=-0.13 * math.pi,
        base_radial_offset=-0.0015,
        is_front=True,
        uv_rect=(0.38, 0.02, 0.56, 0.40),
        arch_camber=0.0012,
        drape_folds=3,
    ))
    # 3. Tà Sau
    panels.append(build_split_skirt_panel(
        name="04_Ta_Sau_Ao",
        mats=mats,
        phi_left_waist=0.46 * math.pi,
        phi_right_waist=1.54 * math.pi,
        phi_left_hem=0.47 * math.pi,
        phi_right_hem=1.53 * math.pi,
        base_radial_offset=0.0005,
        is_front=False,
        uv_rect=(0.58, 0.02, 0.96, 0.40),
        arch_camber=0.0032,
        drape_folds=6,
    ))
    return panels


def build_refined_wide_sleeves(mats: dict) -> list:
    """
    Tay áo Quảng Tụ chống lộ tay hoàn toàn:
    - Cửa vai phủ trùm mỏm vai (z_top = 1.448) và dịch tâm về phía trước (-Y Blender = +Z Raylib)
      khớp với biên độ vung tay thực tế trong animation.
    - Bán kính trước–sau rộng (ry = 0.135 -> 0.172) bao trọn cả bắp tay lẫn cẳng tay.
    - Phần ống trên & mặt trước/sau bám 100% xương Arm/ForeArm của nhân vật;
      chỉ phần túi vải rủ dưới (s_vert < -0.32) mới ăn trọng số cloth_sleeve_*.
    """
    created = []
    mid_steps = 18
    t_body = [0.895 * (r / mid_steps) for r in range(mid_steps + 1)]
    t_cuff = [0.898, 0.905, 0.908, 0.945, 0.980, 0.997, 1.000]
    t_list = t_body + t_cuff
    rings = len(t_list) - 1
    segs = 40

    for side, sign in [("Left", 1.0), ("Right", -1.0)]:
        mesh = bpy.data.meshes.new(f"05_Tay_Ao_Quang_Tay_{side}_Mesh")
        obj = bpy.data.objects.new(f"05_Tay_Ao_Quang_Tay_{side}", mesh)
        bpy.context.scene.collection.objects.link(obj)
        init_all_bone_vertex_groups(obj)

        bm = bmesh.new()
        uv_layer = bm.loops.layers.uv.verify()
        vert_meta = []

        def eval_sleeve_ring(t_val: float, inset: float = 0.0):
            # Phủ trùm mỏm vai (z=1.448) và đi dọc sống tay trên tới cổ tay
            x_top = sign * (0.092 + 0.488 * t_val) - sign * inset * 0.6
            z_top = 1.448 - 0.478 * (t_val ** 1.02) - inset

            # Bụng tay áo rủ võng sâu mềm mại
            x_bot = sign * (0.135 + 0.245 * (t_val ** 0.88)) - sign * inset * 0.6
            z_bot = 1.050 - 0.525 * (t_val ** 1.04) - 0.055 * math.sin(math.pi * t_val) + inset

            cx = 0.5 * (x_top + x_bot)
            cz = 0.5 * (z_top + z_bot)
            # Dịch tâm ống tay ra phía trước (-Y Blender = +Z Raylib) để ôm trọn tay khi vung
            cy = -0.032 - 0.018 * t_val

            dx = 0.5 * (x_top - x_bot)
            dz = 0.5 * (z_top - z_bot)
            ry = max(0.02, (0.135 * (1.0 - t_val) + 0.172 * (t_val ** 0.92)) - inset)

            pts = []
            for i in range(segs):
                theta = (i / segs) * 2.0 * math.pi
                s_vert = math.sin(theta)
                c_horiz = math.cos(theta)
                width_mod = 1.0 - 0.04 * s_vert
                vx = cx + dx * s_vert
                vz = cz + dz * s_vert
                vy = cy + ry * width_mod * c_horiz
                v = bm.verts.new((vx, vy, vz))
                pts.append(v)
                vert_meta.append((t_val, s_vert))
            return pts

        outer_rings = [eval_sleeve_ring(t, inset=0.0) for t in t_list]
        t_inner_list = [1.000, 0.995, 0.955, 0.890, 0.780, 0.650, 0.500, 0.340]
        inner_rings = [eval_sleeve_ring(t, inset=0.0032) for t in t_inner_list]

        uv_u0 = 0.58 if sign > 0 else 0.78

        def add_sleeve_face(v1, v2, v3, v4, u0, u1, v0, v1_uv, mat_idx):
            verts = (v1, v2, v3, v4) if sign > 0 else (v2, v1, v4, v3)
            face = bm.faces.new(verts)
            face.smooth = True
            face.material_index = mat_idx
            uvs = (
                [(u0, v0), (u1, v0), (u1, v1_uv), (u0, v1_uv)]
                if sign > 0 else
                [(u1, v0), (u0, v0), (u0, v1_uv), (u1, v1_uv)]
            )
            for loop, uv in zip(face.loops, uvs):
                loop[uv_layer].uv = uv

        for r in range(rings):
            t_mid = 0.5 * (t_list[r] + t_list[r + 1])
            v0_uv = 0.44 + 0.34 * t_list[r]
            v1_uv = 0.44 + 0.34 * t_list[r + 1]
            mat_idx = 1 if t_mid > 0.908 else (2 if t_mid >= 0.898 else 0)
            for i in range(segs):
                i_next = (i + 1) % segs
                u0_uv = uv_u0 + 0.18 * (i / segs)
                u1_uv = uv_u0 + 0.18 * ((i + 1) / segs)
                add_sleeve_face(
                    outer_rings[r][i], outer_rings[r][i_next],
                    outer_rings[r + 1][i_next], outer_rings[r + 1][i],
                    u0_uv, u1_uv, v0_uv, v1_uv, mat_idx
                )

        for i in range(segs):
            i_next = (i + 1) % segs
            u0_uv = uv_u0 + 0.18 * (i / segs)
            u1_uv = uv_u0 + 0.18 * ((i + 1) / segs)
            add_sleeve_face(
                outer_rings[-1][i], outer_rings[-1][i_next],
                inner_rings[0][i_next], inner_rings[0][i],
                u0_uv, u1_uv, 0.78, 0.79, 1
            )

        for r in range(len(inner_rings) - 1):
            v0_uv = 0.79 + 0.16 * (r / (len(inner_rings) - 1))
            v1_uv = 0.79 + 0.16 * ((r + 1) / (len(inner_rings) - 1))
            for i in range(segs):
                i_next = (i + 1) % segs
                u0_uv = uv_u0 + 0.18 * (i / segs)
                u1_uv = uv_u0 + 0.18 * ((i + 1) / segs)
                add_sleeve_face(
                    inner_rings[r][i], inner_rings[r][i_next],
                    inner_rings[r + 1][i_next], inner_rings[r + 1][i],
                    u0_uv, u1_uv, v0_uv, v1_uv, 1
                )

        bm.normal_update()
        bm.to_mesh(mesh)
        bm.free()

        for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
            obj.data.materials.append(m)

        b_sp2 = "mixamorig:Spine2"
        b_sh = f"mixamorig:{side}Shoulder"
        b_arm = f"mixamorig:{side}Arm"
        b_fore = f"mixamorig:{side}ForeArm"
        b_hand = f"mixamorig:{side}Hand"
        s_tag = "l" if side == "Left" else "r"
        b_slv1 = f"cloth_sleeve_{s_tag}_01"
        b_slv2 = f"cloth_sleeve_{s_tag}_02"

        for v_idx, (t_val, s_vert) in enumerate(vert_meta):
            # Chỉ túi vải rủ dưới (s_vert < -0.32) mới chịu lực vật lý cloth_sleeve_*
            hang_factor = smoothstep(-0.32, -0.90, s_vert) * smoothstep(0.18, 0.42, t_val)
            w_cloth = 0.85 * hang_factor
            w_main = 1.0 - w_cloth
            weights = []

            if t_val < 0.08:
                u = t_val / 0.08
                weights.append((b_sp2, w_main * 0.45 * (1.0 - u)))
                weights.append((b_sh,  w_main * 0.55 * (1.0 - u)))
                weights.append((b_arm, w_main * u))
            elif t_val < 0.42:
                weights.append((b_arm, w_main))
            elif t_val < 0.58:
                u = smoothstep(0.42, 0.58, t_val)
                weights.append((b_arm,  w_main * (1.0 - u)))
                weights.append((b_fore, w_main * u))
            else:
                u = smoothstep(0.58, 1.00, t_val)
                weights.append((b_fore, w_main * (1.0 - 0.30 * u)))
                weights.append((b_hand, w_main * (0.30 * u)))

            if w_cloth > 0.001:
                w_s2 = smoothstep(-0.50, -0.92, s_vert)
                w_s1 = 1.0 - w_s2
                weights.append((b_slv1, w_cloth * w_s1))
                weights.append((b_slv2, w_cloth * w_s2))

            assign_weights_strict4(obj, v_idx, weights)

        apply_subsurf_and_smooth(obj, subsurf_levels=1)
        created.append(obj)

    return created


def build_refined_waist_sash(mats: dict) -> bpy.types.Object:
    mesh = bpy.data.meshes.new("06_Dai_Lung_Va_No_Mesh")
    obj = bpy.data.objects.new("06_Dai_Lung_Va_No", mesh)
    bpy.context.scene.collection.objects.link(obj)
    init_all_bone_vertex_groups(obj)

    bm = bmesh.new()
    uv_layer = bm.loops.layers.uv.verify()
    vert_weights_info = []

    v_belt = [0.000, 0.040, 0.120, 0.350, 0.650, 0.880, 0.960, 1.000]
    belt_segs = 56
    z_c = PROPORTIONS["waist_z"]
    belt_half_h = 0.027

    b_verts = []
    for v in v_belt:
        z = (z_c - belt_half_h) + v * (2.0 * belt_half_h)
        arch = 0.0018 * math.sin(v * math.pi)
        edge_tuck = -0.0014 if (v <= 0.001 or v >= 0.999) else 0.0
        row = []
        for i in range(belt_segs):
            phi = (i / belt_segs) * 2.0 * math.pi
            pt = eval_body_point(z_c, phi, radial_offset=0.0105 + arch + edge_tuck)
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
            face.smooth = True
            face.material_index = 2 if (v_mid < 0.12 or v_mid > 0.88) else 0
            for k, loop in enumerate(face.loops):
                u_uv = 0.30 + 0.26 * ((i + (1 if k in (1, 2) else 0)) / belt_segs)
                v_uv = 0.78 + 0.08 * (v_belt[r + (1 if k in (2, 3) else 0)])
                loop[uv_layer].uv = (u_uv, v_uv)

    knot_phi = -0.44
    knot_pt, knot_tan, knot_nrm = eval_body_frame(z_c, knot_phi, radial_offset=0.016)
    u_ribbon = [0.000, 0.040, 0.130, 0.500, 0.870, 0.960, 1.000]

    for tail_idx, (phi_base, z_bot) in enumerate([(-0.465, 0.42), (-0.405, 0.46)]):
        rows_len = 20
        ribbon_half_w = 0.019
        tail_grid = []

        for r in range(rows_len + 1):
            t = r / rows_len
            z = (z_c - 0.008) * (1.0 - t) + z_bot * t
            base_pt, t_phi, n_out = eval_body_frame(z, phi_base, radial_offset=0.015 + 0.004 * tail_idx)
            wid_scale = (1.0 + 0.15 * t) * (1.0 - 0.25 * smoothstep(0.88, 1.0, t))
            flutter = n_out * (0.0038 * math.sin(2.5 * math.pi * t + tail_idx) * t)

            row = []
            for u in u_ribbon:
                centered_u = u - 0.5
                offset_lat = centered_u * 2.0 * ribbon_half_w * wid_scale
                cup_nrm = n_out * (0.0014 * (1.0 - 4.0 * centered_u * centered_u))
                pos = base_pt + t_phi * offset_lat + flutter + cup_nrm
                row.append(bm.verts.new(pos))
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
                face.smooth = True
                is_trim = (u_mid < 0.13) or (u_mid > 0.87) or (r == rows_len - 1)
                face.material_index = 2 if is_trim else 0
                for k, loop in enumerate(face.loops):
                    u_uv = 0.30 + 0.12 * tail_idx + 0.10 * u_ribbon[c + (1 if k in (1, 2) else 0)]
                    v_uv = 0.87 + 0.11 * ((r + (1 if k in (2, 3) else 0)) / rows_len)
                    loop[uv_layer].uv = (u_uv, v_uv)

    for loop_sign in (-1.0, 1.0):
        loop_u = 12
        loop_grid = []
        for r in range(loop_u + 1):
            t = r / loop_u
            angle = t * math.pi
            reach_lat = loop_sign * 0.048 * math.sin(angle)
            drop_z = -0.011 * math.sin(angle) - 0.007 * (t ** 2)
            out_nrm = 0.004 + 0.008 * math.sin(angle)

            row = []
            for u in u_ribbon:
                w_z = (u - 0.5) * 0.023 * (0.50 + 0.50 * math.sin(angle))
                pos = (
                    knot_pt
                    + knot_tan * reach_lat
                    + knot_nrm * out_nrm
                    + Vector((0.0, 0.0, drop_z + w_z))
                )
                row.append(bm.verts.new(pos))
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
                face.smooth = True
                face.material_index = 2 if (u_mid < 0.13 or u_mid > 0.87) else 0
                for loop in face.loops:
                    loop[uv_layer].uv = (0.52, 0.90)

    knot_rows, knot_cols = 8, 8
    knot_grid = []
    for r in range(knot_rows + 1):
        v = r / knot_rows
        z_off = (v - 0.5) * 0.026
        row = []
        for c in range(knot_cols + 1):
            u = c / knot_cols
            lat_off = (u - 0.5) * 0.022
            bulge = 0.012 * math.sin(math.pi * u) * math.sin(math.pi * v) + 0.004
            pos = knot_pt + knot_tan * lat_off + knot_nrm * bulge + Vector((0.0, 0.0, z_off))
            row.append(bm.verts.new(pos))
            vert_weights_info.append(("bow", 0.5))
        knot_grid.append(row)

    for r in range(knot_rows):
        for c in range(knot_cols):
            face = bm.faces.new((
                knot_grid[r][c],
                knot_grid[r][c + 1],
                knot_grid[r + 1][c + 1],
                knot_grid[r + 1][c],
            ))
            face.smooth = True
            face.material_index = 2 if (r == 0 or r == knot_rows - 1) else 0
            for loop in face.loops:
                loop[uv_layer].uv = (0.54, 0.94)

    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()

    for m in [mats["teal"], mats["ivory"], mats["gold"], mats["inner_cream"]]:
        obj.data.materials.append(m)

    for v_idx, (part_type, param_t) in enumerate(vert_weights_info):
        if part_type in ("belt", "bow"):
            assign_weights_strict4(obj, v_idx, [
                ("mixamorig:Hips", 0.65),
                ("mixamorig:Spine", 0.35),
            ])
        elif part_type in ("tail_0", "tail_1"):
            b1 = "cloth_ribbon_r" if part_type == "tail_0" else "cloth_ribbon_l"
            b2 = "cloth_ribbon_r_02" if part_type == "tail_0" else "cloth_ribbon_l_02"
            w_hips = max(0.0, 1.0 - param_t * 5.0)
            w_rem = 1.0 - w_hips
            w2 = smoothstep(0.32, 0.72, param_t)
            w1 = 1.0 - w2
            assign_weights_strict4(obj, v_idx, [
                ("mixamorig:Hips", w_hips),
                (b1, w_rem * w1),
                (b2, w_rem * w2),
            ])

    apply_subsurf_and_smooth(obj, subsurf_levels=1)
    return obj


# ==============================================================================
# 9. MAIN BUILDER: MERGE & RIG TO ENGINE-READY MIXAMO STRUCTURE
# ==============================================================================

def build_engine_ready_giao_linh():
    clean_existing_setup()

    arm_obj = build_mixamo_and_cloth_armature()
    mats = create_garment_materials()

    parts = [
        build_inner_trung_y_collar(mats),
        build_upper_giao_linh_bodice(mats),
    ]
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

    init_all_bone_vertex_groups(robe_obj)

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

    print("==================================================================")
    print("HOÀN TẤT: Áo Giao Lĩnh đã được mở rộng ống tay & chồng lớp xẻ tà!")
    print(f"- Armature gốc : {arm_obj.name} ({len(arm_obj.data.bones)} bones)")
    print(f"- Mesh đầu ra  : {robe_obj.name} (Data: {robe_obj.data.name})")
    print(f"- Tổng số đỉnh : {len(robe_obj.data.vertices)} vertices | {len(robe_obj.data.polygons)} faces")
    print("==================================================================")


if __name__ == "__main__":
    build_engine_ready_giao_linh()