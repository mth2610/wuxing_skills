# Control Module Agent

## Docs layout
- `docs/API.md` — pure interface (signatures, contracts, invariants)
- `docs/LANDMINES.md` — distilled Symptom→Cause→Rule lessons
- `docs/PROGRESS.md` — backlog / in-progress / log (not created yet — no progress content pending)
Read root `ENGINE_LANDMINES.md` before touching GL/shaders.

## Role
Owns `control/` — the player controller (Module 4).
Splits INPUT (device: keyboard/mouse today, touch/gamepad/net later) from
INTENT (`PlayerIntent`), and applies intents through entities/skill_manager
APIs only. `net/` will serialize `PlayerIntent` verbatim — keep the struct
POD and device-agnostic.

## Scope
- **Read/write:** `control/control.h`, `control/control.c`
- **Read (interface only):** `entities/entities.h`, `core/skill_manager.h`,
  `combat/combat.h`
- **Never:** VFX/render headers; camera and character animation stay in
  `game/` (it reads `Control_GetYaw`/intent, control never draws).

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Hard rules
- Strict C99, no malloc, module-static state only (one local player).
- Movement must respect `Entity_GetSpeedMult` and yield to entities while
  airborne / crowd-controlled / dashing.
- Casting: `SkillManager_CanCast` (cooldown) → `CastSkill` (mana gate lives
  inside skill_manager) → `SkillManager_TriggerCooldown` only on success.
- Basic attack is NOT control's job — `game/` owns it (animation + VFX
  coupling).

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY): grep before read, cite path:line,
batch reads, terse English replies.
