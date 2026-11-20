// ai/ai.c — Module 8: minion steering, hero bots, & Open-World Vampire-Survivors Swarm AI.
// Pure logic — no rendering includes.
#include "ai/ai.h"
#include "skills/skill_manager.h" // hero bots cast off equippedSkills
#include "combat/combat.h"      // projectile snapshot + minion bolt clash submission
#include "maps/map_manager.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// --- Classic Arena Minions (Boss waves) ---
static const float MINION_SPEED_MPS     = 2.0f;
static const float MINION_TRIGGER_RANGE = 1.2f;
static const float MINION_BLAST_RADIUS  = 2.0f;
static const float MINION_BLAST_DAMAGE  = 15.0f;
static const float MINION_BLAST_KB      = 3.0f;
static const float MINION_HP            = 20.0f;
static const float WAVE_RING_RADIUS     = 1.6f;

#define MAX_MINION_EXPLOSIONS 32
static MinionExplosion s_explosions[MAX_MINION_EXPLOSIONS];
static int s_explosionCount = 0;

static void UpdateCampMonsters(float dt);
static void UpdateMinionBolts(float dt);
static void UpdateHordeZones(float dt);

// Fast O(1) lookup from agentId -> camp monster slot (-1 if not a camp monster)
static int s_agentToCamp[MAX_AGENTS];

void AI_Init(void) {
    s_explosionCount = 0;
    for (int i = 0; i < MAX_AGENTS; i++) s_agentToCamp[i] = -1;
    AI_ClearMonsterCamps();
    AI_ClearHordeZones();
}

static int FindMarchTarget(const Agent *self) {
    int best = -1, bestBoss = -1;
    float bestSq = 0.0f, bestBossSq = 0.0f;
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->team == self->team) continue;
        float dx = a->position.x - self->position.x;
        float dz = a->position.z - self->position.z;
        float dSq = dx * dx + dz * dz;
        if (best == -1 || dSq < bestSq) { best = i; bestSq = dSq; }
        if (a->archetype == ARCH_BOSS && (bestBoss == -1 || dSq < bestBossSq)) {
            bestBoss = i; bestBossSq = dSq;
        }
    }
    return (bestBoss != -1) ? bestBoss : best;
}

// --- Hero bots (Dot A4) -------------------------------------------------------

static const float BOT_MOVE_MPS       = 3.5f;
static const float BOT_RANGE_FAR      = 9.0f;
static const float BOT_RANGE_NEAR     = 5.0f;
static const float BOT_EDGE_MARGIN    = 2.5f;
static const float BOT_THINK_SECONDS  = 1.2f;
static const float BOT_DODGE_COOLDOWN = 2.5f;
static const float BOT_DODGE_RANGE    = 4.0f;

typedef struct {
    bool  used;
    int   agentId;
    float thinkTimer;
    float dodgeTimer;
    float strafePhase;
} HeroBot;
static HeroBot s_bots[AI_MAX_HERO_BOTS];

static HeroBotCast s_botCasts[16];
static int         s_botCastCount = 0;

int AI_SpawnHeroBot(Vector3 pos, AgentTeam team) {
    int slot = -1;
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++)
        if (!s_bots[i].used) { slot = i; break; }
    if (slot < 0) return -1;
    int agentId = Entity_SpawnAgent(pos, 100.0f, 0, team, ARCH_HERO);
    if (agentId < 0) return -1;
    s_bots[slot] = (HeroBot){ true, agentId,
                              0.3f + 0.2f * (float)slot,
                              0.0f, (float)slot * 1.7f };
    return agentId;
}

void AI_ClearHeroBots(void) {
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++) {
        if (s_bots[i].used && Entity_GetAgent(s_bots[i].agentId) != NULL)
            Entity_ApplyDamage(s_bots[i].agentId, 1e9f, (Vector3){ 0 });
        s_bots[i].used = false;
    }
}

bool AI_IsHeroBot(int agentId) {
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++)
        if (s_bots[i].used && s_bots[i].agentId == agentId) return true;
    return false;
}

int AI_GetHeroBotCount(AgentTeam team) {
    int n = 0;
    for (int i = 0; i < AI_MAX_HERO_BOTS; i++) {
        if (!s_bots[i].used) continue;
        const Agent *a = Entity_GetAgent(s_bots[i].agentId);
        if (a != NULL && a->team == team) n++;
    }
    return n;
}

static int BotFindTarget(int myId, const Agent *me) {
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (a == NULL || a == me || a->archetype != ARCH_HERO) continue;
        if (!Entity_CanDamageTarget(myId, i)) continue;
        float dx = a->position.x - me->position.x;
        float dz = a->position.z - me->position.z;
        float score = sqrtf(dx * dx + dz * dz) + a->health * 0.05f;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

static void UpdateHeroBots(float dt) {
    Vector3 arenaC; float arenaR;
    Entity_GetArenaBounds(&arenaC, &arenaR);

    for (int b = 0; b < AI_MAX_HERO_BOTS; b++) {
        HeroBot *bot = &s_bots[b];
        if (!bot->used) continue;
        const Agent *me = Entity_GetAgent(bot->agentId);
        if (me == NULL) { bot->used = false; continue; }

        bot->thinkTimer -= dt;
        bot->dodgeTimer -= dt;
        bot->strafePhase += dt;

        int targetId = BotFindTarget(bot->agentId, me);
        const Agent *target = (targetId >= 0) ? Entity_GetAgent(targetId) : NULL;

        if (bot->dodgeTimer <= 0.0f) {
            CombatProjectileInfo proj[32];
            int np = Combat_QueryProjectiles(proj, 32);
            for (int p = 0; p < np; p++) {
                if (proj[p].team == me->team) continue;
                float dx = proj[p].pos.x - me->position.x;
                float dz = proj[p].pos.z - me->position.z;
                if (dx * dx + dz * dz > BOT_DODGE_RANGE * BOT_DODGE_RANGE) continue;
                Vector3 dir = { -dz, 0.0f, dx };
                Entity_Dash(bot->agentId, dir, 10.0f);
                bot->dodgeTimer = BOT_DODGE_COOLDOWN;
                break;
            }
        }

        if (target != NULL && me->vState == AGENT_GROUNDED &&
            !Entity_IsCrowdControlled(bot->agentId) && me->dashTimer <= 0.0f) {
            float dx = target->position.x - me->position.x;
            float dz = target->position.z - me->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            float mx = 0.0f, mz = 0.0f;
            if (dist > 0.01f) {
                float nx = dx / dist, nz = dz / dist;
                if (dist > BOT_RANGE_FAR)       { mx = nx;  mz = nz;  }
                else if (dist < BOT_RANGE_NEAR) { mx = -nx; mz = -nz; }
                else {
                    float side = sinf(bot->strafePhase * 1.3f) > 0.0f ? 1.0f : -1.0f;
                    mx = -nz * side; mz = nx * side;
                }
            }
            Vector3 pos = me->position;
            float speed = BOT_MOVE_MPS * Entity_GetSpeedMult(bot->agentId);
            pos.x += mx * speed * dt;
            pos.z += mz * speed * dt;
            pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
            float ex = pos.x - arenaC.x, ez = pos.z - arenaC.z;
            if (sqrtf(ex * ex + ez * ez) <= arenaR - BOT_EDGE_MARGIN)
                Entity_SetPosition(bot->agentId, pos);
        }

        if (target != NULL && bot->thinkTimer <= 0.0f &&
            !Entity_IsStunned(bot->agentId)) {
            bot->thinkTimer = BOT_THINK_SECONDS + 0.2f * (float)(rand() % 5);
            int start = rand() % AGENT_SKILL_SLOTS;
            for (int k = 0; k < AGENT_SKILL_SLOTS; k++) {
                int skillIndex = me->equippedSkills[(start + k) % AGENT_SKILL_SLOTS];
                if (skillIndex < 0 || !SkillManager_CanCast(skillIndex, bot->agentId))
                    continue;
                if (CastSkill(skillIndex, bot->agentId, me->position,
                              target->position,
                              (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 1.0f })) {
                    SkillManager_TriggerCooldown(skillIndex, bot->agentId, 1.0f);
                    if (s_botCastCount < 16)
                        s_botCasts[s_botCastCount++] = (HeroBotCast){
                            bot->agentId, skillIndex, target->position };
                    break;
                }
            }
        }
    }
}

void AI_Update(float dt) {
    UpdateHeroBots(dt);
    UpdateHordeZones(dt);
    UpdateCampMonsters(dt);
    UpdateMinionBolts(dt);

    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *m = Entity_GetAgent(i);
        if (!m || m->archetype != ARCH_MINION) continue;
        if (m->isStatic || AI_IsCampMonster(i)) continue;
        if (m->vState != AGENT_GROUNDED || Entity_IsCrowdControlled(i)) continue;

        int targetId = FindMarchTarget(m);
        if (targetId < 0) continue;
        const Agent *target = Entity_GetAgent(targetId);

        float dx = target->position.x - m->position.x;
        float dz = target->position.z - m->position.z;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist <= MINION_TRIGGER_RANGE) {
            Vector3 at = m->position;
            int elem = m->currentElement;
            AgentTeam team = m->team;
            if (s_explosionCount < MAX_MINION_EXPLOSIONS) {
                s_explosions[s_explosionCount++] = (MinionExplosion){ at, elem };
            }
            Entity_ApplyDamage(i, 1e9f, (Vector3){ 0 });
            Entity_ApplyAoEDamage(at, MINION_BLAST_RADIUS, MINION_BLAST_DAMAGE,
                                  MINION_BLAST_KB, team);
            continue;
        }

        float speed = MINION_SPEED_MPS * Entity_GetSpeedMult(i);
        Vector3 pos = m->position;
        pos.x += (dx / dist) * speed * dt;
        pos.z += (dz / dist) * speed * dt;
        pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
        Entity_SetPosition(i, pos);
        Entity_SetFacingYaw(i, atan2f(dx, dz));
        Entity_SetNetMonsterMeta(i, (unsigned char)((MONSTER_RANGED_CASTER & 0x3u) |
                                                    ((MON_STATE_CHASE & 0x7u) << 2)));
    }
}

int AI_SpawnMinionWave(int bossAgentId, int count) {
    const Agent *boss = Entity_GetAgent(bossAgentId);
    if (boss == NULL || count <= 0) return 0;

    int spawned = 0;
    for (int k = 0; k < count; k++) {
        float ang = ((float)k / (float)count) * 2.0f * 3.14159265f;
        Vector3 pos = { boss->position.x + cosf(ang) * WAVE_RING_RADIUS, 0.0f,
                        boss->position.z + sinf(ang) * WAVE_RING_RADIUS };
        pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
        int id = Entity_SpawnAgent(pos, MINION_HP, boss->currentElement,
                                   boss->team, ARCH_MINION);
        if (id < 0) break;
        spawned++;
    }
    return spawned;
}

int AI_PollExplosions(MinionExplosion *out, int max) {
    if (out == NULL || max <= 0) return 0;
    int n = (s_explosionCount < max) ? s_explosionCount : max;
    for (int i = 0; i < n; i++) out[i] = s_explosions[i];
    s_explosionCount = 0;
    return n;
}

int AI_PollHeroCasts(HeroBotCast *out, int max) {
    if (out == NULL || max <= 0) { s_botCastCount = 0; return 0; }
    int n = (s_botCastCount < max) ? s_botCastCount : max;
    for (int i = 0; i < n; i++) out[i] = s_botCasts[i];
    s_botCastCount = 0;
    return n;
}

// =============================================================================
// Lightweight Minion Bolt Pool (Replaces heavy CastSkill for Spitter Minions)
// =============================================================================

#define MINION_BOLT_ID_BASE 900000

typedef struct {
    bool    active;
    int     ownerAgentId;
    int     element;
    Vector3 pos;
    Vector3 vel;
    float   radius;
    float   damage;
    float   knockback;
    float   life;
} AIMinionBolt;

static AIMinionBolt s_bolts[AI_MAX_MINION_BOLTS];

static void ClearMinionBolts(void) {
    memset(s_bolts, 0, sizeof(s_bolts));
}

static void SpawnMinionBolt(int ownerId, int element, Vector3 startPos, Vector3 targetPos) {
    int slot = -1;
    for (int i = 0; i < AI_MAX_MINION_BOLTS; i++) {
        if (!s_bolts[i].active) { slot = i; break; }
    }
    if (slot < 0) return;

    float dx = targetPos.x - startPos.x;
    float dz = targetPos.z - startPos.z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return;

    float speed = 8.4f;
    s_bolts[slot] = (AIMinionBolt){
        .active       = true,
        .ownerAgentId = ownerId,
        .element      = element,
        .pos          = (Vector3){ startPos.x, startPos.y + 0.65f, startPos.z },
        .vel          = (Vector3){ (dx / len) * speed, 0.0f, (dz / len) * speed },
        .radius       = 0.24f,
        .damage       = 6.0f,
        .knockback    = 1.2f,
        .life         = 2.2f
    };
}

static void UpdateMinionBolts(float dt) {
    const ClashEvent *evs = NULL;
    int nev = Combat_PeekEvents(&evs);
    for (int i = 0; i < nev; i++) {
        int id = evs[i].skillInstanceId - MINION_BOLT_ID_BASE;
        if (id >= 0 && id < AI_MAX_MINION_BOLTS && s_bolts[id].active) {
            if (evs[i].outcome == CLASH_B_WINS ||
                evs[i].outcome == CLASH_MUTUAL_DESTROY ||
                evs[i].outcome == CLASH_HIT_AGENT) {
                s_bolts[id].active = false;
            }
        }
    }

    for (int i = 0; i < AI_MAX_MINION_BOLTS; i++) {
        AIMinionBolt *b = &s_bolts[i];
        if (!b->active) continue;
        b->life -= dt;
        if (b->life <= 0.0f) { b->active = false; continue; }

        b->pos.x += b->vel.x * dt;
        b->pos.z += b->vel.z * dt;
        float gy = MapManager_GetGroundHeightAt(b->pos.x, b->pos.z);
        if (gy < -50.0f) { b->active = false; continue; }
        b->pos.y = gy + 0.65f;

        // If the bolt reaches an ally hero who is currently dashing, jumping,
        // or inside their post-dodge grace window, let the bolt pass harmlessly
        // instead of submitting a combat collider that would hit them.
        bool skipForDodge = false;
        int nearHeroes[8];
        int nh = Entity_GetNearbyTargetsTeam(b->pos, b->radius + 0.55f, TEAM_ALLY, nearHeroes, 8);
        for (int h = 0; h < nh; h++) {
            const Agent *ha = Entity_GetAgent(nearHeroes[h]);
            if (ha && ha->archetype == ARCH_HERO &&
                (ha->dashTimer > 0.0f || ha->dodgeGraceTimer > 0.0f || ha->vState == AGENT_JUMPING)) {
                skipForDodge = true;
                break;
            }
        }
        if (skipForDodge) continue;

        if (!Combat_SubmitProjectile(b->ownerAgentId, (CombatElement)b->element,
                                     b->pos, b->radius, b->damage, b->knockback,
                                     MINION_BOLT_ID_BASE + i)) {
            b->active = false;
        }
    }
}

int AI_QueryMinionBolts(AIMinionBoltRenderInfo *out, int max) {
    if (!out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < AI_MAX_MINION_BOLTS && n < max; i++) {
        if (!s_bolts[i].active) continue;
        out[n++] = (AIMinionBoltRenderInfo){
            .pos     = s_bolts[i].pos,
            .vel     = s_bolts[i].vel,
            .radius  = s_bolts[i].radius,
            .element = s_bolts[i].element
        };
    }
    return n;
}

// =============================================================================
// PvE Explore Monster Camps & Vampire-Survivors Horde Swarm AI
// =============================================================================

typedef struct {
    bool           used;
    bool           noRespawn;
    bool           isHordeWave;
    int            zoneIdx;         // >= 0 if owned by a HordeZone
    int            agentId;
    Vector3        homePos;
    Vector3        lastKnownPos;
    float          lastHealth;
    int            element;
    MonsterKind    kind;
    MonsterAIState aiState;
    float          stateTimer;
    float          attackCooldown;
    float          respawnTimer;
    float          patrolAngle;
    float          ringSlotAngle;   // 360-deg encirclement slot around hero
    float          yaw;
    float          animPhase;
    float          telegraphMax;
    Vector3        strikeDir;
    bool           strikeDealt;
    int            targetId;
    float          rallyTimer;      // Pack alpha speed boost window
} CampMonster;

static CampMonster s_campMonsters[AI_MAX_CAMP_MONSTERS];
static MonsterKillEvent   s_killEvents[64];
static int                s_killEventCount = 0;
static MonsterStrikeEvent s_strikeEvents[32];
static int                s_strikeEventCount = 0;

static Vector3 s_safeCenter = { 0.0f, 0.0f, 0.0f };
static float   s_safeRadius = 0.0f;

static const float CAMP_AGGRO_RANGE  = 24.0f;
static const float CAMP_LEASH_RANGE  = 46.0f;
static const float CAMP_RESPAWN_SECS = 14.0f;

typedef struct {
    bool           used;
    AIHordeZoneDef def;
    bool           awake;
    float          waveTimer;
    float          escalation; // 0..10s+ ramp inside zone
    int            activeCount;
} HordeZoneState;

static HordeZoneState s_hordeZones[AI_MAX_HORDE_ZONES];
static int            s_hordeZoneCount = 0;
static float          s_hordeDirectorAccum = 0.0f;
static unsigned int   s_tickFrame = 0;

// Cached hero snapshots per frame (with velocity estimate for predictive intercept)
typedef struct {
    int     agentId;
    Vector3 pos;
    Vector3 vel;
    bool    stealthed;
    bool    inSafe;
} HeroSnap;

static HeroSnap s_prevHeroPos[MAX_AGENTS];
static bool     s_prevHeroValid[MAX_AGENTS];

static float CampMonsterHP(MonsterKind kind) {
    switch (kind) {
        case MONSTER_RANGED_CASTER: return 28.0f;
        case MONSTER_LEAPER:        return 38.0f;
        case MONSTER_ELITE_GUARD:   return 160.0f;
        default:                    return 30.0f;
    }
}

static float CampMonsterScale(MonsterKind kind) {
    switch (kind) {
        case MONSTER_ELITE_GUARD:   return 1.65f;
        case MONSTER_LEAPER:        return 1.05f;
        case MONSTER_RANGED_CASTER: return 0.95f;
        default:                    return 0.92f;
    }
}

static void PushStrikeEvent(Vector3 pos, int element, MonsterKind kind, bool isBite) {
    if (s_strikeEventCount < 32) {
        s_strikeEvents[s_strikeEventCount++] = (MonsterStrikeEvent){
            .pos = pos, .element = element, .kind = kind, .isBite = isBite
        };
    }
}

static bool IsValidGroundPoint(float x, float z, Vector3 *outPos) {
    float gy = MapManager_GetGroundHeightAt(x, z);
    if (gy < -50.0f) return false;
    if (s_safeRadius > 0.5f) {
        float dx = x - s_safeCenter.x;
        float dz = z - s_safeCenter.z;
        if (dx * dx + dz * dz <= (s_safeRadius + 2.0f) * (s_safeRadius + 2.0f)) return false;
    }
    if (outPos) *outPos = (Vector3){ x, gy, z };
    return true;
}

static void SpawnCampAgent(CampMonster *cm, int slotIdx) {
    Vector3 p = cm->homePos;
    p.y = MapManager_GetGroundHeightAt(p.x, p.z);
    if (p.y < -50.0f) p.y = 0.0f;
    int id = Entity_SpawnAgent(p, CampMonsterHP(cm->kind), cm->element, TEAM_ENEMY, ARCH_MINION);
    cm->agentId = id;
    if (id >= 0 && id < MAX_AGENTS) {
        s_agentToCamp[id] = slotIdx;
    }
    cm->lastKnownPos = p;
    cm->lastHealth = CampMonsterHP(cm->kind);
    cm->respawnTimer = 0.0f;
    cm->attackCooldown = 0.3f + 0.15f * (float)(rand() % 6);
    cm->aiState = cm->isHordeWave ? MON_STATE_CHASE : MON_STATE_GRAZE;
    cm->stateTimer = 0.0f;
    cm->strikeDealt = false;
    cm->targetId = -1;
}

static int AllocCampSlot(void) {
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        if (!s_campMonsters[i].used) return i;
    }
    return -1;
}

int AI_SpawnMonsterCamp(Vector3 center, int count, int element, MonsterKind kind) {
    int spawned = 0;
    for (int k = 0; k < count; k++) {
        int slot = AllocCampSlot();
        if (slot < 0) break;

        float ang = (count > 1) ? ((float)k / (float)count) * 6.2831853f : 0.0f;
        float rad = (count > 1) ? (2.0f + 0.4f * (float)(k % 3)) : 0.0f;
        CampMonster *cm = &s_campMonsters[slot];
        *cm = (CampMonster){ 0 };
        cm->used = true;
        cm->zoneIdx = -1;
        cm->homePos = (Vector3){ center.x + cosf(ang) * rad, center.y, center.z + sinf(ang) * rad };
        cm->element = (element >= 0 && element <= 4) ? element : 0;
        cm->kind = kind;
        cm->patrolAngle = ang;
        cm->ringSlotAngle = ((float)(slot % 24) / 24.0f) * 6.2831853f;
        cm->animPhase = (float)(slot % 17) * 0.37f;
        SpawnCampAgent(cm, slot);
        if (cm->agentId >= 0) spawned++;
        else cm->used = false;
    }
    return spawned;
}

int AI_SpawnOneShotMonster(Vector3 pos, int element, MonsterKind kind) {
    int slot = AllocCampSlot();
    if (slot < 0) return -1;
    CampMonster *cm = &s_campMonsters[slot];
    *cm = (CampMonster){ 0 };
    cm->used = true;
    cm->noRespawn = true;
    cm->zoneIdx = -1;
    cm->homePos = pos;
    cm->element = (element >= 0 && element <= 4) ? element : 0;
    cm->kind = kind;
    cm->ringSlotAngle = ((float)(slot % 24) / 24.0f) * 6.2831853f;
    cm->animPhase = (float)(slot % 17) * 0.37f;
    SpawnCampAgent(cm, slot);
    if (cm->agentId < 0) {
        cm->used = false;
        return -1;
    }
    return cm->agentId;
}

static int SpawnHordeMonster(int zoneIdx, Vector3 pos, int element, MonsterKind kind) {
    int slot = AllocCampSlot();
    if (slot < 0) return -1;
    CampMonster *cm = &s_campMonsters[slot];
    *cm = (CampMonster){ 0 };
    cm->used = true;
    cm->noRespawn = true;
    cm->isHordeWave = true;
    cm->zoneIdx = zoneIdx;
    cm->homePos = pos;
    cm->element = (element >= 0 && element <= 4) ? element : 0;
    cm->kind = kind;
    cm->ringSlotAngle = ((float)(slot % 24) / 24.0f) * 6.2831853f + 0.13f * (float)(rand() % 5);
    cm->animPhase = (float)(slot % 19) * 0.33f;
    SpawnCampAgent(cm, slot);
    if (cm->agentId < 0) {
        cm->used = false;
        return -1;
    }
    return cm->agentId;
}

void AI_ClearMonsterCamps(void) {
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        if (s_campMonsters[i].used && s_campMonsters[i].agentId >= 0) {
            int aid = s_campMonsters[i].agentId;
            if (aid < MAX_AGENTS) s_agentToCamp[aid] = -1;
            if (Entity_GetAgent(aid) != NULL) {
                Entity_ApplyDamage(aid, 1e9f, (Vector3){ 0 });
            }
        }
        s_campMonsters[i] = (CampMonster){ 0 };
        s_campMonsters[i].agentId = -1;
        s_campMonsters[i].zoneIdx = -1;
    }
    for (int i = 0; i < MAX_AGENTS; i++) s_agentToCamp[i] = -1;
    ClearMinionBolts();
    s_killEventCount = 0;
    s_strikeEventCount = 0;
}

bool AI_IsCampMonster(int agentId) {
    if (agentId < 0 || agentId >= MAX_AGENTS) return false;
    return s_agentToCamp[agentId] >= 0;
}

int AI_PollMonsterKills(MonsterKillEvent *out, int max) {
    if (out == NULL || max <= 0) { s_killEventCount = 0; return 0; }
    int n = (s_killEventCount < max) ? s_killEventCount : max;
    for (int i = 0; i < n; i++) out[i] = s_killEvents[i];
    s_killEventCount = 0;
    return n;
}

int AI_PollMonsterStrikes(MonsterStrikeEvent *out, int max) {
    if (out == NULL || max <= 0) { s_strikeEventCount = 0; return 0; }
    int n = (s_strikeEventCount < max) ? s_strikeEventCount : max;
    for (int i = 0; i < n; i++) out[i] = s_strikeEvents[i];
    s_strikeEventCount = 0;
    return n;
}

void AI_RegisterHordeZone(const AIHordeZoneDef *def) {
    if (!def || s_hordeZoneCount >= AI_MAX_HORDE_ZONES) return;
    HordeZoneState *hz = &s_hordeZones[s_hordeZoneCount++];
    *hz = (HordeZoneState){
        .used = true,
        .def = *def,
        .awake = false,
        .waveTimer = 0.5f,
        .escalation = 0.0f,
        .activeCount = 0
    };
}

void AI_ClearHordeZones(void) {
    memset(s_hordeZones, 0, sizeof(s_hordeZones));
    s_hordeZoneCount = 0;
    s_hordeDirectorAccum = 0.0f;
}

void AI_SetSafeZone(Vector3 center, float radius) {
    s_safeCenter = center;
    s_safeRadius = radius;
}

int AI_GetActiveCampMonsterCount(void) {
    int count = 0;
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        if (s_campMonsters[i].used && s_campMonsters[i].agentId >= 0 &&
            Entity_GetAgent(s_campMonsters[i].agentId) != NULL) {
            count++;
        }
    }
    return count;
}

int AI_GetActiveHordeZoneIndex(Vector3 playerPos) {
    for (int z = 0; z < s_hordeZoneCount; z++) {
        if (!s_hordeZones[z].used) continue;
        float dx = playerPos.x - s_hordeZones[z].def.center.x;
        float dz = playerPos.z - s_hordeZones[z].def.center.z;
        float r = s_hordeZones[z].def.radius;
        if (dx * dx + dz * dz <= r * r) return z;
    }
    return -1;
}

static MonsterKind PickHordeKind(const AIHordeZoneDef *def, int idx, bool allowElite) {
    if (allowElite && idx == 0 && def->eliteCount > 0) return MONSTER_ELITE_GUARD;
    float r = (float)(rand() % 1000) * 0.001f;
    if (r < def->spitterRatio) return MONSTER_RANGED_CASTER;
    if (r < def->spitterRatio + def->leaperRatio) return MONSTER_LEAPER;
    return MONSTER_SWARM_MELEE;
}

static void SpawnHordeBatch(int zoneIdx, Vector3 aroundHero, int count, float minR, float maxR, bool withElite) {
    const AIHordeZoneDef *def = &s_hordeZones[zoneIdx].def;
    // Pick 2-3 compass arc centers around the player for Vampire-Survivors encirclement
    float baseAng = ((float)(rand() % 360)) * 0.01745329f;
    int arcs = 2 + (rand() % 2);
    for (int k = 0; k < count; k++) {
        float arcAng = baseAng + ((float)(k % arcs) / (float)arcs) * 6.2831853f;
        float jitter = (((float)(rand() % 100) * 0.01f) - 0.5f) * 0.95f;
        float ang = arcAng + jitter;
        float rad = minR + ((float)(rand() % 100) * 0.01f) * (maxR - minR);
        float px = aroundHero.x + cosf(ang) * rad;
        float pz = aroundHero.z + sinf(ang) * rad;
        Vector3 sp;
        if (!IsValidGroundPoint(px, pz, &sp)) {
            // Fallback near zone center
            float za = ((float)(rand() % 360)) * 0.01745329f;
            float zr = 4.0f + ((float)(rand() % 100) * 0.01f) * fminf(14.0f, def->radius * 0.45f);
            if (!IsValidGroundPoint(def->center.x + cosf(za) * zr, def->center.z + sinf(za) * zr, &sp)) {
                continue;
            }
        }
        MonsterKind kind = PickHordeKind(def, k, withElite);
        if (SpawnHordeMonster(zoneIdx, sp, def->element, kind) < 0) break;
    }
}

static void HibernateHordeZone(int zoneIdx) {
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        CampMonster *cm = &s_campMonsters[i];
        if (!cm->used || cm->zoneIdx != zoneIdx) continue;
        if (cm->agentId >= 0) {
            if (cm->agentId < MAX_AGENTS) s_agentToCamp[cm->agentId] = -1;
            if (Entity_GetAgent(cm->agentId) != NULL) {
                Entity_ApplyDamage(cm->agentId, 1e9f, (Vector3){ 0 });
            }
        }
        *cm = (CampMonster){ 0 };
        cm->agentId = -1;
        cm->zoneIdx = -1;
    }
}

static void UpdateHordeZones(float dt) {
    if (s_hordeZoneCount <= 0) return;
    s_hordeDirectorAccum += dt;
    if (s_hordeDirectorAccum < 0.25f) return;
    float stepDt = s_hordeDirectorAccum;
    s_hordeDirectorAccum = 0.0f;

    // Collect living heroes
    Vector3 heroPos[16];
    int heroCount = 0;
    for (int a = 0; a < MAX_AGENTS && heroCount < 16; a++) {
        const Agent *ag = Entity_GetAgent(a);
        if (ag && ag->archetype == ARCH_HERO && ag->team == TEAM_ALLY) {
            heroPos[heroCount++] = ag->position;
        }
    }
    if (heroCount == 0) return;

    // Tally active monsters per zone
    for (int z = 0; z < s_hordeZoneCount; z++) s_hordeZones[z].activeCount = 0;
    int totalActive = 0;
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        CampMonster *cm = &s_campMonsters[i];
        if (!cm->used || cm->agentId < 0) continue;
        if (Entity_GetAgent(cm->agentId) == NULL) continue;
        totalActive++;
        if (cm->zoneIdx >= 0 && cm->zoneIdx < s_hordeZoneCount) {
            s_hordeZones[cm->zoneIdx].activeCount++;
        }
    }

    const int GLOBAL_HORDE_CAP = 192;
    for (int z = 0; z < s_hordeZoneCount; z++) {
        HordeZoneState *hz = &s_hordeZones[z];
        if (!hz->used) continue;

        float minDistSq = 1e12f;
        Vector3 nearHero = heroPos[0];
        for (int h = 0; h < heroCount; h++) {
            float dx = heroPos[h].x - hz->def.center.x;
            float dz = heroPos[h].z - hz->def.center.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < minDistSq) { minDistSq = d2; nearHero = heroPos[h]; }
        }

        float wakeDist = hz->def.radius + 25.0f;
        float sleepDist = hz->def.radius + 45.0f;
        if (minDistSq > sleepDist * sleepDist) {
            if (hz->awake) {
                hz->awake = false;
                hz->escalation = 0.0f;
                HibernateHordeZone(z);
            }
            continue;
        }

        if (!hz->awake && minDistSq <= wakeDist * wakeDist) {
            hz->awake = true;
            hz->waveTimer = 1.2f;
            int room = GLOBAL_HORDE_CAP - totalActive;
            int want = hz->def.baseCount - hz->activeCount;
            if (want > room) want = room;
            if (want > 0) {
                SpawnHordeBatch(z, nearHero, want, 16.0f, 24.0f, true);
                totalActive += want;
                hz->activeCount += want;
            }
            continue;
        }

        if (hz->awake && minDistSq <= hz->def.radius * hz->def.radius) {
            hz->escalation += stepDt;
            hz->waveTimer -= stepDt;
            if (hz->waveTimer <= 0.0f) {
                float escFactor = fminf(1.0f, hz->escalation / 45.0f);
                float interval = hz->def.waveIntervalSec * (1.0f - 0.35f * escFactor);
                if (interval < 2.6f) interval = 2.6f;
                hz->waveTimer = interval;

                int roomGlobal = GLOBAL_HORDE_CAP - totalActive;
                int roomZone = hz->def.maxConcurrent - hz->activeCount;
                int batch = hz->def.waveBatchSize + (int)(escFactor * 4.0f);
                if (batch > roomZone) batch = roomZone;
                if (batch > roomGlobal) batch = roomGlobal;
                if (batch > 0) {
                    SpawnHordeBatch(z, nearHero, batch, 18.0f, 26.0f, false);
                    totalActive += batch;
                }
            }
        }
    }
}

static void UpdateCampMonsters(float dt) {
    s_tickFrame++;

    // Layer 1: Cache living ally heroes + estimate velocity for predictive intercept
    HeroSnap heroes[16];
    int heroCount = 0;
    float invDt = (dt > 1e-4f) ? (1.0f / dt) : 0.0f;
    for (int a = 0; a < MAX_AGENTS && heroCount < 16; a++) {
        const Agent *cand = Entity_GetAgent(a);
        if (!cand || cand->team != TEAM_ALLY || cand->archetype != ARCH_HERO) {
            s_prevHeroValid[a] = false;
            continue;
        }
        Vector3 vel = { 0 };
        if (s_prevHeroValid[a]) {
            vel.x = (cand->position.x - s_prevHeroPos[a].pos.x) * invDt;
            vel.z = (cand->position.z - s_prevHeroPos[a].pos.z) * invDt;
        }
        s_prevHeroPos[a].pos = cand->position;
        s_prevHeroValid[a] = true;

        bool inSafe = cand->inSafeZone;
        if (!inSafe && s_safeRadius > 0.5f) {
            float sx = cand->position.x - s_safeCenter.x;
            float sz = cand->position.z - s_safeCenter.z;
            if (sx * sx + sz * sz <= s_safeRadius * s_safeRadius) inSafe = true;
        }
        heroes[heroCount++] = (HeroSnap){
            .agentId = a, .pos = cand->position, .vel = vel,
            .stealthed = cand->isStealthed, .inSafe = inSafe
        };
    }

    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        CampMonster *cm = &s_campMonsters[i];
        if (!cm->used) continue;

        const Agent *me = (cm->agentId >= 0) ? Entity_GetAgent(cm->agentId) : NULL;
        if (me == NULL) {
            if (cm->agentId >= 0) {
                if (cm->agentId < MAX_AGENTS) s_agentToCamp[cm->agentId] = -1;
                if (s_killEventCount < 64) {
                    s_killEvents[s_killEventCount++] = (MonsterKillEvent){
                        cm->lastKnownPos, cm->element, cm->kind
                    };
                }
                cm->agentId = -1;
                if (cm->noRespawn) {
                    cm->used = false;
                    cm->zoneIdx = -1;
                } else {
                    cm->respawnTimer = CAMP_RESPAWN_SECS;
                }
            } else if (!cm->noRespawn) {
                cm->respawnTimer -= dt;
                if (cm->respawnTimer <= 0.0f) {
                    SpawnCampAgent(cm, i);
                }
            }
            continue;
        }

        cm->lastKnownPos = me->position;
        if (cm->attackCooldown > 0.0f) cm->attackCooldown -= dt;
        if (cm->rallyTimer > 0.0f) cm->rallyTimer -= dt;

        // Pack Rally on damage taken: wake nearby mates within 10m
        bool tookDamage = (me->health < cm->lastHealth - 0.1f);
        cm->lastHealth = me->health;
        if (tookDamage) {
            int nearIds[16];
            int nc = Entity_GetNearbyTargetsTeam(me->position, 10.0f, TEAM_ENEMY, nearIds, 16);
            for (int k = 0; k < nc; k++) {
                int cidx = (nearIds[k] >= 0 && nearIds[k] < MAX_AGENTS) ? s_agentToCamp[nearIds[k]] : -1;
                if (cidx >= 0 && s_campMonsters[cidx].aiState == MON_STATE_GRAZE) {
                    s_campMonsters[cidx].aiState = MON_STATE_RALLY;
                    s_campMonsters[cidx].rallyTimer = 3.0f;
                }
            }
        }

        if (me->vState != AGENT_GROUNDED || Entity_IsCrowdControlled(cm->agentId)) {
            // Leaper landing strike check while airborne: forward cone + dodge grace check
            if (cm->kind == MONSTER_LEAPER && cm->aiState == MON_STATE_STRIKE && !cm->strikeDealt) {
                if (cm->targetId >= 0) {
                    const Agent *t = Entity_GetAgent(cm->targetId);
                    if (t) {
                        float ldx = t->position.x - me->position.x;
                        float ldz = t->position.z - me->position.z;
                        float lDistSq = ldx * ldx + ldz * ldz;
                        float lDot = ldx * cm->strikeDir.x + ldz * cm->strikeDir.z;
                        if (lDistSq <= 1.45f * 1.45f && (lDistSq < 0.01f || lDot >= sqrtf(lDistSq) * 0.35f)) {
                            cm->strikeDealt = true;
                            if (t->dashTimer <= 0.0f && t->dodgeGraceTimer <= 0.0f && t->vState != AGENT_JUMPING) {
                                Vector3 kb = (Vector3){ cm->strikeDir.x * 2.0f, 0.0f, cm->strikeDir.z * 2.0f };
                                Entity_ApplyDamage(cm->targetId, 8.0f, kb);
                            }
                            PushStrikeEvent(me->position, cm->element, cm->kind, true);
                        }
                    }
                }
            }
            continue;
        }

        // Safe zone push-out / leash
        if (s_safeRadius > 0.5f) {
            float sx = me->position.x - s_safeCenter.x;
            float sz = me->position.z - s_safeCenter.z;
            float sDist = sqrtf(sx * sx + sz * sz);
            if (sDist < s_safeRadius + 2.2f) {
                cm->aiState = MON_STATE_LEASH;
                cm->targetId = -1;
                if (sDist > 0.01f) {
                    Vector3 pos = me->position;
                    pos.x += (sx / sDist) * 4.5f * dt;
                    pos.z += (sz / sDist) * 4.5f * dt;
                    pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
                    if (pos.y > -50.0f) Entity_SetPosition(cm->agentId, pos);
                }
                continue;
            }
        }

        // Layer 2: Staggered 15Hz target acquisition (every 4th frame per monster)
        if (((unsigned int)i + s_tickFrame) % 4u == 0u || cm->targetId < 0) {
            float aggroR = cm->isHordeWave ? 42.0f : ((cm->aiState == MON_STATE_RALLY) ? 30.0f : CAMP_AGGRO_RANGE);
            float bestSq = aggroR * aggroR;
            int bestHero = -1;
            for (int h = 0; h < heroCount; h++) {
                if (heroes[h].inSafe) continue;
                float dx = heroes[h].pos.x - me->position.x;
                float dz = heroes[h].pos.z - me->position.z;
                float d2 = dx * dx + dz * dz;
                if (heroes[h].stealthed && d2 > 3.5f * 3.5f) continue;
                if (d2 < bestSq) { bestSq = d2; bestHero = heroes[h].agentId; }
            }
            cm->targetId = bestHero;
            if (!cm->isHordeWave) {
                float hdx = me->position.x - cm->homePos.x;
                float hdz = me->position.z - cm->homePos.z;
                if (hdx * hdx + hdz * hdz > CAMP_LEASH_RANGE * CAMP_LEASH_RANGE) {
                    cm->targetId = -1;
                    cm->aiState = MON_STATE_LEASH;
                }
            }
        }

        // Pack Alpha rally aura
        if (cm->kind == MONSTER_ELITE_GUARD && (((unsigned int)i + s_tickFrame) % 12u == 0u)) {
            int nearIds[16];
            int nc = Entity_GetNearbyTargetsTeam(me->position, 8.0f, TEAM_ENEMY, nearIds, 16);
            for (int k = 0; k < nc; k++) {
                int cidx = (nearIds[k] >= 0 && nearIds[k] < MAX_AGENTS) ? s_agentToCamp[nearIds[k]] : -1;
                if (cidx >= 0) s_campMonsters[cidx].rallyTimer = 2.0f;
            }
        }

        const Agent *t = (cm->targetId >= 0) ? Entity_GetAgent(cm->targetId) : NULL;
        if (!t || (s_safeRadius > 0.5f &&
            ((t->position.x - s_safeCenter.x)*(t->position.x - s_safeCenter.x) +
             (t->position.z - s_safeCenter.z)*(t->position.z - s_safeCenter.z)) <= s_safeRadius * s_safeRadius)) {
            cm->targetId = -1;
            t = NULL;
        }

        // Idle / Graze / Leash
        if (t == NULL) {
            cm->aiState = MON_STATE_GRAZE;
            cm->patrolAngle += 0.55f * dt;
            Vector3 goal = {
                cm->homePos.x + cosf(cm->patrolAngle) * 2.2f,
                0.0f,
                cm->homePos.z + sinf(cm->patrolAngle) * 2.2f
            };
            float dx = goal.x - me->position.x;
            float dz = goal.z - me->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > 0.15f) {
                Vector3 pos = me->position;
                float step = 1.3f * dt;
                if (step > dist) step = dist;
                pos.x += (dx / dist) * step;
                pos.z += (dz / dist) * step;
                float gy = MapManager_GetGroundHeightAt(pos.x, pos.z);
                if (gy > -50.0f) {
                    pos.y = gy;
                    Entity_SetPosition(cm->agentId, pos);
                }
                cm->yaw = atan2f(dx, dz);
                cm->animPhase += step * 4.5f;
            }
            continue;
        }

        float dx = t->position.x - me->position.x;
        float dz = t->position.z - me->position.z;
        float dist = sqrtf(dx * dx + dz * dz);
        float dirX = (dist > 0.001f) ? (dx / dist) : 0.0f;
        float dirZ = (dist > 0.001f) ? (dz / dist) : 1.0f;

        // Handle active TELEGRAPH -> STRIKE -> RECOVER combat state machine.
        // Direction (cm->strikeDir & cm->yaw) is LOCKED at telegraph start so
        // dodging sideways during the red telegraph window reliably evades the attack!
        if (cm->aiState == MON_STATE_TELEGRAPH) {
            cm->stateTimer -= dt;
            if (cm->stateTimer <= 0.0f) {
                cm->aiState = MON_STATE_STRIKE;
                cm->strikeDealt = false;
                if (cm->kind == MONSTER_RANGED_CASTER) {
                    // Fire strictly along locked telegraphed strikeDir (1:1 identical on Host and Client!)
                    Vector3 aimPos = {
                        me->position.x + cm->strikeDir.x * 10.0f,
                        me->position.y,
                        me->position.z + cm->strikeDir.z * 10.0f
                    };
                    SpawnMinionBolt(cm->agentId, cm->element, me->position, aimPos);
                    PushStrikeEvent(me->position, cm->element, cm->kind, false);
                    cm->strikeDealt = true;
                    cm->stateTimer = 0.14f;
                    cm->attackCooldown = 1.95f;
                } else if (cm->kind == MONSTER_LEAPER) {
                    Vector3 leapVel = { cm->strikeDir.x * 7.8f, 0.0f, cm->strikeDir.z * 7.8f };
                    Entity_ApplyLaunch(cm->agentId, 3.6f, leapVel);
                    PushStrikeEvent(me->position, cm->element, cm->kind, true);
                    cm->stateTimer = 0.35f;
                    cm->attackCooldown = 1.5f;
                } else if (cm->kind == MONSTER_ELITE_GUARD) {
                    cm->stateTimer = 0.32f;
                    cm->attackCooldown = 1.7f;
                } else {
                    // Swarmer bite lunge along locked strikeDir
                    cm->stateTimer = 0.16f;
                    cm->attackCooldown = 0.95f;
                }
            }
            continue;
        }

        if (cm->aiState == MON_STATE_STRIKE) {
            cm->stateTimer -= dt;
            if (cm->kind == MONSTER_SWARM_MELEE || cm->kind == MONSTER_ELITE_GUARD) {
                float lungeSpd = (cm->kind == MONSTER_ELITE_GUARD) ? 6.8f : 7.2f;
                Vector3 pos = me->position;
                pos.x += cm->strikeDir.x * lungeSpd * dt;
                pos.z += cm->strikeDir.z * lungeSpd * dt;
                float gy = MapManager_GetGroundHeightAt(pos.x, pos.z);
                if (gy > -50.0f) {
                    pos.y = gy;
                    MapManager_ResolveCollision(&pos, 0.32f, 0.45f, &pos.y);
                    Entity_SetPosition(cm->agentId, pos);
                }
                float hdx = t->position.x - pos.x;
                float hdz = t->position.z - pos.z;
                float hDist = sqrtf(hdx * hdx + hdz * hdz);
                float hitRad = (cm->kind == MONSTER_ELITE_GUARD) ? 1.80f : 1.32f;
                float fwdDot = (hDist > 0.01f) ? ((hdx * cm->strikeDir.x + hdz * cm->strikeDir.z) / hDist) : 1.0f;
                // Require target to be inside the forward strike cone (fwdDot >= 0.42f, ~65 deg half-angle)
                // and not currently dashing, jumping, or in post-dodge grace window.
                if (!cm->strikeDealt && hDist <= hitRad && fwdDot >= 0.42f) {
                    cm->strikeDealt = true;
                    if (t->dashTimer <= 0.0f && t->dodgeGraceTimer <= 0.0f && t->vState != AGENT_JUMPING) {
                        float dmg = (cm->kind == MONSTER_ELITE_GUARD) ? 14.0f : 5.0f;
                        Vector3 kb = { cm->strikeDir.x * 1.5f, 0.0f, cm->strikeDir.z * 1.5f };
                        Entity_ApplyDamage(cm->targetId, dmg, kb);
                        if (cm->kind == MONSTER_ELITE_GUARD) {
                            Entity_ApplyLaunch(cm->targetId, 2.8f, kb);
                        }
                    }
                    PushStrikeEvent(pos, cm->element, cm->kind, true);
                }
            }
            if (cm->stateTimer <= 0.0f) {
                cm->aiState = MON_STATE_RECOVER;
                cm->stateTimer = (cm->kind == MONSTER_ELITE_GUARD) ? 0.85f : 0.48f;
            }
            continue;
        }

        if (cm->aiState == MON_STATE_RECOVER) {
            cm->stateTimer -= dt;
            if (cm->stateTimer <= 0.0f) cm->aiState = MON_STATE_CHASE;
            continue;
        }

        // CHASE / RALLY state: check strike trigger & lock telegraph direction immediately
        cm->aiState = MON_STATE_CHASE;
        if (cm->attackCooldown <= 0.0f && !Entity_IsStunned(cm->agentId)) {
            if (cm->kind == MONSTER_RANGED_CASTER && dist <= 12.0f && dist >= 3.0f) {
                cm->aiState = MON_STATE_TELEGRAPH;
                cm->telegraphMax = 0.48f;
                cm->stateTimer = cm->telegraphMax;
                cm->strikeDir = (Vector3){ dirX, 0.0f, dirZ };
                cm->yaw = atan2f(dirX, dirZ);
                continue;
            } else if (cm->kind == MONSTER_LEAPER && dist <= 6.5f && dist >= 2.8f) {
                cm->aiState = MON_STATE_TELEGRAPH;
                cm->telegraphMax = 0.48f;
                cm->stateTimer = cm->telegraphMax;
                cm->strikeDir = (Vector3){ dirX, 0.0f, dirZ };
                cm->yaw = atan2f(dirX, dirZ);
                continue;
            } else if (cm->kind == MONSTER_ELITE_GUARD && dist <= 4.8f) {
                cm->aiState = MON_STATE_TELEGRAPH;
                cm->telegraphMax = 0.58f;
                cm->stateTimer = cm->telegraphMax;
                cm->strikeDir = (Vector3){ dirX, 0.0f, dirZ };
                cm->yaw = atan2f(dirX, dirZ);
                continue;
            } else if (cm->kind == MONSTER_SWARM_MELEE && dist <= 2.15f) {
                cm->aiState = MON_STATE_TELEGRAPH;
                cm->telegraphMax = 0.45f;
                cm->stateTimer = cm->telegraphMax;
                cm->strikeDir = (Vector3){ dirX, 0.0f, dirZ };
                cm->yaw = atan2f(dirX, dirZ);
                continue;
            }
        }

        // Layer 3: Desired direction (360-deg encirclement + flank offset) + Boids Separation
        float goalX = t->position.x;
        float goalZ = t->position.z;
        if (cm->kind == MONSTER_SWARM_MELEE && dist > 2.2f && dist < 9.5f) {
            goalX += cosf(cm->ringSlotAngle) * 1.55f;
            goalZ += sinf(cm->ringSlotAngle) * 1.55f;
        } else if (cm->kind == MONSTER_LEAPER && dist > 3.5f) {
            float flankAng = atan2f(dz, dx) + ((i & 1) ? 0.62f : -0.62f);
            goalX = me->position.x + cosf(flankAng) * 4.0f;
            goalZ = me->position.z + sinf(flankAng) * 4.0f;
        }

        float gdx = goalX - me->position.x;
        float gdz = goalZ - me->position.z;
        float gLen = sqrtf(gdx * gdx + gdz * gdz);
        float moveX = 0.0f, moveZ = 0.0f;
        if (cm->kind == MONSTER_RANGED_CASTER) {
            if (dist > 10.5f) { moveX = dirX; moveZ = dirZ; }
            else if (dist < 5.2f) { moveX = -dirX; moveZ = -dirZ; }
            else {
                float strafe = (i & 1) ? 0.55f : -0.55f;
                moveX = -dirZ * strafe;
                moveZ =  dirX * strafe;
            }
        } else if (gLen > 0.1f) {
            moveX = gdx / gLen;
            moveZ = gdz / gLen;
        }

        // Boids separation via O(1) spatial hash
        int nearIds[12];
        int nc = Entity_GetNearbyTargets(me->position, 1.35f, nearIds, 12);
        float sepX = 0.0f, sepZ = 0.0f;
        for (int k = 0; k < nc; k++) {
            int nid = nearIds[k];
            if (nid == cm->agentId) continue;
            const Agent *nb = Entity_GetAgent(nid);
            if (!nb) continue;
            float sx = me->position.x - nb->position.x;
            float sz = me->position.z - nb->position.z;
            float d2 = sx * sx + sz * sz;
            if (d2 > 0.0001f && d2 < 1.35f * 1.35f) {
                float invD = 1.0f / sqrtf(d2);
                float push = (1.35f - sqrtf(d2)) * 2.2f;
                sepX += (sx * invD) * push;
                sepZ += (sz * invD) * push;
            }
        }

        float steerX = moveX + sepX;
        float steerZ = moveZ + sepZ;
        float steerLen = sqrtf(steerX * steerX + steerZ * steerZ);
        if (steerLen > 0.01f) {
            steerX /= steerLen;
            steerZ /= steerLen;
        }

        float baseSpd = 3.55f;
        if (cm->kind == MONSTER_LEAPER) baseSpd = 4.15f;
        else if (cm->kind == MONSTER_RANGED_CASTER) baseSpd = 2.85f;
        else if (cm->kind == MONSTER_ELITE_GUARD) baseSpd = 3.05f;
        if (cm->rallyTimer > 0.0f) baseSpd *= 1.15f;
        baseSpd *= Entity_GetSpeedMult(cm->agentId);

        // Layer 4: Terrain slope/abyss/rock steering
        Vector3 cand = me->position;
        cand.x += steerX * baseSpd * dt;
        cand.z += steerZ * baseSpd * dt;
        Vector3 sPos = cand, sNorm = { 0, 1, 0 };
        bool okGround = MapManager_SampleGroundSurfaceAt(cand.x, cand.z, &sPos, &sNorm);
        if (!okGround || sPos.y < -50.0f || sNorm.y < 0.68f) {
            // Slide tangentially
            Vector3 alt = me->position;
            alt.x += (-steerZ) * baseSpd * dt * 0.75f;
            alt.z += ( steerX) * baseSpd * dt * 0.75f;
            if (MapManager_SampleGroundSurfaceAt(alt.x, alt.z, &sPos, &sNorm) &&
                sPos.y > -50.0f && sNorm.y >= 0.68f) {
                cand = sPos;
            } else {
                continue;
            }
        } else {
            cand.y = sPos.y;
        }

        MapManager_ResolveCollision(&cand, 0.30f, 0.45f, &cand.y);
        Entity_SetPosition(cm->agentId, cand);
        cm->yaw = atan2f(steerX, steerZ);
        cm->animPhase += baseSpd * dt * 3.8f;
    }

    // Replicate monster kind, AI state, telegraph fraction, and facing yaw onto
    // Agent so net/net.c packs them into the 18B snapshot at 0 extra byte cost.
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        const CampMonster *cm = &s_campMonsters[i];
        if (!cm->used || cm->agentId < 0 || !Entity_GetAgent(cm->agentId)) continue;
        float tFrac = 0.0f;
        if (cm->aiState == MON_STATE_TELEGRAPH && cm->telegraphMax > 0.01f) {
            tFrac = 1.0f - (cm->stateTimer / cm->telegraphMax);
            if (tFrac < 0.0f) tFrac = 0.0f;
            if (tFrac > 1.0f) tFrac = 1.0f;
        } else if (cm->aiState == MON_STATE_STRIKE) {
            tFrac = 1.0f;
        }
        unsigned int tQuant = (unsigned int)(tFrac * 7.0f + 0.5f);
        if (tQuant > 7u) tQuant = 7u;
        unsigned char packed = (unsigned char)(((unsigned int)cm->kind & 0x3u) |
                                               (((unsigned int)cm->aiState & 0x7u) << 2) |
                                               ((tQuant & 0x7u) << 5));
        Entity_SetNetMonsterMeta(cm->agentId, packed);
        Entity_SetFacingYaw(cm->agentId, cm->yaw);
    }
}

void AI_ClientVisualTick(float dt) {
    if (dt <= 0.0f) return;
    static unsigned char s_clientPrevState[MAX_AGENTS];
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (!a || a->archetype != ARCH_MINION || a->isStatic) {
            s_clientPrevState[i] = 0;
            continue;
        }
        MonsterKind kind = (MonsterKind)(a->netMonsterMeta & 0x3u);
        MonsterAIState st = (MonsterAIState)((a->netMonsterMeta >> 2) & 0x7u);
        if (st == MON_STATE_STRIKE && s_clientPrevState[i] != (unsigned char)MON_STATE_STRIKE) {
            float dirX = sinf(a->facingYaw);
            float dirZ = cosf(a->facingYaw);
            if (kind == MONSTER_RANGED_CASTER) {
                Vector3 aimPos = { a->position.x + dirX * 10.0f, a->position.y, a->position.z + dirZ * 10.0f };
                SpawnMinionBolt(i, a->currentElement, a->position, aimPos);
                PushStrikeEvent(a->position, a->currentElement, kind, false);
            } else {
                Vector3 bitePos = { a->position.x + dirX * 1.15f, a->position.y, a->position.z + dirZ * 1.15f };
                PushStrikeEvent(bitePos, a->currentElement, kind, true);
            }
        }
        s_clientPrevState[i] = (unsigned char)st;
    }

    // Advance client-side visual minion bolts at 60 FPS
    for (int i = 0; i < AI_MAX_MINION_BOLTS; i++) {
        AIMinionBolt *b = &s_bolts[i];
        if (!b->active) continue;
        b->life -= dt;
        if (b->life <= 0.0f) { b->active = false; continue; }
        b->pos.x += b->vel.x * dt;
        b->pos.z += b->vel.z * dt;
        float gy = MapManager_GetGroundHeightAt(b->pos.x, b->pos.z);
        if (gy < -50.0f) { b->active = false; continue; }
        b->pos.y = gy + 0.65f;
        int hitIds[4];
        if (Entity_GetNearbyTargetsTeam(b->pos, b->radius + 0.35f, TEAM_ALLY, hitIds, 4) > 0) {
            b->active = false;
        }
    }
}

int AI_QueryMonsterRenderData(AIMonsterRenderInfo *out, int max) {
    if (!out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS && n < max; i++) {
        const CampMonster *cm = &s_campMonsters[i];
        if (!cm->used || cm->agentId < 0) continue;
        const Agent *a = Entity_GetAgent(cm->agentId);
        if (!a) continue;

        float tFrac = 0.0f;
        if (cm->aiState == MON_STATE_TELEGRAPH && cm->telegraphMax > 0.01f) {
            tFrac = 1.0f - (cm->stateTimer / cm->telegraphMax);
            if (tFrac < 0.0f) tFrac = 0.0f;
            if (tFrac > 1.0f) tFrac = 1.0f;
        } else if (cm->aiState == MON_STATE_STRIKE) {
            tFrac = 1.0f;
        }

        out[n++] = (AIMonsterRenderInfo){
            .agentId       = cm->agentId,
            .pos           = a->position,
            .yaw           = cm->yaw,
            .scale         = CampMonsterScale(cm->kind),
            .animPhase     = cm->animPhase,
            .telegraphFrac = tFrac,
            .hitFlash      = (a->hitFlashTimer > 0.0f) ? (a->hitFlashTimer / 0.12f) : 0.0f,
            .element       = a->currentElement,
            .kind          = cm->kind,
            .state         = cm->aiState
        };
    }
    return n;
}
