#ifndef CRYSTAL_MESH_GENERATOR_H
#define CRYSTAL_MESH_GENERATOR_H

#include "raylib.h"

typedef struct {
    float height;
    float radius;
    float taper;       // 0 = cylinder/prism, 1 = sharp tip cone
    float twist;       // total rotation from bottom to top (radians)
    float noise;       // random vertex noise (0..1)
    float bevel;       // bevel amount on edges (0..1)
    float split;       // split tip count/amount
    int sides;         // number of radial sides (e.g. 6 for hexagonal)
    int segments;      // number of vertical slices (e.g. 8)
} CrystalDesc;

// Draws a procedural low-poly crystal using rlgl immediate mode
void ProceduralMesh_DrawCrystal(Vector3 pos, const CrystalDesc *desc, float progress, Color color);

// Draws a cluster of crystals
void ProceduralMesh_DrawCrystalCluster(Vector3 center, const CrystalDesc *desc, int count, int seed, float progress, Color color);

#endif // CRYSTAL_MESH_GENERATOR_H
