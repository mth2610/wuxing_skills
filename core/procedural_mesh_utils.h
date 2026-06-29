#ifndef PROCEDURAL_MESH_UTILS_H
#define PROCEDURAL_MESH_UTILS_H

#include "raylib.h"

// Procedural drawing utilities utilizing raw rlgl calls
void DrawCoreSphere(Vector3 center, float radius, int rings, int slices,
                    Color color);
void DrawCoreCylinder(Vector3 bottom, Vector3 top, float radiusBottom,
                      float radiusTop, int slices, Color color);
void DrawCoreCone(Vector3 bottom, float radius, float height, int slices,
                  Color color);
void DrawCorePlaneRect(Vector3 center, Vector2 size, Color color);
void DrawCorePlanePolygon(Vector3 center, float radius, int sides, Color color);
void DrawCoreCube(Vector3 position, float width, float height, float length,
                  Color color);
void DrawCoreTorus(Vector3 center, float innerRadius, float outerRadius,
                   int sides, int rings, Color color);
void DrawCorePrism(Vector3 bottom, Vector3 top, float radius, int sides,
                   Color color);

/* ============================================================================
 * TUBE MESH SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Mục đích: tách phần "toán dựng ống Bezier theo Frenet-frame + biến đổi
 * mesh hữu cơ" ra khỏi skill code. Mọi skill cần hình ống/dòng chảy hữu cơ
 * (nước, lửa, gió, dây leo gỗ, tia kim loại...) đều build qua đây.
 *
 * Skill code lúc này chỉ cần:
 *   1) Khai báo TubeMeshConfig (hoặc lấy ProceduralMesh_DefaultTubeConfig())
 *      và tinh chỉnh vài hệ số đặc trưng cho element.
 *   2) Gọi ProceduralMesh_BuildTube() mỗi frame để build ring/normal.
 *   3) Gọi ProceduralMesh_DrawTube() để render quads + 2 end-cap.
 *
 * Giới hạn mảng tĩnh (không malloc): tăng MAX nếu skill cần segment dày hơn.
 * ==========================================================================*/

#define TUBE_MESH_MAX_SEGMENTS 48 /* số lát dọc theo path (chiều dài ống) */
#define TUBE_MESH_MAX_RADIAL 24 /* số lát quanh vòng tròn (chu vi ống)  */

/* --- Lớp 1: Path math (Cubic Bezier) — dùng chung, không khai báo lại ở skill
 * --- */
Vector3 ProceduralMesh_BezierPoint(Vector3 p0, Vector3 p1, Vector3 p2,
                                   Vector3 p3, float t);
Vector3 ProceduralMesh_BezierTangent(Vector3 p0, Vector3 p1, Vector3 p2,
                                     Vector3 p3, float t);

/*
 * --- Lớp 2: Cấu hình biến đổi hữu cơ lên mesh ---
 * Đây là phần "cảm giác chuyển động" mà skill muốn tái sử dụng/tinh chỉnh.
 * Mỗi field map trực tiếp tới một hiệu ứng hình học cụ thể trong
 * RenderCustom3DTube gốc, chỉ là được tham số hóa để mỗi skill có "chữ ký"
 * riêng.
 */
typedef struct {
  /* --- Profile bán kính theo chiều dài ống (capsule + taper) --- */
  float capsuleTailExp; /* hệ số mũ trong sqrtf(sin(t*PI)) bo đuôi. 1.0 = mặc
                           định (nước) */
  float tailTaperMin; /* tỉ lệ bán kính tối thiểu ở đuôi (t=0). VD 0.15 = đuôi
                         còn 15% */
  float tailTaperMax; /* tỉ lệ bán kính ở đầu khi taper áp dụng hết (t=1),
                         thường 1.0 */
  float headGrowth; /* headWeight = 1 + headGrowth * t. 0 = không phình đầu */

  /* --- Wobble: xoay frame (Right/Up) quanh trục tangent dọc path + theo thời
   * gian --- */
  float wobbleAmplitude; /* biên độ góc xoay (radian) */
  float wobbleFrequency; /* tần số dao động theo t (dọc path) */
  float wobbleSpeed;     /* tốc độ animate theo thời gian */

  /* --- Deform bề mặt: 2 lớp sóng sin chồng lên nhau (tạo gợn nước/lửa lăn tăn)
   * --- */
  float deform1Amp, deform1FreqT, deform1FreqPhi, deform1Speed;
  float deform2Amp, deform2FreqT, deform2FreqPhi, deform2Speed;

  /* --- End-cap apex: khoảng đẩy ra của đỉnh chóp bo đầu/đuôi, theo % bán kính
   * tại đó --- */
  float tailApexFactor; /* VD 0.25 = đuôi nhọn vừa. Số nhỏ -> đuôi nhọn hơn */
  float headApexFactor; /* VD 0.8  = đầu bo tròn đầy. Số lớn -> đầu phồng tròn
                           hơn */
} TubeMeshConfig;

/* Trả về config mặc định khớp với hành vi gốc của Water Stream (tube_skill.c).
 */
TubeMeshConfig ProceduralMesh_DefaultTubeConfig(void);

/*
 * --- Lớp 3: Dữ liệu mesh đã build (ring/normal + thông tin 2 đầu) ---
 * Build 1 lần/frame/emitter, rồi đưa vào Draw. Tách Build/Draw để skill có thể
 * build trước rồi xử lý thêm (vd raycast theo ring) nếu cần, trước khi vẽ.
 * tailApexFactor/headApexFactor được copy từ TubeMeshConfig lúc Build, nên
 * Draw không cần nhận lại cfg — mỗi skill vẫn giữ "hình dáng" apex riêng.
 */
typedef struct {
  Vector3 rings[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 normals[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 tailCenter, headCenter;
  Vector3 tailTangent, headTangent;
  float tailRadius, headRadius;
  int segments; /* số lát dọc thực tế dùng (<= TUBE_MESH_MAX_SEGMENTS) */
  int radialSegs; /* số lát quanh thực tế dùng (<= TUBE_MESH_MAX_RADIAL) */

  /* Lưu lại từ TubeMeshConfig lúc Build, để Draw dựng end-cap đúng "hình dáng"
   * skill đã chọn (đuôi nhọn/tù, đầu bo nhiều/ít) mà không cần truyền lại cfg.
   */
  float tailApexFactor;
  float headApexFactor;
} TubeMeshData;

/*
 * Build toàn bộ ring + normal dọc theo đường Bezier p0..p3, áp dụng config
 * biến đổi hữu cơ. Đây là phần thay thế toàn bộ loop "for i <= TUBE_SEGMENTS"
 * trong RenderCustom3DTube gốc.
 *
 * - baseRadius:    bán kính gốc (chưa nhân hệ số profile/taper/deform)
 * - flowProgress:  0..1, phần đường đã "chảy" tới (vd tia nước đang bay tới
 * đích)
 * - time:          thời gian hiện tại (GetTime()) dùng để animate wobble/deform
 * - segments/radialSegs: độ chi tiết mesh (phải <= giới hạn MAX ở trên)
 * - cfg: NULL để dùng ProceduralMesh_DefaultTubeConfig()
 */
void ProceduralMesh_BuildTube(TubeMeshData *out, Vector3 p0, Vector3 p1,
                              Vector3 p2, Vector3 p3, float baseRadius,
                              float flowProgress, float time, int segments,
                              int radialSegs, const TubeMeshConfig *cfg);

/*
 * Vẽ tube đã build: quad strip dọc thân + 2 end-cap triangle fan (đuôi + đầu).
 * Phải gọi giữa BeginShaderMode()/EndShaderMode() và
 * rlColor4ub(255,255,255,255) đã được set trước đó theo quy tắc 9.1 trong API
 * doc.
 *
 * - uvLengthScale: độ dài UV.v dọc thân ống, dùng để chỉnh tốc độ "trôi" của
 *   texture flow trong fragment shader (giống TUBE_UV_LENGTH_SCALE gốc).
 */
void ProceduralMesh_DrawTube(const TubeMeshData *data, float uvLengthScale);

#endif // PROCEDURAL_MESH_UTILS_H
