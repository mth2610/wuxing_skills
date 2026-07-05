# Original User Request

## Initial Request — 2026-07-05T03:05:24Z

Chuyển đổi màn hình thử nghiệm `sandbox/vfx_test.c` sang hệ tọa độ mét (1 unit = 1 meter), sửa lỗi projectile bay quá nhanh/cắt hình do sai lệch hệ số lực, và bổ sung tab test cho hàm `VFX_ComposeTriggerImpactBurst` còn thiếu trong `visual_composer.c`.

Working directory: /Users/mth2610/Desktop/c_games/wuxing_skills
Integrity mode: development

## Requirements

### R1. Đồng bộ hệ tọa độ mét (1 unit = 1 meter) cho sandbox/vfx_test.c
- Chuyển đổi toàn bộ các giá trị hằng số kích thước, khoảng cách offset, vận tốc (velocity) và cường độ lực (force strength) trong sandbox/vfx_test.c từ hệ cm cũ sang hệ mét (chia cho 100).
- Cụ thể: Bán kính ánh sáng (150.0f -> 1.5f), decal scale (40.0f -> 0.4f), bán kính hạt (25.0f -> 0.25f, 30.0f -> 0.3f, 40.0f -> 0.4f), độ dày trail ribbon (len 50.0f -> 0.5f, thick 6.0f -> 0.06f), offset spawn và các lực vortex/vector field.
- Khoảng cách bắn thử nghiệm projectile đổi từ 40.0f mét xuống 8.0f mét để nằm trọn trong góc nhìn camera và kích thước map.

### R2. Sửa thông số lực của Projectile trong visual_composer.c
- Trong hàm `VFX_ComposeProjectileTrail` tại visual_composer.c, điều chỉnh lực gia tốc hướng `FORCE_GRAVITY_DIR` từ `325.0f` xuống `3.25f` và lực nhiễu luận `FORCE_NOISE_PERLIN` từ `20.0f` xuống `0.2f` để phù hợp với hệ mét mới.

### R3. Triển khai tab test 'BURST' trong vfx_test.c
- Nâng số lượng tab test trong `vfx_test.c` lên 6 tab bằng cách thêm tab `'BURST'`.
- Khi tab `'BURST'` được chọn và người dùng click chuột vào màn hình thế giới 3D, kích hoạt hàm `VFX_ComposeTriggerImpactBurst` với một `ImpactBurstConfig` mẫu đầy đủ 4 bước:
  - Screen Distortion (méo màn hình)
  - Ground Decal (vết nứt đất hoặc vết cháy)
  - Point Light Flash (ánh sáng lóe lên rồi tắt)
  - Radial Particle Burst (bắn hạt tỏa tròn)

## Acceptance Criteria

### Biên dịch & Vận hành
- [ ] Biên dịch dự án thành công không lỗi (`make` chạy thành công).
- [ ] Mở màn hình VFX Prefab Tester hoạt động bình thường, không bị crash.

### Hiệu ứng VFX Hệ Mét
- [ ] Các hiệu ứng hạt và decal khi spawn ra có kích thước hợp lý (tương quan phù hợp với kích thước nhân vật Player cao ~0.5m).
- [ ] Các hạt vortex xoáy quanh player và hạt trong Vector Field chuyển động với tốc độ vừa phải, mượt mà.
- [ ] Projectile bay với tốc độ hợp lý từ vị trí click chuột đến đích (~8m), kéo theo vệt trail mảnh, biến mất đúng lúc chạm đích mà không bị kéo quá xa hay bị cắt hình (clipping).

### Tab Test BURST
- [ ] Tab "BURST" xuất hiện trên UI điều khiển của VFX Tester bên cạnh các tab cũ.
- [ ] Khi click chuột trong tab "BURST", tạo ra vụ nổ đầy đủ 4 hiệu ứng (rung/méo hình, tạo decal đất nứt, lóe sáng và phun hạt tỏa tròn).
