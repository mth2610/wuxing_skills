#include "game_screen.h"
#include "entities/entities.h"
#include "environment/environment_system.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float s_camAngle   = 0.0f;
static float s_camDist    = 6.0f;
static bool  s_backToMenu = false;

void GameScreen_Init(PlayerEntity *player) {
    s_camAngle   = 0.0f;
    s_camDist    = 6.0f;
    s_backToMenu = false;

    // Center of maps/worlds/verdant_path (100m x 75m, center at 50/37.5) —
    // spawns on the path, clear of the cliff ring. (Previously the corner,
    // to gauge the ~125m diagonal walk time — the island/cliff redesign
    // makes the center the more useful default now.)
    player->position = (Vector3){ 50.0f, 0.0f, 37.5f };
    Entity_SetPosition(player->agentId, player->position);
}

void GameScreen_Update(PlayerEntity *player, Camera3D *camera, float dt) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        s_backToMenu = true;
        return;
    }

    if (IsKeyDown(KEY_Q)) s_camAngle -= 1.6f * dt;
    if (IsKeyDown(KEY_E)) s_camAngle += 1.6f * dt;
    s_camDist -= GetMouseWheelMove() * 0.4f;
    if (s_camDist < 3.0f)  s_camDist = 3.0f;
    if (s_camDist > 12.0f) s_camDist = 12.0f;

    float s = sinf(s_camAngle);
    float c = cosf(s_camAngle);

    // Brisk walk speed, real-world-scaled (1 unit = 1 meter).
    const float moveSpeed = 3.5f;

    Vector3 move = {0};
    if (IsKeyDown(KEY_W)) { move.x -= s; move.z -= c; }
    if (IsKeyDown(KEY_S)) { move.x += s; move.z += c; }
    if (IsKeyDown(KEY_A)) { move.x -= c; move.z += s; }
    if (IsKeyDown(KEY_D)) { move.x += c; move.z -= s; }

    float moveLen = sqrtf(move.x * move.x + move.z * move.z);
    if (moveLen > 0.0001f) {
        player->position.x += (move.x / moveLen) * moveSpeed * dt;
        player->position.z += (move.z / moveLen) * moveSpeed * dt;
    }

    Entity_SetPosition(player->agentId, player->position);

    camera->target = (Vector3){
        player->position.x, player->position.y + 1.6f, player->position.z
    };
    camera->position = (Vector3){
        player->position.x + s * s_camDist,
        player->position.y + s_camDist * 0.85f,
        player->position.z + c * s_camDist
    };
}

void GameScreen_Draw3D(const PlayerEntity *player) {
    Environment_DrawSmartShadow(player->position, ENV_SHAPE_SPHERE, 0.5f, 0.9f);
    DrawCharacter3D(player->position, 0.25f,
                    GetColor(0xFFD39BFF), GetColor(0x3B5998FF), GetColor(0xCCCCCCFF),
                    true, player->position);
}

void GameScreen_DrawHUD(const PlayerEntity *player) {
    const Agent *agent = Entity_GetAgent(player->agentId);
    float hp    = agent ? agent->health    : 0.0f;
    float maxHp = agent ? agent->maxHealth : 1.0f;
    float ratio = (maxHp > 0.0f) ? (hp / maxHp) : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    const int barX = 20, barY = 20, barW = 220, barH = 18;
    DrawRectangle(barX, barY, barW, barH, (Color){ 30, 30, 30, 220 });
    DrawRectangle(barX, barY, (int)(barW * ratio), barH, (Color){ 190, 40, 40, 255 });
    DrawRectangleLines(barX, barY, barW, barH, (Color){ 220, 220, 220, 180 });
}

bool GameScreen_RequestedBackToMenu(void) {
    bool req = s_backToMenu;
    s_backToMenu = false;
    return req;
}
