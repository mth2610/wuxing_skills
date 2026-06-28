#include "sandbox/sandbox_core.h"
#include "core/skill_manager.h"
#include "skills/taiji/wind_storm/wind_skill.h"
#include "environment/environment_system.h"
#include "raymath.h"
#include "rlgl.h"

// Biến môi trường
static const Vector3 arenaCenter = { 600.0f, 0.0f, 440.0f };
static const float arenaRadius = 1800.0f;
static const float gravity = 1500.0f;

typedef struct {
    Vector3 position;
    float radius;
} StonePillar;

#define NUM_PILLARS 3
static StonePillar pillars[NUM_PILLARS] = {
    { { 400.0f, 0.0f, 320.0f }, 25.0f },
    { { 800.0f, 0.0f, 520.0f }, 30.0f },
    { { 600.0f, 0.0f, 260.0f }, 20.0f }
};

// Hàm vẽ helper
static void DrawCircleSolid3D(Vector3 center, float radius, Color color) {
    int segments = 128;
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    rlColor4ub(color.r, color.g, color.b, color.a);
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, center.y, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, center.y, center.z + sinf(a2) * radius };
        rlVertex3f(center.x, center.y, center.z);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();
}

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

static void DrawCharacter3D(Vector3 position, float radius, Color skinCol, Color clothesCol, Color outlineCol, bool isPlayer, Vector3 targetPos) {
    Vector3 leftFoot = { position.x - 8.0f, position.y, position.z };
    Vector3 rightFoot = { position.x + 8.0f, position.y, position.z };
    DrawSphere(leftFoot, 4.0f, outlineCol);
    DrawSphere(rightFoot, 4.0f, outlineCol);

    Vector3 bodyPos = { position.x, position.y + 18.0f, position.z };
    DrawCylinder(bodyPos, 12.0f, 12.0f, 30.0f, 8, clothesCol);
    DrawCylinderWires(bodyPos, 12.0f, 12.0f, 30.0f, 8, outlineCol);

    Vector3 headPos = { position.x, position.y + 38.0f, position.z };
    DrawSphere(headPos, 9.0f, skinCol);
    DrawSphereWires(headPos, 9.0f, 6, 6, outlineCol);

    if (isPlayer) {
        Vector3 armOrigin = { position.x, position.y + 25.0f, position.z };
        Vector3 dir = Vector3Normalize(Vector3Subtract(targetPos, armOrigin));
        Vector3 handPos = Vector3Add(armOrigin, Vector3Scale(dir, 22.0f));
        DrawLine3D(armOrigin, handPos, clothesCol);
        DrawSphere(handPos, 4.0f, skinCol);
        DrawSphereWires(handPos, 4.0f, 4, 4, outlineCol);

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

void InitSandbox(PlayerEntity* player, EnemyEntity* enemy) {
    // Camera
    camera.position = (Vector3){ 600.0f, 500.0f, 840.0f }; 
    camera.target = (Vector3){ 600.0f, 0.0f, 440.0f };    
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Cấu hình Player
    player->position = (Vector3){ 130.0f, 0.0f, 350.0f };
    player->radius = 30.0f;
    player->dashCooldown = 0.0f;
    player->dashTimer = 0.0f;
    player->dashDir = (Vector3){ 0 };
    player->isDashing = false;
    player->zVelocity = 0.0f;
    player->jumpCount = 0;

    // Cấu hình Enemy
    enemy->position = (Vector3){ 900.0f, 0.0f, 350.0f };
    enemy->radius = 35.0f;
    enemy->mode = ENEMY_STATIC;
    enemy->speed = 120.0f;
    enemy->patrolAngle = 0.0f;
    enemy->oscillationScale = 1.0f;
    enemy->knockbackVelocity = (Vector3){ 0 };
}
// Biến toàn cục để điều khiển camera
static float g_cameraAngle = 0.0f;
static float g_camDist = 600.0f;
static float g_camHeight = 450.0f;

void UpdateSandbox(PlayerEntity* player, EnemyEntity* enemy, float dt, UIPanelState* uiState, Vector3* outMouseTarget) {
    Vector2 mousePos = GetMousePosition();

    // 1. NGẮM BẮN CHUỘT 3D
    Ray mouseRay = GetScreenToWorldRay(mousePos, camera);
    if (mouseRay.direction.y != 0.0f) {
        float t = -mouseRay.position.y / mouseRay.direction.y;
        *outMouseTarget = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));
    } else {
        *outMouseTarget = (Vector3){0};
    }

    // 2. DI CHUYỂN PLAYER (DASH & JUMP)
    float inputX = 0.0f;
    float inputZ = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) inputZ -= 1.0f; 
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) inputZ += 1.0f; 
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) inputX -= 1.0f; 
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) inputX += 1.0f; 

    // Chuyển đổi di chuyển theo hướng camera thực tế trên mặt phẳng XZ
    Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
    if (inputX != 0.0f || inputZ != 0.0f) {
        Vector3 camForward = Vector3Subtract(camera.target, camera.position);
        camForward.y = 0.0f;
        camForward = Vector3Normalize(camForward);

        Vector3 camRight = Vector3CrossProduct(camForward, camera.up);
        camRight.y = 0.0f;
        camRight = Vector3Normalize(camRight);

        // inputZ: -1.0f (W) -> đi tới (camForward), 1.0f (S) -> đi lùi (-camForward)
        // inputX: -1.0f (A) -> đi trái (-camRight), 1.0f (D) -> đi phải (camRight)
        moveDir.x = camForward.x * -inputZ + camRight.x * inputX;
        moveDir.z = camForward.z * -inputZ + camRight.z * inputX;
        moveDir = Vector3Normalize(moveDir);
    }

    if (player->dashCooldown > 0.0f) player->dashCooldown -= dt;

    float currentGroundY = 0.0f;
    for (int i = 0; i < NUM_PILLARS; i++) {
        Vector3 diff = Vector3Subtract(player->position, pillars[i].position);
        diff.y = 0.0f;
        float dist = Vector3Length(diff);
        float pillarHeight = pillars[i].radius * 2.5f;
        float checkRadius = player->radius + pillars[i].radius;
        
        if (dist < checkRadius) {
            if (i == 0 && player->position.y >= pillarHeight - 15.0f && player->zVelocity <= 0.0f) {
                currentGroundY = pillarHeight;
            } else {
                Vector3 pushDir = Vector3Normalize(diff);
                player->position.x = pillars[i].position.x + pushDir.x * checkRadius;
                player->position.z = pillars[i].position.z + pushDir.z * checkRadius;
            }
        }
    }

    bool isOnSolid = (player->position.y <= currentGroundY);
    if (!isOnSolid || player->zVelocity > 0.0f) {
        player->zVelocity -= gravity * dt;
        player->position.y += player->zVelocity * dt;
        if (player->position.y <= currentGroundY) {
            player->position.y = currentGroundY;
            player->zVelocity = 0.0f;
            player->jumpCount = 0;
        }
    } else {
        player->zVelocity = 0.0f;
    }

    if (IsKeyPressed(KEY_SPACE)) {
        if (player->position.y <= currentGroundY + 1.0f) {
            player->zVelocity = 550.0f;
            player->jumpCount = 1;
        } else if (player->jumpCount < 2) {
            player->zVelocity = 500.0f;
            player->jumpCount = 2;
        }
    }

    if (player->isDashing) {
        player->dashTimer -= dt;
        float dashSpeed = 1200.0f;
        player->position.x += player->dashDir.x * dashSpeed * dt;
        player->position.z += player->dashDir.z * dashSpeed * dt;

        if (player->dashTimer <= 0.0f) {
            player->isDashing = false;
        }
    } else {
        float moveSpeed = 300.0f;
        player->position.x += moveDir.x * moveSpeed * dt;
        player->position.z += moveDir.z * moveSpeed * dt;

        if (IsKeyPressed(KEY_X) && player->dashCooldown <= 0.0f) {
            player->isDashing = true;
            player->dashTimer = 0.15f;
            player->dashCooldown = 0.8f;
            if (Vector3Length(moveDir) == 0.0f) {
                Vector3 diff = Vector3Subtract(*outMouseTarget, player->position);
                diff.y = 0.0f;
                player->dashDir = Vector3Normalize(diff);
            } else {
                player->dashDir = moveDir;
            }
        }
    }

    Vector3 toChar = Vector3Subtract(player->position, arenaCenter);
    toChar.y = 0.0f;
    if (Vector3Length(toChar) > arenaRadius - player->radius) {
        Vector3 normal = Vector3Normalize(toChar);
        player->position.x = arenaCenter.x + normal.x * (arenaRadius - player->radius);
        player->position.z = arenaCenter.z + normal.z * (arenaRadius - player->radius);
    }

    // 3. CẬP NHẬT ENEMY
    if (IsKeyPressed(KEY_P)) enemy->mode = (enemy->mode + 1) % 3;

    float currentEnemySpeed = enemy->speed;
    if (IsEnemySlowed()) currentEnemySpeed *= 0.4f; 
    if (IsAnySkillCoiling()) currentEnemySpeed = 0.0f; 

    // Tính toán target di chuyển và cập nhật hướng đi tùy theo mode
    Vector3 targetPos = enemy->position;

    if (enemy->mode == ENEMY_STATIC) {
        if (IsAnySkillCoiling()) {
            enemy->oscillationScale += (0.0f - enemy->oscillationScale) * 9.0f * dt;
        } else {
            enemy->oscillationScale += (1.0f - enemy->oscillationScale) * 2.0f * dt;
        }
        float oscillationFreq = IsEnemySlowed() ? 0.8f : 2.5f;
        targetPos = (Vector3){
            900.0f, 
            0.0f, 
            350.0f + sinf(GetTime() * oscillationFreq) * 100.0f * enemy->oscillationScale
        };
    } else if (enemy->mode == ENEMY_CHASE) {
        targetPos = player->position;
        targetPos.y = 0.0f;
    } else if (enemy->mode == ENEMY_PATROL) {
        if (currentEnemySpeed > 0.0f) {
            float rotSpeed = (currentEnemySpeed / 200.0f);
            enemy->patrolAngle += rotSpeed * dt;
        }
        Vector3 patrolCenter = { 600.0f, 0.0f, 440.0f };
        targetPos = (Vector3){
            patrolCenter.x + cosf(enemy->patrolAngle) * 250.0f,
            0.0f,
            patrolCenter.z + sinf(enemy->patrolAngle) * 200.0f
        };
    }

    // Di chuyển lái hướng (Steer/Move) về phía vị trí mục tiêu (giúp các lực đẩy/hút hoạt động tự nhiên)
    if (currentEnemySpeed > 0.0f && enemy->mode != ENEMY_STATIC) {
        Vector3 toTarget = Vector3Subtract(targetPos, enemy->position);
        toTarget.y = 0.0f;
        float dist = Vector3Length(toTarget);
        if (dist > 15.0f) {
            Vector3 dir = Vector3Scale(Vector3Normalize(toTarget), currentEnemySpeed * dt);
            enemy->position = Vector3Add(enemy->position, dir);
        }
    } else if (enemy->mode == ENEMY_STATIC) {
        // Mode Static vẫn tự trôi nhẹ về điểm tĩnh gốc để giữ nhịp dao động
        Vector3 toTarget = Vector3Subtract(targetPos, enemy->position);
        toTarget.y = 0.0f;
        float dist = Vector3Length(toTarget);
        if (dist > 1.0f) {
            Vector3 step = Vector3Scale(Vector3Normalize(toTarget), 150.0f * dt);
            if (Vector3Length(step) > dist) step = toTarget;
            enemy->position = Vector3Add(enemy->position, step);
        } else {
            enemy->position = targetPos;
        }
    }

    // Tích lũy và cập nhật lực Hất Văng (Knockback)
    Vector3 kbForce = GetAccumulatedKnockback();
    if (Vector3Length(kbForce) > 0.01f) {
        enemy->knockbackVelocity = Vector3Add(enemy->knockbackVelocity, kbForce);
        ClearAccumulatedKnockback();
    }

    // Cập nhật vị trí dựa theo vận tốc hất văng knockback
    if (Vector3Length(enemy->knockbackVelocity) > 0.01f) {
        enemy->position = Vector3Add(enemy->position, Vector3Scale(enemy->knockbackVelocity, dt));
        // Ma sát nội tại làm giảm dần tốc độ giật lùi
        enemy->knockbackVelocity = Vector3Subtract(enemy->knockbackVelocity, Vector3Scale(enemy->knockbackVelocity, 8.0f * dt));
        if (Vector3Length(enemy->knockbackVelocity) < 2.0f) {
            enemy->knockbackVelocity = (Vector3){0};
        }
    }

    // Cập nhật lực Hút Lại (Pull Force) từ lốc xoáy
    Vector3 windPullCenter = { 0 };
    float windPullStrength = 0.0f;
    if (GetWindPullForce(&windPullCenter, &windPullStrength)) {
        Vector3 pullDir = Vector3Subtract(windPullCenter, enemy->position);
        pullDir.y = 0.0f;
        float dist = Vector3Length(pullDir);
        if (dist > 10.0f) {
            // Càng gần tâm lực hút càng tăng để hút dính quái
            float pullFactor = Clamp(400.0f / dist, 0.5f, 3.0f);
            Vector3 pullForce = Vector3Scale(Vector3Normalize(pullDir), windPullStrength * pullFactor * dt);
            enemy->position = Vector3Add(enemy->position, pullForce);
        }
    }

    // Giới hạn quái vật không văng ra khỏi võ đài
    Vector3 toEnemy = Vector3Subtract(enemy->position, arenaCenter);
    toEnemy.y = 0.0f;
    if (Vector3Length(toEnemy) > arenaRadius - enemy->radius) {
        Vector3 normal = Vector3Normalize(toEnemy);
        enemy->position.x = arenaCenter.x + normal.x * (arenaRadius - enemy->radius);
        enemy->position.z = arenaCenter.z + normal.z * (arenaRadius - enemy->radius);
    }

    for (int i = 0; i < NUM_PILLARS; i++) {
        Vector3 diff = Vector3Subtract(enemy->position, pillars[i].position);
        diff.y = 0.0f;
        float dist = Vector3Length(diff);
        float minDist = enemy->radius + pillars[i].radius;
        if (dist < minDist) {
            Vector3 pushDir = Vector3Normalize(diff);
            enemy->position.x = pillars[i].position.x + pushDir.x * minDist;
            enemy->position.z = pillars[i].position.z + pushDir.z * minDist;
        }
    }

    if (IsAnySkillShocking()) {
        enemy->position.x += (float)GetRandomValue(-6, 6);
        enemy->position.z += (float)GetRandomValue(-6, 6);
    }

    // Xoay camera bằng phím Q và E
    if (IsKeyDown(KEY_Q)) g_cameraAngle -= 2.5f * dt;
    if (IsKeyDown(KEY_E)) g_cameraAngle += 2.5f * dt;

    // Zoom camera bằng phím R và F, hoặc Mouse Wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        g_camDist -= wheel * 50.0f;
    }
    if (IsKeyDown(KEY_R)) g_camDist -= 300.0f * dt;
    if (IsKeyDown(KEY_F)) g_camDist += 300.0f * dt;
    
    if (g_camDist < 100.0f) g_camDist = 100.0f;
    if (g_camDist > 1500.0f) g_camDist = 1500.0f;
    
    // Tỉ lệ thuận chiều cao theo độ zoom (giữ góc nhìn ~36.8 độ: height/dist = 0.75)
    g_camHeight = g_camDist * 0.75f;

    // Cập nhật Camera góc nhìn thứ 3 (MMORPG)
    camera.target = (Vector3){ player->position.x, player->position.y + 20.0f, player->position.z };
    
    camera.position = (Vector3){ 
        player->position.x + sinf(g_cameraAngle) * g_camDist, 
        player->position.y + g_camHeight, 
        player->position.z + cosf(g_cameraAngle) * g_camDist 
    };
}

void DrawSandbox3D(const PlayerEntity* player, const EnemyEntity* enemy, Vector3 mouseTarget, UIPanelState* uiState) {
    if (player->position.y > 5.0f) {
        DrawLine3D((Vector3){ player->position.x, 0.0f, player->position.z }, player->position, ColorAlpha(GRAY, 0.5f));
    }

    DrawCircleOutline3D((Vector3){ player->position.x, 0.08f, player->position.z }, 25.0f, ColorAlpha(LIME, 0.6f));
    DrawCircleOutline3D((Vector3){ enemy->position.x, 0.08f, enemy->position.z }, 30.0f, ColorAlpha(RED, 0.6f));

    for (int i = 0; i < NUM_PILLARS; i++) {
        float pillarHeight = pillars[i].radius * 2.5f;
        Vector3 center = { pillars[i].position.x, pillars[i].position.y + pillarHeight * 0.5f, pillars[i].position.z };
        Color pCol = (i == 0) ? GetColor(0xDAA520FF) : GetColor(0x3B3B42FF);
        Color pLineCol = (i == 0) ? YELLOW : GetColor(0x73737CFF);
        
        Environment_DrawSmartShadow(pillars[i].position, ENV_SHAPE_CYLINDER, pillars[i].radius, pillarHeight);
        DrawCylinder(center, pillars[i].radius, pillars[i].radius, pillarHeight, 16, pCol);
        DrawCylinderWires(center, pillars[i].radius, pillars[i].radius, pillarHeight, 16, pLineCol);
    }

    Environment_DrawSmartShadow(enemy->position, ENV_SHAPE_SPHERE, 30.0f, 30.0f);
    DrawCharacter3D(enemy->position, 30.0f, GetColor(0xFFC0CBFF), IsEnemySlowed() ? GetColor(0x1B4F72FF) : (IsEnemyBurning() ? RED : GetColor(0x8B2500FF)), IsEnemySlowed() ? SKYBLUE : (IsEnemyBurning() ? YELLOW : GetColor(0xFF5500FF)), false, (Vector3){0});
    
    Environment_DrawSmartShadow(player->position, ENV_SHAPE_SPHERE, 25.0f, 25.0f);
    DrawCharacter3D(player->position, 25.0f, GetColor(0xFFD39BFF), GetColor(0x3B5998FF), player->isDashing ? GetRegisteredSkillColor(uiState->activeSkillIndex) : GetColor(0xCCCCCCFF), true, mouseTarget);
}

void DrawSandboxHUD(void) {
    float angleDegrees = g_cameraAngle * 180.0f / 3.1415926535f;
    // Đưa góc về khoảng [0, 360)
    while (angleDegrees < 0.0f) angleDegrees += 360.0f;
    while (angleDegrees >= 360.0f) angleDegrees -= 360.0f;

    // Vẽ nền đen trong suốt phía sau HUD thông số
    DrawRectangle(10, 80, 520, 80, ColorAlpha(BLACK, 0.6f));
    
    DrawText(TextFormat("[CAMERA HUD] Angle: %.1f deg (%.3f rad)", angleDegrees, g_cameraAngle), 20, 90, 16, GREEN);
    DrawText(TextFormat("Dist (Zoom): %.1f | Height: %.1f (Ratio: %.2f)", g_camDist, g_camHeight, g_camHeight / g_camDist), 20, 110, 16, SKYBLUE);
    DrawText("Q/E: Xoay | R/F hoặc Cuộn chuột: Zoom | WASD di chuyển chuẩn camera", 20, 135, 14, LIGHTGRAY);
}
