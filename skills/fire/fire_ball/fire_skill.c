#include "skills/fire/fire_ball/fire_skill.h"
#include "core/force_field.h"
#include "core/particle_system.h"
#include "core/path_spline.h"
#include "raymath.h"
#include "core/ribbon_strip.h"
#include "rlgl.h"
#include "core/skill_manager.h"
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
static float s_fireTravelSpeed = 1.8f;     // dragon head travel (progress units/s)
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

#define FIRE_SKILL_TUNABLE_COUNT 16
static const char *const s_fireTunableKeys[FIRE_SKILL_TUNABLE_COUNT] = {
    "fire_impact_gravity",   "fire_impact_drag",       "fire_disperse_rise",
    "fire_disperse_curl",    "flame_body_curl",        "flame_body_rise",
    "fire_travel_speed",     "flame_spawn_rate",       "cast_flash_radius",
    "burst_radius_max",      "impact_flash1_radius",   "impact_flash2_radius",
    "impact_spark_radius_max", "disperse_radius_max",  "ribbon_width_max",
    "dragon_head_scale",
};

#define FIRE_PROGRESS_MAX 2.5f

// Particle radii before the *sizeScale*4.0f visual multiplier applied at
// each spawn site — real-world-scaled (meters).
#define CAST_BURST_RADIUS_MIN 0.025f
#define CAST_BURST_LIFETIME_MIN 0.3f
#define CAST_BURST_LIFETIME_MAX 0.8f

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
  float twistPhase;
  float sizeScale;
  Vector3 path[EMITTER_PATH_MAX];
  int pathCount;
  int pathHead; // ĐÃ CẬP NHẬT: Đầu Ring Buffer
  Vector3 sampledPath[MAX_SAMPLED_SEGMENTS];
  int sampledCount;
  float spawnAccum;
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

    return GetBezierPoint(emitter->startPos, dynamicP1, dynamicP2,
                          emitter->targetPos, t);
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
  return (Vector3){Math_Mix(inertiaPos.x, idealUpPos.x, blend),
                   Math_Mix(inertiaPos.y, idealUpPos.y, blend),
                   Math_Mix(inertiaPos.z, idealUpPos.z, blend)};
}

static Vector3 GetDragonPathTangent(FireEmitter *emitter, float t) {
  Vector3 tangent = Vector3Subtract(GetDragonPathPos(emitter, t + 0.01f),
                                    GetDragonPathPos(emitter, t));
  if (tangent.x == 0 && tangent.y == 0 && tangent.z == 0)
    return (Vector3){0.0f, 1.0f, 0.0f};
  return Vector3Normalize(tangent);
}

static void TriggerFireImpact(Vector3 pos, float sizeScale) {
  int sparkCount = GetRandomValue(12, 18) * sizeScale;
  for (int s = 0; s < sparkCount; s++) {
    float angle = Random01() * PI * 2.0f;
    float pitch = (Random01() - 0.5f) * PI;
    float speed = Math_Mix(1.6f, 4.2f, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity =
        (Vector3){cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                  sinf(angle) * speed * cosf(pitch)};
    cfg.radius = Math_Mix(0.008f, s_impactSparkRadiusMax, Random01()) * sizeScale * 4.0f;
    cfg.lifetime = Math_Mix(0.3f, 0.7f, Random01());
    cfg.colorStart = (Color){255, 200, 40, 230};
    cfg.colorEnd = (Color){200, 20, 0, 0};
    cfg.forceField = &s_fireImpactField;
    SpawnParticle(cfg);
  }

  int disperseCount = GetRandomValue(14, 22) * sizeScale;
  for (int v = 0; v < disperseCount; v++) {
    float angle = Random01() * PI * 2.0f;
    float speed = Math_Mix(0.8f, 2.6f, Random01()) * sizeScale;

    ParticleConfig cfg = {0};
    cfg.position = pos;
    cfg.velocity =
        (Vector3){cosf(angle) * speed,
                  (Math_Mix(0.8f, 2.6f, Random01()) + 0.8f) * sizeScale,
                  sinf(angle) * speed};
    cfg.radius = Math_Mix(0.025f, s_disperseRadiusMax, Random01()) * sizeScale * 4.0f;
    cfg.lifetime = Math_Mix(0.6f, 1.3f, Random01());
    cfg.colorStart = (Color){255, 120, 20, 200};
    cfg.colorEnd = (Color){120, 10, 0, 0};
    cfg.forceField = &s_fireDisperseField;
    SpawnParticle(cfg);
  }

  ParticleConfig staticCore1 = {0};
  staticCore1.position = pos;
  staticCore1.radius = s_impactFlash1Radius * sizeScale * 4.0f;
  staticCore1.lifetime = 0.40f;
  staticCore1.colorStart = (Color){255, 100, 10, 180};
  staticCore1.colorEnd = (Color){0, 0, 0, 0};
  SpawnParticle(staticCore1);

  ParticleConfig staticCore2 = {0};
  staticCore2.position = pos;
  staticCore2.radius = s_impactFlash2Radius * sizeScale * 4.0f;
  staticCore2.lifetime = 0.25f;
  staticCore2.colorStart = (Color){255, 230, 80, 255};
  staticCore2.colorEnd = (Color){100, 0, 0, 0};
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
  // the ForceFields from them (see struct decls above) ---
  float tunableValues[FIRE_SKILL_TUNABLE_COUNT] = {
      s_fireImpactGravity,  s_fireImpactDrag,      s_fireDisperseRise,
      s_fireDisperseCurl,   s_flameBodyCurl,       s_flameBodyRise,
      s_fireTravelSpeed,    s_flameSpawnRate,      s_castFlashRadius,
      s_burstRadiusMax,     s_impactFlash1Radius,  s_impactFlash2Radius,
      s_impactSparkRadiusMax, s_disperseRadiusMax, s_ribbonWidthMax,
      s_dragonHeadScale,
  };
  Tuning_LoadFloatsFromPath("skills/fire/fire_ball/fire_ball.tuning",
                             s_fireTunableKeys, tunableValues,
                             FIRE_SKILL_TUNABLE_COUNT);
  s_fireImpactGravity = tunableValues[0];
  s_fireImpactDrag = tunableValues[1];
  s_fireDisperseRise = tunableValues[2];
  s_fireDisperseCurl = tunableValues[3];
  s_flameBodyCurl = tunableValues[4];
  s_flameBodyRise = tunableValues[5];
  s_fireTravelSpeed = tunableValues[6];
  s_flameSpawnRate = tunableValues[7];
  s_castFlashRadius = tunableValues[8];
  s_burstRadiusMax = tunableValues[9];
  s_impactFlash1Radius = tunableValues[10];
  s_impactFlash2Radius = tunableValues[11];
  s_impactSparkRadiusMax = tunableValues[12];
  s_disperseRadiusMax = tunableValues[13];
  s_ribbonWidthMax = tunableValues[14];
  s_dragonHeadScale = tunableValues[15];

  // --- Khởi tạo ForceField ---

  // Tia lửa va chạm: rơi xuống như than đỏ + cản
  ForceField_Clear(&s_fireImpactField);
  ForceField_AddLayer(&s_fireImpactField, (ForceLayer){
    .type = FORCE_GRAVITY_DIR, .direction = {0,-1,0}, .strength = s_fireImpactGravity
  });
  ForceField_AddLayer(&s_fireImpactField, (ForceLayer){
    .type = FORCE_DRAG, .strength = s_fireImpactDrag
  });

  // Quầng lửa bốc: cuộn theo curl + lực bốc lên + cản 3.5
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

  // Thân rồng (core): curl mạnh làm ngọn lửa uốn lượn + bốc nhẹ + cản 9.5
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

  // Thân rồng (aura): curl mạnh làm ngọn lửa uốn lượn + bốc nhẹ + cản 5.2
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

  // Burst khi cast: chỉ cản 2.5
  ForceField_Clear(&s_fireBurstField);
  ForceField_AddLayer(&s_fireBurstField, (ForceLayer){
    .type = FORCE_DRAG, .strength = 2.5f
  });

  s_skillIndex = Skill_GetIndexByName("FIRE");

  static SkillTunableEntry s_fireTunables[FIRE_SKILL_TUNABLE_COUNT] = {
      {"fire_impact_gravity", &s_fireImpactGravity, 0.0f, 19.62f, 1.8f},
      {"fire_impact_drag", &s_fireImpactDrag, 0.0f, 20.0f, 2.5f},
      {"fire_disperse_rise", &s_fireDisperseRise, 0.0f, 19.62f, 2.6f},
      {"fire_disperse_curl", &s_fireDisperseCurl, 0.0f, 5.0f, 0.5f},
      {"flame_body_curl", &s_flameBodyCurl, 0.0f, 5.0f, 1.2f},
      {"flame_body_rise", &s_flameBodyRise, 0.0f, 19.62f, 0.8f},
      {"fire_travel_speed", &s_fireTravelSpeed, 0.5f, 5.0f, 1.8f},
      {"flame_spawn_rate", &s_flameSpawnRate, 0.0f, 2000.0f, 750.0f},
      {"cast_flash_radius", &s_castFlashRadius, 0.0f, 1.0f, 0.8f},
      {"burst_radius_max", &s_burstRadiusMax, 0.0f, 0.3f, 0.065f},
      {"impact_flash1_radius", &s_impactFlash1Radius, 0.0f, 1.0f, 0.6f},
      {"impact_flash2_radius", &s_impactFlash2Radius, 0.0f, 1.0f, 0.33f},
      {"impact_spark_radius_max", &s_impactSparkRadiusMax, 0.0f, 0.1f, 0.022f},
      {"disperse_radius_max", &s_disperseRadiusMax, 0.0f, 0.3f, 0.065f},
      {"ribbon_width_max", &s_ribbonWidthMax, 0.0f, 1.0f, 0.4f},
      {"dragon_head_scale", &s_dragonHeadScale, 0.0f, 0.01f, 0.0012f},
  };
  RegisterSkillTunables(s_skillIndex, s_fireTunables, FIRE_SKILL_TUNABLE_COUNT);
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
      emitters[i].twistPhase = twistPhase;
      emitters[i].sizeScale = sizeScale;
      emitters[i].pathCount = 1;
      emitters[i].pathHead = 0;
      emitters[i].path[0] = startPos;
      emitters[i].spawnAccum = 0.0f;

      float dist = Vector3Distance(startPos, target);
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));

      emitters[i].p1 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.35f));
      emitters[i].p2 = Vector3Add(startPos, Vector3Scale(dir, dist * 0.70f));
      break;
    }
  }

  ParticleConfig flash = {0};
  flash.position = startPos;
  flash.radius = s_castFlashRadius * sizeScale;
  flash.lifetime = 0.25f;
  flash.colorStart = (Color){255, 140, 20, 255};
  flash.colorEnd = (Color){0, 0, 0, 0};
  SpawnParticle(flash);

  int burstCount = GetRandomValue(8, 14) * sizeScale;
  for (int s = 0; s < burstCount; s++) {
    ParticleConfig cfg = {0};
    cfg.position = startPos;
    cfg.velocity = (Vector3){(float)GetRandomValue(-200, 300) * 0.01f * sizeScale,
                             (float)GetRandomValue(100, 400) * 0.01f * sizeScale,
                             (float)GetRandomValue(-200, 300) * 0.01f * sizeScale};
    cfg.radius =
        Math_Mix(CAST_BURST_RADIUS_MIN, s_burstRadiusMax, Random01()) *
        sizeScale * 4.0f;
    cfg.lifetime =
        Math_Mix(CAST_BURST_LIFETIME_MIN, CAST_BURST_LIFETIME_MAX, Random01());
    cfg.colorStart = (Color){255, 90, 10, 200};
    cfg.colorEnd = (Color){0, 0, 0, 0};
    cfg.forceField = &s_fireBurstField;
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

    float targetProgress = emitters[e].headProgress + dt * s_fireTravelSpeed;
    if (targetProgress >= FIRE_PROGRESS_MAX)
      targetProgress = FIRE_PROGRESS_MAX;

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

    if (emitters[e].headProgress > 1.0f &&
        (emitters[e].headProgress - dt * s_fireTravelSpeed) <= 1.0f) {
      Vector3 impactPos = (emitters[e].sampledCount > 0)
                              ? emitters[e].sampledPath[0]
                              : emitters[e].targetPos;
      TriggerFireImpact(impactPos, emitters[e].sizeScale);
    }

    if (emitters[e].sampledCount > 1) {
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
        float rad = Math_Mix(0.03f, 0.06f, sizeTaper) * emitters[e].sizeScale *
                    Math_Mix(0.8f, 1.2f, Random01());

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
            Math_Mix(0.1f, 0.4f, Random01()) * emitters[e].sizeScale;
        Vector3 vel = Vector3Scale(randomDir, outwardSpeed);
        float backwardSpeed =
            Math_Mix(1.6f, 3.4f, Random01()) * emitters[e].sizeScale;
        vel = Vector3Add(vel, Vector3Scale(pureTangent, -backwardSpeed));

        Vector3 spawnPos = {purePos.x + randomDir.x * 0.012f * sizeTaper,
                            purePos.y + randomDir.y * 0.012f * sizeTaper,
                            purePos.z + randomDir.z * 0.012f * sizeTaper};

        ParticleConfig cfgCore = {0};
        cfgCore.position = spawnPos;
        cfgCore.velocity = vel;
        cfgCore.radius = rad * 1.8f;
        cfgCore.lifetime = Math_Mix(0.2f, 0.4f, Random01());
        cfgCore.colorStart = (Color){255, 230, 100, 255};
        cfgCore.colorEnd = (Color){255, 60, 0, 0};
        cfgCore.forceField = &s_flameBodyField;
        SpawnParticle(cfgCore);

        ParticleConfig cfgAura = {0};
        cfgAura.position = (Vector3){spawnPos.x + (Random01() - 0.5f) * 0.03f,
                                     spawnPos.y + (Random01() - 0.5f) * 0.03f,
                                     spawnPos.z + (Random01() - 0.5f) * 0.03f};
        cfgAura.velocity = Vector3Scale(vel, 0.75f);
        cfgAura.radius = rad * 4.8f;
        cfgAura.lifetime = Math_Mix(0.35f, 0.65f, Random01());
        cfgAura.colorStart = (Color){255, 90, 15, 140};
        cfgAura.colorEnd = (Color){100, 5, 0, 0};
        cfgAura.forceField = &s_flameAuraField;
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