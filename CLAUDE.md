# Wuxing Skills — Root Agent Guide

Dự án game C/Raylib 5.5 / OpenGL 3.3. Isometric Night-time Arena. 6 nguyên tố: Water, Wood, Fire, Earth, Metal, Taiji.

## Tài liệu tham chiếu
- `CORE_API.md` — API đầy đủ của engine (particle, trail, force field, shader, mesh...)
- `ENVIRONMENT_API.md` — Hệ thống ánh sáng, shadow, fog
- `MAP_API.md` — Tạo và quản lý bản đồ
- `VFX_ARCHITECTURE.md` — Kiến trúc VFX tổng thể
- `WUXING_ART_DIRECTION.md` — Phong cách nghệ thuật và aesthetic laws

## Module Agents — Phân vùng trách nhiệm

| Agent | Thư mục sở hữu | Được đọc thêm |
|---|---|---|
| **Core Agent** | `core/` | headers `.h` của skills, maps, environment |
| **Skills Agent** | `skills/` | `core/*.h`, `environment/environment_system.h`, `assets/` |
| **Map Agent** | `maps/` | `environment/environment_system.h`, `core/skill_manager.h`, `assets/` |
| **Environment Agent** | `environment/` | `core/decal_system.h`, `core/skill_manager.h` |

## Thư mục CẤM với MỌI agent
```
build/
_deps/
android.wuxing_skills/
```
Không đọc, không list, không touch bất kỳ file nào trong các thư mục trên.

## Quy tắc cross-module
- Agent nhỏ chỉ đọc `.h` (interface) của module khác, **không đọc `.c`**
- Muốn sửa file thuộc module khác → yêu cầu agent chủ sở hữu module đó thực hiện
- Breaking API changes → phải thông báo qua tài liệu trước khi apply

## Build
```bash
make          # Build toàn bộ project
./wuxing      # Chạy game
# Phím K: chuyển đổi map
```

## Tọa độ & Scale chuẩn
- Arena center: `(600.0f, 0.0f, 440.0f)`, radius: `1800.0f`
- Y = 0.0f: mặt đất
- Radii mesh: 10–20f | Force/gravity: 300–700f | Particle speed: 100–300f
