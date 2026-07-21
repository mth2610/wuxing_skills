# AI MODULE API SPECIFICATION

> Module: `ai/` (`ai.h` / `ai.c`) — ../../ROADMAP.md Module 8 (Minion
> Pool + AI). Owner agent: **AI Agent** (see `ai/CLAUDE.md`).

## 1. Scope & Design

Minions are ordinary `ARCH_MINION` agents in the shared entities pool
(`MAX_AGENTS 256` — no separate pool). `ai/` adds BEHAVIOR only, walking the
pool every frame like `Entity_Update` does:
- **March**: steady 2.0 m/s (× `Entity_GetSpeedMult`) toward the nearest
  opposing-team agent, preferring `ARCH_BOSS` (trudges doggedly toward the
  enemy boss).
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

## 3b. Hero bots (Phase A4 — virtual players)

```c
#define AI_MAX_HERO_BOTS 8
int  AI_SpawnHeroBot(Vector3 pos, AgentTeam team); // agent + brain; caller equips
void AI_ClearHeroBots(void);                       // match reset / room close
int  AI_GetHeroBotCount(AgentTeam team);
bool AI_IsHeroBot(int agentId);
typedef struct { int agentId; int skillIndex; Vector3 aim; } HeroBotCast;
int  AI_PollHeroCasts(HeroBotCast *out, int max);  // → net cast mirroring
```

A bot is an ordinary ARCH_HERO plus a brain slot: nearest/weakest-enemy
targeting, hold-skill-range movement (close in >9m, back off <5m, strafe in
band), dash perpendicular to a close enemy projectile
(`Combat_QueryProjectiles`), edge guard against the ring-out
(`Entity_GetArenaBounds`), casts off `equippedSkills` on a ~1.2s think
cadence. Runs HOST-side only — clients see bots through the same snapshot
path as humans. game/'s team-battle INTRO spawns them from the lobby
roster's BOT entries and equips the default loadout; ai/ itself never
touches the registry names. Extra reads this brought: `core/
skill_manager.h` (CanCast/CastSkill) + `combat/combat.h` (projectiles).

## 4. Explicitly NOT in this version

- Pathfinding/avoidance (straight-line steering only).
- Per-minion variety (HP/damage archetypes) — one 20 HP kamikaze type.
- Bot difficulty tiers / per-bot loadout variety (all default loadout).
- Bots respawning mid-round (elimination rules them out by design).

## Autotest

`minion_ai` in `main.c`: wave spawn (team/element inheritance), march +
detonation damaging only the opposing boss, explosion events, 40-minion wave
within pool capacity.
`hero_bot_handicap` in `main.c`: bot spawns/counts, survives 5 simulated
seconds inside the arena, spends mana (cast proof); handicap table steps +
cap; `Entity_ScaleMaxHealth`; free-cast bypasses the mana gate once.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Doc migration: moved from root `ai/docs/API.md` to `ai/docs/API.md` per `DOC_ARCHITECTURE.md` (no content changes needed — already pure interface) | root `ai/docs/API.md` (read directly) | Ground-truth |
