#include "skills/earth/stone_spear/stone_spear_skill.h"
#include "core/particle_system.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/screen_distort.h"
#include "core/camera_fx.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// STONE SPEAR (Thach Tien) - Earth element, long-range projectile.
// Flies straight, shatters into rock fragments on impact or max range.
// Mesh: hand-built hexagonal bipyramid (two stacked cones sharing a base
// ring) with UVs running along the shaft so the dissolve shader can erode
// the spear from tip (v=0) to tail (v=1).
// ---------------------------------------------------------------------------

#define MAX_SPEARS          8
#define SPEAR_RADIAL_SEGS    6      // hexagonal cross-section at the waist
#define SPEAR_SPEED        900.f    // units/sec - fast straight-line flight
#define SPEAR_MAX_RANGE     1100.f
#define SPEAR_LENGTH        70.f    // base length before sizeScale
#define SPEAR_WAIST_RADIUS  6.5f    // base waist radius before sizeScale
#define SPEAR_LIFETIME       2.5f   // safety despawn if it never hits/ranges out
#define SPEAR_SHATTER_FRAGS  14
#define DISSOLVE_DURATION    0.35f  // seconds to fully erode tip->tail on natural death

typedef enum {
    SPEAR_STATE_FLYING = 0,
    SPEAR_STATE_DISSOLVING   // hit max range naturally; eroding before despawn
} SpearState;

typedef struct {
    Vector3   pos;          // tip-trailing pivot (logical center of the spear)
    Vector3   dir;          // normalized flight direction
    float     distanceTraveled;
    float     life;
    float     scale;        // from SkillParams.sizeScale
    float     dissolveT;    // 0 = fully solid, 1 = fully eroded (tip->tail)
    SpearState state;
    bool      active;
} Spear;

static Spear spears[MAX_SPEARS];

static Texture2D crackTex;
static ColorGradient dustGrad;

static Shader   dissolveShader;
static int      dissolveLoc;     // u_dissolve uniform location
static int      timeLoc;         // u_time uniform location (for glow flicker)

void InitStoneSpearSkill(int screenWidth, int screenHeight) {
    (void)screenWidth; (void)screenHeight;

    crackTex = LoadTexture("assets/textures/crack.png");

    ColorGradient_AddStop(&dustGrad, 0.0f, (Color){160, 130, 90, 255});
    ColorGradient_AddStop(&dustGrad, 1.0f, (Color){90, 80, 70, 0});

    dissolveShader = LoadShader(0, "skills/earth/stone_spear/stone_spear.fs");
    dissolveLoc = GetShaderLocation(dissolveShader, "u_dissolve");
    timeLoc     = GetShaderLocation(dissolveShader, "u_time");

    for (int i = 0; i < MAX_SPEARS; i++) spears[i].active = false;
}

void CastStoneSpearSkill(Vector3 startPos, Vector3 target, SkillParams params) {
    for (int i = 0; i < MAX_SPEARS; i++) {
        if (spears[i].active) continue;

        Vector3 toTarget = Vector3Subtract(target, startPos);
        // Keep the spear flying on a flat-ish plane (no homing on Y) unless
        // the caster explicitly aimed up/down; normalize whatever direction
        // was given so straight-line flight stays consistent at any pitch.
        Vector3 dir = Vector3Length(toTarget) > 0.0001f
            ? Vector3Normalize(toTarget)
            : (Vector3){1.f, 0.f, 0.f};

        spears[i] = (Spear){
            .pos = startPos,
            .dir = dir,
            .distanceTraveled = 0.f,
            .life = SPEAR_LIFETIME,
            .scale = params.sizeScale,
            .dissolveT = 0.f,
            .state = SPEAR_STATE_FLYING,
            .active = true
        };
        break;
    }
}

static void ShatterSpear(Spear *s) {
    CameraFX_Shake(0.35f);
    ScreenDistort_AddSource(s->pos, 90.f, 0.5f, 0.5f, 180.f);
    Decal_Spawn(s->pos, (float)GetRandomValue(0, 360), 1.6f * s->scale, crackTex, 3.5f, WHITE);

    for (int p = 0; p < SPEAR_SHATTER_FRAGS; p++) {
        Vector3 burstVel = {
            (float)GetRandomValue(-180, 180),
            (float)GetRandomValue(60, 220),
            (float)GetRandomValue(-180, 180)
        };
        SpawnParticle((ParticleConfig){
            s->pos, burstVel,
            (Color){150, 120, 90, 255}, (Color){60, 55, 50, 0},
            5.f * s->scale, 0.8f,
            NULL, &dustGrad, NULL,
            NULL, 0,
            NULL, 0.f
        });
    }
}

void UpdateStoneSpearSkill(float dt, Vector3 enemyPos, float enemyRadius) {
    for (int i = 0; i < MAX_SPEARS; i++) {
        if (!spears[i].active) continue;
        Spear *s = &spears[i];

        if (s->state == SPEAR_STATE_FLYING) {
            float step = SPEAR_SPEED * dt;
            s->pos = Vector3Add(s->pos, Vector3Scale(s->dir, step));
            s->distanceTraveled += step;

            // Thin dust trail while flying (cheap, low-rate point particles).
            SpawnParticle((ParticleConfig){
                s->pos, (Vector3){0.f, 4.f, 0.f},
                (Color){170, 140, 100, 200}, (Color){90, 80, 70, 0},
                3.f * s->scale, 0.35f,
                NULL, &dustGrad, NULL,
                NULL, 0,
                NULL, 0.f
            });

            float hitDist = (SPEAR_WAIST_RADIUS * s->scale) + enemyRadius;
            if (Vector3Distance(s->pos, enemyPos) <= hitDist) {
                ShatterSpear(s);
                s->active = false;
                continue;
            }

            if (s->distanceTraveled >= SPEAR_MAX_RANGE) {
                // Out of range: don't shatter violently, just erode away.
                s->state = SPEAR_STATE_DISSOLVING;
                s->dissolveT = 0.f;
            }
        } else { // SPEAR_STATE_DISSOLVING
            s->dissolveT += dt / DISSOLVE_DURATION;
            if (s->dissolveT >= 1.f) {
                s->active = false;
                continue;
            }
        }

        s->life -= dt;
        if (s->life <= 0.f) s->active = false;
    }
}

// Build one hexagonal bipyramid (two 6-sided cones sharing a waist ring),
// elongated along the spear's flight direction. UV.v runs 0 (tip) -> 1 (tail)
// so the dissolve shader can erode point-first.
static void DrawSpearMesh(const Spear *s) {
    float halfLen = (SPEAR_LENGTH * 0.5f) * s->scale;
    float waistR  = SPEAR_WAIST_RADIUS * s->scale;

    Vector3 tipPos  = Vector3Add(s->pos, Vector3Scale(s->dir, halfLen));
    Vector3 tailPos = Vector3Add(s->pos, Vector3Scale(s->dir, -halfLen));

    // Local frame around the flight axis.
    Vector3 axis = s->dir;
    Vector3 upTemp = (fabsf(axis.y) > 0.99f) ? (Vector3){1.f, 0.f, 0.f} : (Vector3){0.f, 1.f, 0.f};
    Vector3 rightVec = Vector3Normalize(Vector3CrossProduct(upTemp, axis));
    Vector3 upVec    = Vector3CrossProduct(axis, rightVec);

    Vector3 waistRing[SPEAR_RADIAL_SEGS];
    Vector3 waistNormals[SPEAR_RADIAL_SEGS];
    for (int j = 0; j < SPEAR_RADIAL_SEGS; j++) {
        float phi = j * (2.f * PI) / SPEAR_RADIAL_SEGS;
        Vector3 n = Vector3Add(Vector3Scale(rightVec, cosf(phi)), Vector3Scale(upVec, sinf(phi)));
        waistNormals[j] = n;
        waistRing[j] = Vector3Add(s->pos, Vector3Scale(n, waistR));
    }

    // Tip-side normal blend (cone surface normal ~ midway between radial dir and axis).
    Vector3 tipNormals[SPEAR_RADIAL_SEGS];
    Vector3 tailNormals[SPEAR_RADIAL_SEGS];
    for (int j = 0; j < SPEAR_RADIAL_SEGS; j++) {
        tipNormals[j]  = Vector3Normalize(Vector3Add(waistNormals[j], Vector3Scale(axis, 0.6f)));
        tailNormals[j] = Vector3Normalize(Vector3Add(waistNormals[j], Vector3Scale(axis, -0.6f)));
    }

    const float vTip   = 0.0f;
    const float vWaist  = 0.5f;
    const float vTail  = 1.0f;

    rlPushMatrix();
    rlBegin(RL_TRIANGLES);

    // Tip cone: tip vertex fanned around the waist ring.
    for (int j = 0; j < SPEAR_RADIAL_SEGS; j++) {
        int next = (j + 1) % SPEAR_RADIAL_SEGS;
        float u1 = (float)j / SPEAR_RADIAL_SEGS;
        float u2 = (float)next / SPEAR_RADIAL_SEGS;
        if (next == 0) u2 = 1.0f; // avoid UV seam wrap-around

        rlNormal3f(tipNormals[j].x, tipNormals[j].y, tipNormals[j].z);
        rlTexCoord2f(u1, vTip);
        rlVertex3f(tipPos.x, tipPos.y, tipPos.z);

        rlNormal3f(waistNormals[j].x, waistNormals[j].y, waistNormals[j].z);
        rlTexCoord2f(u1, vWaist);
        rlVertex3f(waistRing[j].x, waistRing[j].y, waistRing[j].z);

        rlNormal3f(waistNormals[next].x, waistNormals[next].y, waistNormals[next].z);
        rlTexCoord2f(u2, vWaist);
        rlVertex3f(waistRing[next].x, waistRing[next].y, waistRing[next].z);
    }

    // Tail cone: tail vertex fanned around the waist ring (wound opposite
    // so backface culling keeps the outer shell consistent).
    for (int j = 0; j < SPEAR_RADIAL_SEGS; j++) {
        int next = (j + 1) % SPEAR_RADIAL_SEGS;
        float u1 = (float)j / SPEAR_RADIAL_SEGS;
        float u2 = (float)next / SPEAR_RADIAL_SEGS;
        if (next == 0) u2 = 1.0f;

        rlNormal3f(waistNormals[j].x, waistNormals[j].y, waistNormals[j].z);
        rlTexCoord2f(u1, vWaist);
        rlVertex3f(waistRing[j].x, waistRing[j].y, waistRing[j].z);

        rlNormal3f(tailNormals[j].x, tailNormals[j].y, tailNormals[j].z);
        rlTexCoord2f(u1, vTail);
        rlVertex3f(tailPos.x, tailPos.y, tailPos.z);

        rlNormal3f(tailNormals[next].x, tailNormals[next].y, tailNormals[next].z);
        rlTexCoord2f(u2, vTail);
        rlVertex3f(tailPos.x, tailPos.y, tailPos.z);

        // second tri to close the quad-like wedge with the waist ring
        rlNormal3f(waistNormals[j].x, waistNormals[j].y, waistNormals[j].z);
        rlTexCoord2f(u1, vWaist);
        rlVertex3f(waistRing[j].x, waistRing[j].y, waistRing[j].z);

        rlNormal3f(waistNormals[next].x, waistNormals[next].y, waistNormals[next].z);
        rlTexCoord2f(u2, vWaist);
        rlVertex3f(waistRing[next].x, waistRing[next].y, waistRing[next].z);

        rlNormal3f(tailNormals[next].x, tailNormals[next].y, tailNormals[next].z);
        rlTexCoord2f(u2, vTail);
        rlVertex3f(tailPos.x, tailPos.y, tailPos.z);
    }

    rlEnd();
    rlPopMatrix();
}

void DrawStoneSpearSkill(void) {
    float time = (float)GetTime();

    BeginShaderMode(dissolveShader);
    SetShaderValue(dissolveShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);

    for (int i = 0; i < MAX_SPEARS; i++) {
        if (!spears[i].active) continue;
        Spear *s = &spears[i];

        // Flying spears stay fully solid; dissolving spears erode tip->tail
        // over DISSOLVE_DURATION. dissolveT maps directly to the shader's
        // erosion threshold against vertex UV.v (0 at tip, 1 at tail).
        float dissolveValue = (s->state == SPEAR_STATE_DISSOLVING) ? s->dissolveT : 0.f;
        SetShaderValue(dissolveShader, dissolveLoc, &dissolveValue, SHADER_UNIFORM_FLOAT);

        DrawSpearMesh(s);
    }

    EndShaderMode();
}

void UnloadStoneSpearSkill(void) {
    UnloadTexture(crackTex);
    UnloadShader(dissolveShader);
}

bool IsStoneSpearSkillCoiling(void) {
    return false; // Stone Spear is a straight-line projectile, never pins the enemy.
}

int GetStoneSpearSkillProjectiles(SkillProjectile *outProjectiles, int maxProjectiles) {
    int count = 0;
    for (int i = 0; i < MAX_SPEARS && count < maxProjectiles; i++) {
        if (!spears[i].active) continue;
        outProjectiles[count].position = spears[i].pos;
        outProjectiles[count].radius = SPEAR_WAIST_RADIUS * spears[i].scale;
        outProjectiles[count].active = true;
        count++;
    }
    return count;
}

void DeactivateStoneSpearProjectile(int index) {
    if (index < 0 || index >= MAX_SPEARS) return;
    if (!spears[index].active) return;
    ShatterSpear(&spears[index]);
    spears[index].active = false;
}
