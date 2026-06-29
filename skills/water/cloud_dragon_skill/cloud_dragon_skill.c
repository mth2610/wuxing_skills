// =============================================================================
//  VAaN LONG TRIEU — Cloud Dragon Tide  (Hệ Thuỷ · Tầm Trung)
//  Ba xoắn ốc thuỷ lực hợp thành triple helix, xoáy về phía địch.
//
//  Phases:
//    COIL  (0.55 s) : Ba cầu nước quay quanh caster, tích tụ năng lượng
//    SURGE (1.40 s) : Ba ống Bezier xoắn cùng nhau lao về target
//    FADE  (0.38 s) : Dissolve-noise tan biến
// =============================================================================

#include "skills/water/cloud_dragon_skill/cloud_dragon_skill.h"
#include "core/camera_fx.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/particle_system.h"
#include "core/procedural_mesh_utils.h"
#include "core/resource_manager.h"
#include "core/screen_distort.h"
#include "core/vfx_light.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
//  CONSTANTS
// ---------------------------------------------------------------------------
#define MAX_DRAGONS 2
#define TUBE_SEGS 28
#define RADIAL_SEGS 12
#define HELIX_COUNT 3
#define TUBE_BASE_RADIUS 5.5f
#define HELIX_MAX_RADIUS 19.0f // max radial spread of the helix
#define ORB_ORBIT_RADIUS 14.0f
#define ORB_RADIUS_BASE 6.2f

#define COIL_TIME 0.55f
#define SURGE_SPEED 1.15f // progress units / second
#define SURGE_LIFE 1.40f  // total duration of surge phase
#define FADE_TIME 0.38f
#define IMPACT_THRESHOLD 0.87f // progress at which collision is checked

// ---------------------------------------------------------------------------
//  STATE
// ---------------------------------------------------------------------------
typedef enum { PHASE_COIL, PHASE_SURGE, PHASE_FADE, PHASE_DEAD } CDPhase;

typedef struct {
  bool active;
  CDPhase phase;
  float phaseTimer;
  float progress; // 0→1 during SURGE
  float dissolve; // 0→1 during FADE
  float scale;
  float damage;
  float knockback;
  bool impactTriggered;
  Vector3 p0, p1, p2, p3; // central Bezier path (world space)
} CloudDragonState;

static CloudDragonState s_dragons[MAX_DRAGONS];

static Shader s_tubeShader;
static Shader s_orbShader;
static Texture2D s_causticsTex;

static int s_tubeDissolveL, s_tubeColorL;
static int s_orbGlowTL, s_orbColorL;

static ColorGradient s_splashGrad;
static ColorGradient s_mistGrad;

// ---------------------------------------------------------------------------
//  BEZIER HELPERS
// ---------------------------------------------------------------------------
static Vector3 BezierPt(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                        float t) {
  float u = 1.0f - t;
  return Vector3Add(Vector3Scale(p0, u * u * u),
                    Vector3Add(Vector3Scale(p1, 3.f * u * u * t),
                               Vector3Add(Vector3Scale(p2, 3.f * u * t * t),
                                          Vector3Scale(p3, t * t * t))));
}

static Vector3 BezierDer(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3,
                         float t) {
  float u = 1.0f - t;
  return Vector3Add(
      Vector3Scale(Vector3Subtract(p1, p0), 3.f * u * u),
      Vector3Add(Vector3Scale(Vector3Subtract(p2, p1), 6.f * u * t),
                 Vector3Scale(Vector3Subtract(p3, p2), 3.f * t * t)));
}

// World-space position of helix strand k at bezier parameter t.
// Strands converge at both endpoints (sin envelope = 0 at t=0 and t=1),
// reaching max radial spread in the middle — the triple-helix effect.
static Vector3 HelixCenter(const CloudDragonState *d, int k, float t,
                           float time) {
  t = Clamp(t, 0.001f, 0.999f);

  Vector3 center = BezierPt(d->p0, d->p1, d->p2, d->p3, t);
  Vector3 rawDer = BezierDer(d->p0, d->p1, d->p2, d->p3, t);
  Vector3 tang = Vector3Normalize(rawDer);
  if (Vector3LengthSqr(tang) < 0.001f)
    tang = (Vector3){0.f, 1.f, 0.f};

  Vector3 up0 =
      (fabsf(tang.y) > 0.99f) ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
  Vector3 right = Vector3Normalize(Vector3CrossProduct(up0, tang));
  Vector3 up = Vector3CrossProduct(tang, right);

  float envelope = sinf(t * 3.14159f); // tapers to 0 at both ends
  float angle = k * (2.0f * 3.14159f / HELIX_COUNT) +
                time * 5.0f            // continuous rotation
                + t * 3.0f * 3.14159f; // 1.5 full twists along length
  float r = HELIX_MAX_RADIUS * d->scale * envelope;

  return Vector3Add(center, Vector3Add(Vector3Scale(right, cosf(angle) * r),
                                       Vector3Scale(up, sinf(angle) * r)));
}

// ---------------------------------------------------------------------------
//  IMPACT VFX
// ---------------------------------------------------------------------------
static void TriggerImpact(CloudDragonState *d) {
  Vector3 pos = BezierPt(d->p0, d->p1, d->p2, d->p3, 1.0f);

  // Combat
  ApplyAoEDamage(pos, 33.0f * d->scale, d->damage, d->knockback);

  // Screen effects
  ScreenDistort_Add(pos, 96.0f, 0.88f, 0.58f, 145.0f);
  CameraFX_Shake(0.55f);

  // Caustic flood decal
  DecalSystem_Add((Vector3){pos.x, pos.y + 0.02f, pos.z},
                  (float)GetRandomValue(0, 360) * DEG2RAD,
                  33.0f * d->scale * 5.2f, s_causticsTex, 3.8f,
                  ColorAlpha(ELEMENT_COLOR_WATER, 0.78f));

  // Main flash light
  VFXLight_Spawn(pos, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.38f),
                 78.0f * d->scale, 0.68f);

  // 24 radial water droplets
  for (int i = 0; i < 24; i++) {
    float a = (float)i * (2.0f * 3.14159f / 24.0f);
    float sp = (float)GetRandomValue(60, 125) * d->scale;
    SpawnParticle((ParticleConfig){
        pos,
        (Vector3){cosf(a) * sp, (float)GetRandomValue(38, 95) * d->scale,
                  sinf(a) * sp},
        ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.38f), (Color){0, 0, 0, 0},
        (float)GetRandomValue(18, 50) / 10.0f * d->scale,
        (float)GetRandomValue(5, 13) / 10.0f, NULL, &s_splashGrad, NULL, NULL,
        0, NULL, 0.f});
  }

  // 8 upward splash jets (narrower)
  for (int i = 0; i < 8; i++) {
    float a = (float)i * (2.0f * 3.14159f / 8.0f);
    float sp = (float)GetRandomValue(20, 55) * d->scale;
    SpawnParticle((ParticleConfig){
        pos,
        (Vector3){cosf(a) * sp * 0.3f,
                  (float)GetRandomValue(90, 180) * d->scale,
                  sinf(a) * sp * 0.3f},
        ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.55f), (Color){0, 0, 0, 0},
        (float)GetRandomValue(10, 28) / 10.0f * d->scale,
        (float)GetRandomValue(5, 10) / 10.0f, NULL, &s_splashGrad, NULL, NULL,
        0, NULL, 0.f});
  }

  // 3 satellite lights positioned where each strand converged
  for (int k = 0; k < HELIX_COUNT; k++) {
    float a = k * (2.0f * 3.14159f / HELIX_COUNT);
    Vector3 lp = {pos.x + cosf(a) * 20.0f * d->scale, pos.y + 4.0f,
                  pos.z + sinf(a) * 20.0f * d->scale};
    VFXLight_Spawn(lp, ELEMENT_COLOR_WATER, 30.0f, 0.45f);
  }
}

// ---------------------------------------------------------------------------
//  INIT
// ---------------------------------------------------------------------------
void InitCloudDragonSkill(int w, int h) {
  (void)w;
  (void)h;
  for (int i = 0; i < MAX_DRAGONS; i++)
    s_dragons[i].active = false;

  s_causticsTex =
      ResourceManager_LoadTexture("assets/textures/water_caustics.png");

  s_tubeShader = ResourceManager_LoadShader(
      "skills/water/cloud_dragon_skill/cloud_dragon.vs",
      "skills/water/cloud_dragon_skill/tube.fs");
  s_orbShader = ResourceManager_LoadShader(
      "skills/water/cloud_dragon_skill/cloud_dragon.vs",
      "skills/water/cloud_dragon_skill/orb.fs");

  s_tubeDissolveL = GetShaderLocation(s_tubeShader, "u_dissolve");
  s_tubeColorL = GetShaderLocation(s_tubeShader, "u_color");
  s_orbGlowTL = GetShaderLocation(s_orbShader, "u_glowT");
  s_orbColorL = GetShaderLocation(s_orbShader, "u_color");

  // Splash gradient: icy white → deep water → transparent
  s_splashGrad.count = 0;
  ColorGradient_AddStop(&s_splashGrad, 0.00f, (Color){205, 242, 255, 245});
  ColorGradient_AddStop(&s_splashGrad, 0.28f, ELEMENT_COLOR_WATER);
  ColorGradient_AddStop(&s_splashGrad, 1.00f, (Color){0, 18, 55, 0});

  // Mist gradient: soft blue → transparent
  s_mistGrad.count = 0;
  ColorGradient_AddStop(&s_mistGrad, 0.00f,
                        ColorAlpha(ELEMENT_COLOR_WATER, 0.62f));
  ColorGradient_AddStop(
      &s_mistGrad, 0.50f,
      ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.22f), 0.32f));
  ColorGradient_AddStop(&s_mistGrad, 1.00f, (Color){0, 0, 0, 0});
}

// ---------------------------------------------------------------------------
//  CAST
// ---------------------------------------------------------------------------
void CastCloudDragonSkill(Vector3 start, Vector3 target, SkillParams params) {
  for (int i = 0; i < MAX_DRAGONS; i++) {
    if (s_dragons[i].active)
      continue;
    CloudDragonState *d = &s_dragons[i];

    Vector3 to = Vector3Subtract(target, start);
    float len = Vector3Length(to);
    float s = (params.sizeScale > 0.f) ? params.sizeScale : 1.0f;

    *d = (CloudDragonState){
        .active = true,
        .phase = PHASE_COIL,
        .phaseTimer = 0.f,
        .progress = 0.f,
        .dissolve = 0.f,
        .scale = s,
        .damage = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params),
        .knockback = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params),
        .impactTriggered = false,
        // Bezier arcs up in first third, levels off into target
        .p0 = start,
        .p1 = Vector3Add(start,
                         (Vector3){to.x * 0.27f, len * 0.44f, to.z * 0.27f}),
        .p2 = Vector3Add(start,
                         (Vector3){to.x * 0.73f, len * 0.28f, to.z * 0.73f}),
        .p3 = target};
    break;
  }
}

// ---------------------------------------------------------------------------
//  UPDATE
// ---------------------------------------------------------------------------
void UpdateCloudDragonSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  float time = (float)GetTime();

  for (int i = 0; i < MAX_DRAGONS; i++) {
    if (!s_dragons[i].active)
      continue;
    CloudDragonState *d = &s_dragons[i];
    d->phaseTimer += dt;

    // ==============================  COIL  ==============================
    if (d->phase == PHASE_COIL) {
      float ratio = Clamp(d->phaseTimer / COIL_TIME, 0.f, 1.f);

      // Each orb orbits at expanding radius, slowly rising
      float orbitR =
          ORB_ORBIT_RADIUS * d->scale * sinf(ratio * 3.14159f * 0.5f);
      float riseY = 9.0f * d->scale * ratio;

      for (int k = 0; k < HELIX_COUNT; k++) {
        float angle = k * (2.0f * 3.14159f / HELIX_COUNT) + time * 4.2f;
        Vector3 orbP = {d->p0.x + cosf(angle) * orbitR, d->p0.y + riseY,
                        d->p0.z + sinf(angle) * orbitR};

        // Swirling mist trail behind each orb
        float jx = (float)(GetRandomValue(-60, 60)) / 60.0f * 3.5f;
        float jz = (float)(GetRandomValue(-60, 60)) / 60.0f * 3.5f;
        SpawnParticle((ParticleConfig){
            (Vector3){orbP.x + jx, orbP.y, orbP.z + jz},
            (Vector3){jx * 0.4f, (float)GetRandomValue(4, 16) * d->scale * 0.4f,
                      jz * 0.4f},
            ColorAlpha(ELEMENT_COLOR_WATER, 0.58f), (Color){0, 0, 0, 0},
            3.0f * d->scale * (GetRandomValue(80, 115) / 100.0f), 0.42f, NULL,
            &s_mistGrad, NULL, NULL, 0, NULL, 0.f});

        // Occasional bright spark upward
        if (GetRandomValue(0, 9) == 0) {
          SpawnParticle((ParticleConfig){
              orbP,
              (Vector3){0.f, (float)GetRandomValue(22, 55) * d->scale, 0.f},
              ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.45f), (Color){0, 0, 0, 0},
              1.6f * d->scale, 0.30f, NULL, &s_splashGrad, NULL, NULL, 0, NULL,
              0.f});
        }
      }

      // Pulsing central gather light
      if (GetRandomValue(0, 5) == 0) {
        VFXLight_Spawn(d->p0, ColorAlpha(ELEMENT_COLOR_WATER, 0.42f),
                       24.0f * d->scale * ratio, 0.13f);
      }

      if (d->phaseTimer >= COIL_TIME) {
        d->phase = PHASE_SURGE;
        d->phaseTimer = 0.f;
        d->progress = 0.f;
      }
    }

    // ==============================  SURGE  ==============================
    else if (d->phase == PHASE_SURGE) {
      d->progress = Clamp(d->progress + SURGE_SPEED * dt, 0.f, 1.f);

      // Spawn head particles at each strand tip
      for (int k = 0; k < HELIX_COUNT; k++) {
        Vector3 head = HelixCenter(d, k, d->progress, time);
        Vector3 cder = BezierDer(d->p0, d->p1, d->p2, d->p3,
                                 Clamp(d->progress, 0.001f, 0.999f));
        Vector3 vel = Vector3Scale(Vector3Normalize(cder), 13.0f * d->scale);
        float scl = GetRandomValue(78, 122) / 100.0f;

        SpawnParticle((ParticleConfig){
            head, vel, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.32f),
            (Color){0, 0, 0, 0}, 2.4f * d->scale * scl, 0.24f, NULL,
            &s_mistGrad, NULL, NULL, 0, NULL, 0.f});
      }

      // Random mid-tube drips
      if (GetRandomValue(0, 4) == 0) {
        int k = GetRandomValue(0, HELIX_COUNT - 1);
        float t = (float)GetRandomValue(5, 88) / 100.0f * d->progress;
        Vector3 drip = HelixCenter(d, k, t, time);
        SpawnParticle((ParticleConfig){
            drip, (Vector3){0.f, -(float)GetRandomValue(8, 22) * d->scale, 0.f},
            ColorAlpha(ELEMENT_COLOR_WATER, 0.52f), (Color){0, 0, 0, 0},
            1.9f * d->scale, 0.40f, NULL, &s_mistGrad, NULL, NULL, 0, NULL,
            0.f});
      }

      // Dynamic light flickering along active section
      if (GetRandomValue(0, 7) == 0) {
        int k = GetRandomValue(0, HELIX_COUNT - 1);
        float lt = (float)GetRandomValue(0, 100) / 100.0f * d->progress;
        Vector3 lp = HelixCenter(d, k, lt, time);
        VFXLight_Spawn(lp, ColorAlpha(ELEMENT_COLOR_WATER, 0.38f),
                       18.0f * d->scale, 0.09f);
      }

      // Impact detection
      if (!d->impactTriggered && d->progress >= IMPACT_THRESHOLD) {
        Vector3 impactPt = BezierPt(d->p0, d->p1, d->p2, d->p3, d->progress);
        float hitR =
            (TUBE_BASE_RADIUS + HELIX_MAX_RADIUS) * d->scale + enemyRadius;
        if (Vector3DistanceSqr(impactPt, enemyPos) <= hitR * hitR) {
          TriggerImpact(d);
          d->impactTriggered = true;
        }
      }

      if (d->phaseTimer >= SURGE_LIFE) {
        if (!d->impactTriggered) {
          TriggerImpact(d);
          d->impactTriggered = true;
        }
        d->phase = PHASE_FADE;
        d->phaseTimer = 0.f;
        d->dissolve = 0.f;
      }
    }

    // ==============================  FADE  ==============================
    else if (d->phase == PHASE_FADE) {
      d->dissolve = d->phaseTimer / FADE_TIME;
      if (d->phaseTimer >= FADE_TIME) {
        d->active = false;
        d->phase = PHASE_DEAD;
      }
    }
  }
}

// ---------------------------------------------------------------------------
//  DRAW HELPERS
// ---------------------------------------------------------------------------

// Renders all three helix tube meshes for one dragon instance.
static void DrawTubes(CloudDragonState *d, float time) {
  // Static arrays live in BSS — safe, single-threaded
  static Vector3 rings[TUBE_SEGS + 1][RADIAL_SEGS];
  static Vector3 normals[TUBE_SEGS + 1][RADIAL_SEGS];

  for (int k = 0; k < HELIX_COUNT; k++) {
    // Build ring geometry
    for (int seg = 0; seg <= TUBE_SEGS; seg++) {
      float t = (float)seg / (float)TUBE_SEGS * d->progress;
      float tFwd = fminf(t + 0.003f, 0.999f);
      if (tFwd <= t + 0.0001f)
        tFwd = fmaxf(t - 0.003f, 0.001f);

      Vector3 pc = HelixCenter(d, k, t, time);
      Vector3 pfwd = HelixCenter(d, k, tFwd, time);
      Vector3 tang = Vector3Normalize(Vector3Subtract(pfwd, pc));
      if (Vector3LengthSqr(tang) < 0.0001f)
        tang = Vector3Normalize(
            BezierDer(d->p0, d->p1, d->p2, d->p3, Clamp(t, 0.001f, 0.999f)));

      Vector3 up0 =
          (fabsf(tang.y) > 0.99f) ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
      Vector3 rgt = Vector3Normalize(Vector3CrossProduct(up0, tang));
      Vector3 up = Vector3CrossProduct(tang, rgt);

      // Radius tapers to thin points at both ends
      float taper = sinf(t * 3.14159f);
      float r = TUBE_BASE_RADIUS * d->scale * (0.18f + 0.82f * taper);

      for (int j = 0; j < RADIAL_SEGS; j++) {
        float phi = (float)j * (2.0f * 3.14159f) / (float)RADIAL_SEGS;
        Vector3 n = Vector3Add(Vector3Scale(rgt, cosf(phi)),
                               Vector3Scale(up, sinf(phi)));
        // Organic deformation wave (from spec §8.6)
        float wave = 1.0f + 0.12f * sinf(t * 18.f + phi * 3.f + time * 10.f) +
                     0.08f * sinf(t * 9.f - phi * 5.f - time * 6.f);
        normals[seg][j] = n;
        rings[seg][j] = Vector3Add(pc, Vector3Scale(n, r * wave));
      }
    }

    // Render quad strips
    rlPushMatrix();
    rlColor4ub(255, 255, 255, 255);
    rlBegin(RL_QUADS);
    for (int seg = 0; seg < TUBE_SEGS; seg++) {
      float v1 = (float)seg / (float)TUBE_SEGS * 4.0f;
      float v2 = (float)(seg + 1) / (float)TUBE_SEGS * 4.0f;
      for (int j = 0; j < RADIAL_SEGS; j++) {
        int nxt = (j + 1) % RADIAL_SEGS;
        float u1 = (float)j / (float)RADIAL_SEGS;
        float u2 = (float)(j + 1) / (float)RADIAL_SEGS;

        rlNormal3f(normals[seg][j].x, normals[seg][j].y, normals[seg][j].z);
        rlTexCoord2f(u1, v1);
        rlVertex3f(rings[seg][j].x, rings[seg][j].y, rings[seg][j].z);

        rlNormal3f(normals[seg][nxt].x, normals[seg][nxt].y,
                   normals[seg][nxt].z);
        rlTexCoord2f(u2, v1);
        rlVertex3f(rings[seg][nxt].x, rings[seg][nxt].y, rings[seg][nxt].z);

        rlNormal3f(normals[seg + 1][nxt].x, normals[seg + 1][nxt].y,
                   normals[seg + 1][nxt].z);
        rlTexCoord2f(u2, v2);
        rlVertex3f(rings[seg + 1][nxt].x, rings[seg + 1][nxt].y,
                   rings[seg + 1][nxt].z);

        rlNormal3f(normals[seg + 1][j].x, normals[seg + 1][j].y,
                   normals[seg + 1][j].z);
        rlTexCoord2f(u1, v2);
        rlVertex3f(rings[seg + 1][j].x, rings[seg + 1][j].y,
                   rings[seg + 1][j].z);
      }
    }
    rlEnd();

    // Triangle end-caps (§8.7)
    {
      // Tail cap
      float rT = TUBE_BASE_RADIUS * d->scale * 0.18f;
      Vector3 pc0 = HelixCenter(d, k, 0.001f, time);
      Vector3 pc1 = HelixCenter(d, k, 0.004f, time);
      Vector3 tTail = Vector3Normalize(Vector3Subtract(pc1, pc0));
      Vector3 tApex = Vector3Subtract(pc0, Vector3Scale(tTail, rT * 0.28f));

      // Head cap
      float progCl = Clamp(d->progress, 0.002f, 0.998f);
      float rH = TUBE_BASE_RADIUS * d->scale *
                 (0.18f + 0.82f * sinf(d->progress * 3.14159f));
      Vector3 phd = HelixCenter(d, k, progCl, time);
      Vector3 phdb = HelixCenter(d, k, progCl - 0.003f, time);
      Vector3 tHead = Vector3Normalize(Vector3Subtract(phd, phdb));
      Vector3 hApex = Vector3Add(phd, Vector3Scale(tHead, rH * 0.28f));

      rlBegin(RL_TRIANGLES);
      for (int j = 0; j < RADIAL_SEGS; j++) {
        int next = (j + 1) % RADIAL_SEGS;

        // Tail (winding toward apex from ring[0])
        rlNormal3f(-tTail.x, -tTail.y, -tTail.z);
        rlTexCoord2f((float)j / RADIAL_SEGS, -0.1f);
        rlVertex3f(tApex.x, tApex.y, tApex.z);
        rlTexCoord2f((float)next / RADIAL_SEGS, 0.0f);
        rlVertex3f(rings[0][next].x, rings[0][next].y, rings[0][next].z);
        rlTexCoord2f((float)j / RADIAL_SEGS, 0.0f);
        rlVertex3f(rings[0][j].x, rings[0][j].y, rings[0][j].z);

        // Head (winding outward from ring[TUBE_SEGS] to apex)
        rlNormal3f(tHead.x, tHead.y, tHead.z);
        rlTexCoord2f((float)j / RADIAL_SEGS, 4.1f);
        rlVertex3f(hApex.x, hApex.y, hApex.z);
        rlTexCoord2f((float)j / RADIAL_SEGS, 4.0f);
        rlVertex3f(rings[TUBE_SEGS][j].x, rings[TUBE_SEGS][j].y,
                   rings[TUBE_SEGS][j].z);
        rlTexCoord2f((float)next / RADIAL_SEGS, 4.0f);
        rlVertex3f(rings[TUBE_SEGS][next].x, rings[TUBE_SEGS][next].y,
                   rings[TUBE_SEGS][next].z);
      }
      rlEnd();
    }

    rlPopMatrix();
  }
}

// ---------------------------------------------------------------------------
//  DRAW
// ---------------------------------------------------------------------------
void DrawCloudDragonSkill(void) {
  float time = (float)GetTime();

  for (int i = 0; i < MAX_DRAGONS; i++) {
    if (!s_dragons[i].active)
      continue;
    CloudDragonState *d = &s_dragons[i];

    // ========================  COIL: orbiting water orbs
    // ========================
    if (d->phase == PHASE_COIL) {
      float ratio = Clamp(d->phaseTimer / COIL_TIME, 0.f, 1.f);
      float glowT = ratio;

      float orbitR =
          ORB_ORBIT_RADIUS * d->scale * sinf(ratio * 3.14159f * 0.5f);
      float riseY = 9.0f * d->scale * ratio;

      float col[4] = {ELEMENT_COLOR_WATER.r / 255.f,
                      ELEMENT_COLOR_WATER.g / 255.f,
                      ELEMENT_COLOR_WATER.b / 255.f, 0.90f};

      rlDisableDepthMask();
      SkillManager_BeginShader(s_orbShader);
      SetShaderValue(s_orbShader, s_orbColorL, col, SHADER_UNIFORM_VEC4);
      SetShaderValue(s_orbShader, s_orbGlowTL, &glowT, SHADER_UNIFORM_FLOAT);

      rlColor4ub(255, 255, 255, 255);
      for (int k = 0; k < HELIX_COUNT; k++) {
        float angle = k * (2.0f * 3.14159f / HELIX_COUNT) + time * 4.2f;
        Vector3 orbPos = {d->p0.x + cosf(angle) * orbitR, d->p0.y + riseY,
                          d->p0.z + sinf(angle) * orbitR};
        // Subtle per-orb breathing variation (deterministic, no flicker)
        float breathScale = 1.0f + 0.08f * sinf((float)k * 1.37f + time * 2.8f);
        float orbR =
            ORB_RADIUS_BASE * d->scale * (0.38f + 0.62f * ratio) * breathScale;

        DrawCoreSphere(orbPos, orbR, 14, 10, ELEMENT_COLOR_WATER);
      }

      SkillManager_EndShader();
      rlEnableDepthMask();
    }

    // ========================  SURGE / FADE: helix tubes
    // ========================
    else if (d->phase == PHASE_SURGE || d->phase == PHASE_FADE) {
      float dissolve = (d->phase == PHASE_FADE) ? d->dissolve : 0.0f;
      float col[4] = {ELEMENT_COLOR_WATER.r / 255.f,
                      ELEMENT_COLOR_WATER.g / 255.f,
                      ELEMENT_COLOR_WATER.b / 255.f, 0.90f};

      rlDisableDepthMask();
      SkillManager_BeginShader(s_tubeShader);
      SetShaderValue(s_tubeShader, s_tubeDissolveL, &dissolve,
                     SHADER_UNIFORM_FLOAT);
      SetShaderValue(s_tubeShader, s_tubeColorL, col, SHADER_UNIFORM_VEC4);

      DrawTubes(d, time);

      SkillManager_EndShader();
      rlEnableDepthMask();
    }
  }
}

// ---------------------------------------------------------------------------
//  REMAINING LIFECYCLE
// ---------------------------------------------------------------------------
void UnloadCloudDragonSkill(void) {
  // ResourceManager owns texture/shader lifetimes — no unloads here
}

bool IsCloudDragonSkillCoiling(void) {
  for (int i = 0; i < MAX_DRAGONS; i++)
    if (s_dragons[i].active && s_dragons[i].phase == PHASE_COIL)
      return true;
  return false;
}

int GetCloudDragonSkillProjectiles(SkillProjectile *out, int max) {
  int c = 0;
  float time = (float)GetTime();

  for (int i = 0; i < MAX_DRAGONS; i++) {
    if (!s_dragons[i].active || s_dragons[i].phase != PHASE_SURGE)
      continue;
    CloudDragonState *d = &s_dragons[i];

    for (int k = 0; k < HELIX_COUNT && c < max; k++) {
      out[c].position =
          HelixCenter(d, k, Clamp(d->progress, 0.001f, 0.999f), time);
      out[c].radius = TUBE_BASE_RADIUS * d->scale;
      out[c].active = true;
      c++;
    }
  }
  return c;
}

void DeactivateCloudDragonProjectile(int index) {
  // Each active dragon exposes HELIX_COUNT projectiles consecutively
  int di = index / HELIX_COUNT;
  if (di < 0 || di >= MAX_DRAGONS || !s_dragons[di].active)
    return;

  CloudDragonState *d = &s_dragons[di];
  if (!d->impactTriggered) {
    TriggerImpact(d);
    d->impactTriggered = true;
  }
  d->phase = PHASE_FADE;
  d->phaseTimer = 0.f;
  d->dissolve = 0.f;
}
