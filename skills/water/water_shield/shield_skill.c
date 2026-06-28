#include "skills/water/water_shield/shield_skill.h"
#include "core/flow_map.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stddef.h>

typedef struct {
  bool active;
  Vector3 position;
  float radius;
  float maxLifetime;
  float lifetimeTimer;
  float introAnimTimer;
} WaterShield;

static WaterShield s_shield = {0};

static Shader s_shieldShader;
static Texture2D s_waterCausticsTex;
static FlowMap s_shieldFlow; // instance riêng cho shield, không đụng các skill khác

static int uTimeLoc;
static int uViewPosLoc;
static int uCausticsTexLoc;

extern Camera3D camera;

// -----------------------------------------------------------------------------
// ÁP DỤNG KỸ THUẬT TỪ FLUID SKILL: VẼ KHỐI CẦU THỦ CÔNG TẠI TỌA ĐỘ THẾ GIỚI
// -----------------------------------------------------------------------------
static void RenderShieldSphere(Vector3 center, float radius) {
  int rings = 32;
  int slices = 32;

  rlCheckRenderBatchLimit(rings * slices * 4);
  rlBegin(RL_QUADS);
  for (int i = 0; i < rings; i++) {
    float theta1 = (float)i * PI / rings;
    float theta2 = (float)(i + 1) * PI / rings;

    float st1 = sinf(theta1), ct1 = cosf(theta1);
    float st2 = sinf(theta2), ct2 = cosf(theta2);

    float vt1 = (float)i / rings;
    float vt2 = (float)(i + 1) / rings;

    for (int j = 0; j < slices; j++) {
      float phi1 = (float)j * (2.0f * PI) / slices;
      float phi2 =
          (j + 1 == slices) ? 0.0f : (float)(j + 1) * (2.0f * PI) / slices;

      float cp1 = cosf(phi1), sp1 = sinf(phi1);
      float cp2 = cosf(phi2), sp2 = sinf(phi2);

      float u1 = (float)j / slices;
      float u2 = (float)(j + 1) / slices;

      Vector3 n1 = {st1 * cp1, ct1, st1 * sp1};
      Vector3 n2 = {st1 * cp2, ct1, st1 * sp2};
      Vector3 n3 = {st2 * cp2, ct2, st2 * sp2};
      Vector3 n4 = {st2 * cp1, ct2, st2 * sp1};

      // rlColor4ub trên từng đỉnh để shader không bị nhân màu đen
      rlNormal3f(n1.x, n1.y, n1.z);
      rlTexCoord2f(u1, vt1);
      rlColor4ub(255, 255, 255, 255);
      rlVertex3f(center.x + n1.x * radius, center.y + n1.y * radius,
                 center.z + n1.z * radius);

      rlNormal3f(n2.x, n2.y, n2.z);
      rlTexCoord2f(u2, vt1);
      rlColor4ub(255, 255, 255, 255);
      rlVertex3f(center.x + n2.x * radius, center.y + n2.y * radius,
                 center.z + n2.z * radius);

      rlNormal3f(n3.x, n3.y, n3.z);
      rlTexCoord2f(u2, vt2);
      rlColor4ub(255, 255, 255, 255);
      rlVertex3f(center.x + n3.x * radius, center.y + n3.y * radius,
                 center.z + n3.z * radius);

      rlNormal3f(n4.x, n4.y, n4.z);
      rlTexCoord2f(u1, vt2);
      rlColor4ub(255, 255, 255, 255);
      rlVertex3f(center.x + n4.x * radius, center.y + n4.y * radius,
                 center.z + n4.z * radius);
    }
  }
  rlEnd();
}

void InitShieldSkill(int screenWidth, int screenHeight) {
  (void)screenWidth;
  (void)screenHeight;
  s_shieldShader = LoadShader("skills/water/water_shield/shield.vs", "skills/water/water_shield/shield.fs");

  uTimeLoc = GetShaderLocation(s_shieldShader, "u_time");
  uViewPosLoc = GetShaderLocation(s_shieldShader, "viewPos");
  uCausticsTexLoc = GetShaderLocation(s_shieldShader, "causticsTex");

  s_waterCausticsTex = LoadTexture("assets/textures/water_caustics.png");
  SetTextureFilter(s_waterCausticsTex, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(s_waterCausticsTex, TEXTURE_WRAP_REPEAT);

  // FlowMap module dùng chung: tự sinh vortex texture và sở hữu nó.
  // Location được cache RIÊNG trong s_shieldFlow, không đụng skill khác dù
  // skill đó cũng dùng flow map trên shader của nó (vd. fire, tornado).
  s_shieldFlow = FlowMap_CreateWithVortexTexture(s_shieldShader, 128, NULL);
  s_shieldFlow.cfg.speed = 1.2f;
  s_shieldFlow.cfg.strength = 0.05f;
  s_shieldFlow.cfg.tiling = 1.0f;
}

void CastShieldSkill(Vector3 position, float radius, float duration) {
  s_shield.active = true;
  s_shield.position = position;
  s_shield.radius = radius;
  s_shield.maxLifetime = duration;
  s_shield.lifetimeTimer = duration;
  s_shield.introAnimTimer = 0.0f;
}

void UpdateShieldSkill(float dt, Vector3 currentCasterPos) {
  if (!s_shield.active)
    return;

  s_shield.position = currentCasterPos;

  if (s_shield.introAnimTimer < 0.12f) {
    s_shield.introAnimTimer += dt;
  }

  s_shield.lifetimeTimer -= dt;
  if (s_shield.lifetimeTimer <= 0.0f) {
    s_shield.active = false;
  }
}

void DrawShieldSkill(void) {
  if (!s_shield.active)
    return;

  float time = (float)GetTime();

  // Intro: phình to từ 0 -> radius trong 0.12s đầu
  float introScale = s_shield.introAnimTimer / 0.12f;
  if (introScale > 1.0f)
    introScale = 1.0f;
  introScale = introScale * introScale * (3.0f - 2.0f * introScale); // smoothstep cho mượt hơn lerp thẳng

  // Outro: hơi co lại + rung nhẹ trong 0.2s cuối để báo shield sắp tắt,
  // ấn tượng hơn so với việc biến mất đột ngột.
  float outroT = s_shield.lifetimeTimer / 0.2f;
  float outroScale = 1.0f;
  if (outroT < 1.0f) {
    outroT = fmaxf(outroT, 0.0f);
    float wobble = sinf(time * 40.0f) * 0.03f * (1.0f - outroT);
    outroScale = (0.85f + 0.15f * outroT) + wobble;
  }

  float finalRadius = s_shield.radius * introScale * outroScale;

  rlDisableDepthMask();
  BeginBlendMode(BLEND_ALPHA);
  BeginShaderMode(s_shieldShader);

  SetShaderValue(s_shieldShader, uTimeLoc, &time, SHADER_UNIFORM_FLOAT);
  SetShaderValue(s_shieldShader, uViewPosLoc, &camera.position,
                 SHADER_UNIFORM_VEC3);
  SetShaderValueTexture(s_shieldShader, uCausticsTexLoc, s_waterCausticsTex);

  // FlowMap tự set uFlowSpeed/uFlowStrength/uFlowTiling/flowTex lên đúng
  // shader đang BeginShaderMode. Không còn rlActiveTextureSlot/rlEnableTexture
  // thủ công nữa (bug cũ gây xung đột slot với causticsTex).
  FlowMap_Apply(&s_shieldFlow, s_shieldShader, time);

  RenderShieldSphere(s_shield.position, finalRadius);

  EndShaderMode();
  EndBlendMode();
  rlEnableDepthMask();
}

void UnloadShieldSkill(void) {
  UnloadShader(s_shieldShader);
  UnloadTexture(s_waterCausticsTex);
  FlowMap_Unload(&s_shieldFlow);
}

bool IsShieldSkillActive(void) { return s_shield.active; }
