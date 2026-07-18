#include "core/mesh_adjacency.h"
#include "core/ribbon_strip.h"
#include "core/resource_manager.h"
#include "core/force_field.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

#define ARCH_MAX_MESH_ELECTRICS 16
#define MAX_ELECTRIC_ARCS 16
#define ELECTRIC_PATH_LEN 8
#define SMOOTH_POINTS_COUNT 32

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
    const ForceField *forceField;
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

int VFX_SpawnMeshElectricity(const struct MeshAdjacency *adj, Color color, float duration, const struct ForceField *forceField) {
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
            s_archElectrics[i].forceField = (const ForceField *)forceField;
            
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

// Helpers for Catmull-Rom spline interpolation
static Vector3 VC_MeshElectricity_GetPathPoint(const Arch_MeshElectricity *e, int a, int idx) {
    if (idx < 0) idx = 0;
    if (idx >= e->pathLengths[a]) idx = e->pathLengths[a] - 1;
    return e->adj->vertices[e->paths[a][idx]];
}

static Vector3 VC_MeshElectricity_InterpolateCatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    Vector3 res;
    res.x = 0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + 
             (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + 
             (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3);
    res.y = 0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + 
             (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + 
             (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3);
    res.z = 0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + 
             (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + 
             (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3);
    return res;
}

static void VC_MeshElectricity_Draw3D(Camera3D cam) {
    Texture2D tex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
    
    for (int i = 0; i < ARCH_MAX_MESH_ELECTRICS; i++) {
        if (!s_archElectrics[i].active) continue;

        // Draw crawling electric discharge filaments
        for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
            int len = s_archElectrics[i].pathLengths[a];
            if (len < 2) continue;

            RibbonPoint points[SMOOTH_POINTS_COUNT];
            float intensity = 0.7f + 0.3f * sinf(GetTime() * 10.0f + a * 3.0f);

            for (int j = 0; j < SMOOTH_POINTS_COUNT; j++) {
                float u = (float)j / (float)(SMOOTH_POINTS_COUNT - 1); // 0 at head, 1 at tail
                
                // Determine segments and local t
                float segmentF = u * (len - 1);
                int segIdx = (int)segmentF;
                if (segIdx >= len - 1) segIdx = len - 2;
                float t = segmentF - (float)segIdx;

                // Sample control points
                Vector3 p0 = VC_MeshElectricity_GetPathPoint(&s_archElectrics[i], a, segIdx - 1);
                Vector3 p1 = VC_MeshElectricity_GetPathPoint(&s_archElectrics[i], a, segIdx);
                Vector3 p2 = VC_MeshElectricity_GetPathPoint(&s_archElectrics[i], a, segIdx + 1);
                Vector3 p3 = VC_MeshElectricity_GetPathPoint(&s_archElectrics[i], a, segIdx + 2);

                // Interpolate position
                Vector3 localPos = VC_MeshElectricity_InterpolateCatmullRom(p0, p1, p2, p3, t);

                // Slow, smooth wave wobble
                float wobbleSpeed = 6.0f;
                float wobbleAmt = 0.012f;
                Vector3 wobble = (Vector3){
                    (float)sinf(GetTime() * wobbleSpeed + j * 0.4f) * wobbleAmt,
                    (float)cosf(GetTime() * wobbleSpeed + j * 0.5f) * wobbleAmt,
                    (float)sinf(GetTime() * wobbleSpeed - j * 0.3f) * wobbleAmt
                };
                Vector3 finalLocal = Vector3Add(localPos, wobble);
                Vector3 worldPosNoForce = Vector3Transform(finalLocal, s_archElectrics[i].transform);

                // Evaluate and apply Force Field influence
                Vector3 force = (Vector3){0};
                if (s_archElectrics[i].forceField) {
                    force = ForceField_Evaluate(s_archElectrics[i].forceField, worldPosNoForce, (Vector3){0}, s_archElectrics[i].elapsed, (Vector3){0}, (Vector3){0});
                }
                Vector3 displacement = Vector3Scale(force, 0.04f); // 4cm per unit strength
                Vector3 worldPos = Vector3Add(worldPosNoForce, displacement);

                points[j].position = worldPos;
                
                // Taper width from head to tail (1.2cm to 0.2cm half-width)
                float baseWidth = 0.012f * (1.0f - u * 0.8f);
                points[j].halfWidth = baseWidth * (0.8f + 0.2f * sinf(GetTime() * 15.0f + j * 0.5f));
                
                // Fade opacity towards the tail (100% to 10%)
                float fadeAlpha = 1.0f - u * 0.9f;
                points[j].tint = ColorAlpha(s_archElectrics[i].color, intensity * fadeAlpha);
                points[j].v = u;
            }

            // Draw using the standard camera-facing ribbon
            DrawRibbonStrip(points, SMOOTH_POINTS_COUNT, tex, cam);
        }
    }
}

void VFX_ComposeMeshElectricity(Vector3 position, Color color, float duration) {
    int handle = VFX_SpawnMeshElectricity(NULL, color, duration, NULL);
    if (handle != -1) {
        VFX_UpdateMeshElectricity(handle, MatrixTranslate(position.x, position.y, position.z));
    }
}

void ComposeMeshElectricityEx(Vector3 position, Color color, float duration, const struct ForceField *forceField) {
    int handle = VFX_SpawnMeshElectricity(NULL, color, duration, forceField);
    if (handle != -1) {
        VFX_UpdateMeshElectricity(handle, MatrixTranslate(position.x, position.y, position.z));
    }
}
