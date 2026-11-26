#ifndef VFX_MODEL_H
#define VFX_MODEL_H

#include "raylib.h"
#include <stdbool.h>

// Rig-agnostic animated 3D VFX model system (e.g. Asian Dragon for Giáng Long
// Chưởng, spirit summons, weapon arrays, qi avatars).
//
// Uses GPU vertex skinning (3x4 affine bone rows uploaded as a single vec4
// uniform array per instance) to avoid mid-frame UpdateMeshBuffer() render-pass
// splits on Vulkan/rlvk (ENGINE_LANDMINES §30) and to allow multiple concurrent
// instances of the same GLB without duplicating VBOs.

#define VFX_MODEL_MAX_ASSETS    16
#define VFX_MODEL_MAX_INSTANCES 32
#define VFX_MODEL_MAX_BONES     64

typedef enum {
    VFX_MODEL_STYLE_SOLID_LIT = 0, // Opaque stylized lit model (baked vertex colors / albedo)
    VFX_MODEL_STYLE_QI_SPIRIT = 1, // Translucent premultiplied qi spirit (core coverage + rim + qi veins + dissolve)
    VFX_MODEL_STYLE_SOLID_AURA = 2 // Opaque body + emissive qi veins + Fresnel rim + dissolve
} VFXModelStyle;

typedef struct VFXModelSpawnParams {
    Vector3       position;
    Vector3       rotationEuler; // radians (pitch X, yaw Y, roll Z)
    float         scale;         // <= 0 defaults to 1.0f
    const char   *clipName;      // NULL or "" plays clip 0 (or bind pose if static)
    bool          loop;
    float         animSpeed;     // <= 0 defaults to 1.0f
    float         lifetime;      // <= 0 = persistent until VFXModel_Kill()
    float         fadeInTime;    // dissolve-in duration in seconds (0 = instant)
    float         fadeOutTime;   // dissolve-out duration before lifetime end (0 = instant)
    VFXModelStyle style;
    Color         tint;          // body tint; alpha == 0 defaults to WHITE
    Color         glowColor;     // Fresnel/vein glow tint; alpha == 0 defaults to tint
    float         glowIntensity; // HDR emissive gain; <= 0 defaults to 1.8f
    float         opacity;       // base coverage for QI_SPIRIT (0..1); <= 0 defaults to 0.72f
} VFXModelSpawnParams;

// System lifecycle
void VFXModel_Init(void);
void VFXModel_Unload(void);
void VFXModel_Update(float dt);
void VFXModel_UpdateFrame(Camera3D camera);
void VFXModel_Draw(void);

// Asset registry (cached via ResourceManager; never unloaded manually)
int  VFXModel_RegisterAsset(const char *glbPath);
bool VFXModel_IsAssetValid(int assetHandle);
int  VFXModel_GetClipCount(int assetHandle);
int  VFXModel_GetBoneCount(int assetHandle);
int  VFXModel_FindClip(int assetHandle, const char *clipName);
int  VFXModel_FindBone(int assetHandle, const char *boneName);

// Instance management
int   VFXModel_Spawn(int assetHandle, const VFXModelSpawnParams *params);
void  VFXModel_Kill(int instanceHandle);
void  VFXModel_KillAll(void);
bool  VFXModel_IsAlive(int instanceHandle);

void  VFXModel_SetTransform(int instanceHandle, Vector3 position, Vector3 rotationEuler, float scale);
void  VFXModel_SetPoseMatrix(int instanceHandle, Matrix worldMatrix);
void  VFXModel_SetDissolve(int instanceHandle, float dissolve01); // < 0 restores auto fadeIn/fadeOut
void  VFXModel_SetSpineReveal(int instanceHandle, float revealStart01, float revealEnd01); // 0=head, 1=tail
void  VFXModel_SetColors(int instanceHandle, Color tint, Color glowColor, float glowIntensity);
bool  VFXModel_PlayClip(int instanceHandle, const char *clipName, bool loop, float speed, float crossFadeSec);
bool  VFXModel_PlayClipIndex(int instanceHandle, int clipIndex, bool loop, float speed, float crossFadeSec);
float VFXModel_GetNormalizedTime(int instanceHandle);

// Bone / socket queries in world space (for RibbonStrip trails, emitters, lights, hitboxes)
bool VFXModel_GetBoneWorldMatrix(int instanceHandle, int boneIndex, Matrix *outWorldMatrix);
bool VFXModel_GetBoneWorldPos(int instanceHandle, const char *boneName, Vector3 *outPos, Vector3 *outForward);

// Procedural spine-along-path override (follow-the-leader serpentine motion).
// Evaluates a caller-supplied 3D world-space curve at each spine bone's arc-length
// behind `headArcDist`, builds a twist-free Parallel-Transport (Bishop) frame with
// curvature banking, and propagates child bones (head, jaw, limbs) automatically.
// `curveFn(arcDist, userData)` returns a world-space point at distance `arcDist` (m).
typedef Vector3 (*VFXModelSpineCurveFn)(float arcDist, void *userData);

void VFXModel_SetSpinePathOverride(int instanceHandle,
                                   int spineBoneCount,
                                   float headArcDist,
                                   float bodyScale,
                                   float bankStrength,
                                   float jawOpenRadians,
                                   VFXModelSpineCurveFn curveFn,
                                   void *userData);
void VFXModel_ClearSpinePathOverride(int instanceHandle);

// Pure math helpers (also covered by unit tests)
void   VFXModel_PackAffineRows(Matrix m, float outRows12[12]);
Matrix VFXModel_TransformToMatrix(Transform t);
Matrix VFXModel_ComputeSkinMatrix(Transform bindPose, Transform animPose);

// Diagnostics
void VFXModel_GetStats(int *activeInstances, int *maxInstances, int *loadedAssets);

#endif // VFX_MODEL_H
