#include "ui_panel.h"
#include "rlgl.h" // BẮT BUỘC THÊM: Để can thiệp đồng bộ ma trận tầng sâu
#include <stdio.h>

static Rectangle rectQty[5];
static Rectangle rectSize[3];
static float sizes[3] = {1.0f, 1.5f, 2.0f};

static Rectangle rectAnchor[2];
static Rectangle rectPath[3];
static Rectangle rectPortalToggle;
static Rectangle skillButtons[32];

static int hoverSkillIndex = -1;

void InitUIPanel(void) {
  for (int i = 0; i < 5; i++) {
    rectQty[i] = (Rectangle){870 + i * 60, 20, 50, 35};
  }
  for (int i = 0; i < 3; i++) {
    rectSize[i] = (Rectangle){870 + i * 100, 70, 90, 35};
  }
  for (int i = 0; i < 2; i++) {
    rectAnchor[i] = (Rectangle){870 + i * 150, 120, 140, 35};
  }
  for (int i = 0; i < 3; i++) {
    rectPath[i] = (Rectangle){870 + i * 100, 170, 95, 35};
  }
  rectPortalToggle = (Rectangle){870, 220, 200, 35};

  int skillCount = GetRegisteredSkillCount();
  if (skillCount > 32)
    skillCount = 32;
  for (int i = 0; i < skillCount; i++) {
    skillButtons[i] = (Rectangle){20 + i * 135, 20, 125, 45};
  }
}

void UpdateUIPanel(Vector2 mousePos, UIPanelState *state) {
  state->clickedOnUI = false;
  hoverSkillIndex = -1;

  int availableSkills[] = {0, 1, 2, 3, 4, 5, 6};
  int availableCount = 7;
  bool activeValid = false;
  for (int i = 0; i < availableCount; i++) {
    if (state->activeSkillIndex == availableSkills[i]) {
      activeValid = true;
      break;
    }
  }
  if (!activeValid) {
    state->activeSkillIndex = availableSkills[0];
  }

  // Kiểm tra hover nút chiêu thức
  for (int i = 0; i < availableCount; i++) {
    if (CheckCollisionPointRec(mousePos, skillButtons[i])) {
      hoverSkillIndex = i;
      break;
    }
  }

  // CHỐT CHẶN HOVER CHỐNG CLICK XUYÊN UI XUỐNG ĐẤT
  bool isMouseOverAnyUI = false;
  if (hoverSkillIndex != -1)
    isMouseOverAnyUI = true;
  for (int i = 0; i < 5; i++)
    if (CheckCollisionPointRec(mousePos, rectQty[i]))
      isMouseOverAnyUI = true;
  for (int i = 0; i < 3; i++)
    if (CheckCollisionPointRec(mousePos, rectSize[i]))
      isMouseOverAnyUI = true;
  for (int i = 0; i < 2; i++)
    if (CheckCollisionPointRec(mousePos, rectAnchor[i]))
      isMouseOverAnyUI = true;
  for (int i = 0; i < 3; i++)
    if (CheckCollisionPointRec(mousePos, rectPath[i]))
      isMouseOverAnyUI = true;
  if (CheckCollisionPointRec(mousePos, rectPortalToggle))
    isMouseOverAnyUI = true;

  if (isMouseOverAnyUI) {
    state->clickedOnUI = true;
  }

  // Xử lý click chuột trái
  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    for (int i = 0; i < 5; i++) {
      if (CheckCollisionPointRec(mousePos, rectQty[i])) {
        state->currentParams.quantity = i + 1;
      }
    }
    for (int i = 0; i < 3; i++) {
      if (CheckCollisionPointRec(mousePos, rectSize[i])) {
        state->currentParams.sizeScale = sizes[i];
      }
    }
    for (int i = 0; i < 2; i++) {
      if (CheckCollisionPointRec(mousePos, rectAnchor[i])) {
        state->currentParams.anchorType = (CastAnchorType)i;
      }
    }
    for (int i = 0; i < 3; i++) {
      if (CheckCollisionPointRec(mousePos, rectPath[i])) {
        state->currentParams.pathType = (CastPathType)i;
      }
    }
    if (CheckCollisionPointRec(mousePos, rectPortalToggle)) {
      state->currentParams.showPortal = !state->currentParams.showPortal;
    }
    if (hoverSkillIndex != -1) {
      state->activeSkillIndex = availableSkills[hoverSkillIndex];
    }
  }
}

void DrawUIPanel(const UIPanelState *state) {
  // =========================================================================
  // SIÊU LÁ CHẮN ĐỒ HỌA: ĐỒNG BỘ LẠI TOÀN BỘ PIPELINE 2D CHỐNG SAI LỆCH MA TRẬN
  // =========================================================================
  rlDrawRenderBatchActive(); // Giải phóng hoàn toàn các phần tử cũ khỏi bộ đệm
                             // vẽ
  EndShaderMode();             // Ép ngắt kết nối mọi Shader rò rỉ
  BeginBlendMode(BLEND_ALPHA); // Đưa Blend Mode về Alpha mặc định cho UI 2D

  // Cưỡng chế nạp lại Ma trận hình chiếu 2D phẳng (Orthographic) chuẩn màn hình
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlOrtho(0.0, (double)GetScreenWidth(), (double)GetScreenHeight(), 0.0, -1.0,
          1.0);

  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlSetTexture(0); // Xóa bỏ kết cấu cũ tránh nhiễm màu hình ảnh rồng
  // =========================================================================

  int availableSkills[] = {0, 1, 2, 3, 4, 5, 6};
  int availableCount = 7;

  Vector2 mousePos = GetMousePosition();

  // Vẽ nút chiêu thức
  for (int i = 0; i < availableCount; i++) {
    int skillIdx = availableSkills[i];
    bool isSelected = (state->activeSkillIndex == skillIdx);
    bool isHover = (hoverSkillIndex == i);
    Color baseColor = GetRegisteredSkillColor(skillIdx);

    Color btnColor = isSelected ? baseColor
                                : (isHover ? ColorAlpha(baseColor, 0.6f)
                                           : ColorAlpha(baseColor, 0.3f));

    DrawRectangleRounded(skillButtons[i], 0.3f, 10, btnColor);
    DrawRectangleRoundedLines(skillButtons[i], 0.3f, 10, WHITE);

    const char *skillName = GetRegisteredSkillName(skillIdx);
    char btnText[64];
    snprintf(btnText, sizeof(btnText), "%s SKILL", skillName);

    int textWidth = MeasureText(btnText, 12);
    DrawText(btnText,
             (int)(skillButtons[i].x + (skillButtons[i].width - textWidth) / 2),
             (int)(skillButtons[i].y + 15), 12, WHITE);
  }

  // Quantity
  DrawText("Quantity:", 770, 30, 16, LIGHTGRAY);
  for (int i = 0; i < 5; i++) {
    bool isSelected = (state->currentParams.quantity == (i + 1));
    bool isHover = CheckCollisionPointRec(mousePos, rectQty[i]);
    Color btnCol = isSelected ? PURPLE : (isHover ? DARKPURPLE : DARKGRAY);
    DrawRectangleRounded(rectQty[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectQty[i], 0.2f, 10, WHITE);
    DrawText(TextFormat("%d", i + 1), (int)rectQty[i].x + 20,
             (int)rectQty[i].y + 10, 15, WHITE);
  }

  // Size
  DrawText("Size:", 770, 80, 16, LIGHTGRAY);
  for (int i = 0; i < 3; i++) {
    bool isSelected = (state->currentParams.sizeScale == sizes[i]);
    bool isHover = CheckCollisionPointRec(mousePos, rectSize[i]);
    Color btnCol = isSelected ? ORANGE : (isHover ? GOLD : DARKGRAY);
    DrawRectangleRounded(rectSize[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectSize[i], 0.2f, 10, WHITE);

    const char *sizeText = (i == 0) ? "1.0x" : (i == 1) ? "1.5x" : "2.0x";
    DrawText(sizeText, (int)rectSize[i].x + 25, (int)rectSize[i].y + 10, 15,
             WHITE);
  }

  // Anchor
  DrawText("Anchor:", 770, 130, 16, LIGHTGRAY);
  const char *anchorNames[] = {"CASTER (SELF)", "TARGET (ENEMY)"};
  for (int i = 0; i < 2; i++) {
    bool isSelected = (state->currentParams.anchorType == (CastAnchorType)i);
    bool isHover = CheckCollisionPointRec(mousePos, rectAnchor[i]);
    Color btnCol = isSelected ? BLUE : (isHover ? DARKBLUE : DARKGRAY);
    DrawRectangleRounded(rectAnchor[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectAnchor[i], 0.2f, 10, WHITE);
    int textW = MeasureText(anchorNames[i], 12);
    DrawText(anchorNames[i],
             (int)(rectAnchor[i].x + (rectAnchor[i].width - textW) / 2),
             (int)rectAnchor[i].y + 12, 12, WHITE);
  }

  // Path
  DrawText("Path:", 770, 180, 16, LIGHTGRAY);
  const char *pathNames[] = {"PROJECTILE", "FALLING", "RISING"};
  for (int i = 0; i < 3; i++) {
    bool isSelected = (state->currentParams.pathType == (CastPathType)i);
    bool isHover = CheckCollisionPointRec(mousePos, rectPath[i]);
    Color btnCol = isSelected ? MAROON : (isHover ? RED : DARKGRAY);
    DrawRectangleRounded(rectPath[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectPath[i], 0.2f, 10, WHITE);
    int textW = MeasureText(pathNames[i], 12);
    DrawText(pathNames[i],
             (int)(rectPath[i].x + (rectPath[i].width - textW) / 2),
             (int)rectPath[i].y + 12, 12, WHITE);
  }

  // Portals
  bool ptHover = CheckCollisionPointRec(mousePos, rectPortalToggle);
  Color ptCol =
      state->currentParams.showPortal ? DARKGREEN : (ptHover ? LIME : DARKGRAY);
  DrawRectangleRounded(rectPortalToggle, 0.2f, 10, ptCol);
  DrawRectangleRoundedLines(rectPortalToggle, 0.2f, 10, WHITE);
  DrawText(state->currentParams.showPortal ? "PORTALS: ON" : "PORTALS: OFF",
           (int)rectPortalToggle.x + 40, (int)rectPortalToggle.y + 10, 16,
           WHITE);
}