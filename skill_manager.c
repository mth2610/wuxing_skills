#include "skill_manager.h"
#include "fluid_skill.h"
#include "metal_skill.h"
#include "fire_skill.h"
#include "wood_skill.h"
#include "electric_skill.h"
#include "wind_skill.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_FLOATING_TEXTS 64
#define MAX_TEMP_PROJECTILES 128
#define MAX_SKILLS 32

typedef struct {
    Vector3 position; // 3D position
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

// External camera reference to project 3D coordinates to 2D screen space
extern Camera3D camera;

// Dynamic Skill Registry Entry
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

// Forward declaration of wrappers
static void CastWaterWrapper(Vector3 startPos, Vector3 target, SkillParams params);
static void CastMetalWrapper(Vector3 startPos, Vector3 target, SkillParams params);
static void CastFireWrapper(Vector3 startPos, Vector3 target, SkillParams params);
static void CastWoodWrapper(Vector3 startPos, Vector3 target, SkillParams params);
static void CastElectricWrapper(Vector3 startPos, Vector3 target, SkillParams params);
static void CastWindWrapper(Vector3 startPos, Vector3 target, SkillParams params);

static void UpdateFluidSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius);
static void UpdateMetalSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius);
static void UpdateFireSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius);
static void UpdateWoodSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius);
static void UpdateElectricSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius);
static void UpdateWindWrapper(float dt, Vector3 enemyPos, float enemyRadius);

static void DrawCircleLines3D(Vector3 center, float radius, Color color) {
    const int segments = 24;
    Vector3 prevPoint = { 0 };
    for (int i = 0; i <= segments; i++) {
        float t = ((float)i / (float)segments) * PI * 2.0f;
        Vector3 point = {
            center.x + radius * cosf(t),
            center.y,
            center.z + radius * sinf(t)
        };
        if (i > 0) {
            DrawLine3D(prevPoint, point, color);
        }
        prevPoint = point;
    }
}

static void AddCastPortal(Vector3 pos, Color portalColor, CastPathType pathType, float sizeScale, Vector3 target) {
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

static void AddFloatingText(Vector3 pos, const char* text, Color color, float size, float maxLife) {
    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (!floatingTexts[i].active) {
            floatingTexts[i].position = pos;
            floatingTexts[i].position.x += GetRandomValue(-25, 25);
            floatingTexts[i].position.y += GetRandomValue(-15, 5); // This offset works on the height Y axis in 3D
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
        RegisterSkill("WIND", LIGHTGRAY, InitWindSkill, CastWindWrapper, UpdateWindWrapper, DrawWindSkill, UnloadWindSkill);
    }
}

int RegisterSkill(const char* name, Color color,
                  void (*init)(int screenWidth, int screenHeight),
                  void (*cast)(Vector3 startPos, Vector3 target, SkillParams params),
                  void (*update)(float dt, Vector3 enemyPos, float enemyRadius),
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

void UpdateSkillManager(float dt, Vector3 enemyPos, float enemyRadius) {
    EnsureBuiltInRegistered();

    if (slowTimer > 0.0f) {
        slowTimer -= dt;
    }
    if (burnTimer > 0.0f) {
        burnTimer -= dt;
        burnTickAccumulator += dt;
        if (burnTickAccumulator >= 0.5f) {
            burnTickAccumulator = 0.0f;
            Vector3 tickPos = enemyPos;
            tickPos.x += (float)GetRandomValue(-20, 20);
            tickPos.y += (float)GetRandomValue(-20, 20); // vertical height jitter
            tickPos.z += (float)GetRandomValue(-20, 20);
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
            floatingTexts[i].position.y += 55.0f * dt; // Rises vertically in Y axis
            if (floatingTexts[i].lifetime <= 0.0f) {
                floatingTexts[i].active = false;
            }
        }
    }
}

void DrawSkillManager(void) {
    float time = (float)GetTime();
    EnsureBuiltInRegistered();

    // Portals are drawn inside 3D mode (must be called within BeginMode3D/EndMode3D context in main.c)
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
            Color col = activePortals[i].color;

            // 1. Draw volumetric glowing aura flat on X-Z plane
            for (float f = 1.0f; f <= 1.4f; f += 0.1f) {
                float alphaFactor = (1.4f - f) / 0.4f;
                DrawCircle3D(activePortals[i].position, r * f, (Vector3){ 1.0f, 0.0f, 0.0f }, 90.0f, ColorAlpha(col, 0.15f * alphaFactor * currentScale));
            }

            // 2. Draw magical pitch-black core
            DrawCircle3D(activePortals[i].position, r * 0.75f, (Vector3){ 1.0f, 0.0f, 0.0f }, 90.0f, ColorAlpha(BLACK, 0.95f * currentScale));
            
            // 3. Draw outline rings
            DrawCircleLines3D(activePortals[i].position, r * 0.85f, ColorAlpha(col, 0.8f * currentScale));
            DrawCircleLines3D(activePortals[i].position, r * 0.65f, ColorAlpha(col, 0.5f * currentScale));
            
            // 4. Draw rotating spokes (Magic Runes Spokes)
            int spokes = 8;
            for (int s = 0; s < spokes; s++) {
                float spAngle = (float)s * (PI * 2.0f / spokes) + time * 1.5f;
                Vector3 pInner = {
                    activePortals[i].position.x + r * 0.5f * cosf(spAngle),
                    activePortals[i].position.y,
                    activePortals[i].position.z + r * 0.5f * sinf(spAngle)
                };
                Vector3 pOuter = {
                    activePortals[i].position.x + r * 1.15f * cosf(spAngle),
                    activePortals[i].position.y,
                    activePortals[i].position.z + r * 1.15f * sinf(spAngle)
                };
                DrawLine3D(pInner, pOuter, ColorAlpha(col, 0.35f * currentScale));
            }
            
            // 5. Draw 8 swirling sparks (drawn as small 3D spheres)
            float rotSpeed = time * 7.0f;
            for (int p = 0; p < 8; p++) {
                float angleOffset = (float)p * (PI * 0.25f) + rotSpeed;
                Vector3 sparkPos = {
                    activePortals[i].position.x + r * 0.85f * cosf(angleOffset),
                    activePortals[i].position.y,
                    activePortals[i].position.z + r * 0.85f * sinf(angleOffset)
                };
                DrawSphere(sparkPos, 3.0f * currentScale, ColorAlpha(col, 0.95f * currentScale));
            }

            // 6. Draw additional outer ring for ground portal
            if (activePortals[i].isGround) {
                DrawCircleLines3D(activePortals[i].position, r * 1.25f, ColorAlpha(col, 0.25f * currentScale));
            }
        }
    }

    // Draw all registered skills
    for (int i = 0; i < registeredSkillCount; i++) {
        if (skillRegistry[i].draw) {
            skillRegistry[i].draw();
        }
    }

    // Floating text is drawn in 2D mode (outside BeginMode3D/EndMode3D context in main.c)
    // We temporarily exit 3D mode in main.c, project the positions to screen-space using GetWorldToScreen,
    // and draw them here.
    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (floatingTexts[i].active) {
            float alpha = floatingTexts[i].lifetime / floatingTexts[i].maxLifetime;
            Color col = ColorAlpha(floatingTexts[i].color, alpha);
            
            // Project 3D coordinate to 2D screen coordinates
            Vector2 screenPos = GetWorldToScreen(floatingTexts[i].position, camera);

            // Draw text outline shadow
            DrawText(floatingTexts[i].text, 
                     (int)screenPos.x - 1, 
                     (int)screenPos.y + 1, 
                     (int)floatingTexts[i].size, 
                     ColorAlpha(BLACK, alpha * 0.7f));
            
            DrawText(floatingTexts[i].text, 
                     (int)screenPos.x, 
                     (int)screenPos.y, 
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

void CastSkill(int skillIndex, Vector3 startPos, Vector3 target, SkillParams params) {
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

    Vector3 adjustedStartPos = startPos;
    Vector3 adjustedTarget = target;
    Color skillColor = skillRegistry[skillIndex].color;

    switch (params.pathType) {
        case CAST_PATH_PROJECTILE: {
            // PROJECTILE: shoulder is at height startPos.y + 25.0f
            Vector3 shoulderPos = (Vector3){ startPos.x, startPos.y + 25.0f, startPos.z };
            Vector3 aimDir = Vector3Normalize(Vector3Subtract(target, shoulderPos));
            adjustedStartPos = Vector3Add(shoulderPos, Vector3Scale(aimDir, 22.0f));
            adjustedTarget = target;
            break;
        }
        case CAST_PATH_UNDERGROUND: {
            if (params.anchorType == CAST_ANCHOR_CASTER) {
                Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
                adjustedStartPos = Vector3Add(startPos, Vector3Scale(dir, 35.0f));
                adjustedStartPos.y = 0.02f; // lay flat on the ground plane (Y-offset to avoid Z-fighting)
                adjustedTarget = target;
                
                if (params.showPortal) {
                    AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_UNDERGROUND, params.sizeScale, target);
                }
            } else {
                adjustedStartPos = target;
                adjustedStartPos.y = 0.02f; // target ground
                adjustedTarget = (Vector3){ target.x, target.y + 100.0f, target.z }; // shoots straight up
                
                if (params.showPortal) {
                    AddCastPortal(adjustedStartPos, skillColor, CAST_PATH_UNDERGROUND, params.sizeScale, adjustedTarget);
                }
            }
            break;
        }
        case CAST_PATH_SKY: {
            if (params.anchorType == CAST_ANCHOR_CASTER) {
                adjustedStartPos = (Vector3){ startPos.x, startPos.y + 348.0f, startPos.z };
                adjustedTarget = target;
            } else {
                adjustedStartPos = (Vector3){ target.x, target.y + 348.0f, target.z };
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

// Built-in skill wrappers implementation using Vector3

static void CastWaterWrapper(Vector3 startPos, Vector3 target, SkillParams params) {
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

static void CastMetalWrapper(Vector3 startPos, Vector3 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 3;
    CastMetalSkill(startPos, target, qty, params.sizeScale);
}

static void CastFireWrapper(Vector3 startPos, Vector3 target, SkillParams params) {
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

static void CastWoodWrapper(Vector3 startPos, Vector3 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 3;
    CastWoodSkill(startPos, target, qty, params.sizeScale);
}

static void CastElectricWrapper(Vector3 startPos, Vector3 target, SkillParams params) {
    int qty = params.quantity > 0 ? params.quantity : 1;
    if (qty > 1) {
        for (int i = 0; i < qty; i++) {
            float angle = ((float)i / (float)(qty - 1) - 0.5f) * 0.35f;
            Vector3 dir = Vector3Normalize(Vector3Subtract(target, startPos));
            // Rotate perp direction vector around Y axis by angle
            Vector3 perp = { -dir.z, 0.0f, dir.x };
            Vector3 offsetTarget = Vector3Add(target, Vector3Scale(perp, angle * 180.0f));
            CastElectricSkill(startPos, offsetTarget, params.sizeScale);
        }
    } else {
        CastElectricSkill(startPos, target, params.sizeScale);
    }
}

static void UpdateFluidSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius) {
    UpdateFluidSkill(dt);
    int waterHits[MAX_TEMP_PROJECTILES];
    int waterHitCount = 0;
    Vector3 waterHitPos[MAX_TEMP_PROJECTILES];
    int numWater = GetFluidSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numWater; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector3Distance(tempProjectiles[i].position, enemyPos);
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

static void UpdateMetalSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius) {
    UpdateMetalSkill(dt);
    int metalHits[MAX_TEMP_PROJECTILES];
    int metalHitCount = 0;
    Vector3 metalHitPos[MAX_TEMP_PROJECTILES];
    int numMetal = GetMetalSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numMetal; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector3Distance(tempProjectiles[i].position, enemyPos);
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

static void UpdateFireSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius) {
    UpdateFireSkill(dt);
    int fireHits[MAX_TEMP_PROJECTILES];
    int fireHitCount = 0;
    Vector3 fireHitPos[MAX_TEMP_PROJECTILES];
    int numFire = GetFireSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numFire; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector3Distance(tempProjectiles[i].position, enemyPos);
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

static void UpdateWoodSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius) {
    UpdateWoodSkill(dt);
    int woodHits[MAX_TEMP_PROJECTILES];
    int woodHitCount = 0;
    Vector3 woodHitPos[MAX_TEMP_PROJECTILES];
    int numWood = GetWoodSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numWood; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector3Distance(tempProjectiles[i].position, enemyPos);
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

static void UpdateElectricSkillWrapper(float dt, Vector3 enemyPos, float enemyRadius) {
    UpdateElectricSkill(dt);
    int electricHits[MAX_TEMP_PROJECTILES];
    int electricHitCount = 0;
    Vector3 electricHitPos[MAX_TEMP_PROJECTILES];
    int numElectric = GetElectricSkillProjectiles(tempProjectiles, MAX_TEMP_PROJECTILES);
    for (int i = 0; i < numElectric; i++) {
        if (tempProjectiles[i].active) {
            float dist = Vector3Distance(tempProjectiles[i].position, enemyPos);
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

static void CastWindWrapper(Vector3 startPos, Vector3 target, SkillParams params) {
    CastWindSkill(startPos, target, params.sizeScale);
}

static void UpdateWindWrapper(float dt, Vector3 enemyPos, float enemyRadius) {
    UpdateWindSkill(dt);
}
