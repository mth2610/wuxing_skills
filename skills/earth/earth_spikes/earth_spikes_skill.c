/*
 * earth_spikes_skill.c  —  Địa Sa Châm (Earth Spikes)
 * Element : Earth  |  Category : SKILL_CAT_AOE_CONTROL
 *
 * Place in: skills/earth/earth_spikes/earth_spikes_skill.c
 *
 * Memory policy: ZERO dynamic allocation.  All state lives in static
 * pools on the data segment.  No malloc / free anywhere.
 *
 * Build note: compile with C99 (-std=c99) and link against Raylib 5.5.
 */

#include "skills/earth/earth_spikes/earth_spikes_skill.h"

#include "core/particle_system.h"   /* SpawnParticle, ParticleConfig, ColorGradient */
#include "core/decal_system.h"      /* Decal_Spawn                                  */
#include "core/screen_distort.h"    /* ScreenDistort_AddSource                      */
#include "core/vfx_light.h"         /* VFXLight_Spawn                               */
#include "core/camera_fx.h"         /* CameraFX_Shake                               */

#include "raymath.h"
#include <math.h>                   /* sinf, cosf, sqrtf                            */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

/* ================================================================
 * § 1  CONFIGURATION  (tune freely; all gameplay knobs are here)
 * ================================================================ */

/* Pool / line geometry */
#define MAX_SPIKES          32       /* static pool  – supports ≥ 4 overlapping casts */
#define SPIKES_PER_CAST      7       /* spikes per cast invocation                     */
#define SPIKE_SPACING       40.0f   /* world-unit distance between spikes on the line */
#define SPIKE_DELAY          0.08f  /* seconds between successive spike activations   */

/* Timing (seconds) */
#define SPIKE_RISE_TIME      0.22f  /* time to fully emerge from the ground           */
#define SPIKE_HOLD_TIME      1.00f  /* time the spike stays fully risen               */
#define SPIKE_DISSOLVE_TIME  0.50f  /* time to crumble back into earth (shader phase) */

/* Geometry */
#define SPIKE_MAX_HEIGHT    52.0f   /* maximum height of a spike  (world units)       */
#define SPIKE_BASE_RADIUS    7.0f   /* base cylinder radius                           */
#define SPIKE_BODY_FRAC      0.70f  /* fraction of height occupied by the body        */
#define SPIKE_TIP_FRAC       0.30f  /* fraction of height occupied by the tip cone    */
#define SPIKE_TOP_RATIO      0.35f  /* ratio of top-to-bottom radius (taper amount)   */

/* VFX / gameplay */
#define AOE_RADIUS          30.0f   /* AoE damage detection radius per spike          */
#define DUST_PARTICLES      14      /* radial dust particles emitted on emergence     */
#define DISTORT_RADIUS      70.0f   /* screen-distortion source radius                */
#define DISTORT_STRENGTH     0.35f  /* screen-distortion intensity                    */
#define DISTORT_SPEED       90.0f   /* screen-distortion wave speed                   */
#define LIGHT_RADIUS        90.0f   /* VFX point-light radius (world units)           */
#define LIGHT_LIFETIME       0.90f  /* VFX point-light duration (seconds)             */
#define SHAKE_TRAUMA         0.12f  /* camera trauma added per spike emergence        */

/* ================================================================
 * § 2  SPIKE LIFECYCLE STATE
 * ================================================================ */

typedef enum {
    SPIKE_INACTIVE   = 0,
    SPIKE_WAITING,      /* delay timer counting down before rise begins */
    SPIKE_RISING,       /* emerging from the ground (geometric animation) */
    SPIKE_HOLDING,      /* fully risen — active collision zone */
    SPIKE_DISSOLVING,   /* crumbling back; dissolve shader active */
} SpikeState;

typedef struct {
    Vector3    pos;              /* world-space ground coordinate (y = 0)   */
    float      delay;            /* seconds remaining before rising          */
    float      riseTimer;        /* elapsed time during RISING phase         */
    float      holdTimer;        /* remaining time during HOLDING phase      */
    float      dissolveTimer;    /* elapsed time during DISSOLVING phase     */
    float      scale;            /* derived from SkillParams.sizeScale       */
    SpikeState state;
    bool       dealtDamage;      /* one-shot flag: damage applied only once  */
    bool       spawnedDecal;     /* one-shot flag: crack decal placed once   */
    bool       spawnedLight;     /* one-shot flag: VFX light spawned once    */
    float      damage;           /* damage value calculated on cast          */
    float      knockback;        /* knockback force calculated on cast       */
} Spike;

/* ================================================================
 * § 3  MODULE-LEVEL STATIC STORAGE  (no heap)
 * ================================================================ */

static Spike         s_spikes[MAX_SPIKES];   /* spike instance pool          */
static Texture2D     s_crackTex;             /* crack decal atlas            */
static Shader        s_dissolveShader;       /* earth_spikes.fs              */
static int           s_uDissolveLoc;         /* uniform: u_dissolve          */
static int           s_uTimeLoc;             /* uniform: u_time              */
static ColorGradient s_dustGrad;             /* gradient for dust particles  */

/* ================================================================
 * § 4  INTERNAL HELPERS
 * ================================================================ */

/* Returns the index of the first inactive slot, or -1 if pool is full. */
static int FindFreeSlot(void)
{
    for (int i = 0; i < MAX_SPIKES; i++) {
        if (s_spikes[i].state == SPIKE_INACTIVE) return i;
    }
    return -1;
}

/*
 * SpawnDustBurst  —  Radial dust explosion at spike emergence site.
 * Emits DUST_PARTICLES particles around the circumference plus one
 * central upward puff.  Child configs must set onLiveEmit = NULL
 * (recursion prevention rule from particle_system.h).
 */
static void SpawnDustBurst(Vector3 pos, float scale)
{
    float radialStep = (2.0f * PI) / (float)DUST_PARTICLES;

    for (int p = 0; p < DUST_PARTICLES; p++) {
        float angle    = (float)p * radialStep;
        float speed    = 55.0f + (float)GetRandomValue(0, 45);
        float upSpeed  = 22.0f + (float)GetRandomValue(0, 38);
        float lifetime = 0.50f + (float)GetRandomValue(0, 25) * 0.01f;

        SpawnParticle((ParticleConfig){
            .position         = pos,
            .velocity         = { cosf(angle) * speed, upSpeed, sinf(angle) * speed },
            .colorStart       = (Color){ 150, 110,  70, 255 },
            .colorEnd         = (Color){  80,  50,  20,   0 },
            .radius           = 3.5f + scale * 1.5f,
            .lifetime         = lifetime,
            .forceField       = NULL,
            .gradient         = &s_dustGrad,
            .spriteAnim       = NULL,
            .onDeathEmit      = NULL,   /* child: must be NULL */
            .onDeathEmitCount = 0,
            .onLiveEmit       = NULL,   /* child: must be NULL */
            .onLiveEmitRate   = 0.0f
        });
    }

    /* Central upward puff (larger, slower) */
    SpawnParticle((ParticleConfig){
        .position         = pos,
        .velocity         = { 0.0f, 85.0f, 0.0f },
        .colorStart       = (Color){ 160, 120,  80, 200 },
        .colorEnd         = (Color){  60,  40,  20,   0 },
        .radius           = 7.0f * scale,
        .lifetime         = 0.70f,
        .forceField       = NULL,
        .gradient         = &s_dustGrad,
        .spriteAnim       = NULL,
        .onDeathEmit      = NULL,
        .onDeathEmitCount = 0,
        .onLiveEmit       = NULL,
        .onLiveEmitRate   = 0.0f
    });
}

/* ================================================================
 * § 5  LIFECYCLE — INIT
 * ================================================================ */

void InitEarthSpikesSkill(int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;

    /* Clear spike pool */
    for (int i = 0; i < MAX_SPIKES; i++) {
        s_spikes[i] = (Spike){ 0 };
        s_spikes[i].state = SPIKE_INACTIVE;
    }

    /* Textures & shaders */
    s_crackTex = LoadTexture("assets/textures/crack.png");

    s_dissolveShader = LoadShader(
        NULL,                                                /* default vertex shader */
        "skills/earth/earth_spikes/earth_spikes.fs"
    );
    s_uDissolveLoc = GetShaderLocation(s_dissolveShader, "u_dissolve");
    s_uTimeLoc     = GetShaderLocation(s_dissolveShader, "u_time");

    /* Dust colour gradient: warm sand → dark soil → transparent */
    ColorGradient_AddStop(&s_dustGrad, 0.00f, (Color){ 150, 110,  70, 255 });
    ColorGradient_AddStop(&s_dustGrad, 0.50f, (Color){ 100,  70,  40, 180 });
    ColorGradient_AddStop(&s_dustGrad, 1.00f, (Color){  60,  40,  20,   0 });
}

/* ================================================================
 * § 6  LIFECYCLE — CAST
 * ================================================================ */

void CastEarthSpikesSkill(Vector3 startPos, Vector3 target, SkillParams params)
{
    /*
     * Project onto the XZ plane (spikes erupt from flat ground).
     * Clamp the line length so no spike lands beyond mid-range.
     */
    Vector3 toTarget = {
        target.x - startPos.x,
        0.0f,
        target.z - startPos.z
    };
    float dist = Vector3Length(toTarget);
    if (dist < 1.0f) return;   /* degenerate: target on caster position */

    /* Normalised direction */
    Vector3 dir = Vector3Scale(toTarget, 1.0f / dist);

    /* Total usable line length (last spike caps at dist or max range) */
    float maxLine  = (float)(SPIKES_PER_CAST - 1) * SPIKE_SPACING;
    float lineLen  = (dist < maxLine) ? dist : maxLine;
    float stepSize = (SPIKES_PER_CAST > 1)
                       ? (lineLen / (float)(SPIKES_PER_CAST - 1))
                       : 0.0f;

    float spawnScale = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
    float baseDmg = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params);
    float baseKb = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params);

    for (int i = 0; i < SPIKES_PER_CAST; i++) {
        int slot = FindFreeSlot();
        if (slot < 0) break;   /* pool exhausted — skip remaining spikes */

        float offset = (float)i * stepSize;

        s_spikes[slot] = (Spike){
            .pos           = { startPos.x + dir.x * offset, 0.0f,
                               startPos.z + dir.z * offset },
            .delay         = (float)i * SPIKE_DELAY,
            .riseTimer     = 0.0f,
            .holdTimer     = SPIKE_HOLD_TIME,
            .dissolveTimer = 0.0f,
            .scale         = spawnScale,
            .state         = SPIKE_WAITING,
            .dealtDamage   = false,
            .spawnedDecal  = false,
            .spawnedLight  = false,
            .damage        = baseDmg,
            .knockback     = baseKb,
        };
    }
}

/* ================================================================
 * § 7  LIFECYCLE — UPDATE
 * ================================================================ */

void UpdateEarthSpikesSkill(float dt, Vector3 enemyPos, float enemyRadius)
{
    for (int i = 0; i < MAX_SPIKES; i++) {
        Spike *sp = &s_spikes[i];
        if (sp->state == SPIKE_INACTIVE) continue;

        switch (sp->state) {

        /* ── Phase 1: Waiting ─────────────────────────────────────── */
        case SPIKE_WAITING:
            sp->delay -= dt;
            if (sp->delay <= 0.0f) {
                sp->state     = SPIKE_RISING;
                sp->riseTimer = 0.0f;

                /* Burst VFX: particles, screen distortion, camera shake */
                SpawnDustBurst(sp->pos, sp->scale);
                ScreenDistort_AddSource(sp->pos, DISTORT_RADIUS,
                                        DISTORT_STRENGTH, DISTORT_STRENGTH,
                                        DISTORT_SPEED);
                CameraFX_Shake(SHAKE_TRAUMA);
            }
            break;

        /* ── Phase 2: Rising ──────────────────────────────────────── */
        case SPIKE_RISING:
            sp->riseTimer += dt;

            /* One-shot: stamp crack decal at ground level */
            if (!sp->spawnedDecal) {
                Decal_Spawn(
                    sp->pos,
                    (float)GetRandomValue(0, 360),
                    SPIKE_BASE_RADIUS * sp->scale * 2.4f,
                    s_crackTex,
                    6.0f,
                    WHITE
                );
                sp->spawnedDecal = true;
            }

            /* One-shot: spawn a warm VFX point-light at the base */
            if (!sp->spawnedLight) {
                VFXLight_Spawn(sp->pos,
                               (Color){ 200, 140, 60, 255 },
                               LIGHT_RADIUS * sp->scale,
                               LIGHT_LIFETIME);
                sp->spawnedLight = true;
            }

            /*
             * Deal AoE damage once — at the halfway point of the rise
             * animation (spike is visually striking the ground level).
             *
             * ── Combat integration point ─────────────────────────────
             *    float dmg = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params);
             *    float kb  = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params);
             *    Enemy_ApplyDamage(dmg);
             *    Enemy_ApplyKnockback(dirToEnemy, kb);
             *    Enemy_ApplyRoot(1.0f);   // 1 s immobilise
             * ────────────────────────────────────────────────────────
             */
            if (!sp->dealtDamage &&
                sp->riseTimer >= SPIKE_RISE_TIME * 0.5f)
            {
                float dx     = enemyPos.x - sp->pos.x;
                float dz     = enemyPos.z - sp->pos.z;
                float distSq = dx * dx + dz * dz;
                float hitRad = (AOE_RADIUS + enemyRadius) * sp->scale;

                if (distSq <= hitRad * hitRad) {
                    char dmgText[16];
                    snprintf(dmgText, sizeof(dmgText), "%d", (int)sp->damage);
                    AddFloatingText(sp->pos, dmgText, (Color){ 139, 69, 19, 255 }, 24.0f, 0.7f);
                    AddFloatingText(sp->pos, "ROOTED!", LIME, 16.0f, 0.8f);

                    AddRootToEnemy(1.0f);

                    Vector3 pushDir = { dx, 0.0f, dz };
                    if (dx == 0.0f && dz == 0.0f) {
                        pushDir = (Vector3){ 0.0f, 0.0f, 1.0f };
                    } else {
                        pushDir = Vector3Normalize(pushDir);
                    }
                    AddKnockbackToEnemy(Vector3Scale(pushDir, sp->knockback));
                }
                sp->dealtDamage = true;   /* mark per-spike, regardless of hit */
            }

            if (sp->riseTimer >= SPIKE_RISE_TIME) {
                sp->state     = SPIKE_HOLDING;
                sp->holdTimer = SPIKE_HOLD_TIME;
            }
            break;

        /* ── Phase 3: Holding ─────────────────────────────────────── */
        case SPIKE_HOLDING:
            sp->holdTimer -= dt;
            if (sp->holdTimer <= 0.0f) {
                sp->state         = SPIKE_DISSOLVING;
                sp->dissolveTimer = 0.0f;
            }
            break;

        /* ── Phase 4: Dissolving ──────────────────────────────────── */
        case SPIKE_DISSOLVING:
            sp->dissolveTimer += dt;
            if (sp->dissolveTimer >= SPIKE_DISSOLVE_TIME) {
                sp->state = SPIKE_INACTIVE;
            }
            break;

        default:
            break;
        }
    }
}

/* ================================================================
 * § 8  LIFECYCLE — DRAW
 * ================================================================ */

void DrawEarthSpikesSkill(void)
{
    float currentTime = (float)GetTime();

    for (int i = 0; i < MAX_SPIKES; i++) {
        const Spike *sp = &s_spikes[i];

        /* Skip invisible / pre-emergence spikes */
        if (sp->state == SPIKE_INACTIVE || sp->state == SPIKE_WAITING) continue;

        /* ── Compute per-frame geometry params ──────────────────── */
        float heightRatio = 1.0f;
        float dissolveAmt = 0.0f;

        if (sp->state == SPIKE_RISING) {
            /* Ease-out quadratic: fast start, decelerates at peak */
            float t   = sp->riseTimer / SPIKE_RISE_TIME;
            float inv = 1.0f - t;
            heightRatio = 1.0f - (inv * inv);
        }
        else if (sp->state == SPIKE_DISSOLVING) {
            heightRatio = 1.0f;
            dissolveAmt = sp->dissolveTimer / SPIKE_DISSOLVE_TIME;
        }

        float currentHeight = SPIKE_MAX_HEIGHT * sp->scale * heightRatio;
        float currentRadius = SPIKE_BASE_RADIUS * sp->scale;

        /* Skip degenerate draw (avoids GPU artefacts on tiny geometry) */
        if (currentHeight < 0.5f) continue;

        /*
         * Spike is split into two DrawCylinder calls:
         *   [body] – thick hexagonal cylinder, tapers toward the top
         *   [tip]  – sharp cone that continues from the body's top edge
         *
         * Both share the total `currentHeight`.
         * DrawCylinder centres the mesh at `position`, so the bottom of
         * a cylinder of height H at position.y is at  position.y – H/2.
         */
        float bodyH = currentHeight * SPIKE_BODY_FRAC;
        float tipH  = currentHeight * SPIKE_TIP_FRAC;

        /* Body: base at y=0, top at y=bodyH → centre at y=bodyH/2 */
        Vector3 bodyPos = { sp->pos.x, bodyH * 0.5f,           sp->pos.z };
        /* Tip : base at y=bodyH, top at y=currentHeight           */
        Vector3 tipPos  = { sp->pos.x, bodyH + tipH * 0.5f,    sp->pos.z };

        /* Colours: earthy dark-brown body, slightly darker tip */
        Color bodyColor = (Color){ 108,  70,  33, 255 };
        Color tipColor  = (Color){  82,  52,  22, 255 };
        Color wireColor = (Color){  55,  36,  12,  80 };

        /* ── Apply dissolve shader for the crumble-back phase ───── */
        if (sp->state == SPIKE_DISSOLVING) {
            BeginShaderMode(s_dissolveShader);
            SetShaderValue(s_dissolveShader, s_uDissolveLoc,
                           &dissolveAmt, SHADER_UNIFORM_FLOAT);
            SetShaderValue(s_dissolveShader, s_uTimeLoc,
                           &currentTime, SHADER_UNIFORM_FLOAT);
        }

        /* ── Body cylinder (hexagonal cross-section, 6 slices) ──── */
        DrawCylinder(bodyPos,
                     currentRadius * SPIKE_TOP_RATIO,   /* narrowed top   */
                     currentRadius,                      /* wide base      */
                     bodyH,
                     6,
                     bodyColor);

        /* ── Tip cone (point at top, blends with body's top rim) ── */
        DrawCylinder(tipPos,
                     0.0f,                              /* sharp point    */
                     currentRadius * SPIKE_TOP_RATIO,   /* matches body   */
                     tipH,
                     6,
                     tipColor);

        if (sp->state == SPIKE_DISSOLVING) {
            EndShaderMode();
        }

        /* ── Wireframe overlay for edge definition (always drawn) ── */
        DrawCylinderWires(bodyPos,
                          currentRadius * SPIKE_TOP_RATIO,
                          currentRadius,
                          bodyH, 6,
                          wireColor);
    }
}

/* ================================================================
 * § 9  LIFECYCLE — UNLOAD
 * ================================================================ */

void UnloadEarthSpikesSkill(void)
{
    UnloadTexture(s_crackTex);
    UnloadShader(s_dissolveShader);
}
