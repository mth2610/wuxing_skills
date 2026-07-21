# Formations Module Agent

## Docs layout
- `docs/API.md` — pure interface (signatures, contracts, invariants)
- `docs/LANDMINES.md` — distilled Symptom→Cause→Rule lessons
- `docs/PROGRESS.md` — backlog / in-progress / log
Read root `ENGINE_LANDMINES.md` before touching GL/shaders.

## Role
Owns `formations/` — Trận Pháp (ROADMAP.md Module 10). Engine/data
split identical to boss/:
- `formation_system.h/.c` — ENGINE, written once: `formationPool[4]`,
  mana-gated `Formation_Deploy`, duration ticks, zone resonance decided once
  at deploy (`Map_QueryZoneAt`, power 1.5 on the def's `resonantZone`).
  **No VFX headers here.**
- `<ten_tran>_def.c` — DATA, one per formation (the AI-creates-formations
  surface): `FormationDef` + `onTick` (gameplay, Entity_* calls only) +
  `drawGround` (VFX — only _def.c files may include VFX headers).

## Scope
- **Read/write:** everything under `formations/`
- **Read (interface only):** `entities/entities.h`, `combat/combat.h`,
  `core/map_manager.h`, `core/decal_system.h` + core VFX `.h` (in `_def.c` only)

## Directories FULLY FORBIDDEN
- `build/`, `_deps/`, `android.wuxing_skills/`

## Adding a formation
1. New `formations/<ten_tran>_def.c` with static onTick/drawGround +
   `const FormationDef FORMATION_<TEN_TRAN> = {...}`.
2. `extern` line in `formation_system.h`.
3. Add the file to `CMakeLists.txt`. Engine untouched.

## Hard rules
- Strict C99, static pool of `MAX_FORMATIONS 4` (thiết kế §VI), no malloc.
- `onTick(center, dt, power, ownerTeam)` — gameplay only; buffs/debuffs via
  team-aware `Entity_ApplyAoEBuff`/`Entity_ApplyStun`/etc. `power` is the
  resonance multiplier (1.0 / 1.5) — scale strength, don't re-query zones.
- Ground visuals must read in the night arena: self-lit primitives /
  element colors, alpha 255.

## Token-efficiency & response rules
Same as root `CLAUDE.md` (MANDATORY).
