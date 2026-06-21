#include "raylib.h"
#include <math.h>
#include "skill_manager.h"

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
    Rectangle btnElectric = { 700, 20, 150, 45 }; // Khung nút hệ Lôi

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        Vector2 mousePos = GetMousePosition();

        // Cập nhật vị trí Enemy dao động nhẹ nhàng lên xuống
        float time = GetTime();
        if (IsAnySkillCoiling()) {
            // Giảm biên độ dao động về 0 nhanh chóng (bị trói cứng)
            enemyOscillationScale += (0.0f - enemyOscillationScale) * 9.0f * dt;
        } else {
            // Phục hồi dao động từ từ
            enemyOscillationScale += (1.0f - enemyOscillationScale) * 2.0f * dt;
        }
        enemyPos.x = 900.0f; // Reset x trước khi giật điện rung lắc
        float oscillationFreq = IsEnemySlowed() ? 0.8f : 2.5f; // Bị làm chậm thì dao động chậm lại
        enemyPos.y = 350.0f + sinf(time * oscillationFreq) * 100.0f * enemyOscillationScale;

        if (IsAnySkillShocking()) {
            enemyPos.x += (float)GetRandomValue(-6, 6);
            enemyPos.y += (float)GetRandomValue(-6, 6);
        }

        // 1. KIỂM TRA TƯƠNG TÁC UI
        bool hoverWater = CheckCollisionPointRec(mousePos, btnWater);
        bool hoverMetal = CheckCollisionPointRec(mousePos, btnMetal);
        bool hoverFire  = CheckCollisionPointRec(mousePos, btnFire);
        bool hoverWood  = CheckCollisionPointRec(mousePos, btnWood); // Hover hệ Mộc
        bool hoverElectric = CheckCollisionPointRec(mousePos, btnElectric); // Hover hệ Lôi
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
            else if (hoverElectric) {
                activeSkill = SKILL_ELECTRIC;
                clickedOnUI = true;
            }

            // 2. NẾU KHÔNG CLICK VÀO UI -> PHÓNG CHIÊU THỨC
            if (!clickedOnUI) {
                Vector2 target = enemyPos;

                // Cấu hình tham số chiêu thức
                SkillParams params = {0};
                params.level = 1;
                params.milestone = 1;
                params.sizeScale = selectedSize;
                params.damage = 100.0f;
                params.quantity = selectedQty;

                CastSkill(activeSkill, characterPos, target, params);
            }
        }

        // Cập nhật vật lý và va chạm chiêu thức qua SkillManager
        UpdateSkillManager(dt, enemyPos, 35.0f);

        // --- VẼ MÀN HÌNH ---
        BeginDrawing();
            ClearBackground(GetColor(0x111111FF)); 

            // Xác định màu sắc Enemy dựa theo hiệu ứng khống chế đang chịu phải
            Color enemyBaseColor = GetColor(0x8B2500FF);
            Color enemyBorderColor = GetColor(0xCD3700FF);
            Color enemyCoreColor = GetColor(0xFF5500FF);
            if (IsEnemySlowed()) {
                enemyBaseColor = GetColor(0x1B4F72FF); // Màu xanh dương sâu lắng
                enemyBorderColor = SKYBLUE;
            } else if (IsEnemyBurning()) {
                enemyBaseColor = RED;
                enemyBorderColor = ORANGE;
                enemyCoreColor = YELLOW;
            }

            // Vẽ Enemy trước làm nền, để các hiệu ứng chiêu thức (quấn rễ) đè lên trên thân Enemy
            DrawCircleV(enemyPos, 35, enemyBaseColor); 
            DrawCircleLinesV(enemyPos, 35, enemyBorderColor); 
            DrawCircleV(enemyPos, 8, enemyCoreColor); 
            DrawText("ENEMY", (int)enemyPos.x - 22, (int)enemyPos.y - 6, 12, WHITE);

            // Vẽ các chiêu thức đang bay và chữ sát thương qua SkillManager
            DrawSkillManager();

            // Vẽ nhân vật (cục tròn đại diện)
            DrawCircleV(characterPos, 30, GRAY); 
            // Vẽ thêm 1 chấm đỏ nhỏ ở tâm nhân vật cho giống cái nòng súng
            DrawCircleV(characterPos, 5, MAROON);

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

            // Vẽ nút Hệ Lôi
            Color colorElectric = activeSkill == SKILL_ELECTRIC ? GetColor(0x8B008BFF) : (hoverElectric ? GetColor(0xBA55D3FF) : GetColor(0x4B0082FF));
            DrawRectangleRounded(btnElectric, 0.3f, 10, colorElectric);
            DrawRectangleRoundedLines(btnElectric, 0.3f, 10, WHITE);
            DrawText("ELEC SKILL", (int)btnElectric.x + 30, (int)btnElectric.y + 15, 15, WHITE);
            
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
            DrawText("Click screen to attack the oscillating Enemy.", 20, 80, 16, LIGHTGRAY);
            DrawText(TextFormat("Active parameters: Qty = %d | Size = %.1fx", selectedQty, selectedSize), 20, 105, 16, GetColor(0x55DD66FF));
            DrawText("Selected element affects floating damage text & monster status effects", 20, 130, 16, GetColor(0xBA55D3FF));

        EndDrawing();
    }

    // Dọn dẹp RAM/VRAM qua hệ thống quản lý
    UnloadSkillManager();
    CloseWindow();
    
    return 0;
}