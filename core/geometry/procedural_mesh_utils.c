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
#include "pm_tube.inl"          // Dòng chảy, Vòi rồng (Bezier, TubeMesh)
#include "pm_water_waves.inl"   // Mặt nước, Sóng cuộn (WavePlane, CurlingWave)
#include "pm_rocks.inl"         // Đá Low-poly, Mảnh vỡ (Rock, ShardCluster)
#include "pm_magic_effects.inl" // Hiệu ứng phép (VortexFunnel, Fissure nứt đất)
#include "pm_gpu_base.inl"      // Lưới Base cho GPU Displacement
#include "pm_organic.inl"       // Cột thạch nhũ, Vũng nước (StonePillar, Puddle)
#include "pm_crystal.inl"       // Hình khối Pha lê (Crystal, Cluster)