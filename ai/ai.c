// ai/ai.c — Module 8: minion steering + self-destruct. Stateless brain: all
// persistent state lives on the Agent itself; this module just walks the
// pool every frame (same iteration idiom as Entity_Update).
#include "ai/ai.h"
#include "skills/skill_manager.h" // hero bots cast off equippedSkills
#include "combat/combat.h"      // projectile snapshot for dash-dodging
#include "maps/map_manager.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h> // rand

// Meter-scaled (1 unit = 1m).
static const float MINION_SPEED_MPS     = 2.0f;
static const float MINION_TRIGGER_RANGE = 1.2f;  // detonate within this
static const float MINION_BLAST_RADIUS  = 2.0f;
static const float MINION_BLAST_DAMAGE  = 15.0f;
static const float MINION_BLAST_KB      = 3.0f;
static const float MINION_HP            = 20.0f;
static const float WAVE_RING_RADIUS     = 1.6f;  // spawn ring around the boss

#define MAX_MINION_EXPLOSIONS 32
static MinionExplosion s_explosions[MAX_MINION_EXPLOSIONS];
static int s_explosionCount = 0;

static void UpdateCampMonsters(float dt);

void AI_Init(void) {
    s_explosionCount = 0;
    AI_ClearMonsterCamps();
}

// Nearest opposing agent; prefers ARCH_BOSS ("lầm lũi về boss địch"), falls
// back to any opposing agent when no enemy boss lives.
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

static const float BOT_MOVE_MPS       = 3.5f;  // mirror control/'s walk speed
static const float BOT_RANGE_FAR      = 9.0f;  // beyond -> close in
static const float BOT_RANGE_NEAR     = 5.0f;  // inside -> back off
static const float BOT_EDGE_MARGIN    = 2.5f;  // stay this far from the ring-out
static const float BOT_THINK_SECONDS  = 1.2f;  // cast attempt cadence
static const float BOT_DODGE_COOLDOWN = 2.5f;
static const float BOT_DODGE_RANGE    = 4.0f;  // enemy projectile this close -> dash

typedef struct {
    bool  used;
    int   agentId;
    float thinkTimer;   // until the next cast attempt
    float dodgeTimer;   // until dashing is allowed again
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
                              0.3f + 0.2f * (float)slot, // stagger first casts
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

// Nearest attackable hero, weighted toward low HP (finish weakened targets).
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
        if (me == NULL) { bot->used = false; continue; } // died -> sweep brain

        bot->thinkTimer -= dt;
        bot->dodgeTimer -= dt;
        bot->strafePhase += dt;

        int targetId = BotFindTarget(bot->agentId, me);
        const Agent *target = (targetId >= 0) ? Entity_GetAgent(targetId) : NULL;

        // Dash away from a close enemy projectile (perpendicular escape).
        if (bot->dodgeTimer <= 0.0f) {
            CombatProjectileInfo proj[32];
            int np = Combat_QueryProjectiles(proj, 32);
            for (int p = 0; p < np; p++) {
                if (proj[p].team == me->team) continue;
                float dx = proj[p].pos.x - me->position.x;
                float dz = proj[p].pos.z - me->position.z;
                if (dx * dx + dz * dz > BOT_DODGE_RANGE * BOT_DODGE_RANGE) continue;
                Vector3 dir = { -dz, 0.0f, dx }; // perpendicular to the threat
                Entity_Dash(bot->agentId, dir, 10.0f); // cooldown-gated inside
                bot->dodgeTimer = BOT_DODGE_COOLDOWN;
                break;
            }
        }

        // Movement: hold skill range on the target, gentle strafe in band.
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
                else { // strafe around the target inside the band
                    float side = sinf(bot->strafePhase * 1.3f) > 0.0f ? 1.0f : -1.0f;
                    mx = -nz * side; mz = nx * side;
                }
            }
            Vector3 pos = me->position;
            float speed = BOT_MOVE_MPS * Entity_GetSpeedMult(bot->agentId);
            pos.x += mx * speed * dt;
            pos.z += mz * speed * dt;
            pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
            // Edge guard: never walk past the ring-out margin.
            float ex = pos.x - arenaC.x, ez = pos.z - arenaC.z;
            if (sqrtf(ex * ex + ez * ez) <= arenaR - BOT_EDGE_MARGIN)
                Entity_SetPosition(bot->agentId, pos);
        }

        // Cast attempt on the think cadence: any equipped, ready skill.
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
    UpdateCampMonsters(dt);

    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *m = Entity_GetAgent(i);
        if (!m || m->archetype != ARCH_MINION) continue;
        if (AI_IsCampMonster(i)) continue;
        if (m->vState != AGENT_GROUNDED || Entity_IsCrowdControlled(i)) continue;

        int targetId = FindMarchTarget(m);
        if (targetId < 0) continue;
        const Agent *target = Entity_GetAgent(targetId);

        float dx = target->position.x - m->position.x;
        float dz = target->position.z - m->position.z;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist <= MINION_TRIGGER_RANGE) {
            // Self-destruct: team-aware AoE, event for the VFX layer, die.
            Vector3 at = m->position;
            int elem = m->currentElement;
            AgentTeam team = m->team;
            if (s_explosionCount < MAX_MINION_EXPLOSIONS) {
                s_explosions[s_explosionCount++] = (MinionExplosion){ at, elem };
            }
            Entity_ApplyDamage(i, 1e9f, (Vector3){ 0 }); // the minion goes first
            Entity_ApplyAoEDamage(at, MINION_BLAST_RADIUS, MINION_BLAST_DAMAGE,
                                  MINION_BLAST_KB, team);
            continue;
        }

        // Steady march (speedMult-aware like any external mover).
        float speed = MINION_SPEED_MPS * Entity_GetSpeedMult(i);
        Vector3 pos = m->position;
        pos.x += (dx / dist) * speed * dt;
        pos.z += (dz / dist) * speed * dt;
        pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
        Entity_SetPosition(i, pos);
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
        if (id < 0) break; // pool full
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
// PvE Explore Monster Camps (Mode 1: Giang Hồ Ngũ Hành)
// =============================================================================

typedef struct {
    bool        used;
    bool        noRespawn;
    int         agentId;
    Vector3     homePos;
    Vector3     lastKnownPos;
    int         element;
    MonsterKind kind;
    float       respawnTimer;
    float       attackTimer;
    float       patrolAngle;
} CampMonster;

static CampMonster s_campMonsters[AI_MAX_CAMP_MONSTERS];
static MonsterKillEvent s_killEvents[32];
static int s_killEventCount = 0;

static const float CAMP_AGGRO_RANGE    = 12.0f;
static const float CAMP_LEASH_RANGE    = 22.0f;
static const float CAMP_RESPAWN_SECS   = 15.0f;

static float CampMonsterHP(MonsterKind kind) {
    switch (kind) {
        case MONSTER_RANGED_CASTER: return 45.0f;
        case MONSTER_ELITE_GUARD:   return 180.0f;
        default:                    return 55.0f;
    }
}

static const char *CampMonsterSkillName(int element) {
    switch (element) {
        case 0:  return "GLACIAL_CANNON";
        case 1:  return "LEAF_WHIRLWIND";
        case 2:  return "FIRE";
        case 3:  return "EARTH_DISC_VOLLEY";
        case 4:  return "METAL_BLADE_STORM";
        default: return "FIRE";
    }
}

static void SpawnCampAgent(CampMonster *cm) {
    Vector3 p = cm->homePos;
    p.y = MapManager_GetGroundHeightAt(p.x, p.z);
    int id = Entity_SpawnAgent(p, CampMonsterHP(cm->kind), cm->element, TEAM_ENEMY, ARCH_MINION);
    cm->agentId = id;
    cm->lastKnownPos = p;
    cm->respawnTimer = 0.0f;
    cm->attackTimer = 0.5f + 0.2f * (float)(rand() % 5);
}

int AI_SpawnMonsterCamp(Vector3 center, int count, int element, MonsterKind kind) {
    int spawned = 0;
    for (int k = 0; k < count; k++) {
        int slot = -1;
        for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
            if (!s_campMonsters[i].used) { slot = i; break; }
        }
        if (slot < 0) break;

        float ang = (count > 1) ? ((float)k / (float)count) * 6.2831853f : 0.0f;
        float rad = (count > 1) ? 2.2f : 0.0f;
        CampMonster *cm = &s_campMonsters[slot];
        *cm = (CampMonster){ 0 };
        cm->used = true;
        cm->homePos = (Vector3){ center.x + cosf(ang) * rad, center.y, center.z + sinf(ang) * rad };
        cm->element = (element >= 0 && element <= 4) ? element : 0;
        cm->kind = kind;
        cm->patrolAngle = ang;
        SpawnCampAgent(cm);
        if (cm->agentId >= 0) spawned++;
        else cm->used = false;
    }
    return spawned;
}

int AI_SpawnOneShotMonster(Vector3 pos, int element, MonsterKind kind) {
    int slot = -1;
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        if (!s_campMonsters[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;
    CampMonster *cm = &s_campMonsters[slot];
    *cm = (CampMonster){ 0 };
    cm->used = true;
    cm->noRespawn = true;
    cm->homePos = pos;
    cm->element = (element >= 0 && element <= 4) ? element : 0;
    cm->kind = kind;
    cm->patrolAngle = 0.0f;
    SpawnCampAgent(cm);
    if (cm->agentId < 0) {
        cm->used = false;
        return -1;
    }
    return cm->agentId;
}

void AI_ClearMonsterCamps(void) {
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        if (s_campMonsters[i].used && s_campMonsters[i].agentId >= 0 &&
            Entity_GetAgent(s_campMonsters[i].agentId) != NULL) {
            Entity_ApplyDamage(s_campMonsters[i].agentId, 1e9f, (Vector3){ 0 });
        }
        s_campMonsters[i] = (CampMonster){ 0 };
        s_campMonsters[i].agentId = -1;
    }
    s_killEventCount = 0;
}

bool AI_IsCampMonster(int agentId) {
    if (agentId < 0) return false;
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        if (s_campMonsters[i].used && s_campMonsters[i].agentId == agentId) return true;
    }
    return false;
}

int AI_PollMonsterKills(MonsterKillEvent *out, int max) {
    if (out == NULL || max <= 0) { s_killEventCount = 0; return 0; }
    int n = (s_killEventCount < max) ? s_killEventCount : max;
    for (int i = 0; i < n; i++) out[i] = s_killEvents[i];
    s_killEventCount = 0;
    return n;
}

static void UpdateCampMonsters(float dt) {
    for (int i = 0; i < AI_MAX_CAMP_MONSTERS; i++) {
        CampMonster *cm = &s_campMonsters[i];
        if (!cm->used) continue;

        const Agent *me = (cm->agentId >= 0) ? Entity_GetAgent(cm->agentId) : NULL;
        if (me == NULL) {
            if (cm->agentId >= 0) {
                if (s_killEventCount < 32) {
                    s_killEvents[s_killEventCount++] = (MonsterKillEvent){
                        cm->lastKnownPos, cm->element, cm->kind
                    };
                }
                cm->agentId = -1;
                if (cm->noRespawn) {
                    cm->used = false;
                } else {
                    cm->respawnTimer = CAMP_RESPAWN_SECS;
                }
            } else {
                cm->respawnTimer -= dt;
                if (cm->respawnTimer <= 0.0f) {
                    SpawnCampAgent(cm);
                }
            }
            continue;
        }

        cm->lastKnownPos = me->position;
        if (cm->attackTimer > 0.0f) cm->attackTimer -= dt;
        if (me->vState != AGENT_GROUNDED || Entity_IsCrowdControlled(cm->agentId)) continue;

        // Find nearest TEAM_ALLY hero within aggro range
        int targetId = -1;
        float bestDistSq = CAMP_AGGRO_RANGE * CAMP_AGGRO_RANGE;
        for (int a = 0; a < MAX_AGENTS; a++) {
            const Agent *cand = Entity_GetAgent(a);
            if (!cand || cand->team != TEAM_ALLY || cand->archetype != ARCH_HERO) continue;
            float dx = cand->position.x - me->position.x;
            float dz = cand->position.z - me->position.z;
            float dSq = dx * dx + dz * dz;
            if (dSq < bestDistSq) {
                bestDistSq = dSq;
                targetId = a;
            }
        }

        float homeDx = me->position.x - cm->homePos.x;
        float homeDz = me->position.z - cm->homePos.z;
        float homeDistSq = homeDx * homeDx + homeDz * homeDz;
        if (homeDistSq > CAMP_LEASH_RANGE * CAMP_LEASH_RANGE) targetId = -1;

        if (targetId >= 0) {
            const Agent *t = Entity_GetAgent(targetId);
            float dx = t->position.x - me->position.x;
            float dz = t->position.z - me->position.z;
            float dist = sqrtf(dx * dx + dz * dz);

            float stopDist = (cm->kind == MONSTER_RANGED_CASTER) ? 6.5f : 1.7f;
            float moveSpd  = (cm->kind == MONSTER_ELITE_GUARD) ? 2.6f : 2.9f;
            moveSpd *= Entity_GetSpeedMult(cm->agentId);

            if (dist > stopDist && dist > 0.001f) {
                Vector3 pos = me->position;
                pos.x += (dx / dist) * moveSpd * dt;
                pos.z += (dz / dist) * moveSpd * dt;
                pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
                Entity_SetPosition(cm->agentId, pos);
                me = Entity_GetAgent(cm->agentId);
            }

            if (cm->attackTimer <= 0.0f && !Entity_IsStunned(cm->agentId)) {
                if (cm->kind == MONSTER_RANGED_CASTER && dist <= 11.0f) {
                    int skIdx = Skill_GetIndexByName(CampMonsterSkillName(cm->element));
                    if (skIdx >= 0 && CastSkill(skIdx, cm->agentId, me->position, t->position,
                                                (SkillParams){ .level = 1, .quantity = 1, .sizeScale = 0.85f })) {
                        cm->attackTimer = 2.4f;
                    }
                } else if (dist <= 2.1f) {
                    float dmg = (cm->kind == MONSTER_ELITE_GUARD) ? 14.0f : 6.0f;
                    Vector3 kb = (dist > 0.01f)
                        ? (Vector3){ (dx / dist) * 2.2f, 0.0f, (dz / dist) * 2.2f }
                        : (Vector3){ 0 };
                    Entity_ApplyDamage(targetId, dmg, kb);
                    if (cm->kind == MONSTER_ELITE_GUARD) {
                        Entity_ApplyLaunch(targetId, 3.2f, kb);
                    }
                    cm->attackTimer = (cm->kind == MONSTER_ELITE_GUARD) ? 1.8f : 1.4f;
                }
            }
        } else {
            // Gentle patrol near home camp
            cm->patrolAngle += 0.6f * dt;
            Vector3 goal = {
                cm->homePos.x + cosf(cm->patrolAngle) * 1.8f,
                0.0f,
                cm->homePos.z + sinf(cm->patrolAngle) * 1.8f
            };
            float dx = goal.x - me->position.x;
            float dz = goal.z - me->position.z;
            float dist = sqrtf(dx * dx + dz * dz);
            if (dist > 0.15f) {
                Vector3 pos = me->position;
                float step = 1.2f * dt;
                if (step > dist) step = dist;
                pos.x += (dx / dist) * step;
                pos.z += (dz / dist) * step;
                pos.y = MapManager_GetGroundHeightAt(pos.x, pos.z);
                Entity_SetPosition(cm->agentId, pos);
            }
        }
    }
}
