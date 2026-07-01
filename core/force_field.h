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
  FORCE_GRAVITY_DIR, // gia tốc cố định theo hướng (vd: -Y = trọng lực)
  FORCE_GRAVITY_POINT, // hút (hoặc đẩy nếu strength < 0) vào một điểm
  FORCE_VORTEX,        // xoáy quanh trục (origin + direction = trục)
  FORCE_WIND,          // gió theo hướng + nhiễu Perlin làm gió rung
  FORCE_NOISE_PERLIN, // trường nhiễu Perlin — đẩy hạt theo vector ngẫu nhiên
  FORCE_NOISE_CURL, // trường Curl noise — không phân kỳ, swirling đẹp
  FORCE_DRAG, // lực cản tỉ lệ vận tốc (cần truyền vel khi evaluate)
  FORCE_VISCOSITY, // giảm chấn mũ: vel *= exp(-strength * dt) — dùng cho chất
                   // lỏng
  FORCE_RADIAL_AXIS, // Lực hướng tâm/li tâm theo TRỤC ĐỘNG (axisOrigin, axisDir
                     // cấp lúc evaluate).
  FORCE_VORTEX_AXIS,
  // LƯU Ý: thứ tự enum này PHẢI khớp với các #define FT_* trong particles.comp
  // (GLSL không include được header C, nên phải giữ đồng bộ thủ công).
} ForceType;

// Mô tả một lớp lực đơn lẻ
typedef struct {
  ForceType type;

  // Vị trí / hướng
  Vector3 origin; // điểm gốc: tâm hút/vortex/... (không dùng cho GRAVITY_DIR,
                  // DRAG, RADIAL_AXIS)
  Vector3 direction; // hướng lực / trục vortex (nên normalize sẵn)

  // Cường độ & suy giảm
  // Với FORCE_RADIAL_AXIS: strength dương = hút vào trục; âm = đẩy ra khỏi
  // trục. radius = vùng tác dụng (tính theo khoảng cách vuông góc tới trục).
  // falloff = suy giảm theo khoảng cách vuông góc tới trục.
  float strength; // cường độ gia tốc (m/s²)
  float radius; // 0.0 = tác dụng vô cực; > 0 = chỉ trong vùng sphere/cylinder
                // bán kính này
  float falloff; // 0.0 = hằng số; 1.0 = tuyến tính; 2.0 = bình phương (chỉ khi
                 // radius > 0)

  // Tham số noise (dùng cho WIND, NOISE_PERLIN, NOISE_CURL)
  float noiseScale; // tần số không gian (lớn hơn = nhiễu chi tiết hơn)
  float noiseSpeed; // tốc độ cuộn noise theo thời gian
} ForceLayer;

// ============================================================
// FORCE FIELD  =  tổ hợp nhiều ForceLayer
// Evaluate tại một điểm → trả về gia tốc tổng (Vector3)
// ============================================================

#define FORCE_FIELD_MAX_LAYERS 8

typedef struct {
  ForceLayer layers[FORCE_FIELD_MAX_LAYERS];
  int layerCount;
} ForceField;

// Xóa toàn bộ layer, trả về ForceField rỗng
void ForceField_Clear(ForceField *ff);

// Thêm một ForceLayer vào ForceField. Trả về false nếu đã đầy (>=
// FORCE_FIELD_MAX_LAYERS)
bool ForceField_AddLayer(ForceField *ff, ForceLayer layer);

// Tính gia tốc tổng tại vị trí pos với vận tốc vel (cần cho FORCE_DRAG), tại
// thời điểm time.
//
// LƯU Ý (BREAKING CHANGE): Tất cả các lời gọi hàm này phải kèm theo 2 tham số
// axisOrigin và axisDir. axisOrigin, axisDir: định nghĩa TRỤC ĐỘNG dùng cho mọi
// layer kiểu FORCE_RADIAL_AXIS. (axisDir PHẢI được normalize trước khi truyền
// vào). Nếu field không chứa FORCE_RADIAL_AXIS hoặc không cần trục động, truyền
// (Vector3){0} cho cả hai.
Vector3 ForceField_Evaluate(const ForceField *ff, Vector3 pos, Vector3 vel,
                            float time, Vector3 axisOrigin, Vector3 axisDir);

// Tính hệ số giảm chấn nhân lên velocity từ tất cả layer FORCE_VISCOSITY trong
// field.
float ForceField_GetViscosityDamping(const ForceField *ff, float dt);

// ============================================================
// GPU LAYOUT
//
// LƯU Ý FORCE_RADIAL_AXIS / FORCE_VORTEX_AXIS: axisOrigin/axisDir là trục
// động (giống tham số truyền vào ForceField_Evaluate) được BAKE vào
// layers[i].origin/direction NGAY LÚC PACK (xem ForceField_PackGPU) — vì
// GPU không có khái niệm "truyền thêm tham số lúc evaluate" mỗi layer.
// Do đó nếu ForceField có nhiều layer AXIS-type dùng axis khác nhau, phải
// gọi ForceField_PackGPU riêng cho từng nhóm (hoặc tách field) — một lần
// pack chỉ áp dụng MỘT cặp axisOrigin/axisDir cho toàn bộ layer AXIS-type
// trong field đó.
// ============================================================

typedef struct {
  Vector4 origin;    // xyz dùng, w đệm
  Vector4 direction; // xyz dùng, w đệm
  Vector4 params0;   // x: strength, y: radius, z: falloff, w: (float)type
  Vector4 params1;   // x: noiseScale, y: noiseSpeed, z/w: dự trữ
} ForceLayerGPU;

typedef struct {
  ForceLayerGPU layers[FORCE_FIELD_MAX_LAYERS];
  int layerCount;
  int _pad[3]; // đệm cho đủ multiple-of-16-byte, khớp ivec4 meta trong GLSL
} ForceFieldGPU;

// Chuyển ForceField (CPU) sang layout phẳng để memcpy thẳng lên SSBO.
// axisOrigin/axisDir: giống tham số cùng tên trong ForceField_Evaluate — trục
// động dùng cho layer FORCE_RADIAL_AXIS/FORCE_VORTEX_AXIS. Truyền (Vector3){0}
// cho cả hai nếu field không chứa layer axis-type nào.
//
// BREAKING CHANGE (so với bản trước chỉ nhận 2 tham số): mọi call site phải
// cập nhật thêm 2 tham số này. Tại thời điểm đổi, hàm này chưa có caller nào
// trong repo (unused) nên không có call site cần sửa.
void ForceField_PackGPU(const ForceField *ff, Vector3 axisOrigin,
                        Vector3 axisDir, ForceFieldGPU *out);

// ============================================================
// WIND ZONE GLOBAL
// Một ForceField toàn cục tự động áp dụng cho MỌI particle trong
// UpdateParticles() — không cần gán per-ParticleConfig.
// Dùng để mô phỏng gió môi trường, bão, hoặc các lực nền xuyên suốt.
// ============================================================

// Thiết lập wind zone toàn cục.
//   direction  : hướng gió chính (sẽ được normalize nội bộ)
//   strength   : gia tốc gió cơ bản (đơn vị giống ForceLayer.strength)
//   noiseAmp   : biên độ nhiễu Curl chồng lên (0 = gió thẳng)
//   noiseFreq  : tần số không gian của nhiễu (0.005 – 0.05 thường tốt)
void    WindZone_Set(Vector3 direction, float strength, float noiseAmp, float noiseFreq);

// Tắt wind zone, dừng áp dụng lên particle.
void    WindZone_Clear(void);

// Trả về true nếu wind zone đang hoạt động.
bool    WindZone_IsActive(void);

// Tính gia tốc wind tại vị trí pos. Gọi nội bộ bởi particle_system.
// Skill code thông thường KHÔNG gọi hàm này trực tiếp.
Vector3 WindZone_Evaluate(Vector3 pos, Vector3 vel, float time);

#endif // FORCE_FIELD_H