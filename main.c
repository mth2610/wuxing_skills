#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include "skill_manager.h"
#include "raymath.h"
#include "sword_rain_skill.h"

// Biến camera toàn cục để chia sẻ với các chiêu thức
Camera3D camera = { 0 };

typedef struct {
    Vector3 position;
    Color color;
    float radius;
    float lifetime;
    float maxLifetime;
    bool active;
} DashParticle;

#define MAX_DASH_PARTICLES 150
static DashParticle dashParticles[MAX_DASH_PARTICLES];

static void SpawnDashParticle(Vector3 pos, Color color, float radius, float lifetime) {
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

// Hàm vẽ vòng tròn phẳng dưới đất 3D (trục X-Z)
static void DrawCircleOutline3D(Vector3 center, float radius, Color color) {
    int segments = 64;
    Vector3 prevPt = { center.x + radius, center.y, center.z };
    for (int i = 1; i <= segments; i++) {
        float angle = ((float)i / segments) * 2.0f * PI;
        Vector3 currPt = {
            center.x + cosf(angle) * radius,
            center.y,
            center.z + sinf(angle) * radius
        };
        DrawLine3D(prevPt, currPt, color);
        prevPt = currPt;
    }
}

// Vẽ bóng đổ phẳng hình elip/tròn dưới mặt đất Y = 0.01f (để tránh z-fighting)
static void DrawShadow3D(Vector3 pos, float radius, float alpha) {
    int segments = 16;
    rlBegin(RL_TRIANGLES);
    Color color = ColorAlpha(BLACK, alpha);
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p0 = { pos.x, 0.01f, pos.z };
        Vector3 p1 = { pos.x + cosf(a1) * radius, 0.01f, pos.z + sinf(a1) * radius };
        Vector3 p2 = { pos.x + cosf(a2) * radius, 0.01f, pos.z + sinf(a2) * radius };
        
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(p1.x, p1.y, p1.z);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(p2.x, p2.y, p2.z);
    }
    rlEnd();
}

// Hàm vẽ nhân vật 3D ghép từ các khối nguyên thủy
static void DrawCharacter3D(Vector3 position, float radius, Color skinCol, Color clothesCol, Color outlineCol, bool isPlayer, Vector3 targetPos) {
    // 1. Chân (feet)
    Vector3 leftFoot = { position.x - 8.0f, position.y, position.z };
    Vector3 rightFoot = { position.x + 8.0f, position.y, position.z };
    DrawSphere(leftFoot, 4.0f, outlineCol);
    DrawSphere(rightFoot, 4.0f, outlineCol);

    // 2. Thân (body cylinder)
    Vector3 bodyPos = { position.x, position.y + 18.0f, position.z };
    DrawCylinder(bodyPos, 12.0f, 12.0f, 30.0f, 8, clothesCol);
    DrawCylinderWires(bodyPos, 12.0f, 12.0f, 30.0f, 8, outlineCol);

    // 3. Đầu (head)
    Vector3 headPos = { position.x, position.y + 38.0f, position.z };
    DrawSphere(headPos, 9.0f, skinCol);
    DrawSphereWires(headPos, 9.0f, 6, 6, outlineCol);

    // 4. Tay (arms)
    if (isPlayer) {
        Vector3 armOrigin = { position.x, position.y + 25.0f, position.z };
        Vector3 dir = Vector3Normalize(Vector3Subtract(targetPos, armOrigin));
        Vector3 handPos = Vector3Add(armOrigin, Vector3Scale(dir, 22.0f));

        DrawLine3D(armOrigin, handPos, clothesCol);
        DrawSphere(handPos, 4.0f, skinCol);
        DrawSphereWires(handPos, 4.0f, 4, 4, outlineCol);

        // Tay còn lại
        Vector3 restHand = { position.x - 14.0f, position.y + 14.0f, position.z };
        DrawLine3D((Vector3){ position.x - 12.0f, position.y + 24.0f, position.z }, restHand, clothesCol);
        DrawSphere(restHand, 3.5f, skinCol);
        DrawSphereWires(restHand, 3.5f, 4, 4, outlineCol);
    } else {
        Vector3 leftHand = { position.x - 14.0f, position.y + 14.0f, position.z };
        Vector3 rightHand = { position.x + 14.0f, position.y + 14.0f, position.z };
        DrawLine3D((Vector3){ position.x - 12.0f, position.y + 24.0f, position.z }, leftHand, clothesCol);
        DrawLine3D((Vector3){ position.x + 12.0f, position.y + 24.0f, position.z }, rightHand, clothesCol);
        DrawSphere(leftHand, 3.5f, skinCol);
        DrawSphere(rightHand, 3.5f, skinCol);
        DrawSphereWires(leftHand, 3.5f, 4, 4, outlineCol);
        DrawSphereWires(rightHand, 3.5f, 4, 4, outlineCol);
    }
}

int main(void) {
    const int screenWidth = 1200;
    const int screenHeight = 700;
    InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");

    // Khởi tạo hệ thống quản lý chiêu thức
    InitSkillManager(screenWidth, screenHeight);
    InitSwordRainSkill(); 

    // Cấu hình Camera3D
    camera.position = (Vector3){ 600.0f, 500.0f, 840.0f }; // Góc isometric nhìn từ trên cao xuống
    camera.target = (Vector3){ 600.0f, 0.0f, 440.0f };    // Tiêu điểm tâm võ đài
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Vị trí người chơi dạng Vector3
    Vector3 characterPos = { 130.0f, 0.0f, 350.0f };
    int activeSkillIndex = 0; 

    // Vị trí quái Enemy
    Vector3 enemyPos = { 900.0f, 0.0f, 350.0f };
    float enemyOscillationScale = 1.0f;

    int selectedQty = 3; 
    float selectedSize = 1.0f; 
    float sizes[3] = { 1.0f, 1.5f, 2.0f };

    // Các biến trạng thái lướt (Dash / Khinh Công)
    float dashCooldown = 0.0f;
    float dashTimer = 0.0f;
    Vector3 dashDir = { 0.0f, 0.0f, 0.0f };
    bool isDashing = false;

    // Các biến vật lý Nhảy
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

    // Vị trí và kích thước các cột đá chướng ngại vật trong không gian 3D
    typedef struct {
        Vector3 position;
        float radius;
    } StonePillar;
    
    #define NUM_PILLARS 3
    StonePillar pillars[NUM_PILLARS] = {
        { { 400.0f, 0.0f, 320.0f }, 25.0f },
        { { 800.0f, 0.0f, 520.0f }, 30.0f },
        { { 600.0f, 0.0f, 260.0f }, 20.0f }
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

    // Khung UI chọn Anchor và Path ở góc trên bên phải
    CastAnchorType selectedAnchor = CAST_ANCHOR_TARGET;
    CastPathType selectedPath = CAST_PATH_PROJECTILE;
    bool showPortals = true;

    Rectangle rectAnchor[2];
    for (int i = 0; i < 2; i++) {
        rectAnchor[i] = (Rectangle){ 870 + i * 150, 120, 140, 35 };
    }
    Rectangle rectPath[3];
    for (int i = 0; i < 3; i++) {
        rectPath[i] = (Rectangle){ 870 + i * 100, 170, 95, 35 };
    }
    Rectangle rectPortalToggle = { 870, 220, 200, 35 };

    // Khung UI các nút bấm chiêu thức
    Rectangle skillButtons[32];
    int skillCount = GetRegisteredSkillCount();
    if (skillCount > 32) skillCount = 32;
    for (int i = 0; i < skillCount; i++) {
        skillButtons[i] = (Rectangle){ 20 + i * 135, 20, 125, 45 };
    }

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 mousePos = GetMousePosition();
        float time = GetTime();

        // 1. NGẮM BẮN CHUỘT 3D (Tia ngắm chuột giao cắt với mặt phẳng Y = 0)
        Ray mouseRay = GetScreenToWorldRay(mousePos, camera);
        Vector3 mouseTarget3D = { 0.0f, 0.0f, 0.0f };
        if (mouseRay.direction.y != 0.0f) {
            float t = -mouseRay.position.y / mouseRay.direction.y;
            mouseTarget3D = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));
        }

        // 2. DỊCH CHUYỂN NHÂN VẬT & KHINH CÔNG (DASH & JUMP)
        Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) moveDir.z -= 1.0f; // Đi xa camera
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) moveDir.z += 1.0f; // Lại gần camera
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) moveDir.x -= 1.0f; // Sang trái
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) moveDir.x += 1.0f; // Sang phải

        float moveLen = Vector3Length(moveDir);
        if (moveLen > 0.0f) {
            moveDir = Vector3Scale(moveDir, 1.0f / moveLen);
        }

        if (dashCooldown > 0.0f) {
            dashCooldown -= dt;
        }

        // Cập nhật nhảy khinh công (Trục Y)
        if (characterPos.y > 0.0f || charZVelocity > 0.0f) {
            charZVelocity -= gravity * dt;
            characterPos.y += charZVelocity * dt;
            if (characterPos.y <= 0.0f) {
                characterPos.y = 0.0f;
                charZVelocity = 0.0f;
            }
        }

        // Kích hoạt nhảy khi nhấn Space
        if (IsKeyPressed(KEY_SPACE) && characterPos.y == 0.0f) {
            charZVelocity = 550.0f;
        }

        if (isDashing) {
            dashTimer -= dt;
            float dashSpeed = 1200.0f;
            characterPos.x += dashDir.x * dashSpeed * dt;
            characterPos.z += dashDir.z * dashSpeed * dt;

            Color trailColor = GetRegisteredSkillColor(activeSkillIndex);
            SpawnDashParticle(characterPos, trailColor, 12.0f, 0.35f);

            if (dashTimer <= 0.0f) {
                isDashing = false;
            }
        } else {
            float moveSpeed = 300.0f;
            characterPos.x += moveDir.x * moveSpeed * dt;
            characterPos.z += moveDir.z * moveSpeed * dt;

            // Kích hoạt lướt (Dash)
            if (IsKeyPressed(KEY_LEFT_SHIFT) && dashCooldown <= 0.0f) {
                isDashing = true;
                dashTimer = 0.15f;
                dashCooldown = 0.8f;
                
                if (Vector3Length(moveDir) == 0.0f) {
                    Vector3 diff = Vector3Subtract(mouseTarget3D, characterPos);
                    diff.y = 0.0f;
                    dashDir = Vector3Normalize(diff);
                } else {
                    dashDir = moveDir;
                }
            }
        }

        // Ràng buộc vị trí nhân vật bên trong võ đài hình tròn X-Z
        Vector3 arenaCenter = { 600.0f, 0.0f, 440.0f };
        float arenaRadius = 360.0f;
        float charRadius = 30.0f;

        Vector3 toChar = Vector3Subtract(characterPos, arenaCenter);
        toChar.y = 0.0f;
        float distToCenter = Vector3Length(toChar);
        if (distToCenter > arenaRadius - charRadius) {
            Vector3 normal = Vector3Normalize(toChar);
            characterPos.x = arenaCenter.x + normal.x * (arenaRadius - charRadius);
            characterPos.z = arenaCenter.z + normal.z * (arenaRadius - charRadius);
        }

        // Va chạm vật lý X-Z với 3 cột đá chướng ngại vật
        for (int i = 0; i < NUM_PILLARS; i++) {
            Vector3 diff = Vector3Subtract(characterPos, pillars[i].position);
            diff.y = 0.0f;
            float dist = Vector3Length(diff);
            float minDist = charRadius + pillars[i].radius;
            if (dist < minDist) {
                Vector3 pushDir = Vector3Normalize(diff);
                characterPos.x = pillars[i].position.x + pushDir.x * minDist;
                characterPos.z = pillars[i].position.z + pushDir.z * minDist;
            }
        }

        // Cập nhật hạt lướt
        for (int i = 0; i < MAX_DASH_PARTICLES; i++) {
            if (dashParticles[i].active) {
                dashParticles[i].lifetime -= dt;
                if (dashParticles[i].lifetime <= 0.0f) {
                    dashParticles[i].active = false;
                }
            }
        }

        // 3. CẬP NHẬT ENEMY THEO CHẾ ĐỘ DI CHUYỂN
        if (IsKeyPressed(KEY_P)) {
            enemyMode = (enemyMode + 1) % 3;
        }

        float currentEnemySpeed = enemySpeed;
        if (IsEnemySlowed()) {
            currentEnemySpeed *= 0.4f; 
        }
        if (IsAnySkillCoiling()) {
            currentEnemySpeed = 0.0f; 
        }

        if (enemyMode == ENEMY_STATIC) {
            if (IsAnySkillCoiling()) {
                enemyOscillationScale += (0.0f - enemyOscillationScale) * 9.0f * dt;
            } else {
                enemyOscillationScale += (1.0f - enemyOscillationScale) * 2.0f * dt;
            }
            enemyPos.x = 900.0f;
            float oscillationFreq = IsEnemySlowed() ? 0.8f : 2.5f;
            enemyPos.z = 350.0f + sinf(time * oscillationFreq) * 100.0f * enemyOscillationScale;
            enemyPos.y = 0.0f;
        } 
        else if (enemyMode == ENEMY_CHASE) {
            if (currentEnemySpeed > 0.0f) {
                Vector3 toPlayer = Vector3Subtract(characterPos, enemyPos);
                toPlayer.y = 0.0f;
                float dist = Vector3Length(toPlayer);
                if (dist > 15.0f) {
                    Vector3 dir = Vector3Scale(Vector3Normalize(toPlayer), currentEnemySpeed * dt);
                    enemyPos = Vector3Add(enemyPos, dir);
                }
            }
        } 
        else if (enemyMode == ENEMY_PATROL) {
            if (currentEnemySpeed > 0.0f) {
                float rotSpeed = (currentEnemySpeed / 200.0f);
                patrolAngle += rotSpeed * dt;
            }
            Vector3 patrolCenter = { 600.0f, 0.0f, 440.0f };
            enemyPos.x = patrolCenter.x + cosf(patrolAngle) * 250.0f;
            enemyPos.z = patrolCenter.z + sinf(patrolAngle) * 200.0f;
            enemyPos.y = 0.0f;
        }

        // Ràng buộc vị trí enemy bên trong võ đài hình tròn X-Z
        float enemyRadius = 35.0f;
        Vector3 toEnemy = Vector3Subtract(enemyPos, arenaCenter);
        toEnemy.y = 0.0f;
        if (Vector3Length(toEnemy) > arenaRadius - enemyRadius) {
            Vector3 normal = Vector3Normalize(toEnemy);
            enemyPos.x = arenaCenter.x + normal.x * (arenaRadius - enemyRadius);
            enemyPos.z = arenaCenter.z + normal.z * (arenaRadius - enemyRadius);
        }

        // Va chạm vật lý của enemy với 3 cột đá
        for (int i = 0; i < NUM_PILLARS; i++) {
            Vector3 diff = Vector3Subtract(enemyPos, pillars[i].position);
            diff.y = 0.0f;
            float dist = Vector3Length(diff);
            float minDist = enemyRadius + pillars[i].radius;
            if (dist < minDist) {
                Vector3 pushDir = Vector3Normalize(diff);
                enemyPos.x = pillars[i].position.x + pushDir.x * minDist;
                enemyPos.z = pillars[i].position.z + pushDir.z * minDist;
            }
        }

        // Choáng lắc lư giật điện
        if (IsAnySkillShocking()) {
            enemyPos.x += (float)GetRandomValue(-6, 6);
            enemyPos.z += (float)GetRandomValue(-6, 6);
        }

        // 4. KIỂM TRA TƯƠNG TÁC UI
        int hoverSkillIndex = -1;
        for (int i = 0; i < skillCount; i++) {
            if (CheckCollisionPointRec(mousePos, skillButtons[i])) {
                hoverSkillIndex = i;
                break;
            }
        }
        bool clickedOnUI = false;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // Kiểm tra click chọn Quantity
            for (int i = 0; i < 5; i++) {
                if (CheckCollisionPointRec(mousePos, rectQty[i])) {
                    selectedQty = i + 1;
                    clickedOnUI = true;
                }
            }
            // Kiểm tra click chọn Size
            for (int i = 0; i < 3; i++) {
                if (CheckCollisionPointRec(mousePos, rectSize[i])) {
                    selectedSize = sizes[i];
                    clickedOnUI = true;
                }
            }

            // Kiểm tra click chọn Anchor
            for (int i = 0; i < 2; i++) {
                if (CheckCollisionPointRec(mousePos, rectAnchor[i])) {
                    selectedAnchor = (CastAnchorType)i;
                    clickedOnUI = true;
                }
            }
            // Kiểm tra click chọn Path
            for (int i = 0; i < 3; i++) {
                if (CheckCollisionPointRec(mousePos, rectPath[i])) {
                    selectedPath = (CastPathType)i;
                    clickedOnUI = true;
                }
            }

            // Kiểm tra click Toggle Portals
            if (CheckCollisionPointRec(mousePos, rectPortalToggle)) {
                showPortals = !showPortals;
                clickedOnUI = true;
            }

            // Click đổi chiêu hệ ngũ hành
            if (hoverSkillIndex != -1) {
                activeSkillIndex = hoverSkillIndex;
                clickedOnUI = true;
            }

            // 5. CLICK TRÊN TRƯỜNG ĐẤU -> BẮN CHIÊU THỨC Tới mục tiêu 3D
            if (!clickedOnUI) {
                // Thiết lập tham số chiêu thức ngũ hành
                SkillParams params = {0};
                params.level = 1;
                params.milestone = 1;
                params.sizeScale = selectedSize;
                params.damage = 100.0f;
                params.quantity = selectedQty;
                params.anchorType = selectedAnchor;
                params.pathType = selectedPath;
                params.showPortal = showPortals;

                CastSkill(activeSkillIndex, characterPos, mouseTarget3D, params);
            }
        }

        // Cập nhật hệ thống chiêu thức ngũ hành
        UpdateSkillManager(dt, enemyPos, 35.0f);

        // --- VẼ MÀN HÌNH ---
        BeginDrawing();
            ClearBackground(GetColor(0x111111FF)); 

            // RENDER 3D MODE
            BeginMode3D(camera);
                // Vẽ nền võ đài 3D
                DrawCylinder((Vector3){ 600.0f, -0.5f, 440.0f }, 360.0f, 360.0f, 1.0f, 48, GetColor(0x28282FFF));
                DrawCylinder((Vector3){ 600.0f, -1.5f, 440.0f }, 365.0f, 365.0f, 1.0f, 48, GetColor(0x1B1B1FFF));
                
                // Vẽ các đường hoa văn võ đài ngũ hành bằng các đường line 3D
                DrawCircleOutline3D((Vector3){ 600.0f, 0.02f, 440.0f }, 320.0f, GetColor(0x3E3E4FFF));
                DrawCircleOutline3D((Vector3){ 600.0f, 0.02f, 440.0f }, 240.0f, GetColor(0x33333FFF));
                DrawCircleOutline3D((Vector3){ 600.0f, 0.02f, 440.0f }, 80.0f, GetColor(0x3A3A4AFF));
                
                DrawLine3D((Vector3){ 600.0f - 360.0f, 0.02f, 440.0f }, (Vector3){ 600.0f + 360.0f, 0.02f, 440.0f }, GetColor(0x2E2E39FF));
                DrawLine3D((Vector3){ 600.0f, 0.02f, 440.0f - 360.0f }, (Vector3){ 600.0f, 0.02f, 440.0f + 360.0f }, GetColor(0x2E2E39FF));

                // Vẽ bóng đổ chân nhân vật và cột đá
                for (int i = 0; i < NUM_PILLARS; i++) {
                    DrawShadow3D(pillars[i].position, pillars[i].radius * 1.1f, 0.5f);
                }

                // Bóng đổ Enemy
                DrawShadow3D(enemyPos, 36.0f, 0.5f);

                // Bóng đổ Player (co giãn theo độ cao nhảy Y)
                float shadowScale = 1.0f - (characterPos.y / 350.0f);
                if (shadowScale < 0.2f) shadowScale = 0.2f;
                DrawShadow3D((Vector3){ characterPos.x, 0.0f, characterPos.z }, 32.0f * shadowScale, 0.5f * shadowScale);

                // Vẽ đường thẳng độ cao biểu diễn vị trí trên không trung
                if (characterPos.y > 5.0f) {
                    DrawLine3D((Vector3){ characterPos.x, 0.0f, characterPos.z }, characterPos, ColorAlpha(GRAY, 0.5f));
                }

                // Vẽ vòng định vị chân luôn ghim sát mặt sàn 3D
                DrawCircleOutline3D((Vector3){ characterPos.x, 0.02f, characterPos.z }, 25.0f, ColorAlpha(LIME, 0.6f));
                DrawCircleOutline3D((Vector3){ enemyPos.x, 0.02f, enemyPos.z }, 30.0f, ColorAlpha(RED, 0.6f));

                // Vẽ 3 cột đá bằng cylinder 3D
                for (int i = 0; i < NUM_PILLARS; i++) {
                    float pillarHeight = pillars[i].radius * 2.5f;
                    Vector3 center = { pillars[i].position.x, pillars[i].position.y + pillarHeight * 0.5f, pillars[i].position.z };
                    DrawCylinder(center, pillars[i].radius, pillars[i].radius, pillarHeight, 16, GetColor(0x3B3B42FF));
                    DrawCylinderWires(center, pillars[i].radius, pillars[i].radius, pillarHeight, 16, GetColor(0x73737CFF));
                }

                // Vẽ Enemy và Player trong không gian 3D
                DrawCharacter3D(enemyPos, 30.0f, GetColor(0xFFC0CBFF), IsEnemySlowed() ? GetColor(0x1B4F72FF) : (IsEnemyBurning() ? RED : GetColor(0x8B2500FF)), IsEnemySlowed() ? SKYBLUE : (IsEnemyBurning() ? YELLOW : GetColor(0xFF5500FF)), false, (Vector3){0});
                
                DrawCharacter3D(characterPos, 25.0f, GetColor(0xFFD39BFF), GetColor(0x3B5998FF), isDashing ? GetRegisteredSkillColor(activeSkillIndex) : GetColor(0xCCCCCCFF), true, mouseTarget3D);

            EndMode3D();

            // RENDER 2D SCREEN OVERLAYS
            // Vẽ các hạt dư ảnh lướt dash
            for (int i = 0; i < MAX_DASH_PARTICLES; i++) {
                if (dashParticles[i].active) {
                    float lifeRatio = dashParticles[i].lifetime / dashParticles[i].maxLifetime;
                    Color pCol = ColorAlpha(dashParticles[i].color, lifeRatio * 0.5f);
                    Vector2 screenPos = GetWorldToScreen(dashParticles[i].position, camera);
                    DrawCircleV(screenPos, dashParticles[i].radius * (0.3f + 0.7f * lifeRatio), pCol);
                }
            }

            // Vẽ các chiêu thức đang bay và chữ sát thương chiếu từ 3D ra màn hình
            DrawSkillManager();

            // Nhãn tên Enemy trên đầu quái
            Vector2 enemyScreenHead = GetWorldToScreen((Vector3){ enemyPos.x, enemyPos.y + 55.0f, enemyPos.z }, camera);
            DrawText("ENEMY", (int)enemyScreenHead.x - 22, (int)enemyScreenHead.y, 12, WHITE);

            // --- VẼ GIAO DIỆN CHỌN SKILL (UI) ---
            for (int i = 0; i < skillCount; i++) {
                bool isSelected = (activeSkillIndex == i);
                bool isHover = (hoverSkillIndex == i);
                Color baseColor = GetRegisteredSkillColor(i);
                
                Color btnColor;
                if (isSelected) {
                    btnColor = baseColor;
                } else if (isHover) {
                    btnColor = ColorAlpha(baseColor, 0.6f);
                } else {
                    btnColor = ColorAlpha(baseColor, 0.3f);
                }
                
                DrawRectangleRounded(skillButtons[i], 0.3f, 10, btnColor);
                DrawRectangleRoundedLines(skillButtons[i], 0.3f, 10, WHITE);
                
                const char* skillName = GetRegisteredSkillName(i);
                char btnText[64];
                snprintf(btnText, sizeof(btnText), "%s SKILL", skillName);
                
                int fontSize = 12;
                int textWidth = MeasureText(btnText, fontSize);
                DrawText(btnText, (int)(skillButtons[i].x + (skillButtons[i].width - textWidth) / 2), (int)(skillButtons[i].y + 15), fontSize, WHITE);
            }
            
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
            for (int i = 0; i < 3; i++) {
                bool isSelected = (selectedSize == sizes[i]);
                bool isHover = CheckCollisionPointRec(mousePos, rectSize[i]);
                Color btnCol = isSelected ? ORANGE : (isHover ? GOLD : DARKGRAY);
                DrawRectangleRounded(rectSize[i], 0.2f, 10, btnCol);
                DrawRectangleRoundedLines(rectSize[i], 0.2f, 10, WHITE);
                DrawText(TextFormat("%.1fx", sizes[i]), (int)rectSize[i].x + 25, (int)rectSize[i].y + 10, 15, WHITE);
            }

            // Vẽ bảng chọn Anchor (Caster / Target)
            DrawText("Anchor:", 770, 130, 16, LIGHTGRAY);
            const char* anchorNames[2] = { "CASTER", "TARGET" };
            for (int i = 0; i < 2; i++) {
                bool isSelected = (selectedAnchor == (CastAnchorType)i);
                bool isHover = CheckCollisionPointRec(mousePos, rectAnchor[i]);
                Color btnCol = isSelected ? RED : (isHover ? MAROON : DARKGRAY);
                DrawRectangleRounded(rectAnchor[i], 0.2f, 10, btnCol);
                DrawRectangleRoundedLines(rectAnchor[i], 0.2f, 10, WHITE);
                DrawText(anchorNames[i], (int)rectAnchor[i].x + 35, (int)rectAnchor[i].y + 10, 14, WHITE);
            }

            // Vẽ bảng chọn Path (Projectile / Ground / Sky)
            DrawText("Path:", 770, 180, 16, LIGHTGRAY);
            const char* pathNames[3] = { "PROJECTILE", "GROUND", "SKY" };
            for (int i = 0; i < 3; i++) {
                bool isSelected = (selectedPath == (CastPathType)i);
                bool isHover = CheckCollisionPointRec(mousePos, rectPath[i]);
                Color btnCol = isSelected ? SKYBLUE : (isHover ? DARKBLUE : DARKGRAY);
                DrawRectangleRounded(rectPath[i], 0.2f, 10, btnCol);
                DrawRectangleRoundedLines(rectPath[i], 0.2f, 10, WHITE);
                int textOffset = (i == 0) ? 10 : ((i == 1) ? 22 : 32);
                DrawText(pathNames[i], (int)rectPath[i].x + textOffset, (int)rectPath[i].y + 10, 13, WHITE);
            }

            // Vẽ nút bật/tắt Cổng Phép Thuật (Portals Toggle)
            DrawText("Effects:", 770, 230, 16, LIGHTGRAY);
            bool hoverToggle = CheckCollisionPointRec(mousePos, rectPortalToggle);
            Color toggleCol = showPortals ? (hoverToggle ? GetColor(0x22AA33FF) : GetColor(0x118822FF)) : 
                                            (hoverToggle ? MAROON : DARKGRAY);
            DrawRectangleRounded(rectPortalToggle, 0.2f, 10, toggleCol);
            DrawRectangleRoundedLines(rectPortalToggle, 0.2f, 10, WHITE);
            DrawText(showPortals ? "PORTALS: ENABLED" : "PORTALS: DISABLED", (int)rectPortalToggle.x + 20, (int)rectPortalToggle.y + 10, 14, WHITE);

            // Bảng hướng dẫn & thông số
            const char* modeStr = "STATIC (STATIONARY)";
            if (enemyMode == ENEMY_CHASE) modeStr = "CHASING PLAYER";
            else if (enemyMode == ENEMY_PATROL) modeStr = "CIRCULAR PATROL";

            const char* anchorStr = (selectedAnchor == CAST_ANCHOR_CASTER) ? "CASTER" : "TARGET";
            const char* pathStr = (selectedPath == CAST_PATH_PROJECTILE) ? "PROJECTILE" : 
                                  ((selectedPath == CAST_PATH_UNDERGROUND) ? "GROUND" : "SKY");
            const char* portalStr = showPortals ? "ON" : "OFF";

            DrawText("Controls: WASD/Arrows to Move | Space to Jump | Left Shift to Dash (leaves elemental trail)", 20, 80, 16, LIGHTGRAY);
            DrawText("Click anywhere to shoot (supports mid-air casting) | Press P to cycle Enemy Mode", 20, 105, 16, LIGHTGRAY);
            DrawText(TextFormat("Params: Qty = %d | Size = %.1fx | Anchor = %s | Path = %s | Portals = %s | Enemy = %s", 
                                selectedQty, selectedSize, anchorStr, pathStr, portalStr, modeStr), 20, 130, 16, GetColor(0x55DD66FF));
            DrawText("Selected element affects skills & dash trail visuals (character/enemy/pillars have shadows)", 20, 155, 16, GetColor(0xBA55D3FF));

        EndDrawing();
    }

    // Dọn dẹp bộ nhớ RAM/VRAM
    UnloadSkillManager();
    CloseWindow();
    
    return 0;
}