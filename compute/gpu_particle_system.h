#ifndef GPU_PARTICLE_SYSTEM_H
#define GPU_PARTICLE_SYSTEM_H

/*
 * compute/gpu_particle_system — Hệ thống particle GPU dùng chung.
 *
 * Module độc lập, nằm ngoài core/. Cả skills lẫn environment đều có thể dùng.
 *
 * Hai path, tự detect runtime:
 *   COMPUTE path  (GL 4.3+ / GLES 3.1+, Android Mali-G68+)
 *                 — physics hoàn toàn trên GPU, dispatch compute shader
 *   CPU/VBO path  (GL 3.3, macOS / thiết bị cũ)
 *                 — physics trên CPU, upload VBO mỗi frame
 *
 * Tích hợp vào main.c:
 *   1. #include "compute/gpu_particle_system.h"
 *   2. GpuParticleSystem_Init()  — sau InitWindow()
 *   3. GpuParticleSystem_Update(dt) — trong game loop (Update)
 *   4. GpuParticleSystem_Draw(camera, tex) — trong game loop (Draw, sau 3D Begin)
 *   5. GpuParticleSystem_Unload() — lúc cleanup
 */

#include "raylib.h"
#include <stdbool.h>

#define MAX_GPU_PARTICLES 8192

typedef struct {
    Vector3 position;
    Vector3 velocity;
    Color   colorStart;
    Color   colorEnd;
    float   radius;
    float   lifetime;  // giây
    float   drag;      // 0.0 = không cản, 0.98 = cản nhẹ, 1.0 = dừng ngay
} GpuParticleConfig;

// Khởi tạo — detect compute capability, tạo buffer/shader
void GpuParticleSystem_Init(void);

// Spawn một particle (hoạt động ở cả hai path)
void GpuParticleSystem_Spawn(GpuParticleConfig cfg);

// Update vật lý — dispatch compute shader hoặc CPU loop
void GpuParticleSystem_Update(float dt);

// Vẽ tất cả particle dưới dạng camera-facing billboard
void GpuParticleSystem_Draw(Camera3D camera, Texture2D texture);

// Cleanup
void GpuParticleSystem_Unload(void);

// Query trạng thái
bool GpuParticleSystem_IsComputeActive(void);  // true = GPU compute, false = CPU/VBO
int  GpuParticleSystem_ActiveCount(void);

// Debug overlay — vẽ thẳng lên màn hình, gọi trong 2D draw phase
void GpuParticleSystem_DrawDebug(int x, int y);

#endif // GPU_PARTICLE_SYSTEM_H
