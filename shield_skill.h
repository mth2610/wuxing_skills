#ifndef SHIELD_SKILL_H
#define SHIELD_SKILL_H

#include "raylib.h"

// Khởi tạo tài nguyên Shader Phase 1 và nạp Texture Caustics/Flow Map cho Khiên
// Nước Cập nhật: Nhận screenWidth và screenHeight để đồng nhất hệ thống
void InitShieldSkill(int screenWidth, int screenHeight);

// Kích hoạt Thủy Giáp Hộ Thể tại một vị trí (Tâm của nhân vật) với bán kính và
// thời gian duy trì cố định
void CastShieldSkill(Vector3 position, float radius, float duration);

// Cập nhật logic đếm ước thời gian và khóa chặt vị trí khiên đi theo nhân vật
// mỗi frame
void UpdateShieldSkill(float dt, Vector3 currentCasterPos);

// Kích hoạt trạng thái Blend Alpha và Shader Flow Map 2 pha để vẽ khối cầu
// khiên nước
void DrawShieldSkill(void);

// Giải phóng Shader và các Texture tĩnh trong VRAM khi đóng game hoặc chuyển
// màn
void UnloadShieldSkill(void);

// Kiểm tra xem trạng thái Khiên bảo vệ có đang hoạt động hay không
bool IsShieldSkillActive(void);

#endif // SHIELD_SKILL_H