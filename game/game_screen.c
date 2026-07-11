#include "game_screen.h"
#include "entities/entities.h"
#include "environment/environment_system.h"
#include "core/skill_manager.h"
#include "core/skill_helper.h"
#include "core/composition/visual_composer.h"
#include "character/character_model.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float s_camAngle   = 0.0f;
static float s_camDist    = 6.0f;
static bool  s_backToMenu = false;
static float s_playerYaw  = 0.0f; // radians, updated while moving, held otherwise

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
        s_playerYaw = atan2f(move.x, move.z);
    }

    Entity_SetPosition(player->agentId, player->position);
    CharacterModel_Update(&player->anim, dt, moveLen > 0.0001f);

    // Basic attack (đấm/đá/chưởng) — free, no cast time, spammable, auto-
    // targets via Entity_ExecuteBasicAttack (no enemy reference needed here
    // at all). Z / C / right-click all fire the same basic attack with a
    // RANDOM punch/kick/palm each press (matches sandbox's touch button —
    // per-type mapping waits for the real combo system). This is the real
    // production home for this input (see game_screen.h header) — sandbox
    // no longer has a PC-keyboard copy of this.
    bool basicAttackPressed = IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_C) ||
                              IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

    if (basicAttackPressed) {
        BasicAttackType basicAttackType = (BasicAttackType)GetRandomValue(0, 2);
        CharacterAnimSlot animSlot = (basicAttackType == BASIC_ATTACK_PUNCH) ? CHAR_ANIM_PUNCH :
                                     (basicAttackType == BASIC_ATTACK_KICK)  ? CHAR_ANIM_KICK : CHAR_ANIM_PALM;
        CharacterModel_TriggerAttackTimed(&player->anim, animSlot,
                                          Entity_GetBasicAttackSeconds(basicAttackType));
        Vector3 targetPos, wallPos;
        int wallElement;
        bool gotWallBonus = Entity_ExecuteBasicAttack(player->agentId, basicAttackType, &targetPos, &wallPos, &wallElement);
        // targetPos is only written when auto-target found someone (stays
        // (0,0,0) otherwise) — turn to face the attack direction if so.
        if (targetPos.x != 0.0f || targetPos.y != 0.0f || targetPos.z != 0.0f) {
            s_playerYaw = atan2f(targetPos.x - player->position.x,
                                 targetPos.z - player->position.z);
        }
        if (gotWallBonus && wallElement == 3 /* Earth — only element with a real wall so far */) {
            VFX_SpawnProcBeam(wallPos, targetPos, EFFECT_PRESET_EARTH_CRACK, 0.12f, 0.35f);
            VFX_ComposeImpact(targetPos, EFFECT_PRESET_EARTH_CRACK, 0.6f);
            AddFloatingText(targetPos, "Cong Huong Dat!", ELEMENT_COLOR_EARTH, 16.0f, 0.6f);
        }
    }

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
    if (CharacterModel_IsLoaded()) {
        CharacterModel_Draw(&player->anim, player->position, s_playerYaw, 1.0f, WHITE);
    } else {
        DrawCharacter3D(player->position, 0.25f,
                        GetColor(0xFFD39BFF), GetColor(0x3B5998FF), GetColor(0xCCCCCCFF),
                        true, player->position);
    }
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

    float mana    = agent ? agent->mana    : 0.0f;
    float maxMana = agent ? agent->maxMana : 1.0f;
    float manaRatio = (maxMana > 0.0f) ? (mana / maxMana) : 0.0f;
    if (manaRatio < 0.0f) manaRatio = 0.0f;
    if (manaRatio > 1.0f) manaRatio = 1.0f;

    const int manaBarY = barY + barH + 6;
    DrawRectangle(barX, manaBarY, barW, barH, (Color){ 30, 30, 30, 220 });
    DrawRectangle(barX, manaBarY, (int)(barW * manaRatio), barH, (Color){ 60, 90, 220, 255 });
    DrawRectangleLines(barX, manaBarY, barW, barH, (Color){ 220, 220, 220, 180 });
}

bool GameScreen_RequestedBackToMenu(void) {
    bool req = s_backToMenu;
    s_backToMenu = false;
    return req;
}
