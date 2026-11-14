#!/usr/bin/env python3
"""
scripts/build_tunic_tienhiep.py

Builds the Wuxia / Xianxia layered ivory & slate-blue robe (`ao_tien_hiep.glb`)
matching the uploaded concept art and the Wuxing Skills `character/` contract:
  - Layered Giao Linh 2-layer cross-collar, wide ivory outer bell sleeves,
    fitted dark-slate inner forearm sleeves & stylized cuffs
  - Wrapped charcoal waist sash, 3-swirl metal buckle, celadon jade beads & tassels
  - Layered front/back ivory skirt flaps over slate-blue under-skirt
  - 2.2mm Solidified 2-sided shells (zero backface culling holes)
  - 28 bones (20 mixamorig:* + 8 cloth_* spring bones matching spring_bone.c)
  - Packed PBR textures with subtle cloud/wave motif & fold shading

Run:
  /Applications/Blender.app/Contents/MacOS/Blender --background --python scripts/build_tunic_tienhiep.py
"""

import math
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector, Matrix, Euler

REPO_ROOT = Path(__file__).resolve().parent.parent
REF_BODY_GLB = REPO_ROOT / "assets/characters/bodies/human_body.glb"
REF_TUNIC_GLB = REPO_ROOT / "assets/characters/equipment/torso/ao_giao_linh.glb"
OUT_GLB = REPO_ROOT / "assets/characters/equipment/torso/ao_tien_hiep.glb"
PREVIEW_DIR = Path(
    "/Users/mth2610/.gemini/antigravity-cli/brain/d53dbc6b-8a92-40ed-b8a4-852145fe0fe5/scratch"
)
PREVIEW_DIR.mkdir(parents=True, exist_ok=True)


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_references():
    # Import human_body.glb for body fit + 20 core bones
    bpy.ops.import_scene.gltf(filepath=str(REF_BODY_GLB))
    body_arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    body_mesh = next(o for o in bpy.data.objects if o.type == "MESH")
    body_arm.name = "RefBodyArmature"
    body_mesh.name = "RefBodyMesh"

    # Import ao_giao_linh.glb to copy exact 8 cloth_* bone specs
    bpy.ops.import_scene.gltf(filepath=str(REF_TUNIC_GLB))
    tunic_arm = next(o for o in bpy.data.objects if o.type == "ARMATURE" and o != body_arm)
    tunic_mesh = next(o for o in bpy.data.objects if o.type == "MESH" and o != body_mesh)

    cloth_bone_specs = []
    for b in tunic_arm.data.bones:
        if b.name.startswith("cloth_"):
            cloth_bone_specs.append({
                "name": b.name,
                "parent": b.parent.name if b.parent else "mixamorig:Hips",
                "head": b.head_local.copy(),
                "tail": b.tail_local.copy(),
            })

    bpy.data.objects.remove(tunic_mesh, do_unlink=True)
    bpy.data.objects.remove(tunic_arm, do_unlink=True)

    # Add the 8 cloth_* bones onto body_arm in Edit Mode
    bpy.context.view_layer.objects.active = body_arm
    bpy.ops.object.mode_set(mode="EDIT")
    ebones = body_arm.data.edit_bones
    for spec in cloth_bone_specs:
        if spec["name"] not in ebones:
            eb = ebones.new(spec["name"])
            eb.head = spec["head"]
            eb.tail = spec["tail"]
            if spec["parent"] in ebones:
                eb.parent = ebones[spec["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")

    return body_arm, body_mesh


def make_cloth_material_ivory_with_clouds():
    """Ivory silk robe material with subtle procedural cloud-swirl motif baked to image."""
    mat = bpy.data.materials.new(name="Wuxia_IvoryCloudSilk_Mat")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Roughness"].default_value = 0.62
    if "Specular" in bsdf.inputs:
        bsdf.inputs["Specular"].default_value = 0.28

    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    # Swirling wave/cloud pattern using Distorted Wave + Noise
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 7.5
    noise.inputs["Detail"].default_value = 6.0
    noise.inputs["Distortion"].default_value = 2.6

    wave = nt.nodes.new("ShaderNodeTexWave")
    wave.wave_type = "RINGS"
    wave.rings_direction = "SPHERICAL"
    wave.inputs["Scale"].default_value = 3.2
    wave.inputs["Distortion"].default_value = 7.0
    wave.inputs["Detail"].default_value = 4.0

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.56
    ramp.color_ramp.elements[0].color = (0.91, 0.90, 0.87, 1.0)   # Warm ivory base
    ramp.color_ramp.elements[1].position = 0.72
    ramp.color_ramp.elements[1].color = (0.72, 0.74, 0.78, 1.0)   # Silver-grey cloud motif

    # Mask clouds mostly to shoulders/chest (Z>1.25) and lower hem (Z<0.65)
    sep_xyz = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tex_coord.outputs["Object"], sep_xyz.inputs["Vector"])

    # Combine AO for soft fold depth
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.samples = 16
    ao.inputs["Distance"].default_value = 0.08

    mix_ao = nt.nodes.new("ShaderNodeMixRGB")
    mix_ao.blend_type = "MULTIPLY"
    mix_ao.inputs["Fac"].default_value = 0.35

    nt.links.new(tex_coord.outputs["Object"], noise.inputs["Vector"])
    nt.links.new(noise.outputs["Color"], wave.inputs["Vector"])
    nt.links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], mix_ao.inputs["Color1"])
    nt.links.new(ao.outputs["Color"], mix_ao.inputs["Color2"])
    nt.links.new(mix_ao.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return mat


def create_simple_mat(name, color, roughness=0.65, metallic=0.0, specular=0.3):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = color
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if "Specular" in bsdf.inputs:
        bsdf.inputs["Specular"].default_value = specular
    return mat


def add_lofted_shell(bm, rings_pts, closed=True, mat_idx=0):
    """Create a lofted open-ended shell from a list of point rings."""
    bm_rings = []
    for pts in rings_pts:
        bm_rings.append([bm.verts.new(p) for p in pts])

    n = len(bm_rings[0])
    for r_i in range(len(bm_rings) - 1):
        ra, rb = bm_rings[r_i], bm_rings[r_i + 1]
        lim = n if closed else n - 1
        for i in range(lim):
            j = (i + 1) % n
            f = bm.faces.new((ra[i], ra[j], rb[j], rb[i]))
            f.material_index = mat_idx
    return bm_rings


def build_outer_bodice_and_skirt(mat_ivory_idx=0, mat_trim_idx=1):
    """
    Build the outer ivory Giao Linh bodice (with open V-neck cross lapel)
    and flowing layered front/back skirt flaps (Z = 0.32 .. 1.54).
    """
    bm = bmesh.new()

    # 1. Open-front cross-collar bodice + split-front skirt
    # Angle theta runs from front-right overlap (-0.35 rad) around back to front-left (+2*pi - 0.15)
    # Using open strip (closed=False) so front lapels overlap cleanly!
    segs = 32
    # (z, rx, ry_f, ry_b, cy, front_gap_rad, pleat_amp)
    levels = [
        (0.33, 0.340, 0.250, 0.290, 0.015, 0.52, 0.022),  # Lower flared hem
        (0.48, 0.295, 0.215, 0.245, 0.010, 0.42, 0.018),  # Mid-lower skirt
        (0.66, 0.245, 0.175, 0.195, 0.005, 0.30, 0.014),  # Mid-upper skirt
        (0.84, 0.195, 0.135, 0.148, 0.002, 0.14, 0.008),  # Hip flare
        (0.95, 0.162, 0.116, 0.120, 0.000, 0.02, 0.002),  # Waist cinch (under sash)
        (1.08, 0.174, 0.122, 0.122, -0.002, -0.04, 0.000), # Lower ribs (slight overlap)
        (1.24, 0.196, 0.132, 0.126, -0.004, 0.06, 0.000), # Mid chest
        (1.38, 0.218, 0.136, 0.128, -0.005, 0.22, 0.000), # Upper chest / V-neck
        (1.49, 0.228, 0.125, 0.122, -0.002, 0.34, 0.000), # Shoulder yoke (covers deltoid root)
        (1.54, 0.155, 0.100, 0.102,  0.002, 0.44, 0.000), # Collar top edge
    ]

    rings = []
    for z, rx, ryf, ryb, cy, gap, pleat in levels:
        pts = []
        # Sweep from front-center + gap (left lapel edge) around back to front-center - gap (right lapel edge)
        # In Blender: -Y is front (angle = -pi/2 = 3pi/2)
        start_ang = -math.pi * 0.5 + gap
        end_ang = math.pi * 1.5 - gap
        for i in range(segs):
            t = i / (segs - 1)
            ang = start_ang + (end_ang - start_ang) * t
            ca, sa = math.cos(ang), math.sin(ang)
            # Subtle fabric pleats on skirt
            p_wave = math.sin(ang * 7.0) * pleat
            ry = ryb if sa >= 0 else ryf
            x = (rx + p_wave) * ca
            y = cy + (ry + p_wave) * sa
            # Pointed diagonal hem asymmetry on front skirt
            z_mod = z
            if z < 0.50 and sa < 0:
                z_mod += 0.05 * (1.0 - abs(ca))
            pts.append(Vector((x, y, z_mod)))
        rings.append(pts)

    add_lofted_shell(bm, rings, closed=False, mat_idx=mat_ivory_idx)

    # 2. Raised Cross-Collar Lapel Trim Bands (Giao Lĩnh V-collar)
    for side in (-1.0, 1.0):
        # Left-over-right crossover trim
        collar_pts = []
        steps = 10
        for s in range(steps):
            t = s / (steps - 1)
            z = 1.55 - t * 0.56  # Neck to waist
            x = side * (0.068 * (1.0 - t) - 0.025 * t)
            y = -0.082 * (1.0 - t) - 0.126 * t
            if side > 0:
                y -= 0.005  # Left lapel sits slightly in front of right lapel
            r1 = Vector((x, y, z))
            r2 = Vector((x + side * 0.024, y + 0.008, z + 0.006))
            collar_pts.append([r1, r2])
        add_lofted_shell(bm, collar_pts, closed=False, mat_idx=mat_trim_idx)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new("OuterRobeMesh")
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new("Tunic_OuterRobe", mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def build_inner_underskirt_and_collar(mat_slate_idx=0, mat_dark_idx=1):
    """Build inner slate-blue under-skirt & inner cross-collar layer."""
    bm = bmesh.new()
    segs = 28

    # Inner under-skirt (slightly longer & narrower than outer ivory skirt, visible in front split)
    skirt_levels = [
        (0.28, 0.285, 0.210, 0.235, 0.012, 0.014),
        (0.46, 0.250, 0.185, 0.205, 0.008, 0.010),
        (0.68, 0.215, 0.155, 0.170, 0.004, 0.006),
        (0.92, 0.158, 0.112, 0.118, 0.000, 0.000),
    ]
    rings = []
    for z, rx, ryf, ryb, cy, pleat in skirt_levels:
        pts = []
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            ca, sa = math.cos(ang), math.sin(ang)
            p_wave = math.sin(ang * 6.0) * pleat
            ry = ryb if sa >= 0 else ryf
            pts.append(Vector(((rx + p_wave) * ca, cy + (ry + p_wave) * sa, z)))
        rings.append(pts)
    add_lofted_shell(bm, rings, closed=True, mat_idx=mat_slate_idx)

    # Inner slate-blue collar + chest V-insert (peaks inside outer collar)
    collar_levels = [
        (1.22, 0.175, 0.120, 0.114, -0.004),
        (1.38, 0.190, 0.122, 0.116, -0.004),
        (1.50, 0.145, 0.095, 0.098,  0.000),
        (1.57, 0.078, 0.072, 0.078,  0.002),
    ]
    c_rings = []
    for z, rx, ryf, ryb, cy in collar_levels:
        pts = []
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            ca, sa = math.cos(ang), math.sin(ang)
            ry = ryb if sa >= 0 else ryf
            pts.append(Vector((rx * ca, cy + ry * sa, z)))
        c_rings.append(pts)
    add_lofted_shell(bm, c_rings, closed=True, mat_idx=mat_dark_idx)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new("InnerLayerMesh")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("Tunic_InnerLayer", mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def build_sleeves_and_cuffs(mat_ivory_idx=0, mat_slate_idx=1, mat_dark_idx=2):
    """
    Build:
      - Wide flowing outer ivory bell sleeves (shoulder -> mid-forearm, draping down)
      - Fitted inner slate-blue forearm sleeves + dark stylized bracer cuffs
    """
    bm = bmesh.new()
    segs = 20

    for side in (-1.0, 1.0):
        # 1. Outer wide ivory bell sleeve (x = 0.125 .. 0.65, raised cap over deltoid)
        # (x_abs, cy, cz_top, cz_bot, ry)
        bell_specs = [
            (0.125,  0.000, 1.578, 1.385, 0.108),  # Deep shoulder yoke overlap
            (0.205,  0.000, 1.580, 1.370, 0.105),  # Deltoid cap clearance (+3.5cm)
            (0.280, -0.002, 1.568, 1.350, 0.098),  # Upper arm
            (0.380, -0.002, 1.552, 1.290, 0.098),  # Mid upper arm flare
            (0.490,  0.002, 1.542, 1.210, 0.105),  # Elbow drape
            (0.580,  0.000, 1.535, 1.135, 0.112),  # Wide bell cuff
            (0.645, -0.002, 1.530, 1.080, 0.118),  # Hanging bell tip
        ]
        b_rings = []
        for xa, cy, z_top, z_bot, ry in bell_specs:
            cz_mid = 0.5 * (z_top + z_bot)
            rz = 0.5 * (z_top - z_bot)
            pts = []
            for i in range(segs):
                ang = 2.0 * math.pi * i / segs
                y = cy + ry * math.cos(ang)
                z = cz_mid + rz * math.sin(ang)
                # Pointed sleeve drape at bottom
                if math.sin(ang) < -0.5:
                    z -= 0.025 * (-math.sin(ang) - 0.5)
                pts.append(Vector((side * xa, y, z)))
            if side < 0:
                pts = list(reversed(pts))
            b_rings.append(pts)
        add_lofted_shell(bm, b_rings, closed=True, mat_idx=mat_ivory_idx)

        # 2. Inner slate-blue forearm sleeve (x = 0.46 .. 0.72)
        inner_specs = [
            (0.46, 0.000, 1.475, 0.056, 0.058),
            (0.58, -0.004, 1.475, 0.054, 0.056),
            (0.68, -0.003, 1.475, 0.044, 0.046),
            (0.73, -0.002, 1.475, 0.040, 0.040),
        ]
        i_rings = []
        for xa, cy, cz, ry, rz in inner_specs:
            pts = []
            for i in range(16):
                ang = 2.0 * math.pi * i / 16
                pts.append(Vector((side * xa, cy + ry * math.cos(ang), cz + rz * math.sin(ang))))
            if side < 0:
                pts = list(reversed(pts))
            i_rings.append(pts)
        add_lofted_shell(bm, i_rings, closed=True, mat_idx=mat_slate_idx)

        # 3. Dark charcoal stylized bracer cuff (x = 0.59 .. 0.72)
        cuff_specs = [
            (0.595, -0.004, 1.475, 0.056, 0.058),
            (0.655, -0.003, 1.475, 0.048, 0.050),
            (0.718, -0.002, 1.475, 0.042, 0.042),
        ]
        c_rings = []
        for xa, cy, cz, ry, rz in cuff_specs:
            pts = []
            for i in range(16):
                ang = 2.0 * math.pi * i / 16
                pts.append(Vector((side * xa, cy + ry * math.cos(ang), cz + rz * math.sin(ang))))
            if side < 0:
                pts = list(reversed(pts))
            c_rings.append(pts)
        add_lofted_shell(bm, c_rings, closed=True, mat_idx=mat_dark_idx)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new("SleevesMesh")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("Tunic_Sleeves", mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def build_belt_buckle_and_tassels(mat_dark_idx=0, mat_metal_idx=1, mat_jade_idx=2, mat_slate_idx=3):
    """
    Build wrapped charcoal waist sash, 3-swirl Taiji metal buckle,
    front jade beads + tassel, and back sash ribbon + tassel.
    """
    bm = bmesh.new()
    segs = 32

    # 1. Wrapped waist sash (Z = 0.90 .. 1.04) — +1.8cm outside bodice to prevent z-fighting
    sash_levels = [
        (0.905, 0.188, 0.140, 0.144),
        (0.945, 0.184, 0.138, 0.142),
        (0.975, 0.182, 0.136, 0.140),
        (1.005, 0.184, 0.137, 0.140),
        (1.035, 0.188, 0.139, 0.140),
    ]
    s_rings = []
    for z, rx, ryf, ryb in sash_levels:
        pts = []
        for i in range(segs):
            ang = 2.0 * math.pi * i / segs
            ca, sa = math.cos(ang), math.sin(ang)
            ry = ryb if sa >= 0 else ryf
            # Subtle wrap ridges
            fold = 0.003 * math.sin((z - 0.90) * 140.0 + ang * 2.0)
            pts.append(Vector(((rx + fold) * ca, (ry + fold) * sa, z)))
        s_rings.append(pts)
    add_lofted_shell(bm, s_rings, closed=True, mat_idx=mat_dark_idx)

    # 2. Front circular metal buckle (3-swirl Ngũ Hành ring + inner ring)
    ring_ret = bmesh.ops.create_cone(
        bm, cap_ends=True, cap_tris=False, segments=24,
        radius1=0.036, radius2=0.034, depth=0.012
    )
    rot_x = Euler((math.radians(90), 0.0, 0.0)).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation((0.0, -0.142, 0.970)) @ rot_x, verts=ring_ret["verts"])
    for f in bm.faces:
        if all(v in ring_ret["verts"] for v in f.verts):
            f.material_index = mat_metal_idx

    # Side buckle wings
    for side in (-1.0, 1.0):
        wing = bmesh.ops.create_cube(bm, size=1.0)
        for v in wing["verts"]:
            v.co.x = side * (0.044 + v.co.x * 0.022)
            v.co.y = -0.138 + v.co.y * 0.010
            v.co.z = 0.970 + v.co.z * 0.020
        for f in bm.faces:
            if all(v in wing["verts"] for v in f.verts):
                f.material_index = mat_metal_idx

    # 3. Front hanging celadon jade beads + tassel (aligned with cloth_ribbon_l/r)
    for bz, brad in [(0.875, 0.014), (0.810, 0.016)]:
        bead = bmesh.ops.create_uvsphere(bm, u_segments=14, v_segments=10, radius=brad)
        for v in bead["verts"]:
            v.co.y += -0.155
            v.co.z += bz
        for f in bm.faces:
            if all(v in bead["verts"] for v in f.verts):
                f.material_index = mat_jade_idx

    # Front tassel cord + tassel cone
    tassel = bmesh.ops.create_cone(
        bm, cap_ends=True, cap_tris=False, segments=16,
        radius1=0.022, radius2=0.007, depth=0.15
    )
    bmesh.ops.transform(bm, matrix=Matrix.Translation((0.0, -0.165, 0.70)), verts=tassel["verts"])
    for f in bm.faces:
        if all(v in tassel["verts"] for v in f.verts):
            f.material_index = mat_slate_idx

    # 4. Diagonal front waist ribbon strips
    for side in (-1.0, 1.0):
        rib_rings = []
        for s in range(6):
            t = s / 5.0
            z = 0.94 - t * 0.36
            cx = side * (0.045 + t * 0.075)
            cy = -0.145 - t * 0.055
            w = 0.024 + t * 0.008
            rib_rings.append([
                Vector((cx - w, cy, z)),
                Vector((cx + w, cy, z)),
            ])
        add_lofted_shell(bm, rib_rings, closed=False, mat_idx=mat_dark_idx)

    # 5. Back hanging charcoal ribbon + tassel (drapes cleanly outside back skirt)
    back_rib = []
    for s in range(8):
        t = s / 7.0
        z = 0.93 - t * 0.44
        cy = 0.144 + t * 0.135
        w = 0.032 + t * 0.012
        back_rib.append([
            Vector((-w, cy, z)),
            Vector(( w, cy, z)),
        ])
    add_lofted_shell(bm, back_rib, closed=False, mat_idx=mat_dark_idx)

    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    mesh = bpy.data.meshes.new("BeltMesh")
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("Tunic_Belt", mesh)
    bpy.context.collection.objects.link(obj)
    return obj


def solidify_and_assign_materials(obj, mats, thickness=0.0022):
    for m in mats:
        obj.data.materials.append(m)
    solid = obj.modifiers.new("Solidify", "SOLIDIFY")
    solid.thickness = thickness
    solid.offset = 1.0
    solid.use_rim = True
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=solid.name)
    for p in obj.data.polygons:
        p.use_smooth = True


def bake_ivory_cloud_texture(tunic_obj, mat_ivory):
    """Bake procedural cloud/wave + AO shader onto 1024x1024 image for glTF export."""
    bpy.context.view_layer.objects.active = tunic_obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.01)
    bpy.ops.object.mode_set(mode="OBJECT")

    img = bpy.data.images.new("AoTienHiep_IvoryCloud_BaseColor", width=1024, height=1024)
    nt = mat_ivory.node_tree
    tex_node = nt.nodes.new("ShaderNodeTexImage")
    tex_node.image = img
    nt.nodes.active = tex_node

    # Select only faces using mat_ivory (slot 0)
    scene = bpy.context.scene
    prev_engine = scene.render.engine
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 12

    bpy.ops.object.select_all(action="DESELECT")
    tunic_obj.select_set(True)
    bpy.ops.object.bake(type="DIFFUSE", pass_filter={"COLOR"}, margin=8)

    bsdf = nt.nodes.get("Principled BSDF")
    for link in list(nt.links):
        if link.to_socket == bsdf.inputs["Base Color"]:
            nt.links.remove(link)
    nt.links.new(tex_node.outputs["Color"], bsdf.inputs["Base Color"])
    img.pack()
    scene.render.engine = prev_engine


def rig_and_weight_tunic(tunic_obj, ref_body_mesh, armature):
    """
    Transfer base Mixamo weights from human_body.glb, then smoothly blend
    lower skirt flaps, bell-sleeve drapes, and front waist pendant onto
    the 8 cloth_* spring bones.
    """
    bpy.ops.object.select_all(action="DESELECT")
    tunic_obj.select_set(True)
    bpy.context.view_layer.objects.active = tunic_obj

    for bone in armature.data.bones:
        if bone.name not in tunic_obj.vertex_groups:
            tunic_obj.vertex_groups.new(name=bone.name)

    dt = tunic_obj.modifiers.new("DataTransferWeights", "DATA_TRANSFER")
    dt.object = ref_body_mesh
    dt.use_vert_data = True
    dt.data_types_verts = {"VGROUP_WEIGHTS"}
    dt.vert_mapping = "POLYINTERP_NEAREST"
    bpy.ops.object.modifier_apply(modifier=dt.name)

    # Blend cloth_* spring weights smoothly
    vgs = tunic_obj.vertex_groups
    for v in tunic_obj.data.vertices:
        co = v.co
        # 1. Skirt flaps (Z < 0.90) -> blend Hips to cloth_skirt_*
        if co.z < 0.90 and abs(co.x) < 0.42:
            t_skirt = min(1.0, max(0.0, (0.90 - co.z) / 0.35))
            if co.y <= 0.0:
                c_name = "cloth_skirt_f_l" if co.x >= 0.0 else "cloth_skirt_f_r"
            else:
                c_name = "cloth_skirt_b_l" if co.x >= 0.0 else "cloth_skirt_b_r"
            # Front jade beads/tassel (center front, Z < 0.90, Y < -0.125, |X| < 0.06)
            if co.y < -0.122 and abs(co.x) < 0.06:
                c_name = "cloth_ribbon_l" if co.x >= 0.0 else "cloth_ribbon_r"

            # Reduce existing weights by (1 - t_skirt) and add cloth weight
            for g in v.groups:
                g.weight *= (1.0 - t_skirt)
            vgs[c_name].add([v.index], t_skirt, "ADD")

        # 2. Hanging bell sleeve drapes (|X| in [0.36, 0.68], Z < 1.38)
        elif 0.36 <= abs(co.x) <= 0.68 and co.z < 1.38:
            t_sl = min(1.0, max(0.0, (1.38 - co.z) / 0.22))
            c_name = "cloth_sleeve_l_01" if co.x > 0 else "cloth_sleeve_r_01"
            for g in v.groups:
                g.weight *= (1.0 - t_sl)
            vgs[c_name].add([v.index], t_sl, "ADD")

    bpy.ops.object.vertex_group_normalize_all(group_select_mode="ALL", lock_active=False)

    unskinned = 0
    for v in tunic_obj.data.vertices:
        if sum(g.weight for g in v.groups) < 1e-4:
            unskinned += 1
            vgs["mixamorig:Spine1"].add([v.index], 1.0, "REPLACE")

    arm_mod = tunic_obj.modifiers.new("Armature", "ARMATURE")
    arm_mod.object = armature
    arm_mod.use_vertex_groups = True
    tunic_obj.parent = armature
    return unskinned


def render_previews_with_body():
    """Render Front, Side, Back, 3/4 fitted over human_body.glb."""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 960
    scene.render.resolution_y = 1280
    scene.render.image_settings.file_format = "PNG"

    world = bpy.data.worlds.new("PreviewWorld")
    scene.world = world
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (0.78, 0.80, 0.84, 1.0)
    bg.inputs["Strength"].default_value = 0.70

    def add_light(name, loc, energy, size=2.0):
        ldata = bpy.data.lights.new(name=name, type="AREA")
        ldata.energy = energy
        ldata.size = size
        lobj = bpy.data.objects.new(name, ldata)
        lobj.location = loc
        direction = Vector((0.0, 0.0, 0.95)) - Vector(loc)
        lobj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
        bpy.context.collection.objects.link(lobj)

    add_light("KeyLight", (1.6, -2.5, 2.2), 185.0, 2.0)
    add_light("FillLight", (-1.8, -2.0, 1.6), 75.0, 3.0)
    add_light("RimLight", (0.2, 2.6, 2.2), 130.0, 2.0)

    cam_data = bpy.data.cameras.new("PreviewCam")
    cam_data.lens = 50.0
    cam = bpy.data.objects.new("PreviewCam", cam_data)
    bpy.context.collection.objects.link(cam)
    scene.camera = cam

    views = [
        ("tunic_front.png", (0.0, -3.4, 0.95)),
        ("tunic_side.png",  (3.4,  0.0, 0.95)),
        ("tunic_back.png",  (0.0,  3.4, 0.95)),
        ("tunic_34.png",    (2.2, -2.5, 1.05)),
    ]
    target = Vector((0.0, 0.0, 0.92))
    for fname, loc in views:
        cam.location = loc
        cam.rotation_euler = (target - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = str(PREVIEW_DIR / fname)
        bpy.ops.render.render(write_still=True)


def main():
    clear_scene()
    armature, ref_body_mesh = import_references()

    mat_ivory = make_cloth_material_ivory_with_clouds()
    mat_slate = create_simple_mat("Wuxia_SlateBlue_Mat", (0.20, 0.23, 0.30, 1.0), roughness=0.68)
    mat_dark  = create_simple_mat("Wuxia_Charcoal_Mat",  (0.055, 0.068, 0.088, 1.0), roughness=0.72)
    mat_metal = create_simple_mat("Wuxia_Pewter_Mat",    (0.45, 0.46, 0.48, 1.0), roughness=0.28, metallic=0.85)
    mat_jade  = create_simple_mat("Wuxia_Celadon_Mat",   (0.20, 0.34, 0.30, 1.0), roughness=0.22, specular=0.65)

    all_mats = [mat_ivory, mat_slate, mat_dark, mat_metal, mat_jade]

    # Build components
    outer_obj = build_outer_bodice_and_skirt(mat_ivory_idx=0, mat_trim_idx=1)
    solidify_and_assign_materials(outer_obj, all_mats, thickness=0.0024)

    inner_obj = build_inner_underskirt_and_collar(mat_slate_idx=1, mat_dark_idx=2)
    solidify_and_assign_materials(inner_obj, all_mats, thickness=0.0022)

    sleeves_obj = build_sleeves_and_cuffs(mat_ivory_idx=0, mat_slate_idx=1, mat_dark_idx=2)
    solidify_and_assign_materials(sleeves_obj, all_mats, thickness=0.0022)

    belt_obj = build_belt_buckle_and_tassels(mat_dark_idx=2, mat_metal_idx=3, mat_jade_idx=4, mat_slate_idx=1)
    solidify_and_assign_materials(belt_obj, all_mats, thickness=0.0022)

    # Join all garment parts into single skinned mesh
    bpy.ops.object.select_all(action="DESELECT")
    for obj in [outer_obj, inner_obj, sleeves_obj, belt_obj]:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = outer_obj
    bpy.ops.object.join()
    outer_obj.name = "Wuxia_AoTienHiep_Mesh"

    # Triangulate cleanly for tangents
    tri = outer_obj.modifiers.new("Triangulate", "TRIANGULATE")
    bpy.ops.object.modifier_apply(modifier=tri.name)

    # Bake ivory cloud motif & AO
    bake_ivory_cloud_texture(outer_obj, mat_ivory)

    # Rig to 28-bone armature (20 Mixamo + 8 cloth_*)
    unskinned = rig_and_weight_tunic(outer_obj, ref_body_mesh, armature)

    # Render previews WITH ref_body_mesh visible to verify fit & zero poke-through!
    render_previews_with_body()

    # Now remove ref_body_mesh and export standalone modular torso GLB
    bpy.data.objects.remove(ref_body_mesh, do_unlink=True)

    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    outer_obj.select_set(True)
    bpy.context.view_layer.objects.active = armature

    OUT_GLB.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(OUT_GLB),
        export_format="GLB",
        use_selection=True,
        export_apply=False,
        export_animations=False,
        export_tangents=True,
        export_yup=True,
    )

    xs = [v.co.x for v in outer_obj.data.vertices]
    ys = [v.co.y for v in outer_obj.data.vertices]
    zs = [v.co.z for v in outer_obj.data.vertices]
    print(f"[EXPORT] {OUT_GLB}")
    print(f"  verts={len(outer_obj.data.vertices)}, polys={len(outer_obj.data.polygons)}")
    print(f"  blender_bbox: X=[{min(xs):.3f},{max(xs):.3f}] Y=[{min(ys):.3f},{max(ys):.3f}] Z=[{min(zs):.3f},{max(zs):.3f}]")
    print(f"  bones={len(armature.data.bones)}, fallback_assigned={unskinned}")


if __name__ == "__main__":
    main()
