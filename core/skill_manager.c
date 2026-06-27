#include "core/skill_manager.h"
#include "skills/taiji/lightning/electric_skill.h"
#include "skills/fire/fire_ball/fire_skill.h"
#include "skills/water/water_projectile/fluid_skill.h"
#include "skills/metal/metal_projectile/metal_skill.h"
#include "raymath.h"
#include "skills/water/water_shield/shield_skill.h"
#include "skills/water/water_stream/tube_skill.h"
#include "skills/taiji/wind_storm/wind_skill.h"
#include "skills/wood/wood_roots/wood_skill.h"
#include "core/sandbox_core.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern PlayerEntity player;

#define MAX_FLOATING_TEXTS 64
#define MAX_TEMP_PROJECTILES 128
#define MAX_SKILLS 32

typedef struct {
  Vector3 position;
  char text[32];
  Color color;
  float size;
  float lifetime;
  float maxLifetime;
  bool active;
} FloatingText;

static FloatingText floatingTexts[MAX_FLOATING_TEXTS];
static SkillProjectile tempProjectiles[MAX_TEMP_PROJECTILES];

static float slowTimer = 0.0f;
static float burnTimer = 0.0f;
static float burnTickAccumulator = 0.0f;

#define MAX_ACTIVE_PORTALS 16

typedef struct {
  Vector3 position;
  Color color;
  float maxLifetime;
  float lifetime;
  float size;
  float angle;
  bool active;
  bool isGround;
} CastPortal;

static CastPortal activePortals[MAX_ACTIVE_PORTALS];
extern Camera3D camera;

typedef struct {
  char name[32];
  Color color;
  int forcePathType;
  int forceAnchorType;
  int forceQuantity;
  float forceSizeScale;
  void (*init)(int screenWidth, int screenHeight);
  void (*cast)(Vector3 startPos, Vector3 target, SkillParams params);
  void (*update)(float dt, Vector3 enemyPos, float enemyRadius);
  void (*draw)(void);
  void (*unload)(void);
} SkillRegistryEntry;

static SkillRegistryEntry skillRegistry[MAX_SKILLS];
static int registeredSkillCount = 0;
static bool builtInRegistered = false;

// --- PROTOTYPES CỦA SKILL WRAPPERS ---
static void CastWaterWrapper(Vector3 startPos, Vector3 target,
                             SkillParams params);
static void CastTubeWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params);
static void CastMetalWrapper(Vector3 startPos, Vector3 target,
                             SkillParams params);
static void CastFireWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params);
static void CastWoodWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params);
static void CastElectricWrapper(Vector3 startPos, Vector3 target,
                                SkillParams params);
static void CastWindWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params);
static void CastShieldWrapper(Vector3 startPos, Vector3 target,
                              SkillParams params);

static void UpdateFluidSkillWrapper(float dt, Vector3 enemyPos,
                                    float enemyRadius);
static void UpdateTubeSkillWrapper(float dt, Vector3 enemyPos,
                                   float enemyRadius);
static void UpdateMetalSkillWrapper(float dt, Vector3 enemyPos,
                                    float enemyRadius);
static void UpdateFireSkillWrapper(float dt, Vector3 enemyPos,
                                   float enemyRadius);
static void UpdateWoodSkillWrapper(float dt, Vector3 enemyPos,
                                   float enemyRadius);
static void UpdateElectricSkillWrapper(float dt, Vector3 enemyPos,
                                       float enemyRadius);
static void UpdateWindWrapper(float dt, Vector3 enemyPos, float enemyRadius);
static void UpdateShieldSkillWrapper(float dt, Vector3 enemyPos,
                                     float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius;
  // Khóa chặt vị trí khiên nước di chuyển bám sát theo player.position có sẵn
  UpdateShieldSkill(dt, player.position);
}

static void DrawCircleLines3D(Vector3 center, float radius, Color color) {
  const int segments = 24;
  Vector3 prevPoint = {0};
  for (int i = 0; i <= segments; i++) {
    float t = ((float)i / (float)segments) * PI * 2.0f;
    Vector3 point = {center.x + radius * cosf(t), center.y,
                     center.z + radius * sinf(t)};
    if (i > 0)
      DrawLine3D(prevPoint, point, color);
    prevPoint = point;
  }
}

static void AddCastPortal(Vector3 pos, Color portalColor, CastPathType pathType,
                          float sizeScale, Vector3 target) {
  for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
    if (!activePortals[i].active) {
      activePortals[i].position = pos;
      activePortals[i].color = ColorAlpha(portalColor, 0.8f);
      activePortals[i].maxLifetime = 0.8f;
      activePortals[i].lifetime = 0.8f;
      activePortals[i].size = 70.0f * sizeScale;
      activePortals[i].active = true;
      activePortals[i].angle = 0.0f;
      activePortals[i].isGround = (pathType == CAST_PATH_UNDERGROUND);
      break;
    }
  }
}

static void AddFloatingText(Vector3 pos, const char *text, Color color,
                            float size, float maxLife) {
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
    if (!floatingTexts[i].active) {
      floatingTexts[i].position = pos;
      floatingTexts[i].position.x += GetRandomValue(-25, 25);
      floatingTexts[i].position.y += GetRandomValue(-15, 5);
      snprintf(floatingTexts[i].text, sizeof(floatingTexts[i].text), "%s",
               text);
      floatingTexts[i].color = color;
      floatingTexts[i].size = size;
      floatingTexts[i].lifetime = maxLife;
      floatingTexts[i].maxLifetime = maxLife;
      floatingTexts[i].active = true;
      break;
    }
  }
}

static void EnsureBuiltInRegistered(void) {
  if (!builtInRegistered) {
    builtInRegistered = true;

    // Đăng ký Quả cầu nước (Index 0)
    RegisterSkill("WATER", SKYBLUE, InitFluidSkill, CastWaterWrapper,
                  UpdateFluidSkillWrapper, NULL, UnloadFluidSkill);

    // Đăng ký Ống nước Thủy Long Ngâm (Index 1)
    RegisterSkill("TUBE", BLUE, InitTubeSkill, CastTubeWrapper,
                  UpdateTubeSkillWrapper, NULL, UnloadTubeSkill);

    // Đăng ký các hệ còn lại
    RegisterSkill("METAL", GOLD, InitMetalSkill, CastMetalWrapper,
                  UpdateMetalSkillWrapper, NULL, UnloadMetalSkill);

    RegisterSkill("FIRE", ORANGE, InitFireSkill, CastFireWrapper,
                  UpdateFireSkillWrapper, NULL, UnloadFireSkill);

    RegisterSkill("WOOD", LIME, InitWoodSkill, CastWoodWrapper,
                  UpdateWoodSkillWrapper, NULL, UnloadWoodSkill);

    RegisterSkill("ELECTRIC", PURPLE, InitElectricSkill, CastElectricWrapper,
                  UpdateElectricSkillWrapper, NULL, UnloadElectricSkill);

    RegisterSkill("WIND", LIGHTGRAY, InitWindSkill, CastWindWrapper,
                  UpdateWindWrapper, NULL, UnloadWindSkill);

    // Đăng ký Hộ Thuẫn (Index 8) - Đã đồng nhất hàm Init không cần wrapper
    RegisterSkill("SHIELD", SKYBLUE, InitShieldSkill, CastShieldWrapper,
                  UpdateShieldSkillWrapper, NULL, UnloadShieldSkill);
  }
}

int RegisterSkill(const char *name, Color color,
                  void (*init)(int screenWidth, int screenHeight),
                  void (*cast)(Vector3 startPos, Vector3 target,
                               SkillParams params),
                  void (*update)(float dt, Vector3 enemyPos, float enemyRadius),
                  void (*draw)(void), void (*unload)(void)) {
  if (!builtInRegistered)
    EnsureBuiltInRegistered();
  if (registeredSkillCount >= MAX_SKILLS)
    return -1;
  for (int i = 0; i < registeredSkillCount; i++) {
    if (strcmp(skillRegistry[i].name, name) == 0)
      return i;
  }
  int index = registeredSkillCount++;
  snprintf(skillRegistry[index].name, sizeof(skillRegistry[index].name), "%s",
           name);
  skillRegistry[index].color = color;
  skillRegistry[index].forcePathType = -1;
  skillRegistry[index].forceAnchorType = -1;
  skillRegistry[index].forceQuantity = -1;
  skillRegistry[index].forceSizeScale = 0.0f;
  skillRegistry[index].init = init;
  skillRegistry[index].cast = cast;
  skillRegistry[index].update = update;
  skillRegistry[index].draw = draw;
  skillRegistry[index].unload = unload;
  return index;
}

void SetSkillOverrides(int skillIndex, int pathType, int anchorType,
                       int quantity, float sizeScale) {
  if (skillIndex < 0 || skillIndex >= registeredSkillCount)
    return;
  skillRegistry[skillIndex].forcePathType = pathType;
  skillRegistry[skillIndex].forceAnchorType = anchorType;
  skillRegistry[skillIndex].forceQuantity = quantity;
  skillRegistry[skillIndex].forceSizeScale = sizeScale;
}

int GetRegisteredSkillCount(void) {
  EnsureBuiltInRegistered();
  return registeredSkillCount;
}

const char *GetRegisteredSkillName(int index) {
  EnsureBuiltInRegistered();
  if (index < 0 || index >= registeredSkillCount)
    return "";
  return skillRegistry[index].name;
}

Color GetRegisteredSkillColor(int index) {
  EnsureBuiltInRegistered();
  if (index < 0 || index >= registeredSkillCount)
    return WHITE;
  return skillRegistry[index].color;
}

void InitSkillManager(int screenWidth, int screenHeight) {
  EnsureBuiltInRegistered();
  for (int i = 0; i < registeredSkillCount; i++) {
    if (skillRegistry[i].init)
      skillRegistry[i].init(screenWidth, screenHeight);
  }
  slowTimer = 0.0f;
  burnTimer = 0.0f;
  burnTickAccumulator = 0.0f;
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++)
    floatingTexts[i].active = false;
  for (int i = 0; i < MAX_ACTIVE_PORTALS; i++)
    activePortals[i].active = false;
}

void UpdateSkillManager(float dt, Vector3 enemyPos, float enemyRadius) {
  EnsureBuiltInRegistered();
  if (slowTimer > 0.0f)
    slowTimer -= dt;
  if (burnTimer > 0.0f) {
    burnTimer -= dt;
    burnTickAccumulator += dt;
    if (burnTickAccumulator >= 0.5f) {
      burnTickAccumulator = 0.0f;
      Vector3 tickPos = enemyPos;
      tickPos.x += (float)GetRandomValue(-20, 20);
      tickPos.y += (float)GetRandomValue(-20, 20);
      tickPos.z += (float)GetRandomValue(-20, 20);
      AddFloatingText(tickPos, "12", RED, 16.0f, 0.4f);
    }
  } else
    burnTickAccumulator = 0.0f;

  for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
    if (activePortals[i].active) {
      activePortals[i].lifetime -= dt;
      if (activePortals[i].lifetime <= 0.0f)
        activePortals[i].active = false;
    }
  }
  for (int i = 0; i < registeredSkillCount; i++) {
    if (skillRegistry[i].update)
      skillRegistry[i].update(dt, enemyPos, enemyRadius);
  }
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
    if (floatingTexts[i].active) {
      floatingTexts[i].lifetime -= dt;
      floatingTexts[i].position.y += 55.0f * dt;
      if (floatingTexts[i].lifetime <= 0.0f)
        floatingTexts[i].active = false;
    }
  }
}

void DrawSkillManagerWorld3D(void) {
  float time = (float)GetTime();
  EnsureBuiltInRegistered();

  for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
    if (activePortals[i].active) {
      float progress =
          1.0f - (activePortals[i].lifetime / activePortals[i].maxLifetime);
      float currentScale = 1.0f;
      if (progress < 0.2f)
        currentScale = progress / 0.2f;
      else if (progress > 0.8f)
        currentScale = (1.0f - progress) / 0.2f;
      float r = activePortals[i].size * currentScale;
      Color col = activePortals[i].color;

      for (float f = 1.0f; f <= 1.4f; f += 0.1f) {
        float alphaFactor = (1.4f - f) / 0.4f;
        DrawCircle3D(activePortals[i].position, r * f,
                     (Vector3){1.0f, 0.0f, 0.0f}, 90.0f,
                     ColorAlpha(col, 0.15f * alphaFactor * currentScale));
      }
      DrawCircle3D(activePortals[i].position, r * 0.75f,
                   (Vector3){1.0f, 0.0f, 0.0f}, 90.0f,
                   ColorAlpha(BLACK, 0.95f * currentScale));
      DrawCircleLines3D(activePortals[i].position, r * 0.85f,
                        ColorAlpha(col, 0.8f * currentScale));
      DrawCircleLines3D(activePortals[i].position, r * 0.65f,
                        ColorAlpha(col, 0.5f * currentScale));

      int spokes = 8;
      for (int s = 0; s < spokes; s++) {
        float spAngle = (float)s * (PI * 2.0f / spokes) + time * 1.5f;
        Vector3 pInner = {
            activePortals[i].position.x + r * 0.5f * cosf(spAngle),
            activePortals[i].position.y,
            activePortals[i].position.z + r * 0.5f * sinf(spAngle)};
        Vector3 pOuter = {
            activePortals[i].position.x + r * 1.15f * cosf(spAngle),
            activePortals[i].position.y,
            activePortals[i].position.z + r * 1.15f * sinf(spAngle)};
        DrawLine3D(pInner, pOuter, ColorAlpha(col, 0.35f * currentScale));
      }
      float rotSpeed = time * 7.0f;
      for (int p = 0; p < 8; p++) {
        float angleOffset = (float)p * (PI * 0.25f) + rotSpeed;
        Vector3 sparkPos = {
            activePortals[i].position.x + r * 0.85f * cosf(angleOffset),
            activePortals[i].position.y,
            activePortals[i].position.z + r * 0.85f * sinf(angleOffset)};
        DrawSphere(sparkPos, 3.0f * currentScale,
                   ColorAlpha(col, 0.95f * currentScale));
      }
      if (activePortals[i].isGround)
        DrawCircleLines3D(activePortals[i].position, r * 1.25f,
                          ColorAlpha(col, 0.25f * currentScale));
    }
  }

  DrawFireSkill();
  DrawFluidSkill();
  DrawTubeSkill();
  DrawWoodSkill();
  DrawElectricSkill();
  DrawMetalSkill();
  DrawWindSkill();
  DrawShieldSkill(); // Vẽ khối cầu khiên nước
}

void DrawSkillManagerOverlay(void) {
  EnsureBuiltInRegistered();
  for (int i = 0; i < registeredSkillCount; i++) {
    if (skillRegistry[i].draw)
      skillRegistry[i].draw();
  }
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
    if (floatingTexts[i].active) {
      float alpha = floatingTexts[i].lifetime / floatingTexts[i].maxLifetime;
      Color col = ColorAlpha(floatingTexts[i].color, alpha);
      Vector2 screenPos = GetWorldToScreen(floatingTexts[i].position, camera);
      DrawText(floatingTexts[i].text, (int)screenPos.x - 1,
               (int)screenPos.y + 1, (int)floatingTexts[i].size,
               ColorAlpha(BLACK, alpha * 0.7f));
      DrawText(floatingTexts[i].text, (int)screenPos.x, (int)screenPos.y,
               (int)floatingTexts[i].size, col);
    }
  }
}

void UnloadSkillManager(void) {
  EnsureBuiltInRegistered();
  for (int i = 0; i < registeredSkillCount; i++) {
    if (skillRegistry[i].unload)
      skillRegistry[i].unload();
  }
}

void CastSkill(int skillIndex, Vector3 startPos, Vector3 target,
               SkillParams params) {
  EnsureBuiltInRegistered();
  if (skillIndex < 0 || skillIndex >= registeredSkillCount)
    return;
  if (skillRegistry[skillIndex].forcePathType != -1)
    params.pathType = (CastPathType)skillRegistry[skillIndex].forcePathType;
  if (skillRegistry[skillIndex].forceAnchorType != -1)
    params.anchorType =
        (CastAnchorType)skillRegistry[skillIndex].forceAnchorType;
  if (skillRegistry[skillIndex].forceQuantity != -1)
    params.quantity = skillRegistry[skillIndex].forceQuantity;
  if (skillRegistry[skillIndex].forceSizeScale > 0.0f)
    params.sizeScale = skillRegistry[skillIndex].forceSizeScale;

  Vector3 adjustedStartPos = startPos;
  Vector3 adjustedTarget = target;
  Color skillColor = skillRegistry[skillIndex].color;

  switch (params.pathType) {
  case CAST_PATH_PROJECTILE: {
    Vector3 shoulderPos = (Vector3){startPos.x, startPos.y + 25.0f, startPos.z};
    Vector3 aimDir = Vector3Normalize(Vector3Subtract(target, shoulderPos));
    adjustedStartPos = Vector3Add(shoulderPos, Vector3Scale(aimDir, 22.0f));
    adjustedTarget = target;
    break;
  }
  case CAST_PATH_UNDERGROUND: {
    if (params.anchorType == CAST_ANCHOR_CASTER) {
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
      adjustedStartPos = Vector3Add(startPos, Vector3Scale(dir, 35.0f));
      adjustedStartPos.y = 0.02f;
      adjustedTarget = target;
      if (params.showPortal)
        AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_UNDERGROUND,
                      params.sizeScale, target);
    } else {
      adjustedStartPos = target;
      adjustedStartPos.y = 0.02f;
      adjustedTarget = (Vector3){target.x, target.y + 100.0f, target.z};
      if (params.showPortal)
        AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_UNDERGROUND,
                      params.sizeScale, adjustedTarget);
    }
    break;
  }
  case CAST_PATH_SKY: {
    if (params.anchorType == CAST_ANCHOR_CASTER) {
      adjustedStartPos = (Vector3){startPos.x, startPos.y + 348.0f, startPos.z};
      adjustedTarget = target;
    } else {
      adjustedStartPos = (Vector3){target.x, target.y + 348.0f, target.z};
      adjustedTarget = target;
    }
    if (params.showPortal)
      AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_SKY,
                    params.sizeScale, target);
    break;
  }
  default:
    break;
  }
  if (skillRegistry[skillIndex].cast)
    skillRegistry[skillIndex].cast(adjustedStartPos, adjustedTarget, params);
}

bool IsEnemySlowed(void) { return slowTimer > 0.0f; }
bool IsEnemyBurning(void) { return burnTimer > 0.0f; }
bool IsAnySkillCoiling(void) { return IsWoodSkillCoiling(); }
bool IsAnySkillShocking(void) { return IsElectricSkillShocking(); }

// --- IMPLEMENTATION CỦA CÁC SKILL WRAPPERS ---

static void CastWaterWrapper(Vector3 startPos, Vector3 target,
                             SkillParams params) {
  int qty = params.quantity > 0 ? params.quantity : 1;
  if (qty > 1) {
    for (int i = 0; i < qty; i++)
      CastFluidSkill(startPos, target,
                     ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f),
                     params.sizeScale);
  } else
    CastFluidSkill(startPos, target, -0.4f, params.sizeScale);
}

static void CastTubeWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params) {
  int qty = params.quantity > 0 ? params.quantity : 1;
  if (qty > 1) {
    for (int i = 0; i < qty; i++)
      CastTubeSkill(startPos, target,
                    ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.6f),
                    params.sizeScale);
  } else
    CastTubeSkill(startPos, target, 0.0f, params.sizeScale);
}

static void CastMetalWrapper(Vector3 startPos, Vector3 target,
                             SkillParams params) {
  CastMetalSkill(startPos, target, params);
}

static void CastFireWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params) {
  int qty = params.quantity > 0 ? params.quantity : 1;
  if (qty > 1) {
    for (int i = 0; i < qty; i++)
      CastFireSkill(startPos, target,
                    ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f),
                    params.sizeScale);
  } else
    CastFireSkill(startPos, target, -0.4f, params.sizeScale);
}

static void CastWoodWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params) {
  CastWoodSkill(startPos, target, params);
}

static void CastElectricWrapper(Vector3 startPos, Vector3 target,
                                SkillParams params) {
  int qty = params.quantity > 0 ? params.quantity : 1;
  if (qty > 1) {
    for (int i = 0; i < qty; i++) {
      float angle = ((float)i / (float)(qty - 1) - 0.5f) * 0.35f;
      Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
      Vector3 offsetTarget = Vector3Add(
          target, Vector3Scale((Vector3){-dir.z, 0.0f, dir.x}, angle * 180.0f));
      CastElectricSkill(startPos, offsetTarget, params.sizeScale);
    }
  } else
    CastElectricSkill(startPos, target, params.sizeScale);
}

static void CastWindWrapper(Vector3 startPos, Vector3 target,
                            SkillParams params) {
  CastWindSkill(startPos, target, params);
}

static void CastShieldWrapper(Vector3 startPos, Vector3 target,
                              SkillParams params) {
  (void)target; // Chiêu tự buff lên bản thân nên bỏ qua hướng target
  float baseRadius = 30.0f;
  float duration = 5.0f;
  CastShieldSkill(startPos,
                  baseRadius *
                      (params.sizeScale > 0.0f ? params.sizeScale : 1.0f),
                  duration);
}

static void UpdateFluidSkillWrapper(float dt, Vector3 enemyPos,
                                    float enemyRadius) {
  UpdateFluidSkill(dt);
  int numWater =
      GetFluidSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
  for (int i = numWater - 1; i >= 0; i--) {
    if (tempProjectiles[i].active &&
        Vector3Distance(tempProjectiles[i].position, enemyPos) <
            (tempProjectiles[i].radius + enemyRadius)) {
      DeactivateFluidProjectile(i);
      slowTimer = 3.0f;
      AddFloatingText(tempProjectiles[i].position, "15", BLUE, 22.0f, 0.7f);
      AddFloatingText(tempProjectiles[i].position, "SLOW!", SKYBLUE, 16.0f,
                      0.8f);
    }
  }
}

static void UpdateTubeSkillWrapper(float dt, Vector3 enemyPos,
                                   float enemyRadius) {
  UpdateTubeSkill(dt);
  int numTube = GetTubeSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
  for (int i = numTube - 1; i >= 0; i--) {
    if (tempProjectiles[i].active &&
        Vector3Distance(tempProjectiles[i].position, enemyPos) <
            (tempProjectiles[i].radius + enemyRadius)) {
      DeactivateTubeProjectile(i);
      slowTimer = 2.0f;
      AddFloatingText(tempProjectiles[i].position, "35", BLUE, 24.0f, 0.7f);
      AddFloatingText(tempProjectiles[i].position, "PIERCE!", SKYBLUE, 18.0f,
                      0.8f);
    }
  }
}

static void UpdateMetalSkillWrapper(float dt, Vector3 enemyPos,
                                    float enemyRadius) {
  UpdateMetalSkill(dt);
  int numMetal =
      GetMetalSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
  for (int i = numMetal - 1; i >= 0; i--) {
    if (tempProjectiles[i].active &&
        Vector3Distance(tempProjectiles[i].position, enemyPos) <
            (tempProjectiles[i].radius + enemyRadius)) {
      DeactivateMetalProjectile(i);
      if (GetRandomValue(1, 100) <= 30)
        AddFloatingText(tempProjectiles[i].position, "CRIT! 95", ORANGE, 28.0f,
                        1.0f);
      else
        AddFloatingText(tempProjectiles[i].position, "45", GOLD, 22.0f, 0.7f);
    }
  }
}

static void UpdateFireSkillWrapper(float dt, Vector3 enemyPos,
                                   float enemyRadius) {
  UpdateFireSkill(dt);
  int numFire = GetFireSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
  for (int i = numFire - 1; i >= 0; i--) {
    if (tempProjectiles[i].active &&
        Vector3Distance(tempProjectiles[i].position, enemyPos) <
            (tempProjectiles[i].radius + enemyRadius)) {
      DeactivateFireProjectile(i);
      burnTimer = 3.0f;
      burnTickAccumulator = 0.0f;
      AddFloatingText(tempProjectiles[i].position, "80", RED, 25.0f, 0.8f);
      AddFloatingText(tempProjectiles[i].position, "BURN!", ORANGE, 18.0f,
                      0.9f);
    }
  }
}

static void UpdateWoodSkillWrapper(float dt, Vector3 enemyPos,
                                   float enemyRadius) {
  UpdateWoodSkill(dt);
  int numWood = GetWoodSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
  for (int i = numWood - 1; i >= 0; i--) {
    if (tempProjectiles[i].active &&
        Vector3Distance(tempProjectiles[i].position, enemyPos) <
            (numWood + enemyRadius)) {
      DeactivateWoodProjectile(i);
      AddFloatingText(tempProjectiles[i].position, "30", LIME, 22.0f, 0.7f);
      AddFloatingText(tempProjectiles[i].position, "ROOTED!", GREEN, 18.0f,
                      0.8f);
    }
  }
}

static void UpdateElectricSkillWrapper(float dt, Vector3 enemyPos,
                                       float enemyRadius) {
  UpdateElectricSkill(dt);
  int numElectric =
      GetElectricSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
  for (int i = numElectric - 1; i >= 0; i--) {
    if (tempProjectiles[i].active &&
        Vector3Distance(tempProjectiles[i].position, enemyPos) <
            (tempProjectiles[i].radius + enemyRadius)) {
      DeactivateElectricProjectile(i);
      AddFloatingText(tempProjectiles[i].position, "120", MAGENTA, 26.0f, 0.9f);
      AddFloatingText(tempProjectiles[i].position, "SHOCK!", PURPLE, 19.0f,
                      0.9f);
    }
  }
}

static void UpdateWindWrapper(float dt, Vector3 enemyPos, float enemyRadius) {
  (void)enemyPos;
  (void)enemyRadius;
  UpdateWindSkill(dt);
}


ProjectedPoint ProjectPointCached(Vector3 worldPos, Camera3D cam) {
  ProjectedPoint pt;
  Vector3 camDir = Vector3Subtract(cam.target, cam.position);
  pt.behindCamera =
      (Vector3DotProduct(camDir, Vector3Subtract(worldPos, cam.position)) <=
       0.0f);
  pt.screenPos = GetWorldToScreen(worldPos, cam);
  pt.depthFactor = Clamp(
      (((float)GetScreenHeight() * 0.5f) / tanf(cam.fovy * 0.5f * DEG2RAD)) /
          (Vector3Distance(cam.position, worldPos) + 0.1f),
      0.2f, 3.0f);
  return pt;
}

typedef struct {
  Vector3 center;
  float radius;
  float height;
} StaticOccluder;
static StaticOccluder staticOccluders[16];
static int staticOccluderCount = 0;

void RegisterStaticOccluder(Vector3 center, float radius, float height) {
  if (staticOccluderCount < 16)
    staticOccluders[staticOccluderCount++] =
        (StaticOccluder){center, radius, height};
}

void ClearStaticOccluders(void) { staticOccluderCount = 0; }

float GetLineOfSightVisibility(Vector3 viewPoint, Vector3 targetPoint) {
  for (int idx = 0; idx < staticOccluderCount; idx++) {
    StaticOccluder oc = staticOccluders[idx];
    Vector2 A = {viewPoint.x, viewPoint.z}, B = {targetPoint.x, targetPoint.z},
            C = {oc.center.x, oc.center.z};
    Vector2 D = {B.x - A.x, B.y - A.y}, V = {A.x - C.x, A.y - C.y};
    float a = D.x * D.x + D.y * D.y;
    if (a < 1e-6f) {
      if (V.x * V.x + V.y * V.y - oc.radius * oc.radius <= 0.0f &&
          viewPoint.y >= 0.0f && viewPoint.y <= oc.height)
        return 0.0f;
      continue;
    }
    float discriminant =
        (2.0f * (V.x * D.x + V.y * D.y)) * (2.0f * (V.x * D.x + V.y * D.y)) -
        4.0f * a * (V.x * V.x + V.y * V.y - oc.radius * oc.radius);
    if (discriminant >= 0.0f) {
      float sqrtDisc = sqrtf(discriminant);
      float u_start = fmaxf(
          0.0f, (-(2.0f * (V.x * D.x + V.y * D.y)) - sqrtDisc) / (2.0f * a));
      float u_end = fminf(1.0f, (-(2.0f * (V.x * D.x + V.y * D.y)) + sqrtDisc) /
                                    (2.0f * a));
      if (u_start <= u_end) {
        float y_min =
            fminf(viewPoint.y + u_start * (targetPoint.y - viewPoint.y),
                  viewPoint.y + u_end * (targetPoint.y - viewPoint.y));
        float y_max =
            fmaxf(viewPoint.y + u_start * (targetPoint.y - viewPoint.y),
                  viewPoint.y + u_end * (targetPoint.y - viewPoint.y));
        if (fmaxf(y_min, 0.0f) <= fminf(y_max, oc.height))
          return 0.0f;
      }
    }
  }
  return 1.0f;
}