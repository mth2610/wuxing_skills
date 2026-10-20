#include "core/force_field.h"
#include "core/particles/particle_plane.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/* ============================================================
 * TUNING CONSTANTS
 * ============================================================ */

#define FF_MIN_STRENGTH 1e-4f
#define FF_MIN_POINT_DIST_SQ 1e-4f
#define FF_MIN_AXIS_DIST_SQ 1e-6f
#define FF_MIN_AXIS_LEN_SQ 1e-6f

#define FF_WIND_GUST_FACTOR 0.35f
#define FF_WIND_Z_OFFSET 53.9f

#define FF_NOISE_OFFSET_A 17.7f
#define FF_NOISE_OFFSET_B 37.3f
#define FF_NOISE_OFFSET_C 73.1f

#define FF_CURL_EPS 0.1f
#define FF_CURL_INV_2EPS (1.0f / (2.0f * FF_CURL_EPS))
#define FF_CURL_OFFSET_1 31.416f
#define FF_CURL_OFFSET_2 67.234f

#define FF_WINDZONE_CURL_SPEED 0.4f
#define FF_WINDZONE_MIN_DIR_LEN 0.0001f

#define FF_PERLIN_MASK 255

/* ============================================================
 * PERLIN NOISE 3D
 * ============================================================ */

/* clang-format off */
#define FF_PERM_DATA \
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
static const unsigned char FF_PERM[512] = { FF_PERM_DATA, FF_PERM_DATA };
/* clang-format on */

static inline int FF_FastFloor(float x)
{
  int xi = (int)x;
  return x < (float)xi ? xi - 1 : xi;
}

static inline float FF_Fade(float t)
{
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float FF_Lerp(float a, float b, float t)
{
  return a + t * (b - a);
}

static const float FF_PERLIN_GRAD_TABLE[16][3] = {
    {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, -1.0f, 1.0f}, {0.0f, 1.0f, -1.0f}, {0.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, -1.0f}};

static inline float FF_PerlinGrad(int hash, float x, float y, float z)
{
  const float *g = FF_PERLIN_GRAD_TABLE[hash & 15];
  return g[0] * x + g[1] * y + g[2] * z;
}

static inline float FF_PerlinImpl(float x, float y, float z)
{
  int ix = FF_FastFloor(x);
  int iy = FF_FastFloor(y);
  int iz = FF_FastFloor(z);

  int X = ix & FF_PERLIN_MASK;
  int Y = iy & FF_PERLIN_MASK;
  int Z = iz & FF_PERLIN_MASK;

  x -= (float)ix;
  y -= (float)iy;
  z -= (float)iz;

  float u = FF_Fade(x);
  float v = FF_Fade(y);
  float w = FF_Fade(z);

  int A = FF_PERM[X] + Y;
  int AA = FF_PERM[A] + Z;
  int AB = FF_PERM[A + 1] + Z;
  int B = FF_PERM[X + 1] + Y;
  int BA = FF_PERM[B] + Z;
  int BB = FF_PERM[B + 1] + Z;

  float x1 = x - 1.0f;
  float y1 = y - 1.0f;
  float z1 = z - 1.0f;

  float l00 = FF_Lerp(FF_PerlinGrad(FF_PERM[AA], x, y, z),
                      FF_PerlinGrad(FF_PERM[BA], x1, y, z), u);
  float l10 = FF_Lerp(FF_PerlinGrad(FF_PERM[AB], x, y1, z),
                      FF_PerlinGrad(FF_PERM[BB], x1, y1, z), u);
  float l01 = FF_Lerp(FF_PerlinGrad(FF_PERM[AA + 1], x, y, z1),
                      FF_PerlinGrad(FF_PERM[BA + 1], x1, y, z1), u);
  float l11 = FF_Lerp(FF_PerlinGrad(FF_PERM[AB + 1], x, y1, z1),
                      FF_PerlinGrad(FF_PERM[BB + 1], x1, y1, z1), u);

  return FF_Lerp(FF_Lerp(l00, l10, v), FF_Lerp(l01, l11, v), w);
}

/* Lấy mẫu cặp dọc theo trục X: chia sẻ hoàn toàn trục Y, Z và tái sử dụng
 * 8 mã băm đỉnh khi ixp == ixm (đúng >90% thời gian). */
static inline void FF_PerlinPairX(float xp, float xm, float y, float z,
                                  float *outP, float *outM)
{
  int iy = FF_FastFloor(y);
  int iz = FF_FastFloor(z);
  int Y = iy & FF_PERLIN_MASK;
  int Z = iz & FF_PERLIN_MASK;
  float yf = y - (float)iy;
  float zf = z - (float)iz;
  float v = FF_Fade(yf);
  float w = FF_Fade(zf);
  float y1 = yf - 1.0f;
  float z1 = zf - 1.0f;

  int ixp = FF_FastFloor(xp);
  int ixm = FF_FastFloor(xm);
  float xfp = xp - (float)ixp;
  float xfm = xm - (float)ixm;
  float up = FF_Fade(xfp);
  float um = FF_Fade(xfm);
  float x1p = xfp - 1.0f;
  float x1m = xfm - 1.0f;

  int Xp = ixp & FF_PERLIN_MASK;
  int A = FF_PERM[Xp] + Y;
  int AA = FF_PERM[A] + Z;
  int AB = FF_PERM[A + 1] + Z;
  int B = FF_PERM[Xp + 1] + Y;
  int BA = FF_PERM[B] + Z;
  int BB = FF_PERM[B + 1] + Z;

  int hAA0 = FF_PERM[AA], hBA0 = FF_PERM[BA];
  int hAB0 = FF_PERM[AB], hBB0 = FF_PERM[BB];
  int hAA1 = FF_PERM[AA + 1], hBA1 = FF_PERM[BA + 1];
  int hAB1 = FF_PERM[AB + 1], hBB1 = FF_PERM[BB + 1];

  float l00p = FF_Lerp(FF_PerlinGrad(hAA0, xfp, yf, zf),
                       FF_PerlinGrad(hBA0, x1p, yf, zf), up);
  float l10p = FF_Lerp(FF_PerlinGrad(hAB0, xfp, y1, zf),
                       FF_PerlinGrad(hBB0, x1p, y1, zf), up);
  float l01p = FF_Lerp(FF_PerlinGrad(hAA1, xfp, yf, z1),
                       FF_PerlinGrad(hBA1, x1p, yf, z1), up);
  float l11p = FF_Lerp(FF_PerlinGrad(hAB1, xfp, y1, z1),
                       FF_PerlinGrad(hBB1, x1p, y1, z1), up);
  *outP = FF_Lerp(FF_Lerp(l00p, l10p, v), FF_Lerp(l01p, l11p, v), w);

  if (ixm != ixp)
  {
    int Xm = ixm & FF_PERLIN_MASK;
    int Am = FF_PERM[Xm] + Y;
    int AAm = FF_PERM[Am] + Z;
    int ABm = FF_PERM[Am + 1] + Z;
    int Bm = FF_PERM[Xm + 1] + Y;
    int BAm = FF_PERM[Bm] + Z;
    int BBm = FF_PERM[Bm + 1] + Z;
    hAA0 = FF_PERM[AAm];
    hBA0 = FF_PERM[BAm];
    hAB0 = FF_PERM[ABm];
    hBB0 = FF_PERM[BBm];
    hAA1 = FF_PERM[AAm + 1];
    hBA1 = FF_PERM[BAm + 1];
    hAB1 = FF_PERM[ABm + 1];
    hBB1 = FF_PERM[BBm + 1];
  }

  float l00m = FF_Lerp(FF_PerlinGrad(hAA0, xfm, yf, zf),
                       FF_PerlinGrad(hBA0, x1m, yf, zf), um);
  float l10m = FF_Lerp(FF_PerlinGrad(hAB0, xfm, y1, zf),
                       FF_PerlinGrad(hBB0, x1m, y1, zf), um);
  float l01m = FF_Lerp(FF_PerlinGrad(hAA1, xfm, yf, z1),
                       FF_PerlinGrad(hBA1, x1m, yf, z1), um);
  float l11m = FF_Lerp(FF_PerlinGrad(hAB1, xfm, y1, z1),
                       FF_PerlinGrad(hBB1, x1m, y1, z1), um);
  *outM = FF_Lerp(FF_Lerp(l00m, l10m, v), FF_Lerp(l01m, l11m, v), w);
}

/* Lấy mẫu cặp dọc theo trục Y: chia sẻ hoàn toàn trục X, Z */
static inline void FF_PerlinPairY(float x, float yp, float ym, float z,
                                  float *outP, float *outM)
{
  int ix = FF_FastFloor(x);
  int iz = FF_FastFloor(z);
  int X = ix & FF_PERLIN_MASK;
  int Z = iz & FF_PERLIN_MASK;
  float xf = x - (float)ix;
  float zf = z - (float)iz;
  float u = FF_Fade(xf);
  float w = FF_Fade(zf);
  float x1 = xf - 1.0f;
  float z1 = zf - 1.0f;

  int iyp = FF_FastFloor(yp);
  int iym = FF_FastFloor(ym);
  float yfp = yp - (float)iyp;
  float yfm = ym - (float)iym;
  float vp = FF_Fade(yfp);
  float vm = FF_Fade(yfm);
  float y1p = yfp - 1.0f;
  float y1m = yfm - 1.0f;

  int permX0 = FF_PERM[X];
  int permX1 = FF_PERM[X + 1];

  int Yp = iyp & FF_PERLIN_MASK;
  int A = permX0 + Yp;
  int AA = FF_PERM[A] + Z;
  int AB = FF_PERM[A + 1] + Z;
  int B = permX1 + Yp;
  int BA = FF_PERM[B] + Z;
  int BB = FF_PERM[B + 1] + Z;

  int hAA0 = FF_PERM[AA], hBA0 = FF_PERM[BA];
  int hAB0 = FF_PERM[AB], hBB0 = FF_PERM[BB];
  int hAA1 = FF_PERM[AA + 1], hBA1 = FF_PERM[BA + 1];
  int hAB1 = FF_PERM[AB + 1], hBB1 = FF_PERM[BB + 1];

  float l00p = FF_Lerp(FF_PerlinGrad(hAA0, xf, yfp, zf),
                       FF_PerlinGrad(hBA0, x1, yfp, zf), u);
  float l10p = FF_Lerp(FF_PerlinGrad(hAB0, xf, y1p, zf),
                       FF_PerlinGrad(hBB0, x1, y1p, zf), u);
  float l01p = FF_Lerp(FF_PerlinGrad(hAA1, xf, yfp, z1),
                       FF_PerlinGrad(hBA1, x1, yfp, z1), u);
  float l11p = FF_Lerp(FF_PerlinGrad(hAB1, xf, y1p, z1),
                       FF_PerlinGrad(hBB1, x1, y1p, z1), u);
  *outP = FF_Lerp(FF_Lerp(l00p, l10p, vp), FF_Lerp(l01p, l11p, vp), w);

  if (iym != iyp)
  {
    int Ym = iym & FF_PERLIN_MASK;
    int Am = permX0 + Ym;
    int AAm = FF_PERM[Am] + Z;
    int ABm = FF_PERM[Am + 1] + Z;
    int Bm = permX1 + Ym;
    int BAm = FF_PERM[Bm] + Z;
    int BBm = FF_PERM[Bm + 1] + Z;
    hAA0 = FF_PERM[AAm];
    hBA0 = FF_PERM[BAm];
    hAB0 = FF_PERM[ABm];
    hBB0 = FF_PERM[BBm];
    hAA1 = FF_PERM[AAm + 1];
    hBA1 = FF_PERM[BAm + 1];
    hAB1 = FF_PERM[ABm + 1];
    hBB1 = FF_PERM[BBm + 1];
  }

  float l00m = FF_Lerp(FF_PerlinGrad(hAA0, xf, yfm, zf),
                       FF_PerlinGrad(hBA0, x1, yfm, zf), u);
  float l10m = FF_Lerp(FF_PerlinGrad(hAB0, xf, y1m, zf),
                       FF_PerlinGrad(hBB0, x1, y1m, zf), u);
  float l01m = FF_Lerp(FF_PerlinGrad(hAA1, xf, yfm, z1),
                       FF_PerlinGrad(hBA1, x1, yfm, z1), u);
  float l11m = FF_Lerp(FF_PerlinGrad(hAB1, xf, y1m, z1),
                       FF_PerlinGrad(hBB1, x1, y1m, z1), u);
  *outM = FF_Lerp(FF_Lerp(l00m, l10m, vm), FF_Lerp(l01m, l11m, vm), w);
}

/* Lấy mẫu cặp dọc theo trục Z: chia sẻ hoàn toàn trục X, Y và 4 bảng băm trung gian */
static inline void FF_PerlinPairZ(float x, float y, float zp, float zm,
                                  float *outP, float *outM)
{
  int ix = FF_FastFloor(x);
  int iy = FF_FastFloor(y);
  int X = ix & FF_PERLIN_MASK;
  int Y = iy & FF_PERLIN_MASK;
  float xf = x - (float)ix;
  float yf = y - (float)iy;
  float u = FF_Fade(xf);
  float v = FF_Fade(yf);
  float x1 = xf - 1.0f;
  float y1 = yf - 1.0f;

  int izp = FF_FastFloor(zp);
  int izm = FF_FastFloor(zm);
  float zfp = zp - (float)izp;
  float zfm = zm - (float)izm;
  float wp = FF_Fade(zfp);
  float wm = FF_Fade(zfm);
  float z1p = zfp - 1.0f;
  float z1m = zfm - 1.0f;

  int A = FF_PERM[X] + Y;
  int B = FF_PERM[X + 1] + Y;
  int permA0 = FF_PERM[A];
  int permA1 = FF_PERM[A + 1];
  int permB0 = FF_PERM[B];
  int permB1 = FF_PERM[B + 1];

  int Zp = izp & FF_PERLIN_MASK;
  int AA = permA0 + Zp;
  int AB = permA1 + Zp;
  int BA = permB0 + Zp;
  int BB = permB1 + Zp;

  int hAA0 = FF_PERM[AA], hBA0 = FF_PERM[BA];
  int hAB0 = FF_PERM[AB], hBB0 = FF_PERM[BB];
  int hAA1 = FF_PERM[AA + 1], hBA1 = FF_PERM[BA + 1];
  int hAB1 = FF_PERM[AB + 1], hBB1 = FF_PERM[BB + 1];

  float l00p = FF_Lerp(FF_PerlinGrad(hAA0, xf, yf, zfp),
                       FF_PerlinGrad(hBA0, x1, yf, zfp), u);
  float l10p = FF_Lerp(FF_PerlinGrad(hAB0, xf, y1, zfp),
                       FF_PerlinGrad(hBB0, x1, y1, zfp), u);
  float l01p = FF_Lerp(FF_PerlinGrad(hAA1, xf, yf, z1p),
                       FF_PerlinGrad(hBA1, x1, yf, z1p), u);
  float l11p = FF_Lerp(FF_PerlinGrad(hAB1, xf, y1, z1p),
                       FF_PerlinGrad(hBB1, x1, y1, z1p), u);
  *outP = FF_Lerp(FF_Lerp(l00p, l10p, v), FF_Lerp(l01p, l11p, v), wp);

  if (izm != izp)
  {
    int Zm = izm & FF_PERLIN_MASK;
    int AAm = permA0 + Zm;
    int ABm = permA1 + Zm;
    int BAm = permB0 + Zm;
    int BBm = permB1 + Zm;
    hAA0 = FF_PERM[AAm];
    hBA0 = FF_PERM[BAm];
    hAB0 = FF_PERM[ABm];
    hBB0 = FF_PERM[BBm];
    hAA1 = FF_PERM[AAm + 1];
    hBA1 = FF_PERM[BAm + 1];
    hAB1 = FF_PERM[ABm + 1];
    hBB1 = FF_PERM[BBm + 1];
  }

  float l00m = FF_Lerp(FF_PerlinGrad(hAA0, xf, yf, zfm),
                       FF_PerlinGrad(hBA0, x1, yf, zfm), u);
  float l10m = FF_Lerp(FF_PerlinGrad(hAB0, xf, y1, zfm),
                       FF_PerlinGrad(hBB0, x1, y1, zfm), u);
  float l01m = FF_Lerp(FF_PerlinGrad(hAA1, xf, yf, z1m),
                       FF_PerlinGrad(hBA1, x1, yf, z1m), u);
  float l11m = FF_Lerp(FF_PerlinGrad(hAB1, xf, y1, z1m),
                       FF_PerlinGrad(hBB1, x1, y1, z1m), u);
  *outM = FF_Lerp(FF_Lerp(l00m, l10m, v), FF_Lerp(l01m, l11m, v), wm);
}

float Noise_Perlin3D(float x, float y, float z)
{
  return FF_PerlinImpl(x, y, z);
}

/* ============================================================
 * VALUE NOISE 3D
 * ============================================================ */

static inline float FF_ValueHash(int ix, int iy, int iz)
{
  unsigned int h = (unsigned int)ix * 1664525u +
                   (unsigned int)iy * 1013904223u +
                   (unsigned int)iz * 22695477u;
  h ^= h >> 16;
  h *= 0x45d9f3bu;
  h ^= h >> 16;
  return (float)(h & 0xFFFFu) * (1.0f / 65535.0f);
}

float Noise_Value3D(float x, float y, float z)
{
  int ix = FF_FastFloor(x);
  int iy = FF_FastFloor(y);
  int iz = FF_FastFloor(z);
  float fx = x - (float)ix;
  float fy = y - (float)iy;
  float fz = z - (float)iz;

  float ux = fx * fx * (3.0f - 2.0f * fx);
  float uy = fy * fy * (3.0f - 2.0f * fy);
  float uz = fz * fz * (3.0f - 2.0f * fz);

  float v000 = FF_ValueHash(ix, iy, iz);
  float v100 = FF_ValueHash(ix + 1, iy, iz);
  float v010 = FF_ValueHash(ix, iy + 1, iz);
  float v110 = FF_ValueHash(ix + 1, iy + 1, iz);
  float v001 = FF_ValueHash(ix, iy, iz + 1);
  float v101 = FF_ValueHash(ix + 1, iy, iz + 1);
  float v011 = FF_ValueHash(ix, iy + 1, iz + 1);
  float v111 = FF_ValueHash(ix + 1, iy + 1, iz + 1);

  float x00 = v000 + ux * (v100 - v000);
  float x10 = v010 + ux * (v110 - v010);
  float x01 = v001 + ux * (v101 - v001);
  float x11 = v011 + ux * (v111 - v011);
  float y0 = x00 + uy * (x10 - x00);
  float y1 = x01 + uy * (x11 - x01);
  return y0 + uz * (y1 - y0);
}

/* ============================================================
 * CURL NOISE 3D
 * ============================================================ */

static inline Vector3 FF_CurlImpl(float sx, float sy, float sz)
{
  const float bx1 = sx + FF_CURL_OFFSET_1;
  const float by1 = sy + FF_CURL_OFFSET_1;
  const float bz1 = sz + FF_CURL_OFFSET_1;
  const float bx2 = sx + FF_CURL_OFFSET_2;
  const float by2 = sy + FF_CURL_OFFSET_2;
  const float bz2 = sz + FF_CURL_OFFSET_2;

  float psi1_pz, psi1_mz, psi1_py, psi1_my;
  FF_PerlinPairZ(sx, sy, sz + FF_CURL_EPS, sz - FF_CURL_EPS, &psi1_pz, &psi1_mz);
  FF_PerlinPairY(sx, sy + FF_CURL_EPS, sy - FF_CURL_EPS, sz, &psi1_py, &psi1_my);

  float psi2_px, psi2_mx, psi2_pz, psi2_mz;
  FF_PerlinPairX(bx1 + FF_CURL_EPS, bx1 - FF_CURL_EPS, by1, bz1, &psi2_px, &psi2_mx);
  FF_PerlinPairZ(bx1, by1, bz1 + FF_CURL_EPS, bz1 - FF_CURL_EPS, &psi2_pz, &psi2_mz);

  float psi3_py, psi3_my, psi3_px, psi3_mx;
  FF_PerlinPairY(bx2, by2 + FF_CURL_EPS, by2 - FF_CURL_EPS, bz2, &psi3_py, &psi3_my);
  FF_PerlinPairX(bx2 + FF_CURL_EPS, bx2 - FF_CURL_EPS, by2, bz2, &psi3_px, &psi3_mx);

  Vector3 c;
  c.x = ((psi3_py - psi3_my) - (psi2_pz - psi2_mz)) * FF_CURL_INV_2EPS;
  c.y = ((psi1_pz - psi1_mz) - (psi3_px - psi3_mx)) * FF_CURL_INV_2EPS;
  c.z = ((psi2_px - psi2_mx) - (psi1_py - psi1_my)) * FF_CURL_INV_2EPS;
  return c;
}

Vector3 Noise_Curl3D(float x, float y, float z, float scale)
{
  return FF_CurlImpl(x * scale, y * scale, z * scale);
}

/* ============================================================
 * FORCE FIELD BASICS
 * ============================================================ */

void ForceField_Clear(ForceField *ff)
{
  if (ff)
    ff->layerCount = 0;
}

bool ForceField_AddLayer(ForceField *ff, ForceLayer layer)
{
  if (!ff)
    return false;
  if (ff->layerCount < 0)
    ff->layerCount = 0;
  if (ff->layerCount >= FORCE_FIELD_MAX_LAYERS)
    return false;
  ff->layers[ff->layerCount++] = layer;
  return true;
}

static inline int FF_ClampedLayerCount(const ForceField *ff)
{
  int n = ff->layerCount;
  if (n < 0)
    return 0;
  return n > FORCE_FIELD_MAX_LAYERS ? FORCE_FIELD_MAX_LAYERS : n;
}

static inline float FF_FalloffFactor(float t, float falloff)
{
  if (falloff <= 0.0f)
    return 1.0f;
  if (falloff <= 1.0f)
    return 1.0f - t;
  {
    float k = 1.0f - t;
    return k * k;
  }
}

typedef struct
{
  float distSq;
  float dist;
  Vector3 radial;
  Vector3 tangent;
} FF_AxisFrame;

#define FF_AXIS_UNKNOWN 0
#define FF_AXIS_VALID 1
#define FF_AXIS_INVALID (-1)

#if defined(__GNUC__) || defined(__clang__)
#define FF_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define FF_NOINLINE __declspec(noinline)
#else
#define FF_NOINLINE
#endif

static FF_NOINLINE bool FF_AxisFrame_Build(FF_AxisFrame *f, float px, float py,
                                           float pz, Vector3 axisOrigin,
                                           Vector3 axisDir)
{
  float lenSq = axisDir.x * axisDir.x + axisDir.y * axisDir.y +
                axisDir.z * axisDir.z;
  if (!(lenSq >= FF_MIN_AXIS_LEN_SQ))
    return false;

  float adx = axisDir.x, ady = axisDir.y, adz = axisDir.z;
  if (fabsf(lenSq - 1.0f) > 1e-5f)
  {
    float invLen = 1.0f / sqrtf(lenSq);
    adx *= invLen;
    ady *= invLen;
    adz *= invLen;
  }

  float tx = px - axisOrigin.x;
  float ty = py - axisOrigin.y;
  float tz = pz - axisOrigin.z;

  float along = tx * adx + ty * ady + tz * adz;
  float rx = tx - adx * along;
  float ry = ty - ady * along;
  float rz = tz - adz * along;

  float distSq = rx * rx + ry * ry + rz * rz;
  if (!(distSq >= FF_MIN_AXIS_DIST_SQ))
    return false;

  float dist = sqrtf(distSq);
  float invDist = 1.0f / dist;
  float ux = rx * invDist;
  float uy = ry * invDist;
  float uz = rz * invDist;

  f->distSq = distSq;
  f->dist = dist;
  f->radial.x = ux;
  f->radial.y = uy;
  f->radial.z = uz;
  f->tangent.x = ady * uz - adz * uy;
  f->tangent.y = adz * ux - adx * uz;
  f->tangent.z = adx * uy - ady * ux;
  return true;
}

/* ============================================================
 * EVALUATE
 * ============================================================ */

Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel,
                            float time, Vector3 axisOrigin, Vector3 axisDir)
{
  float total_ax = 0.0f, total_ay = 0.0f, total_az = 0.0f;
  if (!ff)
    return (Vector3){0.0f, 0.0f, 0.0f};

  const int count = FF_ClampedLayerCount(ff);
  const float px = pos.x, py = pos.y, pz = pos.z;

  FF_AxisFrame axis = {0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
  int axisState = FF_AXIS_UNKNOWN;

  for (int i = 0; i < count; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    const float strength = L->strength;
    const int type = L->type;

    /* Chặn cả strength quá nhỏ lẫn NaN/Inf */
    if (!(fabsf(strength) >= FF_MIN_STRENGTH) || !isfinite(strength))
      continue;

    if (type == FORCE_VISCOSITY || type == FORCE_VECTOR_TEXTURE ||
        type == FORCE_RECEIVER_PLANE)
      continue;

    float atten = 1.0f;
    float relDist = -1.0f;
    float relDistSq = -1.0f;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    bool hasOriginDelta = false;

    if (type != FORCE_RADIAL_AXIS && type != FORCE_VORTEX_AXIS &&
        L->radius > 0.0f)
    {
      dx = px - L->origin.x;
      dy = py - L->origin.y;
      dz = pz - L->origin.z;
      relDistSq = dx * dx + dy * dy + dz * dz;
      hasOriginDelta = true;
      float radius = L->radius;

      if (!(relDistSq < radius * radius))
        continue;

      if (L->falloff > 0.0f)
      {
        relDist = sqrtf(relDistSq);
        atten = FF_FalloffFactor(relDist / radius, L->falloff);
      }
    }

    float acc_x = 0.0f, acc_y = 0.0f, acc_z = 0.0f;

    switch (type)
    {
    case FORCE_GRAVITY_DIR:
      acc_x = L->direction.x * strength;
      acc_y = L->direction.y * strength;
      acc_z = L->direction.z * strength;
      break;

    case FORCE_GRAVITY_POINT:
    {
      if (!hasOriginDelta)
      {
        dx = px - L->origin.x;
        dy = py - L->origin.y;
        dz = pz - L->origin.z;
        relDistSq = dx * dx + dy * dy + dz * dz;
      }
      if (!(relDistSq >= FF_MIN_POINT_DIST_SQ))
        break;
      float dist = (relDist >= 0.0f) ? relDist : sqrtf(relDistSq);
      float k = -strength / (dist * (dist + 1.0f));
      acc_x = dx * k;
      acc_y = dy * k;
      acc_z = dz * k;
    }
    break;

    case FORCE_VORTEX:
    {
      float ax = L->direction.x, ay = L->direction.y, az = L->direction.z;
      float alenSq = ax * ax + ay * ay + az * az;
      if (!(alenSq > 0.0f))
        break;
      if (fabsf(alenSq - 1.0f) > 1e-5f)
      {
        float invALen = 1.0f / sqrtf(alenSq);
        ax *= invALen;
        ay *= invALen;
        az *= invALen;
      }

      if (!hasOriginDelta)
      {
        dx = px - L->origin.x;
        dy = py - L->origin.y;
        dz = pz - L->origin.z;
      }

      float proj = dx * ax + dy * ay + dz * az;
      float rad_x = dx - ax * proj;
      float rad_y = dy - ay * proj;
      float rad_z = dz - az * proj;

      float distSq = rad_x * rad_x + rad_y * rad_y + rad_z * rad_z;
      if (!(distSq >= FF_MIN_AXIS_DIST_SQ))
        break;

      float dist = sqrtf(distSq);
      float k = strength / (dist * (dist + 1.0f));
      acc_x = (ay * rad_z - az * rad_y) * k;
      acc_y = (az * rad_x - ax * rad_z) * k;
      acc_z = (ax * rad_y - ay * rad_x) * k;
    }
    break;

    case FORCE_WIND:
    {
      float t = time * L->noiseSpeed;
      float ns = L->noiseScale;
      float px_s = px * ns, py_s = py * ns, pz_s = pz * ns;
      float nx = FF_PerlinImpl(px_s + t, py_s, pz_s);
      float nz = FF_PerlinImpl(px_s + FF_WIND_Z_OFFSET, py_s, pz_s + t);

      acc_x = (L->direction.x * strength) + (nx * strength * FF_WIND_GUST_FACTOR);
      acc_y = (L->direction.y * strength);
      acc_z = (L->direction.z * strength) + (nz * strength * FF_WIND_GUST_FACTOR);
    }
    break;

    case FORCE_NOISE_PERLIN:
    {
      float t = time * L->noiseSpeed;
      float ns = L->noiseScale;
      float px_s = px * ns, py_s = py * ns, pz_s = pz * ns;

      acc_x = FF_PerlinImpl(px_s + t, py_s + FF_NOISE_OFFSET_A, pz_s + FF_NOISE_OFFSET_A) * strength;
      acc_y = FF_PerlinImpl(px_s + FF_NOISE_OFFSET_B, py_s + t, pz_s + FF_NOISE_OFFSET_B) * strength;
      acc_z = FF_PerlinImpl(px_s + FF_NOISE_OFFSET_C, py_s + FF_NOISE_OFFSET_C, pz_s + t) * strength;
    }
    break;

    case FORCE_NOISE_CURL:
    {
      float t = time * L->noiseSpeed;
      float ns = L->noiseScale;
      Vector3 curl = FF_CurlImpl(px * ns + t, py * ns, pz * ns + t);
      acc_x = curl.x * strength;
      acc_y = curl.y * strength;
      acc_z = curl.z * strength;
    }
    break;

    case FORCE_DRAG:
      acc_x = vel.x * -strength;
      acc_y = vel.y * -strength;
      acc_z = vel.z * -strength;
      break;

    case FORCE_RADIAL_AXIS:
    case FORCE_VORTEX_AXIS:
    {
      if (axisState == FF_AXIS_UNKNOWN)
        axisState = FF_AxisFrame_Build(&axis, px, py, pz, axisOrigin, axisDir)
                        ? FF_AXIS_VALID
                        : FF_AXIS_INVALID;
      if (axisState == FF_AXIS_INVALID)
        continue;

      float radius = L->radius;
      if (radius > 0.0f)
      {
        if (!(axis.distSq < radius * radius))
          continue;
        atten = FF_FalloffFactor(axis.dist / radius, L->falloff);
      }

      if (type == FORCE_RADIAL_AXIS)
      {
        acc_x = axis.radial.x * -strength;
        acc_y = axis.radial.y * -strength;
        acc_z = axis.radial.z * -strength;
      }
      else
      {
        float s = strength / (axis.dist + 1.0f);
        acc_x = axis.tangent.x * s;
        acc_y = axis.tangent.y * s;
        acc_z = axis.tangent.z * s;
      }
    }
    break;

    default:
      break;
    }

    total_ax += acc_x * atten;
    total_ay += acc_y * atten;
    total_az += acc_z * atten;
  }

  return (Vector3){total_ax, total_ay, total_az};
}

float ForceField_GetViscosityDamping(const ForceField *ff, float dt)
{
  if (!ff)
    return 1.0f;

  const int count = FF_ClampedLayerCount(ff);
  float rate = 0.0f;
  for (int i = 0; i < count; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    if (L->type == FORCE_VISCOSITY)
      rate += L->strength;
  }
  if (rate == 0.0f)
    return 1.0f;
  return expf(-rate * dt);
}

void ForceField_ResolveParticleContacts(const ForceField *ff, float radius,
                                        Vector3 *position, Vector3 *velocity)
{
  if (!ff || !position || !velocity)
    return;
  const int count = FF_ClampedLayerCount(ff);
  for (int i = 0; i < count; ++i)
  {
    const ForceLayer *layer = &ff->layers[i];
    if (layer->type == FORCE_RECEIVER_PLANE)
      ParticlePlane_Resolve(layer->origin, layer->direction, radius,
                            layer->strength, layer->falloff, position, velocity);
  }
}

/* ============================================================
 * GPU PACKING
 * ============================================================ */

void ForceField_PackGPU(const ForceField *ff, Vector3 axisOrigin,
                        Vector3 axisDir, ForceFieldGPU *out)
{
  if (!out)
    return;
  memset(out, 0, sizeof(*out));
  if (!ff)
    return;

  bool axisOk = false;
  float lenSq = axisDir.x * axisDir.x + axisDir.y * axisDir.y +
                axisDir.z * axisDir.z;
  if (lenSq >= FF_MIN_AXIS_LEN_SQ)
  {
    if (fabsf(lenSq - 1.0f) > 1e-5f)
    {
      float invLen = 1.0f / sqrtf(lenSq);
      axisDir.x *= invLen;
      axisDir.y *= invLen;
      axisDir.z *= invLen;
    }
    axisOk = true;
  }

  const int count = FF_ClampedLayerCount(ff);
  int packed = 0;
  for (int i = 0; i < count; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    if ((!(fabsf(L->strength) >= FF_MIN_STRENGTH) || !isfinite(L->strength)) &&
        L->type != FORCE_RECEIVER_PLANE)
      continue;

    Vector3 o = L->origin;
    Vector3 d = L->direction;
    if (L->type == FORCE_RADIAL_AXIS || L->type == FORCE_VORTEX_AXIS)
    {
      if (!axisOk)
        continue;
      o = axisOrigin;
      d = axisDir;
    }

    ForceLayerGPU *G = &out->layers[packed++];
    G->origin = (Vector4){o.x, o.y, o.z, 0.0f};
    G->direction = (Vector4){d.x, d.y, d.z, 0.0f};
    G->params0 = (Vector4){L->strength, L->radius, L->falloff, (float)L->type};
    G->params1 = (Vector4){L->noiseScale, L->noiseSpeed, 0.0f, 0.0f};
  }
  out->layerCount = packed;
}

/* ============================================================
 * WIND ZONE GLOBAL
 * ============================================================ */

static ForceField g_windZone;
static bool g_windZoneActive = false;

void WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq)
{
  ForceField_Clear(&g_windZone);

  float len = sqrtf(direction.x * direction.x + direction.y * direction.y +
                    direction.z * direction.z);
  if (len > FF_WINDZONE_MIN_DIR_LEN)
  {
    float invLen = 1.0f / len;
    direction.x *= invLen;
    direction.y *= invLen;
    direction.z *= invLen;
  }
  else
  {
    direction = (Vector3){0.0f, 0.0f, 0.0f};
  }

  ForceField_AddLayer(&g_windZone, (ForceLayer){
                                       .type = FORCE_GRAVITY_DIR,
                                       .direction = direction,
                                       .strength = strength});
  if (noiseAmp > 0.0f)
  {
    ForceField_AddLayer(&g_windZone, (ForceLayer){
                                         .type = FORCE_NOISE_CURL,
                                         .strength = noiseAmp,
                                         .noiseScale = noiseFreq,
                                         .noiseSpeed = FF_WINDZONE_CURL_SPEED});
  }
  g_windZoneActive = true;
}

void WindZone_Clear(void)
{
  ForceField_Clear(&g_windZone);
  g_windZoneActive = false;
}

bool WindZone_IsActive(void) { return g_windZoneActive; }

const ForceField *WindZone_GetField(void)
{
  return g_windZoneActive ? &g_windZone : NULL;
}

Vector3 WindZone_Evaluate(Vector3 pos, Vector3 vel, float time)
{
  if (!g_windZoneActive)
    return (Vector3){0.0f, 0.0f, 0.0f};
  return ForceField_Evaluate(&g_windZone, pos, vel, time,
                             (Vector3){0.0f, 0.0f, 0.0f},
                             (Vector3){0.0f, 0.0f, 0.0f});
}