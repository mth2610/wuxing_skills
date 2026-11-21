# UI MODULE API SPECIFICATION

> Module: `ui/` (`ui.h` / `ui.c`) — ../../ROADMAP.md Module 9 (HUD +
> Auto-Targeting). Owner agent: **UI Agent** (see `ui/CLAUDE.md`).

## 1. Scope & Design

Minimal by philosophy (No Tutorial): skill slot chips + an auto-aim reticle.
Auto-target priority (design doc §XI, mobile-first):
1. **Nearest ENEMY projectile in flight** within 12 m — aiming at it lines
   up the đối-đòn cast that triggers Đấu Pháp. Read from combat/'s
   last-frame snapshot (`Combat_QueryProjectiles`).
2. **The enemy boss** within 30 m (`boss/` singleton).
3. Nothing — the caller keeps its own aim (mouse ray).

The result feeds `PlayerIntent.aimPoint` in `game/game_screen.c`; control/
never knows ui/ exists.

## 2. API

### 2a. HUD, Auto-Aim & Unified Skill Cluster (`ui.h`)
```c
void UI_Init(void);
void UI_SetCamera(const Camera3D *camera); // per frame (reticle projection)
void UI_Update(float dt);                  // recompute the cached aim + HP trail
Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget);
void UI_DrawOverlay(int agentId);          // 2D pass: PC bottom-right skill cluster + reticle + TAB loadout
void UI_DrawCompassMinimap(Vector3 playerPos, float camYaw,
                           Vector3 questTarget, bool hasQuestTarget, bool inSafeZone);
void UI_DrawPlayerVitals(int agentId, int healPotions, int manaPotions,
                         bool *outUseHeal, bool *outUseMana);
```

- Bottom-right skill cluster: 5 Strike nodes (`1-5`) on an arc around Basic Attack (`RMB`), 2 Buff nodes (`6-7`), Khinh Công (`Space`), Né (`Shift`), Thiền (`G`). Same spatial layout on PC (`0.84x` with keycap badges) and Mobile (`1.0x` touch nodes via `UI_PollTouchCombat` / `UI_DrawTouchCombat`).
- Top-left circular compass minimap + bottom-center player vitals (`Sinh Lực`, `Chân Khí`, `Thể Lực`, element crest, quick potions `R`/`F1`).
- Reticle: yellow diamond projected over the target via `GetWorldToScreen`.
- All reads, no `Entity_*` mutations.

### 2b. Unified "Where Winds Meet" Theme & Widget Toolkit (`ui_theme.h`)
- 3 pre-rasterized Vietnamese Unicode font atlases (`ui_font.ttf` at `20px`/`30px`, `wuxia_ui.ttf` at `44px`) loaded via `ResourceManager_LoadFont`.
- `WuxiaUI_BeginFrame(isTouchMode)` / `WuxiaUI_GetContext()` manages responsive `scale`, `safeRect` (respecting Android Mali top-84px gesture zone), and `pointerConsumed`.
- Reusable widgets: `WuxiaUI_DrawPanel`, `WuxiaUI_DrawButton`, `WuxiaUI_DrawFeatureTile`, `WuxiaUI_DrawIconPill`, `WuxiaUI_DrawSkillNode`, `WuxiaUI_DrawSleekBar`, `WuxiaUI_DrawSectionHeader`, `WuxiaUI_DrawSpecularRule`, `WuxiaUI_DrawKeycap`, `WuxiaUI_DrawIcon`.

### 2b. Lobby screen (`ui/ui_lobby.c` — Phase A2)

```c
typedef enum { UI_LOBBY_NONE = 0, UI_LOBBY_START, UI_LOBBY_LEAVE } UILobbyAction;
UILobbyAction UI_LobbyUpdateDraw(const char *joinCode, bool isHost);
```

Full-screen room UI between menu and match, rendered from `Net_GetRoster`
(this file is ui/'s one consumer of `net/net_transport.h`): two team
columns (THANH LONG / BACH HO, 4 slots each), join code banner. Host
clicks: human/bot entry → `Net_HostToggleTeam`, empty slot →
`Net_HostAddBot`, bot's X corner → `Net_HostRemoveBot`; BAT DAU (enabled
at ≥1 member per side) / ROI PHONG return the action for main.c to
execute — ui/ never starts/stops the session itself. Clients render
read-only + the connection state.

## 3. Explicitly NOT in this version

- Mobile virtual buttons / touch input surface (Phase 1 follow-up alongside
  control/'s touch ReadIntent).
- Projectile *lead* prediction (aims at current snapshot position).
- HP/mana/boss bars stay in `game/game_screen.c` (predate this module).

## Autotest

`ui_auto_target` in `main.c`: boss targeted when the field is clear;
projectile beats boss when one is in flight; no target when neither exists.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Doc migration: moved from root `ui/docs/API.md` to `ui/docs/API.md` per `DOC_ARCHITECTURE.md` (no content changes needed — already pure interface) | root `ui/docs/API.md` (read directly) | Ground-truth |
