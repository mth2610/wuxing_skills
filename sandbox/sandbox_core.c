#include "sandbox/sandbox_core.h"
#include "core/skill_manager.h"
#include "environment/environment_system.h"
#include "entities/entities.h"
#include "raymath.h"
#include "rlgl.h"
#include <stddef.h>

// Biến toàn cục hiển thị phím ảo cảm ứng
#if defined(PLATFORM_ANDROID)
bool g_showTouchControls = true;
#else
bool g_showTouchControls = false;
#endif

// Biến môi trường
static const Vector3 arenaCenter = { 600.0f, 0.0f, 440.0f };
static const float arenaRadius = 1800.0f;
static const float gravity = 1500.0f;

static Vector2 g_joystickKnobOffset = { 0, 0 };
static bool g_joystickActive = false;

static bool g_touchJumpActive = false;
static bool g_touchDashActive = false;
static bool g_touchFlyToggleActive = false;
static bool g_touchFlyUpActive = false;
static bool g_touchFlyDownActive = false;
static bool g_touchCamLeftActive = false;
static bool g_touchCamRightActive = false;

static bool g_touchSkillActive[5] = { false };
static bool g_touchAttackActive = false;

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
    player->position = (Vector3){ -1100.0f, 0.0f, 440.0f };
    player->radius = 30.0f;
    player->dashCooldown = 0.0f;
    player->dashTimer = 0.0f;
    player->dashDir = (Vector3){ 0 };
    player->isDashing = false;
    player->zVelocity = 0.0f;
    player->jumpCount = 0;
    player->isFlying = false;

    // Cấu hình Enemy
    enemy->position = (Vector3){ 900.0f, 0.0f, 350.0f };
    enemy->radius = 35.0f;
    enemy->mode = ENEMY_STATIC;
    enemy->speed = 120.0f;
    enemy->patrolAngle = 0.0f;
    enemy->oscillationScale = 1.0f;
    enemy->knockbackVelocity = (Vector3){ 0 };

    // Register player/enemy as real agents in the Entities module agentPool
    // so skills can drive their HP via Entity_ApplyAoEDamage(). Note: legacy
    // skill damage (core/skill_manager.h ApplyAoEDamage/knockback) is NOT
    // bridged to this HP system in this pass — only a skill calling
    // Entity_ApplyAoEDamage directly will move these HP values.
    Entity_Init();
    player->agentId = Entity_SpawnAgent(player->position, 100.0f, 0);
    enemy->agentId = Entity_SpawnAgent(enemy->position, 100.0f, 0);
}
// Biến toàn cục để điều khiển camera
static float g_cameraAngle = 0.0f;
static float g_camDist = 600.0f;
static float g_camHeight = 450.0f;

void UpdateSandbox(PlayerEntity* player, EnemyEntity* enemy, float dt, UIPanelState* uiState, Vector3* outMouseTarget) {
    Vector2 mousePos = GetMousePosition();

    // TAB để bật/tắt phím ảo trên PC (tiện cho việc test chuột)
    if (IsKeyPressed(KEY_TAB)) {
        g_showTouchControls = !g_showTouchControls;
    }

    // Tải và tính toán phím ảo (Touch HUD)
    float screenWidth = (float)GetScreenWidth();
    float screenHeight = (float)GetScreenHeight();
    float touchScale = screenHeight / 700.0f;

    // Joystick ảo
    Vector2 joystickCenter = { 150.0f * touchScale, screenHeight - 150.0f * touchScale };
    float joystickBaseRadius = 65.0f * touchScale;

    // Các phím chức năng theo sơ đồ MMORPG Action mới
    Vector2 attackBtnCenter = { screenWidth - 120.0f * touchScale, screenHeight - 120.0f * touchScale };
    float attackBtnRadius = 52.0f * touchScale;

    Vector2 skillBtnsCenter[5];
    float skillBtnsRadius = 25.0f * touchScale;
    float arcRadius = 135.0f * touchScale;
    for (int k = 0; k < 5; k++) {
        float angle = PI - (k * (PI / 6.0f)); // 180, 150, 120, 90, 60 độ xung quanh nút đánh thường
        skillBtnsCenter[k].x = attackBtnCenter.x + cosf(angle) * arcRadius;
        skillBtnsCenter[k].y = attackBtnCenter.y - sinf(angle) * arcRadius;
    }

    // Nút nhảy (nút nhỏ ở góc trên bên phải nút đánh thường)
    Vector2 jumpBtnCenter = { screenWidth - 50.0f * touchScale, screenHeight - 230.0f * touchScale };
    float jumpBtnRadius = 28.0f * touchScale;

    // Nút lướt (Dash) ở góc dưới bên trái nút đánh thường
    Vector2 dashBtnCenter = { screenWidth - 240.0f * touchScale, screenHeight - 70.0f * touchScale };
    float dashBtnRadius = 28.0f * touchScale;

    // Nút Bay / Đáp đất (Fly Toggle)
    Vector2 flyToggleBtnCenter = { screenWidth - 50.0f * touchScale, screenHeight - 330.0f * touchScale };
    float flyToggleBtnRadius = 28.0f * touchScale;

    // Nút bay lên và bay xuống (chỉ dùng khi bay)
    Vector2 flyUpBtnCenter = { screenWidth - 120.0f * touchScale, screenHeight - 280.0f * touchScale };
    float flyUpBtnRadius = 28.0f * touchScale;

    Vector2 flyDownBtnCenter = { screenWidth - 200.0f * touchScale, screenHeight - 240.0f * touchScale };
    float flyDownBtnRadius = 28.0f * touchScale;

    // Phím xoay camera di chuyển lên góc trên bên phải (để trống góc dưới cho tay di chuyển)
    Vector2 camLeftCenter = { screenWidth - 120.0f * touchScale, 150.0f * touchScale };
    float camLeftRadius = 25.0f * touchScale;

    Vector2 camRightCenter = { screenWidth - 50.0f * touchScale, 150.0f * touchScale };
    float camRightRadius = 25.0f * touchScale;

    // Cập nhật trạng thái kéo Joystick bằng biến static
    static bool s_joystickDragging = false;
    static int s_joystickTouchId = -1;

    // Thu thập các điểm chạm/click
    int inputPointCount = GetTouchPointCount();
    Vector2 inputPoints[10];
    for (int i = 0; i < inputPointCount && i < 10; i++) {
        inputPoints[i] = GetTouchPosition(i);
    }
    // Giả lập click chuột thành 1 chạm trên PC khi bật phím ảo
    if (inputPointCount == 0 && g_showTouchControls && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        inputPoints[0] = GetMousePosition();
        inputPointCount = 1;
    }

    // Kiểm tra xem ngón tay kéo joystick ở frame trước có còn chạm không
    if (s_joystickDragging) {
        if (s_joystickTouchId < 0 || s_joystickTouchId >= inputPointCount) {
            s_joystickDragging = false;
            s_joystickTouchId = -1;
        }
    }

    // Reset trạng thái joystick ảo hàng frame
    g_joystickActive = false;
    g_joystickKnobOffset = (Vector2){ 0, 0 };

    float joyX = 0.0f;
    float joyZ = 0.0f;
    bool touchJump = false;
    bool touchDash = false;
    bool touchFlyToggle = false;
    bool touchFlyUp = false;
    bool touchFlyDown = false;
    bool touchCamLeft = false;
    bool touchCamRight = false;
    bool touchSkill[5] = { false };
    bool touchAttack = false;

    // Xử lý va chạm vùng nút ảo cảm ứng
    for (int i = 0; i < inputPointCount; i++) {
        Vector2 pt = inputPoints[i];

        // Mặc định bất kỳ điểm chạm nào ở nửa bên trái màn hình đều tính là click vào UI (tránh cast chiêu khi di chuyển)
        if (pt.x < screenWidth * 0.45f) {
            uiState->clickedOnUI = true;
        }
        
        // 1. Xử lý Joystick kéo giữ khóa mục tiêu
        if (s_joystickDragging && i == s_joystickTouchId) {
            g_joystickActive = true;
            Vector2 joyVec = Vector2Subtract(pt, joystickCenter);
            float len = Vector2Length(joyVec);
            if (len > joystickBaseRadius) {
                joyVec = Vector2Scale(Vector2Normalize(joyVec), joystickBaseRadius);
            }
            g_joystickKnobOffset = joyVec;
            joyX = joyVec.x / joystickBaseRadius;
            joyZ = joyVec.y / joystickBaseRadius;
            uiState->clickedOnUI = true;
            continue;
        }

        // Nếu chưa kéo, phát hiện xem có chạm vào vùng joystick để bắt đầu kéo không
        if (!s_joystickDragging) {
            float distToJoy = Vector2Distance(pt, joystickCenter);
            if (distToJoy <= joystickBaseRadius * 2.0f) { // Vùng chạm bắt đầu rộng rãi
                s_joystickDragging = true;
                s_joystickTouchId = i;
                g_joystickActive = true;
                Vector2 joyVec = Vector2Subtract(pt, joystickCenter);
                float len = Vector2Length(joyVec);
                if (len > joystickBaseRadius) {
                    joyVec = Vector2Scale(Vector2Normalize(joyVec), joystickBaseRadius);
                }
                g_joystickKnobOffset = joyVec;
                joyX = joyVec.x / joystickBaseRadius;
                joyZ = joyVec.y / joystickBaseRadius;
                uiState->clickedOnUI = true;
                continue;
            }
        }

        // 2. Kiểm tra các phím chức năng với vùng chạm rộng rãi (radius * 1.4f)
        // Phím Dash
        if (Vector2Distance(pt, dashBtnCenter) <= dashBtnRadius * 1.4f) {
            touchDash = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // Phím Jump
        if (Vector2Distance(pt, jumpBtnCenter) <= jumpBtnRadius * 1.4f) {
            touchJump = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // Phím Fly Toggle
        if (Vector2Distance(pt, flyToggleBtnCenter) <= flyToggleBtnRadius * 1.4f) {
            touchFlyToggle = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // Phím Fly Up
        if (player->isFlying && Vector2Distance(pt, flyUpBtnCenter) <= flyUpBtnRadius * 1.4f) {
            touchFlyUp = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // Phím Fly Down
        if (player->isFlying && Vector2Distance(pt, flyDownBtnCenter) <= flyDownBtnRadius * 1.4f) {
            touchFlyDown = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // Phím Cam Left
        if (Vector2Distance(pt, camLeftCenter) <= camLeftRadius * 1.4f) {
            touchCamLeft = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // Phím Cam Right
        if (Vector2Distance(pt, camRightCenter) <= camRightRadius * 1.4f) {
            touchCamRight = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // 3. Phím đánh thường (lớn)
        if (Vector2Distance(pt, attackBtnCenter) <= attackBtnRadius * 1.3f) {
            touchAttack = true;
            uiState->clickedOnUI = true;
            continue;
        }

        // 4. 5 phím kỹ năng nhỏ xung quanh nút đánh thường
        bool hitSkill = false;
        for (int k = 0; k < 5; k++) {
            if (GetSkillAtOrderIndex(k) != -1 && Vector2Distance(pt, skillBtnsCenter[k]) <= skillBtnsRadius * 1.4f) {
                touchSkill[k] = true;
                uiState->clickedOnUI = true;
                hitSkill = true;
                break;
            }
        }
        if (hitSkill) continue;
    }

    // Theo dõi trạng thái sườn lên để kích hoạt nhấn một lần
    static bool prevTouchJump = false;
    static bool prevTouchDash = false;
    static bool prevTouchFlyToggle = false;
    static bool prevTouchSkill[5] = { false };
    static bool prevTouchAttack = false;

    bool jumpPressed = IsKeyPressed(KEY_SPACE) || (touchJump && !prevTouchJump);
    bool dashPressed = (IsKeyPressed(KEY_X) && player->dashCooldown <= 0.0f) || (touchDash && !prevTouchDash && player->dashCooldown <= 0.0f);
    bool flyTogglePressed = IsKeyPressed(KEY_G) || (touchFlyToggle && !prevTouchFlyToggle);

    prevTouchJump = touchJump;
    prevTouchDash = touchDash;
    prevTouchFlyToggle = touchFlyToggle;

    g_touchJumpActive = touchJump;
    g_touchDashActive = touchDash;
    g_touchFlyToggleActive = touchFlyToggle;
    g_touchFlyUpActive = touchFlyUp;
    g_touchFlyDownActive = touchFlyDown;
    g_touchCamLeftActive = touchCamLeft;
    g_touchCamRightActive = touchCamRight;

    // Xuất chiêu khi chạm phím cảm ứng (aim thẳng vào enemy)
    for (int k = 0; k < 5; k++) {
        bool skillPressed = touchSkill[k] && !prevTouchSkill[k];
        if (skillPressed) {
            int skillIdx = GetSkillAtOrderIndex(k);
            if (skillIdx != -1) {
                CastSkill(skillIdx, player->position, enemy->position, uiState->currentParams);
            }
        }
        prevTouchSkill[k] = touchSkill[k];
        g_touchSkillActive[k] = touchSkill[k];
    }

    bool attackPressed = touchAttack && !prevTouchAttack;
    if (attackPressed) {
        CastSkill(uiState->activeSkillIndex, player->position, enemy->position, uiState->currentParams);
    }
    prevTouchAttack = touchAttack;
    g_touchAttackActive = touchAttack;

    if (flyTogglePressed) {
        player->isFlying = !player->isFlying;
        if (!player->isFlying) {
            player->zVelocity = 0.0f; // Triệt tiêu gia tốc khi đáp đất
        }
    }

    // 1. NGẮM BẮN CHUỘT 3D (Chỉ thực hiện ngắm nếu không tương tác với phím ảo)
    if (!uiState->clickedOnUI) {
        Ray mouseRay = GetScreenToWorldRay(mousePos, camera);
        if (mouseRay.direction.y != 0.0f) {
            float t = -mouseRay.position.y / mouseRay.direction.y;
            *outMouseTarget = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));
        } else {
            *outMouseTarget = (Vector3){0};
        }
    } else {
        *outMouseTarget = (Vector3){0};
    }

    // 2. DI CHUYỂN PLAYER (WASD & KEYBOARD / JOYSTICK)
    float inputX = 0.0f;
    float inputZ = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) inputZ -= 1.0f; 
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) inputZ += 1.0f; 
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) inputX -= 1.0f; 
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) inputX += 1.0f; 

    // Gộp input từ phím ảo nếu có hoạt động
    if (g_joystickActive) {
        inputX = joyX;
        inputZ = joyZ;
    }

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

    // Tính toán chiều cao địa hình bên dưới người chơi (Pillars & Ground)
    float currentGroundY = 0.0f;
    for (int i = 0; i < NUM_PILLARS; i++) {
        Vector3 diff = Vector3Subtract(player->position, pillars[i].position);
        diff.y = 0.0f;
        float dist = Vector3Length(diff);
        float pillarHeight = pillars[i].radius * 2.5f;
        float checkRadius = player->radius + pillars[i].radius;
        
        if (dist < checkRadius) {
            // Chỉ đẩy lùi nếu người chơi ở độ cao thấp hơn đỉnh cột
            if (player->position.y < pillarHeight) {
                if (i == 0 && player->position.y >= pillarHeight - 15.0f && player->zVelocity <= 0.0f) {
                    currentGroundY = pillarHeight;
                } else {
                    Vector3 pushDir = Vector3Normalize(diff);
                    player->position.x = pillars[i].position.x + pushDir.x * checkRadius;
                    player->position.z = pillars[i].position.z + pushDir.z * checkRadius;
                }
            } else {
                // Đứng vững trên mặt cột đá
                if (player->zVelocity <= 0.0f) {
                    currentGroundY = pillarHeight;
                }
            }
        }
    }

    // 3. VẬT LÝ DI CHUYỂN / BAY (FLIGHT VS JUMP PHYSICS)
    if (player->isFlying) {
        player->zVelocity = 0.0f;
        player->jumpCount = 0;
        float flySpeed = 350.0f;
        if (IsKeyDown(KEY_SPACE) || touchFlyUp) {
            player->position.y += flySpeed * dt;
        }
        if (IsKeyDown(KEY_LEFT_SHIFT) || touchFlyDown) {
            player->position.y -= flySpeed * dt;
            if (player->position.y <= currentGroundY) {
                player->position.y = currentGroundY;
                player->isFlying = false; // Tự đáp đất khi chạm sàn
            }
        }
    } else {
        // Áp dụng trọng lực thông thường khi không bay
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

        // Nhẩy / Nhẩy kép
        if (jumpPressed) {
            if (player->position.y <= currentGroundY + 1.0f) {
                player->zVelocity = 550.0f;
                player->jumpCount = 1;
            } else if (player->jumpCount < 2) {
                player->zVelocity = 500.0f;
                player->jumpCount = 2;
            }
        }
    }

    // Dash lướt nhanh
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

        if (dashPressed) {
            player->isDashing = true;
            player->dashTimer = 0.15f;
            player->dashCooldown = 0.8f;
            if (Vector3Length(moveDir) == 0.0f) {
                // Dash theo hướng nhắm chuột nếu đang đứng yên
                Vector3 diff = Vector3Subtract(*outMouseTarget, player->position);
                diff.y = 0.0f;
                player->dashDir = Vector3Normalize(diff);
            } else {
                player->dashDir = moveDir;
            }
        }
    }

    // Giới hạn trong võ đài
    Vector3 toChar = Vector3Subtract(player->position, arenaCenter);
    toChar.y = 0.0f;
    if (Vector3Length(toChar) > arenaRadius - player->radius) {
        Vector3 normal = Vector3Normalize(toChar);
        player->position.x = arenaCenter.x + normal.x * (arenaRadius - player->radius);
        player->position.z = arenaCenter.z + normal.z * (arenaRadius - player->radius);
    }

    // 4. CẬP NHẬT ENEMY
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

    // Di chuyển lái hướng (Steer/Move) về phía vị trí mục tiêu
    if (currentEnemySpeed > 0.0f && enemy->mode != ENEMY_STATIC) {
        Vector3 toTarget = Vector3Subtract(targetPos, enemy->position);
        toTarget.y = 0.0f;
        float dist = Vector3Length(toTarget);
        if (dist > 15.0f) {
            Vector3 dir = Vector3Scale(Vector3Normalize(toTarget), currentEnemySpeed * dt);
            enemy->position = Vector3Add(enemy->position, dir);
        }
    } else if (enemy->mode == ENEMY_STATIC) {
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
        enemy->knockbackVelocity = Vector3Subtract(enemy->knockbackVelocity, Vector3Scale(enemy->knockbackVelocity, 8.0f * dt));
        if (Vector3Length(enemy->knockbackVelocity) < 2.0f) {
            enemy->knockbackVelocity = (Vector3){0};
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

    // Xoay camera bằng phím Q/E hoặc phím ảo
    if (IsKeyDown(KEY_Q) || touchCamLeft) g_cameraAngle -= 2.5f * dt;
    if (IsKeyDown(KEY_E) || touchCamRight) g_cameraAngle += 2.5f * dt;

    // Zoom camera bằng phím R và F, hoặc Mouse Wheel
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        g_camDist -= wheel * 50.0f;
    }
    if (IsKeyDown(KEY_R)) g_camDist -= 300.0f * dt;
    if (IsKeyDown(KEY_F)) g_camDist += 300.0f * dt;
    
    if (g_camDist < 100.0f) g_camDist = 100.0f;
    if (g_camDist > 1500.0f) g_camDist = 1500.0f;
    
    g_camHeight = g_camDist * 0.75f;

    // Cập nhật Camera góc nhìn thứ 3 (MMORPG)
    camera.target = (Vector3){ player->position.x, player->position.y + 20.0f, player->position.z };
    
    camera.position = (Vector3){ 
        player->position.x + sinf(g_cameraAngle) * g_camDist, 
        player->position.y + g_camHeight, 
        player->position.z + cosf(g_cameraAngle) * g_camDist
    };

    // Push computed positions into the Entities agentPool (Entities owns no
    // horizontal movement system) and tick the module's own state (cooldowns,
    // modifiers, ring-out).
    Entity_SetPosition(player->agentId, player->position);
    Entity_SetPosition(enemy->agentId, enemy->position);
    Entity_Update(dt);
}

// Simple in-world HP bar drawn as a billboard-less flat quad above the head.
static void DrawAgentHealthBar3D(Vector3 worldPos, float yOffset, int agentId, Color fillColor) {
    const Agent* agent = Entity_GetAgent(agentId);
    if (agent == NULL) return;

    float ratio = (agent->maxHealth > 0.0f) ? (agent->health / agent->maxHealth) : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    float barWidth = 40.0f;
    float barHeight = 5.0f;
    Vector3 barCenter = { worldPos.x, worldPos.y + yOffset, worldPos.z };

    Vector3 bgMin = { barCenter.x - barWidth * 0.5f, barCenter.y, barCenter.z };
    Vector3 bgMax = { barCenter.x + barWidth * 0.5f, barCenter.y + barHeight, barCenter.z };
    DrawCube((Vector3){ (bgMin.x + bgMax.x) * 0.5f, (bgMin.y + bgMax.y) * 0.5f, barCenter.z }, barWidth, barHeight, 1.0f, ColorAlpha(BLACK, 0.6f));

    if (ratio > 0.0f) {
        float fillWidth = barWidth * ratio;
        Vector3 fillCenter = { bgMin.x + fillWidth * 0.5f, (bgMin.y + bgMax.y) * 0.5f, barCenter.z };
        DrawCube(fillCenter, fillWidth, barHeight, 1.2f, fillColor);
    }
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

    // HP bars sourced from the Entities agentPool (Entity_GetAgent), not the
    // legacy PlayerEntity/EnemyEntity structs (which have no HP field).
    DrawAgentHealthBar3D(enemy->position, 50.0f, enemy->agentId, RED);
    DrawAgentHealthBar3D(player->position, 46.0f, player->agentId, LIME);
}

void DrawSandboxHUD(void) {
    float angleDegrees = g_cameraAngle * 180.0f / 3.1415926535f;
    // Đưa góc về khoảng [0, 360)
    while (angleDegrees < 0.0f) angleDegrees += 360.0f;
    while (angleDegrees >= 360.0f) angleDegrees -= 360.0f;

    // Vị trí HUD góc dưới bên trái (chồng lên chỗ chữ cũ của main.c)
    int hudX = 10;
    int hudY = 475;
    int hudW = 560;
    int hudH = 215;

    // Vẽ panel nền tối sang trọng
    DrawRectangle(hudX, hudY, hudW, hudH, ColorAlpha(GetColor(0x15151CFF), 0.85f));
    DrawRectangleLines(hudX, hudY, hudW, hudH, ColorAlpha(GetColor(0x353545FF), 0.9f));
    
    Font defaultFont = GetFontDefault();
    
    // Tiêu đề (Size 20 để nổi bật, spacing = 2.0f cho giãn chữ thoáng đãng)
    DrawTextEx(defaultFont, "[ SYSTEM & CAMERA STATUS ]", (Vector2){ hudX + 15, hudY + 12 }, 20, 2.0f, YELLOW);
    
    // Thông số Camera (Size 10 pixel-perfect cực nét của Raylib)
    DrawTextEx(defaultFont, TextFormat("Angle: %.1f deg (%.3f rad)", angleDegrees, g_cameraAngle), (Vector2){ hudX + 15, hudY + 38 }, 10, 1.0f, GREEN);
    DrawTextEx(defaultFont, TextFormat("Dist : %.1f | Height: %.1f", g_camDist, g_camHeight), (Vector2){ hudX + 15, hudY + 53 }, 10, 1.0f, SKYBLUE);
    DrawTextEx(defaultFont, TextFormat("Ratio: %.3f (Height/Distance)", g_camHeight / g_camDist), (Vector2){ hudX + 15, hudY + 68 }, 10, 1.0f, MAGENTA);

    // Dòng kẻ phân cách
    DrawLine(hudX + 15, hudY + 88, hudX + hudW - 15, hudY + 88, ColorAlpha(GetColor(0x353545FF), 0.5f));

    // Phím tắt điều khiển (Size 10 pixel-perfect)
    DrawTextEx(defaultFont, "CONTROLS & HOTKEYS:", (Vector2){ hudX + 15, hudY + 98 }, 10, 1.0f, GOLD);
    DrawTextEx(defaultFont, "- Q/E Key        : Rotate Camera around Player", (Vector2){ hudX + 25, hudY + 113 }, 10, 1.0f, LIGHTGRAY);
    DrawTextEx(defaultFont, "- R/F or Scroll  : Zoom Camera (Adjust Dist & Height)", (Vector2){ hudX + 25, hudY + 128 }, 10, 1.0f, LIGHTGRAY);
    DrawTextEx(defaultFont, "- WASD / Arrows  : Move Player (Relative to Camera View)", (Vector2){ hudX + 25, hudY + 143 }, 10, 1.0f, LIGHTGRAY);
    DrawTextEx(defaultFont, "- Space / X Key  : Jump / Dash character", (Vector2){ hudX + 25, hudY + 158 }, 10, 1.0f, LIGHTGRAY);
    DrawTextEx(defaultFont, "- K Key / P Key  : Switch Map / Cycle Enemy AI Mode", (Vector2){ hudX + 25, hudY + 173 }, 10, 1.0f, LIGHTGRAY);
    DrawTextEx(defaultFont, "- Left Mouse     : Cast Selected Element Skill", (Vector2){ hudX + 25, hudY + 188 }, 10, 1.0f, LIGHTGRAY);
}

static void DrawTouchButton(Vector2 center, float radius, const char* label, bool active, Color activeColor) {
    Color btnColor = active ? activeColor : ColorAlpha(DARKGRAY, 0.4f);
    Color outlineColor = active ? WHITE : ColorAlpha(WHITE, 0.5f);
    DrawCircleV(center, radius, btnColor);
    DrawCircleLines(center.x, center.y, radius, outlineColor);
    
    // Vẽ text căn giữa
    int fontSize = (int)(radius * 0.4f);
    if (fontSize < 10) fontSize = 10;
    int textW = MeasureText(label, fontSize);
    DrawText(label, (int)(center.x - textW / 2.0f), (int)(center.y - fontSize / 2.0f), fontSize, WHITE);
}

void DrawSandboxTouchControls(const PlayerEntity* player) {
    if (!g_showTouchControls) return;

    // Thiết lập chế độ vẽ 2D overlay
    rlDrawRenderBatchActive();
    EndShaderMode();
    BeginBlendMode(BLEND_ALPHA);

    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlOrtho(0.0, (double)GetScreenWidth(), (double)GetScreenHeight(), 0.0, -1.0, 1.0);

    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();
    rlSetTexture(0);

    float screenWidth = (float)GetScreenWidth();
    float screenHeight = (float)GetScreenHeight();
    float touchScale = screenHeight / 700.0f;

    // Vị trí/kích thước phím ảo tương ứng với tỷ lệ màn hình
    Vector2 joystickCenter = { 150.0f * touchScale, screenHeight - 150.0f * touchScale };
    float joystickBaseRadius = 65.0f * touchScale;
    float joystickKnobRadius = 30.0f * touchScale;

    Vector2 dashBtnCenter = { screenWidth - 210.0f * touchScale, screenHeight - 100.0f * touchScale };
    float dashBtnRadius = 35.0f * touchScale;

    Vector2 jumpBtnCenter = { screenWidth - 100.0f * touchScale, screenHeight - 100.0f * touchScale };
    float jumpBtnRadius = 45.0f * touchScale;

    Vector2 flyToggleBtnCenter = { screenWidth - 100.0f * touchScale, screenHeight - 210.0f * touchScale };
    float flyToggleBtnRadius = 35.0f * touchScale;

    Vector2 flyUpBtnCenter = { screenWidth - 100.0f * touchScale, screenHeight - 310.0f * touchScale };
    float flyUpBtnRadius = 30.0f * touchScale;

    Vector2 flyDownBtnCenter = { screenWidth - 210.0f * touchScale, screenHeight - 210.0f * touchScale };
    float flyDownBtnRadius = 30.0f * touchScale;

    Vector2 camLeftCenter = { screenWidth - 320.0f * touchScale, screenHeight - 100.0f * touchScale };
    float camLeftRadius = 25.0f * touchScale;

    Vector2 camRightCenter = { screenWidth - 320.0f * touchScale, screenHeight - 170.0f * touchScale };
    float camRightRadius = 25.0f * touchScale;

    // 1. Vẽ Joystick ảo
    DrawCircleV(joystickCenter, joystickBaseRadius, ColorAlpha(DARKGRAY, 0.3f));
    DrawCircleLines(joystickCenter.x, joystickCenter.y, joystickBaseRadius, ColorAlpha(WHITE, 0.4f));
    
    Vector2 knobPos = Vector2Add(joystickCenter, g_joystickKnobOffset);
    Color knobColor = g_joystickActive ? ColorAlpha(SKYBLUE, 0.8f) : ColorAlpha(LIGHTGRAY, 0.6f);
    DrawCircleV(knobPos, joystickKnobRadius, knobColor);
    DrawCircleLines(knobPos.x, knobPos.y, joystickKnobRadius, ColorAlpha(WHITE, 0.7f));

    // 2. Vẽ các phím chức năng
    DrawTouchButton(dashBtnCenter, dashBtnRadius, "DASH", g_touchDashActive, ColorAlpha(ORANGE, 0.8f));
    DrawTouchButton(jumpBtnCenter, jumpBtnRadius, "JUMP", g_touchJumpActive, ColorAlpha(LIME, 0.8f));
    
    if (player->isFlying) {
        DrawTouchButton(flyToggleBtnCenter, flyToggleBtnRadius, "LAND", g_touchFlyToggleActive, ColorAlpha(RED, 0.8f));
        DrawTouchButton(flyUpBtnCenter, flyUpBtnRadius, "UP", g_touchFlyUpActive, ColorAlpha(GOLD, 0.8f));
        DrawTouchButton(flyDownBtnCenter, flyDownBtnRadius, "DOWN", g_touchFlyDownActive, ColorAlpha(PURPLE, 0.8f));
    } else {
        DrawTouchButton(flyToggleBtnCenter, flyToggleBtnRadius, "FLY", g_touchFlyToggleActive, ColorAlpha(SKYBLUE, 0.8f));
    }

    // 3. Vẽ phím xoay camera
    DrawTouchButton(camLeftCenter, camLeftRadius, "CAM <", g_touchCamLeftActive, ColorAlpha(BLUE, 0.7f));
    DrawTouchButton(camRightCenter, camRightRadius, "CAM >", g_touchCamRightActive, ColorAlpha(BLUE, 0.7f));

    // Nhãn hướng dẫn bật/tắt phím ảo ở trên PC
#if !defined(PLATFORM_ANDROID)
    DrawText("Press TAB to toggle Touch UI", 10, 10, 15, ColorAlpha(RAYWHITE, 0.7f));
#endif

    // Khôi phục ma trận chiếu và ma trận modelview
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlPopMatrix();

    EndBlendMode();
}

