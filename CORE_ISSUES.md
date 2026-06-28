# Core System Issues & Improvements

Trong quá trình phát triển các kỹ năng (cụ thể là các kỹ năng hệ Thổ như `earth_shatter` và `seismic_pillars`), một số vấn đề, lỗi hiển thị 3D và ý tưởng cải tiến đột phá của Core API đã bộc lộ. Tài liệu này liệt kê chi tiết để phục vụ việc nâng cấp hệ thống lõi:

## 1. Mâu thuẫn tên hàm trong Core API [Đã giải quyết đồng bộ]
Nhiều hàm được thiết kế và ghi chú trong tài liệu hoặc được mong đợi theo logic thông thường lại có tên gọi khác trong thực tế triển khai ở thư mục `core/`:
- **Screen Distort:** Hệ thống có hàm `ScreenDistort_Add` (trước đây ghi nhận sai là `ScreenDistort_AddSource`).
- **Decal System:** Hàm để tạo decal là `DecalSystem_Add` (trước đây ghi nhận sai là `Decal_Spawn`).
- **Combat API:** Tránh dùng các hàm chưa kiểm chứng. Sử dụng `AddKnockbackToEnemy` để hất tung/đẩy lùi kẻ địch vật lý.

## 2. Lỗi hiển thị 3D thường gặp ở lõi & Cách khắc phục
- **Lỗi rò rỉ Depth Mask (Z-buffer Leak):** Khi các hệ thống vẽ trước (như hạt blending hoặc lốc xoáy) tắt Depth Write bằng `rlDisableDepthMask()` mà không bật lại, các mesh 3D vẽ thủ công tiếp theo sẽ bị lỗi Z-buffer nghiêm trọng, dẫn đến việc các tảng đá/gai nhọn bị nhìn xuyên thấu qua nhau và xuyên thấu qua mặt đất/decal.
  - *Khắc phục:* Bắt buộc kích hoạt tường minh `rlEnableDepthMask()` và `rlEnableDepthTest()` ở đầu các hàm vẽ kỹ năng 3D.
- **Lỗi đục thủng Mesh (Alpha Texture Leak):** Áp texture nứt đất có chứa kênh alpha trong suốt lên các mesh 3D kín, nếu nhân trực tiếp alpha của texture vào diffuse color trong shader sẽ làm thủng lưới mesh nhìn xuyên vào lòng rỗng của tảng đá.
  - *Khắc phục:* Khóa chặt kênh Alpha của mesh bằng `1.0f` trong fragment shader, chỉ nhân kênh màu RGB của texture vào diffuse.
- **Tính hình khối sắc lẹm (Flat Shading):** Đối với các khối hình học đa diện góc cạnh (đá tảng, cột bát giác, gai gai), việc gán pháp tuyến mịn tại đỉnh sẽ làm nhòe các cạnh. Cần tính toán Face Normal (tích có hướng của 2 cạnh mặt phẳng) để áp chung cho mặt Quad/Triangle, tạo ra các đường ranh giới (Hard Edges) phân định sáng tối rõ rệt.

## 3. Cải tiến Dựng hình Procedural Mesh
- **Khắc phục lỗi Taper quá đà:** Tránh việc thu hẹp đầu cột đá quá mạnh (Taper) khiến chúng biến thành hình nón cụt nhọn hoắt trông nhân tạo và giống viên pin xếp hàng. Trụ đá tròn Cylinder cần giữ nguyên bán kính đáy lên đỉnh và bịt hai nắp phẳng (Flat Capped) để đảm bảo độ dày vững chãi.
- **Mở rộng Mesh Preset:** Đã tích hợp `MESH_PRESET_PYRAMID` (Chóp tứ giác) và `MESH_PRESET_TETRAHEDRON` (Chóp tam giác) vào thư viện `DrawEffectMesh` của lõi để phục vụ việc tạo hình gai đá/gai gỗ nhanh chóng.

## 4. [Ý TƯỞNG MỚI] Cơ chế Cast Chiêu vẽ đường dẫn (Drag-to-Cast / Path Drawing)
- **Mô tả:** Đối với các kỹ năng tầm gần và trung bình (Short & Mid range) có tính chất mọc tuần tự hoặc dựng tường (như tường lửa, tường băng, gai đất mọc lan truyền), thay vì chỉ click chuột tức thời vào một vị trí mục tiêu đơn lẻ (Instant Click-to-Cast), đề xuất bổ sung cơ chế **nhấn giữ chuột và vẽ/kéo một đường dẫn** ngắn trên mặt đất. Kỹ năng sau đó sẽ mọc và phát tác chính xác men theo đường vẽ uốn lượn đó.
- **Đề xuất nâng cấp Core API:**
  - Bổ sung trạng thái vẽ đường dẫn cho chuột khi ở trạng thái chuẩn bị cast chiêu.
  - Cung cấp một mảng tọa độ các điểm vẽ `Vector3 pathPoints[MAX_POINTS]` và truyền mảng này vào hàm `CastSkill` để kỹ năng sinh vật thể dọc theo đường dẫn thay vì chỉ đi theo đường thẳng caster -> target.
