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

#include "pm_tube.inl"
#include "pm_droplet.inl"
#include "pm_capsule.inl"          // Dòng chảy, Vòi rồng (Bezier, TubeMesh)
#include "pm_water_waves.inl"   // Mặt nước, Sóng cuộn (WavePlane, CurlingWave)
#include "pm_rocks.inl"         // Đá Low-poly, Mảnh vỡ (Rock, ShardCluster)
#include "pm_magic_effects.inl" // Hiệu ứng phép (VortexFunnel, Fissure nứt đất)
#include "pm_gpu_base.inl"      // Lưới Base cho GPU Displacement
#include "pm_organic.inl"       // Cột thạch nhũ, Vũng nước (StonePillar, Puddle)
#include "pm_crystal.inl"       // Hình khối Pha lê (Crystal, Cluster)