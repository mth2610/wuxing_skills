#include "core/liquid/liquid_pbd.h"
#include "core/gfx_quality.h"
#include "core/map_manager.h"
#include "raymath.h"
#include <math.h>
#include <string.h>

#define PBD_SOLVER_ITERATIONS 5
#define PBD_GRAVITY 9.81f
#define PBD_CELL_SIZE 0.095f
#define PBD_GRID_X 64
#define PBD_GRID_Y 32
#define PBD_GRID_Z 64
#define PBD_GRID_CELLS (PBD_GRID_X*PBD_GRID_Y*PBD_GRID_Z)

typedef struct LiquidPBDParticle {
    Vector3 position, previous, velocity;
    float radius, life, maxLife;
    float groundHeight;
    float flattenBlend;
    bool grounded, active;
} LiquidPBDParticle;

static LiquidPBDParticle s_particles[LIQUID_PBD_MAX_PARTICLES];
static int s_nextParticle;
static bool s_initialized;
static int s_hashHead[PBD_GRID_CELLS];
static int s_hashNext[LIQUID_PBD_MAX_PARTICLES];
static int s_particleCell[LIQUID_PBD_MAX_PARTICLES];
static Vector3 s_gridOrigin = {0.0f, 0.0f, 0.0f};

static int PBD_CellIndex(Vector3 p, int *x, int *y, int *z)
{
    float rx = p.x - s_gridOrigin.x;
    float ry = p.y - s_gridOrigin.y;
    float rz = p.z - s_gridOrigin.z;
    *x = (int)Clamp(floorf(rx / PBD_CELL_SIZE) + (float)(PBD_GRID_X / 2), 0.0f, (float)(PBD_GRID_X - 1));
    *y = (int)Clamp(floorf(ry / PBD_CELL_SIZE) + 8.0f, 0.0f, (float)(PBD_GRID_Y - 1));
    *z = (int)Clamp(floorf(rz / PBD_CELL_SIZE) + (float)(PBD_GRID_Z / 2), 0.0f, (float)(PBD_GRID_Z - 1));
    return (*z * PBD_GRID_Y + *y) * PBD_GRID_X + *x;
}

static Vector3 PBD_SafeNormal(Vector3 v, Vector3 fallback)
{
    return Vector3LengthSqr(v) > 0.000001f ? Vector3Normalize(v) : fallback;
}

static void PBD_Basis(Vector3 normal, Vector3 *tangent, Vector3 *bitangent)
{
    Vector3 reference = fabsf(normal.y) < 0.95f ? (Vector3){0.0f, 1.0f, 0.0f} : (Vector3){1.0f, 0.0f, 0.0f};
    *tangent = Vector3Normalize(Vector3CrossProduct(reference, normal));
    *bitangent = Vector3Normalize(Vector3CrossProduct(normal, *tangent));
}

void LiquidPBD_Init(void)
{
    if (s_initialized) return;
    memset(s_particles, 0, sizeof(s_particles));
    for (int i = 0; i < PBD_GRID_CELLS; ++i) s_hashHead[i] = -1;
    s_nextParticle = 0;
    s_initialized = true;
}

void LiquidPBD_SpawnImpact(Vector3 point, Vector3 normal, Vector3 impulse, float force01, float scale)
{
    LiquidPBD_Init();
    s_gridOrigin = point;
    normal = PBD_SafeNormal(normal, (Vector3){0.0f, 1.0f, 0.0f});
    impulse = PBD_SafeNormal(impulse, normal);
    float f = Clamp(force01, 0.0f, 1.0f);
    int count = GfxQuality_Get() <= GFX_LOW ? 48 : (GfxQuality_Get() >= GFX_HIGH ? 160 : 96);
    count = (int)((float)count * (0.45f + 0.55f * f));
    float radius = scale * 0.042f;
    Vector3 tangent, bitangent;
    PBD_Basis(normal, &tangent, &bitangent);
    for (int i = 0; i < count; ++i) {
        float a = ((float)GetRandomValue(0, 359)) * DEG2RAD;
        float ring = sqrtf((float)GetRandomValue(0, 1000) / 1000.0f);
        float lift = (float)GetRandomValue(0, 1000) / 1000.0f;
        Vector3 radial = Vector3Add(Vector3Scale(tangent, cosf(a) * ring),
                                    Vector3Scale(bitangent, sinf(a) * ring));
        LiquidPBDParticle *p = &s_particles[s_nextParticle++ % LIQUID_PBD_MAX_PARTICLES];
        Vector3 start = Vector3Add(point,
            Vector3Add(Vector3Scale(radial, radius * 3.5f),
                       Vector3Scale(normal, radius * (0.8f + lift * 2.0f))));
        float outward = scale * (0.8f + 1.8f * f);
        Vector3 velocity = Vector3Add(Vector3Scale(impulse, scale * (1.5f + 3.0f * f)),
                                      Vector3Add(Vector3Scale(radial, outward),
                                                 Vector3Scale(normal, scale * (0.6f + lift))));
        float gnd = MapManager_GetGroundHeightAt(start.x, start.z);
        *p = (LiquidPBDParticle){
            .position = start,
            .previous = start,
            .velocity = velocity,
            .radius = radius,
            .life = 4.0f,
            .maxLife = 4.0f,
            .groundHeight = gnd,
            .flattenBlend = 0.0f,
            .grounded = false,
            .active = true
        };
    }
}

void LiquidPBD_Update(float dt)
{
    if (!s_initialized || dt <= 0.0f) return;
    dt = fminf(dt, 1.0f / 30.0f);

    int activeIndices[LIQUID_PBD_MAX_PARTICLES];
    int activeCount = 0;
    Vector3 centroid = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < LIQUID_PBD_MAX_PARTICLES; i++) {
        LiquidPBDParticle *p = &s_particles[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0.0f) {
            p->active = false;
            continue;
        }
        p->previous = p->position;
        p->velocity.y -= PBD_GRAVITY * dt;
        p->velocity = Vector3Scale(p->velocity, p->grounded ? 0.985f : 0.997f);
        p->position = Vector3Add(p->position, Vector3Scale(p->velocity, dt));
        p->groundHeight = MapManager_GetGroundHeightAt(p->position.x, p->position.z);
        p->grounded = false;
        centroid = Vector3Add(centroid, p->position);
        activeIndices[activeCount++] = i;
    }

    /* Fast O(1) early-out when no CPU PBD particles are alive */
    if (activeCount == 0) return;

    s_gridOrigin = Vector3Scale(centroid, 1.0f / (float)activeCount);

    for (int iteration = 0; iteration < PBD_SOLVER_ITERATIONS; iteration++) {
        /* Build spatial grid and only touch cells occupied by active particles */
        for (int k = 0; k < activeCount; k++) {
            int i = activeIndices[k];
            int x, y, z;
            int bucket = PBD_CellIndex(s_particles[i].position, &x, &y, &z);
            s_particleCell[i] = bucket;
            s_hashNext[i] = s_hashHead[bucket];
            s_hashHead[bucket] = i;
        }

        for (int k = 0; k < activeCount; k++) {
            int i = activeIndices[k];
            LiquidPBDParticle *a = &s_particles[i];
            int cx, cy, cz;
            PBD_CellIndex(a->position, &cx, &cy, &cz);
            for (int oz = -1; oz <= 1; oz++) {
                int nz = cz + oz;
                if (nz < 0 || nz >= PBD_GRID_Z) continue;
                for (int oy = -1; oy <= 1; oy++) {
                    int ny = cy + oy;
                    if (ny < 0 || ny >= PBD_GRID_Y) continue;
                    for (int ox = -1; ox <= 1; ox++) {
                        int nx = cx + ox;
                        if (nx < 0 || nx >= PBD_GRID_X) continue;
                        for (int j = s_hashHead[(nz * PBD_GRID_Y + ny) * PBD_GRID_X + nx]; j >= 0; j = s_hashNext[j]) {
                            if (j <= i) continue;
                            LiquidPBDParticle *b = &s_particles[j];
                            Vector3 delta = Vector3Subtract(b->position, a->position);
                            float d2 = Vector3LengthSqr(delta);
                            float rest = (a->radius + b->radius) * 0.88f;
                            if (d2 >= rest * rest) continue;
                            float d = sqrtf(fmaxf(d2, 0.0000001f));
                            Vector3 n = Vector3Scale(delta, 1.0f / d);
                            float correction = (rest - d) * 0.5f;
                            a->position = Vector3Subtract(a->position, Vector3Scale(n, correction));
                            b->position = Vector3Add(b->position, Vector3Scale(n, correction));
                        }
                    }
                }
            }
        }

        for (int k = 0; k < activeCount; k++) {
            int i = activeIndices[k];
            LiquidPBDParticle *p = &s_particles[i];
            if (iteration == PBD_SOLVER_ITERATIONS - 1)
                p->groundHeight = MapManager_GetGroundHeightAt(p->position.x, p->position.z);
            if (p->position.y - p->radius < p->groundHeight) {
                p->position.y = p->groundHeight + p->radius;
                p->grounded = true;
            }
        }

        /* Keep final iteration's grid alive for XSPH velocity smoothing below */
        if (iteration < PBD_SOLVER_ITERATIONS - 1) {
            for (int k = 0; k < activeCount; k++) {
                s_hashHead[s_particleCell[activeIndices[k]]] = -1;
            }
        }
    }

    float invDt = 1.0f / dt;
    for (int k = 0; k < activeCount; k++) {
        int i = activeIndices[k];
        LiquidPBDParticle *a = &s_particles[i];
        a->velocity = Vector3Scale(Vector3Subtract(a->position, a->previous), invDt);
    }

    for (int k = 0; k < activeCount; k++) {
        int i = activeIndices[k];
        LiquidPBDParticle *a = &s_particles[i];
        int cx, cy, cz;
        PBD_CellIndex(a->position, &cx, &cy, &cz);
        for (int oz = -1; oz <= 1; oz++) {
            int nz = cz + oz;
            if (nz < 0 || nz >= PBD_GRID_Z) continue;
            for (int oy = -1; oy <= 1; oy++) {
                int ny = cy + oy;
                if (ny < 0 || ny >= PBD_GRID_Y) continue;
                for (int ox = -1; ox <= 1; ox++) {
                    int nx = cx + ox;
                    if (nx < 0 || nx >= PBD_GRID_X) continue;
                    for (int j = s_hashHead[(nz * PBD_GRID_Y + ny) * PBD_GRID_X + nx]; j >= 0; j = s_hashNext[j]) {
                        if (j <= i) continue;
                        LiquidPBDParticle *b = &s_particles[j];
                        Vector3 delta = Vector3Subtract(b->position, a->position);
                        float h = (a->radius + b->radius) * 2.4f;
                        float d2 = Vector3LengthSqr(delta);
                        if (d2 >= h * h) continue;
                        float d = sqrtf(d2);
                        float w = (1.0f - d / h) * 0.07f;
                        Vector3 avg = Vector3Scale(Vector3Add(a->velocity, b->velocity), 0.5f);
                        a->velocity = Vector3Lerp(a->velocity, avg, w);
                        b->velocity = Vector3Lerp(b->velocity, avg, w);
                    }
                }
            }
        }
        if (a->grounded) {
            a->velocity.y = 0.0f;
            a->velocity.x *= 0.82f;
            a->velocity.z *= 0.82f;
        }
        float targetFlatten = a->grounded ? 1.0f : 0.0f;
        a->flattenBlend += (targetFlatten - a->flattenBlend) * fminf(dt * 14.0f, 1.0f);
    }

    /* Clean up the final iteration's occupied grid buckets in O(activeCount) */
    for (int k = 0; k < activeCount; k++) {
        s_hashHead[s_particleCell[activeIndices[k]]] = -1;
    }
}

int LiquidPBD_GetRenderParticles(LiquidPBDRenderParticle *outParticles, int maxParticles)
{
    int count = 0;
    if (!outParticles || maxParticles <= 0) return 0;
    for (int i = 0; i < LIQUID_PBD_MAX_PARTICLES && count < maxParticles; i++) {
        LiquidPBDParticle *p = &s_particles[i];
        if (!p->active) continue;
        float lifeScale = p->maxLife > 0.0f ? Clamp(p->life / (p->maxLife * 0.15f), 0.0f, 1.0f) : 1.0f;
        float r = p->radius * lifeScale;
        if (r <= 0.001f) continue;
        float t = p->flattenBlend;
        float sx = 1.0f + 1.1f * t;
        float sy = 1.0f - 0.60f * t;
        Vector3 radii = {r * sx, r * sy, r * sx};
        outParticles[count++] = (LiquidPBDRenderParticle){p->position, radii};
    }
    return count;
}
