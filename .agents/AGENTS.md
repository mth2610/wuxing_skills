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
