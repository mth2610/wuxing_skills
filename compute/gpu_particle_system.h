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
#include "core/force_field.h"
#include <stdbool.h>

#define MAX_GPU_PARTICLES 8192

// Số slot texture "vector field" đồng thời hỗ trợ cho FORCE_VECTOR_TEXTURE
// (xem core/force_field.h) — PHẢI khớp uVectorField0/uVectorField1 trong
// compute/shaders/gpu_particles.comp.
#define GPU_VECTOR_FIELD_SLOTS 2

typedef struct {
    Vector3 position;
    Vector3 velocity;
    Color   colorStart;
    Color   colorEnd;
    float   radius;
    float   lifetime;  // giây
    float   drag;      // 0.0 = không cản, 0.98 = cản nhẹ, 1.0 = dừng ngay

    // Force field áp dụng cho particle này (chỉ có hiệu lực ở COMPUTE path —
    // CPU/VBO fallback bỏ qua field này). NULL = không có force field.
    // Con trỏ được đăng ký vào registry nội bộ và re-pack MỖI FRAME, nên
    // ForceField phải sống ít nhất bằng đời particle dài nhất dùng nó
    // (dùng static/pool, không dùng biến local trên stack).
    const ForceField *forceField;

    // Trục động cho layer FORCE_RADIAL_AXIS/FORCE_VORTEX_AXIS bên trong
    // forceField (giống tham số axisOrigin/axisDir của ForceField_Evaluate).
    // Bỏ qua nếu forceField không chứa layer axis-type nào. Registry lưu trục
    // theo slot (keyed theo con trỏ forceField) và cập nhật lại mỗi lần một
    // particle mới đăng ký cùng con trỏ đó — tất cả particle spawn cùng
    // frame với cùng forceField sẽ dùng chung trục mới nhất.
    Vector3 axisOrigin;
    Vector3 axisDir;
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

// Gán texture "vector field" (kênh RG = hướng flow XZ remap [-1,1] -> [0,1],
// giống flow_map.h) vào slot (0..GPU_VECTOR_FIELD_SLOTS-1). Dùng chung slot
// index này trong ForceLayer.noiseScale khi tạo layer FORCE_VECTOR_TEXTURE.
// CHỈ có hiệu lực ở COMPUTE path. Truyền tex = {0} (id == 0) để tắt slot.
// Texture phải sống ít nhất bằng đời mọi ForceField dùng slot đó (static/pool,
// không load rồi Unload ngay trong frame) — không được sở hữu/free bởi module
// này.
void GpuParticleSystem_SetVectorFieldTexture(int slot, Texture2D tex);

// Query trạng thái
bool GpuParticleSystem_IsComputeActive(void);  // true = GPU compute, false = CPU/VBO
int  GpuParticleSystem_ActiveCount(void);

// Debug overlay — vẽ thẳng lên màn hình, gọi trong 2D draw phase
void GpuParticleSystem_DrawDebug(int x, int y);

#endif // GPU_PARTICLE_SYSTEM_H
