# Boss Module Agent

## Docs layout
- `docs/API.md` — pure interface (signatures, contracts, invariants)
- `docs/LANDMINES.md` — distilled Symptom→Cause→Rule lessons
- `docs/PROGRESS.md` — backlog / in-progress / log
Read root `ENGINE_LANDMINES.md` before touching GL/shaders.

## Role
Owns `boss/` — Boss Đại Tinh Linh (MODULES_ROADMAP.md Module 5). Strict
engine/data split:
- `boss_system.h/.c` — ENGINE, written once: agent-pool spawn (ARCH_BOSS),
  %HP phase machine, biến hệ via `Entity_SetElement`, AI target + cast loop
  through `core/skill_manager`. **Never include VFX headers here.**
- `<ten_boss>_def.c` — DATA, one file per boss (this is where AI creates new
  bosses): a `BossDef` table + a `drawVisual` callback. ONLY these files may
  include VFX/draw headers (per the roadmap's relaxed rule — boss visuals are
  100% VFX).

## Scope
- **Read/write:** everything under `boss/`
- **Read (interface only):** `entities/entities.h`, `combat/combat.h`,
  `core/skill_manager.h`, core VFX `.h` (in `_def.c` files only)

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Adding a boss (the whole point of the split)
1. New `boss/<ten_boss>_def.c` with a `static void Draw...(const Agent*, float)`
   + `const BossDef BOSS_<TEN_BOSS> = {...}`.
2. `extern const BossDef BOSS_<TEN_BOSS>;` line in `boss_system.h`.
3. Add the file to `CMakeLists.txt`. Engine untouched.
Phase skills are registered skill NAMES (resolved via `Skill_GetIndexByName`
at spawn) — unresolved names simply never cast.

## Hard rules
- Strict C99, static state, no malloc. Single live boss for Phase 0.
- Boss is a pool agent: damage/knockback/ring-out all arrive through the
  normal `Entity_*` path — never special-case boss HP outside entities.
- Visual cue rule (No Tutorial): the phase element MUST be readable from the
  drawVisual (rune/sigil color follows `Agent.currentElement`).

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY): grep before read, cite path:line,
batch reads, terse English replies.
