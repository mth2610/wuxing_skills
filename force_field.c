#include "force_field.h"
#include <math.h>
#include <string.h>

// ============================================================
//  PERLIN NOISE 3D
//  Implement chuẩn Ken Perlin (permutation table + gradient hash)
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

static void InitPermTable(void) {
  for (int i = 0; i < 256; i++)
    perm[i] = perm[i + 256] = PERM_SRC[i];
  perm_initialized = 1;
}

static float PerlinFade(float t) {
  return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float PerlinLerp(float a, float b, float t) { return a + t * (b - a); }

static float PerlinGrad(int hash, float x, float y, float z) {
  int h = hash & 15;
  float u = (h < 8) ? x : y;
  float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
  return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float Noise_Perlin3D(float x, float y, float z) {
  if (!perm_initialized)
    InitPermTable();

  int X = (int)floorf(x) & 255;
  int Y = (int)floorf(y) & 255;
  int Z = (int)floorf(z) & 255;

  x -= floorf(x);
  y -= floorf(y);
  z -= floorf(z);

  float u = PerlinFade(x);
  float v = PerlinFade(y);
  float w = PerlinFade(z);

  int A = perm[X] + Y;
  int AA = perm[A] + Z;
  int AB = perm[A + 1] + Z;
  int B = perm[X + 1] + Y;
  int BA = perm[B] + Z;
  int BB = perm[B + 1] + Z;

  return PerlinLerp(
      PerlinLerp(PerlinLerp(PerlinGrad(perm[AA], x, y, z),
                            PerlinGrad(perm[BA], x - 1, y, z), u),
                 PerlinLerp(PerlinGrad(perm[AB], x, y - 1, z),
                            PerlinGrad(perm[BB], x - 1, y - 1, z), u),
                 v),
      PerlinLerp(PerlinLerp(PerlinGrad(perm[AA + 1], x, y, z - 1),
                            PerlinGrad(perm[BA + 1], x - 1, y, z - 1), u),
                 PerlinLerp(PerlinGrad(perm[AB + 1], x, y - 1, z - 1),
                            PerlinGrad(perm[BB + 1], x - 1, y - 1, z - 1), u),
                 v),
      w);
}

static float ValueHash(int ix, int iy, int iz) {
  unsigned int h = (unsigned int)ix * 1664525u +
                   (unsigned int)iy * 1013904223u +
                   (unsigned int)iz * 22695477u;
  h ^= h >> 16;
  h *= 0x45d9f3bu;
  h ^= h >> 16;
  return (float)(h & 0xFFFFu) * (1.0f / 65535.0f); // [0, 1]
}

float Noise_Value3D(float x, float y, float z) {
  int ix = (int)floorf(x);
  int iy = (int)floorf(y);
  int iz = (int)floorf(z);
  float fx = x - floorf(x);
  float fy = y - floorf(y);
  float fz = z - floorf(z);

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

// ============================================================
//  FIX #3: Curl Noise 3D — divergence-free đúng chuẩn
//
//  Phiên bản cũ (SAI):
//    curl.x = -∂A/∂z        ← đúng (từ ψ=(0,A,0))
//    curl.y = +∂B/∂y        ← SAI: ∂B/∂y không phải thành phần curl nào cả
//    curl.z = +∂A/∂x        ← đúng (từ ψ=(0,A,0))
//  → div ≠ 0 vì ∂(curl.y)/∂y = ∂²B/∂y² ≠ 0
//
//  Phiên bản mới (ĐÚNG):
//    Dùng vector potential ψ = (ψ₁, ψ₂, ψ₃) — 3 noise field độc lập.
//    curl(ψ)_x = ∂ψ₃/∂y − ∂ψ₂/∂z
//    curl(ψ)_y = ∂ψ₁/∂z − ∂ψ₃/∂x
//    curl(ψ)_z = ∂ψ₂/∂x − ∂ψ₁/∂y
//    div(curl(ψ)) = 0 theo định lý (curl luôn divergence-free).
//  → 12 sample thay vì 6, nhưng đảm bảo tính đúng của trường.
// ============================================================

Vector3 Noise_Curl3D(float x, float y, float z, float scale) {
  const float EPS = 0.1f;
  const float RINV = 1.0f / (2.0f * EPS);

  // Hai offset lớn để 3 noise field ψ₁, ψ₂, ψ₃ decorrelated với nhau
  const float OFF1 = 31.416f;
  const float OFF2 = 67.234f;

  float sx = x * scale;
  float sy = y * scale;
  float sz = z * scale;

  // ψ₁ tại (sx, sy, sz) — cần ∂/∂z và ∂/∂y
  float psi1_pz = Noise_Perlin3D(sx, sy, sz + EPS);
  float psi1_mz = Noise_Perlin3D(sx, sy, sz - EPS);
  float psi1_py = Noise_Perlin3D(sx, sy + EPS, sz);
  float psi1_my = Noise_Perlin3D(sx, sy - EPS, sz);

  // ψ₂ tại (sx+OFF1, sy+OFF1, sz+OFF1) — cần ∂/∂x và ∂/∂z
  float psi2_px = Noise_Perlin3D(sx + OFF1 + EPS, sy + OFF1, sz + OFF1);
  float psi2_mx = Noise_Perlin3D(sx + OFF1 - EPS, sy + OFF1, sz + OFF1);
  float psi2_pz = Noise_Perlin3D(sx + OFF1, sy + OFF1, sz + OFF1 + EPS);
  float psi2_mz = Noise_Perlin3D(sx + OFF1, sy + OFF1, sz + OFF1 - EPS);

  // ψ₃ tại (sx+OFF2, sy+OFF2, sz+OFF2) — cần ∂/∂y và ∂/∂x
  float psi3_py = Noise_Perlin3D(sx + OFF2, sy + OFF2 + EPS, sz + OFF2);
  float psi3_my = Noise_Perlin3D(sx + OFF2, sy + OFF2 - EPS, sz + OFF2);
  float psi3_px = Noise_Perlin3D(sx + OFF2 + EPS, sy + OFF2, sz + OFF2);
  float psi3_mx = Noise_Perlin3D(sx + OFF2 - EPS, sy + OFF2, sz + OFF2);

  float dpsi3_dy = (psi3_py - psi3_my) * RINV;
  float dpsi2_dz = (psi2_pz - psi2_mz) * RINV;
  float dpsi1_dz = (psi1_pz - psi1_mz) * RINV;
  float dpsi3_dx = (psi3_px - psi3_mx) * RINV;
  float dpsi2_dx = (psi2_px - psi2_mx) * RINV;
  float dpsi1_dy = (psi1_py - psi1_my) * RINV;

  return (Vector3){
      dpsi3_dy - dpsi2_dz, // curl_x = ∂ψ₃/∂y − ∂ψ₂/∂z
      dpsi1_dz - dpsi3_dx, // curl_y = ∂ψ₁/∂z − ∂ψ₃/∂x
      dpsi2_dx - dpsi1_dy, // curl_z = ∂ψ₂/∂x − ∂ψ₁/∂y
  };
}

void ForceField_Clear(ForceField *ff) { ff->layerCount = 0; }

bool ForceField_AddLayer(ForceField *ff, ForceLayer layer) {
  if (ff->layerCount >= FORCE_FIELD_MAX_LAYERS)
    return false;
  ff->layers[ff->layerCount++] = layer;
  return true;
}

static float CalcAttenuation(const ForceLayer *L, Vector3 pos) {
  if (L->radius <= 0.0f)
    return 1.0f;

  float distSq = Vector3LengthSqr(Vector3Subtract(pos, L->origin));
  if (distSq >= L->radius * L->radius)
    return 0.0f;

  float t = sqrtf(distSq) / L->radius;
  if (L->falloff <= 0.0f)
    return 1.0f;
  if (L->falloff <= 1.0f)
    return 1.0f - t;
  return (1.0f - t) * (1.0f - t);
}

Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel,
                            float time, Vector3 axisOrigin, Vector3 axisDir) {
  Vector3 totalAcc = {0.0f, 0.0f, 0.0f};

  for (int i = 0; i < ff->layerCount; i++) {
    const ForceLayer *L = &ff->layers[i];

    float atten = 1.0f;

    // =====================================================================
    // FIX #2: FORCE_VORTEX_AXIS phải được exclude khỏi CalcAttenuation
    // cùng với FORCE_RADIAL_AXIS.
    //
    // Lý do: Cả hai kiểu này đều dùng axisOrigin/axisDir (hình trụ),
    // KHÔNG dùng L->origin (hình cầu). CalcAttenuation tính khoảng cách
    // Euclid từ L->origin — nếu áp dụng cho axis-type, một particle nằm
    // đúng trong vùng trụ nhưng xa L->origin (theo đường thẳng) sẽ bị
    // continue và bỏ qua hoàn toàn trước khi vào switch.
    // Hai kiểu axis-type tự tính attenuation theo perpDist bên trong case.
    // =====================================================================
    if (L->type != FORCE_RADIAL_AXIS && L->type != FORCE_VORTEX_AXIS) {
      atten = CalcAttenuation(L, pos);
      if (atten <= 0.0f)
        continue;
    }

    Vector3 acc = {0.0f, 0.0f, 0.0f};

    switch (L->type) {

    case FORCE_GRAVITY_DIR: {
      acc = Vector3Scale(L->direction, L->strength);
    } break;

    case FORCE_GRAVITY_POINT: {
      Vector3 toOrigin = Vector3Subtract(L->origin, pos);
      float distSq = Vector3LengthSqr(toOrigin);
      if (distSq < 1e-4f)
        break;
      float dist = sqrtf(distSq);
      float s = L->strength / (dist + 1.0f);
      acc = Vector3Scale(Vector3Scale(toOrigin, 1.0f / dist), s);
    } break;

    case FORCE_VORTEX: {
      Vector3 axis = Vector3Normalize(L->direction);
      Vector3 toPos = Vector3Subtract(pos, L->origin);

      float proj = Vector3DotProduct(toPos, axis);
      Vector3 radial = Vector3Subtract(toPos, Vector3Scale(axis, proj));
      float dist = Vector3Length(radial);
      if (dist < 1e-3f)
        break;

      Vector3 tangent = Vector3Normalize(Vector3CrossProduct(axis, radial));
      float s = L->strength / (dist + 1.0f);
      acc = Vector3Scale(tangent, s);
    } break;

    case FORCE_WIND: {
      float t = time * L->noiseSpeed;
      float nx = Noise_Perlin3D(pos.x * L->noiseScale + t,
                                pos.y * L->noiseScale, pos.z * L->noiseScale);
      float nz =
          Noise_Perlin3D(pos.x * L->noiseScale + 53.9f, pos.y * L->noiseScale,
                         pos.z * L->noiseScale + t);
      Vector3 base = Vector3Scale(L->direction, L->strength);
      Vector3 perturb = {nx * L->strength * 0.35f, 0.0f,
                         nz * L->strength * 0.35f};
      acc = Vector3Add(base, perturb);
    } break;

    case FORCE_NOISE_PERLIN: {
      float t = time * L->noiseSpeed;
      float nx = Noise_Perlin3D(pos.x * L->noiseScale + t,
                                pos.y * L->noiseScale + 17.7f,
                                pos.z * L->noiseScale + 17.7f);
      float ny = Noise_Perlin3D(pos.x * L->noiseScale + 37.3f,
                                pos.y * L->noiseScale + t,
                                pos.z * L->noiseScale + 37.3f);
      float nz = Noise_Perlin3D(pos.x * L->noiseScale + 73.1f,
                                pos.y * L->noiseScale + 73.1f,
                                pos.z * L->noiseScale + t);
      acc = (Vector3){nx * L->strength, ny * L->strength, nz * L->strength};
    } break;

    case FORCE_NOISE_CURL: {
      float t = time * L->noiseSpeed;
      Vector3 curl =
          Noise_Curl3D(pos.x * L->noiseScale + t, pos.y * L->noiseScale,
                       pos.z * L->noiseScale + t, 1.0f);
      acc = Vector3Scale(curl, L->strength);
    } break;

    case FORCE_DRAG: {
      acc = Vector3Scale(vel, -L->strength);
    } break;

    case FORCE_RADIAL_AXIS: {
      if (Vector3LengthSqr(axisDir) < 1e-6f)
        break;

      Vector3 toPoint = Vector3Subtract(pos, axisOrigin);
      float alongAxis = Vector3DotProduct(toPoint, axisDir);
      Vector3 closestOnAxis =
          Vector3Add(axisOrigin, Vector3Scale(axisDir, alongAxis));
      Vector3 radialVec = Vector3Subtract(pos, closestOnAxis);
      float perpDistSq = Vector3LengthSqr(radialVec);

      if (L->radius > 0.0f && perpDistSq >= L->radius * L->radius)
        break;

      if (perpDistSq < 1e-6f)
        break;

      float perpDist = sqrtf(perpDistSq);

      if (L->radius <= 0.0f) {
        atten = 1.0f;
      } else {
        float t = perpDist / L->radius;
        if (L->falloff <= 0.0f)
          atten = 1.0f;
        else if (L->falloff <= 1.0f)
          atten = 1.0f - t;
        else
          atten = (1.0f - t) * (1.0f - t);
      }

      // ==================================================================
      // FIX #1: Dấu ngược chiều — hướng tâm (centripetal) vs li tâm
      //
      // radialVec trỏ TỪ trục → hạt (hướng ra ngoài = li tâm).
      // Phiên bản cũ (SAI):
      //   radialDir = radialVec / perpDist   (trỏ ra ngoài)
      //   acc = radialDir * strength
      //   → strength > 0 = đẩy ra xa trục (li tâm) — TRÁI với .h
      //
      // Phiên bản mới (ĐÚNG, khớp với header):
      //   centripetal = -radialVec / perpDist (trỏ vào trong, về phía trục)
      //   acc = centripetal * strength
      //   → strength > 0 = hút vào trục (hướng tâm) ✓
      //   → strength < 0 = đẩy ra khỏi trục (li tâm)
      // ==================================================================
      Vector3 centripetal = Vector3Scale(radialVec, -1.0f / perpDist);
      acc = Vector3Scale(centripetal, L->strength);
    } break;

    case FORCE_VORTEX_AXIS: {
      if (Vector3LengthSqr(axisDir) < 1e-6f)
        break;

      Vector3 toPoint = Vector3Subtract(pos, axisOrigin);
      float alongAxis = Vector3DotProduct(toPoint, axisDir);
      Vector3 closestOnAxis =
          Vector3Add(axisOrigin, Vector3Scale(axisDir, alongAxis));
      Vector3 radialVec = Vector3Subtract(pos, closestOnAxis);
      float perpDistSq = Vector3LengthSqr(radialVec);

      if (L->radius > 0.0f && perpDistSq >= L->radius * L->radius) {
        atten = 0.0f;
        break;
      }

      if (perpDistSq < 1e-6f)
        break;

      float perpDist = sqrtf(perpDistSq);

      if (L->radius <= 0.0f) {
        atten = 1.0f;
      } else {
        float t = perpDist / L->radius;
        if (L->falloff <= 0.0f)
          atten = 1.0f;
        else if (L->falloff <= 1.0f)
          atten = 1.0f - t;
        else
          atten = (1.0f - t) * (1.0f - t);
      }

      Vector3 tangent =
          Vector3Normalize(Vector3CrossProduct(axisDir, radialVec));

      float s = L->strength / (perpDist + 1.0f);
      acc = Vector3Scale(tangent, s);
    } break;

    case FORCE_VISCOSITY:
    default:
      break;
    } // end switch

    totalAcc = Vector3Add(totalAcc, Vector3Scale(acc, atten));
  }

  return totalAcc;
}

float ForceField_GetViscosityDamping(const ForceField *ff, float dt) {
  float factor = 1.0f;
  for (int i = 0; i < ff->layerCount; i++) {
    const ForceLayer *L = &ff->layers[i];
    if (L->type == FORCE_VISCOSITY) {
      factor *= expf(-L->strength * dt);
    }
  }
  return factor;
}

void ForceField_PackGPU(const ForceField *ff, ForceFieldGPU *out) {
  memset(out, 0, sizeof(*out));
  out->layerCount = ff->layerCount;

  for (int i = 0; i < ff->layerCount; i++) {
    const ForceLayer *L = &ff->layers[i];
    ForceLayerGPU *G = &out->layers[i];

    G->origin = (Vector4){L->origin.x, L->origin.y, L->origin.z, 0.0f};
    G->direction =
        (Vector4){L->direction.x, L->direction.y, L->direction.z, 0.0f};
    G->params0 = (Vector4){L->strength, L->radius, L->falloff, (float)L->type};
    G->params1 = (Vector4){L->noiseScale, L->noiseSpeed, 0.0f, 0.0f};
  }
}