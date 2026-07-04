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
- **Lỗi Raylib Batching Hazard (CỰC KỲ QUAN TRỌNG):** Khi tắt/bật ghi độ sâu (`rlDisableDepthMask()`, `rlEnableDepthMask()`) hoặc tắt/bật test độ sâu (`rlDisableDepthTest()`, `rlEnableDepthTest()`), **BẮT BUỘC** phải gọi hàm xả batch `rlDrawRenderBatchActive();` ngay trước lệnh đó. Nếu không, trạng thái OpenGL sẽ bị thay đổi ngay lập tức nhưng áp dụng sai lên các đỉnh đồ họa (vertices) cũ (ví dụ: mặt đất `rlBegin(RL_TRIANGLES)`) vẫn đang kẹt trong batch chưa được xả, dẫn đến việc mặt đất không ghi độ sâu và làm hỏng toàn bộ hiệu ứng Soft Particles của các chiêu thức khác.

---

## Session Summary (Phiên làm việc ngày 04/07/2026)

### 1. Sửa lỗi Soft Particles (Item 3 trong CORE_ISSUES.md)
- **Tình trạng:** Mặt đất (vẽ bằng immediate mode `rlBegin/rlEnd`) không ghi được độ sâu vào FBO khiến hiệu ứng Soft Particles của chiêu thức bị hỏng (quả cầu hiển thị đặc 100%, không có phần mờ khi chìm).
- **Phát hiện 1 (Batching Hazard):** Khám phá ra lỗi kinh điển của Raylib `rlgl`. Khi một chiêu thức (VD: `fire_skill`, hệ thống shadow) gọi `rlDisableDepthMask` để vẽ hạt/bóng, nó đổi trạng thái OpenGL ngay lập tức. Tuy nhiên, các đỉnh của mặt đất được xếp vào batch từ trước đó CHƯA ĐƯỢC XẢ. Khi batch được xả sau đó, mặt đất bị vẽ với trạng thái tắt ghi độ sâu!
- **Khắc phục 1:** Quét toàn bộ 11 file (gồm môi trường và mọi file skill), bổ sung `rlDrawRenderBatchActive()` TRƯỚC mọi lệnh đổi state độ sâu để ép xả batch an toàn.
- **Phát hiện 2 (Texture Binding trong Immediate Mode):** Trong chế độ vẽ immediate mode (`rlBegin`), việc gọi `SetShaderValueTexture` không tự động map texture. Phải gọi `rlActiveTextureSlot()` và `rlEnableTexture()` thủ công. Đã sửa trong `ScreenDistort_BindDepthForSoftParticles`.
- **Phát hiện 3 (Lật chiều Texture):** Khi `ScreenDistort` sao chép Depth Texture, Raylib dựng hình bằng ma trận Ortho làm lật ngược trục Y. Đã sửa bằng cách truyền chiều cao âm vào `DrawTextureRec` để map đúng đỉnh/đáy.
- **Kết quả:** Hiệu ứng Soft Particles đã hoàn thiện, mượt mà dính sát vào mặt đất và hoàn toàn "miễn nhiễm" với việc người chơi xuất các chiêu thức khác cùng lúc. Đã cập nhật tài liệu `CORE_API.md` cho phép các skill tương lai sử dụng.
