#ifndef SDF_CAPSULE_H
#define SDF_CAPSULE_H

#include "raylib.h"
#include <stdbool.h>

// ============================================================
// WUXING — Analytical Capsule Signed Distance Fields (SDF)
//
// Messiah Engine / Where Winds Meet standard for body-hugging
// auras (chân khí hộ thể) and high-speed particle collisions
// without expensive triangle mesh raycasting.
// ============================================================

#define SDF_MAX_HUMANOID_CAPSULES 12

typedef struct SdfCapsule {
    Vector3 a;      // Segment start point (World space)
    Vector3 b;      // Segment end point (World space)
    float   radius; // Capsule thickness radius
} SdfCapsule;

// Polynomial smooth minimum (blends adjacent capsule geometries seamlessly)
float Sdf_SmoothMin(float d1, float d2, float k);

// Khoảng cách có dấu từ điểm p đến 1 Capsule (d < 0 là bên trong, d > 0 là bên ngoài)
float Sdf_CapsuleDistance(Vector3 p, Vector3 a, Vector3 b, float radius);

// Khoảng cách có dấu kèm Vector pháp tuyến bề mặt (outNormal)
float Sdf_CapsuleDistanceNormal(Vector3 p, Vector3 a, Vector3 b, float radius, Vector3 *outNormal);

// Xây dựng khung Capsule xấp xỉ hình thể nhân vật từ vị trí và chiều cao
void Sdf_BuildHumanoidHierarchy(Vector3 rootPos, float heightScale, float facingAngleDeg,
                                SdfCapsule *outCapsules, int maxCapsules, int *outCount);

// Đánh giá khoảng cách ngắn nhất từ điểm p tới toàn bộ tập hợp Capsule của nhân vật
float Sdf_EvaluateHierarchyDistance(Vector3 p, const SdfCapsule *capsules, int count,
                                    float smoothBlend, Vector3 *outNormal);

#endif // SDF_CAPSULE_H
