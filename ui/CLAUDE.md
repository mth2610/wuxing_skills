# UI Module Agent

## Role
Owns `ui/` — HUD + auto-targeting (MODULES_ROADMAP.md Module 9). Minimal by
philosophy (No Tutorial): skill slot chips with cooldown shading + an
auto-aim reticle. Auto-target priority: enemy projectile in flight (đối-đòn,
from combat/'s snapshot query) → enemy boss → none (caller keeps mouse aim).
Result feeds `PlayerIntent.aimPoint` via game/ — control/ never sees ui/.

## Scope
- **Read/write:** `ui/ui.h`, `ui/ui.c`
- **Read (interface only):** `entities/entities.h`, `combat/combat.h`,
  `boss/boss_system.h`, `core/skill_manager.h` (names/colors/CanCast)

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Hard rules
- Strict C99, static state, no malloc. 2D overlay pass only — never inside
  BeginMode3D (the reticle projects via GetWorldToScreen).
- No instructional text (No Tutorial) — key number + skill name is the cap.
- Auto-aim math must stay read-only (queries; no Entity_* mutations).

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY).
