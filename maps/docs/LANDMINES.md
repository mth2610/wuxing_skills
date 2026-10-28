# maps — Landmines

> Distilled, reusable lessons for the **maps** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting engine traps live in root `ENGINE_LANDMINES.md`. Backlog/log is in `PROGRESS.md`.

### Lake depth contours shift when the camera moves

- **Symptom:** A shore-shaped band appears to expand across the lake during zoom or a jump, and the lake bed has a hard material break at the bank.
- **Cause:** The radial water shader mixed screen-space ray depth with an analytic fallback that did not match the concave bed mesh. Ray depth varies with camera pitch. The bed also used a paved-stone texture while the surrounding shore used earth colors.
- **Rule:** For a radial lake with a known bed profile, calculate optical depth from the same radial equation as its mesh and fade surface coverage by shoreline coordinate. Do not request a scene-depth snapshot for that lake. Use soil/sand bed colors and the surrounding terrain texture family; inspect matched-camera captures at high and low camera heights.

### Vegetation shadows exist in the map but disappear from the viewed meadow

- **Symptom:** The character has an obvious directional shadow while HIGH-quality grass and flowers look ungrounded.
- **Cause:** `maps/toolkit/map_props_nature.inl` culled grass casters around `camera.position` although `environment/env_shadow.c` centers the dynamic shadow box on the gameplay focus. It also reduced grass to one shadow blade per clump and drew the flower far model in the depth pass. The resulting map held vegetation depth, but too little of it reached the visible ground.
- **Rule:** Cull real casters around `EnvShadow_GetFocus()` with chunk bounds, keep a three-blade shadow LOD for grass, and use the flower near model for depth. Check both occupied shadow texels and shadowed receiver samples with `WUXING_SHADOW_DYNAMIC_VERIFY=1`; compare `WUXING_NATURE_SHADOW_MODE=real WUXING_NATURE_SHADOW_CASTERS=all` with `...CASTERS=none` at the same camera. On MED/LOW, real vegetation casters remain disabled by the quality policy in `maps/toolkit/map_props_nature.inl`.

### Pointed grass tips accumulate single-sample aliasing

- **Symptom:** Individual grass tips show tiny bright stair steps that become visible as shimmer across a dense meadow.
- **Cause:** `maps/toolkit/map_props_nature.inl` gave the pointed triangle the final third of each near blade, while its one-segment far LOD made the entire leaf one long, narrow triangle. The default single-sample scene target gives subpixel tips binary coverage; FXAA cannot reconstruct the missing samples.
- **Rule:** Reserve a short final curve span for near pointed tips, darken the tip vertex, and make one-segment far blades shorter and wider. Keep blade specular and transmission restrained so a narrow edge does not become a yellow line. In `maps/toolkit/shaders/nature_opaque.fs`, attenuate the final pixel and blades whose projected UV width is subpixel; keep the geometry opaque. Compare fixed close and distant captures; `WUXING_MSAA=4` is a measured quality option with a bandwidth cost (see `ENGINE_LANDMINES.md` #19).

### Grass orbit distance can defeat mesh LOD

- **Symptom:** Default gameplay zoom submits detailed blades that are already only a few pixels wide, wasting geometry and increasing highlight noise.
- **Cause:** Adding the camera-to-target horizontal orbit radius to both LOD thresholds compensates away the distance introduced by zooming out; ignoring camera height compounds it. Applying transverse rounding again in the fragment shader exaggerates normals already rounded by the mesh generator.
- **Rule:** Select mesh detail using full 3D camera distance and FOV scaling; retain orbit compensation only for draw range. Preserve blade identities when reducing each clump, and filter highlights by projected blade width and normal variance. Keep rounded normals in one stage. Opaque contrast filtering mitigates shimmer but does not reconstruct geometric coverage or provide temporal antialiasing.

### Whole-chunk grass LOD exposes rectangular boundaries

- **Symptom:** Wide camera views reveal straight borders between dense detailed grass and shorter, wider far blades.
- **Cause:** A single detail level selected from the chunk center changes every tuft at the same boundary. Staggering chunk thresholds changes when the rectangle appears, not its shape.
- **Rule:** In `maps/toolkit/shaders/nature_parametric.glsl`, select visible detail per root with an immutable spatial rank distributed across distance transition bands. In `maps/toolkit/map_props_nature.inl`, submit every template intersecting the chunk sphere; the shader selects exactly one per tuft. Hash authoring coordinates before camera transforms to avoid rank changes from floating-point rounding. Keep shadow selection separate.

### Wide cameras expose the moving shadow-map boundary

- **Symptom:** Grass shading changes abruptly along straight lines even when visible tufts no longer select LOD by chunk.
- **Cause:** The dynamic directional shadow map covers a finite moving box. Its previous 3.5% UV edge fade was too narrow to hide the transition across dense grass shadows at wide zoom.
- **Rule:** In `maps/toolkit/shaders/map_shadow.glsl`, fade dynamic coverage across the outer 12% of UV extent while retaining full interior contrast and the static map's separate edge fade. Check the ground and foliage receivers at the same wide gameplay camera; mesh LOD changes cannot repair a shadow coverage edge.

### Smaller grass submissions become slower when IDs upload mid-scene

- **Symptom:** Compact tuft lists reduce submitted vertices, but frame time increases and the scene gains two additional full-resolution depth copies.
- **Cause:** `UpdateTexture` inside `MapProp_DrawMeadow` splits the active scene render pass. The Vulkan backend preserves the scene's readable depth attachment when reopening it; uploading once for each meadow adds extra copies.
- **Rule:** Call `MapProp_PrepareMeadow` after camera finalization and before framebuffer captures or scene drawing. Reuse prepared IDs when the view is unchanged. `MapProp_DrawMeadow` must never prepare or upload: it checks `NatureParametric_IsPrepared` and falls back to GPU rejection for that frame if a camera change arrived after the update (the fallback used to upload mid-pass, which is what split the pass in the first place). Compare renderer counters as well as visible frame timing. The production test checks preparation reuse and camera/resize invalidation.

### Instanced foliage cost tracks submitted instances, not draws or pixels

- **Symptom:** A dense meadow pass costs ~4 ms at 1280x720 and ~4 ms at 640x360, so it looks fill-bound; merging 41 chunk draws into 3 band draws saves ~0.1 ms, so it also looks draw-bound. Neither conclusion is true.
- **Cause:** The pass cost is per submitted instance x per-vertex blade-atlas gathers in `shaders/nature_parametric.glsl`. Halving instances (compact ids: 26 689 -> 13 333) saved ~0.9 ms; clamping instances to one per band dropped the whole grass contribution to ~0 where draw count and every uniform binding stayed identical. Screen resolution changes none of that because blades cover few pixels each.
- **Rule:** Attribute foliage cost by clamping `instanceCount` (and by dropping one flora part at a time), never by counting draws or by scaling resolution alone. Before claiming a win, re-measure with the same clamp so the instance-proportional share is visible; `MEADOW_SUBMISSION` trace reports submitted instances and vertices. Keep `WUXING_MEADOW_SUBMISSION=legacy` as the A/B control. The caster pass is part of the same budget (~1.1 ms of 4.0 ms in song_quao), so trims that apply to both passes win twice.

### A player-centred vegetation field drops remote wind impacts

- **Symptom:** Guided Particle visibly impacts grass or flowers, but the plants do not bend.
- **Cause:** Two silent coordinate/wiring errors compounded. On rlvk, `SetShaderValue(shader, ...)` still writes to the currently active shader, but Nature uploaded wind uniforms before `DrawModel` activated the vegetation program. Then `nature_lit.vs` called `matModel * vertexPosition` world space even though this engine's `matModel` includes the view transform; comparing that view-space point with a world-space impact centre made radial attenuation zero. Separately, a player-centred 18 m receiver can reject a remote impact, and the default arena position may contain only grass-coloured terrain rather than vegetation geometry.
- **Rule:** Keep `BeginShaderMode(vegetationShader) -> wind uniform upload -> vegetation draws -> EndShaderMode()` as one scope for both visible and dynamic-shadow passes. Recover a true world position with the inverse current transform before interaction UV or impact-distance math. A uniform direction is valid for Linear Gust only; Radial Blast derives an outward direction per plant, while Vortex and Turbulence stay in the spatial field instead of being collapsed to one patch-wide sample. Diagnose with `WUXING_WIND_RECEIVER_TRACE=1`: require `guided_cast`, one `guided_arrival`, `vegetation_receiver`, then `vegetation_shader ... uniforms=ok`. A missing stage identifies the broken boundary. Source-wiring assertions alone cannot prove active-shader state or coordinate-space agreement.

### A force strike cannot be represented by amplifying airflow

- **Symptom:** A guide force grabs free leaves, but grass only leans smoothly in one direction and returns without a strike or rebound.
- **Cause:** The receiver reads the guide airflow approximation, so displacement follows current wind rather than integrating Newton force against plant inertia and flexure.
- **Rule:** Keep ambient Wind response intact; separately query anchored Motion forces into a mass/spring/damper tip state. Scroll both displacement and velocity with the same world-cell mapping as the interaction texture, preserve the root mask, and share that texture between visible and shadow passes. Guard: `maps/tests/test_nature_local_wind.py` exercises force magnitude, opposite release response and scrolling.

### Sampling the source plane erases horizontal-axis swirl

- **Symptom:** A travelling guide curls free particles but its swirling airflow barely bends grass beneath it.
- **Cause:** The interaction receiver rasterized at the source centre height. A horizontal-axis vortex on that plane produces mainly vertical airflow, then the vegetation receiver discards Y when computing ground-plane bend.
- **Rule:** Query local map terrain plus representative canopy height for each affected receiver texel. Keep the source geometry in world space; never move the receiver plane to the airborne guide head. Guard: `maps/tests/test_nature_local_wind.py` exercises horizontal-axis swirl below the source.

### Parametric vegetation has a different world-position uniform

- **Symptom:** Wind traces report `uniforms=MISSING` for working parametric grass in both visible and shadow passes.
- **Cause:** The trace required `u_worldFromShaderSpace`, which belongs to expanded meshes. Parametric blades already recover world position through `u_worldOffset`.
- **Rule:** Validate the coordinate contract actually used by the shader: inverse transform or explicit world offset, together with every impact uniform. Log individual locations to distinguish missing impact inputs from an unused coordinate conversion. Guard: `maps/tests/test_nature_local_wind.py`.

### Turbulence can hide the authored blast

- **Symptom:** Grass moves in varied directions after impact, but the result still reads as a Perlin wind wave rather than a blast.
- **Cause:** A filled radial falloff bends the entire affected disc simultaneously, while a stronger and much longer Turbulence source visually dominates the short Radial Blast.
- **Rule:** Render a Radial Blast as an age-driven expanding annulus and give it a short uncontested opening beat. A stronger, longer-lived Turbulence wake may become the primary motion after that beat, but ramp it in rather than applying full strength on the spawn frame. CPU and GPU arrival paths must author identical radius, strength, lifetime, and attack behavior; test both the authored budgets and that wavefront response peaks at the moving front rather than at the centre.

### Independent vegetation sway desynchronizes the wind field

- **Symptom:** Grass has attractive waves, but its motion does not line up with smoke, particles, or changes to Global Wind.
- **Cause:** The vertex shader synthesizes standalone sine bands from time and a normalized direction while Core Wind uses a world-space, advected hash-gradient velocity field.
- **Rule:** Wind owns the forcing: mirror the Core macro field in vegetation and superpose local Vorticles. Vegetation owns only its response—compliance, lag, wind-energy-driven flutter, bend limit, and root mask. Give grass and flowers different response profiles, but never give either an independent motion source. Visible and shadow passes must call the same deformation functions.

### Foliage shadow filtering can dominate a dense meadow

- **Symptom:** A small foliage PCF kernel makes close leaves less jagged but slows a full meadow substantially.
- **Cause:** Each overlapped foliage pixel samples both shadow cascades; multiplying that work across the canopy overwhelms a bandwidth-limited GPU. Linear interpolation of stored depth followed by one comparison also does not equal filtered visibility.
- **Rule:** Interpolate comparison results for close HIGH-quality foliage, fade the extra kernel out from 8 to 10 m, and retain single comparisons at gameplay distance and lower tiers. Share the same cached Core wind between visible grass and casters; evaluate the cache against off-grid Core samples and retain the analytic path for tall/high-compliance plants. Compare full-map frame time at a matched camera; a cheaper wind update alone does not establish a frame-rate gain.

### Indexed foliage must preserve attribute seams and chunk budgets

- **Symptom:** Vertex packing changes blade shading, or smaller chunks reduce memory but slow the full map.
- **Cause:** Position-only deduplication merges authored normal/UV/color seams; shrinking chunks increases draw and LOD overhead.
- **Rule:** Compare all authored attribute bytes, preserve triangle order, and fall back unchanged when the 16-bit range or allocation budget fails. Verify expanded indices against the original streams, including allocation failures. Measure chunk changes independently; vertex savings do not establish a frame-time gain.

### Bright ambient fill washes out captured grass shadows

- **Symptom:** Valid grass silhouettes cast onto the soil, but roots and clumps still appear detached.
- **Cause:** Broad receiver filtering reduces thin-shadow coverage, while nearly unattenuated ambient fill hides the remaining contrast.
- **Rule:** Inspect the receiver before adding casters. Ground uses a 0.65-texel dynamic PCF radius, an ambient visibility floor of 0.70 on soil and 0.48 on turf, and a turf-only 1.45 visibility exponent; grass blades use a 0.78 ambient floor under captured occlusion. Keep unshadowed lighting unchanged. Compare identical-frame grass-caster on/off captures to distinguish missing depth geometry from weak receiver contrast before adjusting the material.

### Stable interaction or wake texels still trigger texture-upload stalls

- **Symptom:** A stationary meadow or settled lake pays texture-upload synchronization every frame even after its interaction or wake texture stops changing.
- **Cause:** `maps/toolkit/map_props_nature.inl` uploaded entire quantized fields unconditionally: the 64×64 interaction field and the 128×128 water wake field. Advancing CPU simulation does not imply those output bytes changed; the water upload also occurred after grass scene draws.
- **Rule:** Continue evaluating interactions, wind and wakes each frame, but upload only when a full byte comparison detects a change. Record the initial loaded bytes and invalidate the cache on texture recreation or destruction; camera-region uniforms must still advance independently. Never infer settled output from an absent interactor while residual waves can still change the texture.

### Compact meadow packing drops boundary roots

- **Symptom:** Atlas draw counts include roots whose descriptors were never written.
- **Cause:** Reconstructing chunk bounds from a rounded center changes the minimum by a few float ULPs, so count and packing passes disagree.
- **Rule:** Store the exact bounds used by counting, reuse them during packing, and size atlases from the summed selected counts. `test_meadow_parametric.py` checks boundary roots and allocation rollback. Raw instancing binding rules are in `ENGINE_LANDMINES.md`.

### Prop haze and lighting change with camera coordinates

- **Symptom:** A nearby prop receives distant material haze, and its directional shading or contact AO changes when the camera rotates or translates.
- **Cause:** `maps/toolkit/prop_lit.c` uploaded world-space sun/camera values against shader/view-space varyings. `maps/toolkit/shaders/prop_lit.fs` also treated view-space Y as ground height. This is the coordinate-space trap in `ENGINE_LANDMINES.md` §9.
- **Rule:** Upload direction and point values in the varyings' space during the active camera scope, omit translation for directions, and recover world height separately. Keep shadow and VFX lighting in their established space; verify physical distance, diffuse response and ground AO under translated/rotated cameras with `maps/tests/test_prop_lighting_space.py`.

### Parametric foliage repeats inverse-camera work per vertex

- **Symptom:** Translation-only tuft draws transform authoring positions and normals into camera space, then immediately recover their original world space in every visible and shadow vertex.
- **Cause:** The parametric renderer inherited the general mesh path's model/inverse-camera pair, although its only model transform is the caller's world offset.
- **Rule:** Upload the exact translation once per draw and keep parametric world positions as `local + u_worldOffset`; preserve MVP separately. Require the offset uniform in both pass fallback checks. Rotation or scale needs an explicit position and normal transform before extending this path. `maps/tests/test_meadow_world_space.py` checks translated/rotated cameras, nonzero offsets, shader expressions and production uploads; matched captures and timings still decide visual equivalence and performance.

### Ungated splat scree and oversized placement bounds thin foreground grass

- **Symptom:** A gentle grassy hillside shows a bare contour strip at close zoom, or expanding meadow bounds makes the foreground turf look sparse even at a tight `0.28 m` grid spacing.
- **Cause:** `ground_splat.fs` gates `splat.g` (cliff/scree) by actual geometric slope (`1.0 - geomNormal.y`), so gentle contours with high `splat.g` still render as green grass, whereas CPU `SongQuaoGrassDensity` rejected tufts on raw `splat.g` alone. Separately, when candidate tufts inside `minBounds..maxBounds` exceed `maxCount`, reservoir sampling in `MapProp_GenerateMeadowPlacements` randomly evicts foreground tufts to store distant tufts beyond `drawDistance`.
- **Rule:** Gate CPU scree rejection by `1.0 - normal.y` using the same slope window as `ground_splat.fs`, and size `minBounds..maxBounds` / `spacing` so accepted candidates stay at or below `maxCount` (zero reservoir thinning in the playable basin).

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-25 | Antigravity | Slope-gated scree rejection, reservoir-thinning guard, and focal-coupled LOD scaling | `maps/worlds/song_quao/song_quao.c`, `maps/toolkit/map_props_meadow_parametric.inl`, `maps/toolkit/shaders/ground_splat.fs`, `maps/toolkit/shaders/nature_surface.glsl` | Ground-truth |
| 2026-10-22 | DeepSeek | Ground shader cost model, branch guards, whole-draw frame marginal | `maps/toolkit/shaders/ground_splat.fs` repeat-harness ablations; song_quao paired full-frame runs (base vs `WUXING_SONGQUAO_SKIP=ground`, 3 pairs) | Ground-truth |
| 2026-10-22 | DeepSeek | Compact-by-default band batching and depth-only caster shader | `maps/toolkit/map_props_meadow.inl`, `maps/toolkit/map_props_meadow_parametric.inl`, `maps/toolkit/shaders/nature_parametric.glsl`, `maps/toolkit/shaders/nature_shadow_parametric.vs`; interleaved cold 170-frame runs (song_quao 1280x720, legacy vs compact) | Ground-truth |
| 2026-10-12 | Codex | DEM shoreline correspondence and foliage budget coverage | maps/pipeline/dem_pipeline.py; maps/toolkit/map_props_nature.inl; matched captures and focused tests | Ground-truth |
| 2026-10-09 | Codex | Interior relief threshold and shared meadow palette | maps/tests/test_verdant_heightmap.py; maps/toolkit/meadow_palette.h; maps/toolkit/shaders/ground_splat.fs | Ground-truth |
| 2026-10-04 | Codex | Heightmap normalization and background/water shader scopes | `scripts/generate_island_heightmap.py`, `maps/toolkit/map_props_cloud.inl`, `maps/toolkit/map_props_nature.inl`, `maps/tests/test_map_background_state.py` | Ground-truth |
| 2026-10-03 | Codex | Translation-only parametric transform cancellation | `maps/toolkit/map_props_meadow_parametric.inl`, `maps/toolkit/shaders/nature_parametric.glsl`, `maps/tests/test_meadow_world_space.py` | Ground-truth |
| 2026-10-03 | Codex | Prop material camera-space lighting and world-height AO | `maps/toolkit/prop_lit.c`, `maps/toolkit/shaders/prop_lit.fs`, `maps/tests/test_prop_lighting_space.py` | Ground-truth |
| 2026-10-03 | Codex | Exact water wake texture reuse and residual-wave lifecycle | `maps/toolkit/map_props_nature.inl`, `maps/tests/test_water_wave_upload.py` | Ground-truth |
| 2026-10-02 | Codex | Grass shadow contrast after substrate correction | `maps/toolkit/shaders/ground_splat.fs`, matched grass-caster on/off captures | Ground-truth |
| 2026-10-02 | Codex | Compact meadow boundary packing | `maps/toolkit/map_props_meadow_parametric.inl`, `maps/tests/test_meadow_parametric.py` | Ground-truth |
| 2026-10-01 | Codex | Exact interaction texture reuse | `maps/toolkit/map_props_nature.inl` | Ground-truth |
| 2026-10-01 | Codex | Grass shadow receiver contrast | `maps/toolkit/shaders/ground_splat.fs`, `maps/toolkit/shaders/nature_surface.glsl`, `maps/toolkit/shaders/map_shadow.glsl` | Ground-truth |
| 2026-10-01 | Codex | Indexed foliage geometry and chunk budget | `maps/toolkit/map_props_mesh_index.inl`, `maps/tests/meadow_mesh_index_test.inl` | Ground-truth |
| 2026-09-26 | Codex | Vegetation shadow and grass-tip landmines | `maps/toolkit/map_props_nature.inl`, `maps/toolkit/shaders/nature_opaque.fs`, `environment/env_shadow.c` | Ground-truth |
| 2026-09-26 | Codex | Lake depth contour and bed material landmine | `maps/toolkit/shaders/water_surface.fs`, `maps/toolkit/shaders/water_bed.fs`, `maps/toolkit/map_props_nature.inl` | Ground-truth |
| 2026-09-27 | Codex | Grass tip contrast and blade silhouette follow-up | `maps/toolkit/map_props_nature.inl`, `maps/toolkit/shaders/nature_surface.glsl`, `maps/worlds/verdant_path/verdant_path.c` | Ground-truth |
| 2026-09-30 | Codex | Grass LOD and shading filtering | `maps/toolkit/map_props_nature.inl`, `maps/toolkit/shaders/nature_surface.glsl` | Ground-truth |
| 2026-09-30 | Codex | Close foliage PCF and sampled Core wind | `maps/toolkit/map_props_nature.inl`, `maps/toolkit/shaders/nature_surface.glsl`, `maps/toolkit/shaders/nature_wind_field.glsl` | Ground-truth |

### Heightmap normalization must match runtime metres

**Symptom:** a nominal 1.15 m lake basin rendered about 2.54 m deep, disagreeing with water bathymetry. **Cause:** the generator normalized against 3.6 m while the map mesh used an 8 m vertical range. **Rule:** bake with the runtime cliff range; verify decoded basin depth, and preserve existing meadow relief when changing normalization.

### Cloud and water uniforms need the active draw shader

**Symptom:** a preceding prop shader could receive background/water uniform uploads; multiple cloud surfaces shared the last creation-time tiling. **Cause:** draw uniforms were uploaded before activating their shader, and tiling was stored only in shared shader state. **Rule:** activate the shader before uploads and drawing; keep cloud tiling on the surface and upload per draw. `maps/tests/test_map_background_state.py` executes the production paths after a sky draw and checks shader/state ownership.

### Floating-plain boundary: geometry and sampler setup

**Symptom:** a cloud perimeter resembles a hard ridge, exposes a straight terrain seam, or produces grain after changing sampler behavior.

**Cause:** a heightmap plateau is inset from its rectangle; a cloud bank with an opaque crest exposes its mesh outline; `matModel` contains the view matrix in this engine, so cloud world coordinates must use an explicit translation; anisotropy may be unavailable and does not itself enable mip selection.

**Rule:** bake mist from actual terrain contours instead of a raised rounded rectangle, use shared sky radiance with explicit world translation, and configure once at initialization. The mist replacement preserves its borrowed texture; allocation failure retains the original model. Use `ResourceManager_LoadTextureVariant` for isolated texture settings and establish trilinear filtering before requesting anisotropy. Do not alter meadow density to conceal a boundary problem.

### Mist sprites must not cut through the plateau

**Symptom:** soft mist shows a hard horizontal cutoff despite a transparent texture border.

**Cause:** a camera-facing billboard intersects opaque ground; the depth test cuts its silhouette before alpha can soften it.

**Rule:** use world-aligned rim quads slightly above the plateau in the transparent pass, with a baked soft mask and no depth writes. Flush both sides of depth-state changes. Opaque cloud sheets write their actual surface depth to cover submerged cliffs; the transparent ribbon does not. Draw sky before the cloud sheet. Do not interpolate terrain depth toward far depth to hide a seam: it creates false surfaces for fog reconstruction.

### Contour mist: general shapes without runtime boundary work

**Symptom:** four-sided rim placement fits one map but creates white walls, missing patches, or displaced mist on curved/concave maps.

**Cause:** a map rectangle describes a bound, not the actual plateau outline. Raised cloud geometry duplicates the terrain border and exposes a finite surface silhouette.

**Rule:** keep the sea flat. Bake closed mist contours from actual indexed/unindexed terrain at initialization, joining duplicated endpoints with canonical low-to-high interpolation. Exclude clockwise depression contours so lakes do not become island rims; explicit contour input can represent holes. Simplify only mist geometry, retain collision/terrain, and draw one cached textured ribbon. Quantization limits and non-manifold slices return failure without replacing existing resources.


### Perimeter mist must stay below interior terrain relief

- **Symptom:** contour mist crosses meadow swales after a terrain update.
- **Cause:** the contour extraction height intersects interior depressions instead of only the island cliff.
- **Rule:** choose a contour below the deepest interior lake/swale and above the cloud sea, and verify below-threshold heightmap cells connect only to the perimeter. Verdant uses -1.25 m; `maps/tests/test_verdant_heightmap.py` checks the baked terrain topology.

### Meadow colors must agree across geometry and distant ground

- **Symptom:** green blades disappear into a differently colored distant carpet or form unrelated golden stripes.
- **Cause:** independent habitat noise and palettes in the blade bake and ground shader.
- **Rule:** derive grass and distant ground color from the same ecology habitat and synchronize `meadow_palette.h` with `shaders/meadow_palette.glsl`; retain ecology coverage when replacing distant blades with ground shading.

### DEM water and foliage must share the rendered terrain

- **Symptom:** a stream has hard cyan sides, a lake is polygonally clipped, and its banks remain a large bare ellipse.
- **Cause:** spline UV width and length were transposed; radial bathymetry, separately sampled water levels and elliptical foliage exclusion described different surfaces. Eight-bit terrain also stepped by about 9 cm over a 24 m range.
- **Rule:** strip UV.y is width. For DEM maps, bake terrain and water from the same triangles and quantized heights; carry physical depth in water vertices and query the companion raster. Use packed 16-bit terrain for centimetre-scale banks. A lowland plateau is only a lake candidate; never call inferred bathymetry or substrate a survey.

### A foliage budget must cover the whole map

- **Symptom:** the southern portion of a dense map loses vegetation at a straight row boundary.
- **Cause:** the placement scan stopped as soon as its capacity filled.
- **Rule:** scan eligible habitat and use deterministic reservoir selection with a separate RNG. A storage budget must not become a geographic cutoff.

### White rectangular cards appear across shoreline reeds

- **Symptom:** Ugly opaque white rectangular cards or paper billboards appear scattered across wetland or river reed thickets.
- **Cause:** `hasPlumes = true` in `MapMeadowStyle` with `texturePath == NULL`. `map_props_nature.inl` fell back to generating planar quad cards intended for plume textures, but rendered them as flat white polygons.
- **Rule:** Default `hasReedPlumes = false` and `hasPlumes = false`. Shoreline reeds should always render as elegant 3D tapered blades with multi-segment Bézier curves unless an explicit plume alpha cutout texture is supplied.

### Ground ecology texture assignment overwrites the primary splatmap

- **Symptom:** Ground splatting disappears and the terrain surface collapses into a flat single-texture look as soon as ecology or habitat is set.
- **Cause:** `MapProp_SetGroundEcology` was assigning `habitatTexture` to `MATERIAL_MAP_DIFFUSE` (slot 0), directly overwriting `texSplat`.
- **Rule:** Keep `texSplat` bound at slot 0 (`MATERIAL_MAP_DIFFUSE`), bind `u_ecology` to `MATERIAL_MAP_OCCLUSION` (slot 4), and bind macro debris to slot 9 (`GROUND_DEBRIS_TEXTURE_SLOT`). Test with `test_ground_binding.py`.

### Billboard texture atlases degrade Xianxia botanical aesthetic

- **Symptom:** Wildflowers appear as flat paper billboards or low-resolution sprites that break immersion under isometric or orbiting camera views.
- **Cause:** Authoring flower fields with texture atlas cards (`petalTexturePath != NULL`).
- **Rule:** Always set `petalTexturePath = NULL` in `MapFloraSystem` / `MapProp_CreateFlowerField`. The engine's procedural 3D phyllotaxis petal and pollen dome generator constructs rich geometric flowers with per-vertex lighting, two-sided transmission, and 8 Xianxia botanical palettes without atlas artifacts.

### Missing `PropLit_UpdateLighting()` renders rocks as unlit dark grey blobs

- **Symptom:** Procedural boulders, mountain crags, and river stones using `prop_lit` appear pitch-dark or flat grey on a map until another map or pass updates lighting.
- **Cause:** `prop_lit.fs` reads `u_lightDir`, `u_lightColor`, `u_ambientColor`, and `u_viewPos`, which are only pushed when `PropLit_UpdateLighting()` is called once per frame before drawing rocks. `DrawSongQuaoMap` omitted `PropLit_UpdateLighting()`.
- **Rule:** Every map `Draw*Map()` function that draws `MapRockSet` or `MapRockSystem` MUST call `PropLit_UpdateLighting()` before drawing rocks. `prop_lit.fs` also guards against zero-length light uniforms with a safe fallback.

### Boolean DEM lake step exposes low-poly triangle sawtooth under transparent water

- **Symptom:** Through transparent shallow lake/river water, the submerged terrain bed shows a stark grid of alternating bright and dark triangles along the shoreline.
- **Cause:** Applying a discontinuous boolean height drop (`np.where(lake, terrain - 0.35, terrain)`) on a `1.0m–1.5m` raster grid creates stair-stepped vertical cliff triangles right inside the translucent water zone, and omitting `vn` normals in `write_water_mesh` leaves water vertices without smooth normals.
- **Rule:** Always smooth lake/stream bathymetry masks with a Gaussian kernel (`sigma >= 2.0–2.4`) before carving the terrain bed so the bank slopes with $C^2$ continuity, level cross-channel stream water heights via Gaussian weighted averaging, and emit explicit `vn 0 1 0` normals in generated water `.obj` files.

### `WATER_SHAPE_TERRAIN` must not overwrite vertex depth with camera slant depth

- **Symptom:** Terrain-clipped DEM water (`u_waterShape == 3`) flickers or clips its shoreline depth depending on camera pitch and backend depth range.
- **Cause:** `MapProp_DrawWaterOverlay` requested `SceneTargets_RequestSoftDepthRegion` for `WATER_SHAPE_TERRAIN`, and `water_surface.fs` took `min(waterDepth, sceneDepth)` using a hardcoded OpenGL NDC depth reconstruction (`2000.0 / (1001.0 - ndc * 999.0)`) that disagrees with vertical water column depth stored in `vertexTexCoord.x`.
- **Rule:** Skip `SceneTargets_RequestSoftDepthRegion` for both `WATER_SHAPE_RADIAL` and `WATER_SHAPE_TERRAIN`, and rely directly on `fragWaterDepth` (`vertexTexCoord.x`) with multi-octave shoreline noise feathering for `u_waterShape == 3`.

### Meadow `shadowDistance` shorter than dynamic shadow cascade radius creates a diagonal seam

- **Symptom:** A visible diagonal line splits the meadow into a darker foreground and brighter midground even when grass LOD is uniform.
- **Cause:** Setting `.shadowDistance = 14.0f` while `EnvShadow_SetFocus(focus, 20.0f)` captures a 20m radius cascade causes grass chunks beyond 14m to stop casting self-shadows inside the active shadow map, and a narrow 3.5% UV border fade (`MapShadowCoverageFade`) exposes the cascade box edge.
- **Rule:** Set `MapFloraStyle.shadowDistance >= 22.0f` to cover the entire 20m dynamic shadow cascade, use a wide UV border fade (`MapShadowCoverageFadeWidth(projected.xy, 0.18)`), and maintain a translucent grass self-shadow floor (`mix(0.38, 1.0, rawDyn)`) in `FoliageShadowVisibility`.


### Ground fill cost cannot be A/B'd by frame totals on this machine

- **Symptom:** Two builds of `song_quao` that differ by a ground-shader edit report total frame times 4 ms apart in one ordered pair and 4 ms the *other* way in the next — and identical configs differ by 2–20 ms. A real 0.85 ms win is indistinguishable from drift, and a "measurement" of a 0.4 ms idea reads as a regression as often as a win.
- **Cause:** the total includes post-process, present and the `noSampledDepth` depth twin, and the machine (Intel Iris 6000 / MoltenVK) changes clock state between runs far more than the delta under test. At 1280x720 the ground is ~23% of a ~22 ms frame, so a shader-sized win is ~1–4% of the number being read.
- **Rule:** measure the ground with the amplitude harness, not the frame total: repeat the ground draw N times with the depth test disabled after the first (`rlDisableDepthTest`, so the repeats are pure overdraw of the same colour) and compare the same N across variants. `per_draw = (total_N - total_1) / (N-1)`; at 1280x720 ground-only with N=8 the per-draw spread drops to ~±0.2 ms, enough to resolve a 10% shader change. The repeat harness is temporary profiling code — remove it before shipping (`song_quao.c`, `DrawSongQuaoMap`). Measured with it: ground ≈ 5.0–5.6 ms/draw at 1280x720, of which the shadow receiver (dynamic 9-tap HIGH + static 4-tap) is ≈0.85 ms, the cloud-shadow fetch ≈0, and the rest is the splat/ecology/grass/soil albedo chain (ALU/instruction bound, not fetch bound — replacing every detail fetch with a constant collapses the draw to ~0.5 ms).
- **Superseded in part (2026-10-22):** the ±0.2 ms figure only holds while the machine sits in one clock state. Across processes minutes apart the *same* shader measured 4.33 and 5.77 ms/draw (net 4.3–6.5 over the session), so cross-process per-draw slopes cannot resolve anything. Block-alternating inside one process (`WUXING_SQ_GROUND_REPEATS=-16`, 15-frame blocks, median of 19 pairs) reduced that to ~1.5 % at best and still moved 17 % between runs. What did hold up: **full-frame paired runs** (base, variant, variant, base ×3, same fixed camera/frame, accept only a consistent direction) resolved −0.47 ms at ~19 ms, and the ground's cost model below. Also, per-draw cost overstates frame value by ~2.7× — see the next entry.

### Ground shader is texture-sample bound; guard branches cost more than they skip

- **Symptom:** Every attempt to make `ground_splat.fs` faster without changing the image returns nothing: moving the two macro `sin()` crest waves to the vertex shader, collapsing the three `normalize()` calls on `microNormal`, dropping the unused `fragColor` varying, and re-issuing the dual-scale broad samples as `textureLod(x, uv, 4.0)` all landed inside noise. Guarding the loam/shingle/trail chain with `if (hasSoil)` (which is dead on pure-grass pixels) measured *worse*: 5.93/6.12 vs 5.46/5.85 ms/draw.
- **Cause:** the draw is texture-sample bound, not ALU bound, and every "cheap" win was ALU. Replacing one `texture*` call plus the chain it feeds moves per_draw by 0.25–0.7 ms; the sample itself is the expense, not the gradient path (`textureLod` with a fixed LOD = same cost). The `hasSoil` guard failed because `LAYER_EPS = 0.003` makes `hasSoil` true almost everywhere (so nothing was skipped), and the extra branch raised register pressure: on this rlvk→MoltenVK path a divergent guard around a large block is a net loss, not a free skip.
- **Rule:** only removing a sample (or the whole layer it feeds) pays in this shader. Measured per_draw prices, 1280x720 HIGH, to price a trade before trying it: dual-scale broad pair 1.3 ms, ecology + soil/path chain 1.6, dynamic shadow 0.9, `u_groundRelief` 0.85, `texSoilMaterial` 0.7, `texPath` 0.7, `texGrassMaterial` 0.65, `u_groundDebris` 0.55, cloud noise 0.3, `texGrass` 0.25. Do not add branch guards for a skip — if a layer must go, delete it or gate it with a uniform the whole draw shares. When a whole-draw ablation "collapses to ~0.5 ms", that 0.5 ms is vertex + raster (112 800 triangles submitted for the full 188x300 m heightfield, ~85 % of them off-screen at the gameplay camera; a `finalColor = vec4(1.0); return;` build still costs 1.22 ms/draw) — it is not recoverable from the fragment shader.

### Deleting the whole ground draw is worth only 2 ms of frame time

- **Symptom:** the ground measures 4.7–6.5 ms per draw and looks like the frame's largest single item, yet a 25 % cut inside it buys ~0.5 ms of frame time and barely moves FPS.
- **Cause:** the draw is not on the frame's critical path. Removing the ground draw entirely at the gameplay camera (1280x720, HIGH, `WUXING_SONGQUAO_SKIP=ground`) moved the frame 19.27 → 17.23 ms, i.e. **−2.05 ms (3/3 paired runs)** — about 0.36 of its isolated per-draw cost — because the rest of the frame (post chain, shadow pass, transparent pass, submission) overlaps it.
- **Rule:** before optimizing inside a map subsystem, delete the whole draw once and measure the frame delta; that is the ceiling, and it is usually far below the isolated per-draw number. Report map optimizations in frame time, from paired alternating full-frame runs. If the goal is FPS rather than map cost, the remaining headroom is in the frame's fixed chain (post ↔ shadow ↔ transparent), not in the ground.



### Drawing the ground last rejects nothing

- **Symptom:** Moving `MapProp_DrawGround` after rocks/debris/flora (so grass blades own their depth first) changes the full-scene frame time by −0.08 ms at 1600x900 — i.e. not at all — and removing the abyss `discard` at the same time does not help either.
- **Cause:** `ground_splat.fs` discards the island underside (`rockScree > 0.85 && splat.r < 0.10 && y <= -7.2`), and a `discard` in the fragment shader means every pipeline that contains it is late-Z. Adding `layout(early_fragment_tests) in;` (with `#extension GL_ARB_shader_image_load_store : require` at `#version 330`) and dropping the discard *still* measured −0.58 ms, so on this path the opaque-order rewrite has no fill-rate upside — whatever the grass's real blade-depth coverage of the ground is, it is not worth a draw-order change.
- **Rule:** don't reorder the opaque draws in `DrawSongQuaoMap` for fill savings, and don't delete the abyss discard to "enable early-Z" — the discard is load-bearing (13.8% of the frame changes without it) and buys nothing back. If a future change wants early-Z, it needs the abyss geometry culled out of the fast-path mesh (two draws: band with discard, body without), which is only worth doing against a measured win, not an assumed one.

### `texture()` is not cheaper than `textureGrad()` on rlvk

- **Symptom:** Replacing the ground's explicit-gradient fetches with implicit ones (identical coordinates, so identical LOD: `textureGrad(texGrass, tiledUV, uvDx, uvDy)` → `texture(texGrass, tiledUV)`, same for `texGrassMaterial`, `u_groundRelief`, `texPath*0.85`, `texSoilMaterial`) saves −0.08 ms per ground draw — nothing.
- **Cause:** MoltenVK lowers both to `sample`/`sample` + `gradient2d` at the same cost; the gradients are not recomputed per tap, and the trilinear/mip cost dominates either form.
- **Rule:** keep `textureGrad` in `ground_splat.fs`. It is not a performance liability here, and the implicit form would be *wrong* anyway: the fetches sit in divergent per-layer branches (`hasGrass`, `soilPossible`, `boundary`), where implicit derivatives are undefined across a quad and produce mip seams exactly at the layer boundaries. Aniso and gather are separate backend traps — see `ENGINE_LANDMINES.md` §26/§27.

### Rocks are 64 `DrawModelEx` calls — ~9 µs of CPU each, so cull before shading

- **Symptom:** On song_quao the rock pass is 64 of the 157 draws in the frame (`VKPROFILE fb=1 draws=157`) yet covers only a corner of the view; a stopwatch around `MapProp_DrawRockSystem` reads 420–650 µs/frame, and repeating the pass 8× costs ~0.27 ms per extra pass (a repeat-slope estimate that *understates* the real per-draw cost, because the fixed per-call overhead — `PropLit_UpdateLighting` + the batch kick — dominates the slope).
- **Cause:** one `DrawModelEx` per placement (30 boulders + 16 river stones + 8 crags + 10 distant crags) with no visibility test at all: off-screen and behind-camera rocks are still submitted, transformed (rlvk bakes the model matrix per vertex) and rasterised to nothing. Instancing does not rescue this — `prop_lit.vs` has no `instanceTransform` attribute, and the earlier `DrawMeshInstanced` attempt was reverted for exactly that reason.
- **Rule:** cull per placement in `MapProp_DrawRockSystem` against the camera frustum, using a bounding sphere baked from the generated mesh (`MapRockSet.boundsCenter/boundsRadius`) and a rotation-independent world bound (origin + `maxScale * (radius + |center|)`), padded by the blob-shadow radius when `drawShadows` is on. Skip only when the sphere is fully outside a plane, so the image is unchanged. At the benchmark camera this drops 42 of 64 draws and ~200–400 µs of CPU per frame. Build the frustum from `rlGetMatrixTransform()` — **not** `rlGetMatrixModelview()`, which is identity inside `BeginMode3D` (`ENGINE_LANDMINES.md` §28) — and re-check the image, not just the draw count, since culling everything also lowers the count.

### Water is one 0.80 ms draw already at the fill floor — early-Z, culling and fetch cuts all measure as no-ops

- **Symptom:** `song_quao`'s river (`MapProp_DrawWaterOverlay` → one `DrawModel`) covers 225 781 px = 24.5% of 1280×720, yet every "obvious" optimisation is a no-op: switching the whole shader to a constant output only recovers its own shading cost, disabling the depth test changes the covered area by 3.9%, and bounding the surface culls nothing.
- **Cause (measured, repeat harness on `WUXING_SQ_WATER_REPEATS`, interleaved pairs, `per_draw = (total_16 − total_1) / 15`):** `(32.81 − 20.91) / 15 = 0.79 ms` and `(33.92 − 21.98) / 15 = 0.80 ms` per draw, i.e. 3.5 ns/px — *below* the ground's 5.7 ns/px, so this shader is already cheap per pixel. Facts that close each candidate:
  - **Depth-failed fragments are 3.9%** (counted deterministically: water painted solid magenta with depth test on vs off ⇒ 225 781 vs 234 850 px, same camera/frame), so `layout(early_fragment_tests) in;` has a ceiling of ≈0.03 ms — below the machine's noise floor.
  - **No removable geometry.** `song_quao_water.obj` is 6628 faces / 19 884 vertices (unindexed, 3 verts per face), all 6628 upward-facing, 0 back-facing, 0 side-facing, 1967.8 m² total, y 0.07–11.2 — no duplicate sheet, no closed volume, and back-face culling therefore removes nothing visible.
  - **~5 trilinear taps per typical pixel** (3 for the flow-phase detail gradient, 2 dual-phase caustics, +1 shore foam only where depth < `foamThreshold`); every one is load-bearing — the caustic term survives into `underwaterTerms` through `mix(underwater, reflectedSky, reflectWeight)`, so it cannot be branched out.
  - **CPU is not the cost:** a stopwatch around the draw reads ~55 µs/frame, of which ~46 µs is the first-call pipeline bind and ~9 µs per repeat (≈50 `SetShaderValue` uploads + the batch kick). Caching uniforms would save single-digit µs.
- **Rule:** do not re-open water for per-pixel savings on this asset — there is no image-neutral headroom left, and the machine cannot resolve a ≤0.1 ms change anyway (see the ground entry above). Culling the *whole* draw is exact and is implemented (`MapWaterSurface.boundsCenter/boundsRadius` baked by `MapProp_ModelBounds`, sphere test in `map_props_cull.inl`), but it is provably a no-op on this map: the river spans x 33.7–157.7, z 24.8–271.3, so its sphere (radius ≈137 m) always intersects the frustum. It only pays on maps whose water body is local — on `verdant_path`'s lake (10.5×7.4 m at (63, 0.075, 25.5)) looking away gives `draws=14` with and without the lake, and looking at it gives `draws=32` vs `31`. If a future water body is a large multi-lobed surface, the cull needs per-chunk bounds (and more draws), not a bigger sphere.

### Ecology moisture channel in `shoreFactor` paints wet-soil spots on dry hillsides

- **Symptom:** Wide views of `song_quao` show dark circular mud blotches scattered across dry hillsides, and close views show a dark circular stain where 3D grass meets the terrain.
- **Cause:** `song_quao_habitat.png` stores Topographic Wetness Index (`moisture`) in channel `B` (`ecology.b`). `ground_splat.fs` computed `shoreFactor = max(ecology.b, 1.0 - smoothstep(0.0, 2.6, signedShore))`, promoting local TWI depressions on elevated hillsides into `wWetSoil` (which subtracts `wGrass` and darkens albedo via `wetCapillaryDarken`). Separately, `GrassShade` in `nature_surface.glsl` stacked five root-darkening multipliers (`selfShadow * rootAO * shadowVis * canVis * ambientFloor`), making 3D grass roots darker than the underlying terrain.
- **Rule:** Drive `shoreFactor` strictly from signed shoreline distance (`1.0 - smoothstep(0.0, 2.2, signedShore)`); use `ecology.b` only for lushness/color variation, never to expose mud on hillsides. Keep `GrassShade` root luminance matched to `ground_splat.fs` and never suppress grass density (`MapFlora_EvaluateFlowerSuppression`) under wildflower colonies.

### Unbent 2D ribbon width shears into 1-pixel dotted lines and upside-down arches under wind

- **Symptom:** At close zoom (`Zoom 10.28x`), grass blades bent by wind or leaning toward a downward-pitched camera collapse into 1-pixel dotted lines or fold into upside-down "U" staples.
- **Cause:** Three compounding geometry issues in `nature_parametric.glsl`, `nature_lit_parametric.vs`, and `map_props_meadow.inl`:
  1. `NatureEvaluateBlade` added `side * (halfWidth * sideSign)` in the unbent XZ plane before `nature_lit_parametric.vs` applied horizontal `dispXZ`. When wind blew parallel to `side`, `dispXZ` sheared the quad strip along its own width vector until left and right edges became colinear (`0 px` projected width).
  2. Wind and interaction were sampled at per-vertex `world` instead of the blade root `rootWorld`, splitting single blades across the `u_windCacheMap` height boundary (`abs(y - u_windCacheRegion.w) < 2.0`) or `u_interactionMap` texels.
  3. Subtracting world-space `dispSq / (...)` from `local.y` exceeded the remaining vertical rise of short blades, and excessive static lean (`> 1.0 * height`) toward `+Z` projected downward on screen (`screen_y ∝ y * cos(pitch) - z * sin(pitch)`).
- **Rule:** Evaluate wind/interaction at `rootWorld = s_cachedClumpTexel.xyz + u_worldOffset` so all vertices of a blade share one force sample. Re-orthogonalize `bentSide` perpendicular to `bentTangent` and blend with 3D `cross(bentTangent, toCamVec)` after spine deformation. Scale `maxAllowedBend` by blade height and use monotonic height compression (`rootLocal.y + bladeSpanY * (1.0 - 0.22 * bendRatio * h)`) with C1-continuous tip taper (`tipStart = 0.78`, `profile(0.78) = 0.31`). Do not add an automatic interactor at `cameraTarget` inside `MapProp_UpdateFloraSystem`.

### River stones appear as dark faceted leeches and riverbed lacks sand/pebbles

- **Symptom:** Water-worn river stones in the stream appear as pitch-black, low-poly faceted blobs with specular pie-slice highlights, and the riverbed appears as an unnatural, murky flat green sludge without sand or pebbles.
- **Cause:** Three separate issues:
  1. `Nature_BuildProceduralRockMesh` in `map_props_rocks.inl` generated meshes via `GenMeshSphere(1.0f, rings, slices)`. Triangle-area normal accumulation left all $(slices + 1)$ vertices at the top pole ($r = 0$) and bottom pole with individual un-averaged triangle normals, radiating faceted pie slices under lighting. In `prop_lit.vs`, transforming normals with `mat3(matModel)` sheared normals when stones were flattened ($hScale < rScale$).
  2. River stones were tinted `{105, 118, 125, 255}` (0.41 albedo) and darkened by excessive base AO and damp multipliers down to ~0.03 (near black).
  3. `ground_splat.fs` forced `streamBedTint = vec3(0.17, 0.23, 0.18)` and `submergedBed = vec3(0.09, 0.14, 0.13)` whenever `streamWet > 0.50`, obliterating all sand and pebble texture under a dark muddy wash. Oversized $1.5\text{m}$ mid-channel boulders blocked the channel.
- **Rule:** Average pole vertices ($r = 0$ and $r = rings$) across all slices for both normals and tangents in `map_props_rocks.inl`. In `prop_lit.vs`, transform normals by `transpose(inverse(mat3(matModel)))` so non-uniform scaling preserves perpendicular normals. In `ground_splat.fs`, generate procedural alluvial quartz/feldspar sand with hydrodynamic current ripples and domain-warped multi-hue river pebble clusters (ivory quartz, warm granite, amber jasper, slate shingle) with dome normal perturbation. Scale river stones realistically ($0.18\text{m} - 0.38\text{m}$) and embed them ($sink = 0.28 \cdot hScale$) along shoreline sandbars while keeping the central channel clear.
