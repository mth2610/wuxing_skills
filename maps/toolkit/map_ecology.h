#ifndef MAP_ECOLOGY_H
#define MAP_ECOLOGY_H
#include "raylib.h"
#include <stdbool.h>
#define MAP_ECOLOGY_RES 1024
#define MAP_ECOLOGY_DISTANCE_RANGE 16.0f
#define MAP_ECOLOGY_MAX_PATHS 16
/* RGBA: coverage, growth, moisture, habitat. Distance RG/BA: signed
 * road/shore meters, encoded as high byte plus fractional low byte. */
typedef struct MapEcology {
    Texture2D habitatTexture, distanceTexture;
    Vector4 rect;
    bool ready;
} MapEcology;
typedef struct {
    float coverage, growth, moisture, habitat, roadDistance, shoreDistance;
} MapEcologySample;
typedef struct {
    Vector4 rect, lake;
    const Vector4 *paths;
    int pathCount;
    float roadHalfWidth;
} MapEcologyConfig;
typedef float (*MapEcologyDensityFn)(float x, float z, void *user);
typedef bool (*MapEcologyEligibleFn)(float x, float z, void *user);
/* Single cached dataset; all callbacks are init-only. One live ecology bake. */
bool MapEcology_Bake(MapEcology *map, const MapEcologyConfig *config,
                     MapEcologyDensityFn density, MapEcologyEligibleFn eligible, void *user);
MapEcologySample MapEcology_Sample(const MapEcology *map, float x, float z);
void MapEcology_Unload(MapEcology *map);
float MapEcology_RoadDistance(const MapEcologyConfig *config, float x, float z);
float MapEcology_ShoreDistance(Vector4 lake, float x, float z);
#endif
