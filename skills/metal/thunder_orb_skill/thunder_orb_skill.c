#include "thunder_orb_skill.h"

#include "core/vfx_proc_ray.h"
#include "core/vfx_light.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/particle_system.h"
#include "entities/entities.h"

#include "raylib.h"
#include <stddef.h>
#include <stdlib.h>
#include <math.h>

extern Camera3D camera;

#ifndef PI
#define PI 3.14159265f
#endif

// ── constants ──────────────────────────────────────────────────────────────

#define THUNDER_ORB_FLIGHT_SPEED     380.0f
#define THUNDER_ORB_ORB_RADIUS        12.0f
#define THUNDER_ORB_FLIGHT_RAYS        7
#define THUNDER_ORB_RAY_LEN_MIN       24.0f
#define THUNDER_ORB_RAY_LEN_MAX       55.0f
#define THUNDER_ORB_RAY_SCALE_MIN      0.55f
#define THUNDER_ORB_RAY_SCALE_MAX      1.0f
#define THUNDER_ORB_SURFACE_ARCS       3
#define THUNDER_ORB_SURF_ARC_LIFE_MIN  0.06f
#define THUNDER_ORB_SURF_ARC_LIFE_MAX  0.14f

#define THUNDER_ORB_ARC_PER_STRIKE    3
#define THUNDER_ORB_ARC_LIFE          0.12f
#define THUNDER_ORB_ARC_RADIUS_MIN   15.0f
#define THUNDER_ORB_ARC_RADIUS_MAX   45.0f
#define THUNDER_ORB_ARC_SLOTS        (THUNDER_ORB_RAIN_SLOTS * THUNDER_ORB_ARC_PER_STRIKE)

#define THUNDER_ORB_RAIN_SLOTS        8
#define THUNDER_ORB_RAIN_DURATION     5.0f
#define THUNDER_ORB_RAIN_RADIUS     130.0f
#define THUNDER_ORB_RAIN_Y_ORIGIN   420.0f
#define THUNDER_ORB_BOLT_SCALE        1.2f
#define THUNDER_ORB_IMPACT_FLASH_RADIUS 180.0f
#define THUNDER_ORB_IMPACT_FLASH_LIFE   0.15f
#define THUNDER_ORB_RAIN_LIGHT_RADIUS   80.0f
#define THUNDER_ORB_BASE_IMPACT_DMG    45.0f
#define THUNDER_ORB_BASE_RAIN_DMG      12.0f

// ── types ──────────────────────────────────────────────────────────────────

typedef struct {
    int     rayId;
    bool    active;
    float   lifeLeft;
    Vector3 from;
    Vector3 to;
} ArcBolt;

typedef struct {
    int     rayId;
    bool    active;
    float   aliveTimer;
    float   lifeInit;    // initial aliveTimer — for the flash→afterglow envelope
    float   gapTimer;
    Vector3 skyOrigin;   // top endpoint (fixed)
    Vector3 groundPoint; // bottom endpoint (fixed)
} RainSlot;

// Short-lived arc crawling across the orb surface (ball-lightning texture)
typedef struct {
    int     rayId;
    bool    active;
    float   lifeLeft;
    Vector3 dirA, dirB;  // unit dirs from orb center — endpoints follow the orb
} SurfaceArc;

typedef enum { PHASE_INACTIVE = 0, PHASE_FLIGHT, PHASE_RAIN } ThunderOrbPhase;

typedef struct {
    ThunderOrbPhase phase;
    int             ownerAgentId;
    SkillParams     params;

    // flight
    Vector3 orbPos;
    Vector3 orbTarget;
    int     flightRayIds[THUNDER_ORB_FLIGHT_RAYS];
    Vector3 flightRayDirs[THUNDER_ORB_FLIGHT_RAYS];
    float   flightRayLengths[THUNDER_ORB_FLIGHT_RAYS];
    float   flightRayScales[THUNDER_ORB_FLIGHT_RAYS];
    SurfaceArc surfArcs[THUNDER_ORB_SURFACE_ARCS];

    // rain
    Vector3   impactPos;
    float     rainTimer;
    Vector3   lastEnemyPos;
    float     lastEnemyRadius;
    RainSlot  rainSlots[THUNDER_ORB_RAIN_SLOTS];
    ArcBolt   arcBolts[THUNDER_ORB_ARC_SLOTS];
} ThunderOrbState;

static ThunderOrbState s = { 0 };

// ── helpers ───────────────────────────────────────────────────────────────

static float RandRange(float lo, float hi) {
    return lo + (GetRandomValue(0, 10000) / 10000.0f) * (hi - lo);
}

// Fibonacci sphere — evenly distributes n points across the unit sphere
static Vector3 FibSphereDir(int i, int n) {
    float phi   = acosf(1.0f - 2.0f * (i + 0.5f) / (float)n);
    float theta = PI * (1.0f + sqrtf(5.0f)) * (float)i;
    return (Vector3){ sinf(phi)*cosf(theta), cosf(phi), sinf(phi)*sinf(theta) };
}

static void EmitOrbParticles(Vector3 pos) {
    ParticleConfig p = { 0 };
    p.colorStart = (Color){ 255, 240, 180, 220 };
    p.colorEnd   = (Color){ 180, 220, 255,   0 };
    p.radius     = 3.5f;
    p.lifetime   = 0.18f;
    for (int i = 0; i < 3; i++) {
        float a = RandRange(0.0f, 2.0f * PI);
        float b = RandRange(-PI * 0.5f, PI * 0.5f);
        float spd = RandRange(60.0f, 140.0f);
        p.position = pos;
        p.velocity = (Vector3){ cosf(b)*cosf(a)*spd, cosf(b)*sinf(a)*spd*0.4f, sinf(b)*spd };
        SpawnParticle(p);
    }
}

// Layered hot core: small white-hot center + wide violet halo, refreshed each
// frame with very short lifetimes so the orb reads as a solid ball of plasma.
static void EmitOrbCore(Vector3 pos) {
    ParticleConfig p = { 0 };
    p.position = pos;
    p.velocity = (Vector3){ 0 };

    p.colorStart = (Color){ 255, 255, 255, 255 };
    p.colorEnd   = (Color){ 210, 190, 255,   0 };
    p.radius     = THUNDER_ORB_ORB_RADIUS * 0.7f;
    p.lifetime   = 0.09f;
    SpawnParticle(p);

    p.colorStart = (Color){ 150, 110, 255, 110 };
    p.colorEnd   = (Color){  70,  30, 200,   0 };
    p.radius     = THUNDER_ORB_ORB_RADIUS * 1.6f;
    p.lifetime   = 0.13f;
    SpawnParticle(p);
}

static ProcRayConfig SurfArcConfig(void) {
    ProcRayConfig c = ProcRay_BoltLightningConfig();
    c.thickness      = 0.6f;
    c.amplitudeRatio = 0.30f; // wander off the chord — reads as crawling on the surface
    c.branchCount    = 0;
    c.taperTip       = 1.0f;
    return c;
}

static Vector3 RandUnitDir(void) {
    float a = RandRange(0.0f, 2.0f * PI);
    float c = RandRange(-1.0f, 1.0f);
    float sq = sqrtf(1.0f - c * c);
    return (Vector3){ sq * cosf(a), c, sq * sinf(a) };
}

static void RespawnSurfaceArc(SurfaceArc *arc) {
    arc->dirA     = RandUnitDir();
    arc->dirB     = RandUnitDir();
    arc->lifeLeft = RandRange(THUNDER_ORB_SURF_ARC_LIFE_MIN, THUNDER_ORB_SURF_ARC_LIFE_MAX);
    arc->rayId    = SpawnProcBolt(SurfArcConfig(), 0.5f);
    arc->active   = arc->rayId >= 0;
}

static void KillAllFlightRays(void) {
    for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++) ProcRay_Kill(s.flightRayIds[i]);
    for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++)
        if (s.surfArcs[i].active) { ProcBolt_Kill(s.surfArcs[i].rayId); s.surfArcs[i].active = false; }
}

static void KillAllRainBolts(void) {
    for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++)
        if (s.rainSlots[i].active) { ProcBolt_Kill(s.rainSlots[i].rayId); s.rainSlots[i].active = false; }
    for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++)
        if (s.arcBolts[i].active) { ProcBolt_Kill(s.arcBolts[i].rayId); s.arcBolts[i].active = false; }
}

static void SpawnArcBurst(Vector3 groundPoint) {
    ProcRayConfig arcCfg = ProcRay_BoltLightningConfig();
    arcCfg.thickness      = 0.8f;
    arcCfg.amplitudeRatio = 0.25f; // more wandering for the short arcs
    arcCfg.branchCount    = 0;     // ground crawlers stay simple strands
    arcCfg.taperTip       = 0.3f;  // die out into the ground

    for (int k = 0; k < THUNDER_ORB_ARC_PER_STRIKE; k++) {
        // find a free arc slot
        int slot = -1;
        for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++) {
            if (!s.arcBolts[i].active) { slot = i; break; }
        }
        if (slot < 0) break;

        float angle = RandRange(0.0f, 2.0f * PI);
        float dist  = RandRange(THUNDER_ORB_ARC_RADIUS_MIN, THUNDER_ORB_ARC_RADIUS_MAX);
        Vector3 tip = { groundPoint.x + cosf(angle)*dist, 0.0f, groundPoint.z + sinf(angle)*dist };

        s.arcBolts[slot].from     = groundPoint;
        s.arcBolts[slot].to       = tip;
        s.arcBolts[slot].lifeLeft = THUNDER_ORB_ARC_LIFE;
        s.arcBolts[slot].rayId    = SpawnProcBolt(arcCfg, 0.6f);
        s.arcBolts[slot].active   = true;
        ProcBolt_Update(s.arcBolts[slot].rayId, groundPoint, tip, 0.6f, 0.0f);
    }
}

// ── rain bolt ─────────────────────────────────────────────────────────────

// Fast upward sparks where the bolt hits the ground — sells the impact energy
static void EmitStrikeSparks(Vector3 groundPoint) {
    ParticleConfig p = { 0 };
    p.colorStart = (Color){ 255, 250, 255, 255 };
    p.colorEnd   = (Color){ 140,  90, 255,   0 };
    p.radius     = 2.2f;
    for (int i = 0; i < 10; i++) {
        float a   = RandRange(0.0f, 2.0f * PI);
        float out = RandRange(30.0f, 130.0f);
        p.position = groundPoint;
        p.velocity = (Vector3){ cosf(a) * out, RandRange(140.0f, 320.0f), sinf(a) * out };
        p.lifetime = RandRange(0.25f, 0.5f);
        SpawnParticle(p);
    }
}

static void SpawnRainBolt(RainSlot *slot) {
    float angle = RandRange(0.0f, 2.0f * PI);
    float dist  = RandRange(0.0f, THUNDER_ORB_RAIN_RADIUS);
    float px    = s.impactPos.x + cosf(angle) * dist;
    float pz    = s.impactPos.z + sinf(angle) * dist;

    // sky entry offset sideways so channels slant like real strikes
    slot->skyOrigin   = (Vector3){ px + RandRange(-45.0f, 45.0f),
                                   THUNDER_ORB_RAIN_Y_ORIGIN,
                                   pz + RandRange(-45.0f, 45.0f) };
    slot->groundPoint = (Vector3){ px, 0.0f, pz };
    slot->lifeInit    = RandRange(0.28f, 0.5f);
    slot->aliveTimer  = slot->lifeInit;
    slot->active      = true;

    slot->rayId = SpawnProcBolt(ProcRay_BoltLightningConfig(), THUNDER_ORB_BOLT_SCALE);
    // generate waypoints now — the bolt is drawn this same frame
    ProcBolt_Update(slot->rayId, slot->skyOrigin, slot->groundPoint,
                    THUNDER_ORB_BOLT_SCALE, 0.0f);
    ProcBolt_SetBrightness(slot->rayId, 1.9f); // leader flash frame
    EmitStrikeSparks(slot->groundPoint);

    // damage enemy if bolt lands nearby
    if (Vector3Distance(slot->groundPoint, s.lastEnemyPos) < s.lastEnemyRadius + 60.0f) {
        Entity_ApplyAoEDamage(slot->groundPoint, 60.0f, THUNDER_ORB_BASE_RAIN_DMG, 80.0f);
        AddFloatingText(s.lastEnemyPos, "12", (Color){ 200, 200, 255, 255 }, 20.0f, 0.6f);
        AddKnockbackToEnemy(Vector3Scale(
            Vector3Normalize(Vector3Subtract(s.lastEnemyPos, slot->groundPoint)), 80.0f));
    }

    // Flash at sky entry
    VFXLight_Spawn(slot->skyOrigin, (Color){ 200, 200, 255, 255 },
                   120.0f, 0.08f, VFX_PRIORITY_HIGH_ULTIMATE);
    // decal + light at ground strike point (no SpawnImpactEffect — avoids CameraFX_Shake)
    SpawnGroundDecal(DECAL_PRESET_TAIJI_LIGHTNING, slot->groundPoint, 35.0f, 3.5f);
    VFXLight_Spawn(slot->groundPoint, ELEMENT_COLOR_METAL,
                   THUNDER_ORB_RAIN_LIGHT_RADIUS, slot->aliveTimer, VFX_PRIORITY_LOW);

    SpawnArcBurst(slot->groundPoint);
}

// ── impact transition ─────────────────────────────────────────────────────

static void TriggerImpact(Vector3 pos) {
    s.impactPos = pos;
    float impactDmg = s.params.damage > 0.0f ? s.params.damage : THUNDER_ORB_BASE_IMPACT_DMG;
    Entity_ApplyAoEDamage(pos, THUNDER_ORB_RAIN_RADIUS * 0.5f, impactDmg, 220.0f);
    SpawnImpactEffect(pos, EFFECT_PRESET_LIGHTNING_IMPACT, 1.5f);
    SpawnGroundDecal(DECAL_PRESET_TAIJI_LIGHTNING, pos, 120.0f, 6.0f);
    VFXLight_Spawn(pos, (Color){ 200, 200, 255, 255 },
                   THUNDER_ORB_IMPACT_FLASH_RADIUS, THUNDER_ORB_IMPACT_FLASH_LIFE,
                   VFX_PRIORITY_HIGH_ULTIMATE);

    KillAllFlightRays();

    s.phase     = PHASE_RAIN;
    s.rainTimer = 0.0f;
    for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++) {
        s.rainSlots[i].active   = false;
        s.rainSlots[i].gapTimer = RandRange(0.0f, 0.3f); // stagger first strikes
        s.rainSlots[i].rayId    = -1;
    }
}

// ── public API ────────────────────────────────────────────────────────────

void InitThunderOrbSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;
    s = (ThunderOrbState){ 0 };
}

void CastThunderOrbSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params) {
    if (s.phase == PHASE_FLIGHT) KillAllFlightRays();
    if (s.phase == PHASE_RAIN)   KillAllRainBolts();

    s = (ThunderOrbState){ 0 };
    s.phase        = PHASE_FLIGHT;
    s.ownerAgentId = agentId;
    s.params       = params;
    s.orbPos       = startPos;
    s.orbTarget    = target;

    // Rays spread evenly in 3D (Fibonacci sphere), each with random length+scale
    for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++) {
        s.flightRayDirs[i]    = FibSphereDir(i, THUNDER_ORB_FLIGHT_RAYS);
        s.flightRayLengths[i] = RandRange(THUNDER_ORB_RAY_LEN_MIN, THUNDER_ORB_RAY_LEN_MAX);
        s.flightRayScales[i]  = RandRange(THUNDER_ORB_RAY_SCALE_MIN, THUNDER_ORB_RAY_SCALE_MAX);
        s.flightRayIds[i]     = SpawnProcRay(ProcRay_LightningConfig(), s.flightRayScales[i]);
        ProcRay_SetPhase(s.flightRayIds[i], (float)i * (2.0f * PI / (float)THUNDER_ORB_FLIGHT_RAYS));
    }
    for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++) RespawnSurfaceArc(&s.surfArcs[i]);
}

void UpdateThunderOrbSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    if (s.phase == PHASE_FLIGHT) {
        // collision with enemy
        if (Vector3Distance(s.orbPos, enemyPos) < THUNDER_ORB_ORB_RADIUS + enemyRadius) {
            if (GetRandomValue(1, 100) <= 25)
                AddFloatingText(enemyPos, "CRIT!", YELLOW, 28.0f, 1.0f);
            else
                AddFloatingText(enemyPos, "HIT", (Color){ 200, 200, 255, 255 }, 22.0f, 0.7f);
            AddKnockbackToEnemy(Vector3Scale(
                Vector3Normalize(Vector3Subtract(enemyPos, s.orbPos)), 280.0f));
            TriggerImpact(s.orbPos);
            return;
        }

        Vector3 dir  = { s.orbTarget.x-s.orbPos.x, s.orbTarget.y-s.orbPos.y, s.orbTarget.z-s.orbPos.z };
        float dist   = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);

        if (dist <= THUNDER_ORB_FLIGHT_SPEED * dt + 1.0f) {
            TriggerImpact(s.orbTarget);
            return;
        }

        float inv = 1.0f / dist;
        s.orbPos.x += dir.x * inv * THUNDER_ORB_FLIGHT_SPEED * dt;
        s.orbPos.y += dir.y * inv * THUNDER_ORB_FLIGHT_SPEED * dt;
        s.orbPos.z += dir.z * inv * THUNDER_ORB_FLIGHT_SPEED * dt;

        // flickering light — plasma is never steady
        VFXLight_Spawn(s.orbPos, (Color){ 220, 230, 255, 200 },
                       RandRange(70.0f, 110.0f), 0.07f, VFX_PRIORITY_LOW);
        EmitOrbCore(s.orbPos);
        EmitOrbParticles(s.orbPos);

        for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++) {
            ProcRay_Update(s.flightRayIds[i], s.orbPos,
                           s.flightRayDirs[i], s.flightRayLengths[i],
                           s.flightRayScales[i], dt);
            ProcRay_SetBrightness(s.flightRayIds[i], RandRange(0.65f, 1.15f));
        }

        // surface arcs crawl on the orb shell, respawning at new spots
        for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++) {
            SurfaceArc *arc = &s.surfArcs[i];
            if (arc->active) {
                arc->lifeLeft -= dt;
                if (arc->lifeLeft <= 0.0f) {
                    ProcBolt_Kill(arc->rayId);
                    arc->active = false;
                }
            }
            if (!arc->active) RespawnSurfaceArc(arc);
            if (arc->active) {
                float r = THUNDER_ORB_ORB_RADIUS * 0.95f;
                Vector3 a = Vector3Add(s.orbPos, Vector3Scale(arc->dirA, r));
                Vector3 b = Vector3Add(s.orbPos, Vector3Scale(arc->dirB, r));
                ProcBolt_Update(arc->rayId, a, b, 0.5f, dt);
            }
        }

    } else if (s.phase == PHASE_RAIN) {
        s.lastEnemyPos    = enemyPos;
        s.lastEnemyRadius = enemyRadius;
        s.rainTimer += dt;
        if (s.rainTimer >= THUNDER_ORB_RAIN_DURATION) {
            KillAllRainBolts();
            s.phase = PHASE_INACTIVE;
            return;
        }

        Entity_ApplyAoEDamage(s.impactPos, THUNDER_ORB_RAIN_RADIUS,
                               s.params.damage * 0.08f * dt,
                               60.0f);

        for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++) {
            RainSlot *slot = &s.rainSlots[i];
            if (slot->active) {
                slot->aliveTimer -= dt;
                if (slot->aliveTimer <= 0.0f) {
                    ProcBolt_Kill(slot->rayId);
                    slot->active   = false;
                    slot->gapTimer = RandRange(0.05f, 0.15f);
                } else {
                    ProcBolt_Update(slot->rayId, slot->skyOrigin, slot->groundPoint,
                                    THUNDER_ORB_BOLT_SCALE, dt);
                    // strike lifecycle: blinding flash ~70ms, then flickering afterglow decay
                    float age = slot->lifeInit - slot->aliveTimer;
                    float b   = (age < 0.07f)
                                    ? 1.9f
                                    : (0.45f + 0.75f * (slot->aliveTimer / slot->lifeInit))
                                          * RandRange(0.8f, 1.1f);
                    ProcBolt_SetBrightness(slot->rayId, b);
                }
            } else {
                slot->gapTimer -= dt;
                if (slot->gapTimer <= 0.0f) SpawnRainBolt(slot);
            }
        }

        for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++) {
            ArcBolt *arc = &s.arcBolts[i];
            if (!arc->active) continue;
            arc->lifeLeft -= dt;
            if (arc->lifeLeft <= 0.0f) {
                ProcBolt_Kill(arc->rayId);
                arc->active = false;
            } else {
                ProcBolt_Update(arc->rayId, arc->from, arc->to, 0.6f, dt);
                ProcBolt_SetBrightness(arc->rayId, arc->lifeLeft / THUNDER_ORB_ARC_LIFE);
            }
        }
    }
}

void DrawThunderOrbSkill(void) {
    if (s.phase == PHASE_FLIGHT) {
        for (int i = 0; i < THUNDER_ORB_FLIGHT_RAYS; i++)
            ProcRay_Draw(s.flightRayIds[i], camera);
        for (int i = 0; i < THUNDER_ORB_SURFACE_ARCS; i++)
            if (s.surfArcs[i].active)
                ProcBolt_Draw(s.surfArcs[i].rayId, camera);
    } else if (s.phase == PHASE_RAIN) {
        for (int i = 0; i < THUNDER_ORB_RAIN_SLOTS; i++)
            if (s.rainSlots[i].active)
                ProcBolt_Draw(s.rainSlots[i].rayId, camera);
        for (int i = 0; i < THUNDER_ORB_ARC_SLOTS; i++)
            if (s.arcBolts[i].active)
                ProcBolt_Draw(s.arcBolts[i].rayId, camera);
    }
}

void UnloadThunderOrbSkill(void) {
    if (s.phase == PHASE_FLIGHT) KillAllFlightRays();
    if (s.phase == PHASE_RAIN)   KillAllRainBolts();
    s = (ThunderOrbState){ 0 };
}

bool IsThunderOrbSkillCoiling(void) { return false; }

int GetThunderOrbSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    if (s.phase != PHASE_FLIGHT || maxProjectiles < 1) return 0;
    outProjectiles[0].position = s.orbPos;
    outProjectiles[0].radius   = THUNDER_ORB_ORB_RADIUS;
    outProjectiles[0].active   = true;
    return 1;
}

void DeactivateThunderOrbProjectile(int index) {
    if (index == 0 && s.phase == PHASE_FLIGHT) TriggerImpact(s.orbPos);
}
