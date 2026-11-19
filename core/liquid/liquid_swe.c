#include "core/liquid/liquid_swe.h"

#include "raymath.h"
#include <float.h>
#include <math.h>
#include <string.h>

#ifndef LIQUID_SWE_HEADLESS
#include "core/liquid/liquid_impact.h"
#include "core/map_manager.h"
#include "core/resource_manager.h"
#include "core/skill_manager.h"
#include "core/vfx_render.h"
#include "environment/env_shadow.h"
#include "environment/environment_system.h"
#include "rlgl.h"
#endif

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define SWE_NX LIQUID_SWE_GRID_NX
#define SWE_NZ LIQUID_SWE_GRID_NZ

typedef struct {
    bool active;
    int handle;
    LiquidSWEDesc desc;
    float age;
    Vector3 origin;
    Vector3 fwd;
    Vector3 right;
    float dx;
    float dz;
    float wetTimer;
    float rippleTimer;
    Vector3 peakPos;
    float peakH;

    int emitterCount;
    LiquidSWEEmitter emitters[LIQUID_SWE_MAX_EMITTERS];
    LiquidSWEManifold manifolds[LIQUID_SWE_MAX_MANIFOLDS];

    /* Virtual-pipe SWE fields */
    float h[SWE_NZ][SWE_NX];
    float b[SWE_NZ][SWE_NX];      /* Terrain elevation */
    float bObs[SWE_NZ][SWE_NX];   /* Target/obstacle bathymetry skirt */
    float foam[SWE_NZ][SWE_NX];
    float wet[SWE_NZ][SWE_NX];
    float fR[SWE_NZ][SWE_NX];
    float fL[SWE_NZ][SWE_NX];
    float fT[SWE_NZ][SWE_NX];
    float fB[SWE_NZ][SWE_NX];

    float hNext[SWE_NZ][SWE_NX];
    float foamNext[SWE_NZ][SWE_NX];
    float wetNext[SWE_NZ][SWE_NX];

    Vector3 posGrid[SWE_NZ][SWE_NX];
    Vector3 normGrid[SWE_NZ][SWE_NX];
    float speedGrid[SWE_NZ][SWE_NX];
} LiquidSWEInstance;

static LiquidSWEInstance s_sweInstances[LIQUID_SWE_MAX_INSTANCES];
static int s_sweNextHandle = 1;

#define SWE_MAX_DROPLETS 256
typedef struct {
    bool active;
    int instIdx;
    Vector3 pos;
    Vector3 vel;
    float radius;
    float age;
    float maxAge;
} SWEDroplet;

static SWEDroplet s_sweDroplets[SWE_MAX_DROPLETS];
static unsigned int s_sweRngState = 0x9E3779B9u;

static float SWE_Rand01(void)
{
    s_sweRngState = s_sweRngState * 1664525u + 1013904223u;
    return (float)((s_sweRngState >> 8) & 0x00FFFFFFu) / 16777216.0f;
}

static void SWE_SpawnDroplet(int instIdx, Vector3 pos, Vector3 vel, float radius, float maxAge)
{
    int slot = -1;
    float oldestRatio = -1.0f;
    int oldestSlot = 0;
    for (int i = 0; i < SWE_MAX_DROPLETS; ++i) {
        if (!s_sweDroplets[i].active) {
            slot = i;
            break;
        }
        float r = s_sweDroplets[i].age / fmaxf(s_sweDroplets[i].maxAge, 0.05f);
        if (r > oldestRatio) {
            oldestRatio = r;
            oldestSlot = i;
        }
    }
    if (slot < 0) slot = oldestSlot;

    s_sweDroplets[slot] = (SWEDroplet){
        .active = true,
        .instIdx = instIdx,
        .pos = pos,
        .vel = vel,
        .radius = radius,
        .age = 0.0f,
        .maxAge = maxAge
    };
}

#ifndef LIQUID_SWE_HEADLESS
static Shader s_sweShader = {0};
static int s_sweFadeLoc = -1;
static int s_sweDeepColorLoc = -1;
static int s_sweCrestColorLoc = -1;
static int s_sweFoamColorLoc = -1;
static int s_sweRenderModeLoc = -1;
static int s_sweCryoAmountLoc = -1;

static void SWE_InitShaderIfNeeded(void)
{
    if (s_sweShader.id == 0) {
        s_sweShader = ResourceManager_LoadShader("core/shaders/tsunami_swe.vs",
                                                 "core/shaders/tsunami_swe.fs");
        if (s_sweShader.id != 0) {
            s_sweFadeLoc = GetShaderLocation(s_sweShader, "u_fade");
            s_sweDeepColorLoc = GetShaderLocation(s_sweShader, "u_deepColor");
            s_sweCrestColorLoc = GetShaderLocation(s_sweShader, "u_crestColor");
            s_sweFoamColorLoc = GetShaderLocation(s_sweShader, "u_foamColor");
            s_sweRenderModeLoc = GetShaderLocation(s_sweShader, "u_renderMode");
            s_sweCryoAmountLoc = GetShaderLocation(s_sweShader, "u_cryoAmount");
        }
    }
}
#endif

static float SWE_Clamp(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static float SWE_Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static float SWE_SmoothStep(float e0, float e1, float x)
{
    float denom = e1 - e0;
    if (fabsf(denom) < 1e-6f) return (x >= e1) ? 1.0f : 0.0f;
    float t = SWE_Clamp((x - e0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static Vector3 SWE_V3Add(Vector3 a, Vector3 b) { return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vector3 SWE_V3Sub(Vector3 a, Vector3 b) { return (Vector3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vector3 SWE_V3Scale(Vector3 v, float s) { return (Vector3){v.x * s, v.y * s, v.z * s}; }

static Vector3 SWE_V3Cross(Vector3 a, Vector3 b)
{
    return (Vector3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static float SWE_V3Len(Vector3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Vector3 SWE_V3Norm(Vector3 v)
{
    float len = SWE_V3Len(v);
    if (len < 1e-5f) return (Vector3){0.0f, 1.0f, 0.0f};
    float inv = 1.0f / len;
    return (Vector3){v.x * inv, v.y * inv, v.z * inv};
}

static bool SWE_IsATLAPattern(LiquidSWEPattern p)
{
    return (p >= LIQUID_SWE_PATTERN_TAI_CHI_WHIP && p <= LIQUID_SWE_PATTERN_AVATAR_STATE) ||
           p == LIQUID_SWE_PATTERN_FROZEN_OCTOPUS;
}

static float SWE_ComputeCryoProgress(const LiquidSWEInstance *inst)
{
    if (!inst || inst->desc.cryoMode <= 0.01f) return 0.0f;
    float tNorm = inst->age / fmaxf(inst->desc.duration, 0.5f);
    return SWE_SmoothStep(0.18f, 0.52f, tNorm) * SWE_Clamp(inst->desc.cryoMode, 0.0f, 1.0f);
}

static float SWE_SampleGroundY(bool sampleTerrain, float x, float z, float fallbackY)
{
#ifndef LIQUID_SWE_HEADLESS
    if (sampleTerrain) {
        float gy = MapManager_GetGroundHeightAt(x, z);
        float wy = 0.0f, wd = 0.0f;
        if (MapManager_GetWaterInfoAt(x, z, &wy, &wd) && wd > 0.01f) {
            gy = fmaxf(gy, wy);
        }
        if (isfinite(gy)) return gy;
    }
#else
    (void)sampleTerrain;
    (void)x;
    (void)z;
#endif
    return fallbackY;
}

static Vector3 SWE_CellWorldXZ(const LiquidSWEInstance *inst, int ix, int iz)
{
    float lx = ((float)ix / (float)(SWE_NX - 1) - 0.5f) * inst->desc.width;
    float lz = ((float)iz / (float)(SWE_NZ - 1)) * inst->desc.length;
    return SWE_V3Add(inst->origin,
                     SWE_V3Add(SWE_V3Scale(inst->right, lx), SWE_V3Scale(inst->fwd, lz)));
}

/* ── Deterministic 3D ATLA Differential-Geometry Manifold Evaluators ──────── */

Vector3 LiquidSWE_EvalWhipSpine3D(float s, float tau, Vector2 origXZ, Vector2 targetXZ,
                                  float archH, float latCurve, float swaySign, float phase)
{
    float cs = SWE_Clamp(s, 0.0f, 1.05f);
    float dx = targetXZ.x - origXZ.x;
    float dz = targetXZ.y - origXZ.y;
    float dist = fmaxf(0.5f, sqrtf(dx * dx + dz * dz));
    float dirX = dx / dist;
    float dirZ = dz / dist;
    float perpX = -dirZ;
    float perpZ = dirX;

    /* 1. Tai-Chi orbital coil around origin */
    float coilAngle = (cs * 5.2f - phase * 0.55f) * swaySign - PI * 0.5f;
    float coilRad = (1.15f + 0.35f * sinf(cs * PI)) * (1.0f - 0.25f * cs);
    float coilX = origXZ.x + dirX * (sinf(coilAngle) * coilRad) + perpX * (cosf(coilAngle) * coilRad);
    float coilZ = origXZ.y + dirZ * (sinf(coilAngle) * coilRad) + perpZ * (cosf(coilAngle) * coilRad);
    float coilY = 0.25f + cs * archH * 0.88f + 0.25f * sinf(cs * 2.0f * PI - phase * 0.5f);

    /* 2. Forward whipping strike trajectory */
    float env = sinf(SWE_Clamp(cs, 0.0f, 1.0f) * PI);
    float whipWave = (0.28f + 0.35f * cs) * swaySign * sinf(cs * 6.0f - tau * 9.5f) * env;
    float sideOffset = latCurve * env + whipWave;
    float strikeX = origXZ.x + dirX * (cs * dist) + perpX * sideOffset;
    float strikeZ = origXZ.y + dirZ * (cs * dist) + perpZ * sideOffset;
    float strikeY = 4.0f * archH * cs * fmaxf(0.0f, 1.0f - cs * 0.90f)
                  + 0.45f * env
                  + SWE_SmoothStep(0.65f, 1.0f, cs) * 0.48f;
    if (strikeY < 0.04f) strikeY = 0.04f;

    float uncoilBlend = SWE_SmoothStep(0.06f, 0.52f, tau + cs * 0.22f);
    return (Vector3){
        SWE_Lerp(coilX, strikeX, uncoilBlend),
        SWE_Lerp(coilY, strikeY, uncoilBlend),
        SWE_Lerp(coilZ, strikeZ, uncoilBlend)
    };
}

Vector3 LiquidSWE_EvalHydroTrajectory3D(float s, Vector2 origXZ, Vector2 targetXZ,
                                        float archH, float latCurve, float swaySign, float phase)
{
    (void)phase;
    float cs = SWE_Clamp(s, 0.0f, 1.05f);
    float dx = targetXZ.x - origXZ.x;
    float dz = targetXZ.y - origXZ.y;
    float dist = fmaxf(0.5f, sqrtf(dx * dx + dz * dz));
    float dirX = dx / dist;
    float dirZ = dz / dist;
    float perpX = -dirZ;
    float perpZ = dirX;

    float env = sinf(SWE_Clamp(cs, 0.0f, 1.0f) * PI);
    float sideOffset = latCurve * env + swaySign * 0.22f * sinf(cs * 2.0f * PI) * env;
    float posX = origXZ.x + dirX * (cs * dist) + perpX * sideOffset;
    float posZ = origXZ.y + dirZ * (cs * dist) + perpZ * sideOffset;

    float archParabola = 4.0f * archH * cs * fmaxf(0.0f, 1.0f - cs * 0.90f);
    float launchLift = SWE_SmoothStep(0.0f, 0.16f, cs) * (1.0f - SWE_SmoothStep(0.78f, 1.02f, cs)) * 0.45f;
    float targetTouchY = SWE_SmoothStep(0.65f, 1.0f, cs) * 0.50f;
    float posY = fmaxf(0.04f, archParabola + launchLift + targetTouchY);

    return (Vector3){posX, posY, posZ};
}

Vector3 LiquidSWE_EvalTwinHorizontalWhip3D(float uLong, float tau, Vector2 origXZ, Vector2 targetXZ,
                                           float planeH, float orbR, float swaySign, float phase)
{
    float clampTau = SWE_Clamp(tau, 0.0f, 1.0f);
    float hubX = SWE_Lerp(origXZ.x, targetXZ.x, clampTau);
    float hubZ = SWE_Lerp(origXZ.y, targetXZ.y, clampTau);
    float s = fabsf(uLong);

    float sprout = SWE_SmoothStep(0.0f, 0.22f, tau) * SWE_SmoothStep(1.04f, 0.88f, tau);
    float maxReach = orbR * 2.35f * sprout;

    float spinAngle = phase * 2.45f * swaySign;
    float whipLag = 2.15f * powf(s, 1.28f) * swaySign;
    float lashWave = 0.22f * sinf(s * 6.5f - phase * 4.2f) * s * swaySign;
    float theta = spinAngle - whipLag + lashWave;

    float posX = hubX + cosf(theta) * (uLong * maxReach);
    float posZ = hubZ + sinf(theta) * (uLong * maxReach);

    float stemRise = SWE_Lerp(0.28f, 1.0f, SWE_SmoothStep(0.0f, 0.22f, s));
    float horizUndulation = 0.11f * sinf(s * 8.0f - phase * 3.8f) * s;
    float posY = fmaxf(0.05f, (planeH * stemRise + horizUndulation) * sprout);

    return (Vector3){posX, posY, posZ};
}

LiquidSWESample LiquidSWE_EvalManifoldPoint(const LiquidSWEManifold *m,
                                            float uLong, float uCross, float localH)
{
    LiquidSWESample out = {0};
    if (!m) {
        out.normal = (Vector3){0.0f, 1.0f, 0.0f};
        return out;
    }

    float orbR = fmaxf(m->sizeScale3D, 0.2f);
    float phase = m->spinPhase;
    float tau = m->progressTau;
    Vector2 origXZ = m->origXZ;
    Vector2 targetXZ = m->targetXZ;
    float archH = m->archHeight;
    float latCurve = m->lateralCurve;
    float swaySign = (m->swaySign >= 0.0f) ? 1.0f : -1.0f;

    float dx = targetXZ.x - origXZ.x;
    float dz = targetXZ.y - origXZ.y;
    float dlen = sqrtf(dx * dx + dz * dz);
    Vector2 moveDir = (dlen > 1e-4f) ? (Vector2){dx / dlen, dz / dlen} : (Vector2){1.0f, 0.0f};
    Vector2 perpDir = {-moveDir.y, moveDir.x};

    Vector3 pos = m->center;
    Vector3 nrm = {0.0f, 1.0f, 0.0f};

    switch (m->shape) {
    case LIQUID_SWE_SHAPE_SPHERE: {
        float sCenter = SWE_Clamp(tau, 0.0f, 1.02f);
        Vector3 center3D = LiquidSWE_EvalHydroTrajectory3D(sCenter, origXZ, targetXZ, archH, latCurve, swaySign, phase);
        Vector3 pAhead = LiquidSWE_EvalHydroTrajectory3D(sCenter + 0.025f, origXZ, targetXZ, archH, latCurve, swaySign, phase);
        Vector3 pBehind = LiquidSWE_EvalHydroTrajectory3D(fmaxf(0.0f, sCenter - 0.025f), origXZ, targetXZ, archH, latCurve, swaySign, phase);
        Vector3 tan3D = SWE_V3Norm(SWE_V3Sub(pAhead, pBehind));

        float theta = (uLong * 0.5f + 0.5f) * PI;
        float phi = uCross * PI + phase * 1.4f * swaySign;
        Vector3 sphN = SWE_V3Norm((Vector3){
            sinf(theta) * cosf(phi),
            cosf(theta),
            sinf(theta) * sinf(phi)
        });

        float alignT = sphN.x * tan3D.x + sphN.y * tan3D.y + sphN.z * tan3D.z;
        float laminarWave = 0.035f * sinf(alignT * 6.0f - phase * 2.2f) + 0.02f * cosf(phi * 3.0f + phase * 1.5f);
        float neckPull = SWE_SmoothStep(0.22f, 0.02f, tau) * SWE_SmoothStep(-0.2f, -0.95f, sphN.y);
        float effR = orbR * 0.92f + fminf(localH, 0.45f) * 0.16f + laminarWave * orbR;

        pos = SWE_V3Add(center3D, SWE_V3Scale(sphN, effR));
        pos.y -= neckPull * center3D.y * 0.65f;
        nrm = SWE_V3Norm(SWE_V3Sub(sphN, SWE_V3Scale(tan3D, 0.12f * alignT)));
        break;
    }

    case LIQUID_SWE_SHAPE_TAI_CHI_WHIP: {
        float whipSpan = 0.62f;
        float sHead = SWE_Clamp(tau * 1.48f, 0.05f, 1.04f);
        float sTail = SWE_Clamp(tau * 1.48f - whipSpan, 0.0f, 0.96f);
        float u01 = SWE_Clamp(uLong * 0.5f + 0.5f, 0.0f, 1.0f);
        float sPoint = SWE_Lerp(sTail, sHead, u01);

        Vector3 spinePt = LiquidSWE_EvalWhipSpine3D(sPoint, tau, origXZ, targetXZ, archH, latCurve, swaySign, phase);
        Vector3 pNext = LiquidSWE_EvalWhipSpine3D(sPoint + 0.022f, tau, origXZ, targetXZ, archH, latCurve, swaySign, phase);
        Vector3 pPrev = LiquidSWE_EvalWhipSpine3D(fmaxf(0.0f, sPoint - 0.022f), tau, origXZ, targetXZ, archH, latCurve, swaySign, phase);
        Vector3 tan3D = SWE_V3Norm(SWE_V3Sub(pNext, pPrev));
        Vector3 binorm3D = SWE_V3Norm(SWE_V3Cross(tan3D, (Vector3){0.0f, 1.0f, 0.0f}));
        Vector3 norm3D = SWE_V3Norm(SWE_V3Cross(binorm3D, tan3D));

        float thetaTube = uCross * PI;
        float noseTerm = fmaxf(0.0f, (u01 - 0.74f) / 0.26f);
        float noseCone = sqrtf(fmaxf(0.0f, 1.0f - noseTerm * noseTerm));
        float tailTaper = powf(SWE_Clamp(u01 / 0.80f, 0.0f, 1.0f), 0.72f);
        float whipCrest = 1.0f + 0.25f * expf(-powf((u01 - 0.72f) * 5.0f, 2.0f));
        float strandTwist = thetaTube * 2.0f - u01 * 9.0f + phase * 2.4f * swaySign;
        float baseR = (orbR * 0.25f + fminf(localH, 0.40f) * 0.09f)
                    * noseCone * tailTaper * whipCrest * (1.0f + 0.12f * cosf(strandTwist));

        float bankRoll = swaySign * (0.45f * sinf(sPoint * 4.5f - phase * 0.4f));
        Vector3 bRot = SWE_V3Add(SWE_V3Scale(binorm3D, cosf(bankRoll)), SWE_V3Scale(norm3D, sinf(bankRoll)));
        Vector3 nRot = SWE_V3Add(SWE_V3Scale(binorm3D, -sinf(bankRoll)), SWE_V3Scale(norm3D, cosf(bankRoll)));

        Vector3 radialDir = SWE_V3Add(SWE_V3Scale(bRot, sinf(thetaTube) * 1.32f),
                                      SWE_V3Scale(nRot, cosf(thetaTube) * 0.75f));
        pos = SWE_V3Add(spinePt, SWE_V3Scale(radialDir, baseR));
        nrm = SWE_V3Norm(SWE_V3Add(SWE_V3Scale(bRot, sinf(thetaTube) * 0.75f),
                                   SWE_V3Scale(nRot, cosf(thetaTube) * 1.32f)));
        break;
    }

    case LIQUID_SWE_SHAPE_TORUS_CHAKRAM: {
        float sCenter = SWE_Clamp(tau, 0.0f, 1.02f);
        Vector3 center3D = LiquidSWE_EvalHydroTrajectory3D(sCenter, origXZ, targetXZ, archH, latCurve * 0.65f, swaySign, phase);

        /* Full 370-deg closed circular ring with smooth overlap */
        float closeArc = SWE_Lerp(0.88f, 1.04f, SWE_SmoothStep(0.0f, 0.24f, tau));
        float ringPhi = uLong * PI * closeArc + phase * 2.6f * swaySign;
        float tubeTheta = uCross * PI;

        /* True orthonormal tilted disc frame */
        float tiltPitch = 0.34f * swaySign * cosf(tau * PI) + latCurve * 0.18f;
        float tiltRoll = 0.28f * sinf(tau * PI) + latCurve * 0.22f;
        Vector3 baseRight = {perpDir.x, 0.0f, perpDir.y};
        Vector3 baseFwd = {moveDir.x, 0.0f, moveDir.y};
        Vector3 baseUp = {0.0f, 1.0f, 0.0f};

        /* Rotate baseRight/baseUp by tiltRoll around baseFwd */
        Vector3 r1 = SWE_V3Add(SWE_V3Scale(baseRight, cosf(tiltRoll)), SWE_V3Scale(baseUp, sinf(tiltRoll)));
        Vector3 u1 = SWE_V3Sub(SWE_V3Scale(baseUp, cosf(tiltRoll)), SWE_V3Scale(baseRight, sinf(tiltRoll)));
        /* Rotate baseFwd/u1 by tiltPitch around r1 */
        Vector3 ringFwd = SWE_V3Norm(SWE_V3Add(SWE_V3Scale(baseFwd, cosf(tiltPitch)), SWE_V3Scale(u1, sinf(tiltPitch))));
        Vector3 ringUp = SWE_V3Norm(SWE_V3Sub(SWE_V3Scale(u1, cosf(tiltPitch)), SWE_V3Scale(baseFwd, sinf(tiltPitch))));
        Vector3 ringRight = SWE_V3Norm(SWE_V3Cross(ringUp, ringFwd));

        float seamTaper = SWE_SmoothStep(1.02f, 0.85f, fabsf(uLong));
        float majorR = orbR * 1.18f;

        /* Sleek Razor Water Chakram cross-section: rounded inner collar, knife-sharp outer hydro-blade */
        float outerFactor = fmaxf(0.0f, cosf(tubeTheta));
        float innerFactor = fmaxf(0.0f, -cosf(tubeTheta));
        float bladeSharpness = 1.0f - 0.86f * powf(outerFactor, 1.15f);
        /* Subtle 6-fold swept spiral micro-fins so the disc stays perfectly circular */
        float spiralFin = 0.042f * orbR * outerFactor * sinf(ringPhi * 6.0f - phase * 4.2f * swaySign);
        float innerRipple = 1.0f + 0.06f * sinf(ringPhi * 3.0f + phase * 2.0f);

        float minorR_rad = (orbR * 0.24f + fminf(localH, 0.35f) * 0.06f) * (0.82f + 0.18f * seamTaper) * innerRipple + spiralFin;
        float minorR_up = (orbR * 0.095f) * bladeSharpness * (1.0f - 0.20f * innerFactor) * (0.80f + 0.20f * seamTaper);

        Vector3 radialRingDir = SWE_V3Add(SWE_V3Scale(ringRight, cosf(ringPhi)),
                                          SWE_V3Scale(ringFwd, sinf(ringPhi)));
        pos = SWE_V3Add(center3D,
                        SWE_V3Add(SWE_V3Scale(radialRingDir, majorR + cosf(tubeTheta) * minorR_rad),
                                  SWE_V3Scale(ringUp, sinf(tubeTheta) * minorR_up)));
        nrm = SWE_V3Norm(SWE_V3Add(SWE_V3Scale(radialRingDir, cosf(tubeTheta) * bladeSharpness * 0.75f),
                                   SWE_V3Scale(ringUp, sinf(tubeTheta))));
        break;
    }

    case LIQUID_SWE_SHAPE_WATER_SPOUT: {
        float sCenter = SWE_Clamp(tau, 0.0f, 1.0f);
        float spoutX = SWE_Lerp(origXZ.x, targetXZ.x, sCenter);
        float spoutZ = SWE_Lerp(origXZ.y, targetXZ.y, sCenter);

        float sHeight = SWE_Clamp(uLong * 0.5f + 0.5f, 0.0f, 1.0f);
        float buildUp = SWE_SmoothStep(0.0f, 0.20f, tau) * SWE_SmoothStep(1.04f, 0.84f, tau);

        float cylTheta = uCross * PI * 1.05f - phase * 2.8f * swaySign + sHeight * 7.5f * swaySign;
        float cX = cosf(cylTheta);
        float cZ = sinf(cylTheta);

        float leanAlong = powf(sHeight, 1.4f) * 0.75f * sinf(tau * PI);
        float leanPerp = 0.24f * sinf(sHeight * 4.2f - phase * 1.8f) * sHeight;
        float leanX = moveDir.x * leanAlong + perpDir.x * leanPerp;
        float leanZ = moveDir.y * leanAlong + perpDir.y * leanPerp;

        float baseFlare = 0.58f * powf(1.0f - sHeight, 2.4f);
        float waistR = 0.34f;
        float crownFlare = 0.64f * sinf(powf(sHeight, 1.15f) * 2.65f);
        float funnelRadius = orbR * (waistR + baseFlare + crownFlare);

        float seamRound = SWE_SmoothStep(1.02f, 0.82f, fabsf(uCross));
        float helixRib = 1.0f + 0.18f * cosf(cylTheta * 3.0f - sHeight * 10.0f + phase * 3.2f * swaySign) * seamRound;
        float effFunnelR = (funnelRadius + fminf(localH, 0.45f) * 0.12f) * helixRib;

        float lipArch = sinf(fminf(1.0f, sHeight * 1.06f) * (PI * 0.5f));
        float curHeight = lipArch * archH * 1.38f * buildUp;

        pos = (Vector3){
            spoutX + leanX + cX * effFunnelR,
            fmaxf(0.02f, curHeight),
            spoutZ + leanZ + cZ * effFunnelR
        };
        float slopeY = 0.32f * (1.0f - 2.0f * sHeight);
        nrm = SWE_V3Norm((Vector3){cX, slopeY, cZ});
        break;
    }

    case LIQUID_SWE_SHAPE_OCTOPUS_FORM: {
        /* Map uLong in [-1, 1] across 8 radial sectors, or per-arm [0..1] when drawn per-arm */
        float numArms = 8.0f;
        float arm01 = SWE_Clamp(uLong * 0.5f + 0.5f, 0.0f, 0.9999f);
        float armIdx = floorf(arm01 * numArms);
        float sArm = (arm01 * numArms) - armIdx;
        float thetaArm = uCross * PI;
        float lashPhase = SWE_Clamp((tau - 0.18f) * 1.45f, 0.0f, 1.0f);

        float centerAngle = (armIdx + 0.5f) * (2.0f * PI / numArms) - PI;
        float armRadX = cosf(centerAngle);
        float armRadZ = sinf(centerAngle);
        float armTangX = -armRadZ;
        float armTangZ = armRadX;

        float armWave = sinf(sArm * 4.5f - phase * 2.4f + armIdx * 1.1f);
        float reachR = SWE_Lerp(orbR * 1.65f, SWE_Lerp(orbR * 1.15f, 0.28f, lashPhase), powf(sArm, 0.90f))
                     + 0.25f * armWave * sArm;
        float tentacleY = sinf(sArm * 2.75f) * archH * (1.12f - 0.35f * lashPhase * sArm)
                        * SWE_SmoothStep(0.0f, 0.12f, sArm);

        float armTaper = (1.0f - 0.72f * sArm) * SWE_SmoothStep(0.0f, 0.15f, sArm);
        float armTubeR = (orbR * 0.25f + fminf(localH, 0.40f) * 0.08f) * armTaper;

        float spineX = m->center.x + armRadX * reachR + armTangX * (0.20f * sArm * armWave);
        float spineZ = m->center.z + armRadZ * reachR + armTangZ * (0.20f * sArm * armWave);

        pos = (Vector3){
            spineX + (armTangX * sinf(thetaArm) + armRadX * cosf(thetaArm) * 0.45f) * armTubeR,
            fmaxf(0.02f, tentacleY + cosf(thetaArm) * armTubeR),
            spineZ + (armTangZ * sinf(thetaArm) + armRadZ * cosf(thetaArm) * 0.45f) * armTubeR
        };
        nrm = SWE_V3Norm((Vector3){
            armTangX * sinf(thetaArm) - armRadX * 0.35f,
            cosf(thetaArm) + 0.35f,
            armTangZ * sinf(thetaArm) - armRadZ * 0.35f
        });
        break;
    }

    case LIQUID_SWE_SHAPE_CURLING_WAVE: {
        float sCenter = SWE_Clamp(tau, 0.0f, 1.02f);
        float baseX = SWE_Lerp(origXZ.x, targetXZ.x, sCenter);
        float baseZ = SWE_Lerp(origXZ.y, targetXZ.y, sCenter);

        float sCrest = SWE_Clamp(uLong * 0.5f + 0.5f, 0.0f, 1.0f);
        float spanFalloff = sqrtf(fmaxf(0.0f, 1.0f - uCross * uCross * 0.88f));

        float curlAngle = powf(sCrest, 1.20f) * (2.45f + 0.55f * SWE_SmoothStep(0.35f, 0.95f, tau));
        float waveHeight = (sinf(curlAngle * 0.66f) * archH * 1.25f) * spanFalloff;
        float forwardOverhang = (sCrest * 1.40f - (1.0f - cosf(curlAngle)) * 0.92f) * orbR;
        float crescentCurve = -0.55f * (uCross * uCross) * orbR;

        float posX = baseX + moveDir.x * (forwardOverhang + crescentCurve) + perpDir.x * (uCross * orbR * 2.25f);
        float posZ = baseZ + moveDir.y * (forwardOverhang + crescentCurve) + perpDir.y * (uCross * orbR * 2.25f);
        float crestRibs = 0.06f * sinf(uCross * 10.0f + phase * 1.8f) * sCrest;

        pos = (Vector3){posX, fmaxf(0.03f, waveHeight + crestRibs + localH * 0.32f), posZ};
        nrm = SWE_V3Norm((Vector3){
            moveDir.x * sinf(curlAngle) - perpDir.x * (uCross * 0.32f),
            cosf(curlAngle * 0.62f) + 0.28f,
            moveDir.y * sinf(curlAngle) - perpDir.y * (uCross * 0.32f)
        });
        break;
    }

    case LIQUID_SWE_SHAPE_TWIN_WHIP:
    default: {
        float s = fabsf(uLong);
        Vector3 spinePt = LiquidSWE_EvalTwinHorizontalWhip3D(uLong, tau, origXZ, targetXZ, archH, orbR, swaySign, phase);
        Vector3 pNext = LiquidSWE_EvalTwinHorizontalWhip3D(SWE_Clamp(uLong + 0.03f, -1.0f, 1.0f), tau, origXZ, targetXZ, archH, orbR, swaySign, phase);
        Vector3 pPrev = LiquidSWE_EvalTwinHorizontalWhip3D(SWE_Clamp(uLong - 0.03f, -1.0f, 1.0f), tau, origXZ, targetXZ, archH, orbR, swaySign, phase);

        Vector3 tan3D = SWE_V3Norm(SWE_V3Sub(pNext, pPrev));
        Vector3 binorm3D = SWE_V3Norm(SWE_V3Cross(tan3D, (Vector3){0.0f, 1.0f, 0.0f}));
        Vector3 norm3D = SWE_V3Norm(SWE_V3Cross(binorm3D, tan3D));

        float thetaTube = uCross * PI;
        float hubFlare = 1.0f + 0.55f * expf(-s * s * 28.0f);
        float tipTerm = fmaxf(0.0f, (s - 0.72f) / 0.28f);
        float tipTaper = sqrtf(fmaxf(0.0f, 1.0f - tipTerm * tipTerm)) * (1.0f - 0.38f * s);
        float spiralTwist = cosf(thetaTube * 2.0f - s * 10.0f + phase * 3.5f * swaySign);
        float tubeR = (orbR * 0.23f + fminf(localH, 0.40f) * 0.09f) * hubFlare * tipTaper * (1.0f + 0.14f * spiralTwist);

        float horizBladeFlatten = SWE_Lerp(1.0f, 1.55f, SWE_SmoothStep(0.25f, 0.85f, s));
        float vertBladeFlatten = SWE_Lerp(1.0f, 0.62f, SWE_SmoothStep(0.25f, 0.85f, s));

        Vector3 radialOffset = SWE_V3Add(SWE_V3Scale(binorm3D, sinf(thetaTube) * horizBladeFlatten),
                                         SWE_V3Scale(norm3D, cosf(thetaTube) * vertBladeFlatten));
        pos = SWE_V3Add(spinePt, SWE_V3Scale(radialOffset, tubeR));
        nrm = SWE_V3Norm(SWE_V3Add(SWE_V3Scale(binorm3D, sinf(thetaTube) * vertBladeFlatten),
                                   SWE_V3Scale(norm3D, cosf(thetaTube) * horizBladeFlatten)));
        break;
    }
    }

    out.position = pos;
    out.normal = nrm;
    return out;
}

/* ── Virtual-Pipe SWE Solver & Mesh Cache ────────────────────────────────── */

static float SWE_ComputeObstacleBathymetry(Vector2 worldXZ, Vector2 targetXZ, float obsScale)
{
    if (obsScale <= 0.01f) return 0.0f;
    float px = (worldXZ.x - targetXZ.x) / obsScale;
    float pz = (worldXZ.y - targetXZ.y) / obsScale;
    float r = sqrtf(px * px + pz * pz);
    float angle = atan2f(pz, px);
    float shape = 1.0f
                + 0.16f * cosf(angle * 3.0f + 0.7f)
                + 0.10f * sinf(angle * 5.0f - 1.2f)
                + 0.06f * cosf(angle * 7.0f);
    float effR = r / fmaxf(shape, 0.4f);
    float skirt = SWE_SmoothStep(1.38f, 0.82f, effR);
    return skirt * 1.15f * obsScale;
}

static void SWE_StepVirtualPipe(LiquidSWEInstance *inst, float dt)
{
    float gravity = fmaxf(inst->desc.gravity, 1.0f);
    float freezeF = SWE_ComputeCryoProgress(inst);
    float fluxFreezeScale = 0.04f + 0.96f * (1.0f - freezeF) * (1.0f - freezeF);
    float pipeAccel = dt * gravity * 1.68f * fluxFreezeScale;
    float dragFactor = expf(-fmaxf(inst->desc.drag, 0.05f) * dt * 0.25f) * 0.993f * (1.0f - 0.75f * freezeF);
    float evap = fmaxf(inst->desc.evaporation, 0.0f) * (1.0f - 0.85f * freezeF);
    bool isATLA = SWE_IsATLAPattern(inst->desc.pattern);

    /* PASS 1: Virtual-Pipe Flux Update + Emitter Force Injection */
    for (int iz = 0; iz < SWE_NZ; ++iz) {
        for (int ix = 0; ix < SWE_NX; ++ix) {
            float hC = inst->h[iz][ix];
            float etaC = hC + inst->b[iz][ix] + inst->bObs[iz][ix];

            float etaR = (ix < SWE_NX - 1) ? (inst->h[iz][ix + 1] + inst->b[iz][ix + 1] + inst->bObs[iz][ix + 1]) : etaC;
            float etaL = (ix > 0)          ? (inst->h[iz][ix - 1] + inst->b[iz][ix - 1] + inst->bObs[iz][ix - 1]) : etaC;
            float etaT = (iz < SWE_NZ - 1) ? (inst->h[iz + 1][ix] + inst->b[iz + 1][ix] + inst->bObs[iz + 1][ix]) : etaC;
            float etaB = (iz > 0)          ? (inst->h[iz - 1][ix] + inst->b[iz - 1][ix] + inst->bObs[iz - 1][ix]) : etaC;

            float fR = fmaxf(0.0f, inst->fR[iz][ix] + pipeAccel * (etaC - etaR));
            float fL = fmaxf(0.0f, inst->fL[iz][ix] + pipeAccel * (etaC - etaL));
            float fT = fmaxf(0.0f, inst->fT[iz][ix] + pipeAccel * (etaC - etaT));
            float fB = fmaxf(0.0f, inst->fB[iz][ix] + pipeAccel * (etaC - etaB));

            if (inst->emitterCount > 0) {
                Vector3 wp = SWE_CellWorldXZ(inst, ix, iz);
                for (int e = 0; e < inst->emitterCount; ++e) {
                    const LiquidSWEEmitter *em = &inst->emitters[e];
                    float rad = fmaxf(0.08f, em->radius);
                    float rx = wp.x - em->posXZ.x;
                    float rz = wp.z - em->posXZ.y;
                    float dist = sqrtf(rx * rx + rz * rz);
                    if (dist < rad) {
                        float falloff = SWE_SmoothStep(rad, 0.0f, dist);
                        float dirX = (dist > 0.001f) ? (rx / dist) : 0.0f;
                        float dirZ = (dist > 0.001f) ? (rz / dist) : 0.0f;
                        float tangX = -dirZ;
                        float tangZ = dirX;

                        float fx = em->linearVel.x + tangX * em->swirl + dirX * em->radialImpulse;
                        float fz = em->linearVel.y + tangZ * em->swirl + dirZ * em->radialImpulse;
                        float boost = dt * falloff * 0.92f;

                        fR += fmaxf(0.0f,  fx) * boost;
                        fL += fmaxf(0.0f, -fx) * boost;
                        fT += fmaxf(0.0f,  fz) * boost;
                        fB += fmaxf(0.0f, -fz) * boost;
                    }
                }
            }

            /* Obstacle solid core reflection (bObs > 0.65m) */
            if (inst->bObs[iz][ix] > 0.65f) {
                fR = fL = fT = fB = 0.0f;
            } else {
                if (ix < SWE_NX - 1 && inst->bObs[iz][ix + 1] > 0.65f) fR = 0.0f;
                if (ix > 0          && inst->bObs[iz][ix - 1] > 0.65f) fL = 0.0f;
                if (iz < SWE_NZ - 1 && inst->bObs[iz + 1][ix] > 0.65f) fT = 0.0f;
                if (iz > 0          && inst->bObs[iz - 1][ix] > 0.65f) fB = 0.0f;
            }

            if (ix <= 1) fL = 0.0f;
            if (ix >= SWE_NX - 2) fR = 0.0f;
            if (iz <= 1) fB = 0.0f;
            if (iz >= SWE_NZ - 2) fT = 0.0f;

            fR *= dragFactor;
            fL *= dragFactor;
            fT *= dragFactor;
            fB *= dragFactor;

            float totalOut = fR + fL + fT + fB;
            if (totalOut > 1e-5f) {
                float maxOut = hC / 0.24f;
                float K = fminf(1.0f, maxOut / totalOut);
                fR *= K;
                fL *= K;
                fT *= K;
                fB *= K;
            } else {
                fR = fL = fT = fB = 0.0f;
            }

            inst->fR[iz][ix] = fR;
            inst->fL[iz][ix] = fL;
            inst->fT[iz][ix] = fT;
            inst->fB[iz][ix] = fB;
        }
    }

    /* PASS 2: Height Integration, Eddy Smoothing, Hydraulic Shock Foam & Ground Wetness */
    for (int iz = 0; iz < SWE_NZ; ++iz) {
        for (int ix = 0; ix < SWE_NX; ++ix) {
            float h = inst->h[iz][ix];
            float bObs = inst->bObs[iz][ix];
            float foam = inst->foam[iz][ix];
            float wet = inst->wet[iz][ix];

            float fInL = (ix > 0)          ? inst->fR[iz][ix - 1] : 0.0f;
            float fInR = (ix < SWE_NX - 1) ? inst->fL[iz][ix + 1] : 0.0f;
            float fInB = (iz > 0)          ? inst->fT[iz - 1][ix] : 0.0f;
            float fInT = (iz < SWE_NZ - 1) ? inst->fB[iz + 1][ix] : 0.0f;
            float inflow = fInL + fInR + fInB + fInT;
            float outflow = inst->fR[iz][ix] + inst->fL[iz][ix] + inst->fT[iz][ix] + inst->fB[iz][ix];

            float dh = (inflow - outflow) * 0.24f;
            float hR = (ix < SWE_NX - 1) ? inst->h[iz][ix + 1] : h;
            float hL = (ix > 0)          ? inst->h[iz][ix - 1] : h;
            float hT = (iz < SWE_NZ - 1) ? inst->h[iz + 1][ix] : h;
            float hB = (iz > 0)          ? inst->h[iz - 1][ix] : h;
            float avgNeighborH = 0.25f * (hR + hL + hT + hB);

            h = fmaxf(0.0f, h + dh);
            if (h > 0.002f && bObs < 0.65f) {
                h = SWE_Lerp(h, avgNeighborH, 0.032f);
            }

            Vector3 wp = SWE_CellWorldXZ(inst, ix, iz);
            float emitterFoamBoost = 0.0f;
            if (inst->emitterCount > 0 && bObs < 0.65f) {
                for (int e = 0; e < inst->emitterCount; ++e) {
                    const LiquidSWEEmitter *em = &inst->emitters[e];
                    float rad = fmaxf(0.08f, em->radius);
                    float dx = wp.x - em->posXZ.x;
                    float dz = wp.z - em->posXZ.y;
                    float dist = sqrtf(dx * dx + dz * dz);
                    if (dist < rad) {
                        float profile = SWE_SmoothStep(rad, 0.0f, dist);
                        float ringProfile = sinf(profile * PI);
                        h = fmaxf(0.0f, h + em->heightRate * profile * dt);
                        emitterFoamBoost += fabsf(em->heightRate) * (0.28f * profile + 0.22f * ringProfile) * dt;
                    }
                }
            }

            /* Dry-moat cleanup around detached 3D manifolds */
            float moatClean = 0.0f;
            for (int m = 0; m < LIQUID_SWE_MAX_MANIFOLDS; ++m) {
                const LiquidSWEManifold *mf = &inst->manifolds[m];
                if (!mf->active || mf->detachAlpha <= 0.16f || mf->shape > LIQUID_SWE_SHAPE_TORUS_CHAKRAM) {
                    continue;
                }
                float capR = fmaxf(0.2f, mf->captureRadius);
                float dx = wp.x - mf->center.x;
                float dz = wp.z - mf->center.z;
                float u = sqrtf(dx * dx + dz * dz) / capR;
                if (u > 0.85f && u < 1.55f) {
                    moatClean = fmaxf(moatClean, SWE_SmoothStep(0.16f, 0.38f, mf->detachAlpha));
                }
            }
            if (moatClean > 0.01f) {
                h *= fmaxf(0.0f, 1.0f - moatClean * dt * 18.0f);
                if (moatClean > 0.35f) h = 0.0f;
            }

            if (bObs > 0.65f) {
                h = 0.0f;
            }

            /* Sponge boundary damping on outer grid rim */
            float nx = (float)ix / (float)(SWE_NX - 1);
            float nz = (float)iz / (float)(SWE_NZ - 1);
            float spongeX = SWE_SmoothStep(0.0f, 0.12f, nx) * SWE_SmoothStep(1.0f, 0.88f, nx);
            float spongeZ = SWE_SmoothStep(0.0f, 0.08f, nz) * SWE_SmoothStep(1.0f, 0.92f, nz);
            float sponge = 0.82f + 0.18f * (spongeX * spongeZ);
            h *= sponge;

            /* Natural drainage / evaporation back to dry ground */
            if (h > 0.0f && evap > 0.0f) {
                float drain = evap * dt * (0.14f + 0.35f * h);
                h = fmaxf(0.0f, h - drain);
                if (h < 0.0011f) h = 0.0f;
            }
            if (!isATLA && h < 0.35f && freezeF < 0.5f) {
                h *= expf(-1.2f * (1.0f - freezeF) * dt);
            }

            float vx = (fInL - inst->fL[iz][ix] + inst->fR[iz][ix] - fInR) * 0.5f;
            float vz = (fInB - inst->fB[iz][ix] + inst->fT[iz][ix] - fInT) * 0.5f;
            float speed = sqrtf(vx * vx + vz * vz);
            inst->speedGrid[iz][ix] = speed;

            float bR = (ix < SWE_NX - 1) ? inst->bObs[iz][ix + 1] : bObs;
            float bL = (ix > 0)          ? inst->bObs[iz][ix - 1] : bObs;
            float bT = (iz < SWE_NZ - 1) ? inst->bObs[iz + 1][ix] : bObs;
            float bB = (iz > 0)          ? inst->bObs[iz - 1][ix] : bObs;
            float dB = fabsf(bR - bL) + fabsf(bT - bB);

            float obstacleImpact = speed * dB * 2.2f;
            float waveSteepness = fmaxf(0.0f, fabsf(h - avgNeighborH) - 0.06f) * 3.2f;
            float foamTarget = SWE_Clamp(speed * 0.14f + obstacleImpact + waveSteepness + emitterFoamBoost * 0.55f, 0.0f, 1.15f);
            foam = foam * expf(-2.2f * dt) + foamTarget * (1.0f - expf(-5.0f * dt));
            foam = SWE_Clamp(foam, 0.0f, 1.35f);
            if (h <= 0.0012f) foam *= 0.75f;

            if (h > 0.014f) {
                float targetWet = SWE_Clamp((h - 0.014f) * 6.5f, 0.0f, 1.0f) * sponge;
                wet = fmaxf(wet, fminf(1.0f, wet + dt * 6.0f * targetWet));
            } else {
                wet = fmaxf(0.0f, wet - dt * 0.28f);
            }
            wet *= sponge;

            inst->hNext[iz][ix] = fminf(h, 4.5f);
            inst->foamNext[iz][ix] = foam;
            inst->wetNext[iz][ix] = wet;
        }
    }

    memcpy(inst->h, inst->hNext, sizeof(inst->h));
    memcpy(inst->foam, inst->foamNext, sizeof(inst->foam));
    memcpy(inst->wet, inst->wetNext, sizeof(inst->wet));
}

static void SWE_RebuildMeshCache(LiquidSWEInstance *inst)
{
    float curlGain = SWE_Clamp(inst->desc.curlIntensity, 0.0f, 1.5f);
    bool isATLA = SWE_IsATLAPattern(inst->desc.pattern);
    Vector3 peakPos = inst->origin;
    float peakH = 0.0f;

    for (int iz = 0; iz < SWE_NZ; ++iz) {
        for (int ix = 0; ix < SWE_NX; ++ix) {
            float h = inst->h[iz][ix];
            Vector3 wp = SWE_CellWorldXZ(inst, ix, iz);

            float hL = inst->h[iz][ix > 0 ? ix - 1 : ix];
            float hR = inst->h[iz][ix < SWE_NX - 1 ? ix + 1 : ix];
            float hD = inst->h[iz > 0 ? iz - 1 : iz][ix];
            float hU = inst->h[iz < SWE_NZ - 1 ? iz + 1 : iz][ix];
            float dhdx = (hR - hL) / (2.0f * inst->dx);
            float dhdz = (hU - hD) / (2.0f * inst->dz);

            float u = (inst->fR[iz][ix] - inst->fL[iz][ix]) * 6.0f;
            float v = (inst->fT[iz][ix] - inst->fB[iz][ix]) * 6.0f;
            float spd = sqrtf(u * u + v * v);
            float dirDotSlope = (spd > 0.25f) ? -((u * dhdx + v * dhdz) / spd) : 0.0f;
            float breakFactor = SWE_Clamp(dirDotSlope * 0.55f, 0.0f, 1.0f) * SWE_Clamp(h / 0.45f, 0.0f, 1.0f);

            float curlDrop = 0.0f;
            if (!isATLA) {
                float curlDist = fminf(curlGain * breakFactor * h * 0.45f, inst->dz * 2.2f);
                curlDrop = curlDist * 0.25f * breakFactor;
                if (spd > 0.25f && curlDist > 0.001f) {
                    float invS = 1.0f / spd;
                    Vector3 flowDir = SWE_V3Add(SWE_V3Scale(inst->right, u * invS),
                                                SWE_V3Scale(inst->fwd, v * invS));
                    wp = SWE_V3Add(wp, SWE_V3Scale(flowDir, curlDist));
                }
            }

            float skirtFollow = fminf(inst->bObs[iz][ix], 0.55f) * SWE_SmoothStep(0.0f, 0.025f, h);
            float y = inst->b[iz][ix] + skirtFollow + fmaxf(h - curlDrop, 0.0f);
            inst->posGrid[iz][ix] = (Vector3){wp.x, y, wp.z};
            if (!isATLA) {
                inst->foam[iz][ix] = fmaxf(inst->foam[iz][ix], SWE_Clamp(breakFactor * 1.45f, 0.0f, 1.0f));
            }

            if (h > peakH) {
                peakH = h;
                peakPos = inst->posGrid[iz][ix];
            }
        }
    }

    inst->peakH = peakH;
    inst->peakPos = peakPos;

    for (int iz = 0; iz < SWE_NZ; ++iz) {
        for (int ix = 0; ix < SWE_NX; ++ix) {
            Vector3 pL = inst->posGrid[iz][ix > 0 ? ix - 1 : ix];
            Vector3 pR = inst->posGrid[iz][ix < SWE_NX - 1 ? ix + 1 : ix];
            Vector3 pD = inst->posGrid[iz > 0 ? iz - 1 : iz][ix];
            Vector3 pU = inst->posGrid[iz < SWE_NZ - 1 ? iz + 1 : iz][ix];
            Vector3 tx = SWE_V3Sub(pR, pL);
            Vector3 tz = SWE_V3Sub(pU, pD);
            Vector3 n = SWE_V3Cross(tx, tz);
            if (n.y < 0.0f) n = SWE_V3Scale(n, -1.0f);
            inst->normGrid[iz][ix] = SWE_V3Norm(n);
        }
    }
}

/* ── ATLA 3-Phase Waterbending Choreography ──────────────────────────────── */

static void SWE_EvalPatchCenter(float tau, Vector2 orig, Vector2 target,
                                float archH, float latCurve, float swaySign,
                                LiquidSWEShapeType shapeType,
                                float *outX, float *outY, float *outZ,
                                float *outDirX, float *outDirZ)
{
    float cs = SWE_Clamp(tau, 0.0f, 1.02f);
    float dx = target.x - orig.x;
    float dz = target.y - orig.y;
    float dist = fmaxf(0.5f, sqrtf(dx * dx + dz * dz));
    float dirX = dx / dist;
    float dirZ = dz / dist;
    float perpX = -dirZ;
    float perpZ = dirX;

    if (shapeType == LIQUID_SWE_SHAPE_TAI_CHI_WHIP) {
        float uncoil = SWE_Clamp((cs - 0.12f) / 0.85f, 0.0f, 1.0f);
        float env = sinf(uncoil * PI);
        float sideOffset = latCurve * env;
        *outX = orig.x + dirX * (uncoil * dist) + perpX * sideOffset;
        *outZ = orig.y + dirZ * (uncoil * dist) + perpZ * sideOffset;
        *outY = fmaxf(0.2f, 4.0f * archH * uncoil * (1.0f - uncoil * 0.88f) + 0.6f * (1.0f - uncoil));
        *outDirX = dirX;
        *outDirZ = dirZ;
        return;
    }

    float env = sinf(fminf(1.0f, cs) * PI);
    float sideOffset = latCurve * env + swaySign * 0.22f * sinf(cs * 2.0f * PI) * env;
    *outX = orig.x + dirX * (cs * dist) + perpX * sideOffset;
    *outZ = orig.y + dirZ * (cs * dist) + perpZ * sideOffset;
    *outY = fmaxf(0.15f, 4.0f * archH * cs * fmaxf(0.0f, 1.0f - cs * 0.90f) + 0.35f * env);
    *outDirX = dirX;
    *outDirZ = dirZ;
}

static void SWE_AddEmitterInternal(LiquidSWEInstance *inst, float x, float z, float radius,
                                   float heightRate, float vx, float vz, float swirl, float radial)
{
    if (inst->emitterCount >= LIQUID_SWE_MAX_EMITTERS) return;
    LiquidSWEEmitter *em = &inst->emitters[inst->emitterCount++];
    em->posXZ = (Vector2){x, z};
    em->radius = radius;
    em->heightRate = heightRate;
    em->linearVel = (Vector2){vx, vz};
    em->swirl = swirl;
    em->radialImpulse = radial;
}

static void SWE_RunATLARoutine(LiquidSWEInstance *inst, int manifoldSlot, float t,
                               Vector2 pOrig, Vector2 pTarget, float capR,
                               float size3D, float maxAltitude,
                               LiquidSWEShapeType shapeType, float swaySign,
                               float curveLateral, float pwr)
{
    if (t < 0.0f) return;

    float dx = pTarget.x - pOrig.x;
    float dz = pTarget.y - pOrig.y;
    float len = fmaxf(0.001f, sqrtf(dx * dx + dz * dz));
    float mDirX = dx / len;
    float mDirZ = dz / len;
    float phase = t * 7.8f;

    if (t < 0.48f) {
        /* PHASE 1: Puddle Extraction from ground into swirling Tai-Chi source */
        float p = t / 0.48f;
        float grow = sinf(p * PI * 0.5f);
        SWE_AddEmitterInternal(inst, pOrig.x, pOrig.y,
                               0.95f + grow * (capR * 0.55f),
                               2.15f * pwr, 0.0f, 0.0f,
                               7.2f * pwr * swaySign,
                               2.8f * pwr * cosf(t * 8.0f));
    } else if (t < 2.28f) {
        /* PHASE 2: Fold SWE puddle into 3D ATLA Waterbending Manifold & Strike */
        float rawP = (t - 0.48f) / 1.80f;
        float tau = rawP * rawP * (2.65f - 1.65f * rawP);
        float launchAlpha = fminf(1.0f, (t - 0.48f) / 0.26f);
        float impactUnfold = (rawP < 0.88f) ? 1.0f : fmaxf(0.12f, 1.0f - ((rawP - 0.88f) / 0.12f) * 0.82f);
        float alpha = launchAlpha * impactUnfold;

        float ptX = 0.0f, ptY = 0.0f, ptZ = 0.0f, ptDirX = 1.0f, ptDirZ = 0.0f;
        SWE_EvalPatchCenter(tau, pOrig, pTarget, maxAltitude, curveLateral, swaySign, shapeType,
                            &ptX, &ptY, &ptZ, &ptDirX, &ptDirZ);

        if (shapeType <= LIQUID_SWE_SHAPE_TORUS_CHAKRAM && rawP > 0.14f && rawP < 0.55f) {
            SWE_AddEmitterInternal(inst, pOrig.x, pOrig.y, capR * 0.85f, -2.8f * pwr,
                                   0.0f, 0.0f, 4.0f * swaySign, -9.5f * pwr);
        }

        if (rawP > 0.16f && shapeType <= LIQUID_SWE_SHAPE_WATER_SPOUT) {
            float wakeX = ptX - ptDirX * (capR * 0.92f);
            float wakeZ = ptZ - ptDirZ * (capR * 0.92f);
            SWE_AddEmitterInternal(inst, wakeX, wakeZ, capR * 0.88f, -3.5f * pwr,
                                   0.0f, 0.0f, 0.0f, -10.5f);
        }

        SWE_AddEmitterInternal(inst, ptX, ptZ, capR * 0.78f, 2.15f * pwr,
                               ptDirX * 13.0f * pwr, ptDirZ * 13.0f * pwr,
                               8.5f * pwr * swaySign,
                               (shapeType >= LIQUID_SWE_SHAPE_WATER_SPOUT) ? (6.5f * pwr) : (-4.5f * pwr));

        if (manifoldSlot >= 0 && manifoldSlot < LIQUID_SWE_MAX_MANIFOLDS) {
            LiquidSWEManifold *mf = &inst->manifolds[manifoldSlot];
            mf->active = true;
            mf->shape = shapeType;
            mf->center = (Vector3){ptX, ptY, ptZ};
            mf->captureRadius = capR;
            mf->detachAlpha = alpha;
            mf->sizeScale3D = size3D;
            mf->spinPhase = phase;
            mf->progressTau = tau;
            mf->origXZ = pOrig;
            mf->targetXZ = pTarget;
            mf->archHeight = maxAltitude;
            mf->lateralCurve = curveLateral;
            mf->swaySign = swaySign;
        }

        if (rawP > 0.80f) {
            float preSplash = (rawP - 0.80f) / 0.20f;
            SWE_AddEmitterInternal(inst, pTarget.x, pTarget.y,
                                   1.20f + preSplash * 0.9f,
                                   2.8f * pwr * preSplash,
                                   mDirX * 19.0f * pwr, mDirZ * 19.0f * pwr,
                                   6.0f * pwr * swaySign,
                                   17.5f * pwr * preSplash);
        }
    } else if (t < 3.55f) {
        /* PHASE 3: SWE Hydraulic Impact Wave climbing & splitting around target bathymetry */
        float crashP = (t - 2.28f) / 0.97f;
        if (crashP < 0.70f) {
            float pulse = sinf((crashP / 0.70f) * PI);
            SWE_AddEmitterInternal(inst, pTarget.x, pTarget.y,
                                   1.40f + crashP * 2.4f,
                                   3.2f * pwr * fmaxf(0.2f, pulse),
                                   mDirX * 21.0f * pwr, mDirZ * 21.0f * pwr,
                                   6.5f * pwr * swaySign,
                                   22.5f * pwr);
        }
    }
}

static void SWE_ScheduleATLAEmitters(LiquidSWEInstance *inst)
{
    inst->emitterCount = 0;
    for (int m = 0; m < LIQUID_SWE_MAX_MANIFOLDS; ++m) {
        inst->manifolds[m].active = false;
    }

    if (!SWE_IsATLAPattern(inst->desc.pattern)) return;

    float t = inst->age;
    float pwr = fmaxf(inst->desc.skillPower, 0.3f);
    float scaleH = fmaxf(inst->desc.waveHeight, 0.5f);
    Vector2 orig = {inst->desc.source.x, inst->desc.source.z};
    Vector2 dst = {inst->desc.target.x, inst->desc.target.z};
    float dx = dst.x - orig.x;
    float dz = dst.y - orig.y;
    float dist = fmaxf(0.5f, sqrtf(dx * dx + dz * dz));
    Vector2 dir = {dx / dist, dz / dist};
    Vector2 perp = {-dir.y, dir.x};

    switch (inst->desc.pattern) {
    case LIQUID_SWE_PATTERN_TAI_CHI_WHIP:
        SWE_RunATLARoutine(inst, 0, t, orig, dst, 2.40f, 1.22f, scaleH * 1.15f,
                           LIQUID_SWE_SHAPE_TAI_CHI_WHIP, 1.0f, 0.0f, pwr);
        break;

    case LIQUID_SWE_PATTERN_ORBITAL_CHAKRAM:
        SWE_RunATLARoutine(inst, 0, t, orig, dst, 2.45f, 1.28f, scaleH * 1.12f,
                           LIQUID_SWE_SHAPE_TORUS_CHAKRAM, 1.0f, 0.35f, pwr);
        break;

    case LIQUID_SWE_PATTERN_WATER_SPOUT:
        SWE_RunATLARoutine(inst, 0, t, orig, dst, 2.55f, 1.25f, scaleH * 1.30f,
                           LIQUID_SWE_SHAPE_WATER_SPOUT, 1.0f, 0.0f, pwr * 1.05f);
        break;

    case LIQUID_SWE_PATTERN_OCTOPUS_FORM:
    case LIQUID_SWE_PATTERN_FROZEN_OCTOPUS: {
        Vector2 octoOrigin = {dst.x - dir.x * 0.35f, dst.y - dir.y * 0.35f};
        float freezeF = SWE_ComputeCryoProgress(inst);
        float effT = (inst->desc.pattern == LIQUID_SWE_PATTERN_FROZEN_OCTOPUS && freezeF > 0.01f)
                         ? SWE_Lerp(t, fminf(t, 1.85f), freezeF)
                         : t;
        SWE_RunATLARoutine(inst, 0, effT, octoOrigin, dst, 2.85f, 1.42f, scaleH * 1.20f,
                           LIQUID_SWE_SHAPE_OCTOPUS_FORM, 1.0f, 0.0f, pwr * 1.10f);
        break;
    }

    case LIQUID_SWE_PATTERN_CURLING_WAVE:
        SWE_RunATLARoutine(inst, 0, t, orig, dst, 2.75f, 1.38f, scaleH * 1.25f,
                           LIQUID_SWE_SHAPE_CURLING_WAVE, 1.0f, 0.0f, pwr * 1.15f);
        break;

    case LIQUID_SWE_PATTERN_TRIPLE_DISCS: {
        Vector2 origL = {orig.x - perp.x * 3.6f, orig.y - perp.y * 3.6f};
        Vector2 origC = {orig.x - dir.x * 1.4f, orig.y - dir.y * 1.4f};
        Vector2 origR = {orig.x + perp.x * 3.6f, orig.y + perp.y * 3.6f};
        SWE_RunATLARoutine(inst, 0, t, origL, dst, 1.75f, 0.98f, scaleH * 1.0f,
                           LIQUID_SWE_SHAPE_TORUS_CHAKRAM, 1.0f, -1.55f, pwr * 0.90f);
        if (t >= 0.24f) {
            SWE_RunATLARoutine(inst, 1, t - 0.24f, origC, dst, 1.80f, 1.04f, scaleH * 1.28f,
                               LIQUID_SWE_SHAPE_TORUS_CHAKRAM, -1.0f, 0.0f, pwr * 0.95f);
        }
        if (t >= 0.48f) {
            SWE_RunATLARoutine(inst, 2, t - 0.48f, origR, dst, 1.75f, 0.98f, scaleH * 1.08f,
                               LIQUID_SWE_SHAPE_TORUS_CHAKRAM, 1.0f, 1.55f, pwr * 0.90f);
        }
        break;
    }

    case LIQUID_SWE_PATTERN_TWIN_WHIPS: {
        Vector2 startHub = {orig.x + dir.x * 1.2f, orig.y + dir.y * 1.2f};
        SWE_RunATLARoutine(inst, 0, t, startHub, dst, 2.75f, 1.38f, 1.38f,
                           LIQUID_SWE_SHAPE_TWIN_WHIP, 1.0f, 0.0f, pwr * 1.15f);
        if (t > 0.55f && t < 2.65f) {
            float rawP = fminf(1.0f, (t - 0.48f) / 1.80f);
            float tau = rawP * rawP * (2.65f - 1.65f * rawP);
            float hubX = SWE_Lerp(startHub.x, dst.x, tau);
            float hubZ = SWE_Lerp(startHub.y, dst.y, tau);
            float spinAngle = t * 7.8f * 2.45f - 1.65f;
            float tipReach = 2.45f * fminf(1.0f, (t - 0.48f) / 0.35f);
            float cosA = cosf(spinAngle);
            float sinA = sinf(spinAngle);
            float tangX = -sinA * 18.0f * pwr;
            float tangZ = cosA * 18.0f * pwr;
            SWE_AddEmitterInternal(inst, hubX + cosA * tipReach, hubZ + sinA * tipReach,
                                   1.05f, 1.15f * pwr, tangX, tangZ, 11.0f * pwr, 8.5f * pwr);
            SWE_AddEmitterInternal(inst, hubX - cosA * tipReach, hubZ - sinA * tipReach,
                                   1.05f, 1.15f * pwr, -tangX, -tangZ, 11.0f * pwr, 8.5f * pwr);
        }
        break;
    }

    case LIQUID_SWE_PATTERN_AVATAR_STATE: {
        Vector2 sphereOrig = {orig.x + dir.x * 0.4f, orig.y + dir.y * 0.4f};
        Vector2 ringOrig = {orig.x + perp.x * 3.4f, orig.y + perp.y * 3.4f};
        Vector2 whipOrig = {orig.x - perp.x * 3.4f, orig.y - perp.y * 3.4f};
        SWE_RunATLARoutine(inst, 0, t, sphereOrig, dst, 1.80f, 0.95f, scaleH * 1.30f,
                           LIQUID_SWE_SHAPE_SPHERE, 1.0f, 0.0f, pwr);
        SWE_RunATLARoutine(inst, 1, t, ringOrig, dst, 1.85f, 1.28f, scaleH * 1.30f,
                           LIQUID_SWE_SHAPE_TORUS_CHAKRAM, -1.0f, 1.25f, pwr * 0.95f);
        if (t >= 0.18f) {
            SWE_RunATLARoutine(inst, 2, t - 0.18f, whipOrig, dst, 1.85f, 1.05f, scaleH * 1.12f,
                               LIQUID_SWE_SHAPE_TAI_CHI_WHIP, 1.0f, -1.75f, pwr * 0.95f);
        }
        break;
    }

    default:
        break;
    }
}

/* ── Public Lifecycle & Simulation API ───────────────────────────────────── */

void LiquidSWE_Init(void)
{
    memset(s_sweInstances, 0, sizeof(s_sweInstances));
    memset(s_sweDroplets, 0, sizeof(s_sweDroplets));
    s_sweNextHandle = 1;
}

void LiquidSWE_Unload(void)
{
    memset(s_sweInstances, 0, sizeof(s_sweInstances));
    memset(s_sweDroplets, 0, sizeof(s_sweDroplets));
}

LiquidSWEDesc LiquidSWE_DefaultDesc(void)
{
    LiquidSWEDesc d = {0};
    d.pattern = LIQUID_SWE_PATTERN_TIDAL_WAVE;
    d.source = (Vector3){1.0f, 0.0f, 4.4f};
    d.target = (Vector3){11.0f, 0.0f, 4.4f};
    d.waveHeight = 2.2f;
    d.waveSpeed = 7.6f;
    d.width = 11.5f;
    d.length = 13.5f;
    d.duration = 3.2f;
    d.curlIntensity = 0.90f;
    d.drag = 0.42f;
    d.gravity = 9.81f;
    d.evaporation = 0.18f;
    d.obstacleScale = 0.0f;
    d.skillPower = 1.20f;
    d.renderMode = 0;
    d.emitCrestSpray = false;
    d.emitWetGround = true;
    d.sampleTerrain = true;
    d.deepColor = (Color){18, 118, 124, 240};
    d.crestColor = (Color){36, 189, 173, 232};
    d.foamColor = (Color){216, 248, 246, 245};
    return d;
}

int LiquidSWE_Spawn(const LiquidSWEDesc *desc)
{
    int slot = -1;
    int oldest = 0;
    float maxAge = -1.0f;
    for (int i = 0; i < LIQUID_SWE_MAX_INSTANCES; ++i) {
        if (!s_sweInstances[i].active) {
            slot = i;
            break;
        }
        if (s_sweInstances[i].age > maxAge) {
            maxAge = s_sweInstances[i].age;
            oldest = i;
        }
    }
    if (slot < 0) slot = oldest;

    LiquidSWEDesc c = desc ? *desc : LiquidSWE_DefaultDesc();
    c.width = SWE_Clamp(c.width, 4.0f, 28.0f);
    c.length = SWE_Clamp(c.length, 6.0f, 28.0f);
    c.waveHeight = SWE_Clamp(c.waveHeight, 0.6f, 5.0f);
    c.waveSpeed = SWE_Clamp(c.waveSpeed, 2.5f, 18.0f);
    c.duration = SWE_Clamp(c.duration, 1.0f, 8.0f);
    if (c.gravity <= 0.1f) c.gravity = 9.81f;
    if (c.skillPower <= 0.05f) c.skillPower = 1.0f;

    LiquidSWEInstance *inst = &s_sweInstances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->active = true;
    inst->handle = s_sweNextHandle++;
    if (s_sweNextHandle <= 0) s_sweNextHandle = 1;
    inst->desc = c;
    inst->dx = c.width / (float)(SWE_NX - 1);
    inst->dz = c.length / (float)(SWE_NZ - 1);

    float dx = c.target.x - c.source.x;
    float dz = c.target.z - c.source.z;
    float lenXZ = sqrtf(dx * dx + dz * dz);
    inst->fwd = (lenXZ > 1e-4f) ? (Vector3){dx / lenXZ, 0.0f, dz / lenXZ} : (Vector3){1.0f, 0.0f, 0.0f};
    inst->right = (Vector3){-inst->fwd.z, 0.0f, inst->fwd.x};

    if (SWE_IsATLAPattern(c.pattern)) {
        Vector3 mid = SWE_V3Scale(SWE_V3Add(c.source, c.target), 0.5f);
        inst->origin = SWE_V3Sub(mid, SWE_V3Scale(inst->fwd, c.length * 0.5f));
    } else if (c.pattern == LIQUID_SWE_PATTERN_TIDAL_WAVE) {
        inst->origin = c.source;
    } else {
        inst->origin = SWE_V3Sub(c.target, SWE_V3Scale(inst->fwd, c.length * 0.5f));
    }

    Vector2 targetXZ = {c.target.x, c.target.z};
    for (int iz = 0; iz < SWE_NZ; ++iz) {
        float nz = (float)iz / (float)(SWE_NZ - 1);
        for (int ix = 0; ix < SWE_NX; ++ix) {
            float nx = (float)ix / (float)(SWE_NX - 1);
            Vector3 wp = SWE_CellWorldXZ(inst, ix, iz);
            inst->b[iz][ix] = SWE_SampleGroundY(c.sampleTerrain, wp.x, wp.z, c.source.y);
            inst->bObs[iz][ix] = SWE_ComputeObstacleBathymetry((Vector2){wp.x, wp.z}, targetXZ, c.obstacleScale);

            if (SWE_IsATLAPattern(c.pattern)) {
                inst->h[iz][ix] = 0.0f;
                continue;
            }

            float sideFade = powf(fmaxf(sinf(SWE_Clamp(nx, 0.0f, 1.0f) * PI), 0.0f), 1.45f);
            if (c.pattern == LIQUID_SWE_PATTERN_RADIAL_DELUGE) {
                float cx = (nx - 0.5f) * c.width;
                float cz = (nz - 0.5f) * c.length;
                float rad = sqrtf(cx * cx + cz * cz);
                float ring = expf(-powf((rad - 1.4f) / 1.1f, 2.0f));
                inst->h[iz][ix] = c.waveHeight * ring;
                if (rad > 0.1f && ring > 0.05f) {
                    float vx = (cx / rad) * c.waveSpeed * ring * 0.25f;
                    float vz = (cz / rad) * c.waveSpeed * ring * 0.25f;
                    if (vx > 0.0f) inst->fR[iz][ix] = vx; else inst->fL[iz][ix] = -vx;
                    if (vz > 0.0f) inst->fT[iz][ix] = vz; else inst->fB[iz][ix] = -vz;
                }
            } else if (c.pattern == LIQUID_SWE_PATTERN_TWIN_TIDAL) {
                float zFade = powf(fmaxf(sinf(nz * PI), 0.0f), 0.65f);
                float leftWall = expf(-powf((nx - 0.24f) * 5.2f, 2.0f));
                float rightWall = expf(-powf((nx - 0.76f) * 5.2f, 2.0f));
                float centerBridge = 0.14f * expf(-powf((nx - 0.50f) * 4.5f, 2.0f));
                inst->h[iz][ix] = c.waveHeight * (leftWall + rightWall + centerBridge) * zFade;
                float vx = (leftWall - rightWall) * c.waveSpeed * 0.28f;
                float vz = (leftWall + rightWall) * c.waveSpeed * 0.06f;
                if (vx > 0.0f) inst->fR[iz][ix] = vx; else inst->fL[iz][ix] = -vx;
                if (vz > 0.0f) inst->fT[iz][ix] = vz; else inst->fB[iz][ix] = -vz;
            } else {
                float sideX = (nx - 0.5f) * 2.0f;
                float arcZ = 0.26f - 0.11f * sideX * sideX + 0.015f * sinf(nx * 13.0f);
                float s = (nz - arcZ) * c.length * 0.90f;
                float sech = 1.0f / coshf(SWE_Clamp(s, -6.0f, 6.0f));
                float organic = 1.0f + 0.08f * sinf(nx * 11.0f) + 0.05f * cosf(nx * 19.0f);
                float bore = sech * sech * sideFade * organic;
                inst->h[iz][ix] = c.waveHeight * bore;
                float vz = c.waveSpeed * powf(fmaxf(bore, 0.0f), 0.35f) * 0.25f;
                inst->fT[iz][ix] = vz;
            }
        }
    }

    SWE_ScheduleATLAEmitters(inst);
    SWE_RebuildMeshCache(inst);
    return inst->handle;
}

static LiquidSWEInstance *SWE_FindInstance(int handle)
{
    for (int i = 0; i < LIQUID_SWE_MAX_INSTANCES; ++i) {
        if (s_sweInstances[i].active && s_sweInstances[i].handle == handle) {
            return &s_sweInstances[i];
        }
    }
    return NULL;
}

void LiquidSWE_Kill(int handle)
{
    LiquidSWEInstance *inst = SWE_FindInstance(handle);
    if (inst) inst->active = false;
}

bool LiquidSWE_IsAnyActive(void)
{
    for (int i = 0; i < LIQUID_SWE_MAX_INSTANCES; ++i) {
        if (s_sweInstances[i].active) return true;
    }
    return false;
}

void LiquidSWE_ClearEmitters(int handle)
{
    LiquidSWEInstance *inst = SWE_FindInstance(handle);
    if (inst) inst->emitterCount = 0;
}

void LiquidSWE_AddEmitter(int handle, const LiquidSWEEmitter *emitter)
{
    LiquidSWEInstance *inst = SWE_FindInstance(handle);
    if (!inst || !emitter || inst->emitterCount >= LIQUID_SWE_MAX_EMITTERS) return;
    inst->emitters[inst->emitterCount++] = *emitter;
}

void LiquidSWE_SetManifold(int handle, int slot, const LiquidSWEManifold *manifold)
{
    LiquidSWEInstance *inst = SWE_FindInstance(handle);
    if (!inst || !manifold || slot < 0 || slot >= LIQUID_SWE_MAX_MANIFOLDS) return;
    inst->manifolds[slot] = *manifold;
}

void LiquidSWE_Update(float dt)
{
    if (dt <= 0.0f) return;
    float stepDt = fminf(dt, 0.033f) * 0.5f;

    for (int i = 0; i < LIQUID_SWE_MAX_INSTANCES; ++i) {
        LiquidSWEInstance *inst = &s_sweInstances[i];
        if (!inst->active) continue;

        inst->age += dt;
        if (inst->age >= inst->desc.duration) {
            inst->active = false;
            continue;
        }

        if (SWE_IsATLAPattern(inst->desc.pattern)) {
            SWE_ScheduleATLAEmitters(inst);
        }

        SWE_StepVirtualPipe(inst, stepDt);
        SWE_StepVirtualPipe(inst, stepDt);
        SWE_RebuildMeshCache(inst);

        /* Spawn ballistic spray & falling water droplets */
        if (inst->desc.emitCrestSpray) {
            float fadeOut = 1.0f - SWE_SmoothStep(inst->desc.duration * 0.72f, inst->desc.duration, inst->age);
            if (fadeOut > 0.1f) {
                /* 1. Shed droplets from active 3D ATLA manifolds */
                for (int m = 0; m < LIQUID_SWE_MAX_MANIFOLDS; ++m) {
                    const LiquidSWEManifold *mf = &inst->manifolds[m];
                    if (!mf->active || mf->detachAlpha < 0.25f) continue;
                    float gBase = SWE_SampleGroundY(inst->desc.sampleTerrain, mf->center.x, mf->center.z, inst->origin.y);
                    int emitN = (mf->shape == LIQUID_SWE_SHAPE_TWIN_WHIP || mf->shape == LIQUID_SWE_SHAPE_OCTOPUS_FORM) ? 3 : 2;
                    for (int k = 0; k < emitN; ++k) {
                        float uL = SWE_Rand01() * 2.0f - 1.0f;
                        float uC = SWE_Rand01() * 2.0f - 1.0f;
                        LiquidSWESample sp = LiquidSWE_EvalManifoldPoint(mf, uL, uC, 0.2f);
                        sp.position.y += gBase;
                        float spd = 2.2f + SWE_Rand01() * 3.4f;
                        Vector3 v = {
                            sp.normal.x * spd + (SWE_Rand01() - 0.5f) * 1.8f,
                            sp.normal.y * spd * 0.6f + 1.4f + SWE_Rand01() * 2.4f,
                            sp.normal.z * spd + (SWE_Rand01() - 0.5f) * 1.8f
                        };
                        SWE_SpawnDroplet(i, sp.position, v, 0.028f + SWE_Rand01() * 0.036f, 0.55f + SWE_Rand01() * 0.45f);
                    }
                }

                /* 2. Breaking SWE wave crest spray */
                int crestEmitted = 0;
                for (int attempt = 0; attempt < 18 && crestEmitted < 3; ++attempt) {
                    int ix = 2 + (int)(SWE_Rand01() * (float)(SWE_NX - 4));
                    int iz = 2 + (int)(SWE_Rand01() * (float)(SWE_NZ - 4));
                    if (inst->h[iz][ix] > 0.18f && (inst->foam[iz][ix] > 0.28f || inst->speedGrid[iz][ix] > 0.18f)) {
                        Vector3 p = inst->posGrid[iz][ix];
                        float vx = (inst->fR[iz][ix] - inst->fL[iz][ix]) * 9.0f;
                        float vz = (inst->fT[iz][ix] - inst->fB[iz][ix]) * 9.0f;
                        Vector3 v = {
                            vx + (SWE_Rand01() - 0.5f) * 2.4f,
                            2.2f + SWE_Rand01() * 3.2f,
                            vz + (SWE_Rand01() - 0.5f) * 2.4f
                        };
                        SWE_SpawnDroplet(i, p, v, 0.026f + SWE_Rand01() * 0.034f, 0.50f + SWE_Rand01() * 0.45f);
                        crestEmitted++;
                    }
                }
            }
        }

#ifndef LIQUID_SWE_HEADLESS
        if (inst->peakH > 0.12f) {
            float wy = 0.0f, wd = 0.0f;
            if (MapManager_GetWaterInfoAt(inst->peakPos.x, inst->peakPos.z, &wy, &wd) && wd > 0.01f) {
                Vector3 vel = SWE_V3Scale(inst->fwd, inst->desc.waveSpeed);
                MapManager_SetWaterInteractor((Vector3){inst->peakPos.x, wy, inst->peakPos.z},
                                              vel, fmaxf(inst->desc.width * 0.28f, 1.4f));
                inst->rippleTimer += dt;
                if (inst->rippleTimer >= 0.16f) {
                    inst->rippleTimer = 0.0f;
                    MapManager_AddWaterRipple((Vector3){inst->peakPos.x, wy, inst->peakPos.z}, 5.5f, 1.3f);
                }
            } else if (inst->desc.emitWetGround) {
                inst->wetTimer += dt;
                if (inst->wetTimer >= 0.28f) {
                    inst->wetTimer = 0.0f;
                    Vector3 hitPt = inst->peakPos;
                    Vector3 hitN = {0.0f, 1.0f, 0.0f};
                    MapManager_SampleGroundSurfaceAt(inst->peakPos.x, inst->peakPos.z, &hitPt, &hitN);
                    LiquidImpactEvent ev = {0};
                    ev.hitPoint = hitPt;
                    ev.hitNormal = hitN;
                    ev.impulseDirection = inst->fwd;
                    ev.initialVelocity = SWE_V3Scale(inst->fwd, inst->desc.waveSpeed);
                    ev.force01 = 0.90f;
                    ev.scale = fmaxf(inst->desc.width * 0.25f, 1.8f);
                    ev.bodyColor = inst->desc.deepColor;
                    ev.glowColor = inst->desc.crestColor;
                    ev.softColor = inst->desc.foamColor;
                    ev.externalBody = true;
                    LiquidImpact_SpawnWater(&ev);
                }
            }
        }
#endif
    }

    /* Update shared ballistic droplet pool (gravity + air drag + ground wetting on landing) */
    float drag = expf(-1.2f * dt);
    for (int d = 0; d < SWE_MAX_DROPLETS; ++d) {
        SWEDroplet *dr = &s_sweDroplets[d];
        if (!dr->active) continue;
        dr->age += dt;
        if (dr->age >= dr->maxAge) {
            dr->active = false;
            continue;
        }
        dr->vel.y -= 9.81f * dt;
        dr->vel.x *= drag;
        dr->vel.z *= drag;
        dr->pos.x += dr->vel.x * dt;
        dr->pos.y += dr->vel.y * dt;
        dr->pos.z += dr->vel.z * dt;

        bool sampleTerr = (dr->instIdx >= 0 && dr->instIdx < LIQUID_SWE_MAX_INSTANCES)
                              ? s_sweInstances[dr->instIdx].desc.sampleTerrain
                              : false;
        float fallbackY = (dr->instIdx >= 0 && dr->instIdx < LIQUID_SWE_MAX_INSTANCES)
                              ? s_sweInstances[dr->instIdx].origin.y
                              : 0.0f;
        float cryoF = (dr->instIdx >= 0 && dr->instIdx < LIQUID_SWE_MAX_INSTANCES && s_sweInstances[dr->instIdx].active)
                          ? SWE_ComputeCryoProgress(&s_sweInstances[dr->instIdx])
                          : 0.0f;
        dr->vel.y += 9.81f * 0.78f * cryoF * dt; /* Snow/ice motes float gently as water freezes */
        float gy = SWE_SampleGroundY(sampleTerr, dr->pos.x, dr->pos.z, fallbackY);
        if (dr->pos.y <= gy + 0.02f && dr->vel.y < 0.0f) {
            /* Stamp micro-wetness where droplet hits the ground */
            if (dr->instIdx >= 0 && dr->instIdx < LIQUID_SWE_MAX_INSTANCES && s_sweInstances[dr->instIdx].active) {
                LiquidSWEInstance *inst = &s_sweInstances[dr->instIdx];
                Vector3 rel = {dr->pos.x - inst->origin.x, 0.0f, dr->pos.z - inst->origin.z};
                float lx = rel.x * inst->right.x + rel.z * inst->right.z;
                float lz = rel.x * inst->fwd.x + rel.z * inst->fwd.z;
                int ix = (int)((lx / fmaxf(inst->desc.width, 0.5f) + 0.5f) * (SWE_NX - 1));
                int iz = (int)((lz / fmaxf(inst->desc.length, 0.5f)) * (SWE_NZ - 1));
                if (ix >= 1 && ix < SWE_NX - 1 && iz >= 1 && iz < SWE_NZ - 1) {
                    inst->wet[iz][ix] = fminf(1.0f, inst->wet[iz][ix] + 0.45f);
                }
            }
            dr->active = false;
        }
    }
}

bool LiquidSWE_GetStats(int handle, float *outMaxH, float *outTotalVolume, float *outMaxFoam)
{
    const LiquidSWEInstance *inst = SWE_FindInstance(handle);
    if (!inst) return false;
    float maxH = 0.0f, maxFoam = 0.0f, vol = 0.0f;
    float cellArea = inst->dx * inst->dz;
    for (int iz = 0; iz < SWE_NZ; ++iz) {
        for (int ix = 0; ix < SWE_NX; ++ix) {
            float h = inst->h[iz][ix];
            if (h > maxH) maxH = h;
            if (inst->foam[iz][ix] > maxFoam) maxFoam = inst->foam[iz][ix];
            vol += h * cellArea;
        }
    }
    if (outMaxH) *outMaxH = maxH;
    if (outTotalVolume) *outTotalVolume = vol;
    if (outMaxFoam) *outMaxFoam = maxFoam;
    return true;
}

bool LiquidSWE_GetCell(int handle, int ix, int iz, float *outH, float *outB, float *outFoam, float *outWetness)
{
    const LiquidSWEInstance *inst = SWE_FindInstance(handle);
    if (!inst || ix < 0 || ix >= SWE_NX || iz < 0 || iz >= SWE_NZ) return false;
    if (outH) *outH = inst->h[iz][ix];
    if (outB) *outB = inst->b[iz][ix] + inst->bObs[iz][ix];
    if (outFoam) *outFoam = inst->foam[iz][ix];
    if (outWetness) *outWetness = inst->wet[iz][ix];
    return true;
}

float LiquidSWE_GetCryoProgress(int handle)
{
    const LiquidSWEInstance *inst = SWE_FindInstance(handle);
    return SWE_ComputeCryoProgress(inst);
}

#ifndef LIQUID_SWE_HEADLESS

static float SWE_SampleLocalH(const LiquidSWEInstance *inst, float worldX, float worldZ)
{
    Vector3 rel = {worldX - inst->origin.x, 0.0f, worldZ - inst->origin.z};
    float lx = rel.x * inst->right.x + rel.z * inst->right.z;
    float lz = rel.x * inst->fwd.x + rel.z * inst->fwd.z;
    float nx = lx / fmaxf(inst->desc.width, 0.5f) + 0.5f;
    float nz = lz / fmaxf(inst->desc.length, 0.5f);
    int ix = (int)(nx * (SWE_NX - 1));
    int iz = (int)(nz * (SWE_NZ - 1));
    if (ix < 0 || ix >= SWE_NX || iz < 0 || iz >= SWE_NZ) return 0.18f;
    return inst->h[iz][ix];
}

static void SWE_DrawManifoldStrip(const LiquidSWEInstance *inst, const LiquidSWEManifold *mf,
                                  float fade, float uMin, float uMax)
{
    const int nu = (mf->shape == LIQUID_SWE_SHAPE_TORUS_CHAKRAM) ? 72 : LIQUID_SWE_MANIFOLD_U_STEPS;
    const int nv = LIQUID_SWE_MANIFOLD_V_STEPS;
    unsigned char orbByte = (unsigned char)SWE_Clamp(mf->detachAlpha * 255.0f, 0.0f, 255.0f);
    unsigned char fadeByte = (unsigned char)SWE_Clamp(fade * 255.0f, 0.0f, 255.0f);
    unsigned char spdByte = 160;

    float groundBase = SWE_SampleGroundY(inst->desc.sampleTerrain, mf->center.x, mf->center.z, inst->origin.y);

    rlCheckRenderBatchLimit(nu * nv * 6);
    rlBegin(RL_TRIANGLES);
    for (int iu = 0; iu < nu; ++iu) {
        float u0 = SWE_Lerp(uMin, uMax, (float)iu / (float)nu);
        float u1 = SWE_Lerp(uMin, uMax, (float)(iu + 1) / (float)nu);
        for (int iv = 0; iv < nv; ++iv) {
            float v0 = -1.0f + 2.0f * ((float)iv / (float)nv);
            float v1 = -1.0f + 2.0f * ((float)(iv + 1) / (float)nv);

            float hLoc = SWE_SampleLocalH(inst, mf->center.x, mf->center.z);
            LiquidSWESample s00 = LiquidSWE_EvalManifoldPoint(mf, u0, v0, hLoc);
            LiquidSWESample s10 = LiquidSWE_EvalManifoldPoint(mf, u1, v0, hLoc);
            LiquidSWESample s01 = LiquidSWE_EvalManifoldPoint(mf, u0, v1, hLoc);
            LiquidSWESample s11 = LiquidSWE_EvalManifoldPoint(mf, u1, v1, hLoc);

            const LiquidSWESample *pts[4] = {&s00, &s10, &s01, &s11};
            const int idx[6] = {0, 1, 2, 1, 3, 2};
            for (int t = 0; t < 6; ++t) {
                const LiquidSWESample *sp = pts[idx[t]];
                Vector3 wp = sp->position;
                wp.y += groundBase;
                rlColor4ub(orbByte, spdByte, 0, fadeByte);
                rlTexCoord2f(0.58f, 0.25f);
                rlNormal3f(sp->normal.x, sp->normal.y, sp->normal.z);
                rlVertex3f(wp.x, wp.y, wp.z);
            }
        }
    }
    rlEnd();
}

void LiquidSWE_Draw3D(Camera3D cam)
{
    SWE_InitShaderIfNeeded();
    if (s_sweShader.id == 0) return;

    bool drawSmartShadow = !EnvShadow_IsEnabled();

    for (int i = 0; i < LIQUID_SWE_MAX_INSTANCES; ++i) {
        const LiquidSWEInstance *inst = &s_sweInstances[i];
        if (!inst->active) continue;

        float fade = 1.0f - SWE_Clamp((inst->age - inst->desc.duration * 0.72f) / (inst->desc.duration * 0.28f), 0.0f, 1.0f);
        if (fade <= 0.01f) continue;

        if (drawSmartShadow && inst->peakH > 0.15f) {
            float gy = SWE_SampleGroundY(inst->desc.sampleTerrain, inst->peakPos.x, inst->peakPos.z, inst->origin.y);
            Environment_DrawSmartShadow((Vector3){inst->peakPos.x, gy + 0.03f, inst->peakPos.z},
                                        ENV_SHAPE_SPHERE, inst->desc.width * 0.45f * fade, inst->peakH * 0.9f * fade);
        }

        float deepV[4] = {inst->desc.deepColor.r / 255.0f, inst->desc.deepColor.g / 255.0f, inst->desc.deepColor.b / 255.0f, 1.0f};
        float crestV[4] = {inst->desc.crestColor.r / 255.0f, inst->desc.crestColor.g / 255.0f, inst->desc.crestColor.b / 255.0f, 1.0f};
        float foamV[4] = {inst->desc.foamColor.r / 255.0f, inst->desc.foamColor.g / 255.0f, inst->desc.foamColor.b / 255.0f, 1.0f};

        VFXRenderScope renderScope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlDisableBackfaceCulling();
        BeginShaderMode(s_sweShader);
        SkillManager_BeginShader(s_sweShader);

        if (s_sweFadeLoc >= 0) SetShaderValue(s_sweShader, s_sweFadeLoc, &fade, SHADER_UNIFORM_FLOAT);
        if (s_sweDeepColorLoc >= 0) SetShaderValue(s_sweShader, s_sweDeepColorLoc, deepV, SHADER_UNIFORM_VEC4);
        if (s_sweCrestColorLoc >= 0) SetShaderValue(s_sweShader, s_sweCrestColorLoc, crestV, SHADER_UNIFORM_VEC4);
        if (s_sweFoamColorLoc >= 0) SetShaderValue(s_sweShader, s_sweFoamColorLoc, foamV, SHADER_UNIFORM_VEC4);
        if (s_sweRenderModeLoc >= 0) SetShaderValue(s_sweShader, s_sweRenderModeLoc, &inst->desc.renderMode, SHADER_UNIFORM_INT);
        float cryoAmt = SWE_ComputeCryoProgress(inst);
        if (s_sweCryoAmountLoc >= 0) SetShaderValue(s_sweShader, s_sweCryoAmountLoc, &cryoAmt, SHADER_UNIFORM_FLOAT);

        /* 1. Draw Terrain-Conforming Ground Wetness Pass */
        if (inst->desc.emitWetGround) {
            rlBegin(RL_TRIANGLES);
            for (int iz = 0; iz < SWE_NZ - 1; ++iz) {
                float nz0 = (float)iz / (float)(SWE_NZ - 1);
                float nz1 = (float)(iz + 1) / (float)(SWE_NZ - 1);
                float fz0 = SWE_SmoothStep(0.05f, 0.22f, nz0) * SWE_SmoothStep(0.95f, 0.78f, nz0);
                float fz1 = SWE_SmoothStep(0.05f, 0.22f, nz1) * SWE_SmoothStep(0.95f, 0.78f, nz1);

                for (int ix = 0; ix < SWE_NX - 1; ++ix) {
                    float w00 = inst->wet[iz][ix];
                    float w10 = inst->wet[iz][ix + 1];
                    float w01 = inst->wet[iz + 1][ix];
                    float w11 = inst->wet[iz + 1][ix + 1];
                    if (w00 < 0.04f && w10 < 0.04f && w01 < 0.04f && w11 < 0.04f) continue;

                    float nx0 = (float)ix / (float)(SWE_NX - 1);
                    float nx1 = (float)(ix + 1) / (float)(SWE_NX - 1);
                    float fx0 = SWE_SmoothStep(0.05f, 0.22f, nx0) * SWE_SmoothStep(0.95f, 0.78f, nx0);
                    float fx1 = SWE_SmoothStep(0.05f, 0.22f, nx1) * SWE_SmoothStep(0.95f, 0.78f, nx1);

                    Vector3 p00 = SWE_CellWorldXZ(inst, ix, iz);     p00.y = inst->b[iz][ix] + 0.080f;
                    Vector3 p10 = SWE_CellWorldXZ(inst, ix + 1, iz); p10.y = inst->b[iz][ix + 1] + 0.080f;
                    Vector3 p01 = SWE_CellWorldXZ(inst, ix, iz + 1); p01.y = inst->b[iz + 1][ix] + 0.080f;
                    Vector3 p11 = SWE_CellWorldXZ(inst, ix + 1, iz + 1); p11.y = inst->b[iz + 1][ix + 1] + 0.080f;

                    const Vector3 wPos[4] = {p00, p10, p01, p11};
                    const float ws[4] = {w00 * fx0 * fz0, w10 * fx1 * fz0, w01 * fx0 * fz1, w11 * fx1 * fz1};
                    const int idx[6] = {0, 1, 2, 1, 3, 2};
                    for (int t = 0; t < 6; ++t) {
                        int id = idx[t];
                        rlColor4ub(0, 0, 255, 255);
                        rlTexCoord2f(ws[id], 0.0f);
                        rlNormal3f(0.0f, 1.0f, 0.0f);
                        rlVertex3f(wPos[id].x, wPos[id].y, wPos[id].z);
                    }
                }
            }
            rlEnd();
        }

        /* 2. Draw Ground SWE Sheet */
        float invRefH = 1.0f / fmaxf(inst->desc.waveHeight * 0.78f, 0.35f);
        rlBegin(RL_TRIANGLES);
        for (int iz = 0; iz < SWE_NZ - 1; ++iz) {
            float nz0 = (float)iz / (float)(SWE_NZ - 1);
            float nz1 = (float)(iz + 1) / (float)(SWE_NZ - 1);
            float fz0 = SWE_SmoothStep(0.0f, 0.08f, nz0) * SWE_SmoothStep(1.0f, 0.92f, nz0);
            float fz1 = SWE_SmoothStep(0.0f, 0.08f, nz1) * SWE_SmoothStep(1.0f, 0.92f, nz1);

            for (int ix = 0; ix < SWE_NX - 1; ++ix) {
                float h00 = inst->h[iz][ix];
                float h10 = inst->h[iz][ix + 1];
                float h01 = inst->h[iz + 1][ix];
                float h11 = inst->h[iz + 1][ix + 1];
                if (h00 < 0.002f && h10 < 0.002f && h01 < 0.002f && h11 < 0.002f) continue;

                float nx0 = (float)ix / (float)(SWE_NX - 1);
                float nx1 = (float)(ix + 1) / (float)(SWE_NX - 1);
                float fx0 = SWE_SmoothStep(0.0f, 0.14f, nx0) * SWE_SmoothStep(1.0f, 0.86f, nx0);
                float fx1 = SWE_SmoothStep(0.0f, 0.14f, nx1) * SWE_SmoothStep(1.0f, 0.86f, nx1);

                const Vector3 vtx[4] = {
                    inst->posGrid[iz][ix],
                    inst->posGrid[iz][ix + 1],
                    inst->posGrid[iz + 1][ix],
                    inst->posGrid[iz + 1][ix + 1]
                };
                const Vector3 nrm[4] = {
                    inst->normGrid[iz][ix],
                    inst->normGrid[iz][ix + 1],
                    inst->normGrid[iz + 1][ix],
                    inst->normGrid[iz + 1][ix + 1]
                };
                const float hs[4] = {h00, h10, h01, h11};
                const float edgeF[4] = {fx0 * fz0, fx1 * fz0, fx0 * fz1, fx1 * fz1};
                const float fm[4] = {
                    inst->foam[iz][ix],
                    inst->foam[iz][ix + 1],
                    inst->foam[iz + 1][ix],
                    inst->foam[iz + 1][ix + 1]
                };
                const float spds[4] = {
                    inst->speedGrid[iz][ix],
                    inst->speedGrid[iz][ix + 1],
                    inst->speedGrid[iz + 1][ix],
                    inst->speedGrid[iz + 1][ix + 1]
                };

                const int idx[6] = {0, 1, 2, 1, 3, 2};
                for (int t = 0; t < 6; ++t) {
                    int id = idx[t];
                    float depth01 = SWE_Clamp(hs[id] * invRefH, 0.0f, 1.0f) * edgeF[id];
                    unsigned char spdByte = (unsigned char)SWE_Clamp(spds[id] * 180.0f, 0.0f, 255.0f);
                    rlColor4ub(0, spdByte, 0, 255);
                    rlTexCoord2f(depth01, fm[id] * edgeF[id]);
                    rlNormal3f(nrm[id].x, nrm[id].y, nrm[id].z);
                    rlVertex3f(vtx[id].x, vtx[id].y, vtx[id].z);
                }
            }
        }
        rlEnd();

        /* 2. Draw Detached 3D ATLA Waterbending Manifolds */
        for (int m = 0; m < LIQUID_SWE_MAX_MANIFOLDS; ++m) {
            const LiquidSWEManifold *mf = &inst->manifolds[m];
            if (!mf->active || mf->detachAlpha <= 0.02f) continue;

            if (mf->shape == LIQUID_SWE_SHAPE_OCTOPUS_FORM) {
                /* Draw each of the 8 tentacles as an independent strip so arms never bridge across sectors */
                for (int arm = 0; arm < 8; ++arm) {
                    float u0 = -1.0f + 2.0f * ((float)arm + 0.02f) / 8.0f;
                    float u1 = -1.0f + 2.0f * ((float)arm + 0.98f) / 8.0f;
                    SWE_DrawManifoldStrip(inst, mf, fade, u0, u1);
                }
            } else {
                SWE_DrawManifoldStrip(inst, mf, fade, -1.0f, 1.0f);
            }
        }

        rlDrawRenderBatchActive();
        SkillManager_EndShader();
        EndShaderMode();
        rlEnableBackfaceCulling();
        VFXRender_EndDraw(&renderScope);
    }

    /* 4. Draw Velocity-Stretched Ballistic Spray Droplets (1 shared batch) */
    bool anyDroplets = false;
    for (int d = 0; d < SWE_MAX_DROPLETS; ++d) {
        if (s_sweDroplets[d].active) { anyDroplets = true; break; }
    }
    if (anyDroplets) {
        const LiquidSWEInstance *refInst = NULL;
        for (int i = 0; i < LIQUID_SWE_MAX_INSTANCES; ++i) {
            if (s_sweInstances[i].active) { refInst = &s_sweInstances[i]; break; }
        }
        Color deepC = refInst ? refInst->desc.deepColor : (Color){18, 118, 124, 240};
        Color crestC = refInst ? refInst->desc.crestColor : (Color){36, 189, 173, 232};
        Color foamC = refInst ? refInst->desc.foamColor : (Color){216, 248, 246, 245};
        float deepV[4] = {deepC.r / 255.0f, deepC.g / 255.0f, deepC.b / 255.0f, 1.0f};
        float crestV[4] = {crestC.r / 255.0f, crestC.g / 255.0f, crestC.b / 255.0f, 1.0f};
        float foamV[4] = {foamC.r / 255.0f, foamC.g / 255.0f, foamC.b / 255.0f, 1.0f};
        float oneFade = 1.0f;
        int rMode = refInst ? refInst->desc.renderMode : 0;

        VFXRenderScope dropScope = VFXRender_BeginDraw(VFX_RENDER_PASS_BODY, VFX_SURFACE_ALPHA, false);
        rlDisableBackfaceCulling();
        BeginShaderMode(s_sweShader);
        SkillManager_BeginShader(s_sweShader);

        if (s_sweFadeLoc >= 0) SetShaderValue(s_sweShader, s_sweFadeLoc, &oneFade, SHADER_UNIFORM_FLOAT);
        if (s_sweDeepColorLoc >= 0) SetShaderValue(s_sweShader, s_sweDeepColorLoc, deepV, SHADER_UNIFORM_VEC4);
        if (s_sweCrestColorLoc >= 0) SetShaderValue(s_sweShader, s_sweCrestColorLoc, crestV, SHADER_UNIFORM_VEC4);
        if (s_sweFoamColorLoc >= 0) SetShaderValue(s_sweShader, s_sweFoamColorLoc, foamV, SHADER_UNIFORM_VEC4);
        if (s_sweRenderModeLoc >= 0) SetShaderValue(s_sweShader, s_sweRenderModeLoc, &rMode, SHADER_UNIFORM_INT);
        float refCryo = refInst ? SWE_ComputeCryoProgress(refInst) : 0.0f;
        if (s_sweCryoAmountLoc >= 0) SetShaderValue(s_sweShader, s_sweCryoAmountLoc, &refCryo, SHADER_UNIFORM_FLOAT);

        rlBegin(RL_TRIANGLES);
        for (int d = 0; d < SWE_MAX_DROPLETS; ++d) {
            const SWEDroplet *dr = &s_sweDroplets[d];
            if (!dr->active) continue;

            float life01 = dr->age / fmaxf(dr->maxAge, 0.05f);
            float alpha = (1.0f - SWE_SmoothStep(0.65f, 1.0f, life01)) * SWE_SmoothStep(0.0f, 0.08f, life01);
            if (alpha <= 0.02f) continue;

            Vector3 toCam = SWE_V3Norm(SWE_V3Sub(cam.position, dr->pos));
            float spd = SWE_V3Len(dr->vel);
            Vector3 upAxis = (spd > 0.25f) ? SWE_V3Scale(dr->vel, 1.0f / spd) : (Vector3){0.0f, 1.0f, 0.0f};
            Vector3 sideAxis = SWE_V3Norm(SWE_V3Cross(toCam, upAxis));

            float halfW = dr->radius * (1.0f - 0.35f * life01);
            float halfH = halfW * (1.15f + fminf(spd * 0.22f, 2.2f));

            Vector3 p0 = SWE_V3Add(SWE_V3Sub(dr->pos, SWE_V3Scale(sideAxis, halfW)), SWE_V3Scale(upAxis, -halfH));
            Vector3 p1 = SWE_V3Add(SWE_V3Add(dr->pos, SWE_V3Scale(sideAxis, halfW)), SWE_V3Scale(upAxis, -halfH));
            Vector3 p2 = SWE_V3Add(SWE_V3Sub(dr->pos, SWE_V3Scale(sideAxis, halfW)), SWE_V3Scale(upAxis,  halfH));
            Vector3 p3 = SWE_V3Add(SWE_V3Add(dr->pos, SWE_V3Scale(sideAxis, halfW)), SWE_V3Scale(upAxis,  halfH));

            const Vector3 pts[4] = {p0, p1, p2, p3};
            const float uvs[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
            const int idx[6] = {0, 1, 2, 1, 3, 2};
            unsigned char aByte = (unsigned char)SWE_Clamp(alpha * 255.0f, 0.0f, 255.0f);

            for (int t = 0; t < 6; ++t) {
                int id = idx[t];
                rlColor4ub(0, 0, 128, aByte);
                rlTexCoord2f(uvs[id][0], uvs[id][1]);
                rlNormal3f(toCam.x, toCam.y, toCam.z);
                rlVertex3f(pts[id].x, pts[id].y, pts[id].z);
            }
        }
        rlEnd();

        rlDrawRenderBatchActive();
        SkillManager_EndShader();
        EndShaderMode();
        rlEnableBackfaceCulling();
        VFXRender_EndDraw(&dropScope);
    }
}

void LiquidSWE_DrawShadowPass(void)
{
    /* Cast real directional sun shadows ONLY for airborne 3D ATLA Manifolds
     * (never for ground SWE waves, which absorb light via Beer-Lambert). */
    rlDisableBackfaceCulling();
    for (int i = 0; i < LIQUID_SWE_MAX_INSTANCES; ++i) {
        const LiquidSWEInstance *inst = &s_sweInstances[i];
        if (!inst->active) continue;

        float fade = 1.0f - SWE_Clamp((inst->age - inst->desc.duration * 0.72f) / (inst->desc.duration * 0.28f), 0.0f, 1.0f);
        if (fade <= 0.05f) continue;

        for (int m = 0; m < LIQUID_SWE_MAX_MANIFOLDS; ++m) {
            const LiquidSWEManifold *mf = &inst->manifolds[m];
            if (!mf->active || mf->detachAlpha <= 0.15f) continue;

            if (mf->shape == LIQUID_SWE_SHAPE_OCTOPUS_FORM) {
                for (int arm = 0; arm < 8; ++arm) {
                    float u0 = -1.0f + 2.0f * ((float)arm + 0.02f) / 8.0f;
                    float u1 = -1.0f + 2.0f * ((float)arm + 0.98f) / 8.0f;
                    SWE_DrawManifoldStrip(inst, mf, fade, u0, u1);
                }
            } else {
                SWE_DrawManifoldStrip(inst, mf, fade, -1.0f, 1.0f);
            }
        }
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

#else

void LiquidSWE_Draw3D(Camera3D cam) { (void)cam; }
void LiquidSWE_DrawShadowPass(void) {}

#endif /* LIQUID_SWE_HEADLESS */
