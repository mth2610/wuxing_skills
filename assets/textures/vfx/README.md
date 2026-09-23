# Cẩm Nang Quản Lý Thư Mục Assets VFX (`assets/textures/vfx/`)

Thư mục này chứa texture/flipbook trích xuất từ Unreal Engine 5 Niagara VFX. Các frame đã được chuẩn hóa trục $Y$ theo từng cell. Tên file giữ lại một số tên legacy; metadata thực trong `catalog.json` mới là nguồn đúng cho grid và kích thước.

---

## Cấu Trúc Phân Cấp Thư Mục

```
assets/textures/vfx/
├── flipbooks/       # Flipbooks/Atlas 64 frames (8x8) cuộn lửa, khói, nổ, plasma
├── baked_atlases/   # Atlas nướng phân tách kênh độc lập (Alpha, Emissive, Normals, Occlusion)
├── weapons/         # Lửa đầu nòng trực giao 3D, khói nòng súng
├── decals/          # Vết đạn găm, vết cháy nổ xém mặt đất
├── flares/          # Tia sáng hào quang, lens flare sao và vòng tròn
├── environment/     # Dải sương mù, mây tầng thấp
└── utilities/       # Mặt nạ cắt biên (Cutout mask), lưới số debug (NumberGrid)
```

---

## 1. `flipbooks/` (Hoạt Họa Atlas Đa Khung Hình)

Phần lớn file là $8 \times 8$ (64 frame), nhưng có ngoại lệ $8 \times 16$ và $1 \times 16$. Không suy luận grid chỉ từ tên file.

| Tên File | Quy Cách | Kênh Dữ Liệu | Ứng Dụng Trong Game |
| :--- | :--- | :--- | :--- |
| `explosion_burst_8x8.png` | 2048x2048 (8x8) | RGB Albedo/Emission | Tia chớp nổ bùng frame đầu tiên |
| `explosion_burst_normals_8x8.png` | 2048x2048 (8x8) | Pháp tuyến Tangent Normal | Chiếu sáng lập thể cho tia chớp nổ |
| `explosion_roil_8x8.png` | 2048x2048 (8x8) | RGB Albedo/Emission | Cuộn lửa quả cầu nổ chính (`NS_Explosion`) |
| `explosion_roil_normals_8x8.png` | 2048x2048 (8x8) | Pháp tuyến Tangent Normal | Ánh sáng mặt trời chiếu lên khói cuộn |
| `explosion_wide_8x8.png` | 8192x8192 (8x16, 128 frame; tên legacy) | RGB Albedo | Vụ nổ xòe ngang quét mặt đất |
| `explosion_core_8x8.png` | 4096x4096 (8x8) | RGB Emission | Tim vụ nổ cực sáng (Core Flash) |
| `fireball_8x8.png` | 2048x2048 (8x8) | RGB Albedo/Emission | Đạn cầu lửa bay (`Fireball Skill`) |
| `fireroil_8x8.png` | 2048x2048 (8x8) | RGB Albedo/Emission | Lửa đối lưu cuộn xoáy ngọn lửa lớn |
| `smoke_puff_8x8.png` | 2048x2048 (8x8) | RGBA Albedo/Density | Khói đen bốc lên sau vụ nổ |
| `smoke_puff_light_8x8.png` | 2048x2048 (8x8) | RGBA Albedo/Density | Khói trắng/xám mỏng cho va chạm đạn |
| `smoke_roil_8x8.png` | 2048x2048 (8x8) | RGBA Density | Cột khói đối lưu bốc cao ống khói |
| `smoke_trail_2x16.png` | 2048x2048 (1x16, 16 frame; tên legacy) | RGBA Density | Đuôi khói tên lửa bay dài |
| `smoke_wispy_8x8.png` | 4096x4096 (8x8) | RGBA Density | Khói tản mác mỏng nhẹ |
| `plasma_wisps_8x8.png` | 4096x4096 (8x8) | RGBA Glow | Khí năng lượng thần thông, quả cầu nhặt |

---

## 2. `baked_atlases/` (Kênh Nướng Độc Lập Cho Shader)

Các texture này được dùng khi shader cần xử lý riêng rẽ độ phát sáng, độ đổ bóng hoặc bề mặt pháp tuyến:

- `explosion_roil_alpha_8x8.png`: Kênh Alpha tinh khiết, dùng làm mask mờ đục hoặc cắt viền.
- `explosion_roil_emissive_8x8.png`: Chỉ lưu độ chói của ngọn lửa (đen = khói nguội, trắng = tim lửa cực nóng).
- `explosion_roil_normals_8x8.png`: Bản đồ pháp tuyến cho ánh sáng directional light rọi lên khói.
- `explosion_roil_occlusion_8x8.png`: Độ tự che khuất (Ambient Occlusion) giữa các cuộn khói.
- `smoke_puff_light_albedo_8x8.png`: Màu khói trắng chuẩn không có ánh lửa.
- `smoke_puff_light_emissive_8x8.png`, `smoke_puff_light_normals_8x8.png`, `smoke_puff_light_occlusion_8x8.png`.

---

## 3. `weapons/` (Vũ Khí & Đầu Nòng)

- `gun_smoke_thin.png`: Tia khói thuốc súng phụt theo viên đạn.

---

## 4. `decals/` (Vết Va Chạm & Cháy Nổ)

- `bullet_hole_diffuse.png`: Texture lỗ đạn găm trên bê tông, kim loại hoặc gỗ.
- `bullet_hole_normals.png`: Bản đồ pháp tuyến tạo độ lồi lõm 3D cho vết đạn găm.
- `explosion_scorch_masks.png`: Vết nám đen loang lổ trên mặt đất sau vụ nổ.

---

## 5. `flares/`, `environment/` & `utilities/`

- `flares/lens_flare_star.png`: Star/corona tròn cho điểm lóe quang học.
- `flares/lens_flare_ring.png`: Streak anamorphic ngang có lõi sao; tên `ring` là legacy, pixel không chứa vòng tròn.
- `environment/fog_bank_dense.png` & `fog_bank_wisps.png`: Dải sương mù thể tích cuộn sát đất.
- `utilities/cutout_mask_8x8.png`: Mask cắt khử viền đen cho atlas 8x8.
- `utilities/number_grid_debug.png`: Lưới đánh số 0..63 dùng để debug chỉ số frame trong quá trình phát triển.
