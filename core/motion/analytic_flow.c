#include "core/motion/analytic_flow.h"

#include "raymath.h"
#include <math.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float s_seed1[ANALYTIC_FLOW_MAX_SAMPLES];
static float s_seed2[ANALYTIC_FLOW_MAX_SAMPLES];
static float s_seed3[ANALYTIC_FLOW_MAX_SAMPLES];
static bool s_seedsReady = false;

static float Analytic_Halton(int index, int base)
{
    float f = 1.0f;
    float r = 0.0f;
    int i = index + 1;
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

static float Analytic_Smoothstep(float e0, float e1, float x)
{
    float t = Clamp((x - e0) / fmaxf(e1 - e0, 1e-5f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static void Analytic_NormalizeDesc(const AnalyticFlowDesc *in,
                                   AnalyticFlowDesc *out,
                                   Vector3 *outDir,
                                   Vector3 *outRight,
                                   float *outDist)
{
    *out = *in;
    if (out->radius <= 0.05f) out->radius = 0.42f;
    if (out->speed <= 0.5f) out->speed = 14.0f;
    if (out->arcHeight < 0.0f) out->arcHeight = 1.6f;
    if (out->sampleCount <= 0) {
        if (out->pattern == ANALYTIC_FLOW_ORB) {
            out->sampleCount = (int)Clamp(96.0f + out->radius * 180.0f, 128.0f, 220.0f);
        } else if (out->pattern == ANALYTIC_FLOW_DRAGON_SURGE) {
            float d = Vector3Distance(out->origin, out->target);
            out->sampleCount = (int)Clamp(140.0f + d * 10.0f, 160.0f, 260.0f);
        } else if (out->pattern == ANALYTIC_FLOW_BLADE_SLASH) {
            out->sampleCount = 180;
        } else {
            out->sampleCount = 200;
        }
    }
    if (out->sampleCount > ANALYTIC_FLOW_MAX_SAMPLES) out->sampleCount = ANALYTIC_FLOW_MAX_SAMPLES;

    Vector3 delta = Vector3Subtract(out->target, out->origin);
    Vector3 horiz = {delta.x, 0.0f, delta.z};
    float distXZ = Vector3Length(horiz);
    if (distXZ < 0.1f) {
        horiz = (Vector3){1.0f, 0.0f, 0.0f};
        distXZ = 0.1f;
    }
    *outDist = fmaxf(Vector3Length(delta), 0.5f);
    *outDir = Vector3Scale(horiz, 1.0f / distXZ);
    *outRight = (Vector3){-outDir->z, 0.0f, outDir->x};
}

void AnalyticFlow_InitSeeds(void)
{
    if (s_seedsReady) return;
    for (int i = 0; i < ANALYTIC_FLOW_MAX_SAMPLES; ++i) {
        s_seed1[i] = Analytic_Halton(i, 2);
        s_seed2[i] = Analytic_Halton(i, 3);
        s_seed3[i] = Analytic_Halton(i, 5);
    }
    s_seedsReady = true;
}

float AnalyticFlow_HaltonSeed(int index, int dim)
{
    AnalyticFlow_InitSeeds();
    if (index < 0) index = 0;
    if (index >= ANALYTIC_FLOW_MAX_SAMPLES) index %= ANALYTIC_FLOW_MAX_SAMPLES;
    if (dim == 1) return s_seed2[index];
    if (dim == 2) return s_seed3[index];
    return s_seed1[index];
}

float AnalyticFlow_TravelTime(const AnalyticFlowDesc *desc)
{
    if (!desc) return 0.35f;
    float speed = desc->speed > 0.5f ? desc->speed : 14.0f;
    float dist = fmaxf(Vector3Distance(desc->origin, desc->target), 0.5f);
    return fmaxf(dist / speed, 0.35f);
}

float AnalyticFlow_DefaultDuration(const AnalyticFlowDesc *desc)
{
    if (!desc) return 1.80f;
    if (desc->duration > 0.1f) return desc->duration;
    float travel = AnalyticFlow_TravelTime(desc);
    switch (desc->pattern) {
    case ANALYTIC_FLOW_BLADE_SLASH:  return 2.65f;
    case ANALYTIC_FLOW_DRAGON_SURGE: return travel + 1.65f;
    case ANALYTIC_FLOW_VORTEX_SURGE: return 2.40f;
    case ANALYTIC_FLOW_ORB:
    default:                         return travel + 1.45f;
    }
}

static void Analytic_EvalOrb(const AnalyticFlowDesc *d,
                             float travelTime,
                             float age,
                             int count,
                             AnalyticFlowSample *out)
{
    if (age < 0.0f || age >= travelTime) {
        for (int i = 0; i < count; ++i) out[i].active = false;
        return;
    }

    float u = Clamp(age / travelTime, 0.0f, 1.0f);
    Vector3 center = Vector3Lerp(d->origin, d->target, u);
    center.y += sinf(u * PI) * d->arcHeight;

    float uNext = Clamp(u + 0.04f, 0.0f, 1.0f);
    Vector3 nextCenter = Vector3Lerp(d->origin, d->target, uNext);
    nextCenter.y += sinf(uNext * PI) * d->arcHeight;

    Vector3 velCenter = Vector3Scale(Vector3Subtract(nextCenter, center), 1.0f / fmaxf(travelTime * 0.04f, 1e-4f));
    Vector3 fwd = Vector3Normalize(Vector3Subtract(nextCenter, center));
    Vector3 up = {0.0f, 1.0f, 0.0f};
    Vector3 side = Vector3CrossProduct(fwd, up);
    if (Vector3LengthSqr(side) < 1e-4f) side = (Vector3){1.0f, 0.0f, 0.0f};
    side = Vector3Normalize(side);
    Vector3 binorm = Vector3CrossProduct(side, fwd);

    float R = d->radius;
    for (int i = 0; i < count; ++i) {
        float s1 = s_seed1[i], s2 = s_seed2[i], s3 = s_seed3[i];
        float shell = 0.25f + 0.75f * cbrtf(s3);
        float omega = 4.5f + 2.5f * (1.0f - s3);
        float theta = s1 * 2.0f * PI + age * omega;
        float phi = acosf(1.0f - 2.0f * s2);

        float axial = cosf(phi) * R * 1.15f * shell - (1.0f - shell) * R * 0.35f;
        float radial = sinf(phi) * R * 0.88f * shell;
        float ct = cosf(theta), st = sinf(theta);

        Vector3 p = center;
        p = Vector3Add(p, Vector3Scale(fwd, axial));
        p = Vector3Add(p, Vector3Scale(side, ct * radial));
        p = Vector3Add(p, Vector3Scale(binorm, st * radial));

        Vector3 swirlVel = Vector3Add(
            Vector3Scale(side, -st * radial * omega),
            Vector3Scale(binorm, ct * radial * omega));

        out[i].position = p;
        out[i].velocity = Vector3Add(velCenter, swirlVel);
        out[i].radius = R * (0.18f + 0.14f * (1.0f - s3));
        out[i].alpha = 1.0f;
        out[i].u = u;
        out[i].active = true;
    }
}

static void Analytic_EvalDragonSurge(const AnalyticFlowDesc *d,
                                     Vector3 right,
                                     float travelTime,
                                     float duration,
                                     float age,
                                     int count,
                                     AnalyticFlowSample *out)
{
    int bodyCount = (int)((float)count * 0.68f);
    float headU = Clamp(age / travelTime, 0.0f, 1.18f);
    float fadeOut = 1.0f - Analytic_Smoothstep(travelTime * 0.92f, duration, age);

    for (int i = 0; i < count; ++i) out[i].active = false;
    if (fadeOut <= 0.02f || age < 0.0f) return;

    Vector3 baseVel = Vector3Scale(Vector3Subtract(d->target, d->origin), 1.0f / fmaxf(travelTime, 1e-4f));
    for (int i = 0; i < bodyCount; ++i) {
        float s1 = s_seed1[i], s2 = s_seed2[i], s3 = s_seed3[i];
        float u = s1;
        float sParam = headU - (1.0f - u) * 0.52f;
        if (sParam <= 0.0f || sParam > 1.0f) continue;

        float slitherPhase = sParam * 14.0f - age * 6.5f + s2 * 0.4f;
        float slitherEnv = sinf(sParam * PI);
        float helixAng = s3 * 2.0f * PI + sParam * 10.0f - age * 5.0f;
        float tubeRadial = d->radius * (0.28f + 0.35f * sinf(u * PI));
        float headSwell = (u > 0.82f) ? sinf(((u - 0.82f) / 0.18f) * PI) * d->radius * 0.72f : 0.0f;

        float lateral = cosf(slitherPhase) * (d->radius * 1.15f) * slitherEnv
                      + cosf(helixAng) * (tubeRadial + headSwell);
        float vertOff = sinf(slitherPhase) * (d->radius * 0.55f) * slitherEnv
                      + sinf(helixAng) * (tubeRadial + headSwell * 0.6f);

        Vector3 p = Vector3Lerp(d->origin, d->target, sParam);
        p.y += sinf(sParam * PI) * d->arcHeight + vertOff;
        p = Vector3Add(p, Vector3Scale(right, lateral));

        float r = (d->radius * 0.34f + 0.10f * s2 + headSwell * 0.28f) * fadeOut;
        if (r <= 0.015f) continue;

        out[i].position = p;
        out[i].velocity = baseVel;
        out[i].radius = r;
        out[i].alpha = fadeOut;
        out[i].u = u;
        out[i].active = true;
    }
}

static void Analytic_EvalBladeSlash(const AnalyticFlowDesc *d,
                                    Vector3 dir,
                                    Vector3 right,
                                    float dist,
                                    float age,
                                    int count,
                                    AnalyticFlowSample *out)
{
    int groupSize = count / 3;
    float baseRadius = fmaxf(dist * 0.42f, 1.8f);
    Vector3 center = d->origin;

    for (int i = 0; i < count; ++i) {
        out[i].active = false;
        float s1 = s_seed1[i], s2 = s_seed2[i], s3 = s_seed3[i];
        int slashIdx = i / (groupSize > 0 ? groupSize : 1);
        if (slashIdx > 2) slashIdx = 2;

        float startT = (float)slashIdx * 0.62f;
        float localT = age - startT;
        if (localT <= 0.0f || localT > 1.35f) continue;

        float swingP = fminf(1.0f, localT / 0.72f);
        float arcSpan = PI * 1.40f;
        float headAng = -arcSpan * 0.5f + swingP * arcSpan * 1.25f;
        float bladeU = s1;
        float theta = headAng - (1.0f - bladeU) * 1.75f;

        float crescentTaper = sinf(bladeU * PI);
        float rRing = baseRadius + (s2 - 0.5f) * (d->radius * 1.4f) * crescentTaper
                    + fmaxf(0.0f, localT - 0.38f) * (1.4f + s3 * 1.8f);
        float tilt = (slashIdx == 0) ? 0.22f : ((slashIdx == 1) ? -0.28f : 0.08f);

        float lx = cosf(theta) * rRing;
        float lz = sinf(theta) * rRing;
        float ly = 0.45f + (s3 - 0.5f) * 0.35f * crescentTaper + lz * tilt;

        Vector3 p = {
            center.x + right.x * lx + dir.x * lz,
            center.y + ly,
            center.z + right.z * lx + dir.z * lz
        };
        float fade = 1.0f - Analytic_Smoothstep(0.85f, 1.35f, localT);
        float rad = d->radius * (0.28f + 0.22f * crescentTaper) * fade;
        if (rad <= 0.015f) continue;

        float angRate = (localT < 0.72f) ? (arcSpan * 1.25f / 0.72f) : 0.0f;
        float vx = -sinf(theta) * rRing * angRate;
        float vz =  cosf(theta) * rRing * angRate;

        out[i].position = p;
        out[i].velocity = (Vector3){
            right.x * vx + dir.x * vz,
            vz * tilt,
            right.z * vx + dir.z * vz
        };
        out[i].radius = rad;
        out[i].alpha = fade;
        out[i].u = bladeU;
        out[i].active = true;
    }
}

static void Analytic_EvalVortexSurge(const AnalyticFlowDesc *d,
                                     float duration,
                                     float age,
                                     int count,
                                     AnalyticFlowSample *out)
{
    for (int i = 0; i < count; ++i) out[i].active = false;
    float uLife = Clamp(age / fmaxf(duration, 0.1f), 0.0f, 1.0f);
    float fade = (1.0f - Analytic_Smoothstep(0.72f, 1.0f, uLife)) * Analytic_Smoothstep(0.0f, 0.08f, uLife);
    if (fade <= 0.01f) return;

    float maxR = fmaxf(d->radius * 2.8f, 1.4f);
    Vector3 center = d->target;

    for (int i = 0; i < count; ++i) {
        float s1 = s_seed1[i], s2 = s_seed2[i], s3 = s_seed3[i];
        float h01 = s1;
        float omega = 7.5f - h01 * 2.5f;
        float ang = s2 * 2.0f * PI + age * omega + h01 * 9.0f;
        float expand = uLife > 0.65f ? ((uLife - 0.65f) / 0.35f) * 1.4f : 0.0f;
        float rad = (0.35f + 0.65f * h01 + expand) * maxR * (0.75f + 0.25f * sinf(s3 * 6.28f + age * 3.0f));
        float py = center.y + 0.15f + h01 * d->arcHeight * (1.0f - expand * 0.4f);

        float r = d->radius * (0.24f + 0.16f * s3) * fade;
        if (r <= 0.015f) continue;

        out[i].position = (Vector3){center.x + cosf(ang) * rad, py, center.z + sinf(ang) * rad};
        out[i].velocity = (Vector3){-sinf(ang) * rad * omega, 0.6f, cosf(ang) * rad * omega};
        out[i].radius = r;
        out[i].alpha = fade;
        out[i].u = h01;
        out[i].active = true;
    }
}

int AnalyticFlow_Evaluate(const AnalyticFlowDesc *desc,
                          float age,
                          AnalyticFlowSample *outSamples,
                          int maxSamples)
{
    if (!desc || !outSamples || maxSamples <= 0) return 0;
    AnalyticFlow_InitSeeds();

    AnalyticFlowDesc d;
    Vector3 dir, right;
    float dist;
    Analytic_NormalizeDesc(desc, &d, &dir, &right, &dist);

    int count = d.sampleCount < maxSamples ? d.sampleCount : maxSamples;
    float travelTime = AnalyticFlow_TravelTime(&d);
    float duration = AnalyticFlow_DefaultDuration(&d);

    switch (d.pattern) {
    case ANALYTIC_FLOW_DRAGON_SURGE:
        Analytic_EvalDragonSurge(&d, right, travelTime, duration, age, count, outSamples);
        break;
    case ANALYTIC_FLOW_BLADE_SLASH:
        Analytic_EvalBladeSlash(&d, dir, right, dist, age, count, outSamples);
        break;
    case ANALYTIC_FLOW_VORTEX_SURGE:
        Analytic_EvalVortexSurge(&d, duration, age, count, outSamples);
        break;
    case ANALYTIC_FLOW_ORB:
    default:
        Analytic_EvalOrb(&d, travelTime, age, count, outSamples);
        break;
    }
    return count;
}

int AnalyticFlow_EvaluateCrownSplash(Vector3 center,
                                     float splashAge,
                                     float scale,
                                     int sampleCount,
                                     AnalyticFlowSample *outSamples,
                                     int maxSamples)
{
    if (!outSamples || maxSamples <= 0 || splashAge <= 0.0f) return 0;
    AnalyticFlow_InitSeeds();

    int count = sampleCount > 0 ? sampleCount : 160;
    if (count > ANALYTIC_FLOW_MAX_SAMPLES) count = ANALYTIC_FLOW_MAX_SAMPLES;
    if (count > maxSamples) count = maxSamples;

    const float g = 9.81f;
    for (int i = 0; i < count; ++i) {
        outSamples[i].active = false;
        float s1 = s_seed1[i], s2 = s_seed2[i], s3 = s_seed3[i];
        float localT = splashAge - s2 * 0.22f;
        if (localT <= 0.0f || localT > 1.45f) continue;

        float azim = s1 * 2.0f * PI + sinf(s3 * 12.0f) * 0.14f;
        float crownSpike = 0.72f + 0.38f * sinf(azim * 10.0f);
        float vRad = (1.6f + s3 * 2.2f) * crownSpike * scale;
        float vUp  = (3.4f + s2 * 2.5f) * crownSpike * scale;

        float ca = cosf(azim), sa = sinf(azim);
        float px = center.x + ca * (0.18f * scale + vRad * localT);
        float pz = center.z + sa * (0.18f * scale + vRad * localT);
        float py = center.y + 0.08f + vUp * localT - 0.5f * g * localT * localT;

        float lifeFade = 1.0f - Analytic_Smoothstep(0.85f, 1.40f, localT);
        float r = (0.085f + 0.055f * s3) * scale * lifeFade;
        if (r <= 0.015f) continue;

        outSamples[i].position = (Vector3){px, py, pz};
        outSamples[i].velocity = (Vector3){ca * vRad, vUp - g * localT, sa * vRad};
        outSamples[i].radius = r;
        outSamples[i].alpha = lifeFade;
        outSamples[i].u = Clamp(localT / 1.45f, 0.0f, 1.0f);
        outSamples[i].active = true;
    }
    return count;
}
