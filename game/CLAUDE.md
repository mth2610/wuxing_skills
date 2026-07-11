# Game Screen Agent

## Role
Owns the real, production-bound gameplay screen (`game/game_screen.h/.c`) —
distinct from `sandbox/`, which stays the dev/test harness (debug panels,
tuning sliders, autotest) and must never be modified by this agent. This is
`MODULES_ROADMAP.md` Module 7's eventual home. Now has real basic-attack
combat (đấm/đá/chưởng, auto-target, wall synergy — a deliberate shortcut ahead
of Module 4 `control/` formalizing input, see game_screen.h's header) and a
real mana bar (Module 1's mana landed). Still no real skill-casting/enemy/boss
(Module 3 `combat/` + Module 5 `boss/` not built yet).

## Scope
- **Read/write:** `game/game_screen.h`, `game/game_screen.c`
- **Read (interface only, `.h` files):** `entities/entities.h`,
  `environment/environment_system.h`, `core/map_manager.h`,
  `sandbox/sandbox_core.h` (for `PlayerEntity` — reused, not duplicated)
- **Never touch:** anything under `sandbox/`, `core/*.c`, `skills/`,
  `maps/*.c`, `environment/*.c`

## Directories FULLY FORBIDDEN
- `build/`
- `_deps/`
- `android.wuxing_skills/`

## Current scope (minimal, will grow)
- WASD movement + orbit camera (Q/E rotate, wheel zoom), driving the shared
  global `PlayerEntity player` declared in `main.c` (same one `InitSandbox`
  already spawns into the Entity/Agent pool at startup — do not spawn a
  second player agent).
- Real HP + mana bar HUD, read via `Entity_GetAgent(player->agentId)`.
- Real basic attack (Z/C/right-click → `Entity_ExecuteBasicAttack`,
  auto-target, wall synergy) — no real skill-casting/enemy/boss yet (Module 3
  `combat/` + Module 5 `boss/` not built).
- Map draw/update (`MapManager_DrawActive/Update`) and most VFX systems
  already run unconditionally in `main.c`'s main loop regardless of active
  screen — this module does NOT need to touch those.

## Growth path (do in order, per MODULES_ROADMAP.md's "don't skip more than
one tier" rule)
1. Module 1 lands → add real mana bar to `GameScreen_DrawHUD`.
2. Module 4 (`control/`) lands → replace the WASD block in
   `GameScreen_Update` with `Control_ReadIntent`/`Control_Apply`; likely
   retire the `PlayerEntity` coupling in favor of whatever `control/` defines.
3. Module 3 (`combat/`) + Module 6 (Thái Cực) land → this screen starts
   hosting real skill casts and a real enemy/boss.
4. Module 7 proper: rename/expand into the full `Game_Init/Update/Draw/
   Unload` + `GameState` state machine described in `MODULES_ROADMAP.md`
   §Module 7 — at that point `main.c` should shrink to init/loop/unload only,
   with this module owning the `SCREEN_GAME` branch's internals fully.

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
