# ĐẶC TẢ KIẾN TRÚC HỆ THỐNG VFX ENGINE (C99 & RAYLIB)

> **Bối cảnh dự án:** Game Fighting 3D Mobile (Android, Raylib, Pure C99).
> **Nguyên tắc cốt lõi:** KHÔNG `malloc/free` runtime (chỉ sử dụng Static Pool + Free-List $O(1)$), KHÔNG magic number, cấu trúc dữ liệu phẳng thân thiện với CPU Cache (Data-Oriented Design).

---

## I. SƠ ĐỒ TỔ CHỨC THƯ MỤC VÀ FILE

Hệ thống được phân rã thành 3 tầng độc lập, đảm bảo phần Core Hệ thống đóng băng (frozen) và chỉ tập trung phát triển mở rộng ở tầng Logic Gameplay.

```text
vfx_engine/
│
├── core/                         # 1. TẦNG HỆ THỐNG LÕI (Bất di bất dịch, pure C99)
│   ├── force_field.h             # Định nghĩa cấu trúc ForceLayer và các hằng số enum loại lực
│   ├── force_field.c             # Trình toán học tích phân lực (Evaluate) cho điểm, trục động
│   ├── particle_system.h         # Quản lý Static Pool hạt, định nghĩa Over-Lifetime Curves
│   ├── particle_system.c         # Update/Draw hạt, xử lý Flipbook/Texture Sheet Animation
│   ├── trail_system.h            # Quản lý Static Pool dải lụa (Follower, Wisp, Projectile)
│   ├── trail_system.c            # Tích phân Euler cho từng Node Trail, GS constraints khoảng cách
│   ├── ribbon_strip.h            # Khai báo cấu trúc RibbonPoint/RibbonVertex và hàm dựng chuỗi đỉnh
│   ├── ribbon_strip.c            # Xử lý ma trận góc nhìn Camera để tính toán bề rộng dải lụa camera-facing
│   └── vfx_curves.h              # Định nghĩa các cấu trúc mảng tĩnh cho Curve/Gradient Over-Lifetime
│
├── shaders/                      # 2. TẦNG ĐỒ HỌA SHADER (Xử lý hoàn toàn trên GPU)
│   ├── include/
│   │   └── vfx_shared.glsl       # Chứa các hàm common: Perlin Noise GPU, Flow Mapping (Sawtooth blend)
│   ├── core_particle.vs/fs       # Shader mặc định cho hệ thống hạt (Billboard, Flipbook UV)
│   ├── metal_skill.fs            # Shader hiệu ứng Kim (Ánh kim sắc bén, vệt chém sáng)
│   └── fluid_blob.vs/fs          # Shader hiệu ứng Thủy (Vertex Displacement + Flow Mapping bọt nước)
│
└── skills/                       # 3. TẦNG LOGIC GAMEPLAY (Nơi skill mới được thêm vào nhẹ nhàng)
    ├── skill_manager.h           # Registry quản lý, kích hoạt, cooldown của bộ Ngũ Hành Skill
    ├── skill_manager.c           # Điều phối vòng lặp Update/Draw từ Main Loop vào các skill active
    ├── metal_skill.c             # Cấu hình cụ thể chiêu Mưa Kiếm (gọi Trail, gán bộ 4 lớp lực trục động)
    └── fluid_skill.c             # Cấu hình cụ thể chiêu Khối Nước/Mưa Nước (gọi Mesh Sphere, set Uniform)


[Gameplay Trigger] -> Nhấn nút cast chiêu -> Kích hoạt Emitter tĩnh trong bộ quản lý Skill
       │
       ▼
[Core Spawning]    -> Sinh vật thể, nạp con trỏ hằng ForceField của Skill vào pool dữ liệu phẳng
       │
       ▼
[Physics Update]   -> Vòng lặp Core quét mảng tĩnh, ném tọa độ từng node vào ForceField_Evaluate()
       │              Nhận gia tốc tổng hợp và áp dụng tích phân Euler bán ngầm (Semi-implicit Euler)
       │
       ▼
[Anchor Update]    -> Late Update: Đồng bộ điểm neo bám sát xương mô hình (không lệch quad hiển thị)
       │              Cập nhật hệ tọa độ "Trục động" mới (axisOrigin/axisDir) phục vụ cho frame kế tiếp
       │
       ▼
[Render Submission]-> Gom nhóm các thực thể cùng loại Shader, bind texture flowmap phụ vào kênh texture 1
                      Gọi rlgl submit chuỗi Triangle Strip qua Ribbon, GPU thực thi biến dạng dòng chảy.