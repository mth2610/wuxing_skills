#ifndef MESH_ADJACENCY_H
#define MESH_ADJACENCY_H

#include "raylib.h"
#include <stdbool.h>

#define MAX_TOPOLOGY_VERTICES 8192
#define MAX_VERTEX_NEIGHBORS 8

typedef struct MeshAdjacency {
    int count;
    Vector3 vertices[MAX_TOPOLOGY_VERTICES];
    unsigned short neighbors[MAX_TOPOLOGY_VERTICES][MAX_VERTEX_NEIGHBORS];
    unsigned char neighborCount[MAX_TOPOLOGY_VERTICES];
} MeshAdjacency;

// Build topological adjacency graph by welding duplicate vertices
void MeshAdjacency_Build(MeshAdjacency *out, Mesh mesh);

// Sample a random vertex position
Vector3 MeshAdjacency_SampleVertex(const MeshAdjacency *adj);

// Sample a random edge point (lerp between two neighbors)
Vector3 MeshAdjacency_SampleEdge(const MeshAdjacency *adj);

// Generate a random path (walk) on the mesh topology graph
// starting at a random vertex, and jumping to a random neighbor.
// Avoids backtrack (doesn't go back to the immediate previous vertex) unless stuck.
// Returns actual path length written to `outPath`.
int MeshAdjacency_GeneratePath(const MeshAdjacency *adj, int startVertex, int length, Vector3 *outPath);

#endif // MESH_ADJACENCY_H
