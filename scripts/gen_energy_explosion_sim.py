"""
gen_energy_explosion_sim.py
==============================
Flipbook Atlas Vụ nổ Năng lượng - CHUẨN VẬT LÝ VÒNG SHOCKWAVE NĂNG LƯỢNG

# 1. LỆNH CHẠY TEST (4x4 Grid):
# blender --background --python gen_energy_explosion_sim.py -- --out ./test_energy_atlas.png --grid 4 --cell 64 --sim-frames 40 --resolution 64 --samples 8
"""

import bpy
import sys
import os
import math
import random
import argparse
import shutil
import numpy as np

def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1 :] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--out", default="./test_energy_atlas.png")
    p.add_argument("--grid", type=int, default=4)
    p.add_argument("--cell", type=int, default=64)
    p.add_argument("--sim-frames", type=int, default=40)
    p.add_argument("--samples", type=int, default=8)
    p.add_argument("--seed", type=int, default=1234)
    p.add_argument("--resolution", type=int, default=64)
    
    # Thiết lập cho một vụ nổ vòng năng lượng (Shockwave) đơn giản, hiệu quả
    p.add_argument("--burst-frames", type=int, default=2)         # Bùng phát cực ngắn để tạo vòng rỗng
    p.add_argument("--force-strength", type=float, default=6.0)   # Lực đẩy văng ra tạo vòng
    p.add_argument("--turb-strength", type=float, default=0.8)    # Nhiễu nhẹ để vòng trông giống plasma
    p.add_argument("--vorticity", type=float, default=2.0)         # Xoáy nhẹ
    
    p.add_argument("--noise-upres", type=int, default=2)
    p.add_argument("--noise-strength", type=float, default=1.0)
    
    p.add_argument("--inflow-density", type=float, default=10.0)
    p.add_argument("--source-radius", type=float, default=1.0)
    p.add_argument("--domain-size", type=float, default=12.0)
    return p.parse_args(argv)

ARGS = parse_args()
GRID = ARGS.grid
CELL = ARGS.cell
N_FRAMES = GRID * GRID
SIM_FRAMES = ARGS.sim_frames
random.seed(ARGS.seed)

def clear_scene():
    if bpy.context.view_layer:
        bpy.context.view_layer.objects.active = None
        
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for mesh in list(bpy.data.meshes):
        bpy.data.meshes.remove(mesh, do_unlink=True)
    for mat in list(bpy.data.materials):
        bpy.data.materials.remove(mat, do_unlink=True)
    for cam in list(bpy.data.cameras):
        bpy.data.cameras.remove(cam, do_unlink=True)

def create_cube_mesh(name, size):
    s = size / 2.0
    verts = [
        (-s, -s, -s), (s, -s, -s), (s, s, -s), (-s, s, -s),
        (-s, -s, s), (s, -s, s), (s, s, s), (-s, s, s)
    ]
    faces = [
        (0, 1, 2, 3), (4, 7, 6, 5),
        (0, 4, 5, 1), (1, 5, 6, 2),
        (2, 6, 7, 3), (4, 0, 3, 7)
    ]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    return mesh

def create_torus_mesh(name, R, r, seg_major=48, seg_minor=16):
    """Sử dụng lại Torus để tạo vòng năng lượng rỗng ở giữa"""
    verts = []
    faces = []
    for i in range(seg_major):
        u = 2 * math.pi * i / seg_major
        cos_u, sin_u = math.cos(u), math.sin(u)
        for j in range(seg_minor):
            v = 2 * math.pi * j / seg_minor
            cos_v, sin_v = math.cos(v), math.sin(v)
            
            x = (R + r * cos_v) * cos_u
            y = (R + r * cos_v) * sin_u
            z = r * sin_v
            verts.append((x, y, z))
            
    for i in range(seg_major):
        i_next = (i + 1) % seg_major
        for j in range(seg_minor):
            j_next = (j + 1) % seg_minor
            v1 = i * seg_minor + j
            v2 = i_next * seg_minor + j
            v3 = i_next * seg_minor + j_next
            v4 = i * seg_minor + j_next
            faces.append((v1, v2, v3, v4))
            
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    return mesh

def create_domain(domain_size, resolution):
    mesh = create_cube_mesh("DomainCube", domain_size)
    domain_obj = bpy.data.objects.new("EnergyDomain", mesh)
    bpy.context.scene.collection.objects.link(domain_obj)

    mod = domain_obj.modifiers.new(name="Fluid", type="FLUID")
    mod.fluid_type = "DOMAIN"
    ds = mod.domain_settings
    ds.domain_type = "GAS"
    ds.resolution_max = resolution
    
    ds.use_adaptive_domain = True
    ds.cache_type = "ALL"
    
    cache_dir = os.path.abspath("./_energy_sim_cache")
    if os.path.exists(cache_dir):
        shutil.rmtree(cache_dir, ignore_errors=True)
    os.makedirs(cache_dir, exist_ok=True)
    ds.cache_directory = cache_dir

    ds.cache_frame_start = 1
    ds.cache_frame_end = SIM_FRAMES
    ds.vorticity = ARGS.vorticity
    
    # Cho phép khói tan dần đều (Linear) trong khoảng 35 frame, tránh bị đen thui đột ngột
    ds.use_dissolve_smoke = True
    ds.use_dissolve_smoke_log = False
    ds.dissolve_speed = 35

    ds.use_noise = True
    ds.noise_scale = ARGS.noise_upres
    ds.noise_strength = ARGS.noise_strength

    return domain_obj, ds

def create_flow(domain_size, resolution):
    src_radius = ARGS.source_radius
    
    # Torus dày hơn một chút
    mesh = create_torus_mesh("FlowTorus", R=src_radius, r=src_radius * 0.4)
    flow_obj = bpy.data.objects.new("EnergyFlowSource", mesh)
    flow_obj.rotation_euler = (math.radians(90), 0, 0)
    bpy.context.scene.collection.objects.link(flow_obj)

    mod = flow_obj.modifiers.new(name="Fluid", type="FLUID")
    mod.fluid_type = "FLOW"
    fs = mod.flow_settings
    fs.flow_type = "SMOKE"
    fs.flow_behavior = "INFLOW"
    fs.flow_source = "MESH"
    
    # ĐIỂM MẤU CHỐT: Tăng surface_distance để nội suy vòng Torus liền khối, không bị đứt 4 khúc
    fs.surface_distance = 0.8  
    
    fs.density = ARGS.inflow_density
    fs.smoke_color = (1.0, 1.0, 1.0) 

    # Bật inflow trong 2 frame đầu tiên để tạo nguồn
    fs.use_inflow = True
    fs.keyframe_insert(data_path="use_inflow", frame=1)
    fs.use_inflow = False
    fs.keyframe_insert(data_path="use_inflow", frame=ARGS.burst_frames + 1)

    flow_obj.hide_render = True
    return flow_obj

def create_explosion_forces():
    """Tạo lực đẩy từ tâm để bung vòng Torus ra thành Shockwave"""
    # 1. Force Field (Chỉ đẩy ở 2 frame đầu)
    bpy.ops.object.effector_add(type='FORCE', location=(0, 0, 0))
    force_obj = bpy.context.active_object
    force_obj.name = "ExplosionForce"
    force = force_obj.field
    force.strength = ARGS.force_strength
    
    force.keyframe_insert(data_path="strength", frame=1)
    force.strength = 0.0
    force.keyframe_insert(data_path="strength", frame=ARGS.burst_frames + 1)

    # 2. Turbulence Field (Nhiễu nhẹ quanh viền)
    bpy.ops.object.effector_add(type='TURBULENCE', location=(0, 0, 0))
    turb_obj = bpy.context.active_object
    turb_obj.name = "ExplosionTurbulence"
    turb = turb_obj.field
    turb.strength = ARGS.turb_strength
    turb.size = 1.5

    # 3. Drag Field (Giữ khối khí lan ra từ từ, không văng mất khỏi màn hình)
    bpy.ops.object.effector_add(type='DRAG', location=(0, 0, 0))
    drag_obj = bpy.context.active_object
    drag_obj.name = "ExplosionDrag"
    drag = drag_obj.field
    drag.linear_drag = 1.0
    
    return force_obj, turb_obj, drag_obj

def setup_camera_and_world():
    domain_size = ARGS.domain_size
    cam_dist = max(10.0, domain_size * 1.5)
    
    cam_data = bpy.data.cameras.new("Camera")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = domain_size * 1.0 
    
    cam_obj = bpy.data.objects.new("Camera", cam_data)
    cam_obj.location = (0, -cam_dist, 0)
    cam_obj.rotation_euler = (math.radians(90), 0, 0)
    bpy.context.scene.collection.objects.link(cam_obj)
    bpy.context.scene.camera = cam_obj

    world = bpy.data.worlds.new("World_Dark")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    if bg:
        bg.inputs["Color"].default_value = (0, 0, 0, 1)

    bpy.context.scene.world = world
    bpy.context.scene.render.film_transparent = True

def assign_pure_white_volume_material(domain_obj):
    mat = bpy.data.materials.new("EnergySimMat")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    density_attr = nt.nodes.new("ShaderNodeAttribute")
    density_attr.attribute_name = "density"

    vol = nt.nodes.new("ShaderNodeVolumePrincipled")
    emission_color_socket = "Emission Color" if "Emission Color" in vol.inputs else "Emission"
    vol.inputs[emission_color_socket].default_value = (1.0, 1.0, 1.0, 1.0)

    # Cân chỉnh lại độ sáng cho vừa phải
    density_mult = nt.nodes.new("ShaderNodeMath")
    density_mult.operation = "MULTIPLY"
    density_mult.inputs[1].default_value = 10.0
    nt.links.new(density_attr.outputs["Fac"], density_mult.inputs[0])
    nt.links.new(density_mult.outputs["Value"], vol.inputs["Density"])

    emission_mult = nt.nodes.new("ShaderNodeMath")
    emission_mult.operation = "MULTIPLY"
    emission_mult.inputs[1].default_value = 50.0
    nt.links.new(density_attr.outputs["Fac"], emission_mult.inputs[0])
    nt.links.new(emission_mult.outputs["Value"], vol.inputs["Emission Strength"])

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(vol.outputs["Volume"], out.inputs["Volume"])

    domain_obj.data.materials.append(mat)

def bake_simulation(domain_obj):
    print(f"[gen_energy_sim] Dang BAKE mo phong vat ly ({SIM_FRAMES} frames)... Vui long doi!")
    bpy.context.view_layer.objects.active = domain_obj
    domain_obj.select_set(True)
    bpy.ops.fluid.bake_all()
    print("[gen_energy_sim] BAKE hoan tat!")

def render_frames(domain_obj):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"

    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "NONE"
    scene.cycles.device = "CPU"
    scene.cycles.samples = ARGS.samples
    scene.cycles.volume_step_rate = 0.15

    scene.render.resolution_x = CELL
    scene.render.resolution_y = CELL
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"

    sample_points = set(
        1 + round(i * (SIM_FRAMES - 1) / (N_FRAMES - 1))
        for i in range(N_FRAMES)
    )
    sample_order = [
        1 + round(i * (SIM_FRAMES - 1) / (N_FRAMES - 1))
        for i in range(N_FRAMES)
    ]

    TMP_DIR = os.path.abspath("./_energy_sim_tmp")
    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)
    os.makedirs(TMP_DIR, exist_ok=True)

    print(f"[gen_energy_sim] Dang render hinh anh...")
    rendered = {}
    for f in range(1, SIM_FRAMES + 1):
        scene.frame_set(f)
        if f in sample_points:
            if f not in rendered:
                frame_path = os.path.join(TMP_DIR, f"frame_f{f:04d}.png")
                scene.render.filepath = frame_path
                bpy.ops.render.render(write_still=True)
                rendered[f] = frame_path
            print(f"  - Da render frame {f}")

    return TMP_DIR, sample_order, rendered

def stitch_atlas(out_path, sample_order, rendered):
    atlas_w = GRID * CELL
    atlas_h = GRID * CELL
    atlas_arr = np.zeros((atlas_h, atlas_w, 4), dtype=np.float32)

    for i, f in enumerate(sample_order):
        img = bpy.data.images.load(rendered[f])
        frame_pixels = np.empty(CELL * CELL * 4, dtype=np.float32)
        img.pixels.foreach_get(frame_pixels)
        frame_arr = frame_pixels.reshape((CELL, CELL, 4))

        col = i % GRID
        row = i // GRID
        ox = col * CELL
        oy = (GRID - 1 - row) * CELL

        atlas_arr[oy : oy + CELL, ox : ox + CELL, :] = frame_arr
        bpy.data.images.remove(img)

    atlas_img = bpy.data.images.new("EnergyExplosionAtlasSim", width=atlas_w, height=atlas_h, alpha=True)
    atlas_img.pixels.foreach_set(atlas_arr.ravel())
    out_abs = os.path.abspath(out_path)
    atlas_img.filepath_raw = out_abs
    atlas_img.file_format = "PNG"
    atlas_img.save()
    print(f"[gen_energy_sim] HOAN TAT: {out_abs}")

def main():
    clear_scene()
    
    sim_domain_size = ARGS.domain_size
    effective_resolution = ARGS.resolution

    domain_obj, ds = create_domain(sim_domain_size, effective_resolution)
    create_flow(sim_domain_size, effective_resolution)
    create_explosion_forces() 
    
    setup_camera_and_world()
    assign_pure_white_volume_material(domain_obj)

    bake_simulation(domain_obj)

    TMP_DIR, sample_order, rendered = render_frames(domain_obj)
    stitch_atlas(ARGS.out, sample_order, rendered)

    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)

if __name__ == "__main__":
    main()