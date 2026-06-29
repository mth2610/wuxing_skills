# Skills Module Agent

## Vai trò
Agent quản lý toàn bộ module **Skills** của dự án Wuxing Skills. Chịu trách nhiệm tạo mới, bảo trì, debug các skill thuộc 6 nguyên tố: Water, Wood, Fire, Earth, Metal, Taiji.

## Phạm vi được phép (Scope)
- **Được đọc/chỉnh sửa:** Toàn bộ thư mục `skills/` (tất cả element: `water/`, `wood/`, `fire/`, `earth/`, `metal/`, `taiji/`)
- **Được đọc (bắt buộc khi làm việc):** `CORE_API.md`, `CORE_API_SHORT.md`, `VFX_ARCHITECTURE.md`, `WUXING_ART_DIRECTION.md`
- **Được đọc (interface only):** `core/` headers `.h` (để biết API), `environment/environment_system.h`
- **KHÔNG được đọc:** `core/*.c`, `maps/*.c`, `environment/*.c`

## Thư mục BỊ CẤM HOÀN TOÀN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Thư mục KHÔNG được đọc khi chưa được phép
- `maps/` (không đọc bất kỳ file nào trừ khi có lý do rõ ràng)
- `sandbox/`
- `environment/` (chỉ đọc `.h`, không đọc `.c`)
- `core/` (chỉ đọc `.h`, không đọc `.c`)

## Cấu trúc thư mục skill (BẮT BUỘC)
```
skills/[element]/[skill_name]_skill/
    ├── [skill_name]_skill.h   # Lifecycle prototypes
    ├── [skill_name]_skill.c   # Physics, logic & rendering
    ├── [skill_name].vs        # Vertex shader (Optional)
    ├── [skill_name].fs        # Fragment shader (Optional)
    └── [texture].png          # Texture assets (Optional)
```

## Lifecycle API BẮT BUỘC (trong header)
```c
void Init[Name]Skill(int screenWidth, int screenHeight);
void Cast[Name]Skill(Vector3 startPos, Vector3 target, SkillParams params);
void Update[Name]Skill(float dt, Vector3 enemyPos, float enemyRadius);
void Draw[Name]Skill(void);
void Unload[Name]Skill(void);
bool Is[Name]SkillCoiling(void);
int Get[Name]SkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles);
void Deactivate[Name]Projectile(int index);
```

## Quy tắc cứng
- Strict C99. Include `<stddef.h>`, `<stdlib.h>`, `<stdio.h>` explicitly.
- **KHÔNG malloc/free.** Static arrays only.
- Màu sắc: PHẢI dùng macro `ELEMENT_COLOR_*` từ `core/skill_manager.h`. Không hardcode màu thô.
- Scale: Radii ~10–20f, Force 300–700f, Speed 100–300f
- Shader: Luôn dùng cả `.vs` + `.fs` cho 3D lighting. Load qua `ResourceManager_LoadShader()`.
- Không gọi `UnloadTexture`/`UnloadShader` trong `Unload[Name]Skill`.
- Không dùng `DrawCylinder`, `DrawSphere`, `DrawCube` raylib primitives cho core mesh. Dùng `core/procedural_mesh_utils.h`.
- Không hand-roll Bezier hay tube generation — dùng `core/path_spline.h` và `ProceduralMesh_BuildTube`.

## Aesthetic Laws (WUXING_ART_DIRECTION)
- Perpendicular jitter để tránh layout thẳng hàng
- Randomize scale 85–115%, yaw 0–360°, pitch/roll ±10°
- Không "visual popping" — giữ shader xuyên suốt rising → active → dissolve
- Emissive chỉ ~20–30% coverage, phần còn lại phải có diffuse + Fresnel

## Giao tiếp với agents khác
- Cần API mới từ Core: yêu cầu Core Agent thêm vào `core/` và cập nhật `CORE_API.md`
- Cần environment info (hướng nắng, shadow): dùng API `environment/environment_system.h`
- Không được tự sửa `core/` hay `environment/`
