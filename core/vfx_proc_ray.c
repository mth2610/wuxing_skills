#include "core/vfx_proc_ray.h"
#include "core/ribbon_strip.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include "core/presets/vfx_presets.h"
#include "core/trail_system.h"
#include "core/vfx_light.h"
#include "core/particle_system.h"

#ifndef PI
#define PI 3.1415926535f
#endif

// ── Presets ────────────────────────────────────────────────────────────────
// thickness fields real-world-scaled ÷100 (root CLAUDE.md "Standard
// coordinates & scale") — were 1.1f/1.6f/1.5f/1.8f, a 1-2 *meter* ribbon
// half-width at the new scale. This shared preset is what was producing a
// screen-covering bloom blob for thunder_orb_skill's 7 flight-phase
// lightning rays (each individually 2+ meters thick) even at correct
// position — a size bug, not the earlier CastSkill()-position bug.

ProcRayConfig ProcRay_LightningConfig(void) {
    return (ProcRayConfig){
        .colorCore      = { 245, 240, 255, 255 },
        .colorGlow      = { 110,  30, 255, 120 },
        .glowWidthMult  = 1.7f,
        .waveSpeed      = 4.5f,
        .amplitudeRatio = 0.38f,
        .jitterStrength = 1.0f,
        .thickness      = 0.011f,
        .envelopePow    = 0.7f,  // fast bloom — bolts diverge early, not just at free end
        .sharpKinks     = true,
        .taperTip       = 0.12f, // tendrils end in a needle point
        .branchCount    = 0,
        .branchScale    = 0.5f,
    };
}

ProcRayConfig ProcRay_BoltLightningConfig(void) {
    return (ProcRayConfig){
        .colorCore      = { 245, 240, 255, 255 },
        .colorGlow      = { 130,  50, 255, 150 },
        .glowWidthMult  = 1.6f,
        .waveSpeed      = 0.0f,   // unused for bolts
        .amplitudeRatio = 0.10f,  // straighter — stays close to the vertical axis
        .jitterStrength = 1.0f,
        .thickness      = 0.016f,
        .envelopePow    = 1.0f,
        .sharpKinks     = true,
        .taperTip       = 0.75f,
        .branchCount    = 3,
        .branchScale    = 0.45f,
    };
}

ProcRayConfig ProcRay_EnergyConfig(void) {
    return (ProcRayConfig){
        .colorCore      = { 255, 240, 120, 255 },
        .colorGlow      = {  80, 200, 255, 100 },
        .glowWidthMult  = 1.8f,
        .waveSpeed      = 3.0f,
        .amplitudeRatio = 0.30f,
        .jitterStrength = 0.3f,
        .thickness      = 0.015f,
        .envelopePow    = 1.0f,
        .sharpKinks     = false,
        .taperTip       = 1.0f,
        .branchCount    = 0,
        .branchScale    = 0.5f,
    };
}

ProcRayConfig ProcRay_WindConfig(void) {
    return (ProcRayConfig){
        .colorCore      = { 200, 240, 255, 180 },
        .colorGlow      = { 160, 230, 255,  60 },
        .glowWidthMult  = 2.0f,
        .waveSpeed      = 2.0f,
        .amplitudeRatio = 0.20f,
        .jitterStrength = 0.05f,
        .thickness      = 0.018f,
        .envelopePow    = 1.5f,
        .sharpKinks     = false,
        .taperTip       = 1.0f,
        .branchCount    = 0,
        .branchScale    = 0.5f,
    };
}

// ── Pool ───────────────────────────────────────────────────────────────────

#define MAX_PROC_RAYS    32
#define RAY_WAYPOINT_CNT  9
#define RAY_RIBBON_PTS   36

typedef struct {
    bool          active;
    ProcRayConfig config;
    float         scale;
    float         phase;
    float         brightness;
    Vector3       refHint;   // used to build perp basis — set per-slot so bolts don't share same p2
    Vector3       waypoints[RAY_WAYPOINT_CNT];
} ProcRaySlot;

static ProcRaySlot s_rays[MAX_PROC_RAYS];
static bool        s_raysInited = false;

static void EnsureInited(void) {
    if (s_raysInited) return;
    for (int i = 0; i < MAX_PROC_RAYS; i++) s_rays[i].active = false;
    s_raysInited = true;
}

// ── Wave generation ────────────────────────────────────────────────────────

static void GenerateWaypoints(Vector3 *out, Vector3 origin, Vector3 dir,
                               float length, float phase, Vector3 refHint,
                               float amplitudeRatio, float jitter, float envelopePow, float scale) {
    Vector3 d = Vector3Normalize(dir);
    // Use caller-supplied refHint so different slots get independent perp planes.
    // Fall back to world-up only if refHint is nearly parallel to d.
    Vector3 ref = (fabsf(Vector3DotProduct(d, refHint)) > 0.9f)
                      ? ((fabsf(d.y) > 0.95f) ? (Vector3){1,0,0} : (Vector3){0,1,0})
                      : refHint;
    Vector3 p1 = Vector3Normalize(Vector3CrossProduct(d, ref));
    Vector3 p2 = Vector3Normalize(Vector3CrossProduct(d, p1));

    float amp = length * amplitudeRatio * fmaxf(scale, 0.1f);

    out[0] = origin;
    for (int i = 1; i < RAY_WAYPOINT_CNT; i++) {
        float t        = (float)i / (float)(RAY_WAYPOINT_CNT - 1);
        float envelope = powf(t, envelopePow);

        // Multi-harmonic at irrational ratios — never repeats a recognisable pattern.
        // Golden ratio (1.618), sqrt(2) (1.414), sqrt(3)/3 (0.577) keep waves incoherent.
        float wR = sinf(phase         + t * PI * 2.0f) * 0.55f
                 + sinf(phase * 1.618f + t * PI * 3.7f) * 0.30f
                 + sinf(phase * 0.577f + t * PI * 7.1f) * 0.15f;
        float wU = cosf(phase * 0.7f  + t * PI * 1.5f) * 0.55f
                 + cosf(phase * 1.414f + t * PI * 4.9f) * 0.30f
                 + cosf(phase * 2.236f + t * PI * 2.6f) * 0.15f;

        // Larger per-node jitter so high-jitter presets (lightning) stay chaotic
        float jR = (float)GetRandomValue(-100, 100) * 0.012f * jitter;
        float jU = (float)GetRandomValue(-100, 100) * 0.010f * jitter;

        Vector3 base   = Vector3Add(origin, Vector3Scale(d, t * length));
        Vector3 offset = Vector3Add(
            Vector3Scale(p1, (wR + jR) * amp * envelope),
            Vector3Scale(p2, (wU + jU) * amp * envelope * 0.65f));
        out[i] = Vector3Add(base, offset);
    }
}

// ── Ribbon draw ────────────────────────────────────────────────────────────

static RibbonPoint s_ribbon[RAY_RIBBON_PTS];

// Draws one lightning channel in 3 passes (outer haze → glow → hot core).
// widthMul/alphaMul scale the whole channel (branches use <1). brightness >1
// pushes alpha toward saturation for the strike-flash frame.
static void DrawChannel(const Vector3 *wp, int wpCount, const ProcRayConfig *cfg,
                        float widthMul, float alphaMul, Camera3D cam) {
    float taper  = (cfg->taperTip > 0.0f) ? cfg->taperTip : 1.0f;
    int   ribPts = (wpCount - 1) * 4 + 1;
    if (ribPts > RAY_RIBBON_PTS) ribPts = RAY_RIBBON_PTS;

    for (int pass = 0; pass < 3; pass++) {
        float w;
        Color tint;
        float aMul = alphaMul;
        if (pass == 0) {         // outer haze — wide, faint, sells the bloom
            w    = cfg->thickness * cfg->glowWidthMult * 2.4f;
            tint = cfg->colorGlow;
            aMul *= 0.30f;
        } else if (pass == 1) {  // main glow
            w    = cfg->thickness * cfg->glowWidthMult;
            tint = cfg->colorGlow;
        } else {                 // hot core
            w    = cfg->thickness;
            tint = cfg->colorCore;
        }
        w *= widthMul;

        for (int k = 0; k < ribPts; k++) {
            float f    = (float)k / (float)(ribPts - 1);
            float edge = 0.08f;
            float tap;
            if      (f < edge)        tap = f / edge;
            else if (f > 1.0f - edge) tap = (1.0f - f) / edge;
            else                      tap = 1.0f;
            tap = tap * tap * (3.0f - 2.0f * tap);

            float widthTaper = 1.0f + (taper - 1.0f) * f;

            float fi  = f * (float)(wpCount - 1);
            int   seg = (int)fi;
            if (seg >= wpCount - 1) seg = wpCount - 2;
            float lt  = fi - (float)seg;

            Vector3 pt;
            if (cfg->sharpKinks) {
                // Linear — preserves sharp kinks at each waypoint (lightning-style)
                pt = Vector3Lerp(wp[seg], wp[seg + 1], lt);
            } else {
                // Catmull-Rom — smooth curves (energy/wind-style)
                Vector3 p0 = wp[(seg > 0) ? seg - 1 : 0];
                Vector3 p1 = wp[seg];
                Vector3 p2 = wp[seg + 1];
                Vector3 p3 = wp[(seg + 2 < wpCount) ? seg + 2 : wpCount - 1];
                float t2 = lt * lt, t3 = t2 * lt;
                pt = (Vector3){
                    0.5f * ((2*p1.x) + (-p0.x+p2.x)*lt + (2*p0.x-5*p1.x+4*p2.x-p3.x)*t2 + (-p0.x+3*p1.x-3*p2.x+p3.x)*t3),
                    0.5f * ((2*p1.y) + (-p0.y+p2.y)*lt + (2*p0.y-5*p1.y+4*p2.y-p3.y)*t2 + (-p0.y+3*p1.y-3*p2.y+p3.y)*t3),
                    0.5f * ((2*p1.z) + (-p0.z+p2.z)*lt + (2*p0.z-5*p1.z+4*p2.z-p3.z)*t2 + (-p0.z+3*p1.z-3*p2.z+p3.z)*t3),
                };
            }

            float a = tint.a * tap * aMul;
            if (a > 255.0f) a = 255.0f;
            s_ribbon[k].position  = pt;
            s_ribbon[k].halfWidth = w * tap * widthTaper;
            s_ribbon[k].v         = f;
            s_ribbon[k].tint      = (Color){ tint.r, tint.g, tint.b, (unsigned char)a };
        }
        DrawRibbonStrip(s_ribbon, ribPts, (Texture2D){0}, cam);
    }
}

// ── Public API ─────────────────────────────────────────────────────────────

int SpawnProcRay(ProcRayConfig config, float scale) {
    EnsureInited();
    for (int i = 0; i < MAX_PROC_RAYS; i++) {
        if (!s_rays[i].active) {
            s_rays[i].active     = true;
            s_rays[i].config     = config;
            s_rays[i].scale      = fmaxf(scale, 0.1f);
            s_rays[i].phase      = 0.0f;
            s_rays[i].brightness = 1.0f;
            // Random refHint so concurrent rays in the same horizontal plane
            // get independent perp bases (avoids all p2 collapsing to world-down).
            float a = (float)GetRandomValue(0, 628) * 0.01f;
            float b = (float)GetRandomValue(0, 314) * 0.01f;
            s_rays[i].refHint = (Vector3){ sinf(b)*cosf(a), cosf(b), sinf(b)*sinf(a) };
            return i;
        }
    }
    TraceLog(LOG_WARNING, "SpawnProcRay: pool full (MAX_PROC_RAYS=%d)", MAX_PROC_RAYS);
    return -1;
}

void ProcRay_SetPhase(int id, float phase) {
    if (id < 0 || id >= MAX_PROC_RAYS || !s_rays[id].active) return;
    s_rays[id].phase = phase;
}

void ProcRay_SetBrightness(int id, float b) {
    if (id < 0 || id >= MAX_PROC_RAYS || !s_rays[id].active) return;
    s_rays[id].brightness = fmaxf(b, 0.0f);
}

void ProcRay_Update(int id, Vector3 origin, Vector3 dir, float length, float scale, float dt) {
    if (id < 0 || id >= MAX_PROC_RAYS || !s_rays[id].active) return;
    ProcRaySlot *s = &s_rays[id];
    s->scale  = fmaxf(scale, 0.1f);
    s->phase += s->config.waveSpeed * dt;
    GenerateWaypoints(s->waypoints, origin, dir, length, s->phase, s->refHint,
                      s->config.amplitudeRatio, s->config.jitterStrength,
                      s->config.envelopePow, s->scale);
}

void ProcRay_Draw(int id, Camera3D cam) {
    if (id < 0 || id >= MAX_PROC_RAYS || !s_rays[id].active) return;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    DrawChannel(s_rays[id].waypoints, RAY_WAYPOINT_CNT, &s_rays[id].config,
                1.0f, s_rays[id].brightness, cam);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void ProcRay_Kill(int id) {
    if (id < 0 || id >= MAX_PROC_RAYS) return;
    s_rays[id].active = false;
}

// ── ProcBolt — two-fixed-endpoint bolt (sky→ground, A→B) ──────────────────
// Both endpoints are clamped. Middle waypoints are randomised every
// BOLT_FLICKER_INTERVAL seconds to produce the characteristic flicker.

#define MAX_PROC_BOLTS      32
#define BOLT_FLICKER_INTERVAL 0.05f
#define BOLT_MAX_BRANCHES     4
#define BRANCH_WP_CNT         6

typedef struct {
    bool          active;
    ProcRayConfig config;
    float         scale;
    float         flickerTimer;
    float         brightness;
    int           branchCount;
    Vector3       waypoints[RAY_WAYPOINT_CNT];
    Vector3       branchWp[BOLT_MAX_BRANCHES][BRANCH_WP_CNT];
} ProcBoltSlot;

static ProcBoltSlot s_bolts[MAX_PROC_BOLTS];
static bool         s_boltsInited = false;

static void EnsureBoltsInited(void) {
    if (s_boltsInited) return;
    for (int i = 0; i < MAX_PROC_BOLTS; i++) s_bolts[i].active = false;
    s_boltsInited = true;
}

// Generate jagged waypoints clamped at both ends. Envelope = sin(t*PI) so
// displacement is 0 at origin AND at endpoint — both stay pinned.
static void GenerateBoltWaypoints(Vector3 *out, Vector3 from, Vector3 to,
                                   float jaggedRatio, float jitter) {
    float len = sqrtf((to.x-from.x)*(to.x-from.x) +
                      (to.y-from.y)*(to.y-from.y) +
                      (to.z-from.z)*(to.z-from.z));
    float jagged = len * jaggedRatio;

    Vector3 dir = { (to.x-from.x)/len, (to.y-from.y)/len, (to.z-from.z)/len };
    Vector3 ref = (fabsf(dir.y) > 0.95f) ? (Vector3){1,0,0} : (Vector3){0,1,0};
    Vector3 p1  = Vector3Normalize(Vector3CrossProduct(dir, ref));
    Vector3 p2  = Vector3Normalize(Vector3CrossProduct(dir, p1));

    out[0] = from;
    out[RAY_WAYPOINT_CNT - 1] = to;
    for (int i = 1; i < RAY_WAYPOINT_CNT - 1; i++) {
        float t        = (float)i / (float)(RAY_WAYPOINT_CNT - 1);
        float envelope = sinf(t * PI);   // 0 at both ends, peak in middle
        float dR = (float)GetRandomValue(-100, 100) * 0.01f * jagged * envelope * jitter;
        float dU = (float)GetRandomValue(-100, 100) * 0.01f * jagged * envelope * 0.7f * jitter;
        Vector3 base = {
            from.x + dir.x * t * len,
            from.y + dir.y * t * len,
            from.z + dir.z * t * len,
        };
        out[i] = (Vector3){
            base.x + p1.x*dR + p2.x*dU,
            base.y + p1.y*dR + p2.y*dU,
            base.z + p1.z*dR + p2.z*dU,
        };
    }
}

// Branch: forks off waypoint forkIdx of the main channel, deviates from the
// main direction, jagged, pinned at the fork and free at the tip.
static void GenerateBranch(Vector3 *out, const Vector3 *mainWp, int forkIdx,
                            Vector3 mainDir, float remLen, float jitter) {
    Vector3 ref = (fabsf(mainDir.y) > 0.95f) ? (Vector3){1,0,0} : (Vector3){0,1,0};
    Vector3 p1  = Vector3Normalize(Vector3CrossProduct(mainDir, ref));
    Vector3 p2  = Vector3Normalize(Vector3CrossProduct(mainDir, p1));

    float ang = (float)GetRandomValue(0, 628) * 0.01f;
    float dev = 0.5f + (float)GetRandomValue(0, 100) * 0.004f; // 0.5–0.9 sideways
    Vector3 side = Vector3Add(Vector3Scale(p1, cosf(ang) * dev),
                              Vector3Scale(p2, sinf(ang) * dev));
    Vector3 bd   = Vector3Normalize(Vector3Add(mainDir, side));
    float   blen = remLen * (0.25f + (float)GetRandomValue(0, 100) * 0.002f); // 25–45%
    float   jag  = blen * 0.22f * jitter;

    out[0] = mainWp[forkIdx];
    for (int i = 1; i < BRANCH_WP_CNT; i++) {
        float t = (float)i / (float)(BRANCH_WP_CNT - 1);
        float dR = (float)GetRandomValue(-100, 100) * 0.01f * jag * t;
        float dU = (float)GetRandomValue(-100, 100) * 0.01f * jag * t * 0.7f;
        out[i] = (Vector3){
            out[0].x + bd.x * t * blen + p1.x * dR + p2.x * dU,
            out[0].y + bd.y * t * blen + p1.y * dR + p2.y * dU,
            out[0].z + bd.z * t * blen + p1.z * dR + p2.z * dU,
        };
    }
}

int SpawnProcBolt(ProcRayConfig config, float scale) {
    EnsureBoltsInited();
    for (int i = 0; i < MAX_PROC_BOLTS; i++) {
        if (!s_bolts[i].active) {
            s_bolts[i].active       = true;
            s_bolts[i].config       = config;
            s_bolts[i].scale        = fmaxf(scale, 0.1f);
            // start at the interval so the first Update generates waypoints
            // immediately (otherwise the first frames draw zeroed points)
            s_bolts[i].flickerTimer = BOLT_FLICKER_INTERVAL;
            s_bolts[i].brightness   = 1.0f;
            s_bolts[i].branchCount  = 0;
            return i;
        }
    }
    TraceLog(LOG_WARNING, "SpawnProcBolt: pool full (MAX_PROC_BOLTS=%d)", MAX_PROC_BOLTS);
    return -1;
}

void ProcBolt_SetBrightness(int id, float b) {
    if (id < 0 || id >= MAX_PROC_BOLTS || !s_bolts[id].active) return;
    s_bolts[id].brightness = fmaxf(b, 0.0f);
}

void ProcBolt_Update(int id, Vector3 from, Vector3 to, float scale, float dt) {
    if (id < 0 || id >= MAX_PROC_BOLTS || !s_bolts[id].active) return;
    ProcBoltSlot *b = &s_bolts[id];
    b->scale = fmaxf(scale, 0.1f);
    b->flickerTimer += dt;
    if (b->flickerTimer >= BOLT_FLICKER_INTERVAL) {
        b->flickerTimer = 0.0f;
        GenerateBoltWaypoints(b->waypoints, from, to,
                               b->config.amplitudeRatio,
                               b->config.jitterStrength);

        // Regenerate branches on each flicker so forks crackle with the channel
        int wanted = b->config.branchCount;
        if (wanted > BOLT_MAX_BRANCHES) wanted = BOLT_MAX_BRANCHES;
        b->branchCount = wanted;
        if (wanted > 0) {
            float len = Vector3Distance(from, to);
            if (len > 0.001f) {
                Vector3 dir = Vector3Scale(Vector3Subtract(to, from), 1.0f / len);
                for (int k = 0; k < wanted; k++) {
                    // fork somewhere in the upper/middle part of the channel
                    int forkIdx = 2 + GetRandomValue(0, RAY_WAYPOINT_CNT - 5);
                    float remLen = len * (1.0f - (float)forkIdx / (float)(RAY_WAYPOINT_CNT - 1));
                    GenerateBranch(b->branchWp[k], b->waypoints, forkIdx, dir,
                                   remLen, b->config.jitterStrength);
                }
            }
        }
    }
}

void ProcBolt_Draw(int id, Camera3D cam) {
    if (id < 0 || id >= MAX_PROC_BOLTS || !s_bolts[id].active) return;
    ProcBoltSlot *b = &s_bolts[id];
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    DrawChannel(b->waypoints, RAY_WAYPOINT_CNT, &b->config, 1.0f, b->brightness, cam);
    for (int k = 0; k < b->branchCount; k++) {
        DrawChannel(b->branchWp[k], BRANCH_WP_CNT, &b->config,
                    b->config.branchScale, b->brightness * 0.7f, cam);
    }
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void ProcBolt_Kill(int id) {
    if (id < 0 || id >= MAX_PROC_BOLTS) return;
    s_bolts[id].active = false;
}

// ── Lightning Trail System (Hợp nhất từ skill_helper) ─────────────────────
#define LIGHTNING_BOLT_WAYPOINTS 9
#define LIGHTNING_BOLT_PUSH_COUNT 50
#define MAX_CONCURRENT_LIGHTNING_TRAILS 16

typedef struct {
    bool active;
    int trailId;
    Vector3 waypoints[LIGHTNING_BOLT_WAYPOINTS];
    float travelDuration;
    float elapsed;
    int pushedCount;
} LightningBoltState;

static LightningBoltState s_lightningBolts[MAX_CONCURRENT_LIGHTNING_TRAILS];
static int s_lightningBoltNextSlot = 0;

typedef struct {
    bool active;
    int trailId;
    bool hasLastRecordedPos;
    Vector3 lastRecordedPos;
    float sign;
} LightningFollowerState;

static LightningFollowerState s_lightningFollowers[MAX_CONCURRENT_LIGHTNING_TRAILS];
static int s_lightningFollowerFldNextSlot = 0;
static ForceField s_lightningFollowerFlds[MAX_CONCURRENT_LIGHTNING_TRAILS];

static void GenerateLightningWaypoints(Vector3 *out, int count, Vector3 start, Vector3 target, float jaggedAmount) {
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, start));
    Vector3 refUp = (fabsf(dir.y) > 0.95f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 perp1 = Vector3Normalize(Vector3CrossProduct(dir, refUp));
    Vector3 perp2 = Vector3Normalize(Vector3CrossProduct(dir, perp1));

    for (int i = 0; i < count; i++) {
        float t = (count > 1) ? (float)i / (float)(count - 1) : 0.0f;
        Vector3 base = Vector3Lerp(start, target, t);
        if (i == 0 || i == count - 1) {
            out[i] = base;
            continue;
        }
        float sign = (i % 2 == 0) ? 1.0f : -1.0f;
        float jitter1 = (float)GetRandomValue(-100, 100) * 0.01f;
        float jitter2 = (float)GetRandomValue(-100, 100) * 0.01f;
        Vector3 offset = Vector3Add(
            Vector3Scale(perp1, sign * jaggedAmount * (0.7f + 0.3f * jitter1)),
            Vector3Scale(perp2, jitter2 * jaggedAmount * 0.5f));
        out[i] = Vector3Add(base, offset);
    }
}

static Vector3 SampleLightningPath(const Vector3 *waypoints, int count, float f) {
    if (f <= 0.0f) return waypoints[0];
    if (f >= 1.0f) return waypoints[count - 1];
    float seg = f * (float)(count - 1);
    int idx = (int)seg;
    float u = seg - (float)idx;
    int next = idx + 1;
    if (next >= count) next = count - 1;
    return Vector3Lerp(waypoints[idx], waypoints[next], u);
}

static void LightningBoltAdvance(int trailId, float dt) {
    LightningBoltState *bolt = NULL;
    for (int i = 0; i < MAX_CONCURRENT_LIGHTNING_TRAILS; i++) {
        if (s_lightningBolts[i].active && s_lightningBolts[i].trailId == trailId) {
            bolt = &s_lightningBolts[i];
            break;
        }
    }
    if (!bolt) return;

    TrailEntity *t = GetTrail(trailId);
    if (!t || !t->active) { bolt->active = false; return; }

    bolt->elapsed += dt;
    float f = bolt->elapsed / bolt->travelDuration;
    if (f > 1.0f) f = 1.0f;

    int targetPushed = (int)(f * (float)LIGHTNING_BOLT_PUSH_COUNT);
    if (targetPushed > LIGHTNING_BOLT_PUSH_COUNT) targetPushed = LIGHTNING_BOLT_PUSH_COUNT;

    Vector3 tip = bolt->waypoints[0];
    while (bolt->pushedCount < targetPushed) {
        bolt->pushedCount++;
        float pushF = (float)bolt->pushedCount / (float)LIGHTNING_BOLT_PUSH_COUNT;
        tip = SampleLightningPath(bolt->waypoints, LIGHTNING_BOLT_WAYPOINTS, pushF);
        UpdateFollowerPosition(trailId, tip);
        SpawnParticle((ParticleConfig){
            .position = tip,
            .colorStart = t->tint,
            .colorEnd = t->tint,
            .radius = 2.2f * t->scale,
            .lifetime = 0.15f,
            .gradient = &s_lightningGrad
        });
    }

    if ((float)rand() / (float)RAND_MAX < dt * 10.0f) {
        VFXLight_Spawn(tip, t->tint, 35.0f * t->scale, 0.08f, VFX_PRIORITY_LOW);
    }

    if (f >= 1.0f) {
        bolt->active = false;
        KillTrail(trailId);
    }
}

#define LIGHTNING_FOLLOWER_MIN_SEGMENT 45.0f

static void LightningTrailFlicker(int trailId, float dt) {
    TrailEntity *t = GetTrail(trailId);
    if (!t || !t->active) return;
    if ((float)rand() / (float)RAND_MAX < dt * 10.0f) {
        VFXLight_Spawn(t->position, t->tint, 35.0f * t->scale, 0.08f, VFX_PRIORITY_LOW);
    }
}

int SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed) {
    VFX_Presets_Init();

    float boltLen = Vector3Distance(start, target);
    float jaggedAmount = fmaxf(0.5f, boltLen * 0.08f) * scale;
    float travelDuration = boltLen / fmaxf(speed, 1.0f);
    Color tint = (Color){ 220, 200, 255, 255 };

    TrailConfig cfg = {
        .type = TRAIL_TYPE_FOLLOWER,
        .pos = start,
        .thick = 2.2f * scale,
        .life = travelDuration + 0.3f,
        .scale = scale,
        .tint = tint,
        .gradient = &s_lightningGrad,
        .onUpdate = LightningBoltAdvance
    };
    int trailId = SpawnTrailEntity(cfg);
    if (trailId < 0) return trailId;

    LightningBoltState *bolt = &s_lightningBolts[s_lightningBoltNextSlot];
    s_lightningBoltNextSlot = (s_lightningBoltNextSlot + 1) % MAX_CONCURRENT_LIGHTNING_TRAILS;
    bolt->active = true;
    bolt->trailId = trailId;
    bolt->travelDuration = travelDuration;
    bolt->elapsed = 0.0f;
    bolt->pushedCount = 0;
    GenerateLightningWaypoints(bolt->waypoints, LIGHTNING_BOLT_WAYPOINTS, start, target, jaggedAmount);
    UpdateFollowerPosition(trailId, bolt->waypoints[0]);

    return trailId;
}

int SpawnLightningFollowerTrail(Vector3 startPos, float scale, float life) {
    VFX_Presets_Init();

    int slot = s_lightningFollowerFldNextSlot;
    s_lightningFollowerFldNextSlot = (s_lightningFollowerFldNextSlot + 1) % MAX_CONCURRENT_LIGHTNING_TRAILS;
    ForceField *followerFld = &s_lightningFollowerFlds[slot];
    ForceField_Clear(followerFld);
    ForceField_AddLayer(followerFld, (ForceLayer){ .type = FORCE_NOISE_PERLIN, .strength = 100.0f, .noiseScale = 0.6f, .noiseSpeed = 14.0f });
    ForceField_AddLayer(followerFld, (ForceLayer){ .type = FORCE_VISCOSITY, .strength = 4.0f });

    LightningFollowerState *st = &s_lightningFollowers[slot];
    st->active = true;
    st->hasLastRecordedPos = false;
    st->sign = 1.0f;

    TrailConfig cfg = {
        .type = TRAIL_TYPE_FOLLOWER,
        .pos = startPos,
        .len = 8.0f * scale,
        .thick = 3.5f * scale,
        .trailLength = 55.0f,
        .life = life,
        .scale = scale,
        .tint = (Color){ 220, 200, 255, 255 },
        .forceField = followerFld,
        .gradient = &s_lightningFollowerGrad,
        .onUpdate = LightningTrailFlicker
    };
    int trailId = SpawnTrailEntity(cfg);
    st->trailId = trailId;
    return trailId;
}

void Lightning_UpdateFollowerTip(int id, Vector3 tipPos, float scale) {
    LightningFollowerState *st = NULL;
    for (int i = 0; i < MAX_CONCURRENT_LIGHTNING_TRAILS; i++) {
        if (s_lightningFollowers[i].active && s_lightningFollowers[i].trailId == id) {
            st = &s_lightningFollowers[i];
            break;
        }
    }
    if (!st) return;

    if (!st->hasLastRecordedPos) {
        st->hasLastRecordedPos = true;
        st->lastRecordedPos = tipPos;
        UpdateFollowerPosition(id, tipPos);
        return;
    }

    float minSeg = LIGHTNING_FOLLOWER_MIN_SEGMENT * fmaxf(scale, 0.01f);
    if (Vector3DistanceSqr(tipPos, st->lastRecordedPos) < minSeg * minSeg) {
        TrailEntity *ent = GetTrail(id);
        if (ent) ent->timeSinceLastFollowerUpdate = 0.0f;
        return;
    }

    Vector3 dir = Vector3Normalize(Vector3Subtract(tipPos, st->lastRecordedPos));
    Vector3 refUp = (fabsf(dir.y) > 0.95f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 perp = Vector3Normalize(Vector3CrossProduct(dir, refUp));
    st->sign = -st->sign;
    Vector3 mid = Vector3Lerp(st->lastRecordedPos, tipPos, 0.5f);
    Vector3 kink = Vector3Add(mid, Vector3Scale(perp, st->sign * minSeg * 0.4f));

    UpdateFollowerPosition(id, kink);
    UpdateFollowerPosition(id, tipPos);
    st->lastRecordedPos = tipPos;
}

void RegenerateLightningWaypoints(Vector3 *waypoints9, Vector3 from, Vector3 to, float scale) {
    VFX_Presets_Init();
    float boltLen = Vector3Distance(from, to);
    float jaggedAmount = boltLen * 0.14f * fmaxf(scale, 0.1f);
    GenerateLightningWaypoints(waypoints9, 9, from, to, jaggedAmount);
}

void RegenerateLightningRay(Vector3 *waypoints9, Vector3 origin, Vector3 direction,
                             float length, float phase, float amplitude, float scale) {
    VFX_Presets_Init();

    Vector3 dir = Vector3Normalize(direction);
    Vector3 refUp = (fabsf(dir.y) > 0.95f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 perp1 = Vector3Normalize(Vector3CrossProduct(dir, refUp));
    Vector3 perp2 = Vector3Normalize(Vector3CrossProduct(dir, perp1));

    float amp = amplitude * fmaxf(scale, 0.1f);

    for (int i = 0; i < 9; i++) {
        float t = (float)i / 8.0f;
        Vector3 base = Vector3Add(origin, Vector3Scale(dir, t * length));

        if (i == 0) {
            waypoints9[0] = origin;
            continue;
        }

        float envelope = powf(t, 1.5f);
        float wR = sinf(phase + t * PI * 2.0f);
        float wU = cosf(phase * 0.7f + t * PI * 1.5f);
        float jR = (float)GetRandomValue(-100, 100) * 0.004f;
        float jU = (float)GetRandomValue(-100, 100) * 0.003f;

        Vector3 offset = Vector3Add(
            Vector3Scale(perp1, (wR + jR) * amp * envelope),
            Vector3Scale(perp2, (wU + jU) * amp * envelope * 0.65f));

        waypoints9[i] = Vector3Add(base, offset);
    }
}

#define LIGHTNING_BOLT_RIBBON_PTS 36
static RibbonPoint s_boltRibbon[LIGHTNING_BOLT_RIBBON_PTS];

void DrawLightningBolt(const Vector3 *waypoints9, float thickness, Camera3D cam) {
    // Legacy fixed palette: violet glow, blue-white core.
    DrawLightningBoltEx(waypoints9, thickness, cam,
                        (Color){100, 20, 255, 130}, (Color){230, 210, 255, 255});
}

void DrawLightningBoltEx(const Vector3 *waypoints9, float thickness, Camera3D cam,
                         Color colorGlow, Color colorCore) {
    for (int pass = 0; pass < 2; pass++) {
        float w = (pass == 0) ? thickness * 1.6f : thickness;
        Color c = (pass == 0) ? colorGlow : colorCore;
        unsigned char r = c.r, g = c.g, b = c.b, a = c.a;

        for (int k = 0; k < LIGHTNING_BOLT_RIBBON_PTS; k++) {
            float f = (float)k / (float)(LIGHTNING_BOLT_RIBBON_PTS - 1);
            Vector3 pt = SampleLightningPath(waypoints9, 9, f);

            float edge = 0.08f;
            float taper;
            if      (f < edge)         taper = f / edge;
            else if (f > 1.0f - edge)  taper = (1.0f - f) / edge;
            else                       taper = 1.0f;
            taper = taper * taper * (3.0f - 2.0f * taper);

            s_boltRibbon[k].position  = pt;
            s_boltRibbon[k].halfWidth = w * taper;
            s_boltRibbon[k].v         = f;
            s_boltRibbon[k].tint      = (Color){ r, g, b, (unsigned char)(a * taper) };
        }
        DrawRibbonStrip(s_boltRibbon, LIGHTNING_BOLT_RIBBON_PTS, (Texture2D){0}, cam);
    }
}
