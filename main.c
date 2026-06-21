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

static void DrawCharacter25D(Vector2 groundPos, float charZ, float radius, Color skinCol, Color clothesCol, Color outlineCol, bool isPlayer, float aimAngle) {
    // Tọa độ vẽ thân/đầu (ở trên không nếu charZ > 0)
    Vector2 visualPos = { groundPos.x, groundPos.y - charZ };
    
    // 1. Vẽ Chân (hai chân bằng hình elip nhỏ, di chuyển theo visualPos)
    Vector2 leftFoot = { visualPos.x - 8.0f, visualPos.y };
    Vector2 rightFoot = { visualPos.x + 8.0f, visualPos.y };
    DrawEllipse((int)leftFoot.x, (int)leftFoot.y, 5, 3, outlineCol);
    DrawEllipse((int)rightFoot.x, (int)rightFoot.y, 5, 3, outlineCol);

    // 2. Vẽ Thân (áo quần)
    Rectangle bodyRect = { visualPos.x - 15, visualPos.y - 38, 30, 36 };
    DrawRectangleRounded(bodyRect, 0.4f, 4, clothesCol);
    DrawRectangleRoundedLines(bodyRect, 0.4f, 4, outlineCol);

    // 3. Vẽ Đầu
    Vector2 headPos = { visualPos.x, visualPos.y - 48 };
    DrawCircleV(headPos, 10.0f, skinCol);
    DrawCircleLines((int)headPos.x, (int)headPos.y, 10.0f, outlineCol);

    // 4. Vẽ Tay (Arms/Hands)
    if (isPlayer) {
        // Tay cầm chiêu thức chỉ hướng xoay theo aimAngle
        Vector2 armOrigin = { visualPos.x, visualPos.y - 25.0f }; // Khớp vai
        Vector2 handPos = {
            armOrigin.x + cosf(aimAngle) * 22.0f,
            armOrigin.y + sinf(aimAngle) * 22.0f
        };
        // Vẽ cánh tay bằng nét dày, bàn tay bằng hình tròn
        DrawLineEx(armOrigin, handPos, 4.0f, clothesCol);
        DrawCircleV(handPos, 5.0f, skinCol);
        DrawCircleLines((int)handPos.x, (int)handPos.y, 5.0f, outlineCol);
        
        // Tay còn lại thả tự nhiên bên sườn
        Vector2 restHand = { visualPos.x - 18.0f, visualPos.y - 20.0f };
        DrawLineEx((Vector2){ visualPos.x - 12.0f, visualPos.y - 30.0f }, restHand, 3.5f, clothesCol);
        DrawCircleV(restHand, 4.5f, skinCol);
        DrawCircleLines((int)restHand.x, (int)restHand.y, 4.5f, outlineCol);
    } else {
        // Enemy buông thõng 2 tay bên sườn
        Vector2 leftHand = { visualPos.x - 18.0f, visualPos.y - 20.0f };
        Vector2 rightHand = { visualPos.x + 18.0f, visualPos.y - 20.0f };
        DrawLineEx((Vector2){ visualPos.x - 12.0f, visualPos.y - 30.0f }, leftHand, 3.5f, clothesCol);
        DrawLineEx((Vector2){ visualPos.x + 12.0f, visualPos.y - 30.0f }, rightHand, 3.5f, clothesCol);
        DrawCircleV(leftHand, 4.5f, skinCol);
        DrawCircleV(rightHand, 4.5f, skinCol);
        DrawCircleLines((int)leftHand.x, (int)leftHand.y, 4.5f, outlineCol);
        DrawCircleLines((int)rightHand.x, (int)rightHand.y, 4.5f, outlineCol);
    }
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

            // Kiểm tra click vào nút chọn nguồn xuất chiêu (Anchor)
            for (int i = 0; i < 2; i++) {
                if (CheckCollisionPointRec(mousePos, rectAnchor[i])) {
                    selectedAnchor = (CastAnchorType)i;
                    clickedOnUI = true;
                }
            }
            // Kiểm tra click vào nút chọn đường đi (Path)
            for (int i = 0; i < 3; i++) {
                if (CheckCollisionPointRec(mousePos, rectPath[i])) {
                    selectedPath = (CastPathType)i;
                    clickedOnUI = true;
                }
            }

            // Kiểm tra click vào nút chọn bật/tắt Portals
            if (CheckCollisionPointRec(mousePos, rectPortalToggle)) {
                showPortals = !showPortals;
                clickedOnUI = true;
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
                params.anchorType = selectedAnchor;
                params.pathType = selectedPath;
                params.showPortal = showPortals;

                // Bắn từ tọa độ chân đất của nhân vật, truyền độ cao nhảy charZ vào params
                Vector2 startCastPos = characterPos; // Chân đất của nhân vật
                params.casterZ = charZ;
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

            // Enemy luôn ở dưới đất không lơ lửng bồng bềnh nữa
            float enemyZ = 0.0f;

            // Vẽ bóng đổ của Enemy dưới mặt đất
            DrawEllipse((int)enemyPos.x, (int)enemyPos.y, 36, 14, ColorAlpha(BLACK, 0.5f));

            // Vẽ bóng đổ của Player dưới mặt đất (co giãn theo độ cao nhảy charZ, không bị lệch khe hở)
            float shadowScale = 1.0f - (charZ / 350.0f);
            if (shadowScale < 0.2f) shadowScale = 0.2f;
            DrawEllipse((int)characterPos.x, (int)characterPos.y, (int)(32.0f * shadowScale), (int)(12.8f * shadowScale), ColorAlpha(BLACK, 0.5f * shadowScale));

            // Vẽ đường dóng thẳng đứng biểu diễn độ cao (Height Lines) khi đang ở trên không
            if (charZ > 5.0f) {
                DrawLine((int)characterPos.x, (int)characterPos.y, (int)characterPos.x, (int)(characterPos.y - charZ), ColorAlpha(GRAY, 0.4f));
            }
            if (enemyZ > 5.0f) {
                DrawLine((int)enemyPos.x, (int)enemyPos.y, (int)enemyPos.x, (int)(enemyPos.y - enemyZ), ColorAlpha(GRAY, 0.4f));
            }

            // Vẽ vòng định vị mặt đất (Ground Rings / Feet indicators) luôn ghim chặt dưới sàn
            DrawEllipseLines((int)characterPos.x, (int)characterPos.y, 25, 10, ColorAlpha(LIME, 0.5f));
            DrawEllipseLines((int)enemyPos.x, (int)enemyPos.y, 30, 12, ColorAlpha(RED, 0.5f));

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
                        Color pOutline = GetColor(0xCCCCCCFF);

                        // Đổi màu viền nếu đang lướt
                        if (isDashing) {
                            if (activeSkill == SKILL_METAL) pOutline = GetColor(0xFFD700FF);
                            else if (activeSkill == SKILL_FIRE) pOutline = RED;
                            else if (activeSkill == SKILL_WOOD) pOutline = GREEN;
                            else if (activeSkill == SKILL_ELECTRIC) pOutline = GOLD;
                            else pOutline = SKYBLUE;
                        }

                        float aimAngle = atan2f(mousePos.y - (characterPos.y - charZ - 25.0f), mousePos.x - characterPos.x);
                        DrawCharacter25D(characterPos, charZ, 25.0f, GetColor(0xFFD39BFF), GetColor(0x3B5998FF), pOutline, true, aimAngle);
                        break;
                    }
                    case ENTITY_ENEMY: {
                        Color eClothes = GetColor(0x8B2500FF);
                        Color eOutline = GetColor(0xFF5500FF);

                        if (IsEnemySlowed()) {
                            eClothes = GetColor(0x1B4F72FF);
                            eOutline = SKYBLUE;
                        } else if (IsEnemyBurning()) {
                            eClothes = RED;
                            eOutline = YELLOW;
                        }

                        float enemyZ = 0.0f; // Đứng trên mặt phẳng đất

                        DrawCharacter25D(enemyPos, enemyZ, 30.0f, GetColor(0xFFC0CBFF), eClothes, eOutline, false, 0.0f);

                        // Vẽ chữ ENEMY trên đầu nhân vật
                        DrawText("ENEMY", (int)enemyPos.x - 22, (int)(enemyPos.y - 65), 12, WHITE);
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

            // Text hướng dẫn
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

    // Dọn dẹp RAM/VRAM qua hệ thống quản lý
    UnloadSkillManager();
    CloseWindow();
    
    return 0;
}