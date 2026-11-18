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

static void test_toroidal_grid_far_travel_collision(void)
{
    GranularGrainState grains[2] = {0};
    const GranularMaterialProfile *sand = GranularSim_GetMaterialProfile(GRANULAR_MAT_DESERT_SAND);

    grains[0].position = (Vector3){11.94f, 1.0f, 8.0f};
    grains[0].velocity = (Vector3){ 2.0f,  0.0f, 0.0f};
    grains[0].radius   = 0.04f;
    grains[0].lifetime = 5.0f;

    grains[1].position = (Vector3){12.06f, 1.0f, 8.0f};
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
    assert(sep >= 0.075f); /* Toroidal grid resolves DEM contact 12m from origin */
    assert(relVx < 0.80f);
    printf("[PASS] test_toroidal_grid_far_travel_collision (sep=%.4f m, relVx=%.4f m/s)\n", sep, relVx);
}

static void test_mass_and_morphology_entrainment_sorting(void)
{
    enum { N = 3 };
    GranularGrainState grains[N] = {
        {.position = {1.2f, 0.03f, 0.0f}, .radius = 0.03f, .lifetime = 6.0f, .seed = 0.17f,
         .materialId = GRANULAR_MAT_FLOWER_PETAL, .flags = 1u},
        {.position = {1.2f, 0.03f, 0.3f}, .radius = 0.03f, .lifetime = 6.0f, .seed = 0.43f,
         .materialId = GRANULAR_MAT_DRY_LEAF, .flags = 1u},
        {.position = {1.2f, 0.03f, -0.3f}, .radius = 0.03f, .lifetime = 6.0f, .seed = 0.71f,
         .materialId = GRANULAR_MAT_COARSE_GRAVEL, .flags = 1u}
    };

    GranularWaveDesc wave = {
        .kind         = GRANULAR_WAVE_SPIRAL_CONVERGE,
        .origin       = {0.0f, 0.15f, 0.0f},
        .direction    = {0.0f, 1.0f, 0.0f},
        .radius       = 3.0f,
        .swirlSpeed   = 4.5f,
        .inwardPull   = 4.0f,
        .liftAccel    = 4.5f,
        .analyticLock = 0.80f,
        .duration     = 2.0f,
        .attackTime   = 0.05f,
        .fadeTime     = 0.2f
    };

    float dt = 1.0f / 60.0f;
    for (int step = 0; step < 30; step++) {
        GranularSim_StepCpuDeterministicWithWaves(
            grains, N, NULL, &wave, 1, dt, (float)step * dt,
            (Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f});
    }

    /* Petals and dry leaves entrain & lift; heavy gravel stays on ground */
    assert(grains[0].flags == 0u && grains[0].position.y > 0.20f);
    assert(grains[1].flags == 0u && grains[1].position.y > 0.15f);
    assert(grains[2].flags != 0u && grains[2].position.y < 0.05f);
    printf("[PASS] test_mass_and_morphology_entrainment_sorting (petalY=%.3f m, leafY=%.3f m, gravelY=%.3f m)\n",
           grains[0].position.y, grains[1].position.y, grains[2].position.y);
}

static void test_progressive_palm_wave_sweep(void)
{
    enum { N = 3 };
    GranularGrainState leaves[N] = {
        {.position = {1.0f, 0.03f, 0.0f}, .radius = 0.03f, .lifetime = 6.0f, .seed = 0.21f,
         .materialId = GRANULAR_MAT_DRY_LEAF, .flags = 1u},
        {.position = {3.0f, 0.03f, 0.0f}, .radius = 0.03f, .lifetime = 6.0f, .seed = 0.52f,
         .materialId = GRANULAR_MAT_DRY_LEAF, .flags = 1u},
        {.position = {5.0f, 0.03f, 0.0f}, .radius = 0.03f, .lifetime = 6.0f, .seed = 0.83f,
         .materialId = GRANULAR_MAT_DRY_LEAF, .flags = 1u}
    };

    GranularWaveDesc jet = {
        .kind         = GRANULAR_WAVE_TRAVELLING_JET,
        .origin       = {0.0f, 0.35f, 0.0f},
        .direction    = {1.0f, 0.0f, 0.0f},
        .radius       = 1.2f,
        .topRadius    = 2.0f,
        .length       = 10.0f,
        .axialSpeed   = 10.0f,
        .waveSpeed    = 8.0f,
        .swirlSpeed   = 5.0f,
        .liftAccel    = 9.0f,
        .analyticLock = 0.85f,
        .duration     = 2.0f,
        .attackTime   = 0.02f,
        .fadeTime     = 0.2f
    };

    float liftTime[3] = {-1.0f, -1.0f, -1.0f};
    float dt = 1.0f / 60.0f;
    for (int step = 0; step < 60; step++) {
        float t = (float)step * dt;
        GranularSim_StepCpuDeterministicWithWaves(
            leaves, N, NULL, &jet, 1, dt, t,
            (Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f});
        for (int i = 0; i < N; i++) {
            if (liftTime[i] < 0.0f && leaves[i].flags == 0u) liftTime[i] = t;
        }
    }

    assert(liftTime[0] >= 0.0f && liftTime[1] > liftTime[0] && liftTime[2] > liftTime[1]);
    printf("[PASS] test_progressive_palm_wave_sweep (liftTimes: 1m=%.3fs, 3m=%.3fs, 5m=%.3fs)\n",
           liftTime[0], liftTime[1], liftTime[2]);
}

static void test_mixed_material_bed_simulation(void)
{
    const GranularMaterialId mats[6] = {
        GRANULAR_MAT_DRY_LEAF, GRANULAR_MAT_FRESH_LEAF, GRANULAR_MAT_DRY_TWIG,
        GRANULAR_MAT_FLOWER_PETAL, GRANULAR_MAT_ICE_SHARD, GRANULAR_MAT_SNOW_POWDER
    };
    GranularGrainState grains[6] = {0};
    for (int i = 0; i < 6; i++) {
        grains[i] = (GranularGrainState){
            .position = {(float)i * 0.25f, 0.50f, 0.0f},
            .velocity = {0.0f, -0.5f, 0.0f},
            .radius = 0.03f,
            .lifetime = 5.0f,
            .seed = 0.15f * (float)(i + 1),
            .materialId = (uint32_t)mats[i],
            .flags = 0u
        };
    }

    float dt = 1.0f / 60.0f;
    for (int step = 0; step < 90; step++) {
        GranularSim_StepCpuDeterministicWithWaves(
            grains, 6, NULL, NULL, 0, dt, (float)step * dt,
            (Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f});
    }

    /* Ice shard (heavy, low drag) settles faster than light flower petal (high lift & drag) */
    assert(grains[4].position.y <= grains[3].position.y + 0.05f);
    for (int i = 0; i < 6; i++) {
        assert(isfinite(grains[i].position.x) && isfinite(grains[i].position.y) && isfinite(grains[i].position.z));
    }
    printf("[PASS] test_mixed_material_bed_simulation (iceY=%.3f m, petalY=%.3f m)\n",
           grains[4].position.y, grains[3].position.y);
}

static void test_moving_tornado_cohesive_translation(void)
{
    enum { N = 64 };
    GranularGrainState grains[N];
    const float baseR = 0.28f, topR = 1.25f, height = 3.2f, swirl = 10.5f, updraft = 3.2f;
    const float stormSpeedX = 6.0f;

    for (int i = 0; i < N; i++) {
        float u = (float)i / (float)N;
        float angle = u * 6.2831853f * 4.0f;
        float hn = u;
        float coneR = baseR + (topR - baseR) * powf(hn, 1.35f);
        float r = coneR * 0.65f;
        grains[i] = (GranularGrainState){
            .position = {cosf(angle) * r, 0.1f + hn * height, sinf(angle) * r},
            .velocity = {stormSpeedX - sinf(angle) * swirl * 0.7f, updraft, cosf(angle) * swirl * 0.7f},
            .radius = 0.02f, .lifetime = 8.0f, .seed = u, .materialId = GRANULAR_MAT_DESERT_SAND
        };
    }

    GranularWaveDesc funnel = {
        .kind          = GRANULAR_WAVE_CONICAL_FUNNEL,
        .origin        = {0.0f, 0.0f, 0.0f},
        .radius        = baseR,
        .topRadius     = topR,
        .length        = height,
        .axialSpeed    = updraft,
        .swirlSpeed    = swirl,
        .duration      = 4.0f,
        .attackTime    = 0.02f,
        .fadeTime      = 0.50f,
        .loopTop       = true,
        .stormVelocity = {stormSpeedX, 0.0f, 0.0f},
        .spinDown      = 0.0f
    };

    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 60; step++) {
        float t = (float)step * dt;
        funnel.origin.x = stormSpeedX * t;
        GranularSim_StepCpuDeterministicWithWaves(
            grains, N, NULL, &funnel, 1, dt, t,
            funnel.origin, (Vector3){0.0f, 1.0f, 0.0f});
    }

    float meanX = 0.0f, maxDistFromCenter = 0.0f;
    for (int i = 0; i < N; i++) {
        meanX += grains[i].position.x;
        float d = hypotf(grains[i].position.x - funnel.origin.x, grains[i].position.z - funnel.origin.z);
        if (d > maxDistFromCenter) maxDistFromCenter = d;
    }
    meanX /= (float)N;
    float lagX = fabsf(funnel.origin.x - meanX);
    assert(lagX < 0.85f);              /* Cohesive column translates with storm, no curtain lag */
    assert(maxDistFromCenter < 1.85f); /* Bounded cone radius during translation */
    printf("[PASS] test_moving_tornado_cohesive_translation (stormX=%.2f m, meanX=%.2f m, lag=%.3f m, maxR=%.3f m)\n",
           funnel.origin.x, meanX, lagX, maxDistFromCenter);
}

static void test_tornado_spindown_no_explosion(void)
{
    enum { N = 64 };
    GranularGrainState grains[N];
    const float baseR = 0.25f, topR = 1.30f, height = 3.4f, swirl = 13.5f, updraft = 3.4f;

    for (int i = 0; i < N; i++) {
        float u = (float)i / (float)N;
        float angle = u * 6.2831853f * 4.0f;
        float hn = u;
        float coneR = baseR + (topR - baseR) * powf(hn, 1.35f);
        float r = coneR * 0.70f;
        grains[i] = (GranularGrainState){
            .position = {cosf(angle) * r, 0.1f + hn * height, sinf(angle) * r},
            .velocity = {-sinf(angle) * swirl, updraft, cosf(angle) * swirl},
            .radius = 0.02f, .lifetime = 8.0f, .seed = u, .materialId = GRANULAR_MAT_DESERT_SAND
        };
    }

    GranularWaveDesc funnel = {
        .kind       = GRANULAR_WAVE_CONICAL_FUNNEL,
        .origin     = {0.0f, 0.0f, 0.0f},
        .radius     = baseR,
        .topRadius  = topR,
        .length     = height,
        .axialSpeed = updraft,
        .swirlSpeed = swirl,
        .duration   = 3.0f,
        .attackTime = 0.01f,
        .fadeTime   = 0.55f,
        .loopTop    = true
    };

    const float dt = 1.0f / 60.0f;
    /* Spin for 0.4s then spin down over 0.55s */
    for (int step = 0; step < 57; step++) {
        float t = (float)step * dt;
        funnel.spinDown = step >= 24 ? (float)(step - 24) / 33.0f : 0.0f;
        GranularSim_StepCpuDeterministicWithWaves(
            grains, N, NULL, &funnel, 1, dt, t,
            (Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f});
    }

    float maxR = 0.0f, meanSpeed = 0.0f;
    for (int i = 0; i < N; i++) {
        float r = hypotf(grains[i].position.x, grains[i].position.z);
        if (r > maxR) maxR = r;
        float spd = sqrtf(grains[i].velocity.x * grains[i].velocity.x +
                          grains[i].velocity.y * grains[i].velocity.y +
                          grains[i].velocity.z * grains[i].velocity.z);
        meanSpeed += spd;
    }
    meanSpeed /= (float)N;
    assert(maxR < topR * 1.15f); /* Zero tangential starburst explosion during spin-down */
    assert(meanSpeed < 2.8f);    /* Swirl actively braked before confinement releases */
    printf("[PASS] test_tornado_spindown_no_explosion (maxR=%.3f m, meanSpeed=%.3f m/s)\n", maxR, meanSpeed);
}

static void test_all_13_material_profiles_valid(void)
{
    for (int i = 0; i < GRANULAR_MAT_COUNT; i++) {
        const GranularMaterialProfile *m = GranularSim_GetMaterialProfile((GranularMaterialId)i);
        assert(m != NULL);
        assert(isfinite(m->densityKgM3) && m->densityKgM3 > 100.0f);
        assert(isfinite(m->springStiffness) && m->springStiffness > 500.0f);
        assert(isfinite(m->shearFrictionMu) && m->shearFrictionMu >= 0.0f);
        assert(isfinite(m->referenceMassKg) && m->referenceMassKg > 0.0f);
        assert(isfinite(m->criticalEntrainmentMps) && m->criticalEntrainmentMps > 0.0f);
        assert(m->morphology <= GRANULAR_MORPH_METAL_SHARD);
    }
    const GranularMaterialProfile *crystal = GranularSim_GetMaterialProfile(GRANULAR_MAT_CRYSTAL_SHARD);
    const GranularMaterialProfile *metal   = GranularSim_GetMaterialProfile(GRANULAR_MAT_METAL_SHARD);
    assert(crystal->morphology == GRANULAR_MORPH_CRYSTAL_SHARD);
    assert(metal->morphology == GRANULAR_MORPH_METAL_SHARD);
    assert(metal->densityKgM3 > crystal->densityKgM3);
    printf("[PASS] test_all_13_material_profiles_valid (count=%d)\n", GRANULAR_MAT_COUNT);
}

static void test_tornado_mass_stratification(void)
{
    enum { N = 60 };
    GranularGrainState grains[N];
    for (int i = 0; i < N; i++) {
        int group = i % 3; // 0: gravel (heavy), 1: sand (medium), 2: dry leaf (light + lift)
        uint32_t mat = (group == 0) ? GRANULAR_MAT_COARSE_GRAVEL
                     : (group == 1 ? GRANULAR_MAT_DESERT_SAND : GRANULAR_MAT_DRY_LEAF);
        float angle = ((float)i / (float)N) * 6.2831853f * 3.0f;
        float r = 0.55f;
        grains[i] = (GranularGrainState){
            .position = {cosf(angle) * r, 0.35f, sinf(angle) * r},
            .velocity = {-sinf(angle) * 7.0f, 1.5f, cosf(angle) * 7.0f},
            .radius = 0.025f, .lifetime = 8.0f, .seed = (float)i / (float)N,
            .materialId = mat
        };
    }

    GranularWaveDesc funnel = {
        .kind       = GRANULAR_WAVE_CONICAL_FUNNEL,
        .origin     = {0.0f, 0.0f, 0.0f},
        .radius     = 0.25f,
        .topRadius  = 1.40f,
        .length     = 3.80f,
        .axialSpeed = 3.5f,
        .swirlSpeed = 8.5f,
        .duration   = 4.0f,
        .attackTime = 0.02f,
        .fadeTime   = 0.40f,
        .loopTop    = false
    };

    const float dt = 1.0f / 60.0f;
    for (int step = 0; step < 90; step++) {
        GranularSim_StepCpuDeterministicWithWaves(
            grains, N, NULL, &funnel, 1, dt, (float)step * dt,
            (Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 1.0f, 0.0f});
    }

    float sumY[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < N; i++) {
        sumY[i % 3] += grains[i].position.y;
    }
    float meanGravel = sumY[0] / 20.0f;
    float meanSand   = sumY[1] / 20.0f;
    float meanLeaf   = sumY[2] / 20.0f;
    assert(meanLeaf > meanSand);
    assert(meanSand > meanGravel);
    printf("[PASS] test_tornado_mass_stratification (gravelY=%.3f m < sandY=%.3f m < leafY=%.3f m)\n",
           meanGravel, meanSand, meanLeaf);
}

int main(void)
{
    test_inelastic_collision();
    test_angle_of_repose_pile_vs_frictionless();
    test_tornado_centripetal_equilibrium();
    test_toroidal_grid_far_travel_collision();
    test_mass_and_morphology_entrainment_sorting();
    test_progressive_palm_wave_sweep();
    test_mixed_material_bed_simulation();
    test_moving_tornado_cohesive_translation();
    test_tornado_spindown_no_explosion();
    test_all_13_material_profiles_valid();
    test_tornado_mass_stratification();
    printf("granular_sim_test: ALL PASSED\n");
    return 0;
}
