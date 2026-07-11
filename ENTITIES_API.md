# ENTITIES MODULE API SPECIFICATION

> Minimal gameplay/combat foundation. This is the first version — only Agent Pool, vertical physics (jump/dash/ring-out), and a damage entry point. Clash Matrix, Formation Pool, Minion Pool, and Boss AI are explicitly out of scope until this foundation is in place and stable.

---

## 1. Scope & Status

This module exists because Core/Skills/Maps/Environment are all pure VFX/render layers — none of them track character state (HP, element, position lifecycle). Entities is the first layer that does.

**Do not add Clash Matrix, Formation Pool, Minion Pool, or Boss logic to this module without explicit instruction.** Those depend on contracts (Map Virtual Trigger Zones, standardized skill damage calls) that don't exist yet.

---

## 2. Agent Struct

```c
typedef enum {
    AGENT_GROUNDED,
    AGENT_JUMPING,
    AGENT_RING_OUT_FALLING
} AgentVerticalState;

#define MAX_AGENT_MODIFIERS 4

// Minimal duration-based modifier slot (e.g. Buff speed multiplier).
typedef struct {
    float speedMult;   // 1.0 = normal speed; multiplicative; <=0 or unset slot ignored
    float duration;     // seconds remaining; <=0 means inactive/empty slot
} AgentModifier;

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float   health;
    float   maxHealth;
    int     currentElement;      // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal — "Vô Hệ", derived from equipped skills, not fixed
    AgentVerticalState vState;
    float   dashCooldown;        // seconds remaining, independent from skill Mana
    bool    isStealthed;         // true when motionless — reserved for future Auto-Targeting/Boss AI, not yet consumed anywhere
    bool    active;
    AgentModifier modifiers[MAX_AGENT_MODIFIERS]; // generic buff/debuff slots, see §8

    // Crowd control — see §12
    float   stunTimer;   // seconds remaining; <=0 = not stunned
    Vector3 pullTarget;
    float   pullTimer;   // seconds remaining; <=0 = not being pulled
    float   pullSpeed;

    // Mana — see §13
    float   mana;
    float   maxMana;
} Agent;

#define MAX_AGENTS 256
static Agent agentPool[MAX_AGENTS]; // sized for 6 real players + minions/mid-tier monsters/2 bosses
```

* **`currentElement`** is not chosen at creation — it's recomputed whenever the agent's 4 equipped skills change (Vô Hệ mechanic, see design doc §II). Recompute logic is NOT part of this minimal version; field exists, update logic comes later.
* **`isStealthed`** and **`dashCooldown`** are reserved fields — no system reads/writes them meaningfully yet in this version. They exist so the struct doesn't need a breaking change when khinh công/auto-targeting are implemented.
* **`modifiers[MAX_AGENT_MODIFIERS]`** — see §8. Data structure + tick-down only; `speedMult` is NOT wired into any movement code yet (no movement system exists to wire it into).

---

## 3. Lifecycle API

```c
void Entity_Init(void);
void Entity_Update(float dt);
void Entity_Draw(void);  // stub — entities are visually represented by skills/billboards owned elsewhere; this may stay empty
void Entity_Unload(void);
```

---

## 4. Damage Entry Point

```c
void Entity_ApplyDamage(int agentId, float damage, Vector3 knockback);
```

* Mutates `agentPool[agentId].health` and applies `knockback` to `velocity`.
* This is the ONLY way HP changes. Skills must call this — not `core/skill_manager.h`'s `ApplyAoEDamage` directly for agent-targeted damage (that function still exists for raw AoE math, but agent HP bookkeeping happens here).
* `agentId < 0` or `health <= 0` after damage → set `active = false`, do not delete from pool (static array, fixed slots).

---

## 5. Vertical Physics — Jump / Dash / Ring-Out (ONE state machine)

Ring-out (death fall outside arena) and khinh công jump/dash are **not separate systems** — both are transitions of `AgentVerticalState` on the same `velocity.y` integration.

```c
void Entity_Jump(int agentId, float force);
void Entity_Dash(int agentId, Vector3 direction, float speed); // sets dashCooldown, does NOT implement movement yet — stub
bool Entity_CheckRingOut(int agentId); // called every Entity_Update tick
```

* **Arena constants** (real-world-scaled, 1 unit = 1 meter — must match `MAP_API.md` §3 exactly — do not redefine elsewhere):
  - Center: `(6.0f, 0.0f, 4.4f)`
  - Radius: `18.0f`
* `Entity_CheckRingOut`: if `agentPool[id].position` is outside `arenaRadius` (XZ distance from `arenaCenter`), transition `vState → AGENT_RING_OUT_FALLING`. Once in this state, gravity pulls `velocity.y` down every frame (`GRAVITY = 5.0f`, below real 9.81 m/s² by design for a floatier fall) until the agent is deactivated (falls below a kill-Y threshold).
* `Entity_Dash` is a **stub in this version** — only sets `dashCooldown`, does not move the agent yet. Real dash movement + afterimage VFX hook come in a later iteration.
* `AGENT_JUMPING` now fully integrates: `Entity_Update` applies `GRAVITY` to `velocity.y` and integrates **all 3 axes** of `position` from `velocity` every frame while in this state (previously a stub — `Entity_Jump` set `velocity.y`/`vState` but nothing moved the agent afterward). Lands back to `AGENT_GROUNDED` when `position.y <= 0`, zeroing `velocity`. Entered by both `Entity_Jump` (voluntary) and `Entity_ApplyLaunch` (§12, involuntary) — same state, same integration, no separate "airborne" enum needed.

---

## 6. Dash/Afterimage Hook (stub, no VFX inside this module)

```c
void Entity_OnDash(int agentId); // currently empty body — reserved hook
```

* Entities module must NOT `#include` `core/particle_system.h` or `core/trail_system.h`. This hook exists so a future VFX layer (owned by Core or Skills) can poll/subscribe to dash events without Entities reaching into rendering code.
* Calling convention and actual VFX wiring TBD — not implemented yet.

---

## 7. Nearby Target Query

```c
// Returns the number of active agents found within radius of center, writing
// their indices into outIds (caller-supplied buffer, size maxIds).
int Entity_GetNearbyTargets(Vector3 center, float radius, int *outIds, int maxIds);
```

* Pure read query — no side effects, no damage application (use `Entity_ApplyDamage` separately).
* XZ-plane distance check (ignore Y), matching the same distance-check pattern used by `Entity_CheckRingOut` (§5).
* Iterates `agentPool[MAX_AGENTS]`, skips inactive agents (`active == false`).
* No team/faction filtering in this version — `Agent` has no team field yet. Known near-term follow-up, not implemented.
* Backing use cases: multi-target skills, trap trigger zones, persistent AoE/control zones, and (later) Map Virtual Trigger Zones — all share this one query instead of reimplementing per-module.

---

## 8. Buff Modifier Slot

```c
void Entity_AddModifier(int agentId, float speedMult, float duration);
```

* Finds the first empty/expired slot (`duration <= 0`) in `agentPool[agentId].modifiers[]` and writes `speedMult`/`duration` into it.
* Simple find-first-empty — no priority/stacking logic. If no empty slot exists, the call is silently dropped (no eviction policy in this minimal version).
* `Entity_Update(dt)` ticks down `duration` on all active slots every frame; when a slot's `duration` reaches `<= 0` it is cleared (`duration = 0`, `speedMult = 0`).
* **Limitation:** this is data-only. `speedMult` is NOT applied to any movement/velocity code — there is no movement system in this minimal version to wire it into. A future iteration (once khinh công/movement exists) will need to read these slots and apply the multiplier; that wiring is explicitly out of scope here.
* Intended consumer: Buff-type skills (Core Agent's future "Entity-Attached" skeleton, `CORE_API.md` §4 — not yet documented) write into this slot; this module only stores and ticks it down.

---

## 9. AoE Composition — Damage & Buff over a Radius

```c
void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage, float knockbackStrength);
void Entity_ApplyAoEBuff(Vector3 center, float radius, float speedMult, float duration);
```

These compose existing primitives rather than reimplementing radius logic — both call `Entity_GetNearbyTargets` (§7) internally, then loop:

* **`Entity_ApplyAoEDamage`**: for each found agent, computes a knockback direction as the normalized XZ-plane vector from `center` to `agentPool[id].position` (Y ignored, same plane convention as §7's distance check), scales it by `knockbackStrength`, and calls `Entity_ApplyDamage` (§4) per agent.
* **`Entity_ApplyAoEBuff`**: for each found agent, calls `Entity_AddModifier` (§8) with `speedMult`/`duration`.
* Small radius ≈ single-target (projectile impact, melee swing). Large radius = true AoE (Tầm trung skills, buff aura). Same two calls cover both — only the radius and call frequency (once at impact vs. ticking every frame) differ.

> **KNOWN LIMITATION — NO TEAM FILTERING.** `Agent` has no team/faction field yet (same gap as §7). `Entity_ApplyAoEBuff` buffs **every active agent inside the radius, ally or enemy** — there is currently no way to scope a buff aura to allies only. Skills Agent: a buff radius will also strengthen enemies standing inside it. This is acceptable for this minimal version but is a known near-term follow-up once `Agent` gains a team field — not implemented here.

> **Supersedes `core/skill_manager.h`'s `ApplyAoEDamage()` for agent-targeted damage.** That function still exists (raw AoE math, no HP bookkeeping, may still be used for non-agent targets like destructible scenery later), but skills dealing damage to `agentPool` should call `Entity_ApplyAoEDamage` instead — it owns the actual HP mutation via `Entity_ApplyDamage` (§4), which `core/skill_manager.h` does not. (Core Agent: `CORE_API.md` should be updated to point skill authors here — not done in this pass, out of scope for Entities Agent.)

---

## 10. Spawn & Read-Only Access

```c
int Entity_SpawnAgent(Vector3 position, float maxHealth, int element);
const Agent *Entity_GetAgent(int agentId);
```

* **`Entity_SpawnAgent`**: scans `agentPool` for the first `!active` slot, initializes it (`position`, `health = maxHealth`, `maxHealth`, `currentElement = element`, `vState = AGENT_GROUNDED`, `active = true`, zeroed `velocity`/`dashCooldown`/`isStealthed`/`modifiers[]`), returns its index `0..MAX_AGENTS-1`. Returns `-1` if the pool is full — caller must check.
* **`Entity_GetAgent`**: returns `&agentPool[agentId]` cast to `const Agent *` if `agentId` is in range `[0, MAX_AGENTS)` and the slot is `active`; otherwise returns `NULL`. **Caller must NULL-check** — do not assume a valid pointer.
* This is **the only sanctioned way for other modules to read agent state** (position, health, vState, etc.). There is no mutable `Agent *` getter, and none should be added — all mutation goes through the existing `Entity_*` setters (`Entity_ApplyDamage`, `Entity_AddModifier`, `Entity_Jump`, `Entity_Dash`, etc.).
* Unblocks: `sandbox/` migrating its duplicate `PlayerEntity`/`EnemyEntity` structs onto `agentPool` (separate Sandbox Agent task, not done here), future HUD/UI reading agent HP/position, and Map's planned Virtual Trigger Zone consumption.

### Position sync setter

```c
void Entity_SetPosition(int agentId, Vector3 position);
```

* Overwrites `agentPool[agentId].position` directly. Entities owns **no horizontal movement system** — callers that compute their own X/Z movement (e.g. sandbox WASD/AI logic) must push the result here each frame so `Entity_GetNearbyTargets` and other position-based queries stay accurate.
* Bounds-checks `agentId` and requires `active`, same pattern as `Entity_ApplyDamage`. No-op otherwise.
* Pure overwrite — no velocity computation, no movement logic added.

---

## 12. Crowd Control — Stun / Launch / Pull

```c
void Entity_ApplyLaunch(int agentId, float verticalForce, Vector3 horizontalVelocity);
void Entity_ApplyStun(int agentId, float duration);
bool Entity_IsStunned(int agentId);
void Entity_ApplyPull(int agentId, Vector3 targetPos, float speed, float duration);
bool Entity_IsCrowdControlled(int agentId);
```

* **Why self-contained, not velocity-impulse-and-let-someone-else-integrate:**
  nothing in this module integrates `position.x/z` from `velocity` for a
  standalone agent (horizontal movement is externally owned, §10's Position
  sync setter). A one-shot velocity impulse for launch/pull would silently do
  nothing for an agent with no external mover (e.g. a sandbox test dummy with
  no per-frame `Entity_SetPosition` caller). So both effects own their own
  position integration directly in `Entity_Update`, the same way
  `AGENT_RING_OUT_FALLING` already owns its y-fall (§5) — not a new
  architecture, just extended to more axes/effects.
* **`Entity_ApplyLaunch`**: sets `velocity` and `vState = AGENT_JUMPING` —
  identical entry point to `Entity_Jump` (§5). `Entity_Update`'s
  `AGENT_JUMPING` branch (now gravity-integrated, see §5) owns the resulting
  ballistic arc and the landing transition back to `AGENT_GROUNDED`.
* **`Entity_ApplyStun`**: sets/refreshes `stunTimer`, ticked down once per
  frame in `Entity_Update` (same shape as `dashCooldown`). Orthogonal to
  `vState` — an agent can be airborne (launched) **and** stunned at the same
  time (a common CC combo). Entities has no action/input system, so it
  cannot itself block a stunned agent from doing anything — **callers
  (skill-cast/input code) must check `Entity_IsStunned` before allowing an
  action**, the same kind of caller-enforced contract `SkillManager_CanCast`
  already relies on.
* **`Entity_ApplyPull`**: sets `pullTarget`/`pullSpeed`/`pullTimer`.
  `Entity_Update`, while `pullTimer > 0`, moves `position.x/z` directly
  toward `pullTarget` at `pullSpeed` units/sec (XZ-plane, same convention as
  `Entity_GetNearbyTargets`), clamped so it doesn't overshoot, and ticks
  `pullTimer` down. Single-target only in this version — no AoE-pull
  variant (no real use case for one yet; add one only when a skill needs it,
  mirroring how §9's AoE wrappers were added on top of single-target
  primitives once needed).
* **`Entity_IsCrowdControlled`**: convenience OR of
  `stunTimer > 0 || vState != AGENT_GROUNDED || pullTimer > 0`, for external
  movement/input systems deciding whether to skip writing `Entity_SetPosition`
  this frame and defer to Entities' own integration. **Not enforced** by
  Entities itself — same caller-contract caveat as stun above.
* **Known limitations, matching existing patterns elsewhere in this doc:**
  no team/faction filtering (same gap as §7/§9), no AoE variant for pull or
  stun (single-target only), and a stunned-but-grounded agent's horizontal
  movement is still whatever an external mover writes each frame — Entities
  can't block it, only report the state via `Entity_IsStunned`.

---

## 13. Mana + Basic Attack

```c
bool Entity_TrySpendMana(int agentId, float amount);

typedef enum { BASIC_ATTACK_PUNCH, BASIC_ATTACK_KICK, BASIC_ATTACK_PALM } BasicAttackType;
bool Entity_ExecuteBasicAttack(int attackerId, BasicAttackType type,
                               Vector3 *outTargetPos,
                               Vector3 *outWallPos, int *outWallElement);

// Swing duration per attack type (punch 1.0s < kick 1.2s < palm 1.4s) —
// single source of truth consumed by game/ and sandbox/ to pace the swing
// animation (CharacterModel_TriggerAttackTimed). Pure data, no side effects.
float Entity_GetBasicAttackSeconds(BasicAttackType type);
```

* **Mana**: `Agent.mana`/`maxMana`, both set to `100.0f` by `Entity_SpawnAgent`
  (no signature change — every spawned agent gets a default pool). Passive
  regen `5.0f/sec`, capped at `maxMana`, ticked in `Entity_Update` right next
  to the stun-timer tick-down. `Entity_TrySpendMana` is all-or-nothing: if
  `amount > mana` it returns `false` and leaves `mana` untouched (no partial
  spend). `core/skill_manager.c`'s `CastSkill()` is the actual spend site —
  every real skill cast costs mana (`Skill_GetManaCost`, default `20.0f` flat
  unless a skill opts into `RegisterSkillManaCost`); insufficient mana aborts
  the cast entirely (no VFX, no cooldown trigger).
* **Basic attack**: deliberately **separate from the `CastSkill`/skill
  pipeline** — free, no cast time, no cooldown, spammable (per design
  decision — a normal combat game needs a mana-free/instant melee option).
  **Auto-targets** — no `targetId` parameter; internally scans
  `Entity_GetNearbyTargets` around the attacker (`AUTO_TARGET_RADIUS = 10.0f`)
  and picks the nearest other active agent. No team filtering yet (`Agent`
  has no `team` field — tracked for `MODULES_ROADMAP.md` Module 1, not this
  pass). Returns `false` immediately, no side effects, if nothing is within
  range. The found target's position is reported via `outTargetPos` (caller
  has no `targetId` to look it up itself — needed for VFX, e.g. a wall-bonus
  beam's endpoint). Melee damage only within `BASIC_ATTACK_RANGE = 1.5f` of
  that target — `PUNCH`/`KICK` apply `Entity_ApplyDamage` with a small XZ
  knockback, `PALM` (chưởng) applies the highest damage plus a small
  `Entity_ApplyLaunch` pop — "phá giáp, đẩy lùi mạnh" per `SKILL_CAT_MELEE`'s
  description (`core/skill_manager.h`).
* **Wall synergy**: if the attacker is within `WALL_CHECK_RADIUS = 3.0f` of an
  active wall (`core/skill_manager.h`'s `SkillManager_FindNearbyWall` — a
  registry any skill with a genuine stationary phase can "ping" every frame
  it's up, same per-tick-refresh idiom as `Entity_ApplyStun`'s caller in
  `stone_prison_skill.c`), the basic attack also applies a small free bonus
  hit (no mana — this path never touches `Entity_TrySpendMana`) and reports
  the wall's position/element via `outWallPos`/`outWallElement` so the caller
  can spawn the matching elemental VFX. **Entities does not spawn VFX itself**
  (hard rule: pure gameplay logic, no rendering) — this out-param handoff
  mirrors `Entity_OnDash`'s existing "entities reports, VFX layer reacts"
  hook philosophy.
* **Only Earth is wired to a real wall as of this version** — `stone_prison_skill.c`'s
  `STATE_HOLDING` pillar registers itself (`element = 3`). Water/Fire/Wood/Metal
  have no skill with a genuine stationary phase yet (checked: Glacial Cannon
  is a pure projectile/wave skill, no standing structure) — the registry is
  generic and ready for those elements' future wall-type skills to register
  into with one call, no changes needed here.

---

## 14. Explicitly NOT in this version

- Clash Matrix (Skill↔Skill projectile collision resolution)
- Formation Pool / Trận Pháp
- Minion Pool
- Boss state machine / Thái Cực transformation
- Map Virtual Trigger Zone consumption (blocked on Map module exposing the API)
- Real dash movement and afterimage rendering
- Auto-targeting / stealth visibility logic
- AoE variants of stun/pull (single-target only, see §12)
- AoE wall bonus / team filtering for basic attacks (single-target only, see §13)
- Networking

These will each get their own section added to this document when their prerequisites exist — do not pre-build them speculatively.
