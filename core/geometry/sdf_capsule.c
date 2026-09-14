#include "core/geometry/sdf_capsule.h"
#include "raymath.h"
#include <math.h>

float Sdf_SmoothMin(float a, float b, float k) {
    if (k <= 0.0001f) return (a < b) ? a : b;
    float h = 0.5f + 0.5f * (b - a) / k;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    return (b * (1.0f - h) + a * h) - k * h * (1.0f - h);
}

float Sdf_CapsuleDistance(Vector3 p, Vector3 a, Vector3 b, float radius) {
    Vector3 pa = Vector3Subtract(p, a);
    Vector3 ba = Vector3Subtract(b, a);
    float baLenSq = Vector3LengthSqr(ba);
    float h = (baLenSq > 1e-6f) ? (Vector3DotProduct(pa, ba) / baLenSq) : 0.0f;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    Vector3 closest = Vector3Add(a, Vector3Scale(ba, h));
    return Vector3Distance(p, closest) - radius;
}

float Sdf_CapsuleDistanceNormal(Vector3 p, Vector3 a, Vector3 b, float radius, Vector3 *outNormal) {
    Vector3 pa = Vector3Subtract(p, a);
    Vector3 ba = Vector3Subtract(b, a);
    float baLenSq = Vector3LengthSqr(ba);
    float h = (baLenSq > 1e-6f) ? (Vector3DotProduct(pa, ba) / baLenSq) : 0.0f;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    Vector3 closest = Vector3Add(a, Vector3Scale(ba, h));
    Vector3 diff = Vector3Subtract(p, closest);
    float dist = Vector3Length(diff);
    if (outNormal) {
        *outNormal = (dist > 1e-5f) ? Vector3Scale(diff, 1.0f / dist) : (Vector3){ 0.0f, 1.0f, 0.0f };
    }
    return dist - radius;
}

static inline Vector3 RotateY(Vector3 v, float rad) {
    float c = cosf(rad);
    float s = sinf(rad);
    return (Vector3){
        v.x * c + v.z * s,
        v.y,
        -v.x * s + v.z * c
    };
}

void Sdf_BuildHumanoidHierarchy(Vector3 rootPos, float heightScale, float facingAngleDeg,
                                SdfCapsule *outCapsules, int maxCapsules, int *outCount) {
    if (!outCapsules || maxCapsules < 10) {
        if (outCount) *outCount = 0;
        return;
    }

    float scale = (heightScale > 0.1f) ? (heightScale / 1.8f) : 1.0f;
    float rad = facingAngleDeg * DEG2RAD;

    // Define local bone positions for a standard Wuxia humanoid stance
    struct BoneDef {
        Vector3 a;
        Vector3 b;
        float radius;
    } bones[10] = {
        // 0: Head
        { { 0.0f, 1.55f * scale, 0.0f }, { 0.0f, 1.75f * scale, 0.0f }, 0.16f * scale },
        // 1: Chest / Torso Upper
        { { 0.0f, 1.15f * scale, 0.0f }, { 0.0f, 1.50f * scale, 0.0f }, 0.24f * scale },
        // 2: Abdomen / Pelvis
        { { 0.0f, 0.85f * scale, 0.0f }, { 0.0f, 1.15f * scale, 0.0f }, 0.22f * scale },
        // 3: Left Upper Arm
        { { 0.28f * scale, 1.45f * scale, 0.0f }, { 0.40f * scale, 1.15f * scale, 0.0f }, 0.11f * scale },
        // 4: Left Forearm
        { { 0.40f * scale, 1.15f * scale, 0.0f }, { 0.45f * scale, 0.85f * scale, 0.10f * scale }, 0.09f * scale },
        // 5: Right Upper Arm
        { { -0.28f * scale, 1.45f * scale, 0.0f }, { -0.40f * scale, 1.15f * scale, 0.0f }, 0.11f * scale },
        // 6: Right Forearm
        { { -0.40f * scale, 1.15f * scale, 0.0f }, { -0.45f * scale, 0.85f * scale, 0.10f * scale }, 0.09f * scale },
        // 7: Left Thigh
        { { 0.14f * scale, 0.85f * scale, 0.0f }, { 0.16f * scale, 0.45f * scale, 0.0f }, 0.14f * scale },
        // 8: Left Shin
        { { 0.16f * scale, 0.45f * scale, 0.0f }, { 0.16f * scale, 0.08f * scale, 0.0f }, 0.12f * scale },
        // 9: Right Thigh
        { { -0.14f * scale, 0.85f * scale, 0.0f }, { -0.16f * scale, 0.45f * scale, 0.0f }, 0.14f * scale },
    };

    int count = (maxCapsules < 10) ? maxCapsules : 10;
    for (int i = 0; i < count; i++) {
        Vector3 rotA = RotateY(bones[i].a, rad);
        Vector3 rotB = RotateY(bones[i].b, rad);
        outCapsules[i].a = Vector3Add(rootPos, rotA);
        outCapsules[i].b = Vector3Add(rootPos, rotB);
        outCapsules[i].radius = bones[i].radius;
    }

    if (outCount) *outCount = count;
}

float Sdf_EvaluateHierarchyDistance(Vector3 p, const SdfCapsule *capsules, int count,
                                    float smoothBlend, Vector3 *outNormal) {
    if (!capsules || count <= 0) return 9999.0f;

    float d = Sdf_CapsuleDistance(p, capsules[0].a, capsules[0].b, capsules[0].radius);
    for (int i = 1; i < count; i++) {
        float di = Sdf_CapsuleDistance(p, capsules[i].a, capsules[i].b, capsules[i].radius);
        d = Sdf_SmoothMin(d, di, smoothBlend);
    }

    if (outNormal) {
        // Numerical gradient evaluation: nabla d
        const float eps = 0.005f;
        float dx1 = Sdf_CapsuleDistance((Vector3){ p.x + eps, p.y, p.z }, capsules[0].a, capsules[0].b, capsules[0].radius);
        float dx0 = Sdf_CapsuleDistance((Vector3){ p.x - eps, p.y, p.z }, capsules[0].a, capsules[0].b, capsules[0].radius);
        float dy1 = Sdf_CapsuleDistance((Vector3){ p.x, p.y + eps, p.z }, capsules[0].a, capsules[0].b, capsules[0].radius);
        float dy0 = Sdf_CapsuleDistance((Vector3){ p.x, p.y - eps, p.z }, capsules[0].a, capsules[0].b, capsules[0].radius);
        float dz1 = Sdf_CapsuleDistance((Vector3){ p.x, p.y, p.z + eps }, capsules[0].a, capsules[0].b, capsules[0].radius);
        float dz0 = Sdf_CapsuleDistance((Vector3){ p.x, p.y, p.z - eps }, capsules[0].a, capsules[0].b, capsules[0].radius);

        for (int i = 1; i < count; i++) {
            dx1 = Sdf_SmoothMin(dx1, Sdf_CapsuleDistance((Vector3){ p.x + eps, p.y, p.z }, capsules[i].a, capsules[i].b, capsules[i].radius), smoothBlend);
            dx0 = Sdf_SmoothMin(dx0, Sdf_CapsuleDistance((Vector3){ p.x - eps, p.y, p.z }, capsules[i].a, capsules[i].b, capsules[i].radius), smoothBlend);
            dy1 = Sdf_SmoothMin(dy1, Sdf_CapsuleDistance((Vector3){ p.x, p.y + eps, p.z }, capsules[i].a, capsules[i].b, capsules[i].radius), smoothBlend);
            dy0 = Sdf_SmoothMin(dy0, Sdf_CapsuleDistance((Vector3){ p.x, p.y - eps, p.z }, capsules[i].a, capsules[i].b, capsules[i].radius), smoothBlend);
            dz1 = Sdf_SmoothMin(dz1, Sdf_CapsuleDistance((Vector3){ p.x, p.y, p.z + eps }, capsules[i].a, capsules[i].b, capsules[i].radius), smoothBlend);
            dz0 = Sdf_SmoothMin(dz0, Sdf_CapsuleDistance((Vector3){ p.x, p.y, p.z - eps }, capsules[i].a, capsules[i].b, capsules[i].radius), smoothBlend);
        }

        Vector3 grad = { (dx1 - dx0) / (2.0f * eps), (dy1 - dy0) / (2.0f * eps), (dz1 - dz0) / (2.0f * eps) };
        float gLen = Vector3Length(grad);
        *outNormal = (gLen > 1e-5f) ? Vector3Scale(grad, 1.0f / gLen) : (Vector3){ 0.0f, 1.0f, 0.0f };
    }

    return d;
}
