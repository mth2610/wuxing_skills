#include "core/vfx_model/vfx_model.h"
#include "core/resource_manager.h"
#include "environment/environment_system.h"
#include "raymath.h"
#include "rlgl.h"
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VFX_MODEL_PATH_MAX 128
#define VFX_MODEL_CLIP_FPS 60.0f

typedef struct VFXModelAsset {
    bool            used;
    char            path[VFX_MODEL_PATH_MAX];
    Model           model;
    ModelAnimation *anims;
    int             animCount;
    int             boneCount;
    int             jawBoneIndex;
    float           spineMinX;
    float           spineMaxX;
    int             boneParents[VFX_MODEL_MAX_BONES];
    float           bindArcDist[VFX_MODEL_MAX_BONES];
    Matrix          invBind[VFX_MODEL_MAX_BONES];
    Matrix          localBind[VFX_MODEL_MAX_BONES];
} VFXModelAsset;

typedef struct VFXModelInstance {
    bool          active;
    int           assetHandle;
    Matrix        worldMatrix;
    Vector3       position;
    Vector3       rotationEuler;
    float         scale;
    bool          useExplicitMatrix;

    int           clipIndex;
    float         clipFrame;
    float         animSpeed;
    bool          loop;

    int           prevClipIndex;
    float         prevClipFrame;
    float         blendTimer;
    float         blendDuration;

    float         age;
    float         lifetime;
    float         fadeInTime;
    float         fadeOutTime;
    float         manualDissolve; // < 0 means automatic fadeIn/fadeOut
    float         revealStart;    // 0.0 = head
    float         revealEnd;      // 1.0 = tail

    VFXModelStyle style;
    Color         tint;
    Color         glowColor;
    float         glowIntensity;
    float         opacity;

    bool                 spinePathActive;
    int                  spineBoneCount;
    float                spineHeadArcDist;
    float                spineBodyScale;
    float                spineBankStrength;
    float                spineJawOpenRad;
    VFXModelSpineCurveFn spineCurveFn;
    void                *spineUserData;

    int           posedBoneCount;
    Transform     posedBones[VFX_MODEL_MAX_BONES];
    Matrix        posedMatrices[VFX_MODEL_MAX_BONES];
    bool          hasPosedMatrices;
    float         boneRows[VFX_MODEL_MAX_BONES * 12];
} VFXModelInstance;

static VFXModelAsset    s_assets[VFX_MODEL_MAX_ASSETS];
static VFXModelInstance s_instances[VFX_MODEL_MAX_INSTANCES];
static int              s_assetCount = 0;

static Shader s_shader;
static bool   s_shaderReady = false;
static float  s_elapsedTime = 0.0f;

static int s_locWorldFromShaderSpace = -1;
static int s_locSkinned              = -1;
static int s_locBoneRows             = -1;
static int s_locTime                 = -1;
static int s_locStyle                = -1;
static int s_locDissolve             = -1;
static int s_locSpineBounds          = -1;
static int s_locRevealRange          = -1;
static int s_locOpacity              = -1;
static int s_locGlowColor            = -1;
static int s_locGlowIntensity        = -1;
static int s_locSunToLight           = -1;
static int s_locSunColor             = -1;
static int s_locSkyColor             = -1;
static int s_locGroundColor          = -1;
static int s_locViewPos              = -1;

// Cached per-frame camera/environment uniforms uploaded inside BeginShaderMode
static Matrix  s_frameWorldFromShaderSpace;
static Vector3 s_frameSunToLight = { 0.45f, 0.78f, 0.43f };
static Vector3 s_frameSunColor   = { 1.0f, 0.96f, 0.90f };
static Vector3 s_frameSkyColor   = { 0.35f, 0.45f, 0.65f };
static Vector3 s_frameGroundColor = { 0.16f, 0.18f, 0.22f };
static Vector3 s_frameViewPos    = { 0.0f, 5.0f, 10.0f };
static bool    s_frameUpdated    = false;

static inline Vector3 ColorToLinearVec3(Color c) {
    return (Vector3){ c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
}

static bool ContainsCaseInsensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

static bool EqualsCaseInsensitive(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static Matrix BuildTRS(Vector3 pos, Vector3 euler, float scale) {
    Matrix s  = MatrixScale(scale, scale, scale);
    Matrix rz = MatrixRotateZ(euler.z);
    Matrix rx = MatrixRotateX(euler.x);
    Matrix ry = MatrixRotateY(euler.y);
    Matrix r  = MatrixMultiply(MatrixMultiply(rz, rx), ry);
    Matrix t  = MatrixTranslate(pos.x, pos.y, pos.z);
    return MatrixMultiply(MatrixMultiply(s, r), t);
}

static Transform BlendTransform(Transform a, Transform b, float alpha) {
    Transform out;
    out.translation = Vector3Lerp(a.translation, b.translation, alpha);
    out.rotation    = QuaternionNlerp(a.rotation, b.rotation, alpha);
    out.scale       = Vector3Lerp(a.scale, b.scale, alpha);
    return out;
}

void VFXModel_PackAffineRows(Matrix m, float outRows12[12]) {
    if (!outRows12) return;
    // Raylib Matrix columns are (m0..m3), (m4..m7), (m8..m11), (m12..m15).
    // Vector3Transform(v, M) computes dot(rowK, vec4(v, 1)).
    outRows12[0]  = m.m0;  outRows12[1]  = m.m4;  outRows12[2]  = m.m8;  outRows12[3]  = m.m12;
    outRows12[4]  = m.m1;  outRows12[5]  = m.m5;  outRows12[6]  = m.m9;  outRows12[7]  = m.m13;
    outRows12[8]  = m.m2;  outRows12[9]  = m.m6;  outRows12[10] = m.m10; outRows12[11] = m.m14;
}

Matrix VFXModel_TransformToMatrix(Transform t) {
    Matrix s = MatrixScale(t.scale.x, t.scale.y, t.scale.z);
    Matrix r = QuaternionToMatrix(t.rotation);
    Matrix tr = MatrixTranslate(t.translation.x, t.translation.y, t.translation.z);
    return MatrixMultiply(MatrixMultiply(s, r), tr);
}

Matrix VFXModel_ComputeSkinMatrix(Transform bindPose, Transform animPose) {
    Matrix bindMat = VFXModel_TransformToMatrix(bindPose);
    Matrix invBind = MatrixInvert(bindMat);
    Matrix poseMat = VFXModel_TransformToMatrix(animPose);
    return MatrixMultiply(invBind, poseMat);
}

static Transform SampleClipBone(const ModelAnimation *anim, float frame, int boneIndex, Transform fallback) {
    if (!anim || anim->keyframeCount <= 0 || !anim->keyframePoses || boneIndex < 0 || boneIndex >= anim->boneCount) {
        return fallback;
    }
    int maxFrame = anim->keyframeCount - 1;
    if (maxFrame <= 0) {
        return anim->keyframePoses[0][boneIndex];
    }
    float clamped = frame;
    if (clamped < 0.0f) clamped = 0.0f;
    if (clamped > (float)maxFrame) clamped = (float)maxFrame;
    int f0 = (int)floorf(clamped);
    int f1 = f0 + 1;
    if (f1 > maxFrame) f1 = maxFrame;
    float frac = clamped - (float)f0;
    if (frac <= 0.0001f || f0 == f1) {
        return anim->keyframePoses[f0][boneIndex];
    }
    return BlendTransform(anim->keyframePoses[f0][boneIndex], anim->keyframePoses[f1][boneIndex], frac);
}

static Matrix BasisMatrix(Vector3 X, Vector3 Y, Vector3 Z, Vector3 P, float scale) {
    Matrix m = MatrixIdentity();
    m.m0 = X.x * scale; m.m4 = Y.x * scale; m.m8  = Z.x * scale; m.m12 = P.x;
    m.m1 = X.y * scale; m.m5 = Y.y * scale; m.m9  = Z.y * scale; m.m13 = P.y;
    m.m2 = X.z * scale; m.m6 = Y.z * scale; m.m10 = Z.z * scale; m.m14 = P.z;
    return m;
}

static void EvaluateSpinePathBones(VFXModelInstance *inst, const VFXModelAsset *asset) {
    int nb = asset->boneCount;
    if (nb > VFX_MODEL_MAX_BONES) nb = VFX_MODEL_MAX_BONES;
    int ns = inst->spineBoneCount;
    if (ns < 2) ns = 2;
    if (ns > nb) ns = nb;

    float sc = (inst->spineBodyScale > 0.01f) ? inst->spineBodyScale : 1.0f;
    Vector3 pts[VFX_MODEL_MAX_BONES];
    Vector3 tangents[VFX_MODEL_MAX_BONES];

    for (int i = 0; i < ns; i++) {
        float s = inst->spineHeadArcDist - asset->bindArcDist[i] * sc;
        pts[i] = inst->spineCurveFn(s, inst->spineUserData);
    }

    // Tangent points head -> tail (+X in Dragon V12 bind pose)
    for (int i = 0; i < ns; i++) {
        Vector3 d;
        if (i == 0)           d = Vector3Subtract(pts[1], pts[0]);
        else if (i == ns - 1) d = Vector3Subtract(pts[ns - 1], pts[ns - 2]);
        else                  d = Vector3Subtract(pts[i + 1], pts[i - 1]);
        float l = Vector3Length(d);
        tangents[i] = (l > 1e-5f) ? Vector3Scale(d, 1.0f / l) : (Vector3){ 1.0f, 0.0f, 0.0f };
    }

    // Parallel-Transport (Bishop) frame with controlled uprightness + curvature banking
    Vector3 upRef = { 0.0f, 1.0f, 0.0f };
    Vector3 T0 = tangents[0];
    Vector3 U = Vector3Subtract(upRef, Vector3Scale(T0, Vector3DotProduct(upRef, T0)));
    if (Vector3Length(U) < 0.05f) U = (Vector3){ 0.0f, 0.0f, 1.0f };
    U = Vector3Normalize(U);

    for (int i = 0; i < ns; i++) {
        Vector3 T = tangents[i];
        if (i > 0) {
            // Transport previous U onto plane perpendicular to T
            U = Vector3Subtract(U, Vector3Scale(T, Vector3DotProduct(U, T)));
            if (Vector3Length(U) < 0.05f) {
                U = Vector3Subtract(upRef, Vector3Scale(T, Vector3DotProduct(upRef, T)));
            }
            U = Vector3Normalize(U);
            // Blend gently toward world-up so the dragon stays regal and never flips upside-down
            Vector3 worldU = Vector3Subtract(upRef, Vector3Scale(T, Vector3DotProduct(upRef, T)));
            if (Vector3Length(worldU) > 0.1f) {
                worldU = Vector3Normalize(worldU);
                U = Vector3Normalize(Vector3Lerp(U, worldU, 0.42f));
            }
        }
        Vector3 R = Vector3Normalize(Vector3CrossProduct(T, U));
        U = Vector3Normalize(Vector3CrossProduct(R, T));

        // Bank into lateral turns based on tangent change
        float bank = 0.0f;
        if (i > 0 && i < ns - 1 && fabsf(inst->spineBankStrength) > 1e-4f) {
            Vector3 dT = Vector3Subtract(tangents[i + 1], tangents[i - 1]);
            float latCurv = Vector3DotProduct(dT, R);
            bank = Clamp(latCurv * inst->spineBankStrength, -0.55f, 0.55f);
        }
        Vector3 Ub = U;
        Vector3 Rb = R;
        if (fabsf(bank) > 1e-4f) {
            float cb = cosf(bank), sb = sinf(bank);
            Ub = Vector3Add(Vector3Scale(U, cb), Vector3Scale(R, sb));
            Rb = Vector3Add(Vector3Scale(R, cb), Vector3Scale(U, -sb));
        }

        inst->posedMatrices[i] = BasisMatrix(T, Ub, Rb, pts[i], sc);
    }

    // Propagate child bones (head, jaw, limbs) in parent order
    for (int b = ns; b < nb; b++) {
        int p = asset->boneParents[b];
        Matrix parentMat = (p >= 0 && p < b) ? inst->posedMatrices[p] : inst->posedMatrices[0];
        Matrix local = asset->localBind[b];
        if (b == asset->jawBoneIndex && fabsf(inst->spineJawOpenRad) > 1e-4f) {
            local = MatrixMultiply(MatrixRotateX(inst->spineJawOpenRad), local);
        }
        inst->posedMatrices[b] = MatrixMultiply(local, parentMat);
    }

    // World matrix is identity when spine path outputs world-space coordinates directly
    inst->worldMatrix = MatrixIdentity();
    inst->useExplicitMatrix = true;
    inst->hasPosedMatrices = true;
    inst->posedBoneCount = nb;

    for (int b = 0; b < nb; b++) {
        Matrix skinMat = MatrixMultiply(asset->invBind[b], inst->posedMatrices[b]);
        VFXModel_PackAffineRows(skinMat, &inst->boneRows[b * 12]);
    }
}

static void EvaluateInstanceBones(VFXModelInstance *inst) {
    if (!inst || !inst->active) return;
    if (inst->assetHandle < 0 || inst->assetHandle >= VFX_MODEL_MAX_ASSETS) return;
    const VFXModelAsset *asset = &s_assets[inst->assetHandle];
    if (!asset->used) return;

    if (inst->spinePathActive && inst->spineCurveFn && asset->boneCount > 0) {
        EvaluateSpinePathBones(inst, asset);
        return;
    }

    int nb = asset->boneCount;
    if (nb > VFX_MODEL_MAX_BONES) nb = VFX_MODEL_MAX_BONES;
    inst->posedBoneCount = nb;
    inst->hasPosedMatrices = false;
    if (nb <= 0) return;

    const ModelAnimation *curAnim =
        (asset->anims && inst->clipIndex >= 0 && inst->clipIndex < asset->animCount)
            ? &asset->anims[inst->clipIndex]
            : NULL;

    bool blending = (inst->blendDuration > 0.0001f &&
                     inst->blendTimer < inst->blendDuration &&
                     asset->anims &&
                     inst->prevClipIndex >= 0 &&
                     inst->prevClipIndex < asset->animCount);
    const ModelAnimation *prevAnim = blending ? &asset->anims[inst->prevClipIndex] : NULL;
    float blendAlpha = blending ? Clamp(inst->blendTimer / inst->blendDuration, 0.0f, 1.0f) : 1.0f;

    for (int b = 0; b < nb; b++) {
        Transform bindT = asset->model.skeleton.bindPose
            ? asset->model.skeleton.bindPose[b]
            : (Transform){ { 0.0f, 0.0f, 0.0f }, QuaternionIdentity(), { 1.0f, 1.0f, 1.0f } };
        Transform poseT = curAnim ? SampleClipBone(curAnim, inst->clipFrame, b, bindT) : bindT;
        if (blending && prevAnim) {
            Transform prevT = SampleClipBone(prevAnim, inst->prevClipFrame, b, bindT);
            poseT = BlendTransform(prevT, poseT, blendAlpha);
        }
        inst->posedBones[b] = poseT;
        Matrix poseMat = VFXModel_TransformToMatrix(poseT);
        Matrix skinMat = MatrixMultiply(asset->invBind[b], poseMat);
        VFXModel_PackAffineRows(skinMat, &inst->boneRows[b * 12]);
    }
}

static float ComputeEffectiveDissolve(const VFXModelInstance *inst) {
    if (!inst) return 1.0f;
    if (inst->manualDissolve >= 0.0f) {
        return Clamp(inst->manualDissolve, 0.0f, 1.0f);
    }
    float d = 0.0f;
    if (inst->fadeInTime > 0.0001f && inst->age < inst->fadeInTime) {
        float t = Clamp(inst->age / inst->fadeInTime, 0.0f, 1.0f);
        d = fmaxf(d, 1.0f - t);
    }
    if (inst->lifetime > 0.0f && inst->fadeOutTime > 0.0001f) {
        float rem = inst->lifetime - inst->age;
        if (rem < inst->fadeOutTime) {
            float t = Clamp(rem / inst->fadeOutTime, 0.0f, 1.0f);
            d = fmaxf(d, 1.0f - t);
        }
    }
    return Clamp(d, 0.0f, 1.0f);
}

void VFXModel_Init(void) {
    memset(s_assets, 0, sizeof(s_assets));
    memset(s_instances, 0, sizeof(s_instances));
    s_assetCount = 0;
    s_elapsedTime = 0.0f;
    s_frameWorldFromShaderSpace = MatrixIdentity();
    s_frameUpdated = false;

    s_shader = ResourceManager_LoadShader("core/vfx_model/shaders/vfx_model.vs",
                                          "core/vfx_model/shaders/vfx_model.fs");
    s_shaderReady = (s_shader.id > 0);
    if (!s_shaderReady) return;

    s_shader.locs[SHADER_LOC_MATRIX_MODEL]  = GetShaderLocation(s_shader, "matModel");
    s_shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(s_shader, "matNormal");
    s_shader.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(s_shader, "colDiffuse");

    s_locWorldFromShaderSpace = GetShaderLocation(s_shader, "u_worldFromShaderSpace");
    s_locSkinned              = GetShaderLocation(s_shader, "u_skinned");
    // ENGINE_LANDMINES §25: query "[0]" first (desktop GL), fall back to base name (rlvk)
    s_locBoneRows = GetShaderLocation(s_shader, "u_boneRows[0]");
    if (s_locBoneRows < 0) {
        s_locBoneRows = GetShaderLocation(s_shader, "u_boneRows");
    }

    s_locTime          = GetShaderLocation(s_shader, "u_time");
    s_locStyle         = GetShaderLocation(s_shader, "u_style");
    s_locDissolve      = GetShaderLocation(s_shader, "u_dissolve");
    s_locSpineBounds   = GetShaderLocation(s_shader, "u_spineBounds");
    s_locRevealRange   = GetShaderLocation(s_shader, "u_revealRange");
    s_locOpacity       = GetShaderLocation(s_shader, "u_opacity");
    s_locGlowColor     = GetShaderLocation(s_shader, "u_glowColor");
    s_locGlowIntensity = GetShaderLocation(s_shader, "u_glowIntensity");
    s_locSunToLight    = GetShaderLocation(s_shader, "u_sunToLight");
    s_locSunColor      = GetShaderLocation(s_shader, "u_sunColor");
    s_locSkyColor      = GetShaderLocation(s_shader, "u_skyColor");
    s_locGroundColor   = GetShaderLocation(s_shader, "u_groundColor");
    s_locViewPos       = GetShaderLocation(s_shader, "u_viewPos");
}

void VFXModel_Unload(void) {
    // Models, animations, and shader are owned by ResourceManager — never unload directly.
    memset(s_assets, 0, sizeof(s_assets));
    memset(s_instances, 0, sizeof(s_instances));
    s_assetCount = 0;
    s_shaderReady = false;
}

int VFXModel_RegisterAsset(const char *glbPath) {
    if (!glbPath || !glbPath[0]) return -1;

    for (int i = 0; i < VFX_MODEL_MAX_ASSETS; i++) {
        if (s_assets[i].used && strcmp(s_assets[i].path, glbPath) == 0) {
            return i;
        }
    }

    int slot = -1;
    for (int i = 0; i < VFX_MODEL_MAX_ASSETS; i++) {
        if (!s_assets[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    Model model = ResourceManager_LoadModel(glbPath);
    if (model.meshCount <= 0) return -1;

    int animCount = 0;
    ModelAnimation *anims = ResourceManager_LoadModelAnimations(glbPath, &animCount);

    VFXModelAsset *a = &s_assets[slot];
    memset(a, 0, sizeof(*a));
    a->used = true;
    snprintf(a->path, sizeof(a->path), "%s", glbPath);
    a->model = model;
    a->anims = anims;
    a->animCount = animCount;
    a->boneCount = model.skeleton.boneCount;
    if (a->boneCount > VFX_MODEL_MAX_BONES) {
        TraceLog(LOG_WARNING, "VFXModel: '%s' has %d bones (> %d max); clamping",
                 glbPath, a->boneCount, VFX_MODEL_MAX_BONES);
        a->boneCount = VFX_MODEL_MAX_BONES;
    }

    a->jawBoneIndex = -1;
    a->spineMinX = -4.5f;
    a->spineMaxX = 5.5f;
    if (model.meshCount > 0 && model.meshes[0].vertices && model.meshes[0].vertexCount > 0) {
        float minX = 1e9f, maxX = -1e9f;
        for (int m = 0; m < model.meshCount; m++) {
            const Mesh *mesh = &model.meshes[m];
            if (!mesh->vertices) continue;
            for (int v = 0; v < mesh->vertexCount; v++) {
                float vx = mesh->vertices[v * 3];
                if (vx < minX) minX = vx;
                if (vx > maxX) maxX = vx;
            }
        }
        if (maxX > minX + 1e-3f) {
            a->spineMinX = minX;
            a->spineMaxX = maxX;
        }
    }
    Matrix bindMats[VFX_MODEL_MAX_BONES];
    for (int b = 0; b < a->boneCount; b++) {
        a->boneParents[b] = model.skeleton.bones ? model.skeleton.bones[b].parent : -1;
        if (model.skeleton.bones && EqualsCaseInsensitive(model.skeleton.bones[b].name, "jaw")) {
            a->jawBoneIndex = b;
        }
        if (model.skeleton.bindPose) {
            bindMats[b] = VFXModel_TransformToMatrix(model.skeleton.bindPose[b]);
            a->invBind[b] = MatrixInvert(bindMats[b]);
        } else {
            bindMats[b] = MatrixIdentity();
            a->invBind[b] = MatrixIdentity();
        }
    }
    for (int b = 0; b < a->boneCount; b++) {
        int p = a->boneParents[b];
        if (p >= 0 && p < a->boneCount) {
            a->localBind[b] = MatrixMultiply(bindMats[b], a->invBind[p]);
        } else {
            a->localBind[b] = bindMats[b];
        }
        if (b == 0 || !model.skeleton.bindPose) {
            a->bindArcDist[b] = 0.0f;
        } else {
            float seg = Vector3Distance(model.skeleton.bindPose[b].translation,
                                        model.skeleton.bindPose[b - 1].translation);
            a->bindArcDist[b] = a->bindArcDist[b - 1] + seg;
        }
    }

    // Raylib's default UploadMesh() skips boneIds/boneWeights VBOs unless GPU
    // skinning is forced at build time. Upload them once onto each mesh's VAO
    // (attributes 7 & 8) so rlvk binds RLVK_VLAYOUT_MESH_BONES with zero
    // per-frame UpdateMeshBuffer() stalls (ENGINE_LANDMINES §30).
    if (a->boneCount > 0) {
        for (int m = 0; m < model.meshCount; m++) {
            Mesh *mesh = &a->model.meshes[m];
            if (!mesh->boneIndices || !mesh->boneWeights || mesh->vertexCount <= 0 || mesh->vaoId == 0) {
                continue;
            }
            if (rlEnableVertexArray(mesh->vaoId)) {
                unsigned int idVbo = rlLoadVertexBuffer(
                    mesh->boneIndices, mesh->vertexCount * 4 * (int)sizeof(unsigned char), false);
                rlSetVertexAttribute(7, 4, RL_UNSIGNED_BYTE, false, 0, 0);
                rlEnableVertexAttribute(7);

                unsigned int wtVbo = rlLoadVertexBuffer(
                    mesh->boneWeights, mesh->vertexCount * 4 * (int)sizeof(float), false);
                rlSetVertexAttribute(8, 4, RL_FLOAT, false, 0, 0);
                rlEnableVertexAttribute(8);

                if (mesh->vboId) {
                    if (mesh->vboId[7] == 0) mesh->vboId[7] = idVbo;
                    if (mesh->vboId[8] == 0) mesh->vboId[8] = wtVbo;
                }
                rlDisableVertexArray();
            }
        }
    }

    s_assetCount++;
    return slot;
}

bool VFXModel_IsAssetValid(int assetHandle) {
    return assetHandle >= 0 && assetHandle < VFX_MODEL_MAX_ASSETS &&
           s_assets[assetHandle].used && s_assets[assetHandle].model.meshCount > 0;
}

int VFXModel_GetClipCount(int assetHandle) {
    if (!VFXModel_IsAssetValid(assetHandle)) return 0;
    return s_assets[assetHandle].animCount;
}

int VFXModel_GetBoneCount(int assetHandle) {
    if (!VFXModel_IsAssetValid(assetHandle)) return 0;
    return s_assets[assetHandle].boneCount;
}

int VFXModel_FindClip(int assetHandle, const char *clipName) {
    if (!VFXModel_IsAssetValid(assetHandle) || !clipName || !clipName[0]) return -1;
    const VFXModelAsset *a = &s_assets[assetHandle];
    if (!a->anims || a->animCount <= 0) return -1;

    for (int i = 0; i < a->animCount; i++) {
        if (EqualsCaseInsensitive(a->anims[i].name, clipName)) return i;
    }
    for (int i = 0; i < a->animCount; i++) {
        if (ContainsCaseInsensitive(a->anims[i].name, clipName)) return i;
    }
    return -1;
}

int VFXModel_FindBone(int assetHandle, const char *boneName) {
    if (!VFXModel_IsAssetValid(assetHandle) || !boneName || !boneName[0]) return -1;
    const VFXModelAsset *a = &s_assets[assetHandle];
    if (!a->model.skeleton.bones || a->boneCount <= 0) return -1;

    for (int i = 0; i < a->boneCount; i++) {
        if (EqualsCaseInsensitive(a->model.skeleton.bones[i].name, boneName)) return i;
    }
    for (int i = 0; i < a->boneCount; i++) {
        if (ContainsCaseInsensitive(a->model.skeleton.bones[i].name, boneName)) return i;
    }
    return -1;
}

int VFXModel_Spawn(int assetHandle, const VFXModelSpawnParams *params) {
    if (!VFXModel_IsAssetValid(assetHandle)) return -1;

    int slot = -1;
    for (int i = 0; i < VFX_MODEL_MAX_INSTANCES; i++) {
        if (!s_instances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    VFXModelSpawnParams p = params ? *params : (VFXModelSpawnParams){ 0 };
    if (p.scale <= 0.0f) p.scale = 1.0f;
    if (p.animSpeed <= 0.0f) p.animSpeed = 1.0f;
    if (p.tint.a == 0) p.tint = WHITE;
    if (p.glowColor.a == 0) p.glowColor = p.tint;
    if (p.glowIntensity <= 0.0f) p.glowIntensity = 1.8f;
    if (p.opacity <= 0.0f) p.opacity = 0.72f;

    int clipIdx = -1;
    if (s_assets[assetHandle].animCount > 0) {
        if (p.clipName && p.clipName[0]) {
            clipIdx = VFXModel_FindClip(assetHandle, p.clipName);
        }
        if (clipIdx < 0) clipIdx = 0;
    }

    VFXModelInstance *inst = &s_instances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->assetHandle = assetHandle;
    inst->position = p.position;
    inst->rotationEuler = p.rotationEuler;
    inst->scale = p.scale;
    inst->useExplicitMatrix = false;
    inst->worldMatrix = BuildTRS(p.position, p.rotationEuler, p.scale);

    inst->clipIndex = clipIdx;
    inst->clipFrame = 0.0f;
    inst->animSpeed = p.animSpeed;
    inst->loop = p.loop;
    inst->prevClipIndex = -1;
    inst->prevClipFrame = 0.0f;
    inst->blendTimer = 0.0f;
    inst->blendDuration = 0.0f;

    inst->age = 0.0f;
    inst->lifetime = p.lifetime;
    inst->fadeInTime = p.fadeInTime;
    inst->fadeOutTime = p.fadeOutTime;
    inst->manualDissolve = -1.0f;
    inst->revealStart = 0.0f;
    inst->revealEnd = 1.0f;

    inst->style = p.style;
    inst->tint = p.tint;
    inst->glowColor = p.glowColor;
    inst->glowIntensity = p.glowIntensity;
    inst->opacity = Clamp(p.opacity, 0.0f, 1.0f);

    EvaluateInstanceBones(inst);
    return slot;
}

void VFXModel_Kill(int instanceHandle) {
    if (instanceHandle < 0 || instanceHandle >= VFX_MODEL_MAX_INSTANCES) return;
    s_instances[instanceHandle].active = false;
}

void VFXModel_KillAll(void) {
    for (int i = 0; i < VFX_MODEL_MAX_INSTANCES; i++) {
        s_instances[i].active = false;
    }
}

bool VFXModel_IsAlive(int instanceHandle) {
    return instanceHandle >= 0 && instanceHandle < VFX_MODEL_MAX_INSTANCES &&
           s_instances[instanceHandle].active;
}

void VFXModel_SetTransform(int instanceHandle, Vector3 position, Vector3 rotationEuler, float scale) {
    if (!VFXModel_IsAlive(instanceHandle)) return;
    VFXModelInstance *inst = &s_instances[instanceHandle];
    inst->position = position;
    inst->rotationEuler = rotationEuler;
    inst->scale = (scale > 0.0f) ? scale : 1.0f;
    inst->useExplicitMatrix = false;
    inst->worldMatrix = BuildTRS(inst->position, inst->rotationEuler, inst->scale);
}

void VFXModel_SetPoseMatrix(int instanceHandle, Matrix worldMatrix) {
    if (!VFXModel_IsAlive(instanceHandle)) return;
    VFXModelInstance *inst = &s_instances[instanceHandle];
    inst->worldMatrix = worldMatrix;
    inst->useExplicitMatrix = true;
}

void VFXModel_SetDissolve(int instanceHandle, float dissolve01) {
    if (!VFXModel_IsAlive(instanceHandle)) return;
    s_instances[instanceHandle].manualDissolve = dissolve01;
}

void VFXModel_SetSpineReveal(int instanceHandle, float revealStart01, float revealEnd01) {
    if (!VFXModel_IsAlive(instanceHandle)) return;
    s_instances[instanceHandle].revealStart = Clamp(revealStart01, 0.0f, 1.0f);
    s_instances[instanceHandle].revealEnd   = Clamp(revealEnd01, 0.0f, 1.0f);
}

void VFXModel_SetColors(int instanceHandle, Color tint, Color glowColor, float glowIntensity) {
    if (!VFXModel_IsAlive(instanceHandle)) return;
    VFXModelInstance *inst = &s_instances[instanceHandle];
    if (tint.a > 0) inst->tint = tint;
    if (glowColor.a > 0) inst->glowColor = glowColor;
    if (glowIntensity >= 0.0f) inst->glowIntensity = glowIntensity;
}

bool VFXModel_PlayClipIndex(int instanceHandle, int clipIndex, bool loop, float speed, float crossFadeSec) {
    if (!VFXModel_IsAlive(instanceHandle)) return false;
    VFXModelInstance *inst = &s_instances[instanceHandle];
    if (!VFXModel_IsAssetValid(inst->assetHandle)) return false;
    const VFXModelAsset *a = &s_assets[inst->assetHandle];
    if (clipIndex < 0 || clipIndex >= a->animCount) return false;

    if (crossFadeSec > 0.0001f && inst->clipIndex >= 0 && inst->clipIndex != clipIndex) {
        inst->prevClipIndex = inst->clipIndex;
        inst->prevClipFrame = inst->clipFrame;
        inst->blendTimer = 0.0f;
        inst->blendDuration = crossFadeSec;
    } else {
        inst->prevClipIndex = -1;
        inst->blendTimer = 0.0f;
        inst->blendDuration = 0.0f;
    }

    inst->clipIndex = clipIndex;
    inst->clipFrame = 0.0f;
    inst->loop = loop;
    inst->animSpeed = (speed > 0.0f) ? speed : 1.0f;
    EvaluateInstanceBones(inst);
    return true;
}

bool VFXModel_PlayClip(int instanceHandle, const char *clipName, bool loop, float speed, float crossFadeSec) {
    if (!VFXModel_IsAlive(instanceHandle)) return false;
    int idx = VFXModel_FindClip(s_instances[instanceHandle].assetHandle, clipName);
    if (idx < 0) return false;
    return VFXModel_PlayClipIndex(instanceHandle, idx, loop, speed, crossFadeSec);
}

float VFXModel_GetNormalizedTime(int instanceHandle) {
    if (!VFXModel_IsAlive(instanceHandle)) return 0.0f;
    const VFXModelInstance *inst = &s_instances[instanceHandle];
    if (inst->lifetime > 0.0f) {
        return Clamp(inst->age / inst->lifetime, 0.0f, 1.0f);
    }
    const VFXModelAsset *a = &s_assets[inst->assetHandle];
    if (a->anims && inst->clipIndex >= 0 && inst->clipIndex < a->animCount) {
        int fc = a->anims[inst->clipIndex].keyframeCount;
        if (fc > 1) return Clamp(inst->clipFrame / (float)(fc - 1), 0.0f, 1.0f);
    }
    return 0.0f;
}

bool VFXModel_GetBoneWorldMatrix(int instanceHandle, int boneIndex, Matrix *outWorldMatrix) {
    if (!outWorldMatrix || !VFXModel_IsAlive(instanceHandle)) return false;
    const VFXModelInstance *inst = &s_instances[instanceHandle];
    if (boneIndex < 0 || boneIndex >= inst->posedBoneCount) return false;

    if (inst->hasPosedMatrices) {
        *outWorldMatrix = MatrixMultiply(inst->posedMatrices[boneIndex], inst->worldMatrix);
        return true;
    }
    Matrix boneModel = VFXModel_TransformToMatrix(inst->posedBones[boneIndex]);
    *outWorldMatrix = MatrixMultiply(boneModel, inst->worldMatrix);
    return true;
}

void VFXModel_SetSpinePathOverride(int instanceHandle,
                                   int spineBoneCount,
                                   float headArcDist,
                                   float bodyScale,
                                   float bankStrength,
                                   float jawOpenRadians,
                                   VFXModelSpineCurveFn curveFn,
                                   void *userData) {
    if (!VFXModel_IsAlive(instanceHandle) || !curveFn) return;
    VFXModelInstance *inst = &s_instances[instanceHandle];
    inst->spinePathActive   = true;
    inst->spineBoneCount    = spineBoneCount;
    inst->spineHeadArcDist  = headArcDist;
    inst->spineBodyScale    = (bodyScale > 0.01f) ? bodyScale : 1.0f;
    inst->spineBankStrength = bankStrength;
    inst->spineJawOpenRad   = jawOpenRadians;
    inst->spineCurveFn      = curveFn;
    inst->spineUserData     = userData;
    EvaluateInstanceBones(inst);
}

void VFXModel_ClearSpinePathOverride(int instanceHandle) {
    if (!VFXModel_IsAlive(instanceHandle)) return;
    VFXModelInstance *inst = &s_instances[instanceHandle];
    inst->spinePathActive = false;
    inst->spineCurveFn    = NULL;
    inst->spineUserData   = NULL;
}

bool VFXModel_GetBoneWorldPos(int instanceHandle, const char *boneName, Vector3 *outPos, Vector3 *outForward) {
    if (!VFXModel_IsAlive(instanceHandle) || !boneName || !boneName[0]) return false;
    const VFXModelInstance *inst = &s_instances[instanceHandle];
    int b = VFXModel_FindBone(inst->assetHandle, boneName);
    if (b < 0) return false;

    Matrix m;
    if (!VFXModel_GetBoneWorldMatrix(instanceHandle, b, &m)) return false;
    if (outPos) {
        *outPos = (Vector3){ m.m12, m.m13, m.m14 };
    }
    if (outForward) {
        Vector3 fwd = { m.m8, m.m9, m.m10 };
        float l = Vector3Length(fwd);
        *outForward = (l > 1e-5f) ? Vector3Scale(fwd, 1.0f / l) : (Vector3){ 0.0f, 0.0f, 1.0f };
    }
    return true;
}

void VFXModel_Update(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    s_elapsedTime += dt;

    for (int i = 0; i < VFX_MODEL_MAX_INSTANCES; i++) {
        VFXModelInstance *inst = &s_instances[i];
        if (!inst->active) continue;

        inst->age += dt;
        if (inst->lifetime > 0.0f && inst->age >= inst->lifetime) {
            inst->active = false;
            continue;
        }

        const VFXModelAsset *a = &s_assets[inst->assetHandle];
        if (a->anims && inst->clipIndex >= 0 && inst->clipIndex < a->animCount) {
            int fc = a->anims[inst->clipIndex].keyframeCount;
            if (fc > 1) {
                float maxF = (float)(fc - 1);
                inst->clipFrame += dt * VFX_MODEL_CLIP_FPS * inst->animSpeed;
                if (inst->loop) {
                    inst->clipFrame = fmodf(inst->clipFrame, maxF);
                    if (inst->clipFrame < 0.0f) inst->clipFrame += maxF;
                } else if (inst->clipFrame > maxF) {
                    inst->clipFrame = maxF;
                }
            }
        }

        if (inst->blendDuration > 0.0f) {
            inst->blendTimer += dt;
            if (inst->blendTimer >= inst->blendDuration) {
                inst->prevClipIndex = -1;
                inst->blendDuration = 0.0f;
            }
        }

        EvaluateInstanceBones(inst);
    }
}

void VFXModel_UpdateFrame(Camera3D camera) {
    s_frameWorldFromShaderSpace = MatrixInvert(GetCameraMatrix(camera));
    s_frameViewPos = camera.position;

    Vector3 sunDir = Environment_GetSunDirection();
    float len = Vector3Length(sunDir);
    if (len > 1e-4f) {
        s_frameSunToLight = Vector3Scale(sunDir, -1.0f / len);
    }
    s_frameSunColor    = ColorToLinearVec3(Environment_GetSunColor());
    s_frameSkyColor    = ColorToLinearVec3(Environment_GetAmbientColor());
    s_frameGroundColor = Vector3Scale(s_frameSkyColor, 0.45f);
    s_frameUpdated     = true;
}

static void DrawSingleInstance(const VFXModelInstance *inst) {
    const VFXModelAsset *asset = &s_assets[inst->assetHandle];
    if (!asset->used || asset->model.meshCount <= 0) return;

    float dissolve = ComputeEffectiveDissolve(inst);
    if (dissolve >= 0.999f) return;

    float skinnedFlag = (inst->posedBoneCount > 0) ? 1.0f : 0.0f;
    int   styleInt    = (int)inst->style;
    Vector3 glowRgb   = ColorToLinearVec3(inst->glowColor);

    // ENGINE_LANDMINES §8 & §10: set all uniforms inside BeginShaderMode right before DrawMesh
    BeginShaderMode(s_shader);
    if (s_locWorldFromShaderSpace >= 0) {
        SetShaderValueMatrix(s_shader, s_locWorldFromShaderSpace, s_frameWorldFromShaderSpace);
    }
    if (s_locTime >= 0)          SetShaderValue(s_shader, s_locTime, &s_elapsedTime, SHADER_UNIFORM_FLOAT);
    if (s_locSkinned >= 0)       SetShaderValue(s_shader, s_locSkinned, &skinnedFlag, SHADER_UNIFORM_FLOAT);
    if (s_locStyle >= 0)         SetShaderValue(s_shader, s_locStyle, &styleInt, SHADER_UNIFORM_INT);
    if (s_locDissolve >= 0)      SetShaderValue(s_shader, s_locDissolve, &dissolve, SHADER_UNIFORM_FLOAT);
    if (s_locSpineBounds >= 0) {
        Vector2 sb = { asset->spineMinX, asset->spineMaxX };
        SetShaderValue(s_shader, s_locSpineBounds, &sb, SHADER_UNIFORM_VEC2);
    }
    if (s_locRevealRange >= 0) {
        Vector2 rr = { inst->revealStart, inst->revealEnd };
        SetShaderValue(s_shader, s_locRevealRange, &rr, SHADER_UNIFORM_VEC2);
    }
    if (s_locOpacity >= 0)       SetShaderValue(s_shader, s_locOpacity, &inst->opacity, SHADER_UNIFORM_FLOAT);
    if (s_locGlowColor >= 0)     SetShaderValue(s_shader, s_locGlowColor, &glowRgb, SHADER_UNIFORM_VEC3);
    if (s_locGlowIntensity >= 0) SetShaderValue(s_shader, s_locGlowIntensity, &inst->glowIntensity, SHADER_UNIFORM_FLOAT);
    if (s_locSunToLight >= 0)    SetShaderValue(s_shader, s_locSunToLight, &s_frameSunToLight, SHADER_UNIFORM_VEC3);
    if (s_locSunColor >= 0)      SetShaderValue(s_shader, s_locSunColor, &s_frameSunColor, SHADER_UNIFORM_VEC3);
    if (s_locSkyColor >= 0)      SetShaderValue(s_shader, s_locSkyColor, &s_frameSkyColor, SHADER_UNIFORM_VEC3);
    if (s_locGroundColor >= 0)   SetShaderValue(s_shader, s_locGroundColor, &s_frameGroundColor, SHADER_UNIFORM_VEC3);
    if (s_locViewPos >= 0)       SetShaderValue(s_shader, s_locViewPos, &s_frameViewPos, SHADER_UNIFORM_VEC3);

    if (inst->posedBoneCount > 0 && s_locBoneRows >= 0) {
        SetShaderValueV(s_shader, s_locBoneRows, inst->boneRows,
                        SHADER_UNIFORM_VEC4, inst->posedBoneCount * 3);
    }

    for (int m = 0; m < asset->model.meshCount; m++) {
        int matIdx = asset->model.meshMaterial ? asset->model.meshMaterial[m] : 0;
        Material mat = (asset->model.materials && matIdx >= 0 && matIdx < asset->model.materialCount)
            ? asset->model.materials[matIdx]
            : LoadMaterialDefault();
        mat.shader = s_shader;

        Color baseC = mat.maps[MATERIAL_MAP_DIFFUSE].color;
        mat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){
            (unsigned char)((baseC.r * inst->tint.r) / 255),
            (unsigned char)((baseC.g * inst->tint.g) / 255),
            (unsigned char)((baseC.b * inst->tint.b) / 255),
            (unsigned char)((baseC.a * inst->tint.a) / 255)
        };
        DrawMesh(asset->model.meshes[m], mat, inst->worldMatrix);
    }
    EndShaderMode();
}

void VFXModel_Draw(void) {
    if (!s_shaderReady) return;

    // Pass 1: Opaque styles (SOLID_LIT, SOLID_AURA) with depth write on
    for (int i = 0; i < VFX_MODEL_MAX_INSTANCES; i++) {
        if (!s_instances[i].active) continue;
        if (s_instances[i].style == VFX_MODEL_STYLE_QI_SPIRIT) continue;
        DrawSingleInstance(&s_instances[i]);
    }

    // Pass 2: Premultiplied translucent QI_SPIRIT (matches VFX_ResolvePremultiplied)
    bool hasSpirit = false;
    for (int i = 0; i < VFX_MODEL_MAX_INSTANCES; i++) {
        if (s_instances[i].active && s_instances[i].style == VFX_MODEL_STYLE_QI_SPIRIT) {
            hasSpirit = true;
            break;
        }
    }
    if (hasSpirit) {
        rlDrawRenderBatchActive();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
        for (int i = 0; i < VFX_MODEL_MAX_INSTANCES; i++) {
            if (!s_instances[i].active) continue;
            if (s_instances[i].style != VFX_MODEL_STYLE_QI_SPIRIT) continue;
            DrawSingleInstance(&s_instances[i]);
        }
        rlDrawRenderBatchActive();
        EndBlendMode();
        rlEnableDepthMask();
    }
}

void VFXModel_GetStats(int *activeInstances, int *maxInstances, int *loadedAssets) {
    int active = 0;
    for (int i = 0; i < VFX_MODEL_MAX_INSTANCES; i++) {
        if (s_instances[i].active) active++;
    }
    if (activeInstances) *activeInstances = active;
    if (maxInstances)    *maxInstances = VFX_MODEL_MAX_INSTANCES;
    if (loadedAssets)    *loadedAssets = s_assetCount;
}
