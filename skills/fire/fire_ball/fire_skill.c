#include "skills/fire/fire_ball/fire_skill.h"
#include "core/force_field.h"
#include "core/particle_system.h"
#include "core/path_spline.h"
#include "raymath.h"
#include "core/ribbon_strip.h"
#include "rlgl.h"
#include "core/skill_manager.h"
#include "core/skill_helper.h"
#include "core/resource_manager.h"
#include "core/tuning.h"
#include "core/utils_math.h"
#include <math.h>
#include <string.h>

#define MAX_EMITTERS 10

// --- Force Fields của Fire Skill ---
// Mỗi loại particle có "cá tính" riêng, đã được hợp nhất lực drag vào ForceField
static ForceField s_fireImpactField;   // tia lửa va chạm: rơi xuống + drag 2.5
static ForceField s_fireDisperseField; // quầng lửa bốc: curl + bốc lên + drag 3.5
static ForceField s_flameBodyField;    // thân rồng lửa (core): curl nhẹ + bốc lên + drag 9.5
static ForceField s_flameAuraField;    // thân rồng lửa (aura): curl nhẹ + bốc lên + drag 5.2
static ForceField s_fireBurstField;    // tia lửa bắn khi cast: chỉ drag 2.5

// Sandbox-tunable physics knobs (see RegisterSkillTunables in
// core/skill_manager.h). All acceleration values are m/s² — compare against
// real gravity (9.81f) when dialing these in the sandbox UI. Loaded from
// skills/fire/fire_ball/fire_ball.tuning on init if present, else these
// defaults (already real-world-scaled: 1 unit = 1 meter) apply.
static float s_fireImpactGravity = 1.8f;   // embers falling accel (m/s²)
static float s_fireImpactDrag = 2.5f;      // drag rate (1/s), not spatial
static float s_fireDisperseRise = 2.6f;    // outward flare buoyancy accel (m/s²)
static float s_fireDisperseCurl = 0.5f;    // curl swirl accel (m/s²)
static float s_flameBodyCurl = 1.2f;       // dragon-body curl accel (m/s²)
static float s_flameBodyRise = 0.8f;       // dragon-body buoyancy accel (m/s²)
// Dragon head travel speed (progress units/s) is a SkillCurve sampled at the
// emitter's own headProgress (already normalized [0,1] over the flight, not
// real-world distance) — never re-derived from cast distance. See
// s_fireFlightMaxDuration below for the hard time cap.
static SkillCurve s_fireTravelSpeedCurve;
static float s_fireFlightMaxDuration = 3.0f; // hard cap on time-to-reach headProgress==1.0 (s)
static float s_flameSpawnRate = 750.0f;    // flame particles spawned/s along body

// Size/radius knobs (meters, before the *sizeScale multiplier applied at
// each spawn site) — added after the initial force-only tunable pass showed
// the effect reads far larger than the character at these defaults; expose
// them so the sandbox can dial the whole effect down without a rebuild.
static float s_castFlashRadius = 0.8f;      // flash at cast origin
static float s_burstRadiusMax = 0.065f;     // cast burst spark radius (upper bound)
static float s_impactFlash1Radius = 0.6f;   // impact static-core flash, primary
static float s_impactFlash2Radius = 0.33f;  // impact static-core flash, secondary
static float s_impactSparkRadiusMax = 0.022f; // falling-ember spark radius (upper bound)
static float s_disperseRadiusMax = 0.065f;  // outward flare ember radius (upper bound)
static float s_ribbonWidthMax = 0.4f;       // dragon-body ribbon width at its widest (tail)
static float s_dragonHeadScale = 0.0012f;   // pixel-to-world-meters ratio for the head billboard

// Per-phase opacity multiplier (0..1) applied to that phase's spawned
// particle alpha, and one fully-configurable, always-additive tunable
// "extra force" mix per phase (core/skill_helper.h's SkillForceMix — all 8
// force types simultaneously available, each with its own strength; 0 =
// that type contributes nothing) composed on top of that phase's existing
// ForceField. Every component defaults to 0 strength (no behavior change)
// until dialed up. See RebuildFire*Field() below, which rebuilds each field
// from current tunable values right before it's used each time (not just
// once at Init) so these new knobs AND the pre-existing curl/rise/gravity/
// drag tunables actually respond live in the sandbox instead of being baked in.
// Per-phase over-lifetime curves (core/skill_curve.h + core/particle_system.h's
// radiusCurve/speedCurve/alphaCurve) — how a particle's size/velocity/opacity
// evolve across its OWN lifetime (t=0 at spawn, t=1 at death), not just a
// flat multiplier. Seeded flat at 1.0 (radius/speed: no change from today's
// behavior; alpha: same as the old plain s_XAlpha multiplier) so these are
// no-ops until shaped in the sandbox — e.g. dragging the radius curve's
// middle keys up and the last key to 0 makes particles balloon then pop
// instead of holding a constant size.
static SkillCurve s_castRadiusCurve, s_castSpeedCurve, s_castAlphaCurve;
static SkillCurve s_flyRadiusCurve, s_flySpeedCurve, s_flyAlphaCurve;
static SkillCurve s_impactRadiusCurve, s_impactSpeedCurve, s_impactAlphaCurve;
static SkillCurve s_disperseRadiusCurve, s_disperseSpeedCurve, s_disperseAlphaCurve;
static SkillForceMix s_castForce;
static SkillForceMix s_flyForce;
static SkillForceMix s_impactForce;
static SkillForceMix s_disperseForce;

// Remaining per-spawn-site shape/feel knobs (count/speed/lifetime/radius-min
// ranges) — everything that visibly changes how dense, fast, or long-lived
// an effect reads, at every phase. Pure implementation-detail constants
// (path sub-step size, ring-buffer limits) are intentionally left as #define
// — they don't change how the effect looks or feels, only how it's computed.
static float s_castBurstCountMin = 8.0f, s_castBurstCountMax = 14.0f;
static float s_castBurstSpeedXZMin = -2.0f, s_castBurstSpeedXZMax = 3.0f; // m/s
static float s_castBurstSpeedYMin = 1.0f, s_castBurstSpeedYMax = 4.0f;   // m/s
static float s_castBurstRadiusMin = 0.025f;
static float s_castBurstLifetimeMin = 0.3f, s_castBurstLifetimeMax = 0.8f;
static float s_castFlashLifetime = 0.25f;

static float s_flyCoreRadiusMin = 0.03f, s_flyCoreRadiusMax = 0.06f;
static float s_flyCoreRadiusRandMin = 0.8f, s_flyCoreRadiusRandMax = 1.2f;
static float s_flyCoreLifetimeMin = 0.2f, s_flyCoreLifetimeMax = 0.4f;
static float s_flyCoreRadiusMult = 1.8f;   // core particle radius = taper-rad * this
static float s_flyAuraRadiusMult = 4.8f;   // aura particle radius = taper-rad * this
static float s_flyAuraLifetimeMin = 0.35f, s_flyAuraLifetimeMax = 0.65f;
static float s_flyOutwardSpeedMin = 0.1f, s_flyOutwardSpeedMax = 0.4f;     // m/s, before *sizeScale
static float s_flyBackwardSpeedMin = 1.6f, s_flyBackwardSpeedMax = 3.4f;  // m/s, before *sizeScale

static float s_impactSparkCountMin = 12.0f, s_impactSparkCountMax = 18.0f;
static float s_impactSparkSpeedMin = 1.6f, s_impactSparkSpeedMax = 4.2f; // m/s
static float s_impactSparkRadiusMin = 0.008f;
static float s_impactSparkLifetimeMin = 0.3f, s_impactSparkLifetimeMax = 0.7f;
static float s_impactFlash1Lifetime = 0.40f;
static float s_impactFlash2Lifetime = 0.25f;

static float s_disperseCountMin = 14.0f, s_disperseCountMax = 22.0f;
static float s_disperseSpeedMin = 0.8f, s_disperseSpeedMax = 2.6f;   // m/s, horizontal
static float s_disperseRadiusMin = 0.025f;
static float s_disperseLifetimeMin = 0.6f, s_disperseLifetimeMax = 1.3f;

static void RebuildFireImpactField(void);
static void RebuildFireDisperseField(void);
static void RebuildFireBodyFields(void);
static void RebuildFireBurstField(void);

// 21 original named tunables + 40 shape/feel-range tunables (count/speed/
// lifetime/radius-min per spawn site) - 4 old single-float alpha entries
// + 4 phases x 3 over-lifetime curves (radius/speed/alpha) + 4 phases x 1
// force mix x SKILL_FORCE_MIX_TUNABLE_COUNT(29) = 21 + 40 - 4 + 12 + 116 = 185
#define FIRE_SKILL_TUNABLE_COUNT 185

#define FIRE_PROGRESS_MAX 2.5f

#define DRAGON_JAW_OSCILLATION_SPEED 30.0f
#define DRAGON_JAW_OSCILLATION_AMP 0.15f
#define DRAGON_HEAD_ORIGIN_X_FACTOR 0.35f

#define DRAGON_BODY_RIBBON_WIDTH_SCALE 2.0f

#define EMITTER_PATH_MAX 360
#define MAX_SAMPLED_SEGMENTS                                                   \
  256 // TỐI ƯU: Tăng kích thước tránh tràn mảng khi hạ spacing
#define MAX_PATH_STEPS_PER_FRAME                                               \
  80 // TỐI ƯU: Chặn đứng tình trạng stall CPU do lag đột biến

extern Camera3D camera;

typedef struct {
  bool active;
  int ownerAgentId;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 p1, p2;
  float headProgress;
  float flightElapsed; // wall-clock time since Cast — safety cap alongside FIRE_PROGRESS_MAX
  float twistPhase;
  float sizeScale;
  Vector3 path[EMITTER_PATH_MAX];
  int pathCount;
  int pathHead; // ĐÃ CẬP NHẬT: Đầu Ring Buffer
  Vector3 sampledPath[MAX_SAMPLED_SEGMENTS];
  int sampledCount;
  float spawnAccum;
  Vector3 forceOffset; // accumulated deflection from the tunable "fly" extra-force slots
  Vector3 forceVel;    // damped velocity driving forceOffset — see UpdateFireSkill
} FireEmitter;

static FireEmitter emitters[MAX_EMITTERS];
static RibbonPoint ribbonBuffer[MAX_SAMPLED_SEGMENTS];
static int s_skillIndex = -1;

static Texture2D particleTex;
static Shader fireShader;
static int timeLoc;
static Texture2D dragonHeadTex;

static Vector3 GetDragonPathPos(FireEmitter *emitter, float t) {
  if (t <= 1.0f) {
    float dist = Vector3Distance(emitter->startPos, emitter->targetPos);
    Vector3 dir = Vector3Normalize(
        Vector3Subtract(emitter->targetPos, emitter->startPos));

    Vector3 perpX = (Vector3){-dir.z, 0.0f, dir.x};
    if (Vector3Length(perpX) == 0.0f)
      perpX = (Vector3){0.0f, 0.0f, 1.0f};
    perpX = Vector3Normalize(perpX);
    Vector3 perpY = Vector3Normalize(Vector3CrossProduct(dir, perpX));

    // Wave amplitude floored (not purely dist * ratio) — at the new
    // real-meter arena scale (18m radius, vs. the old 1800-unit arena),
    // typical cast distances are short enough that a pure dist*0.18f wobble
    // was too subtle to read as "a dragon flying" at all — it only became
    // visible for very long casts (near the map edge). Reported: "the
    // dragon only appears near the map edge, otherwise just a small
    // explosion" — this is why: the flight was there, just imperceptible.
    // Floor is also capped as a fraction of dist (fminf(..., dist*0.6f)) —
    // now that CastSkill()'s own position bug is fixed (core/skill_manager.c)
    // typical casts are genuinely short (often under 1m), and an absolute
    // 0.4f floor could exceed the whole path length, turning the flight
    // into a tight loop-in-place rather than a visible directional arc.
    float waveFreq = 5.5f;
    float waveAmp = fminf(fmaxf(dist * 0.18f, 0.2f), dist * 0.6f) * sinf(t * waveFreq + emitter->twistPhase);
    float waveAmpVert =
        fminf(fmaxf(dist * 0.10f, 0.12f), dist * 0.35f) * cosf(t * waveFreq * 1.5f + emitter->twistPhase);

    Vector3 dynamicP1 = Vector3Add(emitter->p1, Vector3Scale(perpX, waveAmp));
    dynamicP1 = Vector3Add(dynamicP1, Vector3Scale(perpY, waveAmpVert));

    Vector3 dynamicP2 =
        Vector3Add(emitter->p2, Vector3Scale(perpX, -waveAmp * 0.8f));
    dynamicP2 = Vector3Add(dynamicP2, Vector3Scale(perpY, -waveAmpVert * 0.8f));

    return Vector3Add(GetBezierPoint(emitter->startPos, dynamicP1, dynamicP2,
                                      emitter->targetPos, t),
                       emitter->forceOffset);
  }

  float over = t - 1.0f;
  Vector3 vIn =
      Vector3Scale(Vector3Subtract(emitter->targetPos, emitter->p2), 3.0f);
  if (Vector3Length(vIn) > 3.0f)
    vIn = Vector3Scale(Vector3Normalize(vIn), 3.0f);

  // Y-rise and X-wobble reduced from a straight ÷100 (16.0f/2.0f) — at the
  // exact ÷100 scale the post-impact climb (up to 24m at over=1.5) exceeded
  // both the arena radius (18m) and the camera's own max height (~11m),
  // reliably carrying the effect out of frame and reading as "appears
  // disconnected from the character, off in a corner."
  Vector3 idealUpPos = {emitter->targetPos.x + sinf(over * 12.0f) * 0.8f,
                        emitter->targetPos.y + 4.0f * over,
                        emitter->targetPos.z};
  Vector3 inertiaPos = {emitter->targetPos.x + vIn.x * over,
                        emitter->targetPos.y + vIn.y * over,
                        emitter->targetPos.z + vIn.z * over};

  float blend = fminf(over * 3.5f, 1.0f);
  blend = blend * blend * (3.0f - 2.0f * blend);
  return Vector3Add((Vector3){Math_Mix(inertiaPos.x, idealUpPos.x, blend),
                              Math_Mix(inertiaPos.y, idealUpPos.y, blend),
                              Math_Mix(inertiaPos.z, idealUpPos.z, blend)},
                     emitter->forceOffset);
}

static Vector3 GetDragonPathTangent(FireEmitter *emitter, float t) {
  Vector3 tangent = Vector3Subtract(GetDragonPathPos(emitter, t + 0.01f),
                                    GetDragonPathPos(emitter, t));
  if (tangent.x == 0 && tangent.y == 0 && tangent.z == 0)
    return (Vector3){0.0f, 1.0f, 0.0f};
  return Vector3Normalize(tangent);
}

static void TriggerFireImpact(Vector3 pos, float sizeScale) {
  RebuildFireImpactField();
  RebuildFireDisperseField();

  int sparkCount = (int)Math_Mix(s_impactSparkCountMin, s_impactSparkCountMax, Random01()) * sizeScale;
  for (int s = 0; s < sparkCount; s++) {
    float angle = Random01() * PI * 2.0f;
    float pitch = (Random01() - 0.5f) * PI;
    float speed = Math_Mix(s_impactSparkSpeedMin, s_impactSparkSpeedMax, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity =
        (Vector3){cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                  sinf(angle) * speed * cosf(pitch)};
    cfg.radius = Math_Mix(s_impactSparkRadiusMin, s_impactSparkRadiusMax, Random01()) * sizeScale * 4.0f;
    cfg.lifetime = Math_Mix(s_impactSparkLifetimeMin, s_impactSparkLifetimeMax, Random01());
    cfg.colorStart = (Color){255, 200, 40, 230};
    cfg.colorEnd = (Color){200, 20, 0, 0};
    cfg.forceField = &s_fireImpactField;
    cfg.radiusCurve = &s_impactRadiusCurve;
    cfg.speedCurve = &s_impactSpeedCurve;
    cfg.alphaCurve = &s_impactAlphaCurve;
    SpawnParticle(cfg);
  }

  int disperseCount = (int)Math_Mix(s_disperseCountMin, s_disperseCountMax, Random01()) * sizeScale;
  for (int v = 0; v < disperseCount; v++) {
    float angle = Random01() * PI * 2.0f;
    float speed = Math_Mix(s_disperseSpeedMin, s_disperseSpeedMax, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity =
        (Vector3){cosf(angle) * speed,
                  (Math_Mix(s_disperseSpeedMin, s_disperseSpeedMax, Random01()) + 0.8f) * sizeScale,
                  sinf(angle) * speed};
    cfg.radius = Math_Mix(s_disperseRadiusMin, s_disperseRadiusMax, Random01()) * sizeScale * 4.0f;
    cfg.lifetime = Math_Mix(s_disperseLifetimeMin, s_disperseLifetimeMax, Random01());
    cfg.colorStart = (Color){255, 120, 20, 200};
    cfg.colorEnd = (Color){120, 10, 0, 0};
    cfg.forceField = &s_fireDisperseField;
    cfg.radiusCurve = &s_disperseRadiusCurve;
    cfg.speedCurve = &s_disperseSpeedCurve;
    cfg.alphaCurve = &s_disperseAlphaCurve;
    SpawnParticle(cfg);
  }

  ParticleConfig staticCore1 = {0};
  staticCore1.position = pos;
  staticCore1.radius = s_impactFlash1Radius * sizeScale * 4.0f;
  staticCore1.lifetime = s_impactFlash1Lifetime;
  staticCore1.colorStart = (Color){255, 100, 10, 180};
  staticCore1.colorEnd = (Color){0, 0, 0, 0};
  staticCore1.radiusCurve = &s_impactRadiusCurve;
  staticCore1.alphaCurve = &s_impactAlphaCurve;
  SpawnParticle(staticCore1);

  ParticleConfig staticCore2 = {0};
  staticCore2.position = pos;
  staticCore2.radius = s_impactFlash2Radius * sizeScale * 4.0f;
  staticCore2.lifetime = s_impactFlash2Lifetime;
  staticCore2.colorStart = (Color){255, 230, 80, 255};
  staticCore2.colorEnd = (Color){100, 0, 0, 0};
  staticCore2.radiusCurve = &s_impactRadiusCurve;
  staticCore2.alphaCurve = &s_impactAlphaCurve;
  SpawnParticle(staticCore2);
}

void InitFireSkill(int screenWidth, int screenHeight) {
  fireShader = ResourceManager_LoadShader(NULL, "skills/fire/fire_ball/fire.fs");
  timeLoc = GetShaderLocation(fireShader, "u_time");
  dragonHeadTex = LoadTexture("skills/fire/fire_ball/dragon_head.png");

  Image img = GenImageGradientRadial(64, 64, 0.0f, WHITE, BLACK);
  particleTex = LoadTextureFromImage(img);
  UnloadImage(img);

  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;

  // --- Tunable physics knobs: load any sandbox-saved values, then build
  // the ForceFields from them (see struct decls above). Entries are built
  // first (grouped by phase: cast/fly/impact/disperse) and loaded via
  // SkillTunables_LoadPersisted so the ForceField setup below sees the final,
  // persisted values — same order the old flat-array load used. ---
  SkillCurve_SetConstant(&s_fireTravelSpeedCurve, 1.8f); // seed flat at the old constant speed

  // Seed every per-phase radius/speed/alpha curve flat at 1.0 (no change
  // from today's behavior) before building tunable entries below.
  SkillCurve_SetConstant(&s_castRadiusCurve, 1.0f);
  SkillCurve_SetConstant(&s_castSpeedCurve, 1.0f);
  SkillCurve_SetConstant(&s_castAlphaCurve, 1.0f);
  SkillCurve_SetConstant(&s_flyRadiusCurve, 1.0f);
  SkillCurve_SetConstant(&s_flySpeedCurve, 1.0f);
  SkillCurve_SetConstant(&s_flyAlphaCurve, 1.0f);
  SkillCurve_SetConstant(&s_impactRadiusCurve, 1.0f);
  SkillCurve_SetConstant(&s_impactSpeedCurve, 1.0f);
  SkillCurve_SetConstant(&s_impactAlphaCurve, 1.0f);
  SkillCurve_SetConstant(&s_disperseRadiusCurve, 1.0f);
  SkillCurve_SetConstant(&s_disperseSpeedCurve, 1.0f);
  SkillCurve_SetConstant(&s_disperseAlphaCurve, 1.0f);

  s_skillIndex = Skill_GetIndexByName("FIRE");

  // Built as a sequence of assignments (not a single literal) so each
  // phase's named entries stay contiguous with that phase's force slots —
  // the sandbox draws one "-- phase --" header per contiguous run, so
  // interleaving keeps each phase's group together instead of splitting
  // named/force entries into two separate blocks.
  static SkillTunableEntry s_fireTunables[FIRE_SKILL_TUNABLE_COUNT];
  int fn = 0;

  s_fireTunables[fn++] = (SkillTunableEntry){"cast_flash_radius", &s_castFlashRadius, 0.0f, 1.0f, 0.8f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"cast_flash_lifetime", &s_castFlashLifetime, 0.05f, 2.0f, 0.25f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_radius_min", &s_castBurstRadiusMin, 0.0f, 0.3f, 0.025f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_radius_max", &s_burstRadiusMax, 0.0f, 0.3f, 0.065f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_count_min", &s_castBurstCountMin, 0.0f, 50.0f, 8.0f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_count_max", &s_castBurstCountMax, 0.0f, 50.0f, 14.0f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_speed_xz_min", &s_castBurstSpeedXZMin, -10.0f, 10.0f, -2.0f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_speed_xz_max", &s_castBurstSpeedXZMax, -10.0f, 10.0f, 3.0f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_speed_y_min", &s_castBurstSpeedYMin, -10.0f, 10.0f, 1.0f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_speed_y_max", &s_castBurstSpeedYMax, -10.0f, 10.0f, 4.0f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_lifetime_min", &s_castBurstLifetimeMin, 0.05f, 3.0f, 0.3f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"burst_lifetime_max", &s_castBurstLifetimeMax, 0.05f, 3.0f, 0.8f, "cast"};
  s_fireTunables[fn++] = (SkillTunableEntry){"cast_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "cast", &s_castRadiusCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"cast_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "cast", &s_castSpeedCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"cast_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "cast", &s_castAlphaCurve};
  fn += SkillForceMix_MakeTunables(&s_castForce, "cast_force_", "cast", &s_fireTunables[fn]);

  s_fireTunables[fn++] = (SkillTunableEntry){"flame_body_curl", &s_flameBodyCurl, 0.0f, 5.0f, 1.2f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"flame_body_rise", &s_flameBodyRise, 0.0f, 19.62f, 0.8f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fire_travel_speed", NULL, 0.5f, 5.0f, 1.8f, "fly", &s_fireTravelSpeedCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"fire_flight_max_duration", &s_fireFlightMaxDuration, 0.3f, 10.0f, 3.0f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"flame_spawn_rate", &s_flameSpawnRate, 0.0f, 2000.0f, 750.0f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"ribbon_width_max", &s_ribbonWidthMax, 0.0f, 1.0f, 0.4f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"dragon_head_scale", &s_dragonHeadScale, 0.0f, 0.01f, 0.0012f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_core_radius_min", &s_flyCoreRadiusMin, 0.0f, 0.3f, 0.03f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_core_radius_max", &s_flyCoreRadiusMax, 0.0f, 0.3f, 0.06f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_core_radius_rand_min", &s_flyCoreRadiusRandMin, 0.0f, 2.0f, 0.8f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_core_radius_rand_max", &s_flyCoreRadiusRandMax, 0.0f, 2.0f, 1.2f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_core_radius_mult", &s_flyCoreRadiusMult, 0.0f, 10.0f, 1.8f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_aura_radius_mult", &s_flyAuraRadiusMult, 0.0f, 10.0f, 4.8f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_core_lifetime_min", &s_flyCoreLifetimeMin, 0.02f, 2.0f, 0.2f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_core_lifetime_max", &s_flyCoreLifetimeMax, 0.02f, 2.0f, 0.4f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_aura_lifetime_min", &s_flyAuraLifetimeMin, 0.02f, 2.0f, 0.35f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_aura_lifetime_max", &s_flyAuraLifetimeMax, 0.02f, 2.0f, 0.65f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_outward_speed_min", &s_flyOutwardSpeedMin, 0.0f, 5.0f, 0.1f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_outward_speed_max", &s_flyOutwardSpeedMax, 0.0f, 5.0f, 0.4f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_backward_speed_min", &s_flyBackwardSpeedMin, 0.0f, 10.0f, 1.6f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_backward_speed_max", &s_flyBackwardSpeedMax, 0.0f, 10.0f, 3.4f, "fly"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "fly", &s_flyRadiusCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "fly", &s_flySpeedCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"fly_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "fly", &s_flyAlphaCurve};
  fn += SkillForceMix_MakeTunables(&s_flyForce, "fly_force_", "fly", &s_fireTunables[fn]);

  s_fireTunables[fn++] = (SkillTunableEntry){"fire_impact_gravity", &s_fireImpactGravity, 0.0f, 19.62f, 1.8f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fire_impact_drag", &s_fireImpactDrag, 0.0f, 20.0f, 2.5f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_flash1_radius", &s_impactFlash1Radius, 0.0f, 1.0f, 0.6f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_flash1_lifetime", &s_impactFlash1Lifetime, 0.02f, 2.0f, 0.40f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_flash2_radius", &s_impactFlash2Radius, 0.0f, 1.0f, 0.33f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_flash2_lifetime", &s_impactFlash2Lifetime, 0.02f, 2.0f, 0.25f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_radius_min", &s_impactSparkRadiusMin, 0.0f, 0.1f, 0.008f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_radius_max", &s_impactSparkRadiusMax, 0.0f, 0.1f, 0.022f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_count_min", &s_impactSparkCountMin, 0.0f, 60.0f, 12.0f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_count_max", &s_impactSparkCountMax, 0.0f, 60.0f, 18.0f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_speed_min", &s_impactSparkSpeedMin, 0.0f, 10.0f, 1.6f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_speed_max", &s_impactSparkSpeedMax, 0.0f, 10.0f, 4.2f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_lifetime_min", &s_impactSparkLifetimeMin, 0.02f, 3.0f, 0.3f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_spark_lifetime_max", &s_impactSparkLifetimeMax, 0.02f, 3.0f, 0.7f, "impact"};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "impact", &s_impactRadiusCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "impact", &s_impactSpeedCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"impact_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "impact", &s_impactAlphaCurve};
  fn += SkillForceMix_MakeTunables(&s_impactForce, "impact_force_", "impact", &s_fireTunables[fn]);

  s_fireTunables[fn++] = (SkillTunableEntry){"fire_disperse_rise", &s_fireDisperseRise, 0.0f, 19.62f, 2.6f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"fire_disperse_curl", &s_fireDisperseCurl, 0.0f, 5.0f, 0.5f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_radius_min", &s_disperseRadiusMin, 0.0f, 0.3f, 0.025f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_radius_max", &s_disperseRadiusMax, 0.0f, 0.3f, 0.065f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_count_min", &s_disperseCountMin, 0.0f, 60.0f, 14.0f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_count_max", &s_disperseCountMax, 0.0f, 60.0f, 22.0f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_speed_min", &s_disperseSpeedMin, 0.0f, 10.0f, 0.8f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_speed_max", &s_disperseSpeedMax, 0.0f, 10.0f, 2.6f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_lifetime_min", &s_disperseLifetimeMin, 0.02f, 3.0f, 0.6f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_lifetime_max", &s_disperseLifetimeMax, 0.02f, 3.0f, 1.3f, "disperse"};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_radius_curve", NULL, 0.0f, 3.0f, 1.0f, "disperse", &s_disperseRadiusCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_speed_curve", NULL, 0.0f, 3.0f, 1.0f, "disperse", &s_disperseSpeedCurve};
  s_fireTunables[fn++] = (SkillTunableEntry){"disperse_alpha_curve", NULL, 0.0f, 1.0f, 1.0f, "disperse", &s_disperseAlphaCurve};
  fn += SkillForceMix_MakeTunables(&s_disperseForce, "disperse_force_", "disperse", &s_fireTunables[fn]);

  SkillTunables_LoadPersisted("skills/fire/fire_ball/fire_ball.tuning",
                               s_fireTunables, fn);

  // Fields are (re)built from current tunable values at each real use site
  // (RebuildFire*Field(), defined below Init) rather than only once here —
  // this call just seeds a valid initial state before any Cast happens.
  RebuildFireImpactField();
  RebuildFireDisperseField();
  RebuildFireBodyFields();
  RebuildFireBurstField();

  RegisterSkillTunables(s_skillIndex, s_fireTunables, fn);
}

// Rebuilds each phase's ForceField from CURRENT tunable values (including
// the tunable extra-force layer) — called once at Init for a valid initial
// state, and again right before each real use, so dragging a sandbox slider
// (existing curl/rise/gravity/drag knobs, or the new extra-force ones)
// actually changes behavior immediately instead of only taking effect after
// a restart (the previous build-once-at-Init pattern silently ignored any
// post-Init edit to these particular tunables).
static void RebuildFireImpactField(void) {
  ForceField_Clear(&s_fireImpactField);
  ForceField_AddLayer(&s_fireImpactField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = s_fireImpactGravity
  });
  ForceField_AddLayer(&s_fireImpactField, (ForceLayer){
    .type = FORCE_DRAG, .strength = s_fireImpactDrag
  });
  SkillForceMix_AddLayers(&s_impactForce, &s_fireImpactField);
}

static void RebuildFireDisperseField(void) {
  ForceField_Clear(&s_fireDisperseField);
  ForceField_AddLayer(&s_fireDisperseField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = s_fireDisperseRise
  });
  ForceField_AddLayer(&s_fireDisperseField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = s_fireDisperseCurl,
    .noiseScale = 1.5f, .noiseSpeed = 0.6f
  });
  ForceField_AddLayer(&s_fireDisperseField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 3.5f
  });
  SkillForceMix_AddLayers(&s_disperseForce, &s_fireDisperseField);
}

static void RebuildFireBodyFields(void) {
  ForceField_Clear(&s_flameBodyField);
  ForceField_AddLayer(&s_flameBodyField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = s_flameBodyCurl,
    .noiseScale = 1.8f, .noiseSpeed = 0.8f
  });
  ForceField_AddLayer(&s_flameBodyField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = s_flameBodyRise
  });
  ForceField_AddLayer(&s_flameBodyField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 9.5f
  });
  SkillForceMix_AddLayers(&s_flyForce, &s_flameBodyField);

  ForceField_Clear(&s_flameAuraField);
  ForceField_AddLayer(&s_flameAuraField, (ForceLayer){
    .type = FORCE_NOISE_CURL, .strength = s_flameBodyCurl,
    .noiseScale = 1.8f, .noiseSpeed = 0.8f
  });
  ForceField_AddLayer(&s_flameAuraField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,1,0}, .strength = s_flameBodyRise
  });
  ForceField_AddLayer(&s_flameAuraField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 5.2f
  });
  SkillForceMix_AddLayers(&s_flyForce, &s_flameAuraField);
}

static void RebuildFireBurstField(void) {
  ForceField_Clear(&s_fireBurstField);
  ForceField_AddLayer(&s_fireBurstField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 2.5f
  });
  SkillForceMix_AddLayers(&s_castForce, &s_fireBurstField);
}

void CastFireSkill(int agentId, Vector3 startPos, Vector3 target, float twistPhase,
                   float sizeScale) {
  if (!SkillManager_CanCast(s_skillIndex, agentId))
    return;

  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].ownerAgentId = agentId;
      emitters[i].startPos = startPos;
      emitters[i].targetPos = target;
      emitters[i].headProgress = 0.0f;
      emitters[i].flightElapsed = 0.0f;
      emitters[i].twistPhase = twistPhase;
      emitters[i].sizeScale = sizeScale;
      emitters[i].pathCount = 1;
      emitters[i].pathHead = 0;
      emitters[i].path[0] = startPos;
      emitters[i].spawnAccum = 0.0f;
      emitters[i].forceOffset = (Vector3){0};
      emitters[i].forceVel = (Vector3){0};

      float dist = Vector3Distance(startPos, target);
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));

      emitters[i].p1 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f));
      emitters[i].p2 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.70f));
      break;
    }
  }

  RebuildFireBurstField();

  ParticleConfig flash = {0};
  flash.position = startPos;
  flash.radius = s_castFlashRadius * sizeScale;
  flash.lifetime = s_castFlashLifetime;
  flash.colorStart = (Color){255, 140, 20, 255};
  flash.colorEnd = (Color){0, 0, 0, 0};
  flash.radiusCurve = &s_castRadiusCurve;
  flash.alphaCurve = &s_castAlphaCurve;
  SpawnParticle(flash);

  int burstCount = (int)Math_Mix(s_castBurstCountMin, s_castBurstCountMax, Random01()) * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    ParticleConfig cfg = {0};
    cfg.position = startPos;
    cfg.velocity = (Vector3){Math_Mix(s_castBurstSpeedXZMin, s_castBurstSpeedXZMax, Random01()) * sizeScale,
                             Math_Mix(s_castBurstSpeedYMin, s_castBurstSpeedYMax, Random01()) * sizeScale,
                             Math_Mix(s_castBurstSpeedXZMin, s_castBurstSpeedXZMax, Random01()) * sizeScale};
    cfg.radius =
        Math_Mix(s_castBurstRadiusMin, s_burstRadiusMax, Random01()) *
        sizeScale * 4.0f;
    cfg.lifetime =
        Math_Mix(s_castBurstLifetimeMin, s_castBurstLifetimeMax, Random01());
    cfg.colorStart = (Color){255, 90, 10, 200};
    cfg.colorEnd = (Color){0, 0, 0, 0};
    cfg.forceField = &s_fireBurstField;
    cfg.radiusCurve = &s_castRadiusCurve;
    cfg.speedCurve = &s_castSpeedCurve;
    cfg.alphaCurve = &s_castAlphaCurve;
    SpawnParticle(cfg);
  }

  SkillParams cdParams = {0};
  cdParams.sizeScale = sizeScale;
  SkillManager_TriggerCooldown(s_skillIndex, agentId,
                               Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, cdParams));
}

void UpdateFireSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    float prevProgress = emitters[e].headProgress;
    emitters[e].flightElapsed += dt;

    // Speed comes from the curve sampled at the emitter's OWN progress
    // (already normalized [0,1] over the flight path) — never re-derived
    // from real-world cast distance (that only shapes the path's wobble via
    // GetDragonPathPos, not how fast it's traversed).
    float speedNow = SkillCurve_Eval(&s_fireTravelSpeedCurve, prevProgress);
    float targetProgress = prevProgress + dt * speedNow;

    // Safety cap: if a badly-tuned curve stalls near 0 for too long, force
    // completion once flightElapsed exceeds the hard duration cap — a skill
    // must always have a bounded lifetime regardless of its speed curve.
    if (emitters[e].flightElapsed >= s_fireFlightMaxDuration && prevProgress < FIRE_PROGRESS_MAX)
      targetProgress = FIRE_PROGRESS_MAX;

    if (targetProgress >= FIRE_PROGRESS_MAX)
      targetProgress = FIRE_PROGRESS_MAX;

    // Tunable "fly" extra-force mix (every component defaults to 0 strength
    // = no effect) deflects the dragon's actual path/head, not just the
    // decorative ember/aura trail particles below — those have too short a
    // lifetime (0.2-0.4s) for a force to visibly curve them, so without this
    // the sliders would silently do (almost) nothing.
    {
      ForceField flyField = {0};
      SkillForceMix_AddLayers(&s_flyForce, &flyField);
      if (flyField.layerCount > 0) {
        Vector3 headNow = GetDragonPathPos(&emitters[e], prevProgress);
        Vector3 accel = ForceField_Evaluate(&flyField, headNow, emitters[e].forceVel,
                                             emitters[e].flightElapsed, (Vector3){0}, (Vector3){0});
        emitters[e].forceVel = Vector3Scale(Vector3Add(emitters[e].forceVel, Vector3Scale(accel, dt)), 0.92f);
        emitters[e].forceOffset = Vector3Add(emitters[e].forceOffset, Vector3Scale(emitters[e].forceVel, dt));
      }
    }

    float step = 0.008f;
    float currentProgress = emitters[e].headProgress;
    int stepCount = 0;

    while (currentProgress < targetProgress &&
           stepCount < MAX_PATH_STEPS_PER_FRAME) {
      stepCount++;
      currentProgress += step;
      if (currentProgress > targetProgress)
        currentProgress = targetProgress;

      Vector3 pos = GetDragonPathPos(&emitters[e], currentProgress);

      float dist =
          (emitters[e].pathCount > 0)
              ? Vector3Distance(pos, emitters[e].path[emitters[e].pathHead])
              : Vector3Distance(pos, emitters[e].startPos);
      if (dist > 0.015f || emitters[e].pathCount == 0) {
        emitters[e].pathHead =
            (emitters[e].pathHead - 1 + EMITTER_PATH_MAX) % EMITTER_PATH_MAX;
        emitters[e].path[emitters[e].pathHead] = pos;
        if (emitters[e].pathCount < EMITTER_PATH_MAX)
          emitters[e].pathCount++;
      }
    }
    emitters[e].headProgress = currentProgress;

    if (emitters[e].headProgress >= FIRE_PROGRESS_MAX) {
      emitters[e].active = false;
      continue;
    }

    // TỐI ƯU SIÊU TỐC: Trích xuất Ring Buffer ra mảng phẳng bằng memcpy (Không
    // thèm dùng vòng lặp for)
    static Vector3 linearPath[EMITTER_PATH_MAX];
    int head = emitters[e].pathHead;
    int count = emitters[e].pathCount;

    if (head + count <= EMITTER_PATH_MAX) {
      memcpy(linearPath, &emitters[e].path[head], count * sizeof(Vector3));
    } else {
      int part1 = EMITTER_PATH_MAX - head;
      int part2 = count - part1;
      memcpy(linearPath, &emitters[e].path[head], part1 * sizeof(Vector3));
      memcpy(&linearPath[part1], &emitters[e].path[0], part2 * sizeof(Vector3));
    }

    emitters[e].sampledCount =
        SamplePath(linearPath, emitters[e].pathCount, 0.045f,
                   emitters[e].sampledPath, MAX_SAMPLED_SEGMENTS);

    if (emitters[e].headProgress > 1.0f && prevProgress <= 1.0f) {
      Vector3 impactPos = (emitters[e].sampledCount > 0)
                              ? emitters[e].sampledPath[0]
                              : emitters[e].targetPos;
      TriggerFireImpact(impactPos, emitters[e].sizeScale);
    }

    if (emitters[e].sampledCount > 1) {
      RebuildFireBodyFields();
      emitters[e].spawnAccum += s_flameSpawnRate * emitters[e].sizeScale * dt;
      int flamesToSpawn = (int)emitters[e].spawnAccum;
      emitters[e].spawnAccum -= flamesToSpawn;

      for (int k = 0; k < flamesToSpawn; k++) {
        int idx = GetRandomValue(0, emitters[e].sampledCount - 1);
        Vector3 purePos = emitters[e].sampledPath[idx];
        float normDist = (float)idx / (float)(emitters[e].sampledCount - 1);
        float sizeTaper = powf(1.0f - normDist, 1.2f);
        // Floor raised from 0.015f — at 0.015f the tail segment nearest the
        // caster was too thin to read as a continuous trail (reported: the
        // dragon "just appears" at the target with nothing visibly
        // connecting back to the character).
        float rad = Math_Mix(s_flyCoreRadiusMin, s_flyCoreRadiusMax, sizeTaper) * emitters[e].sizeScale *
                    Math_Mix(s_flyCoreRadiusRandMin, s_flyCoreRadiusRandMax, Random01());

        Vector3 pureTangent =
            (idx < emitters[e].sampledCount - 1)
                ? Vector3Normalize(Vector3Subtract(
                      purePos, emitters[e].sampledPath[idx + 1]))
                : Vector3Normalize(Vector3Subtract(
                      emitters[e].sampledPath[idx - 1], purePos));

        Vector3 randomDir = {Random01() - 0.5f, Random01() - 0.5f,
                             Random01() - 0.5f};
        if (Vector3Length(randomDir) == 0.0f)
          randomDir = (Vector3){0, 1, 0};
        randomDir = Vector3Normalize(randomDir);

        float outwardSpeed =
            Math_Mix(s_flyOutwardSpeedMin, s_flyOutwardSpeedMax, Random01()) * emitters[e].sizeScale;
        Vector3 vel = Vector3Scale(randomDir, outwardSpeed);
        float backwardSpeed =
            Math_Mix(s_flyBackwardSpeedMin, s_flyBackwardSpeedMax, Random01()) * emitters[e].sizeScale;
        vel = Vector3Add(vel, Vector3Scale(pureTangent, -backwardSpeed));

        Vector3 spawnPos = {purePos.x + randomDir.x * 0.012f * sizeTaper,
                            purePos.y + randomDir.y * 0.012f * sizeTaper,
                            purePos.z + randomDir.z * 0.012f * sizeTaper};

        ParticleConfig cfgCore = {0};
        cfgCore.position = spawnPos;
        cfgCore.velocity = vel;
        cfgCore.radius = rad * s_flyCoreRadiusMult;
        cfgCore.lifetime = Math_Mix(s_flyCoreLifetimeMin, s_flyCoreLifetimeMax, Random01());
        cfgCore.colorStart = (Color){255, 230, 100, 255};
        cfgCore.colorEnd = (Color){255, 60, 0, 0};
        cfgCore.forceField = &s_flameBodyField;
        cfgCore.radiusCurve = &s_flyRadiusCurve;
        cfgCore.speedCurve = &s_flySpeedCurve;
        cfgCore.alphaCurve = &s_flyAlphaCurve;
        SpawnParticle(cfgCore);

        ParticleConfig cfgAura = {0};
        cfgAura.position = (Vector3){spawnPos.x + (Random01() - 0.5f) * 0.03f,
                                     spawnPos.y + (Random01() - 0.5f) * 0.03f,
                                     spawnPos.z + (Random01() - 0.5f) * 0.03f};
        cfgAura.velocity = Vector3Scale(vel, 0.75f);
        cfgAura.radius = rad * s_flyAuraRadiusMult;
        cfgAura.lifetime = Math_Mix(s_flyAuraLifetimeMin, s_flyAuraLifetimeMax, Random01());
        cfgAura.colorStart = (Color){255, 90, 15, 140};
        cfgAura.colorEnd = (Color){100, 5, 0, 0};
        cfgAura.forceField = &s_flameAuraField;
        cfgAura.radiusCurve = &s_flyRadiusCurve;
        cfgAura.speedCurve = &s_flySpeedCurve;
        cfgAura.alphaCurve = &s_flyAlphaCurve;
        SpawnParticle(cfgAura);
      }
    }
  }
}

void DrawFireSkill(void) {
  bool skillActive = false;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active)
      skillActive = true;
  }
  if (!skillActive)
    return;

  float time = GetTime();
  rlDisableDepthMask();

  SetShaderValue(fireShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(fireShader);
  BeginBlendMode(BLEND_ADDITIVE);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || emitters[e].sampledCount < 2)
      continue;

    int bodySegments = emitters[e].sampledCount;
    for (int i = 0; i < bodySegments; i++) {
      float normDist = (float)i / (float)(bodySegments - 1);
      float taper = powf(1.0f - normDist, 1.4f);

      // Floor raised from 0.015f for the same reason as the flame-particle
      // radius floor above — the tail end (nearest the caster) needs to
      // stay visible, not taper to near-nothing.
      float baseWidth = Math_Mix(0.06f, s_ribbonWidthMax, taper);
      float width =
          baseWidth * emitters[e].sizeScale * DRAGON_BODY_RIBBON_WIDTH_SCALE;

      float brightness =
          taper * (0.8f + 0.2f * sinf(time * 9.0f - (i * 8.0f) * 0.045f));
      unsigned char heat = (unsigned char)(255.0f * brightness);

      ribbonBuffer[i].position = emitters[e].sampledPath[i];
      ribbonBuffer[i].halfWidth = width * 0.5f;
      ribbonBuffer[i].tint = (Color){heat, (unsigned char)(heat * 0.6f),
                                     (unsigned char)(heat * 0.1f),
                                     (unsigned char)(255.0f * taper)};
      ribbonBuffer[i].v = normDist;
    }
    DrawRibbonStrip(ribbonBuffer, bodySegments, particleTex, camera);
  }
  EndBlendMode();
  EndShaderMode();

  BeginBlendMode(BLEND_ADDITIVE);
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || emitters[e].sampledCount < 2)
      continue;

    Vector3 tangent =
        GetDragonPathTangent(&emitters[e], emitters[e].headProgress);
    float scale = s_dragonHeadScale * emitters[e].sizeScale;
    float jawAnim = 1.0f + sinf(time * DRAGON_JAW_OSCILLATION_SPEED) *
                               DRAGON_JAW_OSCILLATION_AMP;
    Vector2 size = {dragonHeadTex.width * scale,
                    dragonHeadTex.height * scale * jawAnim};
    Vector2 origin = {size.x * DRAGON_HEAD_ORIGIN_X_FACTOR, size.y * 0.5f};

    Vector3 basePoint = emitters[e].sampledPath[0];
    float offsetBackward = size.x * 0.38f;
    Vector3 headPos =
        Vector3Subtract(basePoint, Vector3Scale(tangent, offsetBackward));

    Vector3 pointAhead = Vector3Add(headPos, Vector3Scale(tangent, 1.0f));
    Vector2 screenHead = GetWorldToScreen(headPos, camera);
    Vector2 dir =
        Vector2Subtract(GetWorldToScreen(pointAhead, camera), screenHead);
    float rotation = -atan2f(dir.y, dir.x) * RAD2DEG;
    Rectangle sourceRec = {0.0f, 0.0f, (float)dragonHeadTex.width,
                           (float)dragonHeadTex.height *
                               ((dir.x < 0.0f) ? -1.0f : 1.0f)};

    float fade = (emitters[e].headProgress > FIRE_PROGRESS_MAX - 0.4f)
                     ? Clamp(1.0f - (emitters[e].headProgress -
                                     (FIRE_PROGRESS_MAX - 0.4f)) /
                                        0.4f,
                             0.0f, 1.0f)
                     : 1.0f;
    unsigned char alpha = (unsigned char)(255.0f * fade);

    DrawBillboardPro(camera, dragonHeadTex, sourceRec, headPos, camera.up,
                     (Vector2){size.x * 1.3f, size.y * 1.3f},
                     (Vector2){origin.x * 1.3f, origin.y * 1.3f}, rotation,
                     (Color){255, 40, 0, (unsigned char)(alpha * 0.6f)});
    DrawBillboardPro(camera, dragonHeadTex, sourceRec, headPos, camera.up, size,
                     origin, rotation, (Color){255, 180, 40, alpha});
  }
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadFireSkill(void) {
  /* ResourceManager handles shader cleanup */
  UnloadTexture(dragonHeadTex);
  UnloadTexture(particleTex);
}

int GetFireSkillProjectiles(SkillProjectile *outProjectiles,
                            int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && count < maxProjectiles) {
      Vector3 headPos = (emitters[i].sampledCount > 0)
                            ? emitters[i].sampledPath[0]
                            : emitters[i].startPos;
      outProjectiles[count].position = headPos;
      outProjectiles[count].radius = 0.15f * emitters[i].sizeScale;
      outProjectiles[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateFireProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active) {
      if (count == index) {
        emitters[i].active = false;
        Vector3 headPos = (emitters[i].sampledCount > 0)
                              ? emitters[i].sampledPath[0]
                              : emitters[i].startPos;
        TriggerFireImpact(headPos, emitters[i].sizeScale);
        break;
      }
      count++;
    }
  }
}