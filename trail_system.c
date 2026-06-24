#include "trail_system.h"
#include "force_field.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

static TrailEntity trailPool[MAX_TRAIL_PARTICLES];
static int lastUsedIndex = 0;

static Shader trailShader;
static int timeLocTrail;

static float SmoothStepC(float edge0, float edge1, float x) {
  float t = (x - edge0) / (edge1 - edge0);
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;
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

void InitTrailSystem(void) {
  trailShader = LoadShader(0, "metal.fs");
  timeLocTrail = GetShaderLocation(trailShader, "u_time");

  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    trailPool[i].active = false;
  }
}

TrailEntity *GetTrail(int id) {
  if (id < 0 || id >= MAX_TRAIL_PARTICLES)
    return NULL;
  return &trailPool[id];
}

void KillTrail(int id) {
  if (id >= 0 && id < MAX_TRAIL_PARTICLES && trailPool[id].active) {
    trailPool[id].lifetime = 0.0f;
  }
}

int SpawnTrailEntity(TrailType type, Vector3 pos, Vector3 vel, float len,
                     float thick, float life, Vector3 target,
                     float initialAngle, float wobblePhase, float scale,
                     Texture2D tex, Color tint, TrailUpdateCallback onUpdate,
                     TrailDeathCallback onDeath, int ownerTag) {
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    int index = (lastUsedIndex + i) % MAX_TRAIL_PARTICLES;
    if (!trailPool[index].active) {
      trailPool[index].type = type;
      trailPool[index].position = pos;
      trailPool[index].velocity = vel;
      trailPool[index].target = target;
      trailPool[index].length = len;
      trailPool[index].thickness = thick;
      trailPool[index].lifetime = life;
      trailPool[index].maxLifetime = life;
      trailPool[index].active = true;
      trailPool[index].angle = initialAngle;
      trailPool[index].wobblePhase = wobblePhase;
      trailPool[index].scale = scale;
      trailPool[index].sprite = tex;
      trailPool[index].tint = tint;
      trailPool[index].onUpdate = onUpdate;
      trailPool[index].onDeath = onDeath;
      trailPool[index].ownerTag = ownerTag;
      trailPool[index].forceField = NULL; // khởi tạo NULL; caller tự gán sau nếu cần

      trailPool[index].historyHead = 0; // MỚI: Khởi tạo Head của Ring Buffer

      if (type == TRAIL_TYPE_WISP) {
        trailPool[index].historyCount = TRAIL_HISTORY_COUNT;
        Vector3 strandDir = target;
        float waveFreq = (float)GetRandomValue(10, 20);
        float waveAmp = (float)GetRandomValue(5, 18) * scale;

        for (int h = 0; h < TRAIL_HISTORY_COUNT; h++) {
          float t = (float)h / (TRAIL_HISTORY_COUNT - 1);
          Vector3 basePos = Vector3Add(pos, Vector3Scale(strandDir, t * len));
          Vector3 wUp = (fabsf(strandDir.y) > 0.99f)
                            ? (Vector3){1.0f, 0.0f, 0.0f}
                            : (Vector3){0.0f, 1.0f, 0.0f};
          Vector3 wRight =
              Vector3Normalize(Vector3CrossProduct(strandDir, wUp));
          float wave = sinf(t * waveFreq + wobblePhase) * waveAmp * t;
          trailPool[index].history[h] =
              Vector3Add(basePos, Vector3Scale(wRight, wave));
        }
      } else {
        trailPool[index].historyCount = 0;
        for (int h = 0; h < TRAIL_HISTORY_COUNT; h++) {
          trailPool[index].history[h] = pos;
        }
      }

      lastUsedIndex = (index + 1) % MAX_TRAIL_PARTICLES;
      return index;
    }
  }
  return -1;
}

void UpdateTrailSystem(float dt) {
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    if (!trailPool[i].active)
      continue;

    trailPool[i].lifetime -= dt;
    if (trailPool[i].lifetime <= 0.0f) {
      if (trailPool[i].onDeath)
        trailPool[i].onDeath(trailPool[i].position, trailPool[i].scale);
      trailPool[i].active = false;
      continue;
    }

    if (trailPool[i].onUpdate) {
      trailPool[i].onUpdate(i, dt);
    }

    if (trailPool[i].type == TRAIL_TYPE_PROJECTILE) {
      // TỐI ƯU O(1): Xoay vòng Ring Buffer thay vì dịch mảng
      trailPool[i].historyHead =
          (trailPool[i].historyHead + 1) % TRAIL_HISTORY_COUNT;

      Vector3 dir = Vector3Normalize(trailPool[i].velocity);
      trailPool[i].history[trailPool[i].historyHead] =
          Vector3Subtract(trailPool[i].position,
                          Vector3Scale(dir, trailPool[i].length * 0.45f));

      if (trailPool[i].historyCount < TRAIL_HISTORY_COUNT)
        trailPool[i].historyCount++;

      trailPool[i].wobblePhase += dt * 8.0f;
      trailPool[i].position = Vector3Add(
          trailPool[i].position, Vector3Scale(trailPool[i].velocity, dt));

      // TỐI ƯU TOÁN HỌC: Sử dụng bình phương khoảng cách để tránh sqrtf()
      Vector3 toTarget =
          Vector3Subtract(trailPool[i].target, trailPool[i].position);
      float distSqr = Vector3LengthSqr(toTarget);

      if (distSqr > 400.0f) { // 20.0f ^ 2
        Vector3 desiredDir = Vector3Normalize(toTarget);
        float currentSpeed = Vector3Length(trailPool[i].velocity);
        float newSpeed = fminf(currentSpeed + 150.0f * dt, 600.0f);

        // Chỉ gọi sqrtf() để nội suy khi thực sự cần thiết (tính curveStrength)
        float distToTarget = sqrtf(distSqr);
        float curveStrength = fminf(distToTarget / 250.0f, 1.0f);

        Vector3 perpDir = (Vector3){-desiredDir.z, 0.0f, desiredDir.x};
        float wobble =
            sinf(trailPool[i].wobblePhase) * 350.0f * curveStrength * dt;
        Vector3 desiredVel = Vector3Add(Vector3Scale(desiredDir, newSpeed),
                                        Vector3Scale(perpDir, wobble));
        trailPool[i].velocity =
            Vector3Lerp(trailPool[i].velocity, desiredVel, dt * 3.2f);
      }

      if (distSqr < 900.0f) { // 30.0f ^ 2
        if (trailPool[i].onDeath)
          trailPool[i].onDeath(trailPool[i].position, trailPool[i].scale);
        trailPool[i].active = false;
      }

    } else if (trailPool[i].type == TRAIL_TYPE_WISP) {
      trailPool[i].velocity =
          Vector3Scale(trailPool[i].velocity, 1.0f - (dt * 0.8f));
      Vector3 drift = Vector3Scale(trailPool[i].velocity, dt);
      trailPool[i].wobblePhase += dt * 15.0f;

      Vector3 sDir = trailPool[i].target;
      Vector3 wUp = (fabsf(sDir.y) > 0.99f) ? (Vector3){1.0f, 0.0f, 0.0f}
                                            : (Vector3){0.0f, 1.0f, 0.0f};
      Vector3 wRight = Vector3Normalize(Vector3CrossProduct(sDir, wUp));

      for (int h = 0; h < trailPool[i].historyCount; h++) {
        float t = (float)h / (TRAIL_HISTORY_COUNT - 1);
        trailPool[i].history[h] = Vector3Add(trailPool[i].history[h], drift);
        float wriggle =
            cosf(t * 15.0f + trailPool[i].wobblePhase) * 12.0f * t * dt;
        trailPool[i].history[h] =
            Vector3Add(trailPool[i].history[h], Vector3Scale(wRight, wriggle));
      }
    } else if (trailPool[i].type == TRAIL_TYPE_PORTAL) {
      trailPool[i].angle += 140.0f * dt;
    }

    // ÁP DỤNG FORCE FIELD (nếu trail này có gán ForceField)
    if (trailPool[i].forceField) {
      Vector3 acc = ForceField_Evaluate(
          trailPool[i].forceField,
          trailPool[i].position,
          trailPool[i].velocity,
          (float)GetTime());
      trailPool[i].velocity = Vector3Add(
          trailPool[i].velocity, Vector3Scale(acc, dt));
    }
  }
}

void DrawTrailEntities(Camera3D camera) {
  bool active = false;
  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    if (trailPool[i].active) {
      active = true;
      break;
    }
  }
  if (!active)
    return; // Vẫn giữ chốt chặn sớm

  float time = (float)GetTime();

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  SetShaderValue(trailShader, timeLocTrail, &time, SHADER_UNIFORM_FLOAT);
  BeginShaderMode(trailShader);

  for (int i = 0; i < MAX_TRAIL_PARTICLES; i++) {
    if (!trailPool[i].active)
      continue;
    float lifeRatio = trailPool[i].lifetime / trailPool[i].maxLifetime;
    Color c = trailPool[i].tint;

    if (trailPool[i].type == TRAIL_TYPE_PROJECTILE) {
      if (trailPool[i].historyCount > 1) {
        RibbonPoint outerStrip[TRAIL_HISTORY_COUNT];
        RibbonPoint innerStrip[TRAIL_HISTORY_COUNT];

        for (int h = 0; h < trailPool[i].historyCount; h++) {
          // MỚI: Tính vị trí thực trong Ring Buffer (h=0 là Head, h lớn hơn sẽ
          // lùi về quá khứ)
          int idx = (trailPool[i].historyHead - h + TRAIL_HISTORY_COUNT) %
                    TRAIL_HISTORY_COUNT;

          float segRatio =
              1.0f - (float)h / (float)(trailPool[i].historyCount - 1);
          float taper = powf(segRatio, 1.2f);

          outerStrip[h].position =
              trailPool[i].history[idx]; // Đọc theo Index vòng tròn
          outerStrip[h].halfWidth = trailPool[i].thickness * 1.3f * taper;
          outerStrip[h].v = segRatio;
          outerStrip[h].tint =
              (Color){(unsigned char)(segRatio * c.r), c.g, c.b,
                      (unsigned char)((c.a / 255.0f) * 80.0f * lifeRatio)};

          innerStrip[h].position =
              trailPool[i].history[idx]; // Đọc theo Index vòng tròn
          innerStrip[h].halfWidth = trailPool[i].thickness * 0.65f * taper;
          innerStrip[h].v = segRatio;
          innerStrip[h].tint = (Color){(unsigned char)(segRatio * c.r), c.g,
                                       c.b, (unsigned char)(c.a * lifeRatio)};
        }
        DrawRibbonStrip(outerStrip, trailPool[i].historyCount, (Texture2D){0},
                        camera);
        DrawRibbonStrip(innerStrip, trailPool[i].historyCount, (Texture2D){0},
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
                           trailPool[i].length * 1.1f,
                           trailPool[i].thickness * 2.0f, rotation, spriteTint,
                           trailPool[i].sprite);

    } else if (trailPool[i].type == TRAIL_TYPE_WISP) {
      if (trailPool[i].historyCount > 1) {
        RibbonPoint wispStrip[TRAIL_HISTORY_COUNT];
        for (int h = 0; h < trailPool[i].historyCount; h++) {
          float segRatio =
              1.0f - (float)h / (float)(trailPool[i].historyCount - 1);
          float taper = SmoothStepC(0.0f, 0.2f, segRatio) *
                        SmoothStepC(1.0f, 0.5f, 1.0f - segRatio);

          wispStrip[h].position =
              trailPool[i].history[h]; // Wisp được cập nhật tuần tự tại chỗ,
                                       // không dùng Ring Buffer
          wispStrip[h].halfWidth = trailPool[i].thickness * 0.8f * taper;
          wispStrip[h].v = segRatio;

          unsigned char finalAlpha = (unsigned char)(c.a * lifeRatio * taper);
          wispStrip[h].tint = (Color){c.r, c.g, c.b, finalAlpha};
        }
        DrawRibbonStrip(wispStrip, trailPool[i].historyCount, (Texture2D){0},
                        camera);
      }
    } else if (trailPool[i].type == TRAIL_TYPE_PORTAL) {
      float radius = trailPool[i].length;
      float age = trailPool[i].maxLifetime - trailPool[i].lifetime;
      if (age < 0.12f)
        radius *= (age / 0.12f);

      Color portalTint =
          (Color){c.r, c.g, c.b, (unsigned char)(c.a * lifeRatio)};
      DrawCameraFacingQuad(camera, trailPool[i].position, radius * 2.6f,
                           radius * 2.6f, trailPool[i].angle * DEG2RAD,
                           portalTint, (Texture2D){0});
    }
  }

  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadTrailSystem(void) { UnloadShader(trailShader); }