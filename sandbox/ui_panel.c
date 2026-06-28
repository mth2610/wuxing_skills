#include "sandbox/ui_panel.h"
#include "rlgl.h"
#include <stdio.h>

static Rectangle rectQty[5];
static Rectangle rectSize[3];
static float sizes[3] = {1.0f, 1.5f, 2.0f};

static Rectangle rectAnchor[2];
static Rectangle rectPath[3];
static Rectangle rectPortalToggle;
static Rectangle skillButtons[64];
static Rectangle togglePanelBtn;

static int hoverSkillIndex = -1;

void InitUIPanel(void) {
  togglePanelBtn = (Rectangle){20, 15, 180, 32};

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
  if (skillCount > 64)
    skillCount = 64;

  // Sắp xếp lưới nút bấm cho tối đa 64 kỹ năng (6 cột) để tránh tràn màn hình
  float buttonWidth = 110.0f;
  float buttonHeight = 35.0f;
  float spacingX = 8.0f;
  float spacingY = 8.0f;
  int columns = 6;

  for (int i = 0; i < skillCount; i++) {
    int col = i % columns;
    int row = i / columns;
    skillButtons[i] = (Rectangle){
        20.0f + col * (buttonWidth + spacingX),
        60.0f + row * (buttonHeight + spacingY),
        buttonWidth,
        buttonHeight
    };
  }
}

void UpdateUIPanel(Vector2 mousePos, UIPanelState *state) {
  state->clickedOnUI = false;
  hoverSkillIndex = -1;

  // Kiểm tra click vào nút Ẩn/Hiện Bảng Điều Khiển đầu tiên
  bool isOverToggleBtn = CheckCollisionPointRec(mousePos, togglePanelBtn);
  if (isOverToggleBtn) {
    state->clickedOnUI = true;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      state->isPanelOpen = !state->isPanelOpen;
      return;
    }
  }

  // Nếu bảng điều khiển đang đóng, dừng xử lý các nút bấm khác để click xuyên qua đất
  if (!state->isPanelOpen) {
    return;
  }

  // Tự động sử dụng số lượng kỹ năng đã đăng ký thực tế (tối đa 64)
  int availableCount = GetRegisteredSkillCount();
  if (availableCount > 64) availableCount = 64;

  bool activeValid = false;
  for (int i = 0; i < availableCount; i++) {
    if (state->activeSkillIndex == i) {
      activeValid = true;
      break;
    }
  }
  if (!activeValid && availableCount > 0) {
    state->activeSkillIndex = 0;
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

  // Xử lý click chuột trái vào các bảng thông số
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
      state->activeSkillIndex = hoverSkillIndex;
    }
  }
}

void DrawUIPanel(const UIPanelState *state) {
  rlDrawRenderBatchActive();
  EndShaderMode();
  BeginBlendMode(BLEND_ALPHA);

  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlOrtho(0.0, (double)GetScreenWidth(), (double)GetScreenHeight(), 0.0, -1.0,
          1.0);

  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlSetTexture(0);

  Vector2 mousePos = GetMousePosition();

  // Vẽ nút Ẩn/Hiện Bảng Điều Khiển
  bool isOverToggle = CheckCollisionPointRec(mousePos, togglePanelBtn);
  Color toggleCol = state->isPanelOpen ? (isOverToggle ? RED : MAROON) : (isOverToggle ? LIME : DARKGREEN);
  DrawRectangleRounded(togglePanelBtn, 0.2f, 10, toggleCol);
  DrawRectangleRoundedLines(togglePanelBtn, 0.2f, 10, WHITE);
  
  const char *toggleText = state->isPanelOpen ? "[X] AN BANG DIEU KHIEN" : "[+] HIEN BANG DIEU KHIEN";
  int toggleTextW = MeasureText(toggleText, 11);
  DrawText(toggleText, (int)(togglePanelBtn.x + (togglePanelBtn.width - toggleTextW) / 2), (int)togglePanelBtn.y + 10, 11, WHITE);

  // Nếu bảng điều khiển đang đóng, không vẽ gì thêm
  if (!state->isPanelOpen) {
    EndBlendMode();
    return;
  }

  // Đồng bộ danh sách hiển thị với các chiêu thức thực tế (tối đa 64)
  int availableCount = GetRegisteredSkillCount();
  if (availableCount > 64) availableCount = 64;

  // Vẽ nút chiêu thức
  for (int i = 0; i < availableCount; i++) {
    int skillIdx = i;
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
    snprintf(btnText, sizeof(btnText), "%s", skillName);

    int textWidth = MeasureText(btnText, 9);
    DrawText(btnText,
             (int)(skillButtons[i].x + (skillButtons[i].width - textWidth) / 2),
             (int)(skillButtons[i].y + 13), 9, WHITE);
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

  EndBlendMode();
}