#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/vfx_light.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/camera_fx.h"
#include "core/particle_system.h"
#include "core/force_field.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/trail_system.h"
#include "core/ribbon_strip.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdlib.h>
#include <math.h>

#define MAX_VOLUMES 32
#define MAX_EMITTERS 32

static DamageVolume s_volumes[MAX_VOLUMES];
static ParticleEmitter s_emitters[MAX_EMITTERS];

static ColorGradient s_fireGrad;
static ColorGradient s_snowGrad;
static ColorGradient s_waterGrad;
static ColorGradient s_lightningGrad;
static ColorGradient s_lightningFollowerGrad; // reversed: bright at head (current), fade at tail
static ColorGradient s_woodGrad;
static ColorGradient s_earthGrad;
static ColorGradient s_metalGrad;
static ColorGradient s_taijiGrad;

static ForceField s_fireFld;
static ForceField s_snowFld;
static ForceField s_waterFld;
static ForceField s_lightningFld;
static ForceField s_woodFld;
static ForceField s_earthFld;
static ForceField s_metalFld;
static ForceField s_taijiFld;

// Pool of "energy gathering" pull fields for SpawnCastEffect. Each call takes
// the next slot round-robin, so concurrent casts at different positions (e.g.
// 4vs4 PvP) don't fight over a single shared field. Particles store a pointer
// to their slot, so slots must be static (no malloc).
#define MAX_CONCURRENT_CAST_EFFECTS 8
static ForceField s_castPullFlds[MAX_CONCURRENT_CAST_EFFECTS];
static int s_castPullFldNextSlot = 0;

// Pool of directional flight forces for SpawnProjectileTrail, same
// round-robin-slot pattern as s_castPullFlds (particles/trails store a
// pointer to the slot, so it must be static).
#define MAX_CONCURRENT_PROJECTILE_TRAILS 8
static ForceField s_flightFlds[MAX_CONCURRENT_PROJECTILE_TRAILS];
static int s_flightFldNextSlot = 0;

// Pools for SpawnLightningTrail / SpawnLightningFollowerTrail, same
// round-robin-slot pattern as s_flightFlds. Separate from s_lightningFld
// (used by particle emitter presets) because trail zigzag needs a much
// sharper/faster noise profile than particle spark jitter.
#define MAX_CONCURRENT_LIGHTNING_TRAILS 8
static ForceField s_lightningFollowerFlds[MAX_CONCURRENT_LIGHTNING_TRAILS];
static int s_lightningFollowerFldNextSlot = 0;

// SpawnLightningTrail's path is NOT physics/noise-driven — two prior passes
// (TRAIL_TYPE_PROJECTILE with noise+overrides, then TRAIL_TYPE_WISP) were
// both tried and confirmed visually to smooth the path out: PROJECTILE's
// built-in homing steer damps deviation back toward straight every frame,
// and WISP's ConstrainRibbonSegment distance-solver (needed to keep it
// rope-coherent) low-pass-filters any per-node jaggedness into a flowing
// "silk ribbon" sag. Neither can hold a real sharp-angle zigzag, because
// both are built to stay smooth/coherent by design.
//
// Instead: precompute a jagged polyline once at spawn (alternating
// perpendicular offsets, pinned exactly at both ends, no forceField
// involved at all — so no gravity-like sag is possible), then advance a
// TRAIL_TYPE_FOLLOWER's tip along it frame-by-frame from inside its
// onUpdate callback via UpdateFollowerPosition. This is pure geometry, so
// travel speed and shape are both fully caller-controlled.
#define LIGHTNING_BOLT_WAYPOINTS 9
#define LIGHTNING_BOLT_PUSH_COUNT 50 // stays under TRAIL_HISTORY_COUNT (60) so no early point gets evicted
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

// Classic zigzag: alternating perpendicular offset sign per interior
// waypoint (real kinks, not a smooth sine wave), magnitude tapered to zero
// at both endpoints so the bolt starts/ends exactly on start/target. Offset
// is split across two perpendicular axes (not just one) for a bit of true
// 3D irregularity instead of a flat zigzag confined to one plane.
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

// Interpolates a point along the precomputed waypoint polyline at fraction
// f in [0,1] — segment index + local blend, same idea as sampling a spline.
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

    // Bright "hạt" riding the tip — a continuously-refreshed short-lived
    // particle exactly at the current path position (not a separately
    // physics-simulated dot, so it can never drift off the precomputed
    // path the way the old forceField-driven head did).
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

// Sparse-zigzag filter for SpawnLightningFollowerTrail — feeding a smooth
// per-frame path (e.g. an anchor orbiting/sweeping every frame) straight
// into UpdateFollowerPosition records one history node per frame, which is
// a DENSE, smooth curve: visually a wiggly worm, not lightning. Real
// lightning is a handful of long, sharply-angled segments, not many short
// smooth ones. Lightning_UpdateFollowerTip only accepts a new point once the
// caller's real position has moved LIGHTNING_FOLLOWER_MIN_SEGMENT away from
// the last recorded one (few, far-apart points), and inserts one kinked
// midpoint per accepted segment (an actual geometric angle, not physics
// noise) — same "real displacement, not force" philosophy as
// GenerateLightningWaypoints above, just applied incrementally to a caller
// -driven path instead of a precomputed start->target one.
#define LIGHTNING_FOLLOWER_MIN_SEGMENT 45.0f
typedef struct {
    bool active;
    int trailId;
    bool hasLastRecordedPos;
    Vector3 lastRecordedPos;
    float sign;
} LightningFollowerState;
static LightningFollowerState s_lightningFollowers[MAX_CONCURRENT_LIGHTNING_TRAILS];

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
        // Below displacement threshold — no new node, but reset the idle timer so
        // UpdateFollowerPhysics doesn't erase our history while we're waiting for
        // the emitter to travel far enough. Without this, orbital speed ~168 u/s
        // triggers every ~0.21s which exceeds TRAIL_FOLLOWER_IDLE_FADE_TIME (0.15s),
        // causing the trail to be erased faster than it builds.
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

static bool s_helpersInitialized = false;

// Khai báo camera để tính suy giảm rung lắc
extern Camera3D camera;

// Khởi tạo các gradient và trường lực cho Emitter Presets
static void InitHelperResources(void) {
    if (s_helpersInitialized) return;

    // 1. Fire Resources
    s_fireGrad.count = 0;
    ColorGradient_AddStop(&s_fireGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_fireGrad, 0.2f, (Color){ 255, 180, 50, 255 });
    ColorGradient_AddStop(&s_fireGrad, 0.7f, (Color){ 230, 60, 10, 255 });
    ColorGradient_AddStop(&s_fireGrad, 1.0f, (Color){ 30, 30, 30, 0 });

    ForceField_Clear(&s_fireFld);
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 18.0f });
    ForceField_AddLayer(&s_fireFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 10.0f, .noiseScale = 0.08f, .noiseSpeed = 1.5f });

    // 2. Snow Resources
    s_snowGrad.count = 0;
    ColorGradient_AddStop(&s_snowGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_snowGrad, 0.5f, (Color){ 180, 220, 255, 255 });
    ColorGradient_AddStop(&s_snowGrad, 1.0f, (Color){ 150, 200, 255, 0 });

    ForceField_Clear(&s_snowFld);
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.5f, -1.0f, 0.2f}, .strength = 12.0f });
    ForceField_AddLayer(&s_snowFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.1f, .noiseSpeed = 0.8f });

    // 3. Water Resources
    s_waterGrad.count = 0;
    ColorGradient_AddStop(&s_waterGrad, 0.0f, (Color){ 200, 230, 255, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.4f, (Color){ 50, 150, 255, 255 });
    ColorGradient_AddStop(&s_waterGrad, 0.8f, (Color){ 10, 60, 180, 180 });
    ColorGradient_AddStop(&s_waterGrad, 1.0f, (Color){ 5, 20, 100, 0 });

    ForceField_Clear(&s_waterFld);
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 35.0f });
    ForceField_AddLayer(&s_waterFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 15.0f, .noiseScale = 0.05f, .noiseSpeed = 2.0f });

    // 4. Lightning Resources
    s_lightningGrad.count = 0;
    ColorGradient_AddStop(&s_lightningGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_lightningGrad, 0.3f, (Color){ 220, 200, 255, 255 });
    ColorGradient_AddStop(&s_lightningGrad, 0.7f, (Color){ 140, 50, 255, 255 });

    // Follower gradient: head=bright (newest node = current emitter pos), tail=transparent.
    // s_lightningGrad has alpha=0 at segRatio=1 (head) which is correct for the
    // SpawnLightningTrail bolt (advancing tip should look ephemeral), but wrong for
    // SpawnLightningFollowerTrail (the emitter's current position should be the
    // brightest point, with the historical wake fading behind it).
    s_lightningFollowerGrad.count = 0;
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.0f, (Color){ 80, 10, 200, 0 });     // tail: transparent violet
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.25f, (Color){ 140, 50, 255, 180 }); // mid-tail: violet
    ColorGradient_AddStop(&s_lightningFollowerGrad, 0.6f, (Color){ 220, 200, 255, 255 }); // mid-head: bright purple
    ColorGradient_AddStop(&s_lightningFollowerGrad, 1.0f, WHITE);                          // head: white core
    ColorGradient_AddStop(&s_lightningGrad, 1.0f, (Color){ 80, 10, 200, 0 });

    ForceField_Clear(&s_lightningFld);
    ForceField_AddLayer(&s_lightningFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 45.0f, .noiseScale = 0.2f, .noiseSpeed = 5.0f });

    // 5. Wood Resources
    s_woodGrad.count = 0;
    ColorGradient_AddStop(&s_woodGrad, 0.0f, (Color){ 220, 255, 200, 255 });
    ColorGradient_AddStop(&s_woodGrad, 0.4f, ELEMENT_COLOR_WOOD);
    ColorGradient_AddStop(&s_woodGrad, 1.0f, (Color){ 20, 80, 30, 0 });

    ForceField_Clear(&s_woodFld);
    ForceField_AddLayer(&s_woodFld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 10.0f });
    ForceField_AddLayer(&s_woodFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 8.0f, .noiseScale = 0.07f, .noiseSpeed = 1.0f });

    // 6. Earth Resources
    s_earthGrad.count = 0;
    ColorGradient_AddStop(&s_earthGrad, 0.0f, (Color){ 230, 200, 160, 255 });
    ColorGradient_AddStop(&s_earthGrad, 0.5f, ELEMENT_COLOR_EARTH);
    ColorGradient_AddStop(&s_earthGrad, 1.0f, (Color){ 60, 40, 20, 0 });

    ForceField_Clear(&s_earthFld);
    ForceField_AddLayer(&s_earthFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 40.0f });
    ForceField_AddLayer(&s_earthFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 6.0f, .noiseScale = 0.05f, .noiseSpeed = 0.6f });

    // 7. Metal Resources
    s_metalGrad.count = 0;
    ColorGradient_AddStop(&s_metalGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_metalGrad, 0.3f, ELEMENT_COLOR_METAL);
    ColorGradient_AddStop(&s_metalGrad, 1.0f, (Color){ 60, 60, 65, 0 });

    ForceField_Clear(&s_metalFld);
    ForceField_AddLayer(&s_metalFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 25.0f });
    ForceField_AddLayer(&s_metalFld, (ForceLayer){ .type = FORCE_DRAG, .strength = 0.5f });

    // 8. Taiji Resources
    s_taijiGrad.count = 0;
    ColorGradient_AddStop(&s_taijiGrad, 0.0f, WHITE);
    ColorGradient_AddStop(&s_taijiGrad, 0.35f, ELEMENT_COLOR_TAIJI);
    ColorGradient_AddStop(&s_taijiGrad, 1.0f, (Color){ 40, 10, 60, 0 });

    // NOTE: FORCE_VORTEX needs .origin = the actual effect position (it
    // defaults to world origin {0,0,0} otherwise), but this field is shared
    // across all spawn positions — so vortex swirl is centered on world
    // origin, not pos. Using NOISE_CURL only here keeps behavior position-
    // independent and visually correct regardless of where Taiji effects spawn.
    ForceField_Clear(&s_taijiFld);
    ForceField_AddLayer(&s_taijiFld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 18.0f, .noiseScale = 0.12f, .noiseSpeed = 2.5f });

    s_helpersInitialized = true;
}

// 1. Effect Preset Implementation
void SpawnImpactEffect(Vector3 pos, EffectPresetType preset, float scale) {
    InitHelperResources();

    switch (preset) {
        case EFFECT_PRESET_FIRE_EXPLOSION: {
            ScreenDistort_Add(pos, 55.0f * scale, 0.35f, 0.35f, 100.0f);
            CameraFX_Shake(0.38f);
            VFXLight_Spawn(pos, (Color){ 255, 120, 20, 255 }, 65.0f * scale, 0.5f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_BURN, pos, 22.0f * scale, 5.0f);

            // Sinh hạt lửa tung tóe
            int count = (int)(45 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 35 + 20) * scale;
                Vector3 vel = { cosf(a) * speed, (float)(rand() % 45 + 15) * scale, sinf(a) * speed };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.8f + 0.8f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.8f + 0.4f,
                    .gradient = &s_fireGrad,
                    .forceField = &s_fireFld
                });
            }
            break;
        }
        case EFFECT_PRESET_ICE_SHATTER: {
            ScreenDistort_Add(pos, 35.0f * scale, 0.22f, 0.25f, 60.0f);
            CameraFX_Shake(0.20f);
            VFXLight_Spawn(pos, (Color){ 140, 210, 255, 255 }, 45.0f * scale, 0.4f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_ICE, pos, 18.0f * scale, 4.0f);

            int count = (int)(30 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 25 + 10) * scale;
                Vector3 vel = { cosf(a) * speed, (float)(rand() % 30 + 10) * scale, sinf(a) * speed };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.5f + 0.5f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.7f + 0.3f,
                    .gradient = &s_snowGrad,
                    .forceField = &s_snowFld
                });
            }
            break;
        }
        case EFFECT_PRESET_WATER_SPLASH: {
            VFXLight_Spawn(pos, (Color){ 80, 180, 255, 255 }, 40.0f * scale, 0.45f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_WATER, pos, 15.0f * scale, 3.5f);

            int count = (int)(35 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 30 + 15) * scale;
                Vector3 vel = { cosf(a) * speed * 0.7f, (float)(rand() % 50 + 20) * scale, sinf(a) * speed * 0.7f };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 2.0f + 0.6f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.9f + 0.3f,
                    .gradient = &s_waterGrad,
                    .forceField = &s_waterFld
                });
            }
            break;
        }
        case EFFECT_PRESET_LIGHTNING_IMPACT: {
            ScreenDistort_Add(pos, 45.0f * scale, 0.4f, 0.2f, 150.0f);
            CameraFX_Shake(0.35f);
            VFXLight_Spawn(pos, (Color){ 200, 150, 255, 255 }, 55.0f * scale, 0.35f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_TAIJI_LIGHTNING, pos, 14.0f * scale, 3.0f);

            int count = (int)(25 * scale);
            for (int i = 0; i < count; i++) {
                Vector3 vel = { ((float)rand() / (float)RAND_MAX - 0.5f) * 65.0f * scale,
                                ((float)rand() / (float)RAND_MAX) * 75.0f * scale,
                                ((float)rand() / (float)RAND_MAX - 0.5f) * 65.0f * scale };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.4f + 0.4f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.5f + 0.25f,
                    .gradient = &s_lightningGrad,
                    .forceField = &s_lightningFld
                });
            }
            break;
        }
        case EFFECT_PRESET_EARTH_CRACK: {
            ScreenDistort_Add(pos, 30.0f * scale, 0.25f, 0.4f, 50.0f);
            CameraFX_Shake(0.42f);
            VFXLight_Spawn(pos, (Color){ 180, 140, 100, 255 }, 35.0f * scale, 0.6f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_CRACK, pos, 25.0f * scale, 5.5f);
            break;
        }
        case EFFECT_PRESET_WOOD_BLOOM: {
            VFXLight_Spawn(pos, ELEMENT_COLOR_WOOD, 35.0f * scale, 0.45f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_WOOD_MOSS, pos, 18.0f * scale, 5.0f);

            // Leaf/vine burst, upward-biased
            int count = (int)(30 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 20 + 8) * scale;
                Vector3 vel = { cosf(a) * speed, (float)(rand() % 55 + 25) * scale, sinf(a) * speed };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.6f + 0.6f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 1.0f + 0.5f,
                    .gradient = &s_woodGrad,
                    .forceField = &s_woodFld
                });
            }
            break;
        }
        case EFFECT_PRESET_METAL_SHARD: {
            ScreenDistort_Add(pos, 25.0f * scale, 0.18f, 0.15f, 130.0f);
            CameraFX_Shake(0.22f);
            VFXLight_Spawn(pos, ELEMENT_COLOR_METAL, 30.0f * scale, 0.2f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_METAL_SLASH, pos, 16.0f * scale, 4.0f);

            // Sharp shard particles, high pitch range, metallic
            int count = (int)(28 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 60 + 35) * scale;
                float pitch = ((float)rand() / (float)RAND_MAX - 0.5f) * 80.0f * scale;
                Vector3 vel = { cosf(a) * speed, pitch, sinf(a) * speed };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.0f + 0.3f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.4f + 0.2f,
                    .gradient = &s_metalGrad,
                    .forceField = &s_metalFld
                });
            }
            break;
        }
        case EFFECT_PRESET_TAIJI_BURST: {
            ScreenDistort_Add(pos, 50.0f * scale, 0.3f, 0.3f, 90.0f);
            CameraFX_Shake(0.3f);
            VFXLight_Spawn(pos, ELEMENT_COLOR_TAIJI, 70.0f * scale, 0.55f, VFX_PRIORITY_LOW);
            SpawnGroundDecal(DECAL_PRESET_TAIJI_RING, pos, 22.0f * scale, 5.0f);

            // Amethyst-purple radial burst
            int count = (int)(40 * scale);
            for (int i = 0; i < count; i++) {
                float a = (float)i / count * 2.0f * PI;
                float speed = (float)(rand() % 40 + 20) * scale;
                Vector3 vel = { cosf(a) * speed, (float)(rand() % 40 + 15) * scale, sinf(a) * speed };
                SpawnParticle((ParticleConfig){
                    .position = pos,
                    .velocity = vel,
                    .radius = ((float)rand() / (float)RAND_MAX * 1.8f + 0.7f) * scale,
                    .lifetime = (float)rand() / (float)RAND_MAX * 0.9f + 0.4f,
                    .gradient = &s_taijiGrad,
                    .forceField = &s_taijiFld
                });
            }
            break;
        }
    }
}

// Cast/windup Implementation — sustained "energy gathering" at the caster.
// No knockback, no ground decal. Inward-pulling particles via a localized
// gravity-point force field, plus a brief light flash.
void SpawnCastEffect(Vector3 pos, EffectPresetType preset, float scale) {
    InitHelperResources();

    Color flashColor = WHITE;
    ColorGradient *grad = NULL;
    float lightRadius = 40.0f * scale;
    float lightLifetime = 0.5f;
    int count = (int)(20 * scale);
    float spawnRadius = 18.0f * scale;
    float pullStrength = 60.0f * scale;

    switch (preset) {
        case EFFECT_PRESET_FIRE_EXPLOSION:
            flashColor = (Color){ 255, 120, 20, 255 };
            grad = &s_fireGrad;
            break;
        case EFFECT_PRESET_ICE_SHATTER:
            flashColor = (Color){ 140, 210, 255, 255 };
            grad = &s_snowGrad;
            break;
        case EFFECT_PRESET_WATER_SPLASH:
            flashColor = (Color){ 80, 180, 255, 255 };
            grad = &s_waterGrad;
            break;
        case EFFECT_PRESET_LIGHTNING_IMPACT:
            flashColor = (Color){ 200, 150, 255, 255 };
            grad = &s_lightningGrad;
            break;
        case EFFECT_PRESET_EARTH_CRACK:
            flashColor = ELEMENT_COLOR_EARTH;
            grad = &s_earthGrad;
            break;
        case EFFECT_PRESET_WOOD_BLOOM:
            flashColor = ELEMENT_COLOR_WOOD;
            grad = &s_woodGrad;
            break;
        case EFFECT_PRESET_METAL_SHARD:
            flashColor = ELEMENT_COLOR_METAL;
            grad = &s_metalGrad;
            break;
        case EFFECT_PRESET_TAIJI_BURST:
            flashColor = ELEMENT_COLOR_TAIJI;
            grad = &s_taijiGrad;
            lightRadius = 55.0f * scale; // stronger flash for the "no-element" ultimate state
            lightLifetime = 0.65f;
            break;
    }

    VFXLight_Spawn(pos, flashColor, lightRadius, lightLifetime, VFX_PRIORITY_LOW);

    // Localized inward-pulling force field — particles spawn at a ring around
    // pos and get sucked back toward it, reading as "energy gathering".
    // Each call claims the next pool slot round-robin (particles store a
    // pointer to it, so it can't be stack-local) so concurrent casts at
    // different positions don't interfere with each other.
    ForceField *castPullFld = &s_castPullFlds[s_castPullFldNextSlot];
    s_castPullFldNextSlot = (s_castPullFldNextSlot + 1) % MAX_CONCURRENT_CAST_EFFECTS;
    ForceField_Clear(castPullFld);
    ForceField_AddLayer(castPullFld, (ForceLayer){ .type = FORCE_GRAVITY_POINT, .origin = pos, .strength = pullStrength, .radius = spawnRadius * 1.5f, .falloff = 1.0f });

    for (int i = 0; i < count; i++) {
        float a = (float)i / count * 2.0f * PI;
        float r = spawnRadius * (0.6f + 0.4f * ((float)rand() / (float)RAND_MAX));
        Vector3 spawnPos = { pos.x + cosf(a) * r, pos.y + ((float)rand() / (float)RAND_MAX) * spawnRadius * 0.5f, pos.z + sinf(a) * r };
        SpawnParticle((ParticleConfig){
            .position = spawnPos,
            .velocity = (Vector3){ 0.0f, 0.0f, 0.0f },
            .radius = ((float)rand() / (float)RAND_MAX * 1.2f + 0.5f) * scale,
            .lifetime = (float)rand() / (float)RAND_MAX * 0.5f + 0.4f,
            .gradient = grad,
            .forceField = castPullFld
        });
    }
}

// Flight-stage implementation — projectile trail + continuous tail dust
// while a skill flies from start to target. Force magnitude here is the
// sustained/flight regime (300-650f), NOT the ambient/burst regime used by
// Cast/Impact above — see CORE_API.md §1. Matches the water_stream
// tube_skill.c precedent: FORCE_GRAVITY_DIR strength ~300-650f primary pull,
// FORCE_NOISE_PERLIN/CURL ~15-25f secondary wobble.
int SpawnProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed) {
    InitHelperResources();

    ColorGradient *grad = NULL;
    Color tint = WHITE;

    switch (preset) {
        case EFFECT_PRESET_FIRE_EXPLOSION:   grad = &s_fireGrad;      tint = (Color){ 255, 120, 20, 255 };  break;
        case EFFECT_PRESET_ICE_SHATTER:      grad = &s_snowGrad;      tint = (Color){ 140, 210, 255, 255 }; break;
        case EFFECT_PRESET_WATER_SPLASH:     grad = &s_waterGrad;     tint = (Color){ 80, 180, 255, 255 };  break;
        case EFFECT_PRESET_LIGHTNING_IMPACT: grad = &s_lightningGrad; tint = (Color){ 200, 150, 255, 255 }; break;
        case EFFECT_PRESET_EARTH_CRACK:      grad = &s_earthGrad;     tint = ELEMENT_COLOR_EARTH;           break;
        case EFFECT_PRESET_WOOD_BLOOM:       grad = &s_woodGrad;      tint = ELEMENT_COLOR_WOOD;            break;
        case EFFECT_PRESET_METAL_SHARD:      grad = &s_metalGrad;     tint = ELEMENT_COLOR_METAL;           break;
        case EFFECT_PRESET_TAIJI_BURST:      grad = &s_taijiGrad;     tint = ELEMENT_COLOR_TAIJI;           break;
    }

    // Directional flight force: primary pull toward target (sustained
    // regime, 300-650f) + a smaller curl-noise wobble layer (~15-25f) for
    // visual flutter, matching tube_skill.c.
    ForceField *flightFld = &s_flightFlds[s_flightFldNextSlot];
    s_flightFldNextSlot = (s_flightFldNextSlot + 1) % MAX_CONCURRENT_PROJECTILE_TRAILS;
    ForceField_Clear(flightFld);
    Vector3 dir = Vector3Normalize(Vector3Subtract(target, start));
    ForceField_AddLayer(flightFld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = dir, .strength = 325.0f });
    ForceField_AddLayer(flightFld, (ForceLayer){ .type = FORCE_NOISE_PERLIN, .strength = 20.0f, .noiseScale = 0.08f, .noiseSpeed = 2.0f });

    // Tail-dust sub-emitter: spawned once on the head particle, continuously
    // emits trailing particles for as long as the head particle lives.
    static ParticleConfig s_tailEmit;
    s_tailEmit = (ParticleConfig){
        .radius = 1.0f * scale,
        .lifetime = 0.4f,
        .gradient = grad,
        .forceField = flightFld
    };

    SpawnParticle((ParticleConfig){
        .position = start,
        .velocity = Vector3Scale(dir, speed),
        .colorStart = tint,
        .colorEnd = tint,
        .radius = 1.6f * scale,
        .lifetime = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.5f,
        .gradient = grad,
        .forceField = flightFld,
        .onLiveEmit = &s_tailEmit,
        .onLiveEmitRate = 40.0f
    });

    TrailConfig cfg = {
        .type = TRAIL_TYPE_PROJECTILE,
        .pos = start,
        .vel = Vector3Scale(dir, speed),
        .len = 14.0f * scale,
        .thick = 3.0f * scale,
        .trailLength = 60.0f * scale,
        .life = Vector3Distance(start, target) / fmaxf(speed, 1.0f) + 0.5f,
        .target = target,
        .scale = scale,
        .tint = tint,
        .forceField = flightFld,
        .gradient = grad
    };
    return SpawnTrailEntity(cfg);
}

// onUpdate callback shared by both lightning presets — reads live
// position/scale/tint off the trail itself (TrailEntity, not captured
// state), so one function pointer works for every concurrent bolt. Spawns a
// short-lived point light at ~10/sec average, framerate-independent via
// dt-scaled probability, for the characteristic electric flicker.
static void LightningTrailFlicker(int trailId, float dt) {
    TrailEntity *t = GetTrail(trailId);
    if (!t || !t->active) return;
    if ((float)rand() / (float)RAND_MAX < dt * 10.0f) {
        VFXLight_Spawn(t->position, t->tint, 35.0f * t->scale, 0.08f, VFX_PRIORITY_LOW);
    }
}

int SpawnLightningTrail(Vector3 start, Vector3 target, float scale, float speed) {
    InitHelperResources();

    float boltLen = Vector3Distance(start, target);
    float jaggedAmount = fmaxf(30.0f, boltLen * 0.08f) * scale;
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
    InitHelperResources();

    // Light noise for a subtle crackle on top of whatever real path the
    // caller feeds via Lightning_UpdateFollowerTip — that function (not raw
    // UpdateFollowerPosition) is what supplies the actual sparse zigzag
    // shape now (see its comment above), so this forceField no longer has
    // to invent jaggedness from scratch.
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
        .trailLength = 55.0f, // FOLLOWER: integer history-node count, not a fractional ratio. Room for many sparse kink+endpoint pairs before the TRAIL_HISTORY_COUNT(60) cap.
        .life = life,
        .scale = scale,
        .tint = (Color){ 220, 200, 255, 255 },
        .forceField = followerFld,
        .gradient = &s_lightningFollowerGrad,
        .onUpdate = LightningTrailFlicker
    };
    int trailId = SpawnTrailEntity(cfg);
    st->trailId = trailId; // slot was claimed above (round-robin with followerFld), stays valid whether or not the spawn itself succeeded (-1 just means Lightning_UpdateFollowerTip's lookup never matches)
    return trailId;
}

// Stateless immediate-mode lightning bolt renderer -------------------------
// Generates a jagged bolt shape (9 waypoints) from `from` to `to`. Call
// periodically (every 0.04–0.07 s) and the changing random waypoints produce
// the characteristic lightning flicker without any trail pool overhead.
void RegenerateLightningWaypoints(Vector3 *waypoints9, Vector3 from, Vector3 to, float scale) {
    InitHelperResources();
    float boltLen = Vector3Distance(from, to);
    float jaggedAmount = boltLen * 0.14f * fmaxf(scale, 0.1f);
    GenerateLightningWaypoints(waypoints9, 9, from, to, jaggedAmount);
}

#ifndef PI
#define PI 3.1415926535f
#endif

void RegenerateLightningRay(Vector3 *waypoints9, Vector3 origin, Vector3 direction,
                             float length, float phase, float amplitude, float scale) {
    InitHelperResources();

    // Build perpendicular basis from direction
    Vector3 dir = Vector3Normalize(direction);
    Vector3 refUp = (fabsf(dir.y) > 0.95f) ? (Vector3){1.0f, 0.0f, 0.0f} : (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 perp1 = Vector3Normalize(Vector3CrossProduct(dir, refUp));
    Vector3 perp2 = Vector3Normalize(Vector3CrossProduct(dir, perp1));

    float amp = amplitude * fmaxf(scale, 0.1f);

    for (int i = 0; i < 9; i++) {
        float t = (float)i / 8.0f; // 0 at origin, 1 at free end
        Vector3 base = Vector3Add(origin, Vector3Scale(dir, t * length));

        if (i == 0) {
            // Origin is the only fixed point
            waypoints9[0] = origin;
            continue;
        }

        // Envelope: 0 at start, grows to 1 at free end — power controls how fast it blooms
        float envelope = powf(t, 1.5f);

        // Two traveling waves for 3D whip motion — 1 cycle keeps free end from wrapping back
        float wR = sinf(phase + t * PI * 2.0f);
        float wU = cosf(phase * 0.7f + t * PI * 1.5f);

        // Small per-frame random jitter for the electric crackle on top of the wave
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

// Draw two-pass bolt (glow + core) via DrawRibbonStrip.
// Caller must wrap in BeginBlendMode(BLEND_ADDITIVE) / EndBlendMode.
void DrawLightningBolt(const Vector3 *waypoints9, float thickness, Camera3D cam) {
    for (int pass = 0; pass < 2; pass++) {
        float w       = (pass == 0) ? thickness * 1.6f : thickness;
        unsigned char r = (pass == 0) ? 100 : 230;
        unsigned char g = (pass == 0) ?  20 : 210;
        unsigned char b = (pass == 0) ? 255 : 255;
        unsigned char a = (pass == 0) ? 130 : 255;

        for (int k = 0; k < LIGHTNING_BOLT_RIBBON_PTS; k++) {
            float f = (float)k / (float)(LIGHTNING_BOLT_RIBBON_PTS - 1);
            Vector3 pt = SampleLightningPath(waypoints9, 9, f);

            // Symmetric smoothstep taper — both endpoints fade to 0
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

// Audio Preset Implementation — no real per-element SFX assets exist under
// assets/ yet (verified: no .wav/.ogg files anywhere in assets/), so these
// are stub/warning-only for now. Each preset warns once (not every call) via
// a static "already warned" flag, then returns without playing or crashing.
// Once real asset files land (e.g. assets/sounds/fire_cast.wav), replace the
// TraceLog branch below with ResourceManager_LoadSound(path) + PlaySound().
static bool s_castSoundWarned[8] = { false };
static bool s_impactSoundWarned[8] = { false };

void PlayCastSound(EffectPresetType preset) {
    if (preset < 0 || preset >= 8) return;
    if (!s_castSoundWarned[preset]) {
        s_castSoundWarned[preset] = true;
        TraceLog(LOG_WARNING, "AUDIO: no cast SFX asset for EffectPresetType %d yet (stub, not crashing)", preset);
    }
    // TODO: once assets/sounds/*.wav exist per element, load via
    // ResourceManager_LoadSound(path) and PlaySound() here.
}

void PlayImpactSound(EffectPresetType preset) {
    if (preset < 0 || preset >= 8) return;
    if (!s_impactSoundWarned[preset]) {
        s_impactSoundWarned[preset] = true;
        TraceLog(LOG_WARNING, "AUDIO: no impact SFX asset for EffectPresetType %d yet (stub, not crashing)", preset);
    }
    // TODO: once assets/sounds/*.wav exist per element, load via
    // ResourceManager_LoadSound(path) and PlaySound() here.
}

// 2. Damage Volume Implementation
void DamageVolume_Init(void) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        s_volumes[i].active = false;
    }
}

void DamageVolume_Update(float dt) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (!s_volumes[i].active) continue;

        s_volumes[i].timer += dt;
        s_volumes[i].tickTimer += dt;

        if (s_volumes[i].tickTimer >= s_volumes[i].tickInterval) {
            s_volumes[i].tickTimer = 0.0f;
            float damage = s_volumes[i].damagePerSecond * s_volumes[i].tickInterval;
            ApplyAoEDamage(s_volumes[i].center, s_volumes[i].radius, damage, 0.0f);
        }

        if (s_volumes[i].timer >= s_volumes[i].duration) {
            s_volumes[i].active = false;
        }
    }
}

int SpawnDamageVolume(DamageVolume config) {
    for (int i = 0; i < MAX_VOLUMES; i++) {
        if (!s_volumes[i].active) {
            s_volumes[i] = config;
            s_volumes[i].timer = 0.0f;
            s_volumes[i].tickTimer = 0.0f;
            s_volumes[i].active = true;
            return i;
        }
    }
    return -1;
}

void DamageVolume_Unload(void) {
    DamageVolume_Init();
}

// 3. Skill Timeline Implementation
void Timeline_Start(SkillTimeline *t, float duration) {
    t->current = 0.0f;
    t->duration = duration;
}

bool Timeline_Event(SkillTimeline *t, float triggerTime, float dt) {
    return (t->current - dt < triggerTime) && (t->current >= triggerTime);
}

bool Timeline_Finished(SkillTimeline *t) {
    return t->current >= t->duration;
}

// 3b. Layered Timeline Implementation
void Timeline_LayeredStart(LayeredTimeline *t) {
    t->current = 0.0f;
    t->layerCount = 0;
}

bool Timeline_AddLayer(LayeredTimeline *t, const char *tag, float start, float duration) {
    if (t->layerCount >= TIMELINE_MAX_LAYERS)
        return false;
    t->layers[t->layerCount].tag = tag;
    t->layers[t->layerCount].start = start;
    t->layers[t->layerCount].duration = duration;
    t->layerCount++;
    return true;
}

bool Timeline_IsLayerActive(const LayeredTimeline *t, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= t->layerCount)
        return false;
    const TimelineLayer *layer = &t->layers[layerIndex];
    return (t->current >= layer->start) && (t->current < layer->start + layer->duration);
}

float Timeline_LayerProgress(const LayeredTimeline *t, int layerIndex) {
    if (layerIndex < 0 || layerIndex >= t->layerCount)
        return 0.0f;
    const TimelineLayer *layer = &t->layers[layerIndex];
    if (layer->duration <= 0.0f)
        return (t->current >= layer->start) ? 1.0f : 0.0f;
    return Clamp((t->current - layer->start) / layer->duration, 0.0f, 1.0f);
}

bool Timeline_LayerEvent(const LayeredTimeline *t, int layerIndex, float dt) {
    if (layerIndex < 0 || layerIndex >= t->layerCount)
        return false;
    float triggerTime = t->layers[layerIndex].start;
    return (t->current - dt < triggerTime) && (t->current >= triggerTime);
}

// 4. Particle Emitter System Implementation
void EmitterSystem_Init(void) {
    InitHelperResources();
    for (int i = 0; i < MAX_EMITTERS; i++) {
        s_emitters[i].active = false;
    }
}

void EmitterSystem_Update(float dt) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!s_emitters[i].active) continue;

        s_emitters[i].timer += dt;
        if (s_emitters[i].timer >= s_emitters[i].duration) {
            s_emitters[i].active = false;
            continue;
        }

        s_emitters[i].spawnAccum += s_emitters[i].rate * dt;
        int count = (int)s_emitters[i].spawnAccum;
        s_emitters[i].spawnAccum -= count;

        ColorGradient *grad = NULL;
        ForceField *fld = NULL;

        switch (s_emitters[i].type) {
            case EMITTER_FIRE: grad = &s_fireGrad; fld = &s_fireFld; break;
            case EMITTER_SNOW: grad = &s_snowGrad; fld = &s_snowFld; break;
            case EMITTER_WATER_SPURT: grad = &s_waterGrad; fld = &s_waterFld; break;
            case EMITTER_SHOCKED_SPARKS: grad = &s_lightningGrad; fld = &s_lightningFld; break;
            case EMITTER_WOOD_LEAVES: grad = &s_woodGrad; fld = &s_woodFld; break;
            case EMITTER_EARTH_DUST: grad = &s_earthGrad; fld = &s_earthFld; break;
            case EMITTER_METAL_SPARKS: grad = &s_metalGrad; fld = &s_metalFld; break;
            case EMITTER_TAIJI_MOTES: grad = &s_taijiGrad; fld = &s_taijiFld; break;
        }

        for (int k = 0; k < count; k++) {
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f
            };
            Vector3 pos = Vector3Add(s_emitters[i].pos, offset);
            Vector3 vel = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
                ((float)rand() / (float)RAND_MAX * 15.0f + 10.0f),
                ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f
            };

            SpawnParticle((ParticleConfig){
                .position = pos,
                .velocity = vel,
                .radius = (float)GetRandomValue(8, 20) / 10.0f,
                .lifetime = (float)GetRandomValue(5, 12) / 10.0f,
                .gradient = grad,
                .forceField = fld
            });
        }
    }
}

int Emitter_AttachToPoint(EmitterPreset type, Vector3 pos, float ratePerSecond, float duration) {
    for (int i = 0; i < MAX_EMITTERS; i++) {
        if (!s_emitters[i].active) {
            s_emitters[i].type = type;
            s_emitters[i].pos = pos;
            s_emitters[i].rate = ratePerSecond;
            s_emitters[i].duration = duration;
            s_emitters[i].timer = 0.0f;
            s_emitters[i].spawnAccum = 0.0f;
            s_emitters[i].active = true;
            return i;
        }
    }
    return -1;
}

void Emitter_Stop(int emitterId) {
    if (emitterId >= 0 && emitterId < MAX_EMITTERS) {
        s_emitters[emitterId].active = false;
    }
}

void EmitterSystem_Unload(void) {
    EmitterSystem_Init();
}

static void DrawCorePyramid(Vector3 pos, float radius, float height, Color color) {
    Vector3 top = { pos.x, pos.y + height, pos.z };
    Vector3 base[4];
    for (int i = 0; i < 4; i++) {
        float angle = i * 0.5f * 3.14159265f;
        base[i] = (Vector3){ pos.x + cosf(angle) * radius, pos.y, pos.z + sinf(angle) * radius };
    }

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        
        // Tính face normal cho mặt bên
        Vector3 edge1 = Vector3Subtract(base[next], base[i]);
        Vector3 edge2 = Vector3Subtract(top, base[i]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        
        rlNormal3f(normal.x, normal.y, normal.z);
        rlVertex3f(base[i].x, base[i].y, base[i].z);
        rlVertex3f(base[next].x, base[next].y, base[next].z);
        rlVertex3f(top.x, top.y, top.z);
    }
    
    // Đáy hình vuông (2 tam giác)
    Vector3 edgeB1 = Vector3Subtract(base[1], base[0]);
    Vector3 edgeB2 = Vector3Subtract(base[3], base[0]);
    Vector3 baseNormal = Vector3Normalize(Vector3CrossProduct(edgeB2, edgeB1)); // Hướng xuống dưới
    rlNormal3f(baseNormal.x, baseNormal.y, baseNormal.z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlVertex3f(base[1].x, base[1].y, base[1].z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[3].x, base[3].y, base[3].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlEnd();
}

static void DrawCoreTetrahedron(Vector3 pos, float radius, float height, Color color) {
    Vector3 top = { pos.x, pos.y + height, pos.z };
    Vector3 base[3];
    for (int i = 0; i < 3; i++) {
        float angle = i * (2.0f * 3.14159265f / 3.0f);
        base[i] = (Vector3){ pos.x + cosf(angle) * radius, pos.y, pos.z + sinf(angle) * radius };
    }

    rlColor4ub(color.r, color.g, color.b, color.a);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 3; i++) {
        int next = (i + 1) % 3;
        
        // Tính face normal cho mặt bên
        Vector3 edge1 = Vector3Subtract(base[next], base[i]);
        Vector3 edge2 = Vector3Subtract(top, base[i]);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));
        
        rlNormal3f(normal.x, normal.y, normal.z);
        rlVertex3f(base[i].x, base[i].y, base[i].z);
        rlVertex3f(base[next].x, base[next].y, base[next].z);
        rlVertex3f(top.x, top.y, top.z);
    }
    
    // Đáy tam giác
    Vector3 edgeB1 = Vector3Subtract(base[1], base[0]);
    Vector3 edgeB2 = Vector3Subtract(base[2], base[0]);
    Vector3 baseNormal = Vector3Normalize(Vector3CrossProduct(edgeB2, edgeB1));
    rlNormal3f(baseNormal.x, baseNormal.y, baseNormal.z);
    
    rlVertex3f(base[0].x, base[0].y, base[0].z);
    rlVertex3f(base[2].x, base[2].y, base[2].z);
    rlVertex3f(base[1].x, base[1].y, base[1].z);
    rlEnd();
}

// 5. Mesh Preset Implementation
void DrawEffectMesh(MeshPresetType type, Vector3 pos, Vector3 scale, Color color) {
    // Đảm bảo reset vertex color để shader chạy đúng
    rlColor4ub(color.r, color.g, color.b, color.a);

    switch (type) {
        case MESH_PRESET_DISC:
            DrawCorePlanePolygon(pos, scale.x, 24, color);
            break;
        case MESH_PRESET_RING:
            DrawCoreTorus(pos, scale.x * 0.85f, scale.x, 16, 24, color);
            break;
        case MESH_PRESET_CONE:
            DrawCoreCone(pos, scale.x, scale.y, 16, color);
            break;
        case MESH_PRESET_TORNADO: {
            for (int i = 0; i < 3; i++) {
                float r = scale.x * (1.0f + i * 0.15f);
                DrawCoreCylinder(pos, (Vector3){pos.x, pos.y + scale.y, pos.z}, r * 0.5f, r, 12, color);
            }
            break;
        }
        case MESH_PRESET_CYLINDER:
            DrawCoreCylinder(pos, (Vector3){pos.x, pos.y + scale.y, pos.z}, scale.x, scale.x, 16, color);
            break;
        case MESH_PRESET_SPHERE:
            DrawCoreSphere(pos, scale.x, 16, 16, color);
            break;
        case MESH_PRESET_SHOCKWAVE:
            DrawCoreTorus(pos, scale.x * 0.92f, scale.x, 8, 24, color);
            break;
        case MESH_PRESET_PYRAMID:
            DrawCorePyramid(pos, scale.x, scale.y, color);
            break;
        case MESH_PRESET_TETRAHEDRON:
            DrawCoreTetrahedron(pos, scale.x, scale.y, color);
            break;
    }
}

// 6. Shader Material System Implementation
static void Material_FetchLocs(EffectMaterial *mat) {
    mat->uTimeLoc = GetShaderLocation(mat->shader, "u_time");
    mat->uDissolveLoc = GetShaderLocation(mat->shader, "u_dissolve");
    mat->uBaseColorLoc = GetShaderLocation(mat->shader, "u_baseColor");
    mat->uTranslucencyLoc = GetShaderLocation(mat->shader, "u_translucency");
    mat->uRimStrengthLoc = GetShaderLocation(mat->shader, "u_rimStrength");
    mat->uFresnelPowerLoc = GetShaderLocation(mat->shader, "u_fresnelPower");
    mat->uEmissiveIntensityLoc = GetShaderLocation(mat->shader, "u_emissiveIntensity");
    mat->uDistortionStrengthLoc = GetShaderLocation(mat->shader, "u_distortionStrength");
    mat->uHasTexture1Loc = GetShaderLocation(mat->shader, "u_hasTexture1");
    mat->uTexture1Loc = GetShaderLocation(mat->shader, "texture1");
}

EffectMaterial Material_Load(MaterialPreset preset) {
    EffectMaterial mat = {0};
    mat.preset = preset;

    switch (preset) {
        case MATERIAL_FIRE:
            mat.shader = ResourceManager_LoadShader("skills/fire/fire_wildfire/fire_wildfire.vs",
                                                   "skills/fire/fire_wildfire/fire_wildfire.fs");
            break;
        case MATERIAL_ICE:
            mat.shader = ResourceManager_LoadShader("skills/water/frost_blossom_rain_skill/frost_blossom_rain.vs",
                                                   "skills/water/frost_blossom_rain_skill/frost_blossom_rain.fs");
            break;
        case MATERIAL_WATER:
            mat.shader = ResourceManager_LoadShader("skills/water/water_stream/tube.vs",
                                                   "skills/water/water_stream/tube.fs");
            break;
        case MATERIAL_PORTAL:
            mat.shader = ResourceManager_LoadShader("skills/taiji/yin_yang_orb/yin_yang_orb.vs",
                                                   "skills/taiji/yin_yang_orb/yin_yang_orb.fs");
            break;
        case MATERIAL_CUSTOM:
            // Legal but not the intended entry point — Material_LoadCustom() is how a
            // skill actually configures this preset. Falls back to all-zero params.
            mat.shader = ResourceManager_LoadShader("core/shaders/effect_material.vs",
                                                   "core/shaders/effect_material.fs");
            break;
    }

    Material_FetchLocs(&mat);
    return mat;
}

EffectMaterial Material_LoadCustom(EffectMaterialParams params) {
    EffectMaterial mat = {0};
    mat.preset = MATERIAL_CUSTOM;
    mat.shader = ResourceManager_LoadShader("core/shaders/effect_material.vs",
                                           "core/shaders/effect_material.fs");
    Material_FetchLocs(&mat);
    mat.params = params;
    return mat;
}

void Material_SetFloat(EffectMaterial *mat, const char *uniformName, float val) {
    int loc = GetShaderLocation(mat->shader, uniformName);
    if (loc >= 0) {
        SetShaderValue(mat->shader, loc, &val, SHADER_UNIFORM_FLOAT);
    }
}

void Material_Begin(EffectMaterial mat) {
    SkillManager_BeginShader(mat.shader);
    float time = (float)GetTime();
    if (mat.uTimeLoc >= 0) {
        SetShaderValue(mat.shader, mat.uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    }
    if (mat.uBaseColorLoc >= 0) {
        Vector4 baseColorVec = ColorNormalize(mat.params.baseColor);
        SetShaderValue(mat.shader, mat.uBaseColorLoc, &baseColorVec, SHADER_UNIFORM_VEC4);
    }
    if (mat.uTranslucencyLoc >= 0)
        SetShaderValue(mat.shader, mat.uTranslucencyLoc, &mat.params.translucency, SHADER_UNIFORM_FLOAT);
    if (mat.uRimStrengthLoc >= 0)
        SetShaderValue(mat.shader, mat.uRimStrengthLoc, &mat.params.rimStrength, SHADER_UNIFORM_FLOAT);
    if (mat.uFresnelPowerLoc >= 0)
        SetShaderValue(mat.shader, mat.uFresnelPowerLoc, &mat.params.fresnelPower, SHADER_UNIFORM_FLOAT);
    if (mat.uEmissiveIntensityLoc >= 0)
        SetShaderValue(mat.shader, mat.uEmissiveIntensityLoc, &mat.params.emissiveIntensity, SHADER_UNIFORM_FLOAT);
    if (mat.uDistortionStrengthLoc >= 0)
        SetShaderValue(mat.shader, mat.uDistortionStrengthLoc, &mat.params.distortionStrength, SHADER_UNIFORM_FLOAT);
    if (mat.uHasTexture1Loc >= 0) {
        int hasTexture1 = mat.params.texture1.id != 0;
        SetShaderValue(mat.shader, mat.uHasTexture1Loc, &hasTexture1, SHADER_UNIFORM_INT);
    }
    if (mat.uTexture1Loc >= 0 && mat.params.texture1.id != 0) {
        SetShaderValueTexture(mat.shader, mat.uTexture1Loc, mat.params.texture1);
    }
}

void Material_End(void) {
    SkillManager_EndShader();
}

// 7. Ground Decal Implementation
void SpawnGroundDecal(DecalPresetType type, Vector3 pos, float radius, float duration) {
    Texture2D tex = {0};
    Color tint = WHITE;

    switch (type) {
        // Earth
        case DECAL_PRESET_CRACK:
            tex = ResourceManager_LoadTexture("assets/textures/crack.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.7f);
            break;
        case DECAL_PRESET_EARTH_SHATTER:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_stone_shatter.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.75f);
            break;
        case DECAL_PRESET_EARTH_RUNE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_earth_rune.png");
            tint = ColorAlpha(ELEMENT_COLOR_EARTH, 0.85f);
            break;

        // Fire
        case DECAL_PRESET_BURN:
            tex = ResourceManager_LoadTexture("assets/textures/scorch_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.65f);
            break;
        case DECAL_PRESET_FIRE_LAVA:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lava_crack.png");
            tint = ColorAlpha(ELEMENT_COLOR_FIRE, 0.8f);
            break;

        // Water
        case DECAL_PRESET_WATER:
            tex = ResourceManager_LoadTexture("assets/textures/water_caustics.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
            break;
        case DECAL_PRESET_WATER_SPLASH:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_splash_ring.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.6f);
            break;
        case DECAL_PRESET_WATER_RIPPLE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_water_ripple.png");
            tint = ColorAlpha(ELEMENT_COLOR_WATER, 0.5f);
            break;
        case DECAL_PRESET_ICE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_frost_ring.png");
            tint = ColorAlpha(WHITE, 0.55f);
            break;

        // Wood
        case DECAL_PRESET_WOOD_ROOT:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_root_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.75f);
            break;
        case DECAL_PRESET_WOOD_MOSS:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_moss_stain.png");
            tint = ColorAlpha(ELEMENT_COLOR_WOOD, 0.6f);
            break;

        // Metal
        case DECAL_PRESET_METAL_SLASH:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_slash_mark.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.8f);
            break;
        case DECAL_PRESET_METAL_CRATER:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_impact_crater.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.75f);
            break;
        case DECAL_PRESET_METAL_RUNE:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_metal_rune.png");
            tint = ColorAlpha(ELEMENT_COLOR_METAL, 0.85f);
            break;

        // Taiji
        case DECAL_PRESET_TAIJI_RING:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_taiji_ring.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.8f);
            break;
        case DECAL_PRESET_TAIJI_LIGHTNING:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_lightning_char.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.85f);
            break;
        case DECAL_PRESET_TAIJI_WIND:
            tex = ResourceManager_LoadTexture("assets/textures/decals/decal_wind_groove.png");
            tint = ColorAlpha(ELEMENT_COLOR_TAIJI, 0.5f);
            break;

        // Generic — no element tint baked in, caller adjusts via radius/duration only
        case DECAL_PRESET_GENERIC_IMPACT_RING:
            tex = ResourceManager_LoadTexture("assets/textures/generic/impact_ring.png");
            tint = ColorAlpha(WHITE, 0.7f);
            break;
        case DECAL_PRESET_GENERIC_GLOW:
            tex = ResourceManager_LoadTexture("assets/textures/generic/glow_circle.png");
            tint = ColorAlpha(WHITE, 0.4f);
            break;
        case DECAL_PRESET_GENERIC_SHADOW:
            tex = ResourceManager_LoadTexture("assets/textures/generic/shadow_blob.png");
            tint = ColorAlpha(BLACK, 0.5f);
            break;
    }

    Vector3 decalPos = { pos.x, pos.y + 0.02f, pos.z };
    float rotation = (float)GetRandomValue(0, 360);

    // CORE_ISSUES.md Item 4b — lava crack / ripple decal cuộn ra ngoài tâm
    // theo thời gian (decal_flow.fs) thay vì texture đứng yên như mọi preset
    // khác. Glow chỉ bật cho FIRE_LAVA (khe nứt phát sáng) — WATER_RIPPLE
    // cuộn nhưng không cần glow.
    if (type == DECAL_PRESET_FIRE_LAVA) {
        DecalSystem_AddFlowEx(decalPos, rotation, 0.0f, radius, radius, tex,
                              duration, tint, BLEND_ALPHA, 0.02f, 0.6f, 0.8f, 1.5f);
    } else if (type == DECAL_PRESET_WATER_RIPPLE) {
        DecalSystem_AddFlowEx(decalPos, rotation, 0.0f, radius, radius, tex,
                              duration, tint, BLEND_ALPHA, 0.02f, 0.6f, 0.8f, 0.0f);
    } else {
        DecalSystem_Add(decalPos, rotation, radius, tex, duration, tint);
    }
}

// 8. Camera Impulse Implementation
void CameraFX_AddImpulse(Vector3 origin, CameraImpulse impulse) {
    float dist = Vector3Distance(origin, camera.target);
    float factor = 1.0f / (1.0f + dist * impulse.falloff);
    float resultTrauma = impulse.magnitude * factor;
    if (resultTrauma > 0.05f) {
        CameraFX_Shake(resultTrauma);
    }
}

// 9. ForceField Preset Implementation
ForceField ForceField_CreatePreset(ForceFieldPreset preset) {
    ForceField fld;
    ForceField_Clear(&fld);

    switch (preset) {
        case FORCE_PRESET_FIRE_UPDRAFT:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.0f, 1.0f, 0.0f}, .strength = 20.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 12.0f, .noiseScale = 0.06f, .noiseSpeed = 1.2f });
            break;
        case FORCE_PRESET_SNOW_BLIZZARD:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_WIND, .direction = {0.6f, -1.0f, 0.3f}, .strength = 14.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 8.0f, .noiseScale = 0.08f, .noiseSpeed = 0.9f });
            break;
        case FORCE_PRESET_WATER_VORTEX:
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_GRAVITY_DIR, .direction = {0.0f, -1.0f, 0.0f}, .strength = 30.0f });
            ForceField_AddLayer(&fld, (ForceLayer){ .type = FORCE_NOISE_CURL, .strength = 18.0f, .noiseScale = 0.05f, .noiseSpeed = 2.2f });
            break;
    }
    return fld;
}

// 10. Skill Builder Implementation
void SkillBuilder_Start(SkillBuildContext *ctx, Vector3 target, float scale) {
    ctx->target = target;
    ctx->scale = scale;
    ctx->hasExplosion = false;
    ctx->hasDecal = false;
    ctx->hasDamageVolume = false;
}

void SkillBuilder_AddExplosion(SkillBuildContext *ctx, EffectPresetType vfx) {
    ctx->hasExplosion = true;
    ctx->explosionEffect = vfx;
}

void SkillBuilder_AddDecal(SkillBuildContext *ctx, DecalPresetType decal, float radius, float duration) {
    ctx->hasDecal = true;
    ctx->decalType = decal;
    ctx->decalRadius = radius;
    ctx->decalDuration = duration;
}

void SkillBuilder_AddDamageVolume(SkillBuildContext *ctx, float radius, float dps, float duration) {
    ctx->hasDamageVolume = true;
    ctx->damageRadius = radius;
    ctx->damageDps = dps;
    ctx->damageDuration = duration;
}

// Cast-stage hook — its own trigger point, separate from SkillBuilder_Build()
// which fires at impact. Call after SkillBuilder_Start() (needs ctx->target/
// ctx->scale already set) at cast time; fires SpawnCastEffect immediately
// rather than deferring it, since cast happens earlier in the skill
// lifecycle than the impact-time Build() call.
void SkillBuilder_AddCastEffect(SkillBuildContext *ctx, EffectPresetType preset) {
    SpawnCastEffect(ctx->target, preset, ctx->scale);
}

void SkillBuilder_Build(SkillBuildContext *ctx) {
    if (ctx->hasExplosion) {
        SpawnImpactEffect(ctx->target, ctx->explosionEffect, ctx->scale);
    }
    if (ctx->hasDecal) {
        SpawnGroundDecal(ctx->decalType, ctx->target, ctx->decalRadius * ctx->scale, ctx->decalDuration);
    }
    if (ctx->hasDamageVolume) {
        DamageVolume vol = {
            .shape = SHAPE_CIRCLE,
            .center = ctx->target,
            .radius = ctx->damageRadius * ctx->scale,
            .damagePerSecond = ctx->damageDps,
            .tickInterval = 0.25f,
            .duration = ctx->damageDuration
        };
        SpawnDamageVolume(vol);
    }
}
