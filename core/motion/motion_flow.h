#ifndef CORE_MOTION_FLOW_H
#define CORE_MOTION_FLOW_H
#include "core/force_field.h"
#include "core/motion/motion_path.h"

/* ============================================================
 * INLINED PAIRED PERLIN 3D FOR CURL NOISE
 * Bit-identical to Noise_Perlin3D in force_field.c, inlined
 * directly into MotionFlow and PhysicalField curl evaluations.
 * ============================================================ */
#ifndef CORE_NOISE_PERLIN_PAIR_DEFINED
#define CORE_NOISE_PERLIN_PAIR_DEFINED

/* clang-format off */
#define MF_PERM_ROW \
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225, \
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148, \
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32, \
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175, \
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122, \
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54, \
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169, \
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64, \
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212, \
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213, \
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9, \
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104, \
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241, \
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157, \
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93, \
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
static const unsigned char s_mfPerm[512] = { MF_PERM_ROW, MF_PERM_ROW };
#undef MF_PERM_ROW
/* clang-format on */

static const float s_mfGradTable[16][3] = {
    {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, -1.0f, 1.0f}, {0.0f, 1.0f, -1.0f}, {0.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, -1.0f}};

static inline int MotionFlow_FastFloor(float x)
{
  int xi = (int)x;
  return x < (float)xi ? xi - 1 : xi;
}

static inline float MotionFlow_Fade(float t)
{
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float MotionFlow_Lerp(float a, float b, float t)
{
  return a + t * (b - a);
}

static inline float MotionFlow_Grad(int hash, float x, float y, float z)
{
  const float *g = s_mfGradTable[hash & 15];
  return g[0] * x + g[1] * y + g[2] * z;
}

static inline void Noise_Perlin3D_PairX(float xp, float xm, float y, float z,
                                        float *outP, float *outM)
{
  int iy = MotionFlow_FastFloor(y);
  int iz = MotionFlow_FastFloor(z);
  int Y = iy & 255;
  int Z = iz & 255;
  float yf = y - (float)iy;
  float zf = z - (float)iz;
  float v = MotionFlow_Fade(yf);
  float w = MotionFlow_Fade(zf);
  float y1 = yf - 1.0f;
  float z1 = zf - 1.0f;

  int ixp = MotionFlow_FastFloor(xp);
  int ixm = MotionFlow_FastFloor(xm);
  float xfp = xp - (float)ixp;
  float xfm = xm - (float)ixm;
  float up = MotionFlow_Fade(xfp);
  float um = MotionFlow_Fade(xfm);
  float x1p = xfp - 1.0f;
  float x1m = xfm - 1.0f;

  int Xp = ixp & 255;
  int A = s_mfPerm[Xp] + Y;
  int AA = s_mfPerm[A] + Z;
  int AB = s_mfPerm[A + 1] + Z;
  int B = s_mfPerm[Xp + 1] + Y;
  int BA = s_mfPerm[B] + Z;
  int BB = s_mfPerm[B + 1] + Z;

  int hAA0 = s_mfPerm[AA], hBA0 = s_mfPerm[BA];
  int hAB0 = s_mfPerm[AB], hBB0 = s_mfPerm[BB];
  int hAA1 = s_mfPerm[AA + 1], hBA1 = s_mfPerm[BA + 1];
  int hAB1 = s_mfPerm[AB + 1], hBB1 = s_mfPerm[BB + 1];

  float l00p = MotionFlow_Lerp(MotionFlow_Grad(hAA0, xfp, yf, zf),
                               MotionFlow_Grad(hBA0, x1p, yf, zf), up);
  float l10p = MotionFlow_Lerp(MotionFlow_Grad(hAB0, xfp, y1, zf),
                               MotionFlow_Grad(hBB0, x1p, y1, zf), up);
  float l01p = MotionFlow_Lerp(MotionFlow_Grad(hAA1, xfp, yf, z1),
                               MotionFlow_Grad(hBA1, x1p, yf, z1), up);
  float l11p = MotionFlow_Lerp(MotionFlow_Grad(hAB1, xfp, y1, z1),
                               MotionFlow_Grad(hBB1, x1p, y1, z1), up);
  *outP = MotionFlow_Lerp(MotionFlow_Lerp(l00p, l10p, v),
                          MotionFlow_Lerp(l01p, l11p, v), w);

  if (ixm != ixp)
  {
    int Xm = ixm & 255;
    int Am = s_mfPerm[Xm] + Y;
    int AAm = s_mfPerm[Am] + Z;
    int ABm = s_mfPerm[Am + 1] + Z;
    int Bm = s_mfPerm[Xm + 1] + Y;
    int BAm = s_mfPerm[Bm] + Z;
    int BBm = s_mfPerm[Bm + 1] + Z;
    hAA0 = s_mfPerm[AAm];
    hBA0 = s_mfPerm[BAm];
    hAB0 = s_mfPerm[ABm];
    hBB0 = s_mfPerm[BBm];
    hAA1 = s_mfPerm[AAm + 1];
    hBA1 = s_mfPerm[BAm + 1];
    hAB1 = s_mfPerm[ABm + 1];
    hBB1 = s_mfPerm[BBm + 1];
  }

  float l00m = MotionFlow_Lerp(MotionFlow_Grad(hAA0, xfm, yf, zf),
                               MotionFlow_Grad(hBA0, x1m, yf, zf), um);
  float l10m = MotionFlow_Lerp(MotionFlow_Grad(hAB0, xfm, y1, zf),
                               MotionFlow_Grad(hBB0, x1m, y1, zf), um);
  float l01m = MotionFlow_Lerp(MotionFlow_Grad(hAA1, xfm, yf, z1),
                               MotionFlow_Grad(hBA1, x1m, yf, z1), um);
  float l11m = MotionFlow_Lerp(MotionFlow_Grad(hAB1, xfm, y1, z1),
                               MotionFlow_Grad(hBB1, x1m, y1, z1), um);
  *outM = MotionFlow_Lerp(MotionFlow_Lerp(l00m, l10m, v),
                          MotionFlow_Lerp(l01m, l11m, v), w);
}

static inline void Noise_Perlin3D_PairY(float x, float yp, float ym, float z,
                                        float *outP, float *outM)
{
  int ix = MotionFlow_FastFloor(x);
  int iz = MotionFlow_FastFloor(z);
  int X = ix & 255;
  int Z = iz & 255;
  float xf = x - (float)ix;
  float zf = z - (float)iz;
  float u = MotionFlow_Fade(xf);
  float w = MotionFlow_Fade(zf);
  float x1 = xf - 1.0f;
  float z1 = zf - 1.0f;

  int iyp = MotionFlow_FastFloor(yp);
  int iym = MotionFlow_FastFloor(ym);
  float yfp = yp - (float)iyp;
  float yfm = ym - (float)iym;
  float vp = MotionFlow_Fade(yfp);
  float vm = MotionFlow_Fade(yfm);
  float y1p = yfp - 1.0f;
  float y1m = yfm - 1.0f;

  int permX0 = s_mfPerm[X];
  int permX1 = s_mfPerm[X + 1];

  int Yp = iyp & 255;
  int A = permX0 + Yp;
  int AA = s_mfPerm[A] + Z;
  int AB = s_mfPerm[A + 1] + Z;
  int B = permX1 + Yp;
  int BA = s_mfPerm[B] + Z;
  int BB = s_mfPerm[B + 1] + Z;

  int hAA0 = s_mfPerm[AA], hBA0 = s_mfPerm[BA];
  int hAB0 = s_mfPerm[AB], hBB0 = s_mfPerm[BB];
  int hAA1 = s_mfPerm[AA + 1], hBA1 = s_mfPerm[BA + 1];
  int hAB1 = s_mfPerm[AB + 1], hBB1 = s_mfPerm[BB + 1];

  float l00p = MotionFlow_Lerp(MotionFlow_Grad(hAA0, xf, yfp, zf),
                               MotionFlow_Grad(hBA0, x1, yfp, zf), u);
  float l10p = MotionFlow_Lerp(MotionFlow_Grad(hAB0, xf, y1p, zf),
                               MotionFlow_Grad(hBB0, x1, y1p, zf), u);
  float l01p = MotionFlow_Lerp(MotionFlow_Grad(hAA1, xf, yfp, z1),
                               MotionFlow_Grad(hBA1, x1, yfp, z1), u);
  float l11p = MotionFlow_Lerp(MotionFlow_Grad(hAB1, xf, y1p, z1),
                               MotionFlow_Grad(hBB1, x1, y1p, z1), u);
  *outP = MotionFlow_Lerp(MotionFlow_Lerp(l00p, l10p, vp),
                          MotionFlow_Lerp(l01p, l11p, vp), w);

  if (iym != iyp)
  {
    int Ym = iym & 255;
    int Am = permX0 + Ym;
    int AAm = s_mfPerm[Am] + Z;
    int ABm = s_mfPerm[Am + 1] + Z;
    int Bm = permX1 + Ym;
    int BAm = s_mfPerm[Bm] + Z;
    int BBm = s_mfPerm[Bm + 1] + Z;
    hAA0 = s_mfPerm[AAm];
    hBA0 = s_mfPerm[BAm];
    hAB0 = s_mfPerm[ABm];
    hBB0 = s_mfPerm[BBm];
    hAA1 = s_mfPerm[AAm + 1];
    hBA1 = s_mfPerm[BAm + 1];
    hAB1 = s_mfPerm[ABm + 1];
    hBB1 = s_mfPerm[BBm + 1];
  }

  float l00m = MotionFlow_Lerp(MotionFlow_Grad(hAA0, xf, yfm, zf),
                               MotionFlow_Grad(hBA0, x1, yfm, zf), u);
  float l10m = MotionFlow_Lerp(MotionFlow_Grad(hAB0, xf, y1m, zf),
                               MotionFlow_Grad(hBB0, x1, y1m, zf), u);
  float l01m = MotionFlow_Lerp(MotionFlow_Grad(hAA1, xf, yfm, z1),
                               MotionFlow_Grad(hBA1, x1, yfm, z1), u);
  float l11m = MotionFlow_Lerp(MotionFlow_Grad(hAB1, xf, y1m, z1),
                               MotionFlow_Grad(hBB1, x1, y1m, z1), u);
  *outM = MotionFlow_Lerp(MotionFlow_Lerp(l00m, l10m, vm),
                          MotionFlow_Lerp(l01m, l11m, vm), w);
}

static inline void Noise_Perlin3D_PairZ(float x, float y, float zp, float zm,
                                        float *outP, float *outM)
{
  int ix = MotionFlow_FastFloor(x);
  int iy = MotionFlow_FastFloor(y);
  int X = ix & 255;
  int Y = iy & 255;
  float xf = x - (float)ix;
  float yf = y - (float)iy;
  float u = MotionFlow_Fade(xf);
  float v = MotionFlow_Fade(yf);
  float x1 = xf - 1.0f;
  float y1 = yf - 1.0f;

  int izp = MotionFlow_FastFloor(zp);
  int izm = MotionFlow_FastFloor(zm);
  float zfp = zp - (float)izp;
  float zfm = zm - (float)izm;
  float wp = MotionFlow_Fade(zfp);
  float wm = MotionFlow_Fade(zfm);
  float z1p = zfp - 1.0f;
  float z1m = zfm - 1.0f;

  int A = s_mfPerm[X] + Y;
  int B = s_mfPerm[X + 1] + Y;
  int permA0 = s_mfPerm[A];
  int permA1 = s_mfPerm[A + 1];
  int permB0 = s_mfPerm[B];
  int permB1 = s_mfPerm[B + 1];

  int Zp = izp & 255;
  int AA = permA0 + Zp;
  int AB = permA1 + Zp;
  int BA = permB0 + Zp;
  int BB = permB1 + Zp;

  int hAA0 = s_mfPerm[AA], hBA0 = s_mfPerm[BA];
  int hAB0 = s_mfPerm[AB], hBB0 = s_mfPerm[BB];
  int hAA1 = s_mfPerm[AA + 1], hBA1 = s_mfPerm[BA + 1];
  int hAB1 = s_mfPerm[AB + 1], hBB1 = s_mfPerm[BB + 1];

  float l00p = MotionFlow_Lerp(MotionFlow_Grad(hAA0, xf, yf, zfp),
                               MotionFlow_Grad(hBA0, x1, yf, zfp), u);
  float l10p = MotionFlow_Lerp(MotionFlow_Grad(hAB0, xf, y1, zfp),
                               MotionFlow_Grad(hBB0, x1, y1, zfp), u);
  float l01p = MotionFlow_Lerp(MotionFlow_Grad(hAA1, xf, yf, z1p),
                               MotionFlow_Grad(hBA1, x1, yf, z1p), u);
  float l11p = MotionFlow_Lerp(MotionFlow_Grad(hAB1, xf, y1, z1p),
                               MotionFlow_Grad(hBB1, x1, y1, z1p), u);
  *outP = MotionFlow_Lerp(MotionFlow_Lerp(l00p, l10p, v),
                          MotionFlow_Lerp(l01p, l11p, v), wp);

  if (izm != izp)
  {
    int Zm = izm & 255;
    int AAm = permA0 + Zm;
    int ABm = permA1 + Zm;
    int BAm = permB0 + Zm;
    int BBm = permB1 + Zm;
    hAA0 = s_mfPerm[AAm];
    hBA0 = s_mfPerm[BAm];
    hAB0 = s_mfPerm[ABm];
    hBB0 = s_mfPerm[BBm];
    hAA1 = s_mfPerm[AAm + 1];
    hBA1 = s_mfPerm[BAm + 1];
    hAB1 = s_mfPerm[ABm + 1];
    hBB1 = s_mfPerm[BBm + 1];
  }

  float l00m = MotionFlow_Lerp(MotionFlow_Grad(hAA0, xf, yf, zfm),
                               MotionFlow_Grad(hBA0, x1, yf, zfm), u);
  float l10m = MotionFlow_Lerp(MotionFlow_Grad(hAB0, xf, y1, zfm),
                               MotionFlow_Grad(hBB0, x1, y1, zfm), u);
  float l01m = MotionFlow_Lerp(MotionFlow_Grad(hAA1, xf, yf, z1m),
                               MotionFlow_Grad(hBA1, x1, yf, z1m), u);
  float l11m = MotionFlow_Lerp(MotionFlow_Grad(hAB1, xf, y1, z1m),
                               MotionFlow_Grad(hBB1, x1, y1, z1m), u);
  *outM = MotionFlow_Lerp(MotionFlow_Lerp(l00m, l10m, v),
                          MotionFlow_Lerp(l01m, l11m, v), wm);
}
#endif /* CORE_NOISE_PERLIN_PAIR_DEFINED */

typedef struct MotionFlowDesc
{
  float turbulenceSpeedMps, swirlSpeedMps;
  float eddyLengthM;
} MotionFlowDesc;

typedef enum
{
  MOTION_FLOW_TUBE,
  MOTION_FLOW_SPHERE
} MotionFlowDomain;

static inline bool MotionFlow_IsValid(const MotionFlowDesc *flow)
{
  return flow && isfinite(flow->turbulenceSpeedMps) &&
         flow->turbulenceSpeedMps >= 0 && isfinite(flow->swirlSpeedMps) &&
         isfinite(flow->eddyLengthM) && flow->eddyLengthM >= 0;
}

static inline float MotionFlow_BandNorm(float x)
{
  x = Motion_Clamp(x, 0.0f, 1.0f);
  return 1.0f - x * x * x * (x * (6.0f * x - 15.0f) + 10.0f);
}

static inline float MotionFlow_Band(float distance, float radius)
{
  return MotionFlow_BandNorm(distance / radius);
}

static inline Vector3 MotionFlow_Radial(Vector3 p, Vector3 center,
                                        Vector3 axis)
{
  Vector3 d = MotionVec_Sub(p, center);
  return MotionVec_Sub(d, MotionVec_Scale(axis, MotionVec_Dot(d, axis)));
}

static inline float MotionFlow_BandFromDistSq(float distSq, float radiusSq,
                                              float invRadius)
{
  if (distSq >= radiusSq)
    return 0.0f;
  return MotionFlow_BandNorm(sqrtf(distSq) * invRadius);
}

static inline float MotionFlow_BandAt(Vector3 p, Vector3 center, Vector3 axis,
                                      float radius, MotionFlowDomain domain)
{
  Vector3 d = domain == MOTION_FLOW_SPHERE ? MotionVec_Sub(p, center)
                                           : MotionFlow_Radial(p, center, axis);
  float distSq = MotionVec_Dot(d, d);
  if (distSq >= radius * radius)
    return 0.0f;
  return MotionFlow_Band(sqrtf(distSq), radius);
}

static inline Vector3 MotionFlow_Potential(const MotionFlowDesc *flow,
                                           Vector3 p, Vector3 center,
                                           Vector3 axis, float radius,
                                           float time,
                                           MotionFlowDomain domain)
{
  float band = MotionFlow_BandAt(p, center, axis, radius, domain);
  if (band == 0.0f)
    return (Vector3){0};
  float length = flow->eddyLengthM > 0.0f ? flow->eddyLengthM : radius * 0.25f;
  float invLength = 1.0f / length;
  float phase = time * flow->turbulenceSpeedMps * invLength;
  Vector3 q =
      MotionVec_Add(MotionVec_Scale(p, invLength),
                    (Vector3){phase * 0.37f, phase * 0.51f, phase * 0.29f});
  Vector3 noise = {Noise_Perlin3D(q.x, q.y, q.z),
                   Noise_Perlin3D(q.x + 31.416f, q.y + 31.416f, q.z + 31.416f),
                   Noise_Perlin3D(q.x + 67.234f, q.y + 67.234f, q.z + 67.234f)};
  return MotionVec_Scale(noise, flow->turbulenceSpeedMps * length * band);
}

static inline Vector3 MotionFlow_Evaluate(const MotionFlowDesc *flow, Vector3 p,
                                          Vector3 center, Vector3 direction,
                                          float radius, float time,
                                          MotionFlowDomain domain)
{
  Vector3 result = {0};
  if (!MotionFlow_IsValid(flow) || !isfinite(radius) || radius <= 0.0f)
    return result;
  if (flow->swirlSpeedMps == 0.0f && flow->turbulenceSpeedMps <= 0.0f)
    return result;

  Vector3 axis = MotionVec_Normalize(direction);
  if (MotionVec_Dot(axis, axis) < 0.01f)
    axis = (Vector3){0, 1, 0};

  Vector3 d = MotionVec_Sub(p, center);
  Vector3 radial = MotionVec_Sub(d, MotionVec_Scale(axis, MotionVec_Dot(d, axis)));
  Vector3 dVec = (domain == MOTION_FLOW_SPHERE) ? d : radial;
  float distSq = MotionVec_Dot(dVec, dVec);
  float radiusSq = radius * radius;
  float invRadius = 1.0f / radius;

  if (flow->swirlSpeedMps != 0.0f && distSq < radiusSq)
  {
    result = MotionVec_Scale(MotionVec_Cross(axis, radial),
                             flow->swirlSpeedMps * invRadius *
                                 MotionFlow_BandNorm(sqrtf(distSq) * invRadius));
  }

  if (flow->turbulenceSpeedMps > 0.0f)
  {
    float h = radius * 0.005f;
    float maxReach = radius + h;
    if (distSq >= maxReach * maxReach)
      return result;

    float length = flow->eddyLengthM > 0.0f ? flow->eddyLengthM : radius * 0.25f;
    float invLength = 1.0f / length;
    float scale = flow->turbulenceSpeedMps * length;
    float phase = time * flow->turbulenceSpeedMps * invLength;

    float step = h * invLength;
    float qx0 = p.x * invLength + phase * 0.37f;
    float qy0 = p.y * invLength + phase * 0.51f;
    float qz0 = p.z * invLength + phase * 0.29f;
    float qx1 = qx0 + 31.416f, qy1 = qy0 + 31.416f, qz1 = qz0 + 31.416f;
    float qx2 = qx0 + 67.234f, qy2 = qy0 + 67.234f, qz2 = qz0 + 67.234f;

    float hSq = h * h;
    float twoH = 2.0f * h;
    bool isSphere = (domain == MOTION_FLOW_SPHERE);
    float hSqX = isSphere ? hSq : hSq * fmaxf(0.0f, 1.0f - axis.x * axis.x);
    float hSqY = isSphere ? hSq : hSq * fmaxf(0.0f, 1.0f - axis.y * axis.y);
    float hSqZ = isSphere ? hSq : hSq * fmaxf(0.0f, 1.0f - axis.z * axis.z);

    float dSqPx = fmaxf(0.0f, distSq + twoH * dVec.x + hSqX);
    float dSqMx = fmaxf(0.0f, distSq - twoH * dVec.x + hSqX);
    float bpx = MotionFlow_BandFromDistSq(dSqPx, radiusSq, invRadius) * scale;
    float bmx = MotionFlow_BandFromDistSq(dSqMx, radiusSq, invRadius) * scale;
    float ax_y = 0.0f, ax_z = 0.0f;
    if (bpx != 0.0f || bmx != 0.0f)
    {
      float n1p, n1m, n2p, n2m;
      Noise_Perlin3D_PairX(qx1 + step, qx1 - step, qy1, qz1, &n1p, &n1m);
      Noise_Perlin3D_PairX(qx2 + step, qx2 - step, qy2, qz2, &n2p, &n2m);
      ax_y = n1p * bpx - n1m * bmx;
      ax_z = n2p * bpx - n2m * bmx;
    }

    float dSqPy = fmaxf(0.0f, distSq + twoH * dVec.y + hSqY);
    float dSqMy = fmaxf(0.0f, distSq - twoH * dVec.y + hSqY);
    float bpy = MotionFlow_BandFromDistSq(dSqPy, radiusSq, invRadius) * scale;
    float bmy = MotionFlow_BandFromDistSq(dSqMy, radiusSq, invRadius) * scale;
    float ay_x = 0.0f, ay_z = 0.0f;
    if (bpy != 0.0f || bmy != 0.0f)
    {
      float n0p, n0m, n2p, n2m;
      Noise_Perlin3D_PairY(qx0, qy0 + step, qy0 - step, qz0, &n0p, &n0m);
      Noise_Perlin3D_PairY(qx2, qy2 + step, qy2 - step, qz2, &n2p, &n2m);
      ay_x = n0p * bpy - n0m * bmy;
      ay_z = n2p * bpy - n2m * bmy;
    }

    float dSqPz = fmaxf(0.0f, distSq + twoH * dVec.z + hSqZ);
    float dSqMz = fmaxf(0.0f, distSq - twoH * dVec.z + hSqZ);
    float bpz = MotionFlow_BandFromDistSq(dSqPz, radiusSq, invRadius) * scale;
    float bmz = MotionFlow_BandFromDistSq(dSqMz, radiusSq, invRadius) * scale;
    float az_x = 0.0f, az_y = 0.0f;
    if (bpz != 0.0f || bmz != 0.0f)
    {
      float n0p, n0m, n1p, n1m;
      Noise_Perlin3D_PairZ(qx0, qy0, qz0 + step, qz0 - step, &n0p, &n0m);
      Noise_Perlin3D_PairZ(qx1, qy1, qz1 + step, qz1 - step, &n1p, &n1m);
      az_x = n0p * bpz - n0m * bmz;
      az_y = n1p * bpz - n1m * bmz;
    }

    result = MotionVec_Add(
        result,
        MotionVec_Scale((Vector3){ay_z - az_y, az_x - ax_z, ax_y - ay_x},
                        1.0f / twoH));
  }
  return result;
}
#endif