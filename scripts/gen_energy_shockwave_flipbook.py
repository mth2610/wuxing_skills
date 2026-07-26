"""
gen_energy_shockwave_flipbook.py
==================================
Script tạo Flipbook Atlas "Energy Shockwave" cho Game VFX (Blender 3.x / 4.x).

THAY ĐỔI QUAN TRỌNG so với bản trước: dựng lại như MÔ PHỎNG KHÓI 3D THẬT
(cùng pipeline với gen_smoke_flipbook.py — domain hình cầu, Volume
Principled, ánh sáng Key/Fill thật, Cycles volumetric render).

LƯU Ý HÌNH HỌC QUAN TRỌNG: vòng sóng được định nghĩa theo bán kính PHẲNG
trên mặt X-Z (mặt đối diện camera) — KHÔNG dùng bán kính 3D đầy đủ. Nếu
dùng bán kính 3D (tạo 1 vỏ CẦU rỗng thật sự trong không gian), camera nhìn
thẳng vào sẽ luôn thấy nó ĐẶC như 1 khối cầu, vì bất kỳ tia nhìn nào xuyên
qua tâm cũng phải cắt qua vỏ cầu 2 lần (mặt trước + mặt sau) — không có
"lỗ" nào để nhìn xuyên qua. Trục sâu (Y, hướng camera nhìn vào) chỉ dùng
để tạo ĐỘ DÀY/MỀM volume thật qua depth_falloff (mỏng), khác hẳn bản
Emission phẳng 2D hoàn toàn không có chiều sâu trước đó.

Cách dùng (mặc định: grayscale, KHÔNG bake màu, hình tròn; đã thêm lớp
FLICKER biến đổi nhanh để mật độ/năng lượng trông "sôi động" thay vì tĩnh):
blender --background --python gen_energy_shockwave_flipbook.py -- \
    --out ./energy_shockwave_atlas_8x8.png --grid 8 --cell 256 \
    --sim-frames 120 --samples 24 --max-radius 1.7 \
    --irregularity 0.4 --irregularity-fine 0.15 \
    --haze-strength 0.35 --haze-width 2.0 --density-scale 10 \
    --flicker-strength 0.5 --flicker-scale 6.0 --flicker-speed 9.0

Chỉ khi cần 1 bản preview đã tint sẵn màu (không dùng cho bản chính thức trong
game), thêm --bake-color cùng bộ màu theo Ngũ Hành bên dưới:
  Hỏa : --color-inner "1.0,0.9,0.5,1" --color-mid "1.0,0.35,0.05,1" --color-outer "0.3,0.02,0.0,1"
  Thủy: --color-inner "0.85,1.0,1.0,1" --color-mid "0.1,0.55,0.9,1" --color-outer "0.0,0.05,0.2,1"
  Mộc : --color-inner "0.85,1.0,0.55,1" --color-mid "0.15,0.7,0.2,1" --color-outer "0.0,0.15,0.05,1"
  Kim : --color-inner "1.0,1.0,0.95,1" --color-mid "0.75,0.78,0.85,1" --color-outer "0.15,0.15,0.2,1"
  Thổ : --color-inner "0.95,0.85,0.5,1" --color-mid "0.55,0.35,0.1,1" --color-outer "0.1,0.05,0.02,1"
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
    p.add_argument("--out", default="./energy_shockwave_atlas_8x8.png")
    p.add_argument("--grid", type=int, default=8)
    p.add_argument("--cell", type=int, default=256)
    p.add_argument("--sim-frames", type=int, default=120)
    p.add_argument("--samples", type=int, default=24, help="Volume cần nhiều sample hơn Emission phẳng để đỡ noise")
    p.add_argument("--seed", type=int, default=1234)

    # Hình học vòng sóng (đơn vị world-space, giống thang đo domain khói)
    p.add_argument("--max-radius", type=float, default=1.7, help="Bán kính tối đa vòng sóng nở tới")
    p.add_argument("--start-radius", type=float, default=0.08, help="Bán kính khởi đầu (nén, gần như 1 điểm sáng)")
    p.add_argument("--thickness-start", type=float, default=0.16, help="Độ dày vỏ cầu lúc mới nổ (mảnh, sắc)")
    p.add_argument("--thickness-end", type=float, default=0.32, help="Độ dày vỏ cầu lúc tan (dày, mờ, khuếch tán) — không nên vượt quá ~40% max_radius kẻo nuốt hết lỗ rỗng")
    p.add_argument("--depth-thickness", type=float, default=0.4, help="Độ dày theo trục sâu (Y) — mỏng, tạo volume thật nhưng không phải hình cầu")

    # Turbulence (dùng Noise Distortion built-in của Blender — tương đương domain-warp
    # nhẹ, đủ tạo cảm giác xơ tua hữu cơ khi kết hợp với volume scattering + ánh sáng thật)
    p.add_argument("--noise-scale", type=float, default=3.5)
    p.add_argument("--noise-detail", type=float, default=4.5)
    p.add_argument("--noise-roughness", type=float, default=0.55)
    p.add_argument("--noise-distortion", type=float, default=1.3, help="Domain-warp nội bộ của Blender Noise — càng cao càng xoắn/lởm chởm")

    # Lớp FLICKER: turbulence tần số cao, biến đổi NHANH theo thời gian —
    # mô phỏng năng lượng dao động/sôi liên tục (như lửa cháy chập chờn),
    # khác với lớp noise chính (biến đổi chậm, chỉ tạo hình dạng xơ tua).
    p.add_argument("--flicker-strength", type=float, default=0.5, help="Độ mạnh biến đổi năng lượng nhanh (0 = tắt, chỉ còn turbulence chậm)")
    p.add_argument("--flicker-scale", type=float, default=6.0, help="Tần số không gian của flicker (cao = lốm đốm nhỏ dày)")
    p.add_argument("--flicker-speed", type=float, default=9.0, help="Tốc độ biến đổi theo thời gian (W range trên toàn bộ sim) — cao = chớp/sôi nhanh hơn")

    p.add_argument("--density-scale", type=float, default=10.0, help="Hệ số density tổng (đơn vị Cycles volume) — thấp hơn = mờ/loãng hơn")
    p.add_argument("--anisotropy", type=float, default=0.25, help="Tán xạ ánh sáng thuận (forward scattering) của volume")

    # Lớp HAZE: 1 quầng mờ rộng & nhẹ hơn phủ quanh vòng chính, tạo cảm giác
    # khuếch tán/sương mờ thay vì rìa cắt gọn rõ rệt.
    p.add_argument("--haze-strength", type=float, default=0.35, help="Độ mạnh quầng mờ (0 = tắt hẳn, chỉ còn vòng sắc)")
    p.add_argument("--haze-width", type=float, default=2.0, help="Quầng mờ rộng gấp mấy lần độ dày vòng chính")

    # Vùng rỗng CỨNG ở tâm — LUÔN density=0 trong vùng này bất kể haze/noise/
    # irregularity có kéo vào tới đâu. Đây là chốt chặn để tâm không bao giờ
    # bị "nuốt" thành khối đặc ở các frame giữa/cuối vòng đời.
    p.add_argument("--min-hole-fraction", type=float, default=0.5, help="Bán kính lỗ rỗng cứng = fraction * ring_center hiện tại (0 = tắt chốt chặn)")

    # Méo hình dạng vòng (radius KHÔNG đều mọi hướng) — bắt buộc để tránh
    # trông như 1 cái donut/vòng tròn hoàn hảo. Đây là noise TẦN SỐ THẤP làm
    # lệch bán kính ring theo từng vị trí quanh vòng -> rìa lởm chởm, blob.
    p.add_argument("--irregularity", type=float, default=0.4, help="Biên độ méo bán kính THÔ (múi lớn), tính theo tỉ lệ max_radius (0 = tròn hoàn hảo)")
    p.add_argument("--irregularity-scale", type=float, default=1.6, help="Tần số noise méo THÔ (thấp = ít múi lớn/blob to)")
    p.add_argument("--irregularity-fine", type=float, default=0.15, help="Biên độ méo bán kính MỊN (răng cưa nhỏ), tính theo tỉ lệ max_radius — chống bị haze làm mượt mất biên lởm chởm")
    p.add_argument("--irregularity-fine-scale", type=float, default=6.0, help="Tần số noise méo MỊN (cao = răng cưa nhỏ dày)")

    # Mặc định KHÔNG bake màu — xuất grayscale trung tính, tint theo Ngũ Hành
    # ở shader trong game engine (đổi màu qua 1 bộ flipbook chung, tiết kiệm
    # dung lượng + thời gian render thay vì render lại 5 lần cho 5 hành).
    p.add_argument("--bake-color", action="store_true", help="Bake sẵn màu inner/mid/outer vào texture (mặc định: tắt, xuất grayscale)")
    p.add_argument("--color-inner", default="1.0,0.9,0.5,1.0", help="[Chỉ dùng khi --bake-color] Màu lõi vòng sóng (R,G,B,A)")
    p.add_argument("--color-mid", default="1.0,0.35,0.05,1.0", help="[Chỉ dùng khi --bake-color] Màu giữa vòng (R,G,B,A)")
    p.add_argument("--color-outer", default="0.3,0.02,0.0,1.0", help="[Chỉ dùng khi --bake-color] Màu rìa mờ dần (R,G,B,A)")
    return p.parse_args(argv)


ARGS = parse_args()
GRID = ARGS.grid
CELL = ARGS.cell
N_FRAMES = GRID * GRID
SIM_FRAMES = ARGS.sim_frames
random.seed(ARGS.seed)

TMP_DIR = os.path.abspath("./_energy_shockwave_flipbook_tmp")


def parse_color(s):
    vals = tuple(float(x) for x in s.split(","))
    assert len(vals) == 4, "Màu phải có 4 giá trị R,G,B,A"
    return vals


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)


def create_energy_shockwave_volume():
    # 1. Domain: khối CẦU thật (giống hệt smoke), KHÔNG phải mặt phẳng — để có
    #    volume scattering + bóng đổ thật, tạo cảm giác "có khối" chứ không phẳng.
    #    Bán kính domain > max_radius để chứa cả phần khuếch tán rìa lúc tan.
    domain_radius = ARGS.max_radius * 1.3
    bpy.ops.mesh.primitive_uv_sphere_add(radius=domain_radius, location=(0, 0, 1.5))
    sw_obj = bpy.context.active_object
    sw_obj.name = "EnergyShockwaveVolume"

    mat = bpy.data.materials.new("EnergyShockwaveVolumeMat")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    tex_coord = nt.nodes.new("ShaderNodeTexCoord")

    # 2. Bán kính PHẲNG trên mặt X-Z (mặt đối diện camera) — CHỈ dùng X,Z để
    #    định hình vòng sóng, KHÔNG dùng Y (trục sâu/hướng camera nhìn vào).
    #    Đây là điểm mấu chốt: nếu dùng length 3D đầy đủ (X,Y,Z) thì bất kỳ
    #    tia nhìn nào xuyên tâm cũng cắt qua vỏ cầu 2 lần (mặt trước+sau) ->
    #    trông đặc như 1 khối cầu, KHÔNG rỗng được (đó là lỗi bản trước).
    sep_xyz = nt.nodes.new("ShaderNodeSeparateXYZ")
    comb_xz = nt.nodes.new("ShaderNodeCombineXYZ")  # (x, 0, z) -> bỏ y
    length_node = nt.nodes.new("ShaderNodeVectorMath")
    length_node.operation = "LENGTH"
    length_node.label = "PlanarRadiusXZ"

    # Depth falloff: trục Y (chiều sâu) chỉ dùng để tạo ĐỘ DÀY/MỀM volume thật
    # (khác bản phẳng 2D hoàn toàn không có chiều sâu) — mỏng, không phải hình
    # cầu, nên không tạo ra 2 lớp vỏ trước/sau khi nhìn xuyên tâm.
    depth_abs = nt.nodes.new("ShaderNodeMath")
    depth_abs.operation = "ABSOLUTE"
    depth_falloff = nt.nodes.new("ShaderNodeMapRange")
    depth_falloff.inputs["From Min"].default_value = 0.0
    depth_falloff.inputs["From Max"].default_value = ARGS.depth_thickness
    depth_falloff.inputs["To Min"].default_value = 1.0
    depth_falloff.inputs["To Max"].default_value = 0.0
    depth_falloff.clamp = True
    depth_falloff.interpolation_type = "SMOOTHSTEP"

    # Méo bán kính vòng (radius KHÔNG đều mọi hướng) bằng noise TẦN SỐ THẤP
    # lấy mẫu ngay tại vị trí X-Z -> mỗi vị trí quanh vòng "phình/lõm" khác
    # nhau -> rìa lởm chởm, hình blob hữu cơ, KHÔNG còn là donut tròn đều.
    irregularity_time = nt.nodes.new("ShaderNodeValue")
    irregularity_time.label = "IrregularityTime"
    irregularity_time.outputs[0].default_value = 0.0
    irregularity_time.outputs[0].keyframe_insert(data_path="default_value", frame=1)
    irregularity_time.outputs[0].default_value = 1.2
    irregularity_time.outputs[0].keyframe_insert(
        data_path="default_value", frame=SIM_FRAMES
    )

    irregularity_noise = nt.nodes.new("ShaderNodeTexNoise")
    irregularity_noise.noise_dimensions = "4D"
    irregularity_noise.inputs["Scale"].default_value = ARGS.irregularity_scale
    irregularity_noise.inputs["Detail"].default_value = 2.0
    irregularity_noise.inputs["Roughness"].default_value = 0.5
    irregularity_noise.inputs["Distortion"].default_value = 0.4

    # Remap 0..1 -> -irregularity..+irregularity (đơn vị world, theo max_radius)
    irregularity_amount = ARGS.irregularity * ARGS.max_radius
    irregularity_remap = nt.nodes.new("ShaderNodeMapRange")
    irregularity_remap.inputs["From Min"].default_value = 0.0
    irregularity_remap.inputs["From Max"].default_value = 1.0
    irregularity_remap.inputs["To Min"].default_value = -irregularity_amount
    irregularity_remap.inputs["To Max"].default_value = irregularity_amount

    # Lớp méo MỊN (tần số cao) — cộng thêm vào để tạo răng cưa nhỏ trên biên,
    # KHÔNG bị lớp haze (rất mượt/rộng) nuốt mất như chỉ dùng 1 lớp méo thô.
    irregularity_fine_noise = nt.nodes.new("ShaderNodeTexNoise")
    irregularity_fine_noise.noise_dimensions = "4D"
    irregularity_fine_noise.inputs["Scale"].default_value = ARGS.irregularity_fine_scale
    irregularity_fine_noise.inputs["Detail"].default_value = 3.0
    irregularity_fine_noise.inputs["Roughness"].default_value = 0.55
    irregularity_fine_noise.inputs["Distortion"].default_value = 0.3

    irregularity_fine_amount = ARGS.irregularity_fine * ARGS.max_radius
    irregularity_fine_remap = nt.nodes.new("ShaderNodeMapRange")
    irregularity_fine_remap.inputs["From Min"].default_value = 0.0
    irregularity_fine_remap.inputs["From Max"].default_value = 1.0
    irregularity_fine_remap.inputs["To Min"].default_value = -irregularity_fine_amount
    irregularity_fine_remap.inputs["To Max"].default_value = irregularity_fine_amount

    irregularity_total = nt.nodes.new("ShaderNodeMath")
    irregularity_total.operation = "ADD"
    irregularity_total.label = "IrregularityCoarsePlusFine"

    # 3. Ring center: bán kính LỚP VỎ, GIÃN NỞ theo thời gian (ease-out — nhanh
    #    lúc đầu, chậm dần khi tan, giống blast wave thật)
    ring_center = nt.nodes.new("ShaderNodeValue")
    ring_center.label = "RingCenterRadius"
    ring_center.outputs[0].default_value = ARGS.start_radius
    ring_center.outputs[0].keyframe_insert(data_path="default_value", frame=1)
    ring_center.outputs[0].default_value = ARGS.max_radius * 0.75
    ring_center.outputs[0].keyframe_insert(
        data_path="default_value", frame=round(SIM_FRAMES * 0.35)
    )
    ring_center.outputs[0].default_value = ARGS.max_radius
    ring_center.outputs[0].keyframe_insert(data_path="default_value", frame=SIM_FRAMES)

    # Ease-out CHỈ áp cho đúng fcurve của ring_center (không đụng các animation khác)
    ring_center_path = f'nodes["{ring_center.name}"].outputs[0].default_value'
    if nt.animation_data and nt.animation_data.action:
        fcu = nt.animation_data.action.fcurves.find(ring_center_path)
        if fcu:
            for kp in fcu.keyframe_points:
                kp.interpolation = "SINE"

    # effective_ring_center = ring_center (đều mọi hướng) + perturbation (méo
    # theo vị trí) -> đây là bán kính THẬT SỰ dùng để so khớp, không còn tròn đều
    effective_ring_center = nt.nodes.new("ShaderNodeMath")
    effective_ring_center.operation = "ADD"
    effective_ring_center.label = "EffectiveRingCenter"

    # 4. Ring thickness: mảnh -> dày dần khi vòng khuếch tán/tan ra
    ring_thickness = nt.nodes.new("ShaderNodeValue")
    ring_thickness.label = "RingThickness"
    ring_thickness.outputs[0].default_value = ARGS.thickness_start
    ring_thickness.outputs[0].keyframe_insert(data_path="default_value", frame=1)
    ring_thickness.outputs[0].default_value = ARGS.thickness_end
    ring_thickness.outputs[0].keyframe_insert(data_path="default_value", frame=SIM_FRAMES)

    # 5. Band mask (dạng vỏ cầu) = 1 - clamp(|radius - ring_center| / thickness, 0, 1)
    #    => density chỉ khác 0 quanh bán kính ring_center -> RỖNG Ở TÂM tự nhiên,
    #    không cần "khoét" giả — đây chính là khác biệt "lực khác" so với khói
    #    (khói: density đặc dần từ tâm; shockwave: density chỉ tồn tại ở 1 lớp vỏ).
    diff_radius = nt.nodes.new("ShaderNodeMath")
    diff_radius.operation = "SUBTRACT"
    abs_diff = nt.nodes.new("ShaderNodeMath")
    abs_diff.operation = "ABSOLUTE"
    div_thickness = nt.nodes.new("ShaderNodeMath")
    div_thickness.operation = "DIVIDE"
    band_mask = nt.nodes.new("ShaderNodeMapRange")
    band_mask.inputs["From Min"].default_value = 0.0
    band_mask.inputs["From Max"].default_value = 1.0
    band_mask.inputs["To Min"].default_value = 1.0
    band_mask.inputs["To Max"].default_value = 0.0
    band_mask.clamp = True
    band_mask.interpolation_type = "SMOOTHSTEP"  # rìa mềm, không cắt gọn

    # Lớp HAZE: quầng mờ rộng & nhẹ hơn quanh vòng chính (dùng lại abs_diff,
    # chỉ đổi "thước đo" thành thickness rộng hơn nhiều lần) -> cảm giác
    # khuếch tán/sương mờ bao quanh, thay vì chỉ có 1 dải sắc nét.
    haze_thickness = nt.nodes.new("ShaderNodeMath")
    haze_thickness.operation = "MULTIPLY"
    haze_thickness.inputs[1].default_value = ARGS.haze_width
    haze_div = nt.nodes.new("ShaderNodeMath")
    haze_div.operation = "DIVIDE"
    haze_band = nt.nodes.new("ShaderNodeMapRange")
    haze_band.inputs["From Min"].default_value = 0.0
    haze_band.inputs["From Max"].default_value = 1.0
    haze_band.inputs["To Min"].default_value = 1.0
    haze_band.inputs["To Max"].default_value = 0.0
    haze_band.clamp = True
    haze_band.interpolation_type = "SMOOTHSTEP"
    haze_weighted = nt.nodes.new("ShaderNodeMath")
    haze_weighted.operation = "MULTIPLY"
    haze_weighted.inputs[1].default_value = ARGS.haze_strength
    combined_band = nt.nodes.new("ShaderNodeMath")
    combined_band.operation = "MAXIMUM"
    combined_band.label = "CombinedBandWithHaze"

    # 6. Turbulence 4D (Distortion built-in của Blender = domain-warp nội bộ,
    #    kết hợp scattering thật của volume sẽ tạo cảm giác xơ tua hữu cơ mà
    #    không cần dựng thủ công 1 mạng curl-noise tốn kém trên mặt phẳng)
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.noise_dimensions = "4D"
    noise.inputs["Scale"].default_value = ARGS.noise_scale
    noise.inputs["Detail"].default_value = ARGS.noise_detail
    noise.inputs["Roughness"].default_value = ARGS.noise_roughness
    noise.inputs["Distortion"].default_value = ARGS.noise_distortion

    # W biến thiên NHANH hơn bản trước (0->6 thay vì 0->3) -> hình dạng xơ tua
    # thay đổi rõ rệt qua từng frame, không còn cảm giác "đứng yên" giữa các ô.
    noise.inputs["W"].default_value = 0.0
    noise.inputs["W"].keyframe_insert(data_path="default_value", frame=1)
    noise.inputs["W"].default_value = 6.0
    noise.inputs["W"].keyframe_insert(data_path="default_value", frame=SIM_FRAMES)

    # Lớp FLICKER: turbulence tần số cao + biến thiên NHANH theo W riêng, đại
    # diện cho năng lượng dao động/sôi liên tục (khác lớp noise chính chỉ lo
    # hình dạng xơ tua). Nhân (không cộng) vào density -> chỗ flicker thấp sẽ
    # dập tắt cục bộ, chỗ flicker cao sẽ bùng sáng -> cảm giác đang "sống".
    flicker_time = nt.nodes.new("ShaderNodeValue")
    flicker_time.label = "FlickerTime"
    flicker_time.outputs[0].default_value = 0.0
    flicker_time.outputs[0].keyframe_insert(data_path="default_value", frame=1)
    flicker_time.outputs[0].default_value = ARGS.flicker_speed
    flicker_time.outputs[0].keyframe_insert(data_path="default_value", frame=SIM_FRAMES)

    flicker_noise = nt.nodes.new("ShaderNodeTexNoise")
    flicker_noise.noise_dimensions = "4D"
    flicker_noise.inputs["Scale"].default_value = ARGS.flicker_scale
    flicker_noise.inputs["Detail"].default_value = 3.0
    flicker_noise.inputs["Roughness"].default_value = 0.6
    flicker_noise.inputs["Distortion"].default_value = 0.5

    # Remap flicker (0..1) -> hệ số nhân quanh 1.0, biên độ theo flicker_strength
    flicker_mult = nt.nodes.new("ShaderNodeMapRange")
    flicker_mult.inputs["From Min"].default_value = 0.0
    flicker_mult.inputs["From Max"].default_value = 1.0
    flicker_mult.inputs["To Min"].default_value = 1.0 - ARGS.flicker_strength
    flicker_mult.inputs["To Max"].default_value = 1.0 + ARGS.flicker_strength

    # Remap noise (0..1) -> hệ số nhân mật độ: biên độ RỘNG hơn bản trước
    # (gần 0 .. hơn 1) -> tương phản mạnh, cảm giác năng lượng chỗ bùng chỗ
    # tắt thay vì mượt đều 1 tông.
    noise_mult = nt.nodes.new("ShaderNodeMapRange")
    noise_mult.inputs["From Min"].default_value = 0.0
    noise_mult.inputs["From Max"].default_value = 1.0
    noise_mult.inputs["To Min"].default_value = 0.05  # gần như tắt hẳn ở vùng tối nhất
    noise_mult.inputs["To Max"].default_value = 1.3   # bùng sáng hơn ở vùng đỉnh

    # 7. Vòng đời: bùng nhanh lúc nổ, giữ sáng, rồi tan dần
    life_curve = nt.nodes.new("ShaderNodeMath")
    life_curve.operation = "MULTIPLY"
    life_curve.inputs[1].default_value = 0.0
    life_curve.inputs[1].keyframe_insert(data_path="default_value", frame=1)
    life_curve.inputs[1].default_value = 1.0
    life_curve.inputs[1].keyframe_insert(
        data_path="default_value", frame=round(SIM_FRAMES * 0.08)
    )
    life_curve.inputs[1].default_value = 0.85
    life_curve.inputs[1].keyframe_insert(
        data_path="default_value", frame=round(SIM_FRAMES * 0.55)
    )
    life_curve.inputs[1].default_value = 0.0
    life_curve.inputs[1].keyframe_insert(data_path="default_value", frame=SIM_FRAMES)

    # 8. Ghép: density = band_mask * noise_mult * life_curve * density_scale
    mul_band_noise = nt.nodes.new("ShaderNodeMath")
    mul_band_noise.operation = "MULTIPLY"
    mul_life = nt.nodes.new("ShaderNodeMath")
    mul_life.operation = "MULTIPLY"
    mul_scale = nt.nodes.new("ShaderNodeMath")
    mul_scale.operation = "MULTIPLY"
    mul_scale.inputs[1].default_value = ARGS.density_scale

    # 9. Màu theo band_mask (gradient ổn định, không nhấp nháy theo noise):
    #    trong cùng (đỉnh band) -> giữa -> ngoài (rìa/nền) tối dần.
    #    MẶC ĐỊNH grayscale trung tính — tint theo Ngũ Hành nên làm ở shader
    #    trong game engine (1 bộ flipbook dùng chung cho cả 5 hành), KHÔNG
    #    bake cứng màu vào texture trừ khi bật --bake-color.
    if ARGS.bake_color:
        col_inner = parse_color(ARGS.color_inner)
        col_mid = parse_color(ARGS.color_mid)
        col_outer = parse_color(ARGS.color_outer)
    else:
        col_inner = (1.0, 1.0, 1.0, 1.0)
        col_mid = (0.55, 0.55, 0.55, 1.0)
        col_outer = (0.03, 0.03, 0.03, 1.0)

    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = col_outer
    ramp.color_ramp.elements[1].position = 1.0
    ramp.color_ramp.elements[1].color = col_inner
    mid_el = ramp.color_ramp.elements.new(0.6)
    mid_el.color = col_mid

    # 10. Volume Principled thật (có Anisotropy -> ánh sáng tán xạ thật, tạo khối)
    vol = nt.nodes.new("ShaderNodeVolumePrincipled")
    vol.inputs["Anisotropy"].default_value = ARGS.anisotropy

    out = nt.nodes.new("ShaderNodeOutputMaterial")

    # --- Nối dây ---
    nt.links.new(tex_coord.outputs["Object"], sep_xyz.inputs["Vector"])
    nt.links.new(sep_xyz.outputs["X"], comb_xz.inputs["X"])
    nt.links.new(sep_xyz.outputs["Z"], comb_xz.inputs["Z"])  # bỏ Y -> chỉ còn mặt phẳng X-Z

    nt.links.new(comb_xz.outputs["Vector"], length_node.inputs[0])
    nt.links.new(tex_coord.outputs["Object"], noise.inputs["Vector"])

    # Depth falloff theo trục Y (chiều sâu, hướng camera nhìn vào)
    nt.links.new(sep_xyz.outputs["Y"], depth_abs.inputs[0])
    nt.links.new(depth_abs.outputs["Value"], depth_falloff.inputs["Value"])

    # Méo bán kính: sample noise tại vị trí X-Z, remap rồi cộng vào ring_center
    # (2 lớp: THÔ tạo múi lớn + MỊN tạo răng cưa nhỏ không bị haze nuốt mất)
    nt.links.new(comb_xz.outputs["Vector"], irregularity_noise.inputs["Vector"])
    nt.links.new(irregularity_time.outputs[0], irregularity_noise.inputs["W"])
    nt.links.new(irregularity_noise.outputs["Fac"], irregularity_remap.inputs["Value"])

    nt.links.new(comb_xz.outputs["Vector"], irregularity_fine_noise.inputs["Vector"])
    nt.links.new(irregularity_time.outputs[0], irregularity_fine_noise.inputs["W"])
    nt.links.new(irregularity_fine_noise.outputs["Fac"], irregularity_fine_remap.inputs["Value"])

    nt.links.new(irregularity_remap.outputs["Result"], irregularity_total.inputs[0])
    nt.links.new(irregularity_fine_remap.outputs["Result"], irregularity_total.inputs[1])

    nt.links.new(ring_center.outputs[0], effective_ring_center.inputs[0])
    nt.links.new(irregularity_total.outputs["Value"], effective_ring_center.inputs[1])

    nt.links.new(length_node.outputs["Value"], diff_radius.inputs[0])
    nt.links.new(effective_ring_center.outputs["Value"], diff_radius.inputs[1])
    nt.links.new(diff_radius.outputs["Value"], abs_diff.inputs[0])
    nt.links.new(abs_diff.outputs["Value"], div_thickness.inputs[0])
    nt.links.new(ring_thickness.outputs[0], div_thickness.inputs[1])
    nt.links.new(div_thickness.outputs["Value"], band_mask.inputs["Value"])

    nt.links.new(ring_thickness.outputs[0], haze_thickness.inputs[0])
    nt.links.new(abs_diff.outputs["Value"], haze_div.inputs[0])
    nt.links.new(haze_thickness.outputs["Value"], haze_div.inputs[1])
    nt.links.new(haze_div.outputs["Value"], haze_band.inputs["Value"])
    nt.links.new(haze_band.outputs["Result"], haze_weighted.inputs[0])
    nt.links.new(band_mask.outputs["Result"], combined_band.inputs[0])
    nt.links.new(haze_weighted.outputs["Value"], combined_band.inputs[1])

    nt.links.new(noise.outputs["Fac"], noise_mult.inputs["Value"])

    # Flicker: turbulence nhanh, nhân riêng vào sau band*noise_mult
    nt.links.new(tex_coord.outputs["Object"], flicker_noise.inputs["Vector"])
    nt.links.new(flicker_time.outputs[0], flicker_noise.inputs["W"])
    nt.links.new(flicker_noise.outputs["Fac"], flicker_mult.inputs["Value"])

    mul_flicker = nt.nodes.new("ShaderNodeMath")
    mul_flicker.operation = "MULTIPLY"
    mul_flicker.label = "ApplyFlicker"

    nt.links.new(combined_band.outputs["Value"], mul_band_noise.inputs[0])
    nt.links.new(noise_mult.outputs["Result"], mul_band_noise.inputs[1])
    nt.links.new(mul_band_noise.outputs["Value"], mul_flicker.inputs[0])
    nt.links.new(flicker_mult.outputs["Result"], mul_flicker.inputs[1])
    nt.links.new(mul_flicker.outputs["Value"], mul_life.inputs[0])
    nt.links.new(life_curve.outputs["Value"], mul_life.inputs[1])

    # Nhân thêm depth_falloff trước khi áp density_scale cuối cùng
    mul_depth = nt.nodes.new("ShaderNodeMath")
    mul_depth.operation = "MULTIPLY"
    nt.links.new(mul_life.outputs["Value"], mul_depth.inputs[0])
    nt.links.new(depth_falloff.outputs["Result"], mul_depth.inputs[1])

    # Vùng rỗng CỨNG ở tâm: hole_radius = ring_center (GỐC, chưa méo) * fraction.
    # LUÔN ép density=0 trong vùng này, bất kể haze/irregularity có kéo vào
    # tới đâu -> tâm không bao giờ bị "nuốt" thành khối đặc ở frame giữa/cuối.
    hole_radius = nt.nodes.new("ShaderNodeMath")
    hole_radius.operation = "MULTIPLY"
    hole_radius.inputs[1].default_value = ARGS.min_hole_fraction
    inner_mask = nt.nodes.new("ShaderNodeMapRange")
    inner_mask.inputs["From Min"].default_value = 0.0
    inner_mask.inputs["To Min"].default_value = 0.0
    inner_mask.inputs["To Max"].default_value = 1.0
    inner_mask.clamp = True
    inner_mask.interpolation_type = "SMOOTHSTEP"
    inner_mask.label = "HardInnerHoleMask"
    nt.links.new(ring_center.outputs[0], hole_radius.inputs[0])
    nt.links.new(hole_radius.outputs["Value"], inner_mask.inputs["From Max"])
    nt.links.new(length_node.outputs["Value"], inner_mask.inputs["Value"])

    mul_inner = nt.nodes.new("ShaderNodeMath")
    mul_inner.operation = "MULTIPLY"
    nt.links.new(mul_depth.outputs["Value"], mul_inner.inputs[0])
    nt.links.new(inner_mask.outputs["Result"], mul_inner.inputs[1])
    nt.links.new(mul_inner.outputs["Value"], mul_scale.inputs[0])

    nt.links.new(combined_band.outputs["Value"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], vol.inputs["Color"])
    nt.links.new(mul_scale.outputs["Value"], vol.inputs["Density"])

    nt.links.new(vol.outputs["Volume"], out.inputs["Volume"])

    sw_obj.data.materials.append(mat)
    return sw_obj


def setup_camera_and_lights():
    # Camera Orthographic — giống hệt bố cục smoke script (nhìn thẳng vào domain cầu)
    domain_radius = ARGS.max_radius * 1.3
    bpy.ops.object.camera_add(
        location=(0, -5.0, 1.5), rotation=(math.radians(90), 0, 0)
    )
    cam = bpy.context.active_object
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = domain_radius * 2.15
    bpy.context.scene.camera = cam

    # Key light — nguồn sáng chính tạo bóng đổ/khối (giống smoke script)
    bpy.ops.object.light_add(type="AREA", location=(-2.0, -2.5, 3.5))
    key = bpy.context.active_object
    key.data.energy = 700
    key.data.size = 4.0

    # Fill light — hắt sáng viền, tránh mặt tối bị đen hoàn toàn
    bpy.ops.object.light_add(type="AREA", location=(2.0, 2.0, 1.0))
    fill = bpy.context.active_object
    fill.data.energy = 280
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

    print(f"[gen_energy_shockwave] Đang render {N_FRAMES} frames (volumetric)...")
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
        oy = (GRID - 1 - row) * CELL

        atlas_arr[oy : oy + CELL, ox : ox + CELL, :] = frame_arr
        bpy.data.images.remove(img)

    atlas_img = bpy.data.images.new(
        "EnergyShockwaveFlipbookAtlas", width=atlas_w, height=atlas_h, alpha=True
    )
    atlas_img.pixels.foreach_set(atlas_arr.ravel())

    out_abs = os.path.abspath(out_path)
    atlas_img.filepath_raw = out_abs
    atlas_img.file_format = "PNG"
    atlas_img.save()
    print(
        f"[gen_energy_shockwave] HOÀN TẤT! Đã xuất atlas đúng thứ tự Top->Down: {out_abs}"
    )


def main():
    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)
    os.makedirs(TMP_DIR, exist_ok=True)

    clear_scene()
    create_energy_shockwave_volume()
    setup_camera_and_lights()
    render_frames()
    stitch_atlas(ARGS.out)

    if os.path.exists(TMP_DIR):
        shutil.rmtree(TMP_DIR, ignore_errors=True)


if __name__ == "__main__":
    main()
