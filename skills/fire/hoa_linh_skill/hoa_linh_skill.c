// Hỏa Linh Trận (Wildfire Spirits) — wide-area, low-damage Fire zone skill.
// A flame ring sweeps outward over a large circle, then fire-spirit wisps
// wander the zone (noise-driven ribbon trails, contained by a point-gravity
// force field), igniting small burning patches wherever they drift. Huge
// coverage, weak per-tick damage — area denial, not burst.
#include "skills/fire/hoa_linh_skill/hoa_linh_skill.h"
#include "core/particle_system.h"
#include "core/trail_system.h"
#include "core/force_field.h"
#include "core/color_gradient.h"
#include "core/skill_helper.h"
#include "core/skill_manager.h"
#include "core/emitter_system.h"
#include "core/vfx_light.h"
#include "core/camera_fx.h"
#include "core/screen_distort.h"
#include "core/utils_math.h"
#include "entities/entities.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef PI
#define PI 3.1415926535f
#endif

#define MAX_FIELDS        2
#define WISPS_PER_FIELD   6
#define FIELD_RADIUS      170.0f  // wide-area coverage

#define RING_TIME         0.7f    // expanding flame ring
#define ROAM_TIME         4.5f    // wisps wander and ignite patches
#define FADE_TIME         0.8f
#define IGNITE_INTERVAL   0.6f    // per-wisp ground ignition cadence
#define FLAME_RATE        16.0f   // flame particles per second per wisp
#define RING_DAMAGE_TICKS 3

typedef enum {
    FIELD_INACTIVE = 0,
    FIELD_RING,
    FIELD_ROAM,
    FIELD_FADE
} FieldState;

typedef struct {
    FieldState state;
    float   timer;
    float   scale;
    float   radius;
    Vector3 center;
    float   damage;       // deliberately weak: only fractional ticks applied
    int     ringTicksDone;
    int     wispIds[WISPS_PER_FIELD];
    float   igniteTimer[WISPS_PER_FIELD];
    float   flameAccum[WISPS_PER_FIELD]; // continuous flame-particle budget
    ForceField force;     // per-field: noise wander + center pull containment
    int     ownerAgentId; // CORE_ISSUES.md Item 15
} FireField;

static FireField     s_fields[MAX_FIELDS];
static ColorGradient s_flameGrad;
static ColorGradient s_emberGrad;
static int           s_skillIndex = -1;

// Tiny ember pop where a wisp expires — no visual popping on trail death.
static void WispDeathPuff(Vector3 pos, float scale)
{
    for (int i = 0; i < 5; i++) {
        SpawnParticle((ParticleConfig){
            .position = pos,
            .velocity = { (float)GetRandomValue(-25, 25),
                          (float)GetRandomValue(20, 55),
                          (float)GetRandomValue(-25, 25) },
            .radius   = (float)GetRandomValue(10, 22) / 10.0f * scale,
            .lifetime = 0.7f,
            .gradient = &s_emberGrad
        });
    }
}

static void FieldStartFade(FireField *f)
{
    if (f->state != FIELD_RING && f->state != FIELD_ROAM) return;
    f->state = FIELD_FADE;
    f->timer = 0.0f;
    for (int w = 0; w < WISPS_PER_FIELD; w++) {
        if (f->wispIds[w] >= 0) {
            KillTrail(f->wispIds[w]);
            f->wispIds[w] = -1;
        }
    }
}

static bool HoaLinhSkill_HasActiveInstance(int agentId)
{
    for (int i = 0; i < MAX_FIELDS; i++) {
        if (s_fields[i].state != FIELD_INACTIVE && s_fields[i].ownerAgentId == agentId)
            return true;
    }
    return false;
}

static void HoaLinhSkill_Abort(int agentId)
{
    for (int i = 0; i < MAX_FIELDS; i++) {
        if (s_fields[i].state != FIELD_INACTIVE && s_fields[i].ownerAgentId == agentId)
            FieldStartFade(&s_fields[i]);
    }
}

void InitHoaLinhSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;

    s_skillIndex = Skill_GetIndexByName("HOA_LINH");
    RegisterSkillLifecycleQuery(s_skillIndex, HoaLinhSkill_HasActiveInstance);
    RegisterSkillAbort(s_skillIndex, HoaLinhSkill_Abort);

    for (int i = 0; i < MAX_FIELDS; i++) {
        s_fields[i].state = FIELD_INACTIVE;
        for (int w = 0; w < WISPS_PER_FIELD; w++) s_fields[i].wispIds[w] = -1;
    }

    // Flame body: white-hot core -> orange -> element crimson -> smoke-out.
    ColorGradient_AddStop(&s_flameGrad, 0.00f, (Color){ 255, 240, 190, 255 });
    ColorGradient_AddStop(&s_flameGrad, 0.30f, ColorAlpha(ORANGE, 0.95f));
    ColorGradient_AddStop(&s_flameGrad, 0.65f, ColorAlpha(ELEMENT_COLOR_FIRE, 0.8f));
    ColorGradient_AddStop(&s_flameGrad, 1.00f, (Color){ 40, 12, 8, 0 });

    ColorGradient_AddStop(&s_emberGrad, 0.00f, ColorAlpha(ORANGE, 0.9f));
    ColorGradient_AddStop(&s_emberGrad, 0.50f, ColorAlpha(ELEMENT_COLOR_FIRE, 0.6f));
    ColorGradient_AddStop(&s_emberGrad, 1.00f, (Color){ 30, 10, 6, 0 });
}

void CastHoaLinhSkill(int agentId, Vector3 startPos, Vector3 target, SkillParams params)
{
    if (!SkillManager_CanCast(s_skillIndex, agentId)) return;

    FireField *f = NULL;
    for (int i = 0; i < MAX_FIELDS; i++) {
        if (s_fields[i].state == FIELD_INACTIVE) { f = &s_fields[i]; break; }
    }
    if (!f) return;

    float scale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;

    f->state         = FIELD_RING;
    f->timer         = 0.0f;
    f->scale         = scale;
    f->radius        = FIELD_RADIUS * scale;
    f->center        = (Vector3){ target.x, 0.0f, target.z };
    f->ringTicksDone = 0;
    f->ownerAgentId  = agentId;
    // Weak by design — Trap/Utility tier, and only fractions are ever applied.
    f->damage        = Skill_CalculateDamage(SKILL_CAT_TRAP_UTILITY, params);
    for (int w = 0; w < WISPS_PER_FIELD; w++) {
        f->wispIds[w]     = -1;
        f->igniteTimer[w] = (float)GetRandomValue(0, 40) / 100.0f; // §12.3 desync
        f->flameAccum[w]  = 0.0f;
    }

    // Wander field: perlin drift + constant pull to center (containment)
    // + light drag — fast enough that the ribbon stretches into a tongue
    // of flame instead of bunching into a star-shaped blob.
    ForceField_Clear(&f->force);
    ForceField_AddLayer(&f->force, (ForceLayer){
        .type = FORCE_NOISE_PERLIN,
        .origin = { (float)GetRandomValue(0, 999), 0, (float)GetRandomValue(0, 999) },
        .strength = 170.0f, .noiseScale = 0.7f, .noiseSpeed = 0.9f
    });
    ForceField_AddLayer(&f->force, (ForceLayer){
        .type = FORCE_GRAVITY_POINT,
        .origin = { f->center.x, 16.0f, f->center.z },
        .strength = 75.0f, .radius = f->radius * 1.6f, .falloff = 0.0f
    });
    ForceField_AddLayer(&f->force, (ForceLayer){
        .type = FORCE_DRAG, .strength = 0.35f
    });

    SpawnCastEffect(startPos, EFFECT_PRESET_FIRE_EXPLOSION, scale * 0.7f);
    PlayCastSound(EFFECT_PRESET_FIRE_EXPLOSION);
    VFXLight_Spawn(f->center, ORANGE, f->radius * 0.7f, RING_TIME + 0.4f,
                   VFX_PRIORITY_LOW);
    ScreenDistort_Add(f->center, f->radius * 0.5f, 0.5f, 0.4f, 220.0f);
    CameraFX_Shake(0.18f);

    SkillManager_TriggerCooldown(s_skillIndex, agentId,
                                 Skill_CalculateCooldown(SKILL_CAT_TRAP_UTILITY, params));
}

static void SpawnWisps(FireField *f)
{
    for (int w = 0; w < WISPS_PER_FIELD; w++) {
        float a = (float)w / WISPS_PER_FIELD * 2.0f * PI
                + (float)GetRandomValue(-30, 30) * DEG2RAD; // §12.2 jitter
        float r = f->radius * (0.25f + 0.55f * (float)GetRandomValue(0, 100) / 100.0f);
        TrailConfig cfg = { 0 };
        cfg.type        = TRAIL_TYPE_WISP;
        cfg.pos         = (Vector3){ f->center.x + cosf(a) * r,
                                     12.0f + (float)GetRandomValue(0, 12),
                                     f->center.z + sinf(a) * r };
        cfg.vel         = (Vector3){ -sinf(a) * 70.0f, 12.0f, cosf(a) * 70.0f };
        cfg.len         = 8.0f * f->scale;   // narrow blade — wide + slow reads as a star
        cfg.thick       = 5.0f * f->scale;
        cfg.trailLength = 70.0f;
        cfg.life        = ROAM_TIME + FADE_TIME * 0.5f
                        + (float)GetRandomValue(-40, 40) / 100.0f; // §12.3 desync
        cfg.scale       = f->scale;
        cfg.gradient    = &s_flameGrad;
        cfg.tint        = ELEMENT_COLOR_FIRE;
        cfg.forceField  = &f->force;
        cfg.onDeath     = WispDeathPuff;
        cfg.priority    = VFX_PRIORITY_LOW;
        f->wispIds[w] = SpawnTrailEntity(cfg);
    }
}

void UpdateHoaLinhSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    (void)enemyPos; (void)enemyRadius;

    for (int i = 0; i < MAX_FIELDS; i++) {
        FireField *f = &s_fields[i];
        if (f->state == FIELD_INACTIVE) continue;
        f->timer += dt;

        switch (f->state) {
        case FIELD_RING: {
            float t = f->timer / RING_TIME;
            float ringR = SmoothStep01(t) * f->radius;

            // Ember curtain along the expanding rim (continuous, cheap).
            for (int p = 0; p < 6; p++) {
                float a = (float)GetRandomValue(0, 3600) / 10.0f * DEG2RAD;
                SpawnParticle((ParticleConfig){
                    .position = { f->center.x + cosf(a) * ringR, 2.0f,
                                  f->center.z + sinf(a) * ringR },
                    .velocity = { cosf(a) * 30.0f,
                                  (float)GetRandomValue(45, 95),
                                  sinf(a) * 30.0f },
                    .radius   = (float)GetRandomValue(14, 30) / 10.0f * f->scale,
                    .lifetime = 0.8f,
                    .gradient = &s_flameGrad
                });
            }

            // Weak damage as the ring passes — 3 growing-radius ticks.
            // (No giant scorch decals here: 3 stacked full-circle burn marks
            // read as a muddy blob, confirmed visually.)
            int tickDue = (int)(t * RING_DAMAGE_TICKS) + 1;
            if (tickDue > f->ringTicksDone && f->ringTicksDone < RING_DAMAGE_TICKS) {
                f->ringTicksDone++;
                float tickR = f->radius * (float)f->ringTicksDone / RING_DAMAGE_TICKS;
                Entity_ApplyAoEDamage(f->center, tickR, f->damage * 0.15f, 60.0f);
            }

            if (f->timer >= RING_TIME) {
                f->state = FIELD_ROAM;
                f->timer = 0.0f;
                SpawnWisps(f);
                // One crawling-lava heart at the array's center for the
                // whole roam phase — the anchor visual of the zone.
                SpawnGroundDecal(DECAL_PRESET_FIRE_LAVA, f->center,
                                 45.0f * f->scale, ROAM_TIME + FADE_TIME);
            }
            break;
        }

        case FIELD_ROAM: {
            for (int w = 0; w < WISPS_PER_FIELD; w++) {
                if (f->wispIds[w] < 0) continue;
                TrailEntity *tr = GetTrail(f->wispIds[w]);
                if (!tr) { f->wispIds[w] = -1; continue; }

                // Continuous flame tongue at the wisp head — the fire look
                // comes from these particles, the ribbon is just the streak.
                f->flameAccum[w] += dt * FLAME_RATE;
                while (f->flameAccum[w] >= 1.0f) {
                    f->flameAccum[w] -= 1.0f;
                    SpawnParticle((ParticleConfig){
                        .position = { tr->position.x + (float)GetRandomValue(-4, 4),
                                      tr->position.y + (float)GetRandomValue(-3, 3),
                                      tr->position.z + (float)GetRandomValue(-4, 4) },
                        .velocity = { (float)GetRandomValue(-10, 10),
                                      (float)GetRandomValue(50, 85),
                                      (float)GetRandomValue(-10, 10) },
                        .radius   = (float)GetRandomValue(20, 36) / 10.0f * f->scale,
                        .lifetime = 0.55f,
                        .gradient = &s_flameGrad
                    });
                }

                f->igniteTimer[w] += dt;
                if (f->igniteTimer[w] < IGNITE_INTERVAL) continue;
                f->igniteTimer[w] = (float)GetRandomValue(-15, 10) / 100.0f;

                Vector3 ground = { tr->position.x, 0.0f, tr->position.z };
                // Small crawling-lava patch (radial flow shader) + a faint,
                // smaller char mark that outlives it — no stacked blood-blobs.
                SpawnGroundDecal(DECAL_PRESET_FIRE_LAVA, ground, 16.0f * f->scale, 2.5f);
                SpawnGroundDecal(DECAL_PRESET_BURN, ground, 10.0f * f->scale, 4.5f);
                Entity_ApplyAoEDamage(ground, 26.0f * f->scale, f->damage * 0.08f, 0.0f);
                VFXLight_Spawn(tr->position, ORANGE, 40.0f * f->scale, 0.3f,
                               VFX_PRIORITY_LOW);
                if (GetRandomValue(0, 100) < 25) {
                    Emitter_AttachToPoint(EMITTER_FIRE, ground, 7.0f, 1.3f);
                }
            }
            if (f->timer >= ROAM_TIME) FieldStartFade(f);
            break;
        }

        case FIELD_FADE: {
            if (f->timer >= FADE_TIME) f->state = FIELD_INACTIVE;
            break;
        }

        default: break;
        }
    }
}

void DrawHoaLinhSkill(void)
{
    // Only the expanding flame ring is drawn by the skill itself — wisps,
    // particles, decals and lights are all engine-rendered systems.
    for (int i = 0; i < MAX_FIELDS; i++) {
        const FireField *f = &s_fields[i];
        if (f->state != FIELD_RING) continue;

        float t = f->timer / RING_TIME;
        float ringR = SmoothStep01(t) * f->radius;
        float fade = 1.0f - t * t;
        Color c = ColorAlpha(ORANGE, 0.7f * fade);

        // Additive translucent ring: no depth write (VFX_ARCHITECTURE rule 2).
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        DrawEffectMesh(MESH_PRESET_SHOCKWAVE,
                       (Vector3){ f->center.x, 1.5f, f->center.z },
                       (Vector3){ ringR, 6.0f + 10.0f * fade, ringR }, c);
        EndBlendMode();
        rlEnableDepthMask();
    }
}

void UnloadHoaLinhSkill(void)
{
    // No-op: no owned GPU resources; trails are killed via state machine.
}

bool IsHoaLinhSkillCoiling(void)
{
    for (int i = 0; i < MAX_FIELDS; i++) {
        if (s_fields[i].state == FIELD_RING) return true;
    }
    return false;
}

int GetHoaLinhSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles)
{
    int count = 0;
    for (int i = 0; i < MAX_FIELDS && count < maxProjectiles; i++) {
        const FireField *f = &s_fields[i];
        if (f->state != FIELD_ROAM) continue;
        outProjectiles[count].position = f->center;
        outProjectiles[count].radius   = f->radius;
        outProjectiles[count].active   = true;
        count++;
    }
    return count;
}

void DeactivateHoaLinhProjectile(int index)
{
    int count = 0;
    for (int i = 0; i < MAX_FIELDS; i++) {
        FireField *f = &s_fields[i];
        if (f->state != FIELD_ROAM) continue;
        if (count == index) {
            FieldStartFade(f);
            return;
        }
        count++;
    }
}
