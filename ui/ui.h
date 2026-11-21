// ui/ui.h
// Module 9: Unified PC & Mobile HUD + Auto-Targeting ("Where Winds Meet" Style).
#ifndef UI_H
#define UI_H

#include "raylib.h"
#include <stdbool.h>

void UI_Init(void);
void UI_SetCamera(const Camera3D *camera);
void UI_Update(float dt);

// Auto-aim for agentId. Returns the point via outPoint and true when a
// target exists; false = caller keeps its own aim (mouse).
Vector3 UI_GetAutoAimPoint(int agentId, bool *hasTarget);
void    UI_SetManualTarget(int agentId);
int     UI_GetManualTarget(void);

// 2D pass, after the 3D scene: unified bottom-right skill cluster (PC),
// loadout panel (TAB), and auto-aim reticle.
void UI_DrawOverlay(int agentId);

// Top-left circular translucent compass / minimap (shared PC & Mobile)
void UI_DrawCompassMinimap(Vector3 playerPos, float camYaw,
                           Vector3 questTarget, bool hasQuestTarget, bool inSafeZone);

// Bottom-center Where Winds Meet vitals bar (Element crest, Sinh Lực, Chân Khí, Thể Lực, Potions)
void UI_DrawPlayerVitals(int agentId, int healPotions, int manaPotions,
                         bool *outUseHeal, bool *outUseMana);

// --- Lobby screen (Sảnh chờ — ui/ui_lobby.c) ---
typedef enum { UI_LOBBY_NONE = 0, UI_LOBBY_START, UI_LOBBY_LEAVE } UILobbyAction;
UILobbyAction UI_LobbyUpdateDraw(const char *joinCode, bool isHost);

// --- Loadout panel (Trang Bị Võ Học — TAB in PvP) ---
void UI_ToggleLoadout(void);
bool UI_IsLoadoutOpen(void);

// --- Unified Combat Skill Slot & Touch Controls (shared by Game, Sandbox, VFX Tester) ---
typedef struct {
    int   skillIndex;
    char  shortLabel[24];
    Color color;
    bool  ready;
    float cooldownRemain;
    float cooldownTotal;
} UITouchSkillSlot;

typedef struct {
    float joyX;
    float joyZ;
    bool  joyActive;
    bool  attackHeld;
    bool  attackPressed;
    bool  khinhCongHeld;
    bool  khinhCongPressed;
    bool  dodgeHeld;
    bool  dodgePressed;
    bool  meditateHeld;
    bool  skillHeld[7];
    bool  skillPressed[7];
    bool  pointerOverUI;
} UITouchCombatInput;

void UI_SetTouchControlsEnabled(bool enabled);
bool UI_IsTouchControlsEnabled(void);
void UI_FormatShortSkillName(const char *fullName, char *outBuf, int bufSize);
void UI_PollTouchCombat(const UITouchSkillSlot slots[7], UITouchCombatInput *outInput);
void UI_DrawTouchCombat(const UITouchSkillSlot slots[7], const UITouchCombatInput *input);

#endif // UI_H
