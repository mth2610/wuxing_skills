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
} Agent;

#define MAX_AGENTS 8
static Agent agentPool[MAX_AGENTS]; // 4 ally + 4 enemy AI
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

* **Arena constants** (must match `MAP_API.md` §3 exactly — do not redefine elsewhere):
  - Center: `(600.0f, 0.0f, 440.0f)`
  - Radius: `1800.0f`
* `Entity_CheckRingOut`: if `agentPool[id].position` is outside `arenaRadius` (XZ distance from `arenaCenter`), transition `vState → AGENT_RING_OUT_FALLING`. Once in this state, gravity pulls `velocity.y` down every frame (`GRAVITY` strictly 300–700f per project scale rules) until the agent is deactivated (falls below a kill-Y threshold).
* `Entity_Dash` is a **stub in this version** — only sets `dashCooldown`, does not move the agent yet. Real dash movement + afterimage VFX hook come in a later iteration.

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

## 10. Explicitly NOT in this version

- Clash Matrix (Skill↔Skill projectile collision resolution)
- Formation Pool / Trận Pháp
- Minion Pool
- Boss state machine / Thái Cực transformation
- Map Virtual Trigger Zone consumption (blocked on Map module exposing the API)
- Real dash movement and afterimage rendering
- Auto-targeting / stealth visibility logic
- Networking

These will each get their own section added to this document when their prerequisites exist — do not pre-build them speculatively.
