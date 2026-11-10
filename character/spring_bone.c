// character/spring_bone.c
// Lightweight Bone-Driven Secondary Motion / Spring Bone Physics for Wuxia Garments

#include "spring_bone.h"
#include "character_model.h"
#include "raymath.h"
#include <string.h>
#include <math.h>

void SpringBone_Init(SpringBoneSystem *sys) {
    if (!sys) return;
    memset(sys, 0, sizeof(SpringBoneSystem));
    sys->initialized = false;
}

void SpringBone_SetupWuxiaDefaults(SpringBoneSystem *sys) {
    if (!sys) return;
    SpringBone_Init(sys);

    // -------------------------------------------------------------------------
    // 1. Skirt Tà Áo (4 quad quadrants: Front Left/Right, Back Left/Right)
    // -------------------------------------------------------------------------
    // Front Left Skirt
    SpringBone_Add(sys, "cloth_skirt_f_l", "mixamorig:Hips",
                   (Vector3){ 0.12f, -0.92f, 0.14f }, 0.36f, 0.22f, 0.88f, 0.75f);
    // Front Right Skirt
    SpringBone_Add(sys, "cloth_skirt_f_r", "mixamorig:Hips",
                   (Vector3){ -0.12f, -0.92f, 0.14f }, 0.36f, 0.22f, 0.88f, 0.75f);
    // Back Left Skirt
    SpringBone_Add(sys, "cloth_skirt_b_l", "mixamorig:Hips",
                   (Vector3){ 0.12f, -0.94f, -0.12f }, 0.38f, 0.20f, 0.88f, 0.75f);
    // Back Right Skirt
    SpringBone_Add(sys, "cloth_skirt_b_r", "mixamorig:Hips",
                   (Vector3){ -0.12f, -0.94f, -0.12f }, 0.38f, 0.20f, 0.88f, 0.75f);

    // -------------------------------------------------------------------------
    // 2. Bell Sleeves Tay Áo Thụng (hang down from forearms)
    // -------------------------------------------------------------------------
    SpringBone_Add(sys, "cloth_sleeve_l_01", "mixamorig:LeftForeArm",
                   (Vector3){ 0.0f, -0.95f, -0.18f }, 0.24f, 0.28f, 0.85f, 0.85f);
    SpringBone_Add(sys, "cloth_sleeve_r_01", "mixamorig:RightForeArm",
                   (Vector3){ 0.0f, -0.95f, -0.18f }, 0.24f, 0.28f, 0.85f, 0.85f);

    // -------------------------------------------------------------------------
    // 3. Waist Ribbons Dải Lụa Thắt Lưng (lightweight flutter)
    // -------------------------------------------------------------------------
    SpringBone_Add(sys, "cloth_ribbon_l", "mixamorig:Hips",
                   (Vector3){ 0.04f, -0.96f, 0.16f }, 0.30f, 0.14f, 0.84f, 0.60f);
    SpringBone_Add(sys, "cloth_ribbon_r", "mixamorig:Hips",
                   (Vector3){ -0.04f, -0.96f, 0.16f }, 0.30f, 0.14f, 0.84f, 0.60f);

    // -------------------------------------------------------------------------
    // 4. Bodily Collision Spheres (prevent skirt from penetrating thighs & hips)
    // -------------------------------------------------------------------------
    // Pelvis/Hips sphere
    SpringBone_AddCollider(sys, "mixamorig:Hips", (Vector3){ 0.0f, -0.04f, 0.0f }, 0.165f);
    // Left Thigh sphere (centered along upper leg)
    SpringBone_AddCollider(sys, "mixamorig:LeftUpLeg", (Vector3){ 0.0f, -0.18f, 0.0f }, 0.115f);
    // Right Thigh sphere (centered along upper leg)
    SpringBone_AddCollider(sys, "mixamorig:RightUpLeg", (Vector3){ 0.0f, -0.18f, 0.0f }, 0.115f);

    sys->initialized = true;
}

int SpringBone_Add(SpringBoneSystem *sys, const char *name, const char *parentBone,
                   Vector3 localRestDir, float length, float stiffness, float damping, float gravityFactor) {
    if (!sys || !name || !parentBone || sys->boneCount >= SPRING_BONE_MAX) return -1;

    int idx = sys->boneCount++;
    SpringBone *b = &sys->bones[idx];
    memset(b, 0, sizeof(SpringBone));
    strncpy(b->name, name, sizeof(b->name) - 1);
    strncpy(b->parentBone, parentBone, sizeof(b->parentBone) - 1);

    b->localRestDir = Vector3Normalize(localRestDir);
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

int SpringBone_AddCollider(SpringBoneSystem *sys, const char *boneName, Vector3 offset, float radius) {
    if (!sys || !boneName || sys->colliderCount >= SPRING_COLLIDER_MAX) return -1;

    int idx = sys->colliderCount++;
    SpringCollider *c = &sys->colliders[idx];
    c->type = SPRING_COLLIDER_SPHERE;
    strncpy(c->boneName, boneName, sizeof(c->boneName) - 1);
    c->localOffset = offset;
    c->radius = (radius > 0.01f) ? radius : 0.10f;
    c->worldPos = (Vector3){ 0 };

    return idx;
}

static Vector3 TransformPointByModelAndWorld(Vector3 localPt, Matrix modelBoneMat, Vector3 charPos, float charYaw, float charScale) {
    // 1. Transform by bone in model space
    Vector3 modelPt = Vector3Transform(localPt, modelBoneMat);
    // 2. Scale
    modelPt = Vector3Scale(modelPt, charScale);
    // 3. Rotate around Y (charYaw)
    float cosY = cosf(charYaw);
    float sinY = sinf(charYaw);
    Vector3 rotPt = {
        modelPt.x * cosY + modelPt.z * sinY,
        modelPt.y,
        -modelPt.x * sinY + modelPt.z * cosY
    };
    // 4. Translate by charPos
    return Vector3Add(rotPt, charPos);
}

static Vector3 TransformDirByModelAndWorld(Vector3 localDir, Matrix modelBoneMat, float charYaw) {
    // Rotate by bone orientation matrix (ignore translation)
    Matrix rotOnly = modelBoneMat;
    rotOnly.m12 = 0.0f; rotOnly.m13 = 0.0f; rotOnly.m14 = 0.0f;
    Vector3 modelDir = Vector3Transform(localDir, rotOnly);
    modelDir = Vector3Normalize(modelDir);

    float cosY = cosf(charYaw);
    float sinY = sinf(charYaw);
    Vector3 worldDir = {
        modelDir.x * cosY + modelDir.z * sinY,
        modelDir.y,
        -modelDir.x * sinY + modelDir.z * cosY
    };
    return Vector3Normalize(worldDir);
}

void SpringBone_Update(SpringBoneSystem *sys, const struct CharacterAnimState *animState,
                       Vector3 charPos, float charYaw, float charScale, float dt, Vector3 windVelocity) {
    if (!sys || sys->boneCount <= 0) return;

    // Guard dt against hitch spikes (clamp to [0.002s, 0.05s])
    float safeDt = Clamp(dt, 0.002f, 0.050f);

    // 1. Update bodily collision spheres in world space
    for (int c = 0; c < sys->colliderCount; c++) {
        SpringCollider *col = &sys->colliders[c];
        Matrix boneMat;
        if (CharacterModel_GetBoneTransform(col->boneName, animState, &boneMat)) {
            col->worldPos = TransformPointByModelAndWorld(col->localOffset, boneMat, charPos, charYaw, charScale);
        } else {
            // Fallback: estimate from root
            col->worldPos = Vector3Add(charPos, (Vector3){ 0.0f, 0.90f * charScale, 0.0f });
        }
    }

    // 2. Update each spring bone
    for (int b = 0; b < sys->boneCount; b++) {
        SpringBone *bone = &sys->bones[b];

        Matrix parentMat;
        bool hasParent = CharacterModel_GetBoneTransform(bone->parentBone, animState, &parentMat);
        if (!hasParent) {
            parentMat = MatrixIdentity();
        }

        // Anchor head in world space
        Vector3 headWorld = TransformPointByModelAndWorld((Vector3){ 0, 0, 0 }, parentMat, charPos, charYaw, charScale);
        // Rest direction in world space
        Vector3 restDirWorld = TransformDirByModelAndWorld(bone->localRestDir, parentMat, charYaw);
        float currentLength = bone->length * charScale;
        Vector3 restPosWorld = Vector3Add(headWorld, Vector3Scale(restDirWorld, currentLength));

        // First frame initialization
        if (!bone->initialized) {
            bone->headWorldPos = headWorld;
            bone->currentPos = restPosWorld;
            bone->prevPos = restPosWorld;
            bone->restPosWorld = restPosWorld;
            bone->initialized = true;
            continue;
        }

        bone->headWorldPos = headWorld;
        bone->restPosWorld = restPosWorld;

        // --- Calculate Physical Forces ---
        // A. Restoring spring pull toward rest pose
        Vector3 toRest = Vector3Subtract(restPosWorld, bone->currentPos);
        Vector3 springForce = Vector3Scale(toRest, bone->stiffness * 80.0f);

        // B. Gravity (9.81 m/s^2)
        Vector3 gravityForce = (Vector3){ 0.0f, -9.81f * bone->gravityFactor, 0.0f };

        // C. Environmental Wind Force
        Vector3 windForce = Vector3Scale(windVelocity, bone->windInfluence * 1.8f);

        Vector3 totalAccel = Vector3Add(Vector3Add(springForce, gravityForce), windForce);

        // --- Verlet Numerical Integration ---
        Vector3 inertia = Vector3Scale(Vector3Subtract(bone->currentPos, bone->prevPos), bone->damping);
        Vector3 nextPos = Vector3Add(Vector3Add(bone->currentPos, inertia), Vector3Scale(totalAccel, safeDt * safeDt));

        // --- Constraint 1: Distance Constraint (Rigid Bone Length) ---
        Vector3 toTip = Vector3Subtract(nextPos, headWorld);
        float dist = Vector3Length(toTip);
        if (dist > 0.0001f) {
            toTip = Vector3Scale(Vector3Normalize(toTip), currentLength);
            nextPos = Vector3Add(headWorld, toTip);
        } else {
            nextPos = restPosWorld;
        }

        // --- Constraint 2: Angular Cone Constraint ---
        Vector3 curDir = Vector3Normalize(Vector3Subtract(nextPos, headWorld));
        float dot = Vector3DotProduct(restDirWorld, curDir);
        float cosMax = cosf(bone->maxAngleDeg * DEG2RAD);
        if (dot < cosMax) {
            // Slerp or clamp towards restDirWorld
            Vector3 clampedDir = Vector3Normalize(Vector3Add(Vector3Scale(curDir, 0.5f), Vector3Scale(restDirWorld, 0.5f)));
            nextPos = Vector3Add(headWorld, Vector3Scale(clampedDir, currentLength));
        }

        // --- Constraint 3: Spherical Collision Against Body/Thighs ---
        for (int c = 0; c < sys->colliderCount; c++) {
            const SpringCollider *col = &sys->colliders[c];
            float colRadius = col->radius * charScale + bone->boneRadius;
            Vector3 fromCol = Vector3Subtract(nextPos, col->worldPos);
            float colDist = Vector3Length(fromCol);
            if (colDist < colRadius && colDist > 0.0001f) {
                // Push tip outside collision sphere
                Vector3 pushDir = Vector3Normalize(fromCol);
                nextPos = Vector3Add(col->worldPos, Vector3Scale(pushDir, colRadius));

                // Re-enforce rigid bone length constraint from head
                Vector3 correctedToTip = Vector3Normalize(Vector3Subtract(nextPos, headWorld));
                nextPos = Vector3Add(headWorld, Vector3Scale(correctedToTip, currentLength));
            }
        }

        // Update state
        bone->prevPos = bone->currentPos;
        bone->currentPos = nextPos;
    }

    sys->prevCharPos = charPos;
    sys->prevCharYaw = charYaw;
}

bool SpringBone_GetBoneWorldPos(const SpringBoneSystem *sys, const char *boneName, Vector3 *outHead, Vector3 *outTip) {
    if (!sys || !boneName) return false;
    for (int i = 0; i < sys->boneCount; i++) {
        if (strcmp(sys->bones[i].name, boneName) == 0) {
            if (outHead) *outHead = sys->bones[i].headWorldPos;
            if (outTip)  *outTip  = sys->bones[i].currentPos;
            return true;
        }
    }
    return false;
}

bool SpringBone_GetBoneMatrix(const SpringBoneSystem *sys, const char *boneName, Matrix *outMatrix) {
    if (!sys || !boneName || !outMatrix) return false;
    for (int i = 0; i < sys->boneCount; i++) {
        const SpringBone *b = &sys->bones[i];
        if (strcmp(b->name, boneName) == 0) {
            Vector3 dir = Vector3Subtract(b->currentPos, b->headWorldPos);
            float len = Vector3Length(dir);
            if (len < 0.0001f) {
                *outMatrix = MatrixTranslate(b->headWorldPos.x, b->headWorldPos.y, b->headWorldPos.z);
                return true;
            }
            Vector3 normDir = Vector3Normalize(dir);
            // Construct orientation matrix from -Y (default down) to normDir
            Vector3 vFrom = (Vector3){ 0.0f, -1.0f, 0.0f };
            Vector3 axis = Vector3CrossProduct(vFrom, normDir);
            float angle = acosf(Clamp(Vector3DotProduct(vFrom, normDir), -1.0f, 1.0f));
            Matrix rot = MatrixIdentity();
            if (Vector3Length(axis) > 0.001f) {
                rot = MatrixRotate(Vector3Normalize(axis), angle);
            }
            Matrix trans = MatrixTranslate(b->headWorldPos.x, b->headWorldPos.y, b->headWorldPos.z);
            *outMatrix = MatrixMultiply(rot, trans);
            return true;
        }
    }
    return false;
}

void SpringBone_Reset(SpringBoneSystem *sys) {
    if (!sys) return;
    for (int i = 0; i < sys->boneCount; i++) {
        sys->bones[i].initialized = false;
    }
}

void SpringBone_DrawDebug(const SpringBoneSystem *sys) {
    if (!sys || sys->boneCount <= 0) return;

    // Draw bodily colliders in translucent cyan
    for (int c = 0; c < sys->colliderCount; c++) {
        const SpringCollider *col = &sys->colliders[c];
        DrawSphereWires(col->worldPos, col->radius, 8, 8, (Color){ 70, 200, 240, 120 });
    }

    // Draw spring bones
    for (int b = 0; b < sys->boneCount; b++) {
        const SpringBone *sb = &sys->bones[b];
        if (!sb->initialized) continue;

        // Rest line (faint yellow)
        DrawLine3D(sb->headWorldPos, sb->restPosWorld, (Color){ 255, 230, 100, 80 });

        // Dynamic active cloth bone line (vibrant emerald green)
        DrawLine3D(sb->headWorldPos, sb->currentPos, (Color){ 40, 230, 120, 240 });

        // Tip particle sphere
        DrawSphere(sb->currentPos, sb->boneRadius, (Color){ 255, 80, 70, 220 });
        DrawSphereWires(sb->currentPos, sb->boneRadius * 1.2f, 6, 6, WHITE);
    }
}
