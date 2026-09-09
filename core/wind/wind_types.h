#ifndef WUXING_WIND_TYPES_H
#define WUXING_WIND_TYPES_H

#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

// =============================================================================
// GHOST OF TSUSHIMA WIND & VORTICLE ARCHITECTURE
//
// Mô hình động lực học gió phân tầng (Bill Rockenbeck, GDC 2021):
// 1. Macro Wind: Vector gió cơ sở biến điệu theo không-thời gian bởi 2D/3D Noise.
// 2. Terrain-Aware Lift: Xấp xỉ nâng dòng khí dựa trên độ dốc địa hình.
// 3. Vorticles: Mảng hạt gió vô hình (tối đa 256) mô hình hóa tác động cục bộ
//    từ đòn chém kiếm, đạn phi, vụ nổ, và đối lưu nhiệt từ ngọn lửa.
// =============================================================================

#define MAX_VORTICLES 256

typedef enum {
    VORTICLE_LINEAR_GUST = 0, // Luồng gió thẳng có hướng (vệt chém kiếm, đạn phóng lướt)
    VORTICLE_RADIAL_BLAST,    // Sóng xung kích đẩy tỏa tròn (chưởng nổ, dậm chấn động)
    VORTICLE_VORTEX           // Lốc xoáy quanh trục (lửa trại đối lưu, lốc xoáy)
} VorticleType;

typedef struct {
    Vector3      position;    // Vị trí tâm quả cầu gió (world-space)
    Vector3      direction;   // Hướng luồng gió (GUST) hoặc trục xoáy (VORTEX, normalized)
    float        radius;      // Bán kính ảnh hưởng (m)
    float        strength;    // Gia tốc tối đa tại tâm (m/s^2)
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
} WindMacroConfig;

// Callback truy vấn độ cao địa hình tại tọa độ (worldX, worldZ)
typedef float (*TerrainHeightQueryFn)(float worldX, float worldZ, void *userData);

#endif // WUXING_WIND_TYPES_H
