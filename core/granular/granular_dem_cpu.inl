#ifndef CORE_GRANULAR_DEM_CPU_INL
#define CORE_GRANULAR_DEM_CPU_INL

#include "core/granular/granular_sim.h"
#include <math.h>
#include <string.h>

#define GRANULAR_CPU_GRID_DIM 32
#define GRANULAR_CPU_GRID_MASK (GRANULAR_CPU_GRID_DIM - 1)
#define GRANULAR_CPU_GRID_CELLS (GRANULAR_CPU_GRID_DIM * GRANULAR_CPU_GRID_DIM * GRANULAR_CPU_GRID_DIM)

static const GranularMaterialProfile k_granularMaterials[GRANULAR_MAT_COUNT] = {
    [GRANULAR_MAT_DESERT_SAND] = {
        .densityKgM3            = 1600.0f,
        .springStiffness        = 2600.0f,
        .dashpotDamping         = 105.0f,
        .shearFrictionMu        = 0.65f,   // tan(33 deg)
        .rollingResistance      = 0.25f,
        .cohesion               = 0.0f,
        .airDrag                = 0.55f,
        .saltationSplash        = 0.28f,
        .dustEntrainmentRate    = 1.0f,
        .baseColor              = {232, 198, 136, 255},
        .shadowColor            = {148, 114, 72,  255},
        .highlightColor         = {255, 248, 224, 255},
        .glintIntensity         = 1.45f,
        .glintSharpness         = 128.0f,
        .grainAspect            = 0.85f,
        .morphology             = GRANULAR_MORPH_FINE_CONTINUUM,
        .referenceMassKg        = 0.012f,
        .liftCoefficient        = 0.0f,
        .flutterFreqHz          = 0.0f,
        .criticalEntrainmentMps = 2.6f,
    },
    [GRANULAR_MAT_COARSE_GRAVEL] = {
        .densityKgM3            = 2400.0f,
        .springStiffness        = 3200.0f,
        .dashpotDamping         = 115.0f,
        .shearFrictionMu        = 0.84f,   // tan(40 deg)
        .rollingResistance      = 0.45f,
        .cohesion               = 0.0f,
        .airDrag                = 0.22f,
        .saltationSplash        = 0.14f,
        .dustEntrainmentRate    = 0.65f,
        .baseColor              = {138, 128, 116, 255},
        .shadowColor            = {48,  44,  40,  255},
        .highlightColor         = {220, 214, 204, 255},
        .glintIntensity         = 0.45f,
        .glintSharpness         = 48.0f,
        .grainAspect            = 0.62f,
        .morphology             = GRANULAR_MORPH_ANGULAR_CLOD,
        .referenceMassKg        = 0.035f,
        .liftCoefficient        = 0.0f,
        .flutterFreqHz          = 0.0f,
        .criticalEntrainmentMps = 7.8f,
    },
    [GRANULAR_MAT_LOESS_SOIL] = {
        .densityKgM3            = 1450.0f,
        .springStiffness        = 2200.0f,
        .dashpotDamping         = 120.0f,
        .shearFrictionMu        = 1.00f,   // tan(45 deg)
        .rollingResistance      = 0.55f,
        .cohesion               = 18.0f,
        .airDrag                = 0.48f,
        .saltationSplash        = 0.10f,
        .dustEntrainmentRate    = 1.35f,
        .baseColor              = {118, 88,  58,  255},
        .shadowColor            = {42,  30,  20,  255},
        .highlightColor         = {178, 148, 108, 255},
        .glintIntensity         = 0.15f,
        .glintSharpness         = 24.0f,
        .grainAspect            = 0.78f,
        .morphology             = GRANULAR_MORPH_ANGULAR_CLOD,
        .referenceMassKg        = 0.020f,
        .liftCoefficient        = 0.0f,
        .flutterFreqHz          = 0.0f,
        .criticalEntrainmentMps = 4.4f,
    },
    [GRANULAR_MAT_GOLD_IRON_SAND] = {
        .densityKgM3            = 4800.0f,
        .springStiffness        = 3400.0f,
        .dashpotDamping         = 110.0f,
        .shearFrictionMu        = 0.73f,   // tan(36 deg)
        .rollingResistance      = 0.32f,
        .cohesion               = 0.0f,
        .airDrag                = 0.15f,
        .saltationSplash        = 0.22f,
        .dustEntrainmentRate    = 0.55f,
        .baseColor              = {198, 154, 58,  255},
        .shadowColor            = {72,  48,  16,  255},
        .highlightColor         = {255, 232, 130, 255},
        .glintIntensity         = 2.10f,
        .glintSharpness         = 160.0f,
        .grainAspect            = 0.80f,
        .morphology             = GRANULAR_MORPH_FINE_CONTINUUM,
        .referenceMassKg        = 0.032f,
        .liftCoefficient        = 0.0f,
        .flutterFreqHz          = 0.0f,
        .criticalEntrainmentMps = 5.5f,
    },
    [GRANULAR_MAT_VOLCANIC_ASH] = {
        .densityKgM3            = 950.0f,
        .springStiffness        = 1900.0f,
        .dashpotDamping         = 125.0f,
        .shearFrictionMu        = 0.58f,   // tan(30 deg)
        .rollingResistance      = 0.20f,
        .cohesion               = 6.0f,
        .airDrag                = 1.15f,
        .saltationSplash        = 0.32f,
        .dustEntrainmentRate    = 1.60f,
        .baseColor              = {68,  62,  60,  255},
        .shadowColor            = {22,  18,  18,  255},
        .highlightColor         = {255, 130, 48,  255},
        .glintIntensity         = 0.85f,
        .glintSharpness         = 64.0f,
        .grainAspect            = 0.72f,
        .morphology             = GRANULAR_MORPH_FINE_CONTINUUM,
        .referenceMassKg        = 0.004f,
        .liftCoefficient        = 0.15f,
        .flutterFreqHz          = 0.0f,
        .criticalEntrainmentMps = 1.4f,
    },
    [GRANULAR_MAT_DRY_LEAF] = {
        .densityKgM3            = 320.0f,
        .springStiffness        = 1400.0f,
        .dashpotDamping         = 115.0f,
        .shearFrictionMu        = 0.42f,
        .rollingResistance      = 0.65f,
        .cohesion               = 0.0f,
        .airDrag                = 2.60f,
        .saltationSplash        = 0.08f,
        .dustEntrainmentRate    = 0.20f,
        .baseColor              = {196, 122, 44,  255},
        .shadowColor            = {88,  46,  18,  255},
        .highlightColor         = {245, 198, 92,  255},
        .glintIntensity         = 0.35f,
        .glintSharpness         = 24.0f,
        .grainAspect            = 0.52f,
        .morphology             = GRANULAR_MORPH_PLANAR_LAMINA,
        .referenceMassKg        = 0.0008f,
        .liftCoefficient        = 1.35f,
        .flutterFreqHz          = 4.2f,
        .criticalEntrainmentMps = 0.85f,
    },
    [GRANULAR_MAT_FRESH_LEAF] = {
        .densityKgM3            = 680.0f,
        .springStiffness        = 1600.0f,
        .dashpotDamping         = 118.0f,
        .shearFrictionMu        = 0.48f,
        .rollingResistance      = 0.55f,
        .cohesion               = 0.0f,
        .airDrag                = 2.15f,
        .saltationSplash        = 0.06f,
        .dustEntrainmentRate    = 0.10f,
        .baseColor              = {62,  168, 76,  255},
        .shadowColor            = {22,  68,  32,  255},
        .highlightColor         = {180, 245, 130, 255},
        .glintIntensity         = 0.55f,
        .glintSharpness         = 36.0f,
        .grainAspect            = 0.58f,
        .morphology             = GRANULAR_MORPH_PLANAR_LAMINA,
        .referenceMassKg        = 0.0014f,
        .liftCoefficient        = 1.15f,
        .flutterFreqHz          = 3.4f,
        .criticalEntrainmentMps = 1.15f,
    },
    [GRANULAR_MAT_DRY_TWIG] = {
        .densityKgM3            = 720.0f,
        .springStiffness        = 2400.0f,
        .dashpotDamping         = 120.0f,
        .shearFrictionMu        = 0.78f,
        .rollingResistance      = 0.72f,
        .cohesion               = 0.0f,
        .airDrag                = 0.95f,
        .saltationSplash        = 0.12f,
        .dustEntrainmentRate    = 0.25f,
        .baseColor              = {108, 76,  48,  255},
        .shadowColor            = {40,  26,  16,  255},
        .highlightColor         = {166, 128, 90,  255},
        .glintIntensity         = 0.18f,
        .glintSharpness         = 18.0f,
        .grainAspect            = 0.28f,
        .morphology             = GRANULAR_MORPH_ELONGATED_TWIG,
        .referenceMassKg        = 0.0045f,
        .liftCoefficient        = 0.20f,
        .flutterFreqHz          = 1.6f,
        .criticalEntrainmentMps = 2.40f,
    },
    [GRANULAR_MAT_FLOWER_PETAL] = {
        .densityKgM3            = 420.0f,
        .springStiffness        = 1250.0f,
        .dashpotDamping         = 112.0f,
        .shearFrictionMu        = 0.36f,
        .rollingResistance      = 0.45f,
        .cohesion               = 0.0f,
        .airDrag                = 2.95f,
        .saltationSplash        = 0.05f,
        .dustEntrainmentRate    = 0.05f,
        .baseColor              = {255, 168, 202, 255},
        .shadowColor            = {165, 68,  112, 255},
        .highlightColor         = {255, 236, 246, 255},
        .glintIntensity         = 0.42f,
        .glintSharpness         = 28.0f,
        .grainAspect            = 0.72f,
        .morphology             = GRANULAR_MORPH_PLANAR_LAMINA,
        .referenceMassKg        = 0.0005f,
        .liftCoefficient        = 1.50f,
        .flutterFreqHz          = 4.8f,
        .criticalEntrainmentMps = 0.65f,
    },
    [GRANULAR_MAT_ICE_SHARD] = {
        .densityKgM3            = 917.0f,
        .springStiffness        = 3300.0f,
        .dashpotDamping         = 95.0f,
        .shearFrictionMu        = 0.18f,   // Very low ice-on-ice friction
        .rollingResistance      = 0.12f,
        .cohesion               = 0.0f,
        .airDrag                = 0.35f,
        .saltationSplash        = 0.36f,
        .dustEntrainmentRate    = 0.40f,
        .baseColor              = {155, 224, 255, 255},
        .shadowColor            = {42,  96,  148, 255},
        .highlightColor         = {255, 255, 255, 255},
        .glintIntensity         = 2.60f,
        .glintSharpness         = 192.0f,
        .grainAspect            = 0.42f,
        .morphology             = GRANULAR_MORPH_CRYSTAL_SHARD,
        .referenceMassKg        = 0.015f,
        .liftCoefficient        = 0.05f,
        .flutterFreqHz          = 0.0f,
        .criticalEntrainmentMps = 3.2f,
    },
    [GRANULAR_MAT_SNOW_POWDER] = {
        .densityKgM3            = 380.0f,
        .springStiffness        = 1550.0f,
        .dashpotDamping         = 130.0f,
        .shearFrictionMu        = 0.76f,
        .rollingResistance      = 0.60f,
        .cohesion               = 22.0f,
        .airDrag                = 1.85f,
        .saltationSplash        = 0.14f,
        .dustEntrainmentRate    = 1.20f,
        .baseColor              = {238, 246, 255, 255},
        .shadowColor            = {128, 162, 206, 255},
        .highlightColor         = {255, 255, 255, 255},
        .glintIntensity         = 1.75f,
        .glintSharpness         = 160.0f,
        .grainAspect            = 0.88f,
        .morphology             = GRANULAR_MORPH_FINE_CONTINUUM,
        .referenceMassKg        = 0.0018f,
        .liftCoefficient        = 0.25f,
        .flutterFreqHz          = 2.2f,
        .criticalEntrainmentMps = 1.05f,
    },
};

const GranularMaterialProfile *GranularSim_GetMaterialProfile(GranularMaterialId id)
{
    if (id < 0 || id >= GRANULAR_MAT_COUNT) id = GRANULAR_MAT_DESERT_SAND;
    return &k_granularMaterials[id];
}

static inline int GranularCpu_CellIndex(Vector3 p, Vector3 origin, float cellSize)
{
    int gx = ((int)floorf((p.x - origin.x) / cellSize)) & GRANULAR_CPU_GRID_MASK;
    int gy = ((int)floorf((p.y - origin.y) / cellSize)) & GRANULAR_CPU_GRID_MASK;
    int gz = ((int)floorf((p.z - origin.z) / cellSize)) & GRANULAR_CPU_GRID_MASK;
    return (gz * GRANULAR_CPU_GRID_DIM + gy) * GRANULAR_CPU_GRID_DIM + gx;
}

static inline float GranularCpu_WaveEnvelope(const GranularWaveDesc *w)
{
    if (!w || w->kind == GRANULAR_WAVE_NONE || w->duration <= 0.0f) return 0.0f;
    if (w->elapsed < 0.0f || w->elapsed >= w->duration) return 0.0f;
    float env = 1.0f;
    if (w->attackTime > 1e-4f && w->elapsed < w->attackTime) {
        float t = w->elapsed / w->attackTime;
        env *= t * t * (3.0f - 2.0f * t);
    }
    if (w->fadeTime > 1e-4f && (w->duration - w->elapsed) < w->fadeTime) {
        float t = (w->duration - w->elapsed) / w->fadeTime;
        env *= t * t * (3.0f - 2.0f * t);
    }
    return env;
}

static inline void GranularCpu_EvaluateWaves(GranularGrainState *g,
                                             const GranularMaterialProfile *mat,
                                             const GranularWaveDesc *waves,
                                             int waveCount,
                                             float elapsedTime,
                                             Vector3 *ioAcc,
                                             float *outEffWindSpeed)
{
    float maxEffWind = 0.0f;
    if (!waves || waveCount <= 0) {
        *outEffWindSpeed = 0.0f;
        return;
    }

    for (int wi = 0; wi < waveCount; wi++) {
        const GranularWaveDesc *w = &waves[wi];
        float env = GranularCpu_WaveEnvelope(w);
        if (env <= 1e-4f) continue;

        if (w->kind == GRANULAR_WAVE_RADIAL_IMPULSE) {
            Vector3 rel = {
                g->position.x - w->origin.x,
                g->position.y - w->origin.y,
                g->position.z - w->origin.z
            };
            float d = sqrtf(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
            float rad = w->radius > 0.05f ? w->radius : 1.0f;
            if (d < rad) {
                float falloff = 1.0f - d / rad;
                falloff *= falloff * env;
                ioAcc->x += w->direction.x * w->axialSpeed * falloff;
                ioAcc->y += (w->direction.y * w->axialSpeed + w->liftAccel) * falloff;
                ioAcc->z += w->direction.z * w->axialSpeed * falloff;
                if (fabsf(w->swirlSpeed) > 1e-3f) {
                    float tl = sqrtf(rel.x * rel.x + rel.z * rel.z);
                    if (tl > 1e-4f) {
                        float tx = -rel.z / tl;
                        float tz =  rel.x / tl;
                        ioAcc->x += tx * w->swirlSpeed * falloff * 4.0f;
                        ioAcc->z += tz * w->swirlSpeed * falloff * 4.0f;
                    }
                }
                if (fabsf(w->inwardPull) > 1e-3f && d > 1e-4f) {
                    float pull = -w->inwardPull * falloff / d;
                    ioAcc->x += rel.x * pull;
                    ioAcc->y += rel.y * pull;
                    ioAcc->z += rel.z * pull;
                }
                float localWind = (fabsf(w->swirlSpeed) + fabsf(w->liftAccel) * 0.45f) * falloff;
                if (localWind > maxEffWind) maxEffWind = localWind;
            }
        } else if (w->kind == GRANULAR_WAVE_CONICAL_FUNNEL) {
            float spinDown = w->spinDown;
            if (spinDown <= 0.0f && w->fadeTime > 1e-4f && (w->duration - w->elapsed) < w->fadeTime) {
                spinDown = 1.0f - (w->duration - w->elapsed) / w->fadeTime;
            }
            if (spinDown < 0.0f) spinDown = 0.0f; else if (spinDown > 1.0f) spinDown = 1.0f;
            float confineEnv = spinDown > 0.0f ? (1.0f - spinDown * 0.25f) : env;

            float h = g->position.y - w->origin.y;
            float funnelH = w->length > 0.25f ? w->length : 3.0f;
            if (w->loopTop && spinDown < 0.25f && h > funnelH) {
                float f1 = fmodf(g->seed * 13.7f + elapsedTime * 3.1f, 1.0f);
                float aWrap = f1 * 6.2831853f;
                float rWrap = w->radius * (0.35f + 0.65f * fmodf(g->seed * 29.3f, 1.0f));
                g->position.x = w->origin.x + cosf(aWrap) * rWrap;
                g->position.z = w->origin.z + sinf(aWrap) * rWrap;
                g->position.y = w->origin.y + 0.04f + 0.28f * fmodf(g->seed * 7.1f, 1.0f);
                g->velocity = (Vector3){
                    w->stormVelocity.x - sinf(aWrap) * w->swirlSpeed,
                    w->stormVelocity.y + w->axialSpeed * 0.85f,
                    w->stormVelocity.z + cosf(aWrap) * w->swirlSpeed
                };
                h = g->position.y - w->origin.y;
            }
            float hn = h / funnelH;
            if (hn < 0.0f) hn = 0.0f; else if (hn > 1.0f) hn = 1.0f;
            float coneR = w->radius + (w->topRadius - w->radius) * powf(hn, 1.35f);
            float rBias = 0.18f + 0.87f * fmodf(g->seed * 11.37f, 1.0f);
            float targetR = coneR * rBias * (1.0f - 0.35f * spinDown);
            float sx = sinf(hn * 4.5f - elapsedTime * 6.0f) * 0.24f * hn * (1.0f - spinDown);
            float sz = cosf(hn * 3.8f - elapsedTime * 5.2f) * 0.24f * hn * (1.0f - spinDown);
            float rx = g->position.x - (w->origin.x + sx);
            float rz = g->position.z - (w->origin.z + sz);
            float r = sqrtf(rx * rx + rz * rz);
            float radX = r > 1e-4f ? rx / r : cosf(g->seed * 6.2831853f);
            float radZ = r > 1e-4f ? rz / r : sinf(g->seed * 6.2831853f);
            float tangX = -radZ, tangZ = radX;

            // Storm-relative velocity prevents trailing curtain smear when moving
            float vRelX = g->velocity.x - w->stormVelocity.x;
            float vRelY = g->velocity.y - w->stormVelocity.y;
            float vRelZ = g->velocity.z - w->stormVelocity.z;
            float vTang = vRelX * tangX + vRelZ * tangZ;
            float vRad  = vRelX * radX  + vRelZ * radZ;
            float rRatio = r / (coneR > 0.15f ? coneR : 0.15f);
            if (rRatio < 0.25f) rRatio = 0.25f; else if (rRatio > 1.0f) rRatio = 1.0f;

            float targetSwirl = w->swirlSpeed * rRatio * (1.0f - spinDown);
            float tangAcc = (targetSwirl - vTang) * 10.5f * confineEnv;
            float centripetalAcc = -(vTang * vTang) / (r > 0.08f ? r : 0.08f);
            float radAcc = (centripetalAcc + (targetR - r) * 55.0f - vRad * 11.0f) * confineEnv;
            float targetVy = w->axialSpeed * (0.75f + 0.50f * fmodf(g->seed * 5.3f, 1.0f)) * (1.0f - spinDown) - 2.2f * spinDown;
            float yAcc = (9.81f * (1.0f - 0.65f * spinDown) + (targetVy - vRelY) * 7.0f) * confineEnv;
            float trackK = 7.5f * confineEnv;

            ioAcc->x += radX * radAcc + tangX * tangAcc + (w->stormVelocity.x - g->velocity.x) * trackK * 0.35f;
            ioAcc->y += yAcc;
            ioAcc->z += radZ * radAcc + tangZ * tangAcc + (w->stormVelocity.z - g->velocity.z) * trackK * 0.35f;
            float localWind = fabsf(w->swirlSpeed) * (1.0f - spinDown) * confineEnv;
            if (localWind > maxEffWind) maxEffWind = localWind;
        } else if (w->kind == GRANULAR_WAVE_SPIRAL_CONVERGE) {
            Vector3 rel = {
                g->position.x - w->origin.x,
                g->position.y - w->origin.y,
                g->position.z - w->origin.z
            };
            float rXZ = sqrtf(rel.x * rel.x + rel.z * rel.z);
            float maxR = w->radius > 0.2f ? w->radius : 3.5f;
            if (rXZ < maxR) {
                float spatialW = 1.0f - rXZ / maxR;
                spatialW = spatialW * (2.0f - spatialW) * env;
                float lock = (w->analyticLock > 0.0f ? w->analyticLock : 0.75f) * spatialW;
                float invR = rXZ > 1e-4f ? 1.0f / rXZ : 1.0f;
                float radX = rXZ > 1e-4f ? rel.x * invR : cosf(g->seed * 6.2831853f);
                float radZ = rXZ > 1e-4f ? rel.z * invR : sinf(g->seed * 6.2831853f);
                float tangX = -radZ, tangZ = radX;

                // Kinodynamic target state (x*, v*, a_ff*) for inward accretion spiral
                float targetH = 0.28f + 0.85f * (1.0f - rXZ / maxR) + 0.18f * fmodf(g->seed * 19.1f, 1.0f);
                float vTangTarget = w->swirlSpeed * (0.45f + 0.55f * (1.0f - rXZ / maxR));
                float vRadTarget  = -w->inwardPull * 0.28f;
                float vTang = g->velocity.x * tangX + g->velocity.z * tangZ;
                float vRad  = g->velocity.x * radX  + g->velocity.z * radZ;

                float centripetal = -(vTang * vTang) / (rXZ > 0.12f ? rXZ : 0.12f);
                float radAcc  = (centripetal + (vRadTarget - vRad) * 6.5f - w->inwardPull * 0.45f) * lock;
                float tangAcc = (vTangTarget - vTang) * 8.5f * lock;

                // Lightweight grains (leaves/petals) lift smoothly while heavy gravel resists
                float massRatio = 0.002f / (mat->referenceMassKg > 0.0002f ? mat->referenceMassKg : 0.002f);
                if (massRatio > 2.2f) massRatio = 2.2f;
                float liftBoost = (9.81f + (targetH - rel.y) * 18.0f - g->velocity.y * 5.0f + w->liftAccel) * lock * massRatio;

                ioAcc->x += radX * radAcc + tangX * tangAcc;
                ioAcc->y += liftBoost;
                ioAcc->z += radZ * radAcc + tangZ * tangAcc;

                float localWind = (fabsf(w->swirlSpeed) + fabsf(w->inwardPull) * 0.4f) * spatialW;
                if (localWind > maxEffWind) maxEffWind = localWind;
            }
        } else if (w->kind == GRANULAR_WAVE_TRAVELLING_JET) {
            Vector3 dir = w->direction;
            float dLen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (dLen > 1e-4f) { dir.x /= dLen; dir.y /= dLen; dir.z /= dLen; }
            else { dir = (Vector3){1.0f, 0.0f, 0.0f}; }

            Vector3 rel = {
                g->position.x - w->origin.x,
                g->position.y - w->origin.y,
                g->position.z - w->origin.z
            };
            float s = rel.x * dir.x + rel.y * dir.y + rel.z * dir.z;
            float frontDist = w->waveSpeed * w->elapsed;
            float maxLen = w->length > 0.5f ? w->length : 12.0f;

            if (s >= -0.6f && s <= maxLen && s <= frontDist + 0.85f) {
                Vector3 perp = {
                    rel.x - dir.x * s,
                    rel.y - dir.y * s,
                    rel.z - dir.z * s
                };
                float rPerp = sqrtf(perp.x * perp.x + perp.y * perp.y + perp.z * perp.z);
                float sNorm = s > 0.0f ? (s / maxLen) : 0.0f;
                float tubeR = w->radius + (w->topRadius - w->radius) * sNorm;
                if (tubeR < 0.2f) tubeR = 0.2f;

                if (rPerp < tubeR) {
                    float radW = 1.0f - rPerp / tubeR;
                    radW = radW * (2.0f - radW);
                    // Wavefront pulse peak right behind frontDist + sustained slipstream wake
                    float behind = frontDist - s;
                    float frontPulse = expf(-behind * behind * 1.4f);
                    float wake = behind >= 0.0f ? (0.45f + 0.55f * frontPulse) : (1.0f - fminf((-behind) / 0.85f, 1.0f));
                    float wTot = radW * wake * env;
                    float lock = (w->analyticLock > 0.0f ? w->analyticLock : 0.80f) * wTot;

                    Vector3 radDir = rPerp > 1e-4f
                        ? (Vector3){perp.x / rPerp, perp.y / rPerp, perp.z / rPerp}
                        : (Vector3){0.0f, 1.0f, 0.0f};
                    Vector3 tangDir = {
                        dir.y * radDir.z - dir.z * radDir.y,
                        dir.z * radDir.x - dir.x * radDir.z,
                        dir.x * radDir.y - dir.y * radDir.x
                    };

                    // Helical manifold velocity target + centripetal lock
                    float targetFwd = w->axialSpeed * (0.75f + 0.40f * frontPulse);
                    float vFwd = g->velocity.x * dir.x + g->velocity.y * dir.y + g->velocity.z * dir.z;
                    float fwdAcc = (targetFwd - vFwd) * 9.5f * lock;

                    float targetSwirl = w->swirlSpeed * (0.35f + 0.65f * (rPerp / tubeR));
                    float vTang = g->velocity.x * tangDir.x + g->velocity.y * tangDir.y + g->velocity.z * tangDir.z;
                    float tangAcc = (targetSwirl - vTang) * 8.0f * lock;

                    float targetR = tubeR * (0.28f + 0.52f * fmodf(g->seed * 17.3f, 1.0f));
                    float vRad = g->velocity.x * radDir.x + g->velocity.y * radDir.y + g->velocity.z * radDir.z;
                    float centripetal = -(vTang * vTang) / (rPerp > 0.10f ? rPerp : 0.10f);
                    float radAcc = (centripetal + (targetR - rPerp) * 18.0f - vRad * 6.0f) * lock;

                    float massRatio = 0.002f / (mat->referenceMassKg > 0.0002f ? mat->referenceMassKg : 0.002f);
                    if (massRatio > 2.2f) massRatio = 2.2f;
                    float liftKick = (w->liftAccel * (0.4f + 0.6f * frontPulse) + 9.81f * 0.85f) * wTot * massRatio;

                    ioAcc->x += dir.x * fwdAcc + tangDir.x * tangAcc + radDir.x * radAcc;
                    ioAcc->y += dir.y * fwdAcc + tangDir.y * tangAcc + radDir.y * radAcc + liftKick;
                    ioAcc->z += dir.z * fwdAcc + tangDir.z * tangAcc + radDir.z * radAcc;

                    float localWind = (w->axialSpeed + fabsf(w->swirlSpeed) * 0.5f) * wTot;
                    if (localWind > maxEffWind) maxEffWind = localWind;
                }
            }
        }
    }

    *outEffWindSpeed = maxEffWind;
}

float GranularSim_StepCpuDeterministicWithWaves(GranularGrainState *grains, int count,
                                                const GranularMaterialProfile *matOverride,
                                                GranularWaveDesc *waves, int waveCount,
                                                float dt, float elapsedTime,
                                                Vector3 receiverPoint, Vector3 receiverNormal)
{
    if (!grains || count <= 0 || dt <= 0.0f) return 0.0f;
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
        float simTime = elapsedTime + (float)step * subDt;
        if (waves && waveCount > 0) {
            for (int wi = 0; wi < waveCount; wi++) {
                if (waves[wi].kind != GRANULAR_WAVE_NONE) waves[wi].elapsed += subDt;
            }
        }

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

            const GranularMaterialProfile *mat = matOverride
                ? matOverride
                : GranularSim_GetMaterialProfile((GranularMaterialId)grains[i].materialId);

            Vector3 posI = grains[i].position;
            Vector3 velI = grains[i].velocity;
            float radI = grains[i].radius;

            // Gravity + aerodynamic drag
            Vector3 acc = {
                -velI.x * mat->airDrag,
                -9.81f - velI.y * mat->airDrag,
                -velI.z * mat->airDrag
            };

            // Planar aerofoil flutter & cross-flow lift for leaves & petals while airborne
            if (mat->morphology == GRANULAR_MORPH_PLANAR_LAMINA && grains[i].flags != 1u) {
                float phase = simTime * mat->flutterFreqHz * 6.2831853f + grains[i].seed * 37.7f;
                float speedXZ = sqrtf(velI.x * velI.x + velI.z * velI.z);
                float lift = mat->liftCoefficient * (1.8f + 0.85f * speedXZ);
                if (lift > 8.2f) lift = 8.2f; // Terminal glide cap < g
                acc.y += lift;
                acc.x += cosf(phase) * mat->liftCoefficient * 2.4f;
                acc.z += sinf(phase * 1.13f) * mat->liftCoefficient * 2.4f;
            }

            // Unified Kinodynamic Wave evaluation + Shields entrainment gate
            float effWind = 0.0f;
            Vector3 waveAcc = {0.0f, 0.0f, 0.0f};
            GranularCpu_EvaluateWaves(&grains[i], mat, waves, waveCount, simTime, &waveAcc, &effWind);
            if (grains[i].flags == 1u) {
                if (effWind >= mat->criticalEntrainmentMps) {
                    grains[i].flags = 0u; // Break static repose lock!
                    acc.x += waveAcc.x;
                    acc.y += waveAcc.y + 1.5f;
                    acc.z += waveAcc.z;
                }
            } else {
                acc.x += waveAcc.x;
                acc.y += waveAcc.y;
                acc.z += waveAcc.z;
            }

            int cell = GranularCpu_CellIndex(posI, receiverPoint, cellSize);
            int cz = cell / (GRANULAR_CPU_GRID_DIM * GRANULAR_CPU_GRID_DIM);
            int cy = (cell / GRANULAR_CPU_GRID_DIM) % GRANULAR_CPU_GRID_DIM;
            int cx = cell % GRANULAR_CPU_GRID_DIM;

            int contacts = 0;
            for (int dz = -1; dz <= 1; dz++) {
                int nz = (cz + dz) & GRANULAR_CPU_GRID_MASK;
                for (int dy = -1; dy <= 1; dy++) {
                    int ny = (cy + dy) & GRANULAR_CPU_GRID_MASK;
                    for (int dx = -1; dx <= 1; dx++) {
                        int nx = (cx + dx) & GRANULAR_CPU_GRID_MASK;
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

            const GranularMaterialProfile *mat = matOverride
                ? matOverride
                : GranularSim_GetMaterialProfile((GranularMaterialId)grains[i].materialId);

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

            // Angle-of-repose static jamming lock (thin lamina/twigs settle on 1 ground contact)
            int totalContacts = s_coord[i] + (groundContact ? 1 : 0);
            int minContactsToLock = (mat->morphology == GRANULAR_MORPH_PLANAR_LAMINA ||
                                     mat->morphology == GRANULAR_MORPH_ELONGATED_TWIG) ? 1 : 2;
            float speedSq = grains[i].velocity.x * grains[i].velocity.x +
                            grains[i].velocity.y * grains[i].velocity.y +
                            grains[i].velocity.z * grains[i].velocity.z;
            float lockThresh = 0.08f + 0.18f * mat->shearFrictionMu;
            if (totalContacts >= minContactsToLock && speedSq < lockThresh * lockThresh) {
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

float GranularSim_StepCpuDeterministic(GranularGrainState *grains, int count,
                                       const GranularMaterialProfile *mat,
                                       float dt, Vector3 receiverPoint,
                                       Vector3 receiverNormal)
{
    return GranularSim_StepCpuDeterministicWithWaves(grains, count, mat, NULL, 0,
                                                     dt, 0.0f, receiverPoint, receiverNormal);
}

#endif // CORE_GRANULAR_DEM_CPU_INL
