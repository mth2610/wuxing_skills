#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include "skill_manager.h"
#include "raymath.h"
#include "sword_rain_skill.h"
#include "wind_skill.h"

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

// Hàm vẽ vòng tròn phẳng đục dưới đất 3D (trục X-Z)
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

// Vẽ bóng đổ phẳng hình elip/tròn dưới mặt đất Y = 0.05f (để tránh z-fighting)
static void DrawShadow3D(Vector3 pos, float height, float radius, float alpha, Vector3 moonPos) {
    // Project the center of the cylinder (pos.y + height * 0.5f) on the floor Y = 0.05f
    Vector3 centerPoint = { pos.x, pos.y + height * 0.5f, pos.z };
    Vector3 toObj = Vector3Subtract(centerPoint, moonPos);
    
    if (toObj.y >= -0.001f) return; // Prevent backward shadow or div by zero

    // Intersection of line (moonPos + t * toObj) with floor plane Y = 0.05f:
    float t = (0.05f - moonPos.y) / toObj.y;
    Vector3 projectedCenter = Vector3Add(moonPos, Vector3Scale(toObj, t));
    projectedCenter.y = 0.05f;

    int segments = 32;
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    Color color = ColorAlpha(BLACK, alpha);
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p0 = projectedCenter;
        Vector3 p1 = { projectedCenter.x + cosf(a1) * radius, 0.05f, projectedCenter.z + sinf(a1) * radius };
        Vector3 p2 = { projectedCenter.x + cosf(a2) * radius, 0.05f, projectedCenter.z + sinf(a2) * radius };
        
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(p0.x, p0.y, p0.z);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(color.r, color.g, color.b, color.a);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();
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

static void MyBeginMode3D(Camera3D camera)
{
    rlDrawRenderBatchActive();

    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();

    float aspect = (float)GetScreenWidth()/(float)GetScreenHeight();

    if (camera.projection == CAMERA_PERSPECTIVE)
    {
        double top = 10.0*tan(camera.fovy*0.5*DEG2RAD);
        double right = top*aspect;
        rlFrustum(-right, right, -top, top, 10.0, 15000.0);
    }
    else if (camera.projection == CAMERA_ORTHOGRAPHIC)
    {
        double top = camera.fovy/2.0;
        double right = top*aspect;
        rlOrtho(-right, right, -top, top, 0.01, 15000.0);
    }

    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();

    Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
    rlMultMatrixf(MatrixToFloat(matView));

    rlEnableDepthTest();
}

static void MyEndMode3D(void)
{
    rlDrawRenderBatchActive();

    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();

    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();

    rlLoadIdentity();

    rlDisableDepthTest();
}

int main(void) {
    const int screenWidth = 1200;
    const int screenHeight = 700;
    InitWindow(screenWidth, screenHeight, "Avatar: True 3D Element Testbed");

    // Tăng khoảng cách cắt xa (far clipping plane) để vẽ được map rộng 3600 và mặt trăng ở xa
    rlSetClipPlanes(0.1f, 15000.0f);

    // Khởi tạo hệ thống quản lý chiêu thức
    InitSkillManager(screenWidth, screenHeight);
    InitSwordRainSkill(); 

    // Cấu hình màn hình phụ và shader cho Thái Cực
    Shader taijiShader = LoadShader(0, "taiji.fs");
    RenderTexture2D screenTarget = LoadRenderTexture(screenWidth, screenHeight);
    bool taijiActive = false;
    int jumpCount = 0;
    Vector3 moonPos = { 880.0f, 700.0f, -83.0f };

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
    Rectangle rectTaijiToggle = { 870, 270, 200, 35 };

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

        // Định nghĩa các hằng số kích thước võ đài và nhân vật ở đầu vòng lặp để các tính toán va chạm sử dụng
        Vector3 arenaCenter = { 600.0f, 0.0f, 440.0f };
        float arenaRadius = 1800.0f; // Đã mở rộng gấp 3 lần
        float charRadius = 30.0f;

        // 1. NGẮM BẮN CHUỘT 3D (Tia ngắm chuột giao cắt với mặt phẳng Y = 0)
        Ray mouseRay = GetScreenToWorldRay(mousePos, camera);
        Vector3 mouseTarget3D = { 0.0f, 0.0f, 0.0f };
        if (mouseRay.direction.y != 0.0f) {
            float t = -mouseRay.position.y / mouseRay.direction.y;
            mouseTarget3D = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));
        }

        // Phím nhanh bật/tắt Thái Cực
        if (IsKeyPressed(KEY_T)) {
            taijiActive = !taijiActive;
        }

        // Danh sách kỹ năng có sẵn dựa trên chế độ Thái Cực
        int availableSkills[10];
        int availableCount = 0;
        if (!taijiActive) {
            availableSkills[0] = 0; // WATER
            availableSkills[1] = 1; // METAL
            availableSkills[2] = 2; // FIRE
            availableSkills[3] = 3; // WOOD
            availableSkills[4] = 6; // SWORD RAIN
            availableCount = 5;
        } else {
            availableSkills[0] = 4; // ELECTRIC (LÔI)
            availableSkills[1] = 5; // WIND (PHONG)
            availableCount = 2;
        }

        // Đảm bảo chiêu đang chọn luôn hợp lệ
        bool activeValid = false;
        for (int i = 0; i < availableCount; i++) {
            if (activeSkillIndex == availableSkills[i]) {
                activeValid = true;
                break;
            }
        }
        if (!activeValid) {
            activeSkillIndex = availableSkills[0];
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

        // Xác định độ cao mặt đất cục bộ dưới chân (mặc định là sàn đấu Y = 0)
        float currentGroundY = 0.0f;
        
        // Kiểm tra va chạm với các trụ đá (đặc biệt đứng lên trụ 0)
        for (int i = 0; i < NUM_PILLARS; i++) {
            Vector3 diff = Vector3Subtract(characterPos, pillars[i].position);
            diff.y = 0.0f;
            float dist = Vector3Length(diff);
            float pillarHeight = pillars[i].radius * 2.5f;
            float checkRadius = charRadius + pillars[i].radius;
            
            if (dist < checkRadius) {
                // Chỉ đứng lên đỉnh trụ đá đầu tiên (pillars[0]) để nhảy thử, các trụ khác đẩy ngang
                if (i == 0 && characterPos.y >= pillarHeight - 15.0f && charZVelocity <= 0.0f) {
                    currentGroundY = pillarHeight;
                } else {
                    // Đẩy ngang nếu ở dưới đỉnh trụ
                    Vector3 pushDir = Vector3Normalize(diff);
                    characterPos.x = pillars[i].position.x + pushDir.x * checkRadius;
                    characterPos.z = pillars[i].position.z + pushDir.z * checkRadius;
                }
            }
        }

        // Cập nhật nhảy khinh công (Trục Y)
        bool isOnSolid = (characterPos.y <= currentGroundY);
        if (!isOnSolid || charZVelocity > 0.0f) {
            charZVelocity -= gravity * dt;
            characterPos.y += charZVelocity * dt;
            if (characterPos.y <= currentGroundY) {
                characterPos.y = currentGroundY;
                charZVelocity = 0.0f;
                jumpCount = 0;
            }
        } else {
            charZVelocity = 0.0f;
        }

        // Kích hoạt nhảy và nhảy đúp tối đa 2 lần
        if (IsKeyPressed(KEY_SPACE)) {
            if (characterPos.y <= currentGroundY + 1.0f) {
                charZVelocity = 550.0f;
                jumpCount = 1;
            } else if (jumpCount < 2) {
                charZVelocity = 500.0f;
                jumpCount = 2;
            }
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
            if (IsKeyPressed(KEY_X) && dashCooldown <= 0.0f) {
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

        Vector3 toChar = Vector3Subtract(characterPos, arenaCenter);
        toChar.y = 0.0f;
        float distToCenter = Vector3Length(toChar);
        if (distToCenter > arenaRadius - charRadius) {
            Vector3 normal = Vector3Normalize(toChar);
            characterPos.x = arenaCenter.x + normal.x * (arenaRadius - charRadius);
            characterPos.z = arenaCenter.z + normal.z * (arenaRadius - charRadius);
        }

        // Va chạm vật lý X-Z được xử lý bên trên cùng nhảy, tránh đẩy trùng lặp.

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

        // Áp dụng lực hút từ cơn gió (Wind Pull) của chiêu Phong lên quái
        Vector3 windPullCenter = { 0 };
        float windPullStrength = 0.0f;
        if (GetWindPullForce(&windPullCenter, &windPullStrength)) {
            Vector3 pullDir = Vector3Subtract(windPullCenter, enemyPos);
            pullDir.y = 0.0f;
            float pullDist = Vector3Length(pullDir);
            if (pullDist > 10.0f) {
                Vector3 pullForce = Vector3Scale(Vector3Normalize(pullDir), windPullStrength * dt);
                enemyPos = Vector3Add(enemyPos, pullForce);
            }
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

            // Kiểm tra click Toggle Taiji Realm
            if (CheckCollisionPointRec(mousePos, rectTaijiToggle)) {
                taijiActive = !taijiActive;
                clickedOnUI = true;
            }

            // Click đổi chiêu hệ ngũ hành
            if (hoverSkillIndex != -1) {
                activeSkillIndex = availableSkills[hoverSkillIndex];
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

        // Cập nhật vị trí mặt trăng tương đối theo nhân vật (lệch 30 độ so với trục Tây/Đông, bóng đổ Tây và Nam)
        Vector3 moonPos = { characterPos.x + 750.0f, 700.0f, characterPos.z - 433.0f };

        // Cập nhật Camera theo chân nhân vật
        camera.target = characterPos;
        camera.position = (Vector3){ characterPos.x, characterPos.y + 500.0f, characterPos.z + 400.0f };

        // --- VẼ MÀN HÌNH ---
        // 1. RENDER CẢNH 3D VÀ HIỆU ỨNG RA SCREEN TARGET RENDERTEXTURE
        BeginTextureMode(screenTarget);
            ClearBackground(GetColor(0x111111FF)); 

            // RENDER 3D MODE
            MyBeginMode3D(camera);
                // Vẽ nền võ đài 3D phẳng
                DrawCircleSolid3D((Vector3){ 600.0f, -0.05f, 440.0f }, 1805.0f, GetColor(0x1B1B1FFF));
                DrawCircleSolid3D((Vector3){ 600.0f, 0.0f, 440.0f }, 1800.0f, GetColor(0x222228FF));
                
                // Vẽ các đường hoa văn võ đài ngũ hành bằng các đường line 3D sáng màu hơn
                DrawCircleOutline3D((Vector3){ 600.0f, 0.08f, 440.0f }, 1600.0f, GetColor(0x5A5A6FFF));
                DrawCircleOutline3D((Vector3){ 600.0f, 0.08f, 440.0f }, 1200.0f, GetColor(0x4F4F5FFF));
                DrawCircleOutline3D((Vector3){ 600.0f, 0.08f, 440.0f }, 400.0f, GetColor(0x444455FF));
                
                DrawLine3D((Vector3){ 600.0f - 1800.0f, 0.08f, 440.0f }, (Vector3){ 600.0f + 1800.0f, 0.08f, 440.0f }, GetColor(0x40404FFF));
                DrawLine3D((Vector3){ 600.0f, 0.08f, 440.0f - 1800.0f }, (Vector3){ 600.0f, 0.08f, 440.0f + 1800.0f }, GetColor(0x40404FFF));

                // Vẽ mặt trăng trên trời làm nguồn sáng
                DrawSphere(moonPos, 60.0f, WHITE);
                for (float rFactor = 1.1f; rFactor < 1.4f; rFactor += 0.1f) {
                    DrawSphere(moonPos, 60.0f * rFactor, ColorAlpha(YELLOW, 0.06f));
                }

                // Vẽ bóng đổ chân nhân vật và cột đá
                for (int i = 0; i < NUM_PILLARS; i++) {
                    float pillarHeight = pillars[i].radius * 2.5f;
                    DrawShadow3D(pillars[i].position, pillarHeight, pillars[i].radius * 1.1f, 0.5f, moonPos);
                }

                // Bóng đổ Enemy (Chiều cao enemy là 55.0f)
                DrawShadow3D(enemyPos, 55.0f, 36.0f, 0.5f, moonPos);

                // Bóng đổ Player (Chiều cao player là 45.0f, co giãn theo Y và nghiêng theo mặt trăng)
                float shadowScale = 1.0f - (characterPos.y / 350.0f);
                if (shadowScale < 0.2f) shadowScale = 0.2f;
                DrawShadow3D(characterPos, 45.0f, 32.0f * shadowScale, 0.5f * shadowScale, moonPos);

                // Vẽ đường thẳng độ cao biểu diễn vị trí trên không trung
                if (characterPos.y > 5.0f) {
                    DrawLine3D((Vector3){ characterPos.x, 0.0f, characterPos.z }, characterPos, ColorAlpha(GRAY, 0.5f));
                }

                // Vẽ vòng định vị chân luôn ghim sát mặt sàn 3D
                DrawCircleOutline3D((Vector3){ characterPos.x, 0.08f, characterPos.z }, 25.0f, ColorAlpha(LIME, 0.6f));
                DrawCircleOutline3D((Vector3){ enemyPos.x, 0.08f, enemyPos.z }, 30.0f, ColorAlpha(RED, 0.6f));

                // Vẽ 3 cột đá bằng cylinder 3D
                for (int i = 0; i < NUM_PILLARS; i++) {
                    float pillarHeight = pillars[i].radius * 2.5f;
                    Vector3 center = { pillars[i].position.x, pillars[i].position.y + pillarHeight * 0.5f, pillars[i].position.z };
                    // Tô màu cột 0 khác biệt (màu vàng kim nhẹ) để làm điểm đứng thử
                    Color pCol = (i == 0) ? GetColor(0xDAA520FF) : GetColor(0x3B3B42FF);
                    Color pLineCol = (i == 0) ? YELLOW : GetColor(0x73737CFF);
                    DrawCylinder(center, pillars[i].radius, pillars[i].radius, pillarHeight, 16, pCol);
                    DrawCylinderWires(center, pillars[i].radius, pillars[i].radius, pillarHeight, 16, pLineCol);
                }

                // Vẽ Enemy và Player trong không gian 3D
                DrawCharacter3D(enemyPos, 30.0f, GetColor(0xFFC0CBFF), IsEnemySlowed() ? GetColor(0x1B4F72FF) : (IsEnemyBurning() ? RED : GetColor(0x8B2500FF)), IsEnemySlowed() ? SKYBLUE : (IsEnemyBurning() ? YELLOW : GetColor(0xFF5500FF)), false, (Vector3){0});
                
                DrawCharacter3D(characterPos, 25.0f, GetColor(0xFFD39BFF), GetColor(0x3B5998FF), isDashing ? GetRegisteredSkillColor(activeSkillIndex) : GetColor(0xCCCCCCFF), true, mouseTarget3D);

            MyEndMode3D();
        EndTextureMode();

        // 2. VẼ TOÀN BỘ MÀN HÌNH CHÍNH
        BeginDrawing();
            ClearBackground(BLACK);

            // Vẽ screen target kết xuất kèm hiệu ứng shader Thái Cực đơn sắc nếu được kích hoạt
            if (taijiActive) {
                BeginShaderMode(taijiShader);
            }
            DrawTextureRec(screenTarget.texture, (Rectangle){ 0, 0, (float)screenTarget.texture.width, (float)-screenTarget.texture.height }, (Vector2){ 0, 0 }, WHITE);
            if (taijiActive) {
                EndShaderMode();
            }

            // RENDER 2D SCREEN OVERLAYS (UI, Hạt dư ảnh bay)
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
            for (int i = 0; i < availableCount; i++) {
                int skillIdx = availableSkills[i];
                bool isSelected = (activeSkillIndex == skillIdx);
                bool isHover = (hoverSkillIndex == i);
                Color baseColor = GetRegisteredSkillColor(skillIdx);
                
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
                
                const char* skillName = GetRegisteredSkillName(skillIdx);
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

            // Vẽ nút bật/tắt Thái Cực (Taiji Realm Toggle)
            DrawText("Taiji Realm:", 770, 280, 16, LIGHTGRAY);
            bool hoverTaiji = CheckCollisionPointRec(mousePos, rectTaijiToggle);
            Color taijiCol = taijiActive ? (hoverTaiji ? GetColor(0xFF5500FF) : GetColor(0xDD3300FF)) : 
                                           (hoverTaiji ? DARKGRAY : GetColor(0x333333FF));
            DrawRectangleRounded(rectTaijiToggle, 0.2f, 10, taijiCol);
            DrawRectangleRoundedLines(rectTaijiToggle, 0.2f, 10, WHITE);
            DrawText(taijiActive ? "TAIJI: ACTIVE (B&W)" : "TAIJI: INACTIVE", (int)rectTaijiToggle.x + 20, (int)rectTaijiToggle.y + 10, 14, WHITE);

            // Bảng hướng dẫn & thông số
            const char* modeStr = "STATIC (STATIONARY)";
            if (enemyMode == ENEMY_CHASE) modeStr = "CHASING PLAYER";
            else if (enemyMode == ENEMY_PATROL) modeStr = "CIRCULAR PATROL";

            const char* anchorStr = (selectedAnchor == CAST_ANCHOR_CASTER) ? "CASTER" : "TARGET";
            const char* pathStr = (selectedPath == CAST_PATH_PROJECTILE) ? "PROJECTILE" : 
                                  ((selectedPath == CAST_PATH_UNDERGROUND) ? "GROUND" : "SKY");
            const char* portalStr = showPortals ? "ON" : "OFF";

            DrawText("Controls: WASD/Arrows to Move | Space to Jump (Double Jump up to 2x) | X to Dash", 20, 80, 16, LIGHTGRAY);
            DrawText("Press T to toggle Taiji Realm (switch to Phong-Lôi B&W) | Jump on Golden Pillar 0 to stand on top", 20, 105, 16, LIGHTGRAY);
            DrawText(TextFormat("Params: Qty = %d | Size = %.1fx | Anchor = %s | Path = %s | Portals = %s | Enemy = %s", 
                                selectedQty, selectedSize, anchorStr, pathStr, portalStr, modeStr), 20, 130, 16, GetColor(0x55DD66FF));
            DrawText("Camera follow enabled. Character/pillars have dynamic shadows projected from the Moon.", 20, 155, 16, GetColor(0xBA55D3FF));

        EndDrawing();
    }

    // Dọn dẹp bộ nhớ RAM/VRAM
    UnloadShader(taijiShader);
    UnloadRenderTexture(screenTarget);
    UnloadSkillManager();
    CloseWindow();
    
    return 0;
}