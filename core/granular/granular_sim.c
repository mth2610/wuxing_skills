#include "core/granular/granular_sim.h"
#include "core/granular/granular_gpu.h"
#include "core/granular/granular_dem_cpu.inl"
#include "core/motion/motion_fields.h"
#include "core/vfx_render.h"
#include "core/gfx_quality.h"
#if !defined(CORE_GRANULAR_HEADLESS_TEST)
#include "core/map_manager.h"
#endif
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

static GranularGrainState s_cpuGrains[GRANULAR_CPU_MAX_GRAINS];
static int s_cpuGrainCount = 0;
static bool s_granularInit = false;
static bool s_usingGpu = false;
static GranularMaterialId s_activeMatId = GRANULAR_MAT_DESERT_SAND;
static Vector3 s_receiverPoint = {0.0f, 0.0f, 0.0f};
static Vector3 s_receiverNormal = {0.0f, 1.0f, 0.0f};
static float s_totalShearWork = 0.0f;
static float s_simElapsedTime = 0.0f;

static GranularWaveDesc s_waves[GRANULAR_MAX_ACTIVE_WAVES];
static int s_waveCount = 0;

static Vector3 s_cpuImpCenter = {0};
static float s_cpuImpRadius = 0.0f;
static Vector3 s_cpuImpAccel = {0};
static float s_cpuImpSwirl = 0.0f;

static bool s_cpuFunnelActive = false;
static Vector3 s_cpuFunnelBase = {0};
static Vector3 s_cpuFunnelPrevBase = {0};
static bool s_cpuFunnelHasPrev = false;
static Vector3 s_cpuFunnelVel = {0};
static float s_cpuFunnelSpinDown = 0.0f;
static float s_cpuFunnelSpinDownT = 0.0f;
#define GRANULAR_FUNNEL_SPINDOWN_SEC 0.55f
static float s_cpuFunnelBaseR = 0.42f;
static float s_cpuFunnelTopR = 1.65f;
static float s_cpuFunnelH = 3.6f;
static float s_cpuFunnelSwirl = 13.5f;
static float s_cpuFunnelUpdraft = 3.4f;

#define GRANULAR_BED_GRID_DIM 32
static GranularGrainState s_mapBed[GRANULAR_MAP_BED_MAX];
static unsigned char s_bedAwake[GRANULAR_MAP_BED_MAX];
static int s_bedNext[GRANULAR_MAP_BED_MAX];
static int s_bedCellHead[GRANULAR_BED_GRID_DIM * GRANULAR_BED_GRID_DIM];
static int s_mapBedCount = 0;
static float s_bedMinX = -20.0f, s_bedMinZ = -20.0f, s_bedCellSpan = 1.25f;

static float GranularSim_Hash01(unsigned int x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)(x & 0x00ffffffu) / 16777216.0f;
}

bool GranularSim_Init(void)
{
    if (s_granularInit) return true;
    s_granularInit = true;
    s_usingGpu = GranularGpu_Init();
    return true;
}

void GranularSim_Unload(void)
{
    GranularGpu_Unload();
    s_cpuGrainCount = 0;
    s_usingGpu = false;
    s_granularInit = false;
    s_totalShearWork = 0.0f;
}

void GranularSim_Reset(void)
{
    s_cpuGrainCount = 0;
    s_totalShearWork = 0.0f;
    s_simElapsedTime = 0.0f;
    s_waveCount = 0;
    memset(s_waves, 0, sizeof(s_waves));
    GranularGpu_Clear();
}

int GranularSim_SpawnCluster(const GranularGrainState *grains, int count,
                             Vector3 receiverPoint, Vector3 receiverNormal)
{
    if (!grains || count <= 0) return 0;
    if (!s_granularInit) GranularSim_Init();

    GranularMaterialId matId = (GranularMaterialId)grains[0].materialId;
    if (matId < 0 || matId >= GRANULAR_MAT_COUNT) matId = GRANULAR_MAT_DESERT_SAND;
    s_activeMatId = matId;
    const GranularMaterialProfile *mat = GranularSim_GetMaterialProfile(matId);

    s_receiverPoint = receiverPoint;
    s_receiverNormal = Vector3LengthSqr(receiverNormal) > 1e-5f
        ? Vector3Normalize(receiverNormal) : (Vector3){0.0f, 1.0f, 0.0f};

    float maxR = 0.03f;
    for (int i = 0; i < count; i++) {
        if (grains[i].radius > maxR) maxR = grains[i].radius;
    }

    if (s_usingGpu && GranularGpu_IsAvailable()) {
        int cap = GfxQuality_Get() <= GFX_LOW ? 1024 : (GfxQuality_Get() >= GFX_HIGH ? GRANULAR_GPU_MAX_GRAINS : 2048);
        int spawnN = count > cap ? cap : count;
        GranularGpu_UploadGrains(grains, spawnN, s_receiverPoint, s_receiverNormal, maxR * 2.2f, mat);
        s_cpuGrainCount = 0;
        return spawnN;
    }

    int spawnN = count > GRANULAR_CPU_MAX_GRAINS ? GRANULAR_CPU_MAX_GRAINS : count;
    memcpy(s_cpuGrains, grains, (size_t)spawnN * sizeof(GranularGrainState));
    s_cpuGrainCount = spawnN;
    return spawnN;
}

int GranularSim_AppendCluster(const GranularGrainState *grains, int count,
                              Vector3 receiverPoint, Vector3 receiverNormal)
{
    if (!grains || count <= 0) return 0;
    if (!s_granularInit) GranularSim_Init();

    if (GranularSim_GetActiveCount() <= 0) {
        return GranularSim_SpawnCluster(grains, count, receiverPoint, receiverNormal);
    }

    float maxR = 0.03f;
    for (int i = 0; i < count; i++) {
        if (grains[i].radius > maxR) maxR = grains[i].radius;
    }

    if (s_usingGpu && GranularGpu_IsAvailable()) {
        return GranularGpu_AppendGrains(grains, count, receiverPoint, receiverNormal, maxR * 2.2f);
    }

    int avail = GRANULAR_CPU_MAX_GRAINS - s_cpuGrainCount;
    if (avail <= 0) return 0;
    int addN = count > avail ? avail : count;
    memcpy(&s_cpuGrains[s_cpuGrainCount], grains, (size_t)addN * sizeof(GranularGrainState));
    s_cpuGrainCount += addN;
    return addN;
}

int GranularSim_ScatterBed(Vector3 center, float radius, int count,
                           const GranularMaterialId *palette, int paletteCount,
                           float grainRadius, float lifetime, unsigned int seed)
{
    if (count <= 0 || radius <= 0.0f) return 0;
    if (!palette || paletteCount <= 0) {
        static const GranularMaterialId k_defaultPal[1] = {GRANULAR_MAT_DRY_LEAF};
        palette = k_defaultPal;
        paletteCount = 1;
    }
    if (count > GRANULAR_GPU_MAX_GRAINS) count = GRANULAR_GPU_MAX_GRAINS;

    static GranularGrainState s_bedBuf[GRANULAR_GPU_MAX_GRAINS];
    float rBase = grainRadius > 0.004f ? grainRadius : 0.032f;
    float lifeBase = lifetime > 0.2f ? lifetime : 8.0f;

    for (int i = 0; i < count; i++) {
        unsigned int h = (unsigned int)i * 747796405u + seed * 2891336453u + 17u;
        float u0 = GranularSim_Hash01(h + 1u);
        float u1 = GranularSim_Hash01(h + 2u);
        float u2 = GranularSim_Hash01(h + 3u);
        float u3 = GranularSim_Hash01(h + 4u);

        float ang = u0 * 6.2831853f;
        float rad = sqrtf(u1) * radius;
        GranularMaterialId matId = palette[i % paletteCount];
        if (matId < 0 || matId >= GRANULAR_MAT_COUNT) matId = GRANULAR_MAT_DRY_LEAF;
        float r = rBase * (0.75f + 0.50f * u2);

        s_bedBuf[i] = (GranularGrainState){
            .position   = {center.x + cosf(ang) * rad, center.y + r, center.z + sinf(ang) * rad},
            .velocity   = {0.0f, 0.0f, 0.0f},
            .radius     = r,
            .lifetime   = lifeBase * (0.88f + 0.24f * u3),
            .seed       = u0,
            .contactAO  = 0.25f,
            .materialId = (uint32_t)matId,
            .flags      = 1u // Pre-settled resting bed ready for Shields entrainment
        };
    }

    return GranularSim_AppendCluster(s_bedBuf, count, center, (Vector3){0.0f, 1.0f, 0.0f});
}

int GranularSim_SpawnWave(const GranularWaveDesc *wave)
{
    if (!wave || wave->kind == GRANULAR_WAVE_NONE || wave->duration <= 0.0f) return -1;
    for (int i = 0; i < s_waveCount; i++) {
        if (s_waves[i].kind == GRANULAR_WAVE_NONE || s_waves[i].elapsed >= s_waves[i].duration) {
            s_waves[i] = *wave;
            s_waves[i].elapsed = 0.0f;
            return i;
        }
    }
    if (s_waveCount < GRANULAR_MAX_ACTIVE_WAVES) {
        int idx = s_waveCount++;
        s_waves[idx] = *wave;
        s_waves[idx].elapsed = 0.0f;
        return idx;
    }
    // Recycle oldest slot
    int oldest = 0;
    for (int i = 1; i < GRANULAR_MAX_ACTIVE_WAVES; i++) {
        if (s_waves[i].elapsed > s_waves[oldest].elapsed) oldest = i;
    }
    s_waves[oldest] = *wave;
    s_waves[oldest].elapsed = 0.0f;
    return oldest;
}

void GranularSim_ClearWaves(void)
{
    s_waveCount = 0;
    memset(s_waves, 0, sizeof(s_waves));
    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_SetWaves(NULL, 0);
    }
}

void GranularSim_ApplyForceImpulse(Vector3 center, float radius,
                                   Vector3 forceAccel, float swirlRadPerSec)
{
    if (s_mapBedCount > 0 && radius > 0.01f) {
        Vector3 wakeImp = {forceAccel.x * 0.12f, fmaxf(forceAccel.y * 0.12f, 1.4f), forceAccel.z * 0.12f};
        GranularSim_WakeMapBedNear(center, radius, wakeImp);
    }
    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_SetImpulse(center, radius, forceAccel, swirlRadPerSec);
    } else {
        s_cpuImpCenter = center;
        s_cpuImpRadius = radius;
        s_cpuImpAccel = forceAccel;
        s_cpuImpSwirl = swirlRadPerSec;
    }
}

void GranularSim_SetTornadoFunnel(Vector3 basePos, float baseRadius, float topRadius,
                                  float height, float swirlSpeed, float updraftSpeed,
                                  bool active)
{
    if (active) {
        s_cpuFunnelActive = true;
        s_cpuFunnelBase = basePos;
        s_cpuFunnelTopR = topRadius;
        s_cpuFunnelUpdraft = updraftSpeed;
    } else if (!s_usingGpu || !GranularGpu_IsAvailable()) {
        /* CPU spin-down handled below */
    } else {
        s_cpuFunnelActive = false;
    }

    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_SetTornadoFunnel(basePos, baseRadius, topRadius, height,
                                     swirlSpeed, updraftSpeed, active);
    } else if (active) {
        s_cpuFunnelActive = true;
        s_cpuFunnelSpinDown = 0.0f;
        s_cpuFunnelSpinDownT = 0.0f;
        if (s_cpuFunnelHasPrev) {
            float dtEst = 1.0f / 60.0f; /* skill calls once per frame */
            Vector3 v = Vector3Scale(Vector3Subtract(basePos, s_cpuFunnelPrevBase), 1.0f / dtEst);
            float spd = Vector3Length(v);
            if (spd > 25.0f) v = Vector3Scale(v, 25.0f / spd); /* reject teleports */
            s_cpuFunnelVel = Vector3Lerp(s_cpuFunnelVel, v, 0.45f);
        } else {
            s_cpuFunnelVel = (Vector3){0};
            s_cpuFunnelHasPrev = true;
        }
        s_cpuFunnelPrevBase = basePos;
        s_cpuFunnelBase = basePos;
        s_cpuFunnelBaseR = baseRadius;
        s_cpuFunnelTopR = topRadius;
        s_cpuFunnelH = height;
        s_cpuFunnelSwirl = swirlSpeed;
        s_cpuFunnelUpdraft = updraftSpeed;
        s_receiverPoint = basePos;
    } else {
        /* Spin-down collapse instead of cutting centripetal force (starburst). */
        if (s_cpuFunnelActive && s_cpuFunnelSpinDownT <= 0.0f) {
            s_cpuFunnelSpinDownT = GRANULAR_FUNNEL_SPINDOWN_SEC;
            s_cpuFunnelSpinDown = 0.0f;
            s_cpuFunnelVel = Vector3Scale(s_cpuFunnelVel, 0.35f);
        }
        s_cpuFunnelHasPrev = false;
    }
}

void GranularSim_Update(float dt)
{
    if (!s_granularInit || dt <= 0.0f) return;
    s_simElapsedTime += dt;

    if (s_mapBedCount > 0) {
        if (s_cpuFunnelActive) {
            GranularSim_WakeMapBedNear(s_cpuFunnelBase, fmaxf(s_cpuFunnelTopR * 1.1f, 1.8f),
                                       (Vector3){0.0f, fmaxf(s_cpuFunnelUpdraft * 0.65f, 1.8f), 0.0f});
        }
        for (int i = 0; i < s_waveCount; i++) {
            if (s_waves[i].kind != GRANULAR_WAVE_NONE) {
                float wR = fmaxf(s_waves[i].radius, s_waves[i].topRadius);
                if (wR > 0.1f) {
                    GranularSim_WakeMapBedNear(s_waves[i].origin, wR,
                                               (Vector3){0.0f, fmaxf(s_waves[i].liftAccel * 0.15f, 1.4f), 0.0f});
                }
            }
        }
    }

    for (int i = 0; i < s_waveCount; i++) {
        if (s_waves[i].kind != GRANULAR_WAVE_NONE && s_waves[i].elapsed >= s_waves[i].duration) {
            s_waves[i].kind = GRANULAR_WAVE_NONE;
        }
    }

    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_SetWaves(s_waves, s_waveCount);
        GranularGpu_Update(dt);
        for (int i = 0; i < s_waveCount; i++) {
            if (s_waves[i].kind != GRANULAR_WAVE_NONE) s_waves[i].elapsed += dt;
        }
        return;
    }

    if (s_cpuGrainCount <= 0) return;

    if (s_cpuImpRadius > 0.001f) {
        for (int i = 0; i < s_cpuGrainCount; i++) {
            if (s_cpuGrains[i].lifetime <= 0.0f) continue;
            Vector3 rel = Vector3Subtract(s_cpuGrains[i].position, s_cpuImpCenter);
            float d = Vector3Length(rel);
            if (d < s_cpuImpRadius) {
                float w = 1.0f - d / s_cpuImpRadius;
                w *= w;
                s_cpuGrains[i].velocity = Vector3Add(s_cpuGrains[i].velocity,
                    Vector3Scale(s_cpuImpAccel, w * dt));
                if (fabsf(s_cpuImpSwirl) > 0.001f) {
                    Vector3 tang = {-rel.z, 0.0f, rel.x};
                    float tl = Vector3Length(tang);
                    if (tl > 1e-4f) {
                        s_cpuGrains[i].velocity = Vector3Add(s_cpuGrains[i].velocity,
                            Vector3Scale(tang, (s_cpuImpSwirl * w * 4.0f * dt) / tl));
                    }
                }
            }
        }
        s_cpuImpRadius = 0.0f;
    }

    if (s_cpuFunnelSpinDownT > 0.0f) {
        s_cpuFunnelSpinDownT -= dt;
        s_cpuFunnelSpinDown = Clamp(1.0f - s_cpuFunnelSpinDownT / GRANULAR_FUNNEL_SPINDOWN_SEC, 0.0f, 1.0f);
        s_cpuFunnelVel = Vector3Scale(s_cpuFunnelVel, expf(-5.0f * dt));
        if (s_cpuFunnelSpinDownT <= 0.0f) {
            s_cpuFunnelActive = false;
            s_cpuFunnelSpinDown = 0.0f;
            s_cpuFunnelVel = (Vector3){0};
        }
    }

    if (s_cpuFunnelActive) {
        static float s_cpuFunnelTime = 0.0f;
        s_cpuFunnelTime += dt;
        const float sd = s_cpuFunnelSpinDown;
        const float conf = sd > 0.0f ? (1.0f - 0.25f * sd) : 1.0f;
        for (int i = 0; i < s_cpuGrainCount; i++) {
            if (s_cpuGrains[i].lifetime <= 0.0f) continue;
            Vector3 pos = s_cpuGrains[i].position;
            Vector3 vel = s_cpuGrains[i].velocity;
            float seed = s_cpuGrains[i].seed;
            float h = pos.y - s_cpuFunnelBase.y;
            if (h > s_cpuFunnelH && sd < 0.25f) {
                float f1 = fmodf(seed * 13.7f + s_cpuFunnelTime * 3.1f, 1.0f);
                float aWrap = f1 * 6.28318f;
                float rWrap = s_cpuFunnelBaseR * (0.35f + 0.65f * fmodf(seed * 29.3f, 1.0f));
                pos.x = s_cpuFunnelBase.x + cosf(aWrap) * rWrap;
                pos.z = s_cpuFunnelBase.z + sinf(aWrap) * rWrap;
                pos.y = s_cpuFunnelBase.y + 0.04f + 0.28f * fmodf(seed * 7.1f, 1.0f);
                vel = (Vector3){s_cpuFunnelVel.x - sinf(aWrap) * s_cpuFunnelSwirl,
                                s_cpuFunnelUpdraft * 0.85f,
                                s_cpuFunnelVel.z + cosf(aWrap) * s_cpuFunnelSwirl};
                s_cpuGrains[i].position = pos;
                s_cpuGrains[i].velocity = vel;
                h = pos.y - s_cpuFunnelBase.y;
            }

            float hn = Clamp(h / fmaxf(s_cpuFunnelH, 0.25f), 0.0f, 1.0f);
            float coneR = s_cpuFunnelBaseR + (s_cpuFunnelTopR - s_cpuFunnelBaseR) * powf(hn, 1.35f);
            float rBias = 0.18f + 0.87f * fmodf(seed * 11.37f, 1.0f);
            float targetR = coneR * rBias * (1.0f - 0.35f * sd);

            float sx = sinf(hn * 4.5f - s_cpuFunnelTime * 6.0f) * 0.24f * hn * (1.0f - sd);
            float sz = cosf(hn * 3.8f - s_cpuFunnelTime * 5.2f) * 0.24f * hn * (1.0f - sd);
            float rx = pos.x - (s_cpuFunnelBase.x + sx);
            float rz = pos.z - (s_cpuFunnelBase.z + sz);
            float r = sqrtf(rx * rx + rz * rz);
            float invR = r > 1e-4f ? 1.0f / r : 1.0f;
            float radX = r > 1e-4f ? rx * invR : cosf(seed * 6.28318f);
            float radZ = r > 1e-4f ? rz * invR : sinf(seed * 6.28318f);
            float tangX = -radZ;
            float tangZ =  radX;

            /* Storm-relative velocity: the funnel frame translates with its base. */
            float vrx = vel.x - s_cpuFunnelVel.x;
            float vrz = vel.z - s_cpuFunnelVel.z;
            float vTang = vrx * tangX + vrz * tangZ;
            float rRatio = Clamp(r / fmaxf(coneR, 0.15f), 0.25f, 1.0f);
            float targetTang = s_cpuFunnelSwirl * rRatio * (1.0f - sd);
            float tangAcc = (targetTang - vTang) * 10.5f * conf;

            float vRad = vrx * radX + vrz * radZ;
            float centripetalAcc = -(vTang * vTang) / fmaxf(r, 0.08f);
            float radAcc = (centripetalAcc + (targetR - r) * 55.0f - vRad * 11.0f) * conf;

            const GranularMaterialProfile *gm = GranularSim_GetMaterialProfile((GranularMaterialId)s_cpuGrains[i].materialId);
            float massKg = gm->referenceMassKg > 0.0002f ? gm->referenceMassKg : 0.012f;
            float buoyancyStrat = Clamp(0.012f / massKg, 0.42f, 1.55f) + gm->liftCoefficient * 0.18f;
            float targetVy = s_cpuFunnelUpdraft * buoyancyStrat * (0.75f + 0.50f * fmodf(seed * 5.3f, 1.0f)) * (1.0f - sd) - 2.2f * sd;
            float yAcc = (9.81f * (1.0f - 0.65f * sd) + (targetVy - vel.y) * 7.0f) * conf;

            /* Frame tracking: only grains inside the capture radius follow the storm,
             * so stragglers are not dragged across the arena into a curtain. */
            float capture = fmaxf(coneR * 2.2f, 2.5f);
            float trackW = r < capture ? (1.0f - r / capture) : 0.0f;
            float trackK = 6.5f * conf * trackW;

            s_cpuGrains[i].velocity.x += (radX * radAcc + tangX * tangAcc + (s_cpuFunnelVel.x - vel.x) * trackK) * dt;
            s_cpuGrains[i].velocity.y += yAcc * dt;
            s_cpuGrains[i].velocity.z += (radZ * radAcc + tangZ * tangAcc + (s_cpuFunnelVel.z - vel.z) * trackK) * dt;
        }
    }

    if (MotionFields_GetGuideCount() > 0 || MotionFields_GetTargetCount() > 0) {
        for (int i = 0; i < s_cpuGrainCount; i++) {
            if (s_cpuGrains[i].lifetime <= 0.0f) continue;
            const GranularMaterialProfile *gm = GranularSim_GetMaterialProfile((GranularMaterialId)s_cpuGrains[i].materialId);
            float massKg = gm->referenceMassKg > 1e-5f ? gm->referenceMassKg : 0.012f;
            bool isBotanical = (gm->morphology == GRANULAR_MORPH_PLANAR_LAMINA ||
                                gm->morphology == GRANULAR_MORPH_ELONGATED_TWIG);
            unsigned int recvMask = MOTION_RECEIVER_PARTICLE | (isBotanical ? MOTION_RECEIVER_FOLIAGE : 0u);
            MotionFieldSample sample = {0};
            MotionFields_Sample(s_cpuGrains[i].position, s_cpuGrains[i].velocity,
                                massKg, dt, recvMask, NULL, &sample);
            Vector3 mfAcc = Vector3Add(sample.accelerationMps2,
                                       Vector3Scale(sample.forceNewtons, 1.0f / massKg));
            if (s_cpuGrains[i].flags == 1u) {
                float airSpeed = Vector3Length(sample.airflowVelocity);
                float liftForce = mfAcc.y + airSpeed;
                if (liftForce < gm->criticalEntrainmentMps) continue;
                s_cpuGrains[i].flags = 0u;
            }
            s_cpuGrains[i].velocity = Vector3Add(s_cpuGrains[i].velocity,
                                                 Vector3Scale(mfAcc, dt));
            Vector3 airDelta = Vector3Subtract(sample.airflowVelocity, s_cpuGrains[i].velocity);
            float coupleHz = isBotanical ? 4.8f : 2.5f;
            s_cpuGrains[i].velocity = Vector3Add(s_cpuGrains[i].velocity,
                                                 Vector3Scale(airDelta, fminf(dt * coupleHz, 1.0f)));
        }
    }

    s_totalShearWork += GranularSim_StepCpuDeterministicWithWaves(
        s_cpuGrains, s_cpuGrainCount, NULL, s_waves, s_waveCount,
        dt, s_simElapsedTime, s_receiverPoint, s_receiverNormal);

    int alive = 0;
    for (int i = 0; i < s_cpuGrainCount; i++) {
        if (s_cpuGrains[i].lifetime > 0.0f) alive++;
    }
    if (alive == 0) s_cpuGrainCount = 0;
}

static void GranularSim_DrawCpuFallback(Camera3D camera, const GranularMaterialProfile *fallbackMat)
{
    if (s_cpuGrainCount <= 0) return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);
    Vector3 keyLight = Vector3Normalize((Vector3){0.42f, 0.82f, 0.39f});

    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < s_cpuGrainCount; i++) {
        const GranularGrainState *g = &s_cpuGrains[i];
        if (g->lifetime <= 0.0f) continue;

        const GranularMaterialProfile *gm = GranularSim_GetMaterialProfile((GranularMaterialId)g->materialId);
        if (!gm) gm = fallbackMat;

        float lifeAlpha = g->lifetime < 0.35f ? (g->lifetime / 0.35f) : 1.0f;
        float ao = 1.0f - 0.50f * Clamp(g->contactAO, 0.0f, 1.0f);
        unsigned char alpha = (unsigned char)(255.0f * lifeAlpha);
        float s0 = g->seed;
        float s1 = fmodf(s0 * 17.31f, 1.0f);
        float s2 = fmodf(s0 * 29.73f, 1.0f);

        if (gm->morphology == GRANULAR_MORPH_PLANAR_LAMINA) {
            // 3D tumbling & fluttering botanical leaf / petal blade
            bool resting = (g->flags == 1u);
            float r = g->radius * 1.95f * (0.82f + 0.38f * s1);
            float yaw = s0 * 6.2831853f + (resting ? 0.0f : s_simElapsedTime * (2.5f + 3.5f * s1));
            float pitch = resting ? 0.08f * sinf(s0 * 41.0f) : (s_simElapsedTime * (3.2f + 4.8f * s2) + s0 * 6.283f);
            float roll = resting ? 0.08f * cosf(s0 * 29.0f) : sinf(s_simElapsedTime * 4.5f + s0 * 19.0f) * 0.85f;

            float cy = cosf(yaw), sy = sinf(yaw);
            float cp = cosf(pitch), sp = sinf(pitch);
            float cr = cosf(roll), sr = sinf(roll);

            Vector3 lFwd = {cy * cp, sp, sy * cp};
            Vector3 lUp  = {-sy * sr - cy * sp * cr, cp * cr, cy * sr - sy * sp * cr};
            Vector3 lRight = Vector3Normalize(Vector3CrossProduct(lUp, lFwd));

            float nDotL = fabsf(Vector3DotProduct(lUp, keyLight));
            float shade = (0.45f + 0.55f * nDotL) * ao;
            bool isPetal = (g->materialId == GRANULAR_MAT_FLOWER_PETAL);
            float wScale = isPetal ? 0.68f : 0.48f;

            Color cBase = ColorLerp(gm->shadowColor, gm->baseColor, shade);
            if (g->materialId == GRANULAR_MAT_DRY_LEAF) {
                Color autumnGold = {214, 158, 46, 255};
                cBase = ColorLerp(cBase, autumnGold, s2 * 0.55f);
            }
            Color cTip = ColorLerp(cBase, gm->highlightColor, 0.32f);
            Color cMid = ColorLerp(cBase, gm->highlightColor, 0.45f);
            cBase.a = cTip.a = cMid.a = alpha;

            Vector3 pBase = Vector3Subtract(g->position, Vector3Scale(lFwd, r * 0.85f));
            Vector3 pTip  = Vector3Add(g->position, Vector3Scale(lFwd, r * 0.95f));
            Vector3 pMid  = Vector3Add(g->position, Vector3Scale(lUp, r * 0.08f));
            Vector3 pL    = Vector3Subtract(g->position, Vector3Scale(lRight, r * wScale));
            Vector3 pR    = Vector3Add(g->position, Vector3Scale(lRight, r * wScale));
            if (isPetal) {
                Vector3 tipOff = Vector3Scale(lFwd, r * 0.25f);
                pL = Vector3Add(pL, tipOff);
                pR = Vector3Add(pR, tipOff);
            }

            // 4-triangle folded leaf/petal silhouette
            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pBase.x, pBase.y, pBase.z);
            rlColor4ub(cMid.r,  cMid.g,  cMid.b,  alpha); rlVertex3f(pMid.x,  pMid.y,  pMid.z);
            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pL.x,    pL.y,    pL.z);

            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pBase.x, pBase.y, pBase.z);
            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pR.x,    pR.y,    pR.z);
            rlColor4ub(cMid.r,  cMid.g,  cMid.b,  alpha); rlVertex3f(pMid.x,  pMid.y,  pMid.z);

            rlColor4ub(cMid.r,  cMid.g,  cMid.b,  alpha); rlVertex3f(pMid.x,  pMid.y,  pMid.z);
            rlColor4ub(cTip.r,  cTip.g,  cTip.b,  alpha); rlVertex3f(pTip.x,  pTip.y,  pTip.z);
            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pL.x,    pL.y,    pL.z);

            rlColor4ub(cMid.r,  cMid.g,  cMid.b,  alpha); rlVertex3f(pMid.x,  pMid.y,  pMid.z);
            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pR.x,    pR.y,    pR.z);
            rlColor4ub(cTip.r,  cTip.g,  cTip.b,  alpha); rlVertex3f(pTip.x,  pTip.y,  pTip.z);
        } else if (gm->morphology == GRANULAR_MORPH_ELONGATED_TWIG) {
            // Elongated woody branch segment
            float r = g->radius * 1.9f * (0.85f + 0.35f * s1);
            float spd = Vector3Length(g->velocity);
            Vector3 dir = spd > 0.2f ? Vector3Scale(g->velocity, 1.0f / spd)
                                     : (Vector3){cosf(s0 * 6.283f), 0.04f, sinf(s0 * 6.283f)};
            Vector3 side = Vector3Normalize(Vector3CrossProduct(dir, up));
            if (Vector3LengthSqr(side) < 0.01f) side = right;

            Color cDark = ColorLerp(gm->shadowColor, gm->baseColor, 0.35f * ao);
            Color cLit  = ColorLerp(gm->baseColor, gm->highlightColor, 0.25f * ao);
            cDark.a = cLit.a = alpha;

            Vector3 a = Vector3Subtract(g->position, Vector3Scale(dir, r * 1.15f));
            Vector3 b = Vector3Add(g->position, Vector3Scale(dir, r * 1.15f));
            Vector3 w = Vector3Scale(side, r * 0.18f);

            Vector3 p0 = Vector3Subtract(a, w), p1 = Vector3Add(a, w);
            Vector3 p2 = Vector3Add(b, w),      p3 = Vector3Subtract(b, w);
            rlColor4ub(cDark.r, cDark.g, cDark.b, alpha); rlVertex3f(p0.x, p0.y, p0.z);
            rlColor4ub(cLit.r,  cLit.g,  cLit.b,  alpha); rlVertex3f(p1.x, p1.y, p1.z);
            rlColor4ub(cLit.r,  cLit.g,  cLit.b,  alpha); rlVertex3f(p2.x, p2.y, p2.z);
            rlColor4ub(cDark.r, cDark.g, cDark.b, alpha); rlVertex3f(p0.x, p0.y, p0.z);
            rlColor4ub(cLit.r,  cLit.g,  cLit.b,  alpha); rlVertex3f(p2.x, p2.y, p2.z);
            rlColor4ub(cDark.r, cDark.g, cDark.b, alpha); rlVertex3f(p3.x, p3.y, p3.z);
        } else if (gm->morphology == GRANULAR_MORPH_CRYSTAL_SHARD || gm->morphology == GRANULAR_MORPH_METAL_SHARD) {
            // Faceted 3D-shaded diamond/crystal or metallic blade shard
            float r = g->radius * (gm->morphology == GRANULAR_MORPH_METAL_SHARD ? 1.85f : 1.65f) * (0.85f + 0.35f * s1);
            float ang = s0 * 6.2831853f + ((g->flags == 1u) ? 0.0f : s_simElapsedTime * (3.5f + 4.5f * s2));
            Vector3 ax = Vector3Add(Vector3Scale(right, cosf(ang)), Vector3Scale(up, sinf(ang)));
            Vector3 ay = Vector3Subtract(Vector3Scale(up, cosf(ang)), Vector3Scale(right, sinf(ang)));

            Color cShad = gm->shadowColor;
            Color cBase = gm->baseColor;
            Color cHigh = gm->highlightColor;
            float wFrac = gm->morphology == GRANULAR_MORPH_METAL_SHARD ? 0.42f : 0.55f;
            Vector3 pT = Vector3Add(g->position, Vector3Scale(ay, r * 1.25f));
            Vector3 pB = Vector3Subtract(g->position, Vector3Scale(ay, r * 1.25f));
            Vector3 pL = Vector3Subtract(g->position, Vector3Scale(ax, r * wFrac));
            Vector3 pR = Vector3Add(g->position, Vector3Scale(ax, r * wFrac));

            rlColor4ub(cHigh.r, cHigh.g, cHigh.b, alpha); rlVertex3f(pT.x, pT.y, pT.z);
            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pL.x, pL.y, pL.z);
            rlColor4ub(cShad.r, cShad.g, cShad.b, alpha); rlVertex3f(pB.x, pB.y, pB.z);

            rlColor4ub(cHigh.r, cHigh.g, cHigh.b, alpha); rlVertex3f(pT.x, pT.y, pT.z);
            rlColor4ub(cShad.r, cShad.g, cShad.b, alpha); rlVertex3f(pB.x, pB.y, pB.z);
            rlColor4ub(cBase.r, cBase.g, cBase.b, alpha); rlVertex3f(pR.x, pR.y, pR.z);
        } else {
            // Angular clod / fine continuum: faceted irregular pentagon
            float r = g->radius * 1.45f * (0.80f + 0.40f * s1);
            Color cCore = ColorLerp(gm->shadowColor, gm->baseColor, 0.40f + 0.60f * ao);
            Color cEdge = ColorLerp(gm->shadowColor, gm->baseColor, 0.20f + 0.50f * ao);
            float baseAng = s0 * 6.2831853f;
            Vector3 pts[5];
            for (int k = 0; k < 5; k++) {
                float a = baseAng + (float)k * 1.256637f;
                float rk = r * (0.78f + 0.35f * fmodf(s0 * (float)(k * 13 + 7), 1.0f));
                pts[k] = Vector3Add(g->position, Vector3Add(Vector3Scale(right, cosf(a) * rk),
                                                            Vector3Scale(up, sinf(a) * rk)));
            }
            for (int k = 0; k < 5; k++) {
                int kn = (k + 1) % 5;
                rlColor4ub(cCore.r, cCore.g, cCore.b, alpha); rlVertex3f(g->position.x, g->position.y, g->position.z);
                rlColor4ub(cEdge.r, cEdge.g, cEdge.b, alpha); rlVertex3f(pts[k].x, pts[k].y, pts[k].z);
                rlColor4ub(cEdge.r, cEdge.g, cEdge.b, alpha); rlVertex3f(pts[kn].x, pts[kn].y, pts[kn].z);
            }
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void GranularSim_DrawBody(Camera3D camera)
{
    int active = GranularSim_GetActiveCount();
    if (active <= 0 && s_mapBedCount <= 0) return;

    const GranularMaterialProfile *mat = GranularSim_GetMaterialProfile(s_activeMatId);
    VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, true);
    if (scope.active) {
        if (s_usingGpu && GranularGpu_IsAvailable()) {
            GranularGpu_DrawBody(camera, mat);
        } else if (active > 0) {
            GranularSim_DrawCpuFallback(camera, mat);
        }
        VFXRender_EndDraw(&scope);
    }
}

void GranularSim_DrawShadowPass(void)
{
    if (!s_granularInit) return;
    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_DrawShadowPass();
    }
}

void GranularSim_ClearMapBed(void)
{
    s_mapBedCount = 0;
    for (int i = 0; i < GRANULAR_BED_GRID_DIM * GRANULAR_BED_GRID_DIM; i++) {
        s_bedCellHead[i] = -1;
    }
    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_ClearBed();
    }
}

int GranularSim_GetMapBedCount(void)
{
    return s_mapBedCount;
}

int GranularSim_SeedMapBedFromGrains(const GranularGrainState *grains, int count)
{
    if (!s_granularInit) GranularSim_Init();
    if (!grains || count <= 0) {
        GranularSim_ClearMapBed();
        return 0;
    }
    if (count > GRANULAR_MAP_BED_MAX) count = GRANULAR_MAP_BED_MAX;

    float minX = grains[0].position.x, maxX = grains[0].position.x;
    float minZ = grains[0].position.z, maxZ = grains[0].position.z;
    for (int i = 1; i < count; i++) {
        float x = grains[i].position.x;
        float z = grains[i].position.z;
        if (x < minX) minX = x; else if (x > maxX) maxX = x;
        if (z < minZ) minZ = z; else if (z > maxZ) maxZ = z;
    }

    float spanX = maxX - minX;
    float spanZ = maxZ - minZ;
    float maxSpan = (spanX > spanZ) ? spanX : spanZ;
    s_bedMinX = minX - 0.5f;
    s_bedMinZ = minZ - 0.5f;
    s_bedCellSpan = (maxSpan + 1.0f) / (float)GRANULAR_BED_GRID_DIM;
    if (s_bedCellSpan < 0.25f) s_bedCellSpan = 0.25f;

    for (int i = 0; i < GRANULAR_BED_GRID_DIM * GRANULAR_BED_GRID_DIM; i++) {
        s_bedCellHead[i] = -1;
    }

    for (int i = 0; i < count; i++) {
        s_mapBed[i] = grains[i];
        if (s_mapBed[i].lifetime <= 0.0f) s_mapBed[i].lifetime = 999.0f;
        s_bedAwake[i] = 0;

        int cx = (int)floorf((s_mapBed[i].position.x - s_bedMinX) / s_bedCellSpan);
        int cz = (int)floorf((s_mapBed[i].position.z - s_bedMinZ) / s_bedCellSpan);
        if (cx < 0) cx = 0; else if (cx >= GRANULAR_BED_GRID_DIM) cx = GRANULAR_BED_GRID_DIM - 1;
        if (cz < 0) cz = 0; else if (cz >= GRANULAR_BED_GRID_DIM) cz = GRANULAR_BED_GRID_DIM - 1;
        int cell = cz * GRANULAR_BED_GRID_DIM + cx;
        s_bedNext[i] = s_bedCellHead[cell];
        s_bedCellHead[cell] = i;
    }

    s_mapBedCount = count;
    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_UploadBed(s_mapBed, s_mapBedCount);
    }
    return count;
}

int GranularSim_SeedMapBed(Vector3 center, float radius, int count,
                            const GranularMaterialId *mats, int matCount,
                            unsigned int seed)
{
    if (!s_granularInit) GranularSim_Init();
    if (count <= 0 || radius <= 0.1f) return 0;
    if (count > GRANULAR_MAP_BED_MAX) count = GRANULAR_MAP_BED_MAX;

    static const GranularMaterialId kDefaultMats[5] = {
        GRANULAR_MAT_DRY_LEAF,
        GRANULAR_MAT_FRESH_LEAF,
        GRANULAR_MAT_DRY_TWIG,
        GRANULAR_MAT_COARSE_GRAVEL,
        GRANULAR_MAT_LOESS_SOIL
    };
    const GranularMaterialId *useMats = (mats && matCount > 0) ? mats : kDefaultMats;
    int useMatCount = (mats && matCount > 0) ? matCount : 5;

    static GranularGrainState s_seedBuf[GRANULAR_MAP_BED_MAX];
    for (int i = 0; i < count; i++) {
        unsigned int h = (unsigned int)i * 747796405u + seed * 2891336453u + 17u;
        float u0 = GranularSim_Hash01(h + 1u);
        float u1 = GranularSim_Hash01(h + 2u);
        float u2 = GranularSim_Hash01(h + 3u);
        float u3 = GranularSim_Hash01(h + 4u);

        float ang = u0 * 6.2831853f;
        float r = sqrtf(u1) * radius;
        float x = center.x + cosf(ang) * r;
        float z = center.z + sinf(ang) * r;
#if !defined(CORE_GRANULAR_HEADLESS_TEST)
        float y = MapManager_GetGroundHeightAt(x, z) + 0.014f;
#else
        float y = center.y + 0.014f;
#endif
        GranularMaterialId mId = useMats[(int)(u2 * (float)useMatCount) % useMatCount];
        float grainR = 0.024f + 0.016f * u3;

        s_seedBuf[i] = (GranularGrainState){
            .position = {x, y, z},
            .velocity = {0.0f, 0.0f, 0.0f},
            .radius = grainR,
            .lifetime = 999.0f,
            .seed = u3,
            .materialId = (uint32_t)mId
        };
    }

    return GranularSim_SeedMapBedFromGrains(s_seedBuf, count);
}

int GranularSim_WakeMapBedNear(Vector3 center, float radius, Vector3 impulse)
{
    if (s_mapBedCount <= 0 || radius <= 0.05f) return 0;

    int minCx = (int)floorf((center.x - radius - s_bedMinX) / s_bedCellSpan);
    int maxCx = (int)floorf((center.x + radius - s_bedMinX) / s_bedCellSpan);
    int minCz = (int)floorf((center.z - radius - s_bedMinZ) / s_bedCellSpan);
    int maxCz = (int)floorf((center.z + radius - s_bedMinZ) / s_bedCellSpan);
    if (maxCx < 0 || minCx >= GRANULAR_BED_GRID_DIM || maxCz < 0 || minCz >= GRANULAR_BED_GRID_DIM) {
        return 0;
    }
    if (minCx < 0) minCx = 0;
    if (maxCx >= GRANULAR_BED_GRID_DIM) maxCx = GRANULAR_BED_GRID_DIM - 1;
    if (minCz < 0) minCz = 0;
    if (maxCz >= GRANULAR_BED_GRID_DIM) maxCz = GRANULAR_BED_GRID_DIM - 1;

    enum { MAX_WAKE_BATCH = 64 };
    GranularGrainState woken[MAX_WAKE_BATCH];
    int wokenIdx[MAX_WAKE_BATCH];
    int wokenCount = 0;
    float r2 = radius * radius;

    for (int cz = minCz; cz <= maxCz && wokenCount < MAX_WAKE_BATCH; cz++) {
        for (int cx = minCx; cx <= maxCx && wokenCount < MAX_WAKE_BATCH; cx++) {
            int idx = s_bedCellHead[cz * GRANULAR_BED_GRID_DIM + cx];
            while (idx >= 0 && wokenCount < MAX_WAKE_BATCH) {
                if (!s_bedAwake[idx]) {
                    float dx = s_mapBed[idx].position.x - center.x;
                    float dz = s_mapBed[idx].position.z - center.z;
                    if (dx * dx + dz * dz <= r2) {
                        s_bedAwake[idx] = 1;
                        woken[wokenCount] = s_mapBed[idx];
                        woken[wokenCount].velocity = (Vector3){
                            impulse.x + (s_mapBed[idx].seed - 0.5f) * 1.4f,
                            fmaxf(impulse.y, 1.4f) + s_mapBed[idx].seed * 1.2f,
                            impulse.z + (0.5f - s_mapBed[idx].seed) * 1.4f
                        };
                        woken[wokenCount].lifetime = 5.0f;
                        wokenIdx[wokenCount] = idx;
                        wokenCount++;
                    }
                }
                idx = s_bedNext[idx];
            }
        }
    }

    if (wokenCount > 0) {
        if (s_usingGpu && GranularGpu_IsAvailable()) {
            GranularGpu_HideBedIndices(wokenIdx, wokenCount);
        }
        GranularSim_AppendCluster(woken, wokenCount, center, (Vector3){0.0f, 1.0f, 0.0f});
    }
    return wokenCount;
}

int GranularSim_GetActiveCount(void)
{
    if (s_usingGpu && GranularGpu_IsAvailable()) {
        return GranularGpu_GetGrainCount();
    }
    return s_cpuGrainCount;
}

GranularSimStats GranularSim_GetStats(void)
{
    int count = GranularSim_GetActiveCount();
    return (GranularSimStats){
        .active = count > 0,
        .activeCount = count,
        .usingGpuBackend = s_usingGpu && GranularGpu_IsAvailable(),
        .totalShearWork = s_totalShearWork
    };
}
