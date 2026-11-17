#include "core/granular/granular_sim.h"
#include "core/granular/granular_gpu.h"
#include "core/granular/granular_dem_cpu.inl"
#include "core/motion/motion_fields.h"
#include "core/vfx_render.h"
#include "core/gfx_quality.h"
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

static Vector3 s_cpuImpCenter = {0};
static float s_cpuImpRadius = 0.0f;
static Vector3 s_cpuImpAccel = {0};
static float s_cpuImpSwirl = 0.0f;

static bool s_cpuFunnelActive = false;
static Vector3 s_cpuFunnelBase = {0};
static float s_cpuFunnelBaseR = 0.42f;
static float s_cpuFunnelTopR = 1.65f;
static float s_cpuFunnelH = 3.6f;
static float s_cpuFunnelSwirl = 13.5f;
static float s_cpuFunnelUpdraft = 3.4f;

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

void GranularSim_ApplyForceImpulse(Vector3 center, float radius,
                                   Vector3 forceAccel, float swirlRadPerSec)
{
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
    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_SetTornadoFunnel(basePos, baseRadius, topRadius, height,
                                     swirlSpeed, updraftSpeed, active);
    } else {
        s_cpuFunnelActive = active;
        s_cpuFunnelBase = basePos;
        s_cpuFunnelBaseR = baseRadius;
        s_cpuFunnelTopR = topRadius;
        s_cpuFunnelH = height;
        s_cpuFunnelSwirl = swirlSpeed;
        s_cpuFunnelUpdraft = updraftSpeed;
        if (active) s_receiverPoint = basePos;
    }
}

void GranularSim_Update(float dt)
{
    if (!s_granularInit || dt <= 0.0f) return;

    if (s_usingGpu && GranularGpu_IsAvailable()) {
        GranularGpu_Update(dt);
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

    if (s_cpuFunnelActive) {
        static float s_cpuFunnelTime = 0.0f;
        s_cpuFunnelTime += dt;
        for (int i = 0; i < s_cpuGrainCount; i++) {
            if (s_cpuGrains[i].lifetime <= 0.0f) continue;
            Vector3 pos = s_cpuGrains[i].position;
            Vector3 vel = s_cpuGrains[i].velocity;
            float seed = s_cpuGrains[i].seed;
            float h = pos.y - s_cpuFunnelBase.y;
            if (h > s_cpuFunnelH) {
                float f1 = fmodf(seed * 13.7f + s_cpuFunnelTime * 3.1f, 1.0f);
                float aWrap = f1 * 6.28318f;
                float rWrap = s_cpuFunnelBaseR * (0.35f + 0.65f * fmodf(seed * 29.3f, 1.0f));
                pos.x = s_cpuFunnelBase.x + cosf(aWrap) * rWrap;
                pos.z = s_cpuFunnelBase.z + sinf(aWrap) * rWrap;
                pos.y = s_cpuFunnelBase.y + 0.04f + 0.28f * fmodf(seed * 7.1f, 1.0f);
                vel = (Vector3){-sinf(aWrap) * s_cpuFunnelSwirl, s_cpuFunnelUpdraft * 0.85f, cosf(aWrap) * s_cpuFunnelSwirl};
                s_cpuGrains[i].position = pos;
                s_cpuGrains[i].velocity = vel;
                h = pos.y - s_cpuFunnelBase.y;
            }

            float hn = Clamp(h / fmaxf(s_cpuFunnelH, 0.25f), 0.0f, 1.0f);
            float coneR = s_cpuFunnelBaseR + (s_cpuFunnelTopR - s_cpuFunnelBaseR) * powf(hn, 1.35f);
            float rBias = 0.18f + 0.87f * fmodf(seed * 11.37f, 1.0f);
            float targetR = coneR * rBias;

            float sx = sinf(hn * 4.5f - s_cpuFunnelTime * 6.0f) * 0.24f * hn;
            float sz = cosf(hn * 3.8f - s_cpuFunnelTime * 5.2f) * 0.24f * hn;
            float rx = pos.x - (s_cpuFunnelBase.x + sx);
            float rz = pos.z - (s_cpuFunnelBase.z + sz);
            float r = sqrtf(rx * rx + rz * rz);
            float invR = r > 1e-4f ? 1.0f / r : 1.0f;
            float radX = r > 1e-4f ? rx * invR : cosf(seed * 6.28318f);
            float radZ = r > 1e-4f ? rz * invR : sinf(seed * 6.28318f);
            float tangX = -radZ;
            float tangZ =  radX;

            float vTang = vel.x * tangX + vel.z * tangZ;
            float rRatio = Clamp(r / fmaxf(coneR, 0.15f), 0.25f, 1.0f);
            float targetTang = s_cpuFunnelSwirl * rRatio;
            float tangAcc = (targetTang - vTang) * 10.5f;

            float vRad = vel.x * radX + vel.z * radZ;
            float centripetalAcc = -(vTang * vTang) / fmaxf(r, 0.08f);
            float radAcc = centripetalAcc + (targetR - r) * 55.0f - vRad * 11.0f;

            float targetVy = s_cpuFunnelUpdraft * (0.75f + 0.50f * fmodf(seed * 5.3f, 1.0f));
            float yAcc = 9.81f + (targetVy - vel.y) * 7.0f;

            s_cpuGrains[i].velocity.x += (radX * radAcc + tangX * tangAcc) * dt;
            s_cpuGrains[i].velocity.y += yAcc * dt;
            s_cpuGrains[i].velocity.z += (radZ * radAcc + tangZ * tangAcc) * dt;
        }
    }

    if (MotionFields_GetGuideCount() > 0 || MotionFields_GetTargetCount() > 0) {
        for (int i = 0; i < s_cpuGrainCount; i++) {
            if (s_cpuGrains[i].lifetime <= 0.0f) continue;
            MotionFieldSample sample = {0};
            MotionFields_Sample(s_cpuGrains[i].position, s_cpuGrains[i].velocity,
                                0.05f, dt, MOTION_RECEIVER_PARTICLE, NULL, &sample);
            Vector3 mfAcc = Vector3Add(sample.accelerationMps2,
                                       Vector3Scale(sample.forceNewtons, 20.0f));
            s_cpuGrains[i].velocity = Vector3Add(s_cpuGrains[i].velocity,
                                                 Vector3Scale(mfAcc, dt));
            Vector3 airDelta = Vector3Subtract(sample.airflowVelocity, s_cpuGrains[i].velocity);
            s_cpuGrains[i].velocity = Vector3Add(s_cpuGrains[i].velocity,
                                                 Vector3Scale(airDelta, fminf(dt * 2.5f, 1.0f)));
        }
    }

    const GranularMaterialProfile *mat = GranularSim_GetMaterialProfile(s_activeMatId);
    s_totalShearWork += GranularSim_StepCpuDeterministic(
        s_cpuGrains, s_cpuGrainCount, mat, dt, s_receiverPoint, s_receiverNormal);

    int alive = 0;
    for (int i = 0; i < s_cpuGrainCount; i++) {
        if (s_cpuGrains[i].lifetime > 0.0f) alive++;
    }
    if (alive == 0) s_cpuGrainCount = 0;
}

static void GranularSim_DrawCpuFallback(Camera3D camera, const GranularMaterialProfile *mat)
{
    if (s_cpuGrainCount <= 0) return;
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = Vector3CrossProduct(right, forward);

    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < s_cpuGrainCount; i++) {
        const GranularGrainState *g = &s_cpuGrains[i];
        if (g->lifetime <= 0.0f) continue;

        float lifeAlpha = g->lifetime < 0.35f ? (g->lifetime / 0.35f) : 1.0f;
        float ao = 1.0f - 0.55f * Clamp(g->contactAO, 0.0f, 1.0f);
        Color c = ColorLerp(mat->shadowColor, mat->baseColor, 0.30f + 0.70f * ao);
        c.a = (unsigned char)(255.0f * lifeAlpha);
        rlColor4ub(c.r, c.g, c.b, c.a);

        float r = g->radius * 1.45f;
        Vector3 rx = Vector3Scale(right, r);
        Vector3 ry = Vector3Scale(up, r);

        Vector3 p0 = Vector3Subtract(Vector3Subtract(g->position, rx), ry);
        Vector3 p1 = Vector3Subtract(Vector3Add(g->position, rx), ry);
        Vector3 p2 = Vector3Add(Vector3Add(g->position, rx), ry);
        Vector3 p3 = Vector3Add(Vector3Subtract(g->position, rx), ry);

        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlVertex3f(p2.x, p2.y, p2.z);

        rlVertex3f(p0.x, p0.y, p0.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p3.x, p3.y, p3.z);
    }
    rlEnd();
}

void GranularSim_DrawBody(Camera3D camera)
{
    int active = GranularSim_GetActiveCount();
    if (active <= 0) return;

    const GranularMaterialProfile *mat = GranularSim_GetMaterialProfile(s_activeMatId);
    VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, true);
    if (scope.active) {
        if (s_usingGpu && GranularGpu_IsAvailable()) {
            GranularGpu_DrawBody(camera, mat);
        } else {
            GranularSim_DrawCpuFallback(camera, mat);
        }
        VFXRender_EndDraw(&scope);
    }
}

void GranularSim_DrawShadowPass(void)
{
    /* Reserved for shadow-pass grain impostors when static/dynamic shadow casters opt in. */
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
