// combat/combat.c
#include "combat/combat.h"
#include "core/map_manager.h" // Map_QueryZoneAt — Thổ-in-forest penalty
#include "raylib.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

// Agent body radius for projectile↔agent hits (meters, real-world scale).
static const float AGENT_HIT_RADIUS = 0.4f;

typedef struct {
    int           ownerAgentId;
    AgentTeam     team;
    CombatElement elem;
    Vector3       pos;
    float         radius;
    float         damage;
    float         knockback;
    int           skillInstanceId;
    bool          taiji; // owner in Thái Cực: immune to elemental counters
    bool          dead;  // destroyed earlier this same resolve pass
} CombatProjectile;

static CombatProjectile s_projs[MAX_COMBAT_PROJECTILES];
static int s_projCount = 0;

static ClashEvent s_events[MAX_CLASH_EVENTS];
static int s_eventCount = 0;

// Last resolved frame's surviving colliders — see Combat_QueryProjectiles.
static CombatProjectileInfo s_snapshot[MAX_COMBAT_PROJECTILES];
static int s_snapshotCount = 0;

// Tương khắc: beats[a] == b means element a destroys element b.
// Thủy khắc Hỏa, Hỏa khắc Kim, Kim khắc Mộc, Mộc khắc Thổ, Thổ khắc Thủy.
static const CombatElement BEATS[5] = {
    /* ELEM_WATER */ ELEM_FIRE,
    /* ELEM_WOOD  */ ELEM_EARTH,
    /* ELEM_FIRE  */ ELEM_METAL,
    /* ELEM_EARTH */ ELEM_WATER,
    /* ELEM_METAL */ ELEM_WOOD,
};

void Combat_Init(void) {
    s_projCount = 0;
    s_eventCount = 0;
}

static void PushEvent(int skillInstanceId, ClashOutcome outcome, Vector3 point,
                      CombatElement otherElem, int otherAgentId) {
    if (s_eventCount >= MAX_CLASH_EVENTS) return;
    s_events[s_eventCount++] = (ClashEvent){
        .skillInstanceId = skillInstanceId,
        .outcome = outcome,
        .clashPoint = point,
        .otherElem = otherElem,
        .otherAgentId = otherAgentId,
    };
}

bool Combat_SubmitProjectile(int ownerAgentId, CombatElement elem,
                             Vector3 pos, float radius, float damage,
                             float knockback, int skillInstanceId) {
    if (s_projCount >= MAX_COMBAT_PROJECTILES) return false;
    // A dead caster's projectile is rejected outright: it can't be
    // team-attributed (NEUTRAL would hit BOTH sides, and agent-slot reuse
    // could even flip it to the wrong team). The skill's VFX keeps flying;
    // it just stops being a combat collider.
    const Agent *owner = Entity_GetAgent(ownerAgentId);
    if (owner == NULL) return false;
    s_projs[s_projCount++] = (CombatProjectile){
        .ownerAgentId = ownerAgentId,
        .team = owner->team,
        .elem = elem,
        .pos = pos,
        .radius = radius,
        .damage = damage,
        .knockback = knockback,
        .skillInstanceId = skillInstanceId,
        .taiji = owner->taijiActive,
        .dead = false,
    };
    return true;
}

int Combat_DeflectProjectilesInRadius(Vector3 center, float radius, int deflectorAgentId) {
    const Agent *deflector = Entity_GetAgent(deflectorAgentId);
    AgentTeam ownTeam = deflector ? deflector->team : TEAM_NEUTRAL;

    int destroyed = 0;
    for (int i = 0; i < s_projCount; i++) {
        CombatProjectile *p = &s_projs[i];
        if (p->dead || p->team == ownTeam) continue;
        float dx = p->pos.x - center.x;
        float dz = p->pos.z - center.z;
        if (dx * dx + dz * dz > radius * radius) continue;
        p->dead = true;
        PushEvent(p->skillInstanceId, CLASH_B_WINS, p->pos, p->elem, -1);
        destroyed++;
    }
    return destroyed;
}

void Combat_Update(float dt) {
    (void)dt; // pure per-frame resolve — no time integration here

    // NOTE: events are NOT cleared here — Combat_DeflectProjectilesInRadius
    // pushes events during the skill-update phase (before this call) and
    // they must survive until skills poll. Combat_PollEvents drains.

    // --- Tầng 1: projectile ↔ projectile (Đấu Pháp) ---
    for (int i = 0; i < s_projCount; i++) {
        if (s_projs[i].dead) continue;
        for (int j = i + 1; j < s_projCount; j++) {
            if (s_projs[i].dead) break;    // i lost a clash — stop pairing it
            if (s_projs[j].dead) continue; // j already resolved elsewhere
            CombatProjectile *a = &s_projs[i];
            CombatProjectile *b = &s_projs[j];
            if (a->team == b->team) continue; // same team: pass through, no event
            if (!CheckCollisionSpheres(a->pos, a->radius, b->pos, b->radius)) continue;

            Vector3 mid = { (a->pos.x + b->pos.x) * 0.5f,
                            (a->pos.y + b->pos.y) * 0.5f,
                            (a->pos.z + b->pos.z) * 0.5f };

            // Thái Cực: immune to elemental counters — a taiji projectile
            // beats any non-taiji one regardless of the matrix; two taiji
            // projectiles annihilate each other (Lưỡng Nghi cân bằng).
            if (a->taiji != b->taiji) {
                CombatProjectile *win  = a->taiji ? a : b;
                CombatProjectile *lose = a->taiji ? b : a;
                lose->dead = true;
                PushEvent(win->skillInstanceId, CLASH_A_WINS, mid, lose->elem, -1);
                PushEvent(lose->skillInstanceId, CLASH_B_WINS, mid, win->elem, -1);
            } else if (a->taiji && b->taiji) {
                a->dead = true;
                b->dead = true;
                PushEvent(a->skillInstanceId, CLASH_MUTUAL_DESTROY, mid, b->elem, -1);
                PushEvent(b->skillInstanceId, CLASH_MUTUAL_DESTROY, mid, a->elem, -1);
            } else if (BEATS[a->elem] == b->elem) {
                // a khắc b — a keeps flying
                b->dead = true;
                PushEvent(a->skillInstanceId, CLASH_A_WINS, mid, b->elem, -1);
                PushEvent(b->skillInstanceId, CLASH_B_WINS, mid, a->elem, -1);
            } else if (BEATS[b->elem] == a->elem) {
                a->dead = true;
                PushEvent(a->skillInstanceId, CLASH_B_WINS, mid, b->elem, -1);
                PushEvent(b->skillInstanceId, CLASH_A_WINS, mid, a->elem, -1);
            } else {
                // same element or a non-khắc pair: both fizzle
                a->dead = true;
                b->dead = true;
                PushEvent(a->skillInstanceId, CLASH_MUTUAL_DESTROY, mid, b->elem, -1);
                PushEvent(b->skillInstanceId, CLASH_MUTUAL_DESTROY, mid, a->elem, -1);
            }
        }
    }

    // --- Tầng 2: projectile ↔ agent ---
    for (int i = 0; i < s_projCount; i++) {
        CombatProjectile *p = &s_projs[i];
        if (p->dead) continue;

        int ids[MAX_AGENTS];
        int count = Entity_GetNearbyTargets(p->pos, p->radius + AGENT_HIT_RADIUS, ids, MAX_AGENTS);
        for (int k = 0; k < count; k++) {
            int id = ids[k];
            if (id == p->ownerAgentId) continue;
            const Agent *victim = Entity_GetAgent(id);
            if (!victim || victim->team == p->team) continue;

            float dmg = p->damage;
            // Cân bằng Thổ (thiết kế §XI): đạn Thổ bay vào Rừng mất 50% damage.
            if (p->elem == ELEM_EARTH && Map_QueryZoneAt(p->pos) == NAT_FOREST) {
                dmg *= 0.5f;
            }

            Vector3 kb = { 0 };
            float dx = victim->position.x - p->pos.x;
            float dz = victim->position.z - p->pos.z;
            float len = sqrtf(dx * dx + dz * dz);
            if (len > 0.0001f) {
                kb.x = (dx / len) * p->knockback;
                kb.z = (dz / len) * p->knockback;
            }

            Vector3 hitPoint = victim->position;
            CombatElement victimElem = (CombatElement)victim->currentElement;
            Entity_ApplyDamage(id, dmg, kb);
            PushEvent(p->skillInstanceId, CLASH_HIT_AGENT, hitPoint, victimElem, id);
            p->dead = true;
            break; // one victim per projectile per frame
        }
    }

    // Snapshot the survivors for out-of-window readers (ui/ auto-aim) —
    // then clear: immediate mode, everything re-submits next frame.
    s_snapshotCount = 0;
    for (int i = 0; i < s_projCount; i++) {
        if (s_projs[i].dead) continue;
        s_snapshot[s_snapshotCount++] = (CombatProjectileInfo){
            .pos = s_projs[i].pos,
            .radius = s_projs[i].radius,
            .elem = s_projs[i].elem,
            .team = s_projs[i].team,
            .ownerAgentId = s_projs[i].ownerAgentId,
        };
    }
    s_projCount = 0;
}

int Combat_QueryProjectiles(CombatProjectileInfo *out, int max) {
    if (out == NULL || max <= 0) return 0;
    int n = (s_snapshotCount < max) ? s_snapshotCount : max;
    for (int i = 0; i < n; i++) out[i] = s_snapshot[i];
    return n;
}

void Combat_BeginFrame(void) {
    s_eventCount = 0;
}

int Combat_PeekEvents(const ClashEvent **outArr) {
    if (outArr == NULL) return 0;
    *outArr = s_events;
    return s_eventCount;
}

int Combat_PollEvents(ClashEvent *out, int max) {
    if (out == NULL || max <= 0) return 0;
    int n = (s_eventCount < max) ? s_eventCount : max;
    for (int i = 0; i < n; i++) out[i] = s_events[i];
    s_eventCount = 0;
    return n;
}
