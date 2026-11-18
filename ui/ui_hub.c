// ui/ui_hub.c
#include "ui/ui_hub.h"
#include "ui/ui_theme.h"
#include "ui/ui_wardrobe.h"
#include "character/player_profile.h"
#include "character/wardrobe_system.h"
#include "skills/skill_manager.h"
#include "net/net_transport.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>

static bool s_modalOpen = false;
static int  s_modalTab = 0;      // 0 = Vo Hoc 5 O, 1 = Tiem Da Tau
static int  s_selectedSlot = 0;  // 0..4
static bool s_joinBoxOpen = false;

static const char *kRealmNames[5] = {
    "1. HAN THUY DAM (THUY)",
    "2. THANH MOC LAM (MOC)",
    "3. XICH HOA SON (HOA)",
    "4. HOANG THO DAI (THO)",
    "5. KIM TUYET COC (KIM)"
};

static const Color kRealmColors[5] = {
    { 45, 135, 200, 255 },
    { 50, 175, 100, 255 },
    { 210, 75, 55, 255 },
    { 195, 140, 55, 255 },
    { 155, 170, 185, 255 }
};

static bool IsSkillSelectable(int skillIdx) {
    const char *n = GetRegisteredSkillName(skillIdx);
    if (!n || n[0] == '\0') return false;
    if (strncmp(n, "TAIJI_", 6) == 0) return false;
    if (strcmp(n, "CORE_TEST") == 0) return false;
    return true;
}

static bool DrawWuxiaBtn(Rectangle r, const char *label, Color border, Color fill, Color textCol, int fontSize) {
    (void)fill;
    (void)textCol;
    return WuxiaUI_DrawButton(r, label, border, false, (float)fontSize);
}

void UI_Hub_Init(void) {
    s_modalOpen = false;
    s_modalTab = 0;
    s_selectedSlot = 0;
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

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, (Color){ 6, 8, 14, 215 });

    float pw = 860.0f, ph = 540.0f;
    Rectangle p = { sw * 0.5f - pw * 0.5f, sh * 0.5f - ph * 0.5f, pw, ph };
    DrawRectangleRec(p, (Color){ 16, 18, 26, 250 });
    DrawRectangleLinesEx(p, 2.0f, (Color){ 210, 178, 95, 230 });

    PlayerProfile *prof = PlayerProfile_GetGlobal();

    // Header + Currencies
    DrawText("HANH TRANG & TANG KINH CAC (GLOBAL)", (int)p.x + 20, (int)p.y + 16, 20, (Color){ 245, 215, 120, 255 });
    const char *cur = TextFormat("Vang: %d   |   Diem Da Tau: %d   |   Lenh Bai: %d",
                                 prof->gold, prof->daTauPoints, prof->daTauTokens);
    DrawText(cur, (int)(p.x + p.width) - MeasureText(cur, 14) - 65, (int)p.y + 20, 14, (Color){ 220, 230, 200, 255 });

    if (DrawWuxiaBtn((Rectangle){ p.x + p.width - 48, p.y + 12, 34, 30 }, "X",
                     (Color){ 180, 90, 90, 220 }, (Color){ 45, 20, 24, 240 }, WHITE, 16)) {
        s_modalOpen = false;
        return;
    }

    // Tabs
    if (DrawWuxiaBtn((Rectangle){ p.x + 20, p.y + 54, 200, 36 }, "1. VO HOC (5 O)",
                     (s_modalTab == 0) ? GOLD : GRAY,
                     (s_modalTab == 0) ? (Color){ 42, 38, 24, 255 } : (Color){ 24, 26, 36, 255 }, WHITE, 14)) {
        s_modalTab = 0;
    }
    if (DrawWuxiaBtn((Rectangle){ p.x + 230, p.y + 54, 220, 36 }, "2. TIEM DA TAU (DOI DO)",
                     (s_modalTab == 1) ? GOLD : GRAY,
                     (s_modalTab == 1) ? (Color){ 42, 38, 24, 255 } : (Color){ 24, 26, 36, 255 }, WHITE, 14)) {
        s_modalTab = 1;
    }
    if (DrawWuxiaBtn((Rectangle){ p.x + 460, p.y + 54, 200, 36 }, "3. TU DO 3D (MAC DO)",
                     (Color){ 120, 175, 220, 220 }, (Color){ 24, 34, 48, 255 }, (Color){ 190, 230, 255, 255 }, 14)) {
        s_modalOpen = false;
        UI_Wardrobe_Open(Wardrobe_GetGlobalPlayer(), Wardrobe_GetGlobalModularInstance());
        return;
    }

    if (s_modalTab == 0) {
        // Left: 5 Equipped Slots
        DrawText("5 CHIEU THUC CHIEN DAU:", (int)p.x + 22, (int)p.y + 106, 14, LIGHTGRAY);
        for (int s = 0; s < PROFILE_SKILL_SLOTS; s++) {
            Rectangle sr = { p.x + 20, p.y + 130 + s * 74, 260, 64 };
            bool sel = (s == s_selectedSlot);
            int idx = Skill_GetIndexByName(prof->equippedSkillNames[s]);
            const char *sname = (idx >= 0) ? GetRegisteredSkillName(idx) : "(Trong)";
            int lvl = (idx >= 0) ? prof->skillLevel[idx] : 1;
            Color sc = (idx >= 0) ? GetRegisteredSkillColor(idx) : GRAY;
            DrawRectangleRec(sr, sel ? (Color){ 45, 42, 30, 255 } : (Color){ 24, 26, 36, 255 });
            DrawRectangleLinesEx(sr, sel ? 2.5f : 1.0f, sel ? GOLD : (Color){ 90, 95, 115, 200 });
            DrawText(TextFormat("O [%d] (PvE Lv.%d | PvP Lv.1)", s + 1, lvl),
                     (int)sr.x + 10, (int)sr.y + 8, 12, (Color){ 200, 195, 165, 255 });
            DrawText(sname, (int)sr.x + 10, (int)sr.y + 26, 15, sc);
            if (idx >= 0 && lvl < 10) {
                uint32_t req = PlayerProfile_XpForNextLevel(lvl);
                float xpFrac = (req > 0) ? ((float)prof->skillXp[idx] / (float)req) : 0.0f;
                if (xpFrac > 1.0f) xpFrac = 1.0f;
                DrawRectangle((int)sr.x + 10, (int)sr.y + 48, 236, 7, (Color){ 12, 14, 20, 240 });
                DrawRectangle((int)sr.x + 10, (int)sr.y + 48, (int)(236.0f * xpFrac), 7, (Color){ 110, 220, 245, 240 });
            } else if (idx >= 0) {
                DrawText("HOA CANH (MAX)", (int)sr.x + 10, (int)sr.y + 46, 10, GOLD);
            }
            if (CheckCollisionPointRec(GetMousePosition(), sr) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                s_selectedSlot = s;
            }
        }

        // Right: Unlocked Skills
        DrawText("VO HOC DA HOC (BAM DE LAP VAO O DANG CHON):", (int)p.x + 305, (int)p.y + 106, 14, LIGHTGRAY);
        int vis = 0;
        int total = GetRegisteredSkillCount();
        for (int i = 0; i < total && i < MAX_PROFILE_SKILLS; i++) {
            if (!IsSkillSelectable(i)) continue;
            int col = vis % 2;
            int row = vis / 2;
            Rectangle kr = { p.x + 305 + col * 265, p.y + 130 + row * 56, 255, 48 };
            bool unl = prof->skillUnlocked[i];
            const char *name = GetRegisteredSkillName(i);
            Color c = unl ? GetRegisteredSkillColor(i) : DARKGRAY;
            if (DrawWuxiaBtn(kr, TextFormat("%s%s", name, unl ? TextFormat(" [Lv.%d]", prof->skillLevel[i]) : " [KHOA]"),
                             unl ? c : (Color){ 60, 60, 70, 200 },
                             unl ? (Color){ 24, 28, 38, 255 } : (Color){ 16, 16, 22, 220 },
                             unl ? WHITE : GRAY, 12)) {
                if (unl) {
                    PlayerProfile_EquipSkillSlot(s_selectedSlot, i);
                    PlayerProfile_SaveGlobal();
                }
            }
            vis++;
        }
    } else {
        // Tab 2: Dã Tẩu Shop — Unlock skills or buy Wardrobe gear + Elemental Stones
        DrawText("MO KHOA VO HOC MOI & DOI VAT PHAM DA TAU:", (int)p.x + 22, (int)p.y + 104, 14, (Color){ 245, 215, 120, 255 });
        DrawText(TextFormat("Linh Thach: Thuy[%d]  Moc[%d]  Hoa[%d]  Tho[%d]  Kim[%d]",
                            prof->elementalStones[0], prof->elementalStones[1], prof->elementalStones[2],
                            prof->elementalStones[3], prof->elementalStones[4]),
                 (int)p.x + 22, (int)p.y + 124, 12, (Color){ 165, 225, 245, 255 });
        int vis = 0;
        int total = GetRegisteredSkillCount();
        for (int i = 0; i < total && i < MAX_PROFILE_SKILLS; i++) {
            if (!IsSkillSelectable(i) || prof->skillUnlocked[i]) continue;
            int col = vis % 2;
            int row = vis / 2;
            if (row >= 3) break;
            Rectangle kr = { p.x + 20 + col * 405, p.y + 146 + row * 54, 390, 46 };
            int costGold = 200;
            int costPts  = 40;
            const char *lbl = TextFormat("Hoc: %s (%d Vang + %d Diem)", GetRegisteredSkillName(i), costGold, costPts);
            if (DrawWuxiaBtn(kr, lbl, (Color){ 210, 175, 95, 220 }, (Color){ 26, 30, 42, 255 }, WHITE, 12)) {
                if (prof->gold >= costGold && prof->daTauPoints >= costPts) {
                    prof->gold -= costGold;
                    prof->daTauPoints -= costPts;
                    PlayerProfile_UnlockSkill(i);
                    PlayerProfile_SaveGlobal();
                }
            }
            vis++;
        }
        if (vis == 0) {
            DrawText("Da mo khoa toan bo Vo Hoc hien co!", (int)p.x + 24, (int)p.y + 160, 16, (Color){ 150, 230, 150, 255 });
        }

        // Alchemy / Potion Counter (Linh Thảo & Hoàng Thổ Khoáng)
        DrawText(TextFormat("LUYEN DAN DUOC (Linh Thao: %d | Hoang Tho Khoang: %d):",
                            prof->herbs, prof->elementalStones[3]),
                 (int)p.x + 22, (int)p.y + 314, 12, (Color){ 140, 240, 175, 255 });
        struct { const char *lbl; PotionType type; int gold; int herbs; int stone; Color col; } pots[4] = {
            { "Hoi Sinh Dan (40V/2Thao)", POTION_HEAL_HP, 40, 2, 0, (Color){ 235, 110, 110, 255 } },
            { "Tu Khi Dich (40V/2Thao)",  POTION_RESTORE_MANA, 40, 2, 0, SKYBLUE },
            { "Hoang Tho Dan (100V+1K)",  POTION_EARTH_MIGHT, 100, 0, 1, ORANGE },
            { "Ngo Dao Tra x2 (120V+1K)", POTION_SKILL_XP_BOOST, 120, 0, 1, (Color){ 120, 245, 200, 255 } },
        };
        for (int k = 0; k < 4; k++) {
            Rectangle pr = { p.x + 20 + k * 204, p.y + 332, 196, 46 };
            const char *btnLbl = TextFormat("%s [%d]", pots[k].lbl, prof->potions[pots[k].type]);
            if (DrawWuxiaBtn(pr, btnLbl, pots[k].col, (Color){ 24, 28, 38, 255 }, WHITE, 10)) {
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

        // Convert 2 Elemental Stones -> +1 Lenh Bai & +80 Diem
        int totalStones = 0;
        for (int e = 0; e < 5; e++) totalStones += prof->elementalStones[e];
        if (DrawWuxiaBtn((Rectangle){ p.x + 20, p.y + 390, 400, 44 },
                         TextFormat("Luyen 2 Linh Thach -> +1 Lenh Bai & +80 Diem (Co: %d)", totalStones),
                         SKYBLUE, (Color){ 20, 32, 44, 255 }, WHITE, 12)) {
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

        // Wardrobe unlock button
        PlayerWardrobe *pw = Wardrobe_GetGlobalPlayer();
        int lockedIdx = -1;
        for (int w = 0; w < Wardrobe_GetCatalogCount(); w++) {
            const WardrobeCatalogItem *it = Wardrobe_GetCatalogItemByIndex(w);
            if (it && !PlayerWardrobe_HasItem(pw, it->id)) { lockedIdx = w; break; }
        }
        if (lockedIdx >= 0) {
            const WardrobeCatalogItem *it = Wardrobe_GetCatalogItemByIndex(lockedIdx);
            Rectangle wr = { p.x + 20, p.y + 445, 520, 54 };
            const char *wlbl = TextFormat("Doi Trang Bi: %s (150 Vang + 30 Diem)", it->name);
            if (DrawWuxiaBtn(wr, wlbl, GOLD, (Color){ 36, 28, 22, 255 }, (Color){ 255, 225, 135, 255 }, 14)) {
                if (prof->gold >= 150 && prof->daTauPoints >= 30) {
                    prof->gold -= 150;
                    prof->daTauPoints -= 30;
                    PlayerWardrobe_UnlockItem(pw, it->id);
                    Wardrobe_SaveGlobal();
                    PlayerProfile_SaveGlobal();
                }
            }
        }
    }
}

UIHubAction UI_Hub_UpdateDrawMain(int *outParam, char *joinCodeBuf, int joinCodeMax, const char *statusMsg) {
    if (outParam) *outParam = 0;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    PlayerProfile *prof = PlayerProfile_GetGlobal();

    // If a global modal (Wardrobe / Loadout / Shop) is open, draw it on top and eat input
    if (UI_Hub_IsModalOpen()) {
        ClearBackground((Color){ 10, 12, 18, 255 });
        UI_Hub_UpdateDrawModal();
        return UI_HUB_ACT_NONE;
    }

    ClearBackground((Color){ 10, 12, 18, 255 });

    // Top Header Bar (Where Winds Meet Ink-Wash & Gold)
    DrawRectangleGradientV(0, 0, sw, 74, (Color){ 20, 24, 34, 250 }, (Color){ 12, 15, 22, 240 });
    DrawLineEx((Vector2){ 0, 74 }, (Vector2){ (float)sw, 74 }, 1.5f, ColorAlpha(WUXIA_GOLD, 0.72f));
    WuxiaUI_DrawDiamond((Vector2){ 34.0f, 30.0f }, 8.0f, WUXIA_GOLD, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawText("NGU HANH TY VO", 52.0f, 14.0f, 28.0f, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawText(TextFormat("Vang: %d   ·   Diem Da Tau: %d   ·   Chuoi: %d",
                                prof->gold, prof->daTauPoints, prof->daTauStreak),
                     52.0f, 48.0f, 14.0f, WUXIA_JADE);

    // Global Prep Buttons (Top Right)
    if (DrawWuxiaBtn((Rectangle){ (float)sw - 580, 16, 175, 42 }, "TU DO 3D (MAC DO)",
                     WUXIA_AZURE, (Color){ 26, 36, 50, 255 }, WHITE, 13)) {
        UI_Wardrobe_Open(Wardrobe_GetGlobalPlayer(), Wardrobe_GetGlobalModularInstance());
        return UI_HUB_ACT_NONE;
    }
    if (DrawWuxiaBtn((Rectangle){ (float)sw - 395, 16, 185, 42 }, "VO HOC (CHON 5 SKILL)",
                     WUXIA_GOLD, (Color){ 42, 36, 22, 255 }, WUXIA_GOLD_BRIGHT, 13)) {
        UI_Hub_OpenModal(0);
        return UI_HUB_ACT_NONE;
    }
    if (DrawWuxiaBtn((Rectangle){ (float)sw - 200, 16, 175, 42 }, "TIEM DA TAU",
                     WUXIA_JADE, (Color){ 32, 28, 24, 255 }, WHITE, 13)) {
        UI_Hub_OpenModal(1);
        return UI_HUB_ACT_NONE;
    }

    // Left Column: Mode 1 — Explore World (5 Ngũ Hành Maps)
    Rectangle leftPanel = { 28, 94, (float)sw * 0.48f - 38, (float)sh - 160 };
    WuxiaUI_DrawPanel(leftPanel, WUXIA_JADE, true);
    WuxiaUI_DrawText("CHE DO 1: KHAM PHA THE GIOI (PvE)", leftPanel.x + 20.0f, leftPanel.y + 14.0f, 18.0f, WUXIA_JADE);
    WuxiaUI_DrawText("Danh quai, lam nhiem vu Da Tau, luyen cap Vo Hoc & kiem do",
                     leftPanel.x + 20.0f, leftPanel.y + 39.0f, 13.0f, WUXIA_TEXT_MUTED);

    for (int r = 0; r < 5; r++) {
        Rectangle btn = { leftPanel.x + 18, leftPanel.y + 68 + r * 72, leftPanel.width - 36, 58 };
        if (DrawWuxiaBtn(btn, kRealmNames[r], kRealmColors[r], (Color){ 24, 28, 40, 255 }, WHITE, 16)) {
            if (outParam) *outParam = r;
            return UI_HUB_ACT_EXPLORE_REALM;
        }
    }

    // Right Column: Mode 2 — PvP Dual-Boss Escort (1v1 & 2v2)
    Rectangle rightPanel = { (float)sw * 0.50f + 10, 94, (float)sw * 0.48f - 38, (float)sh - 160 };
    WuxiaUI_DrawPanel(rightPanel, WUXIA_CRIMSON, true);
    WuxiaUI_DrawText("CHE DO 2: DAU TRUONG HO PHAP (PvP CONG BANG)", rightPanel.x + 20.0f, rightPanel.y + 14.0f, 18.0f, (Color){ 255, 165, 130, 255 });
    WuxiaUI_DrawText("Tat toan bo chi so do & Lv skill! Ho tong Boss + Chiem Tru Long Mach",
                     rightPanel.x + 20.0f, rightPanel.y + 39.0f, 13.0f, WUXIA_TEXT_MUTED);

    if (DrawWuxiaBtn((Rectangle){ rightPanel.x + 18, rightPanel.y + 72, rightPanel.width - 36, 68 },
                     "DAU 1vs1 (SONG LINH HO PHAP)",
                     (Color){ 235, 150, 90, 240 }, (Color){ 42, 26, 24, 255 }, WHITE, 18)) {
        return UI_HUB_ACT_PVP_1V1;
    }
    if (DrawWuxiaBtn((Rectangle){ rightPanel.x + 18, rightPanel.y + 154, rightPanel.width - 36, 68 },
                     "DAU 2vs2 (SONG LINH HO PHAP)",
                     (Color){ 235, 95, 95, 240 }, (Color){ 46, 22, 26, 255 }, WHITE, 18)) {
        return UI_HUB_ACT_PVP_2V2;
    }

    // Online Room Controls (EOS)
    bool onlineUp = Net_OnlineAvailable();
    Color onCol = onlineUp ? WHITE : GRAY;
    if (DrawWuxiaBtn((Rectangle){ rightPanel.x + 18, rightPanel.y + 244, rightPanel.width - 36, 46 },
                     "TAO PHONG ONLINE (EOS P2P)",
                     onlineUp ? SKYBLUE : DARKGRAY, (Color){ 22, 28, 40, 255 }, onCol, 15)) {
        return UI_HUB_ACT_HOST_ONLINE;
    }
    if (DrawWuxiaBtn((Rectangle){ rightPanel.x + 18, rightPanel.y + 300, rightPanel.width - 36, 46 },
                     "NHAP MA VAO PHONG ONLINE",
                     onlineUp ? SKYBLUE : DARKGRAY, (Color){ 22, 28, 40, 255 }, onCol, 15)) {
        if (onlineUp) s_joinBoxOpen = !s_joinBoxOpen;
    }

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
        Rectangle jb = { rightPanel.x + 18, rightPanel.y + 356, rightPanel.width - 36, 48 };
        DrawRectangleRec(jb, (Color){ 16, 18, 28, 255 });
        DrawRectangleLinesEx(jb, 1.5f, GOLD);
        DrawText(TextFormat("MA PHONG: %s_  (ENTER DE VAO)", joinCodeBuf), (int)jb.x + 14, (int)jb.y + 14, 16, GOLD);
        if (IsKeyPressed(KEY_ENTER) && len >= 3) return UI_HUB_ACT_JOIN_ONLINE;
    }

    if (statusMsg && statusMsg[0] != '\0') {
        DrawText(statusMsg, (int)rightPanel.x + 18, (int)rightPanel.y + 415, 14, (Color){ 245, 155, 100, 255 });
    }

    // Bottom Bar: Dev tools
    if (DrawWuxiaBtn((Rectangle){ 28, (float)sh - 50, 175, 34 }, "DEV: SKILL SANDBOX",
                     GRAY, (Color){ 24, 24, 30, 220 }, LIGHTGRAY, 12)) {
        return UI_HUB_ACT_SANDBOX;
    }
    if (DrawWuxiaBtn((Rectangle){ 215, (float)sh - 50, 175, 34 }, "DEV: VFX TESTER",
                     GRAY, (Color){ 24, 24, 30, 220 }, LIGHTGRAY, 12)) {
        return UI_HUB_ACT_VFX_TESTER;
    }

    return UI_HUB_ACT_NONE;
}
