# FORMATIONS MODULE API SPECIFICATION

> Module: `formations/` — MODULES_ROADMAP.md Module 10 (Trận Pháp).
> Owner agent: **Formations Agent** (see `formations/CLAUDE.md`).

## 1. Scope & Design — engine/data split (same shape as boss/)

- **ENGINE** (`formation_system.h/.c`, written once): `formationPool[4]`
  (design doc §VI), mana-gated deploy (`Entity_TrySpendMana`), duration
  bookkeeping, zone resonance decided ONCE at deploy via `Map_QueryZoneAt`
  (power `1.5` when the center sits on the def's `resonantZone`, else `1.0`).
  No VFX headers.
- **DATA** (`formations/<ten_tran>_def.c`, one per formation — the
  AI-creates-formations surface): `FormationDef` + `onTick` (gameplay,
  team-aware `Entity_*` calls only) + `drawGround` (VFX; only `_def.c`
  files may include VFX headers).

## 2. FormationDef

```c
typedef struct FormationDef {
    const char    *name;
    CombatElement  elem;
    float          radius, duration, manaCost;
    NatureZoneType resonantZone; // sits exactly on this zone → power 1.5 (NAT_NONE = never)
    void (*onTick)(Vector3 center, float dt, float power, AgentTeam ownerTeam);
    void (*drawGround)(Vector3 center, float t, float power); // inside BeginMode3D
} FormationDef;
```

> Deviation from the roadmap draft (documented intentionally): `onTick`
> gained `power` + `ownerTeam` (resonance strength and buff/debuff scoping
> would otherwise need zone re-queries and owner lookups in every def), and
> `drawGround` gained `power` (resonant formations look stronger).

## 3. Engine API

```c
int  Formation_Deploy(const FormationDef *def, Vector3 center, int ownerAgentId);
     // slot id or -1 (pool full / owner invalid / mana short — nothing sticks)
void Formation_Update(float dt); // ticked from main.c after Boss_Update
void Formation_Draw(void);       // main.c SCREEN_GAME 3D pass
int  Formation_GetActiveCount(void);
```

## 4. Formations shipped

| Def | Effect | Resonance |
|---|---|---|
| `FORMATION_CUU_THIEN_LOI_DONG` | stun pulse (0.6s × power) every 1.5s/power on enemies in 4 m; sky bolt per victim | `NAT_RIVER` |
| `FORMATION_HAN_BANG_THUY_TUYET` | slow field: speedMult 0.6 (0.4 resonant) refreshed every 0.5s on enemies in 3.5 m | `NAT_RIVER` |

Adding one: new `_def.c` + `extern` in `formation_system.h` + CMake line.

## 5. Notes for def authors

- `Entity_AddModifier` (through `Entity_ApplyAoEBuff`) is refresh-not-stack
  for identical multipliers (`../../entities/docs/API.md` §8) — re-applying
  your slow every tick is safe and is the intended idiom.
- No deploy input binding yet — deployment is programmatic (boss/AI/game).

## Autotest

`formation_tran_phap` in `main.c`: mana-gate rejection, 30-mana charge,
resonant slow 0.4 on enemies only, duration expiry, 4-slot pool cap.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-07-20 | Claude | Doc migration: moved from root `formations/docs/API.md` to `formations/docs/API.md` per `DOC_ARCHITECTURE.md`; fixed cross-module reference to `../../entities/docs/API.md`; moved "3 remaining designed formations" roadmap note to `PROGRESS.md` | root `formations/docs/API.md` (read directly) | Ground-truth |
