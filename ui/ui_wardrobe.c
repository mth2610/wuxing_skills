// ui/ui_wardrobe.c
// Interactive Wuxia Wardrobe & Equipment Selection UI — Unified "Where Winds Meet" Theme.
#include "ui/ui_wardrobe.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "character/wardrobe_system.h"
#include "character/modular_character.h"
#include "raymath.h"
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

static Color GetRarityColor(WardrobeRarity rarity) {
    switch (rarity) {
        case WARDROBE_RARITY_COMMON:    return (Color){ 210, 215, 225, 255 };
        case WARDROBE_RARITY_UNCOMMON:  return (Color){ 80, 220, 135, 255 };
        case WARDROBE_RARITY_RARE:      return (Color){ 70, 165, 255, 255 };
        case WARDROBE_RARITY_EPIC:      return (Color){ 195, 105, 255, 255 };
        case WARDROBE_RARITY_LEGENDARY: return (Color){ 255, 215, 60, 255 };
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

void UI_Wardrobe_Init(void) {
    WuxiaUI_Init();
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
}

void UI_Wardrobe_Toggle(PlayerWardrobe *pw, ModularInstance *inst) {
    if (s_isOpen) UI_Wardrobe_Close();
    else          UI_Wardrobe_Open(pw, inst);
}

void UI_Wardrobe_Open(PlayerWardrobe *pw, ModularInstance *inst) {
    (void)inst;
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

bool UI_Wardrobe_IsOpen(void) { return s_isOpen; }
void UI_Wardrobe_SetPreviewYaw(float yaw) { s_previewYaw = yaw; }

void UI_Wardrobe_UpdateDraw(PlayerWardrobe *pw, ModularInstance *inst, float dt) {
    if (!s_isOpen || !pw || !inst) return;

    WuxiaUI_BeginFrame(UI_IsTouchControlsEnabled());
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    Vector2 mouse = ctx->pointerPos;
    bool clicked  = ctx->pointerReleased;

    if (s_justOpened) {
        s_justOpened = false;
    } else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_B)) {
        UI_Wardrobe_Close();
        return;
    }

    float screenW = ctx->screenW;
    float screenH = ctx->screenH;

    // 1. Dark translucent backdrop
    DrawRectangle(0, 0, (int)screenW, (int)screenH, (Color){ 8, 10, 15, 244 });

    // 2. Top Header Bar
    Rectangle headerRec = { 20.0f, 14.0f, screenW - 40.0f, 56.0f };
    WuxiaUI_DrawPanel(headerRec, WUXIA_GOLD, true);

    WuxiaUI_DrawDiamond((Vector2){ 38.0f, 34.0f }, 6.0f, WUXIA_GOLD, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawTextTitle("TỦ ĐỒ HIỆP KHÁCH", 52.0f, 18.0f, 22.0f, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawText("Kho Trang Bị & Bộ Sưu Tập Giang Hồ", 52.0f, 44.0f, 12.0f, WUXIA_TEXT_MUTED);

    // Combat Stance Banner in Center
    CombatStance stance = PlayerWardrobe_GetCombatStance(pw);
    float bannerW = fminf(320.0f, screenW * 0.28f);
    Rectangle stanceRec = { screenW * 0.5f - bannerW * 0.5f, 20.0f, bannerW, 44.0f };
    if (stance == COMBAT_STANCE_SWORD) {
        WuxiaUI_DrawPanel(stanceRec, WUXIA_GOLD_BRIGHT, false);
        WuxiaUI_DrawTextCentered("THẾ VÕ: KIẾM PHÁP",
                                 (Rectangle){ stanceRec.x, stanceRec.y + 4.0f, stanceRec.width, 20.0f },
                                 14.5f, WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawTextCentered("Khai mở Trảm, Đâm, Đỡ, Gạt",
                                 (Rectangle){ stanceRec.x, stanceRec.y + 23.0f, stanceRec.width, 16.0f },
                                 11.0f, WUXIA_TEXT_MUTED);
    } else {
        WuxiaUI_DrawPanel(stanceRec, WUXIA_AZURE, false);
        WuxiaUI_DrawTextCentered("THẾ VÕ: QUYỀN CƯỚC",
                                 (Rectangle){ stanceRec.x, stanceRec.y + 4.0f, stanceRec.width, 20.0f },
                                 14.5f, WUXIA_AZURE);
        WuxiaUI_DrawTextCentered("Khai mở Quyền, Chưởng, Cước, Vận khí",
                                 (Rectangle){ stanceRec.x, stanceRec.y + 23.0f, stanceRec.width, 16.0f },
                                 11.0f, WUXIA_TEXT_MUTED);
    }

    // Close Button
    if (WuxiaUI_DrawButton((Rectangle){ screenW - 146.0f, 22.0f, 114.0f, 38.0f },
                           ctx->isTouchMode ? "ĐÓNG" : "ĐÓNG [ESC]", WUXIA_CRIMSON, false, 13.0f)) {
        UI_Wardrobe_Close();
        return;
    }

    // Layout Dimensions
    float contentY = 78.0f;
    float contentH = screenH - contentY - 18.0f;
    float leftW   = fminf(246.0f, screenW * 0.21f);
    float centerW = fminf(440.0f, screenW * 0.36f);
    float rightW  = screenW - leftW - centerW - 60.0f;

    Rectangle leftPanel   = { 20.0f, contentY, leftW, contentH };
    Rectangle centerPanel = { 30.0f + leftW, contentY, centerW, contentH };
    Rectangle rightPanel  = { 40.0f + leftW + centerW, contentY, rightW, contentH };

    // LEFT PANEL: Slot Category Tabs
    WuxiaUI_DrawPanel(leftPanel, WUXIA_BORDER_SUBTLE, true);
    WuxiaUI_DrawSectionHeader(leftPanel.x + 14.0f, leftPanel.y + 12.0f, leftW - 28.0f, "CÁC BỘ PHẬN");

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
    float slotRowH = fminf(49.0f, (contentH - 48.0f) / (float)slotCount - 4.0f);
    float slotRowGap = 4.0f;
    bool hasFullOutfit = (pw->equippedIds[MODULAR_SLOT_FULL_OUTFIT][0] != '\0');

    for (int i = 0; i < slotCount; i++) {
        ModularSlot sl = slots[i];
        Rectangle rowRec = { leftPanel.x + 10.0f, leftPanel.y + 38.0f + i * (slotRowH + slotRowGap), leftW - 20.0f, slotRowH };
        bool isSelected = (s_selectedSlot == sl);
        bool isHover = CheckCollisionPointRec(mouse, rowRec);

        if (clicked && isHover) {
            s_selectedSlot = sl;
            const char *eq = PlayerWardrobe_GetEquippedId(pw, sl);
            if (eq && eq[0]) strncpy(s_selectedItemId, eq, sizeof(s_selectedItemId) - 1);
            else s_selectedItemId[0] = '\0';
        }

        Color rowBg  = isSelected ? (Color){ 48, 42, 26, 255 } : (isHover ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD);
        Color rowBrd = isSelected ? WUXIA_GOLD_BRIGHT : (isHover ? WUXIA_GOLD : WUXIA_BORDER_SUBTLE);
        DrawRectangleRec(rowRec, rowBg);
        DrawRectangleLinesEx(rowRec, isSelected ? 1.5f : 1.0f, rowBrd);

        if (isSelected) {
            DrawRectangle((int)rowRec.x, (int)rowRec.y, 4, (int)rowRec.height, WUXIA_GOLD_BRIGHT);
            WuxiaUI_DrawDiamond((Vector2){ rowRec.x + rowRec.width - 12.0f, rowRec.y + rowRec.height * 0.5f },
                                3.5f, WUXIA_GOLD_BRIGHT, WHITE);
        }

        WuxiaUI_DrawText(GetSlotName(sl), rowRec.x + 10.0f, rowRec.y + 4.0f, 11.0f,
                         isSelected ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_MUTED);

        bool isSuppressedByOutfit = (hasFullOutfit && (sl == MODULAR_SLOT_TORSO || sl == MODULAR_SLOT_LEGS));
        const char *equippedId = PlayerWardrobe_GetEquippedId(pw, sl);
        if (isSuppressedByOutfit) {
            WuxiaUI_DrawText("[Ẩn bởi Nguyên Bộ]", rowRec.x + 10.0f, rowRec.y + 21.0f, 11.0f, WUXIA_GOLD_DIM);
        } else if (equippedId && equippedId[0] != '\0') {
            const WardrobeCatalogItem *cat = Wardrobe_GetCatalogItem(equippedId);
            const char *dispName = cat ? cat->name : equippedId;
            Color nameCol = cat ? GetRarityColor(cat->rarity) : RAYWHITE;
            WuxiaUI_DrawText(dispName, rowRec.x + 10.0f, rowRec.y + 21.0f, 13.0f, nameCol);
        } else {
            WuxiaUI_DrawText("<Để trống>", rowRec.x + 10.0f, rowRec.y + 21.0f, 12.0f, WUXIA_TEXT_MUTED);
        }
    }

    // CENTER PANEL: 3D Character Preview Stage
    WuxiaUI_DrawPanel(centerPanel, WUXIA_BORDER_SUBTLE, true);
    bool inCenter = CheckCollisionPointRec(mouse, centerPanel);
    if (ctx->pointerPressed && inCenter) {
        s_isDragging = true;
        s_dragStartMouse = mouse;
        s_dragStartYaw = s_previewYaw;
    }
    if (s_isDragging) {
        if (ctx->pointerDown) {
            float dx = mouse.x - s_dragStartMouse.x;
            s_previewYaw = s_dragStartYaw + dx * 0.015f;
        } else {
            s_isDragging = false;
        }
    }

    BeginScissorMode((int)centerPanel.x + 2, (int)centerPanel.y + 2, (int)centerPanel.width - 4, (int)centerPanel.height - 4);
    BeginMode3D(s_previewCamera);

    ModularInstance_Update(inst, dt, false);

    DrawCylinder((Vector3){ 0.0f, -0.09f, 0.0f }, 1.05f, 1.05f, 0.05f, 36, (Color){ 20, 22, 30, 255 });
    DrawCylinderWires((Vector3){ 0.0f, -0.09f, 0.0f }, 1.05f, 1.05f, 0.05f, 36, (Color){ 90, 80, 55, 180 });
    DrawCylinder((Vector3){ 0.0f, -0.04f, 0.0f }, 0.88f, 0.88f, 0.05f, 36, (Color){ 28, 32, 42, 255 });
    DrawCylinderWires((Vector3){ 0.0f, -0.04f, 0.0f }, 0.88f, 0.88f, 0.05f, 36, WUXIA_GOLD);
    DrawCylinder((Vector3){ 0.0f, 0.005f, 0.0f }, 0.74f, 0.74f, 0.01f, 36, (Color){ 36, 40, 52, 255 });

    inst->position = (Vector3){ 0.0f, 0.0f, 0.0f };
    inst->yaw = s_previewYaw;
    inst->scale = 1.0f;
    inst->tint = WHITE;
    ModularInstance_Draw(inst);

    EndMode3D();
    EndScissorMode();

    Rectangle promptRec = { centerPanel.x + centerPanel.width * 0.5f - 145.0f,
                            centerPanel.y + centerPanel.height - 34.0f, 290.0f, 24.0f };
    WuxiaUI_DrawRibbon(promptRec, WUXIA_GOLD);
    WuxiaUI_DrawTextCentered(ctx->isTouchMode ? "Vuốt ngang để xoay 360°" : "Kéo chuột trái để xoay 360°",
                             promptRec, 12.0f, WUXIA_TEXT_PRIMARY);

    // RIGHT PANEL: Item Catalog & Details
    float detailsH = 212.0f;
    float gridH = contentH - detailsH - 12.0f;
    Rectangle gridPanel    = { rightPanel.x, rightPanel.y, rightW, gridH };
    Rectangle detailsPanel = { rightPanel.x, rightPanel.y + gridH + 12.0f, rightW, detailsH };

    WuxiaUI_DrawPanel(gridPanel, WUXIA_BORDER_SUBTLE, true);
    WuxiaUI_DrawSectionHeader(gridPanel.x + 14.0f, gridPanel.y + 12.0f, rightW - 28.0f,
                              TextFormat("KHO ĐỒ: %s", GetSlotName(s_selectedSlot)));

    float cardY = gridPanel.y + 40.0f;
    float cardH = 56.0f;
    float cardGap = 8.0f;

    bool canUnequipSlot = (s_selectedSlot != MODULAR_SLOT_BASE_BODY);
    if (canUnequipSlot) {
        Rectangle emptyCard = { gridPanel.x + 12.0f, cardY, rightW - 24.0f, cardH };
        bool isEquippedEmpty = (PlayerWardrobe_GetEquippedId(pw, s_selectedSlot)[0] == '\0');
        bool hoverEmpty = CheckCollisionPointRec(mouse, emptyCard);

        if (clicked && hoverEmpty) {
            PlayerWardrobe_Unequip(pw, s_selectedSlot);
            PlayerWardrobe_ApplyToInstance(pw, inst);
            Wardrobe_SaveGlobal();
            s_selectedItemId[0] = '\0';
        }

        Color emptyBg = isEquippedEmpty ? (Color){ 44, 25, 25, 255 } : (hoverEmpty ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD);
        Color emptyBrd = isEquippedEmpty ? WUXIA_CRIMSON : (hoverEmpty ? WUXIA_GOLD : WUXIA_BORDER_SUBTLE);
        DrawRectangleRec(emptyCard, emptyBg);
        DrawRectangleLinesEx(emptyCard, isEquippedEmpty ? 1.5f : 1.0f, emptyBrd);

        const char *emptyTitle = (s_selectedSlot == MODULAR_SLOT_WEAPON_R) ? "Tay Không (Quyền Cước) — Tháo vũ khí" :
                                 (s_selectedSlot == MODULAR_SLOT_FULL_OUTFIT) ? "Mặc Đồ Ghép Món (Tháo Nguyên Bộ)" :
                                 "<Không Trang Bị — Để Trống>";
        WuxiaUI_DrawText(emptyTitle, emptyCard.x + 14.0f, emptyCard.y + 19.0f, 14.0f,
                         isEquippedEmpty ? (Color){ 255, 130, 130, 255 } : WUXIA_TEXT_PRIMARY);
        cardY += cardH + cardGap;
    }

    int totalCat = Wardrobe_GetCatalogCount();
    for (int i = 0; i < totalCat; i++) {
        const WardrobeCatalogItem *item = Wardrobe_GetCatalogItemByIndex(i);
        if (!item || item->slot != s_selectedSlot) continue;
        if (cardY + cardH > gridPanel.y + gridPanel.height - 8.0f) break;

        Rectangle cardRec = { gridPanel.x + 12.0f, cardY, rightW - 24.0f, cardH };
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

        Color cardBg = isCardSelected ? (Color){ 44, 40, 26, 255 } : (cardHover ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD);
        Color cardBrd = isCardSelected ? WUXIA_GOLD_BRIGHT : (isEquipped ? WUXIA_JADE : (cardHover ? WUXIA_GOLD : WUXIA_BORDER_SUBTLE));
        DrawRectangleRec(cardRec, cardBg);
        DrawRectangleLinesEx(cardRec, (isEquipped || isCardSelected) ? 1.5f : 1.0f, cardBrd);
        if (isEquipped || isCardSelected) {
            DrawRectangle((int)cardRec.x, (int)cardRec.y, 4, (int)cardRec.height, isEquipped ? WUXIA_JADE : WUXIA_GOLD_BRIGHT);
        }

        Color elemCol = GetElementColor(item->element);
        WuxiaUI_DrawText(TextFormat("[%s]", GetElementName(item->element)), cardRec.x + 14.0f, cardRec.y + 8.0f, 11.0f, elemCol);
        WuxiaUI_DrawText(item->name, cardRec.x + 14.0f, cardRec.y + 26.0f, 14.5f,
                         isOwned ? GetRarityColor(item->rarity) : WUXIA_TEXT_MUTED);

        const char *tag = isEquipped ? "ĐANG DÙNG" : (isOwned ? "TRANG BỊ" : "CHƯA MỞ");
        Color tagCol = isEquipped ? WUXIA_JADE : (isOwned ? WUXIA_GOLD : WUXIA_TEXT_MUTED);
        WuxiaUI_DrawTextRight(tag, cardRec.x + cardRec.width - 14.0f, cardRec.y + 20.0f, 12.0f, tagCol);

        cardY += cardH + cardGap;
    }

    // DETAILS PANEL
    WuxiaUI_DrawPanel(detailsPanel, WUXIA_BORDER_SUBTLE, true);
    const WardrobeCatalogItem *selItem = (s_selectedItemId[0] != '\0') ? Wardrobe_GetCatalogItem(s_selectedItemId) : NULL;
    if (selItem) {
        bool isEquipped = (strcmp(PlayerWardrobe_GetEquippedId(pw, selItem->slot), selItem->id) == 0);
        bool isOwned = PlayerWardrobe_HasItem(pw, selItem->id);

        WuxiaUI_DrawDiamond((Vector2){ detailsPanel.x + 22.0f, detailsPanel.y + 19.0f }, 4.5f,
                            GetRarityColor(selItem->rarity), WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawText(selItem->name, detailsPanel.x + 34.0f, detailsPanel.y + 10.0f, 17.0f, GetRarityColor(selItem->rarity));

        const char *typeStr = (selItem->itemType == ITEM_TYPE_FULL_OUTFIT) ? "Nguyên Bộ" : "Món Lẻ";
        WuxiaUI_DrawText(TextFormat("Phẩm chất: %s  ·  Ngũ Hành: %s  ·  Loại: %s",
                                    GetRarityName(selItem->rarity), GetElementName(selItem->element), typeStr),
                         detailsPanel.x + 16.0f, detailsPanel.y + 34.0f, 11.5f, WUXIA_TEXT_MUTED);

        WuxiaUI_DrawText(isOwned ? TextFormat("Lai lịch: %s", selItem->source)
                                 : TextFormat("Điều kiện: %s", selItem->unlockHint[0] ? selItem->unlockHint : selItem->source),
                         detailsPanel.x + 16.0f, detailsPanel.y + 52.0f, 11.5f,
                         isOwned ? WUXIA_GOLD : (Color){ 255, 165, 95, 255 });

        WuxiaUI_DrawTextWrapped(selItem->description,
                                (Rectangle){ detailsPanel.x + 16.0f, detailsPanel.y + 72.0f, detailsPanel.width - 32.0f, 52.0f },
                                12.0f, WUXIA_TEXT_PRIMARY, 4.0f);

        char statsSummary[192] = "";
        if (selItem->stats.bonusHealth > 0.0f)
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary), "+%.0f Sinh Lực  ", selItem->stats.bonusHealth);
        if (selItem->stats.bonusMana > 0.0f)
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary), "+%.0f Chân Khí  ", selItem->stats.bonusMana);
        if (selItem->stats.speedMultiplier > 1.0f)
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary), "+%.0f%% Thân Pháp  ", (selItem->stats.speedMultiplier - 1.0f) * 100.0f);
        if (selItem->stats.elementalBonus > 0.0f)
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary), "+%.0f%% Sát Thương  ", selItem->stats.elementalBonus * 100.0f);
        if (selItem->stats.elementalResist > 0.0f)
            snprintf(statsSummary + strlen(statsSummary), sizeof(statsSummary) - strlen(statsSummary), "+%.0f%% Kháng Thủ  ", selItem->stats.elementalResist * 100.0f);

        if (statsSummary[0] != '\0') {
            WuxiaUI_DrawText(TextFormat("Thuộc tính: %s", statsSummary), detailsPanel.x + 16.0f, detailsPanel.y + 132.0f, 12.0f, WUXIA_JADE);
        }
        if (selItem->stats.specialPerk[0] != '\0') {
            WuxiaUI_DrawText(TextFormat("Đặc tính: %s", selItem->stats.specialPerk), detailsPanel.x + 16.0f, detailsPanel.y + 150.0f, 11.5f, WUXIA_AZURE);
        }

        Rectangle actBtn = { detailsPanel.x + detailsPanel.width - 166.0f, detailsPanel.y + detailsPanel.height - 46.0f, 150.0f, 34.0f };
        if (isEquipped) {
            if (selItem->slot != MODULAR_SLOT_BASE_BODY) {
                if (WuxiaUI_DrawButton(actBtn, "THÁO RA", WUXIA_CRIMSON, false, 13.0f)) {
                    PlayerWardrobe_Unequip(pw, selItem->slot);
                    PlayerWardrobe_ApplyToInstance(pw, inst);
                    Wardrobe_SaveGlobal();
                }
            }
        } else if (isOwned) {
            if (WuxiaUI_DrawButton(actBtn, "TRANG BỊ NGAY", WUXIA_GOLD, true, 13.0f)) {
                PlayerWardrobe_Equip(pw, selItem->id);
                PlayerWardrobe_ApplyToInstance(pw, inst);
                Wardrobe_SaveGlobal();
            }
        } else {
            if (WuxiaUI_DrawButton(actBtn, "MỞ KHÓA & DÙNG", WUXIA_AZURE, false, 12.5f)) {
                PlayerWardrobe_UnlockItem(pw, selItem->id);
                PlayerWardrobe_Equip(pw, selItem->id);
                PlayerWardrobe_ApplyToInstance(pw, inst);
                Wardrobe_SaveGlobal();
            }
        }
    } else {
        WuxiaUI_DrawText("CHƯA CHỌN TRANG BỊ", detailsPanel.x + 16.0f, detailsPanel.y + 16.0f, 15.0f, WUXIA_TEXT_MUTED);
        WuxiaUI_DrawTextWrapped("Nhấn vào một trang bị trong danh sách bên trên để xem lai lịch, thuộc tính và trang bị.",
                                (Rectangle){ detailsPanel.x + 16.0f, detailsPanel.y + 44.0f, detailsPanel.width - 32.0f, 60.0f },
                                13.0f, WUXIA_TEXT_MUTED, 4.0f);
    }
}
