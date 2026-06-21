#include "skill_manager.h"
#include "fluid_skill.h"
#include "metal_skill.h"
#include "fire_skill.h"
#include "wood_skill.h"
#include "electric_skill.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>

#define MAX_FLOATING_TEXTS 64
#define MAX_TEMP_PROJECTILES 128

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

static void AddFloatingText(Vector2 pos, const char* text, Color color, float size, float maxLife) {
    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (!floatingTexts[i].active) {
            floatingTexts[i].position = pos;
            // Thêm độ lệch ngẫu nhiên nhẹ để tránh các chữ bị đè khít lên nhau
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

void InitSkillManager(int screenWidth, int screenHeight) {
    InitFluidSkill(screenWidth, screenHeight);
    InitMetalSkill(screenWidth, screenHeight);
    InitFireSkill(screenWidth, screenHeight);
    InitWoodSkill(screenWidth, screenHeight);
    InitElectricSkill(screenWidth, screenHeight);

    slowTimer = 0.0f;
    burnTimer = 0.0f;
    burnTickAccumulator = 0.0f;

    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        floatingTexts[i].active = false;
    }
}

void UpdateSkillManager(float dt, Vector2 enemyPos, float enemyRadius) {
    // Cập nhật trạng thái hiệu ứng (Status Effects)
    if (slowTimer > 0.0f) {
        slowTimer -= dt;
    }
    if (burnTimer > 0.0f) {
        burnTimer -= dt;
        burnTickAccumulator += dt;
        if (burnTickAccumulator >= 0.5f) { // Nhảy sát thương đốt mỗi 0.5 giây
            burnTickAccumulator = 0.0f;
            Vector2 tickPos = enemyPos;
            tickPos.x += (float)GetRandomValue(-20, 20);
            tickPos.y += (float)GetRandomValue(-20, 20);
            AddFloatingText(tickPos, "12", RED, 16.0f, 0.4f);
        }
    } else {
        burnTickAccumulator = 0.0f;
    }

    // 1. Cập nhật vật lý và hạt của từng chiêu thức
    UpdateFluidSkill(dt);
    UpdateMetalSkill(dt);
    UpdateFireSkill(dt);
    UpdateWoodSkill(dt);
    UpdateElectricSkill(dt);

    // 2. Cập nhật các chữ sát thương bay lên (Floating Text)
    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (floatingTexts[i].active) {
            floatingTexts[i].lifetime -= dt;
            floatingTexts[i].position.y -= 55.0f * dt; // Bay lên trên từ từ
            if (floatingTexts[i].lifetime <= 0.0f) {
                floatingTexts[i].active = false;
            }
        }
    }

    // 3. XỬ LÝ VA CHẠM: Duyệt qua các projectiles của từng hệ

    // --- HỆ THỦY (WATER) ---
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
        slowTimer = 3.0f; // Kích hoạt trạng thái làm chậm 3 giây
        AddFloatingText(waterHitPos[i], "15", BLUE, 22.0f, 0.7f);
        AddFloatingText(waterHitPos[i], "SLOW!", SKYBLUE, 16.0f, 0.8f);
    }

    // --- HỆ KIM (METAL) ---
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
        bool isCrit = GetRandomValue(1, 100) <= 30; // 30% bạo kích
        if (isCrit) {
            AddFloatingText(metalHitPos[i], "CRIT! 95", ORANGE, 28.0f, 1.0f);
        } else {
            AddFloatingText(metalHitPos[i], "45", GOLD, 22.0f, 0.7f);
        }
    }

    // --- HỆ HỎA (FIRE) ---
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
        burnTimer = 3.0f; // Kích hoạt trạng thái thiêu đốt 3 giây
        burnTickAccumulator = 0.0f;
        AddFloatingText(fireHitPos[i], "80", RED, 25.0f, 0.8f);
        AddFloatingText(fireHitPos[i], "BURN!", ORANGE, 18.0f, 0.9f);
    }

    // --- HỆ MỘC (WOOD) ---
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

    // --- HỆ LÔI (ELECTRIC) ---
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

void DrawSkillManager(void) {
    DrawFluidSkill();
    DrawMetalSkill();
    DrawFireSkill();
    DrawWoodSkill();
    DrawElectricSkill();

    // Vẽ toàn bộ chữ sát thương bay lên
    for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
        if (floatingTexts[i].active) {
            float alpha = floatingTexts[i].lifetime / floatingTexts[i].maxLifetime;
            Color col = ColorAlpha(floatingTexts[i].color, alpha);
            
            // Vẽ viền chữ màu đen bóng đổ
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
    UnloadFluidSkill();
    UnloadMetalSkill();
    UnloadFireSkill();
    UnloadWoodSkill();
    UnloadElectricSkill();
}

void CastSkill(SkillType type, Vector2 startPos, Vector2 target, SkillParams params) {
    switch (type) {
        case SKILL_WATER: {
            int qty = params.quantity > 0 ? params.quantity : 1;
            if (qty > 1) {
                for (int i = 0; i < qty; i++) {
                    float twist = ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f); // Symmetrical fanning from -PI/4 to PI/4
                    CastFluidSkill(startPos, target, twist, params.sizeScale);
                }
            } else {
                CastFluidSkill(startPos, target, -0.4f, params.sizeScale); // Default curve
            }
            break;
        }
        case SKILL_METAL: {
            int qty = params.quantity > 0 ? params.quantity : 3;
            CastMetalSkill(startPos, target, qty, params.sizeScale);
            break;
        }
        case SKILL_FIRE: {
            int qty = params.quantity > 0 ? params.quantity : 1;
            if (qty > 1) {
                for (int i = 0; i < qty; i++) {
                    float twist = ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f); // Symmetrical fanning from -PI/4 to PI/4
                    CastFireSkill(startPos, target, twist, params.sizeScale);
                }
            } else {
                CastFireSkill(startPos, target, -0.4f, params.sizeScale); // Default curve
            }
            break;
        }
        case SKILL_WOOD: {
            int qty = params.quantity > 0 ? params.quantity : 3;
            CastWoodSkill(startPos, target, qty, params.sizeScale);
            break;
        }
        case SKILL_ELECTRIC: {
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
            break;
        }
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
