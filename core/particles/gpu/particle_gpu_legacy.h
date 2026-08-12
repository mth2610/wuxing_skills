#ifndef CORE_PARTICLES_GPU_PARTICLE_GPU_BACKEND_H
#define CORE_PARTICLES_GPU_PARTICLE_GPU_BACKEND_H

/*
 * core/particles/gpu/particle_gpu_backend — internal GPU particle backend.
 *
 * Private implementation behind core/particles/particle_manager.h.
 *
 * Hai path, tự detect runtime:
 *   COMPUTE path  (GL 4.3+ / GLES 3.1+, Android Mali-G68+)
 *                 — physics hoàn toàn trên GPU, dispatch compute shader
 *   CPU/VBO path  (GL 3.3, macOS / thiết bị cũ)
 *                 — physics trên CPU, upload VBO mỗi frame
 *
 * Tích hợp vào main.c:
 *   Legacy API only. New VFX code includes core/particles/particle_manager.h.
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
// core/particles/shaders/gpu/particle_gpu.comp.
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

    // Velocity-stretch rendering
    float   stretchStrength;
    float   stretchMinSpeed;

    // Ground collision
    bool    collisionEnabled;
    float   collisionElasticity;
    float   collisionFloorY;

    // Emissive intensity boost for HDR Bloom (1.0 = default, >1.0 = glowing core)
    float   emissiveBoost;
    /* Manager-owned routing metadata. Never authored by VFX code. */
    int     emitterId;
    int     renderMode;
} GpuParticleConfig;

// Khởi tạo — detect compute capability, tạo buffer/shader
void GpuParticleSystem_Init(void);

// Spawn một particle (hoạt động ở cả hai path)
void GpuParticleSystem_Spawn(GpuParticleConfig cfg);

// Update vật lý — dispatch compute shader hoặc CPU loop
void GpuParticleSystem_Update(float dt);

// Vẽ tất cả particle dưới dạng camera-facing billboard
void GpuParticleSystem_Draw(Camera3D camera, Texture2D texture);
/* Liquid-table slot written into the capture's B channel by the NEXT surface
 * draw. core/fluid/fluid_surface.c owns the policy; this is only the wire. */
void GpuParticleSystem_SetSurfaceMaterialId(float materialId);
void GpuParticleSystem_DrawSurfaceEmitter(Camera3D camera, Texture2D texture, int emitterId);
// Far side of the same splat cloud (dual-depth thickness); see fluid_capture_particle_back.fs.
void GpuParticleSystem_DrawSurfaceBackEmitter(Camera3D camera, int emitterId);

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

#endif // CORE_PARTICLES_GPU_PARTICLE_GPU_BACKEND_H
