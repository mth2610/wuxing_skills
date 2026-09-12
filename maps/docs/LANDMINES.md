# maps — Landmines

> Distilled, reusable lessons for the **maps** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting engine traps live in root `ENGINE_LANDMINES.md`. Backlog/log is in `PROGRESS.md`.

### A player-centred vegetation field drops remote wind impacts

- **Symptom:** Guided Particle visibly impacts grass or flowers, but the plants do not bend.
- **Cause:** Two silent coordinate/wiring errors compounded. On rlvk, `SetShaderValue(shader, ...)` still writes to the currently active shader, but Nature uploaded wind uniforms before `DrawModel` activated the vegetation program. Then `nature_lit.vs` called `matModel * vertexPosition` world space even though this engine's `matModel` includes the view transform; comparing that view-space point with a world-space impact centre made radial attenuation zero. Separately, a player-centred 18 m receiver can reject a remote impact, and the default arena position may contain only grass-coloured terrain rather than vegetation geometry.
- **Rule:** Keep `BeginShaderMode(vegetationShader) -> wind uniform upload -> vegetation draws -> EndShaderMode()` as one scope for both visible and dynamic-shadow passes. Recover a true world position with the inverse current transform before interaction UV or impact-distance math. A uniform direction is valid for Linear Gust only; Radial Blast derives an outward direction per plant, while Vortex and Turbulence stay in the spatial field instead of being collapsed to one patch-wide sample. Diagnose with `WUXING_WIND_RECEIVER_TRACE=1`: require `guided_cast`, one `guided_arrival`, `vegetation_receiver`, then `vegetation_shader ... uniforms=ok`. A missing stage identifies the broken boundary. Source-wiring assertions alone cannot prove active-shader state or coordinate-space agreement.

### Turbulence can hide the authored blast

- **Symptom:** Grass moves in varied directions after impact, but the result still reads as a Perlin wind wave rather than a blast.
- **Cause:** A filled radial falloff bends the entire affected disc simultaneously, while a stronger and much longer Turbulence source visually dominates the short Radial Blast.
- **Rule:** Render a Radial Blast as an age-driven expanding annulus and give it a short uncontested opening beat. A stronger, longer-lived Turbulence wake may become the primary motion after that beat, but ramp it in rather than applying full strength on the spawn frame. CPU and GPU arrival paths must author identical radius, strength, lifetime, and attack behavior; test both the authored budgets and that wavefront response peaks at the moving front rather than at the centre.
