// ============================================================================
// vc_earth_spike.inl — Unified Earth Spikes & Stone Pillars Architecture
//
// High-performance procedural geological eruption system supporting:
// - Monolithic stone columns (cột đá basalt / granite)
// - Jagged earth spikes & cluster outcroppings (gai đất / gai đá)
// - Advancing spike waves (sóng gai địa chấn)
// - Circular earthen ring barriers & prisons (ngục đá / trận pháp)
// - Jade crystal obelisks & molten magma crags
//
// Features:
// - Closed-form faceted geometry generation with geological noise & cleavage planes
// - Ground-anchored via MapManager_GetGroundHeightAt (works on hills, basins, arena)
// - Punchy explosive emergence with mechanical overshoot bounce
// - Environmental coupling: ground crack decals, dust rings, soil/shards ejection,
//   smart contact shadows, grass-parting radial wind, and camera trauma
// - Shatter explosion / sinking retract lifecycles
// - Universal VFX Parameter System (VFX_ParamDef) for live sandbox tweaking
// ============================================================================

#ifndef VC_EARTH_SPIKE_INL
#define VC_EARTH_SPIKE_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "core/composition/visual_composer.h"
#include "core/presets/vfx_presets.h"
#include "post_process/camera_fx.h"
#include "core/wind/wind_system.h"
#include "core/material/material_system.h"
#include "environment/environment_system.h"
#include "maps/map_manager.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define VC_EARTH_SPIKE_MAX_VERTICES  128
#define VC_EARTH_SPIKE_MAX_FACES     128
#define VC_EARTH_SPIKE_MAX_SUB       16
#define VC_EARTH_SPIKE_MAX_INSTANCES 8

// ── Internal Mesh Data Structure ────────────────────────────────────────────

typedef struct EarthSpikeMesh {
    Vector3 verts[VC_EARTH_SPIKE_MAX_VERTICES];
    Vector3 normals[VC_EARTH_SPIKE_MAX_VERTICES];
    Vector2 uvs[VC_EARTH_SPIKE_MAX_VERTICES];
    int faceVertIdx[VC_EARTH_SPIKE_MAX_FACES][3];
    int vertCount;
    int faceCount;
} EarthSpikeMesh;

typedef struct EarthSubSpike {
    bool active;
    Vector3 baseOffset;     // displacement relative to instance target
    Vector3 groundPos;      // world space terrain anchor (sampled from map)
    Vector3 tiltDir;        // normalized slant direction vector in XZ plane
    float tiltAngle;        // radians of lean
    float height;           // individual pillar height
    float baseRadius;       // individual footprint radius
    float topRadius;        // apex radius
    float spawnDelay;       // seconds before emergence
    float timer;            // elapsed time since active
    float currentRise;      // current vertical elevation ratio [0 = underground, 1 = full height]
    bool eruptTriggered;    // emergence VFX fired
    bool shattered;         // shatter collapse fired
    EarthSpikeMesh mesh;    // local procedural geometry
} EarthSubSpike;

typedef struct VC_EarthSpikeInstance {
    int id;
    bool active;
    VFX_EarthSpikeConfig config;
    Vector3 center;
    Vector3 forward;
    float timer;
    float totalDuration;
    int subSpikeCount;
    EarthSubSpike subSpikes[VC_EARTH_SPIKE_MAX_SUB];
} VC_EarthSpikeInstance;

static VC_EarthSpikeInstance s_earthSpikeInstances[VC_EARTH_SPIKE_MAX_INSTANCES];
static VFX_EarthSpikeConfig s_liveEarthSpikeConfig;
static bool s_earthSpikeInit = false;
static int s_earthSpikeNextId = 1;

// ── Hash & Deterministic Noise Utilities ────────────────────────────────────

static inline unsigned int EarthSpike_Hash(unsigned int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static inline float EarthSpike_RandomFloat(unsigned int *seed)
{
    *seed = EarthSpike_Hash(*seed + 1013904223u);
    return (float)(*seed % 10000u) / 10000.0f;
}

// ── High-Fidelity Geological Procedural Mesh Generation ─────────────────────

static void EarthSpike_BuildMesh(EarthSpikeMesh *mesh, const VFX_EarthSpikeConfig *cfg,
                                 float height, float baseRadius, float topRadius, int seed)
{
    if (!mesh) return;
    memset(mesh, 0, sizeof(EarthSpikeMesh));

    int N = cfg->facetCount;
    if (N < 4) N = 4;
    if (N > 10) N = 10;

    int M = 4;

    float cosA[12];
    float sinA[12];
    for (int i = 0; i < N; i++) {
        float angle = ((float)i / (float)N) * 2.0f * PI;
        cosA[i] = cosf(angle);
        sinA[i] = sinf(angle);
    }

    bool isSpike = (cfg->shape == VFX_EARTH_SPIKE_SHAPE_SPIKE);
    bool isObelisk = (cfg->shape == VFX_EARTH_SPIKE_SHAPE_OBELISK);
    bool isSlab = (cfg->shape == VFX_EARTH_SPIKE_SHAPE_SLAB);

    int ringVertStart[6];
    int vIdx = 0;

    // 1. Generate vertex rings from bottom (y=0) to top (y=height)
    for (int j = 0; j <= M; j++) {
        ringVertStart[j] = vIdx;
        float vRatio = (float)j / (float)M;
        float y = vRatio * height;

        // Radius taper profile
        float rNominal;
        if (isSpike) {
            float taper = 1.0f - powf(vRatio, 0.75f);
            rNominal = baseRadius * fmaxf(taper, cfg->topRadiusRatio);
        } else if (isObelisk) {
            float taper = (vRatio < 0.70f) ? 1.0f : (1.0f - (vRatio - 0.70f) / 0.30f * (1.0f - cfg->topRadiusRatio));
            rNominal = baseRadius * taper;
        } else {
            rNominal = baseRadius + (topRadius - baseRadius) * vRatio;
        }

        for (int i = 0; i < N; i++) {
            if (vIdx >= VC_EARTH_SPIKE_MAX_VERTICES) break;

            float px = cosA[i] * rNominal;
            float pz = sinA[i] * rNominal;

            // Slab shape: wide along X, narrow along Z
            if (isSlab) {
                px *= 2.2f;
                pz *= 0.45f;
            }

            // Geological fracture noise on middle rings
            if (cfg->jaggedness > 0.001f && j > 0 && j < M) {
                float wave1 = sinf(vRatio * 7.0f + (float)i * 1.57f + (float)(seed % 17));
                float wave2 = cosf(vRatio * 15.0f - (float)i * 3.14f + (float)(seed % 31) * 0.5f);
                float noise = 1.0f + cfg->jaggedness * (0.65f * wave1 + 0.35f * wave2);
                px *= noise;
                pz *= noise;
            }

            mesh->verts[vIdx] = (Vector3){px, y, pz};
            mesh->normals[vIdx] = Vector3Normalize((Vector3){cosA[i], 0.05f, sinA[i]});
            mesh->uvs[vIdx] = (Vector2){(float)i / (float)N, vRatio * 2.0f};
            vIdx++;
        }
    }

    // Top cap vertex (apex point for spike, center point for flat/beveled cap)
    int apexIdx = -1;
    int topCenterIdx = -1;
    if (isSpike && cfg->topRadiusRatio < 0.12f && vIdx < VC_EARTH_SPIKE_MAX_VERTICES) {
        apexIdx = vIdx;
        mesh->verts[vIdx] = (Vector3){0.0f, height + baseRadius * 0.35f, 0.0f};
        mesh->normals[vIdx] = (Vector3){0.0f, 1.0f, 0.0f};
        mesh->uvs[vIdx] = (Vector2){0.5f, 1.0f};
        vIdx++;
    } else if (vIdx < VC_EARTH_SPIKE_MAX_VERTICES) {
        topCenterIdx = vIdx;
        mesh->verts[vIdx] = (Vector3){0.0f, height, 0.0f};
        mesh->normals[vIdx] = (Vector3){0.0f, 1.0f, 0.0f};
        mesh->uvs[vIdx] = (Vector2){0.5f, 0.5f};
        vIdx++;
    }

    // Bottom cap center vertex (for a 100% closed, watertight solid body)
    int bottomCenterIdx = -1;
    if (vIdx < VC_EARTH_SPIKE_MAX_VERTICES) {
        bottomCenterIdx = vIdx;
        mesh->verts[vIdx] = (Vector3){0.0f, 0.0f, 0.0f};
        mesh->normals[vIdx] = (Vector3){0.0f, -1.0f, 0.0f};
        mesh->uvs[vIdx] = (Vector2){0.5f, 0.5f};
        vIdx++;
    }

    mesh->vertCount = vIdx;

    // 2. Build side quad bands as triangles (100% Counter-Clockwise outside-facing)
    int fIdx = 0;
    for (int j = 0; j < M; j++) {
        int r0 = ringVertStart[j];
        int r1 = ringVertStart[j + 1];

        for (int i = 0; i < N; i++) {
            if (fIdx + 2 >= VC_EARTH_SPIKE_MAX_FACES) break;

            int nextI = (i + 1) % N;

            int v00 = r0 + i;       // bottom, angle i
            int v01 = r0 + nextI;   // bottom, angle nextI
            int v10 = r1 + i;       // top, angle i
            int v11 = r1 + nextI;   // top, angle nextI

            // Triangle 1: (v01, v00, v10) — genuine outward CCW
            mesh->faceVertIdx[fIdx][0] = v01;
            mesh->faceVertIdx[fIdx][1] = v00;
            mesh->faceVertIdx[fIdx][2] = v10;
            fIdx++;

            // Triangle 2: (v01, v10, v11) — genuine outward CCW
            mesh->faceVertIdx[fIdx][0] = v01;
            mesh->faceVertIdx[fIdx][1] = v10;
            mesh->faceVertIdx[fIdx][2] = v11;
            fIdx++;
        }
    }

    // 3. Top cap (genuine upward CCW)
    int topRing = ringVertStart[M];
    if (apexIdx >= 0) {
        for (int i = 0; i < N; i++) {
            if (fIdx >= VC_EARTH_SPIKE_MAX_FACES) break;
            int nextI = (i + 1) % N;
            // CCW outward/upward: (topRing + nextI, topRing + i, apexIdx)
            mesh->faceVertIdx[fIdx][0] = topRing + nextI;
            mesh->faceVertIdx[fIdx][1] = topRing + i;
            mesh->faceVertIdx[fIdx][2] = apexIdx;
            fIdx++;
        }
    } else if (topCenterIdx >= 0) {
        for (int i = 0; i < N; i++) {
            if (fIdx >= VC_EARTH_SPIKE_MAX_FACES) break;
            int nextI = (i + 1) % N;
            // CCW facing straight UP (+Y): (topCenterIdx, topRing + nextI, topRing + i)
            mesh->faceVertIdx[fIdx][0] = topCenterIdx;
            mesh->faceVertIdx[fIdx][1] = topRing + nextI;
            mesh->faceVertIdx[fIdx][2] = topRing + i;
            fIdx++;
        }
    }

    // 4. Bottom cap (genuine downward CCW)
    if (bottomCenterIdx >= 0) {
        int bottomRing = ringVertStart[0];
        for (int i = 0; i < N; i++) {
            if (fIdx >= VC_EARTH_SPIKE_MAX_FACES) break;
            int nextI = (i + 1) % N;
            // CCW facing straight DOWN (-Y): (bottomCenterIdx, bottomRing + i, bottomRing + nextI)
            mesh->faceVertIdx[fIdx][0] = bottomCenterIdx;
            mesh->faceVertIdx[fIdx][1] = bottomRing + i;
            mesh->faceVertIdx[fIdx][2] = bottomRing + nextI;
            fIdx++;
        }
    }

    mesh->faceCount = fIdx;
}

static void EarthSpike_DrawMesh(const EarthSpikeMesh *mesh)
{
    rlColor4ub(255, 255, 255, 255);
    rlCheckRenderBatchLimit(mesh->faceCount * 3);
    rlBegin(RL_TRIANGLES);
    for (int f = 0; f < mesh->faceCount; f++) {
        for (int k = 0; k < 3; k++) {
            int idx = mesh->faceVertIdx[f][k];
            Vector3 v = mesh->verts[idx];
            Vector3 n = mesh->normals[idx];
            Vector2 uv = mesh->uvs[idx];
            rlNormal3f(n.x, n.y, n.z);
            rlTexCoord2f(uv.x, uv.y);
            rlVertex3f(v.x, v.y, v.z);
        }
    }
    rlEnd();
}

// ── Formation & Sub-Spike Layout Initialization ─────────────────────────────

static void EarthSpike_InitSubSpikes(VC_EarthSpikeInstance *inst)
{
    const VFX_EarthSpikeConfig *cfg = &inst->config;
    int count = Clamp(cfg->spikeCount, 1, VC_EARTH_SPIKE_MAX_SUB);
    inst->subSpikeCount = count;

    unsigned int rng = (unsigned int)(inst->id * 7919 + 1337);

    Vector3 fwd = Vector3Subtract(cfg->target, cfg->source);
    float distXZ = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
    Vector3 fwdNorm = (distXZ > 0.01f) ? (Vector3){fwd.x / distXZ, 0.0f, fwd.z / distXZ} : (Vector3){0, 0, 1};

    for (int i = 0; i < count; i++) {
        EarthSubSpike *sub = &inst->subSpikes[i];
        memset(sub, 0, sizeof(EarthSubSpike));
        sub->active = true;
        sub->eruptTriggered = false;
        sub->shattered = false;
        sub->timer = 0.0f;
        sub->currentRise = 0.0f;

        float hMod = 0.88f + 0.24f * EarthSpike_RandomFloat(&rng);
        float rMod = 0.90f + 0.20f * EarthSpike_RandomFloat(&rng);
        sub->height = cfg->height * hMod;
        sub->baseRadius = cfg->baseRadius * rMod;
        sub->topRadius = sub->baseRadius * cfg->topRadiusRatio;

        switch (cfg->formation)
        {
        case VFX_EARTH_SPIKE_FORMATION_SINGLE:
        {
            sub->baseOffset = Vector3Zero();
            sub->tiltDir = Vector3Zero();
            sub->tiltAngle = 0.0f;
            sub->spawnDelay = 0.0f;
            break;
        }
        case VFX_EARTH_SPIKE_FORMATION_CLUSTER:
        {
            if (i == 0) {
                // Central tallest primary spire
                sub->baseOffset = Vector3Zero();
                sub->height = cfg->height * 1.15f;
                sub->tiltDir = Vector3Zero();
                sub->tiltAngle = 0.0f;
                sub->spawnDelay = 0.0f;
            } else {
                // Surrounding jagged teeth/spikes thrusting outward at an angle
                float goldenAngle = 2.39996323f; // ~137.5 degrees
                float angle = (float)i * goldenAngle;
                float rDist = cfg->formationRadius * (0.35f + 0.65f * sqrtf((float)i / (float)count));
                sub->baseOffset = (Vector3){cosf(angle) * rDist, 0.0f, sinf(angle) * rDist};
                sub->tiltDir = Vector3Normalize(sub->baseOffset);
                float maxTiltRad = cfg->tiltMaxDeg * DEG2RAD;
                sub->tiltAngle = maxTiltRad * (0.55f + 0.45f * EarthSpike_RandomFloat(&rng));
                sub->spawnDelay = 0.02f * (float)i;
            }
            break;
        }
        case VFX_EARTH_SPIKE_FORMATION_RING:
        {
            // Circular barrier of monoliths/pillars surrounding target
            float angle = ((float)i / (float)count) * 2.0f * PI;
            sub->baseOffset = (Vector3){cosf(angle) * cfg->formationRadius, 0.0f, sinf(angle) * cfg->formationRadius};
            // Slight inward pitch (4 degrees) creating an earthen prison cage
            sub->tiltDir = Vector3Normalize((Vector3){-sub->baseOffset.x, 0.0f, -sub->baseOffset.z});
            sub->tiltAngle = cfg->tiltMaxDeg * DEG2RAD * 0.4f;
            sub->spawnDelay = (cfg->staggerDelay > 0.0f) ? ((float)i * cfg->staggerDelay) : 0.0f;
            break;
        }
        case VFX_EARTH_SPIKE_FORMATION_LINE:
        {
            // Sequential wave surging from source to target
            float tLine = (count > 1) ? ((float)i / (float)(count - 1)) : 1.0f;
            Vector3 pt = Vector3Lerp(cfg->source, cfg->target, tLine);
            sub->baseOffset = Vector3Subtract(pt, cfg->target);
            // Forward piercing slant
            sub->tiltDir = fwdNorm;
            sub->tiltAngle = cfg->tiltMaxDeg * DEG2RAD;
            sub->spawnDelay = (float)i * fmaxf(cfg->staggerDelay, 0.04f);
            break;
        }
        default:
            break;
        }

        // Anchor footprint position to current map elevation
        sub->groundPos.x = cfg->target.x + sub->baseOffset.x;
        sub->groundPos.z = cfg->target.z + sub->baseOffset.z;
        sub->groundPos.y = MapManager_GetGroundHeightAt(sub->groundPos.x, sub->groundPos.z);

        // Precompute procedural rock mesh with deterministic seed
        int spikeSeed = (int)(rng ^ ((unsigned int)(sub->groundPos.x * 37.0f + sub->groundPos.z * 59.0f)));
        EarthSpike_BuildMesh(&sub->mesh, cfg, sub->height, sub->baseRadius, sub->topRadius, spikeSeed);
    }
}

// ── Default Configurations & Presets ─────────────────────────────────────────

VFX_EarthSpikeConfig VFX_EarthSpike_DefaultConfig(void)
{
    VFX_EarthSpikeConfig cfg;
    memset(&cfg, 0, sizeof(VFX_EarthSpikeConfig));
    cfg.preset = VFX_EARTH_SPIKE_PRESET_STONE_PILLAR;
    VFX_EarthSpike_ApplyPreset(&cfg, cfg.preset);
    return cfg;
}

VFX_EarthSpikeConfig *VFX_EarthSpike_GetLiveConfig(void)
{
    if (!s_earthSpikeInit) {
        s_liveEarthSpikeConfig = VFX_EarthSpike_DefaultConfig();
        const char *pEnv = getenv("WUXING_STONE_PILLAR_PRESET");
        if (pEnv && pEnv[0] != '\0') {
            int pVal = atoi(pEnv);
            VFX_EarthSpike_ApplyPreset(&s_liveEarthSpikeConfig, (VFX_EarthSpikePreset)pVal);
        }
        const char *sEnv = getenv("WUXING_STONE_PILLAR_STYLE");
        if (sEnv && sEnv[0] != '\0') {
            int sVal = atoi(sEnv);
            VFX_EarthSpike_ApplyStyle(&s_liveEarthSpikeConfig, (VFX_EarthSpikeStyle)sVal);
        }
        s_earthSpikeInit = true;
    }
    return &s_liveEarthSpikeConfig;
}

void VFX_EarthSpike_ApplyStyle(VFX_EarthSpikeConfig *cfg, VFX_EarthSpikeStyle style)
{
    if (!cfg) return;
    cfg->style = style;
    switch (style)
    {
    case VFX_EARTH_SPIKE_STYLE_GRANITE:
        // Mountain Granite Stone: slate-grey with natural quartz/feldspar strata
        cfg->baseColor = (Color){142, 138, 132, 255};
        cfg->accentColor = (Color){195, 188, 178, 255};
        cfg->ambientGain = 0.38f;
        cfg->specularGain = 0.50f;
        cfg->emissionIntensity = 0.0f;
        cfg->jaggedness = 0.16f;
        break;

    case VFX_EARTH_SPIKE_STYLE_SOIL:
        // Earthen Dirt / Packed Clay: rich warm terracotta earth matching dirt trails
        cfg->baseColor = (Color){150, 102, 62, 255};
        cfg->accentColor = (Color){195, 142, 92, 255};
        cfg->ambientGain = 0.44f;
        cfg->specularGain = 0.15f;
        cfg->emissionIntensity = 0.0f;
        cfg->jaggedness = 0.28f;
        break;

    case VFX_EARTH_SPIKE_STYLE_BASALT:
        // Columnar Basalt: dark volcanic cooling joints (Ghềnh Đá Đĩa)
        cfg->baseColor = (Color){54, 58, 66, 255};
        cfg->accentColor = (Color){100, 110, 125, 255};
        cfg->ambientGain = 0.28f;
        cfg->specularGain = 0.75f;
        cfg->emissionIntensity = 0.0f;
        cfg->jaggedness = 0.04f;
        break;

    case VFX_EARTH_SPIKE_STYLE_MAGMA:
        // Volcanic Magma: scorched obsidian crust with blazing molten seams
        cfg->baseColor = (Color){38, 34, 32, 255};
        cfg->accentColor = (Color){255, 140, 25, 255};
        cfg->ambientGain = 0.35f;
        cfg->specularGain = 0.85f;
        cfg->emissionIntensity = 2.2f;
        cfg->jaggedness = 0.20f;
        break;

    case VFX_EARTH_SPIKE_STYLE_JADE:
        // Jade Crystal: translucent emerald jade with glowing cyan sheen
        cfg->baseColor = (Color){28, 150, 90, 255};
        cfg->accentColor = (Color){120, 255, 180, 255};
        cfg->ambientGain = 0.40f;
        cfg->specularGain = 1.80f;
        cfg->emissionIntensity = 0.85f;
        cfg->jaggedness = 0.04f;
        break;

    default:
        break;
    }
}

void VFX_EarthSpike_KillAll(void)
{
    for (int i = 0; i < VC_EARTH_SPIKE_MAX_INSTANCES; i++) {
        s_earthSpikeInstances[i].active = false;
    }
}

void VFX_EarthSpike_ApplyPreset(VFX_EarthSpikeConfig *cfg, VFX_EarthSpikePreset preset)
{
    if (!cfg) return;
    cfg->preset = preset;

    // Common baseline settings
    cfg->source = (Vector3){6.0f, 0.0f, 4.4f};
    cfg->target = (Vector3){8.5f, 0.0f, 5.8f};
    cfg->heightSegments = 4;
    cfg->ambientGain = 0.40f;
    cfg->specularGain = 0.55f;
    cfg->emissionIntensity = 0.0f;
    cfg->emitDust = true;
    cfg->dustScale = 1.0f;
    cfg->emitDebris = true;
    cfg->debrisChips = 5;
    cfg->spawnGroundCrack = true;
    cfg->groundWaveBurst = true;
    cfg->screenShake = true;
    cfg->traumaAmount = 0.12f;
    cfg->windImpulse = true;
    cfg->windRadius = 3.2f;

    switch (preset)
    {
    case VFX_EARTH_SPIKE_PRESET_STONE_PILLAR:
    {
        cfg->shape = VFX_EARTH_SPIKE_SHAPE_PILLAR;
        cfg->style = VFX_EARTH_SPIKE_STYLE_GRANITE;
        cfg->formation = VFX_EARTH_SPIKE_FORMATION_SINGLE;
        cfg->exitMode = VFX_EARTH_SPIKE_EXIT_SINK;
        cfg->height = 2.8f;
        cfg->baseRadius = 0.42f;
        cfg->topRadiusRatio = 0.72f;
        cfg->facetCount = 8;
        cfg->tiltMaxDeg = 0.0f;
        cfg->spikeCount = 1;
        cfg->formationRadius = 0.0f;
        cfg->staggerDelay = 0.0f;
        cfg->eruptDuration = 0.16f;
        cfg->holdDuration = 2.8f;
        cfg->retractDuration = 0.40f;
        cfg->yOvershoot = 0.10f;
        VFX_EarthSpike_ApplyStyle(cfg, cfg->style);
        break;
    }
    case VFX_EARTH_SPIKE_PRESET_EARTH_SPIKES:
    {
        cfg->shape = VFX_EARTH_SPIKE_SHAPE_SPIKE;
        cfg->style = VFX_EARTH_SPIKE_STYLE_GRANITE;
        cfg->formation = VFX_EARTH_SPIKE_FORMATION_CLUSTER;
        cfg->exitMode = VFX_EARTH_SPIKE_EXIT_SHATTER;
        cfg->height = 2.4f;
        cfg->baseRadius = 0.35f;
        cfg->topRadiusRatio = 0.06f;
        cfg->facetCount = 5;
        cfg->tiltMaxDeg = 22.0f;
        cfg->spikeCount = 5;
        cfg->formationRadius = 1.6f;
        cfg->staggerDelay = 0.02f;
        cfg->eruptDuration = 0.13f;
        cfg->holdDuration = 2.0f;
        cfg->retractDuration = 0.30f;
        cfg->yOvershoot = 0.14f;
        VFX_EarthSpike_ApplyStyle(cfg, cfg->style);
        break;
    }
    case VFX_EARTH_SPIKE_PRESET_SPIKE_WAVE:
    {
        cfg->shape = VFX_EARTH_SPIKE_SHAPE_SPIKE;
        cfg->style = VFX_EARTH_SPIKE_STYLE_SOIL;
        cfg->formation = VFX_EARTH_SPIKE_FORMATION_LINE;
        cfg->exitMode = VFX_EARTH_SPIKE_EXIT_SHATTER;
        cfg->height = 2.2f;
        cfg->baseRadius = 0.32f;
        cfg->topRadiusRatio = 0.05f;
        cfg->facetCount = 6;
        cfg->tiltMaxDeg = 16.0f;
        cfg->spikeCount = 7;
        cfg->formationRadius = 4.5f;
        cfg->staggerDelay = 0.05f;
        cfg->eruptDuration = 0.12f;
        cfg->holdDuration = 1.6f;
        cfg->retractDuration = 0.25f;
        cfg->yOvershoot = 0.12f;
        VFX_EarthSpike_ApplyStyle(cfg, cfg->style);
        break;
    }
    case VFX_EARTH_SPIKE_PRESET_EARTH_PRISON:
    {
        cfg->shape = VFX_EARTH_SPIKE_SHAPE_PILLAR;
        cfg->style = VFX_EARTH_SPIKE_STYLE_BASALT;
        cfg->formation = VFX_EARTH_SPIKE_FORMATION_RING;
        cfg->exitMode = VFX_EARTH_SPIKE_EXIT_SINK;
        cfg->height = 3.5f;
        cfg->baseRadius = 0.48f;
        cfg->topRadiusRatio = 0.80f;
        cfg->facetCount = 6;
        cfg->tiltMaxDeg = 4.0f;
        cfg->spikeCount = 8;
        cfg->formationRadius = 3.2f;
        cfg->staggerDelay = 0.0f;
        cfg->eruptDuration = 0.20f;
        cfg->holdDuration = 3.6f;
        cfg->retractDuration = 0.45f;
        cfg->yOvershoot = 0.08f;
        VFX_EarthSpike_ApplyStyle(cfg, cfg->style);
        break;
    }
    case VFX_EARTH_SPIKE_PRESET_JADE_SPIRE:
    {
        cfg->shape = VFX_EARTH_SPIKE_SHAPE_OBELISK;
        cfg->style = VFX_EARTH_SPIKE_STYLE_JADE;
        cfg->formation = VFX_EARTH_SPIKE_FORMATION_CLUSTER;
        cfg->exitMode = VFX_EARTH_SPIKE_EXIT_SHATTER;
        cfg->height = 2.7f;
        cfg->baseRadius = 0.34f;
        cfg->topRadiusRatio = 0.22f;
        cfg->facetCount = 6;
        cfg->tiltMaxDeg = 16.0f;
        cfg->spikeCount = 5;
        cfg->formationRadius = 1.4f;
        cfg->staggerDelay = 0.02f;
        cfg->eruptDuration = 0.15f;
        cfg->holdDuration = 2.4f;
        cfg->retractDuration = 0.30f;
        cfg->yOvershoot = 0.09f;
        VFX_EarthSpike_ApplyStyle(cfg, cfg->style);
        break;
    }
    case VFX_EARTH_SPIKE_PRESET_MAGMA_CRAG:
    {
        cfg->shape = VFX_EARTH_SPIKE_SHAPE_SPIKE;
        cfg->style = VFX_EARTH_SPIKE_STYLE_MAGMA;
        cfg->formation = VFX_EARTH_SPIKE_FORMATION_CLUSTER;
        cfg->exitMode = VFX_EARTH_SPIKE_EXIT_SHATTER;
        cfg->height = 2.6f;
        cfg->baseRadius = 0.42f;
        cfg->topRadiusRatio = 0.10f;
        cfg->facetCount = 6;
        cfg->tiltMaxDeg = 18.0f;
        cfg->spikeCount = 6;
        cfg->formationRadius = 1.8f;
        cfg->staggerDelay = 0.02f;
        cfg->eruptDuration = 0.15f;
        cfg->holdDuration = 2.2f;
        cfg->retractDuration = 0.30f;
        cfg->yOvershoot = 0.15f;
        VFX_EarthSpike_ApplyStyle(cfg, cfg->style);
        break;
    }
    default:
        break;
    }
}

// ── Spawn & Lifecycle API ───────────────────────────────────────────────────

int VFX_ComposeEarthSpikeEx(Vector3 source, Vector3 target, const VFX_EarthSpikeConfig *cfg)
{
    if (!cfg) cfg = VFX_EarthSpike_GetLiveConfig();
    EarthFx_InitShared();

    VC_EarthSpikeInstance *inst = NULL;
    for (int i = 0; i < VC_EARTH_SPIKE_MAX_INSTANCES; i++) {
        if (!s_earthSpikeInstances[i].active) {
            inst = &s_earthSpikeInstances[i];
            break;
        }
    }
    if (!inst) {
        float oldestTime = -1.0f;
        for (int i = 0; i < VC_EARTH_SPIKE_MAX_INSTANCES; i++) {
            if (s_earthSpikeInstances[i].timer > oldestTime) {
                oldestTime = s_earthSpikeInstances[i].timer;
                inst = &s_earthSpikeInstances[i];
            }
        }
    }
    if (!inst) return 0;

    memset(inst, 0, sizeof(VC_EarthSpikeInstance));
    inst->active = true;
    inst->id = s_earthSpikeNextId++;
    inst->config = *cfg;
    inst->config.source = source;
    inst->config.target = target;
    inst->center = target;

    Vector3 fwd = Vector3Subtract(target, source);
    float dXZ = sqrtf(fwd.x * fwd.x + fwd.z * fwd.z);
    inst->forward = (dXZ > 0.01f) ? (Vector3){fwd.x / dXZ, 0.0f, fwd.z / dXZ} : (Vector3){0, 0, 1};

    float maxStagger = (cfg->formation == VFX_EARTH_SPIKE_FORMATION_LINE)
                           ? ((float)cfg->spikeCount * cfg->staggerDelay)
                           : 0.15f;
    inst->totalDuration = maxStagger + cfg->eruptDuration + cfg->holdDuration + cfg->retractDuration + 0.5f;

    EarthSpike_InitSubSpikes(inst);
    return inst->id;
}

int VFX_ComposeEarthSpike(const VFX_EarthSpikeConfig *cfg)
{
    if (!cfg) cfg = VFX_EarthSpike_GetLiveConfig();
    return VFX_ComposeEarthSpikeEx(cfg->source, cfg->target, cfg);
}

void VFX_KillEarthSpike(int handle)
{
    for (int i = 0; i < VC_EARTH_SPIKE_MAX_INSTANCES; i++) {
        if (s_earthSpikeInstances[i].active && s_earthSpikeInstances[i].id == handle) {
            s_earthSpikeInstances[i].active = false;
            break;
        }
    }
}

// ── Physical Simulation & Update ────────────────────────────────────────────

void VC_EarthSpike_Update(float dt)
{
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    for (int instIdx = 0; instIdx < VC_EARTH_SPIKE_MAX_INSTANCES; instIdx++) {
        VC_EarthSpikeInstance *inst = &s_earthSpikeInstances[instIdx];
        if (!inst->active) continue;

        inst->timer += dt;
        if (inst->timer >= inst->totalDuration) {
            inst->active = false;
            continue;
        }

        const VFX_EarthSpikeConfig *cfg = &inst->config;
        bool anySubActive = false;

        for (int s = 0; s < inst->subSpikeCount; s++) {
            EarthSubSpike *sub = &inst->subSpikes[s];
            if (!sub->active) continue;

            anySubActive = true;
            sub->timer += dt;
            float t = sub->timer - sub->spawnDelay;

            if (t < 0.0f) {
                // Waiting for sequential stagger turn
                sub->currentRise = 0.0f;
                continue;
            }

            // 1. Emergence Trigger (first frame rising above ground)
            if (!sub->eruptTriggered && t >= 0.0f) {
                sub->eruptTriggered = true;

                // Physical Earth Crack under the base
                if (cfg->spawnGroundCrack) {
                    VFX_EarthCrackConfig crackCfg = VFX_EarthCrack_DefaultConfig();
                    crackCfg.mode = VFX_EARTH_CRACK_MODE_RADIAL_POINT;
                    crackCfg.source = sub->groundPos;
                    crackCfg.length = sub->baseRadius * 2.5f + 0.35f;
                    crackCfg.mainWidth = sub->baseRadius * 0.20f;
                    if (crackCfg.mainWidth < 0.08f) crackCfg.mainWidth = 0.08f;
                    crackCfg.depth = 0.08f;
                    crackCfg.radialBranchCount = (sub->baseRadius > 0.45f) ? 5 : 4;
                    crackCfg.subBranchDensity = 2;
                    crackCfg.spreadDuration = fminf(cfg->eruptDuration * 0.85f, 0.18f);
                    crackCfg.holdDuration = cfg->holdDuration;
                    crackCfg.fadeDuration = fminf(cfg->retractDuration, 0.6f);
                    crackCfg.veinGlowIntensity = 0.0f; // Pure physical fracture
                    crackCfg.seed = (int)(EarthSpike_Hash((unsigned int)(sub->groundPos.x * 100.0f) ^
                                                          (unsigned int)(sub->groundPos.z * 100.0f) ^
                                                          (unsigned int)(s * 7919)));
                    VFX_ComposeEarthCrack(&crackCfg);
                }

                // Expanding Dust Ring & Clouds
                if (cfg->emitDust) {
                    VFX_ComposeGroundDustRing(sub->groundPos, VC_MAT_EARTH, sub->baseRadius * 1.8f * cfg->dustScale, 0.45f);
                    VFX_ComposeImpactDustVariant(sub->groundPos, VC_MAT_EARTH, sub->baseRadius * 1.6f * cfg->dustScale, 0.40f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
                }

                // Soil / Stone Pellets Thrown Upward
                if (cfg->emitDebris) {
                    Vector3 debrisVel = {
                        sub->tiltDir.x * 1.3f,
                        3.2f,
                        sub->tiltDir.z * 1.3f
                    };
                    VFX_ComposeDebrisShards(sub->groundPos, debrisVel, VC_MAT_EARTH, 0.05f, cfg->debrisChips);
                }

                // Radial Wind Impulse (parts vegetation around spike)
                if (cfg->windImpulse) {
                    Wind_SpawnRadialBlast(sub->groundPos, cfg->windRadius, 6.0f, 0.25f);
                }

                // Screen Shake Trauma
                if (cfg->screenShake) {
                    CameraFX_Shake(cfg->traumaAmount);
                }
            }

            // 2. Kinematics Evolution
            float tErupt = fmaxf(cfg->eruptDuration, 0.06f);
            float tHold = fmaxf(cfg->holdDuration, 0.1f);
            float tRetract = fmaxf(cfg->retractDuration, 0.08f);

            if (t < tErupt) {
                // Emergence phase: violent upward thrust with mechanical overshoot
                float p = t / tErupt;
                float rise;
                if (p < 0.70f) {
                    float u = p / 0.70f;
                    rise = (1.0f + cfg->yOvershoot) * sinf(u * (PI * 0.5f));
                } else {
                    float u = (p - 0.70f) / 0.30f;
                    rise = 1.0f + cfg->yOvershoot * (1.0f - sinf(u * (PI * 0.5f)));
                }
                sub->currentRise = Clamp(rise, 0.0f, 1.0f + cfg->yOvershoot);
            } else if (t < tErupt + tHold) {
                // Sustain phase: stands solid
                sub->currentRise = 1.0f;
            } else {
                // Exit phase: Shatter or Sink
                if (cfg->exitMode == VFX_EARTH_SPIKE_EXIT_SHATTER) {
                    if (!sub->shattered) {
                        sub->shattered = true;
                        sub->active = false;

                        // Shatter explosion into flying rock debris
                        Vector3 spikeCenter = sub->groundPos;
                        spikeCenter.y += sub->height * 0.45f;
                        VFX_ComposeDebrisShards(spikeCenter, (Vector3){0.0f, 3.2f, 0.0f}, VC_MAT_EARTH, 0.065f, 10);
                        VFX_ComposeImpactDustVariant(sub->groundPos, VC_MAT_EARTH, sub->baseRadius * 2.2f * cfg->dustScale, 0.50f, VFX_IMPACT_DUST_VARIANT_DUST_PUFF);
                        if (cfg->windImpulse) {
                            Wind_SpawnRadialBlast(sub->groundPos, sub->baseRadius * 2.6f, 4.0f, 0.20f);
                        }
                    }
                } else if (cfg->exitMode == VFX_EARTH_SPIKE_EXIT_SINK) {
                    float u = (t - tErupt - tHold) / tRetract;
                    if (u >= 1.0f) {
                        sub->active = false;
                        sub->currentRise = 0.0f;
                    } else {
                        sub->currentRise = 1.0f - (u * u);
                    }
                } else {
                    // PERSIST: stays up indefinitely
                    sub->currentRise = 1.0f;
                }
            }
        }

        if (!anySubActive && inst->config.exitMode != VFX_EARTH_SPIKE_EXIT_PERSIST) {
            inst->active = false;
        }
    }
}

// ── Rendering & Shadow Casting ──────────────────────────────────────────────

void VC_EarthSpike_DrawShadowPass(void)
{
    for (int instIdx = 0; instIdx < VC_EARTH_SPIKE_MAX_INSTANCES; instIdx++) {
        const VC_EarthSpikeInstance *inst = &s_earthSpikeInstances[instIdx];
        if (!inst->active) continue;

        for (int s = 0; s < inst->subSpikeCount; s++) {
            const EarthSubSpike *sub = &inst->subSpikes[s];
            if (!sub->active || sub->currentRise <= 0.01f || sub->mesh.faceCount <= 0) continue;

            float yPos = sub->groundPos.y + (sub->currentRise - 1.0f) * sub->height;
            rlPushMatrix();
            rlTranslatef(sub->groundPos.x, yPos, sub->groundPos.z);
            if (sub->tiltAngle > 0.001f && (fabsf(sub->tiltDir.x) > 0.001f || fabsf(sub->tiltDir.z) > 0.001f)) {
                Vector3 rotAxis = {-sub->tiltDir.z, 0.0f, sub->tiltDir.x};
                rlRotatef(sub->tiltAngle * RAD2DEG, rotAxis.x, rotAxis.y, rotAxis.z);
            }
            EarthSpike_DrawMesh(&sub->mesh);
            rlPopMatrix();
        }
    }
}

void VC_EarthSpike_Draw3D(Camera3D cam)
{
    (void)cam;
    EffectMaterial mat;
    Material_Get(&mat, MAT_ROCK);

    for (int instIdx = 0; instIdx < VC_EARTH_SPIKE_MAX_INSTANCES; instIdx++) {
        const VC_EarthSpikeInstance *inst = &s_earthSpikeInstances[instIdx];
        if (!inst->active) continue;
        const VFX_EarthSpikeConfig *cfg = &inst->config;

        mat.params.baseColor = cfg->baseColor;
        mat.params.rimStrength = cfg->specularGain;
        mat.params.fresnelPower = (cfg->style == VFX_EARTH_SPIKE_STYLE_JADE) ? 4.5f : 2.5f;
        mat.params.emissiveIntensity = cfg->emissionIntensity;
        mat.params.translucency = (cfg->style == VFX_EARTH_SPIKE_STYLE_JADE) ? 0.35f : 0.0f;

        if (cfg->emissionIntensity > 0.01f) {
            mat.vfxOutputEnabled = 1;
            mat.vfxOutput.emissionColor = cfg->accentColor;
            mat.vfxOutput.emissionIntensity = cfg->emissionIntensity;
            mat.vfxOutput.surface = VFX_SURFACE_ALPHA;
        } else {
            mat.vfxOutputEnabled = 0;
        }

        Material_Begin(mat);

        for (int s = 0; s < inst->subSpikeCount; s++) {
            const EarthSubSpike *sub = &inst->subSpikes[s];
            if (!sub->active || sub->currentRise <= 0.005f || sub->mesh.faceCount <= 0) continue;

            float yPos = sub->groundPos.y + (sub->currentRise - 1.0f) * sub->height;
            rlPushMatrix();
            rlTranslatef(sub->groundPos.x, yPos, sub->groundPos.z);
            if (sub->tiltAngle > 0.001f && (fabsf(sub->tiltDir.x) > 0.001f || fabsf(sub->tiltDir.z) > 0.001f)) {
                Vector3 rotAxis = {-sub->tiltDir.z, 0.0f, sub->tiltDir.x};
                rlRotatef(sub->tiltAngle * RAD2DEG, rotAxis.x, rotAxis.y, rotAxis.z);
            }
            EarthSpike_DrawMesh(&sub->mesh);
            rlPopMatrix();
        }

        Material_End();
    }
}

// ── Universal VFX Parameter System (Sandbox Inspector Integration) ──────────

int VFX_EarthSpike_GetParams(VFX_EarthSpikeConfig *cfg, VFX_ParamDef *out, int max)
{
    if (!cfg || !out || max <= 0) return 0;
    int n = 0;

    static const char *presetNames[] = {
        "Stone Pillar", "Earth Spikes", "Spike Wave",
        "Earth Prison", "Jade Spire", "Magma Crag"
    };
    static const char *shapeNames[] = {
        "Pillar", "Spike", "Obelisk", "Slab"
    };
    static const char *styleNames[] = {
        "Granite", "Earth Soil", "Basalt", "Magma", "Jade Crystal"
    };
    static const char *formationNames[] = {
        "Single", "Cluster", "Ring Barrier", "Linear Wave"
    };
    static const char *exitNames[] = {
        "Shatter Crumble", "Sink Underground", "Persist"
    };

#define SPIKE_PARAM_ENUM(pname, pgroup, val, names, count) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_ENUM, \
        .valPtr = (void*)&(val), .minInt = 0, .maxInt = (count) - 1, \
        .enumNames = names, .enumCount = count, .stepFloat = 1.0f \
    }; } while(0)

#define SPIKE_PARAM_FLOAT(pname, pgroup, val, vmin, vmax, vstep) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_FLOAT, \
        .valPtr = (void*)&(val), .minFloat = vmin, .maxFloat = vmax, \
        .stepFloat = vstep \
    }; } while(0)

#define SPIKE_PARAM_INT(pname, pgroup, val, vmin, vmax) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_INT, \
        .valPtr = (void*)&(val), .minInt = vmin, .maxInt = vmax, \
        .stepFloat = 1.0f \
    }; } while(0)

#define SPIKE_PARAM_BOOL(pname, pgroup, val) \
    do { if (n < max) out[n++] = (VFX_ParamDef){ \
        .name = pname, .group = pgroup, .type = VFX_PARAM_BOOL, \
        .valPtr = (void*)&(val) \
    }; } while(0)

    // Architecture & Overview
    SPIKE_PARAM_ENUM("Preset", "Overview", cfg->preset, presetNames, VFX_EARTH_SPIKE_PRESET_COUNT);
    SPIKE_PARAM_ENUM("Shape", "Overview", cfg->shape, shapeNames, VFX_EARTH_SPIKE_SHAPE_COUNT);
    SPIKE_PARAM_ENUM("Style", "Overview", cfg->style, styleNames, VFX_EARTH_SPIKE_STYLE_COUNT);
    SPIKE_PARAM_ENUM("Formation", "Overview", cfg->formation, formationNames, VFX_EARTH_SPIKE_FORMATION_COUNT);
    SPIKE_PARAM_ENUM("Exit Mode", "Overview", cfg->exitMode, exitNames, VFX_EARTH_SPIKE_EXIT_COUNT);

    // Dimensions
    SPIKE_PARAM_FLOAT("Height m", "Dimensions", cfg->height, 0.5f, 7.0f, 0.1f);
    SPIKE_PARAM_FLOAT("Base Radius m", "Dimensions", cfg->baseRadius, 0.10f, 2.0f, 0.05f);
    SPIKE_PARAM_FLOAT("Top Radius Ratio", "Dimensions", cfg->topRadiusRatio, 0.0f, 1.0f, 0.05f);
    SPIKE_PARAM_INT("Facet Count", "Dimensions", cfg->facetCount, 4, 10);
    SPIKE_PARAM_FLOAT("Jaggedness", "Dimensions", cfg->jaggedness, 0.0f, 0.50f, 0.02f);
    SPIKE_PARAM_FLOAT("Tilt Max Deg", "Dimensions", cfg->tiltMaxDeg, 0.0f, 35.0f, 1.0f);

    // Formation
    SPIKE_PARAM_INT("Spike Count", "Formation", cfg->spikeCount, 1, VC_EARTH_SPIKE_MAX_SUB);
    SPIKE_PARAM_FLOAT("Formation Rad m", "Formation", cfg->formationRadius, 0.5f, 6.0f, 0.2f);
    SPIKE_PARAM_FLOAT("Stagger Delay s", "Formation", cfg->staggerDelay, 0.0f, 0.25f, 0.01f);

    // Lifecycle
    SPIKE_PARAM_FLOAT("Erupt Duration s", "Lifecycle", cfg->eruptDuration, 0.06f, 0.60f, 0.02f);
    SPIKE_PARAM_FLOAT("Hold Duration s", "Lifecycle", cfg->holdDuration, 0.5f, 8.0f, 0.2f);
    SPIKE_PARAM_FLOAT("Retract Duration s", "Lifecycle", cfg->retractDuration, 0.15f, 1.0f, 0.05f);
    SPIKE_PARAM_FLOAT("Y Overshoot m", "Lifecycle", cfg->yOvershoot, 0.0f, 0.35f, 0.02f);

    // Optics & Materials
    SPIKE_PARAM_FLOAT("Ambient Gain", "Optics", cfg->ambientGain, 0.1f, 1.0f, 0.05f);
    SPIKE_PARAM_FLOAT("Specular Gain", "Optics", cfg->specularGain, 0.0f, 2.5f, 0.1f);
    SPIKE_PARAM_FLOAT("Emission", "Optics", cfg->emissionIntensity, 0.0f, 4.0f, 0.2f);

    // Particulates & Environment
    SPIKE_PARAM_BOOL("Emit Dust", "Particulates", cfg->emitDust);
    SPIKE_PARAM_FLOAT("Dust Scale", "Particulates", cfg->dustScale, 0.2f, 3.0f, 0.1f);
    SPIKE_PARAM_BOOL("Emit Debris", "Particulates", cfg->emitDebris);
    SPIKE_PARAM_INT("Debris Chips", "Particulates", cfg->debrisChips, 0, 12);
    SPIKE_PARAM_BOOL("Ground Crack", "Particulates", cfg->spawnGroundCrack);
    SPIKE_PARAM_BOOL("Wave Burst", "Particulates", cfg->groundWaveBurst);
    SPIKE_PARAM_BOOL("Screen Shake", "Particulates", cfg->screenShake);
    SPIKE_PARAM_FLOAT("Trauma", "Particulates", cfg->traumaAmount, 0.0f, 0.35f, 0.02f);
    SPIKE_PARAM_BOOL("Wind Impulse", "Particulates", cfg->windImpulse);

#undef SPIKE_PARAM_ENUM
#undef SPIKE_PARAM_FLOAT
#undef SPIKE_PARAM_INT
#undef SPIKE_PARAM_BOOL

    return n;
}

// ── Preset String Helpers ───────────────────────────────────────────────────

const char *VFX_EarthSpikePreset_Name(VFX_EarthSpikePreset preset)
{
    switch (preset) {
    case VFX_EARTH_SPIKE_PRESET_STONE_PILLAR: return "Stone Pillar";
    case VFX_EARTH_SPIKE_PRESET_EARTH_SPIKES: return "Earth Spikes";
    case VFX_EARTH_SPIKE_PRESET_SPIKE_WAVE:   return "Spike Wave";
    case VFX_EARTH_SPIKE_PRESET_EARTH_PRISON: return "Earth Prison";
    case VFX_EARTH_SPIKE_PRESET_JADE_SPIRE:   return "Jade Spire";
    case VFX_EARTH_SPIKE_PRESET_MAGMA_CRAG:   return "Magma Crag";
    default: return "Unknown";
    }
}

const char *VFX_EarthSpikeShape_Name(VFX_EarthSpikeShape shape)
{
    switch (shape) {
    case VFX_EARTH_SPIKE_SHAPE_PILLAR:  return "Pillar";
    case VFX_EARTH_SPIKE_SHAPE_SPIKE:   return "Spike";
    case VFX_EARTH_SPIKE_SHAPE_OBELISK: return "Obelisk";
    case VFX_EARTH_SPIKE_SHAPE_SLAB:    return "Slab";
    default: return "Unknown";
    }
}

const char *VFX_EarthSpikeStyle_Name(VFX_EarthSpikeStyle style)
{
    switch (style) {
    case VFX_EARTH_SPIKE_STYLE_GRANITE: return "Granite";
    case VFX_EARTH_SPIKE_STYLE_SOIL:    return "Earth Soil";
    case VFX_EARTH_SPIKE_STYLE_BASALT:  return "Basalt";
    case VFX_EARTH_SPIKE_STYLE_MAGMA:   return "Magma";
    case VFX_EARTH_SPIKE_STYLE_JADE:    return "Jade Crystal";
    default: return "Unknown";
    }
}

const char *VFX_EarthSpikeFormation_Name(VFX_EarthSpikeFormation formation)
{
    switch (formation) {
    case VFX_EARTH_SPIKE_FORMATION_SINGLE:  return "Single";
    case VFX_EARTH_SPIKE_FORMATION_CLUSTER: return "Cluster";
    case VFX_EARTH_SPIKE_FORMATION_RING:    return "Ring Barrier";
    case VFX_EARTH_SPIKE_FORMATION_LINE:    return "Linear Wave";
    default: return "Unknown";
    }
}

const char *VFX_EarthSpikeExit_Name(VFX_EarthSpikeExit exitMode)
{
    switch (exitMode) {
    case VFX_EARTH_SPIKE_EXIT_SHATTER: return "Shatter Crumble";
    case VFX_EARTH_SPIKE_EXIT_SINK:    return "Sink Underground";
    case VFX_EARTH_SPIKE_EXIT_PERSIST: return "Persist";
    default: return "Unknown";
    }
}

#endif // VC_EARTH_SPIKE_INL
