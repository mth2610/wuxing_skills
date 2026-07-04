#include "mesh_cache.h"
#include "raymath.h"
#include <math.h>

#define CACHE_SIZE 16

typedef struct {
    int seed;
    float jaggedness;
    RockMeshData data;
    bool active;
} RockCache;
static RockCache s_rockCache[CACHE_SIZE];

typedef struct {
    int seed;
    float sharpness;
    ShardClusterMeshData data;
    bool active;
} IceCache;
static IceCache s_iceCache[CACHE_SIZE];

void MeshCache_Init(void) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        s_rockCache[i].active = false;
        s_iceCache[i].active = false;
    }
}

void MeshCache_Unload(void) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        s_rockCache[i].active = false;
        s_iceCache[i].active = false;
    }
}

RockMeshData* MeshCache_GetRock(int seed, float jaggedness) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_rockCache[i].active && s_rockCache[i].seed == seed && fabsf(s_rockCache[i].jaggedness - jaggedness) < 0.01f) {
            return &s_rockCache[i].data;
        }
    }
    int slot = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_rockCache[i].active) { slot = i; break; }
    }
    s_rockCache[slot].active = true;
    s_rockCache[slot].seed = seed;
    s_rockCache[slot].jaggedness = jaggedness;
    ProceduralMesh_BuildRock(&s_rockCache[slot].data, Vector3Zero(), 1.0f, jaggedness, seed, 2);
    return &s_rockCache[slot].data;
}

ShardClusterMeshData* MeshCache_GetIce(int seed, float sharpness) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_iceCache[i].active && s_iceCache[i].seed == seed && fabsf(s_iceCache[i].sharpness - sharpness) < 0.01f) {
            return &s_iceCache[i].data;
        }
    }
    int slot = 0;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_iceCache[i].active) { slot = i; break; }
    }
    s_iceCache[slot].active = true;
    s_iceCache[slot].seed = seed;
    s_iceCache[slot].sharpness = sharpness;
    
    ShardClusterConfig cfg = ProceduralMesh_DefaultShardClusterConfig();
    cfg.thicknessMin = 0.4f;
    cfg.thicknessMax = 0.8f;
    cfg.tipSharpness = sharpness;
    cfg.sides = 6;
    ProceduralMesh_BuildShardCluster(&s_iceCache[slot].data, Vector3Zero(), (Vector3){0,1,0}, 4, 0.7f, 1.2f, seed, &cfg);
    return &s_iceCache[slot].data;
}
