#ifndef GPU_PARTICLE_SYSTEM_H
#define GPU_PARTICLE_SYSTEM_H

/*
 * gpu_particle_system — Extension độc lập, bổ sung cho particle_system.c
 *
 * Hai path, tự detect runtime:
 *   COMPUTE path  (GL 4.3+, Windows/Linux) — physics hoàn toàn trên GPU
 *   CPU/VBO path  (GL 3.3, macOS)          — physics trên CPU, upload VBO mỗi frame
 *
 * Điểm khác với particle_system.c gốc:
 *   • Pool lớn hơn: MAX_GPU_PARTICLES (8192 mặc định)
 *   • Không sub-emitter (giữ đơn giản để GPU-friendly)
 *   • Dùng riêng — không conflict với InitParticleSystem/SpawnParticle
 *
 * Tích hợp vào core:
 *   1. Thêm gpu_particle_system.c vào CMakeLists.txt
 *   2. Gọi GpuParticleSystem_Init() sau InitWindow()
 *   3. Gọi GpuParticleSystem_Update(dt) và GpuParticleSystem_Draw(camera, tex) mỗi frame
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
    float   lifetime;   // giây
    float   drag;       // 0.0 = không cản, 0.98 = cản nhẹ mỗi frame, 1.0 = dừng ngay
} GpuParticleConfig;

// Khởi tạo — detect compute capability, tạo buffer/shader
void GpuParticleSystem_Init(void);

// Spawn particle (hoạt động ở cả hai path)
void GpuParticleSystem_Spawn(GpuParticleConfig cfg);

// Update vật lý — dispatch compute hoặc CPU loop
void GpuParticleSystem_Update(float dt);

// Vẽ tất cả particle dưới dạng camera-facing billboard
void GpuParticleSystem_Draw(Camera3D camera, Texture2D texture);

// Cleanup
void GpuParticleSystem_Unload(void);

// Query
bool GpuParticleSystem_IsComputeActive(void);
int  GpuParticleSystem_ActiveCount(void);

// Debug overlay — vẽ thẳng lên màn hình, gọi trong Draw2D
// Hiển thị: path đang dùng (COMPUTE / CPU VBO), GL version, particle count
void GpuParticleSystem_DrawDebug(int x, int y);

#endif // GPU_PARTICLE_SYSTEM_H
