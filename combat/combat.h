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
// registry is cleared at the end of every Combat_Update). Owner's team is
// read from the agent pool (TEAM_NEUTRAL if ownerAgentId is invalid).
// skillInstanceId is an opaque id the skill uses to recognize its own
// events — unique per live projectile (e.g. pool slot index).
// Returns false if the per-frame registry is full (collider dropped).
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

// Drain the event queue (call once per frame after Combat_Update; events
// not polled before the next Combat_Update are overwritten). Returns the
// number of events written into out (<= max).
int Combat_PollEvents(ClashEvent *out, int max);

// Thái Cực PHONG (Module 6): destroy every projectile submitted this frame
// within radius of center that belongs to a team other than the deflecting
// agent's. Each destroyed projectile's owner skill receives a CLASH_B_WINS
// event so it despawns + draws its clash VFX. Call BETWEEN the skill
// updates (submissions) and Combat_Update — i.e. from the Phong skill's own
// Update. Returns the number of projectiles destroyed.
int Combat_DeflectProjectilesInRadius(Vector3 center, float radius, int deflectorAgentId);

#endif // COMBAT_H
