#include "raylib.h"
#include <math.h>
#include "skill_manager.h"
#include "raymath.h"

typedef struct {
    Vector2 position;
    Color color;
    float radius;
    float lifetime;
    float maxLifetime;
    bool active;
} DashParticle;

#define MAX_DASH_PARTICLES 150
static DashParticle dashParticles[MAX_DASH_PARTICLES];

static void SpawnDashParticle(Vector2 pos, Color color, float radius, float lifetime) {
    for (int i = 0; i < MAX_DASH_PARTICLES; i++) {
        if (!dashParticles[i].active) {
            dashParticles[i].position = pos;
            dashParticles[i].color = color;
            dashParticles[i].radius = radius;
            dashParticles[i].lifetime = lifetime;
            dashParticles[i].maxLifetime = lifetime;
            dashParticles[i].active = true;
            break;
        }
    }
}

typedef enum {
    ENTITY_PLAYER = 0,
    ENTITY_ENEMY,
    ENTITY_PILLAR_0,
    ENTITY_PILLAR_1,
    ENTITY_PILLAR_2
} RenderEntityType;

typedef struct {
    RenderEntityType type;
    float ySortValue;
    int index; // Cho cột đá
} RenderEntity;

static void DrawCylinder25D(Vector2 basePos, float radius, float height, Color baseColor, Color topColor, Color outlineColor) {
    // 1. Vẽ hình elip đáy
    DrawEllipse((int)basePos.x, (int)basePos.y, (int)radius, (int)(radius * 0.4f), baseColor);
    
    // 2. Vẽ thân hình trụ chữ nhật
    DrawRectangle((int)(basePos.x - radius), (int)(basePos.y - height), (int)(radius * 2.0f), (int)height, baseColor);
    
    // 3. Vẽ hình elip nắp đỉnh
    DrawEllipse((int)basePos.x, (int)(basePos.y - height), (int)radius, (int)(radius * 0.4f), topColor);
    
    // 4. Vẽ các đường viền nổi khối 2.5D
    DrawLine((int)(basePos.x - radius), (int)basePos.y, (int)(basePos.x - radius), (int)(basePos.y - height), outlineColor);
    DrawLine((int)(basePos.x + radius), (int)basePos.y, (int)(basePos.x + radius), (int)(basePos.y - height), outlineColor);
    DrawEllipseLines((int)basePos.x, (int)(basePos.y - height), (int)radius, (int)(radius * 0.4f), outlineColor);
    DrawEllipseLines((int)basePos.x, (int)basePos.y, (int)radius, (int)(radius * 0.4f), outlineColor);
}

int main(void) {
    const int screenWidth = 1200;
    const int screenHeight = 700;
    InitWindow(screenWidth, screenHeight, "Avatar: Element Testbed");

    // Khởi tạo hệ thống quản lý chiêu thức
    InitSkillManager(screenWidth, screenHeight);

    Vector2 characterPos = { 130, 350 };
    SkillType activeSkill = SKILL_WATER; // Mặc định vào game là hệ Thủy

    // Khai báo biến vị trí Enemy và tỉ lệ dao động
    Vector2 enemyPos = { 900, 350 };
    float enemyOscillationScale = 1.0f;

    int selectedQty = 3; // Mặc định số lượng là 3
    float selectedSize = 1.0f; // Mặc định kích thước là 1.0x

    // Các biến trạng thái lướt (Dash / Khinh Công)
    float dashCooldown = 0.0f;
    float dashTimer = 0.0f;
    Vector2 dashDir = { 0.0f, 0.0f };
    bool isDashing = false;

    // Các biến trạng thái Nhảy 2.5D
    float charZ = 0.0f;
    float charZVelocity = 0.0f;
    const float gravity = 1500.0f;

    // Các biến chế độ di chuyển của Enemy
    typedef enum {
        ENEMY_STATIC = 0,
        ENEMY_CHASE,
        ENEMY_PATROL
    } EnemyMode;
    EnemyMode enemyMode = ENEMY_STATIC;
    float enemySpeed = 120.0f;
    float patrolAngle = 0.0f;

    // Vị trí và kích thước các cột đá chướng ngại vật
    typedef struct {
        Vector2 position;
        float radius;
    } StonePillar;
    
    #define NUM_PILLARS 3
    StonePillar pillars[NUM_PILLARS] = {
        { { 400.0f, 320.0f }, 25.0f },
        { { 800.0f, 520.0f }, 30.0f },
        { { 600.0f, 260.0f }, 20.0f }
    };


    // Khung UI chọn số lượng (Quantity) và kích thước (Size) ở góc trên bên phải
    Rectangle rectQty[5];
    for (int i = 0; i < 5; i++) {
        rectQty[i] = (Rectangle){ 870 + i * 60, 20, 50, 35 };
    }
    Rectangle rectSize[3];
    for (int i = 0; i < 3; i++) {
        rectSize[i] = (Rectangle){ 870 + i * 100, 70, 90, 35 };
    }

    // Khung UI của các nút bấm (cách nhau 170px)
    Rectangle btnWater = { 20, 20, 150, 45 };
    Rectangle btnMetal = { 190, 20, 150, 45 };
    Rectangle btnFire  = { 360, 20, 150, 45 }; 
    Rectangle btnWood  = { 530, 20, 150, 45 }; // Khung nút hệ Mộc
    Rectangle btnEarth = { 700, 20, 150, 45 }; // Khung nút hệ Thổ

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 mousePos = GetMousePosition();
        float time = GetTime();

        // 1. DỊCH CHUYỂN NHÂN VẬT & KHINH CÔNG (DASH & JUMP)
        Vector2 moveDir = { 0.0f, 0.0f };
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) moveDir.y -= 1.0f;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) moveDir.y += 1.0f;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) moveDir.x -= 1.0f;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) moveDir.x += 1.0f;

        if (Vector2Length(moveDir) > 0.0f) {
            moveDir = Vector2Normalize(moveDir);
        }

        if (dashCooldown > 0.0f) {
            dashCooldown -= dt;
        }

        // Cập nhật trọng lực nhảy (Jump physics Z-axis)
        if (charZ > 0.0f || charZVelocity > 0.0f) {
            charZVelocity -= gravity * dt;
            charZ += charZVelocity * dt;
            if (charZ <= 0.0f) {
                charZ = 0.0f;
                charZVelocity = 0.0f;
            }
        }

        // Kích hoạt nhảy (Jump) khi nhấn Space
        if (IsKeyPressed(KEY_SPACE) && charZ == 0.0f) {
            charZVelocity = 550.0f;
        }

        if (isDashing) {
            dashTimer -= dt;
            float dashSpeed = 1200.0f;
            characterPos.x += dashDir.x * dashSpeed * dt;
            characterPos.y += dashDir.y * dashSpeed * dt;

            // Sinh hạt dư ảnh lướt theo nguyên tố hiện tại, lấy theo chiều cao Z
            Color trailColor = SKYBLUE;
            if (activeSkill == SKILL_METAL) trailColor = GetColor(0xFFD700FF);
            else if (activeSkill == SKILL_FIRE) trailColor = GetColor(0xFF5500FF);
            else if (activeSkill == SKILL_WOOD) trailColor = GetColor(0x22AA33FF);
            else if (activeSkill == SKILL_ELECTRIC) trailColor = GetColor(0xBA55D3FF);

            SpawnDashParticle((Vector2){ characterPos.x, characterPos.y - charZ }, trailColor, 12.0f, 0.35f);

            if (dashTimer <= 0.0f) {
                isDashing = false;
            }
        } else {
            float moveSpeed = 300.0f;
            characterPos.x += moveDir.x * moveSpeed * dt;
            characterPos.y += moveDir.y * moveSpeed * dt;

            // Kích hoạt lướt (Dash) khi nhấn Left Shift
            if (IsKeyPressed(KEY_LEFT_SHIFT) && dashCooldown <= 0.0f) {
                isDashing = true;
                dashTimer = 0.15f;
                dashCooldown = 0.8f;
                
                if (Vector2Length(moveDir) == 0.0f) {
                    dashDir = Vector2Normalize(Vector2Subtract(mousePos, characterPos));
                } else {
                    dashDir = moveDir;
                }
            }
        }

        // Ràng buộc vị trí nhân vật bên trong võ đài hình tròn
        Vector2 arenaCenter = { 600.0f, 440.0f };
        float arenaRadius = 360.0f;
        float charRadius = 30.0f;

        Vector2 toChar = Vector2Subtract(characterPos, arenaCenter);
        if (Vector2Length(toChar) > arenaRadius - charRadius) {
            characterPos = Vector2Add(arenaCenter, Vector2Scale(Vector2Normalize(toChar), arenaRadius - charRadius));
        }

        // Va chạm vật lý với 3 cột đá chướng ngại vật
        for (int i = 0; i < NUM_PILLARS; i++) {
            float dist = Vector2Distance(characterPos, pillars[i].position);
            float minDist = charRadius + pillars[i].radius;
            if (dist < minDist) {
                Vector2 pushDir = Vector2Normalize(Vector2Subtract(characterPos, pillars[i].position));
                characterPos = Vector2Add(pillars[i].position, Vector2Scale(pushDir, minDist));
            }
        }

        // Cập nhật hạt lướt (dash particles)
        for (int i = 0; i < MAX_DASH_PARTICLES; i++) {
            if (dashParticles[i].active) {
                dashParticles[i].lifetime -= dt;
                if (dashParticles[i].lifetime <= 0.0f) {
                    dashParticles[i].active = false;
                }
            }
        }

        // 2. CẬP NHẬT ENEMY THEO CHẾ ĐỘ DI CHUYỂN
        if (IsKeyPressed(KEY_P)) {
            enemyMode = (enemyMode + 1) % 3;
        }

        // Tốc độ thực tế của enemy bị giảm/dừng theo hiệu ứng khống chế
        float currentEnemySpeed = enemySpeed;
        if (IsEnemySlowed()) {
            currentEnemySpeed *= 0.4f; // Chậm 60%
        }
        if (IsAnySkillCoiling()) {
            currentEnemySpeed = 0.0f; // Trói cứng
        }

        if (enemyMode == ENEMY_STATIC) {
            // Dao động dọc truyền thống quanh vị trí (900, 350)
            if (IsAnySkillCoiling()) {
                enemyOscillationScale += (0.0f - enemyOscillationScale) * 9.0f * dt;
            } else {
                enemyOscillationScale += (1.0f - enemyOscillationScale) * 2.0f * dt;
            }
            enemyPos.x = 900.0f;
            float oscillationFreq = IsEnemySlowed() ? 0.8f : 2.5f;
            enemyPos.y = 350.0f + sinf(time * oscillationFreq) * 100.0f * enemyOscillationScale;
        } 
        else if (enemyMode == ENEMY_CHASE) {
            // Đuổi theo người chơi
            if (currentEnemySpeed > 0.0f) {
                Vector2 toPlayer = Vector2Subtract(characterPos, enemyPos);
                float dist = Vector2Length(toPlayer);
                if (dist > 15.0f) {
                    Vector2 dir = Vector2Scale(Vector2Normalize(toPlayer), currentEnemySpeed * dt);
                    enemyPos = Vector2Add(enemyPos, dir);
                }
            }
        } 
        else if (enemyMode == ENEMY_PATROL) {
            // Tuần tra vòng tròn
            if (currentEnemySpeed > 0.0f) {
                float rotSpeed = (currentEnemySpeed / 200.0f);
                patrolAngle += rotSpeed * dt;
            }
            Vector2 patrolCenter = { 600.0f, 440.0f };
            enemyPos.x = patrolCenter.x + cosf(patrolAngle) * 250.0f;
            enemyPos.y = patrolCenter.y + sinf(patrolAngle) * 200.0f;
        }

        // Ràng buộc vị trí enemy bên trong võ đài hình tròn
        float enemyRadius = 35.0f;
        Vector2 toEnemy = Vector2Subtract(enemyPos, arenaCenter);
        if (Vector2Length(toEnemy) > arenaRadius - enemyRadius) {
            enemyPos = Vector2Add(arenaCenter, Vector2Scale(Vector2Normalize(toEnemy), arenaRadius - enemyRadius));
        }

        // Va chạm vật lý của enemy với 3 cột đá chướng ngại vật
        for (int i = 0; i < NUM_PILLARS; i++) {
            float dist = Vector2Distance(enemyPos, pillars[i].position);
            float minDist = enemyRadius + pillars[i].radius;
            if (dist < minDist) {
                Vector2 pushDir = Vector2Normalize(Vector2Subtract(enemyPos, pillars[i].position));
                enemyPos = Vector2Add(pillars[i].position, Vector2Scale(pushDir, minDist));
            }
        }

        // Rung chấn lắc lư khi bị choáng (Shock)
        if (IsAnySkillShocking()) {
            enemyPos.x += (float)GetRandomValue(-6, 6);
            enemyPos.y += (float)GetRandomValue(-6, 6);
        }

        // 1. KIỂM TRA TƯƠNG TÁC UI
        bool hoverWater = CheckCollisionPointRec(mousePos, btnWater);
        bool hoverMetal = CheckCollisionPointRec(mousePos, btnMetal);
        bool hoverFire  = CheckCollisionPointRec(mousePos, btnFire);
        bool hoverWood  = CheckCollisionPointRec(mousePos, btnWood); // Hover hệ Mộc
        bool hoverEarth = CheckCollisionPointRec(mousePos, btnEarth); // Hover hệ Thổ
        bool clickedOnUI = false;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // Kiểm tra click vào nút chọn số lượng
            for (int i = 0; i < 5; i++) {
                if (CheckCollisionPointRec(mousePos, rectQty[i])) {
                    selectedQty = i + 1;
                    clickedOnUI = true;
                }
            }
            // Kiểm tra click vào nút chọn kích thước
            float sizes[3] = { 1.0f, 1.5f, 2.0f };
            for (int i = 0; i < 3; i++) {
                if (CheckCollisionPointRec(mousePos, rectSize[i])) {
                    selectedSize = sizes[i];
                    clickedOnUI = true;
                }
            }

            // Nếu bấm trúng nút UI thì đổi chiêu & đánh dấu là đã click UI
            if (hoverWater) {
                activeSkill = SKILL_WATER;
                clickedOnUI = true;
            } 
            else if (hoverMetal) {
                activeSkill = SKILL_METAL;
                clickedOnUI = true;
            }
            else if (hoverFire) {
                activeSkill = SKILL_FIRE;
                clickedOnUI = true;
            }
            else if (hoverWood) {
                activeSkill = SKILL_WOOD;
                clickedOnUI = true;
            }
            else if (hoverEarth) {
                activeSkill = SKILL_ELECTRIC;
                clickedOnUI = true;
            }

            // 3. NẾU KHÔNG CLICK VÀO UI -> PHÓNG CHIÊU THỨC THEO CHỈ HƯỚNG CHUỘT
            if (!clickedOnUI) {
                Vector2 target = mousePos;

                // Cấu hình tham số chiêu thức
                SkillParams params = {0};
                params.level = 1;
                params.milestone = 1;
                params.sizeScale = selectedSize;
                params.damage = 100.0f;
                params.quantity = selectedQty;

                // Nếu đang nhảy, chiêu thức được bắn từ tọa độ trên không của nhân vật
                Vector2 startCastPos = { characterPos.x, characterPos.y - charZ };
                CastSkill(activeSkill, startCastPos, target, params);
            }
        }

        // Cập nhật vật lý và va chạm chiêu thức qua SkillManager
        UpdateSkillManager(dt, enemyPos, 35.0f);

        // --- VẼ MÀN HÌNH ---
        BeginDrawing();
            ClearBackground(GetColor(0x111111FF)); 

            // Vẽ Võ Đài đá hình tròn làm nền bản đồ
            DrawCircleV(arenaCenter, arenaRadius, GetColor(0x1B1B1FFF)); 
            DrawCircleV(arenaCenter, arenaRadius - 10.0f, GetColor(0x28282FFF));
            
            // Vẽ các vòng tròn cổ kính đồng tâm trong võ đài
            DrawCircleLinesV(arenaCenter, arenaRadius - 40.0f, GetColor(0x3E3E4FFF));
            DrawCircleLinesV(arenaCenter, arenaRadius - 120.0f, GetColor(0x33333FFF));
            DrawCircleLinesV(arenaCenter, 80.0f, GetColor(0x3A3A4AFF));
            
            // Vẽ các đường kinh tuyến ngũ hành giao cắt ở tâm võ đài
            DrawLineV(Vector2Add(arenaCenter, (Vector2){-arenaRadius, 0}), Vector2Add(arenaCenter, (Vector2){arenaRadius, 0}), GetColor(0x2E2E39FF));
            DrawLineV(Vector2Add(arenaCenter, (Vector2){0, -arenaRadius}), Vector2Add(arenaCenter, (Vector2){0, arenaRadius}), GetColor(0x2E2E39FF));

            // Vẽ bóng đổ của các cột đá
            for (int i = 0; i < NUM_PILLARS; i++) {
                DrawEllipse((int)pillars[i].position.x, (int)pillars[i].position.y + pillars[i].radius * 0.4f, 
                            pillars[i].radius * 1.1f, pillars[i].radius * 0.4f, ColorAlpha(BLACK, 0.5f));
            }

            // Vẽ bóng đổ của Enemy dưới mặt đất
            DrawEllipse((int)enemyPos.x, (int)enemyPos.y + 28, 30, 12, ColorAlpha(BLACK, 0.45f));

            // Vẽ bóng đổ của Player dưới mặt đất (Bóng sẽ nhỏ và mờ đi khi nhảy cao)
            float shadowScale = 1.0f - (charZ / 350.0f);
            if (shadowScale < 0.2f) shadowScale = 0.2f;
            DrawEllipse((int)characterPos.x, (int)characterPos.y + 24, 26 * shadowScale, 10 * shadowScale, ColorAlpha(BLACK, 0.5f * shadowScale));

            // Vẽ dư ảnh hạt lướt (dash particles)
            for (int i = 0; i < MAX_DASH_PARTICLES; i++) {
                if (dashParticles[i].active) {
                    float lifeRatio = dashParticles[i].lifetime / dashParticles[i].maxLifetime;
                    Color pCol = ColorAlpha(dashParticles[i].color, lifeRatio * 0.5f);
                    DrawCircleV(dashParticles[i].position, dashParticles[i].radius * (0.3f + 0.7f * lifeRatio), pCol);
                }
            }

            // Vẽ các chiêu thức đang bay và chữ sát thương qua SkillManager
            DrawSkillManager();

            // Sắp xếp các thực thể theo trục Y (Y-Sorting) để vẽ đúng thứ tự xa gần
            RenderEntity drawQueue[5];
            drawQueue[0] = (RenderEntity){ ENTITY_PLAYER, characterPos.y, 0 };
            drawQueue[1] = (RenderEntity){ ENTITY_ENEMY, enemyPos.y, 0 };
            drawQueue[2] = (RenderEntity){ ENTITY_PILLAR_0, pillars[0].position.y, 0 };
            drawQueue[3] = (RenderEntity){ ENTITY_PILLAR_1, pillars[1].position.y, 1 };
            drawQueue[4] = (RenderEntity){ ENTITY_PILLAR_2, pillars[2].position.y, 2 };

            // Sắp xếp nổi bọt (Bubble Sort) cho 5 phần tử
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4 - i; j++) {
                    if (drawQueue[j].ySortValue > drawQueue[j+1].ySortValue) {
                        RenderEntity temp = drawQueue[j];
                        drawQueue[j] = drawQueue[j+1];
                        drawQueue[j+1] = temp;
                    }
                }
            }

            // Vẽ các thực thể theo thứ tự đã sắp xếp Y-Sorting
            for (int i = 0; i < 5; i++) {
                switch (drawQueue[i].type) {
                    case ENTITY_PLAYER: {
                        // Vẽ hình trụ nhân vật
                        Vector2 charVisualPos = { characterPos.x, characterPos.y - charZ };
                        Color pBase = GetColor(0x5A5A5AFF);
                        Color pTop = GetColor(0x808080FF);
                        Color pOutline = GetColor(0xCCCCCCFF);

                        // Đổi màu viền nếu đang lướt
                        if (isDashing) {
                            if (activeSkill == SKILL_METAL) pOutline = GetColor(0xFFD700FF);
                            else if (activeSkill == SKILL_FIRE) pOutline = RED;
                            else if (activeSkill == SKILL_WOOD) pOutline = GREEN;
                            else if (activeSkill == SKILL_ELECTRIC) pOutline = GOLD;
                            else pOutline = SKYBLUE;
                        }

                        DrawCylinder25D(charVisualPos, 25.0f, 50.0f, pBase, pTop, pOutline);

                        // Vẽ hướng ngắm bắn từ tâm hình trụ (ở giữa độ cao)
                        Vector2 aimOrigin = { charVisualPos.x, charVisualPos.y - 25.0f };
                        Vector2 aimDir = Vector2Normalize(Vector2Subtract(mousePos, aimOrigin));
                        DrawCircleV(Vector2Add(aimOrigin, Vector2Scale(aimDir, 22)), 4, MAROON);
                        break;
                    }
                    case ENTITY_ENEMY: {
                        // Xác định màu sắc hình trụ Enemy dựa theo hiệu ứng khống chế
                        Color eBase = GetColor(0x8B2500FF);
                        Color eTop = GetColor(0xCD3700FF);
                        Color eOutline = GetColor(0xFF5500FF);

                        if (IsEnemySlowed()) {
                            eBase = GetColor(0x1B4F72FF);
                            eTop = GetColor(0x2874A6FF);
                            eOutline = SKYBLUE;
                        } else if (IsEnemyBurning()) {
                            eBase = RED;
                            eTop = ORANGE;
                            eOutline = YELLOW;
                        }

                        // Tính độ cao bồng bềnh nếu ở chế độ đứng yên
                        float enemyZ = 0.0f;
                        if (enemyMode == ENEMY_STATIC) {
                            enemyZ = (sinf(time * (IsEnemySlowed() ? 0.8f : 2.5f)) * 15.0f + 15.0f) * enemyOscillationScale;
                        }

                        Vector2 enemyVisualPos = { enemyPos.x, enemyPos.y - enemyZ };
                        DrawCylinder25D(enemyVisualPos, 30.0f, 60.0f, eBase, eTop, eOutline);

                        // Vẽ chữ ENEMY trên đầu hình trụ
                        DrawText("ENEMY", (int)enemyVisualPos.x - 22, (int)(enemyVisualPos.y - 75), 12, WHITE);
                        break;
                    }
                    case ENTITY_PILLAR_0:
                    case ENTITY_PILLAR_1:
                    case ENTITY_PILLAR_2: {
                        int idx = drawQueue[i].index;
                        DrawCylinder25D(pillars[idx].position, pillars[idx].radius, pillars[idx].radius * 2.5f, 
                                     GetColor(0x3B3B42FF), GetColor(0x5A5A63FF), GetColor(0x73737CFF));
                        break;
                    }
                }
            }


            // --- VẼ GIAO DIỆN CHỌN SKILL (UI) ---
            
            // Vẽ nút Hệ Thủy
            Color colorWater = activeSkill == SKILL_WATER ? BLUE : (hoverWater ? SKYBLUE : DARKBLUE);
            DrawRectangleRounded(btnWater, 0.3f, 10, colorWater);
            DrawRectangleRoundedLines(btnWater, 0.3f, 10, WHITE);            
            DrawText("WATER SKILL", (int)btnWater.x + 20, (int)btnWater.y + 15, 15, WHITE);

            // Vẽ nút Hệ Kim
            Color colorMetal = activeSkill == SKILL_METAL ? ORANGE : (hoverMetal ? GOLD : DARKGRAY);
            DrawRectangleRounded(btnMetal, 0.3f, 10, colorMetal);
            DrawRectangleRoundedLines(btnMetal, 0.3f, 10, WHITE);
            DrawText("METAL SKILL", (int)btnMetal.x + 22, (int)btnMetal.y + 15, 15, WHITE);
            
            // Vẽ nút Hệ Hỏa
            Color colorFire = activeSkill == SKILL_FIRE ? RED : (hoverFire ? ORANGE : MAROON);
            DrawRectangleRounded(btnFire, 0.3f, 10, colorFire);
            DrawRectangleRoundedLines(btnFire, 0.3f, 10, WHITE);
            DrawText("FIRE SKILL", (int)btnFire.x + 28, (int)btnFire.y + 15, 15, WHITE);

            // Vẽ nút Hệ Mộc
            Color colorWood = activeSkill == SKILL_WOOD ? GetColor(0x118822FF) : (hoverWood ? GetColor(0x22AA33FF) : GetColor(0x054410FF));
            DrawRectangleRounded(btnWood, 0.3f, 10, colorWood);
            DrawRectangleRoundedLines(btnWood, 0.3f, 10, WHITE);
            DrawText("WOOD SKILL", (int)btnWood.x + 28, (int)btnWood.y + 15, 15, WHITE);

            // Vẽ nút Hệ Thổ (dùng chiêu Lôi/Electric dưới nền)
            Color colorEarth = activeSkill == SKILL_ELECTRIC ? GetColor(0x8B5A2BFF) : (hoverEarth ? GetColor(0xCD853FFF) : GetColor(0x5C3A21FF));
            DrawRectangleRounded(btnEarth, 0.3f, 10, colorEarth);
            DrawRectangleRoundedLines(btnEarth, 0.3f, 10, WHITE);
            DrawText("EARTH SKILL", (int)btnEarth.x + 28, (int)btnEarth.y + 15, 15, WHITE);
            
            // Vẽ bảng chọn số lượng (Quantity Selector)
            DrawText("Quantity:", 770, 30, 16, LIGHTGRAY);
            for (int i = 0; i < 5; i++) {
                bool isSelected = (selectedQty == (i + 1));
                bool isHover = CheckCollisionPointRec(mousePos, rectQty[i]);
                Color btnCol = isSelected ? PURPLE : (isHover ? DARKPURPLE : DARKGRAY);
                DrawRectangleRounded(rectQty[i], 0.2f, 10, btnCol);
                DrawRectangleRoundedLines(rectQty[i], 0.2f, 10, WHITE);
                DrawText(TextFormat("%d", i + 1), (int)rectQty[i].x + 20, (int)rectQty[i].y + 10, 15, WHITE);
            }

            // Vẽ bảng chọn kích thước (Size Selector)
            DrawText("Size:", 770, 80, 16, LIGHTGRAY);
            float sizes[3] = { 1.0f, 1.5f, 2.0f };
            for (int i = 0; i < 3; i++) {
                bool isSelected = (selectedSize == sizes[i]);
                bool isHover = CheckCollisionPointRec(mousePos, rectSize[i]);
                Color btnCol = isSelected ? ORANGE : (isHover ? GOLD : DARKGRAY);
                DrawRectangleRounded(rectSize[i], 0.2f, 10, btnCol);
                DrawRectangleRoundedLines(rectSize[i], 0.2f, 10, WHITE);
                DrawText(TextFormat("%.1fx", sizes[i]), (int)rectSize[i].x + 25, (int)rectSize[i].y + 10, 15, WHITE);
            }

            // Text hướng dẫn
            const char* modeStr = "STATIC (STATIONARY)";
            if (enemyMode == ENEMY_CHASE) modeStr = "CHASING PLAYER";
            else if (enemyMode == ENEMY_PATROL) modeStr = "CIRCULAR PATROL";

            DrawText("Controls: WASD/Arrows to Move | Space to Jump | Left Shift to Dash (leaves elemental trail)", 20, 80, 16, LIGHTGRAY);
            DrawText("Click anywhere to shoot (supports mid-air casting) | Press P to cycle Enemy Mode", 20, 105, 16, LIGHTGRAY);
            DrawText(TextFormat("Params: Qty = %d | Size = %.1fx | Enemy Mode = %s", selectedQty, selectedSize, modeStr), 20, 130, 16, GetColor(0x55DD66FF));
            DrawText("Selected element affects skills & dash trail visuals (character/enemy/pillars have shadows)", 20, 155, 16, GetColor(0xBA55D3FF));

        EndDrawing();
    }

    // Dọn dẹp RAM/VRAM qua hệ thống quản lý
    UnloadSkillManager();
    CloseWindow();
    
    return 0;
}