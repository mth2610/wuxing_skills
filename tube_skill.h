#ifndef TUBE_SKILL_H
#define TUBE_SKILL_H

#include "raylib.h"

#ifndef SKILL_PROJECTILE_DEF
#define SKILL_PROJECTILE_DEF
typedef struct {
    Vector3 position;
    float radius;
    bool active;
} SkillProjectile;
#endif

// Khởi tạo hệ thống Thủy Long (Water Tube)
void InitTubeSkill(int screenWidth, int screenHeight);

// Tung chiêu. 
// twistPhase có thể dùng để đẩy control point lệch sang hai bên, tạo đường uốn lượn khác nhau.
void CastTubeSkill(Vector3 startPos, Vector3 target, float twistPhase, float sizeScale);

// Cập nhật logic (tiến độ dòng chảy, sinh hạt nước văng ra)
void UpdateTubeSkill(float dt);

// Vẽ ống nước bằng Custom 3D Mesh và Shader
void DrawTubeSkill(void);

// Dọn dẹp bộ nhớ Shader
void UnloadTubeSkill(void);

// Trả về tọa độ "Đầu rồng" (điểm đi đầu của dòng nước) để xét va chạm
int GetTubeSkillProjectiles(SkillProjectile* outProjectiles, int maxProjectiles);

// Kích nổ / Dọn dẹp dòng nước khi trúng mục tiêu
void DeactivateTubeProjectile(int index);

#endif // TUBE_SKILL_H