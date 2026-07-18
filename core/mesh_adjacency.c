#include "mesh_adjacency.h"
#include "raymath.h"
#include <string.h>

void MeshAdjacency_Build(MeshAdjacency *out, Mesh mesh) {
    if (!out) return;
    out->count = 0;
    for (int i = 0; i < MAX_TOPOLOGY_VERTICES; i++) {
        out->neighborCount[i] = 0;
    }

    if (mesh.vertexCount == 0 || !mesh.vertices) return;

    int limit = mesh.vertexCount;
    if (limit > MAX_TOPOLOGY_VERTICES) limit = MAX_TOPOLOGY_VERTICES;
    
    // We store the mapping from original vertex index to welded index
    unsigned short weldMap[MAX_TOPOLOGY_VERTICES];

    for (int i = 0; i < limit; i++) {
        float x = mesh.vertices[i * 3 + 0];
        float y = mesh.vertices[i * 3 + 1];
        float z = mesh.vertices[i * 3 + 2];
        Vector3 v = (Vector3){x, y, z};

        // Find duplicate (welding)
        int foundIdx = -1;
        for (int j = 0; j < out->count; j++) {
            float dx = out->vertices[j].x - v.x;
            float dy = out->vertices[j].y - v.y;
            float dz = out->vertices[j].z - v.z;
            float distSq = dx*dx + dy*dy + dz*dz;
            if (distSq < 0.00001f) {
                foundIdx = j;
                break;
            }
        }

        if (foundIdx != -1) {
            weldMap[i] = (unsigned short)foundIdx;
        } else {
            int newIdx = out->count++;
            out->vertices[newIdx] = v;
            weldMap[i] = (unsigned short)newIdx;
        }
    }

    // Now populate edges
    if (mesh.indices) {
        int triCount = mesh.triangleCount;
        for (int i = 0; i < triCount; i++) {
            int i0 = mesh.indices[i * 3 + 0];
            int i1 = mesh.indices[i * 3 + 1];
            int i2 = mesh.indices[i * 3 + 2];

            if (i0 < limit && i1 < limit && i2 < limit) {
                int w0 = weldMap[i0];
                int w1 = weldMap[i1];
                int w2 = weldMap[i2];

                // w0 <-> w1
                if (w0 != w1) {
                    bool exists = false;
                    for (int n = 0; n < out->neighborCount[w0]; n++) {
                        if (out->neighbors[w0][n] == w1) { exists = true; break; }
                    }
                    if (!exists && out->neighborCount[w0] < MAX_VERTEX_NEIGHBORS) {
                        out->neighbors[w0][out->neighborCount[w0]++] = (unsigned short)w1;
                    }
                    
                    exists = false;
                    for (int n = 0; n < out->neighborCount[w1]; n++) {
                        if (out->neighbors[w1][n] == w0) { exists = true; break; }
                    }
                    if (!exists && out->neighborCount[w1] < MAX_VERTEX_NEIGHBORS) {
                        out->neighbors[w1][out->neighborCount[w1]++] = (unsigned short)w0;
                    }
                }
                
                // w1 <-> w2
                if (w1 != w2) {
                    bool exists = false;
                    for (int n = 0; n < out->neighborCount[w1]; n++) {
                        if (out->neighbors[w1][n] == w2) { exists = true; break; }
                    }
                    if (!exists && out->neighborCount[w1] < MAX_VERTEX_NEIGHBORS) {
                        out->neighbors[w1][out->neighborCount[w1]++] = (unsigned short)w2;
                    }
                    
                    exists = false;
                    for (int n = 0; n < out->neighborCount[w2]; n++) {
                        if (out->neighbors[w2][n] == w1) { exists = true; break; }
                    }
                    if (!exists && out->neighborCount[w2] < MAX_VERTEX_NEIGHBORS) {
                        out->neighbors[w2][out->neighborCount[w2]++] = (unsigned short)w1;
                    }
                }

                // w2 <-> w0
                if (w2 != w0) {
                    bool exists = false;
                    for (int n = 0; n < out->neighborCount[w2]; n++) {
                        if (out->neighbors[w2][n] == w0) { exists = true; break; }
                    }
                    if (!exists && out->neighborCount[w2] < MAX_VERTEX_NEIGHBORS) {
                        out->neighbors[w2][out->neighborCount[w2]++] = (unsigned short)w0;
                    }
                    
                    exists = false;
                    for (int n = 0; n < out->neighborCount[w0]; n++) {
                        if (out->neighbors[w0][n] == w2) { exists = true; break; }
                    }
                    if (!exists && out->neighborCount[w0] < MAX_VERTEX_NEIGHBORS) {
                        out->neighbors[w0][out->neighborCount[w0]++] = (unsigned short)w2;
                    }
                }
            }
        }
    } else {
        int triCount = limit / 3;
        for (int i = 0; i < triCount; i++) {
            int i0 = i * 3 + 0;
            int i1 = i * 3 + 1;
            int i2 = i * 3 + 2;

            int w0 = weldMap[i0];
            int w1 = weldMap[i1];
            int w2 = weldMap[i2];

            // w0 <-> w1
            if (w0 != w1) {
                bool exists = false;
                for (int n = 0; n < out->neighborCount[w0]; n++) {
                    if (out->neighbors[w0][n] == w1) { exists = true; break; }
                }
                if (!exists && out->neighborCount[w0] < MAX_VERTEX_NEIGHBORS) {
                    out->neighbors[w0][out->neighborCount[w0]++] = (unsigned short)w1;
                }
                
                exists = false;
                for (int n = 0; n < out->neighborCount[w1]; n++) {
                    if (out->neighbors[w1][n] == w0) { exists = true; break; }
                }
                if (!exists && out->neighborCount[w1] < MAX_VERTEX_NEIGHBORS) {
                    out->neighbors[w1][out->neighborCount[w1]++] = (unsigned short)w0;
                }
            }
            
            // w1 <-> w2
            if (w1 != w2) {
                bool exists = false;
                for (int n = 0; n < out->neighborCount[w1]; n++) {
                    if (out->neighbors[w1][n] == w2) { exists = true; break; }
                }
                if (!exists && out->neighborCount[w1] < MAX_VERTEX_NEIGHBORS) {
                    out->neighbors[w1][out->neighborCount[w1]++] = (unsigned short)w2;
                }
                
                exists = false;
                for (int n = 0; n < out->neighborCount[w2]; n++) {
                    if (out->neighbors[w2][n] == w1) { exists = true; break; }
                }
                if (!exists && out->neighborCount[w2] < MAX_VERTEX_NEIGHBORS) {
                    out->neighbors[w2][out->neighborCount[w2]++] = (unsigned short)w1;
                }
            }

            // w2 <-> w0
            if (w2 != w0) {
                bool exists = false;
                for (int n = 0; n < out->neighborCount[w2]; n++) {
                    if (out->neighbors[w2][n] == w0) { exists = true; break; }
                }
                if (!exists && out->neighborCount[w2] < MAX_VERTEX_NEIGHBORS) {
                    out->neighbors[w2][out->neighborCount[w2]++] = (unsigned short)w0;
                }
                
                exists = false;
                for (int n = 0; n < out->neighborCount[w0]; n++) {
                    if (out->neighbors[w0][n] == w2) { exists = true; break; }
                }
                if (!exists && out->neighborCount[w0] < MAX_VERTEX_NEIGHBORS) {
                    out->neighbors[w0][out->neighborCount[w0]++] = (unsigned short)w2;
                }
            }
        }
    }
}

Vector3 MeshAdjacency_SampleVertex(const MeshAdjacency *adj) {
    if (!adj || adj->count == 0) return (Vector3){0};
    int rIdx = GetRandomValue(0, adj->count - 1);
    return adj->vertices[rIdx];
}

Vector3 MeshAdjacency_SampleEdge(const MeshAdjacency *adj) {
    if (!adj || adj->count == 0) return (Vector3){0};
    int v0 = GetRandomValue(0, adj->count - 1);
    if (adj->neighborCount[v0] == 0) return adj->vertices[v0];
    int nIdx = GetRandomValue(0, adj->neighborCount[v0] - 1);
    int v1 = adj->neighbors[v0][nIdx];
    float t = (float)GetRandomValue(0, 1000) / 1000.0f;
    return Vector3Lerp(adj->vertices[v0], adj->vertices[v1], t);
}

int MeshAdjacency_GeneratePath(const MeshAdjacency *adj, int startVertex, int length, Vector3 *outPath) {
    if (!adj || adj->count == 0 || length <= 0 || !outPath) return 0;

    int current = startVertex;
    if (current < 0 || current >= adj->count) {
        current = GetRandomValue(0, adj->count - 1);
    }

    outPath[0] = adj->vertices[current];
    int pathLength = 1;
    int prev = -1;

    for (int i = 1; i < length; i++) {
        if (adj->neighborCount[current] == 0) break;

        int next = -1;
        
        // Non-backtracking random selection if multiple choices exist
        if (adj->neighborCount[current] > 1 && prev != -1) {
            int eligible[MAX_VERTEX_NEIGHBORS];
            int eligibleCount = 0;
            for (int n = 0; n < adj->neighborCount[current]; n++) {
                int neighbor = adj->neighbors[current][n];
                if (neighbor != prev) {
                    eligible[eligibleCount++] = neighbor;
                }
            }
            if (eligibleCount > 0) {
                int rIdx = GetRandomValue(0, eligibleCount - 1);
                next = eligible[rIdx];
            }
        }

        if (next == -1) {
            int rIdx = GetRandomValue(0, adj->neighborCount[current] - 1);
            next = adj->neighbors[current][rIdx];
        }

        prev = current;
        current = next;
        outPath[pathLength++] = adj->vertices[current];
    }

    return pathLength;
}
