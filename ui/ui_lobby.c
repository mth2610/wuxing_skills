// ui/ui_lobby.c — Sảnh chờ (Đợt A2). Room screen between menu and match:
// two team columns (THANH LONG / BACH HO) of NET_MAX_PLAYERS/2 slots each,
// rendered from the live roster (Net_GetRoster — the host builds it, clients
// receive broadcasts). Pure 2D immediate-mode; net/ owns all room state,
// this file only draws and translates clicks into Net_Host* calls.
#include "ui/ui.h"
#include "net/net_transport.h"
#include <string.h>

#define LOBBY_SLOTS_PER_TEAM (NET_MAX_PLAYERS / 2)

static const char *kTeamName[2] = { "THANH LONG", "BACH HO" };
static const Color kTeamColor[2] = { { 90, 170, 235, 255 }, { 235, 225, 205, 255 } };

// One team column: draws its slots, handles host clicks. Returns nothing —
// mutations go straight through Net_Host* (roster rebroadcast follows).
static void DrawTeamColumn(int team, float x, float y, float w, bool isHost,
                           const NetRosterEntry *roster, int count,
                           Vector2 mouse, bool clicked) {
    DrawText(kTeamName[team], (int)x, (int)(y - 30), 22, kTeamColor[team]);

    // Collect this team's entries in roster order (stable slots visually).
    int shown = 0;
    const float slotH = 52.0f, gap = 10.0f;
    for (int i = 0; i < count && shown < LOBBY_SLOTS_PER_TEAM; i++) {
        if (roster[i].team != team) continue;
        Rectangle r = { x, y + shown * (slotH + gap), w, slotH };
        bool hover = CheckCollisionPointRec(mouse, r);
        bool isBot = (roster[i].flags & NET_ROSTER_BOT) != 0;

        DrawRectangleRec(r, (Color){ 25, 25, 38, 230 });
        DrawRectangleLinesEx(r, 2, hover && isHost ? WHITE : kTeamColor[team]);
        const char *label =
            (roster[i].flags & NET_ROSTER_HOST) ? "CHU PHONG" :
            isBot ? "BOT" : TextFormat("NGUOI CHOI %d", roster[i].slot);
        DrawText(label, (int)r.x + 14, (int)r.y + 8, 20,
                 isBot ? (Color){ 170, 170, 185, 255 } : RAYWHITE);
        DrawText(isBot ? "AI" : (roster[i].agentId == NET_ROSTER_NONE ? "..." : "SAN SANG"),
                 (int)r.x + 14, (int)r.y + 32, 12, (Color){ 140, 200, 140, 255 });

        if (isHost) {
            // Bot slots grow an X corner (remove); the body flips the side.
            if (isBot) {
                Rectangle xr = { r.x + r.width - 26, r.y + 6, 20, 20 };
                DrawText("X", (int)xr.x + 5, (int)xr.y + 2, 16,
                         CheckCollisionPointRec(mouse, xr) ? RED : (Color){ 180, 120, 120, 255 });
                if (clicked && CheckCollisionPointRec(mouse, xr)) {
                    Net_HostRemoveBot(team);
                    shown++;
                    continue;
                }
            }
            if (clicked && hover) Net_HostToggleTeam(i);
        }
        shown++;
    }

    // Empty slots: host clicks one to add a bot on this side.
    for (; shown < LOBBY_SLOTS_PER_TEAM; shown++) {
        Rectangle r = { x, y + shown * (slotH + gap), w, slotH };
        bool hover = CheckCollisionPointRec(mouse, r);
        DrawRectangleLinesEx(r, 1, (Color){ 90, 90, 110, 160 });
        DrawText(isHost ? (hover ? "+ THEM BOT" : "TRONG") : "TRONG",
                 (int)r.x + 14, (int)r.y + 16, 16,
                 hover && isHost ? RAYWHITE : (Color){ 110, 110, 130, 200 });
        if (isHost && clicked && hover) Net_HostAddBot(team);
    }
}

UILobbyAction UI_LobbyUpdateDraw(const char *joinCode, bool isHost) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    Vector2 mouse = GetMousePosition();
    bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    UILobbyAction action = UI_LOBBY_NONE;

    const char *title = "SANH CHO";
    DrawText(title, sw / 2 - MeasureText(title, 34) / 2, 34, 34, RAYWHITE);
    if (joinCode != NULL && joinCode[0] != '\0') {
        const char *code = TextFormat("MA PHONG: %s", joinCode);
        int cw = MeasureText(code, 26);
        DrawRectangle(sw / 2 - cw / 2 - 12, 76, cw + 24, 40, (Color){ 15, 15, 25, 220 });
        DrawText(code, sw / 2 - cw / 2, 84, 26, (Color){ 240, 220, 120, 255 });
    }

    NetRosterEntry roster[NET_MAX_PLAYERS];
    int count = Net_GetRoster(roster, NET_MAX_PLAYERS);

    const float colW = 300.0f, colTop = 170.0f;
    DrawTeamColumn(0, sw / 2 - colW - 40.0f, colTop, colW, isHost, roster, count, mouse, clicked);
    DrawTeamColumn(1, sw / 2 + 40.0f, colTop, colW, isHost, roster, count, mouse, clicked);

    // Footer buttons.
    int t0 = 0, t1 = 0;
    for (int i = 0; i < count; i++) (roster[i].team == 0) ? t0++ : t1++;
    float by = colTop + LOBBY_SLOTS_PER_TEAM * 62.0f + 24.0f;

    if (isHost) {
        bool canStart = (t0 >= 1 && t1 >= 1);
        Rectangle bs = { sw / 2 - 230.0f, by, 220, 46 };
        bool hs = CheckCollisionPointRec(mouse, bs);
        DrawRectangleRounded(bs, 0.2f, 8, canStart ? (hs ? (Color){ 90, 160, 90, 255 } : (Color){ 60, 120, 60, 255 })
                                                   : (Color){ 50, 50, 60, 255 });
        DrawText("BAT DAU", (int)bs.x + 62, (int)bs.y + 12, 22,
                 canStart ? RAYWHITE : (Color){ 120, 120, 130, 255 });
        if (canStart && clicked && hs) action = UI_LOBBY_START;
        if (!canStart) {
            const char *hint = "CAN IT NHAT 1 THANH VIEN MOI PHE";
            DrawText(hint, sw / 2 - MeasureText(hint, 14) / 2, (int)by + 56, 14,
                     (Color){ 200, 160, 110, 255 });
        }
    } else {
        const char *wait = "CHO CHU PHONG BAT DAU...";
        DrawText(wait, sw / 2 - 230 + 10, (int)by + 12, 20, (Color){ 200, 200, 210, 255 });
        if (!Net_IsPeerConnected()) {
            const char *dc = "DANG KET NOI...";
            DrawText(dc, sw / 2 - MeasureText(dc, 16) / 2, (int)by + 56, 16,
                     (Color){ 235, 140, 90, 255 });
        }
    }

    Rectangle bl = { sw / 2 + 10.0f, by, 220, 46 };
    bool hl = CheckCollisionPointRec(mouse, bl);
    DrawRectangleRounded(bl, 0.2f, 8, hl ? (Color){ 150, 80, 80, 255 } : (Color){ 110, 60, 60, 255 });
    DrawText("ROI PHONG", (int)bl.x + 48, (int)bl.y + 12, 22, RAYWHITE);
    if (clicked && hl) action = UI_LOBBY_LEAVE;

    (void)sh;
    return action;
}
