#include "game_screen.h"
#include "entities/entities.h"
#include "environment/environment_system.h"
#include "core/skill_manager.h"
#include "core/skill_helper.h"
#include "core/composition/visual_composer.h"
#include "character/character_model.h"
#include "control/control.h"
#include "boss/boss_system.h"
#include "core/map_manager.h"
#include "game/game_rules.h"
#include "ai/ai.h"
#include <math.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float s_camAngle   = 0.0f;
static float s_camDist    = 6.0f;
static bool  s_backToMenu = false;

// --- Module 7 match state ---
static GameState s_state = GAME_ARENA_INTRO;
static float s_introTimer = 0.0f;
static int s_lastBossPhase = -1; // minion waves trigger on phase change (M8)

// The Phase 0 match runs on DEFAULT_ARENA (its ring-out circle matches the
// entities arena constants — center (6,0,4.4), r=18). Spawn points inside.
static const Vector3 PLAYER_SPAWN = { 2.0f, 0.0f, 4.4f };
static const Vector3 BOSS_SPAWN   = { 10.0f, 0.0f, 4.4f };
static const float   INTRO_SECONDS = 2.0f;

static void ResetMatch(PlayerEntity *player) {
    s_state = GAME_ARENA_INTRO;
    s_introTimer = INTRO_SECONDS;
    s_lastBossPhase = -1;

    // Player agent may have died last match (HP or ring-out) — respawn it.
    if (Entity_GetAgent(player->agentId) == NULL) {
        player->agentId = Entity_SpawnAgent(PLAYER_SPAWN, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
    }
    player->position = PLAYER_SPAWN;
    Entity_SetPosition(player->agentId, player->position);
    Control_Init(player->agentId);

    // Default loadout (keys 1-4). One skill per element — deliberately NOT
    // 2 Âm + 2 Dương (that combination enters Thái Cực; the player should
    // discover it by re-equipping, No Tutorial). Majority tie → Thủy.
    {
        static const struct { const char *name; int element; } kLoadout[AGENT_SKILL_SLOTS] = {
            { "GLACIAL_CANNON", 0 }, // Thủy
            { "FIRE",           2 }, // Hỏa
            { "STONE_PRISON",   3 }, // Thổ
            { "LEAF_WHIRLWIND", 1 }, // Mộc
        };
        for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++) {
            int idx = Skill_GetIndexByName(kLoadout[slot].name);
            if (idx >= 0) Entity_SetEquippedSkill(player->agentId, slot, idx, kLoadout[slot].element);
        }
    }

    // Leftover boss from an aborted match: kill it so the next intro spawns
    // a fresh one (Boss_Spawn would otherwise leak the old pool agent).
    if (Boss_IsAlive()) {
        Entity_ApplyDamage(Boss_GetAgentId(), 1e9f, (Vector3){ 0 });
        Boss_Update(0.0f); // lets the boss system notice the death
    }
}

GameState GameScreen_GetState(void) {
    return s_state;
}

void GameScreen_Init(PlayerEntity *player) {
    s_camAngle   = 0.0f;
    s_camDist    = 6.0f;
    s_backToMenu = false;
    ResetMatch(player);
}

void GameScreen_Update(PlayerEntity *player, Camera3D *camera, float dt) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        s_backToMenu = true;
        ResetMatch(player); // abort → next entry starts a fresh match
        return;
    }

    // --- Match state machine (Module 7) ---
    if (s_state == GAME_ARENA_INTRO) {
        // Pin the match map (only while this screen is active — never from
        // Init, which runs once at startup for every screen).
        if (strcmp(MapManager_GetName(MapManager_GetActiveIndex()), "DEFAULT_ARENA") != 0) {
            for (int i = 0; i < MapManager_GetCount(); i++) {
                if (strcmp(MapManager_GetName(i), "DEFAULT_ARENA") == 0) {
                    MapManager_SetActiveIndex(i);
                    break;
                }
            }
        }
        s_introTimer -= dt;
        if (s_introTimer <= 0.0f) {
            Boss_Spawn(&BOSS_HAC_DIEN_TON_GIA, BOSS_SPAWN, TEAM_ENEMY);
            s_state = GAME_FIGHTING;
        }
        // Camera keeps framing the player during the title card (falls
        // through to the camera block at the bottom).
    } else if (s_state == GAME_FIGHTING) {
        const Agent *pa = Entity_GetAgent(player->agentId);
        if (pa == NULL) {
            s_state = GAME_DEFEAT; // HP hit zero or ring-out finished
        } else {
            // Zone modifier rules (game/game_rules.h — the one rule table):
            // cooldown for the player's element in its current zone, plus
            // forest stealth for Mộc. Combat/ enforces the Thổ projectile
            // penalty itself.
            NatureZoneType zone = Map_QueryZoneAt(pa->position);
            Control_SetCastCooldownMult(GameRules_CooldownMult(pa->currentElement, zone));
            Entity_SetStealth(player->agentId, GameRules_GrantsStealth(pa->currentElement, zone));

            // Module 8: the boss summons a minion wave on every phase
            // change (bigger waves as it gets desperate).
            int phase = Boss_GetPhase();
            if (phase != s_lastBossPhase) {
                if (phase > 0) AI_SpawnMinionWave(Boss_GetAgentId(), 3 + phase);
                s_lastBossPhase = phase;
            }

            if (!Boss_IsAlive()) s_state = GAME_VICTORY;
        }
    } else { // GAME_VICTORY / GAME_DEFEAT
        if (IsKeyPressed(KEY_ENTER)) {
            s_backToMenu = true;
            ResetMatch(player);
            return;
        }
    }

    if (IsKeyDown(KEY_Q)) s_camAngle -= 1.6f * dt;
    if (IsKeyDown(KEY_E)) s_camAngle += 1.6f * dt;
    s_camDist -= GetMouseWheelMove() * 0.4f;
    if (s_camDist < 3.0f)  s_camDist = 3.0f;
    if (s_camDist > 12.0f) s_camDist = 12.0f;

    float s = sinf(s_camAngle);
    float c = cosf(s_camAngle);

    // Module 4: movement/jump/dash/meditate/skill-cast all live in control/
    // now — this screen only forwards the camera and reads back the agent's
    // authoritative position (control writes it via Entity_SetPosition, and
    // entities owns it during jump arcs / dash bursts / knockback).
    // Intents only apply mid-fight — intro/end screens freeze the player.
    Control_SetCamera(s_camAngle, camera);
    PlayerIntent intent = Control_ReadIntent();
    if (s_state == GAME_FIGHTING) Control_Apply(&intent, dt);
    const Agent *selfAgent = Entity_GetAgent(player->agentId);
    if (selfAgent) player->position = selfAgent->position;

    float moveLen = sqrtf(intent.moveDir.x * intent.moveDir.x + intent.moveDir.y * intent.moveDir.y);
    CharacterModel_Update(&player->anim, dt, moveLen > 0.0001f);

    // Basic attack (đấm/đá/chưởng) — free, no cast time, spammable, auto-
    // targets via Entity_ExecuteBasicAttack (no enemy reference needed here
    // at all). Z / C / right-click all fire the same basic attack with a
    // RANDOM punch/kick/palm each press (matches sandbox's touch button —
    // per-type mapping waits for the real combo system). This is the real
    // production home for this input (see game_screen.h header) — sandbox
    // no longer has a PC-keyboard copy of this.
    bool basicAttackPressed = (s_state == GAME_FIGHTING) &&
                              (IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_C) ||
                               IsMouseButtonPressed(MOUSE_BUTTON_RIGHT));

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
            Control_FaceTowards(targetPos);
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

static Color MinionElementColor(int elem) {
    switch (elem) {
        case 0:  return ELEMENT_COLOR_WATER;
        case 1:  return ELEMENT_COLOR_WOOD;
        case 2:  return ELEMENT_COLOR_FIRE;
        case 3:  return ELEMENT_COLOR_EARTH;
        case 4:  return ELEMENT_COLOR_METAL;
        default: return ELEMENT_COLOR_TAIJI;
    }
}

void GameScreen_Draw3D(const PlayerEntity *player) {
    if (s_state == GAME_DEFEAT) return; // the fallen player isn't drawn

    // Minions (ARCH_MINION pool agents — ai/ is pure logic, render lives
    // here): small element-colored spirit orbs with a dark body, bobbing.
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->archetype != ARCH_MINION) continue;
        Color c = MinionElementColor(a->currentElement);
        float bob = 0.25f + 0.05f * sinf((float)GetTime() * 4.0f + (float)i);
        Vector3 body = { a->position.x, a->position.y + bob, a->position.z };
        Environment_DrawSmartShadow(a->position, ENV_SHAPE_SPHERE, 0.2f, 0.6f);
        DrawSphereEx(body, 0.18f, 8, 8,
                     (Color){ (unsigned char)(20 + c.r / 6), (unsigned char)(20 + c.g / 6),
                              (unsigned char)(20 + c.b / 6), 255 });
        DrawCircle3D(body, 0.26f, (Vector3){ 0, 1, 0 }, (float)GetTime() * 90.0f + i * 40.0f, c);
    }

    Environment_DrawSmartShadow(player->position, ENV_SHAPE_SPHERE, 0.5f, 0.9f);
    if (CharacterModel_IsLoaded()) {
        CharacterModel_Draw(&player->anim, player->position, Control_GetYaw(), 1.0f, WHITE);
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

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // Boss HP bar (top center) while a boss lives.
    if (Boss_IsAlive()) {
        const Agent *boss = Entity_GetAgent(Boss_GetAgentId());
        if (boss) {
            float bRatio = (boss->maxHealth > 0.0f) ? boss->health / boss->maxHealth : 0.0f;
            if (bRatio < 0.0f) bRatio = 0.0f;
            if (bRatio > 1.0f) bRatio = 1.0f;
            const int bw = 420, bh = 14, bx = sw / 2 - bw / 2, by = 16;
            DrawRectangle(bx, by, bw, bh, (Color){ 20, 20, 20, 220 });
            DrawRectangle(bx, by, (int)(bw * bRatio), bh, (Color){ 150, 40, 170, 255 });
            DrawRectangleLines(bx, by, bw, bh, (Color){ 220, 220, 220, 180 });
            const char *bn = "HAC DIEN TON GIA";
            DrawText(bn, sw / 2 - MeasureText(bn, 16) / 2, by + bh + 4, 16, (Color){ 230, 230, 240, 255 });
        }
    }

    // Match-state overlays (No Tutorial — one line each, no instructions
    // beyond the exit key).
    if (s_state == GAME_ARENA_INTRO) {
        const char *t = "HAC DIEN TON GIA";
        DrawText(t, sw / 2 - MeasureText(t, 40) / 2, sh / 2 - 60, 40, (Color){ 235, 230, 245, 255 });
    } else if (s_state == GAME_VICTORY) {
        const char *t = "CHIEN THANG";
        DrawText(t, sw / 2 - MeasureText(t, 44) / 2, sh / 2 - 60, 44, (Color){ 240, 220, 120, 255 });
        const char *hint = "ENTER";
        DrawText(hint, sw / 2 - MeasureText(hint, 18) / 2, sh / 2 - 8, 18, (Color){ 200, 200, 200, 255 });
    } else if (s_state == GAME_DEFEAT) {
        const char *t = "THAT BAI";
        DrawText(t, sw / 2 - MeasureText(t, 44) / 2, sh / 2 - 60, 44, (Color){ 200, 70, 70, 255 });
        const char *hint = "ENTER";
        DrawText(hint, sw / 2 - MeasureText(hint, 18) / 2, sh / 2 - 8, 18, (Color){ 200, 200, 200, 255 });
    }
}

bool GameScreen_RequestedBackToMenu(void) {
    bool req = s_backToMenu;
    s_backToMenu = false;
    return req;
}
