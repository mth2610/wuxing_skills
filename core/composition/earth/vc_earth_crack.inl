// ============================================================================
// vc_earth_crack.inl — True Terrain-Deforming Surface Crack System (Hệ Thổ)
//
// Dynamic Geological Ground Fracture & Deformation System:
// A true terrain-deforming ground crack system physically splitting the earth.
//
// 1. True Terrain Deformation (Biến đổi địa hình thực sự qua MapManager):
//    - Directly deforms the actual ground height via MapManager_SetGroundDisplacementHook.
//    - Any map surface (including the arena floor mesh in default_arena) queries
//      MapManager_GetGroundHeightAt and physically splits open:
//      * Deep chasm depression (ΔY < 0, dropping -0.3m to -0.8m).
//      * Uplifted fractured shoulders/horsts (ΔY > 0, heaving +0.1m to +0.2m).
//    - Characters, enemies, dummies, and props physically step down into the crevice.
//    - ZERO overlay decals, ZERO fake floating quads.
//
// 2. Pure Crack Focus (Theo yêu cầu):
//    - NO decals, NO dust particles, NO debris shards, NO camera shake.
//    - Purely the physical ground fracture tearing open and subterranean Qi glow.
//
// 3. Two Generalized Spreading Modes:
//    - Mode 0: Radial Point Spreading (Vết nứt lan tại 1 điểm):
//      Radial fault lines tear outward in 360° from an epicenter.
//    - Mode 1: Path Spreading (Vết nứt lan từ điểm A tới điểm B theo path C):
//      Continuous tectonic fault line tearing along user-defined path waypoints C.
// ============================================================================

#ifndef VC_EARTH_CRACK_INL
#define VC_EARTH_CRACK_INL

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <time.h>
#include "core/composition/visual_composer.h"
#include "core/composition/common/vc_params.h"
#include "core/vfx_render.h"
#include "maps/map_manager.h"
#include "core/wind/wind_system.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define VC_EARTH_CRACK_MAX_INSTANCES 32
#define VC_EARTH_CRACK_MAX_SEGMENTS  96

// ── 2D Fracture Segment ─────────────────────────────────────────────────────

typedef struct {
    Vector2 a;             // Start point (XZ)
    Vector2 b;             // End point (XZ)
    Vector2 dir;           // Normalized unit vector a->b
    Vector2 normal;        // Perpendicular unit vector (-dir.y, dir.x)
    Vector2 normalA;       // Joint-smoothed normal at start point A
    Vector2 normalB;       // Joint-smoothed normal at end point B
    float   length;        // Metric length of segment
    float   invLengthSq;   // 1.0f / (length * length) for division-free projection
    float   startArcDist;  // Arc length from crack origin to segment start
    float   halfWidthA;    // Local fissure half-width at start
    float   halfWidthB;    // Local fissure half-width at end
    float   depthA;        // Local fissure depth at start
    float   depthB;        // Local fissure depth at end
    float   maxRadius;     // Influence radius: fmaxf(halfWidthA, halfWidthB) * 1.35f
    float   minX, maxX;    // Tight 2D AABB
    float   minZ, maxZ;
} EarthCrackSegment;

typedef struct {
    bool                 active;
    int                  id;
    VFX_EarthCrackConfig config;
    float                timer;
    float                windTimer;
    float                progress;             // Normalized propagation [0..1]
    float                activeFrontDist;      // Current metric front reach (meters)
    float                totalLength;          // Total path or radius length (meters)

    float                minX, maxX;           // Global instance AABB
    float                minZ, maxZ;

    int                  segmentCount;
    EarthCrackSegment    segments[VC_EARTH_CRACK_MAX_SEGMENTS];
} VC_EarthCrackInstance;

static VC_EarthCrackInstance s_earthCrackInstances[VC_EARTH_CRACK_MAX_INSTANCES];
static VFX_EarthCrackConfig  s_liveEarthCrackConfig;
static bool                  s_earthCrackInit = false;
static int                   s_earthCrackNextId = 1;
static int                   s_activeEarthCrackCount = 0;

// ── Math & Easing Helpers ───────────────────────────────────────────────────

static inline float EarthCrack_EaseOutCubic(float t)
{
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

static inline unsigned int EarthCrack_Hash(unsigned int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static inline float EarthCrack_Noise2(int ix, int iz, int seed)
{
    unsigned int h = EarthCrack_Hash((unsigned int)(ix * 73856093) ^
                                     (unsigned int)(iz * 19349663) ^
                                     (unsigned int)(seed * 83492791));
    return ((float)(h & 0xFFFFu) / 65535.0f) * 2.0f - 1.0f;
}

// Distance from point P to line segment A->B
static inline float EarthCrack_DistToSegment(Vector2 p, Vector2 a, Vector2 b, float *outT)
{
    Vector2 ab = (Vector2){b.x - a.x, b.y - a.y};
    float lenSq = ab.x * ab.x + ab.y * ab.y;
    if (lenSq < 0.00001f) {
        if (outT) *outT = 0.0f;
        float dx = p.x - a.x;
        float dy = p.y - a.y;
        return sqrtf(dx * dx + dy * dy);
    }

    Vector2 ap = (Vector2){p.x - a.x, p.y - a.y};
    float t = (ap.x * ab.x + ap.y * ab.y) / lenSq;
    t = Clamp(t, 0.0f, 1.0f);
    if (outT) *outT = t;

    Vector2 closest = (Vector2){a.x + t * ab.x, a.y + t * ab.y};
    float dx = p.x - closest.x;
    float dy = p.y - closest.y;
    return sqrtf(dx * dx + dy * dy);
}

// ── Presets & Styles ────────────────────────────────────────────────────────

void VFX_EarthCrack_ApplyStyle(VFX_EarthCrackConfig *cfg, VFX_EarthCrackStyle style)
{
    if (!cfg) return;
    if (style < 0 || style >= VFX_EARTH_CRACK_STYLE_COUNT)
        style = VFX_EARTH_CRACK_STYLE_ANCIENT_EARTH;
    cfg->style = style;

    switch (style) {
        case VFX_EARTH_CRACK_STYLE_ANCIENT_EARTH:
            cfg->soilColor         = (Color){98, 88, 78, 255};   // Warm weathered earth rim
            cfg->rockColor         = (Color){48, 42, 38, 255};   // Broken slate/rock shelf
            cfg->glowColor         = (Color){255, 165, 40, 255}; // Subterranean golden amber Qi
            cfg->leadFrontColor    = (Color){255, 245, 190, 255}; // Searing gold core seam
            cfg->veinGlowIntensity = 0.0f; // Pure physical fracture: vein glow default is 0.0
            cfg->pulseSpeed        = 3.4f;
            break;

        case VFX_EARTH_CRACK_STYLE_MOLTEN_BASALT:
            cfg->soilColor         = (Color){88, 55, 45, 255};   // Charred volcanic rock rim
            cfg->rockColor         = (Color){36, 24, 22, 255};   // Basalt chasm wall
            cfg->glowColor         = (Color){255, 65, 10, 255};  // Molten magma / lava glow
            cfg->leadFrontColor    = (Color){255, 225, 120, 255}; // Incandescent lava core
            cfg->veinGlowIntensity = 3.2f;
            cfg->pulseSpeed        = 4.5f;
            break;

        case VFX_EARTH_CRACK_STYLE_DARK_ABYSS:
            cfg->soilColor         = (Color){60, 52, 44, 255};   // Weathered dark soil rim
            cfg->rockColor         = (Color){24, 20, 18, 255};   // Deep dark basalt chasm wall
            cfg->glowColor         = (Color){0, 0, 0, 0};        // PURE BLACK - NO GLOW (CHỈ MÀU ĐEN)
            cfg->leadFrontColor    = (Color){0, 0, 0, 0};        // PURE BLACK - NO GLOW
            cfg->veinGlowIntensity = 0.0f;                       // Pure black abyss void
            cfg->pulseSpeed        = 0.0f;
            break;

        case VFX_EARTH_CRACK_STYLE_JADE_QI:
            cfg->soilColor         = (Color){68, 88, 76, 255};   // Serpentine mineral soil
            cfg->rockColor         = (Color){30, 46, 36, 255};   // Dark jade shale rim
            cfg->glowColor         = (Color){50, 245, 155, 255}; // Radiant jade spirit Qi
            cfg->leadFrontColor    = (Color){210, 255, 240, 255}; // Pure emerald light seam
            cfg->veinGlowIntensity = 2.8f;
            cfg->pulseSpeed        = 3.0f;
            break;

        case VFX_EARTH_CRACK_STYLE_CELESTIAL_GOLD:
            cfg->soilColor         = (Color){108, 96, 80, 255};  // Weathered temple flagstone rim
            cfg->rockColor         = (Color){58, 50, 40, 255};   // Ancient bedrock shelf
            cfg->glowColor         = (Color){255, 215, 50, 255}; // Imperial celestial gold
            cfg->leadFrontColor    = (Color){255, 255, 215, 255}; // Solar radiant core
            cfg->veinGlowIntensity = 3.0f;
            cfg->pulseSpeed        = 3.6f;
            break;

        case VFX_EARTH_CRACK_STYLE_ABYSSAL_VOID:
            cfg->soilColor         = (Color){76, 64, 90, 255};   // Void-touched stone rim
            cfg->rockColor         = (Color){34, 26, 44, 255};   // Obsidian shelf
            cfg->glowColor         = (Color){185, 75, 255, 255}; // Cosmic rift violet
            cfg->leadFrontColor    = (Color){245, 205, 255, 255}; // Astral singularity core
            cfg->veinGlowIntensity = 2.7f;
            cfg->pulseSpeed        = 2.8f;
            break;

        default:
            break;
    }
}

void VFX_EarthCrack_ApplyPreset(VFX_EarthCrackConfig *cfg, VFX_EarthCrackPreset preset)
{
    if (!cfg) return;
    if (preset < 0 || preset >= VFX_EARTH_CRACK_PRESET_COUNT)
        preset = VFX_EARTH_CRACK_PRESET_RADIAL_BURST;
    cfg->preset = preset;

    cfg->jaggedness        = 0.75f;
    cfg->seed              = 1337;
    cfg->emitDust          = false; // Pure terrain deformation: no particles
    cfg->emitDebris        = false; // Pure terrain deformation: no shards
    cfg->emitDecal         = false; // Pure terrain deformation: no decals
    cfg->windShockwave     = false;
    cfg->cameraTrauma      = false; // Pure terrain deformation: no shake
    cfg->pathPointCount    = 0;

    switch (preset) {
        case VFX_EARTH_CRACK_PRESET_RADIAL_BURST:
            cfg->mode              = VFX_EARTH_CRACK_MODE_RADIAL_POINT;
            cfg->length            = 4.2f;
            cfg->mainWidth         = 0.16f;
            cfg->depth             = 0.10f;
            cfg->radialBranchCount = 5;
            cfg->subBranchDensity  = 3;
            cfg->spreadDuration    = 0.35f;
            cfg->holdDuration      = 3.0f;
            cfg->fadeDuration      = 1.5f;
            VFX_EarthCrack_ApplyStyle(cfg, VFX_EARTH_CRACK_STYLE_ANCIENT_EARTH);
            break;

        case VFX_EARTH_CRACK_PRESET_FISSURE_PATH:
            cfg->mode              = VFX_EARTH_CRACK_MODE_PATH_AB;
            cfg->length            = 7.5f;
            cfg->mainWidth         = 0.15f;
            cfg->depth             = 0.10f;
            cfg->radialBranchCount = 0;
            cfg->subBranchDensity  = 4;
            cfg->spreadDuration    = 0.45f;
            cfg->holdDuration      = 3.0f;
            cfg->fadeDuration      = 1.5f;
            VFX_EarthCrack_ApplyStyle(cfg, VFX_EARTH_CRACK_STYLE_ANCIENT_EARTH);
            break;

        case VFX_EARTH_CRACK_PRESET_SEISMIC_CHASM:
            cfg->mode              = VFX_EARTH_CRACK_MODE_PATH_AB;
            cfg->length            = 9.5f;
            cfg->mainWidth         = 0.22f;
            cfg->depth             = 0.14f;
            cfg->radialBranchCount = 0;
            cfg->subBranchDensity  = 4;
            cfg->spreadDuration    = 0.60f;
            cfg->holdDuration      = 3.8f;
            cfg->fadeDuration      = 1.6f;
            VFX_EarthCrack_ApplyStyle(cfg, VFX_EARTH_CRACK_STYLE_ANCIENT_EARTH);
            break;

        case VFX_EARTH_CRACK_PRESET_MOLTEN_VEINS:
            cfg->mode              = VFX_EARTH_CRACK_MODE_PATH_AB;
            cfg->length            = 7.0f;
            cfg->mainWidth         = 0.18f;
            cfg->depth             = 0.12f;
            cfg->radialBranchCount = 0;
            cfg->subBranchDensity  = 3;
            cfg->spreadDuration    = 0.45f;
            cfg->holdDuration      = 3.2f;
            cfg->fadeDuration      = 1.5f;
            VFX_EarthCrack_ApplyStyle(cfg, VFX_EARTH_CRACK_STYLE_MOLTEN_BASALT);
            break;

        case VFX_EARTH_CRACK_PRESET_DARK_FISSURE:
            cfg->mode              = VFX_EARTH_CRACK_MODE_RADIAL_POINT;
            cfg->length            = 4.5f;
            cfg->mainWidth         = 0.20f;
            cfg->depth             = 0.14f;
            cfg->radialBranchCount = 5;
            cfg->subBranchDensity  = 3;
            cfg->spreadDuration    = 0.35f;
            cfg->holdDuration      = 3.2f;
            cfg->fadeDuration      = 1.5f;
            VFX_EarthCrack_ApplyStyle(cfg, VFX_EARTH_CRACK_STYLE_DARK_ABYSS);
            break;

        case VFX_EARTH_CRACK_PRESET_JADE_FRACTURE:
            cfg->mode              = VFX_EARTH_CRACK_MODE_RADIAL_POINT;
            cfg->length            = 3.8f;
            cfg->mainWidth         = 0.14f;
            cfg->depth             = 0.08f;
            cfg->radialBranchCount = 5;
            cfg->subBranchDensity  = 2;
            cfg->spreadDuration    = 0.30f;
            cfg->holdDuration      = 3.0f;
            cfg->fadeDuration      = 1.2f;
            VFX_EarthCrack_ApplyStyle(cfg, VFX_EARTH_CRACK_STYLE_JADE_QI);
            break;

        case VFX_EARTH_CRACK_PRESET_CELESTIAL_GOLD:
            cfg->mode              = VFX_EARTH_CRACK_MODE_PATH_AB;
            cfg->length            = 7.5f;
            cfg->mainWidth         = 0.16f;
            cfg->depth             = 0.10f;
            cfg->radialBranchCount = 0;
            cfg->subBranchDensity  = 3;
            cfg->spreadDuration    = 0.45f;
            cfg->holdDuration      = 2.8f;
            cfg->fadeDuration      = 1.2f;
            VFX_EarthCrack_ApplyStyle(cfg, VFX_EARTH_CRACK_STYLE_CELESTIAL_GOLD);
            break;

        default:
            break;
    }
}

VFX_EarthCrackConfig VFX_EarthCrack_DefaultConfig(void)
{
    VFX_EarthCrackConfig cfg;
    memset(&cfg, 0, sizeof(VFX_EarthCrackConfig));
    VFX_EarthCrack_ApplyPreset(&cfg, VFX_EARTH_CRACK_PRESET_RADIAL_BURST);
    cfg.veinGlowIntensity = 0.0f;
    return cfg;
}

VFX_EarthCrackConfig *VFX_EarthCrack_GetLiveConfig(void)
{
    if (!s_earthCrackInit) {
        s_liveEarthCrackConfig = VFX_EarthCrack_DefaultConfig();
        s_earthCrackInit = true;
    }
    return &s_liveEarthCrackConfig;
}

const char *VFX_EarthCrackPreset_Name(VFX_EarthCrackPreset preset)
{
    static const char *names[] = {
        "RADIAL BURST", "FISSURE PATH", "SEISMIC CHASM",
        "MOLTEN VEINS", "DARK FISSURE", "JADE FRACTURE", "CELESTIAL GOLD"
    };
    return (preset >= 0 && preset < VFX_EARTH_CRACK_PRESET_COUNT) ? names[preset] : "UNKNOWN";
}

const char *VFX_EarthCrackStyle_Name(VFX_EarthCrackStyle style)
{
    static const char *names[] = {
        "ANCIENT EARTH", "MOLTEN BASALT", "DARK ABYSS", "JADE QI", "CELESTIAL GOLD", "ABYSSAL VOID"
    };
    return (style >= 0 && style < VFX_EARTH_CRACK_STYLE_COUNT) ? names[style] : "UNKNOWN";
}

const char *VFX_EarthCrackMode_Name(VFX_EarthCrackMode mode)
{
    static const char *names[] = {"RADIAL POINT", "PATH A->B"};
    return (mode >= 0 && mode < VFX_EARTH_CRACK_MODE_COUNT) ? names[mode] : "UNKNOWN";
}

int VFX_EarthCrack_GetParams(VFX_EarthCrackConfig *cfg, VFX_ParamDef *outParams, int maxParams)
{
    if (!cfg || !outParams || maxParams <= 0) return 0;
    int idx = 0;

    static const char *presetNames[] = {
        "RADIAL BURST", "FISSURE PATH", "SEISMIC CHASM",
        "MOLTEN VEINS", "DARK FISSURE", "JADE FRACTURE", "CELESTIAL GOLD"
    };
    static const char *styleNames[] = {
        "ANCIENT EARTH", "MOLTEN BASALT", "DARK ABYSS", "JADE QI", "CELESTIAL GOLD", "ABYSSAL VOID"
    };
    static const char *modeNames[] = {"RADIAL POINT", "PATH A->B"};

    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Preset", .group = "CRACK MASTER", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->preset, .minInt = 0, .maxInt = VFX_EARTH_CRACK_PRESET_COUNT - 1,
            .enumNames = presetNames, .enumCount = VFX_EARTH_CRACK_PRESET_COUNT
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Mode", .group = "CRACK MASTER", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->mode, .minInt = 0, .maxInt = VFX_EARTH_CRACK_MODE_COUNT - 1,
            .enumNames = modeNames, .enumCount = VFX_EARTH_CRACK_MODE_COUNT
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Style", .group = "CRACK MASTER", .type = VFX_PARAM_ENUM,
            .valPtr = &cfg->style, .minInt = 0, .maxInt = VFX_EARTH_CRACK_STYLE_COUNT - 1,
            .enumNames = styleNames, .enumCount = VFX_EARTH_CRACK_STYLE_COUNT
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Crack Width", .group = "GEOLOGY", .type = VFX_PARAM_FLOAT,
            .valPtr = &cfg->mainWidth, .minFloat = 0.08f, .maxFloat = 1.20f, .stepFloat = 0.02f
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Chasm Depth", .group = "GEOLOGY", .type = VFX_PARAM_FLOAT,
            .valPtr = &cfg->depth, .minFloat = 0.04f, .maxFloat = 0.60f, .stepFloat = 0.02f
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Jaggedness", .group = "GEOLOGY", .type = VFX_PARAM_FLOAT,
            .valPtr = &cfg->jaggedness, .minFloat = 0.0f, .maxFloat = 2.0f, .stepFloat = 0.10f
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Spread Time", .group = "TIMING", .type = VFX_PARAM_FLOAT,
            .valPtr = &cfg->spreadDuration, .minFloat = 0.2f, .maxFloat = 3.0f, .stepFloat = 0.05f
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Hold Time", .group = "TIMING", .type = VFX_PARAM_FLOAT,
            .valPtr = &cfg->holdDuration, .minFloat = 0.5f, .maxFloat = 8.0f, .stepFloat = 0.5f
        };
    }
    if (idx < maxParams) {
        outParams[idx++] = (VFX_ParamDef){
            .name = "Vein Glow", .group = "OPTICS", .type = VFX_PARAM_FLOAT,
            .valPtr = &cfg->veinGlowIntensity, .minFloat = 0.0f, .maxFloat = 5.0f, .stepFloat = 0.25f
        };
    }
    return idx;
}

// ── Physical Fracture Segment Generation ────────────────────────────────────

static inline void EarthCrack_PushSegment(VC_EarthCrackInstance *inst,
                                          Vector2 a, Vector2 b,
                                          float accumDist,
                                          float widthTaperA, float widthTaperB,
                                          float depthTaperA, float depthTaperB,
                                          const VFX_EarthCrackConfig *cfg)
{
    if (inst->segmentCount >= VC_EARTH_CRACK_MAX_SEGMENTS) return;
    EarthCrackSegment *seg = &inst->segments[inst->segmentCount++];
    seg->a = a;
    seg->b = b;
    Vector2 diff = (Vector2){b.x - a.x, b.y - a.y};
    seg->length = sqrtf(diff.x * diff.x + diff.y * diff.y);
    if (seg->length > 0.0001f) {
        seg->dir = (Vector2){diff.x / seg->length, diff.y / seg->length};
        seg->invLengthSq = 1.0f / (seg->length * seg->length);
    } else {
        seg->dir = (Vector2){1.0f, 0.0f};
        seg->invLengthSq = 0.0f;
    }
    seg->normal = (Vector2){-seg->dir.y, seg->dir.x};
    seg->normalA = seg->normal;
    seg->normalB = seg->normal;
    seg->startArcDist = accumDist;
    seg->halfWidthA = cfg->mainWidth * 0.5f * widthTaperA;
    if (seg->halfWidthA < 0.002f) seg->halfWidthA = 0.002f;
    seg->halfWidthB = cfg->mainWidth * 0.5f * widthTaperB;
    if (seg->halfWidthB < 0.002f) seg->halfWidthB = 0.002f;
    seg->depthA = cfg->depth * depthTaperA;
    if (seg->depthA < 0.004f) seg->depthA = 0.004f;
    seg->depthB = cfg->depth * depthTaperB;
    if (seg->depthB < 0.004f) seg->depthB = 0.004f;
    seg->maxRadius = fmaxf(seg->halfWidthA, seg->halfWidthB) * 1.35f;

    // Segment AABB
    float r = seg->maxRadius;
    seg->minX = fminf(a.x, b.x) - r;
    seg->maxX = fmaxf(a.x, b.x) + r;
    seg->minZ = fminf(a.y, b.y) - r;
    seg->maxZ = fmaxf(a.y, b.y) + r;

    // Expand instance AABB
    if (seg->minX < inst->minX) inst->minX = seg->minX;
    if (seg->maxX > inst->maxX) inst->maxX = seg->maxX;
    if (seg->minZ < inst->minZ) inst->minZ = seg->minZ;
    if (seg->maxZ > inst->maxZ) inst->maxZ = seg->maxZ;
}

static inline void EarthCrack_SmoothNormals(VC_EarthCrackInstance *inst, int startIdx, int count)
{
    if (count <= 1) return;
    for (int k = 0; k < count - 1; k++) {
        EarthCrackSegment *s0 = &inst->segments[startIdx + k];
        EarthCrackSegment *s1 = &inst->segments[startIdx + k + 1];
        Vector2 sum = (Vector2){s0->normal.x + s1->normal.x, s0->normal.y + s1->normal.y};
        float lenSq = sum.x * sum.x + sum.y * sum.y;
        if (lenSq > 0.0001f) {
            float inv = 1.0f / sqrtf(lenSq);
            Vector2 avg = (Vector2){sum.x * inv, sum.y * inv};
            s0->normalB = avg;
            s1->normalA = avg;
        }
    }
}

static void EarthCrack_BakeSegments(VC_EarthCrackInstance *inst)
{
    const VFX_EarthCrackConfig *cfg = &inst->config;
    inst->segmentCount = 0;
    int seed = cfg->seed;

    inst->minX = 1e9f;  inst->maxX = -1e9f;
    inst->minZ = 1e9f;  inst->maxZ = -1e9f;

    if (cfg->mode == VFX_EARTH_CRACK_MODE_RADIAL_POINT) {
        // ── MODE 0: AUTHENTIC GEOLOGICAL GROUND SHATTER (HỆ THỔ) ─────────────
        // Natural rock/earth fracture dynamics:
        // 1. Asymmetric radial fault lines emanating from epicenter (no center blob/crossbar).
        // 2. Continuous fracture propagation with Brownian momentum drift (no 90-deg staircases).
        // 3. Smooth forward-diverging bifurcations (30-42 deg acute Y-splits).
        // 4. Clean razor-sharp needle-taper tips.
        Vector2 center = (Vector2){cfg->source.x, cfg->source.z};
        inst->totalLength = cfg->length;

        int numFaults = cfg->radialBranchCount;
        if (numFaults < 3) numFaults = 3;
        if (numFaults > 6) numFaults = 6;

        float angleStep = (2.0f * PI) / (float)numFaults;

        for (int i = 0; i < numFaults && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS - 8; i++) {
            // Base radial heading with organic angular jitter
            float baseHeading = (float)i * angleStep +
                                EarthCrack_Noise2(i * 13, 101, seed) * (0.32f * angleStep) * cfg->jaggedness;

            // Highly varied fault lengths (geological asymmetry)
            float lenVar = 0.70f + 0.40f * (EarthCrack_Noise2(i * 17, 102, seed) * 0.5f + 0.5f);
            float totalLen = cfg->length * lenVar;

            int segs = 13 + (i % 4);
            float segLen = totalLen / (float)segs;

            // Start near center with small random offset (avoids overlapping crossbar square at origin)
            float rStart = 0.03f + 0.03f * (EarthCrack_Noise2(i, 103, seed) * 0.5f + 0.5f);
            Vector2 p0 = (Vector2){
                center.x + cosf(baseHeading) * rStart,
                center.y + sinf(baseHeading) * rStart
            };
            float accumDist = rStart;
            float currHeading = baseHeading;

            Vector2 branchPoint1 = p0;
            float branchArcDist1 = accumDist;
            float branchHeading1 = currHeading;
            int branchSegIdx1 = 3 + ((i + seed) % 3);

            Vector2 branchPoint2 = p0;
            float branchArcDist2 = accumDist;
            float branchHeading2 = currHeading;
            int branchSegIdx2 = 7 + ((i * 2 + seed) % 3);

            int branchStartIdx = inst->segmentCount;
            for (int s = 0; s < segs && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS; s++) {
                // Natural Brownian fracture drift: organic shear deflection across rock facets
                float noiseTurn = EarthCrack_Noise2(i * 47 + s, 104, seed) * 0.32f * cfg->jaggedness;
                currHeading += noiseTurn;

                // Gentle outward restoring pull to maintain overall radial propagation momentum
                Vector2 fromCenter = (Vector2){p0.x - center.x, p0.y - center.y};
                float distFromCenter = sqrtf(fromCenter.x * fromCenter.x + fromCenter.y * fromCenter.y);
                if (distFromCenter > 0.05f) {
                    float radHeading = atan2f(fromCenter.y, fromCenter.x);
                    float dHead = radHeading - currHeading;
                    while (dHead > PI) dHead -= 2.0f * PI;
                    while (dHead < -PI) dHead += 2.0f * PI;
                    currHeading += dHead * 0.12f;
                }

                // Slight segment length variation
                float localSegLen = segLen * (0.85f + 0.30f * (EarthCrack_Noise2(s, 108, seed) * 0.5f + 0.5f));
                Vector2 p1 = (Vector2){
                    p0.x + cosf(currHeading) * localSegLen,
                    p0.y + sinf(currHeading) * localSegLen
                };

                float u0 = (float)s / (float)segs;
                float u1 = (float)(s + 1) / (float)segs;
                // Sharp continuous taper from epicenter to razor hairline tip (zero joint step)
                float widthTaperA = powf(1.0f - u0, 1.4f);
                float widthTaperB = powf(1.0f - u1, 1.4f);
                float depthTaperA = powf(1.0f - u0, 1.3f);
                float depthTaperB = powf(1.0f - u1, 1.3f);

                EarthCrack_PushSegment(inst, p0, p1, accumDist,
                                       widthTaperA, widthTaperB, depthTaperA, depthTaperB, cfg);

                Vector2 diff = (Vector2){p1.x - p0.x, p1.y - p0.y};
                float dLen = sqrtf(diff.x * diff.x + diff.y * diff.y);

                if (s == branchSegIdx1) {
                    branchPoint1 = p1;
                    branchArcDist1 = accumDist + dLen;
                    branchHeading1 = currHeading;
                }
                if (s == branchSegIdx2) {
                    branchPoint2 = p1;
                    branchArcDist2 = accumDist + dLen;
                    branchHeading2 = currHeading;
                }

                p0 = p1;
                accumDist += dLen;
            }
            EarthCrack_SmoothNormals(inst, branchStartIdx, inst->segmentCount - branchStartIdx);

            // Geological Bifurcations (Splitting forward at acute 30-40 degree angles)
            if (cfg->subBranchDensity > 0 && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS - 8) {
                // First sub-branch
                float side1 = ((i + (seed >> 2)) % 2 == 0) ? 1.0f : -1.0f;
                float bHeading1 = branchHeading1 + side1 * (0.55f + 0.15f * EarthCrack_Noise2(i, 105, seed));
                int bSegs = 4;
                float bLen = (totalLen * 0.35f) / (float)bSegs;
                Vector2 bp0 = branchPoint1;
                float bAccum = branchArcDist1;
                float currBHeading = bHeading1;

                int subStartIdx1 = inst->segmentCount;
                for (int bs = 0; bs < bSegs && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS; bs++) {
                    float bTurn = EarthCrack_Noise2(i * 64 + bs, 106, seed) * 0.22f * cfg->jaggedness;
                    currBHeading += bTurn;
                    Vector2 bp1 = (Vector2){
                        bp0.x + cosf(currBHeading) * bLen,
                        bp0.y + sinf(currBHeading) * bLen
                    };

                    float bu0 = (float)bs / (float)bSegs;
                    float bu1 = (float)(bs + 1) / (float)bSegs;
                    float bWidthTaperA = powf(1.0f - bu0, 1.4f) * 0.45f;
                    float bWidthTaperB = powf(1.0f - bu1, 1.4f) * 0.45f;
                    float bDepthTaperA = powf(1.0f - bu0, 1.4f) * 0.45f;
                    float bDepthTaperB = powf(1.0f - bu1, 1.4f) * 0.45f;

                    EarthCrack_PushSegment(inst, bp0, bp1, bAccum,
                                           bWidthTaperA, bWidthTaperB, bDepthTaperA, bDepthTaperB, cfg);

                    Vector2 bDiff = (Vector2){bp1.x - bp0.x, bp1.y - bp0.y};
                    float bdLen = sqrtf(bDiff.x * bDiff.x + bDiff.y * bDiff.y);
                    bp0 = bp1;
                    bAccum += bdLen;
                }
                EarthCrack_SmoothNormals(inst, subStartIdx1, inst->segmentCount - subStartIdx1);

                // Second sub-branch (on opposite side if density >= 2)
                if (cfg->subBranchDensity >= 2 && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS - 4) {
                    float side2 = -side1;
                    float bHeading2 = branchHeading2 + side2 * (0.58f + 0.12f * EarthCrack_Noise2(i + 7, 107, seed));
                    int bSegs2 = 3;
                    float bLen2 = (totalLen * 0.25f) / (float)bSegs2;
                    Vector2 bp2 = branchPoint2;
                    float bAccum2 = branchArcDist2;
                    float currBHeading2 = bHeading2;

                    int subStartIdx2 = inst->segmentCount;
                    for (int bs = 0; bs < bSegs2 && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS; bs++) {
                        float bTurn = EarthCrack_Noise2(i * 64 + bs + 32, 108, seed) * 0.22f * cfg->jaggedness;
                        currBHeading2 += bTurn;
                        Vector2 bp3 = (Vector2){
                            bp2.x + cosf(currBHeading2) * bLen2,
                            bp2.y + sinf(currBHeading2) * bLen2
                        };

                        float bu0 = (float)bs / (float)bSegs2;
                        float bu1 = (float)(bs + 1) / (float)bSegs2;
                        float bWidthTaperA = powf(1.0f - bu0, 1.4f) * 0.35f;
                        float bWidthTaperB = powf(1.0f - bu1, 1.4f) * 0.35f;
                        float bDepthTaperA = powf(1.0f - bu0, 1.4f) * 0.35f;
                        float bDepthTaperB = powf(1.0f - bu1, 1.4f) * 0.35f;

                        EarthCrack_PushSegment(inst, bp2, bp3, bAccum2,
                                               bWidthTaperA, bWidthTaperB, bDepthTaperA, bDepthTaperB, cfg);

                        Vector2 bDiff2 = (Vector2){bp3.x - bp2.x, bp3.y - bp2.y};
                        float bdLen2 = sqrtf(bDiff2.x * bDiff2.x + bDiff2.y * bDiff2.y);
                        bp2 = bp3;
                        bAccum2 += bdLen2;
                    }
                    EarthCrack_SmoothNormals(inst, subStartIdx2, inst->segmentCount - subStartIdx2);
                }
            }
        }
    } else {
        // ── MODE 1: CRACK SPREADING ALONG PATH C (FROM A TO B) ──────────────
        // Tectonic fault tear with multi-octave stepped shear facets and
        // continuous en-echelon feather tension fractures.
        Vector2 waypoints[16];
        int numWaypoints = 0;

        if (cfg->pathPointCount >= 2) {
            numWaypoints = cfg->pathPointCount > 16 ? 16 : cfg->pathPointCount;
            for (int i = 0; i < numWaypoints; i++) {
                waypoints[i] = (Vector2){cfg->pathPoints[i].x, cfg->pathPoints[i].z};
            }
        } else {
            Vector2 start = (Vector2){cfg->source.x, cfg->source.z};
            Vector2 end   = (Vector2){cfg->target.x, cfg->target.z};
            float dist = sqrtf((end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y));
            if (dist < 0.2f) {
                end = (Vector2){start.x + cfg->length, start.y};
                dist = cfg->length;
            }
            numWaypoints = 2;
            waypoints[0] = start;
            waypoints[1] = end;
        }

        // Calculate total guide path length
        float pathGuideDist = 0.0f;
        for (int i = 1; i < numWaypoints; i++) {
            float dx = waypoints[i].x - waypoints[i - 1].x;
            float dy = waypoints[i].y - waypoints[i - 1].y;
            pathGuideDist += sqrtf(dx * dx + dy * dy);
        }
        if (pathGuideDist < 0.1f) pathGuideDist = 0.1f;

        // Discretize into fine steps of ~0.35m
        int totalTrunkSegs = (int)ceilf(pathGuideDist / 0.36f);
        if (totalTrunkSegs < 10) totalTrunkSegs = 10;
        if (totalTrunkSegs > 26) totalTrunkSegs = 26;

        // Generate main trunk segments with continuous tectonic cleavage
        Vector2 trunkPts[32];
        float trunkArcDist[32];
        trunkPts[0] = waypoints[0];
        trunkArcDist[0] = 0.0f;

        float accumTrunk = 0.0f;
        float currLatDrift = 0.0f;
        for (int s = 1; s <= totalTrunkSegs; s++) {
            float u = (float)s / (float)totalTrunkSegs;
            float targetDist = u * pathGuideDist;
            float walkDist = 0.0f;
            Vector2 interpPos = waypoints[numWaypoints - 1];
            Vector2 segFwd = (Vector2){1.0f, 0.0f};

            for (int w = 1; w < numWaypoints; w++) {
                float dx = waypoints[w].x - waypoints[w - 1].x;
                float dy = waypoints[w].y - waypoints[w - 1].y;
                float segLen = sqrtf(dx * dx + dy * dy);
                if (walkDist + segLen >= targetDist || w == numWaypoints - 1) {
                    float localT = (segLen > 0.001f) ? (targetDist - walkDist) / segLen : 0.0f;
                    localT = Clamp(localT, 0.0f, 1.0f);
                    interpPos = (Vector2){
                        waypoints[w - 1].x + dx * localT,
                        waypoints[w - 1].y + dy * localT
                    };
                    if (segLen > 0.001f) {
                        segFwd = (Vector2){dx / segLen, dy / segLen};
                    }
                    break;
                }
                walkDist += segLen;
            }

            // Continuous tectonic lateral displacement perpendicular to rupture axis
            Vector2 segRight = (Vector2){-segFwd.y, segFwd.x};
            float env = sinf(PI * u); // Smooth envelope: zero at endpoints A and B
            float dLat = EarthCrack_Noise2(s, 111, seed) * 0.16f * cfg->jaggedness * env;
            currLatDrift = currLatDrift * 0.55f + dLat;

            interpPos.x += segRight.x * currLatDrift;
            interpPos.y += segRight.y * currLatDrift;

            trunkPts[s] = interpPos;
            Vector2 dTrunk = (Vector2){interpPos.x - trunkPts[s - 1].x, interpPos.y - trunkPts[s - 1].y};
            accumTrunk += sqrtf(dTrunk.x * dTrunk.x + dTrunk.y * dTrunk.y);
            trunkArcDist[s] = accumTrunk;
        }

        inst->totalLength = accumTrunk;

        // Build trunk segments with needle-sharp tapers at both endpoints
        int trunkStartIdx = inst->segmentCount;
        for (int s = 0; s < totalTrunkSegs && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS; s++) {
            float u0 = (float)s / (float)totalTrunkSegs;
            float u1 = (float)(s + 1) / (float)totalTrunkSegs;
            float pathEnv0 = sinf(PI * Clamp(u0, 0.0f, 1.0f));
            float pathEnv1 = sinf(PI * Clamp(u1, 0.0f, 1.0f));
            float widthTaperA = sqrtf(pathEnv0) * (0.65f + 0.35f * (1.0f - 0.4f * u0));
            float widthTaperB = sqrtf(pathEnv1) * (0.65f + 0.35f * (1.0f - 0.4f * u1));
            float depthTaperA = sqrtf(pathEnv0) * (0.65f + 0.35f * (1.0f - 0.4f * u0));
            float depthTaperB = sqrtf(pathEnv1) * (0.65f + 0.35f * (1.0f - 0.4f * u1));

            EarthCrack_PushSegment(inst, trunkPts[s], trunkPts[s + 1], trunkArcDist[s],
                                   widthTaperA, widthTaperB, depthTaperA, depthTaperB, cfg);
        }
        EarthCrack_SmoothNormals(inst, trunkStartIdx, inst->segmentCount - trunkStartIdx);

        // Secondary lateral tension branches (en-echelon Riedel shears at 32-44 degrees)
        int numSplits = cfg->subBranchDensity;
        if (numSplits < 2) numSplits = 2;
        if (numSplits > 5) numSplits = 5;

        for (int b = 0; b < numSplits && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS - 4; b++) {
            int trunkIdx = 2 + (b * (totalTrunkSegs - 4)) / numSplits;
            if (trunkIdx >= totalTrunkSegs) trunkIdx = totalTrunkSegs - 1;

            Vector2 rootPos = trunkPts[trunkIdx];
            Vector2 trunkDir = inst->segments[trunkIdx].dir;

            float side = (b % 2 == 0) ? 1.0f : -1.0f;
            float branchAngle = side * (0.60f + 0.16f * EarthCrack_Noise2(b, 112, seed));
            Vector2 branchDir = (Vector2){
                trunkDir.x * cosf(branchAngle) - trunkDir.y * sinf(branchAngle),
                trunkDir.x * sinf(branchAngle) + trunkDir.y * cosf(branchAngle)
            };

            float branchTotalLen = (inst->totalLength * 0.22f) * (0.80f + 0.40f * (EarthCrack_Noise2(b, 113, seed) * 0.5f + 0.5f));
            int branchSegs = 4;
            float branchSegLen = branchTotalLen / (float)branchSegs;

            Vector2 bp0 = rootPos;
            float bAccumDist = trunkArcDist[trunkIdx];
            Vector2 currBDir = branchDir;

            int bStartIdx = inst->segmentCount;
            for (int bs = 0; bs < branchSegs && inst->segmentCount < VC_EARTH_CRACK_MAX_SEGMENTS; bs++) {
                float bTurn = EarthCrack_Noise2(b * 16 + bs, 114, seed) * 0.18f * cfg->jaggedness;
                currBDir = (Vector2){
                    currBDir.x * cosf(bTurn) - currBDir.y * sinf(bTurn),
                    currBDir.x * sinf(bTurn) + currBDir.y * cosf(bTurn)
                };

                Vector2 bp1 = (Vector2){
                    bp0.x + currBDir.x * branchSegLen,
                    bp0.y + currBDir.y * branchSegLen
                };

                float bu0 = (float)bs / (float)branchSegs;
                float bu1 = (float)(bs + 1) / (float)branchSegs;
                float bWidthTaperA = powf(1.0f - bu0, 1.4f) * 0.40f;
                float bWidthTaperB = powf(1.0f - bu1, 1.4f) * 0.40f;
                float bDepthTaperA = powf(1.0f - bu0, 1.4f) * 0.40f;
                float bDepthTaperB = powf(1.0f - bu1, 1.4f) * 0.40f;

                EarthCrack_PushSegment(inst, bp0, bp1, bAccumDist,
                                       bWidthTaperA, bWidthTaperB, bDepthTaperA, bDepthTaperB, cfg);

                bp0 = bp1;
                bAccumDist += branchSegLen;
            }
            EarthCrack_SmoothNormals(inst, bStartIdx, inst->segmentCount - bStartIdx);
        }
    }

    if (inst->segmentCount == 0) {
        inst->minX = inst->maxX = inst->minZ = inst->maxZ = 0.0f;
    }
}

// ── Physical Mathematical Ground Displacement Field ─────────────────────────

static float EarthCrack_CalcDisplacement(const VC_EarthCrackInstance *inst, float x, float z)
{
    // 1. Ultra-fast instance AABB culling
    if (x < inst->minX || x > inst->maxX || z < inst->minZ || z > inst->maxZ) {
        return 0.0f;
    }

    float currentFront = inst->activeFrontDist;
    if (currentFront <= 0.01f) return 0.0f;

    // Master lifecycle fade multiplier
    float spreadDur = fmaxf(inst->config.spreadDuration, 0.1f);
    float holdDur   = fmaxf(inst->config.holdDuration, 0.1f);
    float fadeDur   = fmaxf(inst->config.fadeDuration, 0.1f);
    float lifeScale = 1.0f;
    if (inst->timer > spreadDur + holdDur) {
        float fadeT = (inst->timer - (spreadDur + holdDur)) / fadeDur;
        lifeScale = Clamp(1.0f - fadeT, 0.0f, 1.0f);
    }
    if (lifeScale <= 0.001f) return 0.0f;

    float maxDepression = 0.0f;
    float maxUplift     = 0.0f;

    for (int i = 0; i < inst->segmentCount; i++) {
        const EarthCrackSegment *seg = &inst->segments[i];

        // 2. Fast check: segment active along propagating front
        if (seg->startArcDist > currentFront) continue;

        // 3. Fast segment AABB culling: 4 float comparisons reject 99% of segments
        if (x < seg->minX || x > seg->maxX || z < seg->minZ || z > seg->maxZ) continue;

        // 4. Point-to-segment distance using precomputed inverse length squared
        float dx = x - seg->a.x;
        float dz = z - seg->a.y;
        Vector2 ab = (Vector2){seg->b.x - seg->a.x, seg->b.y - seg->a.y};
        float t = (dx * ab.x + dz * ab.y) * seg->invLengthSq;
        t = Clamp(t, 0.0f, 1.0f);

        float arcAtPoint = seg->startArcDist + t * seg->length;
        if (arcAtPoint > currentFront) continue;

        float cx = seg->a.x + t * ab.x;
        float cz = seg->a.y + t * ab.y;
        float distSq = (x - cx) * (x - cx) + (z - cz) * (z - cz);
        float maxRSq = seg->maxRadius * seg->maxRadius;
        if (distSq >= maxRSq) continue;

        float d = sqrtf(distSq);

        // Front tapering at the needle tip
        float tipDist = currentFront - arcAtPoint;
        float tipTaper = Clamp(tipDist / 0.35f, 0.0f, 1.0f);

        float halfW = seg->halfWidthA + (seg->halfWidthB - seg->halfWidthA) * t;
        float depth = (seg->depthA + (seg->depthB - seg->depthA) * t) * lifeScale * tipTaper;

        // Profile across fissure:
        // Inside crack (d < halfW): sharp steep cleft depression
        if (d < halfW) {
            float q = d / halfW;
            float dep = depth * (1.0f - q * q);
            if (dep > maxDepression) maxDepression = dep;
        }
        // Uplifted shoulder (halfW <= d < maxRadius): slight tectonic lip heaving UP
        else {
            float qLip = (d - halfW) / (seg->maxRadius - halfW); // in [0, 1]
            float up = depth * 0.25f * sinf(PI * qLip);
            if (up > maxUplift) maxUplift = up;
        }
    }

    if (maxDepression > 0.0005f) {
        return -maxDepression;
    }
    return maxUplift;
}

// ── Global Ground Displacement Query (Queried by MapManager_GetGroundHeightAt) ──

float VC_EarthCrack_GetGroundDisplacementAt(float x, float z)
{
    float totalDY = 0.0f;
    for (int instIdx = 0; instIdx < VC_EARTH_CRACK_MAX_INSTANCES; instIdx++) {
        const VC_EarthCrackInstance *inst = &s_earthCrackInstances[instIdx];
        if (!inst->active) continue;
        if (x < inst->minX || x > inst->maxX || z < inst->minZ || z > inst->maxZ) continue;
        float dY = EarthCrack_CalcDisplacement(inst, x, z);
        if (fabsf(dY) > fabsf(totalDY)) totalDY = dY;
    }
    float swDY = VC_SeismicWave_GetGroundDisplacementAt(x, z);
    if (fabsf(swDY) > fabsf(totalDY)) totalDY = swDY;
    return totalDY;
}

// ── Primary Ground Wave Uniform Query ────────────────────────────────────────

bool VC_EarthCrack_GetPrimaryWaveData(Vector4 *outOriginDir, Vector4 *outWaveParams, Vector4 *outWaveProfile)
{
    // Do not activate waveActive on terrain shaders for earth cracks:
    // Terrain shaders use finite-difference normal shading over a wide radius (eps = 0.3m),
    // which turns localized sharp crack fissures into muddy, blurry 1.5m airbrushed smudges.
    // Sharp crack geometry is rendered crisply by VC_EarthCrack_Draw3D.
    // If a seismic wave is concurrently active, forward to it:
    return VC_SeismicWave_GetPrimaryWaveData(outOriginDir, outWaveParams, outWaveProfile);
}

// ── Public Trigger API ──────────────────────────────────────────────────────

int VFX_ComposeEarthCrack(const VFX_EarthCrackConfig *cfg)
{
    if (!cfg) return -1;

    // Register displacement and wave param hooks with MapManager
    MapManager_SetGroundDisplacementHook(VC_EarthCrack_GetGroundDisplacementAt);
    MapManager_SetGroundWaveParamsHook(VC_EarthCrack_GetPrimaryWaveData);

    int slot = -1;
    for (int i = 0; i < VC_EARTH_CRACK_MAX_INSTANCES; i++) {
        if (!s_earthCrackInstances[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        float maxTimer = -1.0f;
        for (int i = 0; i < VC_EARTH_CRACK_MAX_INSTANCES; i++) {
            if (s_earthCrackInstances[i].timer > maxTimer) {
                maxTimer = s_earthCrackInstances[i].timer;
                slot = i;
            }
        }
    }
    if (slot < 0) return -1;

    VC_EarthCrackInstance *inst = &s_earthCrackInstances[slot];
    memset(inst, 0, sizeof(VC_EarthCrackInstance));
    inst->active = true;
    inst->id = s_earthCrackNextId++;
    inst->config = *cfg;

    // Automatic seed diversification: If user passed 0 or default preset seed 1337,
    // generate a unique non-repeating seed so repeated casts at the same spot produce unique fractures!
    static unsigned int s_globalCrackSeedCounter = 0;
    if (s_globalCrackSeedCounter == 0) {
        s_globalCrackSeedCounter = ((unsigned int)time(NULL) ^ (unsigned int)clock() ^ 0x85ebca6bu);
    }
    s_globalCrackSeedCounter += 0x61c88647u; // Golden ratio Weyl sequence
    if (inst->config.seed <= 0 || inst->config.seed == 1337) {
        inst->config.seed = (int)(s_globalCrackSeedCounter ^ ((unsigned int)inst->id * 2654435761u));
    }

    inst->timer = 0.0f;
    inst->progress = 0.0f;
    inst->activeFrontDist = 0.0f;

    EarthCrack_BakeSegments(inst);
    s_activeEarthCrackCount++;

    return inst->id;
}

int VFX_ComposeEarthCrackRadial(Vector3 epicenter, float radius, const VFX_EarthCrackConfig *cfgOrNull)
{
    VFX_EarthCrackConfig cfg;
    if (cfgOrNull) cfg = *cfgOrNull;
    else cfg = VFX_EarthCrack_DefaultConfig();

    cfg.mode = VFX_EARTH_CRACK_MODE_RADIAL_POINT;
    cfg.source = epicenter;
    cfg.length = radius;
    return VFX_ComposeEarthCrack(&cfg);
}

int VFX_ComposeEarthCrackPath(const Vector3 *pathWaypoints, int count, float width, const VFX_EarthCrackConfig *cfgOrNull)
{
    if (!pathWaypoints || count < 2) return -1;
    VFX_EarthCrackConfig cfg;
    if (cfgOrNull) cfg = *cfgOrNull;
    else cfg = VFX_EarthCrack_DefaultConfig();

    cfg.mode = VFX_EARTH_CRACK_MODE_PATH_AB;
    if (width > 0.01f) cfg.mainWidth = width;
    cfg.pathPointCount = count > 16 ? 16 : count;
    for (int i = 0; i < cfg.pathPointCount; i++) cfg.pathPoints[i] = pathWaypoints[i];
    cfg.source = pathWaypoints[0];
    cfg.target = pathWaypoints[cfg.pathPointCount - 1];

    return VFX_ComposeEarthCrack(&cfg);
}

int VFX_ComposeEarthCrackAB(Vector3 start, Vector3 end, float width, const VFX_EarthCrackConfig *cfgOrNull)
{
    VFX_EarthCrackConfig cfg;
    if (cfgOrNull) cfg = *cfgOrNull;
    else cfg = VFX_EarthCrack_DefaultConfig();

    cfg.mode = VFX_EARTH_CRACK_MODE_PATH_AB;
    cfg.source = start;
    cfg.target = end;
    if (width > 0.01f) cfg.mainWidth = width;
    cfg.pathPointCount = 0;

    return VFX_ComposeEarthCrack(&cfg);
}

void VFX_KillEarthCrack(int handle)
{
    if (handle <= 0) return;
    for (int i = 0; i < VC_EARTH_CRACK_MAX_INSTANCES; i++) {
        if (s_earthCrackInstances[i].active && s_earthCrackInstances[i].id == handle) {
            s_earthCrackInstances[i].active = false;
            break;
        }
    }
}

void VFX_EarthCrack_KillAll(void)
{
    for (int i = 0; i < VC_EARTH_CRACK_MAX_INSTANCES; i++) {
        s_earthCrackInstances[i].active = false;
    }
    s_activeEarthCrackCount = 0;
}

// ── Per-Frame Lifecycle Update ──────────────────────────────────────────────

void VC_EarthCrack_Update(float dt)
{
    if (dt <= 0.0f) return;

    // Ensure MapManager hooks remain bound
    MapManager_SetGroundDisplacementHook(VC_EarthCrack_GetGroundDisplacementAt);
    MapManager_SetGroundWaveParamsHook(VC_EarthCrack_GetPrimaryWaveData);

    int activeCount = 0;
    for (int i = 0; i < VC_EARTH_CRACK_MAX_INSTANCES; i++) {
        VC_EarthCrackInstance *inst = &s_earthCrackInstances[i];
        if (!inst->active) continue;

        inst->timer += dt;
        float spreadDur = fmaxf(inst->config.spreadDuration, 0.1f);
        float holdDur   = fmaxf(inst->config.holdDuration, 0.1f);
        float fadeDur   = fmaxf(inst->config.fadeDuration, 0.1f);
        float totalLife = spreadDur + holdDur + fadeDur;

        if (inst->timer >= totalLife) {
            inst->active = false;
            continue;
        }

        activeCount++;
        float rawP = Clamp(inst->timer / spreadDur, 0.0f, 1.0f);
        inst->progress = EarthCrack_EaseOutCubic(rawP);
        inst->activeFrontDist = inst->totalLength * inst->progress;

        // Vegetation parting & aerodynamic ground shock along active fissure:
        if (inst->timer < spreadDur + holdDur) {
            inst->windTimer += dt;
            // 1. Advancing needle tips parting grass as rupture expands:
            if (rawP < 1.0f && inst->windTimer >= 0.04f) {
                float curFront = inst->activeFrontDist;
                for (int s = 0; s < inst->segmentCount; s++) {
                    const EarthCrackSegment *seg = &inst->segments[s];
                    if (seg->startArcDist <= curFront && seg->startArcDist + seg->length >= curFront) {
                        float reach = curFront - seg->startArcDist;
                        float segT = (seg->length > 0.001f) ? reach / seg->length : 0.0f;
                        segT = Clamp(segT, 0.0f, 1.0f);
                        float tipX = seg->a.x + (seg->b.x - seg->a.x) * segT;
                        float tipZ = seg->a.y + (seg->b.y - seg->a.y) * segT;
                        float tipY = MapManager_GetGroundHeightAt(tipX, tipZ);
                        Wind_SpawnRadialBlast((Vector3){tipX, tipY, tipZ}, seg->halfWidthA * 3.8f + 0.35f, 6.0f, 0.25f);
                    }
                }
            }
            // 2. Continuous vegetation parting along opened fissure during hold:
            if (inst->windTimer >= 0.10f) {
                inst->windTimer = 0.0f;
                int step = (inst->segmentCount > 16) ? 3 : 2;
                for (int s = 0; s < inst->segmentCount; s += step) {
                    const EarthCrackSegment *seg = &inst->segments[s];
                    if (seg->startArcDist > inst->activeFrontDist) continue;
                    float midX = (seg->a.x + seg->b.x) * 0.5f;
                    float midZ = (seg->a.y + seg->b.y) * 0.5f;
                    float midY = MapManager_GetGroundHeightAt(midX, midZ);
                    Wind_SpawnRadialBlast((Vector3){midX, midY, midZ}, seg->halfWidthA * 3.2f + 0.30f, 4.2f, 0.22f);
                }
            }
        }
    }
    s_activeEarthCrackCount = activeCount;
}

// ── Subterranean Energy Vein (Nestled Deep Inside the Parted Earth) ───────────

void VC_EarthCrack_Draw3D(Camera3D cam)
{
    (void)cam;
    if (s_activeEarthCrackCount <= 0) return;

    for (int instIdx = 0; instIdx < VC_EARTH_CRACK_MAX_INSTANCES; instIdx++) {
        const VC_EarthCrackInstance *inst = &s_earthCrackInstances[instIdx];
        if (!inst->active || inst->segmentCount <= 0) continue;

        float spreadDur = fmaxf(inst->config.spreadDuration, 0.1f);
        float holdDur   = fmaxf(inst->config.holdDuration, 0.1f);
        float fadeDur   = fmaxf(inst->config.fadeDuration, 0.1f);
        float totalLife = spreadDur + holdDur + fadeDur;
        if (inst->timer >= totalLife) continue;

        float lifeAlpha = 1.0f;
        if (inst->timer > spreadDur + holdDur) {
            float fadeT = (inst->timer - (spreadDur + holdDur)) / fadeDur;
            lifeAlpha = Clamp(1.0f - fadeT, 0.0f, 1.0f);
        }
        if (lifeAlpha <= 0.001f) continue;

        float currentFront = inst->activeFrontDist;
        if (currentFront <= 0.01f) continue;

        const VFX_EarthCrackConfig *cfg = &inst->config;

        Color colSoil  = cfg->soilColor;
        Color colRock  = cfg->rockColor;
        Color colAbyss = (Color){6, 5, 4, 255}; // Deep dark crevice abyss void
        Color colGlow  = cfg->glowColor;

        // ── PASS 1: Physical Rock Crevice & Fractured Ground Ribbons ─────────
        rlDrawRenderBatchActive();
        rlSetTexture(0);
        rlDisableBackfaceCulling();
        rlEnableDepthTest();
        rlDisableDepthMask();

        rlBegin(RL_QUADS);
        for (int i = 0; i < inst->segmentCount; i++) {
            const EarthCrackSegment *seg = &inst->segments[i];
            if (seg->startArcDist > currentFront) continue;

            float reach = currentFront - seg->startArcDist;
            float segP = (seg->length > 0.0001f) ? reach / seg->length : 1.0f;
            segP = Clamp(segP, 0.0f, 1.0f);
            if (segP <= 0.001f) continue;

            Vector2 pa = seg->a;
            Vector2 pb = (Vector2){
                seg->a.x + (seg->b.x - seg->a.x) * segP,
                seg->a.y + (seg->b.y - seg->a.y) * segP
            };

            float hwA = seg->halfWidthA;
            float tipTaper = (segP < 1.0f) ? segP : 1.0f;
            float hwB = (seg->halfWidthA + (seg->halfWidthB - seg->halfWidthA) * segP) * tipTaper;
            Vector2 normA = seg->normalA;
            Vector2 normB = seg->normalB;
            if (segP < 1.0f) {
                Vector2 nInterp = (Vector2){
                    seg->normalA.x + (seg->normalB.x - seg->normalA.x) * segP,
                    seg->normalA.y + (seg->normalB.y - seg->normalA.y) * segP
                };
                float nLen = sqrtf(nInterp.x * nInterp.x + nInterp.y * nInterp.y);
                if (nLen > 0.001f) normB = (Vector2){nInterp.x / nLen, nInterp.y / nLen};
            }

            float tipAlphaA = (hwA < 0.024f) ? Clamp(hwA / 0.024f, 0.0f, 1.0f) : 1.0f;
            float tipAlphaB = (hwB < 0.024f) ? Clamp(hwB / 0.024f, 0.0f, 1.0f) : 1.0f;
            if (segP < 1.0f) tipAlphaB *= segP;

            float depthA = seg->depthA * lifeAlpha * tipAlphaA;
            float depthB = (seg->depthA + (seg->depthB - seg->depthA) * segP) * lifeAlpha * tipAlphaB;
            float heaveA = depthA * 0.18f;
            float heaveB = depthB * 0.18f;

            // Lateral cross-section coordinates for A:
            float a0_x = pa.x - normA.x * (hwA * 1.35f);
            float a0_z = pa.y - normA.y * (hwA * 1.35f);
            float a1_x = pa.x - normA.x * (hwA * 0.70f);
            float a1_z = pa.y - normA.y * (hwA * 0.70f);
            float a2_x = pa.x - normA.x * (hwA * 0.25f);
            float a2_z = pa.y - normA.y * (hwA * 0.25f);
            float a3_x = pa.x + normA.x * (hwA * 0.25f);
            float a3_z = pa.y + normA.y * (hwA * 0.25f);
            float a4_x = pa.x + normA.x * (hwA * 0.70f);
            float a4_z = pa.y + normA.y * (hwA * 0.70f);
            float a5_x = pa.x + normA.x * (hwA * 1.35f);
            float a5_z = pa.y + normA.y * (hwA * 1.35f);

            // Slope-conforming height sampling across local terrain:
            float ya0 = MapManager_GetGroundHeightAt(a0_x, a0_z) - EarthCrack_CalcDisplacement(inst, a0_x, a0_z);
            float ya1 = MapManager_GetGroundHeightAt(a1_x, a1_z) - EarthCrack_CalcDisplacement(inst, a1_x, a1_z);
            float ya4 = MapManager_GetGroundHeightAt(a4_x, a4_z) - EarthCrack_CalcDisplacement(inst, a4_x, a4_z);
            float ya5 = MapManager_GetGroundHeightAt(a5_x, a5_z) - EarthCrack_CalcDisplacement(inst, a5_x, a5_z);
            float yaMid = (ya1 + ya4) * 0.5f;

            // Lateral cross-section coordinates for B:
            float b0_x = pb.x - normB.x * (hwB * 1.35f);
            float b0_z = pb.y - normB.y * (hwB * 1.35f);
            float b1_x = pb.x - normB.x * (hwB * 0.70f);
            float b1_z = pb.y - normB.y * (hwB * 0.70f);
            float b2_x = pb.x - normB.x * (hwB * 0.25f);
            float b2_z = pb.y - normB.y * (hwB * 0.25f);
            float b3_x = pb.x + normB.x * (hwB * 0.25f);
            float b3_z = pb.y + normB.y * (hwB * 0.25f);
            float b4_x = pb.x + normB.x * (hwB * 0.70f);
            float b4_z = pb.y + normB.y * (hwB * 0.70f);
            float b5_x = pb.x + normB.x * (hwB * 1.35f);
            float b5_z = pb.y + normB.y * (hwB * 1.35f);

            float yb0 = MapManager_GetGroundHeightAt(b0_x, b0_z) - EarthCrack_CalcDisplacement(inst, b0_x, b0_z);
            float yb1 = MapManager_GetGroundHeightAt(b1_x, b1_z) - EarthCrack_CalcDisplacement(inst, b1_x, b1_z);
            float yb4 = MapManager_GetGroundHeightAt(b4_x, b4_z) - EarthCrack_CalcDisplacement(inst, b4_x, b4_z);
            float yb5 = MapManager_GetGroundHeightAt(b5_x, b5_z) - EarthCrack_CalcDisplacement(inst, b5_x, b5_z);
            float ybMid = (yb1 + yb4) * 0.5f;

            Vector3 a0 = { a0_x, ya0 + 0.0035f, a0_z };
            Vector3 a1 = { a1_x, ya1 + heaveA + 0.0120f, a1_z };
            Vector3 a2 = { a2_x, yaMid + 0.0020f, a2_z };
            Vector3 a3 = { a3_x, yaMid + 0.0020f, a3_z };
            Vector3 a4 = { a4_x, ya4 + heaveA + 0.0120f, a4_z };
            Vector3 a5 = { a5_x, ya5 + 0.0035f, a5_z };

            Vector3 b0 = { b0_x, yb0 + 0.0035f, b0_z };
            Vector3 b1 = { b1_x, yb1 + heaveB + 0.0120f, b1_z };
            Vector3 b2 = { b2_x, ybMid + 0.0020f, b2_z };
            Vector3 b3 = { b3_x, ybMid + 0.0020f, b3_z };
            Vector3 b4 = { b4_x, yb4 + heaveB + 0.0120f, b4_z };
            Vector3 b5 = { b5_x, yb5 + 0.0035f, b5_z };

            unsigned char alphaRockA  = (unsigned char)(240.0f * lifeAlpha * tipAlphaA);
            unsigned char alphaRockB  = (unsigned char)(240.0f * lifeAlpha * tipAlphaB);
            unsigned char alphaAbyssA = (unsigned char)(255.0f * lifeAlpha * tipAlphaA);
            unsigned char alphaAbyssB = (unsigned char)(255.0f * lifeAlpha * tipAlphaB);

            Color cOuter  = { colSoil.r, colSoil.g, colSoil.b, 0 };
            Color cRockA  = { colRock.r, colRock.g, colRock.b, alphaRockA };
            Color cRockB  = { colRock.r, colRock.g, colRock.b, alphaRockB };
            Color cAbyssA = { colAbyss.r, colAbyss.g, colAbyss.b, alphaAbyssA };
            Color cAbyssB = { colAbyss.r, colAbyss.g, colAbyss.b, alphaAbyssB };

            // Quad 0: Left Outer Soil Lip (cOuter -> cRock)
            rlColor4ub(cOuter.r, cOuter.g, cOuter.b, cOuter.a);   rlVertex3f(a0.x, a0.y, a0.z);
            rlColor4ub(cRockA.r, cRockA.g, cRockA.b, cRockA.a);   rlVertex3f(a1.x, a1.y, a1.z);
            rlColor4ub(cRockB.r, cRockB.g, cRockB.b, cRockB.a);   rlVertex3f(b1.x, b1.y, b1.z);
            rlColor4ub(cOuter.r, cOuter.g, cOuter.b, cOuter.a);   rlVertex3f(b0.x, b0.y, b0.z);

            // Quad 1: Left Torn Rock Wall (cRock at top lip -> cAbyss at sunken floor)
            rlColor4ub(cRockA.r,  cRockA.g,  cRockA.b,  cRockA.a);  rlVertex3f(a1.x, a1.y, a1.z);
            rlColor4ub(cAbyssA.r, cAbyssA.g, cAbyssA.b, cAbyssA.a); rlVertex3f(a2.x, a2.y, a2.z);
            rlColor4ub(cAbyssB.r, cAbyssB.g, cAbyssB.b, cAbyssB.a); rlVertex3f(b2.x, b2.y, b2.z);
            rlColor4ub(cRockB.r,  cRockB.g,  cRockB.b,  cRockB.a);  rlVertex3f(b1.x, b1.y, b1.z);

            // Quad 2: Deep Sunken Chasm Floor (pure pitch-black abyss void)
            rlColor4ub(cAbyssA.r, cAbyssA.g, cAbyssA.b, cAbyssA.a); rlVertex3f(a2.x, a2.y, a2.z);
            rlColor4ub(cAbyssA.r, cAbyssA.g, cAbyssA.b, cAbyssA.a); rlVertex3f(a3.x, a3.y, a3.z);
            rlColor4ub(cAbyssB.r, cAbyssB.g, cAbyssB.b, cAbyssB.a); rlVertex3f(b3.x, b3.y, b3.z);
            rlColor4ub(cAbyssB.r, cAbyssB.g, cAbyssB.b, cAbyssB.a); rlVertex3f(b2.x, b2.y, b2.z);

            // Quad 3: Right Torn Rock Wall (cAbyss at sunken floor -> cRock at top lip)
            rlColor4ub(cAbyssA.r, cAbyssA.g, cAbyssA.b, cAbyssA.a); rlVertex3f(a3.x, a3.y, a3.z);
            rlColor4ub(cRockA.r,  cRockA.g,  cRockA.b,  cRockA.a);  rlVertex3f(a4.x, a4.y, a4.z);
            rlColor4ub(cRockB.r,  cRockB.g,  cRockB.b,  cRockB.a);  rlVertex3f(b4.x, b4.y, b4.z);
            rlColor4ub(cAbyssB.r, cAbyssB.g, cAbyssB.b, cAbyssB.a); rlVertex3f(b3.x, b3.y, b3.z);

            // Quad 4: Right Outer Soil Lip (cRock -> cOuter)
            rlColor4ub(cRockA.r, cRockA.g, cRockA.b, cRockA.a);   rlVertex3f(a4.x, a4.y, a4.z);
            rlColor4ub(cOuter.r, cOuter.g, cOuter.b, cOuter.a);   rlVertex3f(a5.x, a5.y, a5.z);
            rlColor4ub(cOuter.r, cOuter.g, cOuter.b, cOuter.a);   rlVertex3f(b5.x, b5.y, b5.z);
            rlColor4ub(cRockB.r, cRockB.g, cRockB.b, cRockB.a);   rlVertex3f(b4.x, b4.y, b4.z);
        }
        rlEnd();
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
        rlEnableDepthMask();
        rlEnableBackfaceCulling();

        // ── PASS 2: Subterranean Vein Glow (Lõi Địa Khí - Additive Emission) ─
        if (cfg->veinGlowIntensity > 0.05f) {
            float pulse = 0.80f + 0.20f * sinf(inst->timer * cfg->pulseSpeed);
            float baseVeinAlpha = Clamp(cfg->veinGlowIntensity * 90.0f * pulse * lifeAlpha, 0.0f, 255.0f);

            if (baseVeinAlpha > 2.0f) {
                VFXRenderScope scope = VFXRender_BeginDraw(VFX_RENDER_PASS_EMISSION, VFX_SURFACE_ADDITIVE, false);
                rlDisableBackfaceCulling();
                rlEnableDepthTest();
                rlDisableDepthMask();
                rlBegin(RL_QUADS);

                for (int i = 0; i < inst->segmentCount; i++) {
                    const EarthCrackSegment *seg = &inst->segments[i];
                    if (seg->startArcDist > currentFront) continue;

                    float reach = currentFront - seg->startArcDist;
                    float segP = (seg->length > 0.0001f) ? reach / seg->length : 1.0f;
                    segP = Clamp(segP, 0.0f, 1.0f);
                    if (segP <= 0.001f) continue;

                    Vector2 pa = seg->a;
                    Vector2 pb = (Vector2){
                        seg->a.x + (seg->b.x - seg->a.x) * segP,
                        seg->a.y + (seg->b.y - seg->a.y) * segP
                    };

                    float tipTaper = (segP < 1.0f) ? segP : 1.0f;
                    float hwA = seg->halfWidthA;
                    float hwB = (seg->halfWidthA + (seg->halfWidthB - seg->halfWidthA) * segP) * tipTaper;
                    Vector2 normA = seg->normalA;
                    Vector2 normB = seg->normalB;
                    if (segP < 1.0f) {
                        Vector2 nInterp = (Vector2){
                            seg->normalA.x + (seg->normalB.x - seg->normalA.x) * segP,
                            seg->normalA.y + (seg->normalB.y - seg->normalA.y) * segP
                        };
                        float nLen = sqrtf(nInterp.x * nInterp.x + nInterp.y * nInterp.y);
                        if (nLen > 0.001f) normB = (Vector2){nInterp.x / nLen, nInterp.y / nLen};
                    }

                    float tipAlphaA = (hwA < 0.024f) ? Clamp(hwA / 0.024f, 0.0f, 1.0f) : 1.0f;
                    float tipAlphaB = (hwB < 0.024f) ? Clamp(hwB / 0.024f, 0.0f, 1.0f) : 1.0f;
                    if (segP < 1.0f) tipAlphaB *= segP;

                    // Narrow luminous vein nestled deep inside the sunken chasm floor (25% of halfWidth)
                    float gWidthA = hwA * 0.25f;
                    float gWidthB = hwB * 0.25f;
                    if (gWidthA < 0.0020f) gWidthA = 0.0020f;
                    if (gWidthB < 0.0020f) gWidthB = 0.0020f;

                    // Epicenter attenuation: prevents multi-branch additive blowout at center
                    float centerFadeA = 1.0f;
                    if (seg->startArcDist < 0.40f) {
                        centerFadeA = 0.25f + 0.75f * (seg->startArcDist / 0.40f);
                    }
                    float endDist = seg->startArcDist + seg->length * segP;
                    float centerFadeB = 1.0f;
                    if (endDist < 0.40f) {
                        centerFadeB = 0.25f + 0.75f * (endDist / 0.40f);
                    }

                    unsigned char vA_start = (unsigned char)Clamp(baseVeinAlpha * centerFadeA * tipAlphaA, 0.0f, 255.0f);
                    unsigned char vA_end   = (unsigned char)Clamp(baseVeinAlpha * centerFadeB * tipAlphaB, 0.0f, 255.0f);
                    if (vA_start <= 1 && vA_end <= 1) continue;

                    // 1. Ambient Lava/Qi Glow on the sunken floor
                    unsigned char glowAlphaA = (unsigned char)(vA_start * 0.70f);
                    unsigned char glowAlphaB = (unsigned char)(vA_end * 0.70f);

                    float rawYa = MapManager_GetGroundHeightAt(pa.x, pa.y);
                    float rawYb = MapManager_GetGroundHeightAt(pb.x, pb.y);
                    float dispA = EarthCrack_CalcDisplacement(inst, pa.x, pa.y);
                    float dispB = EarthCrack_CalcDisplacement(inst, pb.x, pb.y);
                    float floorYa = (rawYa - dispA) + 0.0035f;
                    float floorYb = (rawYb - dispB) + 0.0035f;

                    Vector3 ga0 = { pa.x - normA.x * gWidthA, floorYa, pa.y - normA.y * gWidthA };
                    Vector3 ga1 = { pa.x + normA.x * gWidthA, floorYa, pa.y + normA.y * gWidthA };
                    Vector3 gb1 = { pb.x + normB.x * gWidthB, floorYb, pb.y + normB.y * gWidthB };
                    Vector3 gb0 = { pb.x - normB.x * gWidthB, floorYb, pb.y - normB.y * gWidthB };

                    rlColor4ub(colGlow.r, colGlow.g, colGlow.b, glowAlphaA); rlVertex3f(ga0.x, ga0.y, ga0.z);
                    rlColor4ub(colGlow.r, colGlow.g, colGlow.b, glowAlphaA); rlVertex3f(ga1.x, ga1.y, ga1.z);
                    rlColor4ub(colGlow.r, colGlow.g, colGlow.b, glowAlphaB); rlVertex3f(gb1.x, gb1.y, gb1.z);
                    rlColor4ub(colGlow.r, colGlow.g, colGlow.b, glowAlphaB); rlVertex3f(gb0.x, gb0.y, gb0.z);

                    // 2. Searing Hot Magma / Core Line
                    float cWidthA = gWidthA * 0.40f;
                    float cWidthB = gWidthB * 0.40f;
                    float coreYa = floorYa + 0.0012f;
                    float coreYb = floorYb + 0.0012f;

                    Vector3 ca0 = { pa.x - normA.x * cWidthA, coreYa, pa.y - normA.y * cWidthA };
                    Vector3 ca1 = { pa.x + normA.x * cWidthA, coreYa, pa.y + normA.y * cWidthA };
                    Vector3 cb1 = { pb.x + normB.x * cWidthB, coreYb, pb.y + normB.y * cWidthB };
                    Vector3 cb0 = { pb.x - normB.x * cWidthB, coreYb, pb.y - normB.y * cWidthB };

                    rlColor4ub(cfg->leadFrontColor.r, cfg->leadFrontColor.g, cfg->leadFrontColor.b, vA_start); rlVertex3f(ca0.x, ca0.y, ca0.z);
                    rlColor4ub(cfg->leadFrontColor.r, cfg->leadFrontColor.g, cfg->leadFrontColor.b, vA_start); rlVertex3f(ca1.x, ca1.y, ca1.z);
                    rlColor4ub(cfg->leadFrontColor.r, cfg->leadFrontColor.g, cfg->leadFrontColor.b, vA_end);   rlVertex3f(cb1.x, cb1.y, cb1.z);
                    rlColor4ub(cfg->leadFrontColor.r, cfg->leadFrontColor.g, cfg->leadFrontColor.b, vA_end);   rlVertex3f(cb0.x, cb0.y, cb0.z);
                }

                rlEnd();
                rlDrawRenderBatchActive();
                rlEnableDepthTest();
                rlEnableDepthMask();
                rlEnableBackfaceCulling();
                VFXRender_EndDraw(&scope);
            }
        }
    }
}

#endif // VC_EARTH_CRACK_INL
