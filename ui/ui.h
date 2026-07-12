// ui/ui.h
// HUD + Auto-Targeting (MODULES_ROADMAP.md Module 9). Minimal by design
// (No Tutorial): skill slot chips with cooldown shading + an auto-aim
// reticle. Auto-target priority (thiết kế §XI):
//   1. nearest ENEMY projectile in flight (combat/'s snapshot) — the
//      đối-đòn angle that triggers Đấu Pháp;
//   2. the enemy boss;
//   3. nothing (caller falls back to mouse aim).
// The result feeds PlayerIntent.aimPoint — game/ consumes it, control/
// never knows ui/ exists. UI_API.md will document the contract.
#ifndef UI_H
#define UI_H

#include "raylib.h"
#include <stdbool.h>

void UI_Init(void);
// Forward the active camera every frame (screen projection for the reticle).
void UI_SetCamera(const Camera3D *camera);
void UI_Update(float dt);

// Auto-aim for agentId. Returns the point via outPoint and true when a
// target exists; false = caller keeps its own aim (mouse).
Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget);

// 2D pass, after the 3D scene: skill slots (keys 1-4) with cooldown
// shading + the auto-aim reticle. Call from the game screen's HUD.
void UI_DrawOverlay(int agentId);

// --- Loadout panel (Trang Bị — TAB in game/) ---
// Click a slot, click a skill: equips via Entity_SetEquippedSkill (element
// resolved from the registry), which recomputes Vô Hệ — and silently arms
// the Thái Cực loadout when the player assembles it (No Tutorial: the
// panel never labels Âm/Dương). The match keeps running while it's open —
// swapping mid-fight is a deliberate risk. game/ freezes the player's own
// intents while open.
void UI_ToggleLoadout(void);
bool UI_IsLoadoutOpen(void);

#endif // UI_H
