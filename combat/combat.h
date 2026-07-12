// combat/combat.h
// Combat module — Đấu Pháp: immediate-mode projectile registry + 5x5 Clash
// Matrix (MODULES_ROADMAP.md Module 3). Skills keep owning their projectile
// motion + VFX; every frame a live projectile is *submitted* here as a
// collider, Combat_Update resolves projectile↔projectile clashes and
// projectile↔agent hits, and skills poll ClashEvents to despawn/draw.
// COMBAT_API.md will document the full contract.
#ifndef COMBAT_H
#define COMBAT_H

#include "raylib.h"
#include "entities/entities.h" // AgentTeam + damage entry point
#include <stdbool.h>

// Same numeric convention as entities/entities.h Agent.currentElement
// (0=Water,1=Wood,2=Fire,3=Earth,4=Metal) — safe to cast between them.
typedef enum {
    ELEM_WATER = 0,
    ELEM_WOOD,
    ELEM_FIRE,
    ELEM_EARTH,
    ELEM_METAL
} CombatElement;

// Outcome from the PERSPECTIVE OF YOUR OWN projectile ("A" = the projectile
// whose skillInstanceId is in the event):
//   CLASH_A_WINS         — yours won the clash and keeps flying
//   CLASH_B_WINS         — the opposing projectile won; yours is destroyed
//   CLASH_MUTUAL_DESTROY — both destroyed (same element, or a non-khắc pair)
//   CLASH_PASS_THROUGH   — reserved (same-team projectiles simply never
//                          clash and generate no event)
//   CLASH_HIT_AGENT      — yours hit an enemy agent (damage already applied
//                          by combat); yours is destroyed
typedef enum {
    CLASH_MUTUAL_DESTROY = 0,
    CLASH_A_WINS,
    CLASH_B_WINS,
    CLASH_PASS_THROUGH,
    CLASH_HIT_AGENT
} ClashOutcome;

#define MAX_COMBAT_PROJECTILES 128
#define MAX_CLASH_EVENTS       128

typedef struct {
    int          skillInstanceId; // the id you submitted with — your projectile
    ClashOutcome outcome;
    Vector3      clashPoint;      // midpoint of the clash / agent position
    CombatElement otherElem;      // opposing element (clash) or victim's element (agent hit)
    int          otherAgentId;    // CLASH_HIT_AGENT: victim agent id; else -1
} ClashEvent;

void Combat_Init(void);

// Submit MỖI FRAME while the projectile is alive (immediate-mode: the
// registry is cleared at the end of every Combat_Update). Owner's team +
// taiji flag are read from the agent pool at submit time; a DEAD/invalid
// owner REJECTS the submission (returns false) — an ownerless projectile
// can't be team-attributed, and agent-slot reuse could flip its side.
// skillInstanceId is an opaque id the skill uses to recognize its own
// events — unique per live projectile (skillIndex*1000 + slot by
// convention). Also returns false when the 128-collider registry is full.
bool Combat_SubmitProjectile(int ownerAgentId, CombatElement elem,
                             Vector3 pos, float radius, float damage,
                             float knockback, int skillInstanceId);

// Resolve all submitted colliders:
//   1. projectile↔projectile (different teams only): 5x5 Clash Matrix —
//      Thủy khắc Hỏa, Hỏa khắc Kim, Kim khắc Mộc, Mộc khắc Thổ, Thổ khắc
//      Thủy. Same element or non-khắc pair → mutual destroy.
//   2. projectile↔agent (enemy team only): Entity_ApplyDamage with radial
//      knockback. Đạn Thổ inside NAT_FOREST does -50% damage (thiết kế §XI).
// Emits ClashEvents, then clears the frame's submissions.
void Combat_Update(float dt);

// Frame boundary: clears last frame's events. main.c (later game/) calls it
// once at the top of the frame, BEFORE skill updates — so events produced by
// Combat_Update (and by deflects during skill updates) stay readable for the
// whole following frame.
void Combat_BeginFrame(void);

// Multi-consumer read (skills): borrow the internal event array. Valid until
// the next Combat_BeginFrame. Each skill scans for its own skillInstanceIds
// — peeking is idempotent, several skills can read the same frame's events.
// CONVENTION: skills namespace instance ids as skillIndex*1000 + slot so ids
// never collide across skills.
int Combat_PeekEvents(const ClashEvent **outArr);

// Single-consumer drain — autotest/tools only. A draining consumer starves
// every peeker, so gameplay code must use Combat_PeekEvents instead.
int Combat_PollEvents(ClashEvent *out, int max);

// --- Read-only projectile snapshot (Module 9 auto-targeting) ---
// The registry itself is immediate-mode (cleared every Combat_Update), so
// consumers that run outside the skill-update window (ui/ auto-aim) read
// the LAST resolved frame's still-alive colliders instead.
typedef struct {
    Vector3       pos;
    float         radius;
    CombatElement elem;
    AgentTeam     team;
    int           ownerAgentId;
} CombatProjectileInfo;
int Combat_QueryProjectiles(CombatProjectileInfo *out, int max);

// Thái Cực PHONG (Module 6): destroy every projectile submitted this frame
// within radius of center that belongs to a team other than the deflecting
// agent's. Each destroyed projectile's owner skill receives a CLASH_B_WINS
// event so it despawns + draws its clash VFX. Call BETWEEN the skill
// updates (submissions) and Combat_Update — i.e. from the Phong skill's own
// Update. Returns the number of projectiles destroyed.
int Combat_DeflectProjectilesInRadius(Vector3 center, float radius, int deflectorAgentId);

#endif // COMBAT_H
