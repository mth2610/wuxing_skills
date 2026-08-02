#ifndef DECAL_SYSTEM_H
#define DECAL_SYSTEM_H

#include "raylib.h"
#include "core/geometry/procedural_mesh_utils.h"
#include <stdbool.h>

#define MAX_DECALS 64
#define DECAL_STAMP_SECTORS 16
#define DECAL_STAMP_RINGS 3

typedef unsigned int DecalHandle;
#define DECAL_HANDLE_INVALID ((DecalHandle)0u)

typedef bool (*GroundSurfaceSampleFn)(float x, float z, Vector3 *outPosition,
                                      Vector3 *outNormal, void *userData);

typedef struct {
    Color baseTint;
    Color emissiveTint;
    float emissiveThreshold;
    float emissiveIntensity;
    int priority; // 0..255; higher survives conformal-budget pressure first
    float maxDrawDistance; // <= 0 means unlimited
} DecalMaterialParams;

typedef struct {
    unsigned int generation;
    Vector3 position;
    float rotation;       // Góc quay ban đầu quanh trục Y (độ)
    float rotSpeed;       // Tốc độ xoay liên tục (độ/giây), 0 = tĩnh
    float scale;          // Scale hiện tại (nội suy từ scaleStart → scaleEnd)
    float scaleStart;     // Scale khi vừa spawn
    float scaleEnd;       // Scale khi hết lifetime (tạo hiệu ứng nở/thu)
    float yOffset;        // Y offset tránh Z-fighting, mặc định 0.02f
    Texture2D texture;
    float lifetime;       // Thời gian tồn tại còn lại (giây)
    float maxLifetime;
    float fadeInSeconds;  // 0 = visible immediately
    float fadeOutSeconds; // 0 = only material erosion controls the end
    Color tint;
    DecalMaterialParams material;
    BlendMode blendMode;  // BLEND_ALPHA | BLEND_ADDITIVE | BLEND_MULTIPLIED
    bool active;
    bool flowScroll;      // true: vẽ bằng decal_flow.fs, cuộn ra ngoài tâm theo thời gian
    float flowSpeed;      // tốc độ cuộn radial, thường 0.3-1.0
    float flowStrength;   // trộn flow vs texture gốc [0,1], thường 0.5-1.0
    float glowIntensity;  // boost HDR cho texel sáng (khe nứt) để bloom bắt được; 0 = tắt glow
    // An oriented decal is a true surface-aligned quad.  It is used for marks
    // on walls/ceilings where the legacy ground projection is invalid.
    bool oriented;
    Vector3 surfaceNormal;
    Vector3 surfaceTangent;
    bool conformalStamp;  // subdivided terrain-following material stamp (P4)
    GroundHeightSampleFn heightFn;
    void *heightUserData;
    float edgePhase;
    bool stampHeightsCached;
    float stampHeights[DECAL_STAMP_RINGS + 1][DECAL_STAMP_SECTORS];
    bool stampSurfaceCached;
    Vector3 stampSurfacePositions[DECAL_STAMP_RINGS + 1][DECAL_STAMP_SECTORS];
    Vector3 stampSurfaceNormals[DECAL_STAMP_RINGS + 1][DECAL_STAMP_SECTORS];
} DecalEntity;

// Snapshot of the most recently built render queue. `culled` counts drawable
// decals rejected by CPU camera admission; it is zero until the first Draw.
typedef struct {
    int active;
    int visible;
    int culled;
    int frustumCulled;
    int lodCulled;
    int emissiveSuppressed;
    int distanceCulled;
    float baseCoverage;
    float emissiveCoverage;
} DecalRenderStats;

// Khởi tạo hệ thống Decal
void DecalSystem_Init(void);

// API cơ bản — BLEND_ALPHA, scale tĩnh, không xoay, yOffset = 0.02f
void DecalSystem_Add(Vector3 pos, float rotation, float scale,
                     Texture2D texture, float lifetime, Color tint);

// API đầy đủ — kiểm soát blend mode, scale animate, rotation liên tục
void DecalSystem_AddEx(Vector3 pos, float rotation, float rotSpeed,
                       float scaleStart, float scaleEnd,
                       Texture2D texture, float lifetime,
                       Color tint, BlendMode blendMode, float yOffset);

// Như AddEx nhưng texture cuộn radial ra ngoài tâm theo thời gian (lava
// crawl, ripple spreading) thay vì đứng yên — dùng decal_flow.fs riêng,
// không ảnh hưởng decal tĩnh (AddEx/Add). flowSpeed/flowStrength xem comment
// DecalEntity. glowIntensity: 0 = không glow (mặc định cho mọi preset khác
// FIRE_LAVA); >0 = boost HDR vùng sáng nhất trong texture (khe nứt) để
// pipeline bloom hiện có (PostFX_Draw) tự động phát sáng đúng chỗ đó — không
// phải lúc nào flow decal cũng cần glow (vd. WATER_RIPPLE không cần).
void DecalSystem_AddFlowEx(Vector3 pos, float rotation, float rotSpeed,
                           float scaleStart, float scaleEnd,
                           Texture2D texture, float lifetime,
                           Color tint, BlendMode blendMode, float yOffset,
                           float flowSpeed, float flowStrength,
                           float glowIntensity);

// Surface-aligned counterpart to AddEx. `normal` is normalized internally;
// `rotation` is a roll in degrees around it. `yOffset` is applied along the
// normal, never along world Y. This is a one-shot event API, not a projector.
void DecalSystem_AddOrientedEx(Vector3 pos, Vector3 normal, float rotation,
                               float rotSpeed, float scaleStart, float scaleEnd,
                               Texture2D texture, float lifetime, Color tint,
                               BlendMode blendMode, float yOffset);

// P4 material stamp: a subdivided disc follows the supplied ground-height
// receiver. It never emits a camera-compensated quad, so its silhouette cannot
// expose a square texture boundary. `fadeInSeconds`/`fadeOutSeconds` are
// applied to the actual draw alpha. `maxSlopeDegrees` rejects steep receivers
// from a spawn-time height probe; `heightFn` may be NULL for a flat receiver.
void DecalSystem_AddConformalEx(Vector3 pos, float rotation, float rotSpeed,
                                float scaleStart, float scaleEnd,
                                Texture2D texture, float lifetime, Color tint,
                                BlendMode blendMode, float yOffset,
                                GroundHeightSampleFn heightFn, void *heightUserData,
                                GroundSurfaceSampleFn surfaceFn,
                                float edgePhase, float fadeInSeconds,
                                float fadeOutSeconds, float maxSlopeDegrees);

// Atomic material-stamp spawn. Prefer this over AddConformalEx whenever the
// decal uses the material shader; the material cannot race with another spawn.
DecalHandle DecalSystem_AddConformalMaterialEx(Vector3 pos, float rotation, float rotSpeed,
                                                float scaleStart, float scaleEnd,
                                                Texture2D texture, float lifetime, Color tint,
                                                BlendMode blendMode, float yOffset,
                                                GroundHeightSampleFn heightFn, void *heightUserData,
                                                GroundSurfaceSampleFn surfaceFn,
                                                float edgePhase, float fadeInSeconds,
                                                float fadeOutSeconds, float maxSlopeDegrees,
                                                const DecalMaterialParams *material);

bool DecalSystem_Destroy(DecalHandle handle);
bool DecalSystem_IsAlive(DecalHandle handle);
// Conformal stamps cache their receiver at spawn; moving one is unsupported.
bool DecalSystem_SetTransform(DecalHandle handle, Vector3 position,
                              Vector3 normal, float rotationRadians);

// Batch helper — đặt 1 decal tại mỗi điểm trong points[0..count-1], dùng cho hiệu ứng
// theo đường đi (vd. gai mọc dọc đường, vệt cháy). Wrap quanh DecalSystem_Add, không
// dup logic. Caller chịu trách nhiệm truyền count hợp lý — không tự clamp theo MAX_DECALS
// (giống convention của SamplePath trong path_spline.h dùng maxSegments do caller kiểm soát).
void DecalSystem_AddStreak(const Vector3 *points, int count, float rotation,
                           float scale, Texture2D texture, float lifetime, Color tint);

// Cập nhật thời gian sống, scale nội suy, rotation
void DecalSystem_Update(float dt);

// Cập nhật thông tin camera để bù foreshortening — gọi 1 lần/frame trước DecalSystem_Draw
// Không gọi → decal bị ellipse vì camera nhìn từ góc nghiêng
void DecalSystem_SetCamera(Camera3D camera);

// Vẽ toàn bộ Decal, nhóm theo blendMode để giảm state switch
void DecalSystem_Draw(void);

// Giải phóng hệ thống
void DecalSystem_Unload(void);
void DecalSystem_GetStats(int *active, int *max); // Item 32
void DecalSystem_GetRenderStats(DecalRenderStats *outStats);

#endif // DECAL_SYSTEM_H
