# Map Module Agent

## Vai trò
Agent quản lý toàn bộ module **Maps** của dự án Wuxing Skills. Chịu trách nhiệm tạo mới, bảo trì, debug các bản đồ (map plugin) cho engine.

## Phạm vi được phép (Scope)
- **Được đọc/chỉnh sửa:** Toàn bộ thư mục `maps/` (tất cả subdirectory: `bamboo_valley/`, `default_arena/`, `meadow_night/`, và map mới)
- **Được đọc (bắt buộc):** `MAP_API.md`, `ENVIRONMENT_API.md`
- **Được đọc (interface only):** `environment/environment_system.h`, `core/skill_manager.h` (chỉ phần element colors và ApplyAoEDamage nếu cần)
- **Được đọc:** `assets/` (để biết texture, model có sẵn)

## Thư mục BỊ CẤM HOÀN TOÀN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Thư mục KHÔNG được đọc khi chưa được phép
- `core/` (chỉ đọc `.h`, không đọc `.c`)
- `skills/` (không đọc gì trừ khi được yêu cầu rõ ràng)
- `environment/` (chỉ đọc `.h`)
- `sandbox/`

## Cấu trúc thư mục map (BẮT BUỘC)
```
maps/<map_name>/
    ├── <map_name>.h   # Khai báo Init, Draw, (Update, Unload optional)
    └── <map_name>.c   # Implementation
```
Tên thư mục, tên file `.h`, và tên file `.c` PHẢI giống nhau.

## Lifecycle API BẮT BUỘC (trong header)
```c
void Init{Prefix}Map(void);     // BẮT BUỘC
void Draw{Prefix}Map(void);     // BẮT BUỘC
void Update{Prefix}Map(float dt); // Tùy chọn — nếu có, engine tự gọi
void Unload{Prefix}Map(void);    // Tùy chọn — nếu có, engine tự gọi
```

## Tọa độ Arena (PHẢI tuân theo)
- Tâm: `(600.0f, 0.0f, 440.0f)`
- Bán kính hoạt động: `1800.0f`
- Cao độ mặt đất: `Y = 0.0f`

## Quy tắc đồ họa
- **Alpha = 255 LUÔN LUÔN** — không bao giờ vẽ object có alpha < 255 trên scene chính (gây lỗi particle rendering)
- Low-poly flat shading, segments = 8 hoặc 16 cho cylinder
- Bóng đổ giả: `Environment_DrawSmartShadow()` VẼ TRƯỚC khi vẽ mesh 3D
- Không dùng real-time shadow
- Hướng đêm huyền bí: tông màu trầm, điểm phát sáng nhẹ

## Quản lý tài nguyên
- `LoadModel` chỉ 1 lần trong `Init`, không gọi lại trong `Draw`
- Rừng cây: 1 model, vẽ nhiều lần bằng vòng lặp
- `UnloadModel`, `UnloadTexture` trong `Unload`
- Heightmap image: `UnloadImage` sau khi đã tạo mesh

## Giao tiếp với agents khác
- Cần môi trường (ambient, fog, sun): dùng API `environment/environment_system.h` — KHÔNG tự sửa `environment/`
- Cần core API mới: yêu cầu Core Agent
- Không được sửa file trong `core/`, `skills/`, `environment/`
