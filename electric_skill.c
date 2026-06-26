#include "electric_skill.h"
#include "color_gradient.h"
#include "flow_map.h"
#include "force_field.h"
#include "particle_system.h"
#include "raymath.h"
#include "ribbon_strip.h" // <--- THÊM DÒNG NÀY VÀO ĐÂY
#include "rlgl.h"
#include "sprite_anim.h"
#include <math.h>

#define MAX_EMITTERS 10
#define LIGHTNING_MAX_POINTS 24

static ForceField s_electricSparkField;
static ForceField s_electricWispField; // Curl noise + Trục hút lôi xoáy

static ColorGradient s_electricGradient;
static FlowMapConfig s_electricFlowCfg = {
    .speed = 3.0f, .strength = 0.15f, .tiling = 4.0f};

// Hệ thống Shader chuyên dụng Phase 1
static Shader s_additiveShader;
static Shader s_rimGlowShader;
static Shader s_flowShader;

// Quản lý Procedural Textures tránh phụ thuộc Disk Asset
static Texture2D s_proceduralMainTex;
static Texture2D s_proceduralFlowTex;

typedef struct {
  bool active;
  Vector3 startPos;
  Vector3 targetPos;
  Vector3 currentPos;
  float progress;
  float durationTimer;
  float spawnAccumulator;
  bool impacted;
  float flashTimer;
  Vector3 lightningPath[LIGHTNING_MAX_POINTS];
  int pathCount;
  float sizeScale;
} ElectricEmitter;

static ElectricEmitter emitters[MAX_EMITTERS];
extern Camera3D camera;

// --- Sinh hạt cấu trúc Procedural bên trong RAM ---
static Texture2D GenerateElectricTexture(void) {
  int w = 64, h = 16;
  Image img = GenImageColor(w, h, BLANK);
  for (int y = 0; y < h; y++) {
    float dy = (float)y / (h - 1) - 0.5f;
    float gaussian = expf(
        -4.0f * dy * dy); // Phân phối chuẩn Gauss tạo lõi sáng ở trung tâm Y
    for (int x = 0; x < w; x++) {
      unsigned char alpha = (unsigned char)(255.0f * gaussian);
      ImageDrawPixel(&img, x, y, (Color){255, 255, 255, alpha});
    }
  }
  Texture2D tex = LoadTextureFromImage(img);
  UnloadImage(img);
  return tex;
}

static Texture2D GenerateFlowMapTexture(void) {
  int w = 64, h = 64;
  Image img = GenImageColor(w, h, BLANK);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      unsigned char r =
          (unsigned char)((float)x / w * 255.0f); // Dịch chuyển ngang sang phải
      unsigned char g =
          (unsigned char)((0.5f + 0.15f * sinf(((float)y / h) * PI * 2.0f)) *
                          255.0f); // Tạo sóng gợn nhẹ trục dọc
      ImageDrawPixel(&img, x, y, (Color){r, g, 0, 255});
    }
  }
  Texture2D tex = LoadTextureFromImage(img);
  UnloadImage(img);
  return tex;
}

static void GenerateJaggedPath(Vector3 start, Vector3 end, Vector3 *outPoints,
                               int *outCount, int maxPoints,
                               float maxDisplacement) {
  outPoints[0] = start;
  outPoints[maxPoints - 1] = end;
  *outCount = maxPoints;
  Vector3 dir = Vector3Subtract(end, start);
  float len = Vector3Length(dir);
  if (len < 1.0f) {
    *outCount = 2;
    outPoints[1] = end;
    return;
  }

  Vector3 perp = Vector3Normalize((Vector3){-dir.z, 0.0f, dir.x});
  if (Vector3Length(perp) == 0.0f)
    perp = (Vector3){0, 0, 1};
  Vector3 up = Vector3Normalize(Vector3CrossProduct(dir, perp));

  for (int i = 1; i < maxPoints - 1; i++) {
    float t = (float)i / (maxPoints - 1);
    Vector3 base = Vector3Add(start, Vector3Scale(dir, t));
    float envelope =
        sinf(t * PI); // Triệt tiêu độ lệch ở 2 điểm đầu mút phóng sét
    float disp0 = ((float)GetRandomValue(-1000, 1000) / 1000.0f) *
                  maxDisplacement * envelope;
    float disp1 = ((float)GetRandomValue(-1000, 1000) / 1000.0f) *
                  maxDisplacement * envelope;
    outPoints[i] = Vector3Add(
        base, Vector3Add(Vector3Scale(perp, disp0), Vector3Scale(up, disp1)));
  }
}

void InitElectricSkill(int screenWidth, int screenHeight) {
  s_additiveShader = LoadShader(0, "shaders/additive_soft.fs");
  s_rimGlowShader = LoadShader(0, "shaders/rim_glow.fs");
  s_flowShader = FlowMap_LoadShader("shaders/flow_map.fs");

  s_proceduralMainTex = GenerateElectricTexture();
  s_proceduralFlowTex = GenerateFlowMapTexture();
  s_electricGradient = ColorGradient_MakeElectric();

  for (int i = 0; i < MAX_EMITTERS; i++)
    emitters[i].active = false;

  // Lực hỗn loạn giật tia sét nổ (Perlin Noise tốc độ cao)
  ForceField_Clear(&s_electricSparkField);
  ForceField_AddLayer(&s_electricSparkField,
                      (ForceLayer){.type = FORCE_NOISE_PERLIN,
                                   .strength = 120.0f,
                                   .noiseScale = 0.04f,
                                   .noiseSpeed = 5.0f});

  // Trường lực của Wisp luồng điện: Kết hợp Curl Noise chao đảo mạnh + Trục
  // xoáy hướng tâm thu hẹp đường sét
  ForceField_Clear(&s_electricWispField);
  ForceField_AddLayer(&s_electricWispField,
                      (ForceLayer){.type = FORCE_NOISE_CURL,
                                   .strength = 200.0f,
                                   .noiseScale = 0.08f,
                                   .noiseSpeed = 150.0f});
  ForceField_AddLayer(&s_electricWispField,
                      (ForceLayer){.type = FORCE_RADIAL_AXIS,
                                   .strength = -150.0f,
                                   .radius = 30.0f,
                                   .falloff = 1.0f});
}

void CastElectricSkill(Vector3 startPos, Vector3 target, float sizeScale) {
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (!emitters[i].active) {
      emitters[i].active = true;
      emitters[i].startPos = startPos;
      emitters[i].targetPos = target;
      emitters[i].currentPos = startPos;
      emitters[i].progress = 0.0f;
      emitters[i].durationTimer =
          0.6f; // Thời gian giật điện duy trì (Short lifetime)
      emitters[i].spawnAccumulator = 0.0f;
      emitters[i].impacted = false;
      emitters[i].flashTimer = 0.0f;
      emitters[i].sizeScale = sizeScale;
      break;
    }
  }
}

static void ElectricDeathCallback(Vector3 impactPos, float scale) {
  // Thực thi 8-14 hạt tia lửa điện phân rã bằng dải màu gradient điện
  int sparkCount = GetRandomValue(8, 14);
  for (int i = 0; i < sparkCount; i++) {
    float angle = ((float)GetRandomValue(0, 360) * DEG2RAD);
    float pitch = ((float)GetRandomValue(-90, 90) * DEG2RAD);
    float speed = (200.0f + (float)GetRandomValue(0, 200)) * scale;

    Vector3 vel = {cosf(angle) * speed * cosf(pitch), sinf(pitch) * speed,
                   sinf(angle) * speed * cosf(pitch)};

    ParticleConfig p = {0};
    p.position = impactPos;
    p.velocity = vel;
    p.drag = 1.5f;
    p.radius = (3.0f + (float)GetRandomValue(0, 40) / 10.0f) * scale;
    p.lifetime = 0.3f + (float)GetRandomValue(0, 30) / 100.0f;
    p.physicsFlags = P_PHYSICS_DRAG;
    p.forceField = &s_electricSparkField;
    // Chèn con trỏ mở rộng Phase 1 (Sử dụng hệ thống hạt hiện tại thông qua cơ
    // chế nội suy mới)
    p.colorStart = ColorGradient_Sample(&s_electricGradient, 0.0f);
    p.colorEnd = ColorGradient_Sample(&s_electricGradient, 1.0f);
    SpawnParticle(p);
  }

  // Tỏa sóng Plasma kích nổ (Flash Shockwave bán kính cực lớn, biến mất rất
  // nhanh)
  ParticleConfig flash = {0};
  flash.position = impactPos;
  flash.velocity = (Vector3){0, 0, 0};
  flash.radius = 120.0f * scale;
  flash.lifetime = 0.15f; // Thời gian chớp tắt siêu ngắn
  flash.colorStart = (Color){255, 255, 255, 100};
  flash.colorEnd = (Color){100, 180, 255, 0};
  SpawnParticle(flash);

  // TODO: CameraFX_Shake(0.4f); // Rung màn hình chấn động lôi kích thiên kiếp
}

void UpdateElectricSkill(float dt) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active)
      continue;

    if (!emitters[e].impacted) {
      // Đạn di chuyển tốc độ siêu cao (600 - 900 units/s)
      emitters[e].progress +=
          dt * (750.0f /
                Vector3Distance(emitters[e].startPos, emitters[e].targetPos));

      if (emitters[e].progress >= 1.0f) {
        emitters[e].progress = 1.0f;
        emitters[e].impacted = true;
        emitters[e].currentPos = emitters[e].targetPos;

        // Kích hoạt nổ dây chuyền từ trên không trung (Thiên kiếp đánh xuống)
        Vector3 skyPos = {emitters[e].targetPos.x,
                          emitters[e].targetPos.y + 500.0f,
                          emitters[e].targetPos.z};
        GenerateJaggedPath(skyPos, emitters[e].targetPos,
                           emitters[e].lightningPath, &emitters[e].pathCount,
                           LIGHTNING_MAX_POINTS, 40.0f * emitters[e].sizeScale);

        ElectricDeathCallback(emitters[e].targetPos, emitters[e].sizeScale);
      } else {
        emitters[e].currentPos = Vector3Lerp(
            emitters[e].startPos, emitters[e].targetPos, emitters[e].progress);

        // Tạo chuỗi bụi hạt điện trường dọc đường đạn bay qua
        emitters[e].spawnAccumulator += dt;
        if (emitters[e].spawnAccumulator >= 0.01f) {
          ParticleConfig trail = {0};
          trail.position = emitters[e].currentPos;
          trail.velocity = (Vector3){(float)GetRandomValue(-20, 20),
                                     (float)GetRandomValue(-20, 20),
                                     (float)GetRandomValue(-20, 20)};
          trail.radius = 5.0f * emitters[e].sizeScale;
          trail.lifetime = 0.2f;
          trail.colorStart = (Color){200, 245, 255, 255};
          trail.colorEnd = (Color){30, 80, 255, 0};
          SpawnParticle(trail);
          emitters[e].spawnAccumulator = 0.0f;
        }
      }
    } else {
      // Giai đoạn Shock liên tục tích điện tại điểm va chạm
      emitters[e].durationTimer -= dt;
      if (emitters[e].durationTimer <= 0.0f) {
        emitters[e].active = false;
        continue;
      }

      // Làm mới đường dẫn lôi kích ngẫu nhiên tạo độ giật chớp chớp liên tục
      emitters[e].flashTimer += dt;
      if (emitters[e].flashTimer >= 0.06f) {
        Vector3 skyPos = {emitters[e].targetPos.x + GetRandomValue(-40, 40),
                          emitters[e].targetPos.y + 500.0f,
                          emitters[e].targetPos.z + GetRandomValue(-40, 40)};
        GenerateJaggedPath(skyPos, emitters[e].targetPos,
                           emitters[e].lightningPath, &emitters[e].pathCount,
                           LIGHTNING_MAX_POINTS, 35.0f * emitters[e].sizeScale);
        emitters[e].flashTimer = 0.0f;
      }
    }
  }
}

void DrawElectricSkill(void) {
  rlDisableDepthMask();
  BeginBlendMode(BLEND_ADDITIVE);

  float time = (float)GetTime();

  // Áp dụng kỹ thuật Flow Map để đẩy dòng năng lượng chuyển động chạy cuộn dọc
  // thân tia sét
  BeginShaderMode(s_flowShader);
  FlowMap_Apply(s_flowShader, &s_electricFlowCfg, s_proceduralFlowTex, time);

  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (!emitters[e].active || !emitters[e].impacted)
      continue;

    float fade = emitters[e].durationTimer / 0.6f;
    if (emitters[e].pathCount > 1) {
      RibbonPoint strip[LIGHTNING_MAX_POINTS];
      float w = 25.0f * fade * emitters[e].sizeScale;

      for (int i = 0; i < emitters[e].pathCount; i++) {
        strip[i].position = emitters[e].lightningPath[i];
        strip[i].halfWidth = w * 0.5f;
        strip[i].v = (float)i / (emitters[e].pathCount - 1);

        // Phối hòa dải màu sắc Cyan cực đại pha trộn Electric Blue vùng rìa
        // biên nhờ shader rim_glow phối hợp
        strip[i].tint = (Color){100, 180, 255, (unsigned char)(200.0f * fade)};
      }

      // Thực hiện vẽ dải ribbon đối diện máy ảnh thông qua module dùng chung
      // nền tảng
      DrawRibbonStrip(strip, emitters[e].pathCount, s_proceduralMainTex,
                      camera);
    }
  }
  EndShaderMode();

  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadElectricSkill(void) {
  UnloadShader(s_additiveShader);
  UnloadShader(s_rimGlowShader);
  UnloadShader(s_flowShader);
  UnloadTexture(s_proceduralMainTex);
  UnloadTexture(s_proceduralFlowTex);
}

int GetElectricSkillProjectiles(SkillProjectile *out, int maxProjectiles) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && !emitters[i].impacted && count < maxProjectiles) {
      out[count].position = emitters[i].currentPos;
      out[count].radius = 15.0f * emitters[i].sizeScale;
      out[count].active = true;
      count++;
    }
  }
  return count;
}

void DeactivateElectricProjectile(int index) {
  int count = 0;
  for (int i = 0; i < MAX_EMITTERS; i++) {
    if (emitters[i].active && !emitters[i].impacted) {
      if (count == index) {
        emitters[i].progress = 1.0f;
        emitters[i].impacted = true;
        emitters[i].currentPos = emitters[i].targetPos;
        Vector3 skyPos = {emitters[i].targetPos.x,
                          emitters[i].targetPos.y + 500.0f,
                          emitters[i].targetPos.z};
        GenerateJaggedPath(skyPos, emitters[i].targetPos,
                           emitters[i].lightningPath, &emitters[i].pathCount,
                           LIGHTNING_MAX_POINTS, 40.0f * emitters[i].sizeScale);
        ElectricDeathCallback(emitters[i].targetPos, emitters[i].sizeScale);
        break;
      }
      count++;
    }
  }
}

bool IsElectricSkillShocking(void) {
  for (int e = 0; e < MAX_EMITTERS; e++) {
    if (emitters[e].active && emitters[e].impacted) {
      return true;
    }
  }
  return false;
}