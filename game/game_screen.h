// game/game_screen.h
// The real (production-bound) gameplay screen — as opposed to sandbox/,
// which stays the dev/test harness (debug panels, tuning sliders, autotest)
// and is never touched by this module. This IS MODULES_ROADMAP.md Module 7
// (Game Mode, see GAME_API.md): the Phase 0 match loop vs Boss Hắc Diện Tôn
// Giả on DEFAULT_ARENA — intro title card, FIGHTING with control/-driven
// movement/casting and the game_rules.h zone modifier table applied to the
// player, VICTORY/DEFEAT overlays. Basic attack (đấm/đá/chưởng, auto-target,
// wall synergy) deliberately stays here rather than control/ — it couples to
// character animation and VFX, which pure logic modules must not touch.
//
// Reuses sandbox_core.h's PlayerEntity (position + Entity agentId) — main.c's
// global `player`, spawned once via InitSandbox at startup. The match reset
// respawns its pool agent when a previous match killed it.
#ifndef GAME_SCREEN_H
#define GAME_SCREEN_H

#include "raylib.h"
#include "sandbox/sandbox_core.h"
#include <stdbool.h>

// Module 7 (Game Mode) state machine — the Phase 0 match loop against Boss
// Hắc Diện Tôn Giả on DEFAULT_ARENA. GAME_MENU is represented by main.c's
// SCREEN_MAIN_MENU (this screen is only updated while active); the states
// below are the in-match flow.
typedef enum {
    GAME_MENU = 0,       // owned by main.c's screen switcher
    GAME_ARENA_INTRO,    // boss title card; boss spawns at the end
    GAME_FIGHTING,       // control + combat + boss AI + zone rules live
    GAME_VICTORY,        // boss died — ENTER returns to menu
    GAME_DEFEAT          // player died (HP or ring-out) — ENTER returns to menu
} GameState;

GameState GameScreen_GetState(void);

// Called once at startup (alongside InitSandbox) — does not re-run per
// screen-switch, matching how InitSandbox itself is only called once.
// Resets the match (state → GAME_ARENA_INTRO, player at the arena spawn).
void GameScreen_Init(PlayerEntity *player);

// Match state machine tick + control intents + camera (wheel/Q/E orbit).
// Call only while this screen is active.
void GameScreen_Update(PlayerEntity *player, Camera3D *camera, float dt);

// Draws the player's own character mesh + fake shadow. Call inside the
// existing MyBeginMode3D/MyEndMode3D block, alongside where
// MapManager_DrawActive() already runs unconditionally every frame.
void GameScreen_Draw3D(const PlayerEntity *player);

// Real HUD — HP + mana bars (reads real Entity health/mana, no placeholder
// data — Agent.mana landed this pass, see entities/entities.h). Call after
// EndMode3D, in the 2D overlay pass.
void GameScreen_DrawHUD(const PlayerEntity *player);

// True exactly once the frame ESC is pressed while this screen is active;
// clears itself after being read. main.c should switch back to
// SCREEN_MAIN_MENU when this returns true.
bool GameScreen_RequestedBackToMenu(void);

// Online match: the EOS join code to show in the HUD while the host waits
// for an opponent ("MA PHONG: XXXXX"). NULL/empty clears it (main.c sets it
// after Net_StartHostOnline succeeds and clears it on back-to-menu).
void GameScreen_SetOnlineCode(const char *code);

#endif // GAME_SCREEN_H
