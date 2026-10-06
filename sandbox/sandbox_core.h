#ifndef SANDBOX_CORE_H
#define SANDBOX_CORE_H

#include "raylib.h"
#include "sandbox/ui_panel.h"
#include "character/character_model.h"
#include <math.h>

// Shared chase framing for skill sandbox and VFX defaults (meters).
#define SANDBOX_CAMERA_DEFAULT_YAW 0.0f
#define SANDBOX_CAMERA_DEFAULT_DISTANCE 18.0f
#define SANDBOX_CAMERA_MIN_DISTANCE 2.0f
#define SANDBOX_CAMERA_MAX_DISTANCE 36.0f
#define SANDBOX_CAMERA_HEIGHT_RATIO 0.62f
#define SANDBOX_CAMERA_TARGET_HEIGHT 1.2f
#define SANDBOX_CAMERA_DEFAULT_FOVY 45.0f
#define SANDBOX_CAMERA_PITCH_DEGREES 20.0f

// Preserve the previous camera-to-target radius at every zoom setting while
// lowering elevation. HEIGHT_RATIO defines that legacy radius, not new pitch.
static inline Vector3 SandboxCamera_OrbitPositionAtPitch(Vector3 playerPosition,
                                                        float yaw, float zoomDistance,
                                                        float pitchDegrees)
{
    const float legacyTargetOffset = zoomDistance * SANDBOX_CAMERA_HEIGHT_RATIO
                                  - SANDBOX_CAMERA_TARGET_HEIGHT;
    const float radius = sqrtf(zoomDistance * zoomDistance
                            + legacyTargetOffset * legacyTargetOffset);
    const float pitch = pitchDegrees * 0.017453292519943295f;
    const float horizontal = radius * cosf(pitch);
    return (Vector3){
        playerPosition.x + sinf(yaw) * horizontal,
        playerPosition.y + SANDBOX_CAMERA_TARGET_HEIGHT + radius * sinf(pitch),
        playerPosition.z + cosf(yaw) * horizontal
    };
}

static inline Vector3 SandboxCamera_OrbitPosition(Vector3 playerPosition,
                                                 float yaw, float zoomDistance)
{
    return SandboxCamera_OrbitPositionAtPitch(playerPosition, yaw, zoomDistance,
                                             SANDBOX_CAMERA_PITCH_DEGREES);
}

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
    bool isFlying;
    int agentId; // Entities module agentPool slot (see entities/entities.h)
    CharacterAnimState anim; // character/character_model.h — real model+animation
                              // playback state; no-op fallback to
                              // DrawCharacter3D while no asset is loaded.
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
    int agentId; // Entities module agentPool slot (see entities/entities.h)
} EnemyEntity;

// Khởi tạo các thực thể và môi trường 3D
void InitSandbox(PlayerEntity* player, EnemyEntity* enemy);

// Cập nhật logic vật lý, di chuyển, nhắm chuột
void UpdateSandbox(PlayerEntity* player, EnemyEntity* enemy, float dt, UIPanelState* uiState, Vector3* outMouseTarget);

// Vẽ toàn bộ cảnh 3D (võ đài, người chơi, quái vật, cột đá)
void DrawCharacter3D(Vector3 position, float radius, Color skinCol, Color clothesCol, Color outlineCol, bool isPlayer, Vector3 targetPos);
void DrawSandbox3D(const PlayerEntity* player, const EnemyEntity* enemy, Vector3 mouseTarget, UIPanelState* uiState);

// Vẽ HUD thông tin debug Sandbox (camera parameters, instructions)
void DrawSandboxHUD(void);

// Hệ thống phím cảm ứng & Bay trên Android
extern bool g_showTouchControls;
void DrawSandboxTouchControls(const PlayerEntity* player);

// --- Training dummy (CC test rig, see entities/entities.h §12) ---
// agentPool slot of the standalone test dummy spawned by InitSandbox, or -1
// if not yet spawned. Entities module owns its position/state entirely once
// spawned (no per-frame Entity_SetPosition call for it) — ui_panel.c's CC
// test buttons call Entity_ApplyStun/Launch/Pull directly with this id.
int Sandbox_GetTrainingDummyAgentId(void);
// Respawns the dummy at its fixed test position with full HP and cleared CC
// state (deactivates the old slot, spawns a fresh one).
void Sandbox_ResetTrainingDummy(void);
// agentPool slot of the sandbox's PlayerEntity (set by InitSandbox) — lets
// ui_panel.c's "Pull to Player" test button read a live target position
// without needing a PlayerEntity pointer in its own signature.
int Sandbox_GetPlayerAgentId(void);

// Quay mặt model player về worldPos (chỉ đổi hướng vẽ, không đổi vị trí) —
// gọi ngay khi ra đòn/cast chiêu để nhân vật xoay về hướng đánh thay vì giữ
// hướng di chuyển cuối. Bỏ qua nếu worldPos trùng vị trí player.
void Sandbox_FacePlayerToward(const PlayerEntity* player, Vector3 worldPos);

#endif // SANDBOX_CORE_H
