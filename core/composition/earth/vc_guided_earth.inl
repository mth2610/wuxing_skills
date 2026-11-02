// ── GENERALIZED GUIDED EARTH (HỆ THỔ) ─────────────────────────────────────────
//
// Generalized Physical Earth VFX System:
// Phase 1 (Emergence): Ground eruption with fissures and radial vegetation blast,
//                      orbital converge, or levitation into hovering tactical formation.
// Phase 2 (Flight):    Sequential or simultaneous ballistic parabolic arc (with authored
//                      apex, homing, or spiral swirl), high-fidelity procedural rock
//                      geometry for 6 distinct styles (Boulder, Jagged Slate, Magma Rock,
//                      Mud Chunk, Jade Crystal, Gravel Swarm) with authored CPU lighting,
//                      and aerodynamic wind wake / motion field parting the grass.
// Phase 3 (Impact):    Rhythmic sequential physical shatter into bouncing debris chips
//                      (vc_debris_shards.inl), particulate dust puffs (vc_impact_dust.inl),
//                      radial fissure streaks, conformal ground wave (vc_ground_wave.inl),
//                      and powerful radial wind shockwaves flattening surrounding vegetation.
//
#ifndef VC_GUIDED_EARTH_INL
#define VC_GUIDED_EARTH_INL

#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/geometry/procedural_mesh_utils.h"
#include "core/color_gradient.h"
#include "core/vfx_render.h"
#include "core/map_manager.h"
#include "core/wind/wind_system.h"
#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define VC_GUIDED_EARTH_MAX_INSTANCES 16
#define VC_GUIDED_EARTH_MAX_ROCKS     24

static const Vector3 k_earthKeyLight = {-0.38f, 0.85f, 0.36f};

typedef enum {
    ROCK_STATE_EMERGING = 0,
    ROCK_STATE_HOVERING,
    ROCK_STATE_FLYING,
    ROCK_STATE_IMPACTED
} VC_RockState;

typedef struct {
    Vector3 pos;
    Vector3 prevPos;
    Vector3 launchPos;
    Vector3 targetPos;
    Vector3 localOffset;
    Vector3 spinAxis;
    float spinAngle;
    float spinSpeed;
    float radius;
    float massKg;
    float wakeTimer;
    float crumbTimer;
    float arcHeight;
    unsigned int seed;

    // Per-rock staggered launch & flight state
    VC_RockState state;
    float launchTime;
    float flightProgress;
    bool hasImpacted;
    bool launchWindFired;
    bool emergeFired;

    RockMeshData mesh;
    bool meshBuilt;
} VC_GuidedRock;

typedef struct {
    bool active;
    int id;
    VFX_GuidedEarthConfig config;
    float timer;
    float flightDuration;
    int rockCount;
    VC_GuidedRock rocks[VC_GUIDED_EARTH_MAX_ROCKS];
    Vector3 curTarget;
    bool emergeWindTriggered;
    bool craterSpawned;
} VC_GuidedEarthInstance;

static VC_GuidedEarthInstance s_guidedEarthInstances[VC_GUIDED_EARTH_MAX_INSTANCES];
static VFX_GuidedEarthConfig s_liveGuidedEarthConfig;
static bool s_guidedEarthInit = false;
static int s_guidedEarthNextId = 1;

// ── Deterministic RNG Helpers ────────────────────────────────────────────────

static inline unsigned int GuidedEarth_Hash(unsigned int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = (x >> 16) ^ x;
    return x;
}

static inline float GuidedEarth_RandomFloat(unsigned int *seed)
{
    *seed = GuidedEarth_Hash(*seed + 19937u);
    return (float)(*seed % 10000u) / 10000.0f;
}

// ── High-Fidelity Geological Rock Mesh Building ──────────────────────────────

static void GuidedEarth_RecalcNormals(RockMeshData *mesh)
{
    if (!mesh) return;
    for (int f = 0; f < mesh->faceCount; f++) {
        Vector3 a = mesh->verts[mesh->faceVertIdx[f][0]];
        Vector3 b = mesh->verts[mesh->faceVertIdx[f][1]];
        Vector3 c = mesh->verts[mesh->faceVertIdx[f][2]];
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        if (Vector3DotProduct(n, a) < 0.0f) n = Vector3Negate(n);
        mesh->faceNormals[f] = n;
    }
}

static void GuidedEarth_BuildRockMeshForStyle(RockMeshData *out, VFX_EarthRockStyle style, float radius, float jaggedness, int seed)
{
    if (!out) return;
    memset(out, 0, sizeof(RockMeshData));
    unsigned int rng = (unsigned int)seed;

    switch (style)
    {
    case VFX_EARTH_STYLE_BOULDER:
    {
        // 1. Solid monolithic boulder: Subdivided icosphere + chiseled cleavage cut planes
        ProceduralMesh_BuildRock(out, Vector3Zero(), radius, jaggedness * 0.18f, seed, 1);
        if (out->vertCount <= 0) return;

        // Apply 5 cleavage clipping planes to produce flat chiseled stonecut facets
        int numCuts = 5 + (seed % 2);
        for (int c = 0; c < numCuts; c++) {
            float theta = GuidedEarth_RandomFloat(&rng) * 2.0f * PI;
            float phi = (GuidedEarth_RandomFloat(&rng) - 0.5f) * PI;
            Vector3 cutNormal = {cosf(theta) * cosf(phi), sinf(phi), sinf(theta) * cosf(phi)};
            float cutDist = radius * (0.78f + 0.18f * GuidedEarth_RandomFloat(&rng));

            for (int v = 0; v < out->vertCount; v++) {
                float d = Vector3DotProduct(out->verts[v], cutNormal);
                if (d > cutDist) {
                    out->verts[v] = Vector3Subtract(out->verts[v], Vector3Scale(cutNormal, d - cutDist));
                }
            }
        }
        GuidedEarth_RecalcNormals(out);
        break;
    }

    case VFX_EARTH_STYLE_JAGGED_SLATE:
    {
        // 2. Metamorphic shale/slate throwing disc: Razor beveled perimeter blade
        int rimSides = 10;
        float thickness = radius * 0.22f;
        float hHalf = thickness * 0.5f;

        // Vertices:
        // V0: Top center (0, +thickness, 0)
        // V1: Bottom center (0, -thickness, 0)
        // V[2..11]: Top beveled rim
        // V[12..21]: Knife-edge perimeter rim (Y = 0)
        // V[22..31]: Bottom beveled rim
        out->verts[0] = (Vector3){0.0f, thickness, 0.0f};
        out->verts[1] = (Vector3){0.0f, -thickness, 0.0f};
        out->vertCount = 2 + rimSides * 3;

        for (int k = 0; k < rimSides; k++) {
            float angle = ((float)k / (float)rimSides) * 2.0f * PI;
            float rVar = radius * (0.80f + 0.35f * GuidedEarth_RandomFloat(&rng));
            float cosA = cosf(angle);
            float sinA = sinf(angle);

            // Top rim
            out->verts[2 + k] = (Vector3){cosA * rVar * 0.72f, hHalf, sinA * rVar * 0.72f};
            // Knife-edge rim
            out->verts[2 + rimSides + k] = (Vector3){cosA * rVar, 0.0f, sinA * rVar};
            // Bottom rim
            out->verts[2 + rimSides * 2 + k] = (Vector3){cosA * rVar * 0.72f, -hHalf, sinA * rVar * 0.72f};
        }

        int fCount = 0;
        for (int k = 0; k < rimSides; k++) {
            int next = (k + 1) % rimSides;
            int topK = 2 + k;
            int topNext = 2 + next;
            int edgeK = 2 + rimSides + k;
            int edgeNext = 2 + rimSides + next;
            int botK = 2 + rimSides * 2 + k;
            int botNext = 2 + rimSides * 2 + next;

            // Top cap triangle
            out->faceVertIdx[fCount][0] = 0;
            out->faceVertIdx[fCount][1] = topK;
            out->faceVertIdx[fCount][2] = topNext;
            fCount++;

            // Top bevel quad -> 2 triangles
            out->faceVertIdx[fCount][0] = topK;
            out->faceVertIdx[fCount][1] = edgeK;
            out->faceVertIdx[fCount][2] = topNext;
            fCount++;

            out->faceVertIdx[fCount][0] = topNext;
            out->faceVertIdx[fCount][1] = edgeK;
            out->faceVertIdx[fCount][2] = edgeNext;
            fCount++;

            // Bottom bevel quad -> 2 triangles
            out->faceVertIdx[fCount][0] = edgeK;
            out->faceVertIdx[fCount][1] = botK;
            out->faceVertIdx[fCount][2] = edgeNext;
            fCount++;

            out->faceVertIdx[fCount][0] = edgeNext;
            out->faceVertIdx[fCount][1] = botK;
            out->faceVertIdx[fCount][2] = botNext;
            fCount++;

            // Bottom cap triangle
            out->faceVertIdx[fCount][0] = 1;
            out->faceVertIdx[fCount][1] = botNext;
            out->faceVertIdx[fCount][2] = botK;
            fCount++;
        }
        out->faceCount = fCount;
        GuidedEarth_RecalcNormals(out);
        break;
    }

    case VFX_EARTH_STYLE_MAGMA_ROCK:
    {
        // 3. Volcanic scoria: Porous craggy crust with deep volcanic vesicles/cavities
        ProceduralMesh_BuildRock(out, Vector3Zero(), radius, jaggedness * 0.65f, seed, 1);
        if (out->vertCount <= 0) return;

        // 4 Deep volcanic vesicle cavities
        Vector3 cavities[4];
        for (int c = 0; c < 4; c++) {
            float theta = GuidedEarth_RandomFloat(&rng) * 2.0f * PI;
            float phi = (GuidedEarth_RandomFloat(&rng) - 0.5f) * PI;
            cavities[c] = (Vector3){cosf(theta) * cosf(phi), sinf(phi), sinf(theta) * cosf(phi)};
        }

        for (int v = 0; v < out->vertCount; v++) {
            Vector3 normV = Vector3Normalize(out->verts[v]);
            float maxDent = 0.0f;
            for (int c = 0; c < 4; c++) {
                float dist = Vector3Distance(normV, cavities[c]);
                if (dist < 0.70f) {
                    float f = 1.0f - dist / 0.70f;
                    if (f > maxDent) maxDent = f;
                }
            }
            if (maxDent > 0.0f) {
                out->verts[v] = Vector3Scale(out->verts[v], 1.0f - maxDent * 0.48f);
            }
        }
        GuidedEarth_RecalcNormals(out);
        break;
    }

    case VFX_EARTH_STYLE_MUD_CHUNK:
    {
        // 4. Cohesive clay/mud clump: Smooth undulating 3-harmonic organic lobes
        ProceduralMesh_BuildRock(out, Vector3Zero(), radius, 0.05f, seed, 1);
        if (out->vertCount <= 0) return;

        float p1 = GuidedEarth_RandomFloat(&rng) * 3.14f;
        float p2 = GuidedEarth_RandomFloat(&rng) * 3.14f;

        for (int v = 0; v < out->vertCount; v++) {
            Vector3 dir = Vector3Normalize(out->verts[v]);
            float theta = atan2f(dir.z, dir.x);
            float phi = acosf(Clamp(dir.y, -1.0f, 1.0f));

            float harmonic = 0.22f * sinf(2.0f * theta + p1) * cosf(2.0f * phi) +
                             0.14f * cosf(3.0f * theta + p2);
            float r = radius * (1.0f + harmonic);
            out->verts[v] = Vector3Scale(dir, r);
        }
        GuidedEarth_RecalcNormals(out);
        break;
    }

    case VFX_EARTH_STYLE_JADE_CRYSTAL:
    {
        // 5. Hexagonal crystal prism: 6 vertical facets with sharp pyramidal apex caps
        int sides = 6;
        float length = radius * 1.8f;
        float halfL = length * 0.5f;
        float capL = halfL * 0.55f;

        // V0: Top apex (0, +halfL, 0)
        // V1: Bottom apex (0, -halfL, 0)
        // V[2..7]: Top prism ring
        // V[8..13]: Bottom prism ring
        out->verts[0] = (Vector3){0.0f, halfL, 0.0f};
        out->verts[1] = (Vector3){0.0f, -halfL, 0.0f};
        out->vertCount = 2 + sides * 2;

        for (int k = 0; k < sides; k++) {
            float angle = ((float)k / (float)sides) * 2.0f * PI + 0.25f;
            float cosA = cosf(angle);
            float sinA = sinf(angle);
            out->verts[2 + k] = (Vector3){cosA * radius, capL, sinA * radius};
            out->verts[2 + sides + k] = (Vector3){cosA * radius, -capL, sinA * radius};
        }

        int fCount = 0;
        for (int k = 0; k < sides; k++) {
            int next = (k + 1) % sides;
            int topK = 2 + k;
            int topNext = 2 + next;
            int botK = 2 + sides + k;
            int botNext = 2 + sides + next;

            // Top cap triangle
            out->faceVertIdx[fCount][0] = 0;
            out->faceVertIdx[fCount][1] = topK;
            out->faceVertIdx[fCount][2] = topNext;
            fCount++;

            // Prism side quad -> 2 triangles
            out->faceVertIdx[fCount][0] = topK;
            out->faceVertIdx[fCount][1] = botK;
            out->faceVertIdx[fCount][2] = topNext;
            fCount++;

            out->faceVertIdx[fCount][0] = topNext;
            out->faceVertIdx[fCount][1] = botK;
            out->faceVertIdx[fCount][2] = botNext;
            fCount++;

            // Bottom cap triangle
            out->faceVertIdx[fCount][0] = 1;
            out->faceVertIdx[fCount][1] = botNext;
            out->faceVertIdx[fCount][2] = botK;
            fCount++;
        }
        out->faceCount = fCount;
        GuidedEarth_RecalcNormals(out);
        break;
    }

    case VFX_EARTH_STYLE_GRAVEL_SWARM:
    default:
    {
        // 6. Angular river scree pebbles: Asymmetrical tumbled tri-axial pebble
        ProceduralMesh_BuildRock(out, Vector3Zero(), radius, jaggedness * 0.50f, seed, 1);
        if (out->vertCount <= 0) return;

        float sX = 0.70f + 0.55f * GuidedEarth_RandomFloat(&rng);
        float sY = 0.55f + 0.45f * GuidedEarth_RandomFloat(&rng);
        float sZ = 0.75f + 0.50f * GuidedEarth_RandomFloat(&rng);

        for (int v = 0; v < out->vertCount; v++) {
            out->verts[v].x *= sX;
            out->verts[v].y *= sY;
            out->verts[v].z *= sZ;
        }
        GuidedEarth_RecalcNormals(out);
        break;
    }
    }
}

// ── Default Configurations & Presets ─────────────────────────────────────────

VFX_GuidedEarthConfig VFX_GuidedEarth_DefaultConfig(void)
{
    VFX_GuidedEarthConfig cfg = {0};
    cfg.preset = VFX_EARTH_PRESET_BOULDER_LOB;
    cfg.style = VFX_EARTH_STYLE_BOULDER;
    cfg.spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
    cfg.trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
    cfg.impactMode = VFX_EARTH_IMPACT_SHATTER;

    cfg.source = (Vector3){0.0f, 0.0f, 0.0f};
    cfg.target = (Vector3){6.0f, 0.0f, 4.0f};

    // Geometry & Counts
    cfg.rockCount = 1;
    cfg.rockRadius = 0.95f;
    cfg.rockSquashY = 0.82f;
    cfg.rockSquashZ = 0.88f;
    cfg.jaggedness = 0.35f;
    cfg.clusterSpread = 0.0f;
    cfg.sizeVariation = 0.0f;

    // Emergence & Levitation Formation
    cfg.emergeDuration = 0.65f;
    cfg.hoverDuration = 0.20f;
    cfg.emergeHeight = 1.2f;
    cfg.emergeGroundDust = false;
    cfg.emergeCracks = true;

    // Flight & Parabolic Motion
    cfg.launchStaggerDelay = 0.0f;
    cfg.travelSpeed = 16.0f;
    cfg.arcHeight = 4.2f;
    cfg.arcVariation = 0.25f;
    cfg.trajectorySpread = 0.0f;
    cfg.targetSpread = 0.0f;
    cfg.gravityScale = 1.0f;
    cfg.swirlSpeed = 0.0f;
    cfg.tumbleSpeed = 120.0f;
    cfg.emitDustTrail = false;
    cfg.dustRate = 0.0f;
    cfg.emitTrailRibbon = false;
    cfg.ribbonWidth = 0.35f;

    // Impact
    cfg.shatterChips = 28;
    cfg.shatterVelocity = 6.5f;
    cfg.impactDustRadius = 2.4f;
    cfg.impactShake = 0.0f;
    cfg.impactCraterCrack = true;
    cfg.impactShockRing = true;

    // Material Optics
    cfg.matId = VC_MAT_EARTH;
    cfg.rockBaseColor = (Color){145, 118, 90, 255};
    cfg.veinColor = (Color){255, 140, 40, 255};
    cfg.veinIntensity = 0.0f;
    cfg.ambientGain = 0.28f;
    cfg.specularGain = 0.85f;

    return cfg;
}

VFX_GuidedEarthConfig *VFX_GuidedEarth_GetLiveConfig(void)
{
    if (!s_guidedEarthInit) {
        s_liveGuidedEarthConfig = VFX_GuidedEarth_DefaultConfig();
        s_guidedEarthInit = true;
    }
    return &s_liveGuidedEarthConfig;
}

void VFX_GuidedEarth_ApplyPreset(VFX_GuidedEarthConfig *cfg, VFX_GuidedEarthPreset preset)
{
    if (!cfg) return;
    cfg->preset = preset;

    switch (preset)
    {
    case VFX_EARTH_PRESET_BOULDER_LOB:
        cfg->style = VFX_EARTH_STYLE_BOULDER;
        cfg->spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_SHATTER;
        cfg->rockCount = 1;
        cfg->rockRadius = 1.05f;
        cfg->rockSquashY = 0.90f;
        cfg->rockSquashZ = 0.95f;
        cfg->jaggedness = 0.25f;
        cfg->clusterSpread = 0.0f;
        cfg->sizeVariation = 0.0f;
        cfg->emergeDuration = 0.65f;
        cfg->hoverDuration = 0.20f;
        cfg->emergeHeight = 1.2f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = true;
        cfg->launchStaggerDelay = 0.0f;
        cfg->travelSpeed = 16.0f;
        cfg->arcHeight = 4.2f;
        cfg->trajectorySpread = 0.0f;
        cfg->swirlSpeed = 0.0f;
        cfg->tumbleSpeed = 120.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 30;
        cfg->shatterVelocity = 6.5f;
        cfg->impactDustRadius = 2.4f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = true;
        cfg->rockBaseColor = (Color){145, 118, 90, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_ROCK_BARRAGE:
        // Signature rapid sequential barrage of 8 sharp razor slates
        cfg->style = VFX_EARTH_STYLE_JAGGED_SLATE;
        cfg->spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_SHATTER;
        cfg->rockCount = 8;
        cfg->rockRadius = 0.40f;
        cfg->rockSquashY = 0.30f;
        cfg->rockSquashZ = 0.80f;
        cfg->jaggedness = 0.45f;
        cfg->clusterSpread = 1.20f;
        cfg->sizeVariation = 0.35f;
        cfg->emergeDuration = 0.45f;
        cfg->hoverDuration = 0.30f;
        cfg->emergeHeight = 1.1f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = true;
        cfg->launchStaggerDelay = 0.12f; // Rapid sequential cadence!
        cfg->travelSpeed = 26.0f;
        cfg->arcHeight = 2.4f;
        cfg->trajectorySpread = 1.6f;
        cfg->swirlSpeed = 0.0f;
        cfg->tumbleSpeed = 320.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 14;
        cfg->shatterVelocity = 4.5f;
        cfg->impactDustRadius = 1.4f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = true;
        cfg->rockBaseColor = (Color){115, 110, 105, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_METEOR_CRUSH:
        // Heavy volcanic scoria meteors catapulting sequentially with high arc
        cfg->style = VFX_EARTH_STYLE_MAGMA_ROCK;
        cfg->spawnMode = VFX_EARTH_SPAWN_LEVITATE_RISE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_HIGH_CATAPULT;
        cfg->impactMode = VFX_EARTH_IMPACT_CRATER_CRACK;
        cfg->rockCount = 5;
        cfg->rockRadius = 0.80f;
        cfg->rockSquashY = 0.90f;
        cfg->rockSquashZ = 0.95f;
        cfg->jaggedness = 0.50f;
        cfg->clusterSpread = 1.10f;
        cfg->sizeVariation = 0.40f;
        cfg->emergeDuration = 0.50f;
        cfg->hoverDuration = 0.35f;
        cfg->emergeHeight = 1.4f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->launchStaggerDelay = 0.16f; // Staggered heavy meteors!
        cfg->travelSpeed = 22.0f;
        cfg->arcHeight = 7.5f;
        cfg->trajectorySpread = 1.2f;
        cfg->swirlSpeed = 0.5f;
        cfg->tumbleSpeed = 160.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 28;
        cfg->shatterVelocity = 7.5f;
        cfg->impactDustRadius = 3.0f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = true;
        cfg->rockBaseColor = (Color){45, 40, 38, 255};
        cfg->veinColor = (Color){255, 110, 25, 255};
        cfg->veinIntensity = 0.65f;
        break;

    case VFX_EARTH_PRESET_MUD_SLUG:
        cfg->style = VFX_EARTH_STYLE_MUD_CHUNK;
        cfg->spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_CRATER_CRACK;
        cfg->rockCount = 3;
        cfg->rockRadius = 0.65f;
        cfg->rockSquashY = 0.75f;
        cfg->rockSquashZ = 0.85f;
        cfg->jaggedness = 0.15f;
        cfg->clusterSpread = 0.80f;
        cfg->sizeVariation = 0.25f;
        cfg->emergeDuration = 0.40f;
        cfg->hoverDuration = 0.20f;
        cfg->emergeHeight = 0.9f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->launchStaggerDelay = 0.18f;
        cfg->travelSpeed = 15.0f;
        cfg->arcHeight = 2.6f;
        cfg->trajectorySpread = 0.8f;
        cfg->swirlSpeed = 0.0f;
        cfg->tumbleSpeed = 90.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 12;
        cfg->shatterVelocity = 3.8f;
        cfg->impactDustRadius = 1.8f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = true;
        cfg->impactShockRing = false;
        cfg->rockBaseColor = (Color){95, 72, 50, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_JADE_SHARDS:
        // Hexagonal jade crystal prisms piercing target in rapid cadence
        cfg->style = VFX_EARTH_STYLE_JADE_CRYSTAL;
        cfg->spawnMode = VFX_EARTH_SPAWN_ORBITAL_CONVERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_SPIRAL_BARREL;
        cfg->impactMode = VFX_EARTH_IMPACT_PIERCE_EMBED;
        cfg->rockCount = 7;
        cfg->rockRadius = 0.28f;
        cfg->rockSquashY = 0.35f;
        cfg->rockSquashZ = 0.35f;
        cfg->jaggedness = 0.10f;
        cfg->clusterSpread = 1.10f;
        cfg->sizeVariation = 0.30f;
        cfg->emergeDuration = 0.50f;
        cfg->hoverDuration = 0.25f;
        cfg->emergeHeight = 1.2f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->launchStaggerDelay = 0.10f; // Rapid crystalline barrage!
        cfg->travelSpeed = 30.0f;
        cfg->arcHeight = 1.6f;
        cfg->trajectorySpread = 1.2f;
        cfg->swirlSpeed = 8.0f;
        cfg->tumbleSpeed = 380.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 18;
        cfg->shatterVelocity = 5.2f;
        cfg->impactDustRadius = 1.2f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = false;
        cfg->impactShockRing = true;
        cfg->rockBaseColor = (Color){80, 210, 145, 255};
        cfg->veinColor = (Color){185, 255, 220, 255};
        cfg->veinIntensity = 1.4f;
        break;

    case VFX_EARTH_PRESET_SAND_SWARM:
        // Dense spray of scree pebbles
        cfg->style = VFX_EARTH_STYLE_GRAVEL_SWARM;
        cfg->spawnMode = VFX_EARTH_SPAWN_GROUND_EMERGE;
        cfg->trajectoryMode = VFX_EARTH_TRAJ_PARABOLIC_ARC;
        cfg->impactMode = VFX_EARTH_IMPACT_SHATTER;
        cfg->rockCount = 14;
        cfg->rockRadius = 0.14f;
        cfg->rockSquashY = 0.80f;
        cfg->rockSquashZ = 0.85f;
        cfg->jaggedness = 0.40f;
        cfg->clusterSpread = 1.40f;
        cfg->sizeVariation = 0.45f;
        cfg->emergeDuration = 0.30f;
        cfg->hoverDuration = 0.15f;
        cfg->emergeHeight = 0.8f;
        cfg->emergeGroundDust = false;
        cfg->emergeCracks = false;
        cfg->launchStaggerDelay = 0.05f; // Rapid fire stream!
        cfg->travelSpeed = 22.0f;
        cfg->arcHeight = 1.8f;
        cfg->trajectorySpread = 1.5f;
        cfg->swirlSpeed = 2.5f;
        cfg->tumbleSpeed = 420.0f;
        cfg->emitDustTrail = false;
        cfg->dustRate = 0.0f;
        cfg->shatterChips = 8;
        cfg->shatterVelocity = 3.5f;
        cfg->impactDustRadius = 1.6f;
        cfg->impactShake = 0.0f;
        cfg->impactCraterCrack = false;
        cfg->impactShockRing = false;
        cfg->rockBaseColor = (Color){200, 170, 120, 255};
        cfg->veinIntensity = 0.0f;
        break;

    case VFX_EARTH_PRESET_CUSTOM:
    default:
        break;
    }
}

// ── Naming Helpers ───────────────────────────────────────────────────────────

const char *VFX_GuidedEarthPreset_Name(VFX_GuidedEarthPreset preset)
{
    static const char *const names[] = {
        "Custom", "Boulder Lob", "Rock Barrage", "Meteor Crush",
        "Mud Slug", "Jade Shards", "Sand Swarm"
    };
    return (preset >= 0 && preset < VFX_EARTH_PRESET_COUNT) ? names[preset] : "Unknown";
}

const char *VFX_EarthRockStyle_Name(VFX_EarthRockStyle style)
{
    static const char *const names[] = {
        "Boulder", "Jagged Slate", "Mud Chunk", "Gravel Swarm",
        "Jade Crystal", "Magma Rock"
    };
    return (style >= 0 && style < VFX_EARTH_STYLE_COUNT) ? names[style] : "Unknown";
}

const char *VFX_EarthSpawnMode_Name(VFX_EarthSpawnMode mode)
{
    static const char *const names[] = {
        "Ground Emerge", "Surround Ring", "Orbital Converge", "Levitate Rise", "Instant Launch"
    };
    return (mode >= 0 && mode < VFX_EARTH_SPAWN_COUNT) ? names[mode] : "Unknown";
}

const char *VFX_EarthTrajectory_Name(VFX_EarthTrajectoryMode traj)
{
    static const char *const names[] = {
        "Parabolic Arc", "Guided Homing", "Spiral Barrel", "High Catapult"
    };
    return (traj >= 0 && traj < VFX_EARTH_TRAJ_COUNT) ? names[traj] : "Unknown";
}

const char *VFX_EarthImpact_Name(VFX_EarthImpactMode impact)
{
    static const char *const names[] = {
        "Shatter Debris", "Crater & Cracks", "Quake Shockwave", "Pierce Embed"
    };
    return (impact >= 0 && impact < VFX_EARTH_IMPACT_COUNT) ? names[impact] : "Unknown";
}

// ── Inspector Parameter Exposure ─────────────────────────────────────────────

int VFX_GuidedEarth_GetParams(VFX_GuidedEarthConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    static const char *const presetNames[] = {
        "Custom", "Boulder Lob", "Rock Barrage", "Meteor Crush",
        "Mud Slug", "Jade Shards", "Sand Swarm"
    };
    static const char *const styleNames[] = {
        "Boulder", "Jagged Slate", "Mud Chunk", "Gravel Swarm",
        "Jade Crystal", "Magma Rock"
    };
    static const char *const spawnNames[] = {
        "Ground Emerge", "Surround Ring", "Orbital Converge", "Levitate Rise", "Instant Launch"
    };
    static const char *const trajNames[] = {
        "Parabolic Arc", "Guided Homing", "Spiral Barrel", "High Catapult"
    };
    static const char *const impactNames[] = {
        "Shatter Debris", "Crater & Cracks", "Quake Shockwave", "Pierce Embed"
    };

#define EARTH_PARAM_ENUM(pname, pgroup, val, enames, ecount) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_ENUM, \
        .valPtr = (void*)&(val), .minInt = 0, .maxInt = (ecount) - 1, \
        .stepFloat = 1.0f, .enumNames = enames, .enumCount = ecount \
    }; } while(0)

#define EARTH_PARAM_FLOAT(pname, pgroup, val, vmin, vmax, vstep) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_FLOAT, \
        .valPtr = (void*)&(val), .minFloat = vmin, .maxFloat = vmax, \
        .stepFloat = vstep \
    }; } while(0)

#define EARTH_PARAM_INT(pname, pgroup, val, vmin, vmax) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_INT, \
        .valPtr = (void*)&(val), .minInt = vmin, .maxInt = vmax, \
        .stepFloat = 1.0f \
    }; } while(0)

#define EARTH_PARAM_BOOL(pname, pgroup, val) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_BOOL, \
        .valPtr = (void*)&(val) \
    }; } while(0)

    // Preset & Architecture
    EARTH_PARAM_ENUM("Preset", "Preset", cfg->preset, presetNames, VFX_EARTH_PRESET_COUNT);
    EARTH_PARAM_ENUM("Rock Style", "Preset", cfg->style, styleNames, VFX_EARTH_STYLE_COUNT);
    EARTH_PARAM_ENUM("Spawn Mode", "Preset", cfg->spawnMode, spawnNames, VFX_EARTH_SPAWN_COUNT);
    EARTH_PARAM_ENUM("Trajectory", "Preset", cfg->trajectoryMode, trajNames, VFX_EARTH_TRAJ_COUNT);
    EARTH_PARAM_ENUM("Impact Mode", "Preset", cfg->impactMode, impactNames, VFX_EARTH_IMPACT_COUNT);

    // Physical Geometry & Formation
    EARTH_PARAM_INT("Rock count", "Geometry", cfg->rockCount, 1, VC_GUIDED_EARTH_MAX_ROCKS);
    EARTH_PARAM_FLOAT("Rock radius m", "Geometry", cfg->rockRadius, 0.05f, 2.5f, 0.05f);
    EARTH_PARAM_FLOAT("Size variation", "Geometry", cfg->sizeVariation, 0.0f, 1.0f, 0.05f);
    EARTH_PARAM_FLOAT("Squash Y", "Geometry", cfg->rockSquashY, 0.2f, 2.0f, 0.05f);
    EARTH_PARAM_FLOAT("Squash Z", "Geometry", cfg->rockSquashZ, 0.2f, 2.0f, 0.05f);
    EARTH_PARAM_FLOAT("Jaggedness", "Geometry", cfg->jaggedness, 0.0f, 1.0f, 0.05f);
    EARTH_PARAM_FLOAT("Cluster spread m", "Geometry", cfg->clusterSpread, 0.0f, 3.0f, 0.05f);

    // Phase 1: Emergence & Formation Hold
    EARTH_PARAM_FLOAT("Emerge time s", "Emergence", cfg->emergeDuration, 0.0f, 3.0f, 0.05f);
    EARTH_PARAM_FLOAT("Hover hold s", "Emergence", cfg->hoverDuration, 0.0f, 2.0f, 0.05f);
    EARTH_PARAM_BOOL("Launch cracks", "Emergence", cfg->emergeCracks);

    // Phase 2: Flight & Parabolic Motion
    EARTH_PARAM_FLOAT("Launch stagger s", "Flight", cfg->launchStaggerDelay, 0.0f, 0.60f, 0.02f);
    EARTH_PARAM_FLOAT("Travel speed m/s", "Flight", cfg->travelSpeed, 2.0f, 60.0f, 1.0f);
    EARTH_PARAM_FLOAT("Arc height m", "Flight", cfg->arcHeight, 0.2f, 20.0f, 0.2f);
    EARTH_PARAM_FLOAT("Arc variation", "Flight", cfg->arcVariation, 0.0f, 1.0f, 0.05f);
    EARTH_PARAM_FLOAT("Traj spread m", "Flight", cfg->trajectorySpread, 0.0f, 3.0f, 0.1f);
    EARTH_PARAM_FLOAT("Target spread m", "Flight", cfg->targetSpread, 0.0f, 4.0f, 0.1f);
    EARTH_PARAM_FLOAT("Gravity scale", "Flight", cfg->gravityScale, 0.0f, 3.0f, 0.1f);
    EARTH_PARAM_FLOAT("Swirl speed rad/s", "Flight", cfg->swirlSpeed, 0.0f, 25.0f, 0.5f);
    EARTH_PARAM_FLOAT("Tumble speed deg/s", "Flight", cfg->tumbleSpeed, 0.0f, 720.0f, 10.0f);

    // Phase 3: Impact & Aftermath
    EARTH_PARAM_INT("Shatter chips", "Impact", cfg->shatterChips, 0, 48);
    EARTH_PARAM_FLOAT("Shatter vel m/s", "Impact", cfg->shatterVelocity, 1.0f, 15.0f, 0.5f);
    EARTH_PARAM_FLOAT("Impact dust radius", "Impact", cfg->impactDustRadius, 0.5f, 6.0f, 0.1f);
    EARTH_PARAM_BOOL("Crater cracks", "Impact", cfg->impactCraterCrack);
    EARTH_PARAM_BOOL("Shockwave ring", "Impact", cfg->impactShockRing);

    // Material Optics & Shading
    EARTH_PARAM_FLOAT("Vein intensity", "Optics", cfg->veinIntensity, 0.0f, 4.0f, 0.1f);
    EARTH_PARAM_FLOAT("Ambient floor", "Optics", cfg->ambientGain, 0.10f, 0.60f, 0.02f);
    EARTH_PARAM_FLOAT("Specular gain", "Optics", cfg->specularGain, 0.0f, 3.0f, 0.05f);

#undef EARTH_PARAM_ENUM
#undef EARTH_PARAM_FLOAT
#undef EARTH_PARAM_INT
#undef EARTH_PARAM_BOOL

    return n;
}

// ── Instance Spawning & Execution ────────────────────────────────────────────

int VFX_ComposeGuidedEarthEx(Vector3 source, Vector3 target, const VFX_GuidedEarthConfig *cfg)
{
    EarthFx_InitShared();
    if (!cfg) cfg = VFX_GuidedEarth_GetLiveConfig();

    VC_GuidedEarthInstance *inst = NULL;
    for (int i = 0; i < VC_GUIDED_EARTH_MAX_INSTANCES; i++) {
        if (!s_guidedEarthInstances[i].active) {
            inst = &s_guidedEarthInstances[i];
            break;
        }
    }
    if (!inst) return 0; // Pool exhausted

    memset(inst, 0, sizeof(VC_GuidedEarthInstance));
    inst->active = true;
    inst->id = s_guidedEarthNextId++;
    inst->config = *cfg;
    inst->config.source = source;
    inst->config.target = target;
    inst->curTarget = target;
    inst->timer = 0.0f;
    inst->emergeWindTriggered = false;
    inst->craterSpawned = false;

    float distXZ = sqrtf((target.x - source.x) * (target.x - source.x) +
                         (target.z - source.z) * (target.z - source.z));
    float speed = fmaxf(cfg->travelSpeed, 1.0f);
    inst->flightDuration = fmaxf(distXZ / speed, 0.15f);

    Vector3 fwd = Vector3Subtract(target, source);
    Vector3 fwdNorm = (distXZ > 0.001f) ? (Vector3){fwd.x / distXZ, 0.0f, fwd.z / distXZ} : (Vector3){0.0f, 0.0f, 1.0f};
    Vector3 rgtNorm = (Vector3){-fwdNorm.z, 0.0f, fwdNorm.x};

    int count = Clamp(cfg->rockCount, 1, VC_GUIDED_EARTH_MAX_ROCKS);
    inst->rockCount = count;

    for (int i = 0; i < count; i++) {
        VC_GuidedRock *r = &inst->rocks[i];
        r->seed = 10007u * (unsigned int)(inst->id + 1) + (unsigned int)(i * 977);
        r->wakeTimer = 0.0f;
        r->crumbTimer = 0.0f;
        r->hasImpacted = false;
        r->launchWindFired = false;
        r->flightProgress = 0.0f;

        // Rock radius with sizeVariation: Apex stone (1.3-1.4x), flankers (0.9-1.0x), escort shards (0.4-0.6x)
        float sizeFrac = (count > 1) ? ((float)i / (float)(count - 1)) : 0.0f;
        float sizeMult = (1.0f + cfg->sizeVariation * 0.40f) * (1.0f - cfg->sizeVariation * 0.70f * sizeFrac);
        r->radius = cfg->rockRadius * sizeMult;

        // Spatial formation layout
        if (count == 1) {
            r->localOffset = Vector3Zero();
        } else if (cfg->spawnMode == VFX_EARTH_SPAWN_SURROUND_RING) {
            // 360-degree perimeter ring / scattered field around caster
            float angle = ((float)i / (float)count) * 2.0f * PI + (float)(r->seed % 100) * 0.005f;
            float spreadR = fmaxf(cfg->clusterSpread, 1.4f);
            float dist = spreadR * (0.80f + 0.35f * ((float)(r->seed % 10) / 9.0f));
            float elev = 0.15f * sinf((float)i * 2.1f);
            r->localOffset = (Vector3){cosf(angle) * dist, elev, sinf(angle) * dist};
        } else {
            // Tactical combat fan formation arc
            float spreadFrac = ((float)i / (float)(count - 1)) - 0.5f; // -0.5 to +0.5
            float angle = spreadFrac * 2.2f;
            float arcR = cfg->clusterSpread * 1.5f + 0.35f;
            float lateral = sinf(angle) * arcR;
            float depth = -cosf(angle) * arcR * 0.45f;
            float elev = (1.0f - fabsf(spreadFrac) * 0.35f) * 0.30f;
            r->localOffset = (Vector3){lateral, elev, depth};
        }

        // World-space launch hover position
        Vector3 worldSpawnBase;
        if (cfg->spawnMode == VFX_EARTH_SPAWN_SURROUND_RING) {
            worldSpawnBase = (Vector3){source.x + r->localOffset.x, 0.0f, source.z + r->localOffset.z};
        } else {
            worldSpawnBase = (Vector3){
                source.x + rgtNorm.x * r->localOffset.x + fwdNorm.x * r->localOffset.z,
                0.0f,
                source.z + rgtNorm.z * r->localOffset.x + fwdNorm.z * r->localOffset.z
            };
        }
        float groundY = VFX_GroundHeightFromMap(worldSpawnBase.x, worldSpawnBase.z, NULL);
        r->launchPos = (Vector3){
            worldSpawnBase.x,
            groundY + cfg->emergeHeight + r->localOffset.y,
            worldSpawnBase.z
        };

        // Per-rock destination point (with targetSpread)
        if (cfg->targetSpread > 0.001f && count > 1) {
            float tAngle = ((float)i / (float)count) * 2.0f * PI + (float)(r->seed % 50) * 0.02f;
            float tDist = cfg->targetSpread * (0.35f + 0.65f * ((float)(r->seed % 8) / 7.0f));
            r->targetPos = (Vector3){
                target.x + cosf(tAngle) * tDist,
                target.y,
                target.z + sinf(tAngle) * tDist
            };
        } else {
            r->targetPos = target;
        }

        // Per-rock individual arc height (with arcVariation)
        float arcVar = 1.0f;
        if (cfg->arcVariation > 0.001f && count > 1) {
            float frac = ((float)(i * 3 % count) / (float)(count - 1)) - 0.5f;
            arcVar = 1.0f + frac * cfg->arcVariation * 0.85f;
        }
        r->arcHeight = cfg->arcHeight * arcVar;
        r->emergeFired = false;

        // Sequential launch timing
        r->launchTime = cfg->emergeDuration + cfg->hoverDuration + (float)i * cfg->launchStaggerDelay;
        r->state = (cfg->emergeDuration > 0.001f) ? ROCK_STATE_EMERGING : ROCK_STATE_HOVERING;

        // Tumbling rotation axis & initial angle
        float ax = sinf((float)i * 1.7f + 0.3f);
        float ay = cosf((float)i * 2.3f + 0.5f);
        float az = sinf((float)i * 3.1f + 0.7f);
        r->spinAxis = Vector3Normalize((Vector3){ax, ay, az});
        if (Vector3LengthSqr(r->spinAxis) < 0.001f) r->spinAxis = (Vector3){0.0f, 1.0f, 0.0f};
        r->spinAngle = (float)(r->seed % 360);
        r->spinSpeed = cfg->tumbleSpeed * (0.8f + 0.4f * ((float)(i % 4) / 3.0f));

        // Procedural rock low-poly geometry for chosen style
        GuidedEarth_BuildRockMeshForStyle(&r->mesh, cfg->style, r->radius, cfg->jaggedness, (int)r->seed);
        r->meshBuilt = (r->mesh.faceCount > 0);

        // Initial position
        if (cfg->emergeDuration > 0.001f) {
            float startY = groundY - r->radius * 1.3f;
            r->pos = (Vector3){r->launchPos.x, startY, r->launchPos.z};
        } else {
            r->pos = r->launchPos;
        }
        r->prevPos = r->pos;
    }

    return inst->id;
}

int VFX_ComposeGuidedEarth(const VFX_GuidedEarthConfig *cfg)
{
    if (!cfg) cfg = VFX_GuidedEarth_GetLiveConfig();
    return VFX_ComposeGuidedEarthEx(cfg->source, cfg->target, cfg);
}

void VFX_KillGuidedEarth(int handle)
{
    for (int i = 0; i < VC_GUIDED_EARTH_MAX_INSTANCES; i++) {
        if (s_guidedEarthInstances[i].active && s_guidedEarthInstances[i].id == handle) {
            s_guidedEarthInstances[i].active = false;
            break;
        }
    }
}

// ── Physical Simulation & Phase Evolution ────────────────────────────────────

static void GuidedEarth_TriggerImpact(VC_GuidedEarthInstance *inst, VC_GuidedRock *rock, bool isFinisher)
{
    if (rock->hasImpacted) return;
    rock->hasImpacted = true;

    const VFX_GuidedEarthConfig *cfg = &inst->config;
    Vector3 impactPos = rock->pos;
    impactPos.y = VFX_GroundHeightFromMap(impactPos.x, impactPos.z, NULL);

    // 1. Motion Field / Wind shockwave on vegetation (flattens grass and flower stalks outward)
    float blastRad = rock->radius * (isFinisher ? 5.5f : 4.0f);
    float blastSpeed = isFinisher ? 11.0f : 7.0f;
    Wind_SpawnRadialBlast(impactPos, blastRad, blastSpeed, 0.55f);
    Wind_SpawnTurbulence(impactPos, blastRad * 0.75f, 5.5f, 0.25f, 3.0f, 0.40f);

    // 2. Physical tumbling debris chips under real 9.81 m/s^2 gravity
    int chips = (inst->rockCount > 1) ? (cfg->shatterChips / inst->rockCount + 3) : cfg->shatterChips;
    if (chips > 0) {
        Vector3 baseVel = (Vector3){0.0f, cfg->shatterVelocity * 0.65f, 0.0f};
        VFX_ComposeDebrisShards(impactPos, baseVel, cfg->matId, rock->radius * 0.38f, chips);
    }

    // 3. Particulate expanding dust cloud (soft, non-emissive)
    float dustRad = (inst->rockCount > 1) ? (cfg->impactDustRadius * 0.75f) : cfg->impactDustRadius;
    if (dustRad > 0.05f) {
        VFX_ComposeImpactDustVariant(impactPos, cfg->matId, dustRad, 0.70f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
    }

    // 4. Radial crater fissure streaks (first rock or finisher)
    if (cfg->impactCraterCrack && (isFinisher || !inst->craterSpawned)) {
        inst->craterSpawned = true;
        for (int k = 0; k < 3; k++) {
            float angle = ((float)k / 3.0f) * 2.0f * PI + ((float)rock->seed * 0.01f);
            float dist = cfg->rockRadius * (2.2f + 0.8f * ((float)k / 3.0f));
            Vector3 end = Vector3Add(impactPos, (Vector3){cosf(angle) * dist, 0.0f, sinf(angle) * dist});
            end.y = VFX_GroundHeightFromMap(end.x, end.z, NULL);
            VFX_ComposeFissureStreak(impactPos, end, cfg->rockRadius * 0.28f, 1.0f, inst->timer);
        }
    }

    // 5. Ground wave seismic shock ring
    if (cfg->impactShockRing && isFinisher) {
        VFX_ComposeGroundWave(impactPos, cfg->matId, cfg->rockRadius * 2.8f, 0.5f, VFX_GroundHeightFromMap, NULL);
    }
}

void VC_GuidedEarth_Update(float dt)
{
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        inst->timer += dt;
        const VFX_GuidedEarthConfig *cfg = &inst->config;

        // Vector frame along trajectory
        Vector3 fwd = Vector3Subtract(inst->curTarget, cfg->source);
        float distXZ = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
        Vector3 fwdNorm = (distXZ > 0.001f) ? Vector3Normalize(fwd) : (Vector3){0.0f, 0.0f, 1.0f};
        Vector3 worldUp = (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 rightNorm = Vector3Normalize(Vector3CrossProduct(fwdNorm, worldUp));
        Vector3 upNorm = Vector3CrossProduct(rightNorm, fwdNorm);

        // Motion Field / Wind blast on eruption
        if (!inst->emergeWindTriggered && cfg->emergeDuration > 0.001f) {
            inst->emergeWindTriggered = true;
            Wind_SpawnRadialBlast(cfg->source, cfg->rockRadius * 3.6f, 6.0f, 0.45f);
            Wind_SpawnTurbulence(cfg->source, cfg->rockRadius * 2.8f, 3.5f, 0.25f, 2.5f, 0.35f);

            // Ground eruption soil clods & particulate dust puff
            Vector3 clodVel = Vector3Add((Vector3){0.0f, 3.6f, 0.0f}, Vector3Scale(fwdNorm, 1.2f));
            VFX_ComposeDebrisShards(cfg->source, clodVel, cfg->matId, cfg->rockRadius * 0.20f, 5);
            VFX_ComposeImpactDustVariant(cfg->source, cfg->matId, cfg->rockRadius * 1.5f, 0.60f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
        }

        // Launch ground cracks during emergence
        if (cfg->emergeCracks && inst->timer <= cfg->emergeDuration) {
            float eProgress = Clamp(inst->timer / fmaxf(cfg->emergeDuration, 0.01f), 0.0f, 1.0f);
            for (int k = 0; k < 2; k++) {
                float angle = ((float)k / 2.0f) * PI + 0.45f;
                float crackR = cfg->rockRadius * 1.8f * eProgress;
                Vector3 endP = Vector3Add(cfg->source, (Vector3){cosf(angle) * crackR, 0.0f, sinf(angle) * crackR});
                endP.y = VFX_GroundHeightFromMap(endP.x, endP.z, NULL);
                VFX_ComposeFissureStreak(cfg->source, endP, cfg->rockRadius * 0.22f, eProgress, inst->timer);
            }
        }

        bool allRocksImpacted = true;

        for (int i = 0; i < inst->rockCount; i++) {
            VC_GuidedRock *r = &inst->rocks[i];
            if (r->state == ROCK_STATE_IMPACTED) continue;
            allRocksImpacted = false;

            // ── ROCK_STATE_EMERGING ──────────────────────────────────────────
            if (r->state == ROCK_STATE_EMERGING) {
                float emergeDur = fmaxf(cfg->emergeDuration, 0.01f);
                float prog = Clamp(inst->timer / emergeDur, 0.0f, 1.0f);
                float ease = prog * prog * (3.0f - 2.0f * prog);

                float groundY = VFX_GroundHeightFromMap(r->launchPos.x, r->launchPos.z, NULL);
                float startY = groundY - r->radius * 1.3f;
                r->pos = (Vector3){
                    r->launchPos.x,
                    Math_Mix(startY, r->launchPos.y, ease),
                    r->launchPos.z
                };
                r->prevPos = r->pos;
                r->spinAngle += r->spinSpeed * 0.35f * dt;

                // Per-rock emergence effects directly at this rock's launch position
                if (!r->emergeFired && prog > 0.08f) {
                    r->emergeFired = true;
                    Vector3 groundPt = (Vector3){r->launchPos.x, groundY, r->launchPos.z};
                    Wind_SpawnRadialBlast(groundPt, r->radius * 2.8f, 4.5f, 0.30f);
                    Vector3 clodVel = (Vector3){(r->localOffset.x > 0.0f ? 0.6f : -0.6f), 3.2f, (r->localOffset.z > 0.0f ? 0.6f : -0.6f)};
                    VFX_ComposeDebrisShards(groundPt, clodVel, cfg->matId, r->radius * 0.18f, 3);
                    VFX_ComposeImpactDustVariant(groundPt, cfg->matId, r->radius * 1.3f, 0.50f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);

                    if (cfg->emergeCracks && cfg->spawnMode == VFX_EARTH_SPAWN_SURROUND_RING) {
                        for (int k = 0; k < 2; k++) {
                            float cAngle = ((float)k / 2.0f) * PI + ((float)r->seed * 0.01f);
                            float cDist = r->radius * 1.5f;
                            Vector3 cEnd = Vector3Add(groundPt, (Vector3){cosf(cAngle) * cDist, 0.0f, sinf(cAngle) * cDist});
                            cEnd.y = VFX_GroundHeightFromMap(cEnd.x, cEnd.z, NULL);
                            VFX_ComposeFissureStreak(groundPt, cEnd, r->radius * 0.20f, 1.0f, inst->timer);
                        }
                    }
                }

                if (prog >= 1.0f) {
                    r->state = ROCK_STATE_HOVERING;
                }
            }

            // ── ROCK_STATE_HOVERING ──────────────────────────────────────────
            if (r->state == ROCK_STATE_HOVERING) {
                if (inst->timer >= r->launchTime) {
                    // Transition to FLYING!
                    r->state = ROCK_STATE_FLYING;
                    r->launchWindFired = true;
                    Wind_SpawnGust(r->pos, fwdNorm, r->radius * 3.0f, fminf(cfg->travelSpeed * 0.5f, 10.0f), 0.22f);
                    VFX_ComposeImpactDustVariant(r->pos, cfg->matId, r->radius * 0.70f, 0.40f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
                } else {
                    // Hover in tactical formation socket with subtle floating bob
                    float bob = sinf(inst->timer * 5.0f + (float)i * 1.4f) * 0.04f;
                    r->pos = (Vector3){r->launchPos.x, r->launchPos.y + bob, r->launchPos.z};
                    r->prevPos = r->pos;
                    r->spinAngle += r->spinSpeed * 0.20f * dt;
                }
            }

            // ── ROCK_STATE_FLYING ────────────────────────────────────────────
            if (r->state == ROCK_STATE_FLYING) {
                float flightElapsed = inst->timer - r->launchTime;
                r->flightProgress = Clamp(flightElapsed / fmaxf(inst->flightDuration, 0.05f), 0.0f, 1.0f);
                float tau = r->flightProgress;

                // Parabolic Arc using this rock's individual arcHeight
                float arcY = 0.0f;
                if (cfg->trajectoryMode == VFX_EARTH_TRAJ_HIGH_CATAPULT) {
                    arcY = r->arcHeight * sinf(tau * PI) * (1.0f + 0.55f * (1.0f - tau));
                } else {
                    arcY = 4.0f * r->arcHeight * tau * (1.0f - tau);
                }

                // Gravity drop
                if (cfg->gravityScale > 0.01f) {
                    float gDrop = 0.5f * (9.81f * cfg->gravityScale) * (flightElapsed * flightElapsed * 0.15f);
                    arcY = fmaxf(0.0f, arcY - gDrop * tau);
                }

                // Per-rock 3D baseline vector to its target
                Vector3 rockFwd = Vector3Subtract(r->targetPos, r->launchPos);
                float rockDist = Vector3Length(rockFwd);
                Vector3 rockFwdNorm = (rockDist > 0.001f) ? Vector3Scale(rockFwd, 1.0f / rockDist) : fwdNorm;
                Vector3 rockRightNorm = Vector3Normalize(Vector3CrossProduct(rockFwdNorm, worldUp));
                Vector3 rockUpNorm = Vector3CrossProduct(rockRightNorm, rockFwdNorm);

                // Trajectory spread fan
                Vector3 latSpread = Vector3Zero();
                if (cfg->trajectorySpread > 0.001f && inst->rockCount > 1) {
                    float spreadFrac = ((float)i / (float)(inst->rockCount - 1)) - 0.5f;
                    float lateralDist = spreadFrac * cfg->trajectorySpread * sinf(tau * PI);
                    latSpread = Vector3Scale(rockRightNorm, lateralDist);
                }

                // Swirl corkscrew
                if (cfg->swirlSpeed > 0.01f) {
                    float a = inst->timer * cfg->swirlSpeed + ((float)i / (float)inst->rockCount) * 2.0f * PI;
                    float rollR = cfg->clusterSpread * 0.45f * sinf(tau * PI);
                    latSpread = Vector3Add(latSpread, Vector3Add(
                        Vector3Scale(rockRightNorm, cosf(a) * rollR),
                        Vector3Scale(rockUpNorm, sinf(a) * rollR)
                    ));
                }

                Vector3 basePos = Vector3Lerp(r->launchPos, r->targetPos, tau);
                basePos.y += arcY;
                r->prevPos = r->pos;
                r->pos = Vector3Add(basePos, latSpread);

                // Tumbling rotation
                r->spinAngle += r->spinSpeed * dt;

                // Physical In-Flight Crumb Shedding (1-2 tiny debris chips falling under 9.81 m/s^2)
                r->crumbTimer += dt;
                if (r->crumbTimer >= 0.16f && tau < 0.88f) {
                    r->crumbTimer = 0.0f;
                    Vector3 crumbVel = Vector3Scale(fwdNorm, -0.6f);
                    VFX_ComposeDebrisShards(r->pos, crumbVel, cfg->matId, r->radius * 0.12f, 2);
                }

                // Motion Field / Aerodynamic wake parting vegetation
                r->wakeTimer += dt;
                if (r->wakeTimer >= 0.045f) {
                    r->wakeTimer = 0.0f;
                    float wakeRadius = r->radius * 2.8f;
                    float wakeSpeed = fminf(cfg->travelSpeed * 0.40f, 8.0f);
                    Wind_SpawnGust(r->pos, fwdNorm, wakeRadius, wakeSpeed, 0.18f);

                    float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
                    if (r->pos.y - groundY < wakeRadius * 1.6f) {
                        Vector3 groundPt = (Vector3){r->pos.x, groundY, r->pos.z};
                        Wind_SpawnRadialBlast(groundPt, wakeRadius * 1.1f, wakeSpeed * 0.65f, 0.14f);
                    }
                }

                // Impact trigger
                float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
                if (tau >= 1.0f || (tau > 0.5f && r->pos.y <= groundY + r->radius * 0.3f)) {
                    bool isFinisher = (i == inst->rockCount - 1) || (inst->rockCount == 1);
                    GuidedEarth_TriggerImpact(inst, r, isFinisher);
                    r->state = ROCK_STATE_IMPACTED;
                }
            }
        }

        if (allRocksImpacted) {
            inst->active = false;
        }
    }
}

// ── Faceted Rock Rendering with Authored Lighting ────────────────────────────

static void GuidedEarth_DrawRockFaceted(const VC_GuidedRock *rock, const VFX_GuidedEarthConfig *cfg)
{
    if (!rock->meshBuilt || rock->mesh.faceCount <= 0) return;

    rlPushMatrix();
    rlTranslatef(rock->pos.x, rock->pos.y, rock->pos.z);
    rlRotatef(rock->spinAngle, rock->spinAxis.x, rock->spinAxis.y, rock->spinAxis.z);
    rlScalef(1.0f, cfg->rockSquashY, cfg->rockSquashZ);

    rlCheckRenderBatchLimit(rock->mesh.faceCount * 3);
    rlBegin(RL_TRIANGLES);

    Color base = cfg->rockBaseColor;
    float amb = cfg->ambientGain;
    float specGain = cfg->specularGain;

    for (int f = 0; f < rock->mesh.faceCount; f++) {
        Vector3 norm = rock->mesh.faceNormals[f];
        Vector3 worldNorm = Vector3RotateByAxisAngle(norm, rock->spinAxis, rock->spinAngle * DEG2RAD);
        float ndl = Vector3DotProduct(worldNorm, k_earthKeyLight);
        if (ndl < 0.0f) ndl = 0.0f;

        // Geological style-specific lighting nuances
        float shade = amb + (1.0f - amb) * ndl;
        float specExp = (cfg->style == VFX_EARTH_STYLE_JADE_CRYSTAL) ? 36.0f :
                        (cfg->style == VFX_EARTH_STYLE_MUD_CHUNK) ? 14.0f : 24.0f;
        float glint = powf(ndl, specExp) * specGain;

        Color faceBase = base;
        if (cfg->style == VFX_EARTH_STYLE_BOULDER) {
            // Subtle granite strata tone modulation
            Vector3 v0 = rock->mesh.verts[rock->mesh.faceVertIdx[f][0]];
            float strata = 0.90f + 0.18f * sinf((v0.x + v0.y * 1.5f) * 5.0f);
            shade *= strata;
        } else if (cfg->style == VFX_EARTH_STYLE_MAGMA_ROCK) {
            // Inward depressed vesicular cavities have warm magma core glow
            Vector3 v0 = rock->mesh.verts[rock->mesh.faceVertIdx[f][0]];
            float dist = Vector3Length(v0);
            if (dist < rock->radius * 0.82f) {
                faceBase = (Color){185, 55, 20, 255};
                shade = fmaxf(shade, 0.65f);
            }
        }

        float r = (float)faceBase.r * shade + 255.0f * glint;
        float g = (float)faceBase.g * shade + 255.0f * glint;
        float b = (float)faceBase.b * shade + 255.0f * glint;
        Color faceCol = (Color){
            (unsigned char)Clamp(r, 0.0f, 255.0f),
            (unsigned char)Clamp(g, 0.0f, 255.0f),
            (unsigned char)Clamp(b, 0.0f, 255.0f),
            255
        };
        rlColor4ub(faceCol.r, faceCol.g, faceCol.b, 255);

        for (int k = 0; k < 3; k++) {
            Vector3 v = rock->mesh.verts[rock->mesh.faceVertIdx[f][k]];
            rlVertex3f(v.x, v.y, v.z);
        }
    }
    rlEnd();

    // Emissive vein / crystal ridge accents (magma, jade qi lines)
    if (cfg->veinIntensity > 0.05f) {
        VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
        rlCheckRenderBatchLimit(rock->mesh.faceCount * 2);
        rlBegin(RL_LINES);
        Color vCol = cfg->veinColor;
        unsigned char alpha = (unsigned char)Clamp(80.0f * cfg->veinIntensity, 0.0f, 255.0f);
        rlColor4ub(vCol.r, vCol.g, vCol.b, alpha);
        int step = (cfg->style == VFX_EARTH_STYLE_JADE_CRYSTAL) ? 2 : 4;
        for (int f = 0; f < rock->mesh.faceCount; f += step) {
            Vector3 v0 = rock->mesh.verts[rock->mesh.faceVertIdx[f][0]];
            Vector3 v1 = rock->mesh.verts[rock->mesh.faceVertIdx[f][1]];
            rlVertex3f(v0.x, v0.y, v0.z);
            rlVertex3f(v1.x, v1.y, v1.z);
        }
        rlEnd();
    }

    rlPopMatrix();
}

static Mesh s_earthShadowMesh = {0};
static Material s_earthShadowMat = {0};
static bool s_earthShadowInit = false;

static void GuidedEarth_InitShadowMesh(void)
{
    if (s_earthShadowInit) return;
    s_earthShadowMesh = ProceduralMesh_BuildRockTemplateMesh(1.0f, 0.28f, 1337, 1);
    s_earthShadowMat = LoadMaterialDefault();
    s_earthShadowInit = true;
}

void VC_GuidedEarth_DrawShadowPass(void)
{
    GuidedEarth_InitShadowMesh();
    Shader depthShader = EnvShadow_GetDepthShader();
    if (depthShader.id == 0 || s_earthShadowMesh.vertexCount == 0) return;
    s_earthShadowMat.shader = depthShader;

    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        const VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        for (int i = 0; i < inst->rockCount; i++) {
            const VC_GuidedRock *r = &inst->rocks[i];
            if (r->state == ROCK_STATE_IMPACTED) continue;

            float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
            if (r->pos.y < groundY - r->radius * 0.45f) continue;

            Matrix matScale = MatrixScale(r->radius, r->radius * inst->config.rockSquashY, r->radius * inst->config.rockSquashZ);
            Matrix matRot = MatrixRotate(r->spinAxis, r->spinAngle * DEG2RAD);
            Matrix matTrans = MatrixTranslate(r->pos.x, r->pos.y, r->pos.z);
            Matrix xf = MatrixMultiply(MatrixMultiply(matScale, matRot), matTrans);

            DrawMesh(s_earthShadowMesh, s_earthShadowMat, xf);
        }
    }
}

void VC_GuidedEarth_Draw3D(Camera3D cam)
{
    (void)cam;

    // 1. Draw Smart Directional Contact Shadows projected on terrain
    Environment_BeginShadowBatch();
    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        const VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        for (int i = 0; i < inst->rockCount; i++) {
            const VC_GuidedRock *r = &inst->rocks[i];
            if (r->state == ROCK_STATE_IMPACTED) continue;

            float groundY = VFX_GroundHeightFromMap(r->pos.x, r->pos.z, NULL);
            if (r->pos.y < groundY - r->radius * 0.4f) continue;

            float sWidth = r->radius * 1.15f;
            float sHeight = r->radius * (1.0f + inst->config.rockSquashY * 0.25f);
            Environment_DrawSmartShadow(r->pos, ENV_SHAPE_SPHERE, sWidth, sHeight);
        }
    }
    Environment_EndShadowBatch();

    // 2. Draw 3D Faceted Rock Geometry with Authored Lighting
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();

    for (int instIdx = 0; instIdx < VC_GUIDED_EARTH_MAX_INSTANCES; instIdx++) {
        const VC_GuidedEarthInstance *inst = &s_guidedEarthInstances[instIdx];
        if (!inst->active) continue;

        for (int i = 0; i < inst->rockCount; i++) {
            const VC_GuidedRock *r = &inst->rocks[i];
            if (r->state == ROCK_STATE_IMPACTED) continue;
            GuidedEarth_DrawRockFaceted(r, &inst->config);
        }
    }

    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

#endif // VC_GUIDED_EARTH_INL
