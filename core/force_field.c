#include "core/force_field.h"
#include <math.h>
#include <string.h>

// ============================================================
//  PERLIN NOISE 3D - FAST MATH OPTIMIZED
// ============================================================

// clang-format off
static const int PERM_SRC[256] = {
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
};
// clang-format on

static int perm[512];
static int perm_initialized = 0;

static void InitPermTable(void)
{
  for (int i = 0; i < 256; i++)
    perm[i] = perm[i + 256] = PERM_SRC[i];
  perm_initialized = 1;
}

// TỐI ƯU 1: Fast Floor (Thay thế cho floorf() nặng nề)
static inline int FastFloor(float x)
{
  int xi = (int)x;
  return x < xi ? xi - 1 : xi;
}

// TỐI ƯU 2: Ép Inline các hàm tính toán nhỏ
#define FADE(t) ((t) * (t) * (t) * ((t) * ((t) * 6.0f - 15.0f) + 10.0f))
#define LERP(a, b, t) ((a) + (t) * ((b) - (a)))

static inline float PerlinGrad(int hash, float x, float y, float z)
{
  int h = hash & 15;
  float u = (h < 8) ? x : y;
  float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
  return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float Noise_Perlin3D(float x, float y, float z)
{
  if (!perm_initialized)
    InitPermTable();

  int X = FastFloor(x) & 255;
  int Y = FastFloor(y) & 255;
  int Z = FastFloor(z) & 255;

  x -= (float)FastFloor(x);
  y -= (float)FastFloor(y);
  z -= (float)FastFloor(z);

  float u = FADE(x);
  float v = FADE(y);
  float w = FADE(z);

  int A = perm[X] + Y;
  int AA = perm[A] + Z;
  int AB = perm[A + 1] + Z;
  int B = perm[X + 1] + Y;
  int BA = perm[B] + Z;
  int BB = perm[B + 1] + Z;

  return LERP(
      LERP(LERP(PerlinGrad(perm[AA], x, y, z),
                PerlinGrad(perm[BA], x - 1, y, z), u),
           LERP(PerlinGrad(perm[AB], x, y - 1, z),
                PerlinGrad(perm[BB], x - 1, y - 1, z), u),
           v),
      LERP(LERP(PerlinGrad(perm[AA + 1], x, y, z - 1),
                PerlinGrad(perm[BA + 1], x - 1, y, z - 1), u),
           LERP(PerlinGrad(perm[AB + 1], x, y - 1, z - 1),
                PerlinGrad(perm[BB + 1], x - 1, y - 1, z - 1), u),
           v),
      w);
}

static inline float ValueHash(int ix, int iy, int iz)
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
  int ix = FastFloor(x);
  int iy = FastFloor(y);
  int iz = FastFloor(z);
  float fx = x - (float)ix;
  float fy = y - (float)iy;
  float fz = z - (float)iz;

  float ux = fx * fx * (3.0f - 2.0f * fx);
  float uy = fy * fy * (3.0f - 2.0f * fy);
  float uz = fz * fz * (3.0f - 2.0f * fz);

  float v000 = ValueHash(ix, iy, iz);
  float v100 = ValueHash(ix + 1, iy, iz);
  float v010 = ValueHash(ix, iy + 1, iz);
  float v110 = ValueHash(ix + 1, iy + 1, iz);
  float v001 = ValueHash(ix, iy, iz + 1);
  float v101 = ValueHash(ix + 1, iy, iz + 1);
  float v011 = ValueHash(ix, iy + 1, iz + 1);
  float v111 = ValueHash(ix + 1, iy + 1, iz + 1);

  float x00 = v000 + ux * (v100 - v000);
  float x10 = v010 + ux * (v110 - v010);
  float x01 = v001 + ux * (v101 - v001);
  float x11 = v011 + ux * (v111 - v011);
  float y0 = x00 + uy * (x10 - x00);
  float y1 = x01 + uy * (x11 - x01);
  return y0 + uz * (y1 - y0);
}

Vector3 Noise_Curl3D(float x, float y, float z, float scale)
{
  const float EPS = 0.1f;
  const float RINV = 1.0f / (2.0f * EPS);
  const float OFF1 = 31.416f;
  const float OFF2 = 67.234f;

  float sx = x * scale, sy = y * scale, sz = z * scale;

  float psi1_pz = Noise_Perlin3D(sx, sy, sz + EPS);
  float psi1_mz = Noise_Perlin3D(sx, sy, sz - EPS);
  float psi1_py = Noise_Perlin3D(sx, sy + EPS, sz);
  float psi1_my = Noise_Perlin3D(sx, sy - EPS, sz);

  float psi2_px = Noise_Perlin3D(sx + OFF1 + EPS, sy + OFF1, sz + OFF1);
  float psi2_mx = Noise_Perlin3D(sx + OFF1 - EPS, sy + OFF1, sz + OFF1);
  float psi2_pz = Noise_Perlin3D(sx + OFF1, sy + OFF1, sz + OFF1 + EPS);
  float psi2_mz = Noise_Perlin3D(sx + OFF1, sy + OFF1, sz + OFF1 - EPS);

  float psi3_py = Noise_Perlin3D(sx + OFF2, sy + OFF2 + EPS, sz + OFF2);
  float psi3_my = Noise_Perlin3D(sx + OFF2, sy + OFF2 - EPS, sz + OFF2);
  float psi3_px = Noise_Perlin3D(sx + OFF2 + EPS, sy + OFF2, sz + OFF2);
  float psi3_mx = Noise_Perlin3D(sx + OFF2 - EPS, sy + OFF2, sz + OFF2);

  return (Vector3){
      ((psi3_py - psi3_my) - (psi2_pz - psi2_mz)) * RINV,
      ((psi1_pz - psi1_mz) - (psi3_px - psi3_mx)) * RINV,
      ((psi2_px - psi2_mx) - (psi1_py - psi1_my)) * RINV // Đã sửa psi1_dy thành psi1_py ở đây
  };
}

void ForceField_Clear(ForceField *ff) { ff->layerCount = 0; }

bool ForceField_AddLayer(ForceField *ff, ForceLayer layer)
{
  if (ff->layerCount >= FORCE_FIELD_MAX_LAYERS)
    return false;
  ff->layers[ff->layerCount++] = layer;
  return true;
}

// TỐI ƯU 3: Bóc tách rãnh toàn bộ Struct Vector3 trong khối Evaluate
Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel,
                            float time, Vector3 axisOrigin, Vector3 axisDir)
{
  float total_ax = 0.0f, total_ay = 0.0f, total_az = 0.0f;
  float px = pos.x, py = pos.y, pz = pos.z;
  float vx = vel.x, vy = vel.y, vz = vel.z;

  float ax_orig = axisOrigin.x, ay_orig = axisOrigin.y, az_orig = axisOrigin.z;
  float ax_dir = axisDir.x, ay_dir = axisDir.y, az_dir = axisDir.z;

  for (int i = 0; i < ff->layerCount; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    float strength = L->strength;

    if (fabsf(strength) < 1e-4f)
      continue;

    float atten = 1.0f;
    int type = L->type;

    // Tính Attenuation Nội Tuyến (Thay vì gọi hàm CalcAttenuation)
    if (type != FORCE_RADIAL_AXIS && type != FORCE_VORTEX_AXIS)
    {
      if (L->radius > 0.0f)
      {
        float dx = px - L->origin.x;
        float dy = py - L->origin.y;
        float dz = pz - L->origin.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        float radSq = L->radius * L->radius;

        if (distSq >= radSq)
          continue;

        float dist = sqrtf(distSq);
        float t = dist / L->radius;
        if (L->falloff > 0.0f)
        {
          atten = (L->falloff <= 1.0f) ? (1.0f - t) : (1.0f - t) * (1.0f - t);
        }
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
      if (distSq < 1e-4f)
        break;
      float dist = sqrtf(distSq);
      float s = strength / (dist + 1.0f);
      float invDist = 1.0f / dist;
      acc_x = dx * invDist * s;
      acc_y = dy * invDist * s;
      acc_z = dz * invDist * s;
    }
    break;

    case FORCE_VORTEX:
    {
      // axis = normalize(direction)
      float ax = L->direction.x, ay = L->direction.y, az = L->direction.z;
      float alenSq = ax * ax + ay * ay + az * az;
      if (alenSq > 0.0f)
      {
        float invALen = 1.0f / sqrtf(alenSq);
        ax *= invALen;
        ay *= invALen;
        az *= invALen;
      }

      float dx = px - L->origin.x;
      float dy = py - L->origin.y;
      float dz = pz - L->origin.z;

      float proj = dx * ax + dy * ay + dz * az;
      float rad_x = dx - ax * proj;
      float rad_y = dy - ay * proj;
      float rad_z = dz - az * proj;

      float distSq = rad_x * rad_x + rad_y * rad_y + rad_z * rad_z;
      if (distSq < 1e-6f)
        break;

      float dist = sqrtf(distSq);
      float invDist = 1.0f / dist;
      rad_x *= invDist;
      rad_y *= invDist;
      rad_z *= invDist;

      // Cross product (axis x radial) = tangent
      float tx = ay * rad_z - az * rad_y;
      float ty = az * rad_x - ax * rad_z;
      float tz = ax * rad_y - ay * rad_x;

      float s = strength / (dist + 1.0f);
      acc_x = tx * s;
      acc_y = ty * s;
      acc_z = tz * s;
    }
    break;

    case FORCE_WIND:
    {
      float t = time * L->noiseSpeed;
      float ns = L->noiseScale;
      float nx = Noise_Perlin3D(px * ns + t, py * ns, pz * ns);
      float nz = Noise_Perlin3D(px * ns + 53.9f, py * ns, pz * ns + t);

      float s = strength;
      acc_x = (L->direction.x * s) + (nx * s * 0.35f);
      acc_y = (L->direction.y * s);
      acc_z = (L->direction.z * s) + (nz * s * 0.35f);
    }
    break;

    case FORCE_NOISE_PERLIN:
    {
      float t = time * L->noiseSpeed;
      float ns = L->noiseScale;
      float px_s = px * ns, py_s = py * ns, pz_s = pz * ns;

      acc_x = Noise_Perlin3D(px_s + t, py_s + 17.7f, pz_s + 17.7f) * strength;
      acc_y = Noise_Perlin3D(px_s + 37.3f, py_s + t, pz_s + 37.3f) * strength;
      acc_z = Noise_Perlin3D(px_s + 73.1f, py_s + 73.1f, pz_s + t) * strength;
    }
    break;

    case FORCE_NOISE_CURL:
    {
      float t = time * L->noiseSpeed;
      Vector3 curl = Noise_Curl3D(px * L->noiseScale + t, py * L->noiseScale, pz * L->noiseScale + t, 1.0f);
      acc_x = curl.x * strength;
      acc_y = curl.y * strength;
      acc_z = curl.z * strength;
    }
    break;

    case FORCE_DRAG:
      acc_x = vx * -strength;
      acc_y = vy * -strength;
      acc_z = vz * -strength;
      break;

    case FORCE_RADIAL_AXIS:
    {
      float aDirLenSq = ax_dir * ax_dir + ay_dir * ay_dir + az_dir * az_dir;
      if (aDirLenSq < 1e-6f)
        break;

      float toPt_x = px - ax_orig;
      float toPt_y = py - ay_orig;
      float toPt_z = pz - az_orig;

      float alongAxis = toPt_x * ax_dir + toPt_y * ay_dir + toPt_z * az_dir;
      float close_x = ax_orig + ax_dir * alongAxis;
      float close_y = ay_orig + ay_dir * alongAxis;
      float close_z = az_orig + az_dir * alongAxis;

      float rad_x = px - close_x;
      float rad_y = py - close_y;
      float rad_z = pz - close_z;

      float perpDistSq = rad_x * rad_x + rad_y * rad_y + rad_z * rad_z;

      if (L->radius > 0.0f && perpDistSq >= L->radius * L->radius)
        break;
      if (perpDistSq < 1e-6f)
        break;

      float perpDist = sqrtf(perpDistSq);

      if (L->radius <= 0.0f)
      {
        atten = 1.0f;
      }
      else
      {
        float t = perpDist / L->radius;
        if (L->falloff <= 0.0f)
          atten = 1.0f;
        else if (L->falloff <= 1.0f)
          atten = 1.0f - t;
        else
          atten = (1.0f - t) * (1.0f - t);
      }

      float invD = -1.0f / perpDist;
      acc_x = rad_x * invD * strength;
      acc_y = rad_y * invD * strength;
      acc_z = rad_z * invD * strength;
    }
    break;

    case FORCE_VORTEX_AXIS:
    {
      float aDirLenSq = ax_dir * ax_dir + ay_dir * ay_dir + az_dir * az_dir;
      if (aDirLenSq < 1e-6f)
        break;

      float toPt_x = px - ax_orig;
      float toPt_y = py - ay_orig;
      float toPt_z = pz - az_orig;

      float alongAxis = toPt_x * ax_dir + toPt_y * ay_dir + toPt_z * az_dir;
      float close_x = ax_orig + ax_dir * alongAxis;
      float close_y = ay_orig + ay_dir * alongAxis;
      float close_z = az_orig + az_dir * alongAxis;

      float rad_x = px - close_x;
      float rad_y = py - close_y;
      float rad_z = pz - close_z;

      float perpDistSq = rad_x * rad_x + rad_y * rad_y + rad_z * rad_z;

      if (L->radius > 0.0f && perpDistSq >= L->radius * L->radius)
      {
        atten = 0.0f;
        break;
      }
      if (perpDistSq < 1e-6f)
        break;

      float perpDist = sqrtf(perpDistSq);

      if (L->radius <= 0.0f)
      {
        atten = 1.0f;
      }
      else
      {
        float t = perpDist / L->radius;
        if (L->falloff <= 0.0f)
          atten = 1.0f;
        else if (L->falloff <= 1.0f)
          atten = 1.0f - t;
        else
          atten = (1.0f - t) * (1.0f - t);
      }

      // cross(axisDir, radialVec)
      float tx = ay_dir * rad_z - az_dir * rad_y;
      float ty = az_dir * rad_x - ax_dir * rad_z;
      float tz = ax_dir * rad_y - ay_dir * rad_x;

      float tLenSq = tx * tx + ty * ty + tz * tz;
      if (tLenSq > 0.0f)
      {
        float invTLen = 1.0f / sqrtf(tLenSq);
        tx *= invTLen;
        ty *= invTLen;
        tz *= invTLen;
      }

      float s = strength / (perpDist + 1.0f);
      acc_x = tx * s;
      acc_y = ty * s;
      acc_z = tz * s;
    }
    break;

    case FORCE_VISCOSITY:
    case FORCE_VECTOR_TEXTURE:
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
  float factor = 1.0f;
  for (int i = 0; i < ff->layerCount; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    if (L->type == FORCE_VISCOSITY)
    {
      factor *= expf(-L->strength * dt);
    }
  }
  return factor;
}

// ============================================================
// WIND ZONE GLOBAL
// ============================================================
static ForceField g_windZone;
static bool g_windZoneActive = false;

void WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq)
{
  ForceField_Clear(&g_windZone);
  float len = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
  if (len > 0.0001f)
  {
    direction.x /= len;
    direction.y /= len;
    direction.z /= len;
  }
  ForceField_AddLayer(&g_windZone, (ForceLayer){
                                       .type = FORCE_WIND,
                                       .direction = direction,
                                       .strength = strength});
  if (noiseAmp > 0.0f)
  {
    ForceField_AddLayer(&g_windZone, (ForceLayer){
                                         .type = FORCE_NOISE_CURL,
                                         .strength = noiseAmp,
                                         .noiseScale = noiseFreq,
                                         .noiseSpeed = 0.4f});
  }
  g_windZoneActive = true;
}

void WindZone_Clear(void)
{
  ForceField_Clear(&g_windZone);
  g_windZoneActive = false;
}

bool WindZone_IsActive(void) { return g_windZoneActive; }

Vector3 WindZone_Evaluate(Vector3 pos, Vector3 vel, float time)
{
  if (!g_windZoneActive)
    return (Vector3){0};
  return ForceField_Evaluate(&g_windZone, pos, vel, time, (Vector3){0}, (Vector3){0});
}

void ForceField_PackGPU(const ForceField *ff, Vector3 axisOrigin,
                        Vector3 axisDir, ForceFieldGPU *out)
{
  memset(out, 0, sizeof(*out));

  int packed = 0;
  for (int i = 0; i < ff->layerCount; i++)
  {
    const ForceLayer *L = &ff->layers[i];
    if (fabsf(L->strength) < 1e-4f)
      continue;

    ForceLayerGPU *G = &out->layers[packed++];
    Vector3 o = L->origin;
    Vector3 d = L->direction;
    if (L->type == FORCE_RADIAL_AXIS || L->type == FORCE_VORTEX_AXIS)
    {
      o = axisOrigin;
      d = axisDir;
    }

    G->origin = (Vector4){o.x, o.y, o.z, 0.0f};
    G->direction = (Vector4){d.x, d.y, d.z, 0.0f};
    G->params0 = (Vector4){L->strength, L->radius, L->falloff, (float)L->type};
    G->params1 = (Vector4){L->noiseScale, L->noiseSpeed, 0.0f, 0.0f};
  }
  out->layerCount = packed;
}