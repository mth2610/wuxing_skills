import bpy
import bmesh
import math
from mathutils import Vector, Matrix
from pathlib import Path
import shutil

bpy.ops.wm.read_factory_settings(use_empty=True)

print("=== GENERATING PROCEDURAL WUXIA BASE BODY ===")

parts = []

def loft_rings(name, rings_data, n_verts=16, cap_top=True, cap_bottom=True):
    mesh = bpy.data.meshes.new(f"Mesh_{name}")
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    bm = bmesh.new()

    prev_ring = None
    first_ring = None
    last_ring = None

    for ring_spec in rings_data:
        current_ring = [bm.verts.new(pt) for pt in ring_spec]
        if first_ring is None:
            first_ring = current_ring
        if prev_ring:
            for i in range(n_verts):
                i_next = (i + 1) % n_verts
                bm.faces.new([prev_ring[i], prev_ring[i_next], current_ring[i_next], current_ring[i]])
        prev_ring = current_ring
    last_ring = prev_ring

    if cap_bottom and first_ring:
        bm.faces.new(first_ring)
    if cap_top and last_ring:
        bm.faces.new(list(reversed(last_ring)))

    bm.to_mesh(mesh)
    bm.free()
    parts.append(obj)
    return obj

# -----------------------------------------------------------------------------
# 1. TORSO, NECK & HEAD (Continuous loft with defined wuxia anatomy)
# -----------------------------------------------------------------------------
N_BODY = 24
TORSO_SPEC = [
    # (z, cy, rx, ry_front, ry_back)
    (0.82, -0.010, 0.135, 0.090, 0.105), # Crotch base
    (0.88, -0.010, 0.148, 0.095, 0.125), # Glute base
    (0.95, -0.012, 0.155, 0.100, 0.140), # Gluteus maximus peak
    (1.04, -0.008, 0.148, 0.095, 0.118), # Iliac crest / Hips
    (1.10, -0.005, 0.132, 0.088, 0.098), # Waist narrow athletic
    (1.18,  0.000, 0.138, 0.095, 0.098), # Mid abdomen (navel)
    (1.26,  0.005, 0.152, 0.108, 0.105), # Upper abs / lower ribs
    (1.34,  0.010, 0.172, 0.122, 0.115), # Lats flare
    (1.41,  0.015, 0.182, 0.135, 0.118), # Pectoral peak
    (1.46,  0.008, 0.170, 0.118, 0.112), # Upper chest / clavicle
    (1.49,  0.000, 0.135, 0.095, 0.098), # Trapezius slope
    (1.52, -0.005, 0.082, 0.076, 0.076), # Neck base
    (1.56, -0.005, 0.068, 0.066, 0.066), # Mid neck
    (1.60,  0.012, 0.070, 0.074, 0.068), # Throat / Under jaw
    (1.64,  0.024, 0.076, 0.088, 0.078), # Jawline & Chin
    (1.68,  0.026, 0.084, 0.098, 0.088), # Nose & Cheekbones
    (1.72,  0.012, 0.086, 0.096, 0.092), # Brow & Eyes
    (1.76, -0.005, 0.080, 0.086, 0.090), # Forehead
    (1.80, -0.015, 0.052, 0.058, 0.068), # Crown
]

torso_rings = []
for z, cy, rx, ry_f, ry_b in TORSO_SPEC:
    pts = []
    for i in range(N_BODY):
        ang = 2.0 * math.pi * i / N_BODY
        sin_a = math.sin(ang)
        cos_a = math.cos(ang)
        ry = ry_f if sin_a >= 0 else ry_b
        extra_y = 0.0
        extra_x = 0.0

        # Pectorals: defined chest plates with center sternum cleft
        if 1.35 <= z <= 1.45 and sin_a > 0.2:
            sternum_cleft = 1.0 - math.exp(-12.0 * (cos_a ** 2))
            extra_y = 0.032 * (sin_a ** 1.8) * sternum_cleft

        # Abdominal definition (6-pack)
        if 1.12 <= z <= 1.30 and sin_a > 0.4:
            ab_wave = math.cos((z - 1.12) / 0.18 * 3.0 * math.pi)
            linea_alba = 1.0 - math.exp(-16.0 * (cos_a ** 2))
            extra_y = 0.014 * (sin_a ** 2) * (0.6 + 0.4 * ab_wave) * linea_alba

        # Stylized Face
        if z == 1.68:
            if abs(ang - math.pi/2) < 0.22:
                extra_y = 0.040
            elif abs(abs(ang - math.pi/2) - 0.55) < 0.20:
                extra_y = 0.016
        if z == 1.64 and abs(ang - math.pi/2) < 0.20:
            extra_y = 0.032
        if z == 1.72 and abs(ang - math.pi/2) < 0.35:
            extra_y = 0.018

        # Latissimus dorsi flare
        if 1.28 <= z <= 1.44 and abs(cos_a) > 0.55:
            extra_x = 0.018 * (abs(cos_a) ** 2)

        vx = (rx + extra_x) * cos_a
        vy = cy + (ry * sin_a) + extra_y
        vz = z
        pts.append((vx, vy, vz))
    torso_rings.append(pts)

loft_rings("Torso_Body", torso_rings, n_verts=N_BODY)

# Add stylized ears
for side in [1.0, -1.0]:
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(side * 0.088, -0.01, 1.69))
    ear = bpy.context.active_object
    ear.scale = (0.012, 0.024, 0.042)
    ear.rotation_euler = (math.radians(-10), side * math.radians(12), 0)
    parts.append(ear)

# -----------------------------------------------------------------------------
# 2. CONTINUOUS ARMS (Seamless shoulder blend, contoured bicep/forearm)
# -----------------------------------------------------------------------------
N_ARM = 16
ARM_SPEC = [
    # (x, cy, cz, ry, rz)
    (0.14,  0.005, 1.42, 0.068, 0.072), # Deep shoulder socket
    (0.20,  0.005, 1.43, 0.074, 0.078), # Deltoid junction
    (0.26,  0.005, 1.43, 0.076, 0.080), # Deltoid peak
    (0.33,  0.000, 1.43, 0.058, 0.064), # Bicep peak
    (0.40,  0.000, 1.43, 0.052, 0.056), # Lower bicep
    (0.44, -0.002, 1.43, 0.046, 0.048), # Elbow
    (0.52,  0.000, 1.43, 0.050, 0.048), # Forearm peak
    (0.62,  0.000, 1.43, 0.042, 0.038), # Lower forearm
    (0.66,  0.000, 1.43, 0.035, 0.028), # Wrist
    (0.72,  0.000, 1.43, 0.040, 0.020), # Palm
    (0.79,  0.000, 1.43, 0.036, 0.015), # Knuckles
    (0.84,  0.000, 1.43, 0.022, 0.009), # Fingertips
]

for side in [1.0, -1.0]:
    arm_rings = []
    for x, cy, cz, ry, rz in ARM_SPEC:
        pts = []
        for i in range(N_ARM):
            ang = 2.0 * math.pi * i / N_ARM
            vy = cy + ry * math.cos(ang)
            vz = cz + rz * math.sin(ang)
            vx = side * x
            pts.append((vx, vy, vz))
        if side < 0:
            pts = list(reversed(pts))
        arm_rings.append(pts)
    loft_rings(f"Arm_{'L' if side > 0 else 'R'}", arm_rings, n_verts=N_ARM)

    # Thumb (anatomical angle pointing forward-up)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.013, depth=0.058, vertices=10, location=(side * 0.70, 0.038, 1.42))
    thumb = bpy.context.active_object
    thumb.rotation_euler = (math.radians(35), 0, side * math.radians(25))
    parts.append(thumb)

# -----------------------------------------------------------------------------
# 3. CONTINUOUS LEGS (Nestled inside pelvis, anatomical quad & calf)
# -----------------------------------------------------------------------------
N_LEG = 16
LEG_SPEC = [
    # (z, cx, cy, rx, ry_front, ry_back)
    (0.90, 0.095, -0.010, 0.075, 0.082, 0.095), # Inside pelvis socket
    (0.82, 0.098, -0.005, 0.082, 0.088, 0.094), # Hip joint
    (0.72, 0.098,  0.005, 0.080, 0.094, 0.085), # Upper thigh
    (0.62, 0.098,  0.008, 0.075, 0.088, 0.078), # Mid thigh
    (0.54, 0.098,  0.010, 0.065, 0.072, 0.068), # Above knee
    (0.48, 0.098,  0.015, 0.056, 0.066, 0.056), # Knee / Patella
    (0.42, 0.098,  0.000, 0.052, 0.054, 0.060), # Below knee
    (0.34, 0.098, -0.012, 0.064, 0.058, 0.080), # Calf bulge (gastrocnemius)
    (0.24, 0.098, -0.008, 0.052, 0.050, 0.060), # Lower calf
    (0.14, 0.098, -0.004, 0.042, 0.040, 0.044), # Ankle narrow
    (0.08, 0.098,  0.000, 0.040, 0.042, 0.044), # Ankle base
]

for side in [1.0, -1.0]:
    leg_rings = []
    for z, cx, cy, rx, ry_f, ry_b in LEG_SPEC:
        pts = []
        for i in range(N_LEG):
            ang = 2.0 * math.pi * i / N_LEG
            sin_a = math.sin(ang)
            cos_a = math.cos(ang)
            ry = ry_f if sin_a >= 0 else ry_b
            vx = side * cx + rx * cos_a
            vy = cy + ry * sin_a
            vz = z
            pts.append((vx, vy, vz))
        if side < 0:
            pts = list(reversed(pts))
        leg_rings.append(pts)
    loft_rings(f"Leg_{'L' if side > 0 else 'R'}", leg_rings, n_verts=N_LEG)

    # Foot (Anatomical heel, arch, and toe box)
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(side * 0.098, 0.045, 0.04))
    foot = bpy.context.active_object
    foot.scale = (0.070, 0.17, 0.05)
    bm = bmesh.new()
    bm.from_mesh(foot.data)
    for v in bm.verts:
        if v.co.y > 0: # toes
            v.co.z *= 0.52
            v.co.x *= 1.10
        else: # heel
            v.co.x *= 0.85
            v.co.z *= 1.05
    bm.to_mesh(foot.data)
    bm.free()
    parts.append(foot)

# -----------------------------------------------------------------------------
# 4. FUSE ALL ANATOMY WITH VOXEL REMESH
# -----------------------------------------------------------------------------
bpy.ops.object.select_all(action='DESELECT')
for p in parts:
    p.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
body = bpy.context.active_object
body.name = "Human_Base_Body"

bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# Voxel remesh at 0.009m (9mm resolution for crisp definition)
body.data.remesh_voxel_size = 0.009
bpy.ops.object.voxel_remesh()

# Smooth modifier
mod_smooth = body.modifiers.new(name="Smooth", type='SMOOTH')
mod_smooth.factor = 0.45
mod_smooth.iterations = 2
bpy.ops.object.modifier_apply(modifier=mod_smooth.name)

# Decimate to clean game budget
mod_dec = body.modifiers.new(name="Decimate", type='DECIMATE')
mod_dec.ratio = 0.35
bpy.ops.object.modifier_apply(modifier=mod_dec.name)

bpy.ops.object.shade_smooth()

# -----------------------------------------------------------------------------
# 5. ACCESSORIES: MARTIAL BRIEFS, SASH & TOPKNOT
# -----------------------------------------------------------------------------
# A. Form-fitting Martial Boxer Briefs (Lofted along pelvis)
briefs_rings = []
for z in [0.84, 0.88, 0.94, 1.00, 1.04]:
    pts = []
    rx = 0.150 if z > 0.90 else 0.142
    ry = 0.115 if z > 0.90 else 0.100
    for i in range(20):
        ang = 2.0 * math.pi * i / 20
        pts.append((rx * math.cos(ang), -0.010 + ry * math.sin(ang), z))
    briefs_rings.append(pts)

mesh_briefs = bpy.data.meshes.new("Mesh_Briefs")
briefs = bpy.data.objects.new("Briefs_Cloth", mesh_briefs)
bpy.context.scene.collection.objects.link(briefs)
bm = bmesh.new()
prev = None
for ring in briefs_rings:
    cur = [bm.verts.new(p) for p in ring]
    if prev:
        for i in range(20):
            bm.faces.new([prev[i], prev[(i+1)%20], cur[(i+1)%20], cur[i]])
    prev = cur
bm.to_mesh(mesh_briefs)
bm.free()

# Briefs front tabard flap
bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.102, 0.92))
tabard = bpy.context.active_object
tabard.scale = (0.09, 0.008, 0.11)

# Jade belt buckle
bpy.ops.mesh.primitive_cylinder_add(radius=0.024, depth=0.012, vertices=16, location=(0, 0.102, 1.03))
buckle = bpy.context.active_object
buckle.rotation_euler = (math.radians(90), 0, 0)

# B. Topknot Hair Bun & Hairpin
bpy.ops.mesh.primitive_uv_sphere_add(radius=0.046, segments=16, ring_count=12, location=(0, -0.035, 1.83))
hair_bun = bpy.context.active_object
hair_bun.name = "Hair_Bun"

bpy.ops.mesh.primitive_cylinder_add(radius=0.004, depth=0.18, vertices=8, location=(0, -0.035, 1.83))
pin = bpy.context.active_object
pin.name = "Hair_Pin"
pin.rotation_euler = (0, math.radians(75), 0)

# -----------------------------------------------------------------------------
# 6. MATERIALS
# -----------------------------------------------------------------------------
mat_skin = bpy.data.materials.new("Mat_Skin")
mat_skin.use_nodes = True
bsdf_skin = mat_skin.node_tree.nodes["Principled BSDF"]
bsdf_skin.inputs["Base Color"].default_value = (0.92, 0.77, 0.65, 1.0) # Warm peach skin
bsdf_skin.inputs["Roughness"].default_value = 0.42
body.data.materials.append(mat_skin)

mat_briefs = bpy.data.materials.new("Mat_Briefs")
mat_briefs.use_nodes = True
bsdf_briefs = mat_briefs.node_tree.nodes["Principled BSDF"]
bsdf_briefs.inputs["Base Color"].default_value = (0.12, 0.15, 0.22, 1.0) # Martial navy cloth
bsdf_briefs.inputs["Roughness"].default_value = 0.70
briefs.data.materials.append(mat_briefs)
tabard.data.materials.append(mat_briefs)

mat_jade = bpy.data.materials.new("Mat_Jade")
mat_jade.use_nodes = True
bsdf_jade = mat_jade.node_tree.nodes["Principled BSDF"]
bsdf_jade.inputs["Base Color"].default_value = (0.20, 0.82, 0.60, 1.0) # Wuxia green jade
bsdf_jade.inputs["Roughness"].default_value = 0.25
buckle.data.materials.append(mat_jade)

mat_hair = bpy.data.materials.new("Mat_Hair")
mat_hair.use_nodes = True
bsdf_hair = mat_hair.node_tree.nodes["Principled BSDF"]
bsdf_hair.inputs["Base Color"].default_value = (0.04, 0.04, 0.05, 1.0) # Ink black hair
bsdf_hair.inputs["Roughness"].default_value = 0.55
hair_bun.data.materials.append(mat_hair)

mat_pin = bpy.data.materials.new("Mat_Pin")
mat_pin.use_nodes = True
bsdf_pin = mat_pin.node_tree.nodes["Principled BSDF"]
bsdf_pin.inputs["Base Color"].default_value = (0.95, 0.78, 0.32, 1.0) # Gold hairpin
bsdf_pin.inputs["Metallic"].default_value = 0.85
pin.data.materials.append(mat_pin)

# Join all into unified character object
bpy.ops.object.select_all(action='DESELECT')
briefs.select_set(True)
tabard.select_set(True)
buckle.select_set(True)
hair_bun.select_set(True)
pin.select_set(True)
body.select_set(True)
bpy.context.view_layer.objects.active = body
bpy.ops.object.join()

# -----------------------------------------------------------------------------
# 7. SKELETON (20 STANDARD MIXAMO BONES)
# -----------------------------------------------------------------------------
arm_data = bpy.data.armatures.new("Humanoid_Armature")
arm_obj = bpy.data.objects.new("KiemTien_Body_Mesh", arm_data)
bpy.context.scene.collection.objects.link(arm_obj)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode='EDIT')

BONE_DEFS = [
    ("mixamorig:Hips", None, Vector((0, 0, 0.95)), Vector((0, 0, 1.08))),
    ("mixamorig:Spine", "mixamorig:Hips", Vector((0, 0, 1.08)), Vector((0, 0, 1.22))),
    ("mixamorig:Spine1", "mixamorig:Spine", Vector((0, 0, 1.22)), Vector((0, 0, 1.36))),
    ("mixamorig:Spine2", "mixamorig:Spine1", Vector((0, 0, 1.36)), Vector((0, 0, 1.48))),
    ("mixamorig:Neck", "mixamorig:Spine2", Vector((0, 0, 1.48)), Vector((0, 0, 1.58))),
    ("mixamorig:Head", "mixamorig:Neck", Vector((0, 0, 1.58)), Vector((0, 0, 1.76))),

    # Left Arm
    ("mixamorig:LeftShoulder", "mixamorig:Spine2", Vector((0.06, 0, 1.43)), Vector((0.18, 0, 1.43))),
    ("mixamorig:LeftArm", "mixamorig:LeftShoulder", Vector((0.18, 0, 1.43)), Vector((0.44, 0, 1.43))),
    ("mixamorig:LeftForeArm", "mixamorig:LeftArm", Vector((0.44, 0, 1.43)), Vector((0.66, 0, 1.43))),
    ("mixamorig:LeftHand", "mixamorig:LeftForeArm", Vector((0.66, 0, 1.43)), Vector((0.84, 0, 1.43))),

    # Right Arm
    ("mixamorig:RightShoulder", "mixamorig:Spine2", Vector((-0.06, 0, 1.43)), Vector((-0.18, 0, 1.43))),
    ("mixamorig:RightArm", "mixamorig:RightShoulder", Vector((-0.18, 0, 1.43)), Vector((-0.44, 0, 1.43))),
    ("mixamorig:RightForeArm", "mixamorig:RightArm", Vector((-0.44, 0, 1.43)), Vector((-0.66, 0, 1.43))),
    ("mixamorig:RightHand", "mixamorig:RightForeArm", Vector((-0.66, 0, 1.43)), Vector((-0.84, 0, 1.43))),

    # Left Leg
    ("mixamorig:LeftUpLeg", "mixamorig:Hips", Vector((0.098, 0, 0.88)), Vector((0.098, 0, 0.48))),
    ("mixamorig:LeftLeg", "mixamorig:LeftUpLeg", Vector((0.098, 0, 0.48)), Vector((0.098, 0, 0.08))),
    ("mixamorig:LeftFoot", "mixamorig:LeftLeg", Vector((0.098, 0, 0.08)), Vector((0.098, 0.16, 0.00))),

    # Right Leg
    ("mixamorig:RightUpLeg", "mixamorig:Hips", Vector((-0.098, 0, 0.88)), Vector((-0.098, 0, 0.48))),
    ("mixamorig:RightLeg", "mixamorig:RightUpLeg", Vector((-0.098, 0, 0.48)), Vector((-0.098, 0, 0.08))),
    ("mixamorig:RightFoot", "mixamorig:RightLeg", Vector((-0.098, 0, 0.08)), Vector((-0.098, 0.16, 0.00))),
]

edit_bones = {}
for name, parent_name, head, tail in BONE_DEFS:
    b = arm_data.edit_bones.new(name)
    b.head = head
    b.tail = tail
    if parent_name:
        b.parent = edit_bones[parent_name]
    edit_bones[name] = b

bpy.ops.object.mode_set(mode='OBJECT')

# Skinning
bpy.ops.object.select_all(action='DESELECT')
body.select_set(True)
arm_obj.select_set(True)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.parent_set(type='ARMATURE_AUTO')
print(f"  [Rigging] Bone weights bound! Mesh vertices: {len(body.data.vertices)}, faces: {len(body.data.polygons)}")

# -----------------------------------------------------------------------------
# 8. RENDER BEAUTY SHOTS
# -----------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("Cam_Beauty")
cam_obj = bpy.data.objects.new("Cam_Beauty", cam_data)
bpy.context.scene.collection.objects.link(cam_obj)
bpy.context.scene.camera = cam_obj
cam_obj.location = (0.55, 2.25, 1.20)
cam_obj.rotation_euler = (math.radians(91), 0, math.radians(166))

lights = [
    ((2.2, 2.2, 2.4), 650, (1.0, 0.96, 0.92)),
    ((-2.2, 2.0, 1.8), 380, (0.75, 0.85, 1.0)),
    ((0.0, -2.4, 2.2), 600, (1.0, 0.92, 0.8)),
]
for loc, power, col in lights:
    l_data = bpy.data.lights.new("Light", type='POINT')
    l_data.energy = power
    l_data.color = col
    l_obj = bpy.data.objects.new("Light", l_data)
    bpy.context.scene.collection.objects.link(l_obj)
    l_obj.location = loc

bpy.context.scene.render.resolution_x = 768
bpy.context.scene.render.resolution_y = 768
bpy.context.scene.render.film_transparent = True
preview_path = "/tmp/preview_heroic_body.png"
bpy.context.scene.render.filepath = preview_path
bpy.ops.render.render(write_still=True)
print(f"Rendered beauty preview to {preview_path}!")

# -----------------------------------------------------------------------------
# 9. EXPORT GLB TO WORKSPACE
# -----------------------------------------------------------------------------
workspace = Path(__file__).resolve().parent.parent
out_raw = workspace / "raw_asset" / "bodies" / "human_body.glb"
out_asset = workspace / "assets" / "characters" / "bodies" / "human_body.glb"

out_raw.parent.mkdir(parents=True, exist_ok=True)
out_asset.parent.mkdir(parents=True, exist_ok=True)

bpy.ops.object.select_all(action='DESELECT')
arm_obj.select_set(True)
body.select_set(True)
bpy.context.view_layer.objects.active = arm_obj

base_export_kwargs = dict(
    filepath=str(out_raw),
    export_format='GLB',
    use_selection=True,
    export_animations=False,
    export_draco_mesh_compression_enable=False
)
bpy.ops.export_scene.gltf(**base_export_kwargs)
shutil.copyfile(out_raw, out_asset)

size_kb = out_raw.stat().st_size / 1024
print(f"Exported clean base body: {out_raw} ({size_kb:.1f} KB)")
print(f"Synced to: {out_asset}")
print("=== GENERATION COMPLETE ===")
