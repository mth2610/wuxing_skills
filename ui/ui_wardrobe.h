// ui/ui_wardrobe.h
// Interactive Wuxia Equipment & Wardrobe UI for Wuxing Skills.
//
// Provides:
// 1. Full-screen Wuxia / Tiên Hiệp aesthetic character customization panel.
// 2. Real-time 3D character inspection stage with 360-degree rotation.
// 3. Category slot tabs (Body, Robe, Pants, Boots, Bracers, Hat, Scabbard, Weapon).
// 4. Personal collection browser with rarity color coding & elemental badges.
// 5. One-click equip/unequip with binary combat stance sync (Sword vs Unarmed).

#ifndef UI_WARDROBE_H
#define UI_WARDROBE_H

#include "raylib.h"
#include "character/wardrobe_system.h"
#include "character/modular_character.h"
#include <stdbool.h>

void UI_Wardrobe_Init(void);
void UI_Wardrobe_Toggle(PlayerWardrobe *pw, ModularInstance *inst);
void UI_Wardrobe_Open(PlayerWardrobe *pw, ModularInstance *inst);
void UI_Wardrobe_Close(void);
bool UI_Wardrobe_IsOpen(void);

// Updates input (clicks, drags, keyboard) and renders the full-screen wardrobe interface.
// Call after the main 3D scene pass before EndDrawing().
void UI_Wardrobe_UpdateDraw(PlayerWardrobe *pw, ModularInstance *inst, float dt);

#endif // UI_WARDROBE_H
