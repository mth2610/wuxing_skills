#!/usr/bin/env python3
"""Step 1 of 3 — bake a Mantaflow gas sim in Blender and DUMP THE GRIDS.

    blender --background --python scripts/manta_bake.py -- --preset fire --quick
    blender --background --python scripts/manta_bake.py -- --preset fire

Writes build_cache/<name>/f###.npz, each holding the raw voxel grids
(`density`, `flame`, `temperature`) plus the grid resolution. Nothing is
rendered here.

WHY THE SPLIT (bake → render → pack)
    Baking is the slow part and the LOOK does not depend on it. Rendering in
    Blender (the previous pipeline) tied the two together: every change to
    exposure or channel layout meant re-baking, and Eevee's alpha comes from
    extinction, so an emissive flame rendered bright but almost transparent and
    the sheet could only ever carry one channel of information.
    Dumping the grids instead lets scripts/ti_render.py (Taichi) ray-march them
    with full control over what lands in R, G, B and A — and one bake then feeds
    as many different sheets as we like.

WHY MANTAFLOW STILL DOES THE SIM
    It is a production solver: MacCormack advection, a calibrated
    fuel → flame → soot combustion model, obstacles, wavelet turbulence. Writing
    the equivalent by hand is thousands of lines. Taichi's advantage is on the
    render side, and that is where it is used.

KNOWN LIMIT: the dumped grids are the BASE resolution. Mantaflow's noise
(wavelet upres) lives in a separate grid that Python does not expose, so
`--noise` is off by default here — raise `--res` instead, which the GPU
ray-marcher can afford.
"""

import argparse
import os
import shutil
import sys
import time

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

# Load the preset table WITHOUT touching sys.path. Blender's own scripts import
# short, generic module names (`presets`, `render`, ...), so putting this folder
# on the path lets our files shadow Blender's — and the symptom is not an import
# error but "AttributeError: 'NoneType' object has no attribute 'getDataPointer'"
# raised from inside bpy.ops.fluid.bake_all(). Loading by explicit file path
# cannot collide with anything.
import importlib.util as _ilu  # noqa: E402
_spec = _ilu.spec_from_file_location("fb_presets", os.path.join(HERE, "fb_presets.py"))
_mod = _ilu.module_from_spec(_spec)
_spec.loader.exec_module(_mod)
PRESETS = _mod.PRESETS

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CACHE = os.path.join(ROOT, "build_cache")


def parse_args():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--preset", default="fire", choices=sorted(PRESETS))
    ap.add_argument("--name", default=None, help="cache subdirectory (default: preset)")
    ap.add_argument("--quick", action="store_true", help="res 32, for iterating")
    ap.add_argument("--res", type=int, default=None)
    ap.add_argument("--frames", type=int, default=64)
    ap.add_argument("--noise", action="store_true",
                    help="wavelet upres — improves the SIM but is not in the "
                         "dumped grid, so only useful when rendering in Blender")
    # Overrides, so a look can be swept without editing PRESETS.
    ap.add_argument("--buoyancy", type=float, default=None)
    ap.add_argument("--vorticity", type=float, default=None)
    ap.add_argument("--burn", type=float, default=None)
    ap.add_argument("--flame-smoke", type=float, default=None)
    ap.add_argument("--velocity", type=float, default=None,
                    help="inflow speed along the emitter normals — the radial "
                         "push for a puff")
    return ap.parse_args(argv)


def build(p, res, frames, cache_dir, noise):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = frames

    sx, sy, sz = p["domain_scale"]
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0, 0, sz))
    dom = bpy.context.object
    dom.name = "Domain"
    dom.scale = (sx, sy, sz)
    bpy.ops.object.transform_apply(scale=True)

    bpy.ops.object.modifier_add(type='FLUID')
    dom.modifiers["Fluid"].fluid_type = 'DOMAIN'
    ds = dom.modifiers["Fluid"].domain_settings
    ds.domain_type = 'GAS'
    ds.resolution_max = res
    ds.use_adaptive_domain = False       # a moving domain would change the grid
                                         # shape between frames
    ds.cache_frame_start = 1
    ds.cache_frame_end = frames
    # REPLAY (Blender's default), NOT 'ALL'. 'ALL' is a valid enum value, which
    # is why nothing complained — but with it set, bpy.ops.fluid.bake_all() dies
    # with "'NoneType' object has no attribute 'getDataPointer'". Bisected one
    # setting at a time against a minimal scene: cachedir OK, resolution OK,
    # adaptive-domain OK, cache_type='ALL' FAILED. Under REPLAY the solver steps
    # as the frames are set, which is exactly what dump() walks through anyway.
    ds.cache_type = 'REPLAY'
    # Absolute and normalised. Mantaflow writes its cache through its own C++
    # path handling, and a path containing ".." segments made bake_all die with
    # "'NoneType' object has no attribute 'getDataPointer'" — an error that says
    # nothing about paths at all.
    ds.cache_directory = os.path.abspath(cache_dir)
    ds.alpha = p["alpha"]
    ds.beta = p["buoyancy"]
    ds.vorticity = p["vorticity"]
    ds.burning_rate = p["burn"]
    ds.flame_smoke = p["flame_smoke"]
    ds.flame_vorticity = 0.6
    ds.flame_max_temp = 2.4
    ds.use_dissolve_smoke = p["dissolve"] is not None
    if p["dissolve"]:
        ds.dissolve_speed = p["dissolve"]
    if hasattr(ds, "use_noise"):
        ds.use_noise = noise

    # A domain with no material makes the depsgraph update that runs AFTER
    # bpy.ops.fluid.bake_all() throw — the traceback points at
    # ViewLayer.update(), not at the bake. Cheap to satisfy: an empty material
    # slot is enough, and nothing here renders in Blender anyway.
    dom.data.materials.append(bpy.data.materials.new("DomainVolume"))

    bpy.ops.mesh.primitive_uv_sphere_add(radius=p["fuel_radius"],
                                         location=(0, 0, p["fuel_z"]))
    flow = bpy.context.object
    flow.name = "Fuel"
    # A flattened emitter is right for something rising from a floor (a fire, a
    # smoke column) and wrong for a PUFF, which should start as a sphere and
    # expand equally — a flat source biases the puff wide before any physics runs.
    fz = p.get("fuel_flat", 0.45)
    flow.scale = (1.0, 1.0, fz)
    bpy.ops.object.transform_apply(scale=True)
    bpy.ops.object.modifier_add(type='FLUID')
    flow.modifiers["Fluid"].fluid_type = 'FLOW'
    fs = flow.modifiers["Fluid"].flow_settings
    fs.flow_type = p["flow_type"]
    fs.flow_behavior = 'INFLOW'
    fs.fuel_amount = 1.0
    fs.temperature = p["temperature"]
    fs.surface_distance = 1.2
    fs.use_initial_velocity = True
    fs.velocity_normal = p["velocity_normal"]

    # Fuel stops partway so the tail of the sheet is the effect DYING. A
    # flipbook played once (ANIM_ONCE) needs an ending, not a loop.
    cut = max(1, int(frames * p["fuel_cutoff"]))
    fs.use_inflow = True
    fs.keyframe_insert(data_path="use_inflow", frame=cut)
    fs.use_inflow = False
    fs.keyframe_insert(data_path="use_inflow", frame=cut + 1)
    return dom


def dump(dom, frames, out_dir):
    """Read the voxel grids frame by frame.

    The grids must come from the EVALUATED object: the original datablock's
    modifier holds no simulation state, so reading `dom.modifiers[...]` directly
    returns an empty grid and every frame would dump zeros.
    """
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    t0 = time.time()
    shape = None
    for f in range(1, frames + 1):
        scene.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        ds = dom.evaluated_get(dg).modifiers["Fluid"].domain_settings
        rx, ry, rz = ds.domain_resolution
        if shape is None:
            shape = (rx, ry, rz)
            print("DUMP: grid resolution %dx%dx%d" % shape, flush=True)

        def grab(name):
            arr = np.asarray(getattr(ds, name), dtype=np.float32)
            # Mantaflow's flat layout is x-fastest, then y, then z (Blender Z is
            # up), so reshape as (z, y, x) and let the renderer index [z][y][x].
            return arr.reshape(rz, ry, rx) if arr.size == rx * ry * rz else None

        d = grab("density_grid")
        fl = grab("flame_grid")
        tp = grab("temperature_grid")
        if d is None:
            print("DUMP: frame %d has no density grid (size mismatch) — aborting" % f)
            return None
        # float16: a 128-res bake is ~1 MB/frame/grid at float32 and there are
        # three grids over 64 frames. The solver's values live in [0, ~2], where
        # half precision has ~3 decimal digits — far finer than an 8-bit sheet
        # can carry anyway.
        np.savez_compressed(os.path.join(out_dir, "f%03d.npz" % f),
                            density=d.astype(np.float16),
                            flame=(fl if fl is not None else np.zeros_like(d)).astype(np.float16),
                            temperature=(tp if tp is not None else np.zeros_like(d)).astype(np.float16),
                            res=np.array(shape, np.int32))
        if f % 8 == 1:
            print("DUMP: %d/%d  d.max=%.3f flame.max=%.3f  %.1fs"
                  % (f, frames, float(d.max()),
                     float(fl.max()) if fl is not None else -1.0,
                     time.time() - t0), flush=True)
    return shape


def main():
    a = parse_args()
    p = dict(PRESETS[a.preset])
    for k in ("buoyancy", "vorticity", "burn"):
        if getattr(a, k) is not None:
            p[k] = getattr(a, k)
    if a.flame_smoke is not None:
        p["flame_smoke"] = a.flame_smoke
    if a.velocity is not None:
        p["velocity_normal"] = a.velocity

    name = a.name or a.preset
    res = a.res or (32 if a.quick else 128)
    out_dir = os.path.join(CACHE, name)
    os.makedirs(out_dir, exist_ok=True)

    # A stale Mantaflow cache makes bpy.ops.fluid.bake_all() fail with
    # "AttributeError: 'NoneType' object has no attribute 'getDataPointer'",
    # which names nothing to do with caching, and the failure is silent from the
    # pipeline's point of view — the next stage happily renders the OLD grids and
    # the sheet looks like the change did nothing. Always bake from clean.
    # Mantaflow's cache lives in its OWN tree, not inside the directory we dump
    # .npz grids into. Sharing a directory made bake_all() fail whenever that
    # directory already held our own files — Mantaflow scans its cache root and
    # chokes on what it does not recognise.
    manta_dir = os.path.abspath(os.path.join(CACHE, "_manta", name))
    if os.path.isdir(manta_dir):
        shutil.rmtree(manta_dir)
    # Mantaflow will not create a missing PARENT for its cache; it needs the
    # directory to exist.
    os.makedirs(manta_dir, exist_ok=True)
    # And the dump directory too. A bake produces every frame, so anything left
    # here is from a previous run — and leaving it is what let a FAILED bake go
    # unnoticed: the next stage rendered the stale grids and every measurement
    # afterwards described the old sheet.
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    dom = build(p, res, a.frames, manta_dir, a.noise)
    print("BAKE: %s res=%d frames=%d ..." % (name, res, a.frames), flush=True)
    t0 = time.time()
    try:
        with bpy.context.temp_override(scene=bpy.context.scene, object=dom,
                                       active_object=dom, selected_objects=[dom]):
            bpy.ops.fluid.bake_all()
    except Exception as e:                       # noqa: BLE001 - report and STOP
        print("BAKE: FAILED — %s" % e, flush=True)
        return 1
    print("BAKE: done in %.1fs" % (time.time() - t0), flush=True)

    shape = dump(dom, a.frames, out_dir)
    if shape is None:
        return 1
    # A bake that silently produced nothing must not look like success: the
    # renderer would happily turn empty grids into an empty sheet.
    import glob as _glob
    if len(_glob.glob(os.path.join(out_dir, "f*.npz"))) < a.frames:
        print("BAKE: FAILED — only %d/%d frames dumped"
              % (len(_glob.glob(os.path.join(out_dir, "f*.npz"))), a.frames))
        return 1
    print("BAKE: grids in %s" % out_dir, flush=True)
    print("BAKE: next  python3 scripts/flipbook/render.py %s" % out_dir, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
