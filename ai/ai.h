// ai/ai.h
// Minion brain (MODULES_ROADMAP.md Module 8). Minions are ordinary
// ARCH_MINION agents in the shared entities pool — this module only adds
// BEHAVIOR: steady march toward the enemy boss (fallback: nearest enemy),
// self-destruct AoE on contact, Hero Bots, and Open-World Vampire-Survivors
// Swarm AI + Proximity Wave Director. Pure logic — no rendering includes.
#ifndef AI_H
#define AI_H

#include "entities/entities.h"

void AI_Init(void);
void AI_Update(float dt);
int  AI_SpawnMinionWave(int bossAgentId, int count);

typedef struct {
    Vector3 pos;
    int     element;
} MinionExplosion;
int AI_PollExplosions(MinionExplosion *out, int max);

// --- Hero bots (Dot A4 + Utility AI Simulated Players) ---
#define AI_MAX_HERO_BOTS 8
#define AI_MAX_WORLD_POIS 8

typedef enum {
    AI_BOT_MODE_ARENA_DUEL = 0, // Team Battle / Sparring
    AI_BOT_MODE_MOBA_ESCORT,    // 1v1 / 2v2 Dual-Boss + Convergence Pillar
    AI_BOT_MODE_OPEN_WORLD      // Living Jianghu: Quests, Camps, Loot, World Boss, Social
} AIBotGameMode;

typedef enum {
    AI_PERSONA_BALANCED = 0,
    AI_PERSONA_VANGUARD,  // Aggressive diver: pressures enemy heroes & enemy Boss
    AI_PERSONA_GUARDIAN,  // Objective defender: escorts ally Boss, peels minions, holds Pillar
    AI_PERSONA_DUELIST,   // Tactical counter-puncher: Dau Phap clash & Tuong Sinh combos
    AI_PERSONA_WANDERER,  // Open-World PvE explorer: NPC Da Tau -> Camps -> World Boss
    AI_PERSONA_RIVAL,     // Open-World PvP duelist: social emotes, Spar, occasional PK
    AI_PERSONA_COUNT
} AIBotPersona;

typedef struct {
    AIBotGameMode mode;
    int           realmId;
    int           primaryHumanId;
    int           allyBossId;
    int           enemyBossId;
    Vector3       pillarPos;
    float         pillarCapture;     // -100 (Enemy) .. +100 (Ally)
    bool          spiritOrbActive;
    Vector3       spiritOrbPos;
    Vector3       safeCenter;
    float         safeRadius;
    Vector3       npcQuestPos;
    Vector3       npcAlchemistPos;
    bool          worldBossActive;
    Vector3       worldBossPos;
    Vector3       pois[AI_MAX_WORLD_POIS];
    int           poiCount;
} AIBotWorldContext;

typedef struct {
    int          agentId;
    AIBotPersona persona;
    int          outfitSeed;
    char         displayName[32];
} AIBotProfile;

typedef struct {
    int agentId;
    int emoteId; // 1=Greet, 2=CallBoss, 3=InviteDuel, 11=AcceptSpar
} AIBotSocialEvent;

int  AI_SpawnHeroBot(Vector3 pos, AgentTeam team);
int  AI_SpawnHeroBotEx(Vector3 pos, AgentTeam team, AIBotPersona persona, const char *nameOverride);
void AI_ClearHeroBots(void);
int  AI_GetHeroBotCount(AgentTeam team);
bool AI_IsHeroBot(int agentId);
void AI_SetBotWorldContext(const AIBotWorldContext *ctx);
bool AI_GetHeroBotProfile(int agentId, AIBotProfile *outProfile);
void AI_NotifyBotSocialEvent(int senderAgentId, int targetBotAgentId, int emoteId);
int  AI_PollBotSocialEvents(AIBotSocialEvent *out, int max);

typedef struct {
    int     agentId;
    int     skillIndex; // >= 250 = basic attack (250 + BasicAttackType)
    Vector3 aim;
} HeroBotCast;
int AI_PollHeroCasts(HeroBotCast *out, int max);

// --- PvE Explore Monster Camps & Vampire-Survivors Horde AI ---
#define AI_MAX_CAMP_MONSTERS 224
#define AI_MAX_MINION_BOLTS  128
#define AI_MAX_HORDE_ZONES   24

typedef enum {
    MONSTER_SWARM_MELEE = 0, // Tiểu Yêu Cắn/Cào (Fast swarmer, telegraphed bite lunge)
    MONSTER_RANGED_CASTER,   // Pháp Yêu Phun Tia Nhỏ (Spitter — trivial bolt, no heavy CastSkill)
    MONSTER_ELITE_GUARD,     // Thủ Lĩnh Bầy (Pack Alpha — rallies swarm, short charge/stomp)
    MONSTER_LEAPER           // Yêu Thú Vồ (Flanking pouncer — predictive short leap-bite)
} MonsterKind;

typedef enum {
    MON_STATE_GRAZE = 0,
    MON_STATE_RALLY,
    MON_STATE_CHASE,
    MON_STATE_TELEGRAPH, // Pullback/squash warning before bite/leap/spit/charge
    MON_STATE_STRIKE,    // Active lunge / leap / bolt release
    MON_STATE_RECOVER,
    MON_STATE_LEASH
} MonsterAIState;

typedef struct {
    Vector3     pos;
    int         element;
    MonsterKind kind;
} MonsterKillEvent;

typedef struct {
    Vector3     pos;
    int         element;
    MonsterKind kind;
    bool        isBite;
} MonsterStrikeEvent;

// Read-only visual snapshot for instanced HordeRenderer (game/horde_renderer.c)
typedef struct {
    int            agentId;
    Vector3        pos;
    float          yaw;
    float          scale;
    float          animPhase;
    float          telegraphFrac; // 0..1 during TELEGRAPH/STRIKE
    float          hitFlash;      // 0..1 white hit pop
    int            element;
    MonsterKind    kind;
    MonsterAIState state;
} AIMonsterRenderInfo;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    float   radius;
    int     element;
} AIMinionBoltRenderInfo;

// Proximity Hunting Ground / Vampire-Survivors Wave Director
typedef struct {
    Vector3 center;
    float   radius;          // Hunting zone radius (e.g. 30..65m)
    int     element;
    int     baseCount;       // Initial pack size when waking (e.g. 18..35)
    int     maxConcurrent;   // Max active monsters for this zone (e.g. 45..70)
    float   waveIntervalSec; // Surge cadence (e.g. 4.5s)
    int     waveBatchSize;   // Monsters per surge wave (e.g. 8..14)
    float   leaperRatio;     // 0..1 fraction of MONSTER_LEAPER
    float   spitterRatio;    // 0..1 fraction of MONSTER_RANGED_CASTER
    int     eliteCount;      // 0..2 Pack Alphas per zone
} AIHordeZoneDef;

int  AI_SpawnMonsterCamp(Vector3 center, int count, int element, MonsterKind kind);
int  AI_SpawnOneShotMonster(Vector3 pos, int element, MonsterKind kind);
void AI_ClearMonsterCamps(void);
bool AI_IsCampMonster(int agentId);
int  AI_PollMonsterKills(MonsterKillEvent *out, int max);
int  AI_PollMonsterStrikes(MonsterStrikeEvent *out, int max);

void AI_RegisterHordeZone(const AIHordeZoneDef *def);
void AI_ClearHordeZones(void);
void AI_SetSafeZone(Vector3 center, float radius);
int  AI_GetActiveCampMonsterCount(void);
int  AI_GetActiveHordeZoneIndex(Vector3 playerPos);

int  AI_QueryMonsterRenderData(AIMonsterRenderInfo *out, int max);
int  AI_QueryMinionBolts(AIMinionBoltRenderInfo *out, int max);
void AI_ClientVisualTick(float dt);

#endif // AI_H
