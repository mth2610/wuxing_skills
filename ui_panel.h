#ifndef UI_PANEL_H
#define UI_PANEL_H

#include "raylib.h"
#include "skill_manager.h"

typedef struct {
    int activeSkillIndex;
    bool clickedOnUI;
    SkillParams currentParams;
} UIPanelState;

// Khởi tạo bảng điều khiển (định nghĩa các tọa độ nút bấm)
void InitUIPanel(void);

// Cập nhật trạng thái chuột và thao tác bấm, trả về trạng thái UI hiện tại
void UpdateUIPanel(Vector2 mousePos, UIPanelState *state);

// Vẽ bảng điều khiển lên màn hình 2D
void DrawUIPanel(const UIPanelState *state);

#endif // UI_PANEL_H
