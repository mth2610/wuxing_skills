#include "core/path_spline.h"
#include "core/geometry/sdf_capsule.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>

int main(void) {
    printf("--- Running Messiah VFX Integration Tests ---\n");

    // 1. Test Centripetal Catmull-Rom Spline
    Vector3 p0 = { 0.0f, 0.0f, 0.0f };
    Vector3 p1 = { 1.0f, 0.0f, 0.0f };
    Vector3 p2 = { 1.0f, 1.0f, 0.0f };
    Vector3 p3 = { 2.0f, 1.0f, 0.0f };

    Vector3 start = CatmullRom_Centripetal(p0, p1, p2, p3, 0.0f);
    Vector3 mid   = CatmullRom_Centripetal(p0, p1, p2, p3, 0.5f);
    Vector3 end   = CatmullRom_Centripetal(p0, p1, p2, p3, 1.0f);

    assert(fabsf(start.x - p1.x) < 1e-4f && fabsf(start.y - p1.y) < 1e-4f);
    assert(fabsf(end.x - p2.x) < 1e-4f && fabsf(end.y - p2.y) < 1e-4f);
    assert(mid.x > 0.9f && mid.x < 1.1f);
    assert(mid.y > 0.3f && mid.y < 0.7f);
    printf("[PASS] Centripetal Catmull-Rom precision & boundary match.\n");

    // Test extreme speed difference (dense points followed by distant points)
    Vector3 denseP0 = { 0.0f, 0.0f, 0.0f };
    Vector3 denseP1 = { 0.001f, 0.0f, 0.0f };
    Vector3 denseP2 = { 10.0f, 0.0f, 0.0f };
    Vector3 denseP3 = { 10.001f, 0.0f, 0.0f };

    Vector3 denseMid = CatmullRom_Centripetal(denseP0, denseP1, denseP2, denseP3, 0.5f);
    assert(!isnan(denseMid.x) && !isinf(denseMid.x));
    assert(denseMid.x >= denseP1.x && denseMid.x <= denseP2.x);
    printf("[PASS] Centripetal Catmull-Rom resists abrupt speed shock.\n");

    // 2. Test Capsule SDF
    Vector3 capA = { 0.0f, 0.0f, 0.0f };
    Vector3 capB = { 0.0f, 2.0f, 0.0f };
    float radius = 0.5f;

    // Point outside along mid-cylinder at x = 1.5 -> distance should be 1.5 - 0.5 = 1.0
    Vector3 testP1 = { 1.5f, 1.0f, 0.0f };
    float d1 = Sdf_CapsuleDistance(testP1, capA, capB, radius);
    assert(fabsf(d1 - 1.0f) < 1e-4f);

    // Point inside at (0, 1, 0) -> distance should be -0.5
    Vector3 testPInside = { 0.0f, 1.0f, 0.0f };
    float dInside = Sdf_CapsuleDistance(testPInside, capA, capB, radius);
    assert(fabsf(dInside - (-0.5f)) < 1e-4f);

    // Point above tip at (0, 3, 0) -> distance to capB (0, 2, 0) should be 1.0 - 0.5 = 0.5
    Vector3 testPTip = { 0.0f, 3.0f, 0.0f };
    float dTip = Sdf_CapsuleDistance(testPTip, capA, capB, radius);
    assert(fabsf(dTip - 0.5f) < 1e-4f);
    printf("[PASS] Capsule SDF distance calculation.\n");

    // 3. Test Humanoid Hierarchy
    SdfCapsule capsules[12];
    int count = 0;
    Vector3 root = { 0.0f, 0.0f, 0.0f };
    Sdf_BuildHumanoidHierarchy(root, 1.8f, 0.0f, capsules, 12, &count);
    assert(count == 10);

    // Point near head (0, 1.65, 0) should be inside or very close
    Vector3 normal;
    float dHead = Sdf_EvaluateHierarchyDistance((Vector3){ 0.0f, 1.65f, 0.0f }, capsules, count, 0.05f, &normal);
    assert(dHead < 0.0f); // Inside the head capsule

    // Point far away (10, 10, 10)
    float dFar = Sdf_EvaluateHierarchyDistance((Vector3){ 10.0f, 10.0f, 10.0f }, capsules, count, 0.05f, &normal);
    assert(dFar > 5.0f);
    printf("[PASS] Humanoid Capsule Hierarchy distance evaluation.\n");

    printf("--- All Messiah VFX Integration Tests PASSED ---\n");
    return 0;
}
