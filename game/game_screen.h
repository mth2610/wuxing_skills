// game/game_screen.h
// The real (production-bound) gameplay screen — as opposed to sandbox/,
// which stays the dev/test harness (debug panels, tuning sliders, autotest)
// and is never touched by this module. Currently a minimal walk-around
// experience plus real basic-attack combat (đấm/đá/chưởng, auto-target, wall
// synergy — see entities/entities.h's Entity_ExecuteBasicAttack). Still no
// real skill-casting/enemies/boss yet (blocked on MODULES_ROADMAP.md Module 3
// combat/ + Module 5 boss/, neither built) — this screen will grow into full
// Module 7 (Game Mode) as those land. Basic-attack input living here (rather
// than sandbox/) is a deliberate shortcut ahead of Module 4 (control/)
// formalizing it properly — expect this block to move into control/ wholesale
// once that module exists, per MODULES_ROADMAP.md's ordering rule.
//
// Reuses sandbox_core.h's PlayerEntity (position + Entity agentId) rather
// than inventing a parallel player struct — it's the only player
// representation that exists right now and already carries a valid
// Entity/Agent link (see main.c's global `player`, spawned once via
// InitSandbox at startup regardless of which screen is active). Expect this
// to be swapped for a real control/ PlayerIntent-driven struct once Module 4
// (Player Controller) exists.
#ifndef GAME_SCREEN_H
#define GAME_SCREEN_H

#include "raylib.h"
#include "sandbox/sandbox_core.h"
#include <stdbool.h>

// Called once at startup (alongside InitSandbox) — does not re-run per
// screen-switch, matching how InitSandbox itself is only called once.
void GameScreen_Init(PlayerEntity *player);

// WASD move + wheel/Q/E orbit camera. Call only while this screen is active.
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

#endif // GAME_SCREEN_H
