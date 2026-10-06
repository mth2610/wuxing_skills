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
- **Rule:** Call `MapProp_PrepareMeadow` after camera finalization and before framebuffer captures or scene drawing. Reuse prepared IDs when the view is unchanged; retain Draw's fallback for callers without a preparation phase. Compare renderer counters as well as visible frame timing. The production test checks preparation reuse and camera/resize invalidation.

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

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
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
