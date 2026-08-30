#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/gas/gas_sim.h"
#include "core/gas/gas_sim.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "gas_sim_test: check failed at line %d: %s\n", \
                __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static GasSim s_sim;
static GasSim s_control;

static GasSimConfig TestConfig(void) {
    GasSimConfig config = GasSim_DefaultConfig();
    config.pressureIterations = 12;
    config.buoyancy = 5.0f;
    config.smokeWeight = 0.0f;
    config.velocityDissipation = 0.0f;
    config.densityDissipation = 0.0f;
    config.temperatureDissipation = 0.0f;
    config.reactionDissipation = 0.0f;
    return config;
}

static int TestInjectionAndProjection(void) {
    CHECK(!GasSim_Init(&s_sim, 0, 12, 12));
    CHECK(!GasSim_Init(&s_sim, GAS_SIM_MAX_X + 1, 12, 12));
    CHECK(GasSim_Init(&s_sim, 16, 20, 16));
    CHECK(GasSim_GetCellCount(&s_sim) == 16 * 20 * 16);

    GasSimInjection injection = {0};
    injection.position = (GasSimVec3){0.5f, 0.25f, 0.5f};
    injection.radius = 0.14f;
    injection.velocity = (GasSimVec3){1.0f, 5.0f, -0.75f};
    injection.density = 1.0f;
    injection.temperature = 1.0f;
    injection.reaction = 0.8f;
    GasSim_InjectSphere(&s_sim, injection);

    CHECK(GasSim_GetTotalDensity(&s_sim) > 1.0f);
    CHECK(GasSim_GetMaxTemperature(&s_sim) > 0.7f);
    CHECK(GasSim_GetMaxReaction(&s_sim) > 0.55f);

    float before = GasSim_GetMeanAbsDivergence(&s_sim);
    GasSim_ProjectVelocity(&s_sim, 24);
    float after = GasSim_GetMeanAbsDivergence(&s_sim);
    CHECK(before > 0.001f);
    CHECK(after < before * 0.82f);
    CHECK(GasSim_IsFinite(&s_sim));
    return 0;
}

static int TestBuoyancyAndDissipation(void) {
    CHECK(GasSim_Init(&s_sim, 16, 24, 16));
    GasSimInjection injection = {0};
    injection.position = (GasSimVec3){0.5f, 0.2f, 0.5f};
    injection.radius = 0.12f;
    injection.density = 1.0f;
    injection.temperature = 1.0f;
    injection.reaction = 1.0f;
    GasSim_InjectSphere(&s_sim, injection);

    float initialCenter = GasSim_GetDensityCenterY(&s_sim);
    GasSimConfig config = TestConfig();
    for (int i = 0; i < 16; ++i) GasSim_Step(&s_sim, 1.0f / 30.0f, &config);
    float risenCenter = GasSim_GetDensityCenterY(&s_sim);
    CHECK(risenCenter > initialCenter + 0.004f);
    CHECK(GasSim_IsFinite(&s_sim));

    float beforeDecay = GasSim_GetTotalDensity(&s_sim);
    config.buoyancy = 0.0f;
    config.densityDissipation = 1.5f;
    for (int i = 0; i < 10; ++i) GasSim_Step(&s_sim, 1.0f / 30.0f, &config);
    CHECK(GasSim_GetTotalDensity(&s_sim) < beforeDecay * 0.75f);
    CHECK(GasSim_IsFinite(&s_sim));
    return 0;
}

static float MeanCurl(const GasSim *sim) {
    double sum = 0.0;
    int count = 0;
    for (int z = 1; z < sim->depth - 1; ++z) {
        for (int y = 1; y < sim->height - 1; ++y) {
            for (int x = 1; x < sim->width - 1; ++x) {
                float curlX = 0.5f * (
                    sim->velocityZ[GasSim_Index(sim, x, y + 1, z)] -
                    sim->velocityZ[GasSim_Index(sim, x, y - 1, z)] -
                    sim->velocityY[GasSim_Index(sim, x, y, z + 1)] +
                    sim->velocityY[GasSim_Index(sim, x, y, z - 1)]);
                float curlY = 0.5f * (
                    sim->velocityX[GasSim_Index(sim, x, y, z + 1)] -
                    sim->velocityX[GasSim_Index(sim, x, y, z - 1)] -
                    sim->velocityZ[GasSim_Index(sim, x + 1, y, z)] +
                    sim->velocityZ[GasSim_Index(sim, x - 1, y, z)]);
                float curlZ = 0.5f * (
                    sim->velocityY[GasSim_Index(sim, x + 1, y, z)] -
                    sim->velocityY[GasSim_Index(sim, x - 1, y, z)] -
                    sim->velocityX[GasSim_Index(sim, x, y + 1, z)] +
                    sim->velocityX[GasSim_Index(sim, x, y - 1, z)]);
                sum += sqrtf(curlX * curlX + curlY * curlY + curlZ * curlZ);
                ++count;
            }
        }
    }
    return count > 0 ? (float)(sum / (double)count) : 0.0f;
}

static int TestVorticityConfinement(void) {
    CHECK(GasSim_Init(&s_sim, 16, 16, 16));
    for (int z = 1; z < s_sim.depth - 1; ++z) {
        for (int y = 1; y < s_sim.height - 1; ++y) {
            for (int x = 1; x < s_sim.width - 1; ++x) {
                float dx = (float)x - 7.5f;
                float dy = (float)y - 7.5f;
                float dz = (float)z - 7.5f;
                float radius2 = dx * dx + dy * dy + dz * dz;
                float envelope = expf(-radius2 / 18.0f);
                int index = GasSim_Index(&s_sim, x, y, z);
                s_sim.velocityX[index] = -dy * envelope * 0.7f;
                s_sim.velocityY[index] = dx * envelope * 0.7f;
                s_sim.velocityZ[index] = sinf((float)y * 0.73f) * envelope * 0.35f;
            }
        }
    }
    s_control = s_sim;

    GasSimConfig turbulent = TestConfig();
    turbulent.buoyancy = 0.0f;
    turbulent.pressureIterations = 12;
    turbulent.vorticityStrength = 2.4f;
    GasSimConfig control = turbulent;
    control.vorticityStrength = 0.0f;
    for (int i = 0; i < 5; ++i) {
        GasSim_Step(&s_sim, 1.0f / 30.0f, &turbulent);
        GasSim_Step(&s_control, 1.0f / 30.0f, &control);
    }

    CHECK(MeanCurl(&s_sim) > MeanCurl(&s_control) * 1.015f);
    CHECK(GasSim_IsFinite(&s_sim));
    return 0;
}

int main(void) {
    if (TestInjectionAndProjection() != 0) return 1;
    if (TestBuoyancyAndDissipation() != 0) return 1;
    if (TestVorticityConfinement() != 0) return 1;
    puts("gas_sim_test: PASS");
    return 0;
}
