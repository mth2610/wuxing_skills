#include "core/mesh_adjacency.h"
#include "core/ribbon_strip.h"
#include "core/resource_manager.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

#define ARCH_MAX_MESH_ELECTRICS 16
#define MAX_ELECTRIC_ARCS 16
#define ELECTRIC_PATH_LEN 8

typedef struct {
    bool active;
    const MeshAdjacency *adj;
    Matrix transform;
    float duration;
    float elapsed;
    Color color;
    unsigned short paths[MAX_ELECTRIC_ARCS][ELECTRIC_PATH_LEN];
    unsigned char pathLengths[MAX_ELECTRIC_ARCS];
    short stepsRemaining[MAX_ELECTRIC_ARCS];
    float moveTimer;
} Arch_MeshElectricity;

static Arch_MeshElectricity s_archElectrics[ARCH_MAX_MESH_ELECTRICS];

static void VC_MeshElectricity_InitArc(Arch_MeshElectricity *e, int a) {
    if (!e->adj || e->adj->count == 0) return;
    int startVertex = GetRandomValue(0, e->adj->count - 1);
    e->paths[a][0] = (unsigned short)startVertex;
    e->pathLengths[a] = 1;
    e->stepsRemaining[a] = GetRandomValue(15, 30);
    
    // Generate a valid initial walk of length ELECTRIC_PATH_LEN to avoid zero-length segments
    int current = startVertex;
    int prev = -1;
    for (int p = 1; p < ELECTRIC_PATH_LEN; p++) {
        if (e->adj->neighborCount[current] == 0) {
            e->paths[a][p] = (unsigned short)current;
            continue;
        }
        int next = -1;
        if (e->adj->neighborCount[current] > 1 && prev != -1) {
            int eligible[MAX_VERTEX_NEIGHBORS];
            int eligibleCount = 0;
            for (int n = 0; n < e->adj->neighborCount[current]; n++) {
                int neighbor = e->adj->neighbors[current][n];
                if (neighbor != prev) {
                    eligible[eligibleCount++] = neighbor;
                }
            }
            if (eligibleCount > 0) {
                next = eligible[GetRandomValue(0, eligibleCount - 1)];
            }
        }
        if (next == -1) {
            next = e->adj->neighbors[current][GetRandomValue(0, e->adj->neighborCount[current] - 1)];
        }
        prev = current;
        current = next;
        e->paths[a][p] = (unsigned short)current;
        e->pathLengths[a]++;
    }
}

// Fallback mesh adjacency generated from a Torus
static MeshAdjacency s_fallbackAdjacency;
static bool s_fallbackBuilt = false;

int VFX_SpawnMeshElectricity(const MeshAdjacency *adj, Color color, float duration) {
    if (adj == NULL) {
        if (!s_fallbackBuilt) {
            // Using a Torus mesh scaled to fit the 2m sandbox viewport (0.25f normalized tube thickness, 3.2f scale)
            // Center Radius = 1.6m, Tube Radius = 0.4m, Outer Radius = 2.0m, Inner Radius = 1.2m (clear hollow center)
            Mesh torusMesh = GenMeshTorus(0.25f, 3.2f, 16, 48);
            MeshAdjacency_Build(&s_fallbackAdjacency, torusMesh);
            UnloadMesh(torusMesh);
            s_fallbackBuilt = true;
        }
        adj = &s_fallbackAdjacency;
    }

    for (int i = 0; i < ARCH_MAX_MESH_ELECTRICS; i++) {
        if (!s_archElectrics[i].active) {
            s_archElectrics[i].active = true;
            s_archElectrics[i].adj = adj;
            s_archElectrics[i].transform = MatrixIdentity();
            s_archElectrics[i].duration = duration;
            s_archElectrics[i].elapsed = 0.0f;
            s_archElectrics[i].color = color;
            s_archElectrics[i].moveTimer = 0.0f;
            
            // Initialize all crawling filaments
            for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
                VC_MeshElectricity_InitArc(&s_archElectrics[i], a);
            }
            return i;
        }
    }
    return -1;
}

void VFX_UpdateMeshElectricity(int handle, Matrix transform) {
    if (handle < 0 || handle >= ARCH_MAX_MESH_ELECTRICS || !s_archElectrics[handle].active) return;
    s_archElectrics[handle].transform = transform;
}

void VFX_KillMeshElectricity(int handle) {
    if (handle < 0 || handle >= ARCH_MAX_MESH_ELECTRICS) return;
    s_archElectrics[handle].active = false;
}

static void VC_MeshElectricity_Update(float dt) {
    for (int i = 0; i < ARCH_MAX_MESH_ELECTRICS; i++) {
        if (!s_archElectrics[i].active) continue;
        s_archElectrics[i].elapsed += dt;
        if (s_archElectrics[i].elapsed >= s_archElectrics[i].duration) {
            s_archElectrics[i].active = false;
            continue;
        }

        // Crawling update tick - slower speed (0.10s per step)
        s_archElectrics[i].moveTimer += dt;
        if (s_archElectrics[i].moveTimer >= 0.10f) {
            s_archElectrics[i].moveTimer = 0.0f;

            for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
                s_archElectrics[i].stepsRemaining[a]--;
                
                if (s_archElectrics[i].stepsRemaining[a] <= 0) {
                    // Re-initialize/spawn at a new random location
                    VC_MeshElectricity_InitArc(&s_archElectrics[i], a);
                } else {
                    int head = s_archElectrics[i].paths[a][0];
                    int prev = (s_archElectrics[i].pathLengths[a] > 1) ? s_archElectrics[i].paths[a][1] : -1;
                    
                    int next = -1;
                    int neighborCount = s_archElectrics[i].adj->neighborCount[head];
                    
                    if (neighborCount > 0) {
                        // Find neighbors that aren't the previous vertex to keep moving forward
                        if (neighborCount > 1 && prev != -1) {
                            int eligible[MAX_VERTEX_NEIGHBORS];
                            int eligibleCount = 0;
                            for (int n = 0; n < neighborCount; n++) {
                                int neighborIndex = s_archElectrics[i].adj->neighbors[head][n];
                                if (neighborIndex != prev) {
                                    eligible[eligibleCount++] = neighborIndex;
                                }
                            }
                            if (eligibleCount > 0) {
                                next = eligible[GetRandomValue(0, eligibleCount - 1)];
                            }
                        }
                        
                        if (next == -1) {
                            next = s_archElectrics[i].adj->neighbors[head][GetRandomValue(0, neighborCount - 1)];
                        }
                    }
                    
                    if (next != -1) {
                        // Shift path to the right
                        for (int p = ELECTRIC_PATH_LEN - 1; p > 0; p--) {
                            s_archElectrics[i].paths[a][p] = s_archElectrics[i].paths[a][p - 1];
                        }
                        s_archElectrics[i].paths[a][0] = (unsigned short)next;
                        if (s_archElectrics[i].pathLengths[a] < ELECTRIC_PATH_LEN) {
                            s_archElectrics[i].pathLengths[a]++;
                        }
                    }
                }
            }
        }
    }
}

static void VC_MeshElectricity_Draw3D(Camera3D cam) {
    Texture2D tex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
    
    for (int i = 0; i < ARCH_MAX_MESH_ELECTRICS; i++) {
        if (!s_archElectrics[i].active) continue;

        // Draw crawling electric discharge filaments
        for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
            int len = s_archElectrics[i].pathLengths[a];
            if (len < 2) continue;

            RibbonPoint points[ELECTRIC_PATH_LEN];
            for (int p = 0; p < len; p++) {
                int vIdx = s_archElectrics[i].paths[a][p];
                Vector3 localPos = s_archElectrics[i].adj->vertices[vIdx];

                // Scale down jitter (4cm) to keep arcs close to the mesh surface
                float jitterAmount = 0.04f;
                float frameJitter = sinf(GetTime() * 120.0f + p * 20.0f) * 0.02f;
                
                Vector3 jitter = (Vector3){
                    (float)sinf(GetTime() * 60.0f + vIdx * 9.0f) * jitterAmount + frameJitter,
                    (float)cosf(GetTime() * 63.0f + vIdx * 13.0f) * jitterAmount + frameJitter,
                    (float)sinf(GetTime() * 57.0f - vIdx * 17.0f) * jitterAmount + frameJitter
                };
                Vector3 finalLocal = Vector3Add(localPos, jitter);
                Vector3 worldPos = Vector3Transform(finalLocal, s_archElectrics[i].transform);

                points[p].position = worldPos;
                // Scale width down for thin, elegant filaments (3cm to 4cm total width)
                points[p].halfWidth = 0.015f + 0.005f * sinf(GetTime() * 90.0f + p * 4.0f);
                
                // Pulsing electrical color brightness
                float intensity = 0.75f + 0.25f * sinf(GetTime() * 140.0f + a * 5.0f);
                points[p].tint = ColorAlpha(s_archElectrics[i].color, intensity);
                points[p].v = (float)p / (float)(len - 1);
            }

            // Draw using the standard camera-facing ribbon
            DrawRibbonStrip(points, len, tex, cam);
        }
    }
}

void VFX_ComposeMeshElectricity(Vector3 position, Color color, float duration) {
    int handle = VFX_SpawnMeshElectricity(NULL, color, duration);
    if (handle != -1) {
        VFX_UpdateMeshElectricity(handle, MatrixTranslate(position.x, position.y, position.z));
    }
}
