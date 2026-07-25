"""
gen_smoke_flipbook.py
======================
Script tạo Flipbook Atlas Khói 3D hoàn chỉnh cho Game VFX (Blender 3.x / 4.x).
blender --background --python gen_smoke_flipbook.py --out ./smoke_atlas_8x8.png --grid 8 --cell 256 --sim-frames 120 --samples 1
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
    p.add_argument("--out", default="./smoke_atlas_8x8.png")
    p.add_argument("--grid", type=int, default=8)
    p.add_argument("--cell", type=int, default=256)
    p.add_argument("--sim-frames", type=int, default=120)
    p.add_argument("--samples", type=int, default=16)
    p.add_argument("--seed", type=int, default=1234)
    return p.parse_args(argv)


ARGS = parse_args()
GRID = ARGS.grid
CELL = ARGS.cell
N_FRAMES = GRID * GRID
SIM_FRAMES = ARGS.sim_frames
random.seed(ARGS.seed)

TMP_DIR = os.path.abspath("./_smoke_flipbook_tmp")


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)


def create_procedural_smoke():
    # 1. Tạo Domain quả cầu khói
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.8, location=(0, 0, 1.5))
    smoke_obj = bpy.context.active_object
    smoke_obj.name = "ProceduralSmokeDomain"

    mat = bpy.data.materials.new("ProceduralSmokeMat")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    # 2. Tọa độ & Falloff dạng cầu
    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    vec_math = nt.nodes.new("ShaderNodeVectorMath")
    vec_math.operation = "LENGTH"

    falloff = nt.nodes.new("ShaderNodeMapRange")
    falloff.inputs["From Min"].default_value = 0.0
    falloff.inputs["From Max"].default_value = 1.7
    falloff.inputs["To Min"].default_value = 1.0
    falloff.inputs["To Max"].default_value = 0.0

    # 3. Noise 4D biến thiên theo thời gian (Trục W)
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.noise_dimensions = "4D"
    noise.inputs["Scale"].default_value = 2.2
    noise.inputs["Detail"].default_value = 4.0
    noise.inputs["Roughness"].default_value = 0.6
    noise.inputs["Distortion"].default_value = 1.2

    noise.inputs["W"].default_value = 0.0
    noise.inputs["W"].keyframe_insert(data_path="default_value", frame=1)
    noise.inputs["W"].default_value = 2.8
    noise.inputs["W"].keyframe_insert(
        data_path="default_value", frame=SIM_FRAMES
    )

    mult = nt.nodes.new("ShaderNodeMath")
    mult.operation = "MULTIPLY"

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.35
    ramp.color_ramp.elements[1].position = 0.75

    # 4. Keyframe sinh trưởng mật độ (Density Timeline)
    density_scale = nt.nodes.new("ShaderNodeMath")
    density_scale.operation = "MULTIPLY"

    density_scale.inputs[1].default_value = 0.0
    density_scale.inputs[1].keyframe_insert(data_path="default_value", frame=1)

    density_scale.inputs[1].default_value = 45.0
    density_scale.inputs[1].keyframe_insert(
        data_path="default_value", frame=round(SIM_FRAMES * 0.25)
    )

    density_scale.inputs[1].default_value = 25.0
    density_scale.inputs[1].keyframe_insert(
        data_path="default_value", frame=round(SIM_FRAMES * 0.65)
    )

    density_scale.inputs[1].default_value = 0.0
    density_scale.inputs[1].keyframe_insert(
        data_path="default_value", frame=SIM_FRAMES
    )

    # 5. Shader Volume (Màu xám sáng hứng sáng tốt)
    vol = nt.nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = (0.85, 0.86, 0.90, 1.0)
    vol.inputs["Anisotropy"].default_value = 0.3

    out = nt.nodes.new("ShaderNodeOutputMaterial")

    # Nối dây Shader Node Tree
    nt.links.new(tex_coord.outputs["Object"], vec_math.inputs[0])
    nt.links.new(vec_math.outputs["Value"], falloff.inputs["Value"])
    nt.links.new(tex_coord.outputs["Object"], noise.inputs["Vector"])

    nt.links.new(falloff.outputs["Result"], mult.inputs[0])
    nt.links.new(noise.outputs["Fac"], mult.inputs[1])
    nt.links.new(mult.outputs["Value"], ramp.inputs["Fac"])

    nt.links.new(ramp.outputs["Color"], density_scale.inputs[0])
    nt.links.new(density_scale.outputs["Value"], vol.inputs["Density"])
    nt.links.new(vol.outputs["Volume"], out.inputs["Volume"])

    smoke_obj.data.materials.append(mat)
    return smoke_obj


def setup_camera_and_lights():
    # Camera Orthographic zoom sát khói (~80% khung hình)
    bpy.ops.object.camera_add(
        location=(0, -5.0, 1.5), rotation=(math.radians(90), 0, 0)
    )
    cam = bpy.context.active_object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = 2.3
    bpy.context.scene.camera = cam

    # Key light hắt sáng từ trên xuống
    bpy.ops.object.light_add(type="AREA", location=(-2.0, -2.5, 3.5))
    key = bpy.context.active_object
    key.data.energy = 600
    key.data.size = 4.0

    # Fill light chiếu sáng viền
    bpy.ops.object.light_add(type="AREA", location=(2.0, 2.0, 1.0))
    fill = bpy.context.active_object
    fill.data.energy = 250
    fill.data.size = 3.0

    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    bpy.context.scene.world = world
    bpy.context.scene.render.film_transparent = True


def render_frames():
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

    sample_points = [
        1 + round(i * (SIM_FRAMES - 1) / (N_FRAMES - 1))
        for i in range(N_FRAMES)
    ]

    print(f"[gen_smoke] Đang render {N_FRAMES} frames...")
    for idx, f in enumerate(sample_points):
        scene.frame_set(f)
        frame_path = os.path.join(TMP_DIR, f"frame_{idx:03d}.png")
        scene.render.filepath = frame_path
        bpy.ops.render.render(write_still=True)


def stitch_atlas(out_path):
    atlas_w = GRID * CELL
    atlas_h = GRID * CELL
    atlas_arr = np.zeros((atlas_h, atlas_w, 4), dtype=np.float32)

    frame_files = sorted(
        [
            f
            for f in os.listdir(TMP_DIR)
            if f.startswith("frame_") and f.endswith(".png")
        ]
    )
    assert (
        len(frame_files) >= N_FRAMES
    ), f"Thiếu frame: có {len(frame_files)}, cần {N_FRAMES}."

    for i, fname in enumerate(frame_files[:N_FRAMES]):
        img = bpy.data.images.load(os.path.join(TMP_DIR, fname))

        frame_pixels = np.empty(CELL * CELL * 4, dtype=np.float32)
        img.pixels.foreach_get(frame_pixels)
        frame_arr = frame_pixels.reshape((CELL, CELL, 4))

        col = i % GRID
        row = i // GRID
        ox = col * CELL

        # ⚡ FIX: Lật ngược chiều hàng để xếp đúng thứ tự Top-Left -> Bottom-Right
        oy = (GRID - 1 - row) * CELL

        atlas_arr[oy : oy + CELL, ox : ox + CELL, :] = frame_arr
        bpy.data.images.remove(img)

    atlas_img = bpy.data.images.new(
        "SmokeFlipbookAtlas", width=atlas_w, height=atlas_h, alpha=True
    )
    atlas_img.pixels.foreach_set(atlas_arr.ravel())

    out_abs = os.path.abspath(out_path)
    atlas_img.filepath_raw = out_abs
    atlas_img.file_format = "PNG"
    atlas_img.save()
    print(
        f"[gen_smoke] HOÀN TẤT! Đã xuất atlas đúng thứ tự Top->Down: {out_abs}"
    )

def main():
    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)
    os.makedirs(TMP_DIR, exist_ok=True)

    clear_scene()
    create_procedural_smoke()
    setup_camera_and_lights()
    render_frames()
    stitch_atlas(ARGS.out)

    # Dọn dẹp thư mục tạm sau khi hoàn tất
    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()