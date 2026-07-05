#include "sandbox/ui_panel.h"
#include "compute/gpu_particle_system.h"
#include "core/tuning.h"
#include "core/resource_manager.h"
#include "rlgl.h"
#include <stdio.h>
#include <string.h>

// Real TTF font instead of raylib's small-size-unreadable default bitmap
// font — drop a .ttf at assets/fonts/ui_font.ttf to pick it up (falls back
// to the default font automatically if that file isn't there yet, see
// ResourceManager_LoadFont). baseSize 32 keeps the atlas sharp when drawn
// smaller via DrawTextEx.
static Font s_uiFont;

static void UIText(const char *text, float x, float y, float fontSize, Color color) {
  DrawTextEx(s_uiFont, text, (Vector2){x, y}, fontSize, 1.0f, color);
}
static float UITextWidth(const char *text, float fontSize) {
  return MeasureTextEx(s_uiFont, text, fontSize, 1.0f).x;
}

static Rectangle rectQty[5];
static Rectangle rectSize[3];
static float sizes[3] = {1.0f, 1.5f, 2.0f};

static Rectangle rectAnchor[2];
static Rectangle rectPath[3];
static Rectangle rectPortalToggle;
static Rectangle skillButtons[64];
static Rectangle togglePanelBtn;
static Rectangle backBtn;

static int hoverSkillIndex = -1;
static int skillOrder[64];
static int draggedSkillSlot = -1;
static Vector2 dragOffset = { 0, 0 };

// --- Sandbox live-tuning panel (RegisterSkillTunables consumers only) ---
// Refreshed whenever the selected skill changes; drag a slider to write
// through SkillTunableEntry.value (constant-kind) or SkillCurve.stops[k].value
// (curve-kind, one slider per fixed keyframe t = 0/25/50/75/100%) directly
// into the skill's own static storage, click Save to persist to that skill's
// .tuning file (core/tuning.h) via SkillTunables_Flatten. Entries are grouped
// by SkillTunableEntry.phase (a section header per distinct tag, in
// registration order) — entries with phase == NULL render ungrouped, at the
// top, with no header (today's behavior for any skill that doesn't opt in).
static SkillTunableEntry currentTunables[MAX_SKILL_TUNABLES];
static int currentTunableCount = 0;
static int lastTunableSkillIndex = -2; // -2: "never synced" (distinct from -1 = no skill)
static Rectangle rectTunableSave;

// One draggable slot per slider: constant-kind entries get 1 slot
// (keyIndex == -1, bound to *value); curve-kind entries get SKILL_CURVE_KEYS
// slots (keyIndex 0..4, bound to curve->stops[keyIndex].value).
typedef struct {
  int entryIndex;
  int keyIndex; // -1 = constant-kind; else index into curve->stops
  Rectangle rect;
} TunableSliderSlot;
static TunableSliderSlot tunableSlots[MAX_SKILL_TUNABLES * SKILL_CURVE_KEYS];
static int tunableSlotCount = 0;
static int draggedTunableSlot = -1;

// Row-top Y per entry (content-space, i.e. BEFORE the scroll offset — see
// tunableScrollY), content-space here meaning relative to the CURRENT tab
// only (see tab system below) — switching tabs rebuilds this from scratch.
static float entryRowY[MAX_SKILL_TUNABLES];

// One tab per distinct SkillTunableEntry.phase seen (in registration order);
// entries with phase == NULL (legacy/ungrouped) share one "GENERAL" tab.
// Replaces the old single-scroll-list-with-inline-headers layout — with
// skills now carrying 100-185 tunables across 4+ phases, scrolling past
// three other phases to reach the fourth was the opposite of "gọn gàng".
#define TUNABLE_MAX_TABS 16
static const char *tunableTabNames[TUNABLE_MAX_TABS]; // NULL entries render as "GENERAL"
static int tunableTabCount = 0;
static int activeTunableTab = 0;
static Rectangle tunableTabRects[TUNABLE_MAX_TABS];

// Tuning panel geometry — single column, vertically scrollable within the
// active tab. A column-wrapping layout was tried first but still ran off
// both the bottom AND right edge on a real (smaller-than-assumed) window —
// a scrollbar is the only approach that's correct regardless of window size
// or how many tunables a phase ends up with.
// Top Y values tightened (was 340/380) to match the compacted control block
// above (Quantity..GPU status now ends around y=240 instead of y=340).
#define TUNABLE_PANEL_X 770.0f
#define TUNABLE_TAB_ROW_TOP 280.0f
#define TUNABLE_TAB_ROW_H 30.0f
#define TUNABLE_PANEL_TOP 320.0f
#define TUNABLE_LABEL_ROW_H 22.0f
#define TUNABLE_SLIDER_ROW_H 28.0f
#define TUNABLE_ROW_GAP 14.0f
#define TUNABLE_BOTTOM_MARGIN 20.0f

static float tunableScrollY = 0.0f;   // 0 = scrolled to top
static float tunableContentH = 0.0f;  // total content height (content-space)

static float TunableViewportH(void) {
  float h = (float)GetScreenHeight() - TUNABLE_PANEL_TOP - TUNABLE_BOTTOM_MARGIN;
  return (h > 0.0f) ? h : 0.0f;
}
// Slider width now fills most of the available window width (was a fixed
// 380px) — "expand to the max of the box" for real drag room, instead of a
// cramped fixed-width column. Leaves room for the scrollbar (~20px) and a
// right margin; floors at 200px so a very narrow window doesn't invert.
static float TunableSliderWidth(void) {
  float w = (float)GetScreenWidth() - TUNABLE_PANEL_X - 60.0f;
  return (w > 200.0f) ? w : 200.0f;
}
static float TunableMaxScroll(void) {
  float over = tunableContentH - TunableViewportH();
  return (over > 0.0f) ? over : 0.0f;
}
// The mouse-collidable/drawable rect for slot/header index N is its stored
// content-space rect shifted up by the current scroll offset.
static Rectangle TunableScrolled(Rectangle r) {
  r.y -= tunableScrollY;
  return r;
}

// Pilot skills only (see plan: fire_ball, thunder_orb_skill) — extend this
// list as more skills adopt RegisterSkillTunables.
static const char *TunableConfigPathForSkill(int skillIndex) {
  const char *name = GetRegisteredSkillName(skillIndex);
  if (name == NULL) return NULL;
  if (strcmp(name, "FIRE") == 0) return "skills/fire/fire_ball/fire_ball.tuning";
  if (strcmp(name, "THUNDER_ORB") == 0) return "skills/metal/thunder_orb_skill/thunder_orb_skill.tuning";
  if (strcmp(name, "METAL_WIRE_ORB") == 0) return "skills/metal/metal_wire_orb_skill/metal_wire_orb_skill.tuning";
  return NULL;
}

static bool TunableSamePhase(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

// Lays out only the entries belonging to tunableTabNames[activeTunableTab]
// (NULL = the "GENERAL" bucket for legacy ungrouped entries). Called on
// skill change AND every tab click — cheap (at most MAX_SKILL_TUNABLES
// entries to scan).
static void RefreshTunableTabContent(void) {
  draggedTunableSlot = -1;
  tunableSlotCount = 0;
  tunableScrollY = 0.0f;

  const char *tab = (tunableTabCount > 0) ? tunableTabNames[activeTunableTab] : NULL;
  float y = TUNABLE_PANEL_TOP;

  for (int i = 0; i < currentTunableCount; i++) {
    const SkillTunableEntry *e = &currentTunables[i];
    if (!TunableSamePhase(e->phase, tab)) continue;

    entryRowY[i] = y;
    y += TUNABLE_LABEL_ROW_H;

    if (e->curve != NULL) {
      float gap = 6.0f;
      float slotW = (TunableSliderWidth() - gap * (SKILL_CURVE_KEYS - 1)) / SKILL_CURVE_KEYS;
      for (int k = 0; k < SKILL_CURVE_KEYS; k++) {
        tunableSlots[tunableSlotCount++] = (TunableSliderSlot){
            i, k, (Rectangle){TUNABLE_PANEL_X + k * (slotW + gap), y, slotW, TUNABLE_SLIDER_ROW_H}};
      }
    } else {
      tunableSlots[tunableSlotCount++] = (TunableSliderSlot){
          i, -1, (Rectangle){TUNABLE_PANEL_X, y, TunableSliderWidth(), TUNABLE_SLIDER_ROW_H}};
    }
    y += TUNABLE_SLIDER_ROW_H + TUNABLE_ROW_GAP;
  }

  tunableContentH = y - TUNABLE_PANEL_TOP;
}

static void RefreshTunableLayout(int skillIndex) {
  currentTunableCount = Skill_GetTunables(skillIndex, currentTunables, MAX_SKILL_TUNABLES);
  lastTunableSkillIndex = skillIndex;

  // Build the tab list: one per distinct phase, in first-seen order.
  tunableTabCount = 0;
  const char *lastPhase = (const char *)1; // sentinel, never equals a real phase or NULL on the first iteration
  for (int i = 0; i < currentTunableCount; i++) {
    const char *phase = currentTunables[i].phase;
    if (i == 0 || !TunableSamePhase(phase, lastPhase)) {
      bool alreadyListed = false;
      for (int t = 0; t < tunableTabCount; t++)
        if (TunableSamePhase(tunableTabNames[t], phase)) { alreadyListed = true; break; }
      if (!alreadyListed && tunableTabCount < TUNABLE_MAX_TABS)
        tunableTabNames[tunableTabCount++] = phase;
      lastPhase = phase;
    }
  }

  // Lay out the tab bar buttons themselves (position only depends on tab
  // count/labels, not on which tab is active).
  float tabX = TUNABLE_PANEL_X;
  for (int t = 0; t < tunableTabCount; t++) {
    const char *label = tunableTabNames[t] != NULL ? tunableTabNames[t] : "GENERAL";
    float w = UITextWidth(label, 14) + 24.0f;
    tunableTabRects[t] = (Rectangle){tabX, TUNABLE_TAB_ROW_TOP, w, TUNABLE_TAB_ROW_H};
    tabX += w + 8.0f;
  }

  activeTunableTab = 0;
  RefreshTunableTabContent();

  // Fixed position (on the title row, above the tab bar) — doesn't scroll
  // with content or shift with however many tabs this skill has.
  rectTunableSave = (Rectangle){TUNABLE_PANEL_X + 420.0f, TUNABLE_TAB_ROW_TOP - 44.0f, 130, 34};
}

void InitUIPanel(void) {
  s_uiFont = ResourceManager_LoadFont("assets/fonts/ui_font.ttf", 32);

  togglePanelBtn = (Rectangle){20, 15, 180, 32};
  backBtn = (Rectangle){210, 15, 180, 32};

  // Compacted row pitch (36px, was 50px) and button height (28px, was 35px)
  // so this whole control block takes noticeably less vertical space,
  // leaving more room for the (now wider) tuning panel below it.
  for (int i = 0; i < 5; i++) {
    rectQty[i] = (Rectangle){870 + i * 55, 20, 46, 28};
  }
  for (int i = 0; i < 3; i++) {
    rectSize[i] = (Rectangle){870 + i * 92, 56, 84, 28};
  }
  for (int i = 0; i < 2; i++) {
    rectAnchor[i] = (Rectangle){870 + i * 140, 92, 132, 28};
  }
  for (int i = 0; i < 3; i++) {
    rectPath[i] = (Rectangle){870 + i * 92, 128, 88, 28};
  }
  rectPortalToggle = (Rectangle){870, 164, 190, 28};

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

  for (int i = 0; i < 64; i++) {
    skillOrder[i] = i;
  }
}

void UpdateUIPanel(Vector2 mousePos, UIPanelState *state) {
  state->clickedOnUI = false;
  state->requestedBackToMenu = false;
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

  bool isOverBackBtn = CheckCollisionPointRec(mousePos, backBtn);
  if (isOverBackBtn) {
    state->clickedOnUI = true;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      state->requestedBackToMenu = true;
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
    if (state->activeSkillIndex == skillOrder[i]) {
      activeValid = true;
      break;
    }
  }
  if (!activeValid && availableCount > 0) {
    state->activeSkillIndex = skillOrder[0];
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

  if (isMouseOverAnyUI || draggedSkillSlot != -1) {
    state->clickedOnUI = true;
  }

  // Xử lý kéo thả và click chuột
  if (draggedSkillSlot != -1) {
    state->clickedOnUI = true;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      int targetSlot = -1;
      for (int i = 0; i < availableCount; i++) {
        if (CheckCollisionPointRec(mousePos, skillButtons[i])) {
          targetSlot = i;
          break;
        }
      }
      if (targetSlot != -1 && targetSlot != draggedSkillSlot) {
        // Hoán đổi vị trí trong danh sách sắp xếp
        int temp = skillOrder[draggedSkillSlot];
        skillOrder[draggedSkillSlot] = skillOrder[targetSlot];
        skillOrder[targetSlot] = temp;
        state->activeSkillIndex = skillOrder[targetSlot];
      }
      draggedSkillSlot = -1;
    }
  } else {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      // Click vào phím kỹ năng -> Bắt đầu kéo thả và chọn làm chiêu active
      if (hoverSkillIndex != -1) {
        draggedSkillSlot = hoverSkillIndex;
        dragOffset = (Vector2){ mousePos.x - skillButtons[hoverSkillIndex].x, mousePos.y - skillButtons[hoverSkillIndex].y };
        state->activeSkillIndex = skillOrder[hoverSkillIndex];
        state->clickedOnUI = true;
      }

      // Các nút thông số khác
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
    }
  }

  // --- Sandbox live-tuning panel ---
  if (state->activeSkillIndex != lastTunableSkillIndex) {
    RefreshTunableLayout(state->activeSkillIndex);
  }

  for (int t = 0; t < tunableTabCount; t++) {
    if (CheckCollisionPointRec(mousePos, tunableTabRects[t])) {
      state->clickedOnUI = true;
      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && t != activeTunableTab) {
        activeTunableTab = t;
        RefreshTunableTabContent();
      }
    }
  }

  Rectangle tunableViewport = {TUNABLE_PANEL_X - 10.0f, TUNABLE_PANEL_TOP - 10.0f,
                                TunableSliderWidth() + 40.0f, TunableViewportH() + 20.0f};
  bool overTunablePanel = currentTunableCount > 0 && CheckCollisionPointRec(mousePos, tunableViewport);
  if (overTunablePanel) {
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
      state->clickedOnUI = true;
      tunableScrollY -= wheel * 40.0f;
      float maxScroll = TunableMaxScroll();
      if (tunableScrollY < 0.0f) tunableScrollY = 0.0f;
      if (tunableScrollY > maxScroll) tunableScrollY = maxScroll;
    }
  }

  for (int i = 0; i < tunableSlotCount; i++) {
    if (CheckCollisionPointRec(mousePos, TunableScrolled(tunableSlots[i].rect))) {
      state->clickedOnUI = true;
      if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) draggedTunableSlot = i;
    }
  }
  if (currentTunableCount > 0 && CheckCollisionPointRec(mousePos, rectTunableSave)) {
    state->clickedOnUI = true;
  }

  if (draggedTunableSlot != -1) {
    state->clickedOnUI = true;
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      TunableSliderSlot *slot = &tunableSlots[draggedTunableSlot];
      SkillTunableEntry *e = &currentTunables[slot->entryIndex];
      Rectangle r = slot->rect; // scroll only shifts y, unused below — x/width are scroll-invariant
      float t = (mousePos.x - r.x) / r.width;
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
      float val = e->min + t * (e->max - e->min);
      if (slot->keyIndex < 0) *e->value = val;
      else e->curve->stops[slot->keyIndex].value = val;
    } else {
      draggedTunableSlot = -1;
    }
  }

  if (currentTunableCount > 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
      CheckCollisionPointRec(mousePos, rectTunableSave)) {
    const char *path = TunableConfigPathForSkill(state->activeSkillIndex);
    if (path != NULL) {
      static char flatKeys[SKILL_TUNABLES_MAX_FLAT_KEYS][TUNING_MAX_KEY_LEN];
      static float flatValues[SKILL_TUNABLES_MAX_FLAT_KEYS];
      static const char *flatKeyPtrs[SKILL_TUNABLES_MAX_FLAT_KEYS];
      int n = SkillTunables_Flatten(currentTunables, currentTunableCount, flatKeys,
                                     flatValues, SKILL_TUNABLES_MAX_FLAT_KEYS);
      for (int i = 0; i < n; i++) flatKeyPtrs[i] = flatKeys[i];
      Tuning_SaveFloats(path, flatKeyPtrs, flatValues, n);
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
  float toggleTextW = UITextWidth(toggleText, 13);
  UIText(toggleText, togglePanelBtn.x + (togglePanelBtn.width - toggleTextW) / 2, togglePanelBtn.y + 9, 13, WHITE);

  // Vẽ nút Back
  bool isOverBack = CheckCollisionPointRec(mousePos, backBtn);
  Color backCol = isOverBack ? MAROON : DARKGRAY;
  DrawRectangleRounded(backBtn, 0.2f, 10, backCol);
  DrawRectangleRoundedLines(backBtn, 0.2f, 10, WHITE);

  const char *backText = "[<] QUAY LAI MENU";
  float backTextW = UITextWidth(backText, 13);
  UIText(backText, backBtn.x + (backBtn.width - backTextW) / 2, backBtn.y + 9, 13, WHITE);

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
    int skillIdx = skillOrder[i];
    bool isSelected = (state->activeSkillIndex == skillIdx);
    bool isHover = (hoverSkillIndex == i);
    Color baseColor = GetRegisteredSkillColor(skillIdx);

    // Nếu đang bị kéo thả, vẽ placeholder trống mờ ảo tại ô gốc
    if (draggedSkillSlot == i) {
      DrawRectangleRounded(skillButtons[i], 0.3f, 10, ColorAlpha(DARKGRAY, 0.15f));
      DrawRectangleRoundedLines(skillButtons[i], 0.3f, 10, ColorAlpha(WHITE, 0.3f));
      continue;
    }

    Color btnColor = isSelected ? baseColor
                                : (isHover ? ColorAlpha(baseColor, 0.6f)
                                           : ColorAlpha(baseColor, 0.3f));

    DrawRectangleRounded(skillButtons[i], 0.3f, 10, btnColor);
    DrawRectangleRoundedLines(skillButtons[i], 0.3f, 10, WHITE);

    const char *skillName = GetRegisteredSkillName(skillIdx);
    char btnText[64];
    snprintf(btnText, sizeof(btnText), "%s", skillName);

    float textWidth = UITextWidth(btnText, 12);
    UIText(btnText, skillButtons[i].x + (skillButtons[i].width - textWidth) / 2,
           skillButtons[i].y + 11, 12, WHITE);
  }

  // Vẽ nút đang bị kéo lơ lửng theo chuột
  if (draggedSkillSlot != -1 && draggedSkillSlot < availableCount) {
    int skillIdx = skillOrder[draggedSkillSlot];
    Color baseColor = GetRegisteredSkillColor(skillIdx);
    Rectangle dragRect = {
      mousePos.x - dragOffset.x,
      mousePos.y - dragOffset.y,
      skillButtons[draggedSkillSlot].width,
      skillButtons[draggedSkillSlot].height
    };
    Color btnColor = ColorAlpha(baseColor, 0.8f);
    DrawRectangleRounded(dragRect, 0.3f, 10, btnColor);
    DrawRectangleRoundedLines(dragRect, 0.3f, 10, YELLOW); // Highlight màu vàng lấp lánh khi kéo
    
    const char *skillName = GetRegisteredSkillName(skillIdx);
    float textWidth = UITextWidth(skillName, 12);
    UIText(skillName, dragRect.x + (dragRect.width - textWidth) / 2, dragRect.y + 11, 12, WHITE);
  }

  // Single background card behind the whole right-side control column
  // (Quantity/Size/Anchor/Path/Portals/GPU status AND the Tuning panel
  // below it) so they read as one unified panel instead of two
  // disconnected floating groups of widgets.
  {
    float panelBottom = (currentTunableCount > 0)
                             ? TUNABLE_PANEL_TOP + TunableViewportH() + 10.0f
                             : 250.0f;
    float panelRight = TUNABLE_PANEL_X + TunableSliderWidth() + 40.0f; // + scrollbar/margin
    Rectangle bg = {755.0f, 15.0f, panelRight - 755.0f, panelBottom - 15.0f};
    DrawRectangleRounded(bg, 0.02f, 8, ColorAlpha(BLACK, 0.55f));
    DrawRectangleRoundedLines(bg, 0.02f, 8, ColorAlpha(WHITE, 0.25f));
  }

  // Quantity
  UIText("Quantity:", 770, rectQty[0].y + 5, 14, LIGHTGRAY);
  for (int i = 0; i < 5; i++) {
    bool isSelected = (state->currentParams.quantity == (i + 1));
    bool isHover = CheckCollisionPointRec(mousePos, rectQty[i]);
    Color btnCol = isSelected ? PURPLE : (isHover ? DARKPURPLE : DARKGRAY);
    DrawRectangleRounded(rectQty[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectQty[i], 0.2f, 10, WHITE);
    UIText(TextFormat("%d", i + 1), rectQty[i].x + 18, rectQty[i].y + 5, 14, WHITE);
  }

  // Size
  UIText("Size:", 770, rectSize[0].y + 5, 14, LIGHTGRAY);
  for (int i = 0; i < 3; i++) {
    bool isSelected = (state->currentParams.sizeScale == sizes[i]);
    bool isHover = CheckCollisionPointRec(mousePos, rectSize[i]);
    Color btnCol = isSelected ? ORANGE : (isHover ? GOLD : DARKGRAY);
    DrawRectangleRounded(rectSize[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectSize[i], 0.2f, 10, WHITE);

    const char *sizeText = (i == 0) ? "1.0x" : (i == 1) ? "1.5x" : "2.0x";
    UIText(sizeText, rectSize[i].x + 20, rectSize[i].y + 5, 14, WHITE);
  }

  // Anchor
  UIText("Anchor:", 770, rectAnchor[0].y + 5, 14, LIGHTGRAY);
  const char *anchorNames[] = {"CASTER (SELF)", "TARGET (ENEMY)"};
  for (int i = 0; i < 2; i++) {
    bool isSelected = (state->currentParams.anchorType == (CastAnchorType)i);
    bool isHover = CheckCollisionPointRec(mousePos, rectAnchor[i]);
    Color btnCol = isSelected ? BLUE : (isHover ? DARKBLUE : DARKGRAY);
    DrawRectangleRounded(rectAnchor[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectAnchor[i], 0.2f, 10, WHITE);
    float textW = UITextWidth(anchorNames[i], 12);
    UIText(anchorNames[i], rectAnchor[i].x + (rectAnchor[i].width - textW) / 2,
           rectAnchor[i].y + 8, 12, WHITE);
  }

  // Path
  UIText("Path:", 770, rectPath[0].y + 5, 14, LIGHTGRAY);
  const char *pathNames[] = {"PROJECTILE", "FALLING", "RISING"};
  for (int i = 0; i < 3; i++) {
    bool isSelected = (state->currentParams.pathType == (CastPathType)i);
    bool isHover = CheckCollisionPointRec(mousePos, rectPath[i]);
    Color btnCol = isSelected ? MAROON : (isHover ? RED : DARKGRAY);
    DrawRectangleRounded(rectPath[i], 0.2f, 10, btnCol);
    DrawRectangleRoundedLines(rectPath[i], 0.2f, 10, WHITE);
    float textW = UITextWidth(pathNames[i], 12);
    UIText(pathNames[i], rectPath[i].x + (rectPath[i].width - textW) / 2,
           rectPath[i].y + 8, 12, WHITE);
  }

  // Portals
  bool ptHover = CheckCollisionPointRec(mousePos, rectPortalToggle);
  Color ptCol =
      state->currentParams.showPortal ? DARKGREEN : (ptHover ? LIME : DARKGRAY);
  DrawRectangleRounded(rectPortalToggle, 0.2f, 10, ptCol);
  DrawRectangleRoundedLines(rectPortalToggle, 0.2f, 10, WHITE);
  UIText(state->currentParams.showPortal ? "PORTALS: ON" : "PORTALS: OFF",
         rectPortalToggle.x + 34, rectPortalToggle.y + 6, 14, WHITE);

  // GPU Particle status
  {
    float bx = rectPortalToggle.x;
    float by = rectPortalToggle.y + 36;
    bool compute = GpuParticleSystem_IsComputeActive();
    int  active  = GpuParticleSystem_ActiveCount();

    Color modeColor = compute ? (Color){80, 220, 80, 255} : (Color){220, 180, 60, 255};
    const char *modeText = compute ? "GPU: COMPUTE" : "GPU: CPU/VBO";
    DrawRectangleRounded((Rectangle){bx, by, 190, 26}, 0.2f, 8, (Color){20, 20, 20, 180});
    DrawRectangleRoundedLines((Rectangle){bx, by, 190, 26}, 0.2f, 8, modeColor);
    UIText(modeText, bx + 8, by + 5, 12, modeColor);
    UIText(TextFormat("particles: %d/%d", active, MAX_GPU_PARTICLES), bx, by + 28, 11, LIGHTGRAY);
  }

  // Sandbox live-tuning sliders — only drawn for skills that called
  // RegisterSkillTunables (core/skill_manager.h). Empty for every other skill.
  // Single column, grouped by phase (see RefreshTunableLayout), scrolled
  // with the mouse wheel and clipped to the viewport so a skill with many
  // parameters (fire_ball: 25) stays fully reachable regardless of window
  // size instead of running off the bottom/side.
  if (currentTunableCount > 0) {
    UIText("Tuning (drag to adjust, scroll for more):", TUNABLE_PANEL_X, TUNABLE_TAB_ROW_TOP - 40.0f, 18, LIGHTGRAY);

    // Tab bar — one button per phase (core/skill_manager.h's SkillTunableEntry.phase).
    for (int t = 0; t < tunableTabCount; t++) {
      const char *label = tunableTabNames[t] != NULL ? tunableTabNames[t] : "GENERAL";
      Rectangle r = tunableTabRects[t];
      bool active = (t == activeTunableTab);
      DrawRectangleRounded(r, 0.3f, 8, active ? ORANGE : ColorAlpha(DARKGRAY, 0.7f));
      DrawRectangleRoundedLines(r, 0.3f, 8, WHITE);
      float tw = UITextWidth(label, 14);
      UIText(label, r.x + (r.width - tw) / 2.0f, r.y + 7, 14, active ? BLACK : WHITE);
    }

    float viewportH = TunableViewportH();
    BeginScissorMode((int)(TUNABLE_PANEL_X - 10.0f), (int)TUNABLE_PANEL_TOP,
                      (int)(TunableSliderWidth() + 20.0f), (int)viewportH);

    for (int s = 0; s < tunableSlotCount; s++) {
      if (tunableSlots[s].keyIndex > 0) continue; // label drawn once per entry, not per curve key
      int entryIdx = tunableSlots[s].entryIndex;
      Rectangle r = TunableScrolled((Rectangle){TUNABLE_PANEL_X, entryRowY[entryIdx], 0, 0});
      UIText(currentTunables[entryIdx].label, r.x, r.y - 2, 14, LIGHTGRAY);
    }

    for (int s = 0; s < tunableSlotCount; s++) {
      const TunableSliderSlot *slot = &tunableSlots[s];
      const SkillTunableEntry *e = &currentTunables[slot->entryIndex];
      Rectangle r = TunableScrolled(slot->rect);

      float value = (slot->keyIndex < 0) ? *e->value : e->curve->stops[slot->keyIndex].value;
      float t = (e->max > e->min) ? (value - e->min) / (e->max - e->min) : 0.0f;
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;

      DrawRectangleRounded(r, 0.3f, 6, ColorAlpha(DARKGRAY, 0.6f));
      Rectangle fillRect = {r.x, r.y, r.width * t, r.height};
      DrawRectangleRounded(fillRect, 0.3f, 6, (draggedTunableSlot == s) ? YELLOW : SKYBLUE);
      DrawRectangleRoundedLines(r, 0.3f, 6, WHITE);
      if (slot->keyIndex < 0) {
        UIText(TextFormat("%.3f", value), r.x + 6, r.y + 5, 13, WHITE);
      } else {
        UIText(TextFormat("%d%%", slot->keyIndex * 100 / (SKILL_CURVE_KEYS - 1)), r.x + 4, r.y + 6, 11, WHITE);
      }
    }

    EndScissorMode();

    // Scrollbar indicator (only when content overflows the viewport).
    float maxScroll = TunableMaxScroll();
    if (maxScroll > 0.0f) {
      float trackX = TUNABLE_PANEL_X + TunableSliderWidth() + 14.0f;
      DrawRectangle((int)trackX, (int)TUNABLE_PANEL_TOP, 6, (int)viewportH, ColorAlpha(DARKGRAY, 0.5f));
      float thumbH = viewportH * (viewportH / tunableContentH);
      float thumbY = TUNABLE_PANEL_TOP + (viewportH - thumbH) * (tunableScrollY / maxScroll);
      DrawRectangle((int)trackX, (int)thumbY, 6, (int)thumbH, SKYBLUE);
    }

    bool saveHover = CheckCollisionPointRec(GetMousePosition(), rectTunableSave);
    DrawRectangleRounded(rectTunableSave, 0.2f, 10, saveHover ? LIME : DARKGREEN);
    DrawRectangleRoundedLines(rectTunableSave, 0.2f, 10, WHITE);
    UIText("SAVE", rectTunableSave.x + 40, rectTunableSave.y + 8, 16, WHITE);
  }

  EndBlendMode();
}

int GetSkillAtOrderIndex(int orderIndex) {
  int skillCount = GetRegisteredSkillCount();
  if (orderIndex < 0 || orderIndex >= skillCount || orderIndex >= 64) {
    return -1;
  }
  return skillOrder[orderIndex];
}