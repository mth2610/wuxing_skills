// Shared Wind storage ABI, bindings 3/4.
#define MAX_GPU_VORTICLES 256

struct VorticleGPU {
    vec4 pos_radius;      // xyz = position, w = radius
    vec4 dir_strength;    // xyz = direction, w = strength
    vec4 params;          // x = type, y = lifetime, z = maxLifetime, w = inwardPull
};

struct WindGPU {
    vec4 macro_dir_amp;   // xyz = baseDirection, w = gustAmplitude
    vec4 macro_params;    // x = noiseScale, y = noiseSpeed, z = activeCount, w = terrainLiftK
    vec4 macro_extra;     // x = heightGradientK, yzw = reserved
    VorticleGPU vorticles[MAX_GPU_VORTICLES];
    vec4 guidingOrigin, guidingTarget, guidingDirection;
};

layout(std430, binding = 3) readonly buffer WindBuffer {
    WindGPU uWind;
};

#define WIND_TERRAIN_GRID_SIZE 32
#define WIND_TERRAIN_PACKED_VEC4S 512

struct WindTerrainGPU {
    vec4 origin_cell; // xy = world XZ origin, zw = XZ cell size
    ivec4 meta;       // x = active, y = grid width
    // Two samples per vec4: (height0, valid0, height1, valid1).
    vec4 samples[WIND_TERRAIN_PACKED_VEC4S];
};

layout(std430, binding = 4) readonly buffer WindTerrainBuffer {
    WindTerrainGPU uWindTerrain;
};

