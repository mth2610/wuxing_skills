# Kiến Trúc VFX & Skill Engine — Ngũ Hành Tỷ Võ
> **Tài liệu hướng dẫn & Quy tắc tiết kiệm Token dành cho AI Developer**

Tài liệu này định nghĩa cấu trúc thư mục, kiến trúc lõi của hệ thống VFX và các quy tắc bắt buộc dành cho AI khi phát triển kỹ năng (skills) hoặc tính năng mới nhằm tối ưu hóa hiệu năng làm việc và **tiết kiệm token tối đa**.

---

## 1. Cấu trúc thư mục (Directory Structure)

Toàn bộ mã nguồn được phân chia rõ ràng thành hai phần chính: **Core** (Hệ thống lõi) và **Skills** (Các kỹ năng nguyên tố).

```
wuxing_skills/
├── main.c                      # Điểm chạy chính (Desktop Entry)
├── CMakeLists.txt              # Cấu hình build hệ thống Desktop (CMake)
├── Makefile.Android            # Cấu hình build hệ thống Android APK
├── core/                       # HỆ THỐNG LÕI (Không chứa logic skill cụ thể)
│   ├── color_gradient.c/.h     # Quản lý dải màu chuyển hạt
│   ├── emitter_system.c/.h     # Bộ phát hạt (Particle Emitters)
│   ├── flow_map.c/.h           # Biến dạng dòng chảy UV
│   ├── force_field.c/.h        # Lớp lực tác động lên hạt (Gravity, Viscosity, Curl Noise...)
│   ├── particle_system.c/.h    # Engine quản lý hạt (GPU Compute Shader & CPU fallback)
│   ├── path_spline.c/.h        # Thuật toán nội suy đường spline Catmull-Rom
│   ├── ribbon_strip.c/.h       # Dải ruy-băng 3D vẽ bằng mesh
│   ├── sandbox_core.c/.h       # Cấu hình sandbox giả lập nhân vật và quái
│   ├── skill_manager.c/.h      # Đăng ký & quản lý vòng đời toàn bộ kỹ năng
│   ├── sprite_anim.c/.h        # Hoạt ảnh chuỗi hình ảnh (Spritesheet animation)
│   ├── trail_system.c/.h       # Quản lý kiếm bay & dải khí (Smart Projectiles)
│   ├── ui_panel.c/.h           # Giao diện UI debug tham số kỹ năng
│   ├── utils_math.c/.h         # Các hàm tiện ích toán học
│   ├── FORCE_FIELD_GUIDE.md    # Hướng dẫn chi tiết hệ thống ForceField
│   └── shaders/                # Shader dùng chung & shader hệ thống lõi
│       ├── additive_soft.fs, dissolve.fs, flow_map.fs, rim_glow.fs
│       ├── particles.comp, particles.vs, particles.fs
│       └── particles_cpu.vs, particles_cpu.fs, taiji.fs
└── skills/                     # MODULE KỸ NĂNG (Phân chia theo Hệ Ngũ Hành)
    ├── metal/                  # Hệ Kim
    │   ├── metal_projectile/   # Chiêu Kim Kiếm bắn thẳng (metal_skill)
    │   └── sword_rain/         # Chiêu Mưa Kiếm rơi từ trời (sword_rain)
    ├── wood/                   # Hệ Mộc
    │   └── wood_roots/         # Chiêu Rễ cây bám đất trồi lên (wood_skill)
    ├── water/                  # Hệ Thuỷ
    │   ├── water_projectile/   # Chiêu Bắn hạt nước nhỏ xoắn chéo (fluid_skill)
    │   ├── water_stream/       # Chiêu Dòng nước cuộn uốn lượn (tube_skill)
    │   └── water_shield/       # Chiêu Hộ thuẫn khiên nước (shield_skill)
    ├── fire/                   # Hệ Hoả
    │   └── fire_ball/          # Chiêu Quả cầu lửa đầu rồng (fire_skill)
    ├── earth/                  # Hệ Thổ (Chờ phát triển ở Phase 4)
    └── taiji/                  # Tuyệt học Thái Cực (Trắng - Đen)
        ├── wind_storm/         # Chiêu Gió cuộn hút đạn (wind_skill)
        └── lightning/          # Chiêu Sét đánh hủy diệt (electric_skill)
```

---

## 2. Quy tắc tiết kiệm Token dành cho AI (AI Token-Saving Rules)

Để tránh lãng phí context window và token của mô hình ngôn ngữ lớn, AI **bắt buộc** phải tuân thủ nghiêm ngặt các quy tắc sau khi đọc và viết mã nguồn:

### ⚠️ QUY TẮC 1: KHÔNG ĐỌC TOÀN BỘ PROJECT
* **KHÔNG BAO GIỜ** dùng các câu lệnh grep, find hoặc đọc toàn bộ thư mục một cách bừa bãi.
* Chỉ đọc thư mục chứa skill hoặc chức năng cụ thể đang được chỉnh sửa. Ví dụ: Nếu đang sửa chiêu Quả cầu lửa, chỉ đọc các file trong `skills/fire/fire_ball/`.

### ⚠️ QUY TẮC 2: CHỈ ĐỌC FILE HEADER (.h) CỦA HỆ THỐNG LÕI
* Khi cần hiểu API của Particle, Trail hay ForceField, AI **chỉ được phép đọc file header** (ví dụ `core/particle_system.h`, `core/trail_system.h`).
* **TUYỆT ĐỐI CẤM** đọc file implement `.c` của hệ thống lõi (ví dụ `core/particle_system.c`) vì các file này rất dài và tốn token vô ích.

### ⚠️ QUY TẮC 3: DÙNG ĐƯỜNG DẪN KHAI BÁO INCLUDE RÕ RÀNG
* Tất cả các file C/C++ trong dự án phải dùng đường dẫn tương đối bắt đầu từ root của project khi `#include` các header nội bộ:
  * Ví dụ đúng: `#include "core/particle_system.h"`, `#include "skills/metal/metal_projectile/metal_skill.h"`
  * Ví dụ sai: `#include "particle_system.h"` (gây mơ hồ và bắt buộc AI phải quét thư mục để tìm).

---

## 3. Quy trình phát triển một Kỹ năng mới (Skill Creation Workflow)

Khi AI được giao nhiệm vụ tạo một skill mới, ví dụ chiêu đá rơi hệ Thổ (`earth_rock`), quy trình thực hiện như sau:

1. **Khởi tạo thư mục riêng biệt**:
   * Tạo thư mục `skills/earth/earth_rock/`.
2. **Viết mã nguồn kỹ năng**:
   * Tạo `earth_rock.h` và `earth_rock.c` bên trong thư mục đó.
   * Expose các hàm lifecycle chuẩn hóa:
     ```c
     void InitEarthRockSkill(int screenWidth, int screenHeight);
     void CastEarthRockSkill(Vector3 startPos, Vector3 target, SkillParams params);
     void UpdateEarthRockSkill(float dt, Vector3 enemyPos, float enemyRadius);
     void DrawEarthRockSkill(void); // optional
     void UnloadEarthRockSkill(void);
     ```
3. **Đặt Shader và Texture đi kèm**:
   * Nếu chiêu thức dùng shader riêng biệt (ví dụ `rock.fs`) hoặc texture riêng biệt (`rock_dust.png`), hãy đặt chúng **ngay bên trong** thư mục `skills/earth/earth_rock/`.
4. **Đăng ký kỹ năng vào Skill Manager**:
   * Mở file `core/skill_manager.c`.
   * Thêm `#include "skills/earth/earth_rock/earth_rock.h"`.
   * Đăng ký trong hàm `EnsureBuiltInRegistered()` bằng lệnh `RegisterSkill`.
5. **Cập nhật Build System**:
   * **CMake (Desktop)**: Cập nhật `CMakeLists.txt` bằng cách thêm file `.c` mới vào `add_executable` và cấu hình lệnh `configure_file` để copy shader/texture của skill đó sang thư mục build.
   * **Android**: Cập nhật `Makefile.Android` bằng cách thêm file `.c` mới vào `PROJECT_SOURCE_FILES`, cấu hình `create_temp_project_dirs` và `copy_project_resources` để copy tài nguyên vào build APK.
