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

// Quintic fade: 6t^5 - 15t^4 + 10t^3  (Perlin 2002, đạo hàm liên tục bậc 2)
static float PerlinFade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float PerlinLerp(float a, float b, float t) { return a + t * (b - a); }

// Gradient hash: 12 hướng gradient unit cube
static float PerlinGrad(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = (h < 8) ? x : y;
    float v = (h < 4) ? y : ((h == 12 || h == 14) ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float Noise_Perlin3D(float x, float y, float z) {
    if (!perm_initialized) InitPermTable();

    int X = (int)floorf(x) & 255;
    int Y = (int)floorf(y) & 255;
    int Z = (int)floorf(z) & 255;

    x -= floorf(x);
    y -= floorf(y);
    z -= floorf(z);

    float u = PerlinFade(x);
    float v = PerlinFade(y);
    float w = PerlinFade(z);

    int A  = perm[X]     + Y;
    int AA = perm[A]     + Z;
    int AB = perm[A + 1] + Z;
    int B  = perm[X + 1] + Y;
    int BA = perm[B]     + Z;
    int BB = perm[B + 1] + Z;

    return PerlinLerp(
        PerlinLerp(
            PerlinLerp(PerlinGrad(perm[AA],     x,   y,   z  ),
                       PerlinGrad(perm[BA],     x-1, y,   z  ), u),
            PerlinLerp(PerlinGrad(perm[AB],     x,   y-1, z  ),
                       PerlinGrad(perm[BB],     x-1, y-1, z  ), u),
            v),
        PerlinLerp(
            PerlinLerp(PerlinGrad(perm[AA + 1], x,   y,   z-1),
                       PerlinGrad(perm[BA + 1], x-1, y,   z-1), u),
            PerlinLerp(PerlinGrad(perm[AB + 1], x,   y-1, z-1),
                       PerlinGrad(perm[BB + 1], x-1, y-1, z-1), u),
            v),
        w);
}

// ============================================================
//  VALUE NOISE 3D
//  Hash 3 int → float [0,1], trilinear interpolation
//  Nhanh hơn Perlin ~2x, pattern "blocky" hơn
// ============================================================

static float ValueHash(int ix, int iy, int iz) {
    // Wang hash kết hợp 3 chiều
    unsigned int h = (unsigned int)(ix * 1664525 + iy * 1013904223 + iz * 22695477);
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;
    return (float)(h & 0xFFFFu) * (1.0f / 65535.0f); // [0, 1]
}

float Noise_Value3D(float x, float y, float z) {
    int   ix = (int)floorf(x);
    int   iy = (int)floorf(y);
    int   iz = (int)floorf(z);
    float fx = x - floorf(x);
    float fy = y - floorf(y);
    float fz = z - floorf(z);

    // Smoothstep cho interpolation mượt hơn lerp tuyến tính
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float uz = fz * fz * (3.0f - 2.0f * fz);

    float v000 = ValueHash(ix,     iy,     iz    );
    float v100 = ValueHash(ix + 1, iy,     iz    );
    float v010 = ValueHash(ix,     iy + 1, iz    );
    float v110 = ValueHash(ix + 1, iy + 1, iz    );
    float v001 = ValueHash(ix,     iy,     iz + 1);
    float v101 = ValueHash(ix + 1, iy,     iz + 1);
    float v011 = ValueHash(ix,     iy + 1, iz + 1);
    float v111 = ValueHash(ix + 1, iy + 1, iz + 1);

    float x00 = v000 + ux * (v100 - v000);
    float x10 = v010 + ux * (v110 - v010);
    float x01 = v001 + ux * (v101 - v001);
    float x11 = v011 + ux * (v111 - v011);
    float y0  = x00  + uy * (x10  - x00 );
    float y1  = x01  + uy * (x11  - x01 );
    return y0 + uz * (y1 - y0);
}

// ============================================================
//  CURL NOISE 3D — xấp xỉ 6 sample
//
//  Ý tưởng (Bridson 2007): curl(F) = (dFz/dy - dFy/dz, dFx/dz - dFz/dx, dFy/dx - dFx/dy)
//  Xấp xỉ: dùng 2D curl trên mặt phẳng XZ + field thứ 2 (offset lớn) cho trục Y,
//  tổng cộng 6 sample Perlin — đủ để tạo swirling đẹp cho particles.
//
//  Output: vector khoảng [-1, 1] mỗi trục (divergence-free trên XZ plane)
// ============================================================

Vector3 Noise_Curl3D(float x, float y, float z, float scale) {
    const float EPS  = 0.1f;           // khoảng xấp xỉ đạo hàm số
    const float RINV = 1.0f / (2.0f * EPS); // = 0.5/EPS
    // Offset lớn để field A và B thống kê độc lập (dùng chung hàm Perlin)
    const float OFF  = 31.416f;

    float sx = x * scale;
    float sy = y * scale;
    float sz = z * scale;

    // Field A trên mặt phẳng XZ: lấy dA/dx và dA/dz
    float Apx = Noise_Perlin3D(sx + EPS, sy, sz      );
    float Amx = Noise_Perlin3D(sx - EPS, sy, sz      );
    float Apz = Noise_Perlin3D(sx,       sy, sz + EPS);
    float Amz = Noise_Perlin3D(sx,       sy, sz - EPS);

    // Field B (offset lớn): lấy dB/dy — tạo Y variation độc lập
    float Bpy = Noise_Perlin3D(sx + OFF, sy + EPS, sz + OFF);
    float Bmy = Noise_Perlin3D(sx + OFF, sy - EPS, sz + OFF);

    // curl XZ plane: curlX = -dA/dz, curlZ = +dA/dx
    // curlY từ field B (swirling quanh Y)
    return (Vector3){
        -(Apz - Amz) * RINV,   // -dA/dz
        (Bpy - Bmy) * RINV,    // dB/dy
        (Apx - Amx) * RINV,    //  dA/dx
    };
}

// ============================================================
//  FORCE FIELD API
// ============================================================

void ForceField_Clear(ForceField *ff) {
    ff->layerCount = 0;
}

bool ForceField_AddLayer(ForceField *ff, ForceLayer layer) {
    if (ff->layerCount >= FORCE_FIELD_MAX_LAYERS) return false;
    ff->layers[ff->layerCount++] = layer;
    return true;
}

// Tính attenuation trong [0, 1] dựa trên khoảng cách và falloff
static float CalcAttenuation(const ForceLayer *L, Vector3 pos) {
    if (L->radius <= 0.0f) return 1.0f; // vô cực, luôn full strength

    float distSq = Vector3LengthSqr(Vector3Subtract(pos, L->origin));
    if (distSq >= L->radius * L->radius) return 0.0f; // ngoài vùng

    float t = sqrtf(distSq) / L->radius; // [0, 1]
    if (L->falloff <= 0.0f) return 1.0f;
    if (L->falloff <= 1.0f) return 1.0f - t;             // tuyến tính
    return (1.0f - t) * (1.0f - t);                       // bình phương
}

Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel, float time) {
    Vector3 totalAcc = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < ff->layerCount; i++) {
        const ForceLayer *L = &ff->layers[i];

        float atten = CalcAttenuation(L, pos);
        if (atten <= 0.0f) continue;

        Vector3 acc = {0.0f, 0.0f, 0.0f};

        switch (L->type) {

            // ---- Trọng lực / gia tốc theo hướng cố định ----
            case FORCE_GRAVITY_DIR: {
                acc = Vector3Scale(L->direction, L->strength);
            } break;

            // ---- Hút / đẩy vào một điểm ----
            case FORCE_GRAVITY_POINT: {
                Vector3 toOrigin = Vector3Subtract(L->origin, pos);
                float   distSq   = Vector3LengthSqr(toOrigin);
                if (distSq < 1e-4f) break;
                float dist = sqrtf(distSq);
                // Chuẩn hóa rồi scale; +1 tránh kỳ dị tại gốc
                float s = L->strength / (dist + 1.0f);
                acc = Vector3Scale(Vector3Scale(toOrigin, 1.0f / dist), s);
            } break;

            // ---- Xoáy quanh trục (origin + direction = trục) ----
            case FORCE_VORTEX: {
                Vector3 axis  = Vector3Normalize(L->direction);
                Vector3 toPos = Vector3Subtract(pos, L->origin);

                // Chiếu toPos lên mặt phẳng vuông góc trục
                float   proj   = Vector3DotProduct(toPos, axis);
                Vector3 radial = Vector3Subtract(toPos, Vector3Scale(axis, proj));
                float   dist   = Vector3Length(radial);
                if (dist < 1e-3f) break;

                // Tiếp tuyến = axis × radial (cùng chiều xoáy)
                Vector3 tangent = Vector3Normalize(Vector3CrossProduct(axis, radial));
                float s = L->strength / (dist + 1.0f); // giảm dần theo bán kính
                acc = Vector3Scale(tangent, s);
            } break;

            // ---- Gió theo hướng + nhiễu Perlin làm rung gió ----
            case FORCE_WIND: {
                float t  = time * L->noiseSpeed;
                float nx = Noise_Perlin3D(pos.x * L->noiseScale + t,
                                          pos.y * L->noiseScale,
                                          pos.z * L->noiseScale);
                // Perturb nhẹ theo chiều vuông góc với gió
                Vector3 base     = Vector3Scale(L->direction, L->strength);
                Vector3 perturb  = {nx * L->strength * 0.35f, 0.0f, nx * L->strength * 0.35f};
                acc = Vector3Add(base, perturb);
            } break;

            // ---- Trường nhiễu Perlin (3 field độc lập → vector) ----
            case FORCE_NOISE_PERLIN: {
                float t  = time * L->noiseSpeed;
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

            // ---- Curl noise — swirling không phân kỳ ----
            case FORCE_NOISE_CURL: {
                float t = time * L->noiseSpeed;
                // Tọa độ đã được nhân noiseScale; truyền scale=1.0f tránh double-scale
                Vector3 curl = Noise_Curl3D(
                    pos.x * L->noiseScale + t,
                    pos.y * L->noiseScale,
                    pos.z * L->noiseScale + t,
                    1.0f
                );
                acc = Vector3Scale(curl, L->strength);
            } break;

            // ---- Lực cản tỉ lệ vận tốc ----
            case FORCE_DRAG: {
                acc = Vector3Scale(vel, -L->strength);
            } break;
        }

        totalAcc = Vector3Add(totalAcc, Vector3Scale(acc, atten));
    }

    return totalAcc;
}
