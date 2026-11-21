// ui/ui_lobby.c — Sảnh chờ Đấu Trường (Đợt A2) — Unified "Where Winds Meet" Style.
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "net/net_transport.h"
#include <math.h>
#include <string.h>

#define LOBBY_SLOTS_PER_TEAM (NET_MAX_PLAYERS / 2)

static const char *kTeamName[2]  = { "THANH LONG", "BẠCH HỔ" };
static const Color kTeamColor[2] = { { 84, 168, 238, 255 }, { 226, 192, 118, 255 } };

static void DrawTeamColumn(int team, float x, float y, float w, bool isHost,
                           const NetRosterEntry *roster, int count,
                           Vector2 mouse, bool clicked) {
    WuxiaUI_DrawSectionHeader(x, y - 28.0f, w, kTeamName[team]);

    int shown = 0;
    const float slotH = 56.0f, gap = 10.0f;
    for (int i = 0; i < count && shown < LOBBY_SLOTS_PER_TEAM; i++) {
        if (roster[i].team != team) continue;
        Rectangle r = { x, y + shown * (slotH + gap), w, slotH };
        bool hover = CheckCollisionPointRec(mouse, r);
        bool isBot = (roster[i].flags & NET_ROSTER_BOT) != 0;

        DrawRectangleRec(r, hover && isHost ? WUXIA_BG_CARD_HOV : WUXIA_BG_CARD);
        DrawRectangleLinesEx(r, hover && isHost ? 1.6f : 1.0f,
                             hover && isHost ? WUXIA_GOLD_BRIGHT : ColorAlpha(kTeamColor[team], 0.65f));
        DrawRectangle((int)r.x, (int)r.y, 4, (int)r.height, kTeamColor[team]);

        const char *label =
            (roster[i].flags & NET_ROSTER_HOST) ? "Chủ Phòng" :
            isBot ? "Cao Thủ AI (Bot)" : TextFormat("Đồng Đạo %d", roster[i].slot);
        WuxiaUI_DrawText(label, r.x + 14.0f, r.y + 9.0f, 16.0f,
                         isBot ? WUXIA_TEXT_MUTED : WUXIA_TEXT_PRIMARY);
        WuxiaUI_DrawText(isBot ? "Tự động chiến đấu" : (roster[i].agentId == NET_ROSTER_NONE ? "Đang chờ..." : "Sẵn Sàng"),
                         r.x + 14.0f, r.y + 32.0f, 12.0f, WUXIA_JADE);

        if (isHost) {
            if (isBot) {
                Rectangle xr = { r.x + r.width - 30.0f, r.y + 14.0f, 22.0f, 22.0f };
                bool xHov = CheckCollisionPointRec(mouse, xr);
                WuxiaUI_DrawIcon(WUXIA_ICON_CLOSE, (Vector2){ xr.x + 11.0f, xr.y + 11.0f }, 6.5f,
                                 xHov ? WUXIA_CRIMSON : WUXIA_TEXT_MUTED);
                if (clicked && xHov) {
                    Net_HostRemoveBot(team);
                    shown++;
                    continue;
                }
            }
            if (clicked && hover) Net_HostToggleTeam(i);
        }
        shown++;
    }

    for (; shown < LOBBY_SLOTS_PER_TEAM; shown++) {
        Rectangle r = { x, y + shown * (slotH + gap), w, slotH };
        bool hover = CheckCollisionPointRec(mouse, r);
        DrawRectangleRec(r, (Color){ 14, 16, 22, 185 });
        DrawRectangleLinesEx(r, 1.0f, hover && isHost ? WUXIA_GOLD : WUXIA_BORDER_SUBTLE);
        WuxiaUI_DrawTextCentered(isHost ? (hover ? "+ Thêm Bot" : "Vị Trí Trống") : "Vị Trí Trống",
                                 r, 14.5f, hover && isHost ? WUXIA_GOLD_BRIGHT : WUXIA_TEXT_MUTED);
        if (isHost && clicked && hover) Net_HostAddBot(team);
    }
}

UILobbyAction UI_LobbyUpdateDraw(const char *joinCode, bool isHost) {
    WuxiaUI_BeginFrame(UI_IsTouchControlsEnabled());
    const WuxiaUIContext *ctx = WuxiaUI_GetContext();
    float sw = ctx->screenW;
    float sh = ctx->screenH;
    Vector2 mouse = ctx->pointerPos;
    bool clicked = ctx->pointerReleased;
    UILobbyAction action = UI_LOBBY_NONE;

    float pw = fminf(760.0f, sw - 40.0f);
    float ph = fminf(520.0f, sh - 40.0f);
    Rectangle panel = { sw * 0.5f - pw * 0.5f, sh * 0.5f - ph * 0.5f, pw, ph };
    WuxiaUI_DrawPanel(panel, WUXIA_GOLD, true);

    WuxiaUI_DrawTextCentered("SẢNH CHỜ ĐẤU TRƯỜNG",
                             (Rectangle){ panel.x, panel.y + 16.0f, panel.width, 36.0f },
                             26.0f, WUXIA_GOLD_BRIGHT);
    WuxiaUI_DrawSpecularRule(panel.x + 24.0f, panel.y + 58.0f, panel.width - 48.0f, WUXIA_GOLD, true);

    if (joinCode != NULL && joinCode[0] != '\0') {
        const char *code = TextFormat("MÃ PHÒNG: %s", joinCode);
        WuxiaUI_DrawRibbon((Rectangle){ panel.x + panel.width * 0.5f - 170.0f, panel.y + 68.0f, 340.0f, 32.0f }, WUXIA_GOLD);
        WuxiaUI_DrawTextCentered(code, (Rectangle){ panel.x + panel.width * 0.5f - 170.0f, panel.y + 68.0f, 340.0f, 32.0f },
                                 18.0f, WUXIA_GOLD_BRIGHT);
    }

    NetRosterEntry roster[NET_MAX_PLAYERS];
    int count = Net_GetRoster(roster, NET_MAX_PLAYERS);

    float colW = (panel.width - 96.0f) * 0.5f;
    float colTop = panel.y + 142.0f;
    DrawTeamColumn(0, panel.x + 28.0f, colTop, colW, isHost, roster, count, mouse, clicked);
    DrawTeamColumn(1, panel.x + panel.width - 28.0f - colW, colTop, colW, isHost, roster, count, mouse, clicked);

    int t0 = 0, t1 = 0;
    for (int i = 0; i < count; i++) (roster[i].team == 0) ? t0++ : t1++;
    float by = panel.y + panel.height - 76.0f;

    if (isHost) {
        bool canStart = (t0 >= 1 && t1 >= 1);
        Rectangle bs = { sw * 0.5f - 220.0f, by, 210.0f, 44.0f };
        if (WuxiaUI_DrawButton(bs, "BẮT ĐẦU", canStart ? WUXIA_JADE : WUXIA_TEXT_MUTED, canStart, 16.0f)) {
            if (canStart) action = UI_LOBBY_START;
        }
        if (!canStart) {
            WuxiaUI_DrawTextCentered("Cần ít nhất 1 thành viên mỗi phe",
                                     (Rectangle){ panel.x, by + 48.0f, panel.width, 20.0f },
                                     13.0f, WUXIA_GOLD);
        }
    } else {
        WuxiaUI_DrawText("Đang chờ Chủ Phòng bắt đầu...", sw * 0.5f - 220.0f, by + 12.0f, 15.0f, WUXIA_TEXT_MUTED);
        if (!Net_IsPeerConnected()) {
            WuxiaUI_DrawTextCentered("Đang kết nối...",
                                     (Rectangle){ panel.x, by + 48.0f, panel.width, 20.0f },
                                     13.5f, WUXIA_GOLD);
        }
    }

    Rectangle bl = { sw * 0.5f + 10.0f, by, 210.0f, 44.0f };
    if (WuxiaUI_DrawButton(bl, "RỜI PHÒNG", WUXIA_CRIMSON, false, 16.0f)) {
        action = UI_LOBBY_LEAVE;
    }

    return action;
}
