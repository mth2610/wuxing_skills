#include "core/force_field.h"
#include "core/particles/particle_plane.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

/* ============================================================
 * TUNING CONSTANTS
 * ============================================================ */

/* A layer with |strength| below this is skipped (no visible contribution). */
#define FF_MIN_STRENGTH 1e-4f
/* GRAVITY_POINT: closer than sqrt(this) to the point -> force is zero
 * (avoids the singularity at the center). */
#define FF_MIN_POINT_DIST_SQ 1e-4f
/* Axis-type layers: a point closer than sqrt(this) to the axis has no
 * defined radial direction -> force is zero. */
#define FF_MIN_AXIS_DIST_SQ 1e-6f
/* axisDir shorter than sqrt(this) is not a valid axis. */
#define FF_MIN_AXIS_LEN_SQ 1e-6f

/* WIND layer: gust amplitude relative to the base strength, and the lattice
 * offset that decorrelates the Z gust from the X gust. */
#define FF_WIND_GUST_FACTOR 0.35f
#define FF_WIND_Z_OFFSET 53.9f

/* NOISE_PERLIN: per-axis lattice offsets that decorrelate X/Y/Z channels. */
#define FF_NOISE_OFFSET_A 17.7f
#define FF_NOISE_OFFSET_B 37.3f
#define FF_NOISE_OFFSET_C 73.1f

/* Curl noise: finite-difference step and the lattice offsets that decorrelate
 * the three Perlin potentials. */
#define FF_CURL_EPS 0.1f
#define FF_CURL_INV_2EPS (1.0f / (2.0f * FF_CURL_EPS))
#define FF_CURL_OFFSET_1 31.416f
#define FF_CURL_OFFSET_2 67.234f

/* WindZone: curl layer scroll speed and the shortest direction that can be
 * normalized. */
#define FF_WINDZONE_CURL_SPEED 0.4f
#define FF_WINDZONE_MIN_DIR_LEN 0.0001f

/* Perlin lattice wrap mask (table has 256 unique entries). */
#define FF_PERLIN_MASK 255

/* ============================================================
 * PERLIN NOISE 3D
 * ============================================================ */

/* The permutation table is a compile-time constant (doubled to 512 so the
 * +1 lookups never wrap) stored as bytes: 512 B instead of 2 KB keeps it in
 * a single handful of cache lines, and there is no lazy init, so no
 * data race when particles are updated from several threads. */
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

/* Fast floor (replaces floorf). Inputs are expected to be finite and within
 * int range, which is always true for particle-space coordinates. */
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

/* Perlin's 12 edge gradients (4 repeated to fill 16 slots), as coefficients
 * of (x, y, z). The classic implementation picks u/v and their signs with
 * data-dependent branches; the hash is effectively random, so those branches
 * mispredict constantly (about 6x slower than this table on the same input,
 * 8 gradients per Perlin sample). Every coefficient is -1, 0 or +1, so the
 * products are exact and the result is bit-identical to the branchy form. */
static const float FF_PERLIN_GRAD_TABLE[16][3] = {
    {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, -1.0f, 1.0f}, {0.0f, 1.0f, -1.0f}, {0.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 0.0f}, {0.0f, -1.0f, -1.0f}};

static inline float FF_PerlinGrad(int hash, float x, float y, float z)
{
  const float *g = FF_PERLIN_GRAD_TABLE[hash & 15];
  return g[0] * x + g[1] * y + g[2] * z;
}

/* Internal version so callers inside this file can be inlined. */
static inline float FF_PerlinImpl(float x, float y, float z)
{
  /* One floor per axis (the lattice index and the fraction share it). */
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

/* sx/sy/sz are already scaled by the spatial frequency. */
static inline Vector3 FF_CurlImpl(float sx, float sy, float sz)
{
  /* Shared bases: the same float additions the samples would redo anyway. */
  const float bx1 = sx + FF_CURL_OFFSET_1;
  const float by1 = sy + FF_CURL_OFFSET_1;
  const float bz1 = sz + FF_CURL_OFFSET_1;
  const float bx2 = sx + FF_CURL_OFFSET_2;
  const float by2 = sy + FF_CURL_OFFSET_2;
  const float bz2 = sz + FF_CURL_OFFSET_2;

  float psi1_pz = FF_PerlinImpl(sx, sy, sz + FF_CURL_EPS);
  float psi1_mz = FF_PerlinImpl(sx, sy, sz - FF_CURL_EPS);
  float psi1_py = FF_PerlinImpl(sx, sy + FF_CURL_EPS, sz);
  float psi1_my = FF_PerlinImpl(sx, sy - FF_CURL_EPS, sz);

  float psi2_px = FF_PerlinImpl(bx1 + FF_CURL_EPS, by1, bz1);
  float psi2_mx = FF_PerlinImpl(bx1 - FF_CURL_EPS, by1, bz1);
  float psi2_pz = FF_PerlinImpl(bx1, by1, bz1 + FF_CURL_EPS);
  float psi2_mz = FF_PerlinImpl(bx1, by1, bz1 - FF_CURL_EPS);

  float psi3_py = FF_PerlinImpl(bx2, by2 + FF_CURL_EPS, bz2);
  float psi3_my = FF_PerlinImpl(bx2, by2 - FF_CURL_EPS, bz2);
  float psi3_px = FF_PerlinImpl(bx2 + FF_CURL_EPS, by2, bz2);
  float psi3_mx = FF_PerlinImpl(bx2 - FF_CURL_EPS, by2, bz2);

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

/* layerCount comes from user-visible data; never trust it as an array bound. */
static inline int FF_ClampedLayerCount(const ForceField *ff)
{
  int n = ff->layerCount;
  if (n < 0)
    return 0;
  return n > FORCE_FIELD_MAX_LAYERS ? FORCE_FIELD_MAX_LAYERS : n;
}

/* Attenuation for t = dist / radius in [0, 1): constant, linear or squared. */
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

/* Geometry of a point relative to the dynamic axis, built at most once per
 * ForceField_Evaluate call and shared by every axis-type layer. */
typedef struct
{
  float distSq;    /* squared perpendicular distance to the axis */
  float dist;      /* perpendicular distance to the axis */
  Vector3 radial;  /* unit vector from the axis toward the point */
  Vector3 tangent; /* unit vector cross(axisDir, radial) */
} FF_AxisFrame;

#define FF_AXIS_UNKNOWN 0
#define FF_AXIS_VALID 1
#define FF_AXIS_INVALID (-1)

/* Deliberately NOT inlined. Inlined, Clang hoists the loop-invariant axis
 * normalization (sqrt + divide + projection) above the layer loop, so every
 * call pays for it even when the field has no axis-type layer (measured: about
 * +20 ns per ForceField_Evaluate call). As an opaque call it only runs when an
 * axis layer is actually met, and at most once per Evaluate. */
#if defined(__GNUC__) || defined(__clang__)
#define FF_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define FF_NOINLINE __declspec(noinline)
#else
#define FF_NOINLINE
#endif

/* Returns false when the axis is unusable (zero length, NaN) or the point is
 * (almost) on the axis. axisDir is normalized here, and the radial vector is
 * taken as pos - origin - dir * along. */
static FF_NOINLINE bool FF_AxisFrame_Build(FF_AxisFrame *f, float px, float py, float pz,
                                           Vector3 axisOrigin, Vector3 axisDir)
{
  float lenSq = axisDir.x * axisDir.x + axisDir.y * axisDir.y +
                axisDir.z * axisDir.z;
  if (!(lenSq >= FF_MIN_AXIS_LEN_SQ))
    return false;

  float invLen = 1.0f / sqrtf(lenSq);
  float adx = axisDir.x * invLen;
  float ady = axisDir.y * invLen;
  float adz = axisDir.z * invLen;

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

  /* Only read after FF_AxisFrame_Build succeeded (axisState == FF_AXIS_VALID);
   * the initializer just keeps compilers from flagging the lazy build. */
  FF_AxisFrame axis = {0.0f, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
  int axisState = FF_AXIS_UNKNOWN;

  for (int i = 0; i < count; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    const float strength = L->strength;
    const int type = L->type;

    if (fabsf(strength) < FF_MIN_STRENGTH)
      continue;

    /* Types that never produce an acceleration on the CPU path: skip them
     * before any attenuation work. */
    if (type == FORCE_VISCOSITY || type == FORCE_VECTOR_TEXTURE ||
        type == FORCE_RECEIVER_PLANE)
      continue;

    float atten = 1.0f;

    /* Spherical attenuation around origin (axis-type layers use the
     * perpendicular distance to the axis instead, handled in their case). */
    float relDist = -1.0f; /* >= 0 once the sqrt has been taken */
    if (type != FORCE_RADIAL_AXIS && type != FORCE_VORTEX_AXIS &&
        L->radius > 0.0f)
    {
      float dx = px - L->origin.x;
      float dy = py - L->origin.y;
      float dz = pz - L->origin.z;
      float distSq = dx * dx + dy * dy + dz * dz;
      float radius = L->radius;

      if (distSq >= radius * radius)
        continue;

      /* The sqrt is only needed when the falloff actually uses it. */
      if (L->falloff > 0.0f)
      {
        relDist = sqrtf(distSq);
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
      float dx = L->origin.x - px;
      float dy = L->origin.y - py;
      float dz = L->origin.z - pz;
      float distSq = dx * dx + dy * dy + dz * dz;
      if (distSq < FF_MIN_POINT_DIST_SQ)
        break;
      /* Reuse the attenuation sqrt when it was already taken. */
      float dist = (relDist >= 0.0f) ? relDist : sqrtf(distSq);
      float s = strength / (dist + 1.0f);
      float invDist = 1.0f / dist;
      acc_x = dx * invDist * s;
      acc_y = dy * invDist * s;
      acc_z = dz * invDist * s;
    }
    break;

    case FORCE_VORTEX:
    {
      /* axis = normalize(direction) */
      float ax = L->direction.x, ay = L->direction.y, az = L->direction.z;
      float alenSq = ax * ax + ay * ay + az * az;
      if (alenSq <= 0.0f)
        break;
      float invALen = 1.0f / sqrtf(alenSq);
      ax *= invALen;
      ay *= invALen;
      az *= invALen;

      float dx = px - L->origin.x;
      float dy = py - L->origin.y;
      float dz = pz - L->origin.z;

      float proj = dx * ax + dy * ay + dz * az;
      float rad_x = dx - ax * proj;
      float rad_y = dy - ay * proj;
      float rad_z = dz - az * proj;

      float distSq = rad_x * rad_x + rad_y * rad_y + rad_z * rad_z;
      if (distSq < FF_MIN_AXIS_DIST_SQ)
        break;

      float dist = sqrtf(distSq);
      /* cross(axis, radial) has length dist when radial is not normalized, so
       * dividing the magnitude by dist yields the unit tangent times
       * strength / (dist + 1) in one step. */
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
      float nx = FF_PerlinImpl(px * ns + t, py * ns, pz * ns);
      float nz = FF_PerlinImpl(px * ns + FF_WIND_Z_OFFSET, py * ns, pz * ns + t);

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
        if (axis.distSq >= radius * radius)
          continue;
        atten = FF_FalloffFactor(axis.dist / radius, L->falloff);
      }

      if (type == FORCE_RADIAL_AXIS)
      {
        /* positive strength pulls toward the axis */
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

  /* exp(-a*dt) * exp(-b*dt) == exp(-(a+b)*dt): one expf for all layers. */
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

  /* The shader assumes a unit axis, so normalize once here. An unusable axis
   * leaves axis-type layers out, like the CPU path where they are no-ops. */
  bool axisOk = false;
  float lenSq = axisDir.x * axisDir.x + axisDir.y * axisDir.y +
                axisDir.z * axisDir.z;
  if (lenSq >= FF_MIN_AXIS_LEN_SQ)
  {
    float invLen = 1.0f / sqrtf(lenSq);
    axisDir.x *= invLen;
    axisDir.y *= invLen;
    axisDir.z *= invLen;
    axisOk = true;
  }

  const int count = FF_ClampedLayerCount(ff);
  int packed = 0;
  for (int i = 0; i < count; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    if (fabsf(L->strength) < FF_MIN_STRENGTH && L->type != FORCE_RECEIVER_PLANE)
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
    /* too short (or NaN) to normalize: no directional wind */
    direction = (Vector3){0.0f, 0.0f, 0.0f};
  }

  /* The base wind is a constant acceleration. It used to be a FORCE_WIND
   * layer with noiseScale = noiseSpeed = 0, which sampled Perlin at a fixed
   * lattice point: two wasted noise evaluations per call and a constant
   * sideways (Z) push. The turbulence comes from the curl layer below. */
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