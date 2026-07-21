# GAME MODULE API SPECIFICATION

> Module: `game/` (`game_screen.h/.c`, `game_rules.h/.c`) —
> ../../ROADMAP.md Module 7 (Game Mode) grown out of the earlier minimal
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

// Phase A3 — match mode, set by main.c BEFORE GameScreen_Init per entry point:
typedef enum { GAME_MODE_BOSS = 0, GAME_MODE_TEAM_BATTLE } GameMode;
void GameScreen_SetMode(GameMode mode);
GameMode GameScreen_GetMode(void);
```

### 2b. GAME_MODE_TEAM_BATTLE (Phase A3 — PvP 1v1 → 4v4)

Entry: the net lobby's START button (BẮT ĐẦU, `Net_ConsumeMatchStart` in
main.c) sets TEAM_BATTLE; the menu's ENTER GAME sets BOSS;
`WUXING_NET_BOSS=1` keeps the old invasion-vs-boss run for a net room (dev
only).

- INTRO: no boss. The host lines every living ARCH_HERO up on its side's
  spawn cluster (`TEAM_SPAWN[2]` = x 42 / 58 on VERDANT_PATH, fanned ±z).
- FIGHTING (host): **elimination** — `GameRules_CountAliveHeroes(team)`
  (living ARCH_HERO per side: host, remote players, bots all count;
  minions/bosses don't). A side at 0 ends the match; VICTORY/DEFEAT wording
  follows the HOST's side (cached while alive — a dead host whose team
  wins still gets VICTORY). Zone rules still apply to the local player.
- CLIENT: outcome arrives via `NET_CTRL_STATE` in the HOST's perspective
  and is mapped through team membership (same side as host → keep, else
  swap; own side cached pre-death). Host state dropping back to
  INTRO/FIGHTING while the client shows VICTORY/DEFEAT = rematch signal.
- ENTER on VICTORY/DEFEAT: host → rematch in place
  (`Net_HostRespawnPeerHeroes()` + match reset — the room stays); client →
  waits for the host. BOSS mode keeps ENTER → back-to-menu.
- HUD: team scoreboard "THANH LONG n — m BACH HO" (top center, replaces
  the boss bar slot); INTRO title card reads "SONG DAU".
- **Rendering other heroes** (`GameScreen_Draw3D`): every non-local
  ARCH_HERO (remote players + bots) is drawn with the SAME character model
  as the local player — never the procedural stick figure, so an opponent
  can't be mistaken for a minion orb. Each gets a per-agent
  `CharacterAnimState` (static arrays keyed by agentId) whose walk/idle +
  facing are inferred from the position delta between frames (no intent
  stream client-side). Tint: cool (ally) vs warm (enemy) relative to the
  local player's team — friend/foe at a glance, No Tutorial. Multiple
  animated instances on one shared model is safe here because
  `CharacterModel_Draw` updates+renders atomically per call (OpenGL
  synchronizes buffer upload vs. draw) — the "one instance only" caveat in
  character/ applies to update-all-then-draw, not this interleaving.

Details:
- Spawns: player `(2, 0, 4.4)`, boss `(10, 0, 4.4)` — inside the entities
  ring-out circle (center `(6,0,4.4)`, r=18) which matches DEFAULT_ARENA.
- **Default loadout** (Step 0): GLACIAL_CANNON / FIRE / STONE_PRISON /
  LEAF_WHIRLWIND on keys 1-4 — one per element, deliberately NOT 2 Âm +
  2 Dương (the player discovers Thái Cực by re-equipping, No Tutorial).
- **Auto-targeting** (Module 9): during FIGHTING, `UI_GetAutoAimPoint`
  (incoming enemy projectile → boss) overrides `PlayerIntent.aimPoint`
  before `Control_Apply`; `UI_DrawOverlay` renders slot chips + reticle in
  the HUD pass.
- **Minion waves** (Module 8): the boss summons `3 + phase` minions on
  every phase change; minion rendering (element-colored spirit orbs) lives
  in `GameScreen_Draw3D`; explosion VFX composes in `main.c` from
  `AI_PollExplosions`.
- ESC aborts and resets the match; leftover boss agents are killed on reset
  so re-entry spawns fresh.
- Player agent is respawned by the reset if the previous match killed it.
- Movement/attack input is ignored outside `GAME_FIGHTING`.
- Basic attack (Z/C/right-click, punch/kick/palm strike [đấm/đá/chưởng] +
  wall synergy) stays here — it couples to character animation and VFX,
  which control/ must not touch.

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

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Doc migration: moved from root `game/docs/API.md` to `game/docs/API.md` per `DOC_ARCHITECTURE.md` (no content restructuring — see open note below) | root `game/docs/API.md` (read directly) | Ground-truth |

<!-- TODO(doc-accuracy, not in migration scope): §5 "Explicitly NOT in this
version" still lists "no minions/AI teammates (Module 8)", "no touch
HUD/auto-target (Module 9)", "no networking (Module 11)" — but §2 details
already reference AI_PollExplosions (minions), UI_GetAutoAimPoint
(auto-target), and §2b covers the net team-battle mode. This section looks
stale relative to the rest of the doc; flag for the Game Agent to verify
against game_screen.c and update or remove. -->
