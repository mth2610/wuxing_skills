# COMPUTE MODULE API

> Module GPU compute dùng chung — particle physics, rain, simulation.
> Nằm ở `compute/`, độc lập với `core/`, dùng được từ skills, environment, maps.

---

## 1. Tổng quan kiến trúc

Module tự detect khả năng GPU lúc runtime và chọn một trong hai path:

| Path | Điều kiện | Mô tả |
|---|---|---|
| **COMPUTE** | GL 4.3+ (desktop) hoặc GLES 3.1+ (Android Mali-G68+) | Physics hoàn toàn trên GPU — dispatch compute shader, SSBO |
| **CPU/VBO** | GL 3.3 (macOS) hoặc thiết bị cũ | Physics trên CPU, upload VBO mỗi frame |

Không cần kiểm tra path từ phía caller — API giống nhau ở cả hai path.

---

## 2. Tích hợp vào game loop (main.c)

```c
#include "compute/gpu_particle_system.h"

// Sau InitWindow():
GpuParticleSystem_Init();

// Trong game loop — Update phase:
GpuParticleSystem_Update(dt);

// Trong game loop — Draw 3D phase (sau BeginMode3D / MyBeginMode3D):
GpuParticleSystem_Draw(camera, particleTexture);

// Cleanup:
GpuParticleSystem_Unload();
```

---

## 3. API

### `GpuParticleSystem_Init(void)`
Phát hiện compute capability, khởi tạo shader và buffer. Gọi một lần sau `InitWindow()`.

### `GpuParticleSystem_Spawn(GpuParticleConfig cfg)`
Spawn một particle. Hoạt động ở cả hai path. Ring-buffer — particle cũ bị overwrite khi pool đầy.

```c
typedef struct {
    Vector3 position;
    Vector3 velocity;
    Color   colorStart;
    Color   colorEnd;
    float   radius;
    float   lifetime;  // giây
    float   drag;      // 0.0 = không cản | 0.98 = cản nhẹ | 1.0 = dừng ngay
} GpuParticleConfig;
```

**Ví dụ spawn mưa:**
```c
GpuParticleConfig rain = {
    .position   = (Vector3){ x, 200.0f, z },
    .velocity   = (Vector3){ wind_x, -300.0f, wind_z },
    .colorStart = (Color){ 180, 200, 255, 200 },
    .colorEnd   = (Color){ 180, 200, 255, 0 },
    .radius     = 2.0f,
    .lifetime   = 1.5f,
    .drag       = 0.02f,
};
GpuParticleSystem_Spawn(rain);
```

### `GpuParticleSystem_Update(float dt)`
Update vật lý. COMPUTE path: dispatch compute shader. CPU/VBO path: CPU loop.

### `GpuParticleSystem_Draw(Camera3D camera, Texture2D texture)`
Vẽ billboard particles. Gọi trong 3D draw phase.

### `GpuParticleSystem_Unload(void)`
Giải phóng GPU buffer, shader. Gọi khi kết thúc.

### `GpuParticleSystem_IsComputeActive(void) → bool`
`true` nếu đang dùng GPU compute path.

### `GpuParticleSystem_ActiveCount(void) → int`
Số particle đang sống.

### `GpuParticleSystem_DrawDebug(int x, int y)`
Hiển thị debug overlay (path, GL version, particle count).

---

## 4. Giới hạn

```c
#define MAX_GPU_PARTICLES 8192  // Ring-buffer size
```

Mỗi particle chiếm 80 bytes trong SSBO. Pool fixed-size, không malloc.

---

## 5. Shader files

Tất cả shader nằm trong `compute/shaders/`:

| File | Dùng cho | Mô tả |
|---|---|---|
| `gpu_particles.comp` | COMPUTE path | Physics update: lifetime, drag, integrate |
| `gpu_particles_ssbo.vs` | COMPUTE path | Vertex: billboard từ SSBO + gl_VertexID |
| `gpu_particles.fs` | Cả hai path | Fragment: texture * color interpolate |
| `gpu_particles_vbo.vs` | CPU/VBO path | Vertex: nhận VBO đã được build trên CPU |

---

## 6. Android / GLES Rules

### Compute shader (`.comp`)
- Source giữ `#version 310 es` (GLES 3.1)
- `CompileComputeShader()` runtime-patch → `#version 430 core` trên desktop
- Build script nhận diện `layout(local_size_x` → target ES 3.1 (không cần thêm gì)

### SSBO vertex shader (`_ssbo.vs`)
- Trên desktop: `#version 430 core`, load qua `ResourceManager_LoadShader`
- Trên Android: build script chuyển `#version 430 core` → `#version 310 es` (nhận diện `layout(std430`)
- `ShaderPreprocessor` bỏ qua `#version 310 es` (chỉ xử lý `#version 330`) → GLES 3.1 nhận trực tiếp

### CPU/VBO shader (`_vbo.vs`, `.fs`)
- Desktop: `#version 330 core`
- Android: build script chuyển → `#version 100` (GLES 1.00 / ES 2.0)
- Không có tính năng SSBO → không cần GLES 3.1

### Precision rule (GLES 3.x strict)
Uniform xuất hiện ở cả VS lẫn FS phải cùng precision. Shader compute cần:
```glsl
precision highp float;
precision highp int;
```

---

## 7. Mở rộng (Rain, Fog, v.v.)

Để environment tạo mưa:
```c
#include "compute/gpu_particle_system.h"

// Trong Environment_Update(dt):
for (int i = 0; i < rain_spawn_count; i++) {
    GpuParticleSystem_Spawn((GpuParticleConfig){
        .position = randomRainPosition(),
        .velocity = (Vector3){ wind.x, -400.0f, wind.z },
        .colorStart = (Color){200, 220, 255, 180},
        .colorEnd   = (Color){200, 220, 255, 0},
        .radius   = 1.5f,
        .lifetime = 1.2f,
        .drag     = 0.01f,
    });
}
```

Không cần Init/Update/Draw ở environment — đã được main.c quản lý tập trung.
