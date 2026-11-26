// ui/ui_hub.c — "Where Winds Meet" Character Drawer, Categorized Mode Hub & Global Modal.
#include "ui/ui_hub.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_wardrobe.h"
#include "character/player_profile.h"
#include "character/wardrobe_system.h"
#include "skills/skill_manager.h"
#include "skills/vo_hoc/vo_hoc_codex.h"
#include "net/net_transport.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool  s_modalOpen = false;
static int   s_modalTab = 0;      // 0 = Võ Học 7 Ô, 1 = Tiệm Dã Tẩu
static int   s_selectedSlot = 0;  // 0..6
static int   s_setFilter = -1;    // -1 = Tất cả, 0 = Cơ Bản, 1..VH_SET_COUNT-1 = Bộ Võ Học
static float s_skillScrollY = 0.0f;
static bool  s_joinBoxOpen = false;

static const char *kRealmNames[5] = {
    "1. Hàn Thủy Đầm (Thủy)",
    "2. Thanh Mộc Lâm (Mộc)",
    "3. Xích Hỏa Sơn (Hỏa)",
    "4. Hoàng Thổ Đại Mạc (Thổ)",
    "5. Kim Tuyệt Cốc (Kim)"
};

static const char *kRealmSubtitles[5] = {
    "Thung lũng sông suối · Thích hợp hệ Thủy",
    "Rừng trúc thanh u · Ẩn thân hệ Mộc",
    "Núi lửa xích diễm · Tăng sát thương Hỏa",
    "Sa mạc cổ trận · Dã Tẩu & Bảo tàng",
    "Đỉnh tuyết kiếm các · Thử thách Kim"
};

static const Color kRealmColors[5] = {
    { 74, 168, 232, 255 },
    { 82, 204, 132, 255 },
    { 226, 92, 72, 255 },
    { 226, 178, 92, 255 },
    { 195, 210, 228, 255 }
};

static bool IsSkillSelectable(int skillIdx) {
    const char *n = GetRegisteredSkillName(skillIdx);
    if (!n || n[0] == '\0') return false;
    if (strncmp(n, "TAIJI_", 6) == 0) return false;
    if (strcmp(n, "CORE_TEST") == 0) return false;
    return true;
}

static const char *SkillDisplayNameVN(int skillIdx) {
    if (skillIdx < 0) return "(Trống)";
    const char *raw = GetRegisteredSkillName(skillIdx);
    const VoHocSkillDef *def = VoHoc_FindByName(raw);
    if (def && def->displayName && def->displayName[0]) return def->displayName;
    return raw;
}

static const char *VoHocFilterShortLabel(int filterId) {
    switch (filterId) {
        case -1: return "Tất cả";
        case VH_SET_THUY_TRIEU_QUYEN_PHAP: return "Thủy";
        case VH_SET_BAN_THACH_BAT_CUC_QUYEN: return "Thổ";
        case VH_SET_THIEN_CANG_DOAN_KIM_KIEM: return "Kim";
        case VH_SET_LIET_HOA_PHAN_THIEN_CUOC: return "Hỏa";
        case VH_SET_GIANG_LONG_THAP_BAT_CHUONG: return "Giáng Long";
        case VH_SET_NONE: return "Cơ Bản";
        default: return "Khác";
    }
}

static Color VoHocFilterColor(int filterId) {
    switch (filterId) {
        case VH_SET_THUY_TRIEU_QUYEN_PHAP: return ELEMENT_COLOR_WATER;
        case VH_SET_BAN_THACH_BAT_CUC_QUYEN: return ELEMENT_COLOR_EARTH;
        case VH_SET_THIEN_CANG_DOAN_KIM_KIEM: return ELEMENT_COLOR_METAL;
        case VH_SET_LIET_HOA_PHAN_THIEN_CUOC: return ELEMENT_COLOR_FIRE;
        case VH_SET_GIANG_LONG_THAP_BAT_CHUONG: return WUXIA_GOLD;
        case VH_SET_NONE: return WUXIA_TEXT_MUTED;
        default: return WUXIA_GOLD;
    }
}

static void EquipFullVoHocSet(VoHocSetId setId) {
    if (setId <= VH_SET_NONE || setId >= VH_SET_COUNT) return;
    int strikeSlot = 0;
    int supportSlot = PROFILE_STRIKE_SLOTS;
    int defCount = 0;
    const VoHocSkillDef *defs = VoHoc_GetAllSkills(&defCount);
    for (int i = 0; i < defCount; i++) {
        if (defs[i].setId != setId) continue;
        int idx = Skill_GetIndexByName(defs[i].skillName);
        if (idx < 0) continue;
        if (defs[i].role == VH_ROLE_STRIKE && strikeSlot < PROFILE_STRIKE_SLOTS) {
            PlayerProfile_EquipSkillSlot(strikeSlot++, idx);
        } else if (defs[i].role == VH_ROLE_SUPPORT && supportSlot < PROFILE_SKILL_SLOTS) {
            PlayerProfile_EquipSkillSlot(supportSlot++, idx);
        }
    }
    PlayerProfile_SaveGlobal();
}

void UI_Hub_Init(void) {
    WuxiaUI_Init();
    s_modalOpen = false;
    s_modalTab = 0;
    s_selectedSlot = 0;
    s_setFilter = -1;
    s_skillScrollY = 0.0f;
    s_joinBoxOpen = false;
}

void UI_Hub_OpenModal(int initialTab) {
    s_modalOpen = true;
    s_modalTab = (initialTab >= 0 && initialTab <= 1) ? initialTab : 0;
}

void UI_Hub_CloseModal(void) { s_modalOpen = false; }
bool UI_Hub_IsModalOpen(void) { return s_modalOpen || UI_Wardrobe_IsOpen(); }

void UI_Hub_UpdateDrawModal(void) {
    if (UI_Wardrobe_IsOpen()) {
        UI_Wardrobe_UpdateDraw(Wardrobe_GetGlobalPlayer(), Wardrobe_GetGlobalModularInstance(), GetFrameTime());
        return;
    }
    if (!s_modalOpen) return;

    WuxiaUI_BeginFrame(UI_IsTouchControlsEnabled());
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    float sw = ctx->screenW;
    float sh = ctx->screenH;

    DrawRectangle(0, 0, (int)sw, (int)sh, (Color){ 6, 8, 14, 215 });

    float pw = fminf(980.0f, sw - 32.0f);
    float ph = fminf(610.0f, sh - 36.0f);
    Rectangle p = { sw * 0.5f - pw * 0.5f, sh * 0.5f - ph * 0.5f, pw, ph };
    WuxiaUI_DrawPanel(p, WUXIA_GOLD, true);

    PlayerProfile *prof = PlayerProfile_GetGlobal();

    // Header + Currencies
    WuxiaUI_DrawDiamond((Vector2){ p.x + 24.0f, p.y + 27.0f }, 6.0f, WUXIA_GOLD, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawTextTitle("HÀNH TRANG & TÀNG KINH CÁC", p.x + 40.0f, p.y + 13.0f, 22.0f, WUXIA_GOLD_BRIGHT);

    const char *cur = TextFormat("Vàng: %d   ·   Điểm Dã Tẩu: %d   ·   Lệnh Bài: %d",
                                 prof->gold, prof->daTauPoints, prof->daTauTokens);
    WuxiaUI_DrawTextRight(cur, p.x + p.width - 64.0f, p.y + 19.0f, 13.5f, WUXIA_JADE);

    if (WuxiaUI_DrawIconPill((Rectangle){ p.x + p.width - 48.0f, p.y + 12.0f, 34.0f, 32.0f },
                             WUXIA_ICON_CLOSE, NULL, NULL, WUXIA_CRIMSON, false)) {
        s_modalOpen = false;
        return;
    }
    WuxiaUI_DrawSpecularRule(p.x + 18.0f, p.y + 50.0f, p.width - 36.0f, WUXIA_GOLD, true);

    // Tabs
    if (WuxiaUI_DrawIconPill((Rectangle){ p.x + 20.0f, p.y + 58.0f, 210.0f, 36.0f },
                             WUXIA_ICON_SCROLL, "1. Võ Học (7 Ô)", NULL, WUXIA_GOLD, s_modalTab == 0)) {
        s_modalTab = 0;
    }
    if (WuxiaUI_DrawIconPill((Rectangle){ p.x + 240.0f, p.y + 58.0f, 230.0f, 36.0f },
                             WUXIA_ICON_SHOP, "2. Tiệm Dã Tẩu & Đan", NULL, WUXIA_JADE, s_modalTab == 1)) {
        s_modalTab = 1;
    }
    if (WuxiaUI_DrawIconPill((Rectangle){ p.x + 480.0f, p.y + 58.0f, 200.0f, 36.0f },
                             WUXIA_ICON_BAG, "3. Tủ Đồ 3D", NULL, WUXIA_AZURE, false)) {
        s_modalOpen = false;
        UI_Wardrobe_Open(Wardrobe_GetGlobalPlayer(), Wardrobe_GetGlobalModularInstance());
        return;
    }

    if (s_modalTab == 0) {
        // Left: 7 Equipped Slots (5 Strike [1..5] + 2 Buff/Support [6..7])
        float leftX = p.x + 20.0f;
        float leftW = 268.0f;
        WuxiaUI_DrawSectionHeader(leftX, p.y + 104.0f, leftW, "7 Ô CHIÊU THỨC (5 ĐẢ + 2 TRỢ)");

        int eqIds[PROFILE_SKILL_SLOTS];
        float slotTop = p.y + 128.0f;
        float slotH = 54.0f;
        float slotGap = 7.0f;
        for (int s = 0; s < PROFILE_SKILL_SLOTS; s++) {
            float extraGap = (s >= PROFILE_STRIKE_SLOTS) ? 8.0f : 0.0f;
            Rectangle sr = { leftX, slotTop + s * (slotH + slotGap) + extraGap, leftW, slotH };
            bool sel = (s == s_selectedSlot);
            bool hov = CheckCollisionPointRec(ctx->pointerPos, sr);
            bool isBuffSlot = (s >= PROFILE_STRIKE_SLOTS);
            int idx = Skill_GetIndexByName(prof->equippedSkillNames[s]);
            eqIds[s] = idx;
            const char *rawName = (idx >= 0) ? GetRegisteredSkillName(idx) : NULL;
            const VoHocSkillDef *sdef = rawName ? VoHoc_FindByName(rawName) : NULL;
            const char *sname = SkillDisplayNameVN(idx);
            int lvl = (idx >= 0) ? prof->skillLevel[idx] : 1;
            Color sc = (idx >= 0) ? GetRegisteredSkillColor(idx) : WUXIA_TEXT_MUTED;

            DrawRectangleRec(sr, sel ? (Color){ 46, 42, 26, 248 }
                                     : (hov ? WUXIA_BG_CARD_HOV : (isBuffSlot ? (Color){ 18, 30, 40, 235 } : WUXIA_BG_CARD)));
            DrawRectangleLinesEx(sr, sel ? 1.8f : 1.0f,
                                 sel ? WUXIA_GOLD_BRIGHT : (isBuffSlot ? ColorAlpha(WUXIA_JADE, 0.55f) : WUXIA_BORDER_SUBTLE));
            DrawRectangle((int)sr.x, (int)sr.y, 4, (int)sr.height, isBuffSlot ? WUXIA_JADE : WUXIA_GOLD);

            const char *setShort = sdef ? VoHocFilterShortLabel((int)sdef->setId) : "Cơ Bản";
            WuxiaUI_DrawText(TextFormat("%s [%d] · %s · Lv.%d", isBuffSlot ? "TRỢ" : "ĐẢ", s + 1, setShort, lvl),
                             sr.x + 12.0f, sr.y + 6.0f, 11.0f, isBuffSlot ? WUXIA_JADE : WUXIA_TEXT_MUTED);
            WuxiaUI_DrawText(sname, sr.x + 12.0f, sr.y + 24.0f, 14.0f, sc);
            if (hov && ctx->pointerReleased) {
                if (s_selectedSlot != s) {
                    s_selectedSlot = s;
                    s_skillScrollY = 0.0f;
                }
            }
        }

        // Right column: Loadout Summary + Set Filter Bar + Scrollable Skill Grid
        float rightX = leftX + leftW + 20.0f;
        float rightW = p.x + p.width - 20.0f - rightX;

        VoHocLoadoutSummary sum = VoHoc_EvalLoadout(eqIds, PROFILE_SKILL_SLOTS);
        const char *tierTxt = (sum.setTier >= VO_HOC_TIER_MAJOR_COUNT) ? "Đại Thành (7/7)"
                            : ((sum.setTier >= VO_HOC_TIER_MINOR_COUNT) ? "Tiểu Thành (4/7)" : "Trộn Bộ");
        const char *domSet = (sum.dominantSetId > VH_SET_NONE) ? VoHoc_GetSetName((VoHocSetId)sum.dominantSetId) : "Tự Do";
        WuxiaUI_DrawText(TextFormat("%s · %s  |  Q=%.2f  ·  Công:%d  Thủ:%d  Khống:%d  Trợ:%d",
                                    domSet, tierTxt, sum.avgQ, sum.radar[0], sum.radar[1], sum.radar[2], sum.radar[3]),
                         rightX, p.y + 104.0f, 12.0f, WUXIA_JADE);

        // Set filter pills: Tất cả | Thủy | Thổ | Kim | Hỏa | Giáng Long | Cơ Bản
        static const int kFilters[7] = {
            -1,
            VH_SET_THUY_TRIEU_QUYEN_PHAP,
            VH_SET_BAN_THACH_BAT_CUC_QUYEN,
            VH_SET_THIEN_CANG_DOAN_KIM_KIEM,
            VH_SET_LIET_HOA_PHAN_THIEN_CUOC,
            VH_SET_GIANG_LONG_THAP_BAT_CHUONG,
            VH_SET_NONE
        };
        float filterY = p.y + 126.0f;
        float pillGap = 5.0f;
        float pillW = (rightW - pillGap * 6.0f) / 7.0f;
        for (int f = 0; f < 7; f++) {
            Rectangle fr = { rightX + f * (pillW + pillGap), filterY, pillW, 30.0f };
            bool active = (s_setFilter == kFilters[f]);
            if (WuxiaUI_DrawButton(fr, VoHocFilterShortLabel(kFilters[f]),
                                   VoHocFilterColor(kFilters[f]), active, 12.0f)) {
                s_setFilter = kFilters[f];
                s_skillScrollY = 0.0f;
            }
        }

        // Optional 1-click "Trang bị đủ 7 chiêu" banner when a specific Võ Học set is filtered
        float listHeaderY = filterY + 38.0f;
        if (s_setFilter > VH_SET_NONE && s_setFilter < VH_SET_COUNT) {
            Rectangle eqAllBtn = { rightX, listHeaderY, rightW, 32.0f };
            const char *setFullName = VoHoc_GetSetName((VoHocSetId)s_setFilter);
            if (WuxiaUI_DrawButton(eqAllBtn,
                                   TextFormat("Trang bị trọn bộ 7 chiêu: %s", setFullName),
                                   VoHocFilterColor(s_setFilter), false, 12.5f)) {
                EquipFullVoHocSet((VoHocSetId)s_setFilter);
            }
            listHeaderY += 38.0f;
        }

        // Collect all compatible skills sorted by Võ Học set order (1..VH_SET_COUNT-1, then 0/legacy)
        int filtered[MAX_PROFILE_SKILLS];
        int filteredCount = 0;
        int total = GetRegisteredSkillCount();
        if (total > MAX_PROFILE_SKILLS) total = MAX_PROFILE_SKILLS;

        for (int pass = 1; pass <= VH_SET_COUNT; pass++) {
            int targetSet = (pass < VH_SET_COUNT) ? pass : VH_SET_NONE;
            if (s_setFilter >= 0 && s_setFilter != targetSet) continue;
            for (int i = 0; i < total; i++) {
                if (!IsSkillSelectable(i)) continue;
                const char *name = GetRegisteredSkillName(i);
                if (!VoHoc_IsSkillRoleCompatibleWithSlot(name, s_selectedSlot)) continue;
                const VoHocSkillDef *def = VoHoc_FindByName(name);
                int skillSet = def ? (int)def->setId : VH_SET_NONE;
                if (skillSet != targetSet) continue;
                if (filteredCount < MAX_PROFILE_SKILLS) {
                    filtered[filteredCount++] = i;
                }
            }
        }

        bool needBuff = (s_selectedSlot >= PROFILE_STRIKE_SLOTS);
        WuxiaUI_DrawSectionHeader(rightX, listHeaderY, rightW,
                                  TextFormat("%s (%d chiêu)",
                                             needBuff ? "CHIÊU TRỢ / BUFF (Ô 6–7)" : "CHIÊU ĐẢ (Ô 1–5)",
                                             filteredCount));

        // Scrollable 2-column grid viewport
        float gridTop = listHeaderY + 24.0f;
        float gridH   = (p.y + p.height - 18.0f) - gridTop;
        Rectangle viewRect = { rightX, gridTop, rightW, gridH };
        DrawRectangleRec(viewRect, (Color){ 10, 13, 20, 175 });
        DrawRectangleLinesEx(viewRect, 1.0f, WUXIA_BORDER_SUBTLE);

        float cardH   = 50.0f;
        float rowGap  = 8.0f;
        float pad     = 8.0f;
        float sbW     = 6.0f;
        float colW    = (rightW - pad * 2.0f - 10.0f - sbW - 4.0f) * 0.5f;
        int totalRows = (filteredCount + 1) / 2;
        float contentH = (totalRows > 0) ? (pad * 2.0f + totalRows * cardH + (totalRows - 1) * rowGap) : 0.0f;
        float maxScroll = fmaxf(0.0f, contentH - gridH);

        if (CheckCollisionPointRec(ctx->pointerPos, viewRect)) {
            float wheel = GetMouseWheelMove();
            if (fabsf(wheel) > 0.01f) {
                s_skillScrollY -= wheel * 42.0f;
            }
        }
        if (s_skillScrollY < 0.0f) s_skillScrollY = 0.0f;
        if (s_skillScrollY > maxScroll) s_skillScrollY = maxScroll;

        BeginScissorMode((int)viewRect.x + 1, (int)viewRect.y + 1,
                         (int)viewRect.width - 2, (int)viewRect.height - 2);
        for (int k = 0; k < filteredCount; k++) {
            int i = filtered[k];
            int col = k % 2;
            int row = k / 2;
            Rectangle kr = {
                viewRect.x + pad + col * (colW + 10.0f),
                viewRect.y + pad + row * (cardH + rowGap) - s_skillScrollY,
                colW,
                cardH
            };
            if (kr.y + kr.height < viewRect.y || kr.y > viewRect.y + viewRect.height) continue;

            const char *name = GetRegisteredSkillName(i);
            const VoHocSkillDef *def = VoHoc_FindByName(name);
            bool unl = prof->skillUnlocked[i];
            Color elemCol = unl ? GetRegisteredSkillColor(i) : WUXIA_TEXT_MUTED;
            const char *disp = SkillDisplayNameVN(i);

            int equippedInSlot = -1;
            for (int s = 0; s < PROFILE_SKILL_SLOTS; s++) {
                if (eqIds[s] == i) { equippedInSlot = s; break; }
            }

            bool hov = CheckCollisionPointRec(ctx->pointerPos, viewRect) &&
                       CheckCollisionPointRec(ctx->pointerPos, kr);
            DrawRectangleRec(kr, (equippedInSlot == s_selectedSlot) ? (Color){ 44, 40, 24, 245 }
                               : (hov ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD));
            DrawRectangleLinesEx(kr, (equippedInSlot >= 0) ? 1.5f : 1.0f,
                                 (equippedInSlot == s_selectedSlot) ? WUXIA_GOLD_BRIGHT
                               : ((equippedInSlot >= 0) ? ColorAlpha(elemCol, 0.85f)
                                                        : (hov ? WUXIA_GOLD_DIM : WUXIA_BORDER_SUBTLE)));
            DrawRectangle((int)kr.x, (int)kr.y, 4, (int)kr.height, elemCol);

            WuxiaUI_DrawText(disp, kr.x + 11.0f, kr.y + 7.0f, 13.5f, unl ? elemCol : WUXIA_TEXT_MUTED);
            const char *subTxt = def
                ? TextFormat("%s · %.0f MP · %.1fs CD", VoHocFilterShortLabel((int)def->setId), def->mana, def->cooldown)
                : TextFormat("Cơ Bản · Lv.%d", prof->skillLevel[i]);
            WuxiaUI_DrawText(subTxt, kr.x + 11.0f, kr.y + 27.0f, 10.5f, WUXIA_TEXT_MUTED);

            if (equippedInSlot >= 0) {
                WuxiaUI_DrawTextRight(TextFormat("Ô %d", equippedInSlot + 1),
                                      kr.x + kr.width - 8.0f, kr.y + 8.0f, 10.5f, WUXIA_GOLD_BRIGHT);
            } else if (!unl) {
                WuxiaUI_DrawTextRight("Khóa", kr.x + kr.width - 8.0f, kr.y + 8.0f, 10.5f, WUXIA_CRIMSON);
            }

            if (hov && ctx->pointerReleased && unl) {
                PlayerProfile_EquipSkillSlot(s_selectedSlot, i);
                PlayerProfile_SaveGlobal();
            }
        }
        EndScissorMode();

        // Slim vertical scrollbar when content exceeds viewport
        if (maxScroll > 1.0f) {
            float trackH = gridH - 12.0f;
            float thumbH = fmaxf(28.0f, trackH * (gridH / contentH));
            float thumbY = viewRect.y + 6.0f + (s_skillScrollY / maxScroll) * (trackH - thumbH);
            Rectangle track = { viewRect.x + viewRect.width - sbW - 4.0f, viewRect.y + 6.0f, sbW, trackH };
            Rectangle thumb = { track.x, thumbY, sbW, thumbH };
            DrawRectangleRec(track, (Color){ 26, 30, 42, 180 });
            DrawRectangleRec(thumb, WUXIA_GOLD_DIM);
        }
    } else {
        // Tab 2: Dã Tẩu Shop & Alchemy
        WuxiaUI_DrawSectionHeader(p.x + 20.0f, p.y + 104.0f, p.width - 40.0f, "MỞ KHÓA VÕ HỌC & ĐỔI VẬT PHẨM DÃ TẨU");
        WuxiaUI_DrawText(TextFormat("Linh Thạch: Thủy [%d]  ·  Mộc [%d]  ·  Hỏa [%d]  ·  Thổ [%d]  ·  Kim [%d]   |   Tàn Quyển Thủy: %d",
                                    prof->elementalStones[0], prof->elementalStones[1], prof->elementalStones[2],
                                    prof->elementalStones[3], prof->elementalStones[4],
                                    prof->setScrolls[VH_SET_THUY_TRIEU_QUYEN_PHAP]),
                         p.x + 20.0f, p.y + 126.0f, 12.5f, WUXIA_AZURE);

        int vis = 0;
        int total = GetRegisteredSkillCount();
        float cardW = (p.width - 52.0f) * 0.5f;
        for (int i = 0; i < total && i < MAX_PROFILE_SKILLS; i++) {
            if (!IsSkillSelectable(i) || prof->skillUnlocked[i]) continue;
            int col = vis % 2;
            int row = vis / 2;
            if (row >= 3) break;
            Rectangle kr = { p.x + 20.0f + col * (cardW + 12.0f), p.y + 150.0f + row * 54.0f, cardW, 46.0f };
            int costGold = 300, elem = 0, costStones = 6;
            VoHocSetId setId = VH_SET_NONE;
            const char *skName = GetRegisteredSkillName(i);
            VoHoc_GetSkillUnlockCost(skName, prof->skillUnlocked, total, &costGold, &elem, &costStones, &setId);
            bool hasScroll = (setId > VH_SET_NONE && setId < PROFILE_MAX_SETS && prof->setScrolls[setId] > 0);
            const char *disp = SkillDisplayNameVN(i);
            const char *lbl = hasScroll
                ? TextFormat("Học: %s (1 Tàn Quyển)", disp)
                : TextFormat("Học: %s (%d Vàng + %d Thạch)", disp, costGold, costStones);
            if (WuxiaUI_DrawButton(kr, lbl, WUXIA_GOLD, false, 12.5f)) {
                if (hasScroll) {
                    prof->setScrolls[setId]--;
                    PlayerProfile_UnlockSkill(i);
                    PlayerProfile_SaveGlobal();
                } else if (prof->gold >= costGold && prof->elementalStones[elem] >= costStones) {
                    prof->gold -= costGold;
                    prof->elementalStones[elem] -= costStones;
                    PlayerProfile_UnlockSkill(i);
                    PlayerProfile_SaveGlobal();
                }
            }
            vis++;
        }
        if (vis == 0) {
            WuxiaUI_DrawText("Đã mở khóa toàn bộ Võ Học hiện có!", p.x + 24.0f, p.y + 165.0f, 15.5f, WUXIA_JADE);
        }

        // Alchemy / Potion Counter
        WuxiaUI_DrawSectionHeader(p.x + 20.0f, p.y + 320.0f, p.width - 40.0f,
                                  TextFormat("LUYỆN ĐAN DƯỢC (Linh Thảo: %d  ·  Hoàng Thổ Khoáng: %d)",
                                             prof->herbs, prof->elementalStones[3]));
        struct { const char *lbl; PotionType type; int gold; int herbs; int stone; Color col; } pots[4] = {
            { "Hồi Sinh Đan (40V/2Thảo)", POTION_HEAL_HP, 40, 2, 0, WUXIA_CRIMSON },
            { "Tụ Khí Dịch (40V/2Thảo)",  POTION_RESTORE_MANA, 40, 2, 0, WUXIA_AZURE },
            { "Hoàng Thổ Đan (100V+1K)",  POTION_EARTH_MIGHT, 100, 0, 1, WUXIA_GOLD },
            { "Ngộ Đạo Trà x2 (120V+1K)", POTION_SKILL_XP_BOOST, 120, 0, 1, WUXIA_JADE },
        };
        float potW = (p.width - 70.0f) * 0.25f;
        for (int k = 0; k < 4; k++) {
            Rectangle pr = { p.x + 20.0f + k * (potW + 10.0f), p.y + 344.0f, potW, 48.0f };
            const char *btnLbl = TextFormat("%s [%d]", pots[k].lbl, prof->potions[pots[k].type]);
            if (WuxiaUI_DrawButton(pr, btnLbl, pots[k].col, false, 11.5f)) {
                if (pots[k].herbs > 0 && prof->herbs >= pots[k].herbs) {
                    prof->herbs -= pots[k].herbs;
                    PlayerProfile_AddPotion(pots[k].type, 1);
                    PlayerProfile_SaveGlobal();
                } else if (prof->gold >= pots[k].gold && prof->elementalStones[3] >= pots[k].stone) {
                    prof->gold -= pots[k].gold;
                    prof->elementalStones[3] -= pots[k].stone;
                    PlayerProfile_AddPotion(pots[k].type, 1);
                    PlayerProfile_SaveGlobal();
                }
            }
        }

        int totalStones = 0;
        for (int e = 0; e < 5; e++) totalStones += prof->elementalStones[e];
        if (WuxiaUI_DrawButton((Rectangle){ p.x + 20.0f, p.y + 408.0f, p.width - 40.0f, 44.0f },
                               TextFormat("Luyện 2 Linh Thạch → +1 Lệnh Bài & +80 Điểm Dã Tẩu (Đang có: %d)", totalStones),
                               WUXIA_AZURE, false, 13.0f)) {
            if (totalStones >= 2) {
                int need = 2;
                for (int e = 0; e < 5 && need > 0; e++) {
                    while (prof->elementalStones[e] > 0 && need > 0) {
                        prof->elementalStones[e]--;
                        need--;
                    }
                }
                prof->daTauTokens += 1;
                prof->daTauPoints += 80;
                PlayerProfile_SaveGlobal();
            }
        }

        PlayerWardrobe *pwrd = Wardrobe_GetGlobalPlayer();
        int lockedIdx = -1;
        for (int w = 0; w < Wardrobe_GetCatalogCount(); w++) {
            const WardrobeCatalogItem *it = Wardrobe_GetCatalogItemByIndex(w);
            if (it && !PlayerWardrobe_HasItem(pwrd, it->id)) { lockedIdx = w; break; }
        }
        if (lockedIdx >= 0) {
            const WardrobeCatalogItem *it = Wardrobe_GetCatalogItemByIndex(lockedIdx);
            Rectangle wr = { p.x + 20.0f, p.y + 464.0f, p.width - 40.0f, 50.0f };
            const char *wlbl = TextFormat("Đổi Trang Bị Mới: %s (150 Vàng + 30 Điểm)", it->name);
            if (WuxiaUI_DrawButton(wr, wlbl, WUXIA_GOLD, false, 14.0f)) {
                if (prof->gold >= 150 && prof->daTauPoints >= 30) {
                    prof->gold -= 150;
                    prof->daTauPoints -= 30;
                    PlayerWardrobe_UnlockItem(pwrd, it->id);
                    Wardrobe_SaveGlobal();
                    PlayerProfile_SaveGlobal();
                }
            }
        }
    }
}

UIHubAction UI_Hub_UpdateDrawMain(int *outParam, char *joinCodeBuf, int joinCodeMax, const char *statusMsg) {
    if (outParam) *outParam = 0;
    WuxiaUI_BeginFrame(UI_IsTouchControlsEnabled());
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    float sw = ctx->screenW;
    float sh = ctx->screenH;
    PlayerProfile *prof = PlayerProfile_GetGlobal();

    if (UI_Hub_IsModalOpen()) {
        ClearBackground((Color){ 10, 12, 18, 255 });
        UI_Hub_UpdateDrawModal();
        return UI_HUB_ACT_NONE;
    }

    ClearBackground((Color){ 9, 11, 16, 255 });

    // Subtle atmospheric ink-wash backdrop gradient
    DrawRectangleGradientV(0, 0, (int)sw, (int)sh, (Color){ 16, 20, 30, 255 }, (Color){ 8, 10, 15, 255 });

    // --- LEFT STAGE: Title & 5 Ngũ Hành Realms (Wandering Paths) ---
    float drawerW = fminf(470.0f, sw * 0.44f);
    float leftW   = sw - drawerW - 56.0f;
    float leftX   = 28.0f;

    WuxiaUI_DrawDiamond((Vector2){ leftX + 12.0f, 34.0f }, 8.0f, WUXIA_GOLD, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawTextTitle("NGŨ HÀNH TỶ VÕ", leftX + 30.0f, 16.0f, 32.0f, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawText("Giang Hồ Kiếm Hiệp · Ngũ Hành Tương Sinh Tương Khắc",
                     leftX + 32.0f, 52.0f, 13.5f, WUXIA_TEXT_MUTED);
    WuxiaUI_DrawSpecularRule(leftX, 76.0f, leftW, WUXIA_GOLD, true);

    WuxiaUI_DrawSectionHeader(leftX, 92.0f, leftW, "CHẾ ĐỘ 1 · KHÁM PHÁ THẾ GIỚI (PvE)");
    float realmY = 120.0f;
    float realmAvailH = sh - realmY - 78.0f;
    float realmH = fminf(82.0f, (realmAvailH - 4.0f * 12.0f) / 5.0f);
    for (int r = 0; r < 5; r++) {
        Rectangle btn = { leftX, realmY + r * (realmH + 12.0f), leftW, realmH };
        if (WuxiaUI_DrawFeatureTile(btn, WUXIA_ICON_COMPASS, kRealmNames[r], kRealmSubtitles[r],
                                    NULL, kRealmColors[r], r == 3, false)) {
            if (outParam) *outParam = r;
            return UI_HUB_ACT_EXPLORE_REALM;
        }
    }

    // Bottom-left Dev Tools
    if (WuxiaUI_DrawIconPill((Rectangle){ leftX, sh - 52.0f, 180.0f, 34.0f },
                             WUXIA_ICON_GEAR, "Skill Sandbox", NULL, WUXIA_TEXT_MUTED, false)) {
        return UI_HUB_ACT_SANDBOX;
    }
    if (WuxiaUI_DrawIconPill((Rectangle){ leftX + 192.0f, sh - 52.0f, 170.0f, 34.0f },
                             WUXIA_ICON_SPARK_EVENT, "VFX Tester", NULL, WUXIA_TEXT_MUTED, false)) {
        return UI_HUB_ACT_VFX_TESTER;
    }

    // --- RIGHT DRAWER ("Where Winds Meet" Character & Mode Drawer) ---
    Rectangle drawer = { sw - drawerW - 18.0f, 16.0f, drawerW, sh - 32.0f };
    WuxiaUI_DrawPanel(drawer, WUXIA_GOLD, true);

    float dx = drawer.x + 20.0f;
    float dw = drawer.width - 40.0f;
    float dy = drawer.y + 18.0f;

    // Profile Header Card (like Image 1 top block)
    Rectangle avatarBox = { dx, dy, 62.0f, 62.0f };
    DrawRectangleRec(avatarBox, (Color){ 24, 28, 40, 240 });
    DrawRectangleLinesEx(avatarBox, 1.5f, WUXIA_GOLD);
    const PlayerWardrobe *pwrd = Wardrobe_GetGlobalPlayer();
    bool hasSword = pwrd && PlayerWardrobe_GetCombatStance(pwrd) == COMBAT_STANCE_SWORD;
    WuxiaUI_DrawIcon(hasSword ? WUXIA_ICON_SWORD : WUXIA_ICON_FIST,
                     (Vector2){ avatarBox.x + 31.0f, avatarBox.y + 31.0f }, 19.0f, WUXIA_GOLD_BRIGHT);

    WuxiaUI_DrawTextTitle("Hiệp Khách Giang Hồ", dx + 76.0f, dy + 2.0f, 20.0f, WUXIA_TEXT_PRIMARY);
    WuxiaUI_DrawText(hasSword ? "Thế Võ: Kiếm Pháp" : "Thế Võ: Quyền Cước",
                     dx + 76.0f, dy + 26.0f, 12.5f, WUXIA_JADE);
    WuxiaUI_DrawText(TextFormat("Vàng: %d   ·   Điểm: %d   ·   Chuỗi: %d",
                                prof->gold, prof->daTauPoints, prof->daTauStreak),
                     dx + 76.0f, dy + 44.0f, 12.5f, WUXIA_GOLD_BRIGHT);

    dy += 74.0f;
    WuxiaUI_DrawSpecularRule(dx, dy, dw, WUXIA_GOLD, true);
    dy += 14.0f;

    // Section: Nhân Vật & Võ Học
    WuxiaUI_DrawSectionHeader(dx, dy, dw, "Nhân Vật & Chuẩn Bị");
    dy += 24.0f;
    float tileW3 = (dw - 16.0f) / 3.0f;
    if (WuxiaUI_DrawFeatureTile((Rectangle){ dx, dy, tileW3, 74.0f },
                                WUXIA_ICON_SCROLL, "Võ Học", NULL, "TAB", WUXIA_GOLD, true, false)) {
        UI_Hub_OpenModal(0);
        return UI_HUB_ACT_NONE;
    }
    if (WuxiaUI_DrawFeatureTile((Rectangle){ dx + tileW3 + 8.0f, dy, tileW3, 74.0f },
                                WUXIA_ICON_BAG, "Tủ Đồ 3D", NULL, "B", WUXIA_AZURE, false, false)) {
        UI_Wardrobe_Open(Wardrobe_GetGlobalPlayer(), Wardrobe_GetGlobalModularInstance());
        return UI_HUB_ACT_NONE;
    }
    if (WuxiaUI_DrawFeatureTile((Rectangle){ dx + (tileW3 + 8.0f) * 2.0f, dy, tileW3, 74.0f },
                                WUXIA_ICON_SHOP, "Tiệm Dã Tẩu", NULL, "H", WUXIA_JADE, false, false)) {
        UI_Hub_OpenModal(1);
        return UI_HUB_ACT_NONE;
    }
    dy += 90.0f;

    // Section: Đấu Trường Công Bằng (PvP)
    WuxiaUI_DrawSectionHeader(dx, dy, dw, "Đấu Trường Hộ Pháp (PvP Công Bằng)");
    dy += 24.0f;
    float tileW2 = (dw - 10.0f) * 0.5f;
    if (WuxiaUI_DrawFeatureTile((Rectangle){ dx, dy, tileW2, 82.0f },
                                WUXIA_ICON_CROSSED_SWORDS, "Đấu 1 vs 1", "Song Linh Hộ Pháp",
                                NULL, WUXIA_GOLD, true, false)) {
        return UI_HUB_ACT_PVP_1V1;
    }
    if (WuxiaUI_DrawFeatureTile((Rectangle){ dx + tileW2 + 10.0f, dy, tileW2, 82.0f },
                                WUXIA_ICON_CROSSED_SWORDS, "Đấu 2 vs 2", "Tổ Đội Tranh Trụ",
                                NULL, WUXIA_CRIMSON, false, false)) {
        return UI_HUB_ACT_PVP_2V2;
    }
    dy += 98.0f;

    // Section: Chơi Online (EOS P2P)
    WuxiaUI_DrawSectionHeader(dx, dy, dw, "Đồng Đạo Giang Hồ (Online EOS P2P)");
    dy += 24.0f;
    bool onlineUp = Net_OnlineAvailable();
    if (WuxiaUI_DrawFeatureTile((Rectangle){ dx, dy, tileW2, 68.0f },
                                WUXIA_ICON_SPARK_EVENT, "Tạo Phòng", "Mời bạn qua mã",
                                NULL, WUXIA_AZURE, false, !onlineUp)) {
        return UI_HUB_ACT_HOST_ONLINE;
    }
    if (WuxiaUI_DrawFeatureTile((Rectangle){ dx + tileW2 + 10.0f, dy, tileW2, 68.0f },
                                WUXIA_ICON_CHAT, "Vào Phòng", "Nhập mã phòng",
                                NULL, WUXIA_JADE, false, !onlineUp)) {
        s_joinBoxOpen = !s_joinBoxOpen;
    }
    dy += 80.0f;

    if (s_joinBoxOpen && joinCodeBuf != NULL) {
        int len = (int)strlen(joinCodeBuf);
        int ch;
        while ((ch = GetCharPressed()) != 0) {
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            if (((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) && len < joinCodeMax - 1) {
                joinCodeBuf[len++] = (char)ch;
                joinCodeBuf[len] = '\0';
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) && len > 0) joinCodeBuf[--len] = '\0';
        Rectangle jb = { dx, dy, dw - 86.0f, 42.0f };
        DrawRectangleRec(jb, (Color){ 16, 18, 28, 255 });
        DrawRectangleLinesEx(jb, 1.5f, WUXIA_GOLD);
        WuxiaUI_DrawText(TextFormat("Mã phòng: %s_", joinCodeBuf), jb.x + 12.0f, jb.y + 12.0f, 15.0f, WUXIA_GOLD_BRIGHT);
        if (WuxiaUI_DrawButton((Rectangle){ dx + dw - 78.0f, dy, 78.0f, 42.0f }, "VÀO", WUXIA_JADE, len >= 3, 14.0f)) {
            if (len >= 3) return UI_HUB_ACT_JOIN_ONLINE;
        }
        if (IsKeyPressed(KEY_ENTER) && len >= 3) return UI_HUB_ACT_JOIN_ONLINE;
        dy += 52.0f;
    }

    if (statusMsg && statusMsg[0] != '\0') {
        WuxiaUI_DrawTextWrapped(statusMsg, (Rectangle){ dx, dy, dw, 48.0f }, 13.0f, WUXIA_CRIMSON, 4.0f);
    }

    // Bottom of right-hand drawer: Exit Game button
    Rectangle exitBtn = { dx, drawer.y + drawer.height - 52.0f, dw, 36.0f };
    if (WuxiaUI_DrawIconPill(exitBtn, WUXIA_ICON_CLOSE, "Thoát Game",
                             ctx->isTouchMode ? NULL : "ESC", WUXIA_CRIMSON, false)) {
        return UI_HUB_ACT_EXIT;
    }

    return UI_HUB_ACT_NONE;
}
