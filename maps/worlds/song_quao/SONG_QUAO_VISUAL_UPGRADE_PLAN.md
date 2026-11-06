# Song Quao — Visual Upgrade Plan

**Target:** AAA stylized-realism for the `song_quao` map — realistic light, materials,
terrain and vegetation, but brighter, more vivid and slightly fantastical than a
photograph. 60 FPS on weak hardware. Wuxing identity preserved.

**Status:** plan only. No code, shader or asset has been changed.
This is a user-requested one-off implementation plan, not a new doc archetype.
When the work lands, durable lessons go to `maps/docs/LANDMINES.md`, API changes to
`maps/toolkit/map_props.h` + `maps/docs/API.md`, and this file can be deleted.

**Measured baseline (2026-11-06, unless stated):**

- `test_song_quao.png` (supplied, 1280×720): median luma **0.324**, lower-frame
  p10 **0.103**, median **0.251**, pixels below 0.05 = **1.24%**, below 0.10 =
  **9.0%**. The supplied overview is **dark and low-contrast**, not clipped black.
- Current binary, same gameplay camera as `scripts/capture_song_quao_cliffs.sh`
  (`94,0.5,150` → `104,9,162`, 1280×720, capture fixture): median luma **0.509**,
  lower-frame p10 **0.371**, pixels below 0.05 = **0.0%**. The current close view
  is **bright**; the supplied overview and the current close capture disagree on
  exposure. Every visual judgment must use the same camera and exposure.
- Frame time on the dev machine (Intel Iris 6000, MoltenVK, 1280×720, capture
  fixture): **LOW 69.7 ms, MED 72.7 ms, HIGH 86.9 ms**. The 60 FPS target is
  **not met before any visual upgrade**; §11 treats this as a first-class
  constraint, not a footnote.

**Contents:** [0. Summary](#0-executive-summary) ·
[1. Evidence](#1-evidence-and-current-state-audit) ·
[2. Target look](#2-target-look) ·
[3. Architecture](#3-architecture-decision-no-new-top-level-module) ·
[4. Phase 0](#4-phase-0--measurement-and-guardrails-12-days) ·
[5. Phase 1 Terrain](#5-phase-1--terrain-35-days) ·
[6. Phase 2 Water](#6-phase-2--water-46-days) ·
[7. Phase 3 Vegetation](#7-phase-3--vegetation-58-days) ·
[8. Phase 4 Lighting](#8-phase-4--lighting-shadows-atmosphere-35-days) ·
[9. Phase 5 Rocks/Cliff/Background](#9-phase-5--rocks-cliff-background-35-days) ·
[10. Phase 6 Post/Camera](#10-phase-6--post-camera-final-grade-23-days) ·
[11. Performance](#11-performance-budget-and-quality-tiers) ·
[12. Assets](#12-asset-pipeline) ·
[13. Verification](#13-verification) ·
[14. Risks](#14-risks-and-mitigations) ·
[15. Schedule](#15-schedule-and-exit-criteria) ·
[16. Open decisions](#16-open-decisions) ·
[Appendix A](#appendix-a--fileline-map) ·
[Appendix B](#appendix-b--capture-commands) ·
[Appendix C](#appendix-c--suggested-first-commit-phase-0--quick-wins)

---

## 0. Executive summary

The engine already contains most of the technology the external review asks for:

- `maps/toolkit/shaders/ground_splat.fs` — multi-layer splat, triplanar cliff,
  macro/micro variation, wet soil, riverbed pebbles, debris, rock maps, shadows.
- `maps/toolkit/shaders/nature_surface.glsl` — grass SSS, wrap lighting, canopy
  normal cohesion, wind, real/static shadow reception.
- `maps/toolkit/shaders/water_surface.fs` — Beer–Lambert absorption, Fresnel,
  sun glints, dual-phase flow detail, foam, caustics, ripples, obstacle wakes.
- `maps/toolkit/shaders/prop_lit.fs` — rock moss/lichen, contact AO, wet base.
- `post_process/` — HDR, ACES, LUT, bloom, FXAA, vignette.
- `environment/` — real sun shadows, static shadows, cloud shadows, height fog,
  local fog volumes, god rays.

The frame is ugly because of **wiring, thresholds, placement and two pieces that
need new code** — not because the shaders are primitive.

The two pieces that need new **runtime** code are:

1. **A water mesh that follows the zero-depth contour smoothly** (today it is two
   triangles per 1 m DEM cell clipped at the shoreline, producing stair steps).
2. **A canopy layer** (trees, bushes, shrubs) for the near-background; today the
   map has only grass, reeds, pampas, flowers, rocks and debris.

Everything else is tuning, distribution, pipeline-bake work (terrain AO, flow
map) and asset work inside existing code. The optional SSAO pass is the only
other new render pass, and it is explicitly not on the critical path.

### Five quick wins (1–3 days if asset regeneration is clean; 3–5 if the cliff seam needs work)

1. **Lift exposure and grade per camera, not a missing shadow floor.** The ground,
   foliage and rock shaders already contain luminous shadow floors (0.56–0.65,
   0.70 and 0.38 respectively — see §1.3), so the supplied overview's darkness is
   not a missing floor. Its lower-frame p10 is 0.103 and median 0.251; the current
   close view is 0.371 / 0.509. Raise ambient (`{76,104,138}` → `{96,124,140}`),
   add a warm ground-bounce term, lift the LUT blacks and reduce cloud-shadow
   coverage before touching the floors. Re-measure both cameras.
2. **Separate and tame the two large-shadow sources.** The huge soft bands are a
   mix of pampas/reed shadow casters (3 blades, 2 segments,
   `shadowDistance = 7.5 m` — `map_props_flora.inl:826-846`,
   `map_props_meadow.inl:1057`) and cloud shadows (strength 0.16, coverage 0.46,
   softness 0.24 — `song_quao.c:403-411`). Isolate them with
   `WUXING_SONGQUAO_SKIP=cloudshadow` before changing either. Then make pampas
   cast a 1-blade/4–5 m shadow and reduce cloud coverage/strength. Raising the sun
   to 30–36° also shortens all casters. Do not cut all flora shadows.
3. **Regenerate the ecology splat with a higher rock-slope threshold.** Rock
   currently starts at 17° and the grass density hard-rejects `splat.g > 175`,
   which is reached at roughly 27° slope; the rolling hills read as grey rock and
   lose grass. Move rock to 22–25° and soften the blend.
4. **Re-enable the cliff skirt.** `song_quao.c:374` forces `MAP_SKIP_CLIFF`; the
   map edge is a raw mesh cut. The cliff implementation already traces the real
   coast loop (`map_props_cliff.inl:590`).
5. **Retune water depth and the hard-coded depth ramp.** DEM max depth is
   **0.426 m**, but the config says `maxDepth = 1.45f` (`song_quao.c:569`) and the
   shader's colour ramp is hard-coded `smoothstep(0.06, 0.95, waterDepth)`
   (`water_surface.fs:485`). Set `maxDepth ≈ 0.5`, retune absorption, and end the
   ramp near 0.45–0.50 m. `maxDepth` alone will not make the channel read deep.

The current close capture is **bright** and the supplied overview is **dark**;
they disagree on exposure. Fix the cameras and exposure in Phase 0 before using
either to judge the other.

### Phase order

| Phase | Scope | Why first |
|---|---|---|
| 0 | Measurement, debug views, capture harness | Stop guessing; every later phase is judged by the same frames |
| 1 | Terrain material + splat + AO | Largest screen area; fixes the “flat green/grey” read |
| 2 | Water mesh + shader + riverbank | The river is the composition’s leading line; stair steps are the most obvious defect |
| 3 | Grass, reeds, flowers, new canopy | Fixes “sparse spikes” and fills the empty background |
| 4 | Lighting, shadows, fog, contact AO | Makes the new materials read; removes black shadows |
| 5 | Rocks, cliff, background layers | Grounds objects and closes the map edge |
| 6 | Post, camera, final grade | Last 10%, and the frame the user actually judges |

**No new top-level module is required.** See §3.

---

## 1. Evidence and current-state audit

### 1.1 What the current frame shows

Source: `test_song_quao.png` and the crops under `/tmp/sq_crops/`
(`grass.png`, `water.png`, `edge.png`, `rocks.png`, `bg.png`). Re-captured the
current binary on 2026-11-06 into `/tmp/sq_current/` (`gameplay_center`,
`cliff_panorama`); the current close view is brighter but shows the same shape,
material and water defects. The supplied overview remains the reference for the
island silhouette and the empty background.

| # | Visible defect | Where it reads most |
|---|---|---|
| 1 | Grass clumps are sparse, tall and spike-like, with large bare gaps | `grass.png` |
| 2 | Large soft shadow bands (pampas/reeds + cloud shadows) and a dark, low-contrast bank in the overview | `grass.png`, `water.png` |
| 3 | Terrain is flat green/grey with hard rock bands and visible triangle planes | `water.png`, `bg.png` |
| 4 | Water edge is a 1 m staircase with a foam line that follows the steps | `edge.png` |
| 5 | Water is a flat grey-teal sheet, no visible bed/refraction/ripples | `water.png`, `edge.png` |
| 6 | Boulders are smooth pale domes sitting on the ground | `rocks.png`, `bg.png` |
| 7 | Map edge is a raw cut; no cliff, no mist collar | `edge.png` |
| 8 | Sky/horizon is flat grey; no background silhouettes or tree line | `bg.png` |
| 9 | Contact shading is weak: rocks cast a smart shadow but read flat at distance; grass has canopy AO but no projected contact shadow | all crops |
| 10 | Current close view: the bank is mostly bare sand; grass is dense only in the wet pocket, so the meadow reads as a patch, not a ground cover | `/tmp/sq_current/gameplay_center.png` |

The empty background is **not** missing terrain: upper land
(`ground_y > 0.5 m`) exists in every 10 m band out to 120 m, and the 40–110 m
band has 3.2–4.8 m of height standard deviation. It reads empty because there is
no canopy, no mist band and no silhouette — see §9.3.

The screenshot is a **wide aerial/overview**, not the 15–18 m gameplay orbit
(`scripts/capture_song_quao_cliffs.sh:53` uses origin `94,0.5,150`, eye
`104,9,162`, i.e. ~15.6 m). The plan therefore judges both:

- the **gameplay/hero view** (what the player sees), and
- the **overview** (what the map reads as from above).

### 1.2 Confirmed code-level causes

| Defect | Confirmed cause | File:line |
|---|---|---|
| Dark, low-contrast overview | Sun elevation ~22° (`sunDir.y = -0.38`), pampas/reed casters out to 7.5 m, cloud shadows 0.16/0.46/0.24, ambient `{76,104,138}` with no warm bounce. The receivers **already** floor at 0.56–0.65 (ground), 0.70 (foliage) and 0.38 (rock); this is not a missing shadow floor. Lower-frame p10 is 0.103 | `song_quao.c:382-386`, `:403-411`, `:543`; `map_props_flora.inl:826-846`; `ground_splat.fs:683-688`; `prop_lit.fs:146-150`; `nature_surface.glsl:84-87` |
| Grass patchiness | Density already fades sand 42→76 and `rockScree` 0.35→0.65, but keeps hard backstops `splat.a > 76` / `splat.g > 175`. The splat's rock starts at 17° (full at 32°), so `splat.g > 0.686` is reached near 27° and the backstop removes grass there. The current close view also shows the sand fade dominating the bank | `song_quao.c:110-116`, `:153-156`; `dem_pipeline.py:562-566` |
| Spiky, not lush | `bladesPerClump = 5`, `bladeSegments = 3`, radius 0.27–0.36 m, height re-randomised to roughly 0.2–0.9 m | `song_quao.c:537-539`, `map_props_flora.inl:443-445` |
| Grey rock bands | `wSlope = smoothstep(0.22, 0.44, slope)` plus `rockCliff = smoothstep(0.32, 0.72, rockScree) * smoothstep(0.12, 0.30, slope)`; rock albedo multiplied by 1.38 | `ground_splat.fs:221-223`, `ground_splat.fs:482` |
| Flat terrain | The `{42,58,28}` tint does **not** darken albedo — it is luminance-normalised and only shifts hue by ±22%. The dark olive substrate (mean RGB 0.25/0.25/0.12) and `turfBase {0.155,0.228,0.086}` do; tiling is 3.6 m, macro variation is subtle, mesh is 1 m | `song_quao.c:494`, `song_quao.c:441`; `ground_splat.fs:314-318` |
| Stair-step water | Generator emits 2 triangles per DEM cell and clips at zero depth; only 3 smoothing passes | `dem_pipeline.py:257-326` |
| Flat grey water | `maxDepth = 1.45f` vs actual 0.426 m; absorption `{0.48, 0.14, 0.05}` absorbs only ~17% red over 0.4 m; the colour ramp is hard-coded `smoothstep(0.06, 0.95, waterDepth)`; terrain water does **not** request scene depth | `song_quao.c:569-570`; `water_surface.fs:485`, `:295-324`; `map_props_water.inl:1270-1272` |
| Smooth dome rocks | The granite branch uses `GenMeshSphere` + 9 soft cleavage planes, but its normals are smooth-welded; only basalt/sandstone/obsidian/slate are flat-faceted. Albedo is pale and the moss mask requires `albedo.g` 0.28–0.72 | `map_props_rocks.inl:28-52`, `:409-412`; `prop_lit.fs:109-123` |
| Raw map edge | `s_skipMask = MapProp_GetSkipMask() \| MAP_SKIP_CLIFF` | `song_quao.c:374` |
| Empty background | No canopy system; only grass/reeds/pampas/flowers/rocks/debris; the outer island has terrain but no tree, shrub or mist silhouette | `map_props.h` (no tree API), `song_quao.c:630-670` |
| Floating objects | No SSAO/GTAO pass; rocks call `Environment_DrawSmartShadow` but read weak at distance; grass has canopy contact AO in the terrain shader but no projected contact shadow | `post_process/` (no AO), `map_props_rocks.inl:783`, `:1052`, `ground_splat.fs:659-661` |

Rows marked "already" were verified in the working tree on 2026-11-06. The
supplied overview's darkness is **not** evidence that a shadow floor is missing;
measure the floor and the ambient separately before changing either.

### 1.3 What already works — do not rewrite

- `ground_splat.fs` already has: splat blending, triplanar cliff, macro/micro
  turf, soil/path, wet shoreline, riverbed pebbles, debris map, rock maps,
  history-free PCF, cloud shadows, hemisphere lighting.
- `nature_surface.glsl` already has: grass/foliage SSS, wrap lighting, canopy
  extinction, root AO, wind, shadow reception with a luminous shadow floor.
- `water_surface.fs` already has: per-vertex bathymetry, Beer–Lambert,
  Fresnel, sun glint, dual-phase flow, foam, caustics, ripples, obstacle wakes,
  optional scene depth.
- `prop_lit.fs` already has: moss/lichen on upward faces, skirt moss, wet base,
  lake damp, contact AO, specular.
- `post_process/` already has: HDR, ACES, LUT, bloom, FXAA, vignette.
- `environment/` already has: real shadows, static shadows, cloud shadows,
  height fog, local fog volumes, god rays, time-of-day presets.
- All three lit receivers already have luminous shadow floors: ground
  `shadowFloor = mix(0.56, 0.65, ...)` (`ground_splat.fs:683-684`), foliage
  `translucentVis = mix(0.70, 1.0, ...)` (`nature_surface.glsl:84-87`), rock
  `shadow = mix(0.38, 1.0, ...)` (`prop_lit.fs:149-150`). Tune them; do not add
  a second floor.

**Rule:** every phase must first try to reach the target by changing parameters,
textures, placement or wiring. Only add a system when the existing one cannot
express the result.

---

## 2. Target look

### 2.1 Style pillars

1. **Lush jade island.** Saturated but natural greens, warm sunlit crests, cool
   damp hollows. Not neon, not olive-grey.
2. **Warm afternoon, cool distance.** A low-but-not-red sun, warm key light,
   cool teal ambient, cool mist in the background.
3. **Painterly realism.** Real material response (roughness, normal, wetness,
   SSS), but shapes and colours are slightly enlarged and idealised.
4. **Readable gameplay.** The arena centre stays open; no trees or tall grass
   blocking the camera or skill telegraphs.
5. **Wuxing identity.** Jade water, mossy stone, warm wood, elemental accents
   (water = teal/jade, wood = living green, earth = warm ochre, metal = cool
   grey, fire = warm accent). No generic European fantasy.

### 2.2 Palette and value structure

| Layer | Value (sRGB) | Saturation | Temperature | Materials |
|---|---|---|---|---|
| Foreground 0–12 m | 0.25–0.55 | high (0.55–0.75) | warm sun + cool shade | detailed grass, flowers, pebbles, wet soil, bank rocks |
| Midground 12–40 m | 0.40–0.65 | medium (0.40–0.55) | neutral | river, reeds, shrubs, low hills, path, rocks |
| Near-background 40–90 m | 0.55–0.75 | low-medium (0.25–0.40) | cool | tree clumps, low forest, mounds, mist |
| Sky/horizon | 0.65–0.85 | low (0.15–0.30) | cool zenith, warm horizon | gradient sky, sun disk, thin haze |

Key colours (starting points, tune with captures):

- Sun: `{255, 226, 178}`, intensity 1.60–1.70 (§8.1 is authoritative if the two
  disagree).
- Sky ambient: `{96, 124, 140}` (§8.1).
- Ground bounce: `{84, 70, 48}`.
- Shadow tint: `{46, 66, 78}` with enough alpha/strength to read as cool shade,
  never black.
- Grass root: `{36, 54, 24}`; tip: `{142, 190, 82}`; dry accent `{176, 164, 96}`.
- Water shallow: `{96, 172, 154}`; deep: `{18, 78, 86}`; foam `{246, 250, 248}`.
- Rock: warm grey `{118, 112, 104}` with moss `{58, 86, 38}` and lichen
  `{150, 150, 120}`.
- Soil: `{78, 58, 40}` dry, `{48, 38, 28}` wet.

### 2.3 Three-layer composition

The review’s “three-layer isometric composition” is a **staging** rule, not a
terrain-layout rewrite. Keep the existing river, hills and arena.

| Layer | Screen band (hero camera) | Contents | Technique |
|---|---|---|---|
| Foreground | bottom 0–35% | grass bank, flowers, pebbles, one hero boulder, water edge, wet soil | highest density, SSS, contact shadows, detail normals |
| Midground | 30–65% | river S-bend, reed belt, shrubs, path, low hill, 2–3 rocks | full LOD, real shadows, caustics, foam |
| Near-background | 55–85% | tree clumps, low forest band, mounds, cliff rim, mist | billboards/impostors, desaturation, fog, no hard detail |
| Sky | top 15–45% | gradient sky, sun glow, very thin haze | sky dome; **no distant mountain range** |

Composition rules:

- River is the leading line; place the hero camera so the river enters from the
  lower third and bends behind the midground hill.
- Keep one clear focal point: a sunlit clearing or a large mossy boulder at the
  bend, framed by a foreground grass/boulder cluster.
- Use value contrast, not outline: dark foreground framing, bright midground
  focal point, pale background.
- No wide horizon, no distant cloud bank, no mountain silhouettes. The island
  itself and the mist are the horizon.

### 2.4 Camera

The current gameplay camera is perspective orbit, ~15–18 m, pitch ~26–35°
(`sandbox/sandbox_core.h` defaults; capture uses origin/eye). The review’s
“isometric” look is best approximated by a **long-lens 3/4 perspective**, not
orthographic:

- FOV 28–32° (not 60–75°), pitch 34–38°, yaw 30–45°, distance 20–26 m.
- Orthographic would flatten depth cues and break skill aiming/telegraph
  readability. Do not switch gameplay to orthographic without a design decision.

**Decision for this plan:** add a capture-only hero camera preset (no gameplay
change). If the user wants the gameplay camera changed, that is a separate
cross-module task in `sandbox/`, `control/` and `main.c`.

---

## 3. Architecture decision: no new top-level module

### 3.1 Why no new module

- Terrain, water and vegetation are **map-scoped rendering**. `maps/toolkit`
  already owns them; `maps/worlds/song_quao` owns the content.
- Lighting, shadows and fog already belong to `environment/`.
- Screen-space AO belongs to `post_process/`; it is a post pass, not a new
  module.
- Trees/bushes are a shared map prop, so they belong in `maps/toolkit` as a new
  subsystem file, exactly like rocks/meadow/flora.

A new `vegetation/` module would only be justified if non-map systems (skills
growing forests, bosses destroying trees) needed it. That is not the case today.
If it becomes one later, promote `map_props_canopy` then; do not pre-build it.

### 3.2 Ownership matrix

| Area | Owner | Files |
|---|---|---|
| Map content, config, staging | Map Agent | `maps/worlds/song_quao/song_quao.c/.h`, `maps/worlds/song_quao/` |
| Terrain/water/vegetation rendering | Map Agent | `maps/toolkit/map_props*.inl`, `maps/toolkit/shaders/*` |
| DEM/asset pipeline | Map Agent | `maps/pipeline/dem_pipeline.py`, `maps/pipeline/*` |
| Map assets | Map Agent | `assets/maps/song_quao/*` |
| Sun, shadows, fog, cloud shadows | Environment Agent | config through `environment/environment_system.h`; shader changes in `environment/` |
| AO pass, grade, bloom | Post-Process Agent | `post_process/*` |
| Camera preset, post config | Game/Sandbox Agent | `main.c`, `sandbox/*` |
| Quality tiers | Core Agent (API) / Map Agent (reads) | `core/gfx_quality.h` |

### 3.3 New files and API additions

**New toolkit subsystem (trees/bushes):**

- `maps/toolkit/map_props_canopy.inl` — implementation.
- API added to `maps/toolkit/map_props.h` (single public header, matching the
  existing pattern):
  ```c
  typedef enum { MAP_CANOPY_BROADLEAF = 0, MAP_CANOPY_PINE, MAP_CANOPY_SHRUB } MapCanopySpecies;

  typedef struct {
      Vector3 position;
      float rotationDeg;
      float scale;
      unsigned char species;
      unsigned char variant;
  } MapCanopyPlacement;

  typedef struct { /* near/far models, placements, count, bounds, ready */ } MapCanopySystem;

  MapCanopySystem MapProp_CreateCanopySystem(const MapCanopyPlacement *placements, int count);
  void MapProp_DrawCanopySystem(MapCanopySystem *canopy, Vector3 cameraPosition);
  void MapProp_DrawCanopyShadowCasters(MapCanopySystem *canopy, Shader depthShader);
  void MapProp_UnloadCanopySystem(MapCanopySystem *canopy);
  ```
- New skip flag: `MAP_SKIP_CANOPY = 1 << 15` (append-only).
- Shaders: `maps/toolkit/shaders/canopy_lit.vs/.fs`, `canopy_shadow.vs/.fs`
  (or reuse `nature_lit` + `nature_shadow` if the material fits; prefer reuse
  first, add a variant only if moss/bark needs it).

**New pipeline outputs:**

- `maps/pipeline/terrain_bake.py` — curvature AO, wetness, slope, meadow density.
- `maps/pipeline/water_mesh.py` — marching-squares water contour, flow map,
  water edge map. Or extend `dem_pipeline.py` functions in place.
- `maps/pipeline/canopy_placements.py` — tree/bush placement JSON.

**New terrain AO binding (Phase 1):**

```c
void MapProp_SetGroundAoMap(MapGroundSurface *ground, const char *path);
```

Loads the 4-channel bake from §5.4 and binds it as `u_terrainAo`. If an existing
ground map has a free channel, reuse it and skip this API; do not allocate a new
sampler for one scalar.

**New post pass (optional, Phase 4b):**

- `post_process/ssao.h/.c`, `post_process/shaders/ssao.fs`, `ssao_blur.fs`.
- Only if baked AO + contact shadows are not enough and profiling allows.

**New tests:**

- `maps/tests/test_water_contour.py`
- `maps/tests/test_water_depth_range.py`
- `maps/tests/test_meadow_density.py`
- `maps/tests/test_canopy_placement.py`
- `maps/tests/test_canopy_lod.py`
- `maps/tests/test_terrain_shadow_caster.py` (source-level or runtime)
- `maps/tests/test_post_ssao.py` (if SSAO lands)

---

## 4. Phase 0 — Measurement and guardrails (1–2 days)

**Goal:** make every later change measurable on identical frames.

### Tasks

1. **P0.1 — Capture harness.**
   Add `scripts/capture_song_quao_layers.sh` (copy the pattern from
   `scripts/capture_song_quao_cliffs.sh`) with four fixed cameras:
   - `hero_bank` — 3/4 view over the near bank toward the river bend.
   - `gameplay_center` — existing arena camera (`94,0.5,150` → `104,9,162`).
   - `overview_ridge` — wide aerial matching `test_song_quao.png`.
   - `cliff_panorama` — existing cliff view.
   Same resolution (1600×900 or 1920×1080), same frame, same warmup, no random
   gameplay. Save revision, binary hash, and log.
2. **P0.2 — Baseline metrics.**
   Add `scripts/analyze_song_quao_capture.py` (numpy/Pillow) computing:
   - shadow black ratio: % of pixels with luma < 0.05 inside the lower 2/3;
   - grass coverage: % of foreground ground pixels covered by green blades;
   - water edge step count: 90° corners per 100 m of shoreline;
   - terrain hue variance in the meadow mask;
   - horizon fill: % of the sky band covered by silhouettes/mist;
   - average saturation/value per layer.
3. **P0.3 — Debug views.**
   Add a `u_debugMode` uniform to `ground_splat.fs`:
   `0 = final, 1 = splat, 2 = relief, 3 = macro, 4 = world normal,
   5 = AO, 6 = shadow visibility, 7 = wet band`.
   Add `u_debugMode` to `water_surface.fs`: `0 = final, 1 = depth, 2 = foam,
   3 = flow, 4 = normal, 5 = fresnel`.
   Add `u_debugMode` to `nature_surface.glsl`: `0 = final, 1 = shadow,
   2 = SSS, 3 = canopy normal`.
   These are capture-only paths; compile them out or guard with a uniform so
   shipping cost is one branch.
4. **P0.4 — Performance baseline.**
   Run `scripts/profile_song_quao.sh` and record full/skip passes for far and
   close views at LOW/MED/HIGH. The headless capture freezes the tier
   (`GfxQuality_UpdateAuto` is skipped when `renderVFXMode`), so set
   `WUXING_GFX_QUALITY=low|med|high` explicitly and also record the normal-play
   AUTO rung. Record: frame ms, draw calls, triangles, shadow ms, post ms, VRAM
   if available. **Gate:** if LOW is already > 16.6 ms at the acceptance
   resolution, stop and schedule the optimization phase before Phase 1.
5. **P0.5 — Acceptance board.**
   Save before captures under `/tmp/song_quao_before/` (not committed). Define
   the numeric targets from §13.2 before touching anything.

### Exit criteria

- Four captures render deterministically.
- Metrics script runs and prints a table.
- Debug views show the splat/relief/shadow channels without recompiling.
- Baseline frame time recorded for each quality tier.

---

## 5. Phase 1 — Terrain (3–5 days)

**Goal:** the ground reads as a living meadow with mossy rock outcrops, wet
banks and micro-relief — not a flat green/grey shell.

### 5.1 Material audit first

Before changing values, capture debug views:

- `u_debugMode = 1` (splat): confirm which channel dominates the visible slopes.
- `u_debugMode = 2` (relief): confirm the relief texture is bound and non-flat.
- `u_debugMode = 3` (macro): confirm macro variation exists.
- `u_debugMode = 5` (AO): confirm the AO term is not crushing everything.
- `u_debugMode = 6` (shadow): confirm the black areas are shadow, not albedo.

If a texture is missing or a uniform is zero, fix the binding first. Do not
compensate with color.

### 5.2 Regenerate the ecology splat

File: `maps/pipeline/dem_pipeline.py:558-572`.

Current: `rock_slope = clamp((slope - 17°) / 15°)`; `rock_ridge` from TPI;
`rock = max(rock_slope, rock_ridge, 1 - cliff)`;
`growth = clamp(0.45 + 0.55 * moisture, 0, 1) * (1 - rock)`; grass density
hard-rejects `splat.g > 175` (reached near 27°).

Change:

- Rock starts at **22–24°**, full rock at 38–42°:
  `rock_slope = clamp((slope - 23°) / 17°, 0, 1)`.
- Reduce `rock_ridge` weight (e.g. multiply by 0.6) so only true ridges are
  rocky, not every convex crest.
- Keep `rock[water_depth > 0.01] = 0` and
  `rock = max(rock_slope, rock_ridge, 1 - cliff)`.
- Soften `growth`: `growth = clamp(0.55 + 0.45 * moisture, 0, 1) * (1 - 0.85 * rock)`.
- Add a **meadow density** channel (see 5.4) rather than relying on hard splat
  thresholds.

Regenerate all derived assets (heightfield, splat, water height, habitat,
rock/flora placements) with the same seed. Verify the map dimensions and
height range did not change.

### 5.3 Ground shader tuning

File: `maps/toolkit/shaders/ground_splat.fs`.

| Change | Current | Target | Why |
|---|---|---|---|
| Primary cliff blend (`wSlope`) | `smoothstep(0.22, 0.44, slope)` | `smoothstep(0.30, 0.55, slope)` | Keep grass on 20–30° hills |
| Rock-gate slope (`rockCliff`) | `smoothstep(0.12, 0.30, slope)` | `smoothstep(0.18, 0.36, slope)` | Stop rock bleeding onto gentle slopes |
| Cliff blend rock splat | `smoothstep(0.32, 0.72, rockScree)` | `smoothstep(0.45, 0.85, rockScree)` | Rock only where the splat is strong |
| Rock albedo gain | `rockBase * 1.38` | `rockBase * 1.10–1.20` | Stop pale grey slopes |
| Rock tint | neutral | warm grey + moss tint | Stylised realism |
| Turf base albedo | `{0.155, 0.228, 0.086}` | `{0.20, 0.28, 0.11}` | Measured dark substrate; the `{42,58,28}` tint only shifts hue ±22% and does not brighten |
| Macro variation | subtle | ±8–12% value, ±6–10° hue at 6 m and 22 m | Break flat colour |
| Micro normal | one detail map | second detail normal at 0.35 m, faded by 8 m | Hide 1 m facets at close range |
| Wet band | ~0.3 m | 0.8–1.2 m, darker, roughness 0.35, algae tint | Riverbank transition |
| Shadow floor | `mix(0.56, 0.65, ...)` | keep; raise to 0.62–0.70 only if lower-frame p10 < 0.10 | The floor already exists; measure before changing it |
| Ambient / exposure (config, not shader) | ambient `{76,104,138}`, exposure 1.0 | ambient `{96,124,140}` + warm bounce; exposure 1.0–1.05 | Lift the overview's p10 0.103 / median 0.251 |

Implementation notes:

- Add uniforms `u_detailNormal2`, `u_detailScale2`, `u_terrainAo`,
  `u_shadowFloor` (or hard-code the floor in the shader if no tuning needed).
- The wet band already exists (`ground_splat.fs:520-566`); widen it and make it
  follow the signed shore distance from the regenerated `song_quao_water_height.png`.
- Do not add parallax occlusion mapping yet. First verify whether the facets are
  geometry or material. POM is a HIGH-tier option only if needed.

### 5.4 Baked terrain AO and wetness

Add `maps/pipeline/terrain_bake.py` producing
`assets/maps/song_quao/song_quao_terrain_ao.png` (1024×1024 RGBA):

- R: curvature AO (concave = darker, convex = lighter), blurred 2–4 px.
- G: wetness (distance to water + TWI + shore band).
- B: slope (for shader-side rock/grass blend).
- A: meadow density (macro patches + TWI + slope + noise).

Bind it with a new `MapProp_SetGroundAoMap` (or reuse `u_groundRelief` if the
packing allows). Multiply albedo by `mix(1.0, ao, 0.35)` — subtle, not a dirty
grunge pass. This is the cheapest way to get crevice/valley shading without
SSAO.

### 5.5 Mesh and faceting audit

The heightfield is 189×301 (~1 m cells) with smoothed normals
(`map_props_heightfield.inl:99-138`). Facets in the capture may come from:

1. material bands (`wCliff`) rather than geometry;
2. texture tiling at 3.6 m;
3. normals at steep slope changes.

Do this in order:

1. Fix material bands (5.3) and re-capture.
2. If geometric facets remain at the hero distance, add a **high-quality
   0.5 m heightfield** variant (377×601, ~226k verts, ~452k tris) behind
   `GfxQuality_Get() >= GFX_HIGH`. Do not ship it on LOW/MED.
3. Alternative if 0.5 m is too heavy: keep 1 m and add a screen-space detail
   normal + POM only within 12 m on HIGH. Measure both.

Do not change the map’s macro heightfield shape; the review explicitly says to
learn from the image’s rendering treatment, not its terrain layout.

### 5.6 Terrain shadows (optional, profile first)

Terrain currently does not cast shadows:

- `CaptureSongQuaoStaticShadows` draws only rocks (`song_quao.c:355-368`);
- `DrawSongQuaoShadowCasters` draws only flora (`song_quao.c:345-353`);
- `MapProp_DrawGroundShadowCaster` exists but is unused (`map_props.h:104`).

Options, in order of cost:

1. **Baked AO only** (5.4) — no runtime cost, fixes most crevice shading.
2. **Dynamic terrain shadow** — add `MapProp_DrawGroundShadowCaster` to
   `DrawSongQuaoShadowCasters`; the 18 m focus covers the playable view. The
   whole ground is ~113k tris; profile the shadow pass before keeping it.
3. **Static terrain shadow** — add it to `CaptureSongQuaoStaticShadows`; the
   static capture radius is 156 m (`song_quao.c:361`), i.e. a 312 m box. At
   1024² (MED/HIGH) that is ~30 cm per texel; at 512² (LOW) ~61 cm. Too coarse
   for close contact, fine for large hill shadows. Use only if dynamic is too
   costly.

`maps/docs/API.md:333-341` already records the trap: a shallow heightmap under a
low sun quantizes into full-screen parallel acne bands, so the ground is excluded
from the static caster layer by design. Terrain self-shadow is only viable with
steep relief, a raised sun and a verified receiver bias.

Acceptance: terrain self-shadow must not introduce acne or a visible seam
between dynamic/static coverage. If it does, drop it and rely on AO.

### 5.7 Tests and captures

- Existing: `maps/tests/test_ground_relief.py`,
  `test_ground_lighting_space.py`, `test_ground_binding.py`,
  `test_heightfield_mesh.py`.
- New: `test_terrain_shadow_caster.py` (assert the caster callback includes
  ground when enabled; assert it is excluded when `MAP_SKIP_GROUND`).
- Capture: `hero_bank`, `gameplay_center`, `overview_ridge`; compare turf
  coverage, hue variance, black ratio.

### Exit criteria

- No hard grey rock band on gentle hills; rock appears only on steep/rocky
  areas.
- Meadow covers ≥ 75% of the playable basin (excluding water/path/rock).
- No visible flat 1 m facets at the hero distance.
- Lower-frame p10 ≥ 0.16 (overview) / ≥ 0.30 (close); no crushed black
  clusters that are not deliberate silhouettes.
- No frame-time regression > 5%.

---

## 6. Phase 2 — Water (4–6 days)

**Goal:** a clear, shallow, jade river with a smooth shoreline, visible bed,
flow, foam and caustics — not a flat teal sheet.

### 6.1 Replace the stair-step water mesh

File: `maps/pipeline/dem_pipeline.py:257-326` (`write_water_mesh`).

Current: 2 triangles per DEM cell, clipped at zero depth, explicit vertices per
triangle, 3 smoothing passes → 1 m stair steps.

Target: **marching-squares contour mesh** at 0.5 m (use the existing 2×
super-resolved `ground2`/`water_plane2` fields):

1. Sample `depth = max(0, water_plane - ground)` at the 2× grid vertices.
2. For each cell, classify the 4 corners as wet/dry at `depth > 0.002 m`.
3. Emit full cells as 2 triangles; emit boundary cells as 1–2 polygons using
   marching squares, with contour vertices on cell edges (linear interpolation
   of position and depth).
4. Cache edge intersections by edge ID so adjacent cells share vertices — no
   cracks/T-junctions.
5. Triangulate each boundary polygon as a fan around its centroid.
6. Write `vt.x = depth` (meters) and `vt.y = signed distance to shore` (or flow
   speed) for the shader.
7. Optional: 1–2 Chaikin smoothing passes **on the contour polyline only**,
   then re-triangulate; do not smooth the whole grid or you move the waterline.

Also output:

- `assets/maps/song_quao/song_quao_water_edge.png` — R signed distance to shore,
  G foam mask, B depth; 1024×1024, clamp.
- `assets/maps/song_quao/song_quao_flow.png` — RG flow direction, B speed;
  512×512, repeat/clamp as appropriate.

Acceptance:

- At the hero distance, no 90° shoreline corners; max deviation from the smooth
  contour < 0.10 m.
- Water area matches the raster mask within 2%.
- No cracks when viewed from above or at grazing angles.

### 6.2 Flow map from D8

`compute_d8_flow` already exists (`dem_pipeline.py:139`). For each water cell:

- Convert the D8 direction to a 2D unit vector.
- Accumulate/blur along the channel (2–4 px Gaussian) so the flow is coherent.
- Encode `(dx * 0.5 + 0.5, dz * 0.5 + 0.5)` in RG, normalized speed in B.
- In `water_surface.fs`, sample the flow map at `fragWorldXZ` and replace the
  constant `u_flowVelocity` with `mix(u_flowVelocity, flowMap.xy * 2 - 1,
  flowStrength)`. Keep the global vector as a fallback and as an artist override.

This is what makes the dual-phase flow detail (already in the shader, lines
160–190) actually follow the river instead of a single diagonal.

### 6.3 Shader and config retune

File: `maps/toolkit/shaders/water_surface.fs`; config in
`maps/worlds/song_quao/song_quao.c:557-579`.

| Parameter | Current | Target | Why |
|---|---|---|---|
| `maxDepth` | 1.45 | 0.50 | Actual DEM max is 0.426 m; feeds the radial shapes and the scene-depth clamp |
| Depth colour ramp (shader) | `smoothstep(0.06, 0.95, waterDepth)` | `smoothstep(0.03, 0.45, waterDepth)` | **The real fix**: `u_maxDepth` does not feed this ramp, so 0.426 m currently reaches only ~39% of the deep colour |
| `absorption` | `{0.48, 0.14, 0.05}` | `{2.0, 0.70, 0.30}` (tune) | 0.4 m should tint, not stay clear |
| `deepColor` | `{16,62,78}` | `{18,78,86}` | Jade-teal, not navy |
| `shallowColor` | `{64,148,138,72}` | `{96,172,154,255}` | Brighter shallows; **alpha is ignored** |
| `foamColor` | `{244,248,250,215}` | `{248,252,250,255}` | Clean foam; **alpha is ignored** |
| `foamThreshold` | 0.085 (effectively 0.12 — shader clamps `max(u_foamThreshold, 0.12)`) | 0.14–0.18 | Wider, softer foam band; values below 0.12 are no-ops |
| `waveHeight` | 0.011 | 0.025–0.04 | Visible ripples |
| `detailStrength` | 0.26 | 0.35–0.45 | Micro-normal reads at hero distance |
| `causticsStrength` | 0.52 | 0.70 | Bed needs light |
| `causticsScale` | 3.60 | 4.20 | Smaller, denser caustic cells |
| `scatterCoeff` | 0.26 | 0.35 | More jade SSS |

Also:

- **Enable scene depth for terrain water.** In `map_props_water.inl:1270-1272`,
  `needsSceneDepth` excludes `WATER_SHAPE_TERRAIN`. Remove that exclusion and
  verify the shader combines vertex bathymetry with hardware depth
  (`water_surface.fs:295-324`). Caveat: `SceneTargets_GetDepthTexture()` is the
  **previous frame's** linearised snapshot (`main.c:1923` calls
  `SceneTargets_SnapshotDepth` after the scene pass; see
  `environment/volumetric/docs/LANDMINES.md` #1). Never read the live
  attachment. The one-frame lag is acceptable for slow water; do not use it for
  fast occlusion. Keep the vertex-depth fallback when the texture is unavailable.
- **Fresnel / sky reflection.** Keep F0 ≈ 0.02; add a cheap procedural sky
  reflection color from `MapSkyRadiance` (or a 2-color gradient) modulated by
  Fresnel. Do **not** add planar reflections; too expensive for the target.
- **Refraction.** Only for shallow water (`waterDepth < 0.6 m`), sample the
  scene color with a normal-based UV offset of 0.02–0.06 m, clamp to avoid
  pulling in particles/VFX. Disable on LOW.
- **Foam.** Combine shoreline distance, flow speed and obstacle wakes. Widen the
  band to 0.12–0.35 m; animate with the flow map. Add contact foam around
  submerged rocks (the obstacle system already feeds `u_obstacle0..3`).
- **Caustics on the bed.** The water shader adds caustics to the water color;
  the bed itself is terrain. Add a caustic projection to `ground_splat.fs` when
  `shoreFactor > 0` and `waterDepth > 0.02`, using the same
  `water_caustics.png` and flow map. This makes the shallow riverbed alive.
- **Water bed.** song_quao calls `MapProp_DrawWaterOverlay` directly
  (`song_quao.c:668`); `MapProp_DrawWaterBed` is only invoked by
  `MapProp_DrawWaterSurface` (`map_props_water.inl:1307`), so the separate bed
  mesh is **not** drawn. The terrain is the bed. Do not add the bed mesh blindly;
  improve the underwater terrain shading instead. If a separate bed is later
  needed (e.g. a deep lake), revisit.

### 6.4 Riverbank transition

- Regenerate reeds/pampas placements from the new signed distance field:
  reeds within 0.05–0.35 m of water, pampas 0.3–1.2 m, with a smooth density
  falloff (no hard rings).
- Add bank pebbles/small rocks along the contour (new placement pass from
  `song_quao_water_edge.png`), embedded 30–40%.
- Widen the wet band in the terrain shader (5.3), add a thin sand strip
  (0.2–0.5 m) and darker algae/moss at the waterline.
- Ensure the foam line follows the actual contour, not the old 1 m mesh.

### 6.5 Tests and captures

- Existing: `maps/tests/test_water_wave_upload.py`,
  `test_dem_pipeline.py`.
- New: `test_water_contour.py` (mesh area, no 90° corners, watertight edge),
  `test_water_depth_range.py` (config `maxDepth` within 20% of DEM max).
- Capture: `edge.png` equivalent from the same camera; compare shoreline
  smoothness, foam continuity, bed visibility.

### Exit criteria

- Shoreline is smooth at hero distance (no visible 1 m steps).
- Water reads clear at the edge and jade in the channel; bed and pebbles visible
  through 0.1–0.2 m of water.
- Foam is continuous and follows the bank; no foam “staircase”.
- Caustics visible on the shallow bed.
- No refraction artifacts on particles/VFX.
- Frame-time regression < 0.5 ms at MED.

---

## 7. Phase 3 — Vegetation (5–8 days)

**Goal:** a dense, soft, wind-driven meadow with readable clumps, plus a
near-background canopy that fills the empty horizon.

### 7.1 Grass distribution and shape

Files: `maps/worlds/song_quao/song_quao.c:87-164`, `:214-226`,
`maps/toolkit/map_props_flora.inl:334-453`,
`maps/toolkit/map_props_meadow.inl:121-192`.

Current:

- spacing 0.33 m, capacity 98k, clump radius 0.27–0.36 m,
  height 0.35–0.56 m (then re-randomised to roughly 0.2–0.9 m),
  5 blades, 3 segments, width 0.138, draw 40 m, LOD 21/11 m, shadows 7.5 m.
- density already fades sand 42→76 and `rockScree` 0.35→0.65, then keeps hard
  backstops at `splat.a > 76` / `splat.g > 175`. The current close view shows
  the bank is bare because `splat.a` (sand) dominates there, not because of
  spacing.

Target:

1. **Two layers.**
   - Ground cover: spacing 0.22 m, radius 0.12–0.20 m, height 0.12–0.22 m,
     3 blades, 1–2 segments, draw 18 m, **no shadows**.
   - Main tufts: spacing 0.28–0.30 m, radius 0.18–0.30 m, height 0.20–0.42 m,
     7 blades, 3 segments, draw 45 m, shadows 4 m.
2. **Smooth density** by extending the existing fades, not adding a second set:
   - `sandFade = 1 - smoothstep(60, 110, splat.a)` and remove the hard
     `splat.a > 76` reject (or raise it above 110);
   - `rockFade = 1 - smoothstep(0.55, 0.85, rockScree)` and remove/raise the
     `splat.g > 175` backstop;
   - `slopeFade` from 0.65 to 0.85 normal.y (grass on moderate slopes).
3. **Clump clustering.** The generator already computes a Voronoi patch angle;
   add a density mask (`song_quao_meadow_density.png`, §5.4) so patches are
   intentional and match the terrain AO/wetness.
4. **Shape.** Shorter, wider, more curved blades. Increase blade curvature and
   lean; reduce the tall spike look. Keep taller pampas/reeds near water and
   rocks as accent, not as the default grass.
5. **Capacity.** Keep 98k for the main layer and add a separate 20–50k ground
   cover layer (tiered, §11.3), or raise the cap only after profiling. Do not
   double the triangle count blindly.

### 7.2 LOD

File: `maps/toolkit/map_props_meadow.inl:1030-1090`,
`MapProp_DrawMeadow` at `:1303` (shadow gate `:1491-1509`).

Current far LOD uses 2 blades at 1.85× width — that is what makes distant grass
read as sparse spikes.

Target:

| Distance | Geometry | Shadows | Notes |
|---|---|---|---|
| 0–10 m | 7 blades, 3 segments, full width | Real shadow, 3 blades, 1–2 segments | Full SSS/detail |
| 10–22 m | 3 blades, 2 segments, 1.25× width | Real shadow optional, fade out | Dither fade |
| 22–45 m | 2 crossed billboard cards with a grass texture | None | No giant single blades |
| > 45 m | Fade to terrain colour / density mask | None | No popping |

- Add a dither/alpha fade between LODs (0.5 m band).
- Keep chunked culling; cap visible chunks per frame on LOW.

### 7.3 Shading

File: `maps/toolkit/shaders/nature_surface.glsl`.

The shader already has the right features; tune them:

- Root AO is `0.74–0.90` (`:372`). Keep or deepen slightly for grounding.
- `FoliageShadowVisibility` already has a luminous floor of 0.70 (`:84-86`) and
  the terrain has 0.56–0.65 (`ground_splat.fs:683-684`). The grass-shadow
  problem is the **caster** (3-blade pampas shadows out to 7.5 m) and the low
  ambient, not a missing receiver floor. Tune the floors only after measuring the
  lower-frame p10.
- Increase backlit SSS only when the sun is behind the blade; avoid a constant
  glow.
- Add a per-blade hue variation of ±5–8° and a per-clump value variation of
  ±10%, driven by `phase`/instance hash.
- Add a distance desaturation toward the terrain/fog colour so the meadow does
  not stay neon green at 40 m.
- Keep the canopy-normal cohesion; it is what makes grass read as a soft volume.

### 7.4 Wind

The wind system already exists (`Nature_BeginWindReceiverShader`,
`MapProp_AddNatureWindVorticles`, interaction bending). Tune:

- Global wind 0.6–1.0 m/s equivalent; gust amplitude 0.25–0.4.
- Per-clump phase from `MapMeadowPlacement.phase`; no synchronized waving.
- Add a slow “wind wave” across the meadow using the existing macro flow angle
  (`MapProp_GetMacroFlowAngle`).
- Keep player/skill interaction bending; it sells the meadow.

### 7.5 New canopy system (trees, bushes, shrubs)

**Why:** the near-background is empty. Grass draw distance is 40–45 m; beyond
that there is only terrain and sky. Trees/bushes provide the third layer and
the silhouette that the review asks for.

**Design for weak hardware:**

- Procedural low-poly meshes, no external modeling dependency:
  - Broadleaf: 5–7-sided trunk, 3–5 foliage clusters made of alpha-tested
    cross-cards or low-poly blobs. Near ~1.5–3k tris, far 2 cards.
  - Pine: 6–8-sided trunk, 3 stacked cones/cards. Near ~1–2k tris.
  - Shrub: 3–5 cards or a low-poly blob, 0.6–1.5 m.
- 2 LODs + billboard/impostor. Dither fade at 35–45 m.
- Wind vertex animation with per-instance phase; foliage translucency in the
  lit shader; bark roughness; moss on the lower trunk and upward faces.
- Shadow: near trees (within 20 m of the shadow focus) cast real shadows using
  a 1–2k tri shadow model; far trees use a soft blob/contact shadow or none.
- Placement: generated from the DEM (slope < 0.35, water clearance > 1.5 m,
  `ground_y > 0.5 m`, exclude cliff/void cells, not on path, not within 18 m of
  the arena centre), clustered via Poisson. 60–120 trees + 150–250 bushes,
  concentrated at 30–90 m and along the river bends; 2–3 dense clumps on the far
  ridge for the background silhouette.
- Draw order: opaque, after ground, before water/transparent VFX.
- Skip flag: `MAP_SKIP_CANOPY`; quality tiers reduce count/LOD.

**Asset needs:**

- `assets/textures/vegetation/leaf_broadleaf.png` (alpha-tested, 512/1024).
- `assets/textures/vegetation/leaf_pine.png`.
- `assets/textures/vegetation/bark.png` + normal/roughness.
- Optional: `assets/textures/vegetation/leaf_cluster_atlas.png` for cards.

**Tests:**

- `test_canopy_placement.py`: no trees in water/path/arena, slope/clearance
  constraints, count per band.
- `test_canopy_lod.py`: LOD distances monotonic, billboard fade, shadow caster
  count cap.

### 7.6 Flowers, reeds, pampas

- Flowers: tighter clusters, head size 0.06–0.10 m, 3–5 species, warm
  yellow/orange/white accents. Current 14 clusters / 2,175 flowers is a good
  count; make them read as colonies, not scattered dots.
- Reeds: 1.2–1.8 m, along the new shoreline, dense belts in 2–3 coves.
- Pampas: 1.2–1.6 m, on slopes and near rocks, 2–3 drifts; not everywhere.
- Shadows: reeds/pampas can cast at 6 m but must fade; no hard black fans.

### Exit criteria

- Foreground grass coverage ≥ 65% from the hero camera; no bare 1 m gaps.
- Grass reads as soft clumps, not spikes; far grass is a continuous mat, not
  isolated blades.
- No grass in water/path/rock; shoreline grass follows the smooth contour.
- Tree/bush silhouettes fill ≥ 60% of the near-background band; no empty grey
  horizon.
- Canopy + grass frame-time within budget on MED.

---

## 8. Phase 4 — Lighting, shadows, atmosphere (3–5 days)

**Goal:** warm, readable light with soft cool shadows and depth haze; no black
holes, no floating objects.

### 8.1 Sun, ambient, shadow colour

File: `maps/worlds/song_quao/song_quao.c:382-386`.

Current: ambient `{76,104,138}`, sun `{255,218,162}`, intensity 1.85,
direction `{0.46,-0.38,-0.80}` (elevation ~22°), blob-shadow colour
`{32,46,64,98}`.

Target:

- Sun elevation 30–36°: `sunDir.y ≈ -0.50` to `-0.59`; keep the same azimuth.
  This shortens the giant grass shadows while keeping modelling.
- Sun intensity 1.60–1.70. The supplied overview's p99 luma is 0.758, so the
  lit grass is **not** clipping; 1.85 is simply hot relative to the ambient.
  Raise ambient first, then lower the sun and measure.
- Ambient `{96,124,140}`; add a warm ground-bounce term
  `{84,70,48}` via the hemisphere shader or a constant.
- Blob-shadow colour `{46,66,78,150}` (this is `Environment_DrawSmartShadow`,
  not the shadow-map receiver); test in the capture, not by eye.
- The receiver floors already exist: ground 0.56–0.65 (`ground_splat.fs:683-684`),
  rock 0.38 (`prop_lit.fs:149-150`), foliage 0.70
  (`nature_surface.glsl:84-87`). Raise them only if the lower-frame p10 stays
  below 0.10 after the ambient/exposure change. Do not add a second floor.

### 8.2 Shadow map and filtering

Files: `environment/env_shadow.c`, `maps/toolkit/shaders/map_shadow.glsl`.

- Dynamic focus 18 m is good; keep. Static capture radius 156 m is too large
  for anything except coarse rock shadows; reduce to 96 m and keep 1024²
  (≈19 cm texels; LOW uses 512²) or 2048² if memory allows.
- PCF radius is in texels; 1.55 (`ground_splat.fs:6`) is fine. Do not increase
  it to “soften” — that smears thin grass. Soften via the ambient/grade instead.
- Add a **shadow strength fade** for flora: full at 0–3 m, 0 at 5–6 m. This
  helps the mid-distance fans, but near-camera pampas still cast; reduce the
  pampas caster to 1 blade first. Use `WUXING_SONGQUAO_SKIP=cloudshadow` to
  separate cloud shadows from caster shadows before tuning.
- Keep static rock shadows; add terrain only if 5.6 profiling allows.
- On LOW, disable flora real shadows entirely and use a cheap contact
  darkening.

### 8.3 Contact shadows / AO

Two levels:

1. **Cheap (all tiers):** rocks already call `Environment_DrawSmartShadow`
   (`map_props_rocks.inl:783`, `:1052`); first tune opacity/radius and the
   distance fade so they read. Extend the same treatment to trees, large bushes
   and hero grass clumps. Soft ellipse, opacity 0.25–0.40, radius 1.0–1.4× the
   object footprint. This grounds objects immediately.
2. **Optional SSAO/GTAO (HIGH/ULTRA):** new `post_process/ssao` pass:
   - half-res depth + reconstructed normals;
   - 8–12 taps, 4×4 noise, bilateral blur;
   - composite multiply before bloom;
   - 0.6–1.2 ms budget at 1080p; if over, drop to baked AO only.
   - Must work on both GL 3.3 and Vulkan/GLES; fallback to off when depth
     texture is unavailable.

Also bake **rock/tree vertex AO** into the meshes: darken the lower 20–30% and
crevices; this is free at runtime.

### 8.4 Cloud shadows and god rays

- Cloud shadows: strength 0.16 → 0.10–0.12; coverage 0.46 → 0.35; softness
  0.24 → 0.35. The current dark blobs compete with the grass shadows.
- God rays: `VolumetricFog_SetGodRayIntensity(1.35)` → 0.65–0.80. The current
  value washes out the distance.
- Add 6–8 local fog volumes along the river and tree line: warm-white
  `{226,236,232}`, density 0.02–0.05, edge softness 0.8–0.95, slow drift.
- Atmosphere: base density 0.0072 → 0.009–0.011; height falloff 0.36 → 0.24;
  enable a sigmoid layer at y ≈ 2–4 m, thickness 3–4 m, density 0.004–0.006
  for valley mist. Keep the cliff-base mist volumes.

### 8.5 Sky

- `map_sky.glsl` uses `haze` and `ambient`; retune the fog/haze colour to a
  warm horizon `{206, 196, 176}` and cool zenith `{92, 122, 148}`.
- Keep the sun disk; add a subtle warm glow around it.
- No distant cloud bank, no mountain silhouette. A very thin high haze is fine;
  the review explicitly rejects distant clouds as a composition element.

### Exit criteria

- Shadow black ratio < 2% of the lower frame; no pixel cluster darker than
  luma 0.04 that is not a deliberate silhouette.
- No giant grass shadow fans; shadows fade by 5–6 m.
- Objects have visible contact darkening.
- Distance reads cool and hazy; foreground reads warm and saturated.
- God rays/mist do not wash out the midground.

---

## 9. Phase 5 — Rocks, cliff, background (3–5 days)

### 9.1 Rocks

Files: `maps/toolkit/map_props_rocks.inl:28-52` (mesh),
`:692-708` (material), `maps/toolkit/shaders/prop_lit.fs:109-176`.

Current: the granite branch uses `GenMeshSphere` + 9 soft cleavage planes with
smooth-welded normals; only basalt/sandstone/obsidian/slate are flat-faceted.
Albedo is pale and the moss mask requires `albedo.g` in 0.28–0.72.

Target:

1. **Silhouette.** Start from an icosphere (6×8 or 8×12), apply 3–5 large-scale
   noise displacements, clamp/flatten the bottom (embed), add 2–4 fracture
   planes, and split normals on the fracture edges for facets. 3 variants ×
   2 LODs (near 200–800 tris, far 80–150).
2. **Material.** Keep `prop_lit`; tune:
   - moss threshold `smoothstep(0.15, 0.55, albedo.g)` instead of 0.28–0.72;
   - moss colour `{58,86,38}` / lichen `{150,150,120}`;
   - wet base: roughness 0.28, darker albedo within 0.4 m of ground/water;
   - rock tint warm grey, not pale blue.
3. **Placement.** Embed 25–35% below ground; scale 0.6–2.2 m; cluster near the
   river and slopes; 3–5 hero boulders at composition points. Tune the existing
   smart shadow (opacity/radius) rather than adding a second system.
   Regenerate `song_quao_rock_placements.json` if thresholds change.
4. **Debris.** Add small pebbles/twigs around hero boulders; the debris system
   already exists.

### 9.2 Cliff skirt

File: `maps/worlds/song_quao/song_quao.c:374` and
`maps/toolkit/map_props_cliff.inl:590`.

Current: `MAP_SKIP_CLIFF` is forced; the comment says the skirt was “disjointed”.
The implementation already traces the real coast loop and tucks the top edge
under the meadow (`map_props_cliff.inl:8-9`).

Plan:

1. Remove `| MAP_SKIP_CLIFF`; create/draw the cliff (already coded at
   `song_quao.c:515-516`, `:634-635`, `:645-646`).
2. Capture `cliff_promontory`, `cliff_outlet`, `cliff_panorama`
   (`scripts/capture_song_quao_cliffs.sh`).
3. Fix the seam:
   - ensure `sliceHeight` matches the actual rim height;
   - the top ring must sit under the meadow, not float;
   - if the coast loop self-intersects, use the smoothed loop or split chunks
     (the code already has chunking around `map_props_cliff.inl:1109-1228`);
   - add a rim blend: overhanging grass/shrubs/rocks along the top 1–2 m to
     hide the seam.
4. Improve `cliff_rock.fs`: vertical striation, moss on the top 1–2 m, wet dark
   base, AO gradient into the mist, triplanar detail.
5. Mist: keep `MapProp_DrawIslandMist`; add 3–4 fog volumes at the cliff base;
   ensure the cloud sea meets the cliff without a hard line.

If the cliff still looks disjointed after two attempts, fall back to a
**procedural rim rock band** (large embedded boulders + shrubs) that hides the
cut, and keep `MAP_SKIP_CLIFF`. But the cliff is the better solution.

### 9.3 Background layers (no mountains)

The review says: mounds, tree clumps, low forest, thin mist — no distant
mountain ranges, wide horizon, distant clouds or strong DOF.

Implement:

- **Mounds:** do **not** add them to the heightfield. The outer island is
  already uneven, not flat: cells at 40–110 m have a mean upper surface of
  1.6–2.1 m with 3.2–4.8 m standard deviation and peaks of 8–10 m, and upper
  land (`ground_y > 0.5 m`) exists in every 10 m band out to 120 m (1.0–1.8k
  cells per band). What reads as empty is the missing canopy silhouette and the
  drop to the cliff, not a flat heightfield. If one specific azimuth still reads
  flat after Phase 3, add a **background-only** mound mesh or edit the heightfield
  outside the playable basin and regenerate the cliff loop — never regenerate the
  playable heightfield for a background shape.
- **Tree clumps / low forest:** the new canopy system (7.5) places 2–3 dense
  clusters on the far ridge and along the river bends. Constrain placement to
  `ground_y > 0.5 m`, exclude cliff/void cells (the island drops to −8 m at the
  perimeter), and reject the cells below the rim — 33% of the 30–40 m band,
  rising to 71–85% of the 70–110 m band. Use billboards at 60–90 m and desaturate
  toward the fog colour.
- **Mist bands:** 2–3 layers at 30–60 m, density 0.01–0.03, warm-white; they
  separate the midground from the background and hide the terrain LOD transition.
- **Sky:** gradient + sun glow; no mountain silhouettes.
- **Cliff/cloud sea:** the island base; ensure the cliff, mist and cloud sea
  form one continuous base, not three separate props.

### 9.4 Architectural accents (optional, identity)

Add 2–4 small props at composition points, built from existing toolkit
primitives or simple new meshes:

- a weathered stone lantern at the river bend;
- a small wooden footbridge over the narrow channel;
- a simple shrine gate / torii-like frame at the focal clearing;
- a few wooden fence posts along the path.

These are **map content**, not engine features. They should use the existing
`prop_lit` material, warm wood/stone palette, and contact shadows. Keep them
outside the arena centre and out of skill telegraph lanes.

If scope is tight, ship only the stone lantern + bridge; they add the most
identity per triangle.

### Exit criteria

- Boulders read as embedded, faceted, mossy rocks — not pale domes.
- No raw mesh cut at the map edge; cliff/mist/cloud sea transition is
  continuous from all capture angles.
- Near-background has at least 3 distinguishable layers (existing ridge/terrain
  relief, tree clump, mist) and no empty grey horizon.
- No trees/bushes inside the arena centre or blocking the camera.

---

## 10. Phase 6 — Post, camera, final grade (2–3 days)

### 10.1 Post

Files: `main.c:708-752` (global), `main.c:2016-2024` (song_quao override),
`post_process/*`.

| Parameter | Current | Target | Why |
|---|---|---|---|
| Global saturation | 1.55 (song_quao overrides to 1.15) | keep 1.55 globally; map 1.10–1.15 | The override is the value that matters; do not desaturate the meadow to 1.05, measure per layer |
| Map saturation | 1.15 | 1.10–1.15 | Stylised realism, still vivid |
| Contrast | 1.12 / 1.08 | 1.05 / 1.02 | Lift shadows |
| Bloom threshold | 1.25 | 1.15 | Let water glints/grass tips bloom |
| Bloom intensity | 0.12 | 0.15–0.18 | Subtle, not hazy |
| Vignette radius | 0.85 / 1.08 | 1.00 / 1.15 | Less corner crush |
| Vignette softness | 0.45 / 0.35 | 0.50 / 0.45 | Softer edge |
| Exposure | 1.0 | 1.0–1.05 | Keep ACES headroom |
| LUT | 16³ strip, 256×16 (`post_process/color_grade_lut.h:38`) | retune in place | Teal shadows, warm highlights; a 32³ LUT requires changing `COLOR_GRADE_LUT_SIZE` (Post-Process Agent) |

- Retune `assets/maps/song_quao/song_quao_grade.png`: lift blacks +0.02, cool
  shadows toward teal, warm highlights, slight saturation boost in the midtones
  only. The current LUT is a 16³ strip (256×16), neutral on the diagonal but
  lifts blue in shadows; check a neutral grey ramp before regenerating it.
- No DOF. If a far blur is ever added, ULTRA only and very subtle; the review
  explicitly rejects strong DOF.
- FXAA keep. Optional 1.15× supersampling on HIGH/ULTRA.
- SSAO only if 8.3 lands; otherwise baked AO + contact shadows.

### 10.2 Camera

- Add a capture-only `--camera-preset song_quao_hero` (or equivalent) in
  `sandbox/`/`main.c`: FOV 30°, pitch 35°, yaw 35°, distance 22 m, target at
  the river bend. This is a cross-module change and must be requested from the
  Sandbox/Game owner.
- Gameplay camera: keep the current orbit. If the user wants a true isometric
  gameplay camera, treat it as a separate design task (controls, targeting,
  occlusion, skill aiming).
- Add a “hide HUD” flag for captures if not already available (the capture
  scripts already render neutral smoke; check the UI overlay).

### 10.3 Final validation

- Run the full capture matrix at LOW/MED/HIGH.
- Run `scripts/profile_song_quao.sh`; compare to baseline.
- Run all `maps/tests` and the new tests.
- Check the bright-background behaviour of water/grass/rocks (the project’s
  `BRIGHT_BACKGROUND_VFX_SPEC.md` rule applies to effects; for map materials,
  capture a noon/high-key lighting preset and confirm no washout or black
  shadows).
- Walk the arena: no grass in water, no floating rocks, no cliff holes, no
  camera clipping through trees.

---

## 11. Performance budget and quality tiers

### 11.1 Target

- 1920×1080, 60 FPS = 16.6 ms.
- Reference “weak hardware”: integrated GPU (Apple M1 / Intel Iris Xe) or
  Snapdragon 7-series. Measure on the available Mac; extrapolate cautiously.
- **Current baseline (Intel Iris 6000, MoltenVK, 1280×720, capture fixture):
  LOW 69.7 ms, MED 72.7 ms, HIGH 86.9 ms.** At 60 FPS the budget is 16.6 ms, so
  the current frame is 4.2–5.2× over. The visual upgrade cannot be additive:
  every phase must stay within the existing frame time or trade against a
  measured removal. If 60 FPS on this machine is a hard requirement, schedule a
  separate optimization phase **before** Phase 1; do not promise it from the
  visual work alone.
- GPU total < 13 ms; CPU update + draw < 4 ms (overlapped with the GPU). Within
  the GPU total: post < 2 ms, shadow < 1.5 ms, grass < 4 ms, terrain < 2 ms,
  water < 1 ms, canopy < 2 ms.

### 11.2 Budgets

| Resource | Budget |
|---|---|
| Draw calls | < 400 |
| Triangles (main pass) | < 1.4 M |
| Visible grass clumps | < 25k |
| Grass triangles (after LOD) | < 800k |
| Canopy triangles (after LOD) | < 250k |
| Water triangles | < 40k |
| Terrain triangles | ~113k (1 m) / ~452k (0.5 m HIGH) |
| Shadow casters | < 300k triangles |
| VRAM | < 800 MB |
| New textures | < 80 MB |

### 11.3 Quality tiers

| Feature | LOW | MED | HIGH |
|---|---|---|---|
| Terrain mesh | 1 m | 1 m | 1 m (0.5 m optional) |
| Grass draw | 25 m | 35 m | 45 m |
| Grass shadows | off / contact only | 3 m | 4 m |
| Ground cover layer | 20k | 35k | 50k |
| Canopy | far billboards only | near+far | near+far, more instances |
| Water refraction | off | cheap | on |
| Water scene depth | off | on | on |
| SSAO | off | off | optional |
| Dynamic shadow map | 1024² | 1024² | 2048² |
| Static shadow map | 512² | 1024² | 1024² |
| Render scale | 0.85 | 1.0 | 1.0 (1.15 optional) |
| Fog volumes | 3 | 6 | 8 |
| God rays | 0.4 | 0.7 | 0.8 |

Use `GfxQuality_Get()` at init and at draw time. Avoid rebuilding placements on
tier change; instead cull/limit per frame. `GfxQuality_UpdateAuto` already
targets 60 FPS; make sure the map responds to it.

---

## 12. Asset pipeline

### 12.1 New/generated assets

| Asset | Size | Purpose |
|---|---|---|
| `assets/maps/song_quao/song_quao_terrain_ao.png` | 1024² RGBA | curvature AO, wetness, slope, meadow density |
| `assets/maps/song_quao/song_quao_flow.png` | 512² RG | D8 flow direction + speed |
| `assets/maps/song_quao/song_quao_water_edge.png` | 1024² RGBA | signed shore distance, foam, depth |
| `assets/maps/song_quao/song_quao_meadow_density.png` | 512² RGBA | macro density/height/dryness (optional if packed in AO) |
| `assets/maps/song_quao/song_quao_canopy_placements.json` | small | tree/bush placements |
| `assets/textures/vegetation/leaf_broadleaf.png` | 512/1024 | alpha-tested foliage |
| `assets/textures/vegetation/leaf_pine.png` | 512/1024 | alpha-tested foliage |
| `assets/textures/vegetation/bark.png` | 512/1024 | bark diffuse + normal/roughness |
| `assets/maps/song_quao/song_quao_grade.png` | 256×16 (16³) | retuned LUT; 32³ only if `COLOR_GRADE_LUT_SIZE` changes |

### 12.2 Generation commands

The DEM pipeline is `maps/pipeline/dem_pipeline.py`:

```bash
python3 maps/pipeline/dem_pipeline.py <dem_path> \
  --max-dim 300 --height-range 16 --cliff-depth 8 \
  --cliff-margin 8.5 --resolution-scale 1.0 \
  --stream-ratio 0.01 --prefix song_quao \
  --out-dir assets/maps/song_quao
```

Use the same `<dem_path>` and seed as the current manifest
(`assets/maps/song_quao/song_quao_manifest.json`). Regenerate deterministically;
commit the generated `song_quao_*.png`, `song_quao_water.obj`,
`song_quao_*_data.h`, and the manifest together.

### 12.3 Texture rules

- Read `assets/TEXTURE_PACKING.md` before adding any texture. Map materials are
  not VFX sheets, but keep channel usage documented in the manifest.
- Use sRGB for albedo, linear for normal/roughness/AO/flow.
- Keep new textures ≤ 2K; the target is 1K for detail/flow/AO.
- Do not add a 4K texture without a measured reason.

---

## 13. Verification

### 13.1 Captures

`scripts/capture_song_quao_layers.sh` (new):

| Name | Origin | Eye | Purpose |
|---|---|---|---|
| `hero_bank` | TBD in P0.1 — start from the gameplay origin and pull back along the view vector to 22 m | river bend | hero composition, grass/water/rocks |
| `gameplay_center` | `94,0.5,150` | `104,9,162` | actual gameplay |
| `overview_ridge` | TBD in P0.1 — match the supplied `test_song_quao.png` framing | island ridge | map read, background layers |
| `cliff_panorama` | `6,-2,150` | `-16,7,128` | cliff/cloud sea |

Run at 1600×900 and 1920×1080; save logs and frame times. Also run
`scripts/capture_song_quao_cliffs.sh` for the cliff set.

### 13.2 Metrics

`scripts/analyze_song_quao_capture.py` (new) prints:

- lower-frame luma p10, median and p90 (not just a black-pixel ratio — the
  supplied overview already has only 1.24% below 0.05, so that ratio alone
  passes while the frame still looks dark);
- percentage of the lower 2/3 below luma 0.10;
- grass coverage in the foreground mask;
- water edge 90° corner count per 100 m;
- terrain hue variance in the meadow mask;
- horizon fill % in the sky band;
- average saturation/value per depth band.

Targets:

| Metric | Target |
|---|---|
| Lower-frame p10 (overview) | ≥ 0.16 |
| Lower-frame median (overview) | 0.38–0.50 |
| Pixels below 0.10, lower 2/3 | < 4% |
| Lower-frame p10 (close) | ≥ 0.30 |
| Foreground grass coverage | > 65% |
| Water edge 90° corners | < 5 per 100 m |
| Meadow hue std dev | 8–15° |
| Horizon fill | > 60% |
| Near-background saturation | 0.25–0.40 |
| Foreground saturation | 0.55–0.75 |

### 13.3 Tests

Run:

```bash
python3 -m pytest maps/tests -q
```

Existing relevant tests:

- `test_ground_relief.py`, `test_ground_lighting_space.py`,
  `test_ground_binding.py`, `test_heightfield_mesh.py`
- `test_meadow_distribution.py`, `test_meadow_lod.py`,
  `test_meadow_parametric.py`, `test_meadow_world_space.py`
- `test_water_wave_upload.py`
- `test_dem_pipeline.py`
- `test_island_boundary.py`
- `test_map_ecology.py`
- `test_map_background_state.py`
- `test_nature_local_wind.py`
- `test_prop_lighting_space.py`

New tests from §3.3.

### 13.4 Manual checklist

- Walk the full arena perimeter; no cut edges, holes, floating props.
- Look at the river from upstream and downstream; foam and flow agree.
- Look at the cliff from the cloud sea; no backface holes or z-fighting.
- Rotate the camera through 360° at the hero spot; no grass popping, no shadow
  popping, no LOD cracks.
- Check LOW/MED/HIGH; AUTO should stay at 60 FPS.
- Check the HUD does not cover the focal point in captures.

---

## 14. Risks and mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Current baseline is 70–87 ms; 60 FPS is unreachable before any upgrade | High | High | Measure LOW/MED/HIGH first; add a separate optimization phase if 60 FPS on the dev machine is mandatory; every visual phase must trade cost |
| Overview and close capture disagree on exposure | High | Medium | Fixed cameras + fixed exposure in Phase 0; grade per view; never compare a dark overview with a bright close-up |
| Fix misattributed to a missing shadow floor | Medium | Medium | Floors already exist (0.56–0.65 / 0.38 / 0.70); isolate pampas vs cloud shadows with `WUXING_SONGQUAO_SKIP=cloudshadow` before changing either |
| Grass density kills frame time | High | High | Two-layer LOD, cap 25k visible, profile per tier; keep 98k main cap until measured |
| Terrain self-shadow too costly | Medium | Medium | Bake AO first; dynamic only within 18 m; drop if > 1.5 ms |
| Water refraction pulls in VFX | Medium | Medium | Gate to opaque scene depth, clamp offset, disable on LOW |
| Cliff still disjointed | Medium | High | Fix seam with rim blend; fallback to rim rock band + `MAP_SKIP_CLIFF` |
| SSAO unavailable/expensive on Vulkan/GLES | Medium | Medium | Baked AO + contact shadows first; SSAO HIGH only with fallback |
| Tree alpha overdraw | High | Medium | Billboard LOD, cap instances, alpha-test (not blend), shadow caps |
| Regenerating assets moves placements | Medium | Medium | Deterministic seeds, manifest diff, rerun placement tests |
| Post over-saturates | Medium | Low | Measure saturation per layer; LUT + global cap |
| Camera change breaks gameplay | Low (if capture-only) | High | Keep gameplay camera; hero preset only |
| `maps/CLAUDE.md` stale rules conflict | Low | Low | It still says “no real-time shadows / nighttime mood”; song_quao is daytime with real shadows. Update the guide after the direction is approved |

---

## 15. Schedule and exit criteria

| Phase | Estimate | Exit gate |
|---|---|---|
| 0 | 1–2 d | deterministic captures + metrics + baseline perf |
| 1 | 3–5 d | meadow/rock read fixed; no black terrain; tests pass |
| 2 | 4–6 d | smooth shoreline; clear/jade water; tests pass |
| 3 | 5–8 d | dense meadow; canopy fills background; perf OK |
| 4 | 3–5 d | soft shadows; contact AO; fog depth; no black |
| 5 | 3–5 d | embedded rocks; cliff closed; 3 background layers |
| 6 | 2–3 d | grade/camera final; full validation |

Total: **~4–5 weeks** for one agent, assuming assets are generated procedurally
and no external modeling is required. Add **2–4 weeks** for a separate
optimization phase if 60 FPS on the current dev machine is mandatory; the
current LOW baseline is already 4.2× over the 16.6 ms budget.

### Definition of done

- [ ] `hero_bank`, `gameplay_center`, `overview_ridge`, `cliff_panorama` all
      render without the defects listed in §1.1.
- [ ] Shadow black ratio < 2%; no giant grass shadow fans.
- [ ] Water shoreline smooth; water reads clear at the edge and jade in the
      channel; foam and caustics visible.
- [ ] Meadow coverage > 65% foreground; no bare 1 m gaps; no grass in water.
- [ ] Boulders embedded, faceted, mossy, with contact shadow.
- [ ] Cliff closes the map edge; mist/cloud sea transition continuous.
- [ ] Near-background has tree clumps/low forest/mist; no empty grey horizon.
- [ ] 60 FPS at 1080p on the target hardware at MED; **or** a documented
      target-hardware change plus a measured optimization phase. The current dev
      machine (Intel Iris 6000) is 4–5× over budget at 720p, so this cannot be
      certified by the visual work alone.
- [ ] All existing + new tests pass.
- [ ] No changes to forbidden directories; no edits outside module ownership
      without the owner’s approval.
- [ ] API changes documented in `map_props.h` and `maps/docs/API.md`; durable
      traps recorded in `maps/docs/LANDMINES.md`.

---

## 16. Open decisions

1. **60 FPS target:** the current dev machine (Intel Iris 6000) runs LOW at
   69.7 ms / 720p, 4.2× over budget. Is the target the dev machine, a newer
   integrated GPU, or a phone? If it is the dev machine, schedule the
   optimization phase before Phase 1 and treat every visual addition as a trade.
2. **Acceptance view:** the supplied overview is dark (median 0.324) and the
   current close capture is bright (median 0.509). Which one is the hero frame?
   The plan keeps both, but the grade must be tuned per view.
3. **Camera:** capture-only hero preset (recommended) vs true isometric gameplay.
   The latter is a cross-module design change.
4. **Terrain resolution:** keep 1 m + detail normal (recommended) vs 0.5 m
   heightfield on HIGH. Decide after Phase 1 captures.
5. **SSAO:** baked AO + contact shadows only (recommended first) vs full GTAO.
   Decide after Phase 4 profiling.
6. **Trees:** procedural low-poly (recommended for weak hardware and no external
   pipeline) vs authored glTF assets.
7. **Architecture:** stone lantern + footbridge only (recommended) vs a full
   shrine set.
8. **Map layout:** keep the existing terrain/river layout (recommended; the
   review says learn from the image’s treatment, not its layout) vs re-staging
   the river/hills. Re-staging would invalidate the DEM pipeline and placements.

---

## Appendix A — File/line map

| Area | File:line |
|---|---|
| Skip mask / cliff off | `maps/worlds/song_quao/song_quao.c:374` |
| Static shadow capture | `maps/worlds/song_quao/song_quao.c:355-368` |
| Dynamic shadow casters | `maps/worlds/song_quao/song_quao.c:345-353` |
| Sun/ambient/shadow | `maps/worlds/song_quao/song_quao.c:382-386` |
| Atmosphere/cloud shadows | `maps/worlds/song_quao/song_quao.c:388-416` |
| Ground creation | `maps/worlds/song_quao/song_quao.c:439-444` |
| Ground material maps | `maps/worlds/song_quao/song_quao.c:494-503` |
| Flora style | `maps/worlds/song_quao/song_quao.c:530-553` |
| Water config | `maps/worlds/song_quao/song_quao.c:557-579` |
| Grass density function | `maps/worlds/song_quao/song_quao.c:87-164` |
| Meadow placement generator | `maps/toolkit/map_props_meadow.inl:121-192` |
| Meadow LOD/shadow casters | `maps/toolkit/map_props_meadow.inl:1030-1090`, `:1617-1685` |
| Flora placement | `maps/toolkit/map_props_flora.inl:334-453` |
| Rock mesh | `maps/toolkit/map_props_rocks.inl:28-52` |
| Rock material | `maps/toolkit/map_props_rocks.inl:692-708` |
| Cliff creation | `maps/toolkit/map_props_cliff.inl:590-598` |
| Ground shader cliff blend | `maps/toolkit/shaders/ground_splat.fs:221-223`, `:482` |
| Ground shadow floor | `maps/toolkit/shaders/ground_splat.fs:683-688` |
| Ground wet band | `maps/toolkit/shaders/ground_splat.fs:520-566` |
| Rock shadow floor | `maps/toolkit/shaders/prop_lit.fs:146-150` |
| Foliage shadow floor | `maps/toolkit/shaders/nature_surface.glsl:84-87` |
| Flora shadow casters | `maps/toolkit/map_props_flora.inl:826-846` |
| Rock flat/smooth normals | `maps/toolkit/map_props_rocks.inl:409-412` |
| Water depth ramp | `maps/toolkit/shaders/water_surface.fs:485` |
| Water foam | `maps/toolkit/shaders/water_surface.fs:436-450` |
| Water depth branch | `maps/toolkit/shaders/water_surface.fs:295-324` |
| Depth snapshot (previous frame) | `main.c:1923` |
| Water scene depth gate | `maps/toolkit/map_props_water.inl:1270-1272` |
| Rock moss/AO | `maps/toolkit/shaders/prop_lit.fs:109-176` |
| Grass shading | `maps/toolkit/shaders/nature_surface.glsl:246-451` |
| Water mesh generator | `maps/pipeline/dem_pipeline.py:257-326` |
| Rock slope threshold | `maps/pipeline/dem_pipeline.py:562-566` |
| Shadow resolution/extent | `environment/env_shadow.c:149-156`, `:409-441` |
| Post config | `main.c:708-752`, `main.c:2016-2024` |

## Appendix B — Capture commands

```bash
# Existing cliff/cloud set
bash scripts/capture_song_quao_cliffs.sh /tmp/song_quao_cliffs

# New hero/gameplay/overview set (after P0.1)
bash scripts/capture_song_quao_layers.sh /tmp/song_quao_layers

# Performance baseline
bash scripts/profile_song_quao.sh

# Per-tier baseline (headless capture freezes the tier; AUTO is only in normal play)
for q in low med high; do
  env WUXING_MAP=song_quao WUXING_GFX_QUALITY=$q ./build/wuxing \
    --benchmark-visible --profile-start-frame 30 --warmup 30 \
    --render-neutral-smoke --origin 94,0.5,150 --eye 104,9,162 \
    --out /tmp/sq_$q.png 2>&1 | grep BENCHMARK_FRAMETIME
done

# Isolate cloud shadows from caster shadows
env WUXING_MAP=song_quao WUXING_SONGQUAO_SKIP=cloudshadow ./build/wuxing \
  --benchmark-visible --profile-start-frame 40 --warmup 60 \
  --render-neutral-smoke --origin 94,0.5,150 --eye 104,9,162 \
  --out /tmp/sq_nocloud.png
```

The capture scripts already support `--origin`/`--eye`, `--warmup`,
`--profile-start-frame`, `--render-neutral-smoke`, and `--out`.
Use `WUXING_MAP=song_quao` and `WUXING_SONGQUAO_SKIP` (names such as `ground`,
`flora`, `rocks`, `water`, `sky,cloudsea`, `shadowcasters`, `cloudshadow`) for
subtractive profiling. `WUXING_GFX_QUALITY` accepts `low|med|high|ultra|auto`.

## Appendix C — Suggested first commit (Phase 0 + quick wins)

Keep the first change small and reviewable:

1. Add `u_debugMode` to `ground_splat.fs` / `water_surface.fs` /
   `nature_surface.glsl` (capture-only).
2. Add `scripts/capture_song_quao_layers.sh` and
   `scripts/analyze_song_quao_capture.py`.
3. Raise the rock slope threshold in `dem_pipeline.py` and regenerate the splat.
4. Verify the existing shadow floors and lift ambient/exposure in
   `ground_splat.fs` / `prop_lit.fs` / the environment config; do not add a
   second floor.
5. Reduce flora `shadowDistance` to 4 m and add the distance fade.
6. Remove `MAP_SKIP_CLIFF` and capture the cliff set.
7. Set water `maxDepth = 0.5f` and enable scene depth for terrain water.

Capture before/after for all four cameras, run the tests, and only then start
the water-mesh and canopy work.
