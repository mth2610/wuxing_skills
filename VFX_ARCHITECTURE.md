# Kiến Trúc Engine Kỹ Năng & Bản Đồ — Ngũ Hành Tỷ Võ
> **Tài liệu hướng dẫn Kiến trúc Hệ thống & Quy tắc phát triển dành cho Nhà phát triển AI**

Tài liệu này định nghĩa cấu trúc thư mục, kiến trúc tổng quát của hệ thống VFX, cơ chế điều khiển thông minh, và các quy tắc bắt buộc để phát triển kỹ năng (skills), bản đồ (maps) hoặc môi trường (environment).

---

## 1. Bản Đồ Cấu Trúc Thư Mục (Directory Structure)

Hệ thống được thiết kế theo kiến trúc module hóa cao, chia tách rõ rệt giữa Hệ thống lõi (Core), Bản đồ (Maps), Kỹ năng (Skills), và Môi trường (Environment):

```
wuxing_skills/
├── main.c                          # Điểm khởi chạy chính (Desktop Entry)
├── CMakeLists.txt                  # Cấu hình tự động quét nguồn & biên dịch (CMake)
├── Makefile.Android                # Cấu hình biên dịch Android APK
├── CORE_ISSUES.md                  # Nhật ký lỗi hiển thị 3D & ghi chú kỹ thuật
├── GEMINI.md                       # Nhật ký phiên làm việc & quy tắc AI nâng cao
│
├── core/                           # HỆ THỐNG LÕI ĐỒ HỌA & VFX (Core Engines)
│   ├── skill_manager.c/.h          # Quản lý & đăng ký vòng đời kỹ năng (Skill Registry)
│   ├── map_manager.c/.h            # Quản lý tải động & hiển thị bản đồ (Map Registry)
│   ├── skill_helper.c/.h           # Thư viện hàm tiện ích (VFX Light, Decal, AoE DoT, Mesh)
│   ├── particle_system.c/.h        # Quản lý hạt (GPU Compute & CPU Fallback)
│   ├── trail_system.c/.h           # Quản lý vệt sáng & đạn bay tự định hướng
│   ├── decal_system.c/.h           # Chiếu decal rạn nứt nẻ/vòng tròn sát mặt đất
│   ├── camera_fx.c/.h              # Hiệu ứng camera (Rung giật, xung lực vật lý)
│   ├── screen_distort.c/.h         # Biến dạng thấu kính màn hình (Refraction/Distortion)
│   ├── force_field.c/.h            # Trường lực hạt (Gravity, Wind, Curl Noise)
│   ├── color_gradient.c/.h         # Tiện ích dải màu sắc gradient tuyến tính
│   ├── sprite_anim.c/.h            # Quản lý spritesheet 2D/3D billboard
│   ├── ribbon_strip.c/.h           # Dải ruy-băng vệt sáng 3D
│   ├── path_spline.c/.h            # Nội suy đường spline Catmull-Rom
│   └── shaders/                    # Kho Shaders dùng chung của Core
│
├── sandbox/                        # GIẢ LẬP SANDBOX & TESTING (Không cần cho phát triển skill)
│   ├── sandbox_core.c/.h           # Cập nhật tọa độ nhân vật, AI quái vật, game loop
│   ├── ui_panel.c/.h               # Bảng điều khiển UI debug tham số kỹ năng
│   ├── skill_debugger.c/.h         # Hệ thống test, ghi log tự động & chụp màn hình
│   └── vfx_test.c/.h               # Chương trình chạy test tích hợp ánh sáng & hạt
│
├── environment/                    # HỆ THỐNG MÔI TRƯỜNG (Environment System)
│   └── environment_system.c/.h     # Quản lý ánh sáng toàn cục & bóng đổ thông minh (Fake Shadows)
│
├── skills/                         # MODULE KỸ NĂNG NGUYÊN TỐ (Skills)
│   ├── <element>/                  # Phân nhóm theo Ngũ Hành (fire, water, earth, wood, metal, taiji)
│   │   └── <name>_skill/           # Thư mục riêng của từng kỹ năng
│   │       ├── <name>_skill.c/.h   # Mã nguồn điều khiển logic & dựng hình 3D
│   │       ├── <name>.vs/.fs       # Vertex & Fragment Shader tùy biến của kỹ năng
│   │       └── *.png               # Các texture đi kèm
│   └── generated_skills_registry.h # Tự sinh bởi scripts/generate_registry.py
│
├── maps/                           # MODULE BẢN ĐỒ CHIẾN ĐẤU (Maps)
│   └── <map_name>/                 # Thư mục riêng của từng bản đồ
│       ├── <map_name>.c/.h         # Logic dựng hình địa hình & vật cản 3D
│       ├── <map_name>.vs/.fs       # Shader ánh sáng/nền của map
│       └── *.png                   # Các texture mặt đất/vách đá của map
│
└── scripts/                        # SCRIPTS TIỆN ÍCH TỰ ĐỘNG HÓA
    ├── generate_registry.py        # Tự động quét và đăng ký kỹ năng vào lõi
    └── generate_map_registry.py    # Tự động quét và đăng ký bản đồ vào lõi
```

---

## 2. Các Trụ Cột Kiến Trúc Chính (Core Pillars)

### A. Cơ chế Input Drag-to-Cast (Vẽ Đường Dẫn Tự Do)
- **Hoạt động:** Thay vì click tức thời vào một vị trí mục tiêu đơn lẻ, hệ thống hỗ trợ cơ chế Vector Targeting. Người chơi nhấn giữ chuột trái (hoặc chạm cảm ứng), kéo vẽ một đường dẫn uốn lượn trên mặt đất. Khi nhả chuột (Release), chiêu thức mới được kích hoạt.
- **Truyền nhận tham số:** `SkillParams` chứa:
  - `int pathPointCount`: Số lượng điểm đã vẽ (tối đa 32).
  - `Vector3 pathPoints[32]`: Mảng tọa độ 3D các điểm trên đường dẫn.
- **Nội suy chuyển động:** Các kỹ năng như `seismic_pillars` (trụ đá) hay `wood_thorns` (gai gỗ) sử dụng phương pháp tuyến tính hóa hoặc nội suy khoảng cách giữa các điểm vẽ (`Vector3Lerp`) để xếp đặt vật thể mọc tuần tự dọc theo chính quỹ đạo vẽ tay uốn lượn đó thay vì bắn thẳng.

### B. Hệ thống Bản Đồ Động (Map Manager System)
- **Tải động:** Quản lý qua `MapManager`. Các bản đồ nằm độc lập trong thư mục `maps/`. Phím `K` dùng để đổi map tức thời trong quá trình trải nghiệm game.
- **Cơ cấu API:** Mỗi map plugin bắt buộc expose các hàm lifecycle:
  - `Init[Name]Map(void)`
  - `Update[Name]Map(float dt)`
  - `Draw[Name]Map(void)`
  - `Unload[Name]Map(void)`
- **Đăng ký tự động:** Script `scripts/generate_map_registry.py` tự động quét các thư mục con trong `maps/` để xuất ra registry kết nối vào `core/map_manager.c`.

### C. Ánh Sáng & Bóng Đổ Thông Minh (Environment System)
- **Fake Shadows:** Sử dụng `environment_system.c` chiếu bóng đổ 2D dạng oval mờ dần sát mặt đất dưới chân nhân vật và kẻ địch, khắc phục vấn đề hiệu năng của Realtime Shadow mapping trong Raylib.
- **Static Occluders:** Đăng ký các trụ đá tĩnh cản sáng (`RegisterStaticOccluder`) để tạo ra các vệt đổ bóng dài tĩnh trên sàn đấu theo hướng nguồn sáng chính.

### D. Hệ thống VFX Lõi (VFX Helper APIs)
- Cung cấp các hàm đóng gói (VFX Presets) trong `skill_helper.c` để triệu gọi nhanh chóng nhằm bảo vệ hiệu năng:
  - `SpawnImpactEffect`: Phát nhanh vụ nổ theo preset (lửa, nước, khói, chấn động, biến dạng thấu kính).
  - `SpawnDamageVolume`: Tạo vùng gây sát thương duy trì DoT (Damage over Time) hình tròn/hình hộp tự hủy.
  - `DrawEffectMesh`: Vẽ các mesh cơ bản bằng Flat Shading pháp tuyến bề mặt (Face Normal) góc cạnh rõ ràng gồm chóp tam giác (`TETRAHEDRON`), chóp tứ giác (`PYRAMID`), hình trụ tròn phẳng hai đầu (`CYLINDER`), nón (`CONE`), cầu (`SPHERE`).

---

## 3. Quy Tắc Phát Triển Nghiêm Ngặt Dành Cho AI (Mandatory Rules)

### 1. Quy tắc đặt tên file bắt buộc (Cấm Thay Đổi):
Để các script Python tự động quét sinh cấu hình registry hoạt động chính xác, cấu trúc đặt tên file bắt buộc tuân thủ:
- **Kỹ năng mới:** Lưu tại `skills/<element>/<name>_skill/<name>_skill.c` và `skills/<element>/<name>_skill/<name>_skill.h`.
- **Bản đồ mới:** Lưu tại `maps/<map_name>/<map_name>.c` và `maps/<map_name>/<map_name>.h`.

### 2. Quy tắc Depth Mask & Depth Test:
- Bắt buộc gọi `rlEnableDepthMask()` và `rlEnableDepthTest()` ở đầu hàm vẽ của các mesh 3D kín (trụ đá, gai nhọn) để chống lỗi nhìn xuyên thấu qua nhau và xuyên qua mặt đất/decal.
- Ngược lại, đối với các hạt bán trong suốt (Alpha Blended Particles) vẽ ở sau, phải gọi `rlDisableDepthMask()` khi dùng chế độ `BLEND_ADDITIVE` để tránh che khuất hạt phía sau một cách phi tự nhiên.

### 3. Quy tắc nạp Texture Alpha lên Mesh 3D:
- Cấm nhân trực tiếp kênh Alpha của texture (như texture nứt nẻ đất đá) vào diffuse color trong fragment shader của mesh 3D kín, điều này sẽ tạo ra các lỗ rỗng nhìn xuyên thấu kỳ dị vào lòng mesh rỗng.
- Luôn giữ Alpha của mesh bằng `1.0f` trong shader, chỉ nhân phần RGB của texture để tạo vết nứt tối màu.

### 4. Tiết kiệm Token (Protect Context Window):
- Cấm dùng grep quét toàn bộ dự án. Tuyệt đối không đọc các thư mục bị loại trừ: `_deps/`, `build/`, `android.wuxing_skills/`.
- Khi cần tìm hiểu API lõi, **chỉ được phép đọc file header `.h`** (ví dụ `core/particle_system.h`). Cấm mở đọc file thực thi `.c` của hệ thống lõi.
