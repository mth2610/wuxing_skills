# AI MODULE API SPECIFICATION

> Module: `ai/` (`ai.h` / `ai.c`) — ../../ROADMAP.md Module 8 (Minion
> Pool + AI). Owner agent: **AI Agent** (see `ai/CLAUDE.md`).

## 1. Scope & Design

Minions are ordinary `ARCH_MINION` agents in the shared entities pool
(`MAX_AGENTS 256` — no separate pool). `ai/` adds BEHAVIOR only, walking the
pool every frame like `Entity_Update` does:
- **March & Lane Roles**: MOBA wave minions (`AI_SpawnMinionWave`) spawn as a 2-role mix (~65% **Vanguard Kamikaze** + ~35% **Ranged Spirit Caster**). Both apply soft Boids separation (`r < 1.4m`) and prioritize opposing `ARCH_MINION` (`< 6m`) > opposing `ARCH_HERO` (`< 8.5m`) > opposing `ARCH_BOSS`. Ranged Spirits hold at `7.2m` and fire lightweight `SpawnMinionBolt` projectiles.
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

## 3b. Hero bots (Phase A4 + Utility AI Simulated Players)

```c
#define AI_MAX_HERO_BOTS 8
#define AI_MAX_WORLD_POIS 8

typedef enum {
    AI_BOT_MODE_ARENA_DUEL = 0, // Team Battle / Sparring
    AI_BOT_MODE_MOBA_ESCORT,    // 1v1 / 2v2 Dual-Boss + Convergence Pillar
    AI_BOT_MODE_OPEN_WORLD      // Living Jianghu: Quests, Camps, Loot, World Boss, Social
} AIBotGameMode;

typedef enum {
    AI_PERSONA_BALANCED = 0, AI_PERSONA_VANGUARD, AI_PERSONA_GUARDIAN,
    AI_PERSONA_DUELIST, AI_PERSONA_WANDERER, AI_PERSONA_RIVAL
} AIBotPersona;

void AI_SetBotWorldContext(const AIBotWorldContext *ctx);
int  AI_SpawnHeroBot(Vector3 pos, AgentTeam team);
int  AI_SpawnHeroBotEx(Vector3 pos, AgentTeam team, AIBotPersona persona, const char *nameOverride);
void AI_ClearHeroBots(void);
int  AI_GetHeroBotCount(AgentTeam team);
bool AI_IsHeroBot(int agentId);
bool AI_GetHeroBotProfile(int agentId, AIBotProfile *outProfile);
void AI_NotifyBotSocialEvent(int senderAgentId, int targetBotAgentId, int emoteId);
int  AI_PollBotSocialEvents(AIBotSocialEvent *out, int max);

typedef struct { int agentId; int skillIndex; Vector3 aim; } HeroBotCast;
int  AI_PollHeroCasts(HeroBotCast *out, int max);  // skillIndex >= 250 = basic attack
```

Implemented in `ai/ai_bot_brain.inl` via a 3-layer extensible architecture:
1. **Perception & Skill Introspection (`BotPerceive`)**: scans nearby heroes/minions/bosses, incoming hostile projectiles (`Combat_QueryProjectiles`), and queries `VoHoc_FindByName` at runtime so bots auto-adapt to any future Võ Học set without hardcoded skill logic.
2. **Declarative Utility AI Goal Table (`ScoreGoal` / `SelectBestGoal`)**: scores 8 modular goals (`SURVIVE_RECOVER`, `MOBA_CONTEST_ORB`, `MOBA_ESCORT_BOSS`, `MOBA_ASSAULT_OBJECTIVE`, `COMBAT_DUEL_HERO`, `OW_WORLD_BOSS_RAID`, `OW_HUNT_MONSTERS`, `OW_TOWN_QUEST`) weighted by `AIBotPersona` with hysteresis.
3. **Humanized Motor (`SteerHumanizedMove` & Tactical Combat)**: 8-ray context steering, Khinh Công jump/dash, Thiền Định meditation when low Mana, Đấu Pháp counter-projectiles, Tương Sinh element chaining, predictive aim leading, and melee weaving (`Entity_ExecuteBasicAttack`).

Runs HOST-side only — clients see bots through the same snapshot + cast-mirror path as humans. `open_world/ow_world.c` maintains up to 3 simulated players (`2x WANDERER + 1x RIVAL`) when human peer count is low and rotates them out as real players join.

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
