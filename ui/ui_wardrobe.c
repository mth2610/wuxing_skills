// ui/ui_wardrobe.c
// Interactive Wuxia Wardrobe & Equipment Selection UI for Wuxing Skills.
// Immediate-mode 2D overlay pass with 3D character inspection stage.
// Authentic Eastern Wuxia / Tiên Hiệp aesthetic with full Vietnamese diacritics.

#include "ui/ui_wardrobe.h"
#include "character/wardrobe_system.h"
#include "character/modular_character.h"
#include "core/resource_manager.h"
#include "raymath.h"
#include "rlgl.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static bool        s_isOpen = false;
static ModularSlot s_selectedSlot = MODULAR_SLOT_TORSO;
static char        s_selectedItemId[WARDROBE_ID_MAX_LEN] = "";
static float       s_previewYaw = 0.0f;
static bool        s_isDragging = false;
static Vector2     s_dragStartMouse = {0};
static float       s_dragStartYaw = 0.0f;
static Camera3D    s_previewCamera;
static bool        s_cameraInit = false;
static bool        s_justOpened = false;
static Font        s_uiFont = {0};

// -----------------------------------------------------------------------------
// Color Definitions (Wuxia Lacquer, Jade & Elemental Palette)
// -----------------------------------------------------------------------------
static const Color COLOR_BG_MODAL          = { 8, 10, 15, 246 };
static const Color COLOR_BG_PANEL          = { 18, 20, 28, 252 };
static const Color COLOR_BORDER_GOLD_OUTER = { 105, 85, 45, 180 };
static const Color COLOR_BORDER_GOLD_INNER = { 218, 182, 92, 230 };
static const Color COLOR_BORDER_DIM        = { 46, 52, 68, 190 };
static const Color COLOR_BORDER_SELECT     = { 255, 218, 92, 255 };
static const Color COLOR_TEXT_GOLD         = { 245, 215, 120, 255 };
static const Color COLOR_TEXT_PARCHMENT    = { 190, 185, 172, 255 };
static const Color COLOR_TEXT_MUTED        = { 128, 134, 146, 255 };

static void EnsureFont(void) {
    if (s_uiFont.texture.id == 0) {
        s_uiFont = ResourceManager_LoadFont("assets/fonts/ui_font.ttf", 32);
    }
}

static void DrawWuxiaText(const char *text, float x, float y, float size, Color col) {
    if (!text || text[0] == '\0') return;
    EnsureFont();
    DrawTextEx(s_uiFont, text, (Vector2){ x + 1.0f, y + 1.0f }, size, 1.0f, (Color){ 0, 0, 0, 205 });
    DrawTextEx(s_uiFont, text, (Vector2){ x, y }, size, 1.0f, col);
}

static void DrawWuxiaTextCentered(const char *text, float centerX, float y, float size, Color col) {
    if (!text || text[0] == '\0') return;
    EnsureFont();
    float w = MeasureTextEx(s_uiFont, text, size, 1.0f).x;
    float x = centerX - w * 0.5f;
    DrawTextEx(s_uiFont, text, (Vector2){ x + 1.0f, y + 1.0f }, size, 1.0f, (Color){ 0, 0, 0, 205 });
    DrawTextEx(s_uiFont, text, (Vector2){ x, y }, size, 1.0f, col);
}

static float GetWuxiaTextWidth(const char *text, float size) {
    if (!text || text[0] == '\0') return 0.0f;
    EnsureFont();
    return MeasureTextEx(s_uiFont, text, size, 1.0f).x;
}

static void DrawWuxiaTextWrapped(const char *text, float x, float y, float maxW, float size, Color col, float lineH) {
    if (!text || text[0] == '\0') return;
    char lineBuf[256];
    lineBuf[0] = '\0';
    const char *ptr = text;
    float curY = y;

    while (*ptr != '\0') {
        while (*ptr == ' ') ptr++;
        if (*ptr == '\0') break;

        const char *wordStart = ptr;
        while (*ptr != '\0' && *ptr != ' ') ptr++;
        int wordLen = (int)(ptr - wordStart);

        char testBuf[256];
        if (lineBuf[0] == '\0') {
            snprintf(testBuf, sizeof(testBuf), "%.*s", wordLen, wordStart);
        } else {
            snprintf(testBuf, sizeof(testBuf), "%s %.*s", lineBuf, wordLen, wordStart);
        }

        if (GetWuxiaTextWidth(testBuf, size) > maxW && lineBuf[0] != '\0') {
            DrawWuxiaText(lineBuf, x, curY, size, col);
            curY += lineH;
            snprintf(lineBuf, sizeof(lineBuf), "%.*s", wordLen, wordStart);
        } else {
            strncpy(lineBuf, testBuf, sizeof(lineBuf) - 1);
            lineBuf[sizeof(lineBuf) - 1] = '\0';
        }
    }
    if (lineBuf[0] != '\0') {
        DrawWuxiaText(lineBuf, x, curY, size, col);
    }
}

static Color GetRarityColor(WardrobeRarity rarity) {
    switch (rarity) {
        case WARDROBE_RARITY_COMMON:    return (Color){ 210, 215, 225, 255 }; // Bạch Ngân
        case WARDROBE_RARITY_UNCOMMON:  return (Color){ 80, 220, 135, 255 };  // Bích Ngọc
        case WARDROBE_RARITY_RARE:      return (Color){ 70, 165, 255, 255 };  // Lam Tinh
        case WARDROBE_RARITY_EPIC:      return (Color){ 195, 105, 255, 255 }; // Tử Hà
        case WARDROBE_RARITY_LEGENDARY: return (Color){ 255, 215, 60, 255 };  // Hoàng Kim
        default:                        return RAYWHITE;
    }
}

static const char* GetRarityName(WardrobeRarity rarity) {
    switch (rarity) {
        case WARDROBE_RARITY_COMMON:    return "PHỔ THÔNG";
        case WARDROBE_RARITY_UNCOMMON:  return "TINH XẢO";
        case WARDROBE_RARITY_RARE:      return "HIẾM";
        case WARDROBE_RARITY_EPIC:      return "SỬ THI";
        case WARDROBE_RARITY_LEGENDARY: return "TRUYỀN THUYẾT";
        default:                        return "VÔ HỆ";
    }
}

static Color GetElementColor(WardrobeElement elem) {
    switch (elem) {
        case WARDROBE_ELEM_METAL: return (Color){ 255, 228, 125, 255 };
        case WARDROBE_ELEM_WOOD:  return (Color){ 85, 220, 100, 255 };
        case WARDROBE_ELEM_WATER: return (Color){ 75, 170, 255, 255 };
        case WARDROBE_ELEM_FIRE:  return (Color){ 255, 90, 70, 255 };
        case WARDROBE_ELEM_EARTH: return (Color){ 225, 170, 90, 255 };
        case WARDROBE_ELEM_TAIJI: return (Color){ 245, 245, 255, 255 };
        default:                  return (Color){ 160, 165, 175, 255 };
    }
}

static const char* GetElementName(WardrobeElement elem) {
    switch (elem) {
        case WARDROBE_ELEM_METAL: return "KIM";
        case WARDROBE_ELEM_WOOD:  return "MỘC";
        case WARDROBE_ELEM_WATER: return "THỦY";
        case WARDROBE_ELEM_FIRE:  return "HỎA";
        case WARDROBE_ELEM_EARTH: return "THỔ";
        case WARDROBE_ELEM_TAIJI: return "THÁI CỰC";
        default:                  return "VÔ HỆ";
    }
}

static const char* GetSlotName(ModularSlot slot) {
    switch (slot) {
        case MODULAR_SLOT_FULL_OUTFIT: return "NGUYÊN BỘ (TRƯỜNG BÀO)";
        case MODULAR_SLOT_BASE_BODY:   return "THÂN THỂ";
        case MODULAR_SLOT_TORSO:       return "Y PHỤC (ÁO)";
        case MODULAR_SLOT_LEGS:        return "HẠ PHỤC (QUẦN)";
        case MODULAR_SLOT_BOOTS:       return "CHIẾN HÀI (GIÀY)";
        case MODULAR_SLOT_BRACERS:     return "HỘ UYỂN (CỔ TAY)";
        case MODULAR_SLOT_HEADGEAR:    return "NÓN LÁ / MŨ";
        case MODULAR_SLOT_MASK:        return "MẶT NẠ / KHĂN CHE";
        case MODULAR_SLOT_WAIST:       return "NGỌC BỘI / ĐAI LƯNG";
        case MODULAR_SLOT_BACK:        return "PHI PHONG / VỎ KIẾM";
        case MODULAR_SLOT_WEAPON_R:    return "BINH KHÍ (VŨ KHÍ)";
        case MODULAR_SLOT_WEAPON_L:    return "TẢ BINH KHÍ";
        case MODULAR_SLOT_HAIR:        return "MÁI TÓC";
        default:                       return "TRANG BỊ";
    }
}

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void UI_Wardrobe_Init(void) {
    s_isOpen = false;
    s_selectedSlot = MODULAR_SLOT_TORSO;
    s_selectedItemId[0] = '\0';
    s_previewYaw = 0.0f;
    s_isDragging = false;

    s_previewCamera.position = (Vector3){ 0.0f, 0.96f, 3.85f };
    s_previewCamera.target   = (Vector3){ 0.0f, 0.90f, 0.0f };
    s_previewCamera.up       = (Vector3){ 0.0f, 1.0f, 0.0f };
    s_previewCamera.fovy     = 33.5f;
    s_previewCamera.projection = CAMERA_PERSPECTIVE;
    s_cameraInit = true;

    EnsureFont();
}

void UI_Wardrobe_Toggle(PlayerWardrobe *pw, ModularInstance *inst) {
    if (s_isOpen) {
        UI_Wardrobe_Close();
    } else {
        UI_Wardrobe_Open(pw, inst);
    }
}

void UI_Wardrobe_Open(PlayerWardrobe *pw, ModularInstance *inst) {
    if (!s_cameraInit) UI_Wardrobe_Init();
    s_isOpen = true;
    s_justOpened = true;
    s_previewYaw = 0.0f;
    s_isDragging = false;

    if (pw) {
        const char *eq = PlayerWardrobe_GetEquippedId(pw, s_selectedSlot);
        if (eq && eq[0]) strncpy(s_selectedItemId, eq, sizeof(s_selectedItemId) - 1);
        else s_selectedItemId[0] = '\0';
    }
    TraceLog(LOG_INFO, "UI_WARDROBE: Opened wardrobe panel.");
}

void UI_Wardrobe_Close(void) {
    s_isOpen = false;
    s_isDragging = false;
    Wardrobe_SaveGlobal();
    TraceLog(LOG_INFO, "UI_WARDROBE: Closed wardrobe panel and saved loadout.");
}

bool UI_Wardrobe_IsOpen(void) {
    return s_isOpen;
}

void UI_Wardrobe_SetPreviewYaw(float yaw) {
    s_previewYaw = yaw;
}

// -----------------------------------------------------------------------------
// UI Helper Widgets: Ornate Wuxia Lacquer & Jade Frames
// -----------------------------------------------------------------------------

static void DrawWuxiaFrame(Rectangle rec, Color bg, Color border) {
    DrawRectangleRec(rec, bg);

    float gradH = fminf(38.0f, rec.height * 0.4f);
    DrawRectangleGradientV((int)rec.x, (int)rec.y, (int)rec.width, (int)gradH,
                           (Color){ 46, 52, 70, 65 }, (Color){ 0, 0, 0, 0 });

    DrawRectangleLinesEx(rec, 1.0f, COLOR_BORDER_GOLD_OUTER);
    Rectangle innerRec = { rec.x + 2.0f, rec.y + 2.0f, rec.width - 4.0f, rec.height - 4.0f };
    DrawRectangleLinesEx(innerRec, 1.0f, border);

    float cs = 12.0f;
    float x = rec.x;
    float y = rec.y;
    float r = rec.x + rec.width;
    float b = rec.y + rec.height;
    Color goldBright = (Color){ 255, 220, 110, 240 };
    Color goldDim    = (Color){ 160, 130, 65, 180 };

    // Top-Left corner
    DrawLine((int)x, (int)y, (int)(x + cs), (int)y, goldBright);
    DrawLine((int)x, (int)y, (int)x, (int)(y + cs), goldBright);
    DrawLine((int)(x + 3), (int)(y + 3), (int)(x + cs - 3), (int)(y + 3), goldDim);
    DrawLine((int)(x + 3), (int)(y + 3), (int)(x + 3), (int)(y + cs - 3), goldDim);
    DrawRectangle((int)(x + 1), (int)(y + 1), 2, 2, goldBright);

    // Top-Right corner
    DrawLine((int)r, (int)y, (int)(r - cs), (int)y, goldBright);
    DrawLine((int)r, (int)y, (int)r, (int)(y + cs), goldBright);
    DrawLine((int)(r - 3), (int)(y + 3), (int)(r - cs + 3), (int)(y + 3), goldDim);
    DrawLine((int)(r - 3), (int)(y + 3), (int)(r - 3), (int)(y + cs - 3), goldDim);
    DrawRectangle((int)(r - 3), (int)(y + 1), 2, 2, goldBright);

    // Bottom-Left corner
    DrawLine((int)x, (int)b, (int)(x + cs), (int)b, goldBright);
    DrawLine((int)x, (int)b, (int)x, (int)(b - cs), goldBright);
    DrawLine((int)(x + 3), (int)(b - 3), (int)(x + cs - 3), (int)(b - 3), goldDim);
    DrawLine((int)(x + 3), (int)(b - 3), (int)(x + 3), (int)(b - cs + 3), goldDim);
    DrawRectangle((int)(x + 1), (int)(b - 3), 2, 2, goldBright);

    // Bottom-Right corner
    DrawLine((int)r, (int)b, (int)(r - cs), (int)b, goldBright);
    DrawLine((int)r, (int)b, (int)r, (int)(b - cs), goldBright);
    DrawLine((int)(r - 3), (int)(b - 3), (int)(r - cs + 3), (int)(b - 3), goldDim);
    DrawLine((int)(r - 3), (int)(b - 3), (int)(r - 3), (int)(b - cs + 3), goldDim);
    DrawRectangle((int)(r - 3), (int)(b - 3), 2, 2, goldBright);
}

static void DrawDiamondIcon(float cx, float cy, float radius, Color col) {
    Vector2 pts[4] = {
        { cx, cy - radius },
        { cx + radius, cy },
        { cx, cy + radius },
        { cx - radius, cy }
    };
    DrawTriangle(pts[0], pts[3], pts[1], col);
    DrawTriangle(pts[2], pts[1], pts[3], col);
    DrawLineV(pts[0], pts[1], WHITE);
    DrawLineV(pts[1], pts[2], WHITE);
    DrawLineV(pts[2], pts[3], WHITE);
    DrawLineV(pts[3], pts[0], WHITE);
}

// -----------------------------------------------------------------------------
// Main Update & Draw Loop
// -----------------------------------------------------------------------------

void UI_Wardrobe_UpdateDraw(PlayerWardrobe *pw, ModularInstance *inst, float dt) {
    if (!s_isOpen || !pw || !inst) return;

    EnsureFont();

    Vector2 mouse = GetMousePosition();
    bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    // ESC or B to close (skip on exact frame opened to prevent instant-close)
    if (s_justOpened) {
        s_justOpened = false;
    } else {
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B)) {
            UI_Wardrobe_Close();
            return;
        }
    }

    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();

    // 1. Dark translucent backdrop
    DrawRectangle(0, 0, screenW, screenH, COLOR_BG_MODAL);

    // 2. Top Header Bar
    Rectangle headerRec = { 20, 14, (float)screenW - 40, 56 };
    DrawWuxiaFrame(headerRec, COLOR_BG_PANEL, COLOR_BORDER_GOLD_INNER);

    // Title: TỦ ĐỒ HIỆP KHÁCH with decorative diamond
    DrawDiamondIcon(36.0f, 32.0f, 6.0f, COLOR_BORDER_GOLD_INNER);
    DrawWuxiaText("TỦ ĐỒ HIỆP KHÁCH", 50, 20, 22, COLOR_TEXT_GOLD);
    DrawWuxiaText("Kho Trang Bị & Bộ Sưu Tập Giang Hồ", 50, 44, 12, COLOR_TEXT_PARCHMENT);

    // Combat Stance Banner in Center
    CombatStance stance = PlayerWardrobe_GetCombatStance(pw);
    float bannerW = 320.0f;
    Rectangle stanceRec = { (float)screenW * 0.5f - bannerW * 0.5f, 20, bannerW, 44 };
    if (stance == COMBAT_STANCE_SWORD) {
        DrawRectangleGradientV((int)stanceRec.x, (int)stanceRec.y, (int)stanceRec.width, (int)stanceRec.height,
                               (Color){ 58, 46, 20, 245 }, (Color){ 36, 28, 12, 245 });
        DrawRectangleLinesEx(stanceRec, 1.5f, (Color){ 255, 215, 80, 255 });
        DrawDiamondIcon(stanceRec.x + 18.0f, stanceRec.y + stanceRec.height * 0.5f, 5.0f, GOLD);
        DrawDiamondIcon(stanceRec.x + stanceRec.width - 18.0f, stanceRec.y + stanceRec.height * 0.5f, 5.0f, GOLD);
        DrawWuxiaTextCentered("THẾ VÕ: KIẾM PHÁP", stanceRec.x + stanceRec.width * 0.5f, stanceRec.y + 6, 15, COLOR_TEXT_GOLD);
        DrawWuxiaTextCentered("Khai mở chiêu thức Trảm, Đâm, Đỡ, Gạt", stanceRec.x + stanceRec.width * 0.5f, stanceRec.y + 24, 11, (Color){ 220, 190, 130, 230 });
    } else {
        DrawRectangleGradientV((int)stanceRec.x, (int)stanceRec.y, (int)stanceRec.width, (int)stanceRec.height,
                               (Color){ 20, 42, 60, 245 }, (Color){ 12, 26, 38, 245 });
        DrawRectangleLinesEx(stanceRec, 1.5f, (Color){ 90, 185, 255, 255 });
        DrawDiamondIcon(stanceRec.x + 18.0f, stanceRec.y + stanceRec.height * 0.5f, 5.0f, SKYBLUE);
        DrawDiamondIcon(stanceRec.x + stanceRec.width - 18.0f, stanceRec.y + stanceRec.height * 0.5f, 5.0f, SKYBLUE);
        DrawWuxiaTextCentered("THẾ VÕ: QUYỀN CƯỚC (TAY KHÔNG)", stanceRec.x + stanceRec.width * 0.5f, stanceRec.y + 6, 14, SKYBLUE);
        DrawWuxiaTextCentered("Khai mở thế đánh Quyền, Chưởng, Cước, Vận khí", stanceRec.x + stanceRec.width * 0.5f, stanceRec.y + 24, 11, (Color){ 140, 195, 240, 230 });
    }

    // Close Button (Right top corner)
    Rectangle closeBtn = { (float)screenW - 146, 22, 112, 38 };
    bool closeHover = CheckCollisionPointRec(mouse, closeBtn);
    Color closeBgTop = closeHover ? (Color){ 130, 32, 32, 255 } : (Color){ 75, 20, 20, 255 };
    Color closeBgBot = closeHover ? (Color){ 80, 18, 18, 255 } : (Color){ 45, 12, 12, 255 };
    DrawRectangleGradientV((int)closeBtn.x, (int)closeBtn.y, (int)closeBtn.width, (int)closeBtn.height, closeBgTop, closeBgBot);
    DrawRectangleLinesEx(closeBtn, 1.5f, closeHover ? (Color){ 255, 110, 100, 255 } : (Color){ 180, 70, 70, 230 });
    DrawWuxiaTextCentered("ĐÓNG (ESC)", closeBtn.x + closeBtn.width * 0.5f, closeBtn.y + 11, 13, RAYWHITE);
    if (clicked && closeHover) {
        UI_Wardrobe_Close();
        return;
    }

    // -------------------------------------------------------------------------
    // Layout Dimensions
    // -------------------------------------------------------------------------
    float contentY = 78.0f;
    float contentH = (float)screenH - contentY - 18.0f;

    float leftW   = 246.0f;
    float centerW = 460.0f;
    float rightW  = (float)screenW - leftW - centerW - 60.0f;

    Rectangle leftPanel   = { 20.0f, contentY, leftW, contentH };
    Rectangle centerPanel = { 30.0f + leftW, contentY, centerW, contentH };
    Rectangle rightPanel  = { 40.0f + leftW + centerW, contentY, rightW, contentH };

    // -------------------------------------------------------------------------
    // LEFT PANEL: Slot Category Tabs (11 slots supporting Modular & Full Outfits)
    // -------------------------------------------------------------------------
    DrawWuxiaFrame(leftPanel, COLOR_BG_PANEL, COLOR_BORDER_DIM);
    DrawDiamondIcon(leftPanel.x + 22.0f, leftPanel.y + 20.0f, 4.0f, COLOR_BORDER_GOLD_INNER);
    DrawWuxiaText("CÁC BỘ PHẬN", leftPanel.x + 34, leftPanel.y + 11, 15, COLOR_TEXT_GOLD);
    DrawLine((int)leftPanel.x + 14, (int)leftPanel.y + 32, (int)(leftPanel.x + leftW - 14), (int)leftPanel.y + 32, (Color){ 65, 70, 85, 200 });

    ModularSlot slots[] = {
        MODULAR_SLOT_FULL_OUTFIT,
        MODULAR_SLOT_BASE_BODY,
        MODULAR_SLOT_TORSO,
        MODULAR_SLOT_LEGS,
        MODULAR_SLOT_BOOTS,
        MODULAR_SLOT_BRACERS,
        MODULAR_SLOT_HEADGEAR,
        MODULAR_SLOT_MASK,
        MODULAR_SLOT_WAIST,
        MODULAR_SLOT_BACK,
        MODULAR_SLOT_WEAPON_R
    };
    int slotCount = (int)(sizeof(slots) / sizeof(slots[0]));

    float slotRowH = 49.0f;
    float slotRowGap = 4.0f;
    bool hasFullOutfit = (pw->equippedIds[MODULAR_SLOT_FULL_OUTFIT][0] != '\0');

    for (int i = 0; i < slotCount; i++) {
        ModularSlot sl = slots[i];
        Rectangle rowRec = { leftPanel.x + 10, leftPanel.y + 38 + i * (slotRowH + slotRowGap), leftW - 20, slotRowH };
        bool isSelected = (s_selectedSlot == sl);
        bool isHover = CheckCollisionPointRec(mouse, rowRec);

        if (clicked && isHover) {
            s_selectedSlot = sl;
            const char *eq = PlayerWardrobe_GetEquippedId(pw, sl);
            if (eq && eq[0]) strncpy(s_selectedItemId, eq, sizeof(s_selectedItemId) - 1);
            else s_selectedItemId[0] = '\0';
        }

        Color rowBg = isSelected ? (Color){ 48, 42, 26, 255 } : (isHover ? (Color){ 32, 36, 48, 255 } : (Color){ 18, 20, 27, 255 });
        Color rowBrd = isSelected ? COLOR_BORDER_SELECT : (isHover ? COLOR_BORDER_GOLD_INNER : COLOR_BORDER_DIM);
        DrawRectangleRec(rowRec, rowBg);
        DrawRectangleLinesEx(rowRec, isSelected ? 1.5f : 1.0f, rowBrd);

        if (isSelected) {
            DrawRectangle((int)rowRec.x, (int)rowRec.y, 4, (int)rowRec.height, COLOR_BORDER_SELECT);
            DrawDiamondIcon(rowRec.x + rowRec.width - 12.0f, rowRec.y + rowRec.height * 0.5f, 3.5f, COLOR_BORDER_SELECT);
        }

        // Slot Title
        DrawWuxiaText(GetSlotName(sl), rowRec.x + 10, rowRec.y + 5, 11, isSelected ? COLOR_TEXT_GOLD : (Color){ 180, 185, 195, 255 });

        // Item status
        bool isSuppressedByOutfit = (hasFullOutfit && (sl == MODULAR_SLOT_TORSO || sl == MODULAR_SLOT_LEGS));
        const char *equippedId = PlayerWardrobe_GetEquippedId(pw, sl);
        if (isSuppressedByOutfit) {
            DrawWuxiaText("[ẨN BỞI NGUYÊN BỘ]", rowRec.x + 10, rowRec.y + 24, 11, (Color){ 160, 140, 90, 200 });
        } else if (equippedId && equippedId[0] != '\0') {
            const WardrobeCatalogItem *cat = Wardrobe_GetCatalogItem(equippedId);
            const char *dispName = cat ? cat->name : equippedId;
            Color nameCol = cat ? GetRarityColor(cat->rarity) : RAYWHITE;
            DrawWuxiaText(dispName, rowRec.x + 10, rowRec.y + 24, 13, nameCol);
        } else {
            DrawWuxiaText("<ĐỂ TRỐNG>", rowRec.x + 10, rowRec.y + 24, 12, COLOR_TEXT_MUTED);
        }
    }

    // -------------------------------------------------------------------------
    // CENTER PANEL: 3D Character Preview Stage
    // -------------------------------------------------------------------------
    DrawWuxiaFrame(centerPanel, (Color){ 15, 17, 23, 255 }, COLOR_BORDER_DIM);

    bool inCenter = CheckCollisionPointRec(mouse, centerPanel);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && inCenter) {
        s_isDragging = true;
        s_dragStartMouse = mouse;
        s_dragStartYaw = s_previewYaw;
    }
    if (s_isDragging) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float dx = mouse.x - s_dragStartMouse.x;
            s_previewYaw = s_dragStartYaw + dx * 0.015f;
        } else {
            s_isDragging = false;
        }
    }

    BeginScissorMode((int)centerPanel.x + 2, (int)centerPanel.y + 2, (int)centerPanel.width - 4, (int)centerPanel.height - 4);
    BeginMode3D(s_previewCamera);

    ModularInstance_Update(inst, dt, false);

    // Multi-tiered Carved Stone & Jade Pedestal
    DrawCylinder((Vector3){ 0.0f, -0.09f, 0.0f }, 1.05f, 1.05f, 0.05f, 36, (Color){ 20, 22, 30, 255 });
    DrawCylinderWires((Vector3){ 0.0f, -0.09f, 0.0f }, 1.05f, 1.05f, 0.05f, 36, (Color){ 90, 80, 55, 180 });
    DrawCylinder((Vector3){ 0.0f, -0.04f, 0.0f }, 0.88f, 0.88f, 0.05f, 36, (Color){ 28, 32, 42, 255 });
    DrawCylinderWires((Vector3){ 0.0f, -0.04f, 0.0f }, 0.88f, 0.88f, 0.05f, 36, COLOR_BORDER_GOLD_INNER);
    DrawCylinder((Vector3){ 0.0f, 0.005f, 0.0f }, 0.74f, 0.74f, 0.01f, 36, (Color){ 36, 40, 52, 255 });

    DrawCircleLines(0, 0, 0.84f, (Color){ 180, 150, 75, 160 });
    DrawCircleLines(0, 0, 0.65f, (Color){ 70, 160, 220, 190 });
    DrawCircleLines(0, 0, 0.40f, (Color){ 220, 185, 90, 210 });

    for (int a = 0; a < 8; a++) {
        float rad = (float)a * (2.0f * PI / 8.0f);
        float cosA = cosf(rad);
        float sinA = sinf(rad);
        DrawLine3D((Vector3){ cosA * 0.76f, 0.01f, sinA * 0.76f },
                   (Vector3){ cosA * 0.84f, 0.01f, sinA * 0.84f },
                   (Color){ 245, 210, 100, 220 });
    }

    inst->position = (Vector3){ 0.0f, 0.0f, 0.0f };
    inst->yaw = s_previewYaw;
    inst->scale = 1.0f;
    inst->tint = WHITE;
    ModularInstance_Draw(inst);

    EndMode3D();
    EndScissorMode();

    // Rotation prompt banner below character
    float promptW = 290.0f;
    Rectangle promptRec = { centerPanel.x + centerPanel.width * 0.5f - promptW * 0.5f,
                            centerPanel.y + centerPanel.height - 32.0f, promptW, 24.0f };
    DrawRectangleRec(promptRec, (Color){ 15, 18, 26, 220 });
    DrawRectangleLinesEx(promptRec, 1.0f, (Color){ 70, 80, 100, 190 });
    DrawTriangle((Vector2){ promptRec.x + 12, promptRec.y + 12 },
                 (Vector2){ promptRec.x + 20, promptRec.y + 7 },
                 (Vector2){ promptRec.x + 20, promptRec.y + 17 }, COLOR_TEXT_GOLD);
    DrawTriangle((Vector2){ promptRec.x + promptRec.width - 12, promptRec.y + 12 },
                 (Vector2){ promptRec.x + promptRec.width - 20, promptRec.y + 17 },
                 (Vector2){ promptRec.x + promptRec.width - 20, promptRec.y + 7 }, COLOR_TEXT_GOLD);
    DrawWuxiaTextCentered("Kéo chuột trái để xoay 360°", promptRec.x + promptRec.width * 0.5f, promptRec.y + 5, 12, COLOR_TEXT_PARCHMENT);

    // -------------------------------------------------------------------------
    // RIGHT PANEL: Item Catalog Grid & Details
    // -------------------------------------------------------------------------
    float detailsH = 208.0f;
    float gridH = contentH - detailsH - 12.0f;
    Rectangle gridPanel = { rightPanel.x, rightPanel.y, rightW, gridH };
    Rectangle detailsPanel = { rightPanel.x, rightPanel.y + gridH + 12.0f, rightW, detailsH };

    DrawWuxiaFrame(gridPanel, COLOR_BG_PANEL, COLOR_BORDER_DIM);
    DrawDiamondIcon(gridPanel.x + 22.0f, gridPanel.y + 20.0f, 4.0f, COLOR_BORDER_GOLD_INNER);
    DrawWuxiaText(TextFormat("KHO ĐỒ: %s", GetSlotName(s_selectedSlot)), gridPanel.x + 34, gridPanel.y + 11, 15, COLOR_TEXT_GOLD);
    DrawLine((int)gridPanel.x + 14, (int)gridPanel.y + 32, (int)(gridPanel.x + rightW - 14), (int)gridPanel.y + 32, (Color){ 65, 70, 85, 200 });

    float cardY = gridPanel.y + 40.0f;
    float cardH = 58.0f;
    float cardGap = 8.0f;

    // Optional slots allow unequipping
    bool canUnequipSlot = (s_selectedSlot != MODULAR_SLOT_BASE_BODY);
    if (canUnequipSlot) {
        Rectangle emptyCard = { gridPanel.x + 12, cardY, rightW - 24, cardH };
        bool isEquippedEmpty = (PlayerWardrobe_GetEquippedId(pw, s_selectedSlot)[0] == '\0');
        bool hoverEmpty = CheckCollisionPointRec(mouse, emptyCard);

        if (clicked && hoverEmpty) {
            PlayerWardrobe_Unequip(pw, s_selectedSlot);
            PlayerWardrobe_ApplyToInstance(pw, inst);
            Wardrobe_SaveGlobal();
            s_selectedItemId[0] = '\0';
        }

        Color emptyBg = isEquippedEmpty ? (Color){ 44, 25, 25, 255 } : (hoverEmpty ? (Color){ 30, 34, 44, 255 } : (Color){ 18, 20, 27, 255 });
        Color emptyBrd = isEquippedEmpty ? (Color){ 220, 75, 75, 255 } : (hoverEmpty ? (Color){ 180, 185, 195, 255 } : COLOR_BORDER_DIM);
        DrawRectangleRec(emptyCard, emptyBg);
        DrawRectangleLinesEx(emptyCard, isEquippedEmpty ? 1.5f : 1.0f, emptyBrd);

        const char *emptyTitle = (s_selectedSlot == MODULAR_SLOT_WEAPON_R) ? "TAY KHÔNG (QUYỀN CƯỚC) - Tháo vũ khí" :
                                 (s_selectedSlot == MODULAR_SLOT_FULL_OUTFIT) ? "MẶC ĐỒ GHÉP MÓN (THÁO NGUYÊN BỘ)" :
                                 "<KHÔNG TRANG BỊ - ĐỂ TRỐNG>";
        DrawWuxiaText(emptyTitle, emptyCard.x + 14, emptyCard.y + 18, 14, isEquippedEmpty ? (Color){ 255, 120, 120, 255 } : (Color){ 180, 185, 195, 255 });

        if (isEquippedEmpty) {
            float tagW = GetWuxiaTextWidth("ĐANG DÙNG", 11) + 16.0f;
            Rectangle pillRec = { emptyCard.x + emptyCard.width - tagW - 12, emptyCard.y + 17, tagW, 24 };
            DrawRectangleRec(pillRec, (Color){ 20, 50, 32, 230 });
            DrawRectangleLinesEx(pillRec, 1.0f, (Color){ 75, 215, 120, 240 });
            DrawWuxiaTextCentered("ĐANG DÙNG", pillRec.x + pillRec.width * 0.5f, pillRec.y + 5, 11, (Color){ 90, 240, 140, 255 });
        } else {
            float tagW = GetWuxiaTextWidth("CHỌN THÁO", 11) + 16.0f;
            Rectangle pillRec = { emptyCard.x + emptyCard.width - tagW - 12, emptyCard.y + 17, tagW, 24 };
            DrawRectangleRec(pillRec, hoverEmpty ? (Color){ 60, 25, 25, 240 } : (Color){ 32, 18, 18, 220 });
            DrawRectangleLinesEx(pillRec, 1.0f, hoverEmpty ? (Color){ 220, 80, 80, 240 } : (Color){ 120, 50, 50, 180 });
            DrawWuxiaTextCentered("CHỌN THÁO", pillRec.x + pillRec.width * 0.5f, pillRec.y + 5, 11, hoverEmpty ? (Color){ 255, 130, 130, 255 } : (Color){ 180, 120, 120, 230 });
        }

        cardY += cardH + cardGap;
    }

    int totalCat = Wardrobe_GetCatalogCount();
    for (int i = 0; i < totalCat; i++) {
        const WardrobeCatalogItem *item = Wardrobe_GetCatalogItemByIndex(i);
        if (!item || item->slot != s_selectedSlot) continue;

        Rectangle cardRec = { gridPanel.x + 12, cardY, rightW - 24, cardH };
        bool isOwned = PlayerWardrobe_HasItem(pw, item->id);
        bool isEquipped = (strcmp(PlayerWardrobe_GetEquippedId(pw, s_selectedSlot), item->id) == 0);
        bool isCardSelected = (strcmp(s_selectedItemId, item->id) == 0);
        bool cardHover = CheckCollisionPointRec(mouse, cardRec);

        if (clicked && cardHover) {
            strncpy(s_selectedItemId, item->id, sizeof(s_selectedItemId) - 1);
            if (!isEquipped) {
                PlayerWardrobe_UnlockItem(pw, item->id);
                PlayerWardrobe_Equip(pw, item->id);
                PlayerWardrobe_ApplyToInstance(pw, inst);
                Wardrobe_SaveGlobal();
            }
        }

        Color cardBg = isCardSelected ? (Color){ 44, 40, 26, 255 } : (cardHover ? (Color){ 30, 34, 44, 255 } : (Color){ 18, 20, 27, 255 });
        Color cardBrd = isCardSelected ? COLOR_BORDER_SELECT : (isEquipped ? GOLD : (cardHover ? COLOR_BORDER_GOLD_INNER : COLOR_BORDER_DIM));
        if (!isOwned) {
            cardBg = (Color){ 14, 15, 20, 255 };
            cardBrd = (Color){ 35, 38, 48, 255 };
        }

        DrawRectangleRec(cardRec, cardBg);
        DrawRectangleLinesEx(cardRec, (isEquipped || isCardSelected) ? 1.5f : 1.0f, cardBrd);

        if (isEquipped) {
            DrawRectangle((int)cardRec.x, (int)cardRec.y, 3, (int)cardRec.height, (Color){ 80, 220, 120, 255 });
        } else if (isCardSelected) {
            DrawRectangle((int)cardRec.x, (int)cardRec.y, 3, (int)cardRec.height, COLOR_BORDER_SELECT);
        }

        Color elemCol = GetElementColor(item->element);
        DrawDiamondIcon(cardRec.x + 18.0f, cardRec.y + 15.0f, 3.5f, elemCol);
        DrawWuxiaText(TextFormat("[%s]", GetElementName(item->element)), cardRec.x + 28, cardRec.y + 9, 11, elemCol);

        Color titleCol = isOwned ? GetRarityColor(item->rarity) : (Color){ 110, 115, 125, 255 };
        DrawWuxiaText(item->name, cardRec.x + 14, cardRec.y + 28, 14, titleCol);

        if (isEquipped) {
            float tagW = GetWuxiaTextWidth("ĐANG DÙNG", 11) + 16.0f;
            Rectangle pillRec = { cardRec.x + cardRec.width - tagW - 12, cardRec.y + 17, tagW, 24 };
            DrawRectangleRec(pillRec, (Color){ 20, 50, 32, 230 });
            DrawRectangleLinesEx(pillRec, 1.0f, (Color){ 75, 215, 120, 240 });
            DrawWuxiaTextCentered("ĐANG DÙNG", pillRec.x + pillRec.width * 0.5f, pillRec.y + 5, 11, (Color){ 90, 240, 140, 255 });
        } else if (isOwned) {
            float tagW = GetWuxiaTextWidth("TRANG BỊ", 11) + 16.0f;
            Rectangle pillRec = { cardRec.x + cardRec.width - tagW - 12, cardRec.y + 17, tagW, 24 };
            DrawRectangleRec(pillRec, cardHover ? (Color){ 60, 48, 22, 240 } : (Color){ 30, 26, 18, 220 });
            DrawRectangleLinesEx(pillRec, 1.0f, cardHover ? COLOR_BORDER_SELECT : (Color){ 160, 130, 65, 200 });
            DrawWuxiaTextCentered("TRANG BỊ", pillRec.x + pillRec.width * 0.5f, pillRec.y + 5, 11, cardHover ? COLOR_TEXT_GOLD : (Color){ 190, 175, 140, 240 });
        } else {
            float tagW = GetWuxiaTextWidth("CHƯA MỞ", 11) + 16.0f;
            Rectangle pillRec = { cardRec.x + cardRec.width - tagW - 12, cardRec.y + 17, tagW, 24 };
            DrawRectangleRec(pillRec, (Color){ 22, 24, 30, 220 });
            DrawRectangleLinesEx(pillRec, 1.0f, (Color){ 60, 65, 75, 180 });
            DrawWuxiaTextCentered("CHƯA MỞ", pillRec.x + pillRec.width * 0.5f, pillRec.y + 5, 11, (Color){ 110, 115, 125, 240 });
        }

        cardY += cardH + cardGap;
    }

    // -------------------------------------------------------------------------
    // DETAILS PANEL (Bottom Right: Item Lore, Stats & Online Unlock Hint)
    // -------------------------------------------------------------------------
    DrawWuxiaFrame(detailsPanel, COLOR_BG_PANEL, COLOR_BORDER_DIM);

    const WardrobeCatalogItem *selItem = (s_selectedItemId[0] != '\0') ? Wardrobe_GetCatalogItem(s_selectedItemId) : NULL;
    if (selItem) {
        bool isEquipped = (strcmp(PlayerWardrobe_GetEquippedId(pw, selItem->slot), selItem->id) == 0);
        bool isOwned = PlayerWardrobe_HasItem(pw, selItem->id);

        DrawDiamondIcon(detailsPanel.x + 22.0f, detailsPanel.y + 18.0f, 4.5f, GetRarityColor(selItem->rarity));
        DrawWuxiaText(selItem->name, detailsPanel.x + 34, detailsPanel.y + 9, 17, GetRarityColor(selItem->rarity));

        const char *typeStr = (selItem->itemType == ITEM_TYPE_FULL_OUTFIT) ? "NGUYÊN BỘ" : "MÓN LẺ";
        DrawWuxiaText(TextFormat("Phẩm chất: %s  |  Ngũ Hành: %s  |  Loại: %s",
                                 GetRarityName(selItem->rarity), GetElementName(selItem->element), typeStr),
                      detailsPanel.x + 16, detailsPanel.y + 32, 11, (Color){ 175, 180, 192, 255 });

        // Online Progression Source / Condition
        if (isOwned) {
            DrawWuxiaText(TextFormat("Lai lịch: %s", selItem->source),
                          detailsPanel.x + 16, detailsPanel.y + 50, 11, COLOR_TEXT_GOLD);
        } else {
            DrawWuxiaText(TextFormat("Điều kiện thu thập: %s", selItem->unlockHint[0] ? selItem->unlockHint : selItem->source),
                          detailsPanel.x + 16, detailsPanel.y + 50, 11, (Color){ 255, 160, 90, 255 });
        }

        // Lore description wrapped
        float descMaxW = detailsPanel.width - 32.0f;
        DrawWuxiaTextWrapped(selItem->description, detailsPanel.x + 16, detailsPanel.y + 68, descMaxW, 11.5f, (Color){ 220, 225, 235, 255 }, 15.0f);

        // Combat Attributes & Perks Section (Ready for gameplay damage & stat hooks)
        char statsSummary[192] = "";
        int statTokens = 0;
        if (selItem->stats.bonusHealth > 0.0f) {
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary),
                     "+%.0f Sinh Lực  ", selItem->stats.bonusHealth);
            statTokens++;
        }
        if (selItem->stats.bonusMana > 0.0f) {
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary),
                     "+%.0f Chân Khí  ", selItem->stats.bonusMana);
            statTokens++;
        }
        if (selItem->stats.speedMultiplier > 1.0f) {
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary),
                     "+%.0f%% Thân Pháp  ", (selItem->stats.speedMultiplier - 1.0f) * 100.0f);
            statTokens++;
        }
        if (selItem->stats.elementalBonus > 0.0f) {
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary),
                     "+%.0f%% Sát Thương  ", selItem->stats.elementalBonus * 100.0f);
            statTokens++;
        }
        if (selItem->stats.elementalResist > 0.0f) {
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary),
                     "+%.0f%% Kháng Thủ  ", selItem->stats.elementalResist * 100.0f);
            statTokens++;
        }

        float statsY = detailsPanel.y + 106.0f;
        if (statTokens > 0) {
            DrawWuxiaText(TextFormat("Thuộc tính: %s", statsSummary), detailsPanel.x + 16, statsY, 11, (Color){ 100, 225, 140, 255 });
            statsY += 18.0f;
        }

        if (selItem->stats.specialPerk[0] != '\0') {
            DrawWuxiaTextWrapped(TextFormat("Đặc tính: %s", selItem->stats.specialPerk),
                                 detailsPanel.x + 16, statsY, descMaxW, 11, SKYBLUE, 15.0f);
        }

        // Action Buttons
        Rectangle actBtn = { detailsPanel.x + detailsPanel.width - 162, detailsPanel.y + detailsPanel.height - 42, 146, 32 };
        bool actHover = CheckCollisionPointRec(mouse, actBtn);

        if (isEquipped) {
            if (selItem->slot != MODULAR_SLOT_BASE_BODY) {
                Color btnBgTop = actHover ? (Color){ 140, 32, 32, 255 } : (Color){ 85, 20, 20, 255 };
                Color btnBgBot = actHover ? (Color){ 85, 16, 16, 255 } : (Color){ 50, 12, 12, 255 };
                DrawRectangleGradientV((int)actBtn.x, (int)actBtn.y, (int)actBtn.width, (int)actBtn.height, btnBgTop, btnBgBot);
                DrawRectangleLinesEx(actBtn, 1.5f, actHover ? (Color){ 255, 95, 85, 255 } : (Color){ 190, 65, 65, 240 });
                DrawWuxiaTextCentered("THÁO RA", actBtn.x + actBtn.width * 0.5f, actBtn.y + 8, 13, RAYWHITE);
                if (clicked && actHover) {
                    PlayerWardrobe_Unequip(pw, selItem->slot);
                    PlayerWardrobe_ApplyToInstance(pw, inst);
                    Wardrobe_SaveGlobal();
                }
            }
        } else if (isOwned) {
            Color btnBgTop = actHover ? (Color){ 205, 155, 48, 255 } : (Color){ 125, 92, 26, 255 };
            Color btnBgBot = actHover ? (Color){ 125, 92, 26, 255 } : (Color){ 72, 52, 15, 255 };
            DrawRectangleGradientV((int)actBtn.x, (int)actBtn.y, (int)actBtn.width, (int)actBtn.height, btnBgTop, btnBgBot);
            DrawRectangleLinesEx(actBtn, 1.5f, actHover ? COLOR_BORDER_SELECT : COLOR_BORDER_GOLD_INNER);
            DrawWuxiaTextCentered("TRANG BỊ NGAY", actBtn.x + actBtn.width * 0.5f, actBtn.y + 8, 13, COLOR_TEXT_GOLD);
            if (clicked && actHover) {
                PlayerWardrobe_Equip(pw, selItem->id);
                PlayerWardrobe_ApplyToInstance(pw, inst);
                Wardrobe_SaveGlobal();
            }
        } else {
            // Locked item button: Allow unlocking & equipping!
            Color btnBgTop = actHover ? (Color){ 35, 110, 160, 255 } : (Color){ 25, 75, 115, 255 };
            Color btnBgBot = actHover ? (Color){ 20, 70, 105, 255 } : (Color){ 15, 45, 75, 255 };
            DrawRectangleGradientV((int)actBtn.x, (int)actBtn.y, (int)actBtn.width, (int)actBtn.height, btnBgTop, btnBgBot);
            DrawRectangleLinesEx(actBtn, 1.5f, actHover ? SKYBLUE : (Color){ 60, 150, 220, 220 });
            DrawWuxiaTextCentered("MỞ KHÓA & DÙNG", actBtn.x + actBtn.width * 0.5f, actBtn.y + 8, 12, (Color){ 220, 245, 255, 255 });
            if (clicked && actHover) {
                PlayerWardrobe_UnlockItem(pw, selItem->id);
                PlayerWardrobe_Equip(pw, selItem->id);
                PlayerWardrobe_ApplyToInstance(pw, inst);
                Wardrobe_SaveGlobal();
            }
        }
    } else {
        DrawWuxiaText("CHƯA CHỌN TRANG BỊ", detailsPanel.x + 16, detailsPanel.y + 16, 14, COLOR_TEXT_MUTED);
        DrawWuxiaText("Hãy nhấn vào một trang bị trong danh sách bên trên để xem lai lịch, thuộc tính và trang bị.",
                      detailsPanel.x + 16, detailsPanel.y + 42, 12, (Color){ 150, 155, 165, 255 });
    }
}
