import bpy
import math
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
RAW_ANIM_DIR = REPO_ROOT / "character" / "raw_model" / "animations"
TARGET_GLB = Path("/Users/mth2610/Desktop/character_humanoid.glb")
OUT_ANIM_BANK = REPO_ROOT / "assets" / "characters" / "animations" / "humanoid_anims.glb"
OUT_MESH_ONLY = REPO_ROOT / "assets" / "characters" / "models" / "player_mesh.glb"

OUT_ANIM_BANK.parent.mkdir(parents=True, exist_ok=True)
OUT_MESH_ONLY.parent.mkdir(parents=True, exist_ok=True)

# ---------------------------------------------------------
# 1. Export pure mesh model (no animations)
# ---------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(TARGET_GLB))
target_arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
target_arm.animation_data_clear()
for a in list(bpy.data.actions):
    bpy.data.actions.remove(a)

bpy.ops.export_scene.gltf(
    filepath=str(OUT_MESH_ONLY),
    export_format='GLB',
    export_animations=False,
    export_draco_mesh_compression_enable=False
)
print(f"[Mesh] Exported pure mesh: {OUT_MESH_ONLY} ({OUT_MESH_ONLY.stat().st_size / 1024:.1f} KB)")
shutil.copyfile(OUT_MESH_ONLY, REPO_ROOT / "assets" / "characters" / "player.glb")
print(f"[Mesh] Updated assets/characters/player.glb (pure mesh, {OUT_MESH_ONLY.stat().st_size / 1024:.1f} KB)")

# ---------------------------------------------------------
# 2. Retarget all animations
# ---------------------------------------------------------
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(TARGET_GLB))
target_arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
target_arm.name = 'Humanoid_Armature'

# Keep idle and walk
final_actions = {}
for act in list(bpy.data.actions):
    act.use_fake_user = True
    if 'idle' in act.name.lower():
        act.name = 'idle'
        final_actions['idle'] = act
    elif 'walk' in act.name.lower():
        act.name = 'walk'
        final_actions['walk'] = act

# Delete mesh so animation bank contains only skeleton + curves
for m in [o for o in bpy.data.objects if o.type == 'MESH']:
    bpy.data.objects.remove(m, do_unlink=True)

# Target animations to extract
EXTERNAL_ANIMS = [
    ("running.fbx", "running"),
    ("jumping_down.fbx", "jumping_down"),
    ("punching.fbx", "punching"),
    ("kicking.fbx", "kicking"),
    ("palming.fbx", "palming"),
    ("casting.fbx", "casting"),
    ("loco_idle_sword.bvh", "loco_idle_sword"),
    ("sword_slash_horizontal.bvh", "sword_slash_horizontal"),
    ("sword_slash_backhand.bvh", "sword_slash_backhand"),
    ("sword_parry.bvh", "sword_parry"),
    ("sword_block.bvh", "sword_block"),
]

for filename, clip_name in EXTERNAL_ANIMS:
    file_path = RAW_ANIM_DIR / filename
    if not file_path.exists():
        print(f"  ! Missing {file_path}")
        continue

    before_objs = set(bpy.data.objects)
    before_actions = set(bpy.data.actions)

    if file_path.suffix.lower() == '.bvh':
        bpy.ops.import_anim.bvh(filepath=str(file_path))
    else:
        bpy.ops.import_scene.fbx(filepath=str(file_path))

    new_objs = [o for o in bpy.data.objects if o not in before_objs]
    new_actions = [a for a in bpy.data.actions if a not in before_actions]

    source_arms = [o for o in new_objs if o.type == 'ARMATURE']
    if not source_arms:
        for o in new_objs: bpy.data.objects.remove(o, do_unlink=True)
        for a in new_actions: bpy.data.actions.remove(a)
        continue
    source_arm = source_arms[0]

    src_act = source_arm.animation_data.action if source_arm.animation_data else None
    if not src_act:
        for o in new_objs: bpy.data.objects.remove(o, do_unlink=True)
        for a in new_actions: bpy.data.actions.remove(a)
        continue

    frame_start = int(src_act.frame_range[0])
    frame_end = int(src_act.frame_range[1])

    # Map bones
    bone_map = {}
    for tb in target_arm.data.bones:
        t_short = tb.name.split(':')[-1]
        for sb in source_arm.data.bones:
            s_short = sb.name.split(':')[-1]
            if t_short == s_short:
                bone_map[tb.name] = sb.name
                break

    bpy.context.view_layer.objects.active = target_arm
    bpy.ops.object.mode_set(mode='POSE')

    for t_bone, s_bone in bone_map.items():
        pb = target_arm.pose.bones[t_bone]
        c_rot = pb.constraints.new(type='COPY_ROTATION')
        c_rot.target = source_arm
        c_rot.subtarget = s_bone
        c_rot.target_space = 'WORLD'
        c_rot.owner_space = 'WORLD'

    for pb in target_arm.pose.bones:
        pb.bone.select = True

    bpy.ops.nla.bake(
        frame_start=frame_start,
        frame_end=frame_end,
        only_selected=True,
        visual_keying=True,
        clear_constraints=True,
        bake_types={'POSE'}
    )

    baked = target_arm.animation_data.action
    baked.name = clip_name
    baked.use_fake_user = True
    final_actions[clip_name] = baked
    print(f"  + Retargeted & Baked '{baked.name}' ({len(baked.fcurves)} curves, frames {frame_start}-{frame_end})")

    # Clean up imported temp objects & temporary source actions
    for o in new_objs:
        bpy.data.objects.remove(o, do_unlink=True)
    for a in new_actions:
        if a != baked:
            bpy.data.actions.remove(a)

# Delete any actions not in final_actions
for a in list(bpy.data.actions):
    if a not in final_actions.values():
        bpy.data.actions.remove(a)

print(f"\n[Anims] Clean animation bank has {len(bpy.data.actions)} actions:")
for name in sorted(final_actions.keys()):
    act = final_actions[name]
    print(f"  - {act.name} ({len(act.fcurves)} curves, frames {int(act.frame_range[0])}-{int(act.frame_range[1])})")

base_kwargs = dict(
    filepath=str(OUT_ANIM_BANK),
    export_format='GLB',
    export_animations=True,
    export_draco_mesh_compression_enable=False
)
try:
    bpy.ops.export_scene.gltf(**base_kwargs, export_animation_mode='ACTIONS')
except TypeError:
    bpy.ops.export_scene.gltf(**base_kwargs, export_nla_strips=False)

print(f"\n[Anims] Successfully exported Animation Bank to {OUT_ANIM_BANK} ({OUT_ANIM_BANK.stat().st_size / 1024:.1f} KB)")
