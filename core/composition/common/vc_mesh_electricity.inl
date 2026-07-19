#include "core/mesh_adjacency.h"
#include "core/trail_system.h"
#include "core/color_gradient.h"
#include "core/vfx_light.h"
#include "core/force_field.h"
#include "core/resource_manager.h"
#include "raylib.h"
#include "raymath.h"
#include <stdbool.h>

// Mỗi instance có MAX_ELECTRIC_ARCS arc, mỗi arc = 1 TRAIL_TYPE_FOLLOWER.
// Trail tip được push mỗi frame theo head vertex hiện tại.
// smoothSpline=true → trail system Catmull-Rom nội suy giữa các history node.
//
// Key fix: trail được spawn LAZY ở frame đầu tiên của Update — lúc đó
// transform đã được set đúng bởi VFX_UpdateMeshElectricity. Nếu spawn
// trong VFX_SpawnMeshElectricity thì transform vẫn là MatrixIdentity(),
// tip bắt đầu tại (0,0,0) rồi "flash" ra vị trí đúng ở frame tiếp theo.

#define ARCH_MAX_MESH_ELECTRICS 16
#define MAX_ELECTRIC_ARCS       16
#define ELECTRIC_PATH_LEN        8
#define ELECTRIC_TRAIL_NODES    14
#define CRAWL_INTERVAL          0.10f

typedef struct {
    bool  active;
    bool  trailsSpawned;  // lazy: spawn trail lần đầu ở Update, không phải Spawn
    const MeshAdjacency *adj;
    Matrix transform;
    float  duration;
    float  elapsed;
    Color  color;

    unsigned short paths[MAX_ELECTRIC_ARCS][ELECTRIC_PATH_LEN];
    unsigned char  pathLengths[MAX_ELECTRIC_ARCS];
    short          stepsRemaining[MAX_ELECTRIC_ARCS];
    float          moveTimer;

    int trailIds[MAX_ELECTRIC_ARCS];

    const ForceField *forceField;
} Arch_MeshElectricity;

static Arch_MeshElectricity s_archElectrics[ARCH_MAX_MESH_ELECTRICS];

// ── Gradient ─────────────────────────────────────────────────────────────────

static ColorGradient s_elecGrad = {0};

static void ElecGrad_Init(Color tint)
{
    s_elecGrad.count = 0;
    ColorGradient_AddStop(&s_elecGrad, 0.0f,
        (Color){(unsigned char)fminf(tint.r+80,255),
                (unsigned char)fminf(tint.g+80,255),
                (unsigned char)fminf(tint.b+80,255), 255});
    ColorGradient_AddStop(&s_elecGrad, 0.25f, tint);
    ColorGradient_AddStop(&s_elecGrad, 0.7f,
        (Color){tint.r/2, tint.g/2, tint.b/2, 120});
    ColorGradient_AddStop(&s_elecGrad, 1.0f,
        (Color){tint.r/3, tint.g/3, tint.b/3, 0});
}

// ── Path walk init (không spawn trail — chỉ init path data) ──────────────────

static void VC_MeshElectricity_InitPath(Arch_MeshElectricity *e, int a)
{
    if (!e->adj || e->adj->count == 0) return;
    int startVertex = GetRandomValue(0, e->adj->count - 1);
    e->paths[a][0]       = (unsigned short)startVertex;
    e->pathLengths[a]    = 1;
    e->stepsRemaining[a] = GetRandomValue(15, 30);

    int current = startVertex, prev = -1;
    for (int p = 1; p < ELECTRIC_PATH_LEN; p++) {
        if (e->adj->neighborCount[current] == 0) { e->paths[a][p] = (unsigned short)current; continue; }
        int next = -1;
        if (e->adj->neighborCount[current] > 1 && prev != -1) {
            int eligible[MAX_VERTEX_NEIGHBORS], cnt = 0;
            for (int n = 0; n < e->adj->neighborCount[current]; n++) {
                int nb = e->adj->neighbors[current][n];
                if (nb != prev) eligible[cnt++] = nb;
            }
            if (cnt > 0) next = eligible[GetRandomValue(0, cnt-1)];
        }
        if (next == -1)
            next = e->adj->neighbors[current][GetRandomValue(0, e->adj->neighborCount[current]-1)];
        prev = current; current = next;
        e->paths[a][p] = (unsigned short)current;
        e->pathLengths[a]++;
    }
}

// ── Spawn trail cho 1 arc (gọi khi transform đã đúng) ────────────────────────

static void VC_MeshElectricity_SpawnTrail(Arch_MeshElectricity *e, int a)
{
    // Kill trail cũ nếu còn
    if (e->trailIds[a] >= 0) {
        KillTrail(e->trailIds[a]);
        e->trailIds[a] = -1;
    }

    // Re-init path walk tại vị trí random mới
    VC_MeshElectricity_InitPath(e, a);

    Vector3 headLocal = e->adj->vertices[e->paths[a][0]];
    Vector3 headWorld = Vector3Transform(headLocal, e->transform);

    ElecGrad_Init(e->color);

    TrailConfig tcfg    = {0};
    tcfg.type           = TRAIL_TYPE_FOLLOWER;
    tcfg.pos            = headWorld;
    tcfg.thick          = 0.012f;
    tcfg.len            = 0.018f;
    tcfg.trailLength    = (float)ELECTRIC_TRAIL_NODES;
    tcfg.life           = e->duration - e->elapsed + 0.05f;
    tcfg.gradient       = &s_elecGrad;
    tcfg.widthEnvelope  = TRAIL_WIDTH_ENVELOPE_TAPER_TAIL;
    tcfg.smoothSpline   = true;
    tcfg.priority       = VFX_PRIORITY_LOW;

    e->trailIds[a] = SpawnTrailEntity(tcfg);

    // Push tip ngay lập tức để trail không bắt đầu từ (0,0,0)
    if (e->trailIds[a] >= 0)
        UpdateFollowerPosition(e->trailIds[a], headWorld);
}

// ── Fallback mesh ─────────────────────────────────────────────────────────────

static MeshAdjacency s_fallbackAdjacency;
static bool          s_fallbackBuilt = false;

// ── Public API ────────────────────────────────────────────────────────────────

int VFX_SpawnMeshElectricity(const struct MeshAdjacency *adj, Color color,
                              float duration, const struct ForceField *forceField)
{
    if (adj == NULL) {
        if (!s_fallbackBuilt) {
            Mesh torusMesh = GenMeshTorus(0.25f, 3.2f, 16, 48);
            MeshAdjacency_Build(&s_fallbackAdjacency, torusMesh);
            UnloadMesh(torusMesh);
            s_fallbackBuilt = true;
        }
        adj = &s_fallbackAdjacency;
    }

    for (int i = 0; i < ARCH_MAX_MESH_ELECTRICS; i++) {
        if (!s_archElectrics[i].active) {
            Arch_MeshElectricity *e = &s_archElectrics[i];
            e->active        = true;
            e->trailsSpawned = false;   // lazy — trails sẽ spawn ở Update frame đầu
            e->adj           = adj;
            e->transform     = MatrixIdentity();
            e->duration      = duration;
            e->elapsed       = 0.0f;
            e->color         = color;
            e->moveTimer     = 0.0f;
            e->forceField    = (const ForceField *)forceField;

            // Chỉ init path data — chưa spawn trail
            for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
                e->trailIds[a] = -1;
                VC_MeshElectricity_InitPath(e, a);
            }
            return i;
        }
    }
    return -1;
}

void VFX_UpdateMeshElectricity(int handle, Matrix transform)
{
    if (handle < 0 || handle >= ARCH_MAX_MESH_ELECTRICS || !s_archElectrics[handle].active) return;
    s_archElectrics[handle].transform = transform;
}

void VFX_KillMeshElectricity(int handle)
{
    if (handle < 0 || handle >= ARCH_MAX_MESH_ELECTRICS) return;
    Arch_MeshElectricity *e = &s_archElectrics[handle];
    for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
        if (e->trailIds[a] >= 0) { KillTrail(e->trailIds[a]); e->trailIds[a] = -1; }
    }
    e->active = false;
}

// ── Internal update ───────────────────────────────────────────────────────────

static void VC_MeshElectricity_Update(float dt)
{
    for (int i = 0; i < ARCH_MAX_MESH_ELECTRICS; i++) {
        Arch_MeshElectricity *e = &s_archElectrics[i];
        if (!e->active) continue;

        e->elapsed += dt;
        if (e->elapsed >= e->duration) {
            VFX_KillMeshElectricity(i);
            continue;
        }

        // Lazy spawn: frame đầu tiên, transform đã được set đúng bởi caller
        if (!e->trailsSpawned) {
            for (int a = 0; a < MAX_ELECTRIC_ARCS; a++)
                VC_MeshElectricity_SpawnTrail(e, a);
            e->trailsSpawned = true;
        }

        // Crawl tick
        e->moveTimer += dt;
        if (e->moveTimer >= CRAWL_INTERVAL) {
            e->moveTimer = 0.0f;

            for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
                e->stepsRemaining[a]--;

                if (e->stepsRemaining[a] <= 0) {
                    // Re-spawn trail tại vị trí ngẫu nhiên mới
                    VC_MeshElectricity_SpawnTrail(e, a);
                } else {
                    int head = e->paths[a][0];
                    int prev = (e->pathLengths[a] > 1) ? e->paths[a][1] : -1;
                    int neighborCount = e->adj->neighborCount[head];
                    int next = -1;

                    if (neighborCount > 0) {
                        if (neighborCount > 1 && prev != -1) {
                            int eligible[MAX_VERTEX_NEIGHBORS], cnt = 0;
                            for (int n = 0; n < neighborCount; n++) {
                                int nb = e->adj->neighbors[head][n];
                                if (nb != prev) eligible[cnt++] = nb;
                            }
                            if (cnt > 0) next = eligible[GetRandomValue(0, cnt-1)];
                        }
                        if (next == -1)
                            next = e->adj->neighbors[head][GetRandomValue(0, neighborCount-1)];
                    }

                    if (next != -1) {
                        for (int p = ELECTRIC_PATH_LEN-1; p > 0; p--)
                            e->paths[a][p] = e->paths[a][p-1];
                        e->paths[a][0] = (unsigned short)next;
                        if (e->pathLengths[a] < ELECTRIC_PATH_LEN) e->pathLengths[a]++;
                    }
                }
            }
        }

        // Mỗi frame: push tip trail đến head vertex hiện tại (world space)
        for (int a = 0; a < MAX_ELECTRIC_ARCS; a++) {
            if (e->trailIds[a] < 0) continue;
            Vector3 headLocal = e->adj->vertices[e->paths[a][0]];
            Vector3 headWorld = Vector3Transform(headLocal, e->transform);
            UpdateFollowerPosition(e->trailIds[a], headWorld);
        }

        // VFXLight crackle
        if (GetRandomValue(0, 100) < 8) {
            int a = GetRandomValue(0, MAX_ELECTRIC_ARCS - 1);
            if (e->trailIds[a] >= 0) {
                Vector3 headLocal = e->adj->vertices[e->paths[a][0]];
                Vector3 headWorld = Vector3Transform(headLocal, e->transform);
                VFXLight_Spawn(headWorld, e->color,
                               0.4f * (0.6f + 0.4f * Random01()), 0.07f, VFX_PRIORITY_LOW);
            }
        }
    }
}

static void VC_MeshElectricity_Draw3D(Camera3D cam) { (void)cam; }

// ── Convenience wrappers ──────────────────────────────────────────────────────

void VFX_ComposeMeshElectricity(Vector3 position, Color color, float duration)
{
    int handle = VFX_SpawnMeshElectricity(NULL, color, duration, NULL);
    if (handle != -1)
        VFX_UpdateMeshElectricity(handle, MatrixTranslate(position.x, position.y, position.z));
}

void ComposeMeshElectricityEx(Vector3 position, Color color, float duration,
                               const struct ForceField *forceField)
{
    int handle = VFX_SpawnMeshElectricity(NULL, color, duration, forceField);
    if (handle != -1)
        VFX_UpdateMeshElectricity(handle, MatrixTranslate(position.x, position.y, position.z));
}
