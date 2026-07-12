# COMBAT MODULE API SPECIFICATION

> Module: `combat/` (`combat.h` / `combat.c`) — MODULES_ROADMAP.md Module 3.
> Đấu Pháp: immediate-mode projectile collider registry + 5×5 Clash Matrix.
> Owner agent: **Combat Agent** (see `combat/CLAUDE.md`).

## 1. Scope & Design

Skills keep owning projectile **motion + VFX**. Every frame a live projectile
is *submitted* here as a collider; `Combat_Update` resolves
projectile↔projectile clashes (Tầng 1) and projectile↔agent hits (Tầng 2),
then clears all submissions (immediate mode — resubmit next frame). Skills
poll `ClashEvent`s to despawn projectiles and draw clash VFX. Combat is pure
logic: no VFX headers, events out only.

- Pools: `MAX_COMBAT_PROJECTILES 128` colliders/frame, `MAX_CLASH_EVENTS 128`.
- Reads `entities/entities.h` (teams, damage) and `core/map_manager.h`
  (zone query for the Thổ-in-forest penalty).
- Tick order (owned by `main.c`, later `game/`): skill updates (submissions +
  any `Combat_DeflectProjectilesInRadius` calls) → `Boss_Update` →
  `Combat_Update` → next frame skills poll events.

## 2. Elements & Clash Matrix

```c
typedef enum { ELEM_WATER = 0, ELEM_WOOD, ELEM_FIRE, ELEM_EARTH, ELEM_METAL } CombatElement;
```
Same numeric convention as `Agent.currentElement` — safe to cast.

Tương khắc (BEATS table): Thủy>Hỏa, Hỏa>Kim, Kim>Mộc, Mộc>Thổ, Thổ>Thủy.
Resolution for two colliding projectiles of different teams:

| case | outcome |
|---|---|
| exactly one owner in Thái Cực | taiji projectile wins (immune to counters) |
| both owners in Thái Cực | mutual destroy |
| A khắc B | A wins, keeps flying; B destroyed |
| B khắc A | B wins |
| same element / non-khắc pair | mutual destroy |
| same team | pass through — **no event** |

## 3. API

```c
void Combat_Init(void);
void Combat_BeginFrame(void); // frame boundary: clears last frame's events.
                              // main.c calls it once, BEFORE skill updates.

// Submit MỖI FRAME while the projectile lives. Owner's team + taiji flag
// are read from the agent pool at submit time; a DEAD/invalid owner REJECTS
// the submission (an ownerless projectile can't be team-attributed —
// NEUTRAL would hit both sides, and agent-slot reuse could flip its side;
// the skill's VFX keeps flying, it just stops colliding).
// skillInstanceId: opaque id, unique per live projectile — CONVENTION:
// skillIndex*1000 + slot, so ids never collide across skills.
// Returns false when rejected or the 128-collider frame registry is full.
bool Combat_SubmitProjectile(int ownerAgentId, CombatElement elem,
                             Vector3 pos, float radius, float damage,
                             float knockback, int skillInstanceId);

void Combat_Update(float dt); // resolve + clear submissions (dt unused)

typedef enum {
    CLASH_MUTUAL_DESTROY = 0,
    CLASH_A_WINS,        // yours won and keeps flying
    CLASH_B_WINS,        // yours lost — destroy it
    CLASH_PASS_THROUGH,  // reserved (same-team never clashes, no event)
    CLASH_HIT_AGENT      // yours hit an enemy agent (damage already applied)
} ClashOutcome;

typedef struct {
    int           skillInstanceId; // YOUR projectile ("A" perspective)
    ClashOutcome  outcome;
    Vector3       clashPoint;      // clash midpoint / victim position
    CombatElement otherElem;       // opposing element / victim's element
    int           otherAgentId;    // CLASH_HIT_AGENT: victim id; else -1
} ClashEvent;

// Multi-consumer read for SKILLS: borrow the internal event array (valid
// until the next Combat_BeginFrame). Each skill scans for its own
// skillInstanceIds — peeking is idempotent, every skill sees the same
// frame's events. This is the gameplay path.
int Combat_PeekEvents(const ClashEvent **outArr);

// Single-consumer drain — autotest/tools only (a drain starves peekers).
int Combat_PollEvents(ClashEvent *out, int max);

// Read-only snapshot of the LAST resolved frame's surviving colliders —
// for consumers outside the skill-update window (ui/ auto-targeting; the
// live registry is immediate-mode and already cleared by then).
typedef struct {
    Vector3       pos;
    float         radius;
    CombatElement elem;
    AgentTeam     team;
    int           ownerAgentId;
} CombatProjectileInfo;
int Combat_QueryProjectiles(CombatProjectileInfo *out, int max);

// Thái Cực PHONG: destroy every projectile submitted this frame within
// radius whose team differs from the deflector's. Owners get CLASH_B_WINS
// events. Call BETWEEN skill submissions and Combat_Update. Returns count.
int Combat_DeflectProjectilesInRadius(Vector3 center, float radius, int deflectorAgentId);
```

## 4. Projectile → Agent rules (Tầng 2)

- Hit radius: projectile radius + 0.4 m agent body radius; XZ + Y sphere test.
- Owner and same-team agents are never hit; one victim per projectile per
  frame, then the projectile dies with a `CLASH_HIT_AGENT` event.
- Damage goes through `Entity_ApplyDamage` with radial XZ knockback.
- **Đạn Thổ trong Rừng**: `ELEM_EARTH` projectile positioned inside a
  `NAT_FOREST` zone deals 50% damage (thiết kế §XI; rule mirrored in
  `game/game_rules.c`).

## 5. Convention change for skills

Projectile skills must NOT call `Entity_ApplyDamage` themselves anymore —
submit to the registry and react to events (peek, filter your own
`skillIndex*1000 + slot` ids; destroy on `CLASH_B_WINS` /
`CLASH_MUTUAL_DESTROY` / `CLASH_HIT_AGENT`). AoE/melee skills keep calling
`Entity_ApplyAoEDamage` directly.

**Migrated so far (Step 0, 07/2026):** `FIRE` (dragon head collider),
`GLACIAL_CANNON` (wavefront collider; final burst via team-aware
`Entity_ApplyAoEDamage`; the channel fizzles when its caster dies), `TUBE`
(stream head collider). Remaining VFX-only skills adopt the pattern as they
gain gameplay.

**Per-frame order (owned by main.c):** `Combat_BeginFrame` → screen/entity
updates → `AI_Update` → `Boss_Update` → `Formation_Update` → `Combat_Update`
→ `UpdateSkillManager` (skills submit + peek). Submissions are therefore
resolved by the NEXT frame's `Combat_Update` — one frame of latency, by
design.

## 6. Explicitly NOT in this version

- No projectile motion/ownership (skills keep it) — this is a collider
  registry, not a projectile system.
- No spatial partitioning (128² worst-case pair checks are fine on CPU).
- No projectile↔wall/terrain collision.
- No networking hooks (host-side resolve comes with `net/`).

## Autotest

`combat_clash_matrix` in `main.c` (WUXING_AUTOTEST=1): Thủy–Hỏa head-on →
Thủy wins with events on both sides; same-team pass-through; agent hit with
damage + event; 128-cap enforcement. `taiji_state` covers immunity + deflect.
