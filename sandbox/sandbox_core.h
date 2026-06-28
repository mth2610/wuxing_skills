#ifndef SANDBOX_CORE_H
#define SANDBOX_CORE_H

#include "raylib.h"
#include "sandbox/ui_panel.h"

// Biến camera toàn cục
extern Camera3D camera;

typedef struct {
    Vector3 position;
    float radius;
    float dashCooldown;
    float dashTimer;
    Vector3 dashDir;
    bool isDashing;
    float zVelocity;
    int jumpCount;
} PlayerEntity;

typedef enum {
    ENEMY_STATIC = 0,
    ENEMY_CHASE,
    ENEMY_PATROL
} EnemyMode;

typedef struct {
    Vector3 position;
    float radius;
    EnemyMode mode;
    float speed;
    float patrolAngle;
    float oscillationScale;
    Vector3 knockbackVelocity; // MỚI: Vận tốc hất văng tích lũy
} EnemyEntity;

// Khởi tạo các thực thể và môi trường 3D
void InitSandbox(PlayerEntity* player, EnemyEntity* enemy);

// Cập nhật logic vật lý, di chuyển, nhắm chuột
void UpdateSandbox(PlayerEntity* player, EnemyEntity* enemy, float dt, UIPanelState* uiState, Vector3* outMouseTarget);

// Vẽ toàn bộ cảnh 3D (võ đài, người chơi, quái vật, cột đá)
void DrawSandbox3D(const PlayerEntity* player, const EnemyEntity* enemy, Vector3 mouseTarget, UIPanelState* uiState);

// Vẽ HUD thông tin debug Sandbox (camera parameters, instructions)
void DrawSandboxHUD(void);

#endif // SANDBOX_CORE_H
