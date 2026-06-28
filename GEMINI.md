# ListenLexi - Session Summary & Gemini Notes

## Session Summary (Phiên làm việc ngày 28/06/2026)

### 1. Kỹ năng mới: Seismic Pillars (`seismic_pillars_skill`)
- **Xuất chiêu tuần tự:** Xả chuỗi gồm tối đa 8 cột đá mọc trồi lên tuần tự từ caster tới vị trí target với độ trễ tăng dần `delay = index * 0.08s`.
- **Hất văng & Sát thương:** Khi từng cột đá trồi lên sẽ quét sát thương AoE và hất chéo đối thủ lên trời (`AddKnockbackToEnemy` hướng xả chiêu).
- **Rung camera nhẹ & Bụi đất:** Rung camera bằng xung lực vật lý (`CameraFX_AddImpulse`) và bắn hạt bụi vàng khi cột đá đâm phá địa hình.
- **Mesh Trụ tròn phẳng đầu (Cylinder Flat Capped):** Dựng hình trụ bát giác (`RADIAL_SEGS = 8`, `HEIGHT_SEGS = 3`) thẳng đứng 100%, bịt kín nắp phẳng ở đỉnh bằng Flat Shading pháp tuyến bề mặt (Face Normal) để lộ rõ các thớ cạnh lăng trụ 3D sắc lẹm, không dùng chóp nhọn cụt vát.
- **Ngẫu biến kích thước nhấp nhô:** Từng cột đá được sinh ngẫu biến độc lập về chiều cao tối đa (từ 28.0f đến 55.0f đơn vị) và bán kính (từ 6.0f đến 11.0f đơn vị) tạo bãi đá nhấp nhô nhấp nhô hoang dã tự nhiên.
- **Shader tinh tế mỏng mảnh:** Loại bỏ hoàn toàn mảng cam chói lọi ở nắp đáy chân cột (nhìn giống khúc gỗ/viên pin). Thay vào đó chỉ giữ màu đá nâu xám trầm ấm, điểm xuyết các đường mạch chỉ dung nham vàng cam siêu mỏng mảnh, sắc sảo (`smoothstep(0.81, 0.83)`) chạy lả lướt dọc theo thớ cột đá.

### 2. Mở rộng Mesh Preset cho lõi
Tích hợp thêm 2 preset mesh 3D mới vào lõi `DrawEffectMesh`:
- `MESH_PRESET_PYRAMID` (Chóp tứ giác Flat Shading).
- `MESH_PRESET_TETRAHEDRON` (Chóp tam giác Flat Shading).

### 3. Đồng bộ hóa CORE_ISSUES.md
Ghi nhận lỗi rò rỉ Depth Mask (`rlDisableDepthMask` không bật lại gây lỗi nhìn xuyên thấu), lỗi nhân Alpha làm đục thủng mesh 3D kín, lỗi Taper quá đà biến hình trụ thành pin, và đặc biệt là **Ý tưởng đột phá Drag-to-Cast** (vẽ đường dẫn xả chiêu uốn lượn cự ly gần/trung).

---

## Lưu ý cho các phiên tiếp theo
- **Quy tắc đặt tên file nghiêm ngặt (CẤM THAY ĐỔI):** Tên file kỹ năng custom mới tạo bắt buộc phải đặt theo định dạng chuẩn: `skills/<element>/<name>_skill/<name>_skill.c` và `skills/<element>/<name>_skill/<name>_skill.h`. Cấm thay đổi cấu trúc này để script `generate_registry.py` tự động phát hiện và đăng ký đúng.
- **Include Header chính xác:** Luôn kiểm tra đường dẫn include của file header kỹ năng, tránh việc include sai tên file/đường dẫn sai (ví dụ include nhầm `wildfire_skill.h` thay vì `fire_wildfire_skill.h`).
- **Tránh trùng lặp mã nguồn:** Khi tích hợp code do ChatGPT/AI bên ngoài viết, phải rà soát kỹ và xoá bỏ các hàm bị sinh trùng lặp ở cuối file (lỗi sinh mã nguồn lặp).
- **Độ dày tia lửa 3D:** Khi vẽ tia lửa dạng vệt sáng kéo dài (Stretched Sparks) bằng Quad, luôn giữ độ dày `w` mảnh `0.12f -> 0.18f` và giữ cố định từ đầu đến đuôi để tránh cảm giác bị to dẹt giống dải lụa bay.
- **Ring Buffer cho hệ thống hạt tùy biến:** Khi tự quản lý pool hạt custom trong C, luôn dùng chỉ số chạy vòng tròn (Ring Buffer) thay vì duyệt tìm ô trống tuần tự để tránh bị nghẽn/mất hạt khi bắn liên tục.
- **Camera Shake:** Không bao giờ tự ý thêm hiệu ứng rung camera (`CameraFX_Shake`) nếu chưa hỏi kỹ người dùng.
- **Screen Distort:** Chỉ sử dụng cho các chiêu thức hệ Thủy (Hydro Cleave...) hoặc khi người dùng yêu cầu rõ ràng, hạn chế lạm dụng gây rối mắt.
- **Vẽ chiêu thức dạng Cylinder/Mesh kín:** Luôn kích hoạt tường minh `rlEnableDepthMask()` và `rlEnableDepthTest()` ở đầu hàm vẽ và bịt kín hai đầu nắp phẳng để tránh lỗi xuyên thấu do Z-buffer.
- **Cơ chế Drag-to-Cast:** Khi thiết kế chiêu thức mới cự ly ngắn/trung, cân nhắc đề xuất cơ chế giữ chuột vẽ đường đi trên đất để mọc chiêu thay vì chỉ click tức thời một mục tiêu.
