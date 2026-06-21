#include "skill_manager.h"
#include "fluid_skill.h"
#include "metal_skill.h"
#include "fire_skill.h"
#include "wood_skill.h"
#include "electric_skill.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_FLOATING_TEXTS 64
#define MAX_TEMP_PROJECTILES 128
#define MAX_SKILLS 32

typedef struct {
    Vector2 position;
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
    Vector2 position;
    Color color;
    float maxLifetime;
    float lifetime;
    float size;
    float angle;
    float tiltRatio;
    bool active;
    bool isGround;
} CastPortal;

static CastPortal activePortals[MAX_ACTIVE_PORTALS];

// Dynamic Skill Registry Entry
typedef struct {
    char name[32];
    Color color;
    int forcePathType;
    int forceAnchorType;
    int forceQuantity;
    float forceSizeScale;
    void (*init)(int screenWidth, int screenHeight);
    void (*cast)(Vector2 startPos, Vector2 target, SkillParams params);
    void (*update)(float dt, Vector2 enemyPos, float enemyRadius);
    void (*draw)(void);
    void (*unload)(void);
} SkillRegistryEntry;

static SkillRegistryEntry skillRegistry[MAX_SKILLS];
static int registeredSkillCount = 0;
static bool builtInRegistered = false;

// Forward declaration of wrappers
static void CastWaterWrapper(Vector2 startPos, Vector2 target, SkillParams params);
static void CastMetalWrapper(Vector2 startPos, Vector2 target, SkillParams params);
static void CastFireWrapper(Vector2 startPos, Vector2 target, SkillParams params);
static void CastWoodWrapper(Vector2 startPos, Vector2 target, SkillParams params);
static void CastElectricWrapper(Vector2 startPos, Vector2 target, SkillParams params);

static void UpdateFluidSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius);
static void UpdateMetalSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius);
static void UpdateFireSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius);
static void UpdateWoodSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius);
static void UpdateElectricSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius);

static void DrawTiltedEllipseFilled(Vector2 center, float rx, float ry, float angleRad, Color color) {
    const int segments = 24;
    Vector2 prevPoint = { 0 };
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);

    for (int i = 0; i <= segments; i++) {
        float t = ((float)i / (float)segments) * PI * 2.0f;
        float x = rx * cosf(t);
        float y = ry * sinf(t);
        Vector2 point = {
            center.x + x * cosA - y * sinA,
            center.y + x * sinA + y * cosA
        };
        if (i > 0) {
            DrawTriangle(center, prevPoint, point, color);
        }
        prevPoint = point;
    }
}

static void DrawTiltedEllipseLines(Vector2 center, float rx, float ry, float angleRad, Color color, float thickness) {
    const int segments = 24;
    Vector2 prevPoint = { 0 };
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);

    for (int i = 0; i <= segments; i++) {
        float t = ((float)i / (float)segments) * PI * 2.0f;
        float x = rx * cosf(t);
        float y = ry * sinf(t);
        Vector2 point = {
            center.x + x * cosA - y * sinA,
            center.y + x * sinA + y * cosA
        };
        if (i > 0) {
            DrawLineEx(prevPoint, point, thickness, color);
        }
        prevPoint = point;
    }
}

static void AddCastPortal(Vector2 pos, Color portalColor, CastPathType pathType, float sizeScale, Vector2 target) {
    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        if (!activePortals[i].active) {
            activePortals[i].position = pos;
            activePortals[i].color = ColorAlpha(portalColor, 0.8f);
            activePortals[i].maxLifetime = 0.8f;
            activePortals[i].lifetime = 0.8f;
            activePortals[i].size = 70.0f * sizeScale;
            activePortals[i].active = true;

            if (pathType == CAST_PATH_UNDERGROUND) {
                activePortals[i].isGround = true;
                activePortals[i].angle = 0.0f;
                activePortals[i].tiltRatio = 0.4f;
            } else {
                activePortals[i].isGround = false;
                Vector2 dir = Vector2Normalize(Vector2Subtract(target, pos));
                activePortals[i].angle = atan2f(dir.y, dir.x) + PI/2.0f;
                activePortals[i].tiltRatio = 0.35f;
            }
            break;
        }
    }
}

static void AddFloatingText(Vector2 pos, const char* text, Color color, float size, float maxLife) {
    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (!floatingTexts[i].active) {
            floatingTexts[i].position = pos;
            floatingTexts[i].position.x += GetRandomValue(-25, 25);
            floatingTexts[i].position.y += GetRandomValue(-15, 5);
            snprintf(floatingTexts[i].text, sizeof(floatingTexts[i].text), "%s", text);
            floatingTexts[i].color = color;
            floatingTexts[i].size = size;
            floatingTexts[i].lifetime = maxLife;
            floatingTexts[i].maxLifetime = maxLife;
            floatingTexts[i].active = true;
            break;
        }
    }
}

// Built-in skills registration logic
static void EnsureBuiltInRegistered(void) {
    if (!builtInRegistered) {
        builtInRegistered = true;
        RegisterSkill("WATER", SKYBLUE, InitFluidSkill, CastWaterWrapper, UpdateFluidSkillWrapper, DrawFluidSkill, UnloadFluidSkill);
        RegisterSkill("METAL", GOLD, InitMetalSkill, CastMetalWrapper, UpdateMetalSkillWrapper, DrawMetalSkill, UnloadMetalSkill);
        RegisterSkill("FIRE", ORANGE, InitFireSkill, CastFireWrapper, UpdateFireSkillWrapper, DrawFireSkill, UnloadFireSkill);
        RegisterSkill("WOOD", LIME, InitWoodSkill, CastWoodWrapper, UpdateWoodSkillWrapper, DrawWoodSkill, UnloadWoodSkill);
        RegisterSkill("ELECTRIC", PURPLE, InitElectricSkill, CastElectricWrapper, UpdateElectricSkillWrapper, DrawElectricSkill, UnloadElectricSkill);
    }
}

int RegisterSkill(const char* name, Color color,
                  void (*init)(int screenWidth, int screenHeight),
                  void (*cast)(Vector2 startPos, Vector2 target, SkillParams params),
                  void (*update)(float dt, Vector2 enemyPos, float enemyRadius),
                  void (*draw)(void),
                  void (*unload)(void)) {
    
    // Ensure built-in skills occupy indices 0 to 4
    if (!builtInRegistered) {
        EnsureBuiltInRegistered();
    }

    if (registeredSkillCount >= MAX_SKILLS) return -1;

    for (int i = 0; i < registeredSkillCount; i++) {
        if (strcmp(skillRegistry[i].name, name) == 0) {
            return i;
        }
    }

    int index = registeredSkillCount++;
    snprintf(skillRegistry[index].name, sizeof(skillRegistry[index].name), "%s", name);
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

void SetSkillOverrides(int skillIndex, int pathType, int anchorType, int quantity, float sizeScale) {
    if (skillIndex < 0 || skillIndex >= registeredSkillCount) return;
    skillRegistry[skillIndex].forcePathType = pathType;
    skillRegistry[skillIndex].forceAnchorType = anchorType;
    skillRegistry[skillIndex].forceQuantity = quantity;
    skillRegistry[skillIndex].forceSizeScale = sizeScale;
}

int GetRegisteredSkillCount(void) {
    EnsureBuiltInRegistered();
    return registeredSkillCount;
}

const char* GetRegisteredSkillName(int index) {
    EnsureBuiltInRegistered();
    if (index < 0 || index >= registeredSkillCount) return "";
    return skillRegistry[index].name;
}

Color GetRegisteredSkillColor(int index) {
    EnsureBuiltInRegistered();
    if (index < 0 || index >= registeredSkillCount) return WHITE;
    return skillRegistry[index].color;
}

void InitSkillManager(int screenWidth, int screenHeight) {
    EnsureBuiltInRegistered();

    for (int i = 0; i < registeredSkillCount; i++) {
        if (skillRegistry[i].init) {
            skillRegistry[i].init(screenWidth, screenHeight);
        }
    }

    slowTimer = 0.0f;
    burnTimer = 0.0f;
    burnTickAccumulator = 0.0f;

    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        floatingTexts[i].active = false;
    }

    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        activePortals[i].active = false;
    }
}

void UpdateSkillManager(float dt, Vector2 enemyPos, float enemyRadius) {
    EnsureBuiltInRegistered();

    if (slowTimer > 0.0f) {
        slowTimer -= dt;
    }
    if (burnTimer > 0.0f) {
        burnTimer -= dt;
        burnTickAccumulator += dt;
        if (burnTickAccumulator >= 0.5f) {
            burnTickAccumulator = 0.0f;
            Vector2 tickPos = enemyPos;
            tickPos.x += (float)GetRandomValue(-20, 20);
            tickPos.y += (float)GetRandomValue(-20, 20);
            AddFloatingText(tickPos, "12", RED, 16.0f, 0.4f);
        }
    } else {
        burnTickAccumulator = 0.0f;
    }

    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        if (activePortals[i].active) {
            activePortals[i].lifetime -= dt;
            if (activePortals[i].lifetime <= 0.0f) {
                activePortals[i].active = false;
            }
        }
    }

    // Update all registered skills
    for (int i = 0; i < registeredSkillCount; i++) {
        if (skillRegistry[i].update) {
            skillRegistry[i].update(dt, enemyPos, enemyRadius);
        }
    }

    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (floatingTexts[i].active) {
            floatingTexts[i].lifetime -= dt;
            floatingTexts[i].position.y -= 55.0f * dt;
            if (floatingTexts[i].lifetime <= 0.0f) {
                floatingTexts[i].active = false;
            }
        }
    }
}

void DrawSkillManager(void) {
    float time = (float)GetTime();
    EnsureBuiltInRegistered();

    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        if (activePortals[i].active) {
            float progress = 1.0f - (activePortals[i].lifetime / activePortals[i].maxLifetime);
            float currentScale = 1.0f;
            if (progress < 0.2f) {
                currentScale = progress / 0.2f;
            } else if (progress > 0.8f) {
                currentScale = (1.0f - progress) / 0.2f;
            }
            float r = activePortals[i].size * currentScale;
            float tilt = activePortals[i].tiltRatio;
            float angle = activePortals[i].angle;
            Color col = activePortals[i].color;
            float cosA = cosf(angle);
            float sinA = sinf(angle);

            for (float f = 1.0f; f <= 1.4f; f += 0.1f) {
                float alphaFactor = (1.4f - f) / 0.4f;
                DrawTiltedEllipseFilled(activePortals[i].position, r * f, r * f * tilt, angle, ColorAlpha(col, 0.15f * alphaFactor * currentScale));
            }

            DrawTiltedEllipseFilled(activePortals[i].position, r * 0.75f, r * 0.75f * tilt, angle, ColorAlpha(BLACK, 0.95f * currentScale));
            DrawTiltedEllipseLines(activePortals[i].position, r * 0.85f, r * 0.85f * tilt, angle, ColorAlpha(col, 0.8f * currentScale), 2.5f);
            DrawTiltedEllipseLines(activePortals[i].position, r * 0.65f, r * 0.65f * tilt, angle, ColorAlpha(col, 0.5f * currentScale), 1.5f);
            
            int spokes = 8;
            for (int s = 0; s < spokes; s++) {
                float spAngle = (float)s * (PI * 2.0f / spokes) + time * 1.5f;
                float cosSp = cosf(spAngle);
                float sinSp = sinf(spAngle);
                Vector2 pInner = {
                    activePortals[i].position.x + (r * 0.5f * cosSp) * cosA - (r * 0.5f * tilt * sinSp) * sinA,
                    activePortals[i].position.y + (r * 0.5f * cosSp) * sinA + (r * 0.5f * tilt * sinSp) * cosA
                };
                Vector2 pOuter = {
                    activePortals[i].position.x + (r * 1.15f * cosSp) * cosA - (r * 1.15f * tilt * sinSp) * sinA,
                    activePortals[i].position.y + (r * 1.15f * cosSp) * sinA + (r * 1.15f * tilt * sinSp) * cosA
                };
                DrawLineEx(pInner, pOuter, 1.5f, ColorAlpha(col, 0.35f * currentScale));
            }
            
            float rotSpeed = time * 7.0f;
            for (int p = 0; p < 8; p++) {
                float angleOffset = (float)p * (PI * 0.25f) + rotSpeed;
                float px = r * 0.85f * cosf(angleOffset);
                float py = r * 0.85f * tilt * sinf(angleOffset);
                Vector2 sparkPos = {
                    activePortals[i].position.x + px * cosA - py * sinA,
                    activePortals[i].position.y + px * sinA + py * cosA
                };
                DrawCircleV(sparkPos, 3.0f * currentScale, ColorAlpha(col, 0.95f * currentScale));
            }

            if (activePortals[i].isGround) {
                DrawTiltedEllipseLines(activePortals[i].position, r * 1.25f, r * 1.25f * tilt, angle, ColorAlpha(col, 0.25f * currentScale), 1.0f);
            }
        }
    }

    // Draw all registered skills
    for (int i = 0; i < registeredSkillCount; i++) {
        if (skillRegistry[i].draw) {
            skillRegistry[i].draw();
        }
    }

    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (floatingTexts[i].active) {
            float alpha = floatingTexts[i].lifetime / floatingTexts[i].maxLifetime;
            Color col = ColorAlpha(floatingTexts[i].color, alpha);
            
            DrawText(floatingTexts[i].text, 
                     (int)floatingTexts[i].position.x - 1, 
                     (int)floatingTexts[i].position.y + 1, 
                     (int)floatingTexts[i].size, 
                     ColorAlpha(BLACK, alpha * 0.7f));
            
            DrawText(floatingTexts[i].text, 
                     (int)floatingTexts[i].position.x, 
                     (int)floatingTexts[i].position.y, 
                     (int)floatingTexts[i].size, 
                     col);
        }
    }
}

void UnloadSkillManager(void) {
    EnsureBuiltInRegistered();
    for (int i = 0; i < registeredSkillCount; i++) {
        if (skillRegistry[i].unload) {
            skillRegistry[i].unload();
        }
    }
}

void CastSkill(int skillIndex, Vector2 startPos, Vector2 target, SkillParams params) {
    EnsureBuiltInRegistered();
    if (skillIndex < 0 || skillIndex >= registeredSkillCount) return;

    // Apply overrides if specified
    if (skillRegistry[skillIndex].forcePathType != -1) {
        params.pathType = (CastPathType)skillRegistry[skillIndex].forcePathType;
    }
    if (skillRegistry[skillIndex].forceAnchorType != -1) {
        params.anchorType = (CastAnchorType)skillRegistry[skillIndex].forceAnchorType;
    }
    if (skillRegistry[skillIndex].forceQuantity != -1) {
        params.quantity = skillRegistry[skillIndex].forceQuantity;
    }
    if (skillRegistry[skillIndex].forceSizeScale > 0.0f) {
        params.sizeScale = skillRegistry[skillIndex].forceSizeScale;
    }

    Vector2 adjustedStartPos = startPos;
    Vector2 adjustedTarget = target;
    Color skillColor = skillRegistry[skillIndex].color;

    switch (params.pathType) {
        case CAST_PATH_PROJECTILE: {
            Vector2 shoulderPos = (Vector2){ startPos.x, startPos.y - params.casterZ - 25.0f };
            Vector2 aimDir = Vector2Normalize(Vector2Subtract(target, shoulderPos));
            adjustedStartPos = Vector2Add(shoulderPos, Vector2Scale(aimDir, 22.0f));
            adjustedTarget = target;
            break;
        }
        case CAST_PATH_UNDERGROUND: {
            if (params.anchorType == CAST_ANCHOR_CASTER) {
                Vector2 dir = Vector2Normalize(Vector2Subtract(target, startPos));
                adjustedStartPos = Vector2Add(startPos, Vector2Scale(dir, 35.0f));
                adjustedTarget = target;
                
                if (params.showPortal) {
                    AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_UNDERGROUND, params.sizeScale, target);
                }
            } else {
                adjustedStartPos = target;
                adjustedTarget = (Vector2){ target.x, target.y - 100.0f };
                
                if (params.showPortal) {
                    AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_UNDERGROUND, params.sizeScale, adjustedTarget);
                }
            }
            break;
        }
        case CAST_PATH_SKY: {
            if (params.anchorType == CAST_ANCHOR_CASTER) {
                adjustedStartPos = (Vector2){ startPos.x, startPos.y - params.casterZ - 348.0f };
                adjustedTarget = target;
            } else {
                adjustedStartPos = (Vector2){ target.x, target.y - 348.0f };
                adjustedTarget = target;
            }
            if (params.showPortal) {
                AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_SKY, params.sizeScale, target);
            }
            break;
        }
        default:
            break;
    }

    if (skillRegistry[skillIndex].cast) {
        skillRegistry[skillIndex].cast(adjustedStartPos, adjustedTarget, params);
    }
}

bool IsEnemySlowed(void) {
    return slowTimer > 0.0f;
}

bool IsEnemyBurning(void) {
    return burnTimer > 0.0f;
}

bool IsAnySkillCoiling(void) {
    return IsWoodSkillCoiling();
}

bool IsAnySkillShocking(void) {
    return IsElectricSkillShocking();
}

// Built-in skill wrappers implementation

static void CastWaterWrapper(Vector2 startPos, Vector2 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 1;
    if (qty > 1) {
        for (int i = 0; i < qty; i++) {
            float twist = ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f);
            CastFluidSkill(startPos, target, twist, params.sizeScale);
        }
    } else {
        CastFluidSkill(startPos, target, -0.4f, params.sizeScale);
    }
}

static void CastMetalWrapper(Vector2 startPos, Vector2 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 3;
    CastMetalSkill(startPos, target, qty, params.sizeScale);
}

static void CastFireWrapper(Vector2 startPos, Vector2 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 1;
    if (qty > 1) {
        for (int i = 0; i < qty; i++) {
            float twist = ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f);
            CastFireSkill(startPos, target, twist, params.sizeScale);
        }
    } else {
        CastFireSkill(startPos, target, -0.4f, params.sizeScale);
    }
}

static void CastWoodWrapper(Vector2 startPos, Vector2 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 3;
    CastWoodSkill(startPos, target, qty, params.sizeScale);
}

static void CastElectricWrapper(Vector2 startPos, Vector2 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 1;
    if (qty > 1) {
        for (int i = 0; i < qty; i++) {
            float angle = ((float)i / (float)(qty - 1) - 0.5f) * 0.35f;
            Vector2 dir = Vector2Normalize(Vector2Subtract(target, startPos));
            Vector2 perp = { -dir.y, dir.x };
            Vector2 offsetTarget = Vector2Add(target, Vector2Scale(perp, angle * 180.0f));
            CastElectricSkill(startPos, offsetTarget, params.sizeScale);
        }
    } else {
        CastElectricSkill(startPos, target, params.sizeScale);
    }
}

static void UpdateFluidSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius) {
    UpdateFluidSkill(dt);
    int waterHits[MAX_TEMP_PROJECTILES];
    int waterHitCount = 0;
    Vector2 waterHitPos[MAX_TEMP_PROJECTILES];
    int numWater = GetFluidSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numWater; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector2Distance(tempProjectiles[i].position, enemyPos);
            if (dist < (tempProjectiles[i].radius + enemyRadius)) {
                waterHits[waterHitCount] = i;
                waterHitPos[waterHitCount] = tempProjectiles[i].position;
                waterHitCount++;
            }
        }
    }
    for (int i = waterHitCount - 1; i >= 0; i--) {
        DeactivateFluidProjectile(waterHits[i]);
        slowTimer = 3.0f;
        AddFloatingText(waterHitPos[i], "15", BLUE, 22.0f, 0.7f);
        AddFloatingText(waterHitPos[i], "SLOW!", SKYBLUE, 16.0f, 0.8f);
    }
}

static void UpdateMetalSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius) {
    UpdateMetalSkill(dt);
    int metalHits[MAX_TEMP_PROJECTILES];
    int metalHitCount = 0;
    Vector2 metalHitPos[MAX_TEMP_PROJECTILES];
    int numMetal = GetMetalSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numMetal; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector2Distance(tempProjectiles[i].position, enemyPos);
            if (dist < (tempProjectiles[i].radius + enemyRadius)) {
                metalHits[metalHitCount] = i;
                metalHitPos[metalHitCount] = tempProjectiles[i].position;
                metalHitCount++;
            }
        }
    }
    for (int i = metalHitCount - 1; i >= 0; i--) {
        DeactivateMetalProjectile(metalHits[i]);
        bool isCrit = GetRandomValue(1, 100) <= 30;
        if (isCrit) {
            AddFloatingText(metalHitPos[i], "CRIT! 95", ORANGE, 28.0f, 1.0f);
        } else {
            AddFloatingText(metalHitPos[i], "45", GOLD, 22.0f, 0.7f);
        }
    }
}

static void UpdateFireSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius) {
    UpdateFireSkill(dt);
    int fireHits[MAX_TEMP_PROJECTILES];
    int fireHitCount = 0;
    Vector2 fireHitPos[MAX_TEMP_PROJECTILES];
    int numFire = GetFireSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numFire; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector2Distance(tempProjectiles[i].position, enemyPos);
            if (dist < (tempProjectiles[i].radius + enemyRadius)) {
                fireHits[fireHitCount] = i;
                fireHitPos[fireHitCount] = tempProjectiles[i].position;
                fireHitCount++;
            }
        }
    }
    for (int i = fireHitCount - 1; i >= 0; i--) {
        DeactivateFireProjectile(fireHits[i]);
        burnTimer = 3.0f;
        burnTickAccumulator = 0.0f;
        AddFloatingText(fireHitPos[i], "80", RED, 25.0f, 0.8f);
        AddFloatingText(fireHitPos[i], "BURN!", ORANGE, 18.0f, 0.9f);
    }
}

static void UpdateWoodSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius) {
    UpdateWoodSkill(dt);
    int woodHits[MAX_TEMP_PROJECTILES];
    int woodHitCount = 0;
    Vector2 woodHitPos[MAX_TEMP_PROJECTILES];
    int numWood = GetWoodSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numWood; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector2Distance(tempProjectiles[i].position, enemyPos);
            if (dist < (tempProjectiles[i].radius + enemyRadius)) {
                woodHits[woodHitCount] = i;
                woodHitPos[woodHitCount] = tempProjectiles[i].position;
                woodHitCount++;
            }
        }
    }
    for (int i = woodHitCount - 1; i >= 0; i--) {
        DeactivateWoodProjectile(woodHits[i]);
        AddFloatingText(woodHitPos[i], "30", LIME, 22.0f, 0.7f);
        AddFloatingText(woodHitPos[i], "ROOTED!", GREEN, 18.0f, 0.8f);
    }
}

static void UpdateElectricSkillWrapper(float dt, Vector2 enemyPos, float enemyRadius) {
    UpdateElectricSkill(dt);
    int electricHits[MAX_TEMP_PROJECTILES];
    int electricHitCount = 0;
    Vector2 electricHitPos[MAX_TEMP_PROJECTILES];
    int numElectric = GetElectricSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numElectric; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector2Distance(tempProjectiles[i].position, enemyPos);
            if (dist < (tempProjectiles[i].radius + enemyRadius)) {
                electricHits[electricHitCount] = i;
                electricHitPos[electricHitCount] = tempProjectiles[i].position;
                electricHitCount++;
            }
        }
    }
    for (int i = electricHitCount - 1; i >= 0; i--) {
        DeactivateElectricProjectile(electricHits[i]);
        AddFloatingText(electricHitPos[i], "120", MAGENTA, 26.0f, 0.9f);
        AddFloatingText(electricHitPos[i], "SHOCK!", PURPLE, 19.0f, 0.9f);
    }
}
