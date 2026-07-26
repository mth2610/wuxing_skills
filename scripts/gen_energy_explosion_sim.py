"""
gen_energy_explosion_sim.py
==============================
Flipbook Atlas Vụ nổ Năng lượng - PHIÊN BẢN FIX LỖI ĐỨNG IM BẰNG FORCE FIELD VÀ BAKE

test:
blender --background --python gen_energy_explosion_sim.py -- \
    --out ./test_energy_atlas.png \
    --grid 4 \
    --cell 64 \
    --sim-frames 40 \
    --resolution 64 \
    --samples 8
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
    
    p.add_argument("--burst-frames", type=int, default=3)
    # Thay vì dùng velocity, ta dùng Force Field mạnh để tạo vụ nổ
    p.add_argument("--force-strength", type=float, default=18.0) 
    p.add_argument("--vorticity", type=float, default=2.5)
    p.add_argument("--dissolve-speed", type=int, default=65)
    
    p.add_argument("--no-dissolve", action="store_true")
    p.add_argument("--linear-dissolve", action="store_true")
    p.add_argument("--inflow-density", type=float, default=4.0)
    p.add_argument("--source-radius", type=float, default=None)
    p.add_argument("--shell-thickness", type=float, default=0.2)
    p.add_argument("--domain-size", type=float, default=9.0)
    return p.parse_args(argv)

ARGS = parse_args()
GRID = ARGS.grid
CELL = ARGS.cell
N_FRAMES = GRID * GRID
SIM_FRAMES = ARGS.sim_frames
random.seed(ARGS.seed)

def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)

def create_domain(domain_size, resolution):
    bpy.ops.mesh.primitive_cube_add(size=domain_size, location=(0, 0, 0))
    domain_obj = bpy.context.active_object
    domain_obj.name = "EnergyDomain"

    bpy.ops.object.modifier_add(type="FLUID")
    mod = domain_obj.modifiers["Fluid"]
    mod.fluid_type = "DOMAIN"
    ds = mod.domain_settings
    ds.domain_type = "GAS"
    ds.resolution_max = resolution
    ds.use_adaptive_domain = False

    # CHUYỂN SANG BAKE ALL: Đảm bảo mô phỏng chạy thật 100%, không bị kẹt cache
    ds.cache_type = "ALL"
    
    # Đặt thư mục cache cụ thể và làm sạch nó trước khi chạy
    cache_dir = os.path.abspath("./_energy_sim_cache")
    if os.path.exists(cache_dir):
        shutil.rmtree(cache_dir, ignore_errors=True)
    os.makedirs(cache_dir, exist_ok=True)
    ds.cache_directory = cache_dir

    ds.cache_frame_start = 1
    ds.cache_frame_end = SIM_FRAMES

    ds.vorticity = ARGS.vorticity
    ds.use_dissolve_smoke = not ARGS.no_dissolve
    ds.use_dissolve_smoke_log = not ARGS.linear_dissolve
    ds.dissolve_speed = ARGS.dissolve_speed

    return domain_obj, ds

def create_flow(domain_size, resolution):
    src_radius = ARGS.source_radius if ARGS.source_radius is not None else ARGS.domain_size * 0.11
    
    bpy.ops.mesh.primitive_torus_add(
        major_radius=src_radius, 
        minor_radius=src_radius * 0.12, 
        location=(0, 0, 0),
        rotation=(math.radians(90), 0, 0)
    )
    flow_obj = bpy.context.active_object
    flow_obj.name = "EnergyFlowSource"

    bpy.ops.object.modifier_add(type="FLUID")
    mod = flow_obj.modifiers["Fluid"]
    mod.fluid_type = "FLOW"
    fs = mod.flow_settings
    fs.flow_type = "SMOKE"
    fs.flow_behavior = "INFLOW"
    fs.flow_source = "MESH"
    fs.surface_distance = ARGS.shell_thickness
    
    fs.density = ARGS.inflow_density * 5.0
    fs.smoke_color = (1.0, 1.0, 1.0) 

    # Tắt initial velocity đi vì nó triệt tiêu nhau, để Force Field lo việc đẩy
    fs.use_initial_velocity = False

    fs.use_inflow = True
    fs.keyframe_insert(data_path="use_inflow", frame=1)
    fs.use_inflow = False
    fs.keyframe_insert(data_path="use_inflow", frame=ARGS.burst_frames + 1)

    flow_obj.hide_render = True
    return flow_obj

def create_explosion_force():
    # Tạo lực đẩy cực mạnh ở tâm để tạo sóng xung kích (Shockwave)
    bpy.ops.object.effector_add(type='FORCE', radius=1.0, enter_editmode=False, align='WORLD', location=(0, 0, 0))
    force_obj = bpy.context.active_object
    force_obj.name = "ExplosionForce"
    force = force_obj.field
    
    force.strength = ARGS.force_strength
    force.flow = 0.5  # Giúp lực tác động tự nhiên hơn vào khói
    
    # Chỉ bật lực đẩy mạnh trong vài frame đầu, sau đó tắt để khói trôi ra ngoài tự nhiên
    force.keyframe_insert(data_path="strength", frame=1)
    force.strength = 0.0
    force.keyframe_insert(data_path="strength", frame=ARGS.burst_frames + 2)

    return force_obj

def setup_camera_and_world():
    domain_size = ARGS.domain_size
    cam_dist = max(10.0, domain_size * 1.2)
    bpy.ops.object.camera_add(location=(0, -cam_dist, 0), rotation=(math.radians(90), 0, 0))
    cam = bpy.context.active_object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = domain_size * 1.15
    bpy.context.scene.camera = cam

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

    density_mult = nt.nodes.new("ShaderNodeMath")
    density_mult.operation = "MULTIPLY"
    density_mult.inputs[1].default_value = 10.0
    nt.links.new(density_attr.outputs["Fac"], density_mult.inputs[0])
    nt.links.new(density_mult.outputs["Value"], vol.inputs["Density"])

    emission_mult = nt.nodes.new("ShaderNodeMath")
    emission_mult.operation = "MULTIPLY"
    emission_mult.inputs[1].default_value = 80.0
    nt.links.new(density_attr.outputs["Fac"], emission_mult.inputs[0])
    nt.links.new(emission_mult.outputs["Value"], vol.inputs["Emission Strength"])

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(vol.outputs["Volume"], out.inputs["Volume"])

    domain_obj.data.materials.append(mat)

def bake_simulation(domain_obj):
    print(f"[gen_energy_sim] Dang BAKE mo phong vat ly ({SIM_FRAMES} frames)... Vui long doi!")
    bpy.context.view_layer.objects.active = domain_obj
    domain_obj.select_set(True)
    # Lệnh nướng mô phỏng
    bpy.ops.fluid.bake_all()
    print("[gen_energy_sim] BAKE hoan tat!")

def render_frames(domain_obj):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"

    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "NONE"
    scene.cycles.device = "CPU"
    scene.cycles.samples = ARGS.samples
    scene.cycles.volume_step_rate = 0.2

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
            idx = sample_order.index(f) if f not in rendered else None
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
    create_explosion_force() # Đưa Force Field vào hoạt động
    
    setup_camera_and_world()
    assign_pure_white_volume_material(domain_obj)

    # Thực hiện lệnh Bake đàng hoàng trước khi Render
    bake_simulation(domain_obj)

    TMP_DIR, sample_order, rendered = render_frames(domain_obj)
    stitch_atlas(ARGS.out, sample_order, rendered)

    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)

if __name__ == "__main__":
    main()