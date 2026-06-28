# Core System Issues & Improvements

Trong quá trình phát triển các kỹ năng (cụ thể là các kỹ năng hệ Thủy như `hydro_cleave` và `glacial_spikes`), một số vấn đề và điểm bất cập của Core API đã bộc lộ. Tài liệu này liệt kê các vấn đề để `core_system` agent có thể nắm bắt và cải thiện:

## 1. Mâu thuẫn tên hàm trong Core API
Nhiều hàm được thiết kế và ghi chú trong tài liệu hoặc được mong đợi theo logic thông thường lại có tên gọi khác trong thực tế triển khai ở thư mục `core/`:
- **Screen Distort:** Hệ thống có hàm `ScreenDistort_AddSource` thay vì `ScreenDistort_Add`, gây nhầm lẫn khi tra cứu nhanh.
- **Decal System:** Hàm để tạo decal là `Decal_Spawn` thay vì `DecalSystem_Add` (trong khi các hệ thống khác thường dùng suffix/prefix thống nhất như `_Add` hay `_Create`).
- **Combat API:** Đã từng có sự nhầm lẫn giữa `ApplyDamage`, `ApplyKnockback` (không tồn tại) với `AddKnockbackToEnemy`. Thiếu một chuẩn chung (Standardized Interface) cực kỳ đơn giản để truyền sát thương vào một vùng không gian (AoE Damage) mà không phải tự duyệt vòng lặp thủ công.

## 2. Thiếu sự hỗ trợ truyền Uniforms tự động cho Shader
Mỗi khi một kỹ năng cần dùng custom shader, người viết skill phải tự include `"core/sandbox_core.h"` để lấy biến toàn cục `extern Camera3D camera` và tự truyền `u_viewPos` vào shader thông qua `SetShaderValue`.
- **Đề xuất:** `SkillManager` nên có một hàm wrapper chuẩn (ví dụ: `SkillManager_BeginShader(Shader shader)`) tự động truyền sẵn các biến môi trường thiết yếu như `u_time`, `u_viewPos`, `u_resolution` để giảm thiểu code lặp lại ở từng chiêu thức và tránh lỗi thiếu uniform khiến shader hiển thị sai.

## 3. Khó khăn khi dựng hình Procedural Mesh (Lưới thủ công)
Vì quy định nghiêm ngặt **không được dùng các hàm vẽ hình học có sẵn của Raylib (như DrawCube, DrawCylinder)**, các kỹ năng yêu cầu tạo hình không gian 3D bắt buộc phải tự tính toán toán học (sin, cos, cross-product pháp tuyến) và gọi trực tiếp `rlBegin`/`rlEnd`.
Việc này cực kỳ tốn thời gian, rủi ro cao (rất dễ tính sai pháp tuyến dẫn đến ánh sáng bị hỏng như trường hợp lưới 2D/phẳng).
- **Đề xuất:** Core system nên cung cấp một thư viện nội bộ (ví dụ: `core/procedural_mesh_utils.h`) chứa các hàm dựng hình cơ bản nhưng trả về mảng Vertices hoặc tương thích với `rlgl` (như `DrawCoreCylinder`, `DrawCoreCone`, `DrawCoreSphere`), từ đó người viết skill có thể tập trung vào hiệu ứng thay vì vật lộn với toán học ma trận 3D cơ bản.
