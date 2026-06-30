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
} Agent;

#define MAX_AGENTS 8
static Agent agentPool[MAX_AGENTS]; // 4 ally + 4 enemy AI
```

* **`currentElement`** is not chosen at creation — it's recomputed whenever the agent's 4 equipped skills change (Vô Hệ mechanic, see design doc §II). Recompute logic is NOT part of this minimal version; field exists, update logic comes later.
* **`isStealthed`** and **`dashCooldown`** are reserved fields — no system reads/writes them meaningfully yet in this version. They exist so the struct doesn't need a breaking change when khinh công/auto-targeting are implemented.

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

## 7. Explicitly NOT in this version

- Clash Matrix (Skill↔Skill projectile collision resolution)
- Formation Pool / Trận Pháp
- Minion Pool
- Boss state machine / Thái Cực transformation
- Map Virtual Trigger Zone consumption (blocked on Map module exposing the API)
- Real dash movement and afterimage rendering
- Auto-targeting / stealth visibility logic
- Networking

These will each get their own section added to this document when their prerequisites exist — do not pre-build them speculatively.
