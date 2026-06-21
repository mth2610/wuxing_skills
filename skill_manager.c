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

static void AddCastPortal(Vector2 pos, SkillType element, CastPathType pathType, float sizeScale, Vector2 target) {
    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        if (!activePortals[i].active) {
            activePortals[i].position = pos;
            
            Color col = WHITE;
            switch (element) {
                case SKILL_WATER: col = ColorAlpha(SKYBLUE, 0.8f); break;
                case SKILL_METAL: col = ColorAlpha(GOLD, 0.8f); break;
                case SKILL_FIRE: col = ColorAlpha(ORANGE, 0.8f); break;
                case SKILL_WOOD: col = ColorAlpha(LIME, 0.8f); break;
                case SKILL_ELECTRIC: col = ColorAlpha(PURPLE, 0.8f); break;
            }
            activePortals[i].color = col;
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

    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        activePortals[i].active = false;
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

    // Cập nhật các cổng xuất chiêu (Portals)
    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        if (activePortals[i].active) {
            activePortals[i].lifetime -= dt;
            if (activePortals[i].lifetime <= 0.0f) {
                activePortals[i].active = false;
            }
        }
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
    float time = (float)GetTime();

    // Vẽ tất cả các cổng Cast Portal (Ground & Sky portals) trước để chúng nằm dưới các hạt hiệu ứng chiêu thức
    for (int i = 0; i < MAX_ACTIVE_PORTALS; i++) {
        if (activePortals[i].active) {
            float progress = 1.0f - (activePortals[i].lifetime / activePortals[i].maxLifetime);
            float currentScale = 1.0f;
            if (progress < 0.2f) {
                currentScale = progress / 0.2f; // Khai mở
            } else if (progress > 0.8f) {
                currentScale = (1.0f - progress) / 0.2f; // Thu nhỏ tắt đi
            }
            float r = activePortals[i].size * currentScale;
            float tilt = activePortals[i].tiltRatio;
            float angle = activePortals[i].angle;
            Color col = activePortals[i].color;
            float cosA = cosf(angle);
            float sinA = sinf(angle);

            // 1. Vẽ Hào Quang Loang Màu (Volumetric Glowing Aura) bằng các tầng elip mờ lan tỏa ra ngoài
            for (float f = 1.0f; f <= 1.4f; f += 0.1f) {
                float alphaFactor = (1.4f - f) / 0.4f;
                DrawTiltedEllipseFilled(activePortals[i].position, r * f, r * f * tilt, angle, ColorAlpha(col, 0.15f * alphaFactor * currentScale));
            }

            // 2. Vẽ nhân Lỗ Đen Phép Thuật (Pitch-Black Magical Core)
            DrawTiltedEllipseFilled(activePortals[i].position, r * 0.75f, r * 0.75f * tilt, angle, ColorAlpha(BLACK, 0.95f * currentScale));
            
            // 3. Vẽ vòng sáng năng lượng sắc nét của miệng hố đen
            DrawTiltedEllipseLines(activePortals[i].position, r * 0.85f, r * 0.85f * tilt, angle, ColorAlpha(col, 0.8f * currentScale), 2.5f);
            DrawTiltedEllipseLines(activePortals[i].position, r * 0.65f, r * 0.65f * tilt, angle, ColorAlpha(col, 0.5f * currentScale), 1.5f);
            
            // 4. Vẽ các vạch chia pháp trận năng lượng cổ xoay tròn (Magic Runes Spokes)
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
            
            // 5. Vẽ 8 hạt năng lượng xoáy tròn xung quanh vành hố đen (không trơn tuột)
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

            // 6. Nếu là Ground Portal (pháp trận dưới đất), vẽ thêm các vạch tia sáng phụ toả ra ngoài
            if (activePortals[i].isGround) {
                DrawTiltedEllipseLines(activePortals[i].position, r * 1.25f, r * 1.25f * tilt, angle, ColorAlpha(col, 0.25f * currentScale), 1.0f);
            }
        }
    }

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
    Vector2 anchorPoint = (params.anchorType == CAST_ANCHOR_CASTER) ? startPos : target;
    Vector2 adjustedStartPos = startPos;
    Vector2 adjustedTarget = target;

    switch (params.pathType) {
        case CAST_PATH_PROJECTILE: {
            // PROJECTILE: Xuất phát từ TAY của nhân vật
            // startPos là chân đất của caster. Khớp vai ở: startPos.y - params.casterZ - 25.0f
            Vector2 shoulderPos = (Vector2){ startPos.x, startPos.y - params.casterZ - 25.0f };
            Vector2 aimDir = Vector2Normalize(Vector2Subtract(target, shoulderPos));
            // Tay vươn ra 22.0f pixel theo hướng ngắm bắn
            adjustedStartPos = Vector2Add(shoulderPos, Vector2Scale(aimDir, 22.0f));
            adjustedTarget = target;
            break;
        }
        case CAST_PATH_UNDERGROUND: {
            if (params.anchorType == CAST_ANCHOR_CASTER) {
                // GROUND + CASTER: Dưới chân nhân vật nhưng lệch đi một khoảng nhỏ hướng về mục tiêu
                Vector2 dir = Vector2Normalize(Vector2Subtract(target, startPos));
                adjustedStartPos = Vector2Add(startPos, Vector2Scale(dir, 35.0f));
                adjustedTarget = target;
                
                // Mọc từ dưới chân người chơi: tạo một cổng tròn dưới mặt đất của người chơi
                if (params.showPortal) {
                    AddCastPortal(adjustedStartPos, type, CAST_PATH_UNDERGROUND, params.sizeScale, target);
                }
            } else {
                // GROUND + TARGET: Mọc thẳng từ dưới đất (dưới chân target) chui lên
                // Xuất phát chuẩn xác từ vị trí mục tiêu và bắn thẳng đứng lên trên qua người mục tiêu
                adjustedStartPos = target;
                adjustedTarget = (Vector2){ target.x, target.y - 100.0f };
                
                // Mọc dưới chân đối thủ: tạo một cổng tròn dưới mặt đất của đối thủ
                if (params.showPortal) {
                    AddCastPortal(adjustedStartPos, type, CAST_PATH_UNDERGROUND, params.sizeScale, adjustedTarget);
                }
            }
            break;
        }
        case CAST_PATH_SKY: {
            if (params.anchorType == CAST_ANCHOR_CASTER) {
                // SKY + CASTER: Bắt đầu từ bầu trời phía trên nhân vật cách đầu một khoảng 300.0f pixel
                // Đầu nhân vật ở: startPos.y - params.casterZ - 48.0f
                adjustedStartPos = (Vector2){ startPos.x, startPos.y - params.casterZ - 348.0f };
                adjustedTarget = target;
            } else {
                // SKY + TARGET: Rơi thẳng từ trời xuống đầu target cách đầu 300.0f pixel
                adjustedStartPos = (Vector2){ target.x, target.y - 348.0f };
                adjustedTarget = target;
            }
            // Tạo cổng rifts rực rỡ ở trên bầu trời
            if (params.showPortal) {
                AddCastPortal(adjustedStartPos, type, CAST_PATH_SKY, params.sizeScale, target);
            }
            break;
        }
        default:
            break;
    }

    switch (type) {
        case SKILL_WATER: {
            int qty = params.quantity > 0 ? params.quantity : 1;
            if (qty > 1) {
                for (int i = 0; i < qty; i++) {
                    float twist = ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f); // Symmetrical fanning from -PI/4 to PI/4
                    CastFluidSkill(adjustedStartPos, adjustedTarget, twist, params.sizeScale);
                }
            } else {
                CastFluidSkill(adjustedStartPos, adjustedTarget, -0.4f, params.sizeScale); // Default curve
            }
            break;
        }
        case SKILL_METAL: {
            int qty = params.quantity > 0 ? params.quantity : 3;
            CastMetalSkill(adjustedStartPos, adjustedTarget, qty, params.sizeScale);
            break;
        }
        case SKILL_FIRE: {
            int qty = params.quantity > 0 ? params.quantity : 1;
            if (qty > 1) {
                for (int i = 0; i < qty; i++) {
                    float twist = ((float)i / (float)(qty - 1) - 0.5f) * (PI * 0.5f); // Symmetrical fanning from -PI/4 to PI/4
                    CastFireSkill(adjustedStartPos, adjustedTarget, twist, params.sizeScale);
                }
            } else {
                CastFireSkill(adjustedStartPos, adjustedTarget, -0.4f, params.sizeScale); // Default curve
            }
            break;
        }
        case SKILL_WOOD: {
            int qty = params.quantity > 0 ? params.quantity : 3;
            CastWoodSkill(adjustedStartPos, adjustedTarget, qty, params.sizeScale);
            break;
        }
        case SKILL_ELECTRIC: {
            int qty = params.quantity > 0 ? params.quantity : 1;
            if (qty > 1) {
                for (int i = 0; i < qty; i++) {
                    float angle = ((float)i / (float)(qty - 1) - 0.5f) * 0.35f;
                    Vector2 dir = Vector2Normalize(Vector2Subtract(adjustedTarget, adjustedStartPos));
                    Vector2 perp = { -dir.y, dir.x };
                    Vector2 offsetTarget = Vector2Add(adjustedTarget, Vector2Scale(perp, angle * 180.0f));
                    CastElectricSkill(adjustedStartPos, offsetTarget, params.sizeScale);
                }
            } else {
                CastElectricSkill(adjustedStartPos, adjustedTarget, params.sizeScale);
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
