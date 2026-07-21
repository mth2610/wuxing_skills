# BOSS MODULE API SPECIFICATION

> Module: `boss/` — ../../ROADMAP.md Module 5. Boss Đại Tinh Linh.
> Owner agent: **Boss Agent** (see `boss/CLAUDE.md`).

## 1. Scope & Design — engine/data split

- **ENGINE** (`boss_system.h/.c`, written once): spawns the boss as a normal
  `ARCH_BOSS` agent in the shared entities pool, runs the %HP phase machine,
  biến hệ via `Entity_SetElement`, picks the nearest opposing-team target
  (`Entity_GetNearbyTargetsTeam`, 25 m) and casts the phase skill through
  `core/skill_manager` on a cooldown cadence. **No VFX headers.**
- **DATA** (`boss/<ten_boss>_def.c`, one per boss — the AI-creates-bosses
  surface): a `BossDef` table + `drawVisual` callback. Only `_def.c` files
  may include VFX/draw headers.

Because the boss is a pool agent, damage, knockback, stun, ring-out, and the
Thái Cực flag all arrive through the ordinary `Entity_*` pipeline — nothing
special-cases boss HP.

## 2. BossDef

```c
#define BOSS_MAX_PHASES 4
typedef struct BossDef {
    const char   *name;
    float         maxHealth;
    float         phaseHpThresholds[BOSS_MAX_PHASES]; // [0]=1.0, descending; phase i active when hp/max <= [i]
    CombatElement phaseElements[BOSS_MAX_PHASES];     // biến hệ per phase (drives clash matrix + visual cue)
    const char   *phaseSkillNames[BOSS_MAX_PHASES];   // registered skill NAMES (Skill_GetIndexByName at spawn; unresolved → no cast)
    float         castIntervalSeconds;                // AI cast cadence (default 3.0 if <= 0)
    void        (*drawVisual)(const Agent *self, float phaseT); // phaseT = seconds in current phase
} BossDef;
```

## 3. Engine API

```c
int  Boss_Spawn(const BossDef *def, Vector3 pos, AgentTeam team); // agentId or -1; single live boss
void Boss_Update(float dt);  // phases, biến hệ, <30% HP → Entity_SetTaijiActive, AI casts
void Boss_Draw(void);        // def->drawVisual (inside BeginMode3D)
int  Boss_GetPhase(void);    // 0-based; -1 when no live boss
int  Boss_GetAgentId(void);
bool Boss_IsAlive(void);
```

Wired in `main.c`: `Boss_Update` before `Combat_Update` (boss casts submit
through skills into the combat registry); `Boss_Draw` in the `SCREEN_GAME`
3D pass.

## 4. Bosses

| Def | File | Phases |
|---|---|---|
| `BOSS_HAC_DIEN_TON_GIA` | `hac_dien_ton_gia_def.c` | 400 HP; 100/75/50/25%; Thủy→Hỏa→Thổ→Mộc; GLACIAL_CANNON / FIRE / STONE_PRISON / LEAF_WHIRLWIND; rune ring + ground sigil colored by current element (No-Tutorial cue) |

Adding a boss: new `_def.c` + `extern` in `boss_system.h` + CMake line.
Engine untouched.

## 5. Explicitly NOT in this version

- Multiple simultaneous bosses (single static slot).
- Movement/steering AI (boss is stationary; minion waves are Module 8 `ai/`).
- Per-phase cast patterns beyond one skill per phase.

## Autotest

`boss_hac_dien` in `main.c`: spawn (archetype/element), phase transitions at
62.5%/37.5% HP with biến hệ, AI cast engaging the phase skill's cooldown,
death via the normal damage path ending the fight. `game_mode_loop` covers
the full match integration.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Doc migration: moved from root `boss/docs/API.md` to `boss/docs/API.md` per `DOC_ARCHITECTURE.md`; moved the "Phase 0 ships 1 boss; 1.0 target is 3" roadmap note to `PROGRESS.md` | root `boss/docs/API.md` (read directly) | Ground-truth |
