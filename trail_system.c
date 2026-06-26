#include "trail_system.h"
#include "force_field.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

static TrailEntity trailPool[MAX_TRAIL_PARTICLES];
static int freeListHead = 0; // Đầu free-list, O(1) thay cho quét tuyến tính
static int activeCount = 0;

static Shader defaultShader; // Dùng khi TrailConfig.shader.id == 0

// Cache location "u_time" theo từng shader id khác nhau - vì mỗi shader
// program có location riêng cho cùng tên uniform (driver tự gán, không cố
// định). Static array nhỏ, no-malloc, đủ cho số shader khác nhau thực tế
// dùng cùng lúc trong 1 hệ thống chiêu thức (kim, hỏa, thổ, thủy, mộc +
// vài shader phụ).
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

static RibbonPoint scratchOuter[TRAIL_HISTORY_COUNT];
static RibbonPoint scratchInner[TRAIL_HISTORY_COUNT];

static Shader ResolveShader(const TrailEntity *t) {
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

  trailPool[index].type = config.type;
  trailPool[index].position = config.pos;
  trailPool[index].velocity = config.vel;
  trailPool[index].target = config.target;
  trailPool[index].length = config.len;
  trailPool[index].thickness = config.thick;
  trailPool[index].lifetime = config.life;
  trailPool[index].maxLifetime = config.life;
  trailPool[index].active = true;
  trailPool[index].angle = config.initialAngle;
  trailPool[index].wobblePhase = config.wobblePhase;
  trailPool[index].scale = config.scale;
  trailPool[index].sprite = config.tex;
  trailPool[index].shader = config.shader;
  trailPool[index].tint = config.tint;
  trailPool[index].onUpdate = config.onUpdate;
  trailPool[index].onDeath = config.onDeath;
  trailPool[index].ownerTag = config.ownerTag;
  trailPool[index].forceField = config.forceField;

  trailPool[index].timeSinceLastFollowerUpdate = 0.0f;
  trailPool[index].fadeAccumulator = 0.0f;
  trailPool[index].historyHead = 0;

  if (config.type == TRAIL_TYPE_WISP) {
    trailPool[index].historyCount = TRAIL_HISTORY_COUNT;
    Vector3 strandDir = config.target;
    for (int h = 0; h < TRAIL_HISTORY_COUNT; h++) {
      float t = (float)h / (TRAIL_HISTORY_COUNT - 1);
      trailPool[index].history[h] =
          Vector3Add(config.pos, Vector3Scale(strandDir, t * config.len));
    }
  } else if (config.type == TRAIL_TYPE_FOLLOWER) {
    trailPool[index].historyCount = 0;
  } else {
    trailPool[index].historyCount = 0;
    for (int h = 0; h < TRAIL_HISTORY_COUNT; h++) {
      trailPool[index].history[h] = config.pos;
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

  trailPool[id].historyHead =
      (trailPool[id].historyHead + 1) % TRAIL_HISTORY_COUNT;
  trailPool[id].history[trailPool[id].historyHead] = newTipPos;
  trailPool[id].position = newTipPos;

  if (trailPool[id].historyCount < TRAIL_HISTORY_COUNT) {
    trailPool[id].historyCount++;
  }

  // Reset timer idle do vừa được cập nhật vị trí mới
  trailPool[id].timeSinceLastFollowerUpdate = 0.0f;
  trailPool[id].fadeAccumulator = 0.0f;
}

void UpdateTrailSystem(float dt) {
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

    if (trailPool[i].type == TRAIL_TYPE_PROJECTILE) {
      trailPool[i].historyHead =
          (trailPool[i].historyHead + 1) % TRAIL_HISTORY_COUNT;
      Vector3 dir = Vector3Normalize(trailPool[i].velocity);
      trailPool[i].history[trailPool[i].historyHead] = Vector3Subtract(
          trailPool[i].position,
          Vector3Scale(dir, trailPool[i].length *
                                TRAIL_PROJECTILE_SPAWN_OFFSET_MUL));

      if (trailPool[i].historyCount < TRAIL_HISTORY_COUNT)
        trailPool[i].historyCount++;

      trailPool[i].wobblePhase += dt * TRAIL_PROJECTILE_WOBBLE_FREQ;
      trailPool[i].position = Vector3Add(
          trailPool[i].position, Vector3Scale(trailPool[i].velocity, dt));

      Vector3 toTarget =
          Vector3Subtract(trailPool[i].target, trailPool[i].position);
      float distSqr = Vector3LengthSqr(toTarget);

      if (distSqr > TRAIL_PROJECTILE_RETARGET_DIST_SQR) {
        Vector3 desiredDir = Vector3Normalize(toTarget);
        float currentSpeed = Vector3Length(trailPool[i].velocity);
        float newSpeed = fminf(currentSpeed + TRAIL_PROJECTILE_ACCEL_RATE * dt,
                               TRAIL_PROJECTILE_MAX_SPEED);
        float distToTarget = sqrtf(distSqr);
        float curveStrength =
            fminf(distToTarget / TRAIL_PROJECTILE_CURVE_RANGE, 1.0f);

        Vector3 perpDir = (Vector3){-desiredDir.z, 0.0f, desiredDir.x};
        float wobble = sinf(trailPool[i].wobblePhase) *
                       TRAIL_PROJECTILE_WOBBLE_AMPLITUDE * curveStrength * dt;
        Vector3 desiredVel = Vector3Add(Vector3Scale(desiredDir, newSpeed),
                                        Vector3Scale(perpDir, wobble));
        trailPool[i].velocity =
            Vector3Lerp(trailPool[i].velocity, desiredVel,
                        dt * TRAIL_PROJECTILE_STEER_LERP_RATE);
      }

      if (distSqr < TRAIL_PROJECTILE_HIT_DIST_SQR) {
        KillTrailInternal(i);
        continue;
      }
    } else if (trailPool[i].type == TRAIL_TYPE_WISP) {
      // WISP đã được gọt sạch chuyển động mặc định (sin/cos/drag/drift)
      // Giờ WISP chỉ phụ thuộc vào ForceField giống FOLLOWER.
    } else if (trailPool[i].type == TRAIL_TYPE_PORTAL) {
      trailPool[i].angle += TRAIL_PORTAL_SPIN_DEG_PER_SEC * dt;
    } else if (trailPool[i].type == TRAIL_TYPE_FOLLOWER) {
      // 1. Tự rút ngắn/chết khi không được cập nhật (ví dụ kiếm biến mất)
      trailPool[i].timeSinceLastFollowerUpdate += dt;
      if (trailPool[i].timeSinceLastFollowerUpdate >
          TRAIL_FOLLOWER_IDLE_FADE_TIME) {
        trailPool[i].fadeAccumulator += TRAIL_FOLLOWER_FADE_RATE_PER_SEC * dt;
        int fadeCount = (int)trailPool[i].fadeAccumulator;

        if (fadeCount > 0) {
          trailPool[i].historyCount -= fadeCount;
          trailPool[i].fadeAccumulator -= (float)fadeCount;
        }

        if (trailPool[i].historyCount <= 0) {
          KillTrailInternal(i);
          continue;
        }
      }

      // 2. Tác động ngoại lực xoáy 3D vào đuôi dải lụa
      if (trailPool[i].forceField && trailPool[i].historyCount > 1) {
        // LUÔN BỎ QUA ĐIỂM GỐC h=0 ĐỂ ĐẢM BẢO MỎ NEO DÍNH CHẶT VÀO KIẾM
        for (int h = 1; h < trailPool[i].historyCount; h++) {
          int idx = (trailPool[i].historyHead - h + TRAIL_HISTORY_COUNT) %
                    TRAIL_HISTORY_COUNT;
          Vector3 pt = trailPool[i].history[idx];

          Vector3 acc =
              ForceField_Evaluate(trailPool[i].forceField, pt,
                                  (Vector3){0, 0, 0}, (float)GetTime());
          trailPool[i].history[idx] =
              Vector3Add(pt, Vector3Scale(acc, dt * 0.05f));
        }
      }
    }

    if (trailPool[i].forceField && trailPool[i].type != TRAIL_TYPE_FOLLOWER) {
      Vector3 acc =
          ForceField_Evaluate(trailPool[i].forceField, trailPool[i].position,
                              trailPool[i].velocity, (float)GetTime());
      trailPool[i].velocity =
          Vector3Add(trailPool[i].velocity, Vector3Scale(acc, dt));
    }
  }
}

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
        unsigned char finalAlpha = (unsigned char)(c.a * lifeRatio * taper);
        scratchOuter[h].tint = (Color){c.r, c.g, c.b, finalAlpha};
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
        unsigned char finalAlpha = (unsigned char)(c.a * lifeRatio * taper);
        scratchOuter[h].tint = (Color){c.r, c.g, c.b, finalAlpha};
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
    Shader shader = {0};
    shader.id = activeShaderIds[s];

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
      if (ResolveShader(&trailPool[i]).id != shader.id)
        continue;
      DrawTrailGeometry(i, camera);
    }

    EndShaderMode();
  }

  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadTrailSystem(void) {}