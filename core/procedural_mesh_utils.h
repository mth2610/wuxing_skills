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

/* ============================================================================
 * WAVE PLANE MESH SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Mặt phẳng lưới (grid) chia ô vừa phải (low-poly theo kỷ luật hiệu năng
 * mobile/Android), mỗi đỉnh bị đẩy theo Y bởi tổng 2-3 sóng sin khác tần
 * số/pha/hướng + nhiễu nhỏ để tránh lặp lại đều đặn kiểu "robotic".
 * Build 1 lần/frame (giống TubeMeshData) rồi Draw.
 * ==========================================================================*/

#define WAVE_PLANE_MAX_SEGMENTS_X 24
#define WAVE_PLANE_MAX_SEGMENTS_Z 24

typedef struct {
  float wavelength;     /* bước sóng chính (world units) */
  float amplitude;      /* biên độ đẩy Y chính */
  Vector3 direction;    /* hướng lan truyền sóng, mặt phẳng XZ, sẽ normalize */
  float crestSharpness; /* 0 = sin mượt, càng lớn càng nhọn đỉnh sóng */
} WavePlaneConfig;

/* Config mặc định: sóng vừa phải, hướng +X, không quá đều (đã trộn sẵn
 * 2 lớp sóng phụ + nhiễu trong Build, không cần khai báo riêng ở config). */
WavePlaneConfig ProceduralMesh_DefaultWavePlaneConfig(void);

typedef struct {
  Vector3 verts[WAVE_PLANE_MAX_SEGMENTS_X + 1][WAVE_PLANE_MAX_SEGMENTS_Z + 1];
  Vector3 normals[WAVE_PLANE_MAX_SEGMENTS_X + 1][WAVE_PLANE_MAX_SEGMENTS_Z + 1];
  int segmentsX; /* <= WAVE_PLANE_MAX_SEGMENTS_X */
  int segmentsZ; /* <= WAVE_PLANE_MAX_SEGMENTS_Z */
} WavePlaneMeshData;

/*
 * Build lưới width x length đỉnh trung tâm tại `center`, đẩy Y theo
 * cfg + thời gian (animate). Normal tính xấp xỉ bằng finite-difference
 * giữa các đỉnh lân cận sau khi đẩy.
 * - cfg: NULL để dùng ProceduralMesh_DefaultWavePlaneConfig()
 */
void ProceduralMesh_BuildWavePlane(WavePlaneMeshData *out, Vector3 center,
                                   float width, float length, int segmentsX,
                                   int segmentsZ, float time,
                                   const WavePlaneConfig *cfg);

/* Vẽ wave plane đã build: quad strip phủ toàn lưới. */
void ProceduralMesh_DrawWavePlane(const WavePlaneMeshData *data, Color color);

/* ============================================================================
 * CURLING WAVE MESH SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Tường sóng cuộn (tsunami silhouette): quét một profile tiết diện hở
 * hình "C" (đáy → mặt dốc lên → mép cuộn đè ra ngoài) dọc theo trục chiều
 * rộng (widthDirection). Tái dùng kỹ thuật sweep-along-path giống BuildTube
 * (vốn quét tiết diện tròn kín dọc Bezier) nhưng ở đây tiết diện hở và trục
 * quét là đường thẳng/hơi cong theo chiều rộng thay vì Bezier dài.
 * ==========================================================================*/

#define CURLING_WAVE_MAX_WIDTH_SEGS 32
#define CURLING_WAVE_MAX_PROFILE_SEGS 16 /* số điểm dọc theo tiết diện "C" */

typedef struct {
  float curlAmount; /* 0 = tường phẳng, càng lớn mép trên càng đổ cong ra
                        ngoài (overhang) */
  float height;     /* chiều cao tường tính từ đáy tới mép cuộn */
  float archWidth;  /* tổng chiều rộng tường dọc widthDirection */
} CurlingWaveConfig;

CurlingWaveConfig ProceduralMesh_DefaultCurlingWaveConfig(void);

typedef struct {
  /* verts[i][p]: i = lát dọc theo width, p = điểm dọc theo profile "C"
   * (0 = đáy, profileSegs = mép cuộn ngoài cùng) */
  Vector3 verts[CURLING_WAVE_MAX_WIDTH_SEGS + 1][CURLING_WAVE_MAX_PROFILE_SEGS + 1];
  Vector3 normals[CURLING_WAVE_MAX_WIDTH_SEGS + 1][CURLING_WAVE_MAX_PROFILE_SEGS + 1];
  int widthSegs;   /* <= CURLING_WAVE_MAX_WIDTH_SEGS */
  int profileSegs; /* <= CURLING_WAVE_MAX_PROFILE_SEGS */
} CurlingWaveMeshData;

/*
 * Build tường sóng cuộn bắt đầu từ baseCenter, quét dọc widthDirection
 * (sẽ normalize). cfg: NULL để dùng ProceduralMesh_DefaultCurlingWaveConfig().
 * profileSegs/widthSegs phải <= giới hạn MAX ở trên.
 */
void ProceduralMesh_BuildCurlingWave(CurlingWaveMeshData *out,
                                     Vector3 baseCenter,
                                     Vector3 widthDirection,
                                     const CurlingWaveConfig *cfg,
                                     int profileSegs, int widthSegs);

/* Vẽ curling wave đã build: quad strip phủ toàn bề mặt "C" x width. */
void ProceduralMesh_DrawCurlingWave(const CurlingWaveMeshData *data,
                                    Color color);

/* ============================================================================
 * LOW-POLY ROCK MESH SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Đá tảng góc cạnh low-poly: xuất phát từ icosphere cơ bản, đẩy ngẫu nhiên
 * khoảng cách mỗi đỉnh tới tâm trong [radius*(1-jitter), radius*(1+jitter)]
 * để tạo mặt facet góc cạnh tự nhiên (KHÔNG bóp méo scale một primitive
 * trơn). Chỉ dùng cho đá tảng nổi bật/lớn — đá vụn nền vẫn dùng
 * DrawCoreCube/DrawCoreSphere bóp méo + randomize per-instance.
 * `seed` cố định để cùng seed luôn ra cùng hình dạng (cho phép cache).
 * ==========================================================================*/

#define ROCK_MESH_MAX_VERTS 162  /* icosphere subdivision level 2 (12 + 30*5) đủ dư */
#define ROCK_MESH_MAX_FACES 320

typedef struct {
  Vector3 verts[ROCK_MESH_MAX_VERTS];
  Vector3 faceNormals[ROCK_MESH_MAX_FACES];
  int faceVertIdx[ROCK_MESH_MAX_FACES][3]; /* flat-shaded: 1 normal/face */
  int vertCount;
  int faceCount;
} RockMeshData;

/*
 * Build đá low-poly tâm `center`, bán kính gốc `radius`, jitter bán kính
 * theo `jitterAmount` (vd 0.25 = +-25%), `subdivisions` mức chia icosphere
 * (0 = icosahedron 12 đỉnh, 1 = ~42 đỉnh, 2 = ~162 đỉnh — clamp theo
 * ROCK_MESH_MAX_VERTS). `seed` quyết định nhiễu ngẫu nhiên xác định
 * (deterministic) — build 1 lần lúc cast rồi cache trong instance struct
 * của skill, không cần rebuild mỗi frame (đá không animate hình dạng),
 * giống cách water_stream cache TubeMeshConfig theo emitter.
 */
void ProceduralMesh_BuildRock(RockMeshData *out, Vector3 center, float radius,
                              float jitterAmount, int seed, int subdivisions);

/* Vẽ rock đã build: triangle list, flat shading (1 normal/face) cho đúng
 * cảm giác facet góc cạnh. */
void ProceduralMesh_DrawRock(const RockMeshData *data, Color color);

/* ============================================================================
 * SHARD CLUSTER MESH SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Cụm mảnh vỡ/tinh thể góc cạnh tỏa ra từ 1 điểm gốc: mỗi shard là 1 lăng
 * trụ thon (tapered prism, tiết diện đa giác nhỏ N cạnh) dài/ngắn/dày/mỏng
 * khác nhau, hướng tỏa ngẫu nhiên trong 1 cone quanh hướng chính. Tái dùng
 * ProceduralMesh__Noise2-style PRNG xác định theo seed (cùng tinh thần
 * BuildRock's jitter) để mỗi shard lệch nhau tự nhiên, không đều/robotic.
 * Use case: Metal sword-qi/shard, Water ice-shard.
 * ==========================================================================*/

#define SHARD_CLUSTER_MAX_SHARDS 16
#define SHARD_MAX_SIDES 6 /* tiết diện đa giác mỗi shard, low-poly */

typedef struct {
  /* spreadAngle: nửa góc cone tỏa ra quanh hướng chính (radian). */
  float spreadAngle;
  /* thicknessMin/Max: tỉ lệ bán kính tiết diện so với length của shard đó. */
  float thicknessMin, thicknessMax;
  /* tipSharpness: 0 = đầu shard cắt phẳng (tiết diện đầy), 1 = nhọn hẳn. */
  float tipSharpness;
  int sides; /* số cạnh tiết diện mỗi shard, <= SHARD_MAX_SIDES */
} ShardClusterConfig;

ShardClusterConfig ProceduralMesh_DefaultShardClusterConfig(void);

typedef struct {
  /* Mỗi shard: tiết diện gốc (base) + đỉnh (tip), `sides` đỉnh mỗi vòng.
   * tipRadius có thể ~0 (nhọn) tuỳ tipSharpness lúc Build. */
  Vector3 baseRing[SHARD_CLUSTER_MAX_SHARDS][SHARD_MAX_SIDES];
  Vector3 tipRing[SHARD_CLUSTER_MAX_SHARDS][SHARD_MAX_SIDES];
  Vector3 baseNormal[SHARD_CLUSTER_MAX_SHARDS][SHARD_MAX_SIDES];
  Vector3 tipCenter[SHARD_CLUSTER_MAX_SHARDS];
  Vector3 baseCenter[SHARD_CLUSTER_MAX_SHARDS];
  int sides; /* số cạnh tiết diện dùng chung cho cả cụm */
  int shardCount;
} ShardClusterMeshData;

/*
 * Build cụm `shardCount` shard tỏa từ `origin` theo hướng chính
 * `mainDirection` (sẽ normalize), độ dài mỗi shard random trong
 * [minLength, maxLength], dày/mỏng theo cfg->thicknessMin/Max. `seed`
 * quyết định toàn bộ random (hướng lệch trong cone, độ dài, độ dày) ->
 * xác định, build 1 lần lúc cast rồi cache (shard không animate hình
 * dạng), giống cách BuildRock cache theo seed.
 * cfg: NULL để dùng ProceduralMesh_DefaultShardClusterConfig().
 */
void ProceduralMesh_BuildShardCluster(ShardClusterMeshData *out, Vector3 origin,
                                      Vector3 mainDirection, int shardCount,
                                      float minLength, float maxLength,
                                      int seed, const ShardClusterConfig *cfg);

/* Vẽ toàn bộ cụm: mỗi shard là quad strip quanh thân + tam giác đáy + tam
 * giác đỉnh (hoặc apex nhọn nếu tipRadius~0). */
void ProceduralMesh_DrawShardCluster(const ShardClusterMeshData *data,
                                     Color color);

/* ============================================================================
 * VORTEX FUNNEL MESH SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Phễu xoáy thon dần + xoắn dọc trục thẳng đứng, có sống gờ xoắn ốc nổi
 * trên bề mặt (ridge). Về bản chất đây là 1 case đặc biệt của kỹ thuật
 * sweep-tiết-diện-tròn-dọc-path mà BuildTube/BuildCurlingWave đã dùng:
 * path ở đây là 1 đường THẲNG đứng (không Bezier) thay vì cong, tiết diện
 * là vòng tròn CO DẦN bán kính (topRadius -> bottomRadius) + XOAY dần theo
 * twistAmount, và bán kính từng đỉnh được nhô ra theo sóng cos(phi*ridgeCount)
 * để tạo gờ xoắn ốc. Vì path thẳng đứng cố định (không cần Frenet/Bezier
 * tangent thay đổi hướng), build trực tiếp thay vì gọi lại BuildTube --
 * nhưng layout dữ liệu ring/normal[height][radial] và vòng lặp 2 lớp
 * (height rồi radial) bám sát đúng convention của TubeMeshData/BuildTube.
 * ==========================================================================*/

#define VORTEX_FUNNEL_MAX_HEIGHT_SEGS 32
#define VORTEX_FUNNEL_MAX_RADIAL_SEGS 24

typedef struct {
  float topRadius;
  float bottomRadius;
  float height;
  float twistAmount; /* tổng góc xoay từ đáy lên đỉnh, độ (degree) */
  int ridgeCount;     /* số gờ xoắn ốc nổi trên bề mặt */
  float ridgeAmount;  /* biên độ nhô ra của gờ, tỉ lệ theo bán kính tại lát đó
                          (0 = không gờ, ~0.15 = gờ vừa) */
} VortexFunnelConfig;

VortexFunnelConfig ProceduralMesh_DefaultVortexFunnelConfig(void);

typedef struct {
  /* rings[i][j]: i dọc trục height (0 = đáy, heightSegs = đỉnh), j quanh
   * chu vi (radial). Cùng layout với TubeMeshData để skill code quen tay. */
  Vector3 rings[VORTEX_FUNNEL_MAX_HEIGHT_SEGS + 1][VORTEX_FUNNEL_MAX_RADIAL_SEGS];
  Vector3 normals[VORTEX_FUNNEL_MAX_HEIGHT_SEGS + 1][VORTEX_FUNNEL_MAX_RADIAL_SEGS];
  int heightSegs;
  int radialSegs;
} VortexFunnelMeshData;

/*
 * Build phễu xoáy tâm đáy tại `center`, trục dọc +Y, cao cfg->height.
 * time: dùng để animate xoay toàn phễu theo thời gian (xoáy động) -- truyền
 * 0 nếu muốn hình tĩnh (build-once-cache như Rock).
 * cfg: NULL để dùng ProceduralMesh_DefaultVortexFunnelConfig().
 */
void ProceduralMesh_BuildVortexFunnel(VortexFunnelMeshData *out, Vector3 center,
                                      const VortexFunnelConfig *cfg,
                                      int heightSegs, int radialSegs,
                                      float time);

/* Vẽ phễu đã build: quad strip thân (không cap đáy/đỉnh -- phễu thường mở
 * 2 đầu để lộ bên trong, giống tornado thật). */
void ProceduralMesh_DrawVortexFunnel(const VortexFunnelMeshData *data,
                                     Color color);

/* ============================================================================
 * FISSURE MESH SYSTEM (MỚI)
 * --------------------------------------------------------------------------
 * Vết nứt 3D nổi/lõm dọc theo 1 đường path (khác với decal nứt phẳng 2D đã
 * có) -- dùng core/path_spline.h's SamplePath để rải điểm centerline đều
 * đặn dọc `pathPoints` (giống Anchored-Along-Path skill skeleton), rồi
 * dựng tiết diện góc cạnh bất thường (V-shape lởm chởm, jitter theo seed,
 * cùng tinh thần facet của ShardCluster/Rock) quanh mỗi điểm centerline.
 * Earth skills: Địa chấn, Thạch shatter.
 * ==========================================================================*/

#define FISSURE_MAX_SEGMENTS 48 /* số lát dọc theo path (sau khi SamplePath) */
#define FISSURE_CROSS_VERTS 5 /* số đỉnh tiết diện ngang: mép trái, vai trái,
                                  đáy, vai phải, mép phải */

typedef struct {
  Vector3 verts[FISSURE_MAX_SEGMENTS + 1][FISSURE_CROSS_VERTS];
  Vector3 normals[FISSURE_MAX_SEGMENTS + 1][FISSURE_CROSS_VERTS];
  int segments; /* <= FISSURE_MAX_SEGMENTS, số lát thực tế dùng */
} FissureMeshData;

/*
 * Build vết nứt dọc theo `pathPoints` (>=2 điểm, polyline điều khiển --
 * không phải Bezier control points). Centerline được rải đều bằng SamplePath
 * (spacing ~ width để mật độ hợp lý, clamp <= FISSURE_MAX_SEGMENTS). Mỗi
 * lát ngang gồm FISSURE_CROSS_VERTS đỉnh tạo hình V lởm chởm: mép ở y=0
 * (mặt đất), đáy ở y=-depth (sunken) hoặc +depth nếu raised>0 truyền depth
 * âm. `jaggedness` (0..1) quyết định biên độ jitter ngẫu nhiên (xác định
 * theo seed) lên vị trí mép/vai/đáy + lệch ngang centerline, tránh vết nứt
 * thẳng đều robotic.
 */
void ProceduralMesh_BuildFissure(FissureMeshData *out, const Vector3 *pathPoints,
                                 int pathPointCount, float width, float depth,
                                 float jaggedness, int seed);

/* Vẽ vết nứt đã build: quad strip phủ (FISSURE_CROSS_VERTS-1) dải dọc theo
 * centerline. */
void ProceduralMesh_DrawFissure(const FissureMeshData *data, Color color);

/* ============================================================================
 * GPU VERTEX DISPLACEMENT MESH SYSTEM (MỚI — additive, KHÔNG thay builder CPU
 * ở trên)
 * --------------------------------------------------------------------------
 * Khác với Tube/WavePlane/CurlingWave/Rock/ShardCluster/VortexFunnel/Fissure
 * (build lại CPU mỗi frame, CPU đọc được vị trí đỉnh để raycast/anchor), hệ
 * này bake 1 mesh tĩnh DUY NHẤT lên GPU lúc cast/khởi tạo rồi để Vertex
 * Shader tự uốn/gợn sóng mỗi frame qua uniform — CPU không tính lại hình học
 * và KHÔNG đọc lại được vị trí đỉnh sau displacement.
 *
 * Chỉ dùng cho hiệu ứng thuần hình ảnh, không cần raycast/collision theo
 * hình dạng đã uốn. Nếu skill cần đọc vị trí đỉnh (raycast theo ring, anchor
 * theo bề mặt...), dùng hệ CPU build ở trên thay vì hệ này.
 *
 * Quy trình dùng:
 *   1) Cast-time (1 lần): ProceduralMesh_CreateBaseGrid/CreateBaseCylinder,
 *      cache Mesh trả về trong struct instance của skill.
 *   2) Mỗi frame, sau BeginShaderMode(shader) và trước DrawMesh: set
 *      MeshDisplacementParams rồi gọi ProceduralMesh_SetDisplacementUniforms().
 *   3) Vertex shader của skill include core/shaders/common/displacement.glsl
 *      (sau vs_header.glsl) và gọi DisplaceVertex_Noise/AlongPath/TwistAndTaper
 *      trong main() trước khi gọi VS_FinalOutput().
 *   4) Lúc unload skill (không phải mỗi frame): ProceduralMesh_UnloadBase().
 * ==========================================================================*/

/* Lưới phẳng tĩnh, mặt phẳng local XZ, tâm tại gốc, normal +Y, UV phủ
 * [0,1]x[0,1]. Build 1 lần lúc cast — KHÔNG rebuild mỗi frame, displacement
 * hoàn toàn do vertex shader đảm nhiệm. Giữ segmentsX/Z <= 32 cho mobile. */
Mesh ProceduralMesh_CreateBaseGrid(float width, float length, int segmentsX,
                                   int segmentsZ);

/* Trụ tròn rỗng 2 đầu (không cap), trục local +Y trong [0,1], bán kính local
 * 1 (skill tự scale bán kính thật qua matModel hoặc trong shader). UV.x =
 * góc quanh chu vi [0,1], UV.y = vị trí dọc trục [0,1] — dùng làm tham số
 * `t` cho DisplaceVertex_AlongPath/TwistAndTaper. Build 1 lần lúc cast. */
Mesh ProceduralMesh_CreateBaseCylinder(int radialSegs, int heightSegs);

/* Cầu UV-sphere tĩnh, tâm tại gốc, bán kính THẬT (không phải local 1 — khác
 * CreateBaseCylinder, vì hình cầu thường không cần GPU displacement, chỉ cần
 * 1 mesh DrawMesh-compatible để dùng cùng các hệ thống yêu cầu DrawMesh/
 * Material thay vì draw immediate-mode, vd Soft Particles — xem
 * core/screen_distort.h). Build 1 lần lúc cast, dịch chuyển qua matModel. */
Mesh ProceduralMesh_CreateBaseSphere(float radius, int rings, int slices);

/* Tham số displacement, set mỗi frame rồi đẩy lên shader qua
 * ProceduralMesh_SetDisplacementUniforms(). pathP0..P3 (world space) chỉ
 * được DisplaceVertex_AlongPath dùng. */
typedef struct {
  float amplitude;   /* biên độ đẩy theo normal — DisplaceVertex_Noise */
  float frequency;   /* tần số noise/sóng (world units^-1) */
  float speed;       /* tốc độ animate theo u_time */
  float twistAmount; /* tổng góc xoắn t=0..1, radian — AlongPath/TwistAndTaper */
  float taperStart;  /* hệ số bán kính tại t=0 */
  float taperEnd;    /* hệ số bán kính tại t=1 */
  Vector3 pathP0, pathP1, pathP2, pathP3; /* Bezier control points, world space */
} MeshDisplacementParams;

MeshDisplacementParams ProceduralMesh_DefaultDisplacementParams(void);

/* Set uniform displacement lên shader, bỏ qua an toàn uniform không tồn tại
 * (cùng pattern với SkillManager_BeginShader). Gọi mỗi frame, sau
 * BeginShaderMode(shader), trước DrawMesh/DrawModel. */
void ProceduralMesh_SetDisplacementUniforms(Shader shader,
                                            const MeshDisplacementParams *params);

/* Giải phóng mesh đã bake. Gọi đúng 1 lần lúc unload skill — KHÔNG gọi mỗi
 * frame (mesh cache theo instance, không phải pool động). */
void ProceduralMesh_UnloadBase(Mesh *mesh);

#endif // PROCEDURAL_MESH_UTILS_H
