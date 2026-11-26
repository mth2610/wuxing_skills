// Standalone headless test for core/motion/sheet_aerodynamics.{h,c}
// Tests 6-DOF thin-sheet aerodynamics:
//   1. Anisotropic drag + thin-plate lift slows fall vs ballistic drop
//   2. Shifting center-of-pressure induces pitch-up stall & leaf-fall rocking
//   3. Ground contact settles flat (|N.y| -> 1) and high wind peels sheet back up
//   4. Spring-damper deformation & finite-difference normals stay finite and unit-length
//   5. GLSL mirror shader (core/shaders/common/sheet_deform.glsl) retains load-bearing expressions

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "core/motion/sheet_aerodynamics.c"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, name) do { \
    g_checks++; \
    if (cond) printf("PASS: %s\n", name); \
    else { printf("FAIL: %s\n", name); g_failures++; } \
} while (0)

static bool IsFiniteVec3(Vector3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static Vector4 QuatAxisAngle(Vector3 axis, float rad) {
    float s = sinf(rad * 0.5f);
    return (Vector4){ axis.x * s, axis.y * s, axis.z * s, cosf(rad * 0.5f) };
}

int main(void) {
    // 1. Falling paper experiences strong normal drag & slower terminal speed than free fall
    {
        SheetAeroParams p = SheetAero_DefaultPaper();
        p.enableFloor = false;
        SheetAeroState s;
        Vector4 qFlat = QuatAxisAngle((Vector3){1.0f, 0.0f, 0.0f}, -PI * 0.5f);
        SheetAero_InitState(&s, (Vector3){0.0f, 10.0f, 0.0f}, qFlat, 1.0f, 0.05f, 1.0f);

        for (int k = 0; k < 120; k++) {
            SheetAero_Step(&s, &p, (Vector3){0.0f, 0.0f, 0.0f}, 0.0f, 1.0f / 60.0f, k / 60.0f);
        }
        float ballisticFall = 0.5f * p.gravity * 2.0f * 2.0f;
        float actualFall = 10.0f - s.pos.y;
        CHECK(IsFiniteVec3(s.pos) && IsFiniteVec3(s.vel), "free-fall state stays finite");
        CHECK(actualFall < ballisticFall * 0.55f, "anisotropic face drag slows fall vs ballistic");
    }

    // 2. Knife-edge flick throw pitches nose up into stall & excites S-bend/flutter
    {
        SheetAeroParams p = SheetAero_DefaultPaper();
        p.enableFloor = false;
        Vector4 q;
        Vector3 side, lead, norm;
        SheetAero_ComputeLaunchBasis((Vector3){1.0f, 0.15f, 0.0f}, 0.15f, &q, &side, &lead, &norm);
        Vector3 bR, bU, bN;
        SheetAero_ExtractBasis(q, &bR, &bU, &bN);
        float err = fabsf(bR.x - side.x) + fabsf(bR.y - side.y) + fabsf(bR.z - side.z)
                  + fabsf(bU.x - lead.x) + fabsf(bU.y - lead.y) + fabsf(bU.z - lead.z)
                  + fabsf(bN.x - norm.x) + fabsf(bN.y - norm.y) + fabsf(bN.z - norm.z);
        CHECK(err < 1e-3f, "launch basis quaternion round-trips accurately");

        SheetAeroState s;
        SheetAero_InitState(&s, (Vector3){0.0f, 2.0f, 0.0f}, q, 1.0f, 0.04f, 7.0f);
        s.vel = (Vector3){ lead.x * 14.0f, lead.y * 14.0f, lead.z * 14.0f };
        s.angVel = (Vector3){ side.x * 1.1f, side.y * 1.1f, side.z * 1.1f };

        float maxFlutter = 0.0f;
        float maxAbsSBend = 0.0f;
        float maxPosX = 0.0f;
        for (int k = 0; k < 90; k++) {
            SheetAero_Step(&s, &p, (Vector3){0.0f, 0.0f, 0.0f}, 0.0f, 1.0f / 60.0f, k / 60.0f);
            if (s.flutterAmp > maxFlutter) maxFlutter = s.flutterAmp;
            if (fabsf(s.sBend) > maxAbsSBend) maxAbsSBend = fabsf(s.sBend);
            if (s.pos.x > maxPosX) maxPosX = s.pos.x;
        }
        CHECK(maxPosX > 2.0f, "knife-edge throw travels forward cleanly before stall");
        CHECK(maxFlutter > 0.02f, "high-speed throw excites trailing-edge flutter");
        CHECK(maxAbsSBend > 0.05f, "stall torque excites S-curve camber");
    }

    // 3. Floor contact settles flat and strong wind peels sheet back up
    {
        SheetAeroParams p = SheetAero_DefaultPaper();
        p.enableFloor = true;
        p.floorY = 0.0f;
        SheetAeroState s;
        Vector4 qTilt = QuatAxisAngle((Vector3){1.0f, 0.0f, 0.0f}, 0.85f);
        SheetAero_InitState(&s, (Vector3){0.0f, 0.35f, 0.0f}, qTilt, 1.0f, 0.05f, 2.0f);

        for (int k = 0; k < 180; k++) {
            SheetAero_Step(&s, &p, (Vector3){0.0f, 0.0f, 0.0f}, 0.0f, 1.0f / 60.0f, k / 60.0f);
        }
        Vector3 R, U, N;
        SheetAero_ExtractBasis(s.quat, &R, &U, &N);
        CHECK(s.onFloor && s.restTime > 0.5f, "falling sheet contacts and rests on floor");
        CHECK(fabsf(N.y) > 0.90f, "settled sheet aligns flat against ground plane");

        float settledY = s.pos.y;
        for (int k = 0; k < 45; k++) {
            SheetAero_Step(&s, &p, (Vector3){4.2f, 0.2f, 0.0f}, 2.0f, 1.0f / 60.0f, 3.0f + k / 60.0f);
        }
        CHECK(s.pos.y > settledY + 0.08f, "strong wind gust peels settled sheet off floor");
    }

    // 4. SheetAero_DeformPoint produces finite positions and unit-length normals
    {
        Vector3 outP, outN;
        SheetAero_DeformPoint((Vector3){0.12f, -0.35f, 0.0f}, 0.2175f, 0.55f,
                              0.45f, -0.30f, 0.50f, 0.20f, 0.06f, 2.5f, &outP, &outN);
        float nLen = SheetAero_VecLen(outN);
        CHECK(IsFiniteVec3(outP) && IsFiniteVec3(outN), "deformed point & normal are finite");
        CHECK(fabsf(nLen - 1.0f) < 1e-4f, "finite-difference deformed normal is unit length");
    }

    // 5. Verify GLSL mirror still carries load-bearing expressions
    {
        FILE *fp = fopen("core/shaders/common/sheet_deform.glsl", "r");
        CHECK(fp != NULL, "core/shaders/common/sheet_deform.glsl exists");
        if (fp) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
            buf[n] = '\0';
            fclose(fp);
            CHECK(strstr(buf, "Sheet_DeformPos") != NULL &&
                  strstr(buf, "Sheet_DeformPoint") != NULL &&
                  strstr(buf, "archShrink") != NULL,
                  "GLSL sheet_deform mirror contains load-bearing functions");
        }
    }

    printf("\nSummary: %d/%d checks passed.\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
