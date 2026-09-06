# Meadow and Environment Upgrade Specification

Date: 2026-09-06. Status: proposed implementation specification, not an implementation report.

This document is explicitly requested by the user. It is an exception to the older no-spec convention in `DOC_ARCHITECTURE.md`. Existing API documentation remains a description of implemented interfaces only.

> **Project convention:** Except for the baseline inventory and cited external references, every requirement, API name, threshold, layout and algorithm below is a proposed design decision. Numeric values are initial acceptance targets or tuning ranges, not measured engine capabilities. Change a target only with a recorded reason and comparable evidence.

## 1. Outcome and scope

Produce a reusable outdoor rendering toolkit with a polished martial-arts landscape aesthetic inspired by Justice/Nghich Thuy Han: layered grass, coherent flower patches, soft but readable sunlight, colored shade, grounded paths and believable shallow lake margins.

Deliver the complete pipeline first in a 20 x 20 metre reference area inside Verdant Path, then extend it to the complete meadow map. The reference area contains low grass, a flower patch, tall shoreline vegetation, a path, rocks and a section of lake. Terrain and water outside that area remain visible to expose sky, exposure and off-camera shadow problems.

Required constraints:

- Grass uses procedural opaque geometry with pointed tips; no mandatory grass cutout texture or imported grass model.
- Flower texture data is neutral and tintable. Petal, center and leaf colors remain independent runtime controls. Do not bake directional lighting or cast shadows into assets.
- Real directional vegetation shadows remain supported. A fallback must be named and distinguishable in diagnostics.
- Toolkit algorithms must not contain Verdant-specific colors, coordinates or species distribution.
- Preserve map lifecycle and current consumers through adapters. No unrelated gameplay or skill changes.
- Achieving the appearance of a reference does not imply reproducing its RTX feature set or guaranteeing its quality on every device.

Not required for this delivery: ray/path tracing, mesh shaders, GPU indirect generation, Hi-Z occlusion, cutting/destruction, weather simulation, general fluid simulation or mandatory temporal antialiasing. Add those only after profiling demonstrates a concrete need.

## 2. Verified baseline

| Source inspected | Existing behavior/interface | Upgrade implication |
| --- | --- | --- |
| `maps/toolkit/map_props.h` | Meadow near/far chunk models, shadow geometry, placement density callback and render statistics are declared. | Extend the existing toolkit and preserve old entry points. |
| `maps/toolkit/map_props.h` | A local interaction-field API, flower variants, tint and flower LOD are declared. | Reuse and validate these facilities before replacing them. |
| `maps/toolkit/shaders/ground_splat.fs` | `mask = 1.0` forces grass; texture hue is mixed with a hardcoded green tone. | Implement real layer weights and move biome color into material data. |
| `maps/toolkit/shaders/nature_surface.glsl` | Two-sided normals, transmission, shadow lookup, fixed ambient floor and root darkening exist. | Calibrate light transport and remove compensating constants only with evidence. |
| `maps/toolkit/shaders/water_surface.fs` | Shallow/deep color derives from radial coordinates; reflected sky is synthesized from light/ambient colors. | Add terrain-derived depth and a shared environment reflection source. |
| `environment/env_shadow.h` | Dynamic camera-focused and static cached shadow interfaces are declared. | These are two layers, not proof of a conventional multi-cascade implementation. |
| `environment/environment_system.h` | Sun, ambient, fog and time-of-day presets exist. | Add one explicit resolved frame description shared by consumers. |

The earlier exposure A/B showed a brightness difference in a capture dominated by the island boundary. It did not establish the cause of black smoke in the meadow or prove visual correctness. Phase 0 must reproduce the actual gameplay view and VFX before accepting that diagnosis.

## 3. Ownership and proposed contracts

| Owner | Responsibility | Must not own |
| --- | --- | --- |
| `maps/toolkit` | Terrain materials, species meshes, habitat sampling, placement, chunks, lake geometry and material rendering | Global post-process state or per-skill VFX behavior |
| `maps/worlds/verdant_path` | Layout, seed, species palettes, density masks and selected environment preset | Duplicate lighting/shadow algorithms |
| `environment` | Resolved sun/sky/bounce, wind, fog and directional-shadow policy/resources | Meadow placement or flower mesh generation |
| Core/backend integration work | Color-space/output contract, exposure application, depth/reflection pass support and GPU timing where required | Map-specific brightness hacks |

Core/backend integration is an explicit dependency of the implementation, not an assumed existing capability. A missing prerequisite must be reported against its owning component; do not emulate it with unexplained map tint changes.

### 3.1 Proposed data records

These names are design contracts, not declarations already available to callers.

| Record | Required fields and units | Validation |
| --- | --- | --- |
| `MapSurfaceMaterialDesc` | Neutral albedo, normal, roughness, optional AO/height; linear RGB tint; world tile size in metres; normal strength | Positive tile size; nonmetal terrain; fallback normal and roughness when absent |
| `MapFoliageSpeciesDesc` | Stable species ID; shape type; height/width ranges in metres; curvature/taper; colors; material; LOD and shadow settings | Nonnegative dimensions, ordered ranges, valid asset references |
| `MapBiomeDesc` | Species mixture, density in clumps/m2, patch scale, slope/height/moisture preferences, exclusions, seed | Weights normalized; zero total weight produces no plants |
| `MapHabitatSample` | Terrain height and normal; water coverage/depth; signed shoreline distance; path distance; biome weights | Explicit validity flags for missing water/path data |
| `EnvFrameLighting` | Linear sun RGB/intensity, normalized direction, sky/bounce lighting, reflection reference, wind, fog, version | No NaN; nonzero direction; one immutable snapshot per frame |
| `EnvShadowSettings` | Mode, near coverage in metres, map resolution, filter radius, bias controls, vegetation range | Clamp unsupported settings; expose the effective result |
| `MapVisualFixture` | Map, world camera pose, seed, time, tier, shadow mode, VFX case, warmup, output path | Camera and effect must lie in the intended visible playable area |

### 3.2 Proposed operations

- `Environment_ApplyProfile`: validates and activates a complete profile atomically; increments a version when lighting relevant to cached shadows changes.
- `Environment_GetFrameLighting`: supplies one resolved snapshot; consumers do not independently infer sun direction or tint from a map name.
- `MapBiome_GeneratePlacements`: consumes habitat samples, species and a seed; returns placements plus count/truncation diagnostics.
- `MapNature_Create` / `Update` / `Draw` / `DrawShadowCasters` / `Unload`: reuse current implementations behind a species-aware interface or equivalent adapters.
- `MapVisualFixture_Run`: proposed test integration contract; capture renderer output and report effective state rather than relying on desktop screenshots.

Descriptors are copied or have explicitly documented lifetime requirements. Texture ownership belongs to a shared asset cache or to one surface, never both. Unload cancels callbacks before releasing models. Failed creation cleans partial allocations and returns a failure status. No resource creation during ordinary drawing.

### 3.3 Shared conventions

- One world unit represents one metre; +Y is up.
- Public sun travel direction points from the light into the scene; shading direction `L` is its negation. Adapters handle existing interfaces explicitly.
- Author colors are converted from sRGB to linear exactly once. Albedo/tint are color data; masks, normals, roughness and depth are linear data.
- Opaque surfaces write alpha 1. Flower cutouts discard uncovered pixels and write opaque covered pixels. Do not introduce blended vegetation into the main scene.
- Main and shadow passes share world transforms, wind time, interaction field and cutout rules.
- Frame order: update world/interaction -> resolve lighting -> update required shadow layers -> opaque scene -> required depth/reflection resources -> supported water/VFX composition -> exposure/tonemap -> UI.
- Exact pass placement must follow the verified engine render-target contract; the sequence above does not authorize nested render passes that corrupt state.

## 4. Phase 0 — Reproducible visual baseline

**Inputs:** Current source, executable, active tuning, screenshots and Verdant geometry. **Dependency:** None.

Implementation steps:

1. Record source revision, working-tree diff identity, build time, backend, device, render size, tier, tuning path and effective values in capture metadata.
2. Define four fixed cameras: flower close-up, reed side view, gameplay overview and lake edge. Derive positions from actual Verdant layout, not the default arena origin.
3. Add a calibration patch with neutral gray, white and color swatches plus a known white-smoke fixture. Keep its material identity fixed across maps.
4. Add debug views: unlit albedo, world normal, direct light, indirect light, shadow visibility, linear depth and final output. Display the active mode in metadata.
5. Capture real-shadow on, all-shadow off and projected fallback separately. Disable automatic time changes; pin RNG and animation time for stills.
6. Record a repeatable camera pan and wind sequence in addition to still images. Test executable startup and map reactivation independently.

Artifacts: fixture definitions, before images, effective-state manifest, per-pass timing baseline and a list of reproduced defects with suspected versus proven causes.

Acceptance:

- Consecutive fixed captures on the same backend differ by mean RGB error <= 1/255 with temporal effects disabled. Larger noise must be explained and given a measured comparison tolerance.
- Calibration objects and smoke are visible and not hidden behind mountains or outside the map.
- A changed test setting appears in metadata. Shader fallback/compile failures fail the capture.
- A reported smoke failure includes unlit, lit and composite comparisons; whole-image average brightness alone is insufficient evidence.

## 5. Phase 1 — Lighting and output correctness

**Owners:** Environment plus explicit core/backend integration. **Dependency:** Phase 0.

Implementation steps:

1. Audit upload formats, tint conversion, shader output, intermediate buffers and final transfer function with calibration swatches. Fix missing or double conversion at the responsible boundary.
2. Resolve sunlight, sky illumination and ground bounce from one profile. Start with a neutral daylight profile; preserve independent artistic control over intensity and hue.
3. Treat directional shadow visibility as attenuation of direct light. Ambient occlusion affects indirect light locally, not the entire material indiscriminately.
4. Make thin foliage two-sided without forcing every normal vertical. Use normal blending as a bounded species parameter and test grazing/backlit views.
5. Define transmission strength and thickness per material. Keep diffuse/transmitted energy bounded so a leaf does not glow like an emitter.
6. Use a fixed reference exposure for visual authoring. If automatic exposure is enabled later, use explicit metering bounds, adaptation and cut/reset behavior. Exclude irrelevant sky/UI/emission through a documented metering design.
7. Reactivating a map reapplies the complete profile without reloading resources. Leaving it clears map-owned callbacks and interaction state.

Acceptance:

- A white calibration surface remains neutral under the neutral profile; normalized channel spread <= 5% away from colored local lights.
- In the neutral profile, shadowed gray-ground median luminance is initially targeted at 30–65% of its lit equivalent; record the chosen artistic value rather than enforcing it on every mood.
- Switching maps 20 times does not change the fixed fixture output beyond Phase 0 tolerance or leak resources.
- With fixed exposure, toggling bloom or shadow does not silently alter exposure configuration.
- White smoke is checked on lit and shadowed ground, with emission/VFX body diagnosed separately. It may shade naturally but must not invert color or produce black rectangular regions.

Deliver daylight first, then late-afternoon and bright-moon profiles using the same material assets. Global brightness compensation is not a substitute for passing the color-space tests.

## 6. Phase 2 — Terrain and path materials

**Owner:** Map toolkit. **Dependency:** Phase 1.

Implementation steps:

1. Support four authored layers: meadow substrate, dry soil, wet soil and path margin. Normalize sampled weights; if their sum is zero, choose an explicit base layer.
2. Combine authored weights with slope, path distance and shoreline wetness. Terrain and vegetation sample the same habitat definition.
3. Use neutral albedo detail multiplied by a runtime biome tint. Supply normal and roughness; AO is optional and may contain only local cavity detail.
4. Blend material properties consistently. Renormalize blended normals; never sample a normal or roughness texture as sRGB.
5. Break tiling with two world-space scales and deterministic rotation/offsets. Add height blending only if layer transitions remain visibly flat; avoid mandatory parallax mapping.
6. Make path margins follow terrain height and widen/soften through a distance field, avoiding a floating rectangular strip.
7. Produce mipmaps and select supported filtering/compression by device. Review assets both close up and at gameplay scale.

Starting assets: 1024–2048 square source textures per distinct material set; target GPU formats and actual memory are reported per platform. Shared masks should be channel-packed only where it reduces cost without damaging filtering.

Acceptance:

- Debug layer weights demonstrate real transitions; no forced global grass mask remains in the new material path.
- Repeat boundaries are not identifiable in the fixed gameplay overview; no new moire during camera motion.
- Lit/unlit comparisons show no baked directional shadows in albedo.
- Path and shore transition geometry is grounded, has no z-fighting and agrees with foliage exclusions.
- Legacy callers preserve their appearance through a legacy material adapter until explicitly migrated.

## 7. Phase 3 — Grass and flower asset geometry

**Owner:** Map toolkit. **Dependency:** Phase 1; integrate with Phase 2 before acceptance.

### 7.1 Grass

Generate a curved ribbon from a quadratic Bezier centerline:

`B(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2`, for `t in [0,1]`.

Width decreases as `w(t) = w0 * (1-t)^q`; choose `q` per species. The final segment ends at one tip vertex. Generate valid normals from the curve/ribbon; two-sided lighting handles the reverse face. Root displacement is exactly zero. Use actual indexed-mesh counts in diagnostics, not estimates based only on control points.

Initial authoring ranges:

| Group | Height | Near segments/blade | Blades/clump | Shape |
| --- | --- | --- | --- | --- |
| Ground grass | 0.08–0.22 m | 2–3 | 5–9 | Short, spreading, narrow blades |
| Meadow grass | 0.20–0.45 m | 3–4 | 5–9 | Curved, mixed lean and asymmetry |
| Shore grass/reeds | 0.60–1.40 m | 4–6 | 3–7 | Slender upright stems and bent tips |

Width is authored independently from height. Randomness is seeded per placement; avoid a single thick triangle scaled vertically for every species. Distinct species must differ in silhouette and growth habit. Low tiers reduce segments/blades, not one scene node per plant.

### 7.2 Flowers

Provide at least four and target six morphology presets: open daisy, shallow cup, bell, clustered small blooms, upright spike and bud. Use procedural curved petals/cards plus real stem, leaves and center at near LOD. A single horizontal flower card is acceptable only where its projected size hides the missing volume.

Texture contract:

- Neutral grayscale detail plus alpha coverage; separate linear region masks for petals, center and leaves, or equivalent distinct material geometry.
- Runtime petal/center/leaf tint and subtle seeded variations. No baked sunlight, shadow or fixed species hue.
- Initial atlas cells 256–512 pixels; 8–16 pixel edge dilation/gutters at source resolution, verified through all used mips.
- Generate coverage-preserving alpha mips or an equivalent measured cutoff policy. Main and shadow sampling use the same coverage definition.
- Missing atlas falls back to recognizable procedural flowers, with a visible diagnostic rather than black material output.

Acceptance:

- Inspect front, side and back views against neutral backgrounds. No disappearing backfaces, broken normals or rectangular petal shadows.
- Tip silhouette remains pointed at close and gameplay camera distances.
- Four species remain distinguishable in grayscale screenshots.
- A runtime palette swap changes hue without rebaking textures or changing shadow shape.
- Neutral lighting keeps leaves readable while retaining shape; no unbounded emissive workaround.

## 8. Phase 4 — Habitat-driven placement

**Owner:** Map toolkit with Verdant-authored data. **Dependencies:** Phases 2–3.

Implementation steps:

1. Build/sample terrain height, slope, moisture, path distance and water exclusion in common world coordinates.
2. Compose density as `D = baseDensity * patchWeight * habitatSuitability * exclusionWeight`. Define the factors in [0,1] except base density in clumps/m2.
3. Generate jittered candidates or Poisson-style samples with stable world-cell hashes. Apply acceptance deterministically and keep minimum spacing between dominant plants.
4. Use broad patches (initially 3–8 m) and smaller clumps (0.3–1.2 m). Species mixture changes by habitat, not independent uniform random color.
5. Keep low grass under/around flower groups; expose deliberate ground gaps. Place reeds relative to shoreline distance, not a uniform ring of identical stalks.
6. Reproject roots to the final terrain; constrain tilt by species and surface slope. Inflate render bounds for maximum wind and interaction displacement.
7. Report candidate/accepted/rejected/truncated counts by species. Capacity exhaustion must not silently remove a rectangular part of the meadow.

Acceptance:

- Same seed/data yields identical placement hashes; changing render tier does not reshuffle roots.
- Zero accepted grass roots inside hard road/water exclusions, unless explicitly permitted by species.
- Gameplay views show connected plant masses and readable clearings rather than evenly spaced poles.
- Chunk borders do not appear as density seams. Boundary sampling uses identical rules on both sides.
- A second palette/layout preset reuses the toolkit without shader edits.

## 9. Phase 5 — Shape-correct stable shadows

**Owners:** Environment capture plus map caster/receiver shaders. **Dependencies:** Phases 1 and 3; validate against Phase 4 scene.

Implementation steps:

1. Introduce explicit effective modes: `NONE`, `PROJECTED_FALLBACK`, `REAL`. `NONE` suppresses all vegetation cast-shadow passes; material cavity shading is a separate control. Any existing SHADOW OFF fallback must be labeled/migrated explicitly.
2. Share deformation functions and frame inputs between visible and depth meshes. Near flowers cast their visible petal coverage. Near grass casts a shape-faithful geometry LOD; sparse distant casters are declared approximations.
3. Verify light-space transform, depth range, orientation, world offset and comparison direction with a known isolated blade, flower and box.
4. Stabilize near shadow projection in texel units. Choose coverage from projected plant detail and camera needs; resolution alone is not the quality metric.
5. Separate static caching from near-detail allocation. Reuse current two-layer design first; introduce an additional cascade only if coverage tests fail and timing allows it.
6. Derive depth/normal bias from world texel footprint and light angle. Sweep bias using contact/acne fixtures; reject values that erase thin stems or detach shadows.
7. Filter visibility comparisons, respecting supported R32F sampling behavior. Begin with bounded PCF; optional contact-hardening filtering belongs to the high tier after timing validation.
8. Fade real-shadow distance/LOD boundaries without a second full-length projected silhouette. Limit any local root occlusion independently.
9. Cull casters against the light receiver region, including relevant off-camera casters. Do not reuse camera-visible-only lists blindly.

Acceptance:

- Near flower shadow has identifiable head/center/stem features at the close fixture; covered petals do not cast a solid card rectangle.
- With paused wind and sub-texel camera movements, measured shadow-edge drift on a fixed receiver is initially <= 1 screen pixel at the reference resolution.
- Root-to-shadow detachment <= 2 pixels in the close fixture; no coherent acne bands on ground during a sun-angle sweep.
- The shape follows animated plants at identical time inputs. Off-camera plants capable of casting into view still contribute.
- Shadowed surfaces preserve sky fill; projected and real directional silhouettes are not double-composited.
- Readback occupancy is diagnostic evidence only. Passing requires visible receiver and motion tests.

## 10. Phase 6 — Water, wind and atmospheric depth

**Owners:** Map toolkit and environment; depth/reflection integration where necessary. **Dependencies:** Phases 1–2, with vegetation from Phases 3–5.

### 10.1 Lake

Derive shallow depth from actual terrain/bathymetry and water height. For simple lakes, a precomputed world-space depth field is sufficient and avoids requiring sampled hardware depth. Arbitrary submerged objects/refraction require a separately validated scene-depth/snapshot path.

Use distance-to-shore for wet margins and restrained wave contact; do not use elliptical radius as the final depth model. Apply absorption approximately as `T = exp(-absorption * opticalPathLength)` with linear RGB absorption controls. Keep geometric depth and view-ray optical length distinct.

Sample a shared sky/environment reflection. High tier may use a clipped planar reflection with reduced resolution/update rate; SSR is optional and must have an off-screen fallback. Avoid recursive water reflection. If blended water is unsupported, use an opaque composite with explicit background sampling; do not change global blend state to approximate transparency.

Acceptance: depth colors follow actual lake shape; shoreline has no hard green wall or continuous foam ring; highlights track view/light; absent reflection/depth support selects a declared fallback without breaking smoke or other VFX.

### 10.2 Wind and interaction

Resolve world wind direction/speed/gust once. Combine low-frequency gusts with small tip flutter; use species stiffness and root-pinned deformation. Feed the same time and deformation inputs to shadow rendering. Reuse the current local interaction field before considering a new simulation.

Specify interaction strength in metres, maximum bend, recovery time and field bounds. Initial recovery range is 0.8–2.5 seconds per species. Recenter with world-space continuity; teleports clear or reinitialize the field explicitly. Multiple overlapping interactors must remain bounded.

Acceptance: roots remain fixed; plants recover smoothly; camera movement does not drag interaction marks; 30/60 FPS runs produce equivalent recovery within 5% at matched simulation time.

### 10.3 Atmosphere

Use profile-driven distance/height fog with sky-consistent color. Keep the meadow readable in the reference area; reserve stronger haze for distance. Daylight, late afternoon and bright moon profiles reuse materials and differ in illumination/atmosphere, not a global green/dark overlay. Dust/pollen is optional and cannot obscure silhouette problems.

## 11. Phase 7 — Performance and full-map rollout

**Dependencies:** Reference area passes Phases 0–6. Optimization measurements also occur at each earlier phase.

| Tier | Initial total-frame target | Initial incremental nature + shadow + water GPU budget | Strategy |
| --- | --- | --- | --- |
| Low | 30 FPS / 33.3 ms | <= 6 ms | Opaque grass, reduced geometry/range, environment reflection, declared shadow fallback |
| Medium | 60 FPS / 16.7 ms where measured hardware permits | <= 4 ms | Chunked vegetation, limited near real shadows if supported, reduced reflection cost |
| High | 60 FPS / 16.7 ms on a named capable target | <= 6 ms | Full near species geometry, stable detailed shadows, optional planar reflection |

These are provisional budgets. Record named devices, output resolution, render scale and thermal conditions before assigning tiers. The existing machine is not automatically a High target. CPU time and other rendering passes still need room in the total frame budget.

Implementation steps:

1. Measure GPU time for ground, nature color, vegetation shadows, water and reflection separately; report CPU submission, draw calls, triangles, texture memory and peak allocations.
2. Use warm caches and repeated camera paths; record median and p95 over at least 60 seconds. Mobile sustained tests run at least 10 minutes and record throttling.
3. Keep chunks initially in the 8–16 m range, then tune from culling efficiency and draw cost. Prefer batching/instancing without one object/node per blade.
4. Select LOD from projected size where available, with hysteresis and stable hashes. Reduce segments/leaves first; thin plant count gradually only after checking field coverage.
5. Bound per-frame uploads. Update interaction data once per frame; ordinary wind changes uniforms rather than rebuilding geometry.
6. Profile alpha coverage and overdraw for flowers on target GPUs. Alpha-to-coverage is an optional MSAA-dependent variant, not a universal Early-Z guarantee.
7. Expand habitat data to the complete map. Keep identical camera fixtures and compare reference quality after expansion.
8. Exercise repeated creation/unload and map switching. Resource counts return to baseline after unload; no monotonic memory growth across 20 cycles.

Acceptance: target frame budget is met with p95 evidence, no conspicuous LOD rings/pop-in, no unreported fallback, and the full map preserves the reference area's material/lighting response.

## 12. Regression matrix and release gate

| Case | Required variations | Failure signals |
| --- | --- | --- |
| Color/output | Daylight, afternoon, moon; fixed exposure; automatic exposure only if implemented | Black smoke, crushed albedo, double gamma, map-dependent tint leakage |
| Vegetation | Close/side/gameplay; static and moving wind; all species | Card silhouettes, missing backs, root motion, atlas bleed |
| Shadow | None/fallback/real; stationary/panning camera; grazing/steep sun | Disappearing silhouettes, acne bands, detachment, doubled dark blobs |
| Habitat | Road edge, water edge, chunk boundary, slope | Floating plants, roots underwater, grids or truncation gaps |
| Water/VFX | Visible white smoke over grass and lake; reflection unavailable | Blend-state corruption, black quads, inverted depth, recursive reflection |
| Lifecycle | Fresh startup, map switch, tier switch, unload/reload | Stale profile, callback after free, memory growth |

Release artifacts:

1. Versioned fixture definitions and effective-state manifests.
2. Before/after images for the same cameras plus at least one repeatable motion sequence.
3. Asset manifest: source/license or generation provenance, dimensions, channels, color space, runtime format and memory.
4. Device timing report, tier settings and known limitations.
5. Implemented public contracts documented in the owning module's API docs.

The user reviews the actual meadow view. A successful build, occupied shadow texture or brighter overall image does not satisfy the visual release gate. Do not label the work 'AAA complete' while a required fixture still shows black smoke, obvious card geometry or unstable shadows.

## 13. Delivery order and task boundaries

| Work package | Depends on | Concrete deliverable |
| --- | --- | --- |
| M00 | None | Baseline fixtures and reproduced-defect evidence |
| E10 | M00 | Shared lighting/output contract and calibration passes |
| M20 | E10 | Layered terrain/path material in reference area |
| M30 | E10 | Three grass groups and >= 4 distinct flower morphologies |
| M40 | M20, M30 | Habitat-driven reference-area layout |
| E50 | E10, M30 | Stable shape-correct vegetation shadows |
| M60 | M20, E10 | Terrain-derived lake shading and reflection fallback |
| E60 | M30, E50 | Shared wind, interaction and atmosphere integration |
| Q70 | M40, E50, M60, E60 | Full-map rollout, performance tiers and release evidence |

M20 and M30 can be developed independently after E10. Integration gates remain sequential. Do not defer visual validation until all packages finish: validate each package in the reference area, then perform one final whole-map art pass after integration.

## 14. Reference rationale

- [NetEase interview: path tracing in Justice](https://developer.nvidia.com/blog/implementing-path-tracing-in-justice-an-interview-with-dinggen-zhan-of-netease/): describes the PC PBR foundation and path-tracing integration. It does not establish the mobile pipeline or exact flower/grass geometry. This spec adopts consistent material/light contracts, not an RTX requirement.
- [Sucker Punch: visual effects in Ghost of Tsushima](https://blog.playstation.com/2021/01/12/how-stunning-visual-effects-bring-ghost-of-tsushima-to-life/): describes shared wind and world data across foliage/VFX. This informs the unified wind/habitat design.
- [Arm: Early-Z questions](https://community.arm.com/support-forums/f/mobile-graphics-and-gaming-forum/48915/questions-about-earlyz): reference for platform-specific investigation. Measure cutout/A2C behavior on the actual backend rather than claiming all mobile hardware behaves identically.

## Patch Log

| Date | Editor | Sections | Basis | Tier |
| --- | --- | --- | --- | --- |
| 2026-09-06 | Codex | Baseline inventory | Named source/header files in section 2, read directly | Ground-truth |
| 2026-09-06 | Codex | Requirements, proposed APIs, phases and acceptance targets | User's explicit specification request and accepted plan | Project convention / proposed design |
| 2026-09-06 | Codex | Reference rationale | Linked primary-source publications | External reference, scope qualified |
