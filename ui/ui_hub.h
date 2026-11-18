// ui/ui_hub.h
// Global Game Hub & Preparation UI:
//   - Main Screen: Mode 1 (5 Explore Realms) + Mode 2 (1v1 & 2v2 Dual-Boss PvP)
//   - Global Modal (3 Tabs):
//       1. Tàng Kinh Các (Select 5 Combat Skills + View Lv.1..10)
//       2. Tiệm Đổi Thưởng Dã Tẩu (Buy Skills & Gear with Gold / Points)
//       3. Tủ Đồ 3D (Opens UI_Wardrobe)
#ifndef UI_HUB_H
#define UI_HUB_H

#include <stdbool.h>

typedef enum {
    UI_HUB_ACT_NONE = 0,
    UI_HUB_ACT_EXPLORE_REALM,    // outParam = realmId (0..4)
    UI_HUB_ACT_PVP_1V1,          // Song Linh Ho Phap 1v1
    UI_HUB_ACT_PVP_2V2,          // Song Linh Ho Phap 2v2
    UI_HUB_ACT_HOST_ONLINE,
    UI_HUB_ACT_JOIN_ONLINE,
    UI_HUB_ACT_SANDBOX,
    UI_HUB_ACT_VFX_TESTER
} UIHubAction;

void UI_Hub_Init(void);
void UI_Hub_OpenModal(int initialTab); // 0=Vo Hoc 5 O, 1=Tiem Da Tau
void UI_Hub_CloseModal(void);
bool UI_Hub_IsModalOpen(void);

// Main menu hub screen: draws mode cards, currencies, global prep buttons.
// Returns action + optional realmId in *outParam.
UIHubAction UI_Hub_UpdateDrawMain(int *outParam, char *joinCodeBuf, int joinCodeMax, const char *statusMsg);

// Global prep modal (can be drawn over Main Menu or inside Explore/Lobby).
void UI_Hub_UpdateDrawModal(void);

#endif // UI_HUB_H
