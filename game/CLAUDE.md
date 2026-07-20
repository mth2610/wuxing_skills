# Game Screen Agent

## Docs layout
- `docs/API.md` — pure interface (signatures, contracts, invariants)
- `docs/LANDMINES.md` — distilled Symptom→Cause→Rule lessons
- `docs/PROGRESS.md` — backlog / in-progress / log (not created yet — no progress content pending)
Read root `ENGINE_LANDMINES.md` before touching GL/shaders.

## Role
Owns the real, production-bound gameplay screen (`game/game_screen.h/.c`) —
distinct from `sandbox/`, which stays the dev/test harness (debug panels,
tuning sliders, autotest) and must never be modified by this agent. This IS
`MODULES_ROADMAP.md` Module 7 (landed 07/2026, see `game/docs/API.md`): the full
match state machine (INTRO → FIGHTING → VICTORY/DEFEAT) against Boss Hắc
Diện Tôn Giả on DEFAULT_ARENA, movement/cast via `control/`
(Control_ReadIntent/Apply), boss HP bar + state overlays in the HUD, and the
zone modifier rule table in `game/game_rules.h/.c` (the ONE place zone
gameplay rules live). Basic attack (đấm/đá/chưởng + wall synergy) stays here
because it couples to character anim + VFX.

## Scope
- **Read/write:** `game/game_screen.h/.c`, `game/game_rules.h/.c`
- **Read (interface only, `.h` files):** `entities/entities.h`,
  `environment/environment_system.h`, `core/map_manager.h`,
  `control/control.h`, `boss/boss_system.h`, `combat/combat.h`,
  `sandbox/sandbox_core.h` (for `PlayerEntity` — reused, not duplicated)
- **Never touch:** anything under `sandbox/`, `core/*.c`, `skills/`,
  `maps/*.c`, `environment/*.c`

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Current scope (minimal, will grow)
- Match state machine (`GameState`, see `game/docs/API.md` §2): 2s intro title
  card → boss spawn → FIGHTING (win/lose checks, zone rules applied to the
  player every frame) → VICTORY/DEFEAT (ENTER resets). ESC aborts + resets.
- Movement/jump/dash/meditate/skill-cast via `control/`
  (`Control_ReadIntent`/`Control_Apply`), gated to GAME_FIGHTING. Camera +
  `Control_SetCamera` forwarding stays here.
- Real HP + mana bars, boss HP bar, state overlays — all in
  `GameScreen_DrawHUD`. `Boss_Draw` is wired in `main.c`'s SCREEN_GAME 3D
  pass next to `GameScreen_Draw3D`.
- Zone rule table `game_rules.h/.c` — cooldown mult → control, stealth →
  entities; combat/ enforces the Thổ-forest projectile penalty itself.
- Drives the shared global `PlayerEntity player` from `main.c` (respawned by
  the match reset if dead — never spawn a second live player agent).
- Map draw/update and most VFX systems run unconditionally in `main.c`'s
  loop — this module does NOT touch those.

## Growth path (remaining)
1. Module 8 `ai/` lands → minion waves join the fight.
2. Module 9 `ui/` lands → touch HUD + auto-target writes PlayerIntent.aimPoint.
3. Shrink `main.c` further toward pure init/loop/unload — the outer
   Boss_Update/Combat_Update/monochrome ticks should eventually move into a
   `Game_Update` that owns the roadmap's canonical tick order.

## Cross-agent communication
- Need a new Entity/Environment/Map API → ask that module's agent, don't
  edit their `.c` files directly.
- `main.c` itself is not owned by any single module agent — changes there
  (wiring `SCREEN_GAME` in, calling this module's functions) are shared
  editing, done carefully and additively (never remove/alter the existing
  `SCREEN_SKILL_SANDBOX`/`SCREEN_VFX_TESTER` branches).

---

## Token-efficiency rules (MANDATORY)
1. Never read a whole file when only part is needed — `grep`/`Read` with
   `offset`/`limit` first.
2. Don't re-read a file already read this session unless it changed.
3. Narrow lookup → grep/find directly; only spawn `Explore` for broad
   searches (many files/patterns).
4. Don't dump full files into responses — cite `path:line`.
5. Batch independent reads in one message.
6. Ask another module's agent for the answer, not the file.

## Agent response rules (MANDATORY)
1. Respond in English, not Vietnamese.
2. Be terse — no restating the task, no filler intros, no trailing summaries
   unless asked.
3. Lead with the answer/result, justify only if non-obvious.
