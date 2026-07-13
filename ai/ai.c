// ai/ai.c — Module 8: minion steering + self-destruct. Stateless brain: all
// persistent state lives on the Agent itself; this module just walks the
// pool every frame (same iteration idiom as Entity_Update).
#include "ai/ai.h"
#include "core/skill_manager.h" // hero bots cast off equippedSkills
#include "combat/combat.h"      // projectile snapshot for dash-dodging
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

void AI_Init(void) {
    s_explosionCount = 0;
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

// Nearest enemy hero, weighted toward low HP (finish weakened targets).
static int BotFindTarget(const Agent *me) {
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *a = Entity_GetAgent(i);
        if (a == NULL || a == me || a->archetype != ARCH_HERO) continue;
        if (a->team == me->team) continue;
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

        int targetId = BotFindTarget(me);
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

    for (int i = 0; i < MAX_AGENTS; i++) {
        const Agent *m = Entity_GetAgent(i);
        if (!m || m->archetype != ARCH_MINION) continue;
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
