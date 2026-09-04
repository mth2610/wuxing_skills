#include "core/gas/gas_sim.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static int GasSim_Index(const GasSim *sim, int x, int y, int z) {
    return (z * sim->height + y) * sim->width + x;
}

static int GasSim_ClampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static float GasSim_Clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float GasSim_Sample(const GasSim *sim, const float *field,
                           float x, float y, float z) {
    x = fmaxf(0.0f, fminf(x, (float)(sim->width - 1)));
    y = fmaxf(0.0f, fminf(y, (float)(sim->height - 1)));
    z = fmaxf(0.0f, fminf(z, (float)(sim->depth - 1)));

    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int z0 = (int)floorf(z);
    int x1 = GasSim_ClampInt(x0 + 1, 0, sim->width - 1);
    int y1 = GasSim_ClampInt(y0 + 1, 0, sim->height - 1);
    int z1 = GasSim_ClampInt(z0 + 1, 0, sim->depth - 1);
    float tx = x - (float)x0;
    float ty = y - (float)y0;
    float tz = z - (float)z0;

    float c000 = field[GasSim_Index(sim, x0, y0, z0)];
    float c100 = field[GasSim_Index(sim, x1, y0, z0)];
    float c010 = field[GasSim_Index(sim, x0, y1, z0)];
    float c110 = field[GasSim_Index(sim, x1, y1, z0)];
    float c001 = field[GasSim_Index(sim, x0, y0, z1)];
    float c101 = field[GasSim_Index(sim, x1, y0, z1)];
    float c011 = field[GasSim_Index(sim, x0, y1, z1)];
    float c111 = field[GasSim_Index(sim, x1, y1, z1)];
    float c00 = c000 + (c100 - c000) * tx;
    float c10 = c010 + (c110 - c010) * tx;
    float c01 = c001 + (c101 - c001) * tx;
    float c11 = c011 + (c111 - c011) * tx;
    float c0 = c00 + (c10 - c00) * ty;
    float c1 = c01 + (c11 - c01) * ty;
    return c0 + (c1 - c0) * tz;
}

static void GasSim_ClearBoundary(const GasSim *sim, float *field) {
    for (int z = 0; z < sim->depth; ++z) {
        for (int y = 0; y < sim->height; ++y) {
            for (int x = 0; x < sim->width; ++x) {
                if (x == 0 || y == 0 || z == 0 ||
                    x == sim->width - 1 || y == sim->height - 1 ||
                    z == sim->depth - 1) {
                    field[GasSim_Index(sim, x, y, z)] = 0.0f;
                }
            }
        }
    }
}

static void GasSim_Advect(const GasSim *sim, const float *source, float *target,
                          float dt, float dissipation) {
    float decay = expf(-fmaxf(0.0f, dissipation) * dt);
    for (int z = 0; z < sim->depth; ++z) {
        for (int y = 0; y < sim->height; ++y) {
            for (int x = 0; x < sim->width; ++x) {
                int i = GasSim_Index(sim, x, y, z);
                float px = (float)x - sim->velocityX[i] * dt;
                float py = (float)y - sim->velocityY[i] * dt;
                float pz = (float)z - sim->velocityZ[i] * dt;
                target[i] = GasSim_Sample(sim, source, px, py, pz) * decay;
            }
        }
    }
    GasSim_ClearBoundary(sim, target);
}

static void GasSim_Advect3(const GasSim *sim,
                           const float *src0, const float *src1, const float *src2,
                           float *dst0, float *dst1, float *dst2,
                           float dt, float diss0, float diss1, float diss2)
{
    float decay0 = expf(-fmaxf(0.0f, diss0) * dt);
    float decay1 = expf(-fmaxf(0.0f, diss1) * dt);
    float decay2 = expf(-fmaxf(0.0f, diss2) * dt);
    float maxX = (float)(sim->width - 1);
    float maxY = (float)(sim->height - 1);
    float maxZ = (float)(sim->depth - 1);
    int strideY = sim->width;
    int strideZ = sim->width * sim->height;

    for (int z = 0; z < sim->depth; ++z) {
        int zOffset = z * strideZ;
        for (int y = 0; y < sim->height; ++y) {
            int rowOffset = zOffset + y * strideY;
            int i = rowOffset;
            for (int x = 0; x < sim->width; ++x, ++i) {
                float px = (float)x - sim->velocityX[i] * dt;
                float py = (float)y - sim->velocityY[i] * dt;
                float pz = (float)z - sim->velocityZ[i] * dt;

                px = fmaxf(0.0f, fminf(px, maxX));
                py = fmaxf(0.0f, fminf(py, maxY));
                pz = fmaxf(0.0f, fminf(pz, maxZ));

                int x0 = (int)floorf(px);
                int y0 = (int)floorf(py);
                int z0 = (int)floorf(pz);
                int x1 = GasSim_ClampInt(x0 + 1, 0, sim->width - 1);
                int y1 = GasSim_ClampInt(y0 + 1, 0, sim->height - 1);
                int z1 = GasSim_ClampInt(z0 + 1, 0, sim->depth - 1);

                float tx = px - (float)x0;
                float ty = py - (float)y0;
                float tz = pz - (float)z0;

                int i000 = z0 * strideZ + y0 * strideY + x0;
                int i100 = z0 * strideZ + y0 * strideY + x1;
                int i010 = z0 * strideZ + y1 * strideY + x0;
                int i110 = z0 * strideZ + y1 * strideY + x1;
                int i001 = z1 * strideZ + y0 * strideY + x0;
                int i101 = z1 * strideZ + y0 * strideY + x1;
                int i011 = z1 * strideZ + y1 * strideY + x0;
                int i111 = z1 * strideZ + y1 * strideY + x1;

                /* Field 0 */
                float c00 = src0[i000] + (src0[i100] - src0[i000]) * tx;
                float c10 = src0[i010] + (src0[i110] - src0[i010]) * tx;
                float c01 = src0[i001] + (src0[i101] - src0[i001]) * tx;
                float c11 = src0[i011] + (src0[i111] - src0[i011]) * tx;
                float c0 = c00 + (c10 - c00) * ty;
                float c1 = c01 + (c11 - c01) * ty;
                dst0[i] = (c0 + (c1 - c0) * tz) * decay0;

                /* Field 1 */
                c00 = src1[i000] + (src1[i100] - src1[i000]) * tx;
                c10 = src1[i010] + (src1[i110] - src1[i010]) * tx;
                c01 = src1[i001] + (src1[i101] - src1[i001]) * tx;
                c11 = src1[i011] + (src1[i111] - src1[i011]) * tx;
                c0 = c00 + (c10 - c00) * ty;
                c1 = c01 + (c11 - c01) * ty;
                dst1[i] = (c0 + (c1 - c0) * tz) * decay1;

                /* Field 2 */
                c00 = src2[i000] + (src2[i100] - src2[i000]) * tx;
                c10 = src2[i010] + (src2[i110] - src2[i010]) * tx;
                c01 = src2[i001] + (src2[i101] - src2[i001]) * tx;
                c11 = src2[i011] + (src2[i111] - src2[i011]) * tx;
                c0 = c00 + (c10 - c00) * ty;
                c1 = c01 + (c11 - c01) * ty;
                dst2[i] = (c0 + (c1 - c0) * tz) * decay2;
            }
        }
    }
    GasSim_ClearBoundary(sim, dst0);
    GasSim_ClearBoundary(sim, dst1);
    GasSim_ClearBoundary(sim, dst2);
}

static void GasSim_Swap(float **a, float **b) {
    float *temp = *a;
    *a = *b;
    *b = temp;
}

GasSimConfig GasSim_DefaultConfig(void) {
    GasSimConfig config;
    config.buoyancy = 3.0f;
    config.smokeWeight = 0.25f;
    config.vorticityStrength = 1.6f;
    config.velocityDissipation = 0.15f;
    config.densityDissipation = 0.35f;
    config.temperatureDissipation = 1.0f;
    config.reactionDissipation = 1.8f;
    config.pressureIterations = 8;
    return config;
}

static void GasSim_ApplyVorticityConfinement(GasSim *sim, float dt,
                                              float strength) {
    if (strength <= 0.0f) return;

    /* Reuse the advected-velocity scratch fields for curl. scalarScratchA is
     * free until scalar advection below. This keeps the mobile path allocation
     * free while restoring the small eddies lost by semi-Lagrangian advection. */
    for (int z = 1; z < sim->depth - 1; ++z) {
        for (int y = 1; y < sim->height - 1; ++y) {
            for (int x = 1; x < sim->width - 1; ++x) {
                int i = GasSim_Index(sim, x, y, z);
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
                sim->velocityScratchX[i] = curlX;
                sim->velocityScratchY[i] = curlY;
                sim->velocityScratchZ[i] = curlZ;
                sim->scalarScratchA[i] = sqrtf(curlX * curlX + curlY * curlY +
                                               curlZ * curlZ);
            }
        }
    }
    GasSim_ClearBoundary(sim, sim->scalarScratchA);

    for (int z = 1; z < sim->depth - 1; ++z) {
        for (int y = 1; y < sim->height - 1; ++y) {
            for (int x = 1; x < sim->width - 1; ++x) {
                int i = GasSim_Index(sim, x, y, z);
                float nx = 0.5f * (
                    sim->scalarScratchA[GasSim_Index(sim, x + 1, y, z)] -
                    sim->scalarScratchA[GasSim_Index(sim, x - 1, y, z)]);
                float ny = 0.5f * (
                    sim->scalarScratchA[GasSim_Index(sim, x, y + 1, z)] -
                    sim->scalarScratchA[GasSim_Index(sim, x, y - 1, z)]);
                float nz = 0.5f * (
                    sim->scalarScratchA[GasSim_Index(sim, x, y, z + 1)] -
                    sim->scalarScratchA[GasSim_Index(sim, x, y, z - 1)]);
                float inverseLength = 1.0f / fmaxf(sqrtf(nx * nx + ny * ny +
                                                         nz * nz), 1.0e-5f);
                nx *= inverseLength;
                ny *= inverseLength;
                nz *= inverseLength;
                float curlX = sim->velocityScratchX[i];
                float curlY = sim->velocityScratchY[i];
                float curlZ = sim->velocityScratchZ[i];
                float scale = dt * strength;
                sim->velocityX[i] += (ny * curlZ - nz * curlY) * scale;
                sim->velocityY[i] += (nz * curlX - nx * curlZ) * scale;
                sim->velocityZ[i] += (nx * curlY - ny * curlX) * scale;
            }
        }
    }
}

bool GasSim_Init(GasSim *sim, int width, int height, int depth) {
    if (sim == NULL || width < 3 || height < 3 || depth < 3 ||
        width > GAS_SIM_MAX_X || height > GAS_SIM_MAX_Y || depth > GAS_SIM_MAX_Z) {
        return false;
    }
    memset(sim, 0, sizeof(*sim));
    sim->width = width;
    sim->height = height;
    sim->depth = depth;
    sim->cellCount = width * height * depth;
    return true;
}

void GasSim_Clear(GasSim *sim) {
    if (sim == NULL) return;
    int width = sim->width;
    int height = sim->height;
    int depth = sim->depth;
    memset(sim, 0, sizeof(*sim));
    sim->width = width;
    sim->height = height;
    sim->depth = depth;
    sim->cellCount = width * height * depth;
}

int GasSim_GetCellCount(const GasSim *sim) {
    return sim != NULL ? sim->cellCount : 0;
}

void GasSim_InjectSphere(GasSim *sim, GasSimInjection injection) {
    if (sim == NULL || sim->cellCount <= 0 || injection.radius <= 0.0f) return;
    float shortest = (float)sim->width;
    if ((float)sim->height < shortest) shortest = (float)sim->height;
    if ((float)sim->depth < shortest) shortest = (float)sim->depth;
    float radius = fmaxf(0.5f, injection.radius * shortest);
    float cx = GasSim_Clamp01(injection.position.x) * (float)(sim->width - 1);
    float cy = GasSim_Clamp01(injection.position.y) * (float)(sim->height - 1);
    float cz = GasSim_Clamp01(injection.position.z) * (float)(sim->depth - 1);

    int minX = GasSim_ClampInt((int)floorf(cx - radius), 1, sim->width - 2);
    int maxX = GasSim_ClampInt((int)ceilf(cx + radius), 1, sim->width - 2);
    int minY = GasSim_ClampInt((int)floorf(cy - radius), 1, sim->height - 2);
    int maxY = GasSim_ClampInt((int)ceilf(cy + radius), 1, sim->height - 2);
    int minZ = GasSim_ClampInt((int)floorf(cz - radius), 1, sim->depth - 2);
    int maxZ = GasSim_ClampInt((int)ceilf(cz + radius), 1, sim->depth - 2);
    for (int z = minZ; z <= maxZ; ++z) {
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float dx = (float)x - cx;
                float dy = (float)y - cy;
                float dz = (float)z - cz;
                float distance = sqrtf(dx * dx + dy * dy + dz * dz);
                if (distance >= radius) continue;
                float falloff = 1.0f - distance / radius;
                falloff *= falloff * (3.0f - 2.0f * falloff);
                int i = GasSim_Index(sim, x, y, z);
                sim->velocityX[i] += injection.velocity.x * falloff;
                sim->velocityY[i] += injection.velocity.y * falloff;
                sim->velocityZ[i] += injection.velocity.z * falloff;
                sim->density[i] = fminf(1.0f, sim->density[i] + injection.density * falloff);
                sim->temperature[i] = fminf(1.0f, sim->temperature[i] + injection.temperature * falloff);
                sim->reaction[i] = fminf(1.0f, sim->reaction[i] + injection.reaction * falloff);
            }
        }
    }
}

float GasSim_GetMeanAbsDivergence(const GasSim *sim) {
    if (sim == NULL || sim->cellCount <= 0) return 0.0f;
    double sum = 0.0;
    int count = 0;
    for (int z = 1; z < sim->depth - 1; ++z) {
        for (int y = 1; y < sim->height - 1; ++y) {
            for (int x = 1; x < sim->width - 1; ++x) {
                float dx = sim->velocityX[GasSim_Index(sim, x + 1, y, z)] -
                           sim->velocityX[GasSim_Index(sim, x - 1, y, z)];
                float dy = sim->velocityY[GasSim_Index(sim, x, y + 1, z)] -
                           sim->velocityY[GasSim_Index(sim, x, y - 1, z)];
                float dz = sim->velocityZ[GasSim_Index(sim, x, y, z + 1)] -
                           sim->velocityZ[GasSim_Index(sim, x, y, z - 1)];
                sum += fabsf(0.5f * (dx + dy + dz));
                ++count;
            }
        }
    }
    return count > 0 ? (float)(sum / (double)count) : 0.0f;
}

void GasSim_ProjectVelocity(GasSim *sim, int pressureIterations) {
    if (sim == NULL || sim->cellCount <= 0) return;
    if (pressureIterations < 1) pressureIterations = 1;
    if (pressureIterations > 64) pressureIterations = 64;
    memset(sim->pressure, 0, (size_t)sim->cellCount * sizeof(float));
    memset(sim->pressureScratch, 0, (size_t)sim->cellCount * sizeof(float));

    int w = sim->width;
    int h = sim->height;
    int d = sim->depth;
    int strideY = w;
    int strideZ = w * h;

    for (int z = 1; z < d - 1; ++z) {
        int zOffset = z * strideZ;
        for (int y = 1; y < h - 1; ++y) {
            int rowOffset = zOffset + y * strideY;
            int i = rowOffset + 1;
            for (int x = 1; x < w - 1; ++x, ++i) {
                sim->divergence[i] = -0.5f * (
                    sim->velocityX[i + 1] -
                    sim->velocityX[i - 1] +
                    sim->velocityY[i + strideY] -
                    sim->velocityY[i - strideY] +
                    sim->velocityZ[i + strideZ] -
                    sim->velocityZ[i - strideZ]);
            }
        }
    }

    float *pressure = sim->pressure;
    float *scratch = sim->pressureScratch;
    const float inv6 = 1.0f / 6.0f;
    for (int iteration = 0; iteration < pressureIterations; ++iteration) {
        for (int z = 1; z < d - 1; ++z) {
            int zOffset = z * strideZ;
            for (int y = 1; y < h - 1; ++y) {
                int rowOffset = zOffset + y * strideY;
                int i = rowOffset + 1;
                for (int x = 1; x < w - 1; ++x, ++i) {
                    scratch[i] = (sim->divergence[i] +
                        pressure[i - 1] +
                        pressure[i + 1] +
                        pressure[i - strideY] +
                        pressure[i + strideY] +
                        pressure[i - strideZ] +
                        pressure[i + strideZ]) * inv6;
                }
            }
        }
        GasSim_Swap(&pressure, &scratch);
    }

    if (pressure != sim->pressure) {
        memcpy(sim->pressure, pressure, (size_t)sim->cellCount * sizeof(float));
    }

    for (int z = 1; z < d - 1; ++z) {
        int zOffset = z * strideZ;
        for (int y = 1; y < h - 1; ++y) {
            int rowOffset = zOffset + y * strideY;
            int i = rowOffset + 1;
            for (int x = 1; x < w - 1; ++x, ++i) {
                sim->velocityX[i] -= 0.5f * (
                    sim->pressure[i + 1] -
                    sim->pressure[i - 1]);
                sim->velocityY[i] -= 0.5f * (
                    sim->pressure[i + strideY] -
                    sim->pressure[i - strideY]);
                sim->velocityZ[i] -= 0.5f * (
                    sim->pressure[i + strideZ] -
                    sim->pressure[i - strideZ]);
            }
        }
    }
    GasSim_ClearBoundary(sim, sim->velocityX);
    GasSim_ClearBoundary(sim, sim->velocityY);
    GasSim_ClearBoundary(sim, sim->velocityZ);
}

void GasSim_Step(GasSim *sim, float dt, const GasSimConfig *config) {
    if (sim == NULL || config == NULL || sim->cellCount <= 0 || dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;

    GasSim_Advect3(sim, sim->velocityX, sim->velocityY, sim->velocityZ,
                   sim->velocityScratchX, sim->velocityScratchY, sim->velocityScratchZ,
                   dt, config->velocityDissipation, config->velocityDissipation,
                   config->velocityDissipation);
    memcpy(sim->velocityX, sim->velocityScratchX,
           (size_t)sim->cellCount * sizeof(float));
    memcpy(sim->velocityY, sim->velocityScratchY,
           (size_t)sim->cellCount * sizeof(float));
    memcpy(sim->velocityZ, sim->velocityScratchZ,
           (size_t)sim->cellCount * sizeof(float));

    for (int i = 0; i < sim->cellCount; ++i) {
        sim->velocityY[i] += dt * (config->buoyancy * sim->temperature[i] -
                                   config->smokeWeight * sim->density[i]);
    }
    GasSim_ApplyVorticityConfinement(sim, dt, config->vorticityStrength);
    GasSim_ProjectVelocity(sim, config->pressureIterations);

    GasSim_Advect3(sim, sim->density, sim->temperature, sim->reaction,
                   sim->scalarScratchA, sim->scalarScratchB, sim->scalarScratchC,
                   dt, config->densityDissipation, config->temperatureDissipation,
                   config->reactionDissipation);
    memcpy(sim->density, sim->scalarScratchA,
           (size_t)sim->cellCount * sizeof(float));
    memcpy(sim->temperature, sim->scalarScratchB,
           (size_t)sim->cellCount * sizeof(float));
    memcpy(sim->reaction, sim->scalarScratchC,
           (size_t)sim->cellCount * sizeof(float));
}

float GasSim_GetTotalDensity(const GasSim *sim) {
    if (sim == NULL) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < sim->cellCount; ++i) sum += sim->density[i];
    return (float)sum;
}

float GasSim_GetDensityCenterY(const GasSim *sim) {
    if (sim == NULL || sim->cellCount <= 0) return 0.0f;
    double weightedY = 0.0;
    double weight = 0.0;
    for (int z = 0; z < sim->depth; ++z) {
        for (int y = 0; y < sim->height; ++y) {
            for (int x = 0; x < sim->width; ++x) {
                float density = sim->density[GasSim_Index(sim, x, y, z)];
                weightedY += density * ((double)y / (double)(sim->height - 1));
                weight += density;
            }
        }
    }
    return weight > 1.0e-8 ? (float)(weightedY / weight) : 0.0f;
}

float GasSim_GetMaxTemperature(const GasSim *sim) {
    float maximum = 0.0f;
    if (sim == NULL) return maximum;
    for (int i = 0; i < sim->cellCount; ++i)
        if (sim->temperature[i] > maximum) maximum = sim->temperature[i];
    return maximum;
}

float GasSim_GetMaxReaction(const GasSim *sim) {
    float maximum = 0.0f;
    if (sim == NULL) return maximum;
    for (int i = 0; i < sim->cellCount; ++i)
        if (sim->reaction[i] > maximum) maximum = sim->reaction[i];
    return maximum;
}

bool GasSim_IsFinite(const GasSim *sim) {
    if (sim == NULL) return false;
    for (int i = 0; i < sim->cellCount; ++i) {
        if (!isfinite(sim->velocityX[i]) || !isfinite(sim->velocityY[i]) ||
            !isfinite(sim->velocityZ[i]) || !isfinite(sim->density[i]) ||
            !isfinite(sim->temperature[i]) || !isfinite(sim->reaction[i])) {
            return false;
        }
    }
    return true;
}
