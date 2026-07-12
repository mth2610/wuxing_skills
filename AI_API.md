# AI MODULE API SPECIFICATION

> Module: `ai/` (`ai.h` / `ai.c`) — MODULES_ROADMAP.md Module 8 (Minion
> Pool + AI). Owner agent: **AI Agent** (see `ai/CLAUDE.md`).

## 1. Scope & Design

Minions are ordinary `ARCH_MINION` agents in the shared entities pool
(`MAX_AGENTS 256` — no separate pool). `ai/` adds BEHAVIOR only, walking the
pool every frame like `Entity_Update` does:
- **March**: steady 2.0 m/s (× `Entity_GetSpeedMult`) toward the nearest
  opposing-team agent, preferring `ARCH_BOSS` ("lầm lũi về boss địch").
  Movement is an external mover: `Entity_SetPosition` per frame, yields
  while airborne/crowd-controlled.
- **Self-destruct**: within 1.2 m of the target — the minion dies first,
  then a team-aware `Entity_ApplyAoEDamage` (2 m radius, 15 dmg, 3 kb).
- Pure logic: no VFX includes. Explosions surface as poll events (same
  philosophy as `Entity_OnDash`); `main.c` drains them and composes an
  element-matched `VFX_ComposeImpact`.

## 2. API

```c
void AI_Init(void);
void AI_Update(float dt);          // ticked from main.c before Boss_Update
int  AI_SpawnMinionWave(int bossAgentId, int count); // ring around the boss,
                                   // inherits team+element; returns spawned count

typedef struct { Vector3 pos; int element; } MinionExplosion;
int AI_PollExplosions(MinionExplosion *out, int max); // drained per frame (main.c)
```

## 3. Integration

- `game/game_screen.c` summons a wave on every boss phase change
  (`3 + phase` minions) during `GAME_FIGHTING`.
- Minion rendering lives in `GameScreen_Draw3D` (element-colored spirit orb
  + rotating ring + smart shadow) — ai/ never draws.
- Tuning constants (speed/trigger/blast) are statics in `ai.c` — meter-scaled.

## 4. Explicitly NOT in this version

- Hero-AI brains (AI teammates/opponents) — the module's growth direction.
- Pathfinding/avoidance (straight-line steering only).
- Per-minion variety (HP/damage archetypes) — one 20 HP kamikaze type.

## Autotest

`minion_ai` in `main.c`: wave spawn (team/element inheritance), march +
detonation damaging only the opposing boss, explosion events, 40-minion wave
within pool capacity.
