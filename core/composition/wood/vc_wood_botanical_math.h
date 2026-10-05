// ============================================================================
// VC_WOOD_BOTANICAL_MATH.H — Universal AAA Procedural Foliage & Blossom Math
//
// Pure C99, zero heap allocation, real-world meter coordinates.
//
// Key Formulations:
//   1. Profile Width Equation:
//      w(t) = W * (t^a * (1 - t)^b) / w_max,  where t_m = a / (a + b)
//   2. Cosine Warp Vertebra Distribution:
//      t(u) = 0.5 - 0.5 * cos(u * PI),  u = i / SEG
//   3. 4D Spine & Camber Profile:
//      z_c = curl * t^2 * L   (longitudinal spine droop/recurve)
//      z   = z_c + fold * |x| (transverse V-crease keel)
//   4. Staggered Organic Bloom Curve (Phyllotaxis & easeOutBack):
//      delay_i = (1 - k_i) * 0.45
//      b_i     = easeOutBack( clamp((bloom - delay_i) / 0.55, 0, 1) )
//   5. Subsurface Scattering (SSS) Transmission & Wrap Diffuse:
//      wrap  = max(0.0, (dot(N, L) + 0.6) / 1.6)
//      trans = pow(max(0.0, dot(-L, V)), 2.5) * 0.65
// ============================================================================

#ifndef VC_WOOD_BOTANICAL_MATH_H
#define VC_WOOD_BOTANICAL_MATH_H

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/vfx_light.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// ── Profile Definition ───────────────────────────────────────────────────────
typedef struct {
    float a;        // Base taper exponent (0.4-0.7: rounded base; 0.8-1.5: slender base)
    float b;        // Tip taper exponent (0.8-1.0: rounded tip; 1.2-2.0: acuminate tip)
    float W;        // Maximum half-width as ratio of length L
    float fold;     // Transverse V-keel fold factor (camber)
    float curl;     // Longitudinal spine curvature (negative: downward droop; positive: recurved tip)
} BotanicalProfile;

// ── Standard Presets ────────────────────────────────────────────────────────
static inline BotanicalProfile Botanical_ProfileOval(void)
{
    // Oval broadleaf: rounded base, gently pointed apex, natural downward droop
    BotanicalProfile p;
    p.a = 0.55f;
    p.b = 1.15f;
    p.W = 0.36f;
    p.fold = 0.22f;
    p.curl = -0.16f;
    return p;
}

static inline BotanicalProfile Botanical_ProfileWillow(void)
{
    // Willow: slender lanceolate, weeping S-curve arch, pointed drooping apex
    BotanicalProfile p;
    p.a = 0.85f;
    p.b = 1.65f;
    p.W = 0.15f;
    p.fold = 0.16f;
    p.curl = -0.38f;
    return p;
}

static inline BotanicalProfile Botanical_ProfileMaple(void)
{
    // Maple / notched palmate base blade
    BotanicalProfile p;
    p.a = 0.45f;
    p.b = 0.95f;
    p.W = 0.48f;
    p.fold = 0.26f;
    p.curl = -0.12f;
    return p;
}

static inline BotanicalProfile Botanical_ProfilePetalPlum(void)
{
    // Plum / Peach / Cherry blossom: rounded silky petal, concave cup, recurved apex
    BotanicalProfile p;
    p.a = 0.45f;
    p.b = 0.85f;
    p.W = 0.56f;
    p.fold = 0.18f;
    p.curl = 0.24f;
    return p;
}

static inline BotanicalProfile Botanical_ProfilePetalLotus(void)
{
    // Sacred Lotus: spoon-dished elongated cup, sharp elegant apex tip
    BotanicalProfile p;
    p.a = 0.60f;
    p.b = 1.35f;
    p.W = 0.42f;
    p.fold = 0.32f;
    p.curl = 0.18f;
    return p;
}

static inline BotanicalProfile Botanical_ProfilePetalOrchid(void)
{
    // Celestial Orchid: flared lateral wings with wavy margin
    BotanicalProfile p;
    p.a = 0.50f;
    p.b = 1.20f;
    p.W = 0.40f;
    p.fold = 0.24f;
    p.curl = 0.26f;
    return p;
}

// ── Profile Width Evaluator ──────────────────────────────────────────────────
static inline float Botanical_EvaluateWidth(const BotanicalProfile *p, float t)
{
    if (t <= 0.001f || t >= 0.999f) return 0.0f;
    float tm = p->a / (p->a + p->b);
    float wmax = powf(tm, p->a) * powf(1.0f - tm, p->b);
    if (wmax < 1e-5f) return 0.0f;
    return p->W * (powf(t, p->a) * powf(1.0f - t, p->b)) / wmax;
}

// ── EaseOutBack Organic Overshoot Bloom Curve ────────────────────────────────
static inline float Botanical_EaseOutBack(float x)
{
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float x1 = x - 1.0f;
    return 1.0f + c3 * x1 * x1 * x1 + c1 * x1 * x1;
}

// ── Subsurface Scattering (SSS) Transmission & Wrap Diffuse ─────────────────
static inline float Botanical_WrapDiffuse(Vector3 normal, Vector3 sunDir)
{
    float dot = Vector3DotProduct(normal, sunDir);
    float wrap = (dot + 0.60f) / 1.60f;
    return wrap < 0.0f ? 0.0f : wrap;
}

static inline float Botanical_SubsurfaceTransmission(Vector3 sunDir, Vector3 viewDir)
{
    // Backlight penetration: highest when looking towards sun through translucent petal/leaf
    float dot = -Vector3DotProduct(sunDir, viewDir);
    if (dot <= 0.0f) return 0.0f;
    return powf(dot, 2.5f) * 0.65f;
}

// ── Cosine Vertebra Segment Warp ────────────────────────────────────────────
// Concentrates vertices at base and tip where curvature and taper changes fastest
static inline float Botanical_CosineWarp(float u)
{
    return 0.5f - 0.5f * cosf(u * PI);
}

// ── Cubic Bézier Spine Evaluation (From map_props_nature.inl) ───────────────
static inline Vector3 Botanical_EvalCubicBezier(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float u = 1.0f - t;
    float tt = t * t;
    float uu = u * u;
    float uuu = uu * u;
    float ttt = tt * t;
    return (Vector3){
        uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x + ttt * p3.x,
        uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y + ttt * p3.y,
        uuu * p0.z + 3.0f * uu * t * p1.z + 3.0f * u * tt * p2.z + ttt * p3.z
    };
}

static inline Vector3 Botanical_EvalCubicBezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float u = 1.0f - t;
    float a = 3.0f * u * u;
    float b = 6.0f * u * t;
    float c = 3.0f * t * t;
    return (Vector3){
        a * (p1.x - p0.x) + b * (p2.x - p1.x) + c * (p3.x - p2.x),
        a * (p1.y - p0.y) + b * (p2.y - p1.y) + c * (p3.y - p2.y),
        a * (p1.z - p0.z) + b * (p2.z - p1.z) + c * (p3.z - p2.z)
    };
}

// ── 3D Botanical Sigmoid Petal Elevation Cup (From map_props_nature.inl) ────
static inline float Botanical_EvalPetalElevation(float t, float curveAmount)
{
    // Natural botanical 3D sigmoid cup curve: rises from receptacle with initial bowl slope
    float s = sinf(t * 1.5707963f);
    return curveAmount * (s * 1.25f - 0.25f * t * t);
}

// ── Lush Botanical Spear Blade Profile (From map_props_nature.inl) ──────────
static inline float Botanical_EvalBladeProfile(float t)
{
    // Fuller mid-body for lush coverage with needle tip
    return (0.72f + 0.50f * t) * (1.0f - t * t);
}

// ── 3D Parametric Dished Petal Rendering (Adapted from map_props_nature.inl) ──
static inline void Botanical_RenderParametricPetal(
    Vector3 flowerCenter,
    Vector3 stemNormal,
    Vector3 stemBinormal,
    Vector3 stemTangent,
    float radialAngle,
    float spreadAngle,
    float petalLen,
    const BotanicalProfile *prof,
    float curveAmount,
    float twist,
    Color cBase,
    Color cMid,
    Color cTip,
    Color cGlowRim,
    Vector3 sunDir,
    bool isShadowPass,
    unsigned char alphaByte
)
{
    Vector3 radialDir = Vector3Normalize(Vector3Add(
        Vector3Scale(stemBinormal, cosf(radialAngle)),
        Vector3Scale(stemTangent,  sinf(radialAngle))
    ));
    Vector3 petalDir = Vector3Normalize(Vector3Add(
        Vector3Scale(stemNormal, cosf(spreadAngle)),
        Vector3Scale(radialDir,  sinf(spreadAngle))
    ));
    // Perpendicular tangent around the floral ring — always unit length and never degenerates
    Vector3 petalSide = Vector3Normalize(Vector3Subtract(
        Vector3Scale(stemTangent,  cosf(radialAngle)),
        Vector3Scale(stemBinormal, sinf(radialAngle))
    ));
    Vector3 petalNorm = Vector3Normalize(Vector3CrossProduct(petalSide, petalDir));

    // 4 Key sample points along spine: t0=0.05, t1=0.40, t2=0.72, t3=0.88
    const float tSamples[4] = { 0.05f, 0.40f, 0.72f, 0.88f };
    Vector3 spinePt[4];
    Vector3 leftPt[4];
    Vector3 rightPt[4];
    Vector3 normL[4];
    Vector3 normR[4];

    for (int i = 0; i < 4; i++)
    {
        float t = tSamples[i];
        float w = Botanical_EvaluateWidth(prof, t) * petalLen;
        float dy = Botanical_EvalPetalElevation(t, curveAmount);
        float cup = w * (prof->fold > 0.01f ? prof->fold : 0.24f);

        Vector3 side_i = petalSide;
        if (fabsf(twist) > 0.001f) {
            float phi = twist * t;
            side_i = Vector3Normalize(Vector3Add(
                Vector3Scale(petalSide, cosf(phi)),
                Vector3Scale(petalNorm, sinf(phi))
            ));
        }

        Vector3 pMid = Vector3Add(flowerCenter, Vector3Add(
            Vector3Scale(petalDir, petalLen * t),
            Vector3Scale(petalNorm, dy)
        ));
        spinePt[i] = pMid;
        leftPt[i]  = Vector3Add(Vector3Subtract(pMid, Vector3Scale(side_i, w)), Vector3Scale(petalNorm, cup));
        rightPt[i] = Vector3Add(Vector3Add(pMid, Vector3Scale(side_i, w)), Vector3Scale(petalNorm, cup));

        normL[i] = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(petalDir, 0.22f), Vector3Scale(side_i, 0.35f)), Vector3Scale(petalNorm, 0.85f)));
        normR[i] = Vector3Normalize(Vector3Add(Vector3Subtract(Vector3Scale(petalDir, 0.22f), Vector3Scale(side_i, 0.35f)), Vector3Scale(petalNorm, 0.85f)));
    }

    Vector3 apexPt = Vector3Add(flowerCenter, Vector3Add(
        Vector3Scale(petalDir, petalLen),
        Vector3Scale(petalNorm, Botanical_EvalPetalElevation(1.0f, curveAmount))
    ));
    Vector3 normApex = Vector3Normalize(Vector3Add(Vector3Scale(petalDir, 0.55f), Vector3Scale(petalNorm, 0.75f)));

    // Render segments 0->1, 1->2, 2->3
    for (int i = 0; i < 3; i++)
    {
        Vector3 L0 = leftPt[i],      L1 = leftPt[i + 1];
        Vector3 M0 = spinePt[i],     M1 = spinePt[i + 1];
        Vector3 R0 = rightPt[i],     R1 = rightPt[i + 1];

        if (!isShadowPass)
        {
            float dL0 = 0.38f + 0.62f * Botanical_WrapDiffuse(normL[i], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normL[i]), sunDir);
            float dL1 = 0.38f + 0.62f * Botanical_WrapDiffuse(normL[i + 1], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normL[i + 1]), sunDir);
            float dR0 = 0.38f + 0.62f * Botanical_WrapDiffuse(normR[i], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normR[i]), sunDir);
            float dR1 = 0.38f + 0.62f * Botanical_WrapDiffuse(normR[i + 1], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normR[i + 1]), sunDir);

            // Magical spirit unlit emission floor (0.50 floor + 0.50 diffuse) so rim & petals glow in shadow
            float eL0 = 0.50f * dL0 + 0.50f;
            float eL1 = 0.50f * dL1 + 0.50f;
            float eR0 = 0.50f * dR0 + 0.50f;
            float eR1 = 0.50f * dR1 + 0.50f;

            float s0 = tSamples[i];
            float s1 = tSamples[i + 1];

            // Blend colors with spiritual luminescence
            Color colM0 = (Color){
                (unsigned char)(cBase.r * (1.0f - s0) + cMid.r * s0),
                (unsigned char)(cBase.g * (1.0f - s0) + cMid.g * s0),
                (unsigned char)(cBase.b * (1.0f - s0) + cMid.b * s0),
                alphaByte
            };
            Color colM1 = (Color){
                (unsigned char)(cBase.r * (1.0f - s1) + cMid.r * s1),
                (unsigned char)(cBase.g * (1.0f - s1) + cMid.g * s1),
                (unsigned char)(cBase.b * (1.0f - s1) + cMid.b * s1),
                alphaByte
            };
            // Outer margin rim carries glowing spiritual luminescence
            Color colEdge0 = (Color){
                (unsigned char)fminf(255.0f, colM0.r * 0.40f + cGlowRim.r * 0.60f),
                (unsigned char)fminf(255.0f, colM0.g * 0.40f + cGlowRim.g * 0.60f),
                (unsigned char)fminf(255.0f, colM0.b * 0.40f + cGlowRim.b * 0.60f),
                alphaByte
            };
            Color colEdge1 = (Color){
                (unsigned char)fminf(255.0f, colM1.r * 0.40f + cGlowRim.r * 0.60f),
                (unsigned char)fminf(255.0f, colM1.g * 0.40f + cGlowRim.g * 0.60f),
                (unsigned char)fminf(255.0f, colM1.b * 0.40f + cGlowRim.b * 0.60f),
                alphaByte
            };

            // Left quad: (L0, M0, M1) + (L0, M1, L1)
            rlColor4ub((unsigned char)fminf(255.0f, colEdge0.r * eL0), (unsigned char)fminf(255.0f, colEdge0.g * eL0), (unsigned char)fminf(255.0f, colEdge0.b * eL0), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(L0.x, L0.y, L0.z);
            rlColor4ub((unsigned char)(colM0.r * dL0), (unsigned char)(colM0.g * dL0), (unsigned char)(colM0.b * dL0), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)(colM1.r * dL1), (unsigned char)(colM1.g * dL1), (unsigned char)(colM1.b * dL1), alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);

            rlColor4ub((unsigned char)fminf(255.0f, colEdge0.r * eL0), (unsigned char)fminf(255.0f, colEdge0.g * eL0), (unsigned char)fminf(255.0f, colEdge0.b * eL0), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(L0.x, L0.y, L0.z);
            rlColor4ub((unsigned char)(colM1.r * dL1), (unsigned char)(colM1.g * dL1), (unsigned char)(colM1.b * dL1), alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge1.r * eL1), (unsigned char)fminf(255.0f, colEdge1.g * eL1), (unsigned char)fminf(255.0f, colEdge1.b * eL1), alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(L1.x, L1.y, L1.z);

            // Right quad: (M0, R0, R1) + (M0, R1, M1)
            rlColor4ub((unsigned char)(colM0.r * dR0), (unsigned char)(colM0.g * dR0), (unsigned char)(colM0.b * dR0), alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge0.r * eR0), (unsigned char)fminf(255.0f, colEdge0.g * eR0), (unsigned char)fminf(255.0f, colEdge0.b * eR0), alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(R0.x, R0.y, R0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge1.r * eR1), (unsigned char)fminf(255.0f, colEdge1.g * eR1), (unsigned char)fminf(255.0f, colEdge1.b * eR1), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(R1.x, R1.y, R1.z);

            rlColor4ub((unsigned char)(colM0.r * dR0), (unsigned char)(colM0.g * dR0), (unsigned char)(colM0.b * dR0), alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge1.r * eR1), (unsigned char)fminf(255.0f, colEdge1.g * eR1), (unsigned char)fminf(255.0f, colEdge1.b * eR1), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(R1.x, R1.y, R1.z);
            rlColor4ub((unsigned char)(colM1.r * dR1), (unsigned char)(colM1.g * dR1), (unsigned char)(colM1.b * dR1), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);
        }
        else
        {
            rlVertex3f(L0.x, L0.y, L0.z); rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(M1.x, M1.y, M1.z);
            rlVertex3f(L0.x, L0.y, L0.z); rlVertex3f(M1.x, M1.y, M1.z); rlVertex3f(L1.x, L1.y, L1.z);
            rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(R0.x, R0.y, R0.z); rlVertex3f(R1.x, R1.y, R1.z);
            rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(R1.x, R1.y, R1.z); rlVertex3f(M1.x, M1.y, M1.z);
        }
    }

    // Apex fan triangles (L3, M3, Apex) and (M3, R3, Apex)
    Vector3 L3 = leftPt[3], M3 = spinePt[3], R3 = rightPt[3];
    if (!isShadowPass)
    {
        float dTip = 0.45f + 0.55f * Botanical_WrapDiffuse(normApex, sunDir);
        float eTip = 0.50f * dTip + 0.50f;
        Color colTipLit = (Color){
            (unsigned char)fminf(255.0f, cTip.r * eTip),
            (unsigned char)fminf(255.0f, cTip.g * eTip),
            (unsigned char)fminf(255.0f, cTip.b * eTip),
            alphaByte
        };
        rlColor4ub(colTipLit.r, colTipLit.g, colTipLit.b, alphaByte);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(L3.x, L3.y, L3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(M3.x, M3.y, M3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);

        rlColor4ub(colTipLit.r, colTipLit.g, colTipLit.b, alphaByte);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(M3.x, M3.y, M3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(R3.x, R3.y, R3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);
    }
    else
    {
        rlVertex3f(L3.x, L3.y, L3.z); rlVertex3f(M3.x, M3.y, M3.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);
        rlVertex3f(M3.x, M3.y, M3.z); rlVertex3f(R3.x, R3.y, R3.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);
    }
}

// ── 3D Single Parametric Petal (Standalone / Free airborne falling petal) ────
static inline void Botanical_RenderSinglePetalMesh(
    Vector3 root,
    Vector3 forward,
    Vector3 right,
    Vector3 up,
    float petalLen,
    const BotanicalProfile *prof,
    float curveAmount,
    Color cBase,
    Color cMid,
    Color cTip,
    Color cGlowRim,
    Vector3 sunDir,
    bool isShadowPass,
    unsigned char alphaByte
)
{
    const float tSamples[4] = { 0.05f, 0.38f, 0.70f, 0.88f };
    Vector3 spinePt[4];
    Vector3 leftPt[4];
    Vector3 rightPt[4];
    Vector3 normL[4];
    Vector3 normR[4];

    for (int i = 0; i < 4; i++)
    {
        float t = tSamples[i];
        float w = Botanical_EvaluateWidth(prof, t) * petalLen;
        float dy = Botanical_EvalPetalElevation(t, curveAmount);
        float cup = w * (prof->fold > 0.01f ? prof->fold : 0.28f);

        Vector3 pMid = Vector3Add(root, Vector3Add(
            Vector3Scale(forward, petalLen * t),
            Vector3Scale(up, dy)
        ));
        spinePt[i] = pMid;
        leftPt[i]  = Vector3Add(Vector3Subtract(pMid, Vector3Scale(right, w)), Vector3Scale(up, cup));
        rightPt[i] = Vector3Add(Vector3Add(pMid, Vector3Scale(right, w)), Vector3Scale(up, cup));

        normL[i] = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(forward, 0.25f), Vector3Scale(right, -0.35f)), Vector3Scale(up, 0.85f)));
        normR[i] = Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(forward, 0.25f), Vector3Scale(right,  0.35f)), Vector3Scale(up, 0.85f)));
    }

    Vector3 apexPt = Vector3Add(root, Vector3Add(
        Vector3Scale(forward, petalLen),
        Vector3Scale(up, Botanical_EvalPetalElevation(1.0f, curveAmount))
    ));
    Vector3 normApex = Vector3Normalize(Vector3Add(Vector3Scale(forward, 0.55f), Vector3Scale(up, 0.75f)));

    for (int i = 0; i < 3; i++)
    {
        Vector3 L0 = leftPt[i],      L1 = leftPt[i + 1];
        Vector3 M0 = spinePt[i],     M1 = spinePt[i + 1];
        Vector3 R0 = rightPt[i],     R1 = rightPt[i + 1];

        if (!isShadowPass)
        {
            float dL0 = 0.38f + 0.62f * Botanical_WrapDiffuse(normL[i], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normL[i]), sunDir);
            float dL1 = 0.38f + 0.62f * Botanical_WrapDiffuse(normL[i + 1], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normL[i + 1]), sunDir);
            float dR0 = 0.38f + 0.62f * Botanical_WrapDiffuse(normR[i], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normR[i]), sunDir);
            float dR1 = 0.38f + 0.62f * Botanical_WrapDiffuse(normR[i + 1], sunDir) + 0.40f * Botanical_WrapDiffuse(Vector3Negate(normR[i + 1]), sunDir);

            float eL0 = 0.50f * dL0 + 0.50f;
            float eL1 = 0.50f * dL1 + 0.50f;
            float eR0 = 0.50f * dR0 + 0.50f;
            float eR1 = 0.50f * dR1 + 0.50f;

            float s0 = tSamples[i];
            float s1 = tSamples[i + 1];

            Color colM0 = (Color){
                (unsigned char)(cBase.r * (1.0f - s0) + cMid.r * s0),
                (unsigned char)(cBase.g * (1.0f - s0) + cMid.g * s0),
                (unsigned char)(cBase.b * (1.0f - s0) + cMid.b * s0),
                alphaByte
            };
            Color colM1 = (Color){
                (unsigned char)(cBase.r * (1.0f - s1) + cMid.r * s1),
                (unsigned char)(cBase.g * (1.0f - s1) + cMid.g * s1),
                (unsigned char)(cBase.b * (1.0f - s1) + cMid.b * s1),
                alphaByte
            };
            Color colEdge0 = (Color){
                (unsigned char)fminf(255.0f, colM0.r * 0.40f + cGlowRim.r * 0.60f),
                (unsigned char)fminf(255.0f, colM0.g * 0.40f + cGlowRim.g * 0.60f),
                (unsigned char)fminf(255.0f, colM0.b * 0.40f + cGlowRim.b * 0.60f),
                alphaByte
            };
            Color colEdge1 = (Color){
                (unsigned char)fminf(255.0f, colM1.r * 0.40f + cGlowRim.r * 0.60f),
                (unsigned char)fminf(255.0f, colM1.g * 0.40f + cGlowRim.g * 0.60f),
                (unsigned char)fminf(255.0f, colM1.b * 0.40f + cGlowRim.b * 0.60f),
                alphaByte
            };

            // Left quad
            rlColor4ub((unsigned char)fminf(255.0f, colEdge0.r * eL0), (unsigned char)fminf(255.0f, colEdge0.g * eL0), (unsigned char)fminf(255.0f, colEdge0.b * eL0), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(L0.x, L0.y, L0.z);
            rlColor4ub((unsigned char)(colM0.r * dL0), (unsigned char)(colM0.g * dL0), (unsigned char)(colM0.b * dL0), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)(colM1.r * dL1), (unsigned char)(colM1.g * dL1), (unsigned char)(colM1.b * dL1), alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);

            rlColor4ub((unsigned char)fminf(255.0f, colEdge0.r * eL0), (unsigned char)fminf(255.0f, colEdge0.g * eL0), (unsigned char)fminf(255.0f, colEdge0.b * eL0), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(L0.x, L0.y, L0.z);
            rlColor4ub((unsigned char)(colM1.r * dL1), (unsigned char)(colM1.g * dL1), (unsigned char)(colM1.b * dL1), alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge1.r * eL1), (unsigned char)fminf(255.0f, colEdge1.g * eL1), (unsigned char)fminf(255.0f, colEdge1.b * eL1), alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(L1.x, L1.y, L1.z);

            // Right quad
            rlColor4ub((unsigned char)(colM0.r * dR0), (unsigned char)(colM0.g * dR0), (unsigned char)(colM0.b * dR0), alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge0.r * eR0), (unsigned char)fminf(255.0f, colEdge0.g * eR0), (unsigned char)fminf(255.0f, colEdge0.b * eR0), alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(R0.x, R0.y, R0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge1.r * eR1), (unsigned char)fminf(255.0f, colEdge1.g * eR1), (unsigned char)fminf(255.0f, colEdge1.b * eR1), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(R1.x, R1.y, R1.z);

            rlColor4ub((unsigned char)(colM0.r * dR0), (unsigned char)(colM0.g * dR0), (unsigned char)(colM0.b * dR0), alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colEdge1.r * eR1), (unsigned char)fminf(255.0f, colEdge1.g * eR1), (unsigned char)fminf(255.0f, colEdge1.b * eR1), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(R1.x, R1.y, R1.z);
            rlColor4ub((unsigned char)(colM1.r * dR1), (unsigned char)(colM1.g * dR1), (unsigned char)(colM1.b * dR1), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);
        }
        else
        {
            rlVertex3f(L0.x, L0.y, L0.z); rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(M1.x, M1.y, M1.z);
            rlVertex3f(L0.x, L0.y, L0.z); rlVertex3f(M1.x, M1.y, M1.z); rlVertex3f(L1.x, L1.y, L1.z);
            rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(R0.x, R0.y, R0.z); rlVertex3f(R1.x, R1.y, R1.z);
            rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(R1.x, R1.y, R1.z); rlVertex3f(M1.x, M1.y, M1.z);
        }
    }

    // Apex fan
    Vector3 L3 = leftPt[3], M3 = spinePt[3], R3 = rightPt[3];
    if (!isShadowPass)
    {
        float dTip = 0.45f + 0.55f * Botanical_WrapDiffuse(normApex, sunDir);
        float eTip = 0.50f * dTip + 0.50f;
        Color colTipLit = (Color){
            (unsigned char)fminf(255.0f, cTip.r * eTip),
            (unsigned char)fminf(255.0f, cTip.g * eTip),
            (unsigned char)fminf(255.0f, cTip.b * eTip),
            alphaByte
        };
        rlColor4ub(colTipLit.r, colTipLit.g, colTipLit.b, alphaByte);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(L3.x, L3.y, L3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(M3.x, M3.y, M3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);

        rlColor4ub(colTipLit.r, colTipLit.g, colTipLit.b, alphaByte);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(M3.x, M3.y, M3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(R3.x, R3.y, R3.z);
        rlNormal3f(normApex.x, normApex.y, normApex.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);
    }
    else
    {
        rlVertex3f(L3.x, L3.y, L3.z); rlVertex3f(M3.x, M3.y, M3.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);
        rlVertex3f(M3.x, M3.y, M3.z); rlVertex3f(R3.x, R3.y, R3.z); rlVertex3f(apexPt.x, apexPt.y, apexPt.z);
    }
}

// ── 3D Golden Angle Phyllotaxis Receptacle Dome (From map_props_nature.inl) ──
static inline void Botanical_RenderFlowerCenterDome(
    Vector3 head,
    Vector3 stemNormal,
    Vector3 stemBinormal,
    Vector3 stemTangent,
    float cR,
    float cHeight,
    Color centerColor,
    Color apexColor,
    Color stamenColor,
    float time,
    int seed,
    bool isShadowPass,
    unsigned char alphaByte
)
{
    Vector3 apex = Vector3Add(head, Vector3Scale(stemNormal, cHeight * 1.35f));

    // 6-sided 3D dome with golden angle phyllotaxis micro-modulation
    for (int s = 0; s < 6; s++) {
        float a0 = (float)s * 1.04719755f;
        float a1 = (float)(s + 1) * 1.04719755f;
        float goldMod0 = 1.0f + 0.08f * sinf((float)s * 2.39996f);
        float goldMod1 = 1.0f + 0.08f * sinf((float)(s + 1) * 2.39996f);

        Vector3 r0 = Vector3Add(head, Vector3Add(
            Vector3Scale(stemBinormal, cosf(a0) * cR * goldMod0),
            Vector3Scale(stemTangent,  sinf(a0) * cR * goldMod0)
        ));
        Vector3 r1 = Vector3Add(head, Vector3Add(
            Vector3Scale(stemBinormal, cosf(a1) * cR * goldMod1),
            Vector3Scale(stemTangent,  sinf(a1) * cR * goldMod1)
        ));

        if (!isShadowPass) {
            rlColor4ub(apexColor.r, apexColor.g, apexColor.b, alphaByte);
            rlNormal3f(stemNormal.x, stemNormal.y, stemNormal.z);
            rlVertex3f(apex.x, apex.y, apex.z);

            rlColor4ub(centerColor.r, centerColor.g, centerColor.b, alphaByte);
            rlVertex3f(r0.x, r0.y, r0.z);

            rlColor4ub(centerColor.r, centerColor.g, centerColor.b, alphaByte);
            rlVertex3f(r1.x, r1.y, r1.z);
        } else {
            rlVertex3f(apex.x, apex.y, apex.z);
            rlVertex3f(r0.x, r0.y, r0.z);
            rlVertex3f(r1.x, r1.y, r1.z);
        }
    }

    // Incandescent Stamen Filament Ring with Glowing Pollen Pearls
    if (!isShadowPass) {
        float pulse = 1.0f + 0.16f * sinf(time * 4.2f + (float)seed * 1.15f);
        float ringR = cR * 1.45f;
        Color pearlGlow = (Color){255, 255, 210, alphaByte};

        for (int i = 0; i < 8; i++) {
            float a = ((float)i / 8.0f) * 2.0f * PI + 0.25f;
            Vector3 fBase = Vector3Add(head, Vector3Add(
                Vector3Scale(stemBinormal, cosf(a) * cR * 0.85f),
                Vector3Scale(stemTangent,  sinf(a) * cR * 0.85f)
            ));
            Vector3 fTip = Vector3Add(head, Vector3Add(
                Vector3Add(Vector3Scale(stemBinormal, cosf(a) * ringR), Vector3Scale(stemTangent, sinf(a) * ringR)),
                Vector3Scale(stemNormal, cHeight * 1.25f * pulse)
            ));

            // Stamen filament
            rlColor4ub(stamenColor.r, stamenColor.g, stamenColor.b, alphaByte);
            rlNormal3f(stemNormal.x, stemNormal.y, stemNormal.z);
            rlVertex3f(fBase.x, fBase.y, fBase.z);
            rlVertex3f(fTip.x, fTip.y, fTip.z);
            rlVertex3f(fBase.x, fBase.y, fBase.z);

            // Glowing pollen pearl at tip
            float pSize = cR * 0.16f;
            Vector3 pL = Vector3Add(fTip, Vector3Scale(stemBinormal, -pSize));
            Vector3 pR = Vector3Add(fTip, Vector3Scale(stemBinormal,  pSize));
            Vector3 pT = Vector3Add(fTip, Vector3Scale(stemNormal,    pSize * 1.2f));

            rlColor4ub(pearlGlow.r, pearlGlow.g, pearlGlow.b, alphaByte);
            rlVertex3f(fTip.x, fTip.y, fTip.z);
            rlVertex3f(pL.x, pL.y, pL.z);
            rlVertex3f(pT.x, pT.y, pT.z);

            rlVertex3f(fTip.x, fTip.y, fTip.z);
            rlVertex3f(pT.x, pT.y, pT.z);
            rlVertex3f(pR.x, pR.y, pR.z);
        }

        // Spawn dynamic point light from the incandescent stamen core!
        VFXLight_Spawn(apex, stamenColor, cR * 14.0f, 0.05f, VFX_PRIORITY_LOW);
    }
}

// ── 3D Bézier Leaf Blade with Glowing Chi Spine (From map_props_nature.inl) ──
static inline void Botanical_RenderBezierLeafBlade(
    Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
    Vector3 leafNormRef,
    float width,
    float fold,
    Color cBase,
    Color cMid,
    Color cTip,
    Color cVeinGlow,
    Vector3 sunDir,
    bool isShadowPass,
    unsigned char alphaByte
)
{
    #define LEAF_BEZIER_SEGS 4
    Vector3 spinePt[LEAF_BEZIER_SEGS + 1];
    Vector3 leftPt[LEAF_BEZIER_SEGS + 1];
    Vector3 rightPt[LEAF_BEZIER_SEGS + 1];
    Vector3 normL[LEAF_BEZIER_SEGS + 1];
    Vector3 normR[LEAF_BEZIER_SEGS + 1];
    float tVals[LEAF_BEZIER_SEGS + 1];

    for (int i = 0; i <= LEAF_BEZIER_SEGS; i++)
    {
        float u = (float)i / (float)LEAF_BEZIER_SEGS;
        float t = Botanical_CosineWarp(u);
        tVals[i] = t;

        Vector3 C = Botanical_EvalCubicBezier(p0, p1, p2, p3, t);
        Vector3 T = Vector3Normalize(Botanical_EvalCubicBezierTangent(p0, p1, p2, p3, t));

        Vector3 crossS = Vector3CrossProduct(T, leafNormRef);
        float lenS = Vector3Length(crossS);
        Vector3 S = (lenS > 0.001f) ? Vector3Scale(crossS, 1.0f / lenS) : (Vector3){1.0f, 0.0f, 0.0f};
        Vector3 Ngeo = Vector3Normalize(Vector3CrossProduct(S, T));

        float profile = Botanical_EvalBladeProfile(t);
        float halfW = (width * 0.5f) * fmaxf(profile, 0.03f);
        Vector3 foldOffset = Vector3Scale(Ngeo, halfW * (fold > 0.01f ? fold : 0.22f));

        spinePt[i] = C;
        leftPt[i]  = Vector3Add(Vector3Subtract(C, Vector3Scale(S, halfW)), foldOffset);
        rightPt[i] = Vector3Add(Vector3Add(C, Vector3Scale(S, halfW)), foldOffset);

        normL[i] = Vector3Normalize(Vector3Add(Vector3Scale(S, -0.35f), Vector3Scale(Ngeo, 0.85f)));
        normR[i] = Vector3Normalize(Vector3Add(Vector3Scale(S,  0.35f), Vector3Scale(Ngeo, 0.85f)));
    }

    for (int i = 0; i < LEAF_BEZIER_SEGS; i++)
    {
        Vector3 L0 = leftPt[i],  L1 = leftPt[i + 1];
        Vector3 M0 = spinePt[i], M1 = spinePt[i + 1];
        Vector3 R0 = rightPt[i], R1 = rightPt[i + 1];

        if (!isShadowPass)
        {
            float dL = 0.38f + 0.62f * Botanical_WrapDiffuse(normL[i], sunDir) + 0.38f * Botanical_WrapDiffuse(Vector3Negate(normL[i]), sunDir);
            float dR = 0.38f + 0.62f * Botanical_WrapDiffuse(normR[i], sunDir) + 0.38f * Botanical_WrapDiffuse(Vector3Negate(normR[i]), sunDir);
            float eL = 0.50f * dL + 0.50f;
            float eR = 0.50f * dR + 0.50f;

            float t0 = tVals[i];
            float t1 = tVals[i + 1];

            Color colBlade0 = (Color){
                (unsigned char)(cBase.r * (1.0f - t0) + cTip.r * t0),
                (unsigned char)(cBase.g * (1.0f - t0) + cTip.g * t0),
                (unsigned char)(cBase.b * (1.0f - t0) + cTip.b * t0),
                alphaByte
            };
            Color colBlade1 = (Color){
                (unsigned char)(cBase.r * (1.0f - t1) + cTip.r * t1),
                (unsigned char)(cBase.g * (1.0f - t1) + cTip.g * t1),
                (unsigned char)(cBase.b * (1.0f - t1) + cTip.b * t1),
                alphaByte
            };

            // Left quad: (L0, M0, M1) + (L0, M1, L1)
            // Midrib M0 and M1 emit incandescent spirit jade glow (cVeinGlow)
            rlColor4ub((unsigned char)fminf(255.0f, colBlade0.r * eL), (unsigned char)fminf(255.0f, colBlade0.g * eL), (unsigned char)fminf(255.0f, colBlade0.b * eL), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(L0.x, L0.y, L0.z);
            rlColor4ub(cVeinGlow.r, cVeinGlow.g, cVeinGlow.b, alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub(cVeinGlow.r, cVeinGlow.g, cVeinGlow.b, alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);

            rlColor4ub((unsigned char)fminf(255.0f, colBlade0.r * eL), (unsigned char)fminf(255.0f, colBlade0.g * eL), (unsigned char)fminf(255.0f, colBlade0.b * eL), alphaByte);
            rlNormal3f(normL[i].x, normL[i].y, normL[i].z); rlVertex3f(L0.x, L0.y, L0.z);
            rlColor4ub(cVeinGlow.r, cVeinGlow.g, cVeinGlow.b, alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);
            rlColor4ub((unsigned char)fminf(255.0f, colBlade1.r * eL), (unsigned char)fminf(255.0f, colBlade1.g * eL), (unsigned char)fminf(255.0f, colBlade1.b * eL), alphaByte);
            rlNormal3f(normL[i + 1].x, normL[i + 1].y, normL[i + 1].z); rlVertex3f(L1.x, L1.y, L1.z);

            // Right quad: (M0, R0, R1) + (M0, R1, M1)
            rlColor4ub(cVeinGlow.r, cVeinGlow.g, cVeinGlow.b, alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colBlade0.r * eR), (unsigned char)fminf(255.0f, colBlade0.g * eR), (unsigned char)fminf(255.0f, colBlade0.b * eR), alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(R0.x, R0.y, R0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colBlade1.r * eR), (unsigned char)fminf(255.0f, colBlade1.g * eR), (unsigned char)fminf(255.0f, colBlade1.b * eR), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(R1.x, R1.y, R1.z);

            rlColor4ub(cVeinGlow.r, cVeinGlow.g, cVeinGlow.b, alphaByte);
            rlNormal3f(normR[i].x, normR[i].y, normR[i].z); rlVertex3f(M0.x, M0.y, M0.z);
            rlColor4ub((unsigned char)fminf(255.0f, colBlade1.r * eR), (unsigned char)fminf(255.0f, colBlade1.g * eR), (unsigned char)fminf(255.0f, colBlade1.b * eR), alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(R1.x, R1.y, R1.z);
            rlColor4ub(cVeinGlow.r, cVeinGlow.g, cVeinGlow.b, alphaByte);
            rlNormal3f(normR[i + 1].x, normR[i + 1].y, normR[i + 1].z); rlVertex3f(M1.x, M1.y, M1.z);
        }
        else
        {
            rlVertex3f(L0.x, L0.y, L0.z); rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(M1.x, M1.y, M1.z);
            rlVertex3f(L0.x, L0.y, L0.z); rlVertex3f(M1.x, M1.y, M1.z); rlVertex3f(L1.x, L1.y, L1.z);
            rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(R0.x, R0.y, R0.z); rlVertex3f(R1.x, R1.y, R1.z);
            rlVertex3f(M0.x, M0.y, M0.z); rlVertex3f(R1.x, R1.y, R1.z); rlVertex3f(M1.x, M1.y, M1.z);
        }
    }
}

// ── 3D Morphological Calyx Sepals (Lá Đài Bảo Vệ Nụ & Khung Nở) ──────────────
static inline void Botanical_RenderCalyxSepals(
    Vector3 flowerCenter,
    Vector3 stemNormal,
    Vector3 stemBinormal,
    Vector3 stemTangent,
    float sepalLen,
    float sepalWidth,
    float spreadAngle,      // 0.05-0.12 rad: closed bud; 1.15-1.40 rad: reflexed open bloom
    Color sepalBase,
    Color sepalTip,
    Vector3 sunDir,
    bool isShadowPass,
    unsigned char alphaByte
)
{
    const int sepalCount = 4;
    for (int s = 0; s < sepalCount; s++)
    {
        float ang = ((float)s / (float)sepalCount) * 2.0f * PI + 0.3927f; // 45-deg offset
        Vector3 radDir = Vector3Normalize(Vector3Add(
            Vector3Scale(stemBinormal, cosf(ang)),
            Vector3Scale(stemTangent,  sinf(ang))
        ));
        Vector3 sepalDir = Vector3Normalize(Vector3Add(
            Vector3Scale(stemNormal, cosf(spreadAngle)),
            Vector3Scale(radDir,  sinf(spreadAngle))
        ));
        Vector3 sepalSide = Vector3Normalize(Vector3Subtract(
            Vector3Scale(stemTangent,  cosf(ang)),
            Vector3Scale(stemBinormal, sinf(ang))
        ));
        Vector3 sepalNorm = Vector3Normalize(Vector3CrossProduct(sepalSide, sepalDir));

        Vector3 vBase = flowerCenter;
        Vector3 vMid  = Vector3Add(flowerCenter, Vector3Scale(sepalDir, sepalLen * 0.50f));
        Vector3 vL    = Vector3Add(vMid, Vector3Scale(sepalSide, -sepalWidth * 0.5f));
        Vector3 vR    = Vector3Add(vMid, Vector3Scale(sepalSide,  sepalWidth * 0.5f));
        Vector3 vTip  = Vector3Add(flowerCenter, Vector3Scale(sepalDir, sepalLen));

        if (!isShadowPass)
        {
            float d = 0.35f + 0.65f * Botanical_WrapDiffuse(sepalNorm, sunDir);
            float e = 0.50f * d + 0.50f;
            Color cB = (Color){ (unsigned char)(sepalBase.r * d), (unsigned char)(sepalBase.g * d), (unsigned char)(sepalBase.b * d), alphaByte };
            Color cT = (Color){ (unsigned char)(sepalTip.r * e), (unsigned char)(sepalTip.g * e), (unsigned char)(sepalTip.b * e), alphaByte };

            rlColor4ub(cB.r, cB.g, cB.b, alphaByte);
            rlNormal3f(sepalNorm.x, sepalNorm.y, sepalNorm.z);
            rlVertex3f(vBase.x, vBase.y, vBase.z);
            rlColor4ub(cT.r, cT.g, cT.b, alphaByte);
            rlVertex3f(vL.x, vL.y, vL.z);
            rlVertex3f(vR.x, vR.y, vR.z);

            rlColor4ub(cT.r, cT.g, cT.b, alphaByte);
            rlNormal3f(sepalNorm.x, sepalNorm.y, sepalNorm.z);
            rlVertex3f(vL.x, vL.y, vL.z);
            rlVertex3f(vTip.x, vTip.y, vTip.z);
            rlVertex3f(vR.x, vR.y, vR.z);
        }
        else
        {
            rlVertex3f(vBase.x, vBase.y, vBase.z); rlVertex3f(vL.x, vL.y, vL.z); rlVertex3f(vR.x, vR.y, vR.z);
            rlVertex3f(vL.x, vL.y, vL.z); rlVertex3f(vTip.x, vTip.y, vTip.z); rlVertex3f(vR.x, vR.y, vR.z);
        }
    }
}

#endif // VC_WOOD_BOTANICAL_MATH_H

