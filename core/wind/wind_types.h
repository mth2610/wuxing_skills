#ifndef WUXING_WIND_TYPES_H
#define WUXING_WIND_TYPES_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

// =============================================================================
// GHOST OF TSUSHIMA WIND & VORTICLE ARCHITECTURE
//
// Mô hình động lực học gió phân tầng (Bill Rockenbeck, GDC 2021):
// 1. Macro Wind: Vector gió cơ sở biến điệu theo không-thời gian bởi 3D gradient noise.
// 2. Terrain-Aware Lift: Xấp xỉ nâng dòng khí dựa trên độ dốc địa hình.
// 3. Vorticles: Mảng hạt gió vô hình (tối đa 256) mô hình hóa tác động cục bộ
//    từ đòn chém kiếm, đạn phi, vụ nổ, và đối lưu nhiệt từ ngọn lửa.
// =============================================================================

#define MAX_VORTICLES 256
#define WIND_TERRAIN_GRID_SIZE 32
#define WIND_TERRAIN_GRID_SAMPLES (WIND_TERRAIN_GRID_SIZE * WIND_TERRAIN_GRID_SIZE)

typedef enum {
    VORTICLE_LINEAR_GUST = 0, // Luồng gió thẳng có hướng (vệt chém kiếm, đạn phóng lướt)
    VORTICLE_RADIAL_BLAST,    // Sóng xung kích đẩy tỏa tròn (chưởng nổ, dậm chấn động)
    VORTICLE_VORTEX,          // Lốc xoáy quanh trục (lửa trại đối lưu, lốc xoáy)
    VORTICLE_TURBULENCE       // Nhiễu loạn hash-gradient 3D cục bộ (va chạm skill, vụ nổ)
} VorticleType;

typedef struct {
    Vector3      position;    // Vị trí tâm quả cầu gió (world-space)
    Vector3      direction;   // Hướng luồng gió (GUST) hoặc trục xoáy (VORTEX, normalized)
                              // Với TURBULENCE: x = noiseScale, y = noiseSpeed,
                              // z = attack time (s)
    float        radius;      // Bán kính ảnh hưởng (m)
    float        strength;    // Độ lớn vận tốc khí mục tiêu tối đa tại tâm (m/s)
    VorticleType type;
    float        lifetime;    // Thời gian còn lại (s)
    float        maxLifetime; // Thời gian sống ban đầu (s)
    float        inwardPull;  // Lực hút/đẩy xuyên tâm (cho VORTEX: dương = hút, âm = đẩy)
    bool         active;
} VorticleData;

typedef struct {
    Vector3 baseDirection;  // Hướng & vận tốc gió cơ sở (m/s, vd: {2.0f, 0.0f, 1.0f})
    float   gustAmplitude;  // Biên độ gió rít (tỉ lệ so với base: 0.0 = gió tĩnh, 1.0 = gấp đôi)
    float   noiseScale;     // Tần số không gian của sóng gió Perlin (mặc định ~0.08)
    float   noiseSpeed;     // Tốc độ trôi sóng gió theo thời gian (mặc định ~1.2)
    float   terrainLiftK;   // Hệ số nâng khí động học khi gặp dốc địa hình (mặc định ~1.5)
    float   heightGradientK;// Hệ số gradient vận tốc theo cao độ (0.0 = đồng đều, 1.0 = lớp biên đầy đủ)
} WindMacroConfig;

// Callback truy vấn độ cao địa hình tại tọa độ (worldX, worldZ)
typedef float (*TerrainHeightQueryFn)(float worldX, float worldZ, void *userData);

// CPU source of truth for the terrain approximation uploaded to compute.
// `valid` prevents missing terrain from being interpreted as a real Y=0 sample.
typedef struct {
    float heights[WIND_TERRAIN_GRID_SAMPLES];
    unsigned char valid[WIND_TERRAIN_GRID_SAMPLES];
    Vector2 originXZ;
    Vector2 cellSizeXZ;
    bool built;
    bool active;
    unsigned int version;
} WindTerrainGrid;

// Trạng thái đợt Gió Dẫn Đường toàn cục (Ghost of Tsushima Windicator State)
typedef struct {
    bool    active;      // Đang có đợt gió dẫn đường hoạt động
    Vector3 playerPos;   // Vị trí người chơi khi gọi gió
    Vector3 targetPos;   // Điểm mốc mục tiêu định hướng
    Vector3 direction;   // Vector đơn vị V_main = normalize(targetPos - playerPos)
    float   speed;       // Vận tốc gió cực đại của đợt gió (m/s, mặc định ~16.0)
    float   duration;    // Tổng thời gian đợt gió thổi (s, mặc định ~2.2)
    float   elapsed;     // Thời gian đã trôi qua trong đợt gió hiện tại
    float   progress;    // Tiến trình chuẩn hóa [0.0 .. 1.0]
    float   intensity;   // Hệ số cường độ tức thời theo phong bì attack/sustain/decay
} WindGuidingGust;

#endif // WUXING_WIND_TYPES_H
