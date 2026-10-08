#ifndef CORE_MOTION_WIND_GPU_H
#define CORE_MOTION_WIND_GPU_H
#include "core/wind/wind_system.h"
#include "core/motion/motion_gpu.h"
#include <string.h>
// Shared byte-identical Wind snapshot for component compute consumers.
#define MAX_GPU_VORTICLES MAX_VORTICLES

typedef struct {
    Vector4 pos_radius;      // xyz = position, w = radius
    Vector4 dir_strength;    // xyz = direction, w = strength
    Vector4 params;          // x = type, y = lifetime, z = maxLifetime, w = inwardPull
} VorticleGPU;

typedef struct {
    Vector4 macro_dir_amp;   // xyz = baseDirection, w = gustAmplitude
    Vector4 macro_params;    // x = noiseScale, y = noiseSpeed, z = activeCount, w = terrainLiftK
    Vector4 macro_extra;     // x = heightGradientK, yzw = reserved
    VorticleGPU vorticles[MAX_GPU_VORTICLES];
    Vector4 guidingOrigin, guidingTarget, guidingDirection;
} WindGPU;

#define WIND_TERRAIN_PACKED_VEC4S (WIND_TERRAIN_GRID_SAMPLES / 2)

typedef struct {
    Vector4 origin_cell; // xy = origin XZ, zw = cell size XZ
    int meta[4];         // x = active, y = grid width
    Vector4 samples[WIND_TERRAIN_PACKED_VEC4S]; // height, valid, height, valid
} WindTerrainGPU;

static inline void MotionGpu_PackWind(WindGPU *out) {
    memset(out, 0, sizeof(*out));
        WindMacroConfig macro = Wind_GetMacro();
        out->macro_dir_amp = (Vector4){ macro.baseDirection.x, macro.baseDirection.y, macro.baseDirection.z, macro.gustAmplitude };
        int vortCount = 0;
        const VorticleData *vArray = Wind_GetActiveVorticles(&vortCount);
        if (vortCount > MAX_GPU_VORTICLES) vortCount = MAX_GPU_VORTICLES;
        out->macro_params = (Vector4){ macro.noiseScale, macro.noiseSpeed,
                                           (float)vortCount, macro.terrainLiftK };
        out->macro_extra  = (Vector4){ macro.heightGradientK, (float)Wind_GetPublishedMotionAirflowCount(), 0.0f, 0.0f };
        WindGuidingGust guiding=Wind_GetGuidingWindState();
        out->guidingOrigin=MotionGpu_V4(guiding.playerPos,guiding.active?1:0);
        out->guidingTarget=MotionGpu_V4(guiding.targetPos,guiding.intensity);
        out->guidingDirection=MotionGpu_V4(guiding.direction,guiding.speed);
        for (int v = 0; v < vortCount; v++) {
            out->vorticles[v].pos_radius = (Vector4){ vArray[v].position.x, vArray[v].position.y, vArray[v].position.z, vArray[v].radius };
            out->vorticles[v].dir_strength = (Vector4){ vArray[v].direction.x, vArray[v].direction.y, vArray[v].direction.z, vArray[v].strength };
            out->vorticles[v].params = (Vector4){ (float)vArray[v].type, vArray[v].lifetime, vArray[v].maxLifetime, vArray[v].inwardPull };
        }
}
static inline void MotionGpu_PackWindTerrain(const WindTerrainGrid *terrainGrid, WindTerrainGPU *out) {
    memset(out, 0, sizeof(*out));
            out->origin_cell = (Vector4){
                terrainGrid->originXZ.x, terrainGrid->originXZ.y,
                terrainGrid->cellSizeXZ.x, terrainGrid->cellSizeXZ.y,
            };
            out->meta[0] = terrainGrid->active ? 1 : 0;
            out->meta[1] = WIND_TERRAIN_GRID_SIZE;
            for (int i = 0; i < WIND_TERRAIN_GRID_SAMPLES; i += 2) {
                out->samples[i / 2] = (Vector4){
                    terrainGrid->heights[i], (float)terrainGrid->valid[i],
                    terrainGrid->heights[i + 1], (float)terrainGrid->valid[i + 1],
                };
            }
}
#endif
