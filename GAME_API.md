# GAME MODULE API SPECIFICATION

> Module: `game/` (`game_screen.h/.c`, `game_rules.h/.c`) —
> MODULES_ROADMAP.md Module 7 (Game Mode) grown out of the earlier minimal
> game screen. Owner agent: **Game Agent** (see `game/CLAUDE.md`).

## 1. Scope & Design

The production match loop for Phase 0: player (control/) vs Boss Hắc Diện
Tôn Giả (boss/) on DEFAULT_ARENA, with the zone modifier rule table
centralized here. `main.c` still owns the top-level screen switcher
(`SCREEN_MAIN_MENU/SKILL_SANDBOX/VFX_TESTER/GAME`) and the global tick of
map/VFX/entities/boss/combat — this module owns everything inside the
`SCREEN_GAME` branch.

## 2. Match state machine (`game_screen.h`)

```c
typedef enum {
    GAME_MENU = 0,     // represented by main.c's SCREEN_MAIN_MENU
    GAME_ARENA_INTRO,  // 2s boss title card; pins DEFAULT_ARENA; spawns boss at the end
    GAME_FIGHTING,     // control intents live; zone rules applied to the player; win/lose checks
    GAME_VICTORY,      // boss died — ENTER → menu (match resets)
    GAME_DEFEAT        // player died (HP or ring-out) — ENTER → menu (match resets)
} GameState;

GameState GameScreen_GetState(void);
void GameScreen_Init(PlayerEntity *player);   // once at startup; resets the match
void GameScreen_Update(PlayerEntity *player, Camera3D *camera, float dt);
void GameScreen_Draw3D(const PlayerEntity *player);
void GameScreen_DrawHUD(const PlayerEntity *player); // HP/mana bars, boss bar, state overlays
bool GameScreen_RequestedBackToMenu(void);
```

Details:
- Spawns: player `(2, 0, 4.4)`, boss `(10, 0, 4.4)` — inside the entities
  ring-out circle (center `(6,0,4.4)`, r=18) which matches DEFAULT_ARENA.
- ESC aborts and resets the match; leftover boss agents are killed on reset
  so re-entry spawns fresh.
- Player agent is respawned by the reset if the previous match killed it.
- Movement/attack input is ignored outside `GAME_FIGHTING`.
- Basic attack (Z/C/right-click, đấm/đá/chưởng + wall synergy) stays here —
  it couples to character animation and VFX, which control/ must not touch.

## 3. Zone modifier rule table (`game_rules.h`)

The ONE place gameplay zone rules live (maps are pure data, Module 2):

```c
float GameRules_CooldownMult(int element, NatureZoneType zone);  // Thủy+RIVER → 0.5
float GameRules_DamageMult(int element, NatureZoneType zone);    // Hỏa+RIVER → 0.5; Thổ+FOREST → 0.5
bool  GameRules_GrantsStealth(int element, NatureZoneType zone); // Mộc+FOREST
float GameRules_KnockbackMult(int element, NatureZoneType zone); // Thổ+DESERT → 1.5
float GameRules_RangeMult(int element, NatureZoneType zone);     // Thủy+DESERT → 0.5
```

Application points today: cooldown → `Control_SetCastCooldownMult` (set per
frame from the player's zone/element during FIGHTING); stealth →
`Entity_SetStealth`; the Thổ projectile penalty is enforced inside
`combat/` (needs the projectile position). Knockback/range multipliers are
tabled but not yet consumed — wire them as the relevant systems grow.

## 4. Thái Cực integration

`main.c` fades `PostFX_SetMonochrome` toward 1 while any live taiji agent
exists (player via 2 Âm + 2 Dương loadout, boss below 30% HP) — the whole
canvas goes black-and-white and fades back on exit.

## 5. Explicitly NOT in this version

- No minions/AI teammates (Module 8), no touch HUD/auto-target (Module 9),
  no formations (Module 10), no networking (Module 11).
- `main.c` has not yet shrunk to pure init/loop/unload — the full
  `Game_Init/Update/Draw/Unload` takeover of the SCREEN_GAME branch's outer
  ticks remains the target shape for the next pass.
- Equipped-skill loadout UI: `Agent.equippedSkills` starts empty; equipping
  is currently programmatic (`Entity_SetEquippedSkill`).

## Autotest

`game_mode_loop` in `main.c`: intro → boss spawn → FIGHTING; boss death →
VICTORY; player death → DEFEAT; rule-table spot checks.
