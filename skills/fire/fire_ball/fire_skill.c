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

#include "fire_skill_params.inl"

static void RebuildFireImpactField(void);
static void RebuildFireDisperseField(void);
static void RebuildFireBodyFields(void);
static void RebuildFireBurstField(void);

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
    cfg.emissiveCurve = &s_impactEmissiveCurve;
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
    cfg.emissiveCurve = &s_disperseEmissiveCurve;
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
  staticCore1.emissiveCurve = &s_impactEmissiveCurve;
  SpawnParticle(staticCore1);

  ParticleConfig staticCore2 = {0};
  staticCore2.position = pos;
  staticCore2.radius = s_impactFlash2Radius * sizeScale * 4.0f;
  staticCore2.lifetime = s_impactFlash2Lifetime;
  staticCore2.colorStart = (Color){255, 230, 80, 255};
  staticCore2.colorEnd = (Color){100, 0, 0, 0};
  staticCore2.radiusCurve = &s_impactRadiusCurve;
  staticCore2.alphaCurve = &s_impactAlphaCurve;
  staticCore2.emissiveCurve = &s_impactEmissiveCurve;
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
  SkillCurve_SetConstant(&s_castEmissiveCurve, 1.0f);
  SkillCurve_SetConstant(&s_flyRadiusCurve, 1.0f);
  SkillCurve_SetConstant(&s_flySpeedCurve, 1.0f);
  SkillCurve_SetConstant(&s_flyAlphaCurve, 1.0f);
  SkillCurve_SetConstant(&s_flyEmissiveCurve, 1.0f);
  SkillCurve_SetConstant(&s_impactRadiusCurve, 1.0f);
  SkillCurve_SetConstant(&s_impactSpeedCurve, 1.0f);
  SkillCurve_SetConstant(&s_impactAlphaCurve, 1.0f);
  SkillCurve_SetConstant(&s_impactEmissiveCurve, 1.0f);
  SkillCurve_SetConstant(&s_disperseRadiusCurve, 1.0f);
  SkillCurve_SetConstant(&s_disperseSpeedCurve, 1.0f);
  SkillCurve_SetConstant(&s_disperseAlphaCurve, 1.0f);
  SkillCurve_SetConstant(&s_disperseEmissiveCurve, 1.0f);

  s_skillIndex = Skill_GetIndexByName("FIRE");

  // Built as a sequence of assignments (not a single literal) so each
  // phase's named entries stay contiguous with that phase's force slots —
  // the sandbox draws one "-- phase --" header per contiguous run, so
  // interleaving keeps each phase's group together instead of splitting
  // named/force entries into two separate blocks.
  static SkillTunableEntry s_fireTunables[FIRE_SKILL_TUNABLE_COUNT];
  int fn = 0;

  #include "fire_skill_tunables.inl"

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
  flash.emissiveCurve = &s_castEmissiveCurve;
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
    cfg.emissiveCurve = &s_castEmissiveCurve;
    SpawnParticle(cfg);
  }

  SkillParams cdParams = {0};
  cdParams.sizeScale = sizeScale;
  SkillManager_TriggerCooldown(s_skillIndex, agentId,
                               Skill_CalculateCooldown(SKILL_CAT_PROJECTILE, cdParams));
}

void UpdateFireSkill(float dt) {
  // Zero-instance early-out: nothing to advance when no emitter is active.
  bool anyActive = false;
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (emitters[e].active) { anyActive = true; break; }
  }
  if (!anyActive)
    return;

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
        cfgCore.emissiveCurve = &s_flyEmissiveCurve;
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
        cfgAura.emissiveCurve = &s_flyEmissiveCurve;
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
  rlDrawRenderBatchActive();
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
  rlDrawRenderBatchActive();
  rlEnableDepthMask();
}

void UnloadFireSkill(void) {
  // No-op: textures/shaders are owned by ResourceManager, never unload here.
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