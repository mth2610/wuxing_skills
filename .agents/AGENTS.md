# Quy tắc dành cho AI Coding Assistant (Workspace Rules)

Tập tin này định nghĩa các ràng buộc và quy tắc tìm kiếm dành cho các AI Agent hoạt động trong dự án này để bảo vệ context window, tăng tốc độ xử lý và **tiết kiệm token**.

---

## 1. Các thư mục BỊ CẤM đọc và tìm kiếm (Ignored Folders)
Khi thực hiện các thao tác tìm kiếm mã nguồn hoặc duyệt thư mục, AI **tuyệt đối không được đọc hoặc index** các thư mục sau:
* `_deps/` — Chứa mã nguồn tải về của thư viện Raylib. Việc quét thư mục này sẽ gây ô nhiễm kết quả tìm kiếm và tiêu tốn hàng triệu token.
* `build/` — Thư mục build trên máy tính (chứa cache CMake và file biên dịch tạm).
* `android.wuxing_skills/` (Ngoại trừ `AndroidManifest.xml` khi cần thiết) — Chứa các file build tạm của Android APK.
* `environment/` — Hệ thống ánh sáng và Fake Shading hoạt động hoàn toàn độc lập. Cấm các agent làm skill dùng `grep` hoặc đọc vào thư mục này.
* `maps/` — Hệ thống Map Plugin hoạt động độc lập. Cấm các agent làm skill hoặc environment dùng `grep` hoặc đọc vào thư mục này unless designed to create maps.

---

## 2. Quy tắc tiết kiệm Token khi phân tích mã nguồn
* **Chỉ đọc Header (.h):** Khi cần tìm hiểu cách dùng các hệ thống lõi (như Particle System, Trail System, Force Field), chỉ được mở file header tương ứng (ví dụ: `core/particle_system.h`). **Cấm đọc** file C thực thi (`core/particle_system.c`).
* **Đọc cô đọng:** Chỉ đọc file của Kỹ năng (Skill) đang cần chỉnh sửa, không lan man sang các hệ khác.
* **Không quét toàn bộ mã nguồn:** Tránh việc dùng các câu lệnh tìm kiếm toàn văn (`grep_search` hoặc `ripgrep`) trên toàn bộ dự án trừ khi thực sự cần thiết, và hãy luôn sử dụng bộ lọc loại trừ các thư mục cấm ở mục 1.

---

## 3. Quy tắc BẮT BUỘC thiết lập tham số điều chỉnh (Live-tuning for all Skill parameters)
Khi tạo mới hoặc chỉnh sửa bất kỳ kỹ năng (skill) nào, AI **tuyệt đối không được hardcode** các thông số kỹ thuật hay các tham số hiệu ứng. Tất cả các tham số phải được khai báo và đăng ký để người dùng có thể điều chỉnh trực tiếp trên bảng điều khiển của Sandbox (Live-tuning).

### Yêu cầu cụ thể:
1. **Tham số Logic & Physics:**
   - Số lượng đạn/đá/tia (boulder count, projectile count) -> Đăng ký min/max.
   - Vận tốc, gia tốc (speed, gravity force, turbulence) của đạn/tia sét/hạt.
   - Thời gian chờ, thời gian hoạt ảnh (delay timers, rise duration, flight delay, step intervals).
   - Khoảng cách, bán kính spawn, chiều cao lơ lửng (spawn radius, hover height, target distance offsets).
   - Biên độ dao động vật lý (amplitude, frequency của chuyển động nhấp nhô, lắc lư).
   - Lượng sát thương gây ra (damage), tầm tác động AoE (damage radius), lực đẩy lùi (knockback).
2. **Tham số hiệu ứng (Cast, Impact, Burst VFX):**
   - Độ mờ/đậm của khói/bụi (color alpha, particle alpha).
   - Cường độ đẩy khói/hạt bốc lên cao (upward bias, speed min/max).
   - Tuổi thọ của hạt (particle lifetime min/max), bán kính hạt (radius min/max).
   - Độ mạnh của rung màn hình (camera shake intensity), bán kính/độ mạnh méo màn hình (screen distort radius/strength).
   - Cường độ phát sáng và bán kính của điểm sáng (VFX point light color, radius, lifetime).
3. **Cách triển khai bắt buộc:**
   - Toàn bộ các tham số này phải được định nghĩa dưới dạng các biến tĩnh `static float` trong tệp hằng số tham số của skill (`skills/<element>/<name>_skill/<name>_skill_params.inl`).
   - Đăng ký ánh xạ các biến này vào mảng `s_tunables` tương ứng tại tệp đăng ký `skills/<element>/<name>_skill/<name>_skill_tunables.inl`.
   - Cập nhật số lượng phần tử đăng ký tương ứng `#define <NAME>_TUNABLE_COUNT <N>` trong hàm `Init[Name]Skill` của tệp `.c`.
   - Trong code thực thi của skill, đọc trực tiếp từ các biến tĩnh này thay vì sử dụng hằng số số học (ví dụ: dùng `s_boulderSpeed * dt` thay vì `15.0f * dt`, dùng `(unsigned char)s_puffAlpha` thay vì `40`).

