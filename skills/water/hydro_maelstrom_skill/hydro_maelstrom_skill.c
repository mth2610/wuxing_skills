/*
 * ╔═══════════════════════════════════════════════════════════════════╗
 * ║            HÃI TRIỀU TRẬN  —  HYDRO MAELSTROM                    ║
 * ║  Hệ Thủy · Tầm Trung                                             ║
 * ║                                                                   ║
 * ║  Triệu hồi một cơn lốc xoáy nước cuốn hút kẻ địch vào trung     ║
 * ║  tâm xoáy. Sau khi co cụm hoàn toàn, lốc xoáy bùng phát thành   ║
 * ║  một cột nước khổng lồ bắn thẳng lên trời, gây sát thương diện   ║
 * ║  rộng.                                                            ║
 * ║                                                                   ║
 * ║  Phases:                                                          ║
 * ║    FORMING  (0.55s) – Nước hội tụ từ xung quanh tạo phễu xoáy   ║
 * ║    SPINNING (2.0s)  – Phễu xoáy đầy đủ, kéo địch vào tâm        ║
 * ║    IMPLODE  (0.35s) – Sụp đổ, màn hình rung                      ║
 * ║    GEYSER   (0.75s) – Cột nước bùng phá, gây sát thương          ║
 * ╚═══════════════════════════════════════════════════════════════════╝
 */

#include "skills/water/hydro_maelstrom_skill/hydro_maelstrom_skill.h"
#include "core/camera_fx.h"
#include "core/color_gradient.h"
#include "core/decal_system.h"
#include "core/force_field.h"
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

#ifndef PI
#define PI 3.1415926535f
#endif

// ════════════════════════════════════════════════════════════════════
//  CONSTANTS
// ════════════════════════════════════════════════════════════════════

#define MAX_MAELSTROMS 2

// Funnel mesh geometry
#define FUNNEL_RINGS 14 // Horizontal stacked rings (top→tip)
#define FUNNEL_SEGS 24  // Radial segments per ring

// Geyser tube geometry
#define GEYSER_SEGS_V 26 // Bezier path divisions
#define GEYSER_SEGS_R 18 // Radial segments

// Phase durations (seconds)
#define FORMING_TIME 0.55f
#define SPINNING_TIME 2.00f
#define IMPLODE_TIME 0.35f
#define GEYSER_TIME 0.75f

// Spatial scale constants (world units)
#define MAELSTROM_RADIUS 20.0f // Outer radius of funnel rim
#define FUNNEL_DEPTH 13.0f     // Vertical depth of the funnel cone
#define GEYSER_HEIGHT 52.0f    // Max height of the geyser column
#define GEYSER_BASE_R 3.2f     // Tube radius at geyser base

// ════════════════════════════════════════════════════════════════════
//  STATE MACHINE
// ════════════════════════════════════════════════════════════════════

typedef enum {
  MAELSTROM_IDLE,
  MAELSTROM_FORMING,
  MAELSTROM_SPINNING,
  MAELSTROM_IMPLODE,
  MAELSTROM_GEYSER
} MaelstromState;

typedef struct {
  Vector3 center;
  float timer;
  float scale;
  float spinAngle; // Accumulated Y-rotation (radians)
  float damage;
  float knockback;
  float geyserProg; // 0..1 geyser tube expansion
  float dissolve;   // 0..1 shader noise-dissolve mask
  float pulseTimer; // Countdown for rhythmic light pulses
  MaelstromState state;
  bool active;
  bool hitDealt;
} Maelstrom;

// ════════════════════════════════════════════════════════════════════
//  STATIC GLOBALS
// ════════════════════════════════════════════════════════════════════

static Maelstrom s_m[MAX_MAELSTROMS];
static Shader s_shader;
static int s_uTimeLoc;
static int s_uDissolveLoc;
static Texture2D s_causticsDecalTex;
static Texture2D s_splashDecalTex;
static ColorGradient s_waterGrad;
static ColorGradient s_foamGrad;
static ColorGradient s_geyserGrad;
static ForceField s_vortexFF;

// ════════════════════════════════════════════════════════════════════
//  UTILITY
// ════════════════════════════════════════════════════════════════════

static int FindFreeSlot(void) {
  for (int i = 0; i < MAX_MAELSTROMS; i++)
    if (!s_m[i].active)
      return i;
  return -1;
}

static Vector3 BezPt(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
  float u = 1.0f - t;
  return Vector3Add(Vector3Scale(p0, u * u * u),
                    Vector3Add(Vector3Scale(p1, 3.0f * u * u * t),
                               Vector3Add(Vector3Scale(p2, 3.0f * u * t * t),
                                          Vector3Scale(p3, t * t * t))));
}

static Vector3 BezTan(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t) {
  float u = 1.0f - t;
  return Vector3Add(
      Vector3Scale(Vector3Subtract(p1, p0), 3.0f * u * u),
      Vector3Add(Vector3Scale(Vector3Subtract(p2, p1), 6.0f * u * t),
                 Vector3Scale(Vector3Subtract(p3, p2), 3.0f * t * t)));
}

// ════════════════════════════════════════════════════════════════════
//  VFX PARTICLE HELPERS
// ════════════════════════════════════════════════════════════════════

// Forming phase: water tendrils flying inward from a wide perimeter
static void SpawnFormingTendril(Maelstrom *m) {
  float a = (float)GetRandomValue(0, 360) * DEG2RAD;
  float r =
      MAELSTROM_RADIUS * m->scale * ((float)GetRandomValue(110, 200) / 100.0f);
  float yJ = (float)GetRandomValue(0, 18) / 10.0f;
  Vector3 pos = {m->center.x + r * cosf(a), m->center.y + yJ,
                 m->center.z + r * sinf(a)};
  Vector3 inward = Vector3Normalize(Vector3Subtract(m->center, pos));
  SpawnParticle((ParticleConfig){
      .position = pos,
      .velocity = Vector3Scale(inward, 28.0f + (float)GetRandomValue(0, 18)),
      .colorStart = ELEMENT_COLOR_WATER,
      .colorEnd = ColorAlpha(ELEMENT_COLOR_WATER, 0.0f),
      .radius = 1.6f * m->scale + (float)GetRandomValue(0, 8) * 0.12f,
      .lifetime = FORMING_TIME * 0.75f,
      .forceField = NULL,
      .gradient = &s_waterGrad,
      .spriteAnim = NULL,
      .onDeathEmit = NULL,
      .onDeathEmitCount = 0,
      .onLiveEmit = NULL,
      .onLiveEmitRate = 0.0f});
}

// Spinning phase: orbital droplets with vortex force steering
static void SpawnOrbitDroplet(Maelstrom *m) {
  float a = (float)GetRandomValue(0, 360) * DEG2RAD;
  float r =
      MAELSTROM_RADIUS * m->scale * ((float)GetRandomValue(30, 95) / 100.0f);
  float fy = (float)GetRandomValue(0, 60) / 100.0f;
  Vector3 pos = {m->center.x + r * cosf(a),
                 m->center.y - fy * FUNNEL_DEPTH * m->scale,
                 m->center.z + r * sinf(a)};
  // Tangential velocity (orbital) + downward pull toward funnel tip
  Vector3 tang = Vector3Normalize((Vector3){-sinf(a), 0.0f, cosf(a)});
  float spd = 20.0f + (float)GetRandomValue(0, 12);
  Vector3 vel =
      Vector3Add(Vector3Scale(tang, spd),
                 (Vector3){0.0f, -7.0f - (float)GetRandomValue(0, 8), 0.0f});
  SpawnParticle((ParticleConfig){
      .position = pos,
      .velocity = vel,
      .colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.88f),
      .colorEnd = (Color){0, 0, 0, 0},
      .radius = 1.0f * m->scale + (float)GetRandomValue(0, 8) * 0.12f,
      .lifetime = 0.45f + (float)GetRandomValue(0, 5) * 0.08f,
      .forceField = &s_vortexFF,
      .gradient = &s_waterGrad,
      .spriteAnim = NULL,
      .onDeathEmit = NULL,
      .onDeathEmitCount = 0,
      .onLiveEmit = NULL,
      .onLiveEmitRate = 0.0f});
}

// Spinning phase: rising mist from the funnel surface
static void SpawnMist(Maelstrom *m) {
  float a = (float)GetRandomValue(0, 360) * DEG2RAD;
  float r =
      MAELSTROM_RADIUS * m->scale * ((float)GetRandomValue(5, 85) / 100.0f);
  float d = (float)GetRandomValue(0, 6) * 0.1f;
  Vector3 pos = {m->center.x + r * cosf(a),
                 m->center.y - d * FUNNEL_DEPTH * m->scale,
                 m->center.z + r * sinf(a)};
  SpawnParticle((ParticleConfig){
      .position = pos,
      .velocity =
          (Vector3){0.0f, 2.2f + (float)GetRandomValue(0, 10) * 0.25f, 0.0f},
      .colorStart =
          ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.45f), 0.52f),
      .colorEnd = ColorAlpha(WHITE, 0.0f),
      .radius = 3.0f * m->scale + (float)GetRandomValue(0, 12) * 0.1f,
      .lifetime = 0.55f + (float)GetRandomValue(0, 4) * 0.1f,
      .forceField = NULL,
      .gradient = NULL,
      .spriteAnim = NULL,
      .onDeathEmit = NULL,
      .onDeathEmitCount = 0,
      .onLiveEmit = NULL,
      .onLiveEmitRate = 0.0f});
}

// Implode phase: high-speed rush toward center
static void SpawnImplosionRush(Maelstrom *m, float implodeT) {
  float a = (float)GetRandomValue(0, 360) * DEG2RAD;
  float r = MAELSTROM_RADIUS * m->scale * (1.0f - implodeT) * 0.9f;
  Vector3 pos = {m->center.x + r * cosf(a), m->center.y,
                 m->center.z + r * sinf(a)};
  SpawnParticle((ParticleConfig){
      .position = pos,
      .velocity = Vector3Scale(
          Vector3Normalize(Vector3Subtract(m->center, pos)), 40.0f),
      .colorStart = ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.55f),
      .colorEnd = ColorAlpha(ELEMENT_COLOR_WATER, 0.0f),
      .radius = 1.3f * m->scale,
      .lifetime = 0.26f,
      .forceField = NULL,
      .gradient = &s_foamGrad,
      .spriteAnim = NULL,
      .onDeathEmit = NULL,
      .onDeathEmitCount = 0,
      .onLiveEmit = NULL,
      .onLiveEmitRate = 0.0f});
}

// Geyser phase: rising spray particles along the column
static void SpawnGeyserSpray(Maelstrom *m) {
  float a = (float)GetRandomValue(0, 360) * DEG2RAD;
  float h = (float)GetRandomValue(0, 85) / 100.0f * GEYSER_HEIGHT * m->scale *
            m->geyserProg;
  Vector3 gp = {m->center.x + cosf(a) * GEYSER_BASE_R * m->scale * 0.85f,
                m->center.y + h,
                m->center.z + sinf(a) * GEYSER_BASE_R * m->scale * 0.85f};
  SpawnParticle((ParticleConfig){
      .position = gp,
      .velocity = (Vector3){cosf(a) * (float)GetRandomValue(3, 12) * m->scale,
                            (float)GetRandomValue(22, 52) * m->scale,
                            sinf(a) * (float)GetRandomValue(3, 12) * m->scale},
      .colorStart = ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.48f),
      .colorEnd = ColorAlpha(ELEMENT_COLOR_WATER, 0.0f),
      .radius = (float)GetRandomValue(8, 24) / 10.0f * m->scale,
      .lifetime = 0.45f + (float)GetRandomValue(0, 5) * 0.09f,
      .forceField = NULL,
      .gradient = &s_geyserGrad,
      .spriteAnim = NULL,
      .onDeathEmit = NULL,
      .onDeathEmitCount = 0,
      .onLiveEmit = NULL,
      .onLiveEmitRate = 0.0f});
}

// Final geyser peak explosion: radial blast + falling droplets
static void TriggerGeyserBlast(Maelstrom *m) {
  Vector3 peak =
      Vector3Add(m->center, (Vector3){0.0f, GEYSER_HEIGHT * m->scale, 0.0f});

  ScreenDistort_Add(m->center, 130.0f, 1.0f, 0.65f, 220.0f);
  CameraFX_Shake(0.9f);
  VFXLight_Spawn(peak, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.5f),
                 100.0f * m->scale, 0.65f);

  // Splash impact: 1 large central ring + 2 smaller asymmetric satellite blobs.
  // Avoids the "perfect circle" look by letting 3 layers overlap irregularly.
  DecalSystem_Add(
      m->center, (float)GetRandomValue(0, 360),
      MAELSTROM_RADIUS * m->scale * 3.8f, s_splashDecalTex, 3.2f,
      ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.30f), 0.58f));
  for (int k = 0; k < 2; k++) {
    float a = (float)GetRandomValue(0, 360) * DEG2RAD;
    float r =
        MAELSTROM_RADIUS * m->scale * ((float)GetRandomValue(28, 50) / 100.0f);
    Vector3 sPos = {m->center.x + cosf(a) * r, m->center.y,
                    m->center.z + sinf(a) * r};
    DecalSystem_Add(
        sPos, (float)GetRandomValue(0, 360),
        MAELSTROM_RADIUS * m->scale * ((float)GetRandomValue(18, 28) / 10.0f),
        s_splashDecalTex, 2.8f,
        ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.20f), 0.40f));
  }

  // Radial burst at peak
  for (int i = 0; i < 28; i++) {
    float a = (float)i / 28.0f * 2.0f * PI;
    float spd = (float)GetRandomValue(45, 100) * m->scale;
    SpawnParticle((ParticleConfig){
        .position = peak,
        .velocity =
            (Vector3){cosf(a) * spd, (float)GetRandomValue(15, 65) * m->scale,
                      sinf(a) * spd},
        .colorStart = ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.52f),
        .colorEnd = ColorAlpha(ELEMENT_COLOR_WATER, 0.0f),
        .radius = (float)GetRandomValue(18, 52) / 10.0f * m->scale,
        .lifetime = 0.85f + (float)GetRandomValue(0, 6) * 0.1f,
        .forceField = NULL,
        .gradient = &s_geyserGrad,
        .spriteAnim = NULL,
        .onDeathEmit = NULL,
        .onDeathEmitCount = 0,
        .onLiveEmit = NULL,
        .onLiveEmitRate = 0.0f});
  }
  // Heavy gravity-pulled droplets that arc and fall back
  for (int i = 0; i < 18; i++) {
    float a = (float)GetRandomValue(0, 360) * DEG2RAD;
    float r = (float)GetRandomValue(8, 42) * m->scale;
    SpawnParticle((ParticleConfig){
        .position = peak,
        .velocity =
            (Vector3){cosf(a) * r, -(float)GetRandomValue(5, 24), sinf(a) * r},
        .colorStart = ColorAlpha(ELEMENT_COLOR_WATER, 0.9f),
        .colorEnd = ColorAlpha(ELEMENT_COLOR_WATER, 0.0f),
        .radius = (float)GetRandomValue(6, 20) / 10.0f * m->scale,
        .lifetime = 1.1f,
        .forceField = NULL,
        .gradient = &s_waterGrad,
        .spriteAnim = NULL,
        .onDeathEmit = NULL,
        .onDeathEmitCount = 0,
        .onLiveEmit = NULL,
        .onLiveEmitRate = 0.0f});
  }
}

// ════════════════════════════════════════════════════════════════════
//  PROCEDURAL MESH DRAW: FUNNEL VORTEX
//  Procedural inverted-cone made of FUNNEL_RINGS horizontal rings.
//  Rings taper quadratically from outerR (top rim) to ~zero (tip).
//  Each ring vertex is perturbed with overlapping sine waves +
//  progressive twist to create an organic swirling funnel.
// ════════════════════════════════════════════════════════════════════

static void DrawFunnelMesh(Maelstrom *m, float time) {
  // Progress: funnel grows in FORMING, full in SPINNING, shrinks in IMPLODE
  float prog;
  if (m->state == MAELSTROM_FORMING)
    prog = 1.0f - (m->timer / FORMING_TIME);
  else if (m->state == MAELSTROM_SPINNING)
    prog = 1.0f;
  else
    prog = m->timer / IMPLODE_TIME;
  if (prog < 0.01f)
    return;

  float outerR = MAELSTROM_RADIUS * m->scale * prog;
  float depth = FUNNEL_DEPTH * m->scale * prog;
  float sa = m->spinAngle;
  float slopeAngle = atan2f(outerR, depth); // Funnel wall angle for normals

  Vector3 verts[FUNNEL_RINGS + 1][FUNNEL_SEGS];
  Vector3 norms[FUNNEL_RINGS + 1][FUNNEL_SEGS];

  for (int ri = 0; ri <= FUNNEL_RINGS; ri++) {
    float frac = (float)ri / FUNNEL_RINGS;
    float ringR = outerR * (1.0f - frac * frac); // Quadratic taper

    // Height: descend from center.y, with a vertical sine wave
    float yBase = m->center.y - depth * frac;
    float yWave = 1.1f * sinf(frac * PI * 3.0f + time * 4.8f + sa * 0.5f) *
                  (1.0f - frac); // Wave damps near tip
    float ry = yBase + yWave;

    for (int si = 0; si < FUNNEL_SEGS; si++) {
      // Angle advances with accumulated spin + progressive inner twist
      float phi =
          (float)si * (2.0f * PI) / FUNNEL_SEGS + sa * (1.0f + frac * 2.2f);

      // Organic surface perturbation (two overlapping wave frequencies)
      float wave = 1.0f + 0.11f * sinf(phi * 4.0f + time * 7.5f) +
                   0.07f * sinf(phi * 7.0f - time * 10.5f + frac * PI * 1.5f);
      float r = ringR * wave;

      float cx = cosf(phi), sz = sinf(phi);
      verts[ri][si] = (Vector3){m->center.x + r * cx, ry, m->center.z + r * sz};

      // Analytical funnel-wall normal: outward + upward at slope angle
      norms[ri][si] = Vector3Normalize((Vector3){
          cx * cosf(slopeAngle), sinf(slopeAngle), sz * cosf(slopeAngle)});
    }
  }

  rlColor4ub(255, 255, 255, 255);
  rlBegin(RL_QUADS);
  for (int ri = 0; ri < FUNNEL_RINGS; ri++) {
    float v1 = (float)ri / FUNNEL_RINGS + time * 0.35f;
    float v2 = (float)(ri + 1) / FUNNEL_RINGS + time * 0.35f;
    for (int si = 0; si < FUNNEL_SEGS; si++) {
      int sn = (si + 1) % FUNNEL_SEGS;
      float u1 = (float)si / FUNNEL_SEGS;
      float u2 = (float)(si + 1) / FUNNEL_SEGS;

      rlNormal3f(norms[ri][si].x, norms[ri][si].y, norms[ri][si].z);
      rlTexCoord2f(u1, v1);
      rlVertex3f(verts[ri][si].x, verts[ri][si].y, verts[ri][si].z);

      rlNormal3f(norms[ri][sn].x, norms[ri][sn].y, norms[ri][sn].z);
      rlTexCoord2f(u2, v1);
      rlVertex3f(verts[ri][sn].x, verts[ri][sn].y, verts[ri][sn].z);

      rlNormal3f(norms[ri + 1][sn].x, norms[ri + 1][sn].y, norms[ri + 1][sn].z);
      rlTexCoord2f(u2, v2);
      rlVertex3f(verts[ri + 1][sn].x, verts[ri + 1][sn].y, verts[ri + 1][sn].z);

      rlNormal3f(norms[ri + 1][si].x, norms[ri + 1][si].y, norms[ri + 1][si].z);
      rlTexCoord2f(u1, v2);
      rlVertex3f(verts[ri + 1][si].x, verts[ri + 1][si].y, verts[ri + 1][si].z);
    }
  }
  rlEnd();

  // Top rim disc cap (closes the funnel opening, CCW winding from above)
  rlBegin(RL_TRIANGLES);
  for (int si = 0; si < FUNNEL_SEGS; si++) {
    int sn = (si + 1) % FUNNEL_SEGS;
    // Center apex
    rlNormal3f(0.0f, 1.0f, 0.0f);
    rlTexCoord2f(0.5f, 0.5f + time * 0.2f);
    rlVertex3f(m->center.x, m->center.y, m->center.z);
    // si (CCW order: center → si → sn)
    rlNormal3f(0.0f, 1.0f, 0.0f);
    rlTexCoord2f((float)si / FUNNEL_SEGS, time * 0.2f);
    rlVertex3f(verts[0][si].x, verts[0][si].y, verts[0][si].z);
    // sn
    rlNormal3f(0.0f, 1.0f, 0.0f);
    rlTexCoord2f((float)sn / FUNNEL_SEGS, time * 0.2f);
    rlVertex3f(verts[0][sn].x, verts[0][sn].y, verts[0][sn].z);
  }
  rlEnd();
}

// ════════════════════════════════════════════════════════════════════
//  PROCEDURAL MESH DRAW: CONCENTRIC RIM RINGS
//  Three tori orbiting at different funnel depths, breathing and
//  pulsing to emphasize the vortex's layered energy structure.
// ════════════════════════════════════════════════════════════════════

static void DrawRimRings(Maelstrom *m, float time) {
  float prog;
  if (m->state == MAELSTROM_FORMING)
    prog = 1.0f - (m->timer / FORMING_TIME);
  else if (m->state == MAELSTROM_SPINNING)
    prog = 1.0f;
  else
    return;

  float baseR = MAELSTROM_RADIUS * m->scale * prog;

  for (int k = 0; k < 3; k++) {
    float frac = (float)k / 3.0f;
    float depth = FUNNEL_DEPTH * m->scale * prog * frac * 0.62f;
    float rCore = baseR * (1.0f - frac * frac * 0.86f); // Shrinks deeper
    float thick = 1.9f * m->scale * (1.0f - frac * 0.45f);

    // Each ring breathes at a different phase
    float pulse = rCore * (1.0f + 0.045f * sinf(time * 5.2f + (float)k * 1.3f));

    Color c =
        ColorAlpha(ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.38f - frac * 0.14f),
                   (0.68f - frac * 0.2f) * prog);

    Vector3 rCenter = {m->center.x, m->center.y - depth, m->center.z};

    rlColor4ub(255, 255, 255, 255);
    DrawCoreTorus(rCenter, pulse - thick, pulse + thick, 7, 36, c);
  }
}

// ════════════════════════════════════════════════════════════════════
//  PROCEDURAL MESH DRAW: GEYSER COLUMN
//  Vertical cubic Bezier tube using the Frenet-frame methodology.
//  Radius tapers (wide base → needle tip), twisted around Y-axis,
//  and deformed with overlapping sine-wave perturbations for an
//  organic pressurized-water feel.
// ════════════════════════════════════════════════════════════════════

static void DrawGeyserTube(Maelstrom *m, float time) {
  if (m->geyserProg < 0.01f)
    return;

  // Bezier control points: mostly vertical with subtle organic lean
  Vector3 p0 = m->center;
  Vector3 p3 =
      Vector3Add(m->center, (Vector3){0.0f, GEYSER_HEIGHT * m->scale, 0.0f});
  Vector3 p1 = Vector3Add(p0, (Vector3){2.8f * m->scale,
                                        GEYSER_HEIGHT * 0.30f * m->scale,
                                        1.6f * m->scale});
  Vector3 p2 = Vector3Add(p0, (Vector3){-2.0f * m->scale,
                                        GEYSER_HEIGHT * 0.70f * m->scale,
                                        -1.4f * m->scale});

  Vector3 rings[GEYSER_SEGS_V + 1][GEYSER_SEGS_R];
  Vector3 normals[GEYSER_SEGS_V + 1][GEYSER_SEGS_R];

  for (int i = 0; i <= GEYSER_SEGS_V; i++) {
    float t = (float)i / GEYSER_SEGS_V;
    float tCur = t * m->geyserProg;

    Vector3 pos = BezPt(p0, p1, p2, p3, tCur);
    Vector3 tang = Vector3Normalize(BezTan(p0, p1, p2, p3, tCur));

    // Frenet frame
    Vector3 upTemp =
        (fabsf(tang.y) > 0.99f) ? (Vector3){1, 0, 0} : (Vector3){0, 1, 0};
    Vector3 rt = Vector3Normalize(Vector3CrossProduct(upTemp, tang));
    Vector3 up = Vector3CrossProduct(tang, rt);

    // Twist increases with height, animated over time
    float twist = t * PI * 2.0f + time * 5.5f;
    Vector3 tUp = Vector3Add(Vector3Scale(up, cosf(twist)),
                             Vector3Scale(rt, sinf(twist)));
    Vector3 tRight = Vector3Normalize(Vector3CrossProduct(tUp, tang));

    // Radius: capsule taper + base clamp for clean entry
    float capTaper = 0.35f + 0.65f * sqrtf(fmaxf(0.0f, sinf(t * PI)));
    float baseTaper = Clamp(tCur * 7.0f, 0.0f, 1.0f);
    float r = GEYSER_BASE_R * m->scale * capTaper * baseTaper;

    for (int j = 0; j < GEYSER_SEGS_R; j++) {
      float phi = (float)j * (2.0f * PI) / GEYSER_SEGS_R;
      Vector3 nrm = Vector3Add(Vector3Scale(tRight, cosf(phi)),
                               Vector3Scale(tUp, sinf(phi)));

      // Two-frequency surface deformation for pressurized look
      float w1 = sinf(t * 20.0f + phi * 3.0f + time * 12.0f);
      float w2 = sinf(t * 10.0f - phi * 5.0f - time * 8.5f);
      float def = 1.0f + w1 * 0.13f + w2 * 0.08f;

      normals[i][j] = nrm;
      rings[i][j] = Vector3Add(pos, Vector3Scale(nrm, r * def));
    }
  }

  // Animated V UV: simulates water rushing upward at high speed
  float uvFlowOffset = time * 0.9f;

  rlColor4ub(255, 255, 255, 255);
  rlBegin(RL_QUADS);
  for (int i = 0; i < GEYSER_SEGS_V; i++) {
    float v1 = (float)i / GEYSER_SEGS_V * 3.8f + uvFlowOffset;
    float v2 = (float)(i + 1) / GEYSER_SEGS_V * 3.8f + uvFlowOffset;
    for (int j = 0; j < GEYSER_SEGS_R; j++) {
      int jn = (j + 1) % GEYSER_SEGS_R;
      float u1 = (float)j / GEYSER_SEGS_R;
      float u2 = (float)jn / GEYSER_SEGS_R;

      rlNormal3f(normals[i][j].x, normals[i][j].y, normals[i][j].z);
      rlTexCoord2f(u1, v1);
      rlVertex3f(rings[i][j].x, rings[i][j].y, rings[i][j].z);

      rlNormal3f(normals[i][jn].x, normals[i][jn].y, normals[i][jn].z);
      rlTexCoord2f(u2, v1);
      rlVertex3f(rings[i][jn].x, rings[i][jn].y, rings[i][jn].z);

      rlNormal3f(normals[i + 1][jn].x, normals[i + 1][jn].y,
                 normals[i + 1][jn].z);
      rlTexCoord2f(u2, v2);
      rlVertex3f(rings[i + 1][jn].x, rings[i + 1][jn].y, rings[i + 1][jn].z);

      rlNormal3f(normals[i + 1][j].x, normals[i + 1][j].y, normals[i + 1][j].z);
      rlTexCoord2f(u1, v2);
      rlVertex3f(rings[i + 1][j].x, rings[i + 1][j].y, rings[i + 1][j].z);
    }
  }
  rlEnd();

  // Closed head cap (Anti-Robotic Rule: no hollow openings)
  Vector3 headPos = BezPt(p0, p1, p2, p3, m->geyserProg);
  Vector3 headTang = Vector3Normalize(BezTan(p0, p1, p2, p3, m->geyserProg));
  float headR = GEYSER_BASE_R * m->scale * 0.18f;
  Vector3 apex = Vector3Add(headPos, Vector3Scale(headTang, headR * 0.5f));

  rlBegin(RL_TRIANGLES);
  for (int j = 0; j < GEYSER_SEGS_R; j++) {
    int jn = (j + 1) % GEYSER_SEGS_R;

    rlNormal3f(headTang.x, headTang.y, headTang.z);
    rlTexCoord2f(0.5f, 3.8f + uvFlowOffset);
    rlVertex3f(apex.x, apex.y, apex.z);

    rlNormal3f(normals[GEYSER_SEGS_V][j].x, normals[GEYSER_SEGS_V][j].y,
               normals[GEYSER_SEGS_V][j].z);
    rlTexCoord2f((float)j / GEYSER_SEGS_R, 3.8f + uvFlowOffset);
    rlVertex3f(rings[GEYSER_SEGS_V][j].x, rings[GEYSER_SEGS_V][j].y,
               rings[GEYSER_SEGS_V][j].z);

    rlNormal3f(normals[GEYSER_SEGS_V][jn].x, normals[GEYSER_SEGS_V][jn].y,
               normals[GEYSER_SEGS_V][jn].z);
    rlTexCoord2f((float)jn / GEYSER_SEGS_R, 3.8f + uvFlowOffset);
    rlVertex3f(rings[GEYSER_SEGS_V][jn].x, rings[GEYSER_SEGS_V][jn].y,
               rings[GEYSER_SEGS_V][jn].z);
  }
  rlEnd();
}

// ════════════════════════════════════════════════════════════════════
//  LIFECYCLE
// ════════════════════════════════════════════════════════════════════

void InitHydroMaelstromSkill(int w, int h) {
  (void)w;
  (void)h;
  for (int i = 0; i < MAX_MAELSTROMS; i++)
    s_m[i].active = false;

  s_shader = ResourceManager_LoadShader(
      "skills/water/hydro_maelstrom_skill/hydro_maelstrom.vs",
      "skills/water/hydro_maelstrom_skill/hydro_maelstrom.fs");
  s_uTimeLoc = GetShaderLocation(s_shader, "u_time");
  s_uDissolveLoc = GetShaderLocation(s_shader, "u_dissolve");

  s_causticsDecalTex =
      ResourceManager_LoadTexture("assets/textures/water_caustics.png");
  s_splashDecalTex =
      ResourceManager_LoadTexture("assets/textures/splash_ring.png");

  // Water gradient: cyan core → bright rim → transparent
  ColorGradient_AddStop(&s_waterGrad, 0.0f, ELEMENT_COLOR_WATER);
  ColorGradient_AddStop(&s_waterGrad, 0.5f,
                        ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.30f));
  ColorGradient_AddStop(&s_waterGrad, 1.0f,
                        ColorAlpha(ELEMENT_COLOR_WATER, 0.0f));

  // Foam gradient: white burst → cyan → transparent
  ColorGradient_AddStop(&s_foamGrad, 0.0f,
                        ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.65f));
  ColorGradient_AddStop(&s_foamGrad, 0.4f,
                        ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.30f));
  ColorGradient_AddStop(&s_foamGrad, 1.0f,
                        ColorAlpha(ELEMENT_COLOR_WATER, 0.0f));

  // Geyser gradient: bright core white → element cyan → transparent
  ColorGradient_AddStop(&s_geyserGrad, 0.0f,
                        ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.55f));
  ColorGradient_AddStop(&s_geyserGrad, 0.4f, ELEMENT_COLOR_WATER);
  ColorGradient_AddStop(&s_geyserGrad, 1.0f,
                        ColorAlpha(ELEMENT_COLOR_WATER, 0.0f));
}

void CastHydroMaelstromSkill(Vector3 startPos, Vector3 target,
                             SkillParams params) {
  (void)startPos;
  int slot = FindFreeSlot();
  if (slot < 0)
    return;

  float sc = (params.sizeScale > 0.0f) ? params.sizeScale : 1.0f;
  float totalLife = FORMING_TIME + SPINNING_TIME + IMPLODE_TIME + GEYSER_TIME;

  s_m[slot] = (Maelstrom){
      .center = target,
      .timer = FORMING_TIME,
      .scale = sc,
      .spinAngle = 0.0f,
      .damage = Skill_CalculateDamage(SKILL_CAT_AOE_CONTROL, params),
      .knockback = Skill_CalculateKnockback(SKILL_CAT_AOE_CONTROL, params),
      .geyserProg = 0.0f,
      .dissolve = 0.0f,
      .pulseTimer = 0.0f,
      .state = MAELSTROM_FORMING,
      .active = true,
      .hitDealt = false};

  // Ground caustic pool: 3 overlapping circles offset from center.
  // DecalSystem_Add always produces a perfect circle (engine-level radial
  // falloff in decal.fs). Overlapping 3 shifted blobs breaks the geometric
  // symmetry and reads as an organic water pool instead of a UI ring.
  {
    float jitterR = MAELSTROM_RADIUS * sc * 0.28f;
    float baseScale = MAELSTROM_RADIUS * sc;
    for (int k = 0; k < 3; k++) {
      float a = (float)k * (2.0f * PI / 3.0f) +
                (float)GetRandomValue(0, 40) * DEG2RAD;
      float r = jitterR * ((float)GetRandomValue(55, 100) / 100.0f);
      Vector3 dPos = {target.x + cosf(a) * r, target.y, target.z + sinf(a) * r};
      float dScale = baseScale * ((float)GetRandomValue(32, 42) / 10.0f);
      float alpha = 0.32f + (float)k * 0.06f;
      DecalSystem_Add(dPos, (float)GetRandomValue(0, 360), dScale,
                      s_causticsDecalTex, totalLife + 0.5f,
                      ColorAlpha(ELEMENT_COLOR_WATER, alpha));
    }
  }

  // Ambient base light lasting entire skill
  VFXLight_Spawn(target, ELEMENT_COLOR_WATER, 55.0f * sc, totalLife);
}

void UpdateHydroMaelstromSkill(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius; // Handled by ApplyAoEDamage

  // Rebuild vortex force field each frame (only feeds particles during
  // SPINNING)
  ForceField_Clear(&s_vortexFF);
  for (int i = 0; i < MAX_MAELSTROMS; i++) {
    if (s_m[i].active && s_m[i].state == MAELSTROM_SPINNING) {
      ForceField_AddLayer(
          &s_vortexFF,
          (ForceLayer){.type = FORCE_VORTEX,
                       .origin = s_m[i].center,
                       .direction = (Vector3){0.0f, -1.0f,
                                              0.0f}, // Spin axis (CCW downward)
                       .strength = 14.0f,
                       .radius = MAELSTROM_RADIUS * s_m[i].scale * 1.8f,
                       .falloff = 1.0f});
      ForceField_AddLayer(
          &s_vortexFF,
          (ForceLayer){.type = FORCE_GRAVITY_POINT,
                       .origin = s_m[i].center,
                       .strength = 7.0f, // Attract toward center
                       .radius = MAELSTROM_RADIUS * s_m[i].scale * 2.2f,
                       .falloff = 1.0f});
      break;
    }
  }

  for (int i = 0; i < MAX_MAELSTROMS; i++) {
    if (!s_m[i].active)
      continue;
    Maelstrom *m = &s_m[i];

    switch (m->state) {

    // ── FORMING ─────────────────────────────────────────────
    case MAELSTROM_FORMING:
      m->timer -= dt;
      m->spinAngle += 1.8f * dt;
      if (GetRandomValue(0, 100) < 65)
        SpawnFormingTendril(m);
      if (m->timer <= 0.0f) {
        m->state = MAELSTROM_SPINNING;
        m->timer = SPINNING_TIME;
        CameraFX_Shake(0.35f);
      }
      break;

    // ── SPINNING ────────────────────────────────────────────
    case MAELSTROM_SPINNING: {
      m->timer -= dt;
      float spinProg = 1.0f - (m->timer / SPINNING_TIME);
      // Spin accelerates as the vortex tightens
      m->spinAngle += (3.5f + spinProg * 7.0f) * dt;

      if (GetRandomValue(0, 100) < 72)
        SpawnOrbitDroplet(m);
      if (GetRandomValue(0, 100) < 38)
        SpawnMist(m);

      // Rhythmic light pulse to animate scene illumination
      m->pulseTimer -= dt;
      if (m->pulseTimer <= 0.0f) {
        m->pulseTimer = 0.28f;
        VFXLight_Spawn(m->center, ColorLerp(ELEMENT_COLOR_WATER, WHITE, 0.28f),
                       42.0f * m->scale * (0.85f + 0.3f * spinProg), 0.28f);
      }

      if (m->timer <= 0.0f) {
        m->state = MAELSTROM_IMPLODE;
        m->timer = IMPLODE_TIME;
        CameraFX_Shake(0.75f);
        ScreenDistort_Add(m->center, 90.0f, 0.65f, 0.45f, 175.0f);
      }
      break;
    }

    // ── IMPLODE ─────────────────────────────────────────────
    case MAELSTROM_IMPLODE: {
      m->timer -= dt;
      m->spinAngle += 16.0f * dt; // Spin up fast on collapse
      float implodeT = 1.0f - (m->timer / IMPLODE_TIME);
      m->dissolve = implodeT;

      if (GetRandomValue(0, 100) < 88)
        SpawnImplosionRush(m, implodeT);

      if (m->timer <= 0.0f) {
        m->state = MAELSTROM_GEYSER;
        m->timer = GEYSER_TIME;
        m->geyserProg = 0.0f;
        m->dissolve = 0.0f;
      }
      break;
    }

    // ── GEYSER ──────────────────────────────────────────────
    case MAELSTROM_GEYSER: {
      m->timer -= dt;
      float gT = 1.0f - (m->timer / GEYSER_TIME);

      // Fast shoot-up (ease-out), then tube holds while dissolving
      m->geyserProg = Clamp(gT * 3.0f, 0.0f, 1.0f);
      m->dissolve = (gT > 0.55f) ? ((gT - 0.55f) / 0.45f) : 0.0f;

      // Trigger impact at 35% of geyser phase (mid-column height)
      if (!m->hitDealt && gT >= 0.35f) {
        m->hitDealt = true;
        TriggerGeyserBlast(m);
        ApplyAoEDamage(m->center, MAELSTROM_RADIUS * m->scale * 1.35f,
                       m->damage, m->knockback);
      }

      if (GetRandomValue(0, 100) < 62)
        SpawnGeyserSpray(m);

      if (m->timer <= 0.0f) {
        m->active = false;
        m->state = MAELSTROM_IDLE;
      }
      break;
    }

    default:
      break;
    }
  }
}

void DrawHydroMaelstromSkill(void) {
  bool any = false;
  for (int i = 0; i < MAX_MAELSTROMS; i++)
    if (s_m[i].active) {
      any = true;
      break;
    }
  if (!any)
    return;

  float time = (float)GetTime();

  rlDisableDepthMask();
  SkillManager_BeginShader(s_shader);

  for (int i = 0; i < MAX_MAELSTROMS; i++) {
    if (!s_m[i].active)
      continue;
    Maelstrom *m = &s_m[i];

    // Bind per-instance uniforms (u_time auto-bound by
    // SkillManager_BeginShader)
    SetShaderValue(s_shader, s_uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(s_shader, s_uDissolveLoc, &m->dissolve,
                   SHADER_UNIFORM_FLOAT);

    switch (m->state) {
    case MAELSTROM_FORMING:
    case MAELSTROM_SPINNING:
    case MAELSTROM_IMPLODE:
      DrawFunnelMesh(m, time);
      DrawRimRings(m, time);
      break;
    case MAELSTROM_GEYSER:
      DrawGeyserTube(m, time);
      break;
    default:
      break;
    }
  }

  SkillManager_EndShader();
  rlEnableDepthMask();
}

void UnloadHydroMaelstromSkill(void) {
  // All cached textures & shaders are freed globally by the Resource Manager
}

// ════════════════════════════════════════════════════════════════════
//  ENGINE ↔ SKILL COMMUNICATION
// ════════════════════════════════════════════════════════════════════

// Returns true during SPINNING: engine should apply a gravitational pin effect
bool IsHydroMaelstromSkillCoiling(void) {
  for (int i = 0; i < MAX_MAELSTROMS; i++)
    if (s_m[i].active && s_m[i].state == MAELSTROM_SPINNING)
      return true;
  return false;
}

// Expose the maelstrom center as an AoE "projectile" during active phases
int GetHydroMaelstromSkillProjectiles(SkillProjectile *outProjectiles,
                                      int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_MAELSTROMS && count < maxProjectiles; i++) {
    if (!s_m[i].active)
      continue;
    if (s_m[i].state == MAELSTROM_SPINNING ||
        s_m[i].state == MAELSTROM_IMPLODE) {
      outProjectiles[count].position = s_m[i].center;
      outProjectiles[count].radius = MAELSTROM_RADIUS * s_m[i].scale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateHydroMaelstromProjectile(int index) {
  if (index >= 0 && index < MAX_MAELSTROMS && s_m[index].active)
    s_m[index].active = false;
}