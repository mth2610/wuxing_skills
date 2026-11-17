// character/spring_bone.c
// Lightweight Bone-Driven Secondary Motion / Spring Bone Physics for Wuxia Garments

#include "spring_bone.h"
#include "character_model.h"
#include "raymath.h"
#include <string.h>
#include <math.h>

#define SPRING_MAX_SKELETON_BONES 128

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static Matrix MatrixFromTransform(Transform t)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x, t.scale.y, t.scale.z),
                                         QuaternionToMatrix(t.rotation)),
                          MatrixTranslate(t.translation.x, t.translation.y, t.translation.z));
}

static Matrix BindWorldMatrix(const Model *model, int boneIndex)
{
    if (!model || !model->skeleton.bones || !model->skeleton.bindPose)
        return MatrixIdentity();
    if (boneIndex < 0 || boneIndex >= model->skeleton.boneCount)
        return MatrixIdentity();
    return MatrixFromTransform(model->skeleton.bindPose[boneIndex]);
}

// Direction of the bone's local +Y axis (the axis glTF bones and every rig in
// this project point along) expressed in the given matrix' space.
static Vector3 MatrixAxisY(Matrix mat)
{
    Vector3 dir = {mat.m4, mat.m5, mat.m6};
    float len = Vector3Length(dir);
    if (len < 0.0001f)
        return (Vector3){0.0f, -1.0f, 0.0f};
    return Vector3Scale(dir, 1.0f / len);
}

// Rotates a direction by the purely rotational part of `mat`.
static Vector3 RotateDirByMatrix(Vector3 dir, Matrix mat)
{
    Matrix rotOnly = mat;
    rotOnly.m12 = 0.0f;
    rotOnly.m13 = 0.0f;
    rotOnly.m14 = 0.0f;
    Vector3 out = Vector3Transform(dir, rotOnly);
    float len = Vector3Length(out);
    return (len > 0.0001f) ? Vector3Scale(out, 1.0f / len) : dir;
}

static bool NameStartsWith(const char *name, const char *prefix)
{
    if (!name || !prefix)
        return false;
    size_t n = strlen(prefix);
    return strncmp(name, prefix, n) == 0;
}

static void SpringBone_TopologicalSort(SpringBoneSystem *sys);

static void TuneByName(SpringBone *b)
{
    if (!b)
        return;
    if (strstr(b->name, "sleeve"))
    {
        b->stiffness = 0.28f;
        b->damping = 0.85f;
        b->gravityFactor = 0.85f;
        b->windInfluence = 0.75f;
        b->maxAngleDeg = 52.0f;
        b->boneRadius = 0.022f;
    }
    else if (strstr(b->name, "ribbon"))
    {
        b->stiffness = 0.13f;
        b->damping = 0.84f;
        b->gravityFactor = 0.60f;
        b->windInfluence = 1.00f;
        b->maxAngleDeg = 62.0f;
        b->boneRadius = 0.014f;
    }
    else if (strstr(b->name, "skirt") || strstr(b->name, "hem") || strstr(b->name, "robe"))
    {
        b->stiffness = 0.20f;
        b->damping = 0.88f;
        b->gravityFactor = 0.78f;
        b->windInfluence = 0.65f;
        b->maxAngleDeg = 48.0f;
        b->boneRadius = 0.026f;
    }
    else
    {
        b->stiffness = 0.22f;
        b->damping = 0.86f;
        b->gravityFactor = 0.80f;
        b->windInfluence = 0.60f;
        b->maxAngleDeg = 50.0f;
        b->boneRadius = 0.022f;
    }
    // Segments further down a chain are lighter: less pull, more flutter.
    if (b->parentSpring >= 0)
    {
        b->stiffness *= 0.75f;
        b->damping *= 0.98f;
        b->windInfluence = Clamp(b->windInfluence + 0.20f, 0.0f, 1.0f);
        b->maxAngleDeg += 8.0f;
    }
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void SpringBone_Init(SpringBoneSystem *sys)
{
    if (!sys)
        return;
    memset(sys, 0, sizeof(SpringBoneSystem));
    sys->initialized = false;
}

void SpringBone_AddDefaultColliders(SpringBoneSystem *sys)
{
    if (!sys)
        return;
    if (sys->colliderCount > 0)
        return;

    // Hông & Đùi (2 tầng mỗi bên)
    SpringBone_AddCollider(sys, "mixamorig:Hips", (Vector3){0.0f, -0.04f, 0.02f}, 0.165f);
    SpringBone_AddCollider(sys, "mixamorig:LeftUpLeg", (Vector3){0.0f, -0.16f, 0.02f}, 0.120f);
    SpringBone_AddCollider(sys, "mixamorig:RightUpLeg", (Vector3){0.0f, -0.16f, 0.02f}, 0.120f);
    SpringBone_AddCollider(sys, "mixamorig:LeftUpLeg", (Vector3){0.0f, -0.34f, 0.02f}, 0.105f);
    SpringBone_AddCollider(sys, "mixamorig:RightUpLeg", (Vector3){0.0f, -0.34f, 0.02f}, 0.105f);

    // Toàn bộ Cánh tay & Cẳng tay (đẩy ống tay áo bọc quanh tay khi gió thổi)
    SpringBone_AddCollider(sys, "mixamorig:LeftArm", (Vector3){0.0f, -0.12f, 0.00f}, 0.085f);
    SpringBone_AddCollider(sys, "mixamorig:RightArm", (Vector3){0.0f, -0.12f, 0.00f}, 0.085f);
    SpringBone_AddCollider(sys, "mixamorig:LeftForeArm", (Vector3){0.0f, -0.10f, 0.00f}, 0.080f);
    SpringBone_AddCollider(sys, "mixamorig:RightForeArm", (Vector3){0.0f, -0.10f, 0.00f}, 0.080f);

    // Ngực & Thân trên
    SpringBone_AddCollider(sys, "mixamorig:Spine1", (Vector3){0.0f, 0.04f, 0.00f}, 0.135f);
    SpringBone_AddCollider(sys, "mixamorig:Spine2", (Vector3){0.0f, 0.05f, 0.00f}, 0.145f);
}

void SpringBone_SetupWuxiaDefaults(SpringBoneSystem *sys)
{
    if (!sys)
        return;
    SpringBone_Init(sys);

    // 1. Skirt Tà Áo
    SpringBone_Add(sys, "cloth_skirt_f_l", "mixamorig:Hips",
                   (Vector3){0.12f, -0.92f, 0.14f}, 0.36f, 0.22f, 0.88f, 0.75f);
    SpringBone_Add(sys, "cloth_skirt_f_r", "mixamorig:Hips",
                   (Vector3){-0.12f, -0.92f, 0.14f}, 0.36f, 0.22f, 0.88f, 0.75f);
    SpringBone_Add(sys, "cloth_skirt_b_l", "mixamorig:Hips",
                   (Vector3){0.12f, -0.94f, -0.12f}, 0.38f, 0.20f, 0.88f, 0.75f);
    SpringBone_Add(sys, "cloth_skirt_b_r", "mixamorig:Hips",
                   (Vector3){-0.12f, -0.94f, -0.12f}, 0.38f, 0.20f, 0.88f, 0.75f);

    // 2. Bell Sleeves Tay Áo Thụng
    SpringBone_Add(sys, "cloth_sleeve_l_01", "mixamorig:LeftForeArm",
                   (Vector3){0.0f, -0.95f, -0.18f}, 0.24f, 0.28f, 0.85f, 0.85f);
    SpringBone_Add(sys, "cloth_sleeve_r_01", "mixamorig:RightForeArm",
                   (Vector3){0.0f, -0.95f, -0.18f}, 0.24f, 0.28f, 0.85f, 0.85f);

    // 3. Waist Ribbons Dải Lụa Thắt Lưng
    SpringBone_Add(sys, "cloth_ribbon_l", "mixamorig:Hips",
                   (Vector3){0.04f, -0.96f, 0.16f}, 0.30f, 0.14f, 0.84f, 0.60f);
    SpringBone_Add(sys, "cloth_ribbon_r", "mixamorig:Hips",
                   (Vector3){-0.04f, -0.96f, 0.16f}, 0.30f, 0.14f, 0.84f, 0.60f);

    // 4. Bodily Collision Spheres
    SpringBone_AddDefaultColliders(sys);

    sys->initialized = true;
}

int SpringBone_Add(SpringBoneSystem *sys, const char *name, const char *parentBone,
                   Vector3 localRestDir, float length, float stiffness, float damping, float gravityFactor)
{
    if (!sys || !name || sys->boneCount >= SPRING_BONE_MAX)
        return -1;

    int idx = sys->boneCount++;
    SpringBone *b = &sys->bones[idx];
    memset(b, 0, sizeof(SpringBone));
    strncpy(b->name, name, sizeof(b->name) - 1);
    if (parentBone)
        strncpy(b->parentBone, parentBone, sizeof(b->parentBone) - 1);

    b->parentSpring = -1;
    b->boneIndex = -1;
    b->localHeadOffset = (Vector3){0.0f, 0.0f, 0.0f};
    b->localRestDir = Vector3Normalize(localRestDir);
    b->bindHead = (Vector3){0.0f, 0.0f, 0.0f};
    b->bindDir = b->localRestDir;
    b->modelHeadPos = (Vector3){0.0f, 0.0f, 0.0f};
    b->modelDir = b->localRestDir;
    b->length = (length > 0.02f) ? length : 0.30f;
    b->stiffness = Clamp(stiffness, 0.02f, 0.80f);
    b->damping = Clamp(damping, 0.50f, 0.98f);
    b->gravityFactor = Clamp(gravityFactor, 0.0f, 2.0f);
    b->windInfluence = 0.55f;
    b->maxAngleDeg = 48.0f;
    b->boneRadius = 0.025f;
    b->initialized = false;

    return idx;
}

int SpringBone_AddCollider(SpringBoneSystem *sys, const char *boneName, Vector3 offset, float radius)
{
    if (!sys || !boneName || sys->colliderCount >= SPRING_COLLIDER_MAX)
        return -1;

    int idx = sys->colliderCount++;
    SpringCollider *c = &sys->colliders[idx];
    c->type = SPRING_COLLIDER_SPHERE;
    strncpy(c->boneName, boneName, sizeof(c->boneName) - 1);
    c->localOffset = offset;
    c->radius = (radius > 0.01f) ? radius : 0.10f;
    c->worldPos = (Vector3){0};

    return idx;
}

int SpringBone_RegisterModelCloth(SpringBoneSystem *sys, const Model *model)
{
    if (!sys || !model || !model->skeleton.bones || model->skeleton.boneCount <= 0)
        return 0;
    if (!model->skeleton.bindPose)
        return 0;

    const int boneCount = model->skeleton.boneCount;
    int added = 0;

    int springOfBone[SPRING_MAX_SKELETON_BONES];
    bool needsGeometryMeasure[SPRING_BONE_MAX];
    for (int i = 0; i < SPRING_MAX_SKELETON_BONES; i++)
        springOfBone[i] = -1;
    for (int i = 0; i < SPRING_BONE_MAX; i++)
        needsGeometryMeasure[i] = false;

    // -------------------------------------------------------------------------
    // Pass 1 — register or claim existing unmeasured default entries.
    // -------------------------------------------------------------------------
    for (int i = 0; i < boneCount && i < SPRING_MAX_SKELETON_BONES; i++)
    {
        const char *name = model->skeleton.bones[i].name;
        if (!NameStartsWith(name, "cloth"))
            continue;

        int existingIdx = -1;
        for (int k = 0; k < sys->boneCount; k++)
        {
            if (strcmp(sys->bones[k].name, name) == 0)
            {
                existingIdx = k;
                break;
            }
        }

        if (existingIdx >= 0)
        {
            springOfBone[i] = existingIdx;
            // If this came from SetupWuxiaDefaults (boneIndex < 0), re-measure from real rig!
            if (sys->bones[existingIdx].boneIndex < 0)
            {
                sys->bones[existingIdx].boneIndex = i;
                sys->bones[existingIdx].length = 0.0f;
                sys->bones[existingIdx].initialized = false;
                needsGeometryMeasure[existingIdx] = true;
                added++;
            }
            continue;
        }

        if (sys->boneCount >= SPRING_BONE_MAX)
            break;

        int idx = sys->boneCount++;
        SpringBone *b = &sys->bones[idx];
        memset(b, 0, sizeof(SpringBone));
        strncpy(b->name, name, sizeof(b->name) - 1);
        b->parentSpring = -1;
        b->boneIndex = i;
        b->localRestDir = (Vector3){0.0f, -1.0f, 0.0f};
        b->localHeadOffset = (Vector3){0.0f, 0.0f, 0.0f};
        b->length = 0.0f;
        b->initialized = false;
        springOfBone[i] = idx;
        needsGeometryMeasure[idx] = true;
        added++;
    }

    if (added == 0)
    {
        if (sys->boneCount > 0)
            sys->initialized = true;
        return 0;
    }

    // -------------------------------------------------------------------------
    // Pass 2 — chain topology, anchor frame, bind direction and segment length.
    // -------------------------------------------------------------------------
    for (int i = 0; i < boneCount && i < SPRING_MAX_SKELETON_BONES; i++)
    {
        int si = springOfBone[i];
        if (si < 0 || si >= sys->boneCount || !needsGeometryMeasure[si])
            continue;

        SpringBone *b = &sys->bones[si];
        Matrix bindWorld = BindWorldMatrix(model, i);
        Vector3 head = {bindWorld.m12, bindWorld.m13, bindWorld.m14};
        b->bindHead = head;

        int root = i;
        for (int guard = 0; guard < 64; guard++)
        {
            int p = model->skeleton.bones[root].parent;
            if (p < 0 || p >= boneCount)
                break;
            if (!NameStartsWith(model->skeleton.bones[p].name, "cloth"))
                break;
            root = p;
        }

        int skeletonParent = model->skeleton.bones[i].parent;
        if (root != i && skeletonParent >= 0 && skeletonParent < SPRING_MAX_SKELETON_BONES)
        {
            b->parentSpring = springOfBone[skeletonParent];
        }
        else
        {
            b->parentSpring = -1;
        }

        int anchor = model->skeleton.bones[root].parent;
        Matrix bindAnchor = MatrixIdentity();
        if (anchor >= 0 && anchor < boneCount)
        {
            const char *anchorName = model->skeleton.bones[anchor].name;
            if (anchorName)
                strncpy(b->parentBone, anchorName, sizeof(b->parentBone) - 1);
            bindAnchor = BindWorldMatrix(model, anchor);
        }

        Matrix bindLocal = MatrixMultiply(bindWorld, MatrixInvert(bindAnchor));
        b->localHeadOffset = (Vector3){bindLocal.m12, bindLocal.m13, bindLocal.m14};

        Vector3 dirModel = (Vector3){0.0f, -1.0f, 0.0f};
        bool haveDir = false;
        for (int c = 0; c < boneCount; c++)
        {
            if (model->skeleton.bones[c].parent != i)
                continue;
            if (!NameStartsWith(model->skeleton.bones[c].name, "cloth"))
                continue;
            Matrix childWorld = BindWorldMatrix(model, c);
            Vector3 childHead = {childWorld.m12, childWorld.m13, childWorld.m14};
            Vector3 delta = Vector3Subtract(childHead, head);
            float d = Vector3Length(delta);
            if (d > 0.005f)
            {
                dirModel = Vector3Scale(delta, 1.0f / d);
                b->length = d;
                haveDir = true;
                break;
            }
        }
        if (!haveDir)
        {
            Vector3 axis = MatrixAxisY(bindLocal);
            if (axis.y > 0.25f)
                axis = Vector3Scale(axis, -1.0f);
            dirModel = Vector3Normalize(axis);
        }
        b->bindDir = dirModel;

        Matrix anchorRotInv = bindAnchor;
        anchorRotInv.m12 = anchorRotInv.m13 = anchorRotInv.m14 = 0.0f;
        anchorRotInv = MatrixInvert(anchorRotInv);
        b->localRestDir = RotateDirByMatrix(dirModel, anchorRotInv);

        TuneByName(b);
        b->initialized = false;
    }

    // -------------------------------------------------------------------------
    // Pass 3a — single-pass mesh scan to accumulate terminal segment direction.
    // -------------------------------------------------------------------------
    Vector3 accDir[SPRING_BONE_MAX];
    int accCount[SPRING_BONE_MAX];
    bool isTerminalMeasure[SPRING_BONE_MAX];
    for (int i = 0; i < SPRING_BONE_MAX; i++)
    {
        accDir[i] = (Vector3){0};
        accCount[i] = 0;
        isTerminalMeasure[i] = (i < sys->boneCount && needsGeometryMeasure[i] && sys->bones[i].length <= 0.005f);
    }

    for (int mi = 0; mi < model->meshCount; mi++)
    {
        const Mesh *mesh = &model->meshes[mi];
        if (!mesh->boneWeights || !mesh->boneIndices || !mesh->vertices)
            continue;
        int counter = 0;
        for (int v = 0; v < mesh->vertexCount; v++)
        {
            int dominant = -1;
            float best = 0.0f;
            for (int k = 0; k < 4; k++, counter++)
            {
                float w = mesh->boneWeights[counter];
                if (w > best)
                {
                    best = w;
                    dominant = mesh->boneIndices[counter];
                }
            }
            if (best < 0.35f || dominant < 0 || dominant >= SPRING_MAX_SKELETON_BONES)
                continue;
            int springIdx = springOfBone[dominant];
            if (springIdx < 0 || springIdx >= sys->boneCount || !isTerminalMeasure[springIdx])
                continue;

            const SpringBone *b = &sys->bones[springIdx];
            Vector3 vp = {mesh->vertices[v * 3 + 0], mesh->vertices[v * 3 + 1], mesh->vertices[v * 3 + 2]};
            Vector3 delta = Vector3Subtract(vp, b->bindHead);
            float dLenSq = Vector3LengthSqr(delta);
            if (dLenSq < 0.0001f)
                continue;
            float invD = 1.0f / sqrtf(dLenSq);
            accDir[springIdx] = Vector3Add(accDir[springIdx], Vector3Scale(delta, invD));
            accCount[springIdx]++;
        }
    }

    bool anyTerminalValid = false;
    for (int si = 0; si < sys->boneCount; si++)
    {
        if (!isTerminalMeasure[si])
            continue;
        if (accCount[si] < 8)
        {
            isTerminalMeasure[si] = false;
            continue;
        }
        float avgLen = Vector3Length(accDir[si]);
        if (avgLen < 0.05f)
        {
            isTerminalMeasure[si] = false;
            continue;
        }
        sys->bones[si].bindDir = Vector3Scale(accDir[si], 1.0f / avgLen);
        anyTerminalValid = true;
    }

    // -------------------------------------------------------------------------
    // Pass 3b — single-pass mesh scan to measure max projection (hem reach)
    // across all terminal bones simultaneously (O(V) instead of O(B * V)).
    // -------------------------------------------------------------------------
    float maxProj[SPRING_BONE_MAX];
    for (int i = 0; i < SPRING_BONE_MAX; i++)
        maxProj[i] = 0.0f;

    if (anyTerminalValid)
    {
        for (int mi = 0; mi < model->meshCount; mi++)
        {
            const Mesh *mesh = &model->meshes[mi];
            if (!mesh->boneWeights || !mesh->boneIndices || !mesh->vertices)
                continue;
            int counter = 0;
            for (int v = 0; v < mesh->vertexCount; v++)
            {
                int dominant = -1;
                float best = 0.0f;
                for (int k = 0; k < 4; k++, counter++)
                {
                    float w = mesh->boneWeights[counter];
                    if (w > best)
                    {
                        best = w;
                        dominant = mesh->boneIndices[counter];
                    }
                }
                if (best < 0.35f || dominant < 0 || dominant >= SPRING_MAX_SKELETON_BONES)
                    continue;
                int si = springOfBone[dominant];
                if (si < 0 || si >= sys->boneCount || !isTerminalMeasure[si])
                    continue;

                const SpringBone *b = &sys->bones[si];
                Vector3 vp = {mesh->vertices[v * 3 + 0], mesh->vertices[v * 3 + 1], mesh->vertices[v * 3 + 2]};
                float proj = Vector3DotProduct(Vector3Subtract(vp, b->bindHead), b->bindDir);
                if (proj > maxProj[si])
                    maxProj[si] = proj;
            }
        }

        for (int si = 0; si < sys->boneCount; si++)
        {
            if (!isTerminalMeasure[si])
                continue;
            SpringBone *b = &sys->bones[si];
            if (maxProj[si] > 0.02f)
                b->length = maxProj[si];

            Matrix anchorRotInv = MatrixIdentity();
            if (b->parentBone[0] != '\0')
            {
                for (int a = 0; a < boneCount; a++)
                {
                    const char *an = model->skeleton.bones[a].name;
                    if (!an || strcmp(an, b->parentBone) != 0)
                        continue;
                    anchorRotInv = BindWorldMatrix(model, a);
                    anchorRotInv.m12 = anchorRotInv.m13 = anchorRotInv.m14 = 0.0f;
                    anchorRotInv = MatrixInvert(anchorRotInv);
                    break;
                }
            }
            b->localRestDir = RotateDirByMatrix(b->bindDir, anchorRotInv);
        }
    }

    for (int i = 0; i < sys->boneCount; i++)
    {
        if (!needsGeometryMeasure[i])
            continue;
        if (sys->bones[i].length < 0.02f)
            sys->bones[i].length = 0.18f;
        sys->bones[i].length = Clamp(sys->bones[i].length, 0.04f, 1.20f);
        sys->bones[i].initialized = false;
    }

    SpringBone_AddDefaultColliders(sys);
    SpringBone_TopologicalSort(sys);
    sys->initialized = true;
    return added;
}

// -----------------------------------------------------------------------------
// Simulation (Optimized with precomputed trig, anchor memoization & sub-steps)
// -----------------------------------------------------------------------------

static inline Vector3 TransformPointFast(Vector3 localPt, Matrix modelBoneMat,
                                         Vector3 charPos, float cosY, float sinY, float charScale)
{
    Vector3 modelPt = Vector3Transform(localPt, modelBoneMat);
    modelPt.x *= charScale;
    modelPt.y *= charScale;
    modelPt.z *= charScale;
    return (Vector3){
        modelPt.x * cosY + modelPt.z * sinY + charPos.x,
        modelPt.y + charPos.y,
        -modelPt.x * sinY + modelPt.z * cosY + charPos.z};
}

static inline Vector3 TransformDirFast(Vector3 localDir, Matrix modelBoneMat, float cosY, float sinY)
{
    // Rotate by upper-left 3x3 of modelBoneMat (ignoring translation)
    Vector3 modelDir = {
        localDir.x * modelBoneMat.m0 + localDir.y * modelBoneMat.m4 + localDir.z * modelBoneMat.m8,
        localDir.x * modelBoneMat.m1 + localDir.y * modelBoneMat.m5 + localDir.z * modelBoneMat.m9,
        localDir.x * modelBoneMat.m2 + localDir.y * modelBoneMat.m6 + localDir.z * modelBoneMat.m10};
    float lenSq = Vector3LengthSqr(modelDir);
    if (lenSq > 1e-8f)
    {
        float invLen = 1.0f / sqrtf(lenSq);
        modelDir.x *= invLen;
        modelDir.y *= invLen;
        modelDir.z *= invLen;
    }
    // Yaw rotation around +Y preserves unit length (no second sqrtf needed)
    return (Vector3){
        modelDir.x * cosY + modelDir.z * sinY,
        modelDir.y,
        -modelDir.x * sinY + modelDir.z * cosY};
}

static inline Vector3 WorldToModelPointFast(Vector3 world, Vector3 charPos, float cosY, float sinY, float invScale)
{
    float px = world.x - charPos.x;
    float py = world.y - charPos.y;
    float pz = world.z - charPos.z;
    return (Vector3){
        (px * cosY - pz * sinY) * invScale,
        py * invScale,
        (px * sinY + pz * cosY) * invScale};
}

static inline Vector3 WorldToModelDirFast(Vector3 dir, float cosY, float sinY)
{
    Vector3 m = {
        dir.x * cosY - dir.z * sinY,
        dir.y,
        dir.x * sinY + dir.z * cosY};
    float lenSq = Vector3LengthSqr(m);
    if (lenSq > 1e-8f)
    {
        float invLen = 1.0f / sqrtf(lenSq);
        return (Vector3){m.x * invLen, m.y * invLen, m.z * invLen};
    }
    return dir;
}

static void SpringBone_TopologicalSort(SpringBoneSystem *sys)
{
    int n = sys->boneCount;
    if (n <= 1)
        return;

    bool done[SPRING_BONE_MAX];
    int mapOldToNew[SPRING_BONE_MAX];
    int oldOfNew[SPRING_BONE_MAX];
    for (int i = 0; i < SPRING_BONE_MAX; i++)
    {
        done[i] = false;
        mapOldToNew[i] = -1;
    }

    int written = 0;
    bool progress = true;
    while (written < n && progress)
    {
        progress = false;
        for (int i = 0; i < n; i++)
        {
            if (done[i])
                continue;
            int ps = sys->bones[i].parentSpring;
            if (ps >= 0 && (ps >= n || mapOldToNew[ps] < 0))
                continue;
            mapOldToNew[i] = written;
            oldOfNew[written] = i;
            done[i] = true;
            written++;
            progress = true;
        }
    }
    for (int i = 0; i < n; i++)
    {
        if (done[i])
            continue;
        mapOldToNew[i] = written;
        oldOfNew[written] = i;
        done[i] = true;
        written++;
    }

    SpringBone sorted[SPRING_BONE_MAX];
    for (int i = 0; i < n; i++)
    {
        SpringBone src = sys->bones[oldOfNew[i]];
        int ps = src.parentSpring;
        src.parentSpring = (ps >= 0 && ps < n) ? mapOldToNew[ps] : -1;
        sorted[i] = src;
    }
    memcpy(sys->bones, sorted, sizeof(SpringBone) * (size_t)n);
}

static void SpringBone_Simulate(SpringBoneSystem *sys, const struct CharacterAnimState *animState,
                                Vector3 charPos, float charYaw, float charScale, float dt,
                                Vector3 windVelocity, SpringWindFn windFn, void *windUser)
{
    if (!sys || sys->boneCount <= 0)
        return;

    float safeDt = Clamp(dt, 0.002f, 0.050f);
    const float cosY = cosf(charYaw);
    const float sinY = sinf(charYaw);
    const float invScale = (charScale > 0.0001f) ? (1.0f / charScale) : 1.0f;

    Vector3 charVel = {0};
    if (sys->initialized)
    {
        Vector3 delta = Vector3Subtract(charPos, sys->prevCharPos);
        if (Vector3LengthSqr(delta) < 9.0f)
            charVel = Vector3Scale(delta, 1.0f / safeDt);
    }

    // 1. Update bodily collision spheres once per frame
    for (int c = 0; c < sys->colliderCount; c++)
    {
        SpringCollider *col = &sys->colliders[c];
        Matrix boneMat;
        if (CharacterModel_GetBoneTransform(col->boneName, animState, &boneMat))
        {
            col->worldPos = TransformPointFast(col->localOffset, boneMat, charPos, cosY, sinY, charScale);
        }
        else
        {
            col->worldPos = Vector3Add(charPos, (Vector3){0.0f, 0.90f * charScale, 0.0f});
        }
    }

// 2. Memoize unique anchor bone matrices across all chains (typically only 3 bones)
#define MAX_UNIQUE_ANCHORS 16
    const char *anchorNames[MAX_UNIQUE_ANCHORS];
    Matrix anchorMats[MAX_UNIQUE_ANCHORS];
    int uniqueAnchorCount = 0;
    int boneAnchorSlot[SPRING_BONE_MAX];

    for (int b = 0; b < sys->boneCount; b++)
    {
        const char *pName = sys->bones[b].parentBone;
        if (pName[0] == '\0')
        {
            boneAnchorSlot[b] = -1;
            continue;
        }
        int found = -1;
        for (int u = 0; u < uniqueAnchorCount; u++)
        {
            if (strcmp(anchorNames[u], pName) == 0)
            {
                found = u;
                break;
            }
        }
        if (found < 0 && uniqueAnchorCount < MAX_UNIQUE_ANCHORS)
        {
            Matrix mat = MatrixIdentity();
            CharacterModel_GetBoneTransform(pName, animState, &mat);
            found = uniqueAnchorCount++;
            anchorNames[found] = pName;
            anchorMats[found] = mat;
        }
        boneAnchorSlot[b] = found;
    }

    // 3. Precompute frame anchor heads & rest directions
    Vector3 frameAnchorHead[SPRING_BONE_MAX];
    Vector3 frameRestDir[SPRING_BONE_MAX];
    for (int b = 0; b < sys->boneCount; b++)
    {
        SpringBone *bone = &sys->bones[b];
        Matrix anchorMat = (boneAnchorSlot[b] >= 0) ? anchorMats[boneAnchorSlot[b]] : MatrixIdentity();
        if (bone->parentSpring < 0)
        {
            frameAnchorHead[b] = TransformPointFast(bone->localHeadOffset, anchorMat, charPos, cosY, sinY, charScale);
        }
        else
        {
            frameAnchorHead[b] = (Vector3){0};
        }
        frameRestDir[b] = TransformDirFast(bone->localRestDir, anchorMat, cosY, sinY);
    }

    // 4. Sub-stepped Verlet integration (2 sub-steps when dt > 12ms prevents tunneling & overshoot)
    const int subSteps = (safeDt > 0.012f) ? 2 : 1;
    const float subDt = safeDt / (float)subSteps;
    const float subDtSq = subDt * subDt;
    const float subStepScale = subDt * 60.0f;

    for (int step = 0; step < subSteps; step++)
    {
        for (int b = 0; b < sys->boneCount; b++)
        {
            SpringBone *bone = &sys->bones[b];

            Vector3 headWorld = (bone->parentSpring >= 0 && bone->parentSpring < sys->boneCount)
                                    ? sys->bones[bone->parentSpring].currentPos
                                    : frameAnchorHead[b];

            Vector3 restDirWorld = frameRestDir[b];
            float currentLength = bone->length * charScale;
            Vector3 restPosWorld = Vector3Add(headWorld, Vector3Scale(restDirWorld, currentLength));

            if (!bone->initialized)
            {
                bone->headWorldPos = headWorld;
                bone->currentPos = restPosWorld;
                bone->prevPos = restPosWorld;
                bone->restPosWorld = restPosWorld;
                bone->restDirWorld = restDirWorld;
                bone->modelHeadPos = WorldToModelPointFast(headWorld, charPos, cosY, sinY, invScale);
                bone->modelDir = WorldToModelDirFast(Vector3Subtract(restPosWorld, headWorld), cosY, sinY);
                bone->initialized = true;
                continue;
            }

            bone->headWorldPos = headWorld;
            bone->restPosWorld = restPosWorld;
            bone->restDirWorld = restDirWorld;

            // Forces: Restoring spring + Gravity + Relative wind
            Vector3 toRest = Vector3Subtract(restPosWorld, bone->currentPos);
            Vector3 springForce = Vector3Scale(toRest, bone->stiffness * 80.0f);
            Vector3 gravityForce = (Vector3){0.0f, -9.81f * bone->gravityFactor, 0.0f};

            Vector3 wind = windFn ? windFn(headWorld, windUser) : windVelocity;
            Vector3 relativeWind = Vector3Subtract(wind, Vector3Scale(charVel, 0.55f));
            Vector3 windForce = Vector3Scale(relativeWind, bone->windInfluence * 1.8f);

            Vector3 totalAccel = Vector3Add(Vector3Add(springForce, gravityForce), windForce);

            // Frame-rate independent Verlet integration
            float effectiveDamping = powf(bone->damping, subStepScale);
            Vector3 inertia = Vector3Scale(Vector3Subtract(bone->currentPos, bone->prevPos), effectiveDamping);
            Vector3 nextPos = Vector3Add(Vector3Add(bone->currentPos, inertia), Vector3Scale(totalAccel, subDtSq));

            // Constraint 1: Rigid bone length
            Vector3 toTip = Vector3Subtract(nextPos, headWorld);
            float dist = Vector3Length(toTip);
            Vector3 curDir;
            if (dist > 0.0001f)
            {
                curDir = Vector3Scale(toTip, 1.0f / dist);
                nextPos = Vector3Add(headWorld, Vector3Scale(curDir, currentLength));
            }
            else
            {
                curDir = restDirWorld;
                nextPos = restPosWorld;
            }

            // Constraint 2: Exact C0-continuous spherical cone projection (zero snap/jitter)
            float dot = Vector3DotProduct(restDirWorld, curDir);
            float maxRad = bone->maxAngleDeg * DEG2RAD;
            float cosMax = cosf(maxRad);
            if (dot < cosMax)
            {
                Vector3 perp = Vector3Subtract(curDir, Vector3Scale(restDirWorld, dot));
                float perpLen = Vector3Length(perp);
                Vector3 clampedDir;
                if (perpLen > 1e-5f)
                {
                    float sinMax = sinf(maxRad);
                    clampedDir = Vector3Add(Vector3Scale(restDirWorld, cosMax),
                                            Vector3Scale(perp, sinMax / perpLen));
                }
                else
                {
                    clampedDir = restDirWorld;
                }
                nextPos = Vector3Add(headWorld, Vector3Scale(clampedDir, currentLength));
            }

            // Constraint 3: Spherical body/torso/thigh collision
            for (int c = 0; c < sys->colliderCount; c++)
            {
                const SpringCollider *col = &sys->colliders[c];
                float colRadius = col->radius * charScale + bone->boneRadius;
                Vector3 fromCol = Vector3Subtract(nextPos, col->worldPos);
                float colDistSq = Vector3LengthSqr(fromCol);
                if (colDistSq < colRadius * colRadius && colDistSq > 1e-8f)
                {
                    float colDist = sqrtf(colDistSq);
                    Vector3 pushDir = Vector3Scale(fromCol, 1.0f / colDist);
                    nextPos = Vector3Add(col->worldPos, Vector3Scale(pushDir, colRadius));

                    Vector3 correctedDir = Vector3Normalize(Vector3Subtract(nextPos, headWorld));
                    nextPos = Vector3Add(headWorld, Vector3Scale(correctedDir, currentLength));
                }
            }

            bone->prevPos = bone->currentPos;
            bone->currentPos = nextPos;

            // Export model-space pose on final sub-step
            if (step == subSteps - 1)
            {
                bone->modelHeadPos = WorldToModelPointFast(headWorld, charPos, cosY, sinY, invScale);
                bone->modelDir = WorldToModelDirFast(Vector3Subtract(nextPos, headWorld), cosY, sinY);
            }
        }
    }

    sys->prevCharPos = charPos;
    sys->prevCharYaw = charYaw;
    sys->stamp++;
}

void SpringBone_Update(SpringBoneSystem *sys, const struct CharacterAnimState *animState,
                       Vector3 charPos, float charYaw, float charScale, float dt, Vector3 windVelocity)
{
    SpringBone_Simulate(sys, animState, charPos, charYaw, charScale, dt, windVelocity, NULL, NULL);
}

void SpringBone_UpdateWithWind(SpringBoneSystem *sys, const struct CharacterAnimState *animState,
                               Vector3 charPos, float charYaw, float charScale, float dt,
                               SpringWindFn windFn, void *userData)
{
    SpringBone_Simulate(sys, animState, charPos, charYaw, charScale, dt,
                        (Vector3){0.0f, 0.0f, 0.0f}, windFn, userData);
}

bool SpringBone_GetBoneWorldPos(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outTip)
{
    if (!sys || !boneName || !boneName[0])
        return false;
    char c0 = boneName[0];
    for (int i = 0; i < sys->boneCount; i++)
    {
        if (sys->bones[i].name[0] == c0 && strcmp(sys->bones[i].name, boneName) == 0)
        {
            if (outHead)
                *outHead = sys->bones[i].headWorldPos;
            if (outTip)
                *outTip = sys->bones[i].currentPos;
            return true;
        }
    }
    return false;
}

bool SpringBone_GetBoneModelPose(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outDir)
{
    if (!sys || !boneName || !boneName[0])
        return false;
    char c0 = boneName[0];
    for (int i = 0; i < sys->boneCount; i++)
    {
        const SpringBone *b = &sys->bones[i];
        if (!b->initialized || b->name[0] != c0 || strcmp(b->name, boneName) != 0)
            continue;
        if (outHead)
            *outHead = b->modelHeadPos;
        if (outDir)
            *outDir = b->modelDir;
        return true;
    }
    return false;
}

bool SpringBone_GetBindPose(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outDir)
{
    if (!sys || !boneName || !boneName[0])
        return false;
    char c0 = boneName[0];
    for (int i = 0; i < sys->boneCount; i++)
    {
        const SpringBone *b = &sys->bones[i];
        if (b->name[0] != c0 || strcmp(b->name, boneName) != 0)
            continue;
        if (outHead)
            *outHead = b->bindHead;
        if (outDir)
            *outDir = b->bindDir;
        return true;
    }
    return false;
}

bool SpringBone_GetBoneMatrix(const SpringBoneSystem *sys, const char *boneName, Matrix *outMatrix)
{
    if (!sys || !boneName || !boneName[0] || !outMatrix)
        return false;
    char c0 = boneName[0];
    for (int i = 0; i < sys->boneCount; i++)
    {
        const SpringBone *b = &sys->bones[i];
        if (b->name[0] == c0 && strcmp(b->name, boneName) == 0)
        {
            Vector3 dir = Vector3Subtract(b->currentPos, b->headWorldPos);
            float len = Vector3Length(dir);
            if (len < 0.0001f)
            {
                *outMatrix = MatrixTranslate(b->headWorldPos.x, b->headWorldPos.y, b->headWorldPos.z);
                return true;
            }
            Vector3 normDir = Vector3Scale(dir, 1.0f / len);
            Vector3 vFrom = (Vector3){0.0f, -1.0f, 0.0f};
            Vector3 axis = Vector3CrossProduct(vFrom, normDir);
            float angle = acosf(Clamp(Vector3DotProduct(vFrom, normDir), -1.0f, 1.0f));
            Matrix rot = MatrixIdentity();
            if (Vector3Length(axis) > 0.001f)
            {
                rot = MatrixRotate(Vector3Normalize(axis), angle);
            }
            Matrix trans = MatrixTranslate(b->headWorldPos.x, b->headWorldPos.y, b->headWorldPos.z);
            *outMatrix = MatrixMultiply(rot, trans);
            return true;
        }
    }
    return false;
}

void SpringBone_Reset(SpringBoneSystem *sys)
{
    if (!sys)
        return;
    for (int i = 0; i < sys->boneCount; i++)
    {
        sys->bones[i].initialized = false;
    }
}

void SpringBone_DrawDebug(const SpringBoneSystem *sys)
{
    if (!sys || sys->boneCount <= 0)
        return;

    for (int c = 0; c < sys->colliderCount; c++)
    {
        const SpringCollider *col = &sys->colliders[c];
        DrawSphereWires(col->worldPos, col->radius, 8, 8, (Color){70, 200, 240, 120});
    }

    for (int b = 0; b < sys->boneCount; b++)
    {
        const SpringBone *sb = &sys->bones[b];
        if (!sb->initialized)
            continue;

        DrawLine3D(sb->headWorldPos, sb->restPosWorld, (Color){255, 230, 100, 80});
        DrawLine3D(sb->headWorldPos, sb->currentPos, (Color){40, 230, 120, 240});
        DrawSphere(sb->currentPos, sb->boneRadius, (Color){255, 80, 70, 220});
        DrawSphereWires(sb->currentPos, sb->boneRadius * 1.2f, 6, 6, WHITE);
    }
}