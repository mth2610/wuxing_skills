Markdown# ĐẶC TẢ KIẾN TRÚC HỆ THỐNG VFX ENGINE (C99 & RAYLIB)

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
II. BẢNG MÔ TẢ CẤU TRÚC CHI TIẾT (ARCHITECTURAL SPECIFICATION)Tên Module / Thành phầnKiểu tổ chức dữ liệu (Data Layout)Nhiệm vụ chính trong EngineKhả năng mở rộng cho tương laiforce_fieldMảng phẳng tĩnh chứa các ForceLayer cục bộ (Max 8 layers/field).Nhận tọa độ + vận tốc của một điểm, quét qua các lớp lực đang active để trả về một Vector gia tốc tổng hợp (Additive Acceleration).Thêm lực mới (như lực va chạm, lực nảy) chỉ cần thêm enum và viết thêm toán học trong switch-case tại .c.particle_systemStatic Pool Array cố định kích thước. Mỗi hạt là một struct phẳng.Cập nhật tuổi thọ (lifeRatio), nội suy kích thước/màu sắc dựa trên Over-Lifetime Curves, lật trang UV cho hoạt ảnh Flipbook.Thêm tính năng hạt Mesh 3D bằng cách bổ sung một cờ RENDER_MODE_MESH bên cạnh mode QUAD mặc định.trail_systemStatic Pool Array cho các TrailEntity. Mỗi dải quản lý một Ring-Buffer Array chứa các Node vị trí quá khứ.Tích phân chuyển động cho từng node độc lập dựa trên ForceField, ghim điểm đầu theo điểm neo, giải quyết ràng buộc khoảng cách bằng Gauss-Seidel.Hỗ trợ mọi hình thái kỹ năng dạng dải (Rồng lửa, Roi mây, Dây xích). Chỉ cần skill bơm vị trí neo và hướng trục vào mỗi frame.ribbon_stripKhông lưu trữ bộ nhớ dài hạn, chỉ sử dụng Scratch Buffer Array dùng chung mỗi frame.Nhận mảng các Node vị trí 3D, dựa trên hướng Camera để dựng các cặp đỉnh (Left/Right vertices) vuông góc, tạo thành Triangle Strip liên tục.Hỗ trợ cuộn texture (Texture Scrolling) hoặc áp Flow Mapping lên dải lụa bằng cách map dữ liệu v (UV dọc) chạy theo thời gian.skill_managerMảng tĩnh quản lý trạng thái các Emitter đang hoạt động của tất cả các hệ.Đóng vai trò làm "Trạm trung chuyển". Đọc thông số kỹ năng từ Gameplay, ra lệnh cho particle_system và trail_system sinh vật thể hiển thị.Khi thêm một hệ mới (ví dụ hệ Hỏa fire_skill.c), bạn chỉ cần đăng ký hàm Init/Update/Draw của nó vào Manager mà không phải sửa một dòng nào của tầng Core.III. THIẾT KẾ ĐẶC TẢ CÁC ĐẶC TÍNH NÂNG CAO1. Hệ thống Sub-Emitter (Hạt sinh hạt không Malloc)Để loại bỏ việc cấp phát động, các hạt con được sinh ra dựa trên một bảng thiết kế sự kiện tĩnh (SubEmitterBlueprint). Hệ thống Core quản lý vòng đời và tự kích hoạt vòng lặp sinh hạt từ Pool tĩnh.C// Định nghĩa các loại sự kiện kích hoạt sinh hạt con
typedef enum {
  VFX_EVENT_ON_BIRTH,   // Sinh ra ngay khi hạt cha vừa xuất hiện
  VFX_EVENT_ON_TICK,    // Sinh liên tục dọc đường bay dựa theo thời gian định kỳ
  VFX_EVENT_ON_DEATH    // Sinh ra khi hạt cha hết lifetime (ví dụ: vỡ vụn, nổ bùng)
} VfxEventType;

// Bản thiết kế tĩnh của một Sub-emitter (Nằm cố định trong bộ nhớ tĩnh của Skill)
typedef struct {
  VfxEventType eventType;
  int targetOwnerTag;       // Tag của hệ skill nhận hạt con này (ví dụ: METAL_SKILL_TAG)
  
  // Tần suất sinh hạt
  float spawnInterval;      // Khoảng thời gian giữa mỗi lần sinh (Chỉ dùng cho hành vi ON_TICK)
  int minSpawnCount;        // Số hạt sinh tối thiểu mỗi lần kích hoạt
  int maxSpawnCount;        // Số hạt sinh tối đa mỗi lần kích hoạt

  // Cấu hình ngẫu nhiên cho hạt con khi khởi tạo
  float minLife, maxLife;
  float minSpeed, maxSpeed;
  float minRadius, maxRadius;
  Color colorStart;
  Color colorEnd;
  
  const ForceField *forceField; // Lực áp vào hạt con
} SubEmitterBlueprint;

// Bổ sung vào cấu trúc dữ liệu của hạt cha trong Pool tĩnh
typedef struct {
  // ... Các dữ liệu vật lý cũ của hạt cha ...
  const SubEmitterBlueprint *subEmitter; // Bộ blueprint cấu hình đính kèm (NULL nếu không dùng)
  float subEmitterTimer;                 // Đồng hồ đếm ngược phục vụ riêng cho sự kiện ON_TICK
} ParticleEntity;
2. Tích hợp Đỉnh Hình Học Hỗ Trợ Flow MappingĐể Shader trên GPU có thể thực hiện uốn cong dòng chảy và cuộn bề mặt một cách tối ưu, cấu trúc dữ liệu đỉnh của ribbon_strip cần cung cấp đủ hai bộ tọa độ UV: một bộ cố định bề mặt, một bộ tịnh tiến theo thời gian.C// Cấu trúc dữ liệu đỉnh chuẩn hóa để submit dữ liệu lên GPU thông qua rlgl
typedef struct {
  Vector3 position;  // Vị trí không gian thế giới 3D
  Vector2 uv0;       // Bộ UV gốc: Dùng để map texture hiển thị chính dọc theo chiều dài trail
  Vector2 uv1;       // Bộ UV phụ: Dùng để map vân Flow Map chứa dữ liệu vector hướng chảy
  Color tint;        // Màu sắc vertex (hỗ trợ pha màu mượt từ đỉnh)
} RibbonVertex;
IV. GIẢI THUẬT SHADER FLOW MAPPING (GPU SAWTOOTH BLENDING)Giải thuật này sử dụng hàm fract() để tạo hai pha răng cưa lệch pha nhau $0.5$ chu kỳ, triệt tiêu hoàn toàn hiện tượng kéo giãn thô bạo của vân texture, tạo ảo giác dòng chảy chuyển động liền mạch vô tận.OpenGL Shading Language#version 330

in vec2 fragTexCoord; // Nhận UV từ Vertex Shader
in vec4 fragColor;

uniform sampler2D texture0;  // Diffuse Map (Ví dụ: Vân nước xiết hoặc vân ngọn lửa)
uniform sampler2D flowMap;   // Flow Map (Kênh R, G chứa vector vận tốc hướng [-1, 1])
uniform float u_time;        // Trục thời gian thực cập nhật liên tục từ CPU
uniform float u_flowSpeed;   // Hệ số nhân tốc độ dòng chảy cục bộ

void main() {
    // 1. Đọc dữ liệu từ Flow map và giải mã từ dải màu [0, 1] về dải số thực hướng vector [-1, 1]
    vec2 flowDir = texture(flowMap, fragTexCoord).rg * 2.0 - 1.0;
    flowDir *= u_flowSpeed;

    // 2. Thiết lập hai pha tiến trình răng cưa lệch nhau nửa chu kỳ thời gian
    float progressA = fract(u_time);
    float progressB = fract(u_time + 0.5);

    // 3. Tính toán hai tọa độ UV mới đã bị biến dạng bởi vector dòng chảy
    vec2 uvA = fragTexCoord + flowDir * progressA;
    vec2 uvB = fragTexCoord + flowDir * progressB;

    // 4. Tính toán trọng số Blend hình quả chuông để che giấu khoảnh khắc UV bị reset về 0
    float weightB = abs(0.5 - progressA) / 0.5; // Chạy tuần hoàn mượt mà từ 1.0 -> 0.0 -> 1.0
    float weightA = 1.0 - weightB;

    // 5. Lấy mẫu màu sắc từ hai pha độc lập và thực hiện pha trộn
    vec4 texA = texture(texture0, uvA) * weightA;
    vec4 texB = texture(texture0, uvB) * weightB;

    finalColor = (texA + texB) * fragColor;
}
V. LUỒNG DỮ LIỆU ĐỒNG BỘ MỖI FRAME (DATA FLOW)Sơ đồ tuần tự xử lý dữ liệu đảm bảo tính độc lập giữa các tầng và tối ưu hóa tốc độ truy cập bộ nhớ:Plaintext[Gameplay Trigger] -> Nhấn nút cast chiêu -> Kích hoạt Emitter tĩnh trong bộ quản lý Skill
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