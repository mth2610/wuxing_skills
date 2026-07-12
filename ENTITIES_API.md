# ENTITIES MODULE API SPECIFICATION

> Gameplay/combat foundation: Agent Pool, vertical physics (jump/dash/
> ring-out), damage entry point, teams, mana + Thiền Định, Vô Hệ loadout,
> Thái Cực state (MODULES_ROADMAP.md Module 1 "Entities Combat v2" +
> Module 6 state, both landed). Clash Matrix lives in `combat/`
> (COMBAT_API.md); Boss logic in `boss/` (BOSS_API.md); Formation/Minion
> pools remain out of scope here.

---

## 1. Scope & Status

This module exists because Core/Skills/Maps/Environment are all pure VFX/render layers — none of them track character state (HP, element, position lifecycle). Entities is the first layer that does.

**Do not add Formation Pool or Minion Pool logic to this module without explicit instruction.** Minions are just `ARCH_MINION` agents in this pool; their brain goes in `ai/` (Module 8).

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

// Module 1: teams + archetypes. TEAM_NEUTRAL doubles as "no filtering".
typedef enum { TEAM_ALLY = 0, TEAM_ENEMY, TEAM_NEUTRAL } AgentTeam;
// One mixed pool for heroes/minions/bosses; archetype is a tag for ai/boss/game.
typedef enum { ARCH_HERO = 0, ARCH_MINION, ARCH_BOSS } AgentArchetype;

#define AGENT_SKILL_SLOTS 4 // Vô Hệ loadout size

typedef struct {
    Vector3 position;
    Vector3 velocity;
    float   health;
    float   maxHealth;
    int     currentElement;      // 0=Water,1=Wood,2=Fire,3=Earth,4=Metal — Vô Hệ: majority of equipped slots (§15)
    AgentVerticalState vState;
    float   dashCooldown;        // seconds remaining, independent from skill Mana
    bool    isStealthed;         // set by game/'s zone rule (Mộc in NAT_FOREST) via Entity_SetStealth; consumed by auto-target/boss AI later
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

    // Team / archetype — Module 1 (§15)
    AgentTeam      team;
    AgentArchetype archetype;

    // Thiền Định — §16
    bool    isMeditating;
    float   meditateTimer;       // seconds remaining; <=0 = not meditating

    // Vô Hệ loadout — §15. skillId < 0 = empty slot.
    int     equippedSkills[AGENT_SKILL_SLOTS];
    int     equippedElements[AGENT_SKILL_SLOTS];

    // Real dash burst — §5. Horizontal velocity integrated while dashTimer > 0.
    Vector3 dashVelocity;
    float   dashTimer;

    // Cảnh Giới Thái Cực — §17
    bool    taijiActive;
} Agent;

#define MAX_AGENTS 256
static Agent agentPool[MAX_AGENTS]; // sized for 6 real players + minions/mid-tier monsters/2 bosses
```

* **`currentElement`** is recomputed whenever the loadout changes (`Entity_SetEquippedSkill` → `Entity_RecomputeElement`, §15). `Entity_SetElement` overrides it directly (boss biến hệ).
* **`modifiers[MAX_AGENT_MODIFIERS]`** — see §8. `speedMult` is consumed by movement code through `Entity_GetSpeedMult` (control/ multiplies its move speed by it).

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
void Entity_Dash(int agentId, Vector3 direction, float speed); // REAL: 0.15s horizontal burst, 1.0s cooldown, fires Entity_OnDash
bool Entity_CheckRingOut(int agentId); // called every Entity_Update tick
```

* **Arena constants** (real-world-scaled, 1 unit = 1 meter — must match `MAP_API.md` §3 exactly — do not redefine elsewhere):
  - Center: `(6.0f, 0.0f, 4.4f)`
  - Radius: `18.0f`
* `Entity_CheckRingOut`: if `agentPool[id].position` is outside `arenaRadius` (XZ distance from `arenaCenter`), transition `vState → AGENT_RING_OUT_FALLING`. Once in this state, gravity pulls `velocity.y` down every frame (`GRAVITY = 5.0f`, below real 9.81 m/s² by design for a floatier fall) until the agent is deactivated (falls below a kill-Y threshold).
* `Entity_Dash` is **real** (Module 4 companion work): rejects while `dashCooldown > 0`, normalizes the XZ direction, then `Entity_Update` integrates `dashVelocity` for exactly `DASH_DURATION = 0.15s` (final step clamped so total distance is exactly `speed × 0.15`), arms `DASH_COOLDOWN = 1.0s`, cancels Thiền Định, and fires the `Entity_OnDash` hook (§6).
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

// Team-filtered variant (Module 1): only agents whose team == filter.
int Entity_GetNearbyTargetsTeam(Vector3 center, float radius, AgentTeam filter,
                                int *outIds, int maxIds);
```

* Pure read query — no side effects, no damage application (use `Entity_ApplyDamage` separately).
* XZ-plane distance check (ignore Y), matching the same distance-check pattern used by `Entity_CheckRingOut` (§5).
* Iterates `agentPool[MAX_AGENTS]`, skips inactive agents (`active == false`).
* Use `Entity_GetNearbyTargetsTeam` when a side matters (buffs, boss targeting, Phong suction); the unfiltered variant remains for neutral queries.
* Backing use cases: multi-target skills, trap trigger zones, persistent AoE/control zones, and (later) Map Virtual Trigger Zones — all share this one query instead of reimplementing per-module.

---

## 8. Buff Modifier Slot

```c
void Entity_AddModifier(int agentId, float speedMult, float duration);
float Entity_GetSpeedMult(int agentId); // product of active slots; 1.0 default
```

* **Refresh-not-stack (Module 10 companion fix):** if an ACTIVE slot already holds the exact same `speedMult`, its `duration` is refreshed (extended, never shortened) instead of occupying a new slot — a repeating source (aura, formation tick) used to fill the array with copies that MULTIPLY in `Entity_GetSpeedMult` (0.4 re-applied twice became 0.16). Distinct multipliers still stack multiplicatively by design.
* Otherwise finds the first empty/expired slot (`duration <= 0`) and writes `speedMult`/`duration` into it. If no empty slot exists, the call is silently dropped (no eviction policy in this minimal version).
* `Entity_Update(dt)` ticks down `duration` on all active slots every frame; when a slot's `duration` reaches `<= 0` it is cleared (`duration = 0`, `speedMult = 0`).
* **Wired (Module 1/4):** `Entity_GetSpeedMult(agentId)` returns the product of all active slots' `speedMult` (1.0 when none). `control/` multiplies its 3.5 m/s walk speed by it every frame; any other external mover should do the same.
* Intended consumer: Buff-type skills (Core Agent's future "Entity-Attached" skeleton, `CORE_API.md` §4 — not yet documented) write into this slot; this module only stores and ticks it down.

---

## 9. AoE Composition — Damage & Buff over a Radius

```c
void Entity_ApplyAoEDamage(Vector3 center, float radius, float damage,
                           float knockbackStrength, AgentTeam attackerTeam);
void Entity_ApplyAoEBuff(Vector3 center, float radius, float speedMult,
                         float duration, AgentTeam allyTeam);
```

These compose existing primitives rather than reimplementing radius logic — both call `Entity_GetNearbyTargets` (§7) internally, then loop:

* **`Entity_ApplyAoEDamage`**: for each found agent, computes a knockback direction as the normalized XZ-plane vector from `center` to `agentPool[id].position` (Y ignored, same plane convention as §7's distance check), scales it by `knockbackStrength`, and calls `Entity_ApplyDamage` (§4) per agent.
* **`Entity_ApplyAoEBuff`**: for each found agent, calls `Entity_AddModifier` (§8) with `speedMult`/`duration`.
* Small radius ≈ single-target (projectile impact, melee swing). Large radius = true AoE (Tầm trung skills, buff aura). Same two calls cover both — only the radius and call frequency (once at impact vs. ticking every frame) differ.

> **TEAM FILTERING (Module 1 — the old §9 limitation is fixed).**
> `Entity_ApplyAoEDamage` skips every agent whose `team == attackerTeam`
> (never friendly-fires); pass `TEAM_NEUTRAL` to hit both real teams.
> `Entity_ApplyAoEBuff` only buffs agents whose `team == allyTeam`.
> Breaking change from v1: both functions gained a trailing team parameter —
> all call sites (sandbox, skill template) were migrated.

> **Supersedes `core/skill_manager.h`'s `ApplyAoEDamage()` for agent-targeted damage.** That function still exists (raw AoE math, no HP bookkeeping, may still be used for non-agent targets like destructible scenery later), but skills dealing damage to `agentPool` should call `Entity_ApplyAoEDamage` instead — it owns the actual HP mutation via `Entity_ApplyDamage` (§4), which `core/skill_manager.h` does not. (Core Agent: `CORE_API.md` should be updated to point skill authors here — not done in this pass, out of scope for Entities Agent.)

---

## 10. Spawn & Read-Only Access

```c
int Entity_SpawnAgent(Vector3 position, float maxHealth, int element,
                      AgentTeam team, AgentArchetype archetype);
const Agent *Entity_GetAgent(int agentId);
```

* **`Entity_SpawnAgent`**: scans `agentPool` for the first `!active` slot, initializes it (`position`, `health = maxHealth`, `maxHealth`, `currentElement = element`, `team`, `archetype`, `vState = AGENT_GROUNDED`, `active = true`, empty loadout — all `equippedSkills[] = -1` — zeroed `velocity`/`dashCooldown`/`isStealthed`/`modifiers[]`/meditate/dash/taiji state), returns its index `0..MAX_AGENTS-1`. Returns `-1` if the pool is full — caller must check. Breaking change from v1: two trailing parameters added.
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
  and picks the nearest active agent **on a different team** (Module 1 team
  filtering landed — your own side is never punched). Returns `false`
  immediately, no side effects, if nothing hostile is within range. The found target's position is reported via `outTargetPos` (caller
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

## 15. Team, Archetype & Vô Hệ Loadout (Module 1)

```c
typedef enum { TEAM_ALLY = 0, TEAM_ENEMY, TEAM_NEUTRAL } AgentTeam;
typedef enum { ARCH_HERO = 0, ARCH_MINION, ARCH_BOSS } AgentArchetype;
#define AGENT_SKILL_SLOTS 4

void Entity_SetEquippedSkill(int agentId, int slot, int skillId, int element);
int  Entity_RecomputeElement(int agentId);
void Entity_SetElement(int agentId, int element); // boss biến hệ override
```

* **Teams**: consumed by AoE damage/buff (§9), the nearby-team query (§7),
  basic-attack auto-target (§13), `combat/` (projectile sides), `boss/`
  (target picking). `TEAM_NEUTRAL` is both a real side (training dummies…)
  and the "no filtering" value for AoE damage.
* **Archetype**: pure tag — one mixed pool, no per-archetype pools. `boss/`
  spawns `ARCH_BOSS`; `ai/` (Module 8) will spawn `ARCH_MINION`.
* **Vô Hệ**: entities deliberately does not know the skill registry — the
  caller passes the element (0..4) alongside the skill id when equipping.
  `Entity_RecomputeElement` (run automatically by `Entity_SetEquippedSkill`)
  sets `currentElement` to the majority element across non-empty slots (tie →
  lowest element index; empty loadout → unchanged). It ALSO evaluates the
  Thái Cực loadout trigger (§17).

## 16. Thiền Định — Meditate (Module 1)

```c
void Entity_StartMeditate(int agentId); // requires grounded, not stunned/pulled
bool Entity_IsMeditating(int agentId);
```

* 3-second channel (`MEDITATE_DURATION`), regen `34/sec` (refills the default
  100 pool from empty within the window) instead of the passive `5/sec`.
* Cancelled by: damage, jump, dash, launch, stun, pull, or an actual position
  change through `Entity_SetPosition` (external movers push position every
  frame even when idle — only a real move > 1mm breaks the channel).

## 17. Cảnh Giới Thái Cực (Module 6 state — skills/postfx elsewhere)

```c
void Entity_SetTaijiActive(int agentId, bool active);
bool Entity_IsTaijiActive(int agentId);
void Entity_SetStealth(int agentId, bool stealthed); // zone rule setter (game/)
```

* Entered automatically by a balanced loadout — exactly 2 Âm (Thủy/Mộc) +
  2 Dương (Hỏa/Kim) across a FULL 4-slot loadout, checked inside
  `Entity_RecomputeElement` — or forced by `boss/` below 30% HP.
* While active: `combat/` grants clash immunity (COMBAT_API.md §2);
  `control/` locks the 4 normal slots and maps slot 0/1 to
  `TAIJI_PHONG`/`TAIJI_LOI`; `main.c` fades `PostFX_SetMonochrome` in.
* **Exits automatically when mana reaches 0** (checked in `Entity_Update`
  before regen — the tick that drains the pool exits the state). Lôi's
  45-mana cost is the intended drain (thiết kế §XVII "Vô Sát").

## 18. Explicitly NOT in this version

- Formation Pool / Trận Pháp (Module 10)
- Minion brain (`ai/`, Module 8 — `ARCH_MINION` agents already fit the pool)
- Auto-targeting / stealth *consumption* (flag is set by game/, nothing reads it yet)
- AoE variants of stun/pull (single-target only, see §12)
- Networking

These will each get their own section added to this document when their prerequisites exist — do not pre-build them speculatively.
