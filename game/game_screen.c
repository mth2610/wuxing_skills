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
#include "ui/ui.h"
#include "net/net_transport.h"
#include "combat/combat.h"
#include "core/audio_system.h"
#include <math.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static float s_camAngle = 0.0f;
static float s_camDist = 6.0f;
static bool s_backToMenu = false;

// --- Module 7 match state ---
static GameState s_state = GAME_ARENA_INTRO;
static GameMode s_mode = GAME_MODE_BOSS; // Đợt A3 — main.c sets per entry
static char s_onlineCode[16] = {0};      // EOS room code shown while hosting
static float s_introTimer = 0.0f;
static int s_lastBossPhase = -1;      // minion waves trigger on phase change (M8)
static float s_swingSlowTimer = 0.0f; // movement damped while a swing plays

// Team battle: two spawn clusters facing each other across the plateau
// center; heroes fan out ±z within their cluster.
static const Vector3 TEAM_SPAWN[2] = {{42.0f, 0.0f, 37.5f}, {58.0f, 0.0f, 37.5f}};

// Per-agent render state for OTHER heroes (remote players + bots), so each
// gets its own walk/idle animation + facing on the shared character model.
static CharacterAnimState s_remoteAnim[MAX_AGENTS];
static Vector3 s_remotePrevPos[MAX_AGENTS];
static float s_remoteYaw[MAX_AGENTS];
static bool s_remoteAnimInit[MAX_AGENTS];

// The Phase 0 match runs on VERDANT_PATH — the real grass island (100x75m,
// flat plateau at Y=0, cliff falloff past ~34m from center). Ring-out
// bounds + player placement are applied in the INTRO tick (only while this
// screen is ACTIVE — the bounds are global, and the sandbox needs the
// DEFAULT_ARENA circle back; main.c restores it on back-to-menu).
#define MATCH_MAP_NAME "VERDANT_PATH"
static const Vector3 MATCH_ARENA_CENTER = {50.0f, 0.0f, 37.5f};
static const float MATCH_ARENA_RADIUS = 34.0f;
static const Vector3 PLAYER_SPAWN = {46.0f, 0.0f, 37.5f};
static const Vector3 BOSS_SPAWN = {54.0f, 0.0f, 37.5f};
static const float INTRO_SECONDS = 2.0f;

// Default loadout (keys 1-4). One skill per element — deliberately NOT
// 2 Âm + 2 Dương (that combination enters Thái Cực; the player should
// discover it by re-equipping, No Tutorial). Majority tie → Thủy.
static void EquipDefaultLoadout(int agentId)
{
    static const struct
    {
        const char *name;
        int element;
    } kLoadout[AGENT_SKILL_SLOTS] = {
        {"GLACIAL_CANNON", 0}, // Thủy
        {"FIRE", 2},           // Hỏa
        {"STONE_PRISON", 3},   // Thổ
        {"LEAF_WHIRLWIND", 1}, // Mộc
    };
    for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++)
    {
        int idx = Skill_GetIndexByName(kLoadout[slot].name);
        if (idx >= 0)
            Entity_SetEquippedSkill(agentId, slot, idx, kLoadout[slot].element);
    }
}

static void ResetMatch(PlayerEntity *player)
{
    s_state = GAME_ARENA_INTRO;
    s_introTimer = INTRO_SECONDS;
    s_lastBossPhase = -1;
    s_swingSlowTimer = 0.0f;
    if (UI_IsLoadoutOpen())
        UI_ToggleLoadout();
    // Team battle: bots re-materialize from the roster each round (the
    // INTRO tick spawns them) — clear the old brains/agents first.
    if (s_mode == GAME_MODE_TEAM_BATTLE)
        AI_ClearHeroBots();
    // Fresh remote-hero render state (slots get reused across rounds).
    for (int i = 0; i < MAX_AGENTS; i++)
        s_remoteAnimInit[i] = false;

    // Player agent may have died last match (HP or ring-out) — respawn it.
    // Placement happens in the INTRO tick (bounds are only correct while
    // this screen is active); spawning at the match point here is fine
    // because ResetMatch only runs at startup or while in-game.
    if (Entity_GetAgent(player->agentId) == NULL)
    {
        player->agentId = Entity_SpawnAgent(PLAYER_SPAWN, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
        // Team battle: the lobby may have moved the host to side 1 — the
        // respawned agent must keep that side (roster's HOST entry has it).
        if (s_mode == GAME_MODE_TEAM_BATTLE)
        {
            NetRosterEntry roster[NET_MAX_PLAYERS];
            int rc = Net_GetRoster(roster, NET_MAX_PLAYERS);
            for (int i = 0; i < rc; i++)
            {
                if (!(roster[i].flags & NET_ROSTER_HOST))
                    continue;
                Entity_SetAgentTeam(player->agentId,
                                    roster[i].team == 0 ? TEAM_ALLY : TEAM_ENEMY);
                break;
            }
        }
    }
    Control_Init(player->agentId);

    EquipDefaultLoadout(player->agentId);

    // Leftover boss from an aborted match: kill it so the next intro spawns
    // a fresh one (Boss_Spawn would otherwise leak the old pool agent).
    if (Boss_IsAlive())
    {
        Entity_ApplyDamage(Boss_GetAgentId(), 1e9f, (Vector3){0});
        Boss_Update(0.0f); // lets the boss system notice the death
    }
}

GameState GameScreen_GetState(void)
{
    return s_state;
}

void GameScreen_SetMode(GameMode mode) { s_mode = mode; }
GameMode GameScreen_GetMode(void) { return s_mode; }

// Host, team battle: line every living hero up on its side's spawn cluster
// (remote heroes joined at the invasion point; the round starts in ranks).
static void PlaceHeroesAtTeamSpawns(void)
{
    int placed[2] = {0, 0};
    for (int i = 0; i < MAX_AGENTS; i++)
    {
        const Agent *a = Entity_GetAgent(i);
        if (a == NULL || a->archetype != ARCH_HERO)
            continue;
        int t = (a->team == TEAM_ALLY) ? 0 : 1;
        Vector3 pos = TEAM_SPAWN[t];
        pos.z += (float)placed[t] * 1.8f - 2.7f;
        Entity_SetPosition(i, pos);
        placed[t]++;
    }
}

void GameScreen_Init(PlayerEntity *player)
{
    s_camAngle = 0.0f;
    s_camDist = 6.0f;
    s_backToMenu = false;
    UI_Init();
    ResetMatch(player);
}

void GameScreen_Update(PlayerEntity *player, Camera3D *camera, float dt)
{
    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (UI_IsLoadoutOpen())
        {
            UI_ToggleLoadout(); // ESC closes the panel first, not the match
        }
        else
        {
            s_backToMenu = true;
            ResetMatch(player); // abort → next entry starts a fresh match
            return;
        }
    }

    // Trang Bị panel — mid-fight swapping is allowed (the match keeps
    // running; the risk is the point). Player intents freeze while open.
    if (s_state == GAME_FIGHTING && IsKeyPressed(KEY_TAB))
    {
        UI_ToggleLoadout();
    }

    // --- Match state machine (Module 7) ---
    if (s_state == GAME_ARENA_INTRO)
    {
        // Pin the match map + its ring-out bounds (only while this screen
        // is active — never from Init, which runs once at startup for every
        // screen; main.c restores the DEFAULT_ARENA bounds on exit).
        if (strcmp(MapManager_GetName(MapManager_GetActiveIndex()), MATCH_MAP_NAME) != 0)
        {
            for (int i = 0; i < MapManager_GetCount(); i++)
            {
                if (strcmp(MapManager_GetName(i), MATCH_MAP_NAME) == 0)
                {
                    MapManager_SetActiveIndex(i);
                    break;
                }
            }
        }
        Entity_SetArenaBounds(MATCH_ARENA_CENTER, MATCH_ARENA_RADIUS);
        if (s_mode == GAME_MODE_TEAM_BATTLE)
        {
            // Both sides stand in ranks through the title card. The host
            // owns placement (clients mirror via snapshots).
            if (!Net_ClientDrivesWorld())
            {
                NetRosterEntry roster[NET_MAX_PLAYERS];
                int rc = Net_GetRoster(roster, NET_MAX_PLAYERS);

                // Retire heroes that aren't part of this room — leftover
                // sandbox/test agents would otherwise count toward a side
                // (elimination + handicap both read the pool).
                for (int i = 0; i < MAX_AGENTS; i++)
                {
                    const Agent *a = Entity_GetAgent(i);
                    if (a == NULL || a->archetype != ARCH_HERO)
                        continue;
                    if (i == player->agentId || AI_IsHeroBot(i))
                        continue;
                    bool inRoster = false;
                    for (int r = 0; r < rc; r++)
                        if (roster[r].agentId == (unsigned char)i &&
                            roster[r].agentId != NET_ROSTER_NONE)
                        {
                            inRoster = true;
                            break;
                        }
                    if (!inRoster)
                        Entity_ApplyDamage(i, 1e9f, (Vector3){0});
                }

                // Bots from the lobby roster materialize here (Đợt A4):
                // spawn until each side's living bot count matches it.
                int want[2] = {0, 0};
                for (int i = 0; i < rc; i++)
                    if (roster[i].flags & NET_ROSTER_BOT)
                        want[roster[i].team]++;
                for (int t = 0; t < 2; t++)
                {
                    AgentTeam team = (t == 0) ? TEAM_ALLY : TEAM_ENEMY;
                    while (AI_GetHeroBotCount(team) < want[t])
                    {
                        int botId = AI_SpawnHeroBot(TEAM_SPAWN[t], team);
                        if (botId < 0)
                            break;
                        EquipDefaultLoadout(botId);
                    }
                }
                PlaceHeroesAtTeamSpawns();
                const Agent *pa = Entity_GetAgent(player->agentId);
                if (pa != NULL)
                    player->position = pa->position;
            }
        }
        else
        {
            // Hold the player at the spawn point through the title card.
            player->position = PLAYER_SPAWN;
            Entity_SetPosition(player->agentId, player->position);
        }
        s_introTimer -= dt;
        if (s_introTimer <= 0.0f)
        {
            if (s_mode == GAME_MODE_BOSS)
            {
                Boss_Spawn(&BOSS_HAC_DIEN_TON_GIA, BOSS_SPAWN, TEAM_ENEMY);
            }
            else if (!Net_ClientDrivesWorld())
            {
                // Handicap buff (Đợt A4): the side that accepts fighting
                // short-handed gets per-missing-player multipliers — once,
                // at round start. Bots already count, so a bot-filled slot
                // grants nothing.
                int ally = GameRules_CountAliveHeroes(TEAM_ALLY);
                int enemy = GameRules_CountAliveHeroes(TEAM_ENEMY);
                int shortTeam = (ally < enemy) ? 0 : 1;
                int deficit = (ally < enemy) ? (enemy - ally) : (ally - enemy);
                if (deficit > 0)
                {
                    TeamHandicap h = GameRules_HandicapFor(deficit);
                    AgentTeam team = (shortTeam == 0) ? TEAM_ALLY : TEAM_ENEMY;
                    for (int i = 0; i < MAX_AGENTS; i++)
                    {
                        const Agent *a = Entity_GetAgent(i);
                        if (a == NULL || a->archetype != ARCH_HERO || a->team != team)
                            continue;
                        Entity_ScaleMaxHealth(i, h.maxHpMult);
                        Entity_AddModifier(i, h.speedMult, 3600.0f); // whole round
                    }
                    TraceLog(LOG_INFO, "[GAME] handicap +%d cho phe %d (HP x%.2f, speed x%.2f)",
                             deficit, shortTeam, h.maxHpMult, h.speedMult);
                }
            }
            s_state = GAME_FIGHTING;
        }
        // Camera keeps framing the player during the title card (falls
        // through to the camera block at the bottom).
    }
    else if (s_state == GAME_FIGHTING)
    {
        // Connected client: the host owns rules/waves/outcomes — this side
        // only points the camera/HUD at the hero the host assigned us
        // (mirrored 1:1 into the local pool by snapshot sync).
        if (Net_ClientDrivesWorld())
        {
            int hid = Net_GetLocalHeroAgentId();
            if (hid >= 0)
                player->agentId = hid;
            // Match outcome comes from the host, stated from the HOST's
            // perspective — map it through team membership: same side as
            // the host → keep it, opposite side → swap.
            // Our own side, cached while our hero lives — the outcome often
            // lands right after we died, when the agent is already gone.
            static int s_mySide = -1;
            const Agent *me = Entity_GetAgent(player->agentId);
            if (me != NULL)
                s_mySide = (me->team == TEAM_ALLY) ? 0 : 1;
            int rs = Net_GetRemoteMatchState();
            if (rs == (int)GAME_VICTORY || rs == (int)GAME_DEFEAT)
            {
                bool sameSide = false; // invasion default: we oppose the host
                if (s_mode == GAME_MODE_TEAM_BATTLE && s_mySide >= 0)
                {
                    NetRosterEntry roster[NET_MAX_PLAYERS];
                    int rc = Net_GetRoster(roster, NET_MAX_PLAYERS);
                    for (int i = 0; i < rc; i++)
                    {
                        if (!(roster[i].flags & NET_ROSTER_HOST))
                            continue;
                        sameSide = (s_mySide == (int)roster[i].team);
                        break;
                    }
                }
                bool hostWon = (rs == (int)GAME_VICTORY);
                s_state = (hostWon == sameSide) ? GAME_VICTORY : GAME_DEFEAT;
            }
        }
        else if (s_mode == GAME_MODE_TEAM_BATTLE)
        {
            // Elimination (Đợt A3): a side with no living heroes loses.
            // Bots (A4) and remote players are heroes in the same pool.
            const Agent *pa = Entity_GetAgent(player->agentId);
            if (pa != NULL)
            {
                NatureZoneType zone = Map_QueryZoneAt(pa->position);
                Control_SetCastCooldownMult(GameRules_CooldownMult(pa->currentElement, zone));
                Entity_SetStealth(player->agentId, GameRules_GrantsStealth(pa->currentElement, zone));
            }
            // Remember the host's side while alive — a dead host with
            // living teammates still wins as a team.
            static int s_hostSide = 0;
            if (pa != NULL)
                s_hostSide = (pa->team == TEAM_ALLY) ? 0 : 1;
            int ally = GameRules_CountAliveHeroes(TEAM_ALLY);
            int enemy = GameRules_CountAliveHeroes(TEAM_ENEMY);
            if (ally == 0 || enemy == 0)
            {
                int aliveTeam = (ally > 0) ? 0 : 1;
                s_state = (aliveTeam == s_hostSide) ? GAME_VICTORY : GAME_DEFEAT;
            }
        }
        else
        {
            const Agent *pa = Entity_GetAgent(player->agentId);
            if (pa == NULL)
            {
                s_state = GAME_DEFEAT; // HP hit zero or ring-out finished
            }
            else
            {
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
                if (phase != s_lastBossPhase)
                {
                    if (phase > 0)
                        AI_SpawnMinionWave(Boss_GetAgentId(), 3 + phase);
                    s_lastBossPhase = phase;
                }

                if (!Boss_IsAlive())
                    s_state = GAME_VICTORY;
            }
        }
    }
    else
    { // GAME_VICTORY / GAME_DEFEAT
        if (s_mode == GAME_MODE_TEAM_BATTLE && Net_ClientDrivesWorld())
        {
            // The host decides the rematch — its state dropping back to
            // INTRO/FIGHTING is the signal a new round started.
            int rs = Net_GetRemoteMatchState();
            if (rs == (int)GAME_ARENA_INTRO || rs == (int)GAME_FIGHTING)
                s_state = (GameState)rs;
        }
        if (IsKeyPressed(KEY_ENTER))
        {
            if (s_mode == GAME_MODE_TEAM_BATTLE && !Net_ClientDrivesWorld())
            {
                // Team battle rematch: same room, fresh round. Dead heroes
                // (host's own included, via ResetMatch) come back.
                Net_HostRespawnPeerHeroes();
                ResetMatch(player);
                return;
            }
            s_backToMenu = true;
            ResetMatch(player);
            return;
        }
    }

    // Outcome sync: the host tells the connected client how the match
    // stands (no-op offline / as client — the transport gates by mode).
    Net_HostSetMatchState((int)s_state);

    // One line per state change — the only reliable headless evidence of a
    // round's arc (net tests grep for it) — plus the outcome stinger.
    {
        static GameState s_prevLogged = (GameState)-1;
        if (s_state != s_prevLogged)
        {
            static const char *kNames[] = {"MENU", "INTRO", "FIGHTING", "VICTORY", "DEFEAT"};
            TraceLog(LOG_INFO, "[GAME] state -> %s", kNames[(int)s_state]);
            if (s_state == GAME_VICTORY)
                Audio_PlaySFX(SFX_VICTORY);
            else if (s_state == GAME_DEFEAT)
                Audio_PlaySFX(SFX_DEFEAT);
            s_prevLogged = s_state;
        }
    }

    // Thái Cực sting — the frame the local player crosses into the state.
    {
        static bool s_prevTaiji = false;
        bool now = Entity_IsTaijiActive(player->agentId);
        if (now && !s_prevTaiji)
            Audio_PlaySFX(SFX_TAIJI_ENTER);
        s_prevTaiji = now;
    }

    // Combat hits/clashes → SFX (host only; the client mirrors positions,
    // not combat events). Peek is non-draining, one frame's worth.
    if (!Net_ClientDrivesWorld())
    {
        const ClashEvent *evs = NULL;
        int n = Combat_PeekEvents(&evs);
        for (int i = 0; i < n; i++)
        {
            if (evs[i].outcome == CLASH_HIT_AGENT)
                Audio_PlaySFXAt(SFX_SKILL_HIT, evs[i].clashPoint);
            else if (evs[i].outcome == CLASH_A_WINS ||
                     evs[i].outcome == CLASH_B_WINS ||
                     evs[i].outcome == CLASH_MUTUAL_DESTROY)
                Audio_PlaySFXAt(SFX_CLASH, evs[i].clashPoint); // Đấu Pháp
        }
    }

    if (IsKeyDown(KEY_Q))
        s_camAngle -= 1.6f * dt;
    if (IsKeyDown(KEY_E))
        s_camAngle += 1.6f * dt;
    s_camDist -= GetMouseWheelMove() * 0.4f;
    if (s_camDist < 3.0f)
        s_camDist = 3.0f;
    if (s_camDist > 12.0f)
        s_camDist = 12.0f;

    float s = sinf(s_camAngle);
    float c = cosf(s_camAngle);

    // Module 4: movement/jump/dash/meditate/skill-cast all live in control/
    // now — this screen only forwards the camera and reads back the agent's
    // authoritative position (control writes it via Entity_SetPosition, and
    // entities owns it during jump arcs / dash bursts / knockback).
    // Intents only apply mid-fight — intro/end screens freeze the player.
    Control_SetCamera(s_camAngle, camera);
    UI_SetCamera(camera);
    UI_Update(dt);
    PlayerIntent intent = Control_ReadIntent();
    // Module 9 auto-targeting: an incoming enemy projectile (đối-đòn) or
    // the boss overrides the raw mouse aim — mobile-first UX (thiết kế §XI).
    if (s_state == GAME_FIGHTING && !UI_IsLoadoutOpen())
    {
        bool hasAuto = false;
        Vector3 autoPt = UI_GetAutoAimPoint(player->agentId, &hasAuto);
        if (hasAuto)
            intent.aimPoint = autoPt;

        // Swing damping: while an attack/cast animation plays, walking
        // drops to 35% so the feet stop sliding through the swing — the
        // "vừa đánh vừa chạy" root-motion feel without real root motion.
        if (s_swingSlowTimer > 0.0f)
            s_swingSlowTimer -= dt;
        Control_SetMoveSpeedMult(s_swingSlowTimer > 0.0f ? 0.35f : 1.0f);

        if (Net_ClientDrivesWorld())
        {
            // The host simulates our hero; snapshots move it back to us.
            Net_ClientSubmitIntent(&intent);
        }
        else
        {
            Control_Apply(&intent, dt);

            // Cast flourish: control casts silently (pure logic) — it
            // reports the fired skill back so the character swings.
            int castIdx = Control_ConsumeCastFired();
            if (castIdx >= 0)
            {
                float castSecs = Skill_GetCastAnimSeconds(castIdx);
                CharacterModel_TriggerAttackTimed(&player->anim, CHAR_ANIM_CAST, castSecs);
                s_swingSlowTimer = castSecs * 0.6f; // free up before the anim tail
                // Đợt A5: connected clients replay this cast as pure VFX.
                Net_HostNotifyCast(player->agentId, castIdx, intent.aimPoint);
                const Agent *pc = Entity_GetAgent(player->agentId);
                if (pc != NULL)
                    Audio_PlaySFXAt(pc->taijiActive ? SFX_CAST_TAIJI
                                                    : Audio_CastSfxForElement(pc->currentElement),
                                    player->position);
            }
        }
    }
    const Agent *selfAgent = Entity_GetAgent(player->agentId);
    if (selfAgent)
        player->position = selfAgent->position;

    float moveLen = sqrtf(intent.moveDir.x * intent.moveDir.x + intent.moveDir.y * intent.moveDir.y);
    CharacterModel_Update(&player->anim, dt, moveLen > 0.0001f);

    // Basic attack (đấm/đá/chưởng) — free, no cast time, spammable, auto-
    // targets via Entity_ExecuteBasicAttack (no enemy reference needed here
    // at all). Z / C / right-click all fire the same basic attack with a
    // RANDOM punch/kick/palm each press (matches sandbox's touch button —
    // per-type mapping waits for the real combo system). This is the real
    // production home for this input (see game_screen.h header) — sandbox
    // no longer has a PC-keyboard copy of this.
    // Melee rides PlayerIntent now (net-replicated): the swing anim + damping
    // always play locally for feedback; the gameplay hit runs locally only
    // when we own the simulation — a connected client's intent already went
    // to the host, which executes the hit there.
    bool basicAttackPressed = (s_state == GAME_FIGHTING) && !UI_IsLoadoutOpen() &&
                              intent.basicAttack > 0;

    if (basicAttackPressed)
    {
        BasicAttackType basicAttackType = (BasicAttackType)(intent.basicAttack - 1);
        CharacterAnimSlot animSlot = (basicAttackType == BASIC_ATTACK_PUNCH) ? CHAR_ANIM_PUNCH : (basicAttackType == BASIC_ATTACK_KICK) ? CHAR_ANIM_KICK
                                                                                                                                        : CHAR_ANIM_PALM;
        float swingSecs = Entity_GetBasicAttackSeconds(basicAttackType);
        CharacterModel_TriggerAttackTimed(&player->anim, animSlot, swingSecs);
        s_swingSlowTimer = swingSecs * 0.6f; // damp walking through the swing
        Audio_PlaySFXAt(SFX_MELEE_HIT, player->position);

        if (!Net_ClientDrivesWorld())
        {
            Vector3 targetPos, wallPos;
            int wallElement;
            bool gotWallBonus = Entity_ExecuteBasicAttack(player->agentId, basicAttackType, &targetPos, &wallPos, &wallElement);
            // targetPos is only written when auto-target found someone (stays
            // (0,0,0) otherwise) — turn to face the attack direction if so.
            if (targetPos.x != 0.0f || targetPos.y != 0.0f || targetPos.z != 0.0f)
            {
                Control_FaceTowards(targetPos);
            }
            if (gotWallBonus && wallElement == 3 /* Earth — only element with a real wall so far */)
            {
                // F0 purge: proc beam deleted, no one-shot successor. The
                // resonance keeps its impact.
                (void)wallPos;
                VFX_ComposeImpactPackage(targetPos, (Vector3){0.0f, 1.0f, 0.0f},
                                         VC_MAT_EARTH, 0.6f, 0.40f);
                AddFloatingText(targetPos, "Cong Huong Dat!", ELEMENT_COLOR_EARTH, 16.0f, 0.6f);
            }
        }
    }

    camera->target = (Vector3){
        player->position.x, player->position.y + 1.6f, player->position.z};
    camera->position = (Vector3){
        player->position.x + s * s_camDist,
        player->position.y + s_camDist * 0.85f,
        player->position.z + c * s_camDist};
}

static Color MinionElementColor(int elem)
{
    switch (elem)
    {
    case 0:
        return ELEMENT_COLOR_WATER;
    case 1:
        return ELEMENT_COLOR_WOOD;
    case 2:
        return ELEMENT_COLOR_FIRE;
    case 3:
        return ELEMENT_COLOR_EARTH;
    case 4:
        return ELEMENT_COLOR_METAL;
    default:
        return ELEMENT_COLOR_TAIJI;
    }
}

void GameScreen_Draw3D(const PlayerEntity *player)
{
    if (s_state == GAME_DEFEAT)
        return; // the fallen player isn't drawn

    // Minions (ARCH_MINION pool agents — ai/ is pure logic, render lives
    // here): small element-colored spirit orbs with a dark body, bobbing.
    for (int i = 0; i < MAX_AGENTS; i++)
    {
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->archetype != ARCH_MINION)
            continue;
        Color c = MinionElementColor(a->currentElement);
        float bob = 0.25f + 0.05f * sinf((float)GetTime() * 4.0f + (float)i);
        Vector3 body = {a->position.x, a->position.y + bob, a->position.z};
        Environment_DrawSmartShadow(a->position, ENV_SHAPE_SPHERE, 0.2f, 0.6f);
        DrawSphereEx(body, 0.18f, 8, 8,
                     (Color){(unsigned char)(20 + c.r / 6), (unsigned char)(20 + c.g / 6),
                             (unsigned char)(20 + c.b / 6), 255});
        DrawCircle3D(body, 0.26f, (Vector3){0, 1, 0}, (float)GetTime() * 90.0f + i * 40.0f, c);
    }

    // Other heroes (teammates + opponents: remote players and bots) and a
    // snapshot-mirrored boss (client has no boss/ module state — draw the
    // ARCH_BOSS agent as a dark core + element ring so the fight reads).
    // Every hero uses the SAME real character model as the local player —
    // an opponent must never read as a minion (a small orb). CharacterModel_
    // Draw updates+renders the shared mesh atomically, so drawing several in
    // a row is safe (each pose renders before the next Draw remaps it).
    const Agent *self = Entity_GetAgent(player->agentId);
    AgentTeam myTeam = self ? self->team : TEAM_ALLY;
    float frameDt = GetFrameTime();
    for (int i = 0; i < MAX_AGENTS; i++)
    {
        const Agent *a = Entity_GetAgent(i);
        if (!a || i == player->agentId)
            continue;
        if (a->archetype == ARCH_HERO)
        {
            Environment_DrawSmartShadow(a->position, ENV_SHAPE_SPHERE, 0.5f, 0.9f);
            // Ally = cool tint, enemy = warm tint — reads friend/foe at a
            // glance without labels (No Tutorial).
            Color tint = (a->team == myTeam) ? (Color){150, 200, 255, 255}
                                             : (Color){255, 170, 150, 255};
            if (CharacterModel_IsLoaded())
            {
                // Per-agent anim state: infer walk/idle + facing from the
                // position delta between frames (no intent stream here).
                float dx = a->position.x - s_remotePrevPos[i].x;
                float dz = a->position.z - s_remotePrevPos[i].z;
                float moved = sqrtf(dx * dx + dz * dz);
                if (!s_remoteAnimInit[i])
                {
                    CharacterModel_ResetState(&s_remoteAnim[i]);
                    s_remoteAnimInit[i] = true;
                }
                if (moved > 0.001f)
                    s_remoteYaw[i] = atan2f(dx, dz);
                CharacterModel_Update(&s_remoteAnim[i], frameDt, moved > 0.02f * frameDt * 60.0f);
                CharacterModel_Draw(&s_remoteAnim[i], a->position, s_remoteYaw[i], 1.0f, tint);
                s_remotePrevPos[i] = a->position;
            }
            else
            {
                DrawCharacter3D(a->position, 0.25f,
                                GetColor(0xD9B08CFF), GetColor(0x7A2E2EFF), GetColor(0xAAAAAAFF),
                                true, a->position);
            }
        }
        else if (a->archetype == ARCH_BOSS && !Boss_IsAlive())
        {
            Color c = MinionElementColor(a->currentElement);
            float bob = 0.9f + 0.12f * sinf((float)GetTime() * 1.3f);
            Vector3 core = {a->position.x, a->position.y + bob, a->position.z};
            Environment_DrawSmartShadow(a->position, ENV_SHAPE_SPHERE, 0.9f, 1.2f);
            DrawSphereEx(core, 0.55f, 12, 12,
                         (Color){(unsigned char)(10 + c.r / 8), (unsigned char)(10 + c.g / 8),
                                 (unsigned char)(10 + c.b / 8), 255});
            DrawCircle3D(core, 0.9f, (Vector3){0, 1, 0}, (float)GetTime() * 40.0f, c);
        }
    }

    Environment_DrawSmartShadow(player->position, ENV_SHAPE_SPHERE, 0.5f, 0.9f);
    if (CharacterModel_IsLoaded())
    {
        CharacterModel_Draw(&player->anim, player->position, Control_GetYaw(), 1.0f, WHITE);
    }
    else
    {
        DrawCharacter3D(player->position, 0.25f,
                        GetColor(0xFFD39BFF), GetColor(0x3B5998FF), GetColor(0xCCCCCCFF),
                        true, player->position);
    }

    // Meditation VFX component — drawn for every meditating hero (local + remote).
    // Pure visual; gameplay cancel/regen stays in entities/control.
    {
        float t = (float)GetTime();
        for (int i = 0; i < MAX_AGENTS; i++)
        {
            if (!Entity_IsMeditating(i))
                continue;
            const Agent *a = Entity_GetAgent(i);
            if (!a || a->archetype != ARCH_HERO)
                continue;
            Vector3 p = (i == player->agentId) ? player->position : a->position;
            // VFX_ComposeMeditate(p, Entity_GetMeditateProgress(i), t);
        }
    }
}

void GameScreen_DrawHUD(const PlayerEntity *player)
{
    const Agent *agent = Entity_GetAgent(player->agentId);
    float hp = agent ? agent->health : 0.0f;
    float maxHp = agent ? agent->maxHealth : 1.0f;
    float ratio = (maxHp > 0.0f) ? (hp / maxHp) : 0.0f;
    if (ratio < 0.0f)
        ratio = 0.0f;
    if (ratio > 1.0f)
        ratio = 1.0f;

    const int barX = 20, barY = 20, barW = 220, barH = 18;
    DrawRectangle(barX, barY, barW, barH, (Color){30, 30, 30, 220});
    DrawRectangle(barX, barY, (int)(barW * ratio), barH, (Color){190, 40, 40, 255});
    DrawRectangleLines(barX, barY, barW, barH, (Color){220, 220, 220, 180});

    float mana = agent ? agent->mana : 0.0f;
    float maxMana = agent ? agent->maxMana : 1.0f;
    float manaRatio = (maxMana > 0.0f) ? (mana / maxMana) : 0.0f;
    if (manaRatio < 0.0f)
        manaRatio = 0.0f;
    if (manaRatio > 1.0f)
        manaRatio = 1.0f;

    const int manaBarY = barY + barH + 6;
    DrawRectangle(barX, manaBarY, barW, barH, (Color){30, 30, 30, 220});
    DrawRectangle(barX, manaBarY, (int)(barW * manaRatio), barH, (Color){60, 90, 220, 255});
    DrawRectangleLines(barX, manaBarY, barW, barH, (Color){220, 220, 220, 180});

    // Module 9 overlay: skill slot chips + auto-aim reticle.
    UI_DrawOverlay(player->agentId);

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    // Boss HP bar (top center) while a boss lives.
    if (Boss_IsAlive())
    {
        const Agent *boss = Entity_GetAgent(Boss_GetAgentId());
        if (boss)
        {
            float bRatio = (boss->maxHealth > 0.0f) ? boss->health / boss->maxHealth : 0.0f;
            if (bRatio < 0.0f)
                bRatio = 0.0f;
            if (bRatio > 1.0f)
                bRatio = 1.0f;
            const int bw = 420, bh = 14, bx = sw / 2 - bw / 2, by = 16;
            DrawRectangle(bx, by, bw, bh, (Color){20, 20, 20, 220});
            DrawRectangle(bx, by, (int)(bw * bRatio), bh, (Color){150, 40, 170, 255});
            DrawRectangleLines(bx, by, bw, bh, (Color){220, 220, 220, 180});
            const char *bn = "HAC DIEN TON GIA";
            DrawText(bn, sw / 2 - MeasureText(bn, 16) / 2, by + bh + 4, 16, (Color){230, 230, 240, 255});
        }
    }

    // Team battle scoreboard (top center — the boss bar's slot; the two
    // never coexist): living hero count per side, colored per team.
    if (s_mode == GAME_MODE_TEAM_BATTLE)
    {
        int ally = GameRules_CountAliveHeroes(TEAM_ALLY);
        int enemy = GameRules_CountAliveHeroes(TEAM_ENEMY);
        const char *score = TextFormat("THANH LONG  %d — %d  BACH HO", ally, enemy);
        int scw = MeasureText(score, 22);
        DrawRectangle(sw / 2 - scw / 2 - 12, 12, scw + 24, 34, (Color){15, 15, 25, 200});
        DrawText(score, sw / 2 - scw / 2, 18, 22, (Color){225, 225, 235, 255});
    }

    // Online status strip (below the boss bar): the host shows its room
    // code until the opponent arrives; a joining client shows the handshake
    // wait (hero id assignment doubles as "snapshots are flowing").
    if (Net_GetMode() == NET_MODE_HOST && !Net_IsPeerConnected() && s_onlineCode[0] != '\0')
    {
        const char *t = TextFormat("MA PHONG: %s", s_onlineCode);
        int tw = MeasureText(t, 30);
        DrawRectangle(sw / 2 - tw / 2 - 14, 52, tw + 28, 66, (Color){15, 15, 25, 200});
        DrawText(t, sw / 2 - tw / 2, 60, 30, (Color){240, 220, 120, 255});
        const char *w = "DANG CHO DOI THU VAO PHONG...";
        DrawText(w, sw / 2 - MeasureText(w, 16) / 2, 96, 16, (Color){200, 200, 210, 255});
    }
    else if (Net_GetMode() == NET_MODE_CLIENT && Net_GetLocalHeroAgentId() < 0)
    {
        const char *w = "DANG KET NOI DEN HOST...";
        DrawText(w, sw / 2 - MeasureText(w, 22) / 2, 60, 22, (Color){240, 220, 120, 255});
    }

    // Match-state overlays (No Tutorial — one line each, no instructions
    // beyond the exit key).
    if (s_state == GAME_ARENA_INTRO)
    {
        const char *t = (s_mode == GAME_MODE_TEAM_BATTLE) ? "SONG DAU" : "HAC DIEN TON GIA";
        DrawText(t, sw / 2 - MeasureText(t, 40) / 2, sh / 2 - 60, 40, (Color){235, 230, 245, 255});
    }
    else if (s_state == GAME_VICTORY)
    {
        const char *t = "CHIEN THANG";
        DrawText(t, sw / 2 - MeasureText(t, 44) / 2, sh / 2 - 60, 44, (Color){240, 220, 120, 255});
        const char *hint = "ENTER";
        DrawText(hint, sw / 2 - MeasureText(hint, 18) / 2, sh / 2 - 8, 18, (Color){200, 200, 200, 255});
    }
    else if (s_state == GAME_DEFEAT)
    {
        const char *t = "THAT BAI";
        DrawText(t, sw / 2 - MeasureText(t, 44) / 2, sh / 2 - 60, 44, (Color){200, 70, 70, 255});
        const char *hint = "ENTER";
        DrawText(hint, sw / 2 - MeasureText(hint, 18) / 2, sh / 2 - 8, 18, (Color){200, 200, 200, 255});
    }
}

bool GameScreen_RequestedBackToMenu(void)
{
    bool req = s_backToMenu;
    s_backToMenu = false;
    return req;
}

void GameScreen_SetOnlineCode(const char *code)
{
    if (code == NULL)
    {
        s_onlineCode[0] = '\0';
        return;
    }
    strncpy(s_onlineCode, code, sizeof(s_onlineCode) - 1);
    s_onlineCode[sizeof(s_onlineCode) - 1] = '\0';
}
