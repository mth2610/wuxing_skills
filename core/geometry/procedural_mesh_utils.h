#ifndef PROCEDURAL_MESH_UTILS_H
#define PROCEDURAL_MESH_UTILS_H

#include "core/deform/mesh_deform.h"
#include "raylib.h"

// Procedural drawing utilities utilizing raw rlgl calls
void DrawCoreSphere(Vector3 center, float radius, int rings, int slices,
                    Color color);
// Camera-facing quad — shape defined by shader alpha, not mesh silhouette
// (see comment at definition in pm_core_shapes.inl).
void DrawCoreBillboardQuad(Vector3 center, float halfSize, Camera3D cam, Color color);
// Flat quad on a fixed world-space plane (center + normal) — not camera-
// facing, not terrain-conforming (see comment at definition in
// pm_core_shapes.inl). For a density-field effect (e.g. smoke) sitting on
// any surface orientation: a sloped rock face, wall, ceiling.
void DrawCoreOrientedQuad(Vector3 center, Vector3 normal, float halfSize, Color color);
// Fixed world-space "cross billboard" — N vertical rectangles sharing the Y
// axis through `base`, rising to `base + (0,height,0)`. v=0 at base, v=1 at
// top (see comment at definition in pm_core_shapes.inl).
void DrawCoreCrossQuads(Vector3 base, float halfWidth, float height, int planeCount, Color color);
// Returns absolute world-space Y at (worldX, worldZ) — same semantics as
// MAP_API.md's GetHeightmapHeight. userData is whatever the caller passed
// to DrawCoreGroundPatch (e.g. a heightmap Image + terrain size/center).
typedef float (*GroundHeightSampleFn)(float worldX, float worldZ, void *userData);
// Subdivided ground-plane patch, per-vertex height via `heightFn` (NULL =
// flat, all vertices at center.y). `yLift` (meters) pushes every vertex
// above the sampled height — needed to avoid z-fighting against real ground
// geometry (see comment at definition in pm_core_shapes.inl; same fix as
// core/decals/decal_system.c's yOffset for ground decals).
void DrawCoreGroundPatch(Vector3 center, float halfSize, int subdiv, float yLift,
                         GroundHeightSampleFn heightFn, void *userData, Color color);
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
#define TUBE_MESH_MAX_RADIAL 24   /* số lát quanh vòng tròn (chu vi ống)  */

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
/* Bezier — tiện ích đường cong dùng chung, không thuộc hình nào. */
Vector3 ProceduralMesh_BezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);
Vector3 ProceduralMesh_BezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t);

/* ===========================================================================
 * BA LOẠI MESH QUÉT ĐỘC LẬP — mỗi hình một module, không dùng chung gì
 *
 *   pm_tube.inl     ống nước — bán kính hằng, hai đầu mở
 *   pm_droplet.inl  giọt nước — mũi nhọn ở đuôi, chỏm cầu ở đầu
 *   pm_capsule.inl  con nhộng — thân trụ, hai nửa cầu
 *
 * KHÔNG hình nào có nắp. Nắp cũ là hai quạt tam giác có đỉnh đẩy ra theo
 * tiếp tuyến — hai hình NÓN, cái "đầu bút chì". Ống mở hai đầu theo định
 * nghĩa; giọt nước và con nhộng tự khép bằng chính đường bao của chúng.
 * ===========================================================================*/

/* Kẹp offset đỉnh của một MẶT CẮT QUÉT (tube/droplet/capsule) sao cho không
 * bao giờ đẩy đỉnh qua tâm mặt cắt hay quá gần vành liền kề — DÙNG CHUNG cho
 * cả ba hình trên, sống ở đây (không phải file riêng của hình nào) đúng lý
 * do Bezier utilities ở trên cũng sống ở đây. Hiện chỉ pm_tube.inl gọi;
 * pm_droplet.inl/pm_capsule.inl áp dụng dOffset hoàn toàn KHÔNG kẹp — một
 * khoảng trống riêng, chưa đụng tới.
 *
 * localRadius PHẢI là bán kính ĐÃ BIẾN DẠNG tại đúng đỉnh này (sau khi đã áp
 * sàn/trần của kênh SCALE nếu có) — KHÔNG phải bán kính danh nghĩa của cả
 * vành. Xem comment đầy đủ tại định nghĩa (procedural_mesh_utils.c) và
 * core/tests/pm_tube_offset_clamp_test.c cho chứng minh bằng số của vì sao
 * điều này bắt buộc.
 *
 * ringGapLimit — trần THỨ HAI đo bằng khoảng cách hai vành (chặn hai vành
 * cắt nhau). Trần dùng là trần THẤP HƠN giữa hai cái. maxRadiusFrac ∈ (0,1).
 *
 * "Soft knee" (tanh), không phải kẹp cứng — dưới 70% trần giữ nguyên y = x,
 * trên đó bo mượt tiệm cận trần, không gãy đạo hàm. */
Vector3 PMSweptSection_ClampOffset(Vector3 rawOffset, float localRadius,
                                   float maxRadiusFrac, float ringGapLimit);

/* ỐNG NƯỚC — r(t) = 1 — hai đầu MỞ */
typedef struct
{
  /* ĐƯỜNG BAO BÁN KÍNH — r(t) = tailFrac + (1 - tailFrac) * t^pow
   *
   * Mặc định 0/0 nghĩa là tailFrac = 1, tức r(t) = 1: ống thẳng, đúng như
   * trước. Đặt tailFrac < 1 thì hình nở ra từ đuôi (t=0) lên đầu (t=1), và
   * radiusPow > 1 dồn chỗ nở về phía đầu — cái phễu.
   *
   * Đây là chỗ ĐÚNG để làm phễu. Width envelope của trail thì không: nó dựng
   * cả ống ở MỘT bán kính (halfWidth của đầu), nên một envelope mỏng-ở-đầu co
   * toàn bộ khối lại chứ không tạo hình. */
  float radiusTailFrac; /* bán kính ở đuôi, tỉ lệ so với đầu. 0 = 1.0 */
  float radiusPow;      /* p trong t^p. 0 = 1.0 (tuyến tính) */

  /* r(t) LUÔN chốt đầu (t=1) đúng 1.0x — số hạng (1-tailFrac) triệt tiêu tại
   * đó bất kể tailFrac. Nghĩa là "đầu" hình học (t=1, đầu ĐƯỜNG ĐI, không
   * phải khái niệm to/nhỏ) luôn LÀ bán kính gọi hàm yêu cầu (headR), và chỉ
   * có ĐUÔI (t=0) co giãn quanh nó — dùng tailFrac > 1 để đuôi phình to hơn
   * headR thay vì < 1 để đuôi hẹp lại là CÙNG một cơ chế, chỉ đổi hướng.
   *
   * Vấn đề: với một trail DI CHUYỂN, đầu đường đi (t=1) là ĐẦU HIỆN TẠI —
   * cái cần NHỎ, không phải cái nên giữ nguyên headR. Ép nó nhỏ bằng
   * tailFrac chỉ đẩy đuôi phình to hơn headR (đo được: tailFrac=8.33 x
   * headR=0.35 cho đuôi 2.9 m — to gấp nhiều lần bán kính người gọi thật sự
   * xin), vì headR luôn bị neo ở t=1 bất kể ai muốn nó nhỏ ở đó.
   *
   * Cờ này đổi t dùng trong r(t) thành (1-t): NEO đổi sang t=0 (đuôi), nên
   * headR — đúng bán kính người gọi xin — nằm ở t=0, và t=1 (đầu đường đi)
   * co lại còn tailFrac x headR, tailFrac vẫn ở miền [0,1] tự nhiên như tài
   * liệu field trên nói (không cần bẻ nó vượt 1 để lách). false = hành vi cũ,
   * neo ở t=1.
   *
   * MỘT CỜ, KHÔNG PHẢI HAI — đổi tên từ `radiusAnchorAtTail` 05/08/2026 sau
   * khi cái tên cũ chính là lỗi. Cờ này KHÔNG chỉ nói về bán kính; nó nói
   * MỘT sự thật về caller: "đầu phát của tôi nằm ở t=0 của path này, không
   * phải t=1". Từ sự thật đó suy ra BA thứ neo cùng một chỗ, và pm_tube.inl
   * áp cả ba qua đúng một biến `tEnv = anchorAtTail ? (1-t) : t`:
   *   1. đường bao bán kính r(t)      — mảnh ở đầu phát
   *   2. toạ độ ENVELOPE của deform    — MeshDeformLayer.env/envStart/envEnd,
   *      tức UV_ENV_HEAD_WELD: "không xê dịch tại nguồn phát"
   *   3. trọng số uốn trục centerlineAmp (t*t) — gốc đứng yên tại nguồn phát
   * Khi cờ này chỉ neo (1) mà bỏ (2), hai cái neo chạy NGƯỢC nhau: chỗ ống
   * dày nhất (t=0) có envelope = 0 (không churn) còn chỗ envelope = 1 (t=1)
   * lại là chỗ ống mảnh nhất (tailFrac x headR). Tích capsuleCurve x env —
   * biên độ phình TUYỆT ĐỐI thật sự nhìn thấy — đạt đỉnh chỉ 0.197 so với
   * 1.000 của cùng bộ số trên cột khói: gấp 5 lần yếu hơn, ở MỌI biên độ.
   * Đó đúng là triệu chứng "vẫn phẳng" của smoke trail, và là lý do cờ này
   * mang tên chung thay vì tên riêng của bán kính — đo bằng số ở
   * core/tests/pm_tube_envelope_anchor_test.c. */
  bool anchorAtTail;

  /* NGUYÊN TẮC CHUNG trước, chi tiết sau — xem core/deform/README.md: "the
   * drive coordinate is not the raw parametric position", cùng nguyên tắc
   * core/uv/uv_deform.h đã đặt tên cho toạ độ UV, áp dụng ở đây cho toạ độ
   * hình học/vertex. Hai field này (+ noiseSpanLenOverride bên dưới) là bản
   * cụ thể của nguyên tắc đó cho pm_tube.inl.
   *
   * MÉT cho MỘT chu kỳ [0,1] của tọa độ nhiễu (churn + uốn trục), thay vì
   * dùng thẳng t (phân số dọc TOÀN BỘ path hiện có). 0 = hành vi cũ, dùng t.
   *
   * VẤN ĐỀ t GIẢI QUYẾT SAI cho một path đang DI CHUYỂN: PMTubeSamplePath
   * đặt vành i ở khoảng cách `t * tổng-chiều-dài-path-hiện-tại`. Với cột
   * đứng yên (path đông cứng, chiều dài không đổi) thì t <=> một vị trí THẬT
   * cố định mãi mãi — không sao. Với một trail đang chạy, minVertexDistance
   * chỉ thêm node theo QUÃNG ĐƯỜNG thật đã đi, còn SỐ VÀNH của mesh
   * (tubeMaxRings) là HẰNG SỐ đặt lúc spawn — nên khi emitter chạy nhanh,
   * path thật DÀI ra và cùng chừng đó vành bị KÉO GIÃN; khi emitter chậm lại
   * (hay đảo hướng, như quỹ đạo Lissajous của fixture test), path thật NGẮN
   * lại và cùng chừng đó vành bị DỒN NÉN. Trường nhiễu có latticeAlong cố
   * định (vd. "3 ô dọc thân") thì 3 ô đó CO GIÃN theo vận tốc emitter — một
   * kiểu "thở phồng-xẹp" thuần hình học, chẳng liên quan gì khói thật, và là
   * lý do một trail di chuyển đọc ra như một tấm ảnh bị kéo lê thay vì khói
   * đang tan — xem core/composition/common/vc_smoke_trail.inl, phiên
   * 05/08/2026 xác nhận từ chính video test.
   *
   * >0 THÌ tọa độ nhiễu = (t * spanLen) / noiseWavelength — spanLen là chiều
   * dài THẬT (mét) mà [startT,endT] hiện đang trải, đã tính sẵn ở đầu
   * PMTube_BuildAlongPath. Nghĩa là "1 ô lattice" luôn rộng đúng chừng đó
   * mét, bất kể trail đang dài hay ngắn lúc này — tách nghĩa vật lý của toạ
   * độ nhiễu ra khỏi tổng chiều dài path đang dao động theo tốc độ.
   *
   * CHỈ áp cho toạ độ NHIỄU (uốn trục + churn bề mặt) — KHÔNG áp cho
   * capsuleCurve (r(t) vẫn dùng t thô, vì hình dạng phễu là tỉ lệ TƯƠNG ĐỐI
   * theo toàn thân, đúng ý muốn dù thân dài hay ngắn). */
  float noiseWavelength;

  /* GHI ĐÈ chiều dài path dùng cho tọa độ nhiễu (tNoise = t*L/noiseWavelength)
   * bằng một giá trị caller tự cấp thay vì spanLen tính THÔ lại mỗi khung
   * hình trong PMTube_BuildAlongPath. 0 (mặc định qua {0}) = dùng spanLen thô
   * như cũ — không set field này thì không đổi gì.
   *
   * LÝ DO CẦN, xác nhận 05/08/2026: spanLen thô là MỘT giá trị lái tọa độ
   * nhiễu của TOÀN BỘ mesh cùng lúc (mọi vành dùng chung nó qua t*spanLen).
   * Trên một trail ĐANG DI CHUYỂN, spanLen thô có thể nhảy đáng kể chỉ trong
   * MỘT khung hình (buffer lịch sử đầy lên/vơi đi khi tốc độ emitter đổi đột
   * ngột) — và vì mọi vành cùng đọc lại toạ độ nhiễu từ giá trị mới đó CÙNG
   * LÚC, một bước nhảy đủ lớn để vượt ranh giới ô lattice khiến CẢ mesh
   * "chớp" sang một mảng giá trị nhiễu không tương quan trong đúng một khung
   * — quan sát được là "những vùng lồi lõm đổi pha cho nhau một cái rụp".
   *
   * core/trails/trail_system.c cấp giá trị này từ `TrailEntity.
   * tubeNoiseSpanLen` — một bản LÀM MỊN theo thời gian (low-pass, hằng số
   * ~0.35s) của cùng phép đo, cập nhật trong UpdateTrailSystem() nơi có dt
   * thật. Chỉ áp cho TỌA ĐỘ NHIỄU: capsuleCurve (hình dạng phễu) và
   * ringGap/offsetLimit (kẹp hình học chống tự cắt) vẫn đọc chiều dài THẬT
   * của khung hiện tại, không làm mịn — hình dạng/kẹp an toàn cần chính xác
   * NGAY, chỉ tọa độ nhiễu mới cần ổn định qua thời gian. */
  float noiseSpanLenOverride;

  /* NHÂN trên phần "cuộn theo THỜI GIAN THẬT" của toạ độ nhiễu
   * (core/trails/trail_system.c: `tubeCfg.noiseOffset = runNoiseOffset *
   * noiseOffsetScrollMul`, runNoiseOffset = -uvScrollOffset*0.5, đồng hồ
   * DUY NHẤT chạy theo giây). Mặc định 1.0 — PMTube_DefaultConfig() đặt rõ,
   * không dựa vào {0} — giữ nguyên hành vi cũ cho MỌI caller không set field
   * này (cột khói, spark trail, ember trail...).
   *
   * SAO CẦN TẮT NÓ CHO MỘT TRAIL DI CHUYỂN, xác nhận 05/08/2026 sau khi tăng
   * smoketrail2_noise làm nó HỖN LOẠN hơn chứ không "hoà quyện" với chuyển
   * động — cái cột đứng yên đã ĐÚNG và ĐẸP với đúng công thức này, vấn đề chỉ
   * lộ ra khi mesh di chuyển dọc path, nên phải là DI CHUYỂN xung đột với
   * chính cơ chế "cuộn theo thời gian", không phải công thức nhiễu sai:
   *
   * Cơ chế runNoiseOffset ra đời để cho CỘT — path đông cứng, t KHÔNG mang
   * nghĩa "tuổi vật chất" (t=0 mãi mãi là nguồn, bất kể cột đã tồn tại bao
   * lâu) — một cách duy nhất để trông như "đang trôi": cuộn toạ độ nhiễu
   * theo ĐỒNG HỒ THẬT. Đó là NGUỒN CHUYỂN ĐỘNG DUY NHẤT của nó, nên mạch lạc.
   *
   * Một trail ĐANG DI CHUYỂN thì khác về bản chất: t CỦA NÓ đã mang nghĩa
   * "tuổi vật chất" rồi — vành ở t nhỏ là vật chất CŨ (đã lùi về cuối buffer
   * lịch sử), vành ở t lớn là vật chất MỚI (vừa phát ra) — do CHÍNH chuyển
   * động thật của emitter quyết định, không cần giả lập gì thêm. Cộng thêm
   * runNoiseOffset (một đồng hồ ĐỘC LẬP, tốc độ cố định, không liên quan gì
   * tốc độ emitter) vào CÙNG toạ độ đó là hai nguồn chuyển động không ăn khớp
   * cùng lái một trường — mẫu nhiễu cứ trôi tới trong khi vật chất thật đang
   * "già đi" lùi ra sau, không tương ứng gì nhau. Tăng biên độ nhiễu chỉ làm
   * cái xung đột đó TO hơn, ồn hơn — đúng triệu chứng "cao hơn = hỗn loạn
   * hơn" quan sát được, không phải noise amplitude sai.
   *
   * 0.0 THÌ tắt hẳn — vật chất vẫn "biến đổi khi già đi" hoàn toàn tự nhiên,
   * hoàn toàn miễn phí, chỉ nhờ chuyển động thật (t đổi theo buffer lịch sử);
   * phần thời gian thuần (không gắn toạ độ, đến từ tham số `time` của
   * MeshDeform_Evaluate — trường tự "thở" tại chỗ) vẫn còn nguyên, không mất
   * hẳn chuyển động nội bộ. */
  float noiseOffsetScrollMul;

  /* UỐN TRỤC — mét. Đẩy CẢ MẶT CẮT sang ngang, không phải đẩy từng đỉnh.
   *
   * Đây là thứ mà biến dạng bề mặt không bao giờ làm được. Bề mặt gợn thì cái
   * ống vẫn thẳng, chỉ sần lên; muốn thân uốn lượn như cột khói thật thì phải
   * dịch chính cái TRỤC. Ảnh khung dây của bản gốc cho thấy rõ: hai ba khúc
   * uốn lớn suốt chiều cao, mặt cắt vẫn tròn đều.
   *
   * Cần noiseField (không dùng preset): lấy hai lát u cố định của cùng trường
   * đó làm hai vô hướng khử tương quan cho hai trục ngang. 0 = tắt. */
  float centerlineAmp;

  /* Nhiễu động khung mặt cắt quanh trục tiếp tuyến. */
  float wobbleAmplitude, wobbleFrequency, wobbleSpeed;
  /* Hai lớp sóng sin trên bề mặt. TUẦN HOÀN theo cả t và phi, tức một đường
   * XOẮN — bật lên là có gờ xoắn ốc chạy dọc thân, đọc ra là lốc xoáy. Để 0
   * trừ khi thực sự muốn thế. */
  float deform1Amp, deform1FreqT, deform1FreqPhi, deform1Speed;
  float deform2Amp, deform2FreqT, deform2FreqPhi, deform2Speed;

  /* Biến dạng bằng noise — core/deform/mesh_deform.h. */
  float noiseAmp;    /* biên độ, theo tỉ lệ bán kính tại điểm đó */
  float noiseScale;  /* số ô lattice dọc thân (nguồn thủ tục) */
  float noiseSpeed;  /* tốc độ trôi của trường theo thời gian */
  float noiseOffset; /* dịch trường DỌC thân — toạ độ vật chất, không phải hình học */
  const unsigned char *noisePixels; /* R8G8B8A8, lát liền hai trục. NULL = lattice thủ tục */
  int noiseImgW, noiseImgH;
  const MeshDeformField *noiseField; /* trường đầy đủ. NULL = hai octave mặc định */

  /* Khung vận chuyển song song. Bắt buộc cho đường CONG: khung dựng lại từ một
   * vector tham chiếu toàn cục làm roll mặt cắt đổi theo hướng tiếp tuyến, nên
   * UV bị xoắn dọc thân (đo được 14% một vòng quấn). */
  bool useTransportFrame;
} PMTubeConfig;

typedef struct
{
  Vector3 rings[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 normals[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 tailCenter, headCenter;
  Vector3 tailTangent, headTangent;
  float tailRadius, headRadius;
  /* Bán kính danh nghĩa (trước biến dạng) của từng vành — để chỗ vẽ biết chu
   * vi thật mà chọn số lát texture QUANH thân cho khớp mét. */
  float ringRadius[TUBE_MESH_MAX_SEGMENTS + 1];
  int segments;
  int radialSegs;
  /* Bản sao của PMTubeConfig.anchorAtTail, do BuildAlongPath ghi vào.
   *
   * NẰM TRONG MESH, KHÔNG PHẢI THAM SỐ CỦA HÀM VẼ — 06/08/2026, và đó là cả
   * bài học. Cùng một cờ đã bị quên ở hai chỗ khác nhau trong hai ngày
   * (đường bao deform, rồi mặt nạ alpha ở PMTube_DrawFaded), mỗi lần vì nó
   * là thứ người gọi PHẢI NHỚ truyền đi tiếp. Gắn nó vào chính cái mesh thì
   * không ai quên được nữa: hàm vẽ nhận mesh, và mesh tự khai đầu phát của
   * nó nằm ở đâu. Xem doc `anchorAtTail` ở PMTubeConfig phía trên. */
  bool anchorAtTail;
} PMTubeMesh;

PMTubeConfig PMTube_DefaultConfig(void);
void PMTube_BuildAlongPath(PMTubeMesh *out, const Vector3 *pathPoints, int pathCount,
                          float baseRadius, float startT, float endT, float time,
                          int segments, int radialSegs, const PMTubeConfig *cfg);
void PMTube_Draw(const PMTubeMesh *data, float uvLengthScale);
void PMTube_DrawEx(const PMTubeMesh *data, float uvLengthScale, float uvOffset);
/* Như DrawEx nhưng alpha tắt dần ở hai đầu, mang bằng MÀU ĐỈNH (không phải
 * uniform — xem chú thích tại chỗ định nghĩa). Caller không gọi rlColor4ub
 * trước: màu nền đi vào qua `base`. */
void PMTube_DrawFaded(const PMTubeMesh *data, float uvLengthScale, float uvOffset,
                      Color base, float fadeInEnd, float fadeOutStart,
                      float metresPerTile);

/* GIỌT NƯỚC — mũi nhọn ở đuôi + chỏm cầu ở đầu, tự khép */
typedef struct
{
  float tailSharp; /* độ nhọn mũi đuôi. 0 = 1.6 */
  /* Nhiễu động khung mặt cắt quanh trục tiếp tuyến. */
  float wobbleAmplitude, wobbleFrequency, wobbleSpeed;
  /* Hai lớp sóng sin trên bề mặt. TUẦN HOÀN theo cả t và phi, tức một đường
   * XOẮN — bật lên là có gờ xoắn ốc chạy dọc thân, đọc ra là lốc xoáy. Để 0
   * trừ khi thực sự muốn thế. */
  float deform1Amp, deform1FreqT, deform1FreqPhi, deform1Speed;
  float deform2Amp, deform2FreqT, deform2FreqPhi, deform2Speed;

  /* Biến dạng bằng noise — core/deform/mesh_deform.h. */
  float noiseAmp;    /* biên độ, theo tỉ lệ bán kính tại điểm đó */
  float noiseScale;  /* số ô lattice dọc thân (nguồn thủ tục) */
  float noiseSpeed;  /* tốc độ trôi của trường theo thời gian */
  float noiseOffset; /* dịch trường DỌC thân — toạ độ vật chất, không phải hình học */
  const unsigned char *noisePixels; /* R8G8B8A8, lát liền hai trục. NULL = lattice thủ tục */
  int noiseImgW, noiseImgH;
  const MeshDeformField *noiseField; /* trường đầy đủ. NULL = hai octave mặc định */

  /* Khung vận chuyển song song. Bắt buộc cho đường CONG: khung dựng lại từ một
   * vector tham chiếu toàn cục làm roll mặt cắt đổi theo hướng tiếp tuyến, nên
   * UV bị xoắn dọc thân (đo được 14% một vòng quấn). */
  bool useTransportFrame;
} PMDropletConfig;

typedef struct
{
  Vector3 rings[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 normals[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 tailCenter, headCenter;
  Vector3 tailTangent, headTangent;
  float tailRadius, headRadius;
  int segments;
  int radialSegs;
} PMDropletMesh;

PMDropletConfig PMDroplet_DefaultConfig(void);
void PMDroplet_BuildAlongPath(PMDropletMesh *out, const Vector3 *pathPoints, int pathCount,
                          float baseRadius, float startT, float endT, float time,
                          int segments, int radialSegs, const PMDropletConfig *cfg);
void PMDroplet_Draw(const PMDropletMesh *data, float uvLengthScale);
void PMDroplet_DrawEx(const PMDropletMesh *data, float uvLengthScale, float uvOffset);

/* CON NHỘNG — trụ + hai nửa cầu, tự khép */
typedef struct
{
  float capFrac; /* phần chiều dài mỗi chỏm cầu. 0 = 0.25; 0.5 = hình cầu */
  /* Nhiễu động khung mặt cắt quanh trục tiếp tuyến. */
  float wobbleAmplitude, wobbleFrequency, wobbleSpeed;
  /* Hai lớp sóng sin trên bề mặt. TUẦN HOÀN theo cả t và phi, tức một đường
   * XOẮN — bật lên là có gờ xoắn ốc chạy dọc thân, đọc ra là lốc xoáy. Để 0
   * trừ khi thực sự muốn thế. */
  float deform1Amp, deform1FreqT, deform1FreqPhi, deform1Speed;
  float deform2Amp, deform2FreqT, deform2FreqPhi, deform2Speed;

  /* Biến dạng bằng noise — core/deform/mesh_deform.h. */
  float noiseAmp;    /* biên độ, theo tỉ lệ bán kính tại điểm đó */
  float noiseScale;  /* số ô lattice dọc thân (nguồn thủ tục) */
  float noiseSpeed;  /* tốc độ trôi của trường theo thời gian */
  float noiseOffset; /* dịch trường DỌC thân — toạ độ vật chất, không phải hình học */
  const unsigned char *noisePixels; /* R8G8B8A8, lát liền hai trục. NULL = lattice thủ tục */
  int noiseImgW, noiseImgH;
  const MeshDeformField *noiseField; /* trường đầy đủ. NULL = hai octave mặc định */

  /* Khung vận chuyển song song. Bắt buộc cho đường CONG: khung dựng lại từ một
   * vector tham chiếu toàn cục làm roll mặt cắt đổi theo hướng tiếp tuyến, nên
   * UV bị xoắn dọc thân (đo được 14% một vòng quấn). */
  bool useTransportFrame;
} PMCapsuleConfig;

typedef struct
{
  Vector3 rings[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 normals[TUBE_MESH_MAX_SEGMENTS + 1][TUBE_MESH_MAX_RADIAL];
  Vector3 tailCenter, headCenter;
  Vector3 tailTangent, headTangent;
  float tailRadius, headRadius;
  int segments;
  int radialSegs;
} PMCapsuleMesh;

PMCapsuleConfig PMCapsule_DefaultConfig(void);
void PMCapsule_BuildAlongPath(PMCapsuleMesh *out, const Vector3 *pathPoints, int pathCount,
                          float baseRadius, float startT, float endT, float time,
                          int segments, int radialSegs, const PMCapsuleConfig *cfg);
void PMCapsule_Draw(const PMCapsuleMesh *data, float uvLengthScale);
void PMCapsule_DrawEx(const PMCapsuleMesh *data, float uvLengthScale, float uvOffset);

/* Giọt nước còn dựng được dọc một đường Bezier — water stream dùng đường này. */
void PMDroplet_BuildBezier(PMDropletMesh *out, Vector3 p0, Vector3 p1, Vector3 p2,
                           Vector3 p3, float baseRadius, float flowProgress,
                           float time, int segments, int radialSegs,
                           const PMDropletConfig *cfg);


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

typedef struct
{
  float wavelength;     /* bước sóng chính (world units) */
  float amplitude;      /* biên độ đẩy Y chính */
  Vector3 direction;    /* hướng lan truyền sóng, mặt phẳng XZ, sẽ normalize */
  float crestSharpness; /* 0 = sin mượt, càng lớn càng nhọn đỉnh sóng */
} WavePlaneConfig;

/* Config mặc định: sóng vừa phải, hướng +X, không quá đều (đã trộn sẵn
 * 2 lớp sóng phụ + nhiễu trong Build, không cần khai báo riêng ở config). */
WavePlaneConfig ProceduralMesh_DefaultWavePlaneConfig(void);

typedef struct
{
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
 * SHOCKWAVE ANNULUS MESH
 * --------------------------------------------------------------------------
 * A ground-conforming annulus whose cross-section has a leading raised lip.
 * Unlike a decal, this carries a real silhouette, normal field, and seam-safe
 * UVs: U goes around the circumference, V travels inner -> outer edge.  The
 * builder is CPU-side because the ground query is authoritative and the
 * large-scale irregular outline must participate in terrain conformance.
 *
 * `radialJitter` only affects the outer outline (in metres); `lipJitter` is
 * vertical (metres) and is constrained to the raised profile.  `angularLobes`
 * is intentionally integral so the deformation closes exactly at U=0/1.
 * ==========================================================================*/

#define SHOCKWAVE_MAX_SLICES 64
#define SHOCKWAVE_MAX_RADIALS 8
#define SHOCKWAVE_HEIGHT_SAMPLES 24

typedef struct
{
    float radius;          /* centre -> crest-front distance, metres */
    float bandWidth;       /* annulus width, metres */
    float lipHeight;       /* raised crest height, metres */
    float crestU;          /* 0..1 across band; > 0.5 gives an outward-leading lip */
    float radialJitter;    /* outer-silhouette variation, metres */
    float lipJitter;       /* vertical variation at the crest, metres */
    int angularLobes;      /* low-frequency, closed deformation count */
    float angularPhase;    /* radians; caller advances it over time */
    float yLift;           /* lift above sampled terrain, metres */
} ShockwaveMeshConfig;

typedef struct
{
    Vector3 verts[SHOCKWAVE_MAX_SLICES + 1][SHOCKWAVE_MAX_RADIALS + 1];
    Vector3 normals[SHOCKWAVE_MAX_SLICES + 1][SHOCKWAVE_MAX_RADIALS + 1];
    Vector2 uv[SHOCKWAVE_MAX_SLICES + 1][SHOCKWAVE_MAX_RADIALS + 1];
    int slices;
    int radials;
} ShockwaveMeshData;

ShockwaveMeshConfig ProceduralMesh_DefaultShockwaveConfig(void);
void ProceduralMesh_BuildShockwave(ShockwaveMeshData *out, Vector3 center,
                                   const ShockwaveMeshConfig *cfg, int slices,
                                   int radials, GroundHeightSampleFn heightFn,
                                   void *userData);
/* `radialColors` is optional; when non-NULL it contains `radials + 1` colours
 * indexed by cross-band V. Draw inside the caller's chosen shader/blend pass. */
void ProceduralMesh_DrawShockwave(const ShockwaveMeshData *data,
                                  const Color *radialColors);

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

typedef struct
{
  float curlAmount; /* 0 = tường phẳng, càng lớn mép trên càng đổ cong ra
                        ngoài (overhang) */
  float height;     /* chiều cao tường tính từ đáy tới mép cuộn */
  float archWidth;  /* tổng chiều rộng tường dọc widthDirection */
} CurlingWaveConfig;

CurlingWaveConfig ProceduralMesh_DefaultCurlingWaveConfig(void);

typedef struct
{
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

#define ROCK_MESH_MAX_VERTS 162 /* icosphere subdivision level 2 (12 + 30*5) đủ dư */
#define ROCK_MESH_MAX_FACES 320

typedef struct
{
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

/* Build MỘT rock mẫu (GPU-resident Mesh, UploadMesh một lần duy nhất — cùng
 * vòng đời với shader/texture, KHÔNG BAO GIỜ rebuild) để dùng với
 * DrawMeshInstanced khi cần N bản sao cùng hình dạng trong cùng 1 frame
 * (vd VFX_ComposeFloatingStones, core/composition/vc_earth.inl). Đánh đổi:
 * mọi instance dùng chung 1 silhouette, biến thể chỉ đến từ transform
 * (vị trí/xoay/scale) — không còn N hình dạng khác nhau theo seed riêng
 * như ProceduralMesh_BuildRock/MeshCache_GetRock. Xem CORE_API.md "GPU
 * Instancing — standard pattern" và CORE_ISSUES.md Item 40. */
Mesh ProceduralMesh_BuildRockTemplateMesh(float radius, float jitterAmount, int seed, int subdivisions);

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

typedef struct
{
  /* spreadAngle: nửa góc cone tỏa ra quanh hướng chính (radian). */
  float spreadAngle;
  /* thicknessMin/Max: tỉ lệ bán kính tiết diện so với length của shard đó. */
  float thicknessMin, thicknessMax;
  /* tipSharpness: 0 = đầu shard cắt phẳng (tiết diện đầy), 1 = nhọn hẳn. */
  float tipSharpness;
  int sides; /* số cạnh tiết diện mỗi shard, <= SHARD_MAX_SIDES */
} ShardClusterConfig;

ShardClusterConfig ProceduralMesh_DefaultShardClusterConfig(void);

typedef struct
{
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

typedef struct
{
  float topRadius;
  float bottomRadius;
  float height;
  float twistAmount; /* tổng góc xoay từ đáy lên đỉnh, độ (degree) */
  int ridgeCount;    /* số gờ xoắn ốc nổi trên bề mặt */
  float ridgeAmount; /* biên độ nhô ra của gờ, tỉ lệ theo bán kính tại lát đó
                         (0 = không gờ, ~0.15 = gờ vừa) */
} VortexFunnelConfig;

VortexFunnelConfig ProceduralMesh_DefaultVortexFunnelConfig(void);

typedef struct
{
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
#define FISSURE_CROSS_VERTS 5   /* số đỉnh tiết diện ngang: mép trái, vai trái, \
                                    đáy, vai phải, mép phải */

typedef struct
{
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

/* Như DrawFissure nhưng chỉ vẽ `maxSegments` lát đầu (progressive reveal:
 * vết nứt "chạy" từ A sang B theo thời gian thay vì hiện hết ngay). */
void ProceduralMesh_DrawFissurePartial(const FissureMeshData *data, Color color, int maxSegments);

/* Như DrawFissurePartial nhưng tô màu gradient theo cross-section (5 màu ứng
 * với mép/vai/đáy/vai/mép, xem FISSURE_CROSS_VERTS) thay vì 1 màu đặc — tự
 * mang shading riêng (rìa sáng, đáy tối gần đen) KHÔNG phụ thuộc ánh sáng
 * scene. Dùng cái này thay vì DrawFissurePartial+EffectMaterial(lit) khi
 * scene có ít/không ánh sáng thật (map tối, cảnh đêm) — mesh lit sẽ chìm
 * thành đen-trên-đen nếu không có nguồn sáng chiếu vào. */
void ProceduralMesh_DrawFissureShaded(const FissureMeshData *data, const Color crossColors[FISSURE_CROSS_VERTS], int maxSegments);

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

/* Tham số displacement, set mỗi frame rồi đẩy lên shader qua
 * ProceduralMesh_SetDisplacementUniforms(). pathP0..P3 (world space) chỉ
 * được DisplaceVertex_AlongPath dùng. */
typedef struct
{
  float amplitude;                        /* biên độ đẩy theo normal — DisplaceVertex_Noise */
  float frequency;                        /* tần số noise/sóng (world units^-1) */
  float speed;                            /* tốc độ animate theo u_time */
  float twistAmount;                      /* tổng góc xoắn t=0..1, radian — AlongPath/TwistAndTaper */
  float taperStart;                       /* hệ số bán kính tại t=0 */
  float taperEnd;                         /* hệ số bán kính tại t=1 */
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

// --- Raw Procedural Drawers ---
void ProceduralMesh_DrawOrganicStonePillar(Vector3 pillarPos, float currentHeight, float baseRad, float topRad);
void ProceduralMesh_DrawOrganicPuddle(Vector3 pos, float radius);

// Thêm vào procedural_mesh_utils.h
typedef struct
{
  float height;
  float radius;
  float taper;
  float twist;
  float noise;
  float bevel;
  float split;
  int sides;
  int segments;
} CrystalDesc;

// Draw a single crystal at the given position with the specified description and progress (0.0 to 1.0)
void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color);

/* TỐI ƯU: ProceduralMesh_DrawCrystalCluster trước đây gọi rlPushMatrix +
 * rlBegin/rlEnd riêng cho từng viên (N draw call/state-push cho N viên).
 * Giờ nó build toàn bộ cụm vào 1 buffer phẳng (xoay tiltDeg bằng toán vector
 * CPU thay vì GL matrix stack) rồi vẽ bằng đúng 1 rlBegin/rlEnd — xem
 * ProceduralMesh_BuildCrystalCluster/ProceduralMesh_DrawCrystalClusterMesh
 * bên dưới nếu cần build 1 lần và cache (progress cố định) thay vì build lại
 * mỗi frame. */
#define CRYSTAL_CLUSTER_MAX_CRYSTALS 8
#define CRYSTAL_CLUSTER_MAX_TRIS 1024 /* 8 viên * tối đa ~126 tam giác/viên (LOD con capped ở sides/segments<=8) */

typedef struct
{
  Vector3 pos[CRYSTAL_CLUSTER_MAX_TRIS * 3];
  Vector3 normal[CRYSTAL_CLUSTER_MAX_TRIS * 3];
  Vector2 uv[CRYSTAL_CLUSTER_MAX_TRIS * 3];
  int triCount;
} CrystalClusterMeshData;

/* Build cụm crystal quanh `center`: mỗi viên con lệch theo seed (giống hệt
 * layout cũ), xoay tiltDeg bằng CPU vector math, ghi thẳng vào world-space
 * buffer `out`. Sides/segments của viên con bị clamp <= 8 để giữ bộ nhớ tĩnh
 * hợp lý (cluster dùng cho chi tiết phụ, không cần LOD cao như crystal chính). */
void ProceduralMesh_BuildCrystalCluster(CrystalClusterMeshData *out, Vector3 center,
                                        const CrystalDesc *desc, int count, int seed,
                                        float progress);

/* Vẽ cụm đã build: đúng 1 rlBegin(RL_TRIANGLES)/rlEnd() cho toàn bộ cụm. */
void ProceduralMesh_DrawCrystalClusterMesh(const CrystalClusterMeshData *data, Color color);

/* Tiện ích build+draw ngay trong 1 lệnh (dùng buffer scratch static nội bộ) —
 * giữ nguyên chữ ký cũ nên các call site hiện có không cần sửa gì, vẫn nhận
 * được lợi ích 1-draw-call thay vì N. */
void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color);

/* ============================================================================
 * CRYSTAL CLUSTER — GPU-RESIDENT MESH (cast-burst nhiều viên, vd 8-10 viên)
 * --------------------------------------------------------------------------
 * ProceduralMesh_DrawCrystalCluster ở trên vẫn build lại CPU + submit qua
 * rlBegin/rlEnd MỖI FRAME — ổn cho cụm nhỏ (3-5 viên chi tiết thấp, vd
 * micro-crystal ở chân skill). Với 1 cú cast tạo NHIỀU viên chi tiết cao (vd
 * 10 viên x 16 sides x 16 segments ~ 15.000 đỉnh) và VFX sống nhiều frame,
 * build lại + rlVertex3f từng đỉnh mỗi frame là nút thắt CPU thật sự (không
 * phải "N draw call" — rlgl tự gộp — mà là N lệnh gọi hàm rời rạc + toán
 * sin/cos/normalize lặp lại vô ích khi hình dạng không đổi).
 *
 * Hàm dưới đây build đúng 1 LẦN thành Mesh thật (VBO GPU, giống hệt convention
 * của ProceduralMesh_CreateBaseGrid/CreateBaseCylinder ở khối "GPU VERTEX
 * DISPLACEMENT" bên dưới): cast-time only, cache Mesh trong instance struct
 * của skill, KHÔNG gọi lại mỗi frame. Hiệu ứng "mọc lên" (progress 0->1)
 * không bake vào CPU nữa mà giao cho GPU qua uniform `u_growProgress` của
 * CrystalMaterial (core/shaders/crystal.vs nhân progress vào trục Y trước
 * MVP) — CPU mỗi frame chỉ còn đúng 1 dòng set uniform + 1 lệnh DrawMesh. */

/* CẢNH BÁO HIỆU NĂNG: hàm này gọi UploadMesh (tạo VBO/VAO MỚI trên GPU) mỗi
 * lần build — đó là 1 lệnh đồng bộ hoá GPU-driver thật sự tốn kém (khác hẳn
 * DrawMesh, chỉ set uniform + draw call rất rẻ). Build 1 lần thì không sao,
 * nhưng nếu skill build lại mỗi lần cast (VD 1 cụm mới mỗi lần bắn) và nhiều
 * cast dồn vào cùng 1 khoảng ngắn (nhiều nhân vật/click liên tục), nhiều lần
 * UploadMesh dồn dập SẼ gây giật khung hình dù per-frame draw đã rẻ.
 * → Nếu skill cast liên tục/dồn dập: dùng ProceduralMesh_BuildCrystalTemplateMesh
 *   (build 1 lần duy nhất, vĩnh viễn) + tự tính transform per-instance thay vì
 *   gọi hàm này mỗi cast. Chỉ dùng hàm này cho mesh thật sự tĩnh, build hiếm
 *   (VD 1 prop trang trí cố định trong map, build lúc load level).
 *
 * Build cụm crystal ở progress=1.0 (full-grown), local space quanh gốc
 * (0,0,0) — dùng `transform` khi DrawMesh để đặt vào world position thật.
 * Trả Mesh rỗng (vertexCount=0) nếu desc/count không hợp lệ.
 * Cast-time only — gọi 1 lần lúc bắt đầu VFX, cache vào instance struct của
 * skill, KHÔNG gọi mỗi frame. Nhớ gọi ProceduralMesh_UnloadBase() khi VFX kết
 * thúc (cùng convention với CreateBaseGrid/CreateBaseCylinder). */
Mesh ProceduralMesh_BuildCrystalClusterMesh(const CrystalDesc *desc, int count, int seed);

/* Build ĐÚNG 1 viên pha lê "mẫu" (local space, tâm gốc (0,0,0), thẳng đứng —
 * KHÔNG có jitter vị trí/tilt/scale của 1 viên con trong cluster). Gọi 1 LẦN
 * DUY NHẤT (lười — lazy static, giống cách shader/texture chỉ Load 1 lần),
 * KHÔNG BAO GIỜ build lại hay unload trong suốt vòng đời game. Dùng lại nhiều
 * lần qua ProceduralMesh_DrawBakedCrystalCluster với `transform` khác nhau
 * (dịch/xoay/scale tính trên CPU) để vẽ nhiều viên "trông khác nhau" mà
 * không tốn UploadMesh nào thêm — đây là cách tối ưu đúng cho skill cast
 * dồn dập nhiều viên/nhiều lần. Xem VFX_DrawIceCrystalBurst (core/composition/
 * vc_water.inl) làm ví dụ đầy đủ (build template 1 lần + vòng lặp DrawMesh
 * với transform ngẫu nhiên xác định theo seed). */
Mesh ProceduralMesh_BuildCrystalTemplateMesh(const CrystalDesc *desc);

/* Vẽ Mesh đã build ở trên. Gọi giữa CrystalMaterial_Begin/CrystalMaterial_End
 * (hoặc bất kỳ block đã BeginShaderMode nào khác — DrawMesh tự set mvp/
 * matModel qua shader.locs, không cần thao tác gì thêm, xem comment trong
 * SkillManager_BeginShader). `material` nên lấy từ
 * ProceduralMesh_GetPassthroughMaterial(shader) để không phải tự quản lý
 * Material/texture maps. */
void ProceduralMesh_DrawBakedCrystalCluster(Mesh mesh, Material material, Matrix transform);

/* Material dùng chung, "trong suốt" về mặt texture/color (LoadMaterialDefault
 * load 1 lần, cache) — chỉ để làm phương tiện gọi DrawMesh với shader tuỳ
 * biến (crystal.vs/.fs qua CrystalMaterial, hoặc bất kỳ shader custom nào
 * dùng tên uniform chuẩn "mvp"/"matModel"); mọi uniform khác (u_baseColor,
 * u_growProgress...) do CrystalMaterial_Begin/SetGrowProgress set riêng, hàm
 * này không đụng vào. Đổi `.shader` mỗi lần gọi theo shader đang active. */
Material ProceduralMesh_GetPassthroughMaterial(Shader shader);

#endif // PROCEDURAL_MESH_UTILS_H
