#ifndef FORCE_FIELD_H
#define FORCE_FIELD_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

// ============================================================
// NOISE PRIMITIVES  — dùng độc lập hoặc qua ForceField
// ============================================================

// Perlin noise 3D chuẩn, output khoảng [-1, 1]
float Noise_Perlin3D(float x, float y, float z);

// Value noise 3D (rẻ hơn Perlin), output [0, 1]
float Noise_Value3D(float x, float y, float z);

// Curl noise 3D xấp xỉ (6 sample), output là vector divergence-free
// scale: tần số không gian; output biên độ khoảng [-1, 1] mỗi trục
Vector3 Noise_Curl3D(float x, float y, float z, float scale);

// ============================================================
// FORCE PRIMITIVES  — các kiểu lực cơ bản
// ============================================================

typedef enum {
    FORCE_GRAVITY_DIR,    // gia tốc cố định theo hướng (vd: -Y = trọng lực)
    FORCE_GRAVITY_POINT,  // hút (hoặc đẩy nếu strength < 0) vào một điểm
    FORCE_VORTEX,         // xoáy quanh trục (origin + direction = trục)
    FORCE_WIND,           // gió theo hướng + nhiễu Perlin làm gió rung
    FORCE_NOISE_PERLIN,   // trường nhiễu Perlin — đẩy hạt theo vector ngẫu nhiên
    FORCE_NOISE_CURL,     // trường Curl noise — không phân kỳ, swirling đẹp
    FORCE_DRAG,           // lực cản tỉ lệ vận tốc (cần truyền vel khi evaluate)
} ForceType;

// Mô tả một lớp lực đơn lẻ
typedef struct {
    ForceType type;

    // Vị trí / hướng
    Vector3 origin;     // điểm gốc: tâm hút/vortex/... (không dùng cho GRAVITY_DIR, DRAG)
    Vector3 direction;  // hướng lực / trục vortex (nên normalize sẵn)

    // Cường độ & suy giảm
    float strength;  // cường độ gia tốc (m/s²)
    float radius;    // 0.0 = tác dụng vô cực; > 0 = chỉ trong vùng sphere bán kính này
    float falloff;   // 0.0 = hằng số; 1.0 = tuyến tính; 2.0 = bình phương (chỉ khi radius > 0)

    // Tham số noise (dùng cho WIND, NOISE_PERLIN, NOISE_CURL)
    float noiseScale;  // tần số không gian (lớn hơn = nhiễu chi tiết hơn)
    float noiseSpeed;  // tốc độ cuộn noise theo thời gian
} ForceLayer;

// ============================================================
// FORCE FIELD  =  tổ hợp nhiều ForceLayer
// Evaluate tại một điểm → trả về gia tốc tổng (Vector3)
// ============================================================

#define FORCE_FIELD_MAX_LAYERS 8

typedef struct {
    ForceLayer layers[FORCE_FIELD_MAX_LAYERS];
    int        layerCount;
} ForceField;

// Xóa toàn bộ layer, trả về ForceField rỗng
void ForceField_Clear(ForceField *ff);

// Thêm một ForceLayer vào ForceField. Trả về false nếu đã đầy (>= FORCE_FIELD_MAX_LAYERS)
bool ForceField_AddLayer(ForceField *ff, ForceLayer layer);

// Tính gia tốc tổng tại vị trí pos với vận tốc vel (cần cho FORCE_DRAG), tại thời điểm time
// Có thể gọi mỗi frame cho từng particle, từng trail, v.v.
Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel, float time);

#endif // FORCE_FIELD_H
