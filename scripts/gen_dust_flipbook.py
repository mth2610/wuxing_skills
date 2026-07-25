"""
gen_dust_flipbook_white_4x4.py
==============================
Script tạo Flipbook Atlas Bụi Trắng (fb_dust_4x4) tối ưu cho Game VFX.

- Grid: 4x4 (Power-of-Two 1024x1024 với cell 256x256)
- Color: Pure White (đã tối ưu màu & ánh sáng để dễ Tint)
- Alpha: Premultiplied Alpha RGBA
- Layout: Đã camera zoom sát, căn chỉnh frame bỏ bớt khoảng trống thừa.

run: blender --background --python gen_dust_flipbook_white_4x4.py --out ./fb_dust_4x4.png --grid 4 --cell 256
"""

import argparse
import math
import os
import random
import shutil
import sys
import bpy
import numpy as np


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1 :] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--out", default="./fb_dust_white_4x4.png")
    p.add_argument("--grid", type=int, default=4)
    p.add_argument("--cell", type=int, default=256)
    p.add_argument("--sim-frames", type=int, default=90)
    p.add_argument("--samples", type=int, default=16)
    p.add_argument("--seed", type=int, default=42)
    return p.parse_args(argv)


ARGS = parse_args()
GRID = ARGS.grid
CELL = ARGS.cell
N_FRAMES = GRID * GRID
SIM_FRAMES = ARGS.sim_frames
random.seed(ARGS.seed)

TMP_DIR = os.path.abspath("./_dust_white_tmp")


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)


def create_procedural_dust_white():
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.6, location=(0, 0, 1.2))
    dust_obj = bpy.context.active_object
    dust_obj.name = "ProceduralDustDomain"

    mat = bpy.data.materials.new("ProceduralDustMatWhite")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    # 1. Tọa độ & Falloff dạng cầu (Mở rộng From Max để bụi lan rộng lề hơn)
    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    vec_math = nt.nodes.new("ShaderNodeVectorMath")
    vec_math.operation = "LENGTH"

    falloff = nt.nodes.new("ShaderNodeMapRange")
    falloff.inputs["From Min"].default_value = 0.0
    falloff.inputs["From Max"].default_value = 1.8
    falloff.inputs["To Min"].default_value = 1.0
    falloff.inputs["To Max"].default_value = 0.0

    # 2. Noise 4D hạt mịn
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.noise_dimensions = "4D"
    noise.inputs["Scale"].default_value = 4.5
    noise.inputs["Detail"].default_value = 6.0
    noise.inputs["Roughness"].default_value = 0.55
    noise.inputs["Distortion"].default_value = 0.8

    noise.inputs["W"].default_value = 0.0
    noise.inputs["W"].keyframe_insert(data_path="default_value", frame=1)
    noise.inputs["W"].default_value = 3.2
    noise.inputs["W"].keyframe_insert(data_path="default_value", frame=SIM_FRAMES)

    mult = nt.nodes.new("ShaderNodeMath")
    mult.operation = "MULTIPLY"

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.40
    ramp.color_ramp.elements[1].position = 0.70

    # 3. Keyframe Density
    density_scale = nt.nodes.new("ShaderNodeMath")
    density_scale.operation = "MULTIPLY"

    density_scale.inputs[1].default_value = 0.0
    density_scale.inputs[1].keyframe_insert(data_path="default_value", frame=1)

    density_scale.inputs[1].default_value = 25.0
    density_scale.inputs[1].keyframe_insert(
        data_path="default_value", frame=round(SIM_FRAMES * 0.2)
    )

    density_scale.inputs[1].default_value = 12.0
    density_scale.inputs[1].keyframe_insert(
        data_path="default_value", frame=round(SIM_FRAMES * 0.6)
    )

    density_scale.inputs[1].default_value = 0.0
    density_scale.inputs[1].keyframe_insert(
        data_path="default_value", frame=SIM_FRAMES
    )

    # 4. Shader Volume Trắng Thuần (1.0, 1.0, 1.0)
    vol = nt.nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    vol.inputs["Anisotropy"].default_value = 0.2

    out = nt.nodes.new("ShaderNodeOutputMaterial")

    # Nối dây Shader
    nt.links.new(tex_coord.outputs["Object"], vec_math.inputs[0])
    nt.links.new(vec_math.outputs["Value"], falloff.inputs["Value"])
    nt.links.new(tex_coord.outputs["Object"], noise.inputs["Vector"])

    nt.links.new(falloff.outputs["Result"], mult.inputs[0])
    nt.links.new(noise.outputs["Fac"], mult.inputs[1])
    nt.links.new(mult.outputs["Value"], ramp.inputs["Fac"])

    nt.links.new(ramp.outputs["Color"], density_scale.inputs[0])
    nt.links.new(density_scale.outputs["Value"], vol.inputs["Density"])
    nt.links.new(vol.outputs["Volume"], out.inputs["Volume"])

    dust_obj.data.materials.append(mat)
    return dust_obj


def setup_camera_and_lights():
    bpy.ops.object.camera_add(
        location=(0, -4.5, 1.2), rotation=(math.radians(90), 0, 0)
    )
    cam = bpy.context.active_object
    cam.data.type = "ORTHO"
    # Giảm ortho_scale xuống 1.5 để camera zoom sát bụi, xóa bỏ khoảng trống thừa
    cam.data.ortho_scale = 1.5
    bpy.context.scene.camera = cam

    # Đèn chính & phụ dạng Neutral White
    bpy.ops.object.light_add(type="AREA", location=(-2.0, -2.0, 3.0))
    key = bpy.context.active_object
    key.data.color = (1.0, 1.0, 1.0)
    key.data.energy = 550
    key.data.size = 3.5

    bpy.ops.object.light_add(type="AREA", location=(2.0, 1.5, 1.0))
    fill = bpy.context.active_object
    fill.data.color = (1.0, 1.0, 1.0)
    fill.data.energy = 220
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

    # Trimming: Lấy từ frame 6 đến 84 để bỏ qua các frame hoàn toàn trống ở đầu/cuối
    start_f = 6
    end_f = SIM_FRAMES - 6

    sample_points = [
        round(start_f + i * (end_f - start_f) / (N_FRAMES - 1))
        for i in range(N_FRAMES)
    ]

    print(f"[gen_dust_white_4x4] Đang render {N_FRAMES} frames (Tối ưu padding)...")
    for idx, f in enumerate(sample_points):
        scene.frame_set(f)
        frame_path = os.path.join(TMP_DIR, f"frame_{idx:03d}.png")
        scene.render.filepath = frame_path
        bpy.ops.render.render(write_still=True)


def stitch_atlas_premultiplied(out_path):
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
    assert len(frame_files) >= N_FRAMES, f"Thiếu frame: cần {N_FRAMES}."

    for i, fname in enumerate(frame_files[:N_FRAMES]):
        img = bpy.data.images.load(os.path.join(TMP_DIR, fname))

        frame_pixels = np.empty(CELL * CELL * 4, dtype=np.float32)
        img.pixels.foreach_get(frame_pixels)
        frame_arr = frame_pixels.reshape((CELL, CELL, 4))

        # Premultiplied Alpha calculation: RGB = RGB * Alpha
        rgb = frame_arr[:, :, :3]
        alpha = frame_arr[:, :, 3:4]
        frame_arr[:, :, :3] = rgb * alpha

        col = i % GRID
        row = i // GRID
        ox = col * CELL
        oy = (GRID - 1 - row) * CELL

        atlas_arr[oy : oy + CELL, ox : ox + CELL, :] = frame_arr
        bpy.data.images.remove(img)

    atlas_img = bpy.data.images.new(
        "DustFlipbookWhite4x4Atlas", width=atlas_w, height=atlas_h, alpha=True
    )
    atlas_img.pixels.foreach_set(atlas_arr.ravel())

    out_abs = os.path.abspath(out_path)
    atlas_img.filepath_raw = out_abs
    atlas_img.file_format = "PNG"
    atlas_img.save()
    print(f"[gen_dust_white_4x4] HOÀN TẤT! Đã xuất atlas: {out_abs}")


def main():
    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)
    os.makedirs(TMP_DIR, exist_ok=True)

    clear_scene()
    create_procedural_dust_white()
    setup_camera_and_lights()
    render_frames()
    stitch_atlas_premultiplied(ARGS.out)

    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()