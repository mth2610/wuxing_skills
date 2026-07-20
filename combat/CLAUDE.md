# Combat Module Agent

## Docs layout
- `docs/API.md` — pure interface (signatures, contracts, invariants)
- `docs/LANDMINES.md` — distilled Symptom→Cause→Rule lessons
- `docs/PROGRESS.md` — backlog / in-progress / log
Read root `ENGINE_LANDMINES.md` before touching GL/shaders.

## Role
Owns `combat/` — Đấu Pháp: the immediate-mode projectile collider registry +
5x5 Clash Matrix (MODULES_ROADMAP.md Module 3). Skills keep owning projectile
motion/VFX; they submit colliders every frame, combat resolves
projectile↔projectile clashes and projectile↔agent damage, skills poll
`ClashEvent`s to despawn and draw clash VFX.

## Scope
- **Read/write:** `combat/combat.h`, `combat/combat.c`
- **Read (interface only):** `entities/entities.h`, `core/map_manager.h`
  (zone query for the Thổ-in-forest penalty)
- **Never:** any VFX/render header — combat is pure logic, events out only.

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Hard rules
- Strict C99, static arrays only (`MAX_COMBAT_PROJECTILES 128`), no malloc.
- Element indices match `Agent.currentElement` (0=Water..4=Metal) — never
  reorder either enum independently.
- Tương khắc table: Thủy>Hỏa, Hỏa>Kim, Kim>Mộc, Mộc>Thổ, Thổ>Thủy. Same
  element / non-khắc pair → mutual destroy. Same team → pass through silently.
- Skills must NOT call `Entity_ApplyDamage` for projectiles anymore — that is
  combat's job on `CLASH_HIT_AGENT`. AoE/melee skills keep using
  `Entity_ApplyAoEDamage` directly.
- `Combat_Update` runs once per frame from `main.c` (later `game/`), AFTER
  skill updates, and clears all submissions at the end (immediate mode).

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY): grep before read, no full-file dumps,
batch independent reads, respond tersely in English.
