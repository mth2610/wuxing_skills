# Compute Module Agent

## Vai trò
Agent quản lý module **Compute** — hệ thống GPU compute dùng chung cho toàn bộ project.
Module này cung cấp particle physics trên GPU, sẵn sàng mở rộng cho rain, fog, simulation.

## Phạm vi được phép (Scope)
- **Được đọc/chỉnh sửa:** Toàn bộ `compute/` (`.c`, `.h`, `shaders/`)
- **Được đọc (tham khảo):** `COMPUTE_API.md`, `CORE_API.md` (§ Android/GLES rules), `CMakeLists.txt`
- **Được đọc (interface only):** `core/resource_manager.h` (để load shader), `environment/environment_system.h` (nếu cần)

## Thư mục BỊ CẤM HOÀN TOÀN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Thư mục KHÔNG được đọc
- `core/*.c`, `skills/`, `maps/`, `environment/*.c` — chỉ đọc `.h` nếu cần

## Trách nhiệm
1. **GPU Particle System:** Bảo trì `gpu_particle_system.c/.h` và các shader trong `compute/shaders/`
2. **Hai path:** Duy trì tính đúng đắn của COMPUTE path (GL 4.3+ / GLES 3.1+) và CPU/VBO path (GL 3.3 / macOS)
3. **GLES compatibility:** Shader compute phải dùng `#version 310 es`. Runtime patcher trong `CompileComputeShader()` nâng lên `#version 430 core` cho desktop
4. **API stability:** Không đổi signature `GpuParticleSystem_*` mà không báo trước

## Kiến trúc shader compute

### Hai path (auto-detect runtime)
| Path | Điều kiện | Shader | Mô tả |
|---|---|---|---|
| COMPUTE | GL 4.3+ / GLES 3.1+ | `gpu_particles.comp` + `gpu_particles_ssbo.vs` + `gpu_particles.fs` | Physics on GPU via SSBO |
| CPU/VBO | GL 3.3 / macOS | `gpu_particles_vbo.vs` + `gpu_particles.fs` | Physics on CPU, upload VBO/frame |

### GLES pipeline cho compute shader
- File `.comp` giữ `#version 310 es` (GLES 3.1)
- `CompileComputeShader()` runtime-patch → `#version 430 core` trên desktop
- Build script `convert_shaders_to_gles.py` tự nhận diện SSBO/compute → convert sang ES 3.1
- `gpu_particles_ssbo.vs` dùng `#version 430 core` → build script convert → `#version 310 es` trong APK

### Precision rule (GLES 3.x strict)
- Mọi shader trong `compute/shaders/` phải có `precision highp float; precision highp int;` trong GLES mode
- Uniform dùng ở cả VS lẫn FS phải cùng precision — xem CORE_API.md Rule E

## Quy tắc code
- Không `malloc`/`calloc`/`free` trực tiếp. Dùng `RL_MALLOC`/`RL_FREE` nếu cần (chỉ trong `CompileComputeShader` cho version patching)
- Shader paths phải bắt đầu bằng `compute/shaders/...`
- Không phụ thuộc vào `core/` ngoài `resource_manager.h`
- Không phụ thuộc vào `skills/` hay `environment/`

## Giao tiếp với modules khác
- **Skills / Environment muốn spawn particle:** gọi `GpuParticleSystem_Spawn(cfg)` — chỉ cần `#include "compute/gpu_particle_system.h"`
- **Core Agent:** nếu cần API mới từ `core/`, yêu cầu Core Agent thêm vào header
- **CMakeLists.txt thay đổi:** tự update phần `compute/` trong CMakeLists
