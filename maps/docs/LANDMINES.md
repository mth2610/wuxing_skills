# maps — Landmines

> Distilled, reusable lessons for the **maps** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting engine traps live in root `ENGINE_LANDMINES.md`. Backlog/log is in `PROGRESS.md`.

### Vegetation shadows exist in the map but disappear from the viewed meadow

- **Symptom:** The character has an obvious directional shadow while HIGH-quality grass and flowers look ungrounded.
- **Cause:** `maps/toolkit/map_props_nature.inl` culled grass casters around `camera.position` although `environment/env_shadow.c` centers the dynamic shadow box on the gameplay focus. It also reduced grass to one shadow blade per clump and drew the flower far model in the depth pass. The resulting map held vegetation depth, but too little of it reached the visible ground.
- **Rule:** Cull real casters around `EnvShadow_GetFocus()` with chunk bounds, keep a three-blade shadow LOD for grass, and use the flower near model for depth. Check both occupied shadow texels and shadowed receiver samples with `WUXING_SHADOW_DYNAMIC_VERIFY=1`; compare `WUXING_NATURE_SHADOW_MODE=real WUXING_NATURE_SHADOW_CASTERS=all` with `...CASTERS=none` at the same camera. On MED/LOW, real vegetation casters remain disabled by the quality policy in `maps/toolkit/map_props_nature.inl`.

### Pointed grass tips accumulate single-sample aliasing

- **Symptom:** Individual grass tips show tiny bright stair steps that become visible as shimmer across a dense meadow.
- **Cause:** `maps/toolkit/map_props_nature.inl` gave the pointed triangle the final third of each near blade, while its one-segment far LOD made the entire leaf one long, narrow triangle. The default single-sample scene target gives subpixel tips binary coverage; FXAA cannot reconstruct the missing samples.
- **Rule:** Reserve a short final curve span for near pointed tips and make one-segment far blades shorter and wider. In `maps/toolkit/shaders/nature_opaque.fs`, attenuate the final pixel and blades whose projected UV width is subpixel; keep the geometry opaque. Compare fixed close and distant captures; `WUXING_MSAA=4` is a measured quality option with a bandwidth cost (see `ENGINE_LANDMINES.md` #19).

### A player-centred vegetation field drops remote wind impacts

- **Symptom:** Guided Particle visibly impacts grass or flowers, but the plants do not bend.
- **Cause:** Two silent coordinate/wiring errors compounded. On rlvk, `SetShaderValue(shader, ...)` still writes to the currently active shader, but Nature uploaded wind uniforms before `DrawModel` activated the vegetation program. Then `nature_lit.vs` called `matModel * vertexPosition` world space even though this engine's `matModel` includes the view transform; comparing that view-space point with a world-space impact centre made radial attenuation zero. Separately, a player-centred 18 m receiver can reject a remote impact, and the default arena position may contain only grass-coloured terrain rather than vegetation geometry.
- **Rule:** Keep `BeginShaderMode(vegetationShader) -> wind uniform upload -> vegetation draws -> EndShaderMode()` as one scope for both visible and dynamic-shadow passes. Recover a true world position with the inverse current transform before interaction UV or impact-distance math. A uniform direction is valid for Linear Gust only; Radial Blast derives an outward direction per plant, while Vortex and Turbulence stay in the spatial field instead of being collapsed to one patch-wide sample. Diagnose with `WUXING_WIND_RECEIVER_TRACE=1`: require `guided_cast`, one `guided_arrival`, `vegetation_receiver`, then `vegetation_shader ... uniforms=ok`. A missing stage identifies the broken boundary. Source-wiring assertions alone cannot prove active-shader state or coordinate-space agreement.

### Turbulence can hide the authored blast

- **Symptom:** Grass moves in varied directions after impact, but the result still reads as a Perlin wind wave rather than a blast.
- **Cause:** A filled radial falloff bends the entire affected disc simultaneously, while a stronger and much longer Turbulence source visually dominates the short Radial Blast.
- **Rule:** Render a Radial Blast as an age-driven expanding annulus and give it a short uncontested opening beat. A stronger, longer-lived Turbulence wake may become the primary motion after that beat, but ramp it in rather than applying full strength on the spawn frame. CPU and GPU arrival paths must author identical radius, strength, lifetime, and attack behavior; test both the authored budgets and that wavefront response peaks at the moving front rather than at the centre.

### Independent vegetation sway desynchronizes the wind field

- **Symptom:** Grass has attractive waves, but its motion does not line up with smoke, particles, or changes to Global Wind.
- **Cause:** The vertex shader synthesizes standalone sine bands from time and a normalized direction while Core Wind uses a world-space, advected hash-gradient velocity field.
- **Rule:** Wind owns the forcing: mirror the Core macro field in vegetation and superpose local Vorticles. Vegetation owns only its response—compliance, lag, wind-energy-driven flutter, bend limit, and root mask. Give grass and flowers different response profiles, but never give either an independent motion source. Visible and shadow passes must call the same deformation functions.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-09-26 | Codex | Vegetation shadow and grass-tip landmines | `maps/toolkit/map_props_nature.inl`, `maps/toolkit/shaders/nature_opaque.fs`, `environment/env_shadow.c` | Ground-truth |
