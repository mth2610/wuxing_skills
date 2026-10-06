#ifndef VC_WOOD_BOTANICAL_PROFILE_H
#define VC_WOOD_BOTANICAL_PROFILE_H
#include <math.h>
#include "raylib.h"
#ifndef PI
#define PI 3.14159265358979323846f
#endif
#define BOTANICAL_BLADE_SEGMENTS 12
#define BOTANICAL_BLADE_STRIPS 4
#define BOTANICAL_BLADE_VERTEX_COUNT (BOTANICAL_BLADE_SEGMENTS * BOTANICAL_BLADE_STRIPS * 6)
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

// ── Cosine Vertebra Segment Warp ────────────────────────────────────────────
// Concentrates vertices at base and tip where curvature and taper changes fastest
static inline float Botanical_CosineWarp(float u)
{
    return 0.5f - 0.5f * cosf(u * PI);
}

/* Broadside planform of the actual tessellated blade; excludes petiole,
 * camber and orientation. Cd uses this full projected face area. */
static inline float Botanical_ProfilePlanformArea(const BotanicalProfile *profile,
    float lengthM, const float *samples, int count)
{
    float area = 0, previousT = 0, previousWidth = 0;
    for (int i=0;i<=count;i++) {
        float t = i<count ? samples[i] : 1;
        float halfWidth = Botanical_EvaluateWidth(profile,t)*lengthM;
        area += (previousWidth + halfWidth)*(t-previousT)*lengthM;
        previousT=t; previousWidth=halfWidth;
    }
    return area;
}
/* Rendering and material area use the same bounded longitudinal samples. */
static inline float Botanical_BladeSampleT(int index)
{
    return Botanical_CosineWarp((float)index/BOTANICAL_BLADE_SEGMENTS);
}
/* Local X spans the blade, Y is its normal direction, Z follows its spine.
 * Centerline asymmetry bends the outline without changing broadside area. */
static inline Vector3 Botanical_BladeLocalPoint(const BotanicalProfile *profile,
    float lengthM,float t,float across,float curveRatio,float cupRatio,float asymmetry)
{
    float width=Botanical_EvaluateWidth(profile,t)*lengthM;
    return (Vector3){lengthM*asymmetry*sinf(PI*t)+across*width,
        lengthM*curveRatio*t*t+cupRatio*width*across*across,lengthM*t};
}
static inline Vector3 Botanical_BladeLocalNormal(const BotanicalProfile *profile,
    float lengthM,float t,float across,float curveRatio,float cupRatio,float asymmetry)
{
    float before=fmaxf(0,t-.002f),after=fminf(1,t+.002f);
    Vector3 a=Botanical_BladeLocalPoint(profile,lengthM,before,across,curveRatio,cupRatio,asymmetry);
    Vector3 b=Botanical_BladeLocalPoint(profile,lengthM,after,across,curveRatio,cupRatio,asymmetry);
    Vector3 tangent={b.x-a.x,b.y-a.y,b.z-a.z};
    /* Cross the longitudinal tangent with the width-normalized transverse
     * derivative, which remains finite at the collapsed base and apex. */
    Vector3 normal={-tangent.z*2*cupRatio*across,tangent.z,
        tangent.x*2*cupRatio*across-tangent.y};
    float length=sqrtf(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
    if(length<1e-12f) return (Vector3){0,1,0};
    return (Vector3){normal.x/length,normal.y/length,normal.z/length};
}
static inline float Botanical_BladePlanformArea(const BotanicalProfile *profile,float lengthM)
{
    float samples[BOTANICAL_BLADE_SEGMENTS+1];
    for(int i=0;i<=BOTANICAL_BLADE_SEGMENTS;i++) samples[i]=Botanical_BladeSampleT(i);
    return Botanical_ProfilePlanformArea(profile,lengthM,samples,BOTANICAL_BLADE_SEGMENTS+1);
}
static inline float Botanical_LeafPlanformArea(float sizeM, int shape)
{
    if (shape==2) { /* Maple: sum the six rendered planar lobe triangles. */
        float length=sizeM*.95f, width=length*.52f;
        return length*width*(.60f*.42f + .35f*.60f + 1.15f*.42f-.42f*.60f);
    }
    BotanicalProfile profile=shape==1?Botanical_ProfileWillow():Botanical_ProfileOval();
    return Botanical_BladePlanformArea(&profile,sizeM*(shape==1?1.45f:1.05f));
}
/* Detached blade geometry only: compound blossom profiles keep their authored
 * proportions. Plum is a representative 14 x 9 mm peach-family petal; lotus
 * and orchid are authored species representatives, not universal dimensions. */
static inline BotanicalProfile Botanical_DetachedPetalProfile(int flowerType)
{
    BotanicalProfile profile=flowerType==1?Botanical_ProfilePetalOrchid():
        flowerType==2?Botanical_ProfilePetalPlum():Botanical_ProfilePetalLotus();
    if (flowerType==2) profile.W=9.0f/28.0f;
    return profile;
}
static inline float Botanical_PetalPlanformArea(float sizeM,int flowerType)
{
    BotanicalProfile profile=Botanical_DetachedPetalProfile(flowerType);
    return Botanical_BladePlanformArea(&profile,sizeM*1.25f);
}
static inline float Botanical_PetalDefaultSize(int flowerType)
{
    return flowerType==2?.014f/1.25f:flowerType==1?.030f/1.25f:.060f/1.25f;
}
static inline float Botanical_LeafDefaultSize(int leafShape)
{
    return leafShape==1?.100f/1.45f:leafShape==2?.080f/(.95f*1.05f):.080f/1.05f;
}
static inline float Botanical_ResolvePetalSize(float sizeM,int flowerType)
{
    return !isfinite(sizeM)?NAN:sizeM>0?sizeM:Botanical_PetalDefaultSize(flowerType);
}
static inline float Botanical_ResolveLeafSize(float sizeM,int leafShape)
{
    return !isfinite(sizeM)?NAN:sizeM>0?sizeM:Botanical_LeafDefaultSize(leafShape);
}
/* Artistic aerodynamic flutter envelope, not an aeroelastic material law.
 * Relative flow powers flutter; a co-moving body receives no aerodynamic input.
 * A falling body can draw flutter energy from motion through stationary air. */
static inline float Botanical_FlutterAirWeight(Vector3 relativeVelocityMps)
{
    float relativeSpeedMps = sqrtf(relativeVelocityMps.x*relativeVelocityMps.x +
        relativeVelocityMps.y*relativeVelocityMps.y + relativeVelocityMps.z*relativeVelocityMps.z);
    if (!isfinite(relativeSpeedMps) || relativeSpeedMps <= 0) return 0;
    return fminf(relativeSpeedMps / 3.0f, 1.0f);
}
#endif
