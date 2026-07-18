#!/usr/bin/env python3
"""
Chuẩn bị mô hình 3D từ AI để upload lên Mixamo.
Chạy bằng lệnh headless của Blender:
blender --background --python prep_for_mixamo.py -- input.glb output.fbx
"""

import sys
import os
import mathutils

try:
    import bpy
except ImportError:
    sys.exit("Vui lòng chạy script này bên trong Blender Python.")

def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    if len(argv) < 2:
        sys.exit("Usage: blender --background --python prep_for_mixamo.py -- <input_model.glb/obj> <output_mixamo.fbx> [decimate_ratio]")
    
    input_model = argv[0]
    output_fbx = argv[1]
    decimate_ratio = float(argv[2]) if len(argv) > 2 else 1.0 # Mặc định giữ nguyên lưới
    return input_model, output_fbx, decimate_ratio

def wipe_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def import_model(filepath):
    ext = os.path.splitext(filepath)[1].lower()
    if ext in ['.glb', '.gltf']:
        bpy.ops.import_scene.gltf(filepath=filepath)
    elif ext == '.obj':
        # Xử lý tương thích giữa các phiên bản Blender mới và cũ
        if hasattr(bpy.ops.wm, "obj_import"):
            bpy.ops.wm.obj_import(filepath=filepath)
        else:
            bpy.ops.import_scene.obj(filepath=filepath)
    elif ext == '.fbx':
        bpy.ops.import_scene.fbx(filepath=filepath)
    else:
        sys.exit(f"Định dạng không được hỗ trợ: {ext}")
        
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == 'MESH']
    if not meshes:
        sys.exit("Không tìm thấy Mesh nào trong file AI.")
    return meshes

def process_for_mixamo(meshes, decimate_ratio):
    # 1. Gộp toàn bộ mảnh mesh rời rạc thành một (Bắt buộc cho Mixamo)
    bpy.ops.object.select_all(action='DESELECT')
    for m in meshes:
        m.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    char_mesh = bpy.context.view_layer.objects.active
    char_mesh.name = "Character_Mixamo"

    # 2. Xóa các dữ liệu rác (nếu có Animation hoặc Armature rác từ AI)
    bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    
    # 3. Apply mọi Scale/Rotation hiện tại
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # 4. Tính toán đưa nhân vật chạm đất (Z=0) và đứng ngay giữa trục (0,0,0)
    bbox = [char_mesh.matrix_world @ mathutils.Vector(corner) for corner in char_mesh.bound_box]
    min_z = min([v.z for v in bbox])
    center_x = sum([v.x for v in bbox]) / 8.0
    center_y = sum([v.y for v in bbox]) / 8.0

    # Dời gốc Origin xuống dưới gót chân
    bpy.context.scene.cursor.location = (center_x, center_y, min_z)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')

    # Kéo nhân vật về gốc tọa độ thế giới
    char_mesh.location = (0, 0, 0)
    
    # Apply lại lần cuối để khóa cứng tọa độ tại 0,0,0
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    print("  Đã đưa nhân vật về tọa độ gốc (0,0,0) và chạm mặt đất.")

    # 5. Giảm lưới (nếu cần thiết)
    if decimate_ratio < 1.0:
        before = len(char_mesh.data.polygons)
        mod = char_mesh.modifiers.new(name="Decimate", type='DECIMATE')
        mod.ratio = decimate_ratio
        bpy.ops.object.modifier_apply(modifier=mod.name)
        after = len(char_mesh.data.polygons)
        print(f"  Đã giảm lưới: {before} -> {after} faces.")

    return char_mesh

def export_to_mixamo(output_fbx):
    # Xuất FBX, nhúng thẳng Texture vào bên trong file để Mixamo có thể đọc màu
    bpy.ops.export_scene.fbx(
        filepath=output_fbx,
        use_selection=False,  # Xuất toàn bộ Scene (giờ chỉ còn 1 nhân vật)
        axis_forward='-Z',    # Trục chuẩn của FBX
        axis_up='Y',          # Trục đứng chuẩn của Mixamo
        bake_space_transform=True,
        path_mode='COPY',     # Bắt buộc để nhúng texture
        embed_textures=True   # Bắt buộc để Mixamo nhận diện màu
    )

def main():
    input_model, output_fbx, decimate_ratio = parse_args()
    
    print(f"Bắt đầu xử lý file AI: {input_model}")
    wipe_scene()
    
    meshes = import_model(input_model)
    process_for_mixamo(meshes, decimate_ratio)
    
    export_to_mixamo(output_fbx)
    print(f"Hoàn tất! File đã sẵn sàng để upload Mixamo: {output_fbx}")

if __name__ == "__main__":
    main()