#define CORE_GRANULAR_HEADLESS_TEST 1
#include "core/granular/granular_dem_cpu.inl"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_inelastic_collision(void)
{
    GranularGrainState grains[2] = {0};
    const GranularMaterialProfile *sand = GranularSim_GetMaterialProfile(GRANULAR_MAT_DESERT_SAND);

    grains[0].position = (Vector3){-0.06f, 1.0f, 0.0f};
    grains[0].velocity = (Vector3){ 2.0f,  0.0f, 0.0f};
    grains[0].radius   = 0.04f;
    grains[0].lifetime = 5.0f;

    grains[1].position = (Vector3){ 0.06f, 1.0f, 0.0f};
    grains[1].velocity = (Vector3){-2.0f,  0.0f, 0.0f};
    grains[1].radius   = 0.04f;
    grains[1].lifetime = 5.0f;

    for (int step = 0; step < 30; step++) {
        GranularSim_StepCpuDeterministic(grains, 2, sand, 1.0f / 60.0f,
                                         (Vector3){0.0f, 0.0f, 0.0f},
                                         (Vector3){0.0f, 1.0f, 0.0f});
    }

    float sep = fabsf(grains[1].position.x - grains[0].position.x);
    float relVx = fabsf(grains[1].velocity.x - grains[0].velocity.x);
    assert(sep >= 0.075f); /* No interpenetration */
    assert(relVx < 0.80f); /* >80% inelastic energy dissipation */
    printf("[PASS] test_inelastic_collision (sep=%.4f m, relVx=%.4f m/s)\n", sep, relVx);
}

static void test_angle_of_repose_pile_vs_frictionless(void)
{
    enum { N = 192 };
    GranularGrainState sandGrains[N];
    GranularGrainState smoothGrains[N];
    GranularMaterialProfile sand = *GranularSim_GetMaterialProfile(GRANULAR_MAT_DESERT_SAND);
    GranularMaterialProfile frictionless = sand;
    frictionless.shearFrictionMu = 0.0f;
    frictionless.rollingResistance = 0.0f;

    const float r = 0.045f;
    const float spacing = r * 2.15f;
    for (int i = 0; i < N; i++) {
        int layer = i / 16;
        int cell = i % 16;
        int gx = cell % 4;
        int gz = cell / 4;
        float jx = ((layer % 2) * 0.35f - 0.175f) * r;
        float jz = (((layer / 2) % 2) * 0.35f - 0.175f) * r;
        Vector3 p = {
            (gx - 1.5f) * spacing + jx,
            0.08f + (float)layer * spacing,
            (gz - 1.5f) * spacing + jz
        };

        sandGrains[i] = (GranularGrainState){
            .position = p, .velocity = {0.0f, -0.5f, 0.0f},
            .radius = r, .lifetime = 10.0f, .seed = (float)i / (float)N
        };
        smoothGrains[i] = sandGrains[i];
    }

    float totalShear = 0.0f;
    for (int step = 0; step < 180; step++) {
        totalShear += GranularSim_StepCpuDeterministic(
            sandGrains, N, &sand, 1.0f / 60.0f,
            (Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f});
        GranularSim_StepCpuDeterministic(
            smoothGrains, N, &frictionless, 1.0f / 60.0f,
            (Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f});
    }

    float maxSandY = 0.0f, maxSmoothY = 0.0f;
    float maxSandAO = 0.0f;
    for (int i = 0; i < N; i++) {
        if (sandGrains[i].position.y > maxSandY) maxSandY = sandGrains[i].position.y;
        if (smoothGrains[i].position.y > maxSmoothY) maxSmoothY = smoothGrains[i].position.y;
        if (sandGrains[i].contactAO > maxSandAO) maxSandAO = sandGrains[i].contactAO;
    }

    printf("  sandPeak=%.4f m (%.2f r), smoothPeak=%.4f m (%.2f r), shear=%.2f, maxAO=%.2f\n",
           maxSandY, maxSandY / r, maxSmoothY, maxSmoothY / r, totalShear, maxSandAO);
    assert(maxSandY > 4.5f * r);       /* 3D load-bearing sand pile */
    assert(maxSandY > maxSmoothY + r); /* Friction holds taller pile than mu=0 */
    assert(totalShear > 0.0f);         /* Positive shear work for dust entrainment */
    assert(maxSandAO > 0.2f);          /* Packing AO populated for crevice shading */
    printf("[PASS] test_angle_of_repose_pile_vs_frictionless (sandPeak=%.3f m, smoothPeak=%.3f m, shear=%.2f, maxAO=%.2f)\n",
           maxSandY, maxSmoothY, totalShear, maxSandAO);
}

static void test_tornado_centripetal_equilibrium(void)
{
    enum { N = 96 };
    GranularGrainState grains[N];
    const GranularMaterialProfile *sand = GranularSim_GetMaterialProfile(GRANULAR_MAT_DESERT_SAND);
    const float baseR = 0.22f, topR = 1.35f, height = 3.60f, swirl = 7.5f, updraft = 3.8f;

    for (int i = 0; i < N; i++) {
        float u = (float)i / (float)N;
        float angle = u * 6.2831853f * 5.0f;
        float hn = u;
        float coneR = baseR + (topR - baseR) * powf(hn, 1.35f);
        float r = coneR * (0.25f + 0.70f * fmodf(u * 7.3f, 1.0f));
        grains[i] = (GranularGrainState){
            .position = {cosf(angle) * r, 0.1f + hn * height, sinf(angle) * r},
            .velocity = {-sinf(angle) * swirl * 0.7f, updraft, cosf(angle) * swirl * 0.7f},
            .radius = 0.02f, .lifetime = 5.0f, .seed = u
        };
    }

    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 120; step++) {
        float t = (float)step * dt;
        for (int i = 0; i < N; i++) {
            float h = grains[i].position.y;
            if (h > height) {
                grains[i].position.y = 0.05f;
                h = 0.05f;
            }
            float hn = fminf(fmaxf(h / height, 0.0f), 1.0f);
            float coneR = baseR + (topR - baseR) * powf(hn, 1.35f);
            float rBias = 0.18f + 0.87f * fmodf(grains[i].seed * 11.37f, 1.0f);
            float targetR = coneR * rBias;
            float rx = grains[i].position.x, rz = grains[i].position.z;
            float r = sqrtf(rx * rx + rz * rz);
            float radX = r > 1e-4f ? rx / r : 1.0f;
            float radZ = r > 1e-4f ? rz / r : 0.0f;
            float tangX = -radZ, tangZ = radX;
            float vTang = grains[i].velocity.x * tangX + grains[i].velocity.z * tangZ;
            float vRad = grains[i].velocity.x * radX + grains[i].velocity.z * radZ;
            float rRatio = fminf(fmaxf(r / fmaxf(coneR, 0.15f), 0.25f), 1.0f);
            float tangAcc = (swirl * rRatio - vTang) * 10.5f;
            float centripetal = -(vTang * vTang) / fmaxf(r, 0.08f);
            float radAcc = centripetal + (targetR - r) * 55.0f - vRad * 11.0f;
            grains[i].velocity.x += (radX * radAcc + tangX * tangAcc) * dt;
            grains[i].velocity.y += (9.81f + (updraft - grains[i].velocity.y) * 7.0f) * dt;
            grains[i].velocity.z += (radZ * radAcc + tangZ * tangAcc) * dt;
        }
        GranularSim_StepCpuDeterministic(grains, N, sand, dt, (Vector3){0}, (Vector3){0, 1, 0});
        (void)t;
    }

    float maxR = 0.0f, meanR = 0.0f;
    for (int i = 0; i < N; i++) {
        float r = hypotf(grains[i].position.x, grains[i].position.z);
        meanR += r;
        if (r > maxR) maxR = r;
    }
    meanR /= (float)N;
    assert(meanR < topR * 0.85f);  /* Solid interior, not blown outward */
    assert(maxR < topR * 1.25f);   /* No centrifugal ring blow-up */
    printf("[PASS] test_tornado_centripetal_equilibrium (meanR=%.3f m, maxR=%.3f m)\n", meanR, maxR);
}

int main(void)
{
    test_inelastic_collision();
    test_angle_of_repose_pile_vs_frictionless();
    test_tornado_centripetal_equilibrium();
    printf("granular_sim_test: ALL PASSED\n");
    return 0;
}
