# entities — Landmines

> Distilled, reusable lessons for the **entities** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting engine traps live in root `ENGINE_LANDMINES.md`. Open backlog / resolved-item log is in `PROGRESS.md`.

_No module-local landmines distilled yet._ Add entries here when a debugging session in `entities/` yields a reusable lesson; promote to `ENGINE_LANDMINES.md` if another module could hit it.

### Watch: AoE buff/damage has no team filtering
- **Symptom:** `Entity_ApplyAoEBuff` buffs every agent in radius, friend or foe.
- **Cause:** no ally/enemy filter parameter exists yet (see `PROGRESS.md`).
- **Rule:** until team filtering lands, don't assume AoE helpers respect teams — filter at the call site if you need friend-only/foe-only.
