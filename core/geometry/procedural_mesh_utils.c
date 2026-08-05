#include "core/geometry/procedural_mesh_utils.h"
#include "core/geometry/mesh_cache.h"
#include "core/path_spline.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/* ===========================================================================
 * SHARED INTERNAL HELPERS (Sử dụng chung cho nhiều file inl bên dưới)
 * =========================================================================*/

/* Deterministic small hash-based PRNG */
static unsigned int ProceduralMesh__Hash(unsigned int x)
{
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

/* Trả về float xác định trong [-1,1] từ 2 chỉ số nguyên + seed. */
static float ProceduralMesh__Noise2(int ix, int iz, int seed)
{
  unsigned int h = ProceduralMesh__Hash((unsigned int)(ix * 73856093) ^
                                        (unsigned int)(iz * 19349663) ^
                                        (unsigned int)(seed * 83492791));
  return ((float)(h & 0xFFFFu) / 65535.0f) * 2.0f - 1.0f;
}

/* ===========================================================================
 * MODULE INCLUDES (Tách code thành các khối logic nhỏ)
 * =========================================================================*/

#include "pm_core_shapes.inl"   // Hình học cơ bản (Sphere, Cylinder, Plane...)

/* Bezier — tiện ích ĐƯỜNG CONG, không thuộc hình nào. Sống ở đây (chứ không
 * trong pm_tube/pm_droplet/pm_capsule) vì ba module hình là độc lập với nhau,
 * và một đường cong không phải một cái mesh. */
Vector3 ProceduralMesh_BezierPoint(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    float tt = t * t, uu = u * u;
    float uuu = uu * u, ttt = tt * t;
    Vector3 p = Vector3Scale(p0, uuu);
    p = Vector3Add(p, Vector3Scale(p1, 3.0f * uu * t));
    p = Vector3Add(p, Vector3Scale(p2, 3.0f * u * tt));
    p = Vector3Add(p, Vector3Scale(p3, ttt));
    return p;
}

Vector3 ProceduralMesh_BezierTangent(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float u = 1.0f - t;
    Vector3 d = {0};
    d.x = 3.0f * u * u * (p1.x - p0.x) + 6.0f * u * t * (p2.x - p1.x) + 3.0f * t * t * (p3.x - p2.x);
    d.y = 3.0f * u * u * (p1.y - p0.y) + 6.0f * u * t * (p2.y - p1.y) + 3.0f * t * t * (p3.y - p2.y);
    d.z = 3.0f * u * u * (p1.z - p0.z) + 6.0f * u * t * (p2.z - p1.z) + 3.0f * t * t * (p3.z - p2.z);
    return d;
}

/* Kẹp offset đỉnh của một mặt cắt quét — xem doc đầy đủ tại khai báo trong
 * procedural_mesh_utils.h. Định nghĩa ở ĐÂY (top-level, không phải trong
 * pm_tube.inl) đúng như Bezier utilities phía trên, để pm_droplet.inl/
 * pm_capsule.inl gọi được khi cần mà không phải copy-paste.
 *
 * SỬA 05/08/2026, THAY CHO bản kẹp cũ từng nằm thẳng trong pm_tube.inl.
 * Bản cũ lấy trần offset theo bán kính DANH NGHĨA của cả vành
 * (baseRadius*capsuleCurve*headWeight) — ĐỘC LẬP với kênh SCALE đã co bán
 * kính THẬT tại chính đỉnh đó xuống bao nhiêu. Hai kênh nhiễu không tương
 * quan (SCALE co bán kính về sàn PM_TUBE_MIN_RADIUS_FRAC=0.25 TRONG KHI
 * OFFSET đẩy vào gần hết trần cũ 0.55x bán kính DANH NGHĨA) cộng dồn tại
 * CÙNG một đỉnh là chuyện hoàn toàn có thể — hai trường độc lập, cả hai đều
 * quét liên tục qua mọi (u, v, t) của mesh. Đo bằng số (xem
 * core/tests/pm_tube_offset_clamp_test.c): 0.25x trừ thêm 0.55x (của bán
 * kính danh nghĩa CHƯA co) ra đúng -0.30x — mặt cắt lộn qua tâm. Đây chính
 * là bậc thang/lộn ngược quan sát được trên trail funnel (0.12x đầu mỏng),
 * không phải một hiện tượng riêng của trail: tỉ lệ trên là bất biến theo tỉ
 * lệ (scale-invariant), cột khói dày hơn chỉ CHE nó đi chứ không tránh được.
 *
 * FIX: lấy trần theo `localRadius` — bán kính ĐÃ BIẾN DẠNG (đã qua sàn của
 * kênh SCALE) tại chính đỉnh này — thay vì bán kính danh nghĩa của vành.
 * Bất biến sau đó đúng THEO CẤU TRÚC công thức, không cần dò từng trường
 * hợp: |offset kẹp| < localRadius*maxRadiusFrac (tiệm cận, không bao giờ
 * chạm) ⟹ bán kính hiệu dụng > localRadius*(1-maxRadiusFrac) > 0 với mọi
 * maxRadiusFrac < 1, vì localRadius luôn > 0 (đã qua sàn PM_TUBE_MIN_RADIUS_
 * FRAC > 0 ở caller). */
#define PM_SWEPT_SECTION_OFFSET_KNEE_FRAC 0.70f
Vector3 PMSweptSection_ClampOffset(Vector3 rawOffset, float localRadius,
                                   float maxRadiusFrac, float ringGapLimit)
{
    float limit = ringGapLimit;
    float radiusLimit = localRadius * maxRadiusFrac;
    if (radiusLimit < limit) limit = radiusLimit;
    if (limit < 0.0f) limit = 0.0f;

    float offSqr = rawOffset.x * rawOffset.x + rawOffset.y * rawOffset.y +
                   rawOffset.z * rawOffset.z;
    float knee = limit * PM_SWEPT_SECTION_OFFSET_KNEE_FRAC;
    if (knee <= 1e-6f || offSqr <= knee * knee) return rawOffset;

    /* KHÔNG dùng tanh(x/limit)*limit thẳng từ gốc — tanh cong ngay từ giá
     * trị NHỎ, co bớt cả những chỗ nhiễu vốn dĩ ổn, chưa từng chạm limit bao
     * giờ. Dưới knee: y = x. Trên knee: mượt, không gãy đạo hàm tại knee lẫn
     * khi tiến tới limit — xem core/tests/pm_tube_offset_clamp_test.c cho
     * phép đo đạo hàm bằng finite-difference. */
    float offLen = sqrtf(offSqr);
    float range = limit - knee; // > 0: limit luôn > knee khi knee > 1e-6
    float excess = offLen - knee;
    float softLen = knee + range * tanhf(excess / range);
    float s = softLen / offLen;
    return Vector3Scale(rawOffset, s);
}

#include "pm_tube.inl"
#include "pm_droplet.inl"
#include "pm_capsule.inl"          // Dòng chảy, Vòi rồng (Bezier, TubeMesh)
#include "pm_water_waves.inl"   // Mặt nước, Sóng cuộn (WavePlane, CurlingWave)
#include "pm_rocks.inl"         // Đá Low-poly, Mảnh vỡ (Rock, ShardCluster)
#include "pm_magic_effects.inl" // Hiệu ứng phép (VortexFunnel, Fissure nứt đất)
#include "pm_gpu_base.inl"      // Lưới Base cho GPU Displacement
#include "pm_organic.inl"       // Cột thạch nhũ, Vũng nước (StonePillar, Puddle)
#include "pm_crystal.inl"       // Hình khối Pha lê (Crystal, Cluster)