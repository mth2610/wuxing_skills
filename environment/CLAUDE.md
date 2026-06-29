# Environment Module Agent

## Vai trò
Agent quản lý module **Environment System** của dự án Wuxing Skills. Chịu trách nhiệm về ánh sáng, bóng đổ giả (Smart Fake Shadow), sương mù, ambient color, sun direction — toàn bộ bầu không khí toàn cục của engine.

## Phạm vi được phép (Scope)
- **Được đọc/chỉnh sửa:** Toàn bộ thư mục `environment/` (`environment_system.h`, `environment_system.c`)
- **Được đọc (bắt buộc):** `ENVIRONMENT_API.md`
- **Được đọc (tham khảo):** `MAP_API.md` (phần 4 — để hiểu cách Map Agent dùng Environment API)
- **Được đọc (interface only):** `core/skill_manager.h` (chỉ phần color), `core/decal_system.h` (Smart Shadow dùng decal system)

## Thư mục BỊ CẤM HOÀN TOÀN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Thư mục KHÔNG được đọc khi chưa được phép
- `skills/` (không đọc gì trừ khi được yêu cầu rõ ràng)
- `maps/` (không đọc gì)
- `core/` (chỉ đọc `.h`, không đọc `.c`)
- `sandbox/`

## Trách nhiệm hệ thống

### 1. Smart Fake Shadow
```c
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);
```
- Tự động scale/fade shadow theo độ cao `pos.y`
- Shadow ngả theo `s_sunDirection`
- Soft edges, không double-blending
- **KHÔNG được thay thế bằng real-time shadow**

### 2. Lighting Config
```c
Vector3 Environment_GetSunDirection(void);  void Environment_SetSunDirection(Vector3 dir);
Color   Environment_GetSunColor(void);      void Environment_SetSunColor(Color col);
Color   Environment_GetAmbientColor(void);  void Environment_SetAmbientColor(Color col);
Color   Environment_GetShadowColor(void);   void Environment_SetShadowColor(Color col);
```

### 3. Fog
```c
EnvFogConfig Environment_GetFogConfig(void);
void         Environment_SetFogConfig(EnvFogConfig config);
```

### 4. Lifecycle (được Core gọi tự động — KHÔNG để skills/maps gọi lại)
```c
void Environment_Init(void);
void Environment_Update(float dt);
```

## Quy tắc quan trọng
- `Environment_Init` và `Environment_Update` **chỉ** được gọi từ `sandbox_core.c`. Skills và Maps không được gọi lại.
- Mọi thay đổi public API phải cập nhật `ENVIRONMENT_API.md`.
- Default sun direction: Tây Nam `normalize(0.5, -0.8, -0.3)` — đây là hướng chuẩn của toàn project.
- Shadow system dùng decal pool từ `core/decal_system.h` — không tạo decal pool riêng.

## Giao tiếp với agents khác
- Map Agent CÓ THỂ gọi `Environment_Set*` trong `InitMap` — đây là thiết kế đúng
- Skills Agent có thể đọc `Environment_GetSunDirection()` để tính hiệu ứng ánh sáng — OK
- Nếu cần thêm API mới: thêm vào `environment_system.h/.c` và cập nhật `ENVIRONMENT_API.md`
- Không được tự sửa `core/`, `maps/`, `skills/`
