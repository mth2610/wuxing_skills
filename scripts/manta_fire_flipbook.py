#!/usr/bin/env python3
"""Fire flipbook baked with MANTAFLOW (Blender's fluid solver), rendered in Eevee.

    blender --background --python scripts/manta_fire_flipbook.py -- --quick
    blender --background --python scripts/manta_fire_flipbook.py --

(the bare `--` is Blender's separator; everything after it is ours)

WHY THIS AND NOT THE TWO EARLIER ATTEMPTS

    gen_fire_flipbook.py  — Blender + Cycles. Audited in E4 at 4.1% cell coverage
    and height/width 1.00: it was rendering a spherical PUFF, and Cycles volume
    at low sample counts is stochastic, which is what E4 follow-up 4 traced the
    writhing dissipation rim to.

    sim_fire_flipbook.py  — a hand-written numpy solver. It works and needs no
    Blender (kept as the fast fallback), but 200 lines of semi-Lagrangian +
    Jacobi cannot compete with Mantaflow's FLIP/MacCormack advection, wavelet
    turbulence and its dedicated fuel/flame combustion model.

    So: Mantaflow for the SIMULATION (it is already embedded in Blender — there
    is no standalone build to install; `pip` has no mantaflow package at all),
    and **Eevee** for the RENDER. Eevee rasterises volumes instead of path
    tracing them, so the noise that made the previous sheets writhe is gone by
    construction rather than by raising sample counts.

OUTPUT
    RGB = greyscale flame/temperature, A = smoke opacity. No colour is baked in:
    per F3 the black-body ramp is applied at the call site. Straight alpha.
"""

import argparse
import math
import os
import sys
import time

import bpy


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true",
                    help="res 32 + 64px cells: a couple of minutes, for iterating")
    ap.add_argument("--out", default="fire_atlas_manta_8x8.png")
    ap.add_argument("--grid", type=int, default=8)
    ap.add_argument("--cell", type=int, default=None)
    ap.add_argument("--res", type=int, default=None, help="Mantaflow resolution_max")
    # Shape knobs, exposed for the same reason the numpy script exposes them:
    # this is the loop an artist runs, and it must not require editing code.
    ap.add_argument("--buoyancy", type=float, default=3.0, help="domain beta (heat lift)")
    ap.add_argument("--vorticity", type=float, default=0.35)
    ap.add_argument("--burn", type=float, default=0.9, help="burning rate")
    ap.add_argument("--flame-smoke", type=float, default=0.8, help="soot produced by flame")
    ap.add_argument("--emission", type=float, default=0.9,
                    help="flame emission gain. Above ~1.5 every flame pixel "
                         "clips to pure white and the sheet loses the "
                         "temperature gradient it exists to carry.")
    # parse_args(argv) — NOT parse_args(): argparse defaults to sys.argv, which
    # under Blender still holds Blender's own flags.
    return ap.parse_args(argv)


def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def build_scene(a, res, frames):
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = frames

    # ── Domain ───────────────────────────────────────────────────────────────
    # TALL, not cubic. This is the whole structural fix from the E4 audit: a
    # cubic domain cannot produce height/width above ~1, because the flame is
    # bounded by the box it burns in.
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0, 0, 2.0))
    dom = bpy.context.object
    dom.name = "Domain"
    dom.scale = (0.7, 0.7, 2.0)
    bpy.ops.object.transform_apply(scale=True)

    bpy.ops.object.modifier_add(type='FLUID')
    dom.modifiers["Fluid"].fluid_type = 'DOMAIN'
    ds = dom.modifiers["Fluid"].domain_settings
    ds.domain_type = 'GAS'
    ds.resolution_max = res
    ds.use_adaptive_domain = False
    ds.cache_frame_start = 1
    ds.cache_frame_end = frames
    ds.cache_type = 'ALL'
    ds.cache_directory = os.path.join(CACHE_DIR, "manta")

    ds.alpha = -0.35          # density pulls DOWN (soot falls back)
    ds.beta = a.buoyancy      # heat lifts
    ds.vorticity = a.vorticity
    ds.burning_rate = a.burn
    ds.flame_smoke = a.flame_smoke
    ds.flame_vorticity = 0.6
    ds.flame_max_temp = 2.4
    ds.dissolve_speed = max(12, frames)
    ds.use_dissolve_smoke = True
    if hasattr(ds, "use_noise"):
        # Wavelet turbulence: the detail a hand-written solver cannot afford.
        # Off for --quick because it multiplies bake time by the upres factor.
        ds.use_noise = not a.quick
        if ds.use_noise:
            ds.noise_scale = 2

    # ── Flow (fuel inflow at the floor) ──────────────────────────────────────
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.26, location=(0, 0, 0.3))
    flow = bpy.context.object
    flow.name = "Fuel"
    flow.scale = (1.0, 1.0, 0.45)
    bpy.ops.object.transform_apply(scale=True)
    bpy.ops.object.modifier_add(type='FLUID')
    flow.modifiers["Fluid"].fluid_type = 'FLOW'
    fs = flow.modifiers["Fluid"].flow_settings
    fs.flow_type = 'FIRE'          # fuel + flame, soot comes from flame_smoke
    fs.flow_behavior = 'INFLOW'
    fs.fuel_amount = 1.0
    fs.temperature = 1.4
    fs.surface_distance = 1.2
    fs.use_initial_velocity = True
    fs.velocity_normal = 1.4

    # The emitter is a boundary condition, not part of the picture. Left
    # visible it renders as a solid white blob in the middle of every cell —
    # which is exactly what the first Mantaflow sheet showed.
    flow.hide_render = True

    # Fuel cuts off partway through so the tail of the sheet is the flame DYING
    # — a one-shot flipbook (ANIM_ONCE) needs an ending, not a loop.
    cutoff = int(frames * 0.78)   # fuel runs longer; the last frames are the die-off
    fs.use_inflow = True
    fs.keyframe_insert(data_path="use_inflow", frame=cutoff)
    fs.use_inflow = False
    fs.keyframe_insert(data_path="use_inflow", frame=cutoff + 1)

    return dom


EMISSION_GAIN = 0.9


def build_material(dom):
    """Greyscale volume: white emission driven by `flame`, density by `density`.

    Deliberately NO blackbody node. The sheet must stay a temperature/density
    MASK so the call site owns the colour (F3) — baking Blender's blackbody in
    would lock every element to fire's palette.
    """
    mat = bpy.data.materials.new("FlameMask")
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    vol = nt.nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Color"].default_value = (1, 1, 1, 1)
    vol.inputs["Density"].default_value = 1.0
    vol.inputs["Blackbody Intensity"].default_value = 0.0
    vol.inputs["Emission Color"].default_value = (1, 1, 1, 1)
    vol.inputs["Emission Strength"].default_value = 1.0

    dens = nt.nodes.new("ShaderNodeAttribute"); dens.attribute_name = "density"
    flame = nt.nodes.new("ShaderNodeAttribute"); flame.attribute_name = "flame"
    mul = nt.nodes.new("ShaderNodeMath"); mul.operation = 'MULTIPLY'
    # 25 blew every flame pixel to pure white and threw away the temperature
    # gradient the sheet exists to carry — the mask must stay GREYSCALE.
    mul.inputs[1].default_value = EMISSION_GAIN
    # Mantaflow's density grid peaks around 1 but AVERAGES far below it, and
    # Eevee integrates density over a short ray through a small domain — so the
    # unscaled grid renders as a barely-visible haze (measured: 1.5% coverage).
    dmul = nt.nodes.new("ShaderNodeMath"); dmul.operation = 'MULTIPLY'
    dmul.inputs[1].default_value = 12.0

    nt.links.new(dens.outputs["Fac"], dmul.inputs[0])
    nt.links.new(dmul.outputs[0], vol.inputs["Density"])
    nt.links.new(flame.outputs["Fac"], mul.inputs[0])
    nt.links.new(mul.outputs[0], vol.inputs["Emission Strength"])
    nt.links.new(vol.outputs["Volume"], out.inputs["Volume"])

    dom.data.materials.append(mat)


def build_camera_and_render(cell, quick):
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(0, -8.0, 2.0), rotation=(math.radians(90), 0, 0))
    cam = bpy.context.object
    cam.data.type = 'ORTHO'
    # Frame the domain's HEIGHT: the flame is the tall thing, and the cell is
    # square, so height is what sets the scale.
    cam.data.ortho_scale = 4.0
    scene.camera = cam

    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = cell
    scene.render.resolution_y = cell
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'   # no Filmic: this is a MASK
    ee = scene.eevee
    ee.use_taa_reprojection = False
    ee.taa_render_samples = 8 if quick else 32
    # Volumetric resolution is what decides whether the flame has structure or
    # is a soft blob; the default 8px step is far too coarse for a 64px cell.
    ee.volumetric_tile_size = '2'
    ee.volumetric_samples = 64 if quick else 128
    ee.volumetric_start = 0.1
    ee.volumetric_end = 20.0


def bake(dom):
    with bpy.context.temp_override(scene=bpy.context.scene, object=dom,
                                   active_object=dom,
                                   selected_objects=[dom]):
        bpy.ops.fluid.bake_all()


def main():
    a = parse_args()
    res = a.res or (32 if a.quick else 128)
    cell = a.cell or (64 if a.quick else 256)
    frames = a.grid * a.grid

    global EMISSION_GAIN
    EMISSION_GAIN = a.emission
    clear_scene()
    dom = build_scene(a, res, frames)
    build_material(dom)
    build_camera_and_render(cell, a.quick)

    t0 = time.time()
    print("MANTA: baking res=%d frames=%d ..." % (res, frames), flush=True)
    bake(dom)
    print("MANTA: bake done in %.1fs" % (time.time() - t0), flush=True)

    scene = bpy.context.scene
    frame_dir = os.path.join(CACHE_DIR, "frames")
    os.makedirs(frame_dir, exist_ok=True)
    for f in range(1, frames + 1):
        scene.frame_set(f)
        scene.render.filepath = os.path.join(frame_dir, "f%03d.png" % f)
        bpy.ops.render.render(write_still=True)
        if f % 8 == 1:
            print("MANTA: rendered %d/%d  %.1fs" % (f, frames, time.time() - t0), flush=True)

    print("MANTA: frames in %s" % frame_dir, flush=True)
    print("MANTA: now run  python3 scripts/pack_flipbook.py %s --grid %d --out %s"
          % (frame_dir, a.grid, a.out), flush=True)
    return 0


CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                         "build_cache_manta")

if __name__ == "__main__":
    os.makedirs(CACHE_DIR, exist_ok=True)
    sys.exit(main())
