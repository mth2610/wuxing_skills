# AI Module Agent

## Docs layout
- `docs/API.md` — pure interface (signatures, contracts, invariants)
- `docs/LANDMINES.md` — distilled Symptom→Cause→Rule lessons
- `docs/PROGRESS.md` — backlog / in-progress / log (not created yet — no progress content pending)
Read root `ENGINE_LANDMINES.md` before touching GL/shaders.

## Role
Owns `ai/` — minion brain (MODULES_ROADMAP.md Module 8). Minions are plain
`ARCH_MINION` agents in the shared entities pool (no pool of its own); this
module adds behavior only: march toward the enemy boss, self-destruct AoE.
Later grows hero-AI brains for AI teammates/opponents.

## Scope
- **Read/write:** `ai/ai.h`, `ai/ai.c`
- **Read (interface only):** `entities/entities.h`, `combat/combat.h`
- **Never:** VFX/render headers — explosions are poll events
  (`AI_PollExplosions`), the render side composes the VFX.

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Hard rules
- Strict C99, static arrays, no malloc. Brain is stateless — persistent
  state lives on the Agent, AI_Update walks the pool.
- Movement is an external mover: `Entity_SetPosition` each frame, respect
  `Entity_GetSpeedMult`, yield while airborne/crowd-controlled.
- Damage only through team-aware `Entity_ApplyAoEDamage` / `Entity_ApplyDamage`.

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY): grep before read, cite path:line,
batch reads, terse English replies.
