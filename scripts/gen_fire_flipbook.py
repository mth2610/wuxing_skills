"""
gen_fire_flipbook.py
================

Procedural Fire Flipbook Generator
Blender 3.x / 4.x

Example:

blender --background --python gen_fire_flipbook.py -- \
    --out ./fire_atlas.png \
    --grid 8 \
    --cell 256 \
    --sim-frames 120 \
    --samples 8

(Phần 1)
"""

import bpy
import sys
import os
import math
import random
import shutil
import argparse
import numpy as np


# --------------------------------------------------------
# Args
# --------------------------------------------------------

def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--")+1:] if "--" in argv else []

    p = argparse.ArgumentParser()

    p.add_argument("--out", default="./fire_atlas.png")
    p.add_argument("--grid", type=int, default=8)
    p.add_argument("--cell", type=int, default=256)
    p.add_argument("--sim-frames", type=int, default=120)
    p.add_argument("--samples", type=int, default=8)
    p.add_argument("--seed", type=int, default=1234)

    return p.parse_args(argv)


ARGS = parse_args()

GRID = ARGS.grid
CELL = ARGS.cell
N_FRAMES = GRID * GRID
SIM_FRAMES = ARGS.sim_frames

random.seed(ARGS.seed)

TMP_DIR = os.path.abspath("./_fire_tmp")


# --------------------------------------------------------
# Utils
# --------------------------------------------------------

def clear_scene():

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)

    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)

    for w in list(bpy.data.worlds):
        if w.users == 0:
            bpy.data.worlds.remove(w)


# --------------------------------------------------------
# Fire Material
# --------------------------------------------------------

def create_fire():

    bpy.ops.mesh.primitive_uv_sphere_add(
        radius=1.35,
        location=(0,0,1.4)
    )

    obj = bpy.context.active_object
    obj.name = "Fire"

    mat = bpy.data.materials.new("FireMaterial")
    mat.use_nodes = True

    nt = mat.node_tree
    nt.nodes.clear()

    #
    # Nodes
    #

    tex = nt.nodes.new("ShaderNodeTexCoord")
    tex.location = (-1300,0)

    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.location = (-1100,0)

    #
    # Flow animation
    #

    mapping.inputs["Location"].default_value[1] = 0.0
    mapping.inputs["Location"].keyframe_insert(
        data_path="default_value",
        frame=1,
        index=1
    )

    mapping.inputs["Location"].default_value[1] = 3.8
    mapping.inputs["Location"].keyframe_insert(
        data_path="default_value",
        frame=SIM_FRAMES,
        index=1
    )

    #
    # Large Noise
    #

    noise1 = nt.nodes.new("ShaderNodeTexNoise")
    noise1.location = (-820,250)

    noise1.noise_dimensions = '4D'
    noise1.inputs["Scale"].default_value = 2.0
    noise1.inputs["Detail"].default_value = 4.0
    noise1.inputs["Roughness"].default_value = .55
    noise1.inputs["Distortion"].default_value = .45

    noise1.inputs["W"].default_value = 0.0
    noise1.inputs["W"].keyframe_insert(
        data_path="default_value",
        frame=1
    )

    noise1.inputs["W"].default_value = 5.0
    noise1.inputs["W"].keyframe_insert(
        data_path="default_value",
        frame=SIM_FRAMES
    )

    #
    # Medium Noise
    #

    noise2 = nt.nodes.new("ShaderNodeTexNoise")
    noise2.location = (-820,-80)

    noise2.noise_dimensions = '4D'
    noise2.inputs["Scale"].default_value = 6.5
    noise2.inputs["Detail"].default_value = 8.0
    noise2.inputs["Roughness"].default_value = .6

    noise2.inputs["W"].default_value = 0.0
    noise2.inputs["W"].keyframe_insert(
        data_path="default_value",
        frame=1
    )

    noise2.inputs["W"].default_value = 9.0
    noise2.inputs["W"].keyframe_insert(
        data_path="default_value",
        frame=SIM_FRAMES
    )

    #
    # Fine Noise
    #

    noise3 = nt.nodes.new("ShaderNodeTexNoise")
    noise3.location = (-820,-420)

    noise3.noise_dimensions = '4D'
    noise3.inputs["Scale"].default_value = 16
    noise3.inputs["Detail"].default_value = 12
    noise3.inputs["Roughness"].default_value = .7

    noise3.inputs["W"].default_value = 0.0
    noise3.inputs["W"].keyframe_insert(
        data_path="default_value",
        frame=1
    )

    noise3.inputs["W"].default_value = 15.0
    noise3.inputs["W"].keyframe_insert(
        data_path="default_value",
        frame=SIM_FRAMES
    )

    #
    # Sphere Falloff
    #

    vec_len = nt.nodes.new("ShaderNodeVectorMath")
    vec_len.operation = "LENGTH"
    vec_len.location = (-850,620)

    falloff = nt.nodes.new("ShaderNodeMapRange")
    falloff.location = (-620,620)

    falloff.inputs["From Min"].default_value = 0
    falloff.inputs["From Max"].default_value = 1.2
    falloff.inputs["To Min"].default_value = 1
    falloff.inputs["To Max"].default_value = 0

    #
    # Multiply noises
    #

    mul1 = nt.nodes.new("ShaderNodeMath")
    mul1.operation = "MULTIPLY"
    mul1.location = (-520,220)

    mul2 = nt.nodes.new("ShaderNodeMath")
    mul2.operation = "MULTIPLY"
    mul2.location = (-330,100)

    mul3 = nt.nodes.new("ShaderNodeMath")
    mul3.operation = "MULTIPLY"
    mul3.location = (-140,-10)

    #
    # Fire Shape Ramp
    #

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.location = (90,0)

    r = ramp.color_ramp

    r.elements.remove(r.elements[1])

    e0 = r.elements[0]
    e0.position = .18
    e0.color = (0,0,0,1)

    e1 = r.elements.new(.35)
    e1.color = (.4,.02,.0,1)

    e2 = r.elements.new(.55)
    e2.color = (.95,.18,.02,1)

    e3 = r.elements.new(.78)
    e3.color = (1,.6,.1,1)

    e4 = r.elements.new(.93)
    e4.color = (1,1,.85,1)
    
        #
    # Density Timeline
    #

    density = nt.nodes.new("ShaderNodeMath")
    density.operation = "MULTIPLY"
    density.location = (340,40)

    density.inputs[1].default_value = 0.0
    density.inputs[1].keyframe_insert(
        data_path="default_value",
        frame=1
    )

    density.inputs[1].default_value = 60.0
    density.inputs[1].keyframe_insert(
        data_path="default_value",
        frame=int(SIM_FRAMES*0.18)
    )

    density.inputs[1].default_value = 95.0
    density.inputs[1].keyframe_insert(
        data_path="default_value",
        frame=int(SIM_FRAMES*0.45)
    )

    density.inputs[1].default_value = 40.0
    density.inputs[1].keyframe_insert(
        data_path="default_value",
        frame=int(SIM_FRAMES*0.75)
    )

    density.inputs[1].default_value = 0.0
    density.inputs[1].keyframe_insert(
        data_path="default_value",
        frame=SIM_FRAMES
    )

    #
    # Flicker
    #

    flicker = nt.nodes.new("ShaderNodeMath")
    flicker.operation = "MULTIPLY"
    flicker.location = (340,-210)

    flicker.inputs[1].default_value = 90

    for f in range(1, SIM_FRAMES+1, 3):

        flicker.inputs[1].default_value = random.uniform(75,120)

        flicker.inputs[1].keyframe_insert(
            data_path="default_value",
            frame=f
        )

    #
    # Volume
    #

    volume = nt.nodes.new("ShaderNodeVolumePrincipled")
    volume.location = (620,-10)

    volume.inputs["Color"].default_value = (
        1.0,
        .58,
        .16,
        1.0
    )

    volume.inputs["Anisotropy"].default_value = .15

    #
    # Emission
    #

    volume.inputs["Emission Color"].default_value = (
        1.0,
        .55,
        .08,
        1.0
    )

    nt.links.new(
        flicker.outputs[0],
        volume.inputs["Emission Strength"]
    )

    nt.links.new(
        density.outputs[0],
        volume.inputs["Density"]
    )

    #
    # Output
    #

    output = nt.nodes.new("ShaderNodeOutputMaterial")
    output.location = (900,0)

    #
    # Links
    #

    nt.links.new(
        tex.outputs["Object"],
        mapping.inputs["Vector"]
    )

    nt.links.new(
        mapping.outputs["Vector"],
        noise1.inputs["Vector"]
    )

    nt.links.new(
        mapping.outputs["Vector"],
        noise2.inputs["Vector"]
    )

    nt.links.new(
        mapping.outputs["Vector"],
        noise3.inputs["Vector"]
    )

    nt.links.new(
        tex.outputs["Object"],
        vec_len.inputs[0]
    )

    nt.links.new(
        vec_len.outputs[1],
        falloff.inputs["Value"]
    )

    nt.links.new(
        noise1.outputs["Fac"],
        mul1.inputs[0]
    )

    nt.links.new(
        noise2.outputs["Fac"],
        mul1.inputs[1]
    )

    nt.links.new(
        mul1.outputs[0],
        mul2.inputs[0]
    )

    nt.links.new(
        noise3.outputs["Fac"],
        mul2.inputs[1]
    )

    nt.links.new(
        mul2.outputs[0],
        mul3.inputs[0]
    )

    nt.links.new(
        falloff.outputs["Result"],
        mul3.inputs[1]
    )

    nt.links.new(
        mul3.outputs[0],
        ramp.inputs["Fac"]
    )

    nt.links.new(
        ramp.outputs["Alpha"],
        density.inputs[0]
    )

    nt.links.new(
        ramp.outputs["Color"],
        volume.inputs["Color"]
    )

    nt.links.new(
        volume.outputs["Volume"],
        output.inputs["Volume"]
    )

    obj.data.materials.append(mat)

    return obj


# --------------------------------------------------------
# Camera
# --------------------------------------------------------

def setup_camera():

    bpy.ops.object.camera_add(
        location=(0,-4.8,1.4),
        rotation=(math.radians(90),0,0)
    )

    cam = bpy.context.active_object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = 2.0

    bpy.context.scene.camera = cam

    bpy.ops.object.light_add(
        type='AREA',
        location=(-2,-2,4)
    )

    key = bpy.context.active_object
    key.data.energy = 900
    key.data.size = 5

    bpy.ops.object.light_add(
        type='AREA',
        location=(2,2,1)
    )

    fill = bpy.context.active_object
    fill.data.energy = 250
    fill.data.size = 4

    world = bpy.data.worlds.new("World")
    world.use_nodes = True

    bpy.context.scene.world = world
    bpy.context.scene.render.film_transparent = True
    
    # --------------------------------------------------------
# Render
# --------------------------------------------------------

def render_frames():

    scene = bpy.context.scene

    scene.render.engine = "CYCLES"

    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "NONE"

    scene.cycles.device = "CPU"
    scene.cycles.samples = ARGS.samples
    scene.cycles.volume_step_rate = 0.35

    scene.render.resolution_x = CELL
    scene.render.resolution_y = CELL

    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"

    sample_points = [
        1 + round(i * (SIM_FRAMES - 1) / (N_FRAMES - 1))
        for i in range(N_FRAMES)
    ]

    print(f"[fire] Rendering {N_FRAMES} frames...")

    for idx, frame in enumerate(sample_points):

        scene.frame_set(frame)

        outfile = os.path.join(
            TMP_DIR,
            f"frame_{idx:03d}.png"
        )

        scene.render.filepath = outfile

        bpy.ops.render.render(write_still=True)


# --------------------------------------------------------
# Stitch Atlas
# --------------------------------------------------------

def stitch_atlas(out_path):

    atlas_w = GRID * CELL
    atlas_h = GRID * CELL

    atlas = np.zeros(
        (atlas_h, atlas_w, 4),
        dtype=np.float32
    )

    frames = sorted(
        f for f in os.listdir(TMP_DIR)
        if f.endswith(".png")
    )

    assert len(frames) >= N_FRAMES

    for i, fname in enumerate(frames[:N_FRAMES]):

        img = bpy.data.images.load(
            os.path.join(TMP_DIR, fname)
        )

        pixels = np.empty(
            CELL * CELL * 4,
            dtype=np.float32
        )

        img.pixels.foreach_get(pixels)

        arr = pixels.reshape(
            (CELL, CELL, 4)
        )

        col = i % GRID
        row = i // GRID

        ox = col * CELL
        oy = (GRID - 1 - row) * CELL

        atlas[
            oy:oy+CELL,
            ox:ox+CELL,
            :
        ] = arr

        bpy.data.images.remove(img)

    atlas_img = bpy.data.images.new(
        "FireAtlas",
        width=atlas_w,
        height=atlas_h,
        alpha=True
    )

    atlas_img.pixels.foreach_set(
        atlas.ravel()
    )

    atlas_img.filepath_raw = os.path.abspath(out_path)
    atlas_img.file_format = "PNG"

    atlas_img.save()

    print("[fire] Saved:", os.path.abspath(out_path))


# --------------------------------------------------------
# Main
# --------------------------------------------------------

def main():

    if os.path.exists(TMP_DIR):
        shutil.rmtree(
            TMP_DIR,
            ignore_errors=True
        )

    os.makedirs(
        TMP_DIR,
        exist_ok=True
    )

    clear_scene()

    create_fire()

    setup_camera()

    render_frames()

    stitch_atlas(
        ARGS.out
    )

    shutil.rmtree(
        TMP_DIR,
        ignore_errors=True
    )

    print("[fire] Done.")


if __name__ == "__main__":
    main()