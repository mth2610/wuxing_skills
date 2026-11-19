#include "game_screen.h"
#include "entities/entities.h"
#include "environment/environment_system.h"
#include "environment/env_shadow.h"
#include "skills/skill_manager.h"
#include "skills/skill_helper.h"
#include "core/composition/visual_composer.h"
#include "character/character_model.h"
#include "character/wardrobe_system.h"
#include "character/player_profile.h"
#include "control/control.h"
#include "boss/boss_system.h"
#include "maps/map_manager.h"
#include "maps/toolkit/map_props.h"
#include "game/game_rules.h"
#include "game/quest_datau.h"
#include "game/horde_renderer.h"
#include "open_world/ow_world.h"
#include "open_world/ow_loot.h"
#include "open_world/ow_quest_npc.h"
#include "open_world/ow_sandbox_modes.h"
#include "open_world/ow_weather.h"
#include "open_world/realms/ow_desert_earth.h"
#include "open_world/realms/ow_song_quao.h"
#include "ai/ai.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"
#include "ui/ui_hub.h"
#include "net/net_transport.h"
#include "combat/combat.h"
#include "audio/audio_system.h"
#include "sandbox/sandbox_core.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

#define GAME_DEFAULT_TILT_DEGREES 35.264f
#define GAME_DEFAULT_ZOOM_RATIO 1.8f

static float s_camAngle = 0.0f;
static float s_camDist = SANDBOX_CAMERA_DEFAULT_DISTANCE / GAME_DEFAULT_ZOOM_RATIO; // Zoom 1.8x default (10.0f)
static float s_camPitch = GAME_DEFAULT_TILT_DEGREES; // Tilt 35.264 deg default
static bool s_backToMenu = false;

// --- Module 7 match state ---
static GameState s_state = GAME_ARENA_INTRO;
static GameMode s_mode = GAME_MODE_BOSS; // Đợt A3 — main.c sets per entry
static int s_exploreRealm = 0;           // 0=Thủy, 1=Mộc, 2=Hỏa, 3=Thổ, 4=Kim
static char s_onlineCode[16] = {0};      // EOS room code shown while hosting
static float s_introTimer = 0.0f;
static int s_lastBossPhase = -1;      // minion waves trigger on phase change (M8)
static float s_swingSlowTimer = 0.0f; // movement damped while a swing plays
static float s_playerRespawnTimer = 0.0f;
static float s_pvpMatchTimer = 240.0f;
static float s_pillarCapture = 0.0f;
static float s_pillarPulseTimer = 0.0f;
static float s_minionWaveTimer = 18.0f;
static float s_spiritOrbTimer = 40.0f;
static bool  s_spiritOrbActive = false;
static Vector3 s_spiritOrbPos = { 0 };
static float s_spiritOrbBanner = 0.0f;
static float s_worldBossTimer = 75.0f;
static bool  s_worldBossActive = false;
static float s_worldBossBanner = 0.0f;
static int   s_tuongSinhLastElem = -1;
static int   s_tuongSinhChain = 0;
static float s_tuongSinhTimer = 0.0f;
static bool  s_wasExploreClient = false;
static float s_failoverBannerTimer = 0.0f;
static Camera3D s_hudCamera = { 0 };
static UITouchCombatInput s_gameTouchInput = {0};

static void BuildGameTouchSlots(const PlayerEntity *player, UITouchSkillSlot outSlots[5])
{
    const Agent *a = player ? Entity_GetAgent(player->agentId) : NULL;
    for (int k = 0; k < 5; k++)
    {
        outSlots[k].skillIndex = -1;
        outSlots[k].shortLabel[0] = '\0';
        outSlots[k].color = (Color){120, 180, 230, 255};
        outSlots[k].ready = false;
    }
    if (!a) return;

    for (int k = 0; k < AGENT_SKILL_SLOTS && k < 5; k++)
    {
        int idx = a->equippedSkills[k];
        if (idx >= 0)
        {
            outSlots[k].skillIndex = idx;
            UI_FormatShortSkillName(GetRegisteredSkillName(idx), outSlots[k].shortLabel, sizeof(outSlots[k].shortLabel));
            outSlots[k].color = GetRegisteredSkillColor(idx);
            outSlots[k].ready = SkillManager_CanCast(idx, player->agentId);
        }
    }
}

// Team battle: two spawn clusters facing each other across the plateau
// center; heroes fan out ±z within their cluster.
static const Vector3 TEAM_SPAWN[2] = {{86.0f, 0.0f, 150.0f}, {102.0f, 0.0f, 150.0f}};

// Per-agent render state for OTHER heroes (remote players + bots), so each
// gets its own walk/idle animation + facing on the shared character model.
static CharacterAnimState s_remoteAnim[MAX_AGENTS];
static Vector3 s_remotePrevPos[MAX_AGENTS];
static float s_remoteYaw[MAX_AGENTS];
static bool s_remoteAnimInit[MAX_AGENTS];
static ModularInstance s_remoteModular[MAX_AGENTS];
static bool s_remoteModularInit[MAX_AGENTS];

static void EnsureRemoteModularInit(int agentId)
{
    if (agentId < 0 || agentId >= MAX_AGENTS || s_remoteModularInit[agentId])
        return;
    ModularInstance_Init(&s_remoteModular[agentId], MODULAR_TIER_HERO);
    s_remoteModular[agentId].useUniqueGpuBuffers = true;
    PlayerWardrobe rw;
    PlayerWardrobe_Init(&rw, "Remote_Hero");
    PlayerWardrobe_ApplyToInstance(&rw, &s_remoteModular[agentId]);
    s_remoteModularInit[agentId] = true;
}

static uint32_t ComputeWardrobeHash(const PlayerWardrobe *pw)
{
    uint32_t h = 2166136261u;
    if (!pw) return h;
    for (int s = 0; s < MODULAR_SLOT_COUNT; s++)
    {
        for (const char *p = pw->equippedIds[s]; *p; p++)
        {
            h ^= (uint8_t)*p;
            h *= 16777619u;
        }
        h ^= (uint32_t)(s + 1) * 31u;
    }
    return h;
}

static void SyncRemoteWardrobeAndCasts(int localAgentId)
{
    if (Net_GetMode() == NET_MODE_OFF)
        return;

    static uint32_t s_lastSentHash = 0;
    static int s_lastPeerCount = -1;
    static int s_lastAgentId = -1;
    const PlayerWardrobe *localPw = Wardrobe_GetGlobalPlayer();
    int peers = Net_GetPeerCount();
    if (localPw && localAgentId >= 0)
    {
        uint32_t h = ComputeWardrobeHash(localPw);
        if (h != s_lastSentHash || peers != s_lastPeerCount || localAgentId != s_lastAgentId)
        {
            uint8_t indices[NET_WARDROBE_SLOTS];
            memset(indices, 0xFF, sizeof(indices));
            int catCount = Wardrobe_GetCatalogCount();
            for (int s = 0; s < MODULAR_SLOT_COUNT && s < NET_WARDROBE_SLOTS; s++)
            {
                const char *id = localPw->equippedIds[s];
                if (!id || !id[0]) continue;
                for (int c = 0; c < catCount && c < 255; c++)
                {
                    const WardrobeCatalogItem *ci = Wardrobe_GetCatalogItemByIndex(c);
                    if (ci && strcmp(ci->id, id) == 0)
                    {
                        indices[s] = (uint8_t)c;
                        break;
                    }
                }
            }
            Net_SendWardrobe(localAgentId, indices, MODULAR_SLOT_COUNT);
            s_lastSentHash = h;
            s_lastPeerCount = peers;
            s_lastAgentId = localAgentId;
        }
    }

    NetWardrobeUpdate wUpdates[8];
    int wn = Net_PollWardrobeUpdates(wUpdates, 8);
    for (int k = 0; k < wn; k++)
    {
        int aid = wUpdates[k].agentId;
        if (aid < 0 || aid >= MAX_AGENTS || aid == localAgentId)
            continue;
        EnsureRemoteModularInit(aid);
        PlayerWardrobe rpw = {0};
        snprintf(rpw.playerId, sizeof(rpw.playerId), "Remote_%d", aid);
        for (int s = 0; s < wUpdates[k].slotCount && s < MODULAR_SLOT_COUNT; s++)
        {
            uint8_t cidx = wUpdates[k].catalogIndices[s];
            if (cidx == 0xFF) continue;
            const WardrobeCatalogItem *ci = Wardrobe_GetCatalogItemByIndex((int)cidx);
            if (ci)
            {
                strncpy(rpw.equippedIds[s], ci->id, WARDROBE_ID_MAX_LEN - 1);
                rpw.equippedIds[s][WARDROBE_ID_MAX_LEN - 1] = '\0';
            }
        }
        PlayerWardrobe_ApplyToInstance(&rpw, &s_remoteModular[aid]);
    }

    NetCastEvent casts[16];
    int cn = Net_PollCastEvents(casts, 16);
    for (int k = 0; k < cn; k++)
    {
        int aid = casts[k].agentId;
        if (aid < 0 || aid >= MAX_AGENTS || aid == localAgentId)
            continue;
        if (!s_remoteAnimInit[aid])
        {
            CharacterModel_ResetState(&s_remoteAnim[aid]);
            s_remoteAnimInit[aid] = true;
        }
        const Agent *ca = Entity_GetAgent(aid);
        if (ca)
        {
            float dx = casts[k].aim.x - ca->position.x;
            float dz = casts[k].aim.z - ca->position.z;
            if (dx * dx + dz * dz > 0.001f)
                s_remoteYaw[aid] = atan2f(dx, dz);
        }
        bool hasSword = s_remoteModularInit[aid] &&
                        (s_remoteModular[aid].loadout.itemIndices[MODULAR_SLOT_WEAPON_R] >= 0);
        CharacterModel_Trigger(&s_remoteAnim[aid], hasSword ? CHAR_ANIM_SWORD_SLASH : CHAR_ANIM_CAST);
    }
}

// The match runs on SONG_QUAO — the celestial floating island. Ring-out
// bounds + player placement are applied in the INTRO tick (only while this
// screen is ACTIVE — the bounds are global, and the sandbox needs the
// DEFAULT_ARENA circle back; main.c restores it on back-to-menu).
#define MATCH_MAP_NAME "SONG_QUAO"
static const Vector3 MATCH_ARENA_CENTER = {94.15f, 0.0f, 150.0f};
static const float MATCH_ARENA_RADIUS = 75.0f;
static const Vector3 PLAYER_SPAWN = {90.0f, 0.0f, 150.0f};
static const Vector3 BOSS_SPAWN = {98.0f, 0.0f, 150.0f};
static const float INTRO_SECONDS = 2.0f;

static int GameSkillElementByName(const char *name)
{
    if (!name) return 0;
    if (strcmp(name, "INFERNO_SURGE") == 0 || strcmp(name, "FIRE") == 0) return 2;
    if (strcmp(name, "ICE_CRYSTAL_LANCE") == 0 || strcmp(name, "TIDAL_STREAM") == 0 ||
        strcmp(name, "WATER_STREAM") == 0) return 0;
    if (strcmp(name, "VINE_SNARE") == 0) return 1;
    if (strcmp(name, "EARTH_DISC_VOLLEY") == 0 || strcmp(name, "EARTH_PILLAR_PRISON") == 0 ||
        strcmp(name, "SAND_STORM") == 0) return 3;
    if (strcmp(name, "METAL_BLADE_STORM") == 0) return 4;
    return 0;
}

static bool IsModePvP(GameMode m)
{
    return m == GAME_MODE_TEAM_BATTLE ||
           m == GAME_MODE_PVP_ESCORT_1V1 ||
           m == GAME_MODE_PVP_ESCORT_2V2;
}

static int GameSkillLevelProvider(int skillIndex)
{
    return PlayerProfile_GetEffectiveSkillLevel(skillIndex, IsModePvP(s_mode));
}

static void GameSkillScalingProvider(int skillIndex, int *outLevel, int *outMilestone,
                                     int *outQuantity, float *outSizeScale,
                                     float *outDamageMult, float *outCooldownMult)
{
    PlayerProfile_GetSkillScaling(skillIndex, IsModePvP(s_mode),
                                  outLevel, outMilestone, outQuantity,
                                  outSizeScale, outDamageMult, outCooldownMult);
}

static void ApplyModeBalanceToPlayer(int agentId)
{
    if (IsModePvP(s_mode)) {
        Entity_SetPvEBonuses(agentId, NULL);
        return;
    }
    const PlayerWardrobe *pw = Wardrobe_GetGlobalPlayer();
    if (!pw) {
        Entity_SetPvEBonuses(agentId, NULL);
        return;
    }
    WardrobeItemStats ws = PlayerWardrobe_GetTotalStats(pw);
    float dmgOut = 1.0f + fmaxf(0.0f, ws.elementalBonus);
    float dmgTaken = fmaxf(0.25f, 1.0f - fmaxf(0.0f, ws.elementalResist));
    float moveSpd = (ws.speedMultiplier > 0.0f) ? ws.speedMultiplier : 1.0f;
    if (s_mode == GAME_MODE_EXPLORE && s_exploreRealm >= 0 && s_exploreRealm <= 3) {
        if (OW_GetMightTimer() > 0.0f) {
            dmgOut += 0.35f;
            dmgTaken = fmaxf(0.25f, dmgTaken - 0.30f);
        }
        const Agent *pa = Entity_GetAgent(agentId);
        dmgOut *= OWSandbox_GetSlaughterDamageMult(pa);
        moveSpd *= OWWeather_GetMoveSpeedMult() * OWSandbox_GetSlaughterSpeedMult(pa);
    }
    AgentPvEBonuses b = {
        .bonusMaxHealth  = ws.bonusHealth,
        .bonusMaxMana    = ws.bonusMana,
        .moveSpeedMult   = moveSpd,
        .damageOutMult   = dmgOut,
        .damageTakenMult = dmgTaken,
        .manaRegenMult   = 1.0f + fmaxf(0.0f, ws.manaRegenBonus),
    };
    Entity_SetPvEBonuses(agentId, &b);
}

// Default loadout (keys 1-5) from PlayerProfile, falling back to 5 starter skills.
static void EquipDefaultLoadout(int agentId)
{
    static const struct
    {
        const char *name;
        int element;
    } kLoadout[AGENT_SKILL_SLOTS] = {
        {"ICE_CRYSTAL_LANCE", 0},  // Thủy
        {"INFERNO_SURGE", 2},      // Hỏa
        {"EARTH_DISC_VOLLEY", 3},  // Thổ
        {"METAL_BLADE_STORM", 4},  // Kim
        {"VINE_SNARE", 1},         // Mộc
    };
    const PlayerProfile *prof = PlayerProfile_GetGlobal();
    for (int slot = 0; slot < AGENT_SKILL_SLOTS; slot++)
    {
        const char *skName = (prof && prof->equippedSkillNames[slot][0] != '\0')
            ? prof->equippedSkillNames[slot] : kLoadout[slot].name;
        int idx = Skill_GetIndexByName(skName);
        int elem = (prof && prof->equippedSkillNames[slot][0] != '\0')
            ? GameSkillElementByName(skName) : kLoadout[slot].element;
        if (idx < 0) {
            idx = Skill_GetIndexByName(kLoadout[slot].name);
            elem = kLoadout[slot].element;
        }
        if (idx >= 0)
            Entity_SetEquippedSkill(agentId, slot, idx, elem);
    }
}

static const char *ExploreMapNameForRealm(int realm)
{
    switch (realm) {
        case 0: return "SONG_QUAO";     // Thủy
        case 1: return "SONG_QUAO";     // Mộc
        case 2: return "NINH_THUAN";    // Hỏa
        case 3: return "NINH_THUAN";    // Thổ
        case 4: return "DEFAULT_ARENA"; // Kim
        default: return "SONG_QUAO";
    }
}

static const char *ExploreRealmTitle(int realm)
{
    static const char *kTitles[5] = {
        "HAN THUY DAM (THUY)",
        "THANH MOC LAM (MOC)",
        "XICH HOA SON (HOA)",
        "HOANG THO DAI (THO)",
        "KIM TUYET COC (KIM)"
    };
    return kTitles[(realm >= 0 && realm < 5) ? realm : 0];
}

static void ResetMatch(PlayerEntity *player)
{
    s_state = GAME_ARENA_INTRO;
    s_introTimer = INTRO_SECONDS;
    s_lastBossPhase = -1;
    s_swingSlowTimer = 0.0f;
    s_playerRespawnTimer = 0.0f;
    s_pvpMatchTimer = 240.0f;
    s_pillarCapture = 0.0f;
    s_pillarPulseTimer = 0.0f;
    s_minionWaveTimer = 18.0f;
    s_spiritOrbTimer = 40.0f;
    s_spiritOrbActive = false;
    s_spiritOrbBanner = 0.0f;
    s_worldBossTimer = 75.0f;
    s_worldBossActive = false;
    s_worldBossBanner = 0.0f;
    s_tuongSinhLastElem = -1;
    s_tuongSinhChain = 0;
    s_tuongSinhTimer = 0.0f;
    s_wasExploreClient = false;
    s_failoverBannerTimer = 0.0f;
    Net_SetExploreMode(s_mode == GAME_MODE_EXPLORE);
    if (UI_IsLoadoutOpen())
        UI_ToggleLoadout();
    if (UI_Hub_IsModalOpen())
        UI_Hub_CloseModal();
    AI_ClearMonsterCamps();
    AI_ClearHeroBots();
    for (int i = 0; i < MAX_AGENTS; i++)
    {
        s_remoteAnimInit[i] = false;
        s_remoteModularInit[i] = false;
    }

    if (Entity_GetAgent(player->agentId) == NULL)
    {
        Vector3 spawn = PLAYER_SPAWN;
        spawn.y = MapManager_GetGroundHeightAt(spawn.x, spawn.z);
        player->agentId = Entity_SpawnAgent(spawn, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
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
    Control_SetSkillLevelProvider(GameSkillLevelProvider);
    Control_SetSkillScalingProvider(GameSkillScalingProvider);

    EquipDefaultLoadout(player->agentId);
    ApplyModeBalanceToPlayer(player->agentId);

    Boss_ClearAll();
}

GameState GameScreen_GetState(void)
{
    return s_state;
}

void GameScreen_SetMode(GameMode mode) { s_mode = mode; }
GameMode GameScreen_GetMode(void) { return s_mode; }
void GameScreen_SetExploreRealm(int realmId) { s_exploreRealm = (realmId >= 0 && realmId < 5) ? realmId : 0; }
int  GameScreen_GetExploreRealm(void) { return s_exploreRealm; }

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
        pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
        Entity_SetPosition(i, pos);
        placed[t]++;
    }
}

void GameScreen_Init(PlayerEntity *player)
{
    s_camAngle = 0.0f;
    s_camDist = SANDBOX_CAMERA_DEFAULT_DISTANCE / GAME_DEFAULT_ZOOM_RATIO; // Zoom 1.8x default (10.0f)
    s_camPitch = GAME_DEFAULT_TILT_DEGREES; // Tilt 35.264 deg default
    s_backToMenu = false;
    UI_Init();
    ResetMatch(player);
}

void GameScreen_Update(PlayerEntity *player, Camera3D *camera, float dt)
{
    if (IsKeyPressed(KEY_ESCAPE))
    {
        if (UI_Hub_IsModalOpen())
        {
            UI_Hub_CloseModal();
            EquipDefaultLoadout(player->agentId);
            ApplyModeBalanceToPlayer(player->agentId);
            return;
        }
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

    // TAB opens full Global Hub Modal in Explore Mode, or quick Loadout in PvP
    if (s_state == GAME_FIGHTING && IsKeyPressed(KEY_TAB))
    {
        if (s_mode == GAME_MODE_EXPLORE)
        {
            if (UI_Hub_IsModalOpen()) {
                UI_Hub_CloseModal();
                EquipDefaultLoadout(player->agentId);
                ApplyModeBalanceToPlayer(player->agentId);
            } else {
                UI_Hub_OpenModal(0);
            }
        }
        else
        {
            UI_ToggleLoadout();
        }
    }

    // --- Match state machine (Module 7) ---
    if (s_mode == GAME_MODE_EXPLORE && s_exploreRealm >= 0 && s_exploreRealm <= 3 && s_state == GAME_FIGHTING)
    {
        OW_DrawHUD(player, s_hudCamera);
        int peerCount = (Net_GetMode() != NET_MODE_OFF) ? (Net_GetPeerCount() + 1) : 1;
        if (peerCount > NET_EXPLORE_MAX_PLAYERS) peerCount = NET_EXPLORE_MAX_PLAYERS;
        DrawText(TextFormat("CO-OP EOS P2P: %d/%d", peerCount, NET_EXPLORE_MAX_PLAYERS),
                 20, 98, 13, (Color){ 140, 235, 195, 255 });
    }

    if (s_state == GAME_ARENA_INTRO)
    {
        static MapScaleMode s_lastAppliedScale = MAP_SCALE_MOBA;
        MapScaleMode wantScale = (s_mode == GAME_MODE_EXPLORE && s_exploreRealm != 4)
            ? MAP_SCALE_OPEN_WORLD : MAP_SCALE_MOBA;
        MapManager_SetScaleMode(wantScale);

        const char *targetMap = (s_mode == GAME_MODE_EXPLORE)
            ? ExploreMapNameForRealm(s_exploreRealm) : MATCH_MAP_NAME;
        if (strcmp(MapManager_GetName(MapManager_GetActiveIndex()), targetMap) != 0 ||
            s_lastAppliedScale != wantScale)
        {
            s_lastAppliedScale = wantScale;
            for (int i = 0; i < MapManager_GetCount(); i++)
            {
                if (strcmp(MapManager_GetName(i), targetMap) == 0)
                {
                    MapManager_SetActiveIndex(i);
                    break;
                }
            }
        }
        if (s_mode == GAME_MODE_EXPLORE ||
            s_mode == GAME_MODE_PVP_ESCORT_1V1 ||
            s_mode == GAME_MODE_PVP_ESCORT_2V2 ||
            s_mode == GAME_MODE_TEAM_BATTLE)
        {
            Environment_ApplyMoonlightNight();
        }
        else
        {
            Environment_SetNightMode(false);
        }
        if (s_mode == GAME_MODE_EXPLORE)
        {
            Vector3 mc = {0};
            float mr = 30.0f;
            MapManager_GetActiveBounds(&mc, &mr);
            Entity_SetArenaBounds(mc, mr);
            float rMinX, rMaxX, rMinZ, rMaxZ;
            if (MapManager_GetActivePlayableRect(&rMinX, &rMaxX, &rMinZ, &rMaxZ)) {
                Entity_SetPlayableRect(rMinX, rMaxX, rMinZ, rMaxZ);
            } else {
                Entity_ClearPlayableRect();
            }
            Vector3 sp = MapManager_GetActiveSpawnPoint();
            player->position = sp;
            Entity_SetPosition(player->agentId, sp);
        }
        else
        {
            Entity_ClearPlayableRect();
            Entity_SetArenaBounds(MATCH_ARENA_CENTER, MATCH_ARENA_RADIUS);
        }
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
        else if (s_mode == GAME_MODE_PVP_ESCORT_1V1 || s_mode == GAME_MODE_PVP_ESCORT_2V2)
        {
            Vector3 spA = TEAM_SPAWN[0];
            spA.y = MapManager_GetGroundHeightAt(spA.x, spA.z);
            player->position = spA;
            Entity_SetPosition(player->agentId, spA);
            Entity_SetPvEBonuses(player->agentId, NULL);
        }
        else if (s_mode != GAME_MODE_EXPLORE)
        {
            // Hold the player at the spawn point through the title card.
            player->position = PLAYER_SPAWN;
            player->position.y = MapManager_GetGroundHeightAt(player->position.x, player->position.z);
            Entity_SetPosition(player->agentId, player->position);
        }
        s_introTimer -= dt;
        if (s_introTimer <= 0.0f)
        {
            if (s_mode == GAME_MODE_BOSS)
            {
                Vector3 bossSpawn = BOSS_SPAWN;
                bossSpawn.y = MapManager_GetGroundHeightAt(bossSpawn.x, bossSpawn.z);
                Boss_Spawn(&BOSS_MOC_LINH, bossSpawn, TEAM_ENEMY);
            }
            else if (s_mode == GAME_MODE_EXPLORE)
            {
                if (s_exploreRealm == 2) MapProp_SetGrassState(MAP_GRASS_STATE_SCORCHED, 0.85f);
                else if (s_exploreRealm == 3) MapProp_SetGrassState(MAP_GRASS_STATE_DRY, 0.0f);
                else MapProp_SetGrassState(MAP_GRASS_STATE_LUSH, 0.0f);

                Vector3 sp = MapManager_GetActiveSpawnPoint();
                DaTau_Init(s_exploreRealm);
                if (s_exploreRealm >= 0 && s_exploreRealm <= 3) {
                    const OWRealmDef *rdef = (s_exploreRealm <= 1)
                        ? OWRealm_GetSongQuao(s_exploreRealm)
                        : OWRealm_GetDesertEarth();
                    OW_Init(rdef, !Net_ClientDrivesWorld());
                } else {
                    Vector3 ac = {0};
                    float ar = 20.0f;
                    MapManager_GetActiveBounds(&ac, &ar);
                    float campDist = (ar > 25.0f) ? 11.0f : 7.0f;
                    AI_SpawnMonsterCamp((Vector3){ sp.x + campDist, 0, sp.z }, 3, s_exploreRealm, MONSTER_SWARM_MELEE);
                    AI_SpawnMonsterCamp((Vector3){ sp.x - campDist, 0, sp.z }, 3, s_exploreRealm, MONSTER_SWARM_MELEE);
                    AI_SpawnMonsterCamp((Vector3){ sp.x, 0, sp.z + campDist }, 2, s_exploreRealm, MONSTER_RANGED_CASTER);
                    AI_SpawnMonsterCamp((Vector3){ sp.x, 0, sp.z - campDist }, 1, s_exploreRealm, MONSTER_ELITE_GUARD);
                }
            }
            else if (s_mode == GAME_MODE_PVP_ESCORT_1V1 || s_mode == GAME_MODE_PVP_ESCORT_2V2)
            {
                int teamSize = (s_mode == GAME_MODE_PVP_ESCORT_2V2) ? 2 : 1;
                if (!Net_ClientDrivesWorld())
                {
                    while (GameRules_CountAliveHeroes(TEAM_ALLY) < teamSize)
                    {
                        int b = AI_SpawnHeroBot(TEAM_SPAWN[0], TEAM_ALLY);
                        if (b < 0) break;
                        EquipDefaultLoadout(b);
                    }
                    while (GameRules_CountAliveHeroes(TEAM_ENEMY) < teamSize)
                    {
                        int b = AI_SpawnHeroBot(TEAM_SPAWN[1], TEAM_ENEMY);
                        if (b < 0) break;
                        EquipDefaultLoadout(b);
                    }
                    PlaceHeroesAtTeamSpawns();

                    Vector3 bPosA = { 78.0f, 0.0f, 142.0f };
                    bPosA.y = MapManager_GetGroundHeightAt(bPosA.x, bPosA.z);
                    Boss_SpawnSlot(0, &BOSS_MOC_LINH, bPosA, TEAM_ALLY);
                    BossRoute routeA = {
                        .waypoints = {
                            { 84.0f, 0.0f, 138.0f },
                            { 90.0f, 0.0f, 142.0f },
                            MATCH_ARENA_CENTER
                        },
                        .waypointCount = 3,
                        .marchSpeedMps = 1.25f
                    };
                    Boss_SetRoute(0, &routeA);

                    Vector3 bPosB = { 110.0f, 0.0f, 158.0f };
                    bPosB.y = MapManager_GetGroundHeightAt(bPosB.x, bPosB.z);
                    Boss_SpawnSlot(1, &BOSS_HAC_DIEN_TON_GIA, bPosB, TEAM_ENEMY);
                    BossRoute routeB = {
                        .waypoints = {
                            { 104.0f, 0.0f, 162.0f },
                            { 98.0f, 0.0f, 158.0f },
                            MATCH_ARENA_CENTER
                        },
                        .waypointCount = 3,
                        .marchSpeedMps = 1.25f
                    };
                    Boss_SetRoute(1, &routeB);
                }
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
        static int s_syncedClientHeroId = -1;
        if (!Net_ClientDrivesWorld())
            s_syncedClientHeroId = -1;
        if (Net_ClientDrivesWorld())
        {
            s_wasExploreClient = (s_mode == GAME_MODE_EXPLORE);
            int hid = Net_GetLocalHeroAgentId();
            if (hid >= 0)
            {
                player->agentId = hid;
                const Agent *ha = Entity_GetAgent(hid);
                if (ha != NULL)
                {
                    bool allEmpty = true;
                    for (int sl = 0; sl < AGENT_SKILL_SLOTS; sl++)
                    {
                        if (ha->equippedSkills[sl] >= 0) { allEmpty = false; break; }
                    }
                    if (hid != s_syncedClientHeroId || allEmpty)
                    {
                        if (hid != s_syncedClientHeroId)
                            Control_Init(hid);
                        s_syncedClientHeroId = hid;
                        EquipDefaultLoadout(hid);
                        const Agent *syncedA = Entity_GetAgent(hid);
                        if (syncedA != NULL)
                        {
                            for (int sl = 0; sl < AGENT_SKILL_SLOTS; sl++)
                                Net_ClientSendLoadout(sl, syncedA->equippedSkills[sl], syncedA->equippedElements[sl]);
                        }
                    }
                }
            }
            if (s_mode == GAME_MODE_EXPLORE)
            {
                DaTau_Update(dt);
                DaTau_TickZoneHold(dt, Entity_GetAgent(player->agentId) != NULL);
                if (!Net_IsPeerConnected())
                {
                    // Seamless Host Failover: keep player & local profile intact,
                    // transition to solo local simulation with fresh monster camps!
                    Net_Stop();
                    s_wasExploreClient = false;
                    s_failoverBannerTimer = 4.0f;
                    Vector3 curPos = player->position;
                    player->agentId = Entity_SpawnAgent(curPos, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
                    Control_Init(player->agentId);
                    EquipDefaultLoadout(player->agentId);
                    ApplyModeBalanceToPlayer(player->agentId);
                    Vector3 sp = MapManager_GetActiveSpawnPoint();
                    AI_SpawnMonsterCamp((Vector3){ sp.x + 10.0f, 0, sp.z }, 3, s_exploreRealm, MONSTER_SWARM_MELEE);
                    AI_SpawnMonsterCamp((Vector3){ sp.x - 10.0f, 0, sp.z }, 3, s_exploreRealm, MONSTER_SWARM_MELEE);
                    AI_SpawnMonsterCamp((Vector3){ sp.x, 0, sp.z + 10.0f }, 2, s_exploreRealm, MONSTER_RANGED_CASTER);
                    AI_SpawnMonsterCamp((Vector3){ sp.x, 0, sp.z - 10.0f }, 1, s_exploreRealm, MONSTER_ELITE_GUARD);
                }
                NetKillReward rewards[16];
                int nr = Net_ClientPollKillRewards(rewards, 16);
                if (nr > 0)
                {
                    PlayerProfile *prof = PlayerProfile_GetGlobal();
                    const Agent *me = Entity_GetAgent(player->agentId);
                    for (int r = 0; r < nr; r++)
                    {
                        prof->gold += rewards[r].gold;
                        if (rewards[r].isBoss || rewards[r].monsterKind == MONSTER_ELITE_GUARD)
                            prof->elementalStones[s_exploreRealm % 5] += 1;
                        if (me && me->equippedSkills[0] >= 0)
                            PlayerProfile_AddSkillXp(me->equippedSkills[0], (uint32_t)rewards[r].xp);
                        DaTau_OnMonsterKilled(rewards[r].monsterKind, rewards[r].element, rewards[r].isBoss);
                    }
                    PlayerProfile_SaveGlobal();
                }
            }
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
        else if (s_mode == GAME_MODE_EXPLORE)
        {
            DaTau_Update(dt);
            if (s_exploreRealm >= 0 && s_exploreRealm <= 3) {
                OW_Update(player, dt);
                if (Entity_GetAgent(player->agentId))
                    ApplyModeBalanceToPlayer(player->agentId);
            }
            if (s_worldBossBanner > 0.0f) s_worldBossBanner -= dt;
            if (IsKeyPressed(KEY_N)) {
                DaTau_SkipWithToken();
            }

            // World Boss Kỳ Ngộ Giang Hồ every 75s
            if (!s_worldBossActive)
            {
                s_worldBossTimer -= dt;
                if (s_worldBossTimer <= 0.0f)
                {
                    Vector3 sp = MapManager_GetActiveSpawnPoint();
                    Vector3 wbPos = (s_exploreRealm == 3) ? (Vector3){ 65.0f, 0.0f, 95.0f }
                                                          : (Vector3){ sp.x + 8.0f, 0.0f, sp.z + 6.0f };
                    wbPos.y = MapManager_GetGroundHeightAt(wbPos.x, wbPos.z);
                    if (Boss_SpawnSlot(0, &BOSS_HAC_DIEN_TON_GIA, wbPos, TEAM_ENEMY) >= 0)
                    {
                        s_worldBossActive = true;
                        s_worldBossBanner = 4.0f;
                    }
                    s_worldBossTimer = 75.0f;
                }
            }
            else if (!Boss_IsSlotAlive(0))
            {
                s_worldBossActive = false;
                PlayerProfile *prof = PlayerProfile_GetGlobal();
                prof->gold += 400;
                prof->daTauPoints += 100;
                prof->daTauTokens += 1;
                prof->elementalStones[s_exploreRealm % 5] += 2;
                const Agent *pa0 = Entity_GetAgent(player->agentId);
                if (pa0 && pa0->equippedSkills[0] >= 0)
                    PlayerProfile_AddSkillXp(pa0->equippedSkills[0], 250u);
                DaTau_OnMonsterKilled(MONSTER_ELITE_GUARD, s_exploreRealm, true);
                Net_HostNotifyKillReward(MONSTER_ELITE_GUARD, s_exploreRealm, true, 400, 250);
                PlayerProfile_SaveGlobal();
                s_worldBossBanner = 4.0f;
            }

            const Agent *pa = Entity_GetAgent(player->agentId);
            if (s_failoverBannerTimer > 0.0f) s_failoverBannerTimer -= dt;
            DaTau_TickZoneHold(dt, pa != NULL);
            if (pa == NULL)
            {
                s_playerRespawnTimer += dt;
                if (s_playerRespawnTimer >= 5.0f)
                {
                    s_playerRespawnTimer = 0.0f;
                    Vector3 sp = MapManager_GetActiveSpawnPoint();
                    player->agentId = Entity_SpawnAgent(sp, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
                    Control_Init(player->agentId);
                    EquipDefaultLoadout(player->agentId);
                    ApplyModeBalanceToPlayer(player->agentId);
                }
            }
            else
            {
                NatureZoneType zone = Map_QueryZoneAt(pa->position);
                Control_SetCastCooldownMult(GameRules_CooldownMult(pa->currentElement, zone));
                Entity_SetStealth(player->agentId, GameRules_GrantsStealth(pa->currentElement, zone));

                MonsterKillEvent kills[64];
                int nk = AI_PollMonsterKills(kills, 64);
                if (nk > 0)
                {
                    OW_RecordKills(nk);
                    PlayerProfile *prof = PlayerProfile_GetGlobal();
                    int orbBudget = 10;
                    for (int k = 0; k < nk; k++)
                    {
                        int goldGain = (kills[k].kind == MONSTER_ELITE_GUARD) ? 60 : 12;
                        uint32_t xpGain = (kills[k].kind == MONSTER_ELITE_GUARD) ? 75u : 18u;
                        if (s_exploreRealm >= 0 && s_exploreRealm <= 3) {
                            if (orbBudget > 0) {
                                OWLoot_SpawnOrb(kills[k].pos, OW_LOOT_SKILL_XP, (int)xpGain);
                                if ((k & 1) == 0 || kills[k].kind == MONSTER_ELITE_GUARD) {
                                    OWLoot_SpawnOrb(kills[k].pos, OW_LOOT_GOLD, goldGain);
                                }
                                orbBudget--;
                            } else {
                                prof->gold += goldGain;
                                int skIdx = pa->equippedSkills[k % AGENT_SKILL_SLOTS];
                                if (skIdx >= 0) PlayerProfile_AddSkillXp(skIdx, xpGain);
                            }
                        } else {
                            prof->gold += goldGain;
                            int skIdx = pa->equippedSkills[k % AGENT_SKILL_SLOTS];
                            if (skIdx >= 0) PlayerProfile_AddSkillXp(skIdx, xpGain);
                        }
                        if (kills[k].kind == MONSTER_ELITE_GUARD)
                            prof->elementalStones[s_exploreRealm % 5] += 1;
                        DaTau_OnMonsterKilled((int)kills[k].kind, kills[k].element, false);
                        Net_HostNotifyKillReward((int)kills[k].kind, kills[k].element, false, goldGain, (int)xpGain);
                    }
                    PlayerProfile_SaveGlobal();
                }
            }
        }
        else if (s_mode == GAME_MODE_PVP_ESCORT_1V1 || s_mode == GAME_MODE_PVP_ESCORT_2V2)
        {
            s_pvpMatchTimer -= dt;
            if (s_pvpMatchTimer < 0.0f) s_pvpMatchTimer = 0.0f;

            const Agent *pa = Entity_GetAgent(player->agentId);
            if (pa == NULL)
            {
                if (s_playerRespawnTimer == 0.0f)
                {
                    int allyBoss = Boss_GetSlotAgentId(0);
                    const Agent *ba = Entity_GetAgent(allyBoss);
                    if (ba)
                    {
                        Entity_ApplyDamage(allyBoss, ba->maxHealth * 0.05f, (Vector3){ 0 });
                        Entity_ApplyStun(allyBoss, 2.0f);
                    }
                }
                s_playerRespawnTimer += dt;
                if (s_playerRespawnTimer >= 5.0f)
                {
                    s_playerRespawnTimer = 0.0f;
                    Vector3 sp = TEAM_SPAWN[0];
                    sp.y = MapManager_GetGroundHeightAt(sp.x, sp.z);
                    player->agentId = Entity_SpawnAgent(sp, 100.0f, 0, TEAM_ALLY, ARCH_HERO);
                    Control_Init(player->agentId);
                    EquipDefaultLoadout(player->agentId);
                    Entity_SetPvEBonuses(player->agentId, NULL);
                    Entity_AddModifier(player->agentId, 1.5f, 4.0f);
                }
            }
            else
            {
                NatureZoneType zone = Map_QueryZoneAt(pa->position);
                Control_SetCastCooldownMult(GameRules_CooldownMult(pa->currentElement, zone));
                Entity_SetStealth(player->agentId, GameRules_GrantsStealth(pa->currentElement, zone));
            }

            int nearIds[16];
            int nc = Entity_GetNearbyTargets(MATCH_ARENA_CENTER, 8.0f, nearIds, 16);
            int allyPresence = 0, enemyPresence = 0;
            for (int i = 0; i < nc; i++)
            {
                const Agent *na = Entity_GetAgent(nearIds[i]);
                if (!na) continue;
                int w = (na->archetype == ARCH_BOSS) ? 3 : (na->archetype == ARCH_HERO ? 1 : 0);
                if (na->team == TEAM_ALLY) allyPresence += w;
                else if (na->team == TEAM_ENEMY) enemyPresence += w;
            }
            if (allyPresence > enemyPresence)
                s_pillarCapture = fminf(100.0f, s_pillarCapture + 14.0f * dt);
            else if (enemyPresence > allyPresence)
                s_pillarCapture = fmaxf(-100.0f, s_pillarCapture - 14.0f * dt);

            s_pillarPulseTimer += dt;
            if (s_pillarPulseTimer >= 3.0f)
            {
                s_pillarPulseTimer = 0.0f;
                if (s_pillarCapture >= 50.0f && Boss_IsSlotAlive(1))
                    Entity_ApplyDamage(Boss_GetSlotAgentId(1), 18.0f, (Vector3){ 0 });
                else if (s_pillarCapture <= -50.0f && Boss_IsSlotAlive(0))
                    Entity_ApplyDamage(Boss_GetSlotAgentId(0), 18.0f, (Vector3){ 0 });
            }

            // Periodic minion waves from both Bosses every 18s
            s_minionWaveTimer -= dt;
            if (s_minionWaveTimer <= 0.0f)
            {
                s_minionWaveTimer = 18.0f;
                if (Boss_IsSlotAlive(0)) AI_SpawnMinionWave(Boss_GetSlotAgentId(0), 2);
                if (Boss_IsSlotAlive(1)) AI_SpawnMinionWave(Boss_GetSlotAgentId(1), 2);
            }

            // 45s Map Twist: Linh Châu (Spirit Orb) near center
            if (s_spiritOrbBanner > 0.0f) s_spiritOrbBanner -= dt;
            if (!s_spiritOrbActive)
            {
                s_spiritOrbTimer -= dt;
                if (s_spiritOrbTimer <= 0.0f)
                {
                    s_spiritOrbActive = true;
                    s_spiritOrbPos = (Vector3){
                        MATCH_ARENA_CENTER.x + ((GetRandomValue(0, 1) == 0) ? 5.0f : -5.0f),
                        0.0f,
                        MATCH_ARENA_CENTER.z + ((GetRandomValue(0, 1) == 0) ? 5.0f : -5.0f)
                    };
                    s_spiritOrbPos.y = MapManager_GetGroundHeightAt(s_spiritOrbPos.x, s_spiritOrbPos.z);
                    s_spiritOrbTimer = 45.0f;
                }
            }
            else
            {
                int orbPickers[8];
                int np = Entity_GetNearbyTargets(s_spiritOrbPos, 2.2f, orbPickers, 8);
                for (int i = 0; i < np; i++)
                {
                    const Agent *picker = Entity_GetAgent(orbPickers[i]);
                    if (!picker || picker->archetype != ARCH_HERO) continue;
                    int bossSlot = (picker->team == TEAM_ALLY) ? 0 : 1;
                    int bossId = Boss_GetSlotAgentId(bossSlot);
                    const Agent *ba = Entity_GetAgent(bossId);
                    if (ba) Entity_ApplyDamage(bossId, -ba->maxHealth * 0.10f, (Vector3){ 0 }); // heal 10%
                    Entity_AddModifier(orbPickers[i], 1.35f, 8.0f);
                    s_spiritOrbActive = false;
                    s_spiritOrbBanner = 3.0f;
                    break;
                }
            }

            if (!Boss_IsSlotAlive(1))
                s_state = GAME_VICTORY;
            else if (!Boss_IsSlotAlive(0))
                s_state = GAME_DEFEAT;
            else if (s_pvpMatchTimer <= 0.0f)
            {
                const Agent *b0 = Entity_GetAgent(Boss_GetSlotAgentId(0));
                const Agent *b1 = Entity_GetAgent(Boss_GetSlotAgentId(1));
                float f0 = (b0 && b0->maxHealth > 0.0f) ? (b0->health / b0->maxHealth) : 0.0f;
                float f1 = (b1 && b1->maxHealth > 0.0f) ? (b1->health / b1->maxHealth) : 0.0f;
                float scoreAlly  = f0 + (s_pillarCapture * 0.002f);
                float scoreEnemy = f1 - (s_pillarCapture * 0.002f);
                s_state = (scoreAlly >= scoreEnemy) ? GAME_VICTORY : GAME_DEFEAT;
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

    // Thái Cực mode disabled in active gameplay per user request (incomplete feature).

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
            {
                Audio_PlaySFXAt(SFX_CLASH, evs[i].clashPoint); // Đấu Pháp
                if (evs[i].outcome == CLASH_A_WINS)
                {
                    Entity_RestoreMana(player->agentId, 15.0f);
                    AddFloatingText(evs[i].clashPoint, "Dau Phap +15 MP!", GOLD, 16.0f, 0.8f);
                    if (s_mode == GAME_MODE_EXPLORE) DaTau_OnClashWon();
                }
            }
        }
    }

    if (IsKeyDown(KEY_Q))
        s_camAngle -= 1.6f * dt;
    if (IsKeyDown(KEY_E))
        s_camAngle += 1.6f * dt;
    s_camDist -= GetMouseWheelMove() * 0.4f;
    if (s_camDist < 3.0f)
        s_camDist = 3.0f;
    if (s_camDist > 22.0f)
        s_camDist = 22.0f;

    // Module 4: movement/jump/dash/meditate/skill-cast all live in control/
    // now — this screen only forwards the camera and reads back the agent's
    // authoritative position (control writes it via Entity_SetPosition, and
    // entities owns it during jump arcs / dash bursts / knockback).
    // Intents only apply mid-fight — intro/end screens freeze the player.
    Control_SetCamera(s_camAngle, camera);
    UI_SetCamera(camera);
    UI_Update(dt);
    PlayerIntent intent = Control_ReadIntent();

    UITouchSkillSlot touchSlots[5];
    BuildGameTouchSlots(player, touchSlots);
    int touchExtraSkillIdx = -1;
    if (s_state == GAME_FIGHTING && !UI_IsLoadoutOpen() && !UI_Hub_IsModalOpen())
    {
        UI_PollTouchCombat(touchSlots, &s_gameTouchInput);
        if (s_gameTouchInput.joyActive)
        {
            float s = sinf(s_camAngle);
            float c = cosf(s_camAngle);
            intent.moveDir.x += s_gameTouchInput.joyX * c + s_gameTouchInput.joyZ * s;
            intent.moveDir.y += -s_gameTouchInput.joyX * s + s_gameTouchInput.joyZ * c;
        }
        if (s_gameTouchInput.attackPressed && intent.basicAttack == 0)
            intent.basicAttack = 1 + GetRandomValue(0, 2);
        if (s_gameTouchInput.khinhCongPressed)
        {
            intent.jump = true;
            intent.dash = true;
        }
        if (s_gameTouchInput.dodgePressed)
            intent.dash = true;
        if (s_gameTouchInput.meditateHeld)
            intent.meditate = true;
        for (int k = 0; k < 4; k++)
        {
            if (s_gameTouchInput.skillPressed[k] && intent.castSkillSlot < 0)
            {
                intent.castSkillSlot = k;
                break;
            }
        }
        if (s_gameTouchInput.skillPressed[4] && touchSlots[4].skillIndex >= 0)
            touchExtraSkillIdx = touchSlots[4].skillIndex;
    }
    else
    {
        memset(&s_gameTouchInput, 0, sizeof(s_gameTouchInput));
    }

    // Module 9 auto-targeting: an incoming enemy projectile (đối-đòn) or
    // the boss overrides the raw mouse aim — mobile-first UX (thiết kế §XI).
    if (s_state == GAME_FIGHTING && !UI_IsLoadoutOpen() && !UI_Hub_IsModalOpen())
    {
        static Vector2 s_worldTapDownPos = {0};
        static bool s_worldTapTracking = false;
        if (!s_gameTouchInput.pointerOverUI && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            s_worldTapDownPos = GetMousePosition();
            s_worldTapTracking = true;
        }
        if (s_worldTapTracking && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            s_worldTapTracking = false;
            Vector2 up = GetMousePosition();
            float tdx = up.x - s_worldTapDownPos.x;
            float tdy = up.y - s_worldTapDownPos.y;
            if (!s_gameTouchInput.pointerOverUI && (tdx * tdx + tdy * tdy) <= 18.0f * 18.0f)
            {
                bool handledTap = false;
                if (s_mode == GAME_MODE_EXPLORE && s_exploreRealm >= 0 && s_exploreRealm <= 3)
                    handledTap = OWQuestNpc_HandleTap(player->position, up, *camera);

                if (!handledTap)
                {
                    const Agent *selfA = Entity_GetAgent(player->agentId);
                    int bestPick = -1;
                    float bestPxSq = 68.0f * 68.0f;
                    if (selfA)
                    {
                        for (int idx = 0; idx < MAX_AGENTS; idx++)
                        {
                            if (idx == player->agentId) continue;
                            const Agent *cand = Entity_GetAgent(idx);
                            if (!cand || !Entity_CanDamageTarget(player->agentId, idx)) continue;
                            Vector3 mid = { cand->position.x, cand->position.y + 0.9f, cand->position.z };
                            Vector2 sp = GetWorldToScreen(mid, *camera);
                            float px = sp.x - up.x, py = sp.y - up.y;
                            float d2 = px * px + py * py;
                            if (d2 < bestPxSq) { bestPxSq = d2; bestPick = idx; }
                        }
                    }
                    if (bestPick >= 0)
                        UI_SetManualTarget(bestPick);
                }
            }
        }

        bool hasAuto = false;
        Vector3 autoPt = UI_GetAutoAimPoint(player->agentId, &hasAuto);
        if (hasAuto)
        {
            intent.aimPoint = autoPt;
        }
        else if (UI_IsTouchControlsEnabled())
        {
            // On touch screens, never aim at the corner button where the finger tapped;
            // aim forward in the character's current facing direction.
            float yaw = Control_GetYaw();
            intent.aimPoint = (Vector3){
                player->position.x + sinf(yaw) * 8.0f,
                player->position.y,
                player->position.z + cosf(yaw) * 8.0f
            };
        }

        // Swing damping: while an attack/cast animation plays, walking
        // drops to 35% so the feet stop sliding through the swing — the
        // "vừa đánh vừa chạy" root-motion feel without real root motion.
        if (s_swingSlowTimer > 0.0f)
            s_swingSlowTimer -= dt;
        if (s_tuongSinhTimer > 0.0f)
            s_tuongSinhTimer -= dt;
        Control_SetMoveSpeedMult(s_swingSlowTimer > 0.0f ? 0.35f : 1.0f);

        SyncRemoteWardrobeAndCasts(player->agentId);

        if (Net_ClientDrivesWorld())
        {
            // The host simulates our hero; snapshots move it back to us.
            // Update local facing yaw immediately so the client character
            // rotates toward its movement/cast direction instead of staying frozen.
            Control_UpdateFacingFromIntent(&intent);
            Net_ClientSubmitIntent(&intent);
        }
        else
        {
            Control_Apply(&intent, dt);

            // Cast flourish: control casts silently (pure logic) — it
            // reports the fired skill back so the character swings.
            int castIdx = Control_ConsumeCastFired();
            if (castIdx < 0 && touchExtraSkillIdx >= 0 &&
                !Entity_IsStunned(player->agentId) &&
                SkillManager_CanCast(touchExtraSkillIdx, player->agentId))
            {
                SkillParams sp = {.level = 1, .quantity = 1, .sizeScale = 1.0f};
                if (CastSkill(touchExtraSkillIdx, player->agentId, player->position, intent.aimPoint, sp))
                {
                    SkillManager_TriggerCooldown(touchExtraSkillIdx, player->agentId, 1.0f);
                    Control_FaceTowards(intent.aimPoint);
                    castIdx = touchExtraSkillIdx;
                }
            }
            if (castIdx >= 0)
            {
                float castSecs = Skill_GetCastAnimSeconds(castIdx);
                char animClip[64];
                float animSecs = 0.0f;
                if (Skill_ConsumeAnimTrigger(player->agentId, animClip, sizeof(animClip), &animSecs))
                    CharacterModel_TriggerByName(&player->anim, animClip, animSecs);
                else
                    CharacterModel_TriggerAttackTimed(&player->anim, CHAR_ANIM_CAST, castSecs);
                s_swingSlowTimer = castSecs * 0.6f; // free up before the anim tail
                // Đợt A5: connected clients replay this cast as pure VFX.
                Net_HostNotifyCast(player->agentId, castIdx, intent.aimPoint);
                const Agent *pc = Entity_GetAgent(player->agentId);
                // Ngũ Hành Tương Sinh 3-step combo -> Ngộ Đạo Thái Cực!
                int castElem = GameSkillElementByName(GetRegisteredSkillName(castIdx));
                if (pc != NULL) {
                    int sfxElem = (castElem >= 0) ? castElem : pc->currentElement;
                    Audio_PlaySFXAt(pc->taijiActive ? SFX_CAST_TAIJI
                                                    : Audio_CastSfxForElement(sfxElem),
                                    player->position);
                }
                if (s_tuongSinhTimer > 0.0f && s_tuongSinhLastElem >= 0 &&
                    castElem == (s_tuongSinhLastElem + 1) % 5)
                {
                    s_tuongSinhChain++;
                    if (s_tuongSinhChain >= 3 && pc && !pc->taijiActive)
                    {
                        Entity_SetTaijiActive(player->agentId, true);
                        Audio_PlaySFX(SFX_TAIJI_ENTER);
                        AddFloatingText(player->position, "NGO DAO THAI CUC!", ELEMENT_COLOR_TAIJI, 20.0f, 1.4f);
                        s_tuongSinhChain = 0;
                    }
                }
                else
                {
                    s_tuongSinhChain = 1;
                }
                s_tuongSinhLastElem = castElem;
                s_tuongSinhTimer = 6.0f;
            }
        }
    }
    const Agent *selfAgent = Entity_GetAgent(player->agentId);
    if (selfAgent)
        player->position = selfAgent->position;

    {
        char animClip[64];
        float animSecs = 0.0f;
        if (Skill_ConsumeAnimTrigger(player->agentId, animClip, sizeof(animClip), &animSecs))
            CharacterModel_TriggerByName(&player->anim, animClip, animSecs);
    }

    int jumpFired = Control_ConsumeJumpFired();
    if (jumpFired == 1)
        CharacterModel_TriggerVariant(&player->anim, CHAR_ANIM_JUMP, 0);
    else if (jumpFired == 2)
        CharacterModel_TriggerVariant(&player->anim, CHAR_ANIM_JUMP, 1);

    float moveLen = sqrtf(intent.moveDir.x * intent.moveDir.x + intent.moveDir.y * intent.moveDir.y);
    CharacterAnimSlot locoSlot = CHAR_ANIM_IDLE;
    if (selfAgent && (selfAgent->vState == AGENT_JUMPING || selfAgent->vState == AGENT_RING_OUT_FALLING))
    {
        locoSlot = (selfAgent->velocity.y > 0.25f) ? CHAR_ANIM_JUMP : CHAR_ANIM_FALL;
    }
    else if (moveLen > 0.0001f)
    {
        locoSlot = (selfAgent && !selfAgent->staminaExhausted) ? CHAR_ANIM_RUN : CHAR_ANIM_WALK;
    }
    CharacterModel_UpdateLocomotion(&player->anim, dt, locoSlot);
    SkillHelper_UpdateMeleeTrails(dt, &player->anim, player->position, Control_GetYaw());

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
        SkillHelper_StartMeleeTrails(player->agentId, &player->anim, player->position,
                                     Control_GetYaw(), (int)basicAttackType, swingSecs);
        s_swingSlowTimer = swingSecs * 0.6f; // damp walking through the swing
        Audio_PlaySFXAt(SFX_MARTIAL_SWING, player->position);

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
                SkillHelper_SpawnMeleeImpact(player->agentId, targetPos);
                Audio_PlaySFXAt(SFX_MELEE_HIT, targetPos);
            }
            if (gotWallBonus && wallElement == 3 /* Earth — only element with a real wall so far */)
            {
                (void)wallPos;
                AddFloatingText(targetPos, "Cong Huong Dat!", ELEMENT_COLOR_EARTH, 16.0f, 0.6f);
            }
        }
    }

    camera->target = (Vector3){
        player->position.x, player->position.y + SANDBOX_CAMERA_TARGET_HEIGHT, player->position.z};
    camera->position = SandboxCamera_OrbitPositionAtPitch(player->position, s_camAngle, s_camDist, s_camPitch);
    s_hudCamera = *camera;
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

    // Minions & Open-World Hordes (instanced batch renderer — < 10 draw calls total)
    HordeRenderer_Draw3D(s_hudCamera);

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
                EnsureRemoteModularInit(i);
                ModularInstance_DrawWithState(&s_remoteModular[i], &s_remoteAnim[i], a->position, s_remoteYaw[i], 1.0f, tint);
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
            DrawSphereEx(core, 0.55f, 12, 12,
                         (Color){(unsigned char)(10 + c.r / 8), (unsigned char)(10 + c.g / 8),
                                 (unsigned char)(10 + c.b / 8), 255});
            DrawCircle3D(core, 0.9f, (Vector3){0, 1, 0}, (float)GetTime() * 40.0f, c);
        }
    }

    if (s_mode == GAME_MODE_EXPLORE && s_exploreRealm >= 0 && s_exploreRealm <= 3 && s_state == GAME_FIGHTING)
    {
        OW_Draw3D(player, s_hudCamera);
    }
    int lockedTarget = UI_GetManualTarget();
    if (!EnvShadow_IsCapturing() && lockedTarget >= 0)
    {
        const Agent *lt = Entity_GetAgent(lockedTarget);
        if (lt)
        {
            Vector3 rp = { lt->position.x, lt->position.y + 0.12f, lt->position.z };
            DrawCircle3D(rp, 0.95f, (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 255, 90, 75, 235 });
            DrawCircle3D(rp, 1.15f, (Vector3){ 1, 0, 0 }, 90.0f, GOLD);
        }
    }

    if (s_mode == GAME_MODE_PVP_ESCORT_1V1 || s_mode == GAME_MODE_PVP_ESCORT_2V2)
    {
        // Convergence Pillar (Trụ Long Mạch Thái Cực) at MATCH_ARENA_CENTER
        Vector3 pBase = MATCH_ARENA_CENTER;
        pBase.y = MapManager_GetGroundHeightAt(pBase.x, pBase.z);
        Color pCol = (s_pillarCapture >= 20.0f) ? (Color){ 80, 190, 255, 255 }
                   : (s_pillarCapture <= -20.0f) ? (Color){ 245, 95, 75, 255 }
                   : (Color){ 235, 215, 130, 255 };
        DrawCylinder((Vector3){ pBase.x, pBase.y + 1.8f, pBase.z }, 0.65f, 0.85f, 3.6f, 12, (Color){ 32, 34, 46, 255 });
        if (!EnvShadow_IsCapturing())
        {
            DrawCylinderWires((Vector3){ pBase.x, pBase.y + 1.8f, pBase.z }, 0.68f, 0.88f, 3.6f, 12, pCol);
            DrawCircle3D((Vector3){ pBase.x, pBase.y + 0.12f, pBase.z }, 8.0f, (Vector3){ 0, 1, 0 }, 0.0f, Fade(pCol, 0.45f));
        }

        if (s_spiritOrbActive)
        {
            float bob = 0.8f + 0.2f * sinf((float)GetTime() * 3.5f);
            Vector3 op = { s_spiritOrbPos.x, s_spiritOrbPos.y + bob, s_spiritOrbPos.z };
            DrawSphere(op, 0.42f, (Color){ 255, 225, 95, 255 });
            DrawCircle3D(op, 0.75f, (Vector3){ 0, 1, 0 }, (float)GetTime() * 120.0f, GOLD);
        }
    }
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
    rlDrawRenderBatchActive();
    EndShaderMode();
    BeginBlendMode(BLEND_ALPHA);
    Camera2D cam2d = {0};
    cam2d.zoom = 1.0f;
    BeginMode2D(cam2d);
    rlSetTexture(0);

    const Agent *agent = Entity_GetAgent(player->agentId);
    float hp = agent ? agent->health : 0.0f;
    float maxHp = agent ? agent->maxHealth : 1.0f;
    float ratio = (maxHp > 0.0f) ? (hp / maxHp) : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    float mana = agent ? agent->mana : 0.0f;
    float maxMana = agent ? agent->maxMana : 1.0f;
    float manaRatio = (maxMana > 0.0f) ? (mana / maxMana) : 0.0f;
    if (manaRatio < 0.0f) manaRatio = 0.0f;
    if (manaRatio > 1.0f) manaRatio = 1.0f;

    float stam = agent ? agent->stamina : 0.0f;
    float maxStam = agent ? agent->maxStamina : 1.0f;
    float stamRatio = (maxStam > 0.0f) ? (stam / maxStam) : 0.0f;
    if (stamRatio < 0.0f) stamRatio = 0.0f;
    if (stamRatio > 1.0f) stamRatio = 1.0f;

    // --- Top-left Player Status Card (Where Winds Meet ink-wash & gold corners) ---
    const int cardX = 16, cardY = 14, cardW = 264, cardH = 78;
    Rectangle cardRec = {(float)cardX, (float)cardY, (float)cardW, (float)cardH};
    Color cardAccent = (agent && agent->taijiActive) ? WUXIA_GOLD_BRIGHT : WUXIA_GOLD;
    WuxiaUI_DrawPanel(cardRec, cardAccent, true);

    // Elemental diamond crest on left
    Color elemBadgeCol = WUXIA_JADE;
    if (agent) {
        if (agent->currentElement == 0) elemBadgeCol = WUXIA_AZURE;
        else if (agent->currentElement == 1) elemBadgeCol = (Color){ 95, 205, 115, 255 };
        else if (agent->currentElement == 2) elemBadgeCol = (Color){ 235, 95, 65, 255 };
        else if (agent->currentElement == 3) elemBadgeCol = WUXIA_GOLD;
        else if (agent->currentElement == 4) elemBadgeCol = (Color){ 210, 220, 235, 255 };
    }
    WuxiaUI_DrawDiamond((Vector2){ (float)cardX + 22.0f, (float)cardY + 32.0f }, 12.0f, elemBadgeCol, WUXIA_GOLD_BRIGHT);

    const int barX = cardX + 42, barY = cardY + 13, barW = 208, barH = 18;
    Rectangle hpRec = {(float)barX, (float)barY, (float)barW, (float)barH};
    WuxiaUI_DrawResourceBar(hpRec, ratio,
                            (Color){ 215, 62, 62, 255 }, (Color){ 148, 28, 32, 255 },
                            WUXIA_GOLD_DIM,
                            TextFormat("SINH LUC  %d/%d", (int)hp, (int)maxHp), 11.5f);

    const int manaBarY = barY + barH + 8, manaH = 14;
    Rectangle mpRec = {(float)barX, (float)manaBarY, (float)barW, (float)manaH};
    Color mpTop = (agent && agent->taijiActive) ? WUXIA_GOLD_BRIGHT : (Color){ 78, 162, 245, 255 };
    Color mpBot = (agent && agent->taijiActive) ? (Color){ 175, 130, 40, 255 } : (Color){ 36, 92, 185, 255 };
    WuxiaUI_DrawResourceBar(mpRec, manaRatio, mpTop, mpBot,
                            (Color){ 125, 155, 205, 190 },
                            TextFormat("CHAN KHI  %d/%d", (int)mana, (int)maxMana), 10.5f);

    const int stamBarY = manaBarY + manaH + 6, stamH = 10;
    Rectangle stamRec = {(float)barX, (float)stamBarY, (float)barW, (float)stamH};
    Color stamTop = (agent && agent->staminaExhausted) ? (Color){ 185, 115, 55, 255 } : (Color){ 95, 215, 145, 255 };
    Color stamBot = (agent && agent->staminaExhausted) ? (Color){ 120, 65, 30, 255 } : (Color){ 45, 140, 85, 255 };
    WuxiaUI_DrawResourceBar(stamRec, stamRatio, stamTop, stamBot,
                            (Color){ 120, 185, 145, 180 },
                            (agent && agent->staminaExhausted) ? "THE LUC (KIET SUC)" : TextFormat("THE LUC  %d/%d", (int)stam, (int)maxStam), 9.0f);

    // Module 9 overlay: skill slot chips + auto-aim reticle.
    UI_DrawOverlay(player->agentId);

    // Floating 2D HP bars for nearby enemies (monsters & bosses)
    if (s_state == GAME_FIGHTING && (s_mode == GAME_MODE_EXPLORE || s_mode == GAME_MODE_PVP_ESCORT_1V1 || s_mode == GAME_MODE_PVP_ESCORT_2V2))
    {
        for (int i = 0; i < MAX_AGENTS; i++)
        {
            if (i == player->agentId) continue;
            const Agent *ea = Entity_GetAgent(i);
            if (!ea || ea->maxHealth <= 0.0f) continue;
            if (ea->archetype == ARCH_HERO || ea->isStatic) continue;
            float dx = ea->position.x - player->position.x;
            float dz = ea->position.z - player->position.z;
            if (dx * dx + dz * dz > 28.0f * 28.0f) continue;
            Vector3 head = { ea->position.x, ea->position.y + (ea->archetype == ARCH_BOSS ? 2.2f : 1.1f), ea->position.z };
            Vector2 sp = GetWorldToScreen(head, s_hudCamera);
            if (sp.x < 0 || sp.x > GetScreenWidth() || sp.y < 0 || sp.y > GetScreenHeight()) continue;
            int bw = (ea->archetype == ARCH_BOSS) ? 72 : 42;
            int bh = (ea->archetype == ARCH_BOSS) ? 7 : 5;
            float frac = ea->health / ea->maxHealth;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            DrawRectangle((int)sp.x - bw / 2 - 1, (int)sp.y - 1, bw + 2, bh + 2, (Color){ 10, 10, 16, 200 });
            Color barCol = (ea->team == TEAM_ALLY) ? (Color){ 70, 185, 255, 255 } : (Color){ 225, 65, 60, 255 };
            DrawRectangle((int)sp.x - bw / 2, (int)sp.y, (int)(bw * frac), bh, barCol);
        }
    }

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    if (s_mode == GAME_MODE_PVP_ESCORT_1V1 || s_mode == GAME_MODE_PVP_ESCORT_2V2)
    {
        const Agent *b0 = Entity_GetAgent(Boss_GetSlotAgentId(0));
        const Agent *b1 = Entity_GetAgent(Boss_GetSlotAgentId(1));
        float f0 = (b0 && b0->maxHealth > 0.0f) ? b0->health / b0->maxHealth : 0.0f;
        float f1 = (b1 && b1->maxHealth > 0.0f) ? b1->health / b1->maxHealth : 0.0f;
        int mins = (int)s_pvpMatchTimer / 60;
        int secs = (int)s_pvpMatchTimer % 60;
        const char *hdr = TextFormat("THANH LONG %.0f%%   [%02d:%02d]   BACH HO %.0f%%",
                                     f0 * 100.0f, mins, secs, f1 * 100.0f);
        int hw = MeasureText(hdr, 18);
        DrawRectangle(sw / 2 - hw / 2 - 16, 12, hw + 32, 50, (Color){ 14, 16, 24, 215 });
        DrawRectangleLinesEx((Rectangle){ (float)(sw / 2 - hw / 2 - 16), 12.0f, (float)(hw + 32), 50.0f },
                             1.5f, (Color){ 210, 180, 105, 210 });
        DrawText(hdr, sw / 2 - hw / 2, 18, 18, (Color){ 240, 232, 210, 255 });
        // Pillar capture bar
        int barW = 240, barX = sw / 2 - barW / 2, barY = 44;
        DrawRectangle(barX, barY, barW, 10, (Color){ 25, 25, 36, 240 });
        int half = barW / 2;
        int delta = (int)((s_pillarCapture / 100.0f) * half);
        if (delta > 0) DrawRectangle(barX + half - delta, barY, delta, 10, (Color){ 75, 185, 255, 255 });
        else if (delta < 0) DrawRectangle(barX + half, barY, -delta, 10, (Color){ 235, 85, 70, 255 });
        DrawRectangle(barX + half - 1, barY - 1, 2, 12, WHITE);
        if (s_playerRespawnTimer > 0.0f)
        {
            const char *rt = TextFormat("HOI SINH SAU %.1fs...", 5.0f - s_playerRespawnTimer);
            DrawText(rt, sw / 2 - MeasureText(rt, 26) / 2, sh / 2 - 20, 26, (Color){ 255, 210, 100, 255 });
        }
        if (s_spiritOrbBanner > 0.0f)
        {
            const char *ob = "LINH CHAU HO PHAP! +10% MAU BOSS & TANG TOC!";
            DrawText(ob, sw / 2 - MeasureText(ob, 20) / 2, 76, 20, GOLD);
        }
    }
    else if (s_mode == GAME_MODE_EXPLORE)
    {
        const PlayerProfile *prof = PlayerProfile_GetGlobal();
        const DaTauQuest *dq = DaTau_GetCurrent();
        // Top-right Dã Tẩu Quest Scroll Card + Currencies
        int qW = 326, qH = 108, qX = sw - qW - 16, qY = 14;
        Rectangle qRec = { (float)qX, (float)qY, (float)qW, (float)qH };
        WuxiaUI_DrawPanel(qRec, WUXIA_GOLD, true);
        WuxiaUI_DrawDiamond((Vector2){ (float)qX + 16.0f, (float)qY + 16.0f }, 5.0f, WUXIA_AZURE, WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawText(ExploreRealmTitle(s_exploreRealm), (float)(qX + 28), (float)(qY + 8), 14.0f, WUXIA_AZURE);
        WuxiaUI_DrawText(TextFormat("Vang: %d   ·   Diem: %d   ·   Chuoi: %d", prof->gold, prof->daTauPoints, prof->daTauStreak),
                         (float)(qX + 12), (float)(qY + 28), 12.5f, WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawText(dq->title, (float)(qX + 12), (float)(qY + 48), 14.0f, WUXIA_TEXT_PRIMARY);
        WuxiaUI_DrawText(TextFormat("%s (%d/%d)", dq->desc, dq->currentCount, dq->targetCount),
                         (float)(qX + 12), (float)(qY + 67), 12.5f, WUXIA_JADE);
        if (UI_IsTouchControlsEnabled())
        {
            Rectangle skipBtn = { (float)(qX + 12), (float)(qY + 84), 136.0f, 20.0f };
            if (WuxiaUI_DrawButton(skipBtn, TextFormat("DOI NV (LB:%d)", prof->daTauTokens), WUXIA_GOLD, false, 11.5f))
                DaTau_SkipWithToken();
        }
        else
        {
            WuxiaUI_DrawText(TextFormat("[N] Doi nhiem vu (Lenh bai: %d)", prof->daTauTokens),
                             (float)(qX + 12), (float)(qY + 86), 11.5f, WUXIA_TEXT_MUTED);
        }
        if (DaTau_GetCompleteBannerTimer() > 0.0f)
        {
            const char *msg = DaTau_GetLastRewardText();
            DrawText(msg, sw / 2 - MeasureText(msg, 22) / 2, 74, 22, (Color){ 255, 225, 110, 255 });
        }
        if (s_worldBossBanner > 0.0f)
        {
            const char *wb = s_worldBossActive ? "KY NGO GIANG HO: DAI TINH LINH XUAT THE!"
                                               : "HA DAI TINH LINH! +400 VANG +100 DIEM +1 LENH BAI!";
            DrawText(wb, sw / 2 - MeasureText(wb, 22) / 2, 104, 22, (Color){ 255, 130, 110, 255 });
        }
        if (s_failoverBannerTimer > 0.0f)
        {
            const char *fb = "CHU PHONG ROI DI - CHUYEN VE CHE DO TU CHU!";
            DrawText(fb, sw / 2 - MeasureText(fb, 20) / 2, 134, 20, GOLD);
        }
        if (s_playerRespawnTimer > 0.0f)
        {
            const char *rt = TextFormat("HOI SINH SAU %.1fs...", 5.0f - s_playerRespawnTimer);
            DrawText(rt, sw / 2 - MeasureText(rt, 26) / 2, sh / 2 - 20, 26, (Color){ 255, 210, 100, 255 });
        }
        if (s_spiritOrbBanner > 0.0f)
        {
            const char *ob = "LINH CHAU HO PHAP! +10% MAU BOSS & TANG TOC!";
            DrawText(ob, sw / 2 - MeasureText(ob, 20) / 2, 76, 20, GOLD);
        }
    }

    // Boss HP bar (top center) while a boss lives.
    if (s_mode == GAME_MODE_BOSS && Boss_IsAlive())
    {
        const Agent *boss = Entity_GetAgent(Boss_GetAgentId());
        if (boss)
        {
            float bRatio = (boss->maxHealth > 0.0f) ? boss->health / boss->maxHealth : 0.0f;
            if (bRatio < 0.0f) bRatio = 0.0f;
            if (bRatio > 1.0f) bRatio = 1.0f;
            int bw = sw - 560;
            if (bw > 420) bw = 420;
            if (bw < 240) bw = 240;
            const int bh = 14, bx = sw / 2 - bw / 2, by = 36;
            Rectangle plate = {(float)(bx - 12), 12.0f, (float)(bw + 24), 46.0f};
            DrawRectangleRec(plate, (Color){14, 14, 22, 205});
            DrawRectangleLinesEx(plate, 1.5f, (Color){165, 130, 185, 190});

            const char *bn = "HAC DIEN TON GIA";
            DrawText(bn, sw / 2 - MeasureText(bn, 14) / 2, 17, 14, (Color){238, 226, 248, 255});

            Rectangle bRec = {(float)bx, (float)by, (float)bw, (float)bh};
            DrawRectangleRec(bRec, (Color){22, 16, 28, 235});
            if (bRatio > 0.0f)
            {
                int fillW = (int)(bw * bRatio);
                DrawRectangle(bx, by, fillW, bh, (Color){160, 46, 185, 255});
                DrawRectangle(bx, by, fillW, 4, (Color){215, 110, 235, 180});
            }
            DrawRectangleLinesEx(bRec, 1.0f, (Color){200, 175, 220, 180});
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
        float tw = WuxiaUI_MeasureText(t, 24.0f);
        float bw = tw + 48.0f;
        if (bw < 270.0f) bw = 270.0f;
        Rectangle mp = { (float)sw * 0.5f - bw * 0.5f, 12.0f, bw, 56.0f };
        WuxiaUI_DrawPanel(mp, WUXIA_GOLD, true);
        WuxiaUI_DrawTextCentered(t, (Rectangle){ mp.x, mp.y + 6.0f, mp.width, 26.0f }, 24.0f, WUXIA_GOLD_BRIGHT);
        WuxiaUI_DrawTextCentered("DANG CHO DONG DAO VAO PHONG...",
                                 (Rectangle){ mp.x, mp.y + 34.0f, mp.width, 18.0f }, 12.5f, WUXIA_TEXT_MUTED);
    }
    else if (Net_GetMode() == NET_MODE_CLIENT && Net_GetLocalHeroAgentId() < 0)
    {
        const char *w = "DANG KET NOI DEN HOST...";
        WuxiaUI_DrawText(w, (float)sw * 0.5f - WuxiaUI_MeasureText(w, 20.0f) * 0.5f, 24.0f, 20.0f, WUXIA_GOLD_BRIGHT);
    }

    // Match-state overlays
    if (s_mode == GAME_MODE_EXPLORE && s_exploreRealm >= 0 && s_exploreRealm <= 3 && s_state == GAME_FIGHTING)
    {
        OW_DrawHUD(player, s_hudCamera);
        int peerCount = (Net_GetMode() != NET_MODE_OFF) ? (Net_GetPeerCount() + 1) : 1;
        if (peerCount > NET_EXPLORE_MAX_PLAYERS) peerCount = NET_EXPLORE_MAX_PLAYERS;
        WuxiaUI_DrawText(TextFormat("CO-OP EOS P2P: %d/%d", peerCount, NET_EXPLORE_MAX_PLAYERS),
                         20.0f, 86.0f, 13.0f, WUXIA_JADE);
    }

    if (s_state == GAME_ARENA_INTRO)
    {
        const char *t = (s_mode == GAME_MODE_EXPLORE) ? ExploreRealmTitle(s_exploreRealm)
                      : (s_mode == GAME_MODE_PVP_ESCORT_1V1 || s_mode == GAME_MODE_PVP_ESCORT_2V2) ? "SONG LINH HO PHAP"
                      : (s_mode == GAME_MODE_TEAM_BATTLE) ? "SONG DAU"
                      : "HAC DIEN TON GIA";
        DrawText(t, sw / 2 - MeasureText(t, 36) / 2, sh / 2 - 60, 36, (Color){235, 230, 245, 255});
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

    if (UI_IsTouchControlsEnabled() && s_state == GAME_FIGHTING && !UI_IsLoadoutOpen() && !UI_Hub_IsModalOpen())
    {
        float ty = (s_mode == GAME_MODE_EXPLORE) ? 128.0f : 14.0f;
        float tx = (float)sw - 16.0f;
        struct { const char *lbl; Color col; int action; } topBtns[3] = {
            { "MENU",  WUXIA_CRIMSON, 2 },
            { "TU DO", WUXIA_AZURE, 1 },
            { "CHIEU", WUXIA_GOLD, 0 }
        };
        for (int b = 0; b < 3; b++)
        {
            if (b == 1 && s_mode != GAME_MODE_EXPLORE) continue;
            float bw = WuxiaUI_MeasureText(topBtns[b].lbl, 12.5f) + 24.0f;
            tx -= bw;
            Rectangle br = { tx, ty, bw, 28.0f };
            if (WuxiaUI_DrawButton(br, topBtns[b].lbl, topBtns[b].col, false, 12.5f))
            {
                if (topBtns[b].action == 0) UI_ToggleLoadout();
                else if (topBtns[b].action == 1) UI_Hub_OpenModal(0);
                else { s_backToMenu = true; ResetMatch((PlayerEntity *)player); }
            }
            tx -= 6.0f;
        }
    }

    EndMode2D();
    EndBlendMode();

    if (s_state == GAME_FIGHTING && !UI_IsLoadoutOpen() && !UI_Hub_IsModalOpen())
    {
        UITouchSkillSlot touchSlots[5];
        BuildGameTouchSlots(player, touchSlots);
        UI_DrawTouchCombat(touchSlots, &s_gameTouchInput);
    }
    if (UI_Hub_IsModalOpen())
    {
        UI_Hub_UpdateDrawModal();
        if (!UI_Hub_IsModalOpen())
        {
            EquipDefaultLoadout(player->agentId);
            ApplyModeBalanceToPlayer(player->agentId);
        }
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
