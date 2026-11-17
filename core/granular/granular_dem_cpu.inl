#ifndef CORE_GRANULAR_DEM_CPU_INL
#define CORE_GRANULAR_DEM_CPU_INL

#include "core/granular/granular_sim.h"
#include <math.h>
#include <string.h>

#define GRANULAR_CPU_GRID_DIM 24
#define GRANULAR_CPU_GRID_CELLS (GRANULAR_CPU_GRID_DIM * GRANULAR_CPU_GRID_DIM * GRANULAR_CPU_GRID_DIM)

static const GranularMaterialProfile k_granularMaterials[GRANULAR_MAT_COUNT] = {
    [GRANULAR_MAT_DESERT_SAND] = {
        .densityKgM3         = 1600.0f,
        .springStiffness     = 2600.0f,
        .dashpotDamping      = 105.0f,
        .shearFrictionMu     = 0.65f,   // tan(33 deg)
        .rollingResistance   = 0.25f,
        .cohesion            = 0.0f,
        .airDrag             = 0.55f,
        .saltationSplash     = 0.28f,
        .dustEntrainmentRate = 1.0f,
        .baseColor           = {232, 198, 136, 255},
        .shadowColor         = {148, 114, 72,  255},
        .highlightColor      = {255, 248, 224, 255},
        .glintIntensity      = 1.45f,
        .glintSharpness      = 128.0f,
        .grainAspect         = 0.85f,
    },
    [GRANULAR_MAT_COARSE_GRAVEL] = {
        .densityKgM3         = 2400.0f,
        .springStiffness     = 3200.0f,
        .dashpotDamping      = 115.0f,
        .shearFrictionMu     = 0.84f,   // tan(40 deg)
        .rollingResistance   = 0.45f,
        .cohesion            = 0.0f,
        .airDrag             = 0.22f,
        .saltationSplash     = 0.14f,
        .dustEntrainmentRate = 0.65f,
        .baseColor           = {138, 128, 116, 255},
        .shadowColor         = {48,  44,  40,  255},
        .highlightColor      = {220, 214, 204, 255},
        .glintIntensity      = 0.45f,
        .glintSharpness      = 48.0f,
        .grainAspect         = 0.62f,
    },
    [GRANULAR_MAT_LOESS_SOIL] = {
        .densityKgM3         = 1450.0f,
        .springStiffness     = 2200.0f,
        .dashpotDamping      = 120.0f,
        .shearFrictionMu     = 1.00f,   // tan(45 deg)
        .rollingResistance   = 0.55f,
        .cohesion            = 18.0f,
        .airDrag             = 0.48f,
        .saltationSplash     = 0.10f,
        .dustEntrainmentRate = 1.35f,
        .baseColor           = {118, 88,  58,  255},
        .shadowColor         = {42,  30,  20,  255},
        .highlightColor      = {178, 148, 108, 255},
        .glintIntensity      = 0.15f,
        .glintSharpness      = 24.0f,
        .grainAspect         = 0.78f,
    },
    [GRANULAR_MAT_GOLD_IRON_SAND] = {
        .densityKgM3         = 4800.0f,
        .springStiffness     = 3400.0f,
        .dashpotDamping      = 110.0f,
        .shearFrictionMu     = 0.73f,   // tan(36 deg)
        .rollingResistance   = 0.32f,
        .cohesion            = 0.0f,
        .airDrag             = 0.15f,
        .saltationSplash     = 0.22f,
        .dustEntrainmentRate = 0.55f,
        .baseColor           = {198, 154, 58,  255},
        .shadowColor         = {72,  48,  16,  255},
        .highlightColor      = {255, 232, 130, 255},
        .glintIntensity      = 2.10f,
        .glintSharpness      = 160.0f,
        .grainAspect         = 0.80f,
    },
    [GRANULAR_MAT_VOLCANIC_ASH] = {
        .densityKgM3         = 950.0f,
        .springStiffness     = 1900.0f,
        .dashpotDamping      = 125.0f,
        .shearFrictionMu     = 0.58f,   // tan(30 deg)
        .rollingResistance   = 0.20f,
        .cohesion            = 6.0f,
        .airDrag             = 1.15f,
        .saltationSplash     = 0.32f,
        .dustEntrainmentRate = 1.60f,
        .baseColor           = {68,  62,  60,  255},
        .shadowColor         = {22,  18,  18,  255},
        .highlightColor      = {255, 130, 48,  255},
        .glintIntensity      = 0.85f,
        .glintSharpness      = 64.0f,
        .grainAspect         = 0.72f,
    },
};

const GranularMaterialProfile *GranularSim_GetMaterialProfile(GranularMaterialId id)
{
    if (id < 0 || id >= GRANULAR_MAT_COUNT) id = GRANULAR_MAT_DESERT_SAND;
    return &k_granularMaterials[id];
}

static inline int GranularCpu_CellIndex(Vector3 p, Vector3 origin, float cellSize)
{
    int gx = (int)floorf((p.x - origin.x) / cellSize) + (GRANULAR_CPU_GRID_DIM / 2);
    int gy = (int)floorf((p.y - origin.y) / cellSize) + (GRANULAR_CPU_GRID_DIM / 2);
    int gz = (int)floorf((p.z - origin.z) / cellSize) + (GRANULAR_CPU_GRID_DIM / 2);
    if (gx < 0) gx = 0; else if (gx >= GRANULAR_CPU_GRID_DIM) gx = GRANULAR_CPU_GRID_DIM - 1;
    if (gy < 0) gy = 0; else if (gy >= GRANULAR_CPU_GRID_DIM) gy = GRANULAR_CPU_GRID_DIM - 1;
    if (gz < 0) gz = 0; else if (gz >= GRANULAR_CPU_GRID_DIM) gz = GRANULAR_CPU_GRID_DIM - 1;
    return (gz * GRANULAR_CPU_GRID_DIM + gy) * GRANULAR_CPU_GRID_DIM + gx;
}

float GranularSim_StepCpuDeterministic(GranularGrainState *grains, int count,
                                       const GranularMaterialProfile *mat,
                                       float dt, Vector3 receiverPoint,
                                       Vector3 receiverNormal)
{
    if (!grains || count <= 0 || dt <= 0.0f) return 0.0f;
    if (!mat) mat = &k_granularMaterials[GRANULAR_MAT_DESERT_SAND];
    if (count > GRANULAR_CPU_MAX_GRAINS) count = GRANULAR_CPU_MAX_GRAINS;

    float nLen = sqrtf(receiverNormal.x * receiverNormal.x +
                       receiverNormal.y * receiverNormal.y +
                       receiverNormal.z * receiverNormal.z);
    Vector3 rNorm = (nLen > 1e-5f)
        ? (Vector3){receiverNormal.x / nLen, receiverNormal.y / nLen, receiverNormal.z / nLen}
        : (Vector3){0.0f, 1.0f, 0.0f};

    float maxRadius = 0.02f;
    for (int i = 0; i < count; i++) {
        if (grains[i].radius > maxRadius) maxRadius = grains[i].radius;
    }
    float cellSize = maxRadius * 2.2f;
    if (cellSize < 0.04f) cellSize = 0.04f;

    static int s_cpuHeads[GRANULAR_CPU_GRID_CELLS];
    static int s_cpuNext[GRANULAR_CPU_MAX_GRAINS];
    static Vector3 s_forces[GRANULAR_CPU_MAX_GRAINS];
    static int s_coord[GRANULAR_CPU_MAX_GRAINS];

    const int substeps = 4;
    float subDt = fminf(dt, 1.0f / 30.0f) / (float)substeps;
    float totalShearWork = 0.0f;

    for (int step = 0; step < substeps; step++) {
        for (int c = 0; c < GRANULAR_CPU_GRID_CELLS; c++) s_cpuHeads[c] = -1;

        for (int i = 0; i < count; i++) {
            s_forces[i] = (Vector3){0.0f, 0.0f, 0.0f};
            s_coord[i] = 0;
            if (grains[i].lifetime <= 0.0f) {
                s_cpuNext[i] = -1;
                continue;
            }
            int cell = GranularCpu_CellIndex(grains[i].position, receiverPoint, cellSize);
            s_cpuNext[i] = s_cpuHeads[cell];
            s_cpuHeads[cell] = i;
        }

        for (int i = 0; i < count; i++) {
            if (grains[i].lifetime <= 0.0f) continue;

            Vector3 posI = grains[i].position;
            Vector3 velI = grains[i].velocity;
            float radI = grains[i].radius;

            // Gravity + aerodynamic drag
            Vector3 acc = {
                -velI.x * mat->airDrag,
                -9.81f - velI.y * mat->airDrag,
                -velI.z * mat->airDrag
            };

            int cell = GranularCpu_CellIndex(posI, receiverPoint, cellSize);
            int cz = cell / (GRANULAR_CPU_GRID_DIM * GRANULAR_CPU_GRID_DIM);
            int cy = (cell / GRANULAR_CPU_GRID_DIM) % GRANULAR_CPU_GRID_DIM;
            int cx = cell % GRANULAR_CPU_GRID_DIM;

            int contacts = 0;
            for (int dz = -1; dz <= 1; dz++) {
                int nz = cz + dz;
                if (nz < 0 || nz >= GRANULAR_CPU_GRID_DIM) continue;
                for (int dy = -1; dy <= 1; dy++) {
                    int ny = cy + dy;
                    if (ny < 0 || ny >= GRANULAR_CPU_GRID_DIM) continue;
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = cx + dx;
                        if (nx < 0 || nx >= GRANULAR_CPU_GRID_DIM) continue;
                        int j = s_cpuHeads[(nz * GRANULAR_CPU_GRID_DIM + ny) * GRANULAR_CPU_GRID_DIM + nx];
                        while (j >= 0) {
                            if (j != i && grains[j].lifetime > 0.0f) {
                                float rx = grains[j].position.x - posI.x;
                                float ry = grains[j].position.y - posI.y;
                                float rz = grains[j].position.z - posI.z;
                                float distSq = rx * rx + ry * ry + rz * rz;
                                float sumR = radI + grains[j].radius;
                                float interactR = sumR * (mat->cohesion > 0.0f ? 1.15f : 1.0f);

                                if (distSq > 1e-8f && distSq < interactR * interactR) {
                                    float dist = sqrtf(distSq);
                                    float invDist = 1.0f / dist;
                                    float nX = rx * invDist;
                                    float nY = ry * invDist;
                                    float nZ = rz * invDist;
                                    float overlap = sumR - dist;

                                    if (overlap > 0.0f) {
                                        contacts++;
                                        float vrx = grains[j].velocity.x - velI.x;
                                        float vry = grains[j].velocity.y - velI.y;
                                        float vrz = grains[j].velocity.z - velI.z;
                                        float vn = vrx * nX + vry * nY + vrz * nZ;

                                        // Harada Ch.29 Eq. 1-2: Normal repulsive spring + inelastic dashpot
                                        float normOverlap = fminf(overlap / fmaxf(sumR, 0.01f), 0.65f);
                                        float fSpring = mat->springStiffness * normOverlap * sqrtf(normOverlap);
                                        float fDamp = -mat->dashpotDamping * vn;
                                        float maxDamp = 0.80f / fmaxf(subDt, 1e-4f);
                                        if (fDamp > maxDamp * fabsf(vn)) fDamp = maxDamp * fabsf(vn);
                                        float fn = fSpring + fDamp;
                                        if (fn < 0.0f) fn = 0.0f;

                                        acc.x -= nX * fn;
                                        acc.y -= nY * fn;
                                        acc.z -= nZ * fn;

                                        // Harada Ch.29 Eq. 3: Tangential Coulomb shear friction
                                        float vtx = vrx - vn * nX;
                                        float vty = vry - vn * nY;
                                        float vtz = vrz - vn * nZ;
                                        float vtLen = sqrtf(vtx * vtx + vty * vty + vtz * vtz);
                                        if (vtLen > 1e-4f) {
                                            float ftMag = fminf((0.45f * vtLen) / fmaxf(subDt, 1e-4f), mat->shearFrictionMu * fn);
                                            float invVt = 1.0f / vtLen;
                                            acc.x += vtx * invVt * ftMag;
                                            acc.y += vty * invVt * ftMag;
                                            acc.z += vtz * invVt * ftMag;
                                            totalShearWork += ftMag * vtLen * subDt;
                                        }
                                    } else if (mat->cohesion > 0.0f) {
                                        // Capillary bridge for damp loam/soil
                                        float gap = -overlap / fmaxf(sumR * 0.15f, 0.005f);
                                        float pull = mat->cohesion * (1.0f - fminf(gap, 1.0f));
                                        acc.x += nX * pull;
                                        acc.y += nY * pull;
                                        acc.z += nZ * pull;
                                    }
                                }
                            }
                            j = s_cpuNext[j];
                        }
                    }
                }
            }
            s_forces[i] = acc;
            s_coord[i] = contacts;
        }

        // Integrate + receiver plane contact + Coulomb ground friction
        for (int i = 0; i < count; i++) {
            if (grains[i].lifetime <= 0.0f) continue;

            grains[i].velocity.x += s_forces[i].x * subDt;
            grains[i].velocity.y += s_forces[i].y * subDt;
            grains[i].velocity.z += s_forces[i].z * subDt;

            grains[i].position.x += grains[i].velocity.x * subDt;
            grains[i].position.y += grains[i].velocity.y * subDt;
            grains[i].position.z += grains[i].velocity.z * subDt;

            float dx = grains[i].position.x - receiverPoint.x;
            float dy = grains[i].position.y - receiverPoint.y;
            float dz = grains[i].position.z - receiverPoint.z;
            float distToPlane = dx * rNorm.x + dy * rNorm.y + dz * rNorm.z;
            float pen = grains[i].radius - distToPlane;

            bool groundContact = false;
            if (pen > 0.0f) {
                groundContact = true;
                grains[i].position.x += rNorm.x * pen;
                grains[i].position.y += rNorm.y * pen;
                grains[i].position.z += rNorm.z * pen;

                float vn = grains[i].velocity.x * rNorm.x +
                           grains[i].velocity.y * rNorm.y +
                           grains[i].velocity.z * rNorm.z;
                float vtx = grains[i].velocity.x - vn * rNorm.x;
                float vty = grains[i].velocity.y - vn * rNorm.y;
                float vtz = grains[i].velocity.z - vn * rNorm.z;

                if (vn < 0.0f) {
                    float impactSpeed = -vn;
                    float restitution = (impactSpeed > 1.2f) ? mat->saltationSplash * 0.45f : 0.06f;
                    vn = impactSpeed * restitution;

                    // Tangential Coulomb friction on ground impact
                    float vtLen = sqrtf(vtx * vtx + vty * vty + vtz * vtz);
                    if (vtLen > 1e-4f) {
                        float frictionLoss = fminf(1.0f, mat->shearFrictionMu * 0.72f);
                        float keep = 1.0f - frictionLoss;
                        totalShearWork += frictionLoss * vtLen * impactSpeed * 0.1f;
                        vtx *= keep;
                        vty *= keep;
                        vtz *= keep;
                    }
                } else {
                    float damp = expf(-(6.0f + 14.0f * mat->shearFrictionMu) * subDt);
                    vtx *= damp;
                    vty *= damp;
                    vtz *= damp;
                }

                grains[i].velocity.x = vtx + rNorm.x * vn;
                grains[i].velocity.y = vty + rNorm.y * vn;
                grains[i].velocity.z = vtz + rNorm.z * vn;
            }

            // Angle-of-repose static jamming lock
            int totalContacts = s_coord[i] + (groundContact ? 1 : 0);
            float speedSq = grains[i].velocity.x * grains[i].velocity.x +
                            grains[i].velocity.y * grains[i].velocity.y +
                            grains[i].velocity.z * grains[i].velocity.z;
            float lockThresh = 0.08f + 0.18f * mat->shearFrictionMu;
            if (totalContacts >= 2 && speedSq < lockThresh * lockThresh) {
                float lockDamp = expf(-(18.0f + 22.0f * mat->rollingResistance) * subDt);
                grains[i].velocity.x *= lockDamp;
                grains[i].velocity.y *= lockDamp;
                grains[i].velocity.z *= lockDamp;
                grains[i].flags = 1u;
            } else {
                grains[i].flags = groundContact ? 2u : 0u;
            }

            float ao = (float)totalContacts / 7.0f;
            if (ao > 1.0f) ao = 1.0f;
            grains[i].contactAO = ao;
            grains[i].lifetime -= subDt;
        }
    }
    return totalShearWork;
}

#endif // CORE_GRANULAR_DEM_CPU_INL
