#include "trail_system.h"
#include "force_field.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

// ═══════════════════════════════════════════════════════════════════════════════
// POOL & FREE-LIST
// ═══════════════════════════════════════════════════════════════════════════════
static void KillTrailInternal(int id);
static TrailEntity trailPool[MAX_TRAIL_PARTICLES];
static int freeListHead = 0; // đầu free-list, O(1) thay cho quét tuyến tính
static int activeCount = 0;

// ═══════════════════════════════════════════════════════════════════════════════
// SHADER CACHE
// Cache location "u_time" theo từng shader id khác nhau - vì mỗi shader
// program có location riêng cho cùng tên uniform (driver tự gán, không cố
// định). Static array nhỏ, no-malloc, đủ cho số shader khác nhau thực tế
// dùng cùng lúc trong 1 hệ thống chiêu thức.
// ═══════════════════════════════════════════════════════════════════════════════

static Shader defaultShader; // dùng khi TrailConfig.shader.id == 0

#define TRAIL_SHADER_CACHE_SIZE 16
static unsigned int shaderCacheIds[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheTimeLocs[TRAIL_SHADER_CACHE_SIZE];
static int shaderCacheCount = 0;

static int GetCachedTimeLoc(Shader shader) {
  for (int i = 0; i < shaderCacheCount; i++) {
    if (shaderCacheIds[i] == shader.id)
      return shaderCacheTimeLocs[i];
  }
  int loc = GetShaderLocation(shader, "u_time");
  if (shaderCacheCount < TRAIL_SHADER_CACHE_SIZE) {
    shaderCacheIds[shaderCacheCount] = shader.id;
    shaderCacheTimeLocs[shaderCacheCount] = loc;
    shaderCacheCount++;
  }
  return loc;
}

// ═══════════════════════════════════════════════════════════════════════════════
// SCRATCH BUFFERS  (single-threaded, reused every frame, no heap alloc)
// ═══════════════════════════════════════════════════════════════════════════════

static RibbonPoint scratchOuter[TRAIL_HISTORY_COUNT];
static RibbonPoint scratchInner[TRAIL_HISTORY_COUNT];

// Lưu vị trí node TRƯỚC khi constraint projection để tái tính velocity sau
// khi constraint đã dịch các node (bước C trong chuỗi tích phân WISP).
static Vector3 scratchNodePrevPos[TRAIL_HISTORY_COUNT];

// ═══════════════════════════════════════════════════════════════════════════════
// PHYSICS CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

// Số lần lặp Gauss-Seidel cho constraint khoảng cách ribbon (WISP).
#define WISP_CONSTRAINT_ITERS 2

// ═══════════════════════════════════════════════════════════════════════════════
// INTERNAL UTILITIES
// ═══════════════════════════════════════════════════════════════════════════════

static inline Shader ResolveShader(const TrailEntity *t) {
  return (t->shader.id != 0) ? t->shader : defaultShader;
}

static float SmoothStepC(float edge0, float edge1, float x) {
  float t = (x - edge0) / (edge1 - edge0);
  if (t < 0.0f)
    return 0.0f;
  if (t > 1.0f)
    return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// ─── Camera-facing quad draw ─────────────────────
static void DrawCameraFacingQuad(Camera3D camera, Vector3 center, float width,
                                 float height, float rotation, Color tint,
                                 Texture2D tex) {
  Matrix matView = GetCameraMatrix(camera);
  Vector3 right = {matView.m0, matView.m4, matView.m8};
  Vector3 up = {matView.m1, matView.m5, matView.m9};

  float cosR = cosf(rotation);
  float sinR = sinf(rotation);
  Vector3 rVec = Vector3Add(Vector3Scale(right, cosR), Vector3Scale(up, sinR));
  Vector3 uVec =
      Vector3Subtract(Vector3Scale(up, cosR), Vector3Scale(right, sinR));

  Vector3 tl =
      Vector3Add(Vector3Subtract(center, Vector3Scale(rVec, width * 0.5f)),
                 Vector3Scale(uVec, height * 0.5f));
  Vector3 tr = Vector3Add(Vector3Add(center, Vector3Scale(rVec, width * 0.5f)),
                          Vector3Scale(uVec, height * 0.5f));
  Vector3 bl =
      Vector3Subtract(Vector3Subtract(center, Vector3Scale(rVec, width * 0.5f)),
                      Vector3Scale(uVec, height * 0.5f));
  Vector3 br =
      Vector3Subtract(Vector3Add(center, Vector3Scale(rVec, width * 0.5f)),
                      Vector3Scale(uVec, height * 0.5f));

  if (tex.id > 0)
    rlSetTexture(tex.id);
  rlBegin(RL_QUADS);
  rlColor4ub(tint.r, tint.g, tint.b, tint.a);
  rlTexCoord2f(0.0f, 0.0f);
  rlVertex3f(tl.x, tl.y, tl.z);
  rlTexCoord2f(0.0f, 1.0f);
  rlVertex3f(bl.x, bl.y, bl.z);
  rlTexCoord2f(1.0f, 1.0f);
  rlVertex3f(br.x, br.y, br.z);
  rlTexCoord2f(1.0f, 0.0f);
  rlVertex3f(tr.x, tr.y, tr.z);
  rlEnd();
  if (tex.id > 0)
    rlSetTexture(0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// PHYSICS HELPERS
// ═══════════════════════════════════════════════════════════════════════════════

static inline void ConstrainRibbonSegment(Vector3 *a, Vector3 *b, float restLen,
                                          bool pinnedA) {
  Vector3 delta = Vector3Subtract(*b, *a);
  float dist2 = Vector3LengthSqr(delta);

  float restLen2 = restLen * restLen;
  if (fabsf(dist2 - restLen2) < restLen2 * 1e-8f)
    return;
  if (dist2 < 1e-10f)
    return;

  float dist = sqrtf(dist2);
  float err = dist - restLen;
  Vector3 dir = Vector3Scale(delta, 1.0f / dist);

  if (pinnedA) {
    *b = Vector3Subtract(*b, Vector3Scale(dir, err));
  } else {
    Vector3 half = Vector3Scale(dir, err * 0.5f);
    *a = Vector3Add(*a, half);
    *b = Vector3Subtract(*b, half);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// PHYSICS: PROJECTILE
// ═══════════════════════════════════════════════════════════════════════════════
static bool UpdateProjectilePhysics(int i, float dt, float time) {
  TrailEntity *t = &trailPool[i];

  // ── 1. Ghi vị trí vào lịch sử TRƯỚC khi di chuyển ───────────────────────
  t->historyHead = (t->historyHead + 1) % TRAIL_HISTORY_COUNT;
  {
    float velLen = Vector3Length(t->velocity);
    Vector3 dir = (velLen > 1e-6f) ? Vector3Scale(t->velocity, 1.0f / velLen)
                                   : (Vector3){0.0f, 0.0f, 1.0f};
    t->history[t->historyHead] = Vector3Subtract(
        t->position,
        Vector3Scale(dir, t->length * TRAIL_PROJECTILE_SPAWN_OFFSET_MUL));
  }

  // Áp dụng giới hạn độ dài đuôi linh hoạt dựa trên trailLength thiết lập
  int maxNodes =
      (t->trailLength > 0.0f) ? (int)t->trailLength : TRAIL_HISTORY_COUNT;
  if (maxNodes > TRAIL_HISTORY_COUNT)
    maxNodes = TRAIL_HISTORY_COUNT;
  if (maxNodes < 1)
    maxNodes = 1;

  if (t->historyCount < maxNodes) {
    t->historyCount++;
  } else if (t->historyCount > maxNodes) {
    t->historyCount = maxNodes;
  }

  // ── 2. Steering: lerp velocity về hướng target ───────────────────────────
  t->wobblePhase += dt * TRAIL_PROJECTILE_WOBBLE_FREQ;

  Vector3 toTarget = Vector3Subtract(t->target, t->position);
  float distSqr = Vector3LengthSqr(toTarget);

  if (distSqr > TRAIL_PROJECTILE_RETARGET_DIST_SQR) {
    Vector3 desiredDir = Vector3Normalize(toTarget);
    float currentSpeed = Vector3Length(t->velocity);
    float newSpeed = fminf(currentSpeed + TRAIL_PROJECTILE_ACCEL_RATE * dt,
                           TRAIL_PROJECTILE_MAX_SPEED);
    float distToTarget = sqrtf(distSqr);
    float curveStrength =
        fminf(distToTarget / TRAIL_PROJECTILE_CURVE_RANGE, 1.0f);
    Vector3 perpDir = (Vector3){-desiredDir.z, 0.0f, desiredDir.x};
    float wobble = sinf(t->wobblePhase) * TRAIL_PROJECTILE_WOBBLE_AMPLITUDE *
                   curveStrength * dt;
    Vector3 desiredVel = Vector3Add(Vector3Scale(desiredDir, newSpeed),
                                    Vector3Scale(perpDir, wobble));
    t->velocity = Vector3Lerp(t->velocity, desiredVel,
                              dt * TRAIL_PROJECTILE_STEER_LERP_RATE);
  }

  // ── 3 & 4. ForceField → velocity (semi-implicit Euler) + viscosity ────────
  if (t->forceField) {
    Vector3 acc =
        ForceField_Evaluate(t->forceField, t->position, t->velocity, time);
    t->velocity = Vector3Add(t->velocity, Vector3Scale(acc, dt));

    float viscDamp = ForceField_GetViscosityDamping(t->forceField, dt);
    t->velocity = Vector3Scale(t->velocity, viscDamp);
  }

  // ── 5. Tích phân position ────────────────────────────────────────────────
  t->position = Vector3Add(t->position, Vector3Scale(t->velocity, dt));

  // ── 6. Hit-check ──────────────────────────────────────────────────────────
  if (distSqr < TRAIL_PROJECTILE_HIT_DIST_SQR) {
    KillTrailInternal(i);
    return true;
  }
  return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PHYSICS: WISP
// ═══════════════════════════════════════════════════════════════════════════════
static void UpdateWispPhysics(int i, float dt, float time) {
  TrailEntity *t = &trailPool[i];
  if (!t->forceField || t->historyCount < 2 || t->nodeRestLen <= 0.0f)
    return;

  float viscDamp = ForceField_GetViscosityDamping(t->forceField, dt);
  float restLen = t->nodeRestLen;

  // ── Bước A: Semi-implicit Euler — velocity rồi predict position ──────────
  for (int h = 0; h < t->historyCount; h++) {
    Vector3 acc = ForceField_Evaluate(t->forceField, t->history[h],
                                      t->nodeVelocity[h], time);

    t->nodeVelocity[h] = Vector3Scale(
        Vector3Add(t->nodeVelocity[h], Vector3Scale(acc, dt)), viscDamp);

    scratchNodePrevPos[h] = t->history[h];
    t->history[h] =
        Vector3Add(t->history[h], Vector3Scale(t->nodeVelocity[h], dt));
  }

  // ── Bước B: Constraint khoảng cách Gauss-Seidel ──────────────────────────
  for (int iter = 0; iter < WISP_CONSTRAINT_ITERS; iter++) {
    for (int h = 1; h < t->historyCount; h++) {
      ConstrainRibbonSegment(&t->history[h - 1], &t->history[h], restLen,
                             false);
    }
  }

  // ── Bước C: Tái tính velocity từ độ dịch thực tế sau constraint ──────────
  if (dt > 1e-7f) {
    float invDt = 1.0f / dt;
    for (int h = 0; h < t->historyCount; h++) {
      t->nodeVelocity[h] = Vector3Scale(
          Vector3Subtract(t->history[h], scratchNodePrevPos[h]), invDt);
    }
  }

  t->position = t->history[0];
}

// ═══════════════════════════════════════════════════════════════════════════════
// PHYSICS: FOLLOWER
// ═══════════════════════════════════════════════════════════════════════════════
static bool UpdateFollowerPhysics(int i, float dt, float time) {
  TrailEntity *t = &trailPool[i];

  // ── Idle-fade: tự rút ngắn ribbon khi không được UpdateFollowerPosition ───
  t->timeSinceLastFollowerUpdate += dt;
  if (t->timeSinceLastFollowerUpdate > TRAIL_FOLLOWER_IDLE_FADE_TIME) {
    t->fadeAccumulator += TRAIL_FOLLOWER_FADE_RATE_PER_SEC * dt;
    int fadeCount = (int)t->fadeAccumulator;
    if (fadeCount > 0) {
      t->historyCount -= fadeCount;
      t->fadeAccumulator -= (float)fadeCount;
    }
    if (t->historyCount <= 0) {
      KillTrailInternal(i);
      return true;
    }
  }

  if (!t->forceField || t->historyCount < 2)
    return false;

  float viscDamp = ForceField_GetViscosityDamping(t->forceField, dt);

  // ── Per-node semi-implicit Euler ─────────────────────────────────────────
  for (int h = 1; h < t->historyCount; h++) {
    int idx = (t->historyHead - h + TRAIL_HISTORY_COUNT) % TRAIL_HISTORY_COUNT;

    Vector3 acc = ForceField_Evaluate(t->forceField, t->history[idx],
                                      t->nodeVelocity[idx], time);

    t->nodeVelocity[idx] = Vector3Scale(
        Vector3Add(t->nodeVelocity[idx], Vector3Scale(acc, dt)), viscDamp);
    t->history[idx] =
        Vector3Add(t->history[idx], Vector3Scale(t->nodeVelocity[idx], dt));
  }

  return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════════════════════════

void InitTrailSystem(Shader defaultShaderIn) {
  defaultShader = defaultShaderIn;
  shaderCacheCount = 0;
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    trailPool[i].active = false;
    trailPool[i].nextFree = i + 1;
  }
  freeListHead = 0;
  activeCount = 0;
}

TrailEntity *GetTrail(int id) {
  if (id < 0 || id >= MAX_TRAIL_PARTICLES)
    return NULL;
  return &trailPool[id];
}

static void KillTrailInternal(int id) {
  if (trailPool[id].onDeath)
    trailPool[id].onDeath(trailPool[id].position, trailPool[id].scale);
  trailPool[id].active = false;
  trailPool[id].nextFree = freeListHead;
  freeListHead = id;
  activeCount--;
}

void KillTrail(int id) {
  if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active)
    return;
  KillTrailInternal(id);
}

int GetActiveTrailCount(void) { return activeCount; }

int SpawnTrailEntity(TrailConfig config) {
  if (freeListHead >= MAX_TRAIL_PARTICLES)
    return -1;

  int index = freeListHead;
  freeListHead = trailPool[index].nextFree;

  TrailEntity *t = &trailPool[index];
  t->type = config.type;
  t->position = config.pos;
  t->velocity = config.vel;
  t->target = config.target;
  t->length = config.len;
  t->thickness = config.thick;
  t->trailLength = config.trailLength; // Ghi nhận biến độ dài đuôi mới vào pool
  t->lifetime = config.life;
  t->maxLifetime = config.life;
  t->active = true;
  t->angle = config.initialAngle;
  t->wobblePhase = config.wobblePhase;
  t->scale = config.scale;
  t->sprite = config.tex;
  t->shader = config.shader;
  t->tint = config.tint;
  t->onUpdate = config.onUpdate;
  t->onDeath = config.onDeath;
  t->ownerTag = config.ownerTag;
  t->forceField = config.forceField;

  t->timeSinceLastFollowerUpdate = 0.0f;
  t->fadeAccumulator = 0.0f;
  t->historyHead = 0;
  t->driftVelocity = (Vector3){0.0f, 0.0f, 0.0f};

  for (int h = 0; h < TRAIL_HISTORY_COUNT; h++) {
    t->nodeVelocity[h] = (Vector3){0.0f, 0.0f, 0.0f};
  }

  if (config.type == TRAIL_TYPE_WISP) {
    t->historyCount = TRAIL_HISTORY_COUNT;
    t->nodeRestLen = (TRAIL_HISTORY_COUNT > 1 && config.len > 0.0f)
                         ? config.len / (float)(TRAIL_HISTORY_COUNT - 1)
                         : 0.0f;

    Vector3 strandDir = config.target;
    for (int h = 0; h < TRAIL_HISTORY_COUNT; h++) {
      float u = (float)h / (float)(TRAIL_HISTORY_COUNT - 1);
      t->history[h] =
          Vector3Add(config.pos, Vector3Scale(strandDir, u * config.len));
      t->nodeVelocity[h] = config.vel;
    }
  } else if (config.type == TRAIL_TYPE_FOLLOWER) {
    t->historyCount = 0;
    t->nodeRestLen = 0.0f;
  } else {
    t->historyCount = 0;
    t->nodeRestLen = 0.0f;
    for (int h = 0; h < TRAIL_HISTORY_COUNT; h++) {
      t->history[h] = config.pos;
    }
  }

  activeCount++;
  return index;
}

void UpdateFollowerPosition(int id, Vector3 newTipPos) {
  if (id < 0 || id >= MAX_TRAIL_PARTICLES || !trailPool[id].active)
    return;
  if (trailPool[id].type != TRAIL_TYPE_FOLLOWER)
    return;

  TrailEntity *t = &trailPool[id];
  t->historyHead = (t->historyHead + 1) % TRAIL_HISTORY_COUNT;

  t->history[t->historyHead] = newTipPos;
  t->nodeVelocity[t->historyHead] = (Vector3){0.0f, 0.0f, 0.0f};
  t->position = newTipPos;

  // Áp dụng giới hạn độ dài đuôi khi mọc node lụa follower
  int maxNodes =
      (t->trailLength > 0.0f) ? (int)t->trailLength : TRAIL_HISTORY_COUNT;
  if (maxNodes > TRAIL_HISTORY_COUNT)
    maxNodes = TRAIL_HISTORY_COUNT;
  if (maxNodes < 1)
    maxNodes = 1;

  if (t->historyCount < maxNodes) {
    t->historyCount++;
  } else if (t->historyCount > maxNodes) {
    t->historyCount = maxNodes;
  }

  t->timeSinceLastFollowerUpdate = 0.0f;
  t->fadeAccumulator = 0.0f;
}

void UpdateTrailSystem(float dt) {
  float time = (float)GetTime();

  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    if (!trailPool[i].active)
      continue;

    trailPool[i].lifetime -= dt;
    if (trailPool[i].lifetime <= 0.0f) {
      KillTrailInternal(i);
      continue;
    }

    if (trailPool[i].onUpdate)
      trailPool[i].onUpdate(i, dt);

    bool killed = false;
    switch (trailPool[i].type) {
    case TRAIL_TYPE_PROJECTILE:
      killed = UpdateProjectilePhysics(i, dt, time);
      break;
    case TRAIL_TYPE_WISP:
      UpdateWispPhysics(i, dt, time);
      break;
    case TRAIL_TYPE_PORTAL:
      trailPool[i].angle += TRAIL_PORTAL_SPIN_DEG_PER_SEC * dt;
      break;
    case TRAIL_TYPE_FOLLOWER:
      killed = UpdateFollowerPhysics(i, dt, time);
      break;
    }

    if (killed)
      continue;
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// DRAW
// ═══════════════════════════════════════════════════════════════════════════════

static void DrawTrailGeometry(int i, Camera3D camera) {
  float lifeRatio = trailPool[i].lifetime / trailPool[i].maxLifetime;
  Color c = trailPool[i].tint;

  if (trailPool[i].type == TRAIL_TYPE_PROJECTILE) {
    if (trailPool[i].historyCount > 1) {
      for (int h = 0; h < trailPool[i].historyCount; h++) {
        int idx = (trailPool[i].historyHead - h + TRAIL_HISTORY_COUNT) %
                  TRAIL_HISTORY_COUNT;
        float segRatio =
            1.0f - (float)h / (float)(trailPool[i].historyCount - 1);
        float taper = powf(segRatio, TRAIL_PROJECTILE_TAPER_POWER);

        scratchOuter[h].position = trailPool[i].history[idx];
        scratchOuter[h].halfWidth =
            trailPool[i].thickness * TRAIL_PROJECTILE_OUTER_WIDTH_MUL * taper;
        scratchOuter[h].v = segRatio;
        scratchOuter[h].tint = (Color){
            (unsigned char)(segRatio * c.r), c.g, c.b,
            (unsigned char)((c.a / 255.0f) * TRAIL_PROJECTILE_OUTER_ALPHA_MAX *
                            lifeRatio)};

        scratchInner[h].position = trailPool[i].history[idx];
        scratchInner[h].halfWidth =
            trailPool[i].thickness * TRAIL_PROJECTILE_INNER_WIDTH_MUL * taper;
        scratchInner[h].v = segRatio;
        scratchInner[h].tint = (Color){(unsigned char)(segRatio * c.r), c.g,
                                       c.b, (unsigned char)(c.a * lifeRatio)};
      }
      DrawRibbonStrip(scratchOuter, trailPool[i].historyCount, (Texture2D){0},
                      camera);
      DrawRibbonStrip(scratchInner, trailPool[i].historyCount, (Texture2D){0},
                      camera);
    }

    Matrix matView = GetCameraMatrix(camera);
    Vector3 right = {matView.m0, matView.m4, matView.m8};
    Vector3 up = {matView.m1, matView.m5, matView.m9};
    Vector3 vDir = Vector3Normalize(trailPool[i].velocity);
    float rotation =
        atan2f(Vector3DotProduct(vDir, up), Vector3DotProduct(vDir, right));
    Color spriteTint =
        (Color){128, 128, 128, (unsigned char)(255.0f * lifeRatio)};
    DrawCameraFacingQuad(camera, trailPool[i].position,
                         trailPool[i].length * TRAIL_PROJECTILE_QUAD_LENGTH_MUL,
                         trailPool[i].thickness *
                             TRAIL_PROJECTILE_QUAD_THICK_MUL,
                         rotation, spriteTint, trailPool[i].sprite);

  } else if (trailPool[i].type == TRAIL_TYPE_WISP) {
    if (trailPool[i].historyCount > 1) {
      for (int h = 0; h < trailPool[i].historyCount; h++) {
        float segRatio =
            1.0f - (float)h / (float)(trailPool[i].historyCount - 1);
        float taper =
            SmoothStepC(0.0f, TRAIL_WISP_HEAD_TAPER_EDGE, segRatio) *
            SmoothStepC(1.0f, TRAIL_WISP_TAIL_TAPER_EDGE, 1.0f - segRatio);

        scratchOuter[h].position = trailPool[i].history[h];
        scratchOuter[h].halfWidth = trailPool[i].thickness * 0.8f * taper;
        scratchOuter[h].v = segRatio;
        scratchOuter[h].tint =
            (Color){c.r, c.g, c.b, (unsigned char)(c.a * lifeRatio * taper)};
      }
      DrawRibbonStrip(scratchOuter, trailPool[i].historyCount, (Texture2D){0},
                      camera);
    }

  } else if (trailPool[i].type == TRAIL_TYPE_PORTAL) {
    float radius = trailPool[i].length;
    float age = trailPool[i].maxLifetime - trailPool[i].lifetime;
    if (age < TRAIL_PORTAL_SPAWN_GROW_TIME)
      radius *= (age / TRAIL_PORTAL_SPAWN_GROW_TIME);
    Color portalTint = (Color){c.r, c.g, c.b, (unsigned char)(c.a * lifeRatio)};
    DrawCameraFacingQuad(
        camera, trailPool[i].position, radius * TRAIL_PORTAL_QUAD_SIZE_MUL,
        radius * TRAIL_PORTAL_QUAD_SIZE_MUL, trailPool[i].angle * DEG2RAD,
        portalTint, (Texture2D){0});

  } else if (trailPool[i].type == TRAIL_TYPE_FOLLOWER) {
    if (trailPool[i].historyCount > 1) {
      for (int h = 0; h < trailPool[i].historyCount; h++) {
        int idx = (trailPool[i].historyHead - h + TRAIL_HISTORY_COUNT) %
                  TRAIL_HISTORY_COUNT;
        float segRatio =
            1.0f - (float)h / (float)(trailPool[i].historyCount - 1);
        float taper =
            SmoothStepC(0.0f, TRAIL_WISP_HEAD_TAPER_EDGE, segRatio) *
            SmoothStepC(1.0f, TRAIL_WISP_TAIL_TAPER_EDGE, 1.0f - segRatio);

        scratchOuter[h].position = trailPool[i].history[idx];
        scratchOuter[h].halfWidth = trailPool[i].thickness * 0.8f * taper;
        scratchOuter[h].v = segRatio;
        scratchOuter[h].tint =
            (Color){c.r, c.g, c.b, (unsigned char)(c.a * lifeRatio * taper)};
      }
      DrawRibbonStrip(scratchOuter, trailPool[i].historyCount, (Texture2D){0},
                      camera);
    }
  }
}

static unsigned int activeShaderIds[TRAIL_SHADER_CACHE_SIZE];

void DrawTrailEntities(Camera3D camera) {
  if (activeCount == 0)
    return;

  float time = (float)GetTime();
  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  int activeShaderCount = 0;
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    if (!trailPool[i].active)
      continue;
    unsigned int sid = ResolveShader(&trailPool[i]).id;
    bool found = false;
    for (int s = 0; s < activeShaderCount; s++) {
      if (activeShaderIds[s] == sid) {
        found = true;
        break;
      }
    }
    if (!found && activeShaderCount < TRAIL_SHADER_CACHE_SIZE) {
      activeShaderIds[activeShaderCount++] = sid;
    }
  }

  for (int s = 0; s < activeShaderCount; s++) {
    Shader fullShader = defaultShader;
    for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
      if (trailPool[i].active &&
          ResolveShader(&trailPool[i]).id == activeShaderIds[s]) {
        fullShader = ResolveShader(&trailPool[i]);
        break;
      }
    }

    int timeLoc = GetCachedTimeLoc(fullShader);
    if (timeLoc >= 0)
      SetShaderValue(fullShader, timeLoc, &time, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(fullShader);

    for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
      if (!trailPool[i].active)
        continue;
      if (ResolveShader(&trailPool[i]).id != activeShaderIds[s])
        continue;
      DrawTrailGeometry(i, camera);
    }

    EndShaderMode();
  }

  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadTrailSystem(void) {}