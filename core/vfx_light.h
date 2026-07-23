#ifndef VFX_LIGHT_H
#define VFX_LIGHT_H

#include "raylib.h"
#include <stdbool.h>

#define MAX_VFX_LIGHTS 16

// Cấu trúc phẳng truyền trực tiếp sang Shader qua Uniform Array
typedef struct {
    Vector3 position;
    float radius;
    Color color;
} VFXLightData;

// Priority used for pool eviction when MAX_VFX_LIGHTS is full (CORE_ISSUES.md
// Item 12). VFX_PRIORITY_HIGH_ULTIMATE should win over VFX_PRIORITY_LOW when
// the pool is saturated.
typedef enum {
    VFX_PRIORITY_LOW = 0,
    VFX_PRIORITY_HIGH_ULTIMATE
} VFXPriority;

// Khởi tạo/Xóa hệ thống quản lý Light
void VFXLight_Init(void);
void VFXLight_Reset(void);

// Spawn một point light động (Hiệu ứng skill, vfx nổ, tia chớp...). When the
// pool is full: scans for the lowest-priority slot (ties broken by shortest
// remaining lifetime) and evicts it instead of rejecting the new spawn. A
// same-or-lower priority incoming spawn can still fail to find a slot only if
// every existing slot has strictly higher priority — in that case the spawn
// is silently dropped, same as the old full-pool behavior.
void VFXLight_Spawn(Vector3 pos, Color color, float radius, float lifetime,
                    VFXPriority priority);

// Cập nhật lifetime của các light theo delta time
void VFXLight_Update(float dt);

// Lấy danh sách các light đang hoạt động để nạp vào Uniform Array của Shader
void VFXLight_GetActive(VFXLightData *out, int *count, int maxCount);
void VFXLight_GetStats(int *active, int *max); // Item 32

// ── Đợt E / E2 — bind the pool to any lit shader ─────────────────────────────
//
// Uploads the active lights to `shader`'s u_vfxLight* uniforms. Pair it with
//     #include "core/shaders/common/vfx_lights.glsl"
//     color += VFXLights_Accumulate(worldPos, N, albedo);
// in the fragment shader. Uniform locations are looked up once per shader id and
// cached, so this is cheap to call every frame.
//
// `maxLights` is the tier budget (HIGH 4 / MED 2 / LOW 0) — 4 lights x 3 uniform
// arrays is a real uniform-block cost on Mali. Passing 0 uploads a count of 0
// and the shader's loop exits immediately, so a shader can include the block
// unconditionally and still pay nothing on low tiers.
//
// Adopting this in a shader that is NOT lit by the same convention will make it
// disagree with the smoke and characters around it about where the light is.
// The falloff and half-Lambert wrap in the .glsl are the shared contract.
void VFXLight_BindToShader(Shader shader, int maxLights);

// ── The general path: register once, forget ──────────────────────────────────
//
// Calling VFXLight_BindToShader by hand from every surface's draw function does
// not scale — every new ground/prop shader means finding its per-frame hook and
// editing it, and the one that gets missed is invisible until someone notices
// an effect not lighting that particular surface. (Exactly how the paved path
// stayed dark while the grass worked.)
//
// Instead: register the shader once when it is loaded, and main.c binds them all
// in one place each frame. Adopting a new shader then costs TWO lines in the
// shader (#include + one call) and ONE at load — no per-frame code at all.
//
// Registration is idempotent; registering a shader that lacks the uniforms logs
// once and is then skipped, so a shader can be registered speculatively.
void VFXLight_RegisterShader(Shader shader);

// Binds the pool to every registered shader. Call once per frame, inside the 3D
// pass, before anything is drawn. `maxLights` is the tier budget.
void VFXLight_BindAll(int maxLights);

// Draw every active light as a wire sphere at its world position, sized to its
// radius. Call inside the 3D pass. Gated by tuning.cfg → vfx_light_debug.
//
// This exists because "the light is in the wrong place" and "the SURFACE thinks
// it is in the wrong place" produce the identical symptom — nothing lit — and no
// amount of logging the pool distinguishes them. Seeing the sphere land where
// the effect is, and seeing whether the ground under it brightens, separates the
// two in one glance.
void VFXLight_DrawDebug(void);

// Parks a controllable test light at `pos` (raised 1 m), re-spawned every frame.
// Gated by tuning.cfg → vfx_light_test (0/1), sized by vfx_light_test_radius.
// Call once per frame, before VFXLight_BindAll. A skill's own light is typically
// under 2 m wide and lives under half a second, which is far too small to answer
// "is this surface lit at all" — this one is not.
void VFXLight_DebugTestLight(Vector3 pos);

#endif // VFX_LIGHT_H