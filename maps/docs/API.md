# Wuxing Skills - Map Creator API Documentation

This document provides a detailed guide, technical spec, and sample source skeletons for creating a new map (Map Plugin) in the **Wuxing Skills** engine.

If you are an AI Agent or a Developer, just read this document carefully, create the corresponding map folder, fill in the code, and run `make`. The automation system will auto-detect and register the new map into the game.

---

## 1. Directory Structure & Naming (Required)

`maps/` is split into 2 parts:

```
maps/
    toolkit/            # Reusable code (NOT a map itself) — see section 1b
    worlds/
        <map_name>/     # Each finished map is one subfolder here
            <map_name>.h
            <map_name>.c
```

The engine uses an auto-scan script, `generate_map_registry.py` — it recursively scans all of `maps/` (regardless of folder depth) to find `.h` files declaring `Init{Prefix}Map`/`Draw{Prefix}Map`, so **a new map must always live under `maps/worlds/`**, never directly under `maps/`. No need to touch CMakeLists.txt or the script when adding a new map.

1. Must live in its own subfolder under `maps/worlds/`.
2. The subfolder name and the `.c`/`.h` filenames must match exactly.
   - Format: `maps/worlds/<map_name>/<map_name>.c` and `maps/worlds/<map_name>/<map_name>.h`
   - Example: `maps/worlds/desert_lava/desert_lava.c` and `maps/worlds/desert_lava/desert_lava.h`

---

## 1b. Map Toolkit (`maps/toolkit/`) — Read Before Hand-Writing Ground/Rock Drawing Code

**This is the most important section for an AI to be able to produce a finished map purely by reading this doc.** Before hand-writing `rlgl`/`DrawModel` code for ground, paths, or scattered rocks — check whether `maps/toolkit/map_props.h` already has a function for it. Only fall back to manual code (sections 9-12) for what the toolkit doesn't yet cover (hilly terrain/heightmaps, 3D-model forests, lava lakes...).

The toolkit consists of several files, all **owned by the Map Agent** (not the Core Agent — `prop_lit`/`grass_material` used to live in `core/` but have fully moved here since only `maps/` uses them). `map_props.h`'s implementation is split into one `.inl` file per prop type (`map_props_ground.inl`, `map_props_strip.inl`, `map_props_rocks.inl`), `#include`d back into `map_props.c` — adding a new prop type means adding a new `.inl` + `#include`ing it into `map_props.c`, not writing directly into `map_props.c`.

### `maps/toolkit/map_props.h` — the main function set, use this first

```c
// --- Ground plane ---
typedef struct { Model model; Vector3 drawOffset; bool ready; } MapGroundSurface;

MapGroundSurface MapProp_CreateGround(float width, float depth, float tileSize,
                                       const char *splatMapPath,
                                       const char *grassTexPath,
                                       const char *pathTexPath);
// Sloped-island variant: heightmapPath grayscale (WHITE=plateau, BLACK=cliff
// edge), cliffDepth = meters the edge sinks below the plateau. Same
// splat/grass/path textures + shader as MapProp_CreateGround.
MapGroundSurface MapProp_CreateGroundHeightmap(const char *heightmapPath, float width, float depth,
                                                float cliffDepth, float tileSize,
                                                const char *splatMapPath,
                                                const char *grassTexPath,
                                                const char *pathTexPath);
void MapProp_DrawGround(const MapGroundSurface *ground, Vector3 worldCenter);
void MapProp_UnloadGround(MapGroundSurface *ground);

// --- Flat strip (stone path, walkway, bridge) ---
typedef struct { Model model; bool ready; } MapStripSurface;

MapStripSurface MapProp_CreateStrip(float length, float width, float tileSize,
                                     const char *diffusePath,
                                     const char *normalPath,   // NULL = flat texture, no shader
                                     const char *roughnessPath); // pass all 3 paths = uses prop_lit
void MapProp_DrawStrip(const MapStripSurface *strip, Vector3 worldCenter, float yOffset);
void MapProp_UnloadStrip(MapStripSurface *strip);

// --- Rock props (1 rock model, scattered at many positions) ---
typedef struct { Model model; bool ready; } MapRockSet;

typedef struct {
    Vector3 position;   // X/Z world; Y is ignored — rocks auto-sink halfway into the ground
    float   radiusScale; // XZ scale ratio
    float   heightScale; // Y scale ratio — flattened for a boulder silhouette
    float   rotationDeg;
} MapRockPlacement;

MapRockSet MapProp_CreateRocks(const char *diffusePath,
                                const char *normalPath, const char *roughnessPath); // NULL/NULL = flat
void MapProp_DrawRocks(const MapRockSet *rocks, const MapRockPlacement *placements, int count, bool drawShadow);
void MapProp_UnloadRocks(MapRockSet *rocks);

// Legacy placement helper for optional rock compositions. Not the map boundary
// recipe: floating plains use cloud banks and open sky. Fixed seed = fixed layout.
int MapProp_GenerateMountainRing(MapRockPlacement *outPlacements, int maxCount,
                                  float mapWidth, float mapDepth,
                                  float minRadiusScale, float maxRadiusScale,
                                  float minHeightScale, float maxHeightScale,
                                  unsigned int seed);

// Legacy rounded-rectangle descriptor; new maps use actual terrain contours.
typedef struct {
    Vector4 rect;
    float cornerRadius, mistWidth, cloudLift, cloudBankWidth, groundInset;
} MapIslandBoundary;

// --- Sea of clouds (abyss beneath the floating island) ---
typedef struct { Model model; bool ready; Vector2 tiling, size; MapIslandBoundary boundary; Model mistModel; bool mistReady; Texture2D mistTexture; } MapCloudSea;

MapCloudSea MapProp_CreateCloudSea(float width, float depth, float tileSize);
void MapProp_DrawCloudSea(const MapCloudSea *cloud, Vector3 worldCenter, float yOffset);
void MapProp_DrawIslandMist(const MapCloudSea *cloud, Vector3 worldCenter);
typedef struct { const Vector3 *points; int count; } MapBoundaryContour;
typedef struct { float innerWidth, outerWidth, heightOffset, outerDrop, opacity; } MapBoundaryMistStyle;
bool MapProp_SetCloudSeaBoundaryContours(MapCloudSea *cloud,
    const MapBoundaryContour *contours, int count, const MapBoundaryMistStyle *style);
bool MapProp_SetCloudSeaGroundBoundary(MapCloudSea *cloud,
    const MapGroundSurface *ground, float contourHeight, const MapBoundaryMistStyle *style);
void MapProp_UnloadCloudSea(MapCloudSea *cloud);

// Opt-in far-depth sky; draw after opaque scenery inside BeginMode3D.
MapSkyDome MapProp_CreateSkyDome(void);
void MapProp_DrawSkyDome(const MapSkyDome *sky);
void MapProp_UnloadSkyDome(MapSkyDome *sky);
```

* `MapProp_CreateGround` (`map_props_ground.inl`): `tileSize` = world meters per repeat of `grassTexPath`/`pathTexPath`. Drawn with a dedicated shader, `maps/toolkit/shaders/ground_splat.fs`: reads the red channel of `splatMapPath` as a mask (white = grass, black = dirt), blends `grassTexPath`/`pathTexPath` per that mask, plus `grassDepth` so the grass/dirt edge blends more naturally. **Check the log when adding a new map** — this is a file that gets hand-edited often; a small GLSL syntax slip (e.g. a stray character before `#version`) makes the shader fail to compile and raylib silently falls back to the default shader (`WARNING: SHADER: ... Failed to compile fragment shader code` in the log) — the ground still renders (no crash) but looks completely wrong vs. intent, with no other indication besides the log.
* `MapProp_CreateGroundHeightmap` (`map_props_ground.inl`): same shader/textures as `MapProp_CreateGround`, only the mesh source differs — uses `GenMeshHeightmap` instead of `GenMeshPlane`, giving a ground that dips into a cliff at the edges instead of staying flat. `heightmapPath` is a grayscale image: **white = flat walkable plateau** (kept at local Y=0, matching the whole project's "ground is Y=0" convention), **black = cliff edge** (sinks down by `cliffDepth` meters). Generate the heightmap image with `python3 scripts/generate_island_heightmap.py <out.png> <size> <seed>` — the default 80x80 Verdant bake includes warped meadow swales up to about 0.82 m deep, a 1.15 m lake basin, and a thin cliff border. `maps/worlds/verdant_path/verdant_path.c` selects the -1.25 m contour below interior relief for perimeter mist; grass roots sample the generated mesh. Its normalization range must match the runtime `CLIFF_DEPTH` (8 m). Don't tune `plateau_edge`/`falloff_width` in the script too low — the goal is a flat map interior with only the border as a cliff. **`cliffDepth` must be smaller (less negative) than the `yOffset` passed to `MapProp_DrawCloudSea`** — otherwise the cliff face will poke through the cloud plane below it. **Implementation note:** `GenMeshHeightmap` returns a mesh spanning local `[0,width]x[0,depth]`, it does NOT auto-center like `GenMeshPlane` — centering is done via `MapGroundSurface.drawOffset` (added to `worldCenter` at `DrawModel` time inside `MapProp_DrawGround`), **not by directly editing `mesh.vertices` after creation** — that approach was tried and only rendered 1/4 of the mesh correctly (root cause unclear, not worth chasing further — offset-at-draw-time is the safe workaround).
* `MapProp_CreateStrip` (`map_props_strip.inl`): drawn with `maps/toolkit/shaders/path_blend.fs` — texture repeats by per-surface `tiling`, world-space noise breaks up the geometric edge using opaque cutout, and edge wear blends the road into vegetation. Passing both `normalPath` and `roughnessPath` enables tangent-space normal lighting and restrained roughness-driven specular response; passing `NULL` for both selects the cheaper diffuse-only path.
* `MapProp_CreateRocks` (`map_props_rocks.inl`): pass `NULL` for `normalPath`+`roughnessPath` to use a simple flat texture (no shader); pass all 3 texture paths to use the `prop_lit` material (normal map + roughness, real lighting response).
* `MapProp_DrawRocks`'s `drawShadow`: **set `false` for the border/cliff rock ring** — dozens of `Environment_DrawSmartShadow` calls stacked/overlapping is real alpha overdraw (measured, not theoretical, as the cause of an FPS drop), and shadows at the map's edge are meaningless anyway. Only set `true` for a small number of decorative scattered rocks where the shadow is clearly visible.
* `MapProp_GenerateMountainRing` remains a deterministic placement helper. It is not used for the floating-plain boundary; keep ordinary biome rocks inside the playable map.
* `MapProp_CreateCloudSea` (`map_props_cloud.inl`): cloud density is now sourced from a **texture**, `assets/textures/cloud_noise.png` (grayscale, tileable, generated with `python3 scripts/generate_cloud_noise.py`) — NOT computed via per-pixel FBM/`sin()` like the first version. Changed because this plane usually covers a very large screen area (fill-rate bound); 2-layer multi-octave FBM costs dozens of `sin()` calls per pixel and was measured (not theorized) to cause real FPS drops on weaker machines — 1-2 texture reads are much cheaper. Drawn with `maps/toolkit/shaders/cloud_sea.fs` — uses `discard` for low-density areas instead of alpha-blending, since `maps/CLAUDE.md` forbids alpha < 255 in the main scene (breaks particles). `width`/`depth` should be much larger than the map so it reads as an endless sea of clouds, not an obviously cut-off card. Only visible when standing near the cliff edge (`MapProp_CreateGroundHeightmap`) looking down through gaps between the border rocks — standing in the middle of the plateau, the ground itself blocks the view, matching real terrain.
* Calling convention: `Create*` is called exactly once in `Init{Prefix}Map`, `Draw*` is called every frame in `Draw{Prefix}Map`, `Unload*` is called in `Unload{Prefix}Map` if the map declares an Unload function. `MapProp_GenerateMountainRing` is the exception — it doesn't load any resource, it only fills an array with numbers, so it can be called anywhere inside `Init`, even with no corresponding `Unload`.
* Rocks using `prop_lit` (all 3 paths supplied) need `PropLit_UpdateLighting()` called **once per frame before drawing** (see the full example in section 6). Ground/strip/cloud sea each push their own lighting uniforms inside their own `Draw*`, no extra call needed.

### Reusable meadow, flower, and water surfaces

`map_props.h` also exposes three geometry-batched natural surfaces:

Parametric meadow draws support world translation only. `maps/toolkit/map_props_meadow_parametric.inl` uploads `u_worldOffset` for visible and shadow passes; their vertex shaders use `local + u_worldOffset` for wind, interaction, lighting and root LOD distance, with unchanged local normals. MVP still contains the camera transform. Missing offset uniforms select the expanded fallback; adding rotation or scale requires extending this position/normal contract. `maps/tests/test_meadow_world_space.py` executes the production uploads and verifies camera cancellation, nonzero offsets and unchanged clip transforms.

Ground retains authored substrate color/detail under a restrained olive-earth palette. `MapProp_SetGroundReliefMap(ground, path)` optionally binds a ResourceManager-owned companion at raw 2D unit 8, separate from material slots. Its RG channels carry normalized substrate/soil relief; BA carry litter/moss classification. `assets/textures/verdant_terrain_relief.png` is **derived**, not authored displacement: `maps/toolkit/tools/bake_ground_relief.py` reconstructs periodic, zero-mean heights from existing normal RGB with a least-squares Poisson solve and percentile normalization; its BA classifications come from substrate chroma. Original material normal RGB and roughness A remain unchanged. Close pixels sample this mipmapped companion; mixed material boundaries additionally sample soil height, while unresolved pixels retain ecology coverage without relief reads. `MapGroundSurface.tiling` is per-surface and uploaded during the active draw. `MapProp_DrawIslandMist` draws twelve overlapping, world-aligned quads (48 vertices, one texture batch) in the transparent pass, using the neutral 256px RGBA sprite baked by `maps/toolkit/generate_cloud_mist.py` (`/usr/bin/python3` with NumPy/Pillow). No particles, runtime noise, or vertex updates are involved. It flushes before and after disabling depth writes; depth testing remains active. Call after opaque scenery, before volumetric fog. Perspective supplies parallax without camera offsets.

`WUXING_NO_CLOUD_SEA=1` suppresses the Verdant Path background sheet for same-binary frame-cost comparisons; it is a measurement switch, not a quality preset.

Cloud noise uses a separate raw unit; it must not replace the turf's SPECULAR/METALNESS material slot. `maps/worlds/verdant_path/verdant_path.c` uses `VolumetricFog_SetDistantCoverage(2/3)` and atmospheric base density 0.008 to broaden the distant transition, and retains local mist XZ extents scaled by `sqrt(2/3)`; other maps restore default distant coverage on activation.

- Untextured, non-plumed meadows use indexed tuft templates and one immutable RGBA32F blade atlas in `maps/toolkit/map_props_meadow_parametric.inl`. Regular meadows share canonical near descriptors across visible and shadow LODs, deriving width and far-curve compensation. `shaders/nature_parametric.glsl` matches the CPU blade authoring in `map_props_nature.inl`: far height scales by 0.78, lean by 0.67 and droop by 0.65, retaining the 0.12-height tip floor and 0.68-height tip anchor. Reeds and special low-blade layouts retain exact authored variants within the same atlas. Initialization falls back to expanded meshes if required shader uniforms or resources are unavailable. `WUXING_MEADOW_RENDERER=legacy` selects expanded meshes; The default keeps the shared atlas and submits candidates for GPU rejection. `WUXING_MEADOW_SUBMISSION=compact` opts into CPU-compacted submissions; `legacy` explicitly selects the default GPU-rejection path. `MEADOW_PARAMETRIC` reports atlas bytes. `MapMeadowSurface.parametric` is private state owned and released by the surface.
- Parametric visible LOD selection in `maps/toolkit/map_props_meadow_parametric.inl` uses each tuft's root distance and immutable spatial hash, with 4 m near/mid and 10 m mid/far transition bands. The opt-in compact path writes four exact tuft IDs per RGBA32F texel; only selected roots enter each chunk/template draw. Call `MapProp_PrepareMeadow` after finalizing the camera and before rendering to stage IDs without splitting scene passes. Camera, offset, projection, FOV, resolution, quality and range changes invalidate preparation; an unchanged view reuses IDs without uploading. `MapProp_DrawMeadow` retains an upload fallback for callers without preparation. `WUXING_MEADOW_SUBMISSION_TRACE=1` reports candidate/submitted instances and vertices. Parametric visible chunks retain source order by default. `WUXING_MEADOW_ORDER=sorted` opts into stable nearest-first view-depth ordering, including orthographic cameras; `legacy` explicitly retains source order. Shadow geometry never reads the visible ID list and retains its original order; expanded fallback meshes retain chunk-level staggering and hysteresis.
- `MapMeadowStyle.botanicalVariation` in `maps/toolkit/map_props_nature.inl` defaults to zero for existing shapes; values in [0,1] mix spatially coherent arches with rare broad/needle accents. Height stretching inversely scales width to retain approximate blade area; control-point structure and stable botanical identities remain shared across LODs. Verdant opts into 1.0.
- `maps/worlds/verdant_path/verdant_path.c` derives meadow height from `0.42 + 0.24 * growth` metres and scales sparse habitat by `0.65 + 0.35 * coverage`; placement jitter and path taper are applied afterward. Flower colonies retain a shorter grass underlayer. The ecology bake remains a normalized growth potential, rather than a second physical height source.
- `maps/toolkit/meadow_palette.h` shares the ecology habitat field with immutable blade colors. For `botanicalVariation > 0`, grass tips blend toward a green/olive/straw canopy palette; reeds retain authored colors. `shaders/meadow_palette.glsl` supplies the same linear RGB palette to distant ground, masked by ecology coverage so clearings remain clear. Keep the CPU and GLSL palette definitions synchronized.
- `shaders/nature_surface.glsl` and `shaders/ground_splat.fs` use the configured Environment hemispheric irradiance without a minimum ambient brightness. Grass solar transmission retains its albedo hue; Beer-Lambert canopy attenuation increases for low sun, while sky illumination remains independent of directional/cloud shadow visibility.
- `maps/toolkit/shaders/map_shadow.glsl` fades dynamic shadow coverage over the outer 12% of its UV extent to soften the moving box in wide camera views. The interior retains full contrast; static coverage keeps its 3.5% edge fade.
- `MapEcology_Bake` creates one cached 1024² habitat texture (coverage/growth/moisture/habitat) and a second texture containing signed road/shore distances in meters. `MapEcology_Sample` provides matching bilinear CPU sampling; `MapProp_SetGroundEcology` borrows the data for ground shading. Detach ground before `MapEcology_Unload`; only one baked dataset is live at a time. Verdant uses it for placement eligibility, canopy height, ground palette, road blending and shoreline wetness. Ground normal RGB / roughness A keep their existing texture contract.
- `MapProp_CreateMeadow` builds curved multi-segment foliage blades from caller-owned placements. Every blade terminates in one pointed triangle instead of a blunt final quad, saving three vertices per blade while improving its silhouette. Eligible multi-segment, non-plumed meshes pack byte-identical vertices into 16-bit indices at initialization, preserving every triangle and all position/normal/UV/color seams; meshes exceeding the index range or saving less than 8% retain their original layout. `MEADOW_MESH` logs aggregate stored vertices and attribute/index bytes across all LODs. It partitions the field into configurable chunks, builds compensated near/mid/far geometry, and selects/culls chunks by distance plus a conservative camera-frustum sphere test. Draw and near-LOD ranges scale with `GfxQuality`, so mobile tiers submit fewer chunks without rebuilding the field. Mesh LOD uses full 3D camera distance with FOV scaling; the camera orbit offset applies only to visibility range. Reduced grass LODs retain stable blade seeds from the detailed clump. Expanded fallback LOD thresholds are spatially staggered over a 4 m band and retain per-chunk hysteresis; the parametric path uses the per-tuft selection described above. Set `shadowDistance` to enable the directional depth-pass proxy (three blades, two segments per tuft) at HIGH. Grass no longer creates projected contact wedges; lower tiers omit the optional depth geometry. Untextured geometry uses a dedicated opaque shader with no alpha discard, preserving Early-Z/HSR on tile-based mobile GPUs; only authored cutout foliage uses `nature_lit.fs`. An optional second UV channel supports cutout materials while the first remains dedicated to wind phase/root masking. `MapProp_DrawMeadow` applies coherent macro gusts, restrained per-plant flutter, two-sided thin-foliage transmission, procedural root contact occlusion, environment directional shadows, and VFX point lights.
- `MapProp_BeginNatureInteraction(focus, dt)`, `MapProp_AddNatureInteractor(position, radius, strength)`, and `MapProp_EndNatureInteraction()` update one moving 64×64 vector-displacement field. Begin scrolls the local 18 m field with the focus and applies exponential recovery; call Add any number of times for players, monsters, or vehicles; End performs one 16 KB texture upload. Call `MapProp_AddNatureWindVorticles(time)` once between Begin/End to make both meadows and flower fields receive the Core Wind system. Vegetation's broad world-space grass waves now mirror `Wind_GetMacroAt`'s hash-gradient noise, drift, base velocity, and gust amplitude instead of running independent sine waves. MED/HIGH evaluate both horizontal noise channels; LOW/UNLIT retain the same advected field and use one channel along the base direction to halve the gradient-noise work. Short, low-compliance grass samples a camera-centred 32x32 RGBA8 macro-wind cache over 64 m, refreshed on 30 Hz intervals with immediate invalidation when its inputs change and interpolated between consecutive Core wind samples. Its 4 KB upload occurs only when the cache interval, centre, reference height, or macro configuration changes. The shader retains the analytic field outside the cached region, beyond 2 m from its reference height, for high-frequency fields, or when allocation fails; reeds and flowers use the analytic path. Both visible and shadow shaders share the same cache. `WUXING_WIND_RECEIVER_TRACE=1` reports CPU update cost and an off-grid comparison against Core wind at blade height. This cache approximates spatial/temporal wind and does not accumulate rendered frames. Grass and flowers transform that shared velocity through different receiver profiles: grass reacts quickly with more transverse flutter, while flowers lag, bend less, and oscillate more slowly. Flutter amplitude is proportional to live wind deflection, so zero wind produces zero procedural motion. Local airflow maps to canopy displacement at 0.10 m per m/s, independently of the existing species-specific ambient macro compliance, with the existing 0.55 m displacement cap. Motion guide, target and typed external fields additionally drive a rooted canopy spring through `MotionFields_SampleBody` with `RECEIVER_ROOTED`, explicit projected area and XZ response axes in `maps/toolkit/map_props_nature.inl`. Acceleration channels become `massKg * accelerationMps2` before the spring; Newton channels retain their units. Receiver response uses representative 3 g tip mass, 0.30 N/m stiffness, damping ratio 0.30, bounded 120 Hz substeps, and the same 0.55 m tip-deflection cap. Independent target airflow also drives aerodynamic force against the current tip velocity: 1.225 kg/m³ air density, drag coefficient 1.2, representative projected blade area `0.25 × 0.012 m²`, and backward-Euler quadratic drag. Ordinary Wind is not sampled again by this adapter. An authored drag law suppresses the adapter's automatic airflow drag. Direct typed flow bends the representative tip alongside the existing ambient Wind deformation; this constrained canopy approximation does not replace the shader's ambient medium as a fluid boundary condition. Roots remain anchored; displacement and velocity persist in world-aligned receiver texels when the field scrolls. The spring overshoots to the opposite side after forcing stops, then settles. Only active field bounds and still-moving texels are evaluated; an idle receiver with no motion fields skips the spring pass. This is a representative canopy response rather than simulation of individual stalk geometry or tree trunks. `[MOTION] rooted_canopy` traces expose the queried cell count and maximum tip displacement when wind tracing is enabled. The strongest Linear Gust or Radial Blast is sent to both vegetation vertex shaders as a direct analytic impact. A Radial Blast derives its outward direction per plant and travels as a soft expanding annulus, so nearby plants react first and the pressure front then sweeps outward; it is not rendered as a filled Perlin patch. Vortex and Turbulence sample the active map terrain plus a representative 0.25 m grass canopy height per affected interaction texel; they never sample at the airborne source centre. Invalid terrain samples are skipped, and flat maps retain their existing zero-height fallback. They remain spatially sampled and superposed in the interaction texture using the exact Core evaluator, because collapsing either field to one direction would rotate or flip the whole patch as a rigid sheet. Guided Particle presents the radial impulse briefly, then its Turbulence ramps over 0.2 s into the stronger, longer-lived primary motion. Every custom-uniform upload and its vegetation draw stay inside the same explicit `BeginShaderMode`/`EndShaderMode` scope, which is required by rlvk. Expanded-mesh vertex shaders recover world space with `u_worldFromShaderSpace`, because this engine's `matModel` includes the view transform; translation-only parametric shaders use their explicit `u_worldOffset` instead. The receiver window follows the dominant impact, roots remain pinned by the height mask, and visible geometry plus dynamic shadow casters use the same deformation. Set `WUXING_WIND_RECEIVER_TRACE=1` to log Guided cast/arrival, selected receiver source, shader id, reflected uniform status, radius, bend strength, normalized impact age, and each reflected uniform location; active sources refresh those traces every 0.5 s. Call `MapProp_ClearNatureInteraction()` on map unload to release the texture and prevent old trails leaking into the next map.
- `MapProp_GenerateMeadowPlacements` provides deterministic jittered-grid distribution over arbitrary bounds. A map-supplied density callback returns `[0,1]`, allowing biome masks and hard exclusions for roads, water, cliffs, or gameplay clearings; generated roots sample the real ground mesh height.
- `MapProp_CreateFlowerField` builds volumetric crossed stems, leaves, bloom heads, and flower centers. With `petalTexturePath`, each head becomes one authored full-bloom alpha-cutout card plus an opaque 3D pollen center; `atlasColumns`/`atlasRows` and per-placement `bloomVariant` select different species silhouettes. Texture RGB is reduced to luminance, so runtime tint owns all flower hue. `maps/toolkit/textures/wildflower_bloom_atlas_v2.png` is the reusable neutral 4×2 atlas (eight species at 256² per cell). Without a texture, the builder falls back to procedural petal geometry. Every field also owns a compensated far mesh: a stable world-space hash retains 56% of blooms, enlarges them slightly to preserve the field's visual mass, and removes sub-pixel stems. This cuts the textured far mesh from 36 vertices per source flower to about 3.36 while also reducing alpha-tested bloom coverage. The field-level switch uses spatial staggering and persistent hysteresis to prevent camera-boundary thrashing. Each field stores a conservative bounding sphere and is rejected before shader setup when outside the view frustum or its quality-scaled draw range; use `MapProp_SetFlowerFieldDrawDistance` to tune that range and `MapProp_SetFlowerFieldLod` to tune the quality-scaled far-LOD and contact-shadow ranges. A visible field remains one vegetation draw call plus, only at useful MED/HIGH distances, one batched multiplicative contact-shadow draw. LOW/UNLIT neither allocate nor draw the optional shadow mesh. Large maps should create localized fields (typically one per authored flower patch) so invisible patches cost no draw call. Shadow direction updates from the current environment light without issuing a draw per flower.

Call `MapProp_ResetNatureRenderStats()` once before a map's nature draws, then read `MapProp_GetNatureRenderStats()` for submitted near/far meadow chunks, near/far flower fields, frustum/distance rejects, and contact-shadow draw counts. `meadowShadowDistanceCulled` and `flowerShadowDistanceCulled` isolate visible batches whose contact shadows were intentionally skipped. These counters measure CPU submission decisions; use platform GPU timing separately for shader/fill cost.
- `MapProp_CreateWaterSurface` builds a tessellated lake plus an irregular seed-driven, multi-ring bank that slopes from the waterline to `bankGroundY`. Its shader combines multi-directional displacement, reconstructed wave normals, texture-driven microdetail, Fresnel response, sun glint, depth color, restrained broken shoreline foam, and VFX point-light response. `detailScale` and `detailStrength` tune reusable surface breakup; `MapProp_GetWaterEdgePoint` places reeds/rocks against the exact generated shoreline. Radial lakes calculate surface optical depth from the bed profile and fade transparency at the shore; other shapes can use scene depth. In `maps/toolkit/map_props_nature.inl`, the CPU wake field continues advancing at fixed 60 Hz; `MapProp_DrawWaterBed` uploads its 128×128 RGBA8 texture only when its 64 KiB quantized payload or texture identity changes. `waveUploadedPixels` and `waveUploadedTextureId` are toolkit-owned cache fields: initialization records the loaded texture bytes, and unload releases and resets the cache. Cache allocation failure retains unconditional uploads without changing the simulation.

The caller owns layout and art direction through placement/config structs; the toolkit owns geometry, shaders, rendering, and cleanup. This lets maps reuse the same rendering quality without sharing identical layouts. Every created surface must be paired with its corresponding `MapProp_Unload*` call.

At HIGH quality, a map may register an Environment dynamic-caster callback and
call `MapProp_DrawMeadowShadowCasters` / `MapProp_DrawFlowerFieldShadowCaster`
inside it. Flowers submit full near geometry and preserve their atlas alpha
silhouette in the depth pass. Meadows use a stable shadow-only geometry LOD:
three pointed blades per clump, with the same wind/interaction deformation.
Meadow caster culling follows the Environment shadow focus and includes each
chunk's radius so an orbit camera does not drop visible ground shadows.
HIGH combines those real silhouettes with a short, subdued root-contact layer;
it is 10% of the authored projection length, remains a single six-vertex quad
per plant, and uses analytic side/tip feathering instead of a hard black wedge.
Its reduced strength prevents it from replacing the directional silhouette. Distance
rejection stays at batch/chunk level; no backend-dependent model-matrix fade
can silently turn the multiplied pass white. MED/SHADOW OFF use a softer 62%
projection-length fallback, while LOW/UNLIT
omit vegetation shadows. Set `WUXING_NATURE_SHADOW_MODE=real` to disable the
contact layer for shadow-map validation, or `projected` to suppress vegetation
casters and compare the fallback in isolation. The default is `hybrid`.
For automated caster isolation, `WUXING_NATURE_SHADOW_CASTERS=flower` records
only flower geometry, `meadow` records only grass/reeds, and `none` suppresses
all vegetation casters for a fixed-camera receiver comparison. An active filter
also bypasses gameplay distance culling so an off-camera diagnostic region can
still be captured. Combine it with `WUXING_SHADOW_FOCUS_X/Z` and
`WUXING_SHADOW_DYNAMIC_VERIFY=1` to log occupied dynamic shadow-map texels.
The same diagnostic also projects a 256x256 ground grid through the receiver
comparison and reports normal/flipped texture coverage, catching a sampler
orientation regression that caster occupancy alone cannot detect.
The 2026-09-05 Vulkan validation at Verdant Path's first flower meadow
(`focus=(27,20)`, 2048²) recorded `16079` occupied texels in flower-real mode
and `419/65536` shadowed ground probes in the correct orientation, versus
`202/65536` if flipped; vegetation casters suppressed recorded `0` occupied
texels. This is the regression baseline:
the atlas-cutout flower geometry is demonstrably present in the R32F map.
Always unregister the callback before unloading map-owned models.

Opaque toolkit materials use `map_shadow.glsl` through `MapShadow_ConfigureShader`,
`MapShadow_AttachMaterial`, and `MapShadow_UpdateShader`. The Environment module
owns separate camera-following dynamic and world-fixed static capture layers;
map materials explicitly bind both and take the minimum visibility. Build the
static layer once with `MapProp_DrawRockShadowCasters` between Environment's
static Begin/End calls. Terrain self-shadow capture is optional: shallow
heightmaps under a low sun can quantize into full-screen parallel acne bands,
so Verdant Path deliberately leaves its ground out of the static caster layer.
The heightmap still receives rock and vegetation shadows and retains its
normal-based terrain lighting. Use `MapProp_DrawGroundShadowCaster` only when a
map has sufficiently steep relief and a verified receiver bias.
Maps that support runtime shadow toggles or moving directional light should
repeat that capture only when `EnvShadow_NeedsStaticCapture()` returns true;
this also handles a mobile user enabling the optional layer after map init.
Ground and path light their real mesh normals, while nature materials use a
cheaper two-sided thin-foliage response suitable for dense fields.
`MapShadow_UpdateShader` also folds `inverse(rlGetMatrixTransform())` into both
light projections. This is required by the project's custom 3D pass, where
DrawMesh exposes model-plus-view shader space rather than raw world space;
uploading an uncorrected world light-VP leaves a valid shadow map but samples it
at unrelated coordinates on Vulkan.
Shared map shadow receivers select PCF cost from `GfxQuality`: HIGH uses one center plus
eight surrounding manually-bilinear PCF taps (36 point samples) for sub-texel
grass/flower edges, MED uses
four direct taps, and LOW/UNLIT uses one comparison. Comparing first and then
interpolating coverage avoids both unsupported R32F linear filtering and the
block grid caused by interpolating stored depth. Both capture layers fade across the outer 3.5% of their
coverage so a moving focus cannot reveal a hard square boundary.
The center carries 28% of the smooth resolve so thin stems cannot fall between
the surrounding taps. HIGH blends `thinFeatureBoost * 0.25` of the darkest cardinal/center sample into that smooth dynamic
resolve and applies a mild post-PCF contrast curve, while leaving the static-cache
filter untouched. Its default 1.15-texel dynamic footprint can be overridden at shader compile time
with `MAP_DYNAMIC_SHADOW_RADIUS`. Ground uses 0.65 texels to retain grass shadow silhouettes,
and keeps 70% of ambient fill on soil, blending to 48% under the turf layer.
Turf resolves captured visibility with a 1.45 exponent so thin blade shadows stay
readable against the darker substrate. Grass blades keep 78% of
ambient fill inside captured shadows; neither adjustment darkens unshadowed pixels or adds samples.
Foliage retains its separate close-range filter described above. Textured vegetation
casters add derivative-based conservative alpha coverage in the depth pass;
this retains minified petal tips without changing the visible geometry or atlas
silhouette. The dynamic receiver uses a smaller vegetation-safe depth bias so
short flowers are not erased. For large maps, treat the dynamic target as a
near cascade with `EnvShadow_SetFocus`; Verdant Path uses a 20 m half-extent
(about 1.95 cm/texel at 2048²) centered on `camera.target`, while the static
cache supplies distant terrain and rock occlusion. Real vegetation casters are
culled conservatively in world space around this capture region—not against the
shorter contact-shadow range. The bound expands for low sun angles, avoiding
false-negative caster rejection across Vulkan/OpenGL matrix conventions while
still skipping distant field chunks.

Use `MapProp_SetGroundTint` to grade the tiled terrain into the same palette as its 3D vegetation. This prevents the common failure where the ground reads as a bright photographic carpet while foliage reads as dark disconnected props.

### `maps/toolkit/prop_lit.h` — real-lit material for rocks/paths

```c
Shader   PropLit_GetShader(void);
Material PropLit_MakeMaterial(Texture2D diffuse, Texture2D normal, Texture2D roughness);
void     PropLit_UpdateLighting(void);
```
`map_props.c` calls these automatically when you pass all 3 paths to `MapProp_CreateStrip`/`CreateRocks` — you usually don't need to call them directly, **except** `PropLit_UpdateLighting()` still must be called once per frame in `Draw{Prefix}Map` (it doesn't run automatically). Reads lighting via `Environment_Get{SunDirection,SunColor,AmbientColor}()`, adjust automatically for time-of-day if the map uses a day/night cycle. Never call `UnloadShader()`/`UnloadMaterial()` on the result — the shader is shared, cached via `ResourceManager_LoadShader`.

In `maps/toolkit/prop_lit.c`, call `PropLit_UpdateLighting()` inside the active 3D camera scope. It converts the sun direction without translation and camera position into the shader/view space emitted by `DrawMesh`; sun and ambient colors retain their Environment values. `maps/toolkit/shaders/prop_lit.fs` keeps view direction, shading normals, shadow sampling and VFX lighting in that space, while the existing inverse-view uniform recovers world position for cloud visibility and ground-contact AO. Material haze uses the physical camera-to-prop distance.

### `maps/toolkit/grass_material.h` — alternate ground material (currently shelved)

Texture-blend hybrid ground material (grassBase + grassDetail + dirt, blended via `fbm2` noise) — was once tried as the main ground but shelved due to an unresolved visual issue (see `core/docs/PROGRESS.md` Item 38). Still in the codebase for possible reuse later; **not the default choice** — `MapProp_CreateGround` currently uses its own `ground_splat.fs`, not `grass_material`.

---

## 2. Header File (`.h`) Declaration Rules

The auto-scan script reads the `.h` file's contents to find the prefix.
* All functions must use one consistent capitalization (CamelCase) for the Prefix.
* **Required**: declare at minimum the two functions `Init{Prefix}Map` and `Draw{Prefix}Map` in the `.h` file.

### Standard header skeleton:
```c
#ifndef DESERT_LAVA_MAP_H
#define DESERT_LAVA_MAP_H

// Required
void InitDesertLavaMap(void);
void DrawDesertLavaMap(void);

// Optional (if present, the script auto-registers them)
void UpdateDesertLavaMap(float dt);
void UnloadDesertLavaMap(void);

#endif // DESERT_LAVA_MAP_H
```
*Note: if the `.h` file contains `UpdateDesertLavaMap` and `UnloadDesertLavaMap`, the system will automatically call them in the engine's main loop.*

---

## 3. Space Planning & Arena Coordinates

Maps are drawn as a "floating island" to support the ring-out fall mechanic along the Z axis. When drawing a map, use the following coordinate constants to stay in sync with player/monster movement and collision logic:

Real-world-scaled: 1 unit = 1 meter (rescaled from the old 1cm-scale — see root `CLAUDE.md` "Standard coordinates & scale"). Code samples further below in this doc (§ examples using `600.0f`/`440.0f`/`1800.0f`) predate the rescale — use the constants below, not the old sample literals, when writing a new map.

* **Arena Center:** `(6.0f, 0.0f, 4.4f)` (constant `arenaCenter`).
* **Active Radius:** `18.0f` (constant `arenaRadius`). All walkable ground should stay within this radius.
* **Default Elevation (Y):** the main ground sits at `Y = 0.0f` (or a very small offset like `-0.0005f`).

---

## 4. Environment & Lighting System API (`environment_system.h`)

To create an eerie nighttime atmosphere or change moonlight/fog colors, use the following functions in your `Init` function:

### Lighting & fog setup:
* `void Environment_SetAmbientColor(Color col)`: ambient (shadowed-area) color (e.g. deep blue-black for night).
* `void Environment_SetSunColor(Color col)`: main moonlight/sunlight color hitting objects.
* `void Environment_SetSunDirection(Vector3 dir)`: moonlight direction (auto-normalized).
* `void Environment_SetShadowColor(Color col)`: shadow color and darkness.
* `void Environment_SetFogConfig(EnvFogConfig config)`: fog configuration.
  ```c
  typedef struct {
      Color color;    // Fog color (usually matches the Ambient color)
      float start;    // Distance where fog starts fading in (from the Camera)
      float end;      // Distance at which fog is fully opaque
      float density;  // Density (usually leave at 1.0f)
      bool enabled;   // Enable/disable fog
  } EnvFogConfig;
  ```

### Fake Shadow Setup (Smart Fake Shadow) — Extremely Important:
For mobile optimization, the engine **forbids real-time shadows**. Every static obstacle on the map (stone pillars, tree trunks, boulders) must have a fake shadow drawn flush to the ground by calling the following function **before** drawing that object's 3D model:
```c
void Environment_DrawSmartShadow(Vector3 pos, EnvShadowShapeType shape, float width, float height);
```
*   `pos`: the object's foot position (where it touches the ground).
*   `shape`: shape type (`ENV_SHAPE_SPHERE`, `ENV_SHAPE_CYLINDER`, `ENV_SHAPE_BOX`).
*   `width`: diameter/width.
*   `height`: height (used to compute how far the shadow stretches based on sun direction).

---

## 5. Graphics & Mesh Design Principles

1. **Aesthetics:** Fit the mysterious nighttime style. Warm muted tones, brownish-gray stone, combined with subtle glowing accents from elemental veins.
2. **Low-Poly & Flat Shading:**
   - Prefer Raylib's basic primitives (`DrawCylinder`, `DrawSphere`, `DrawCube`).
   - Cylinders should use a low segment count (e.g. `segments = 8` or `16`) to expose the sharp facets characteristic of low-poly.
3. **Alpha Discipline (Required):**
   - **Absolutely do NOT draw any object on the map with an Alpha value less than 255** (e.g. drawing a translucent water pool with alpha = 200).
   - *Reason:* The engine renders the entire 3D scene into a single shared Canvas. Writing Alpha < 255 onto this Canvas causes particle effects drawn on top afterward to get ugly dark-gray blocky artifacts around them.
   - *Solution:* Draw water surfaces fully opaque (Alpha = 255), using dark blue-green tones to simulate water depth.

---

## 6. Sample Source Skeleton — Using the Toolkit (Recommended, Start Here)

All maps are floating plains above a sea of clouds, with open sky in the distance. Use the shared ground/cloud boundary configuration below for grassland, desert, and stone biomes. Change the interior materials and props for each biome; do not enclose the horizon with mountains or giant rocks.

```c
#include "verdant_path.h"           // rename to match your map
#include "raylib.h"
#include "environment/environment_system.h"
#include "maps/toolkit/prop_lit.h"
#include "maps/toolkit/map_props.h"

#define MAP_WIDTH 100.0f
#define MAP_DEPTH 75.0f
static const Vector3 kMapCenter = {MAP_WIDTH * 0.5f, 0.0f, MAP_DEPTH * 0.5f};

#define PATH_LENGTH (MAP_WIDTH - 10.0f)
#define PATH_WIDTH 4.0f

// The cloud sheet covers the lower cliff; keep it below the playable plateau.
#define CLIFF_DEPTH 8.0f
#define CLOUD_SEA_Y -3.5f

#define ROCK_COUNT 6
static const MapRockPlacement kRocks[ROCK_COUNT] = {
    {{15.0f, 0.0f, 10.0f}, 0.6f, 0.5f, 20.0f},
    {{30.0f, 0.0f, 60.0f}, 0.9f, 0.7f, 100.0f},
    {{70.0f, 0.0f, 15.0f}, 0.5f, 0.45f, 200.0f},
    {{85.0f, 0.0f, 55.0f}, 1.1f, 0.8f, 60.0f},
    {{45.0f, 0.0f, 65.0f}, 0.7f, 0.55f, 320.0f},
    {{20.0f, 0.0f, 45.0f}, 0.8f, 0.6f, 150.0f},
};

static MapGroundSurface s_ground;
static MapStripSurface s_path;
static MapRockSet s_rocks;         // decorative scattered rocks — prop_lit, shadow on
static MapSkyDome s_sky;
static MapCloudSea s_cloudSea;
static bool s_ready = false;

void InitVerdantPathMap(void)
{
    // 1. Lighting/fog — section 4
    Environment_SetAmbientColor((Color){60, 65, 85, 255});
    Environment_SetSunColor((Color){200, 205, 220, 255});
    Environment_SetSunDirection((Vector3){0.5f, -0.8f, -0.3f});
    Environment_SetShadowColor((Color){10, 10, 15, 150});

    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){40, 45, 60, 255};
    fog.start = 60.0f;
    fog.end = 140.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);

    // 2. Floating-island ground — heightmap generated with (keep the size small, see the script's docstring):
    //    python3 scripts/generate_island_heightmap.py assets/heightmaps/<map_name>_island.png 64 <seed>
    s_ground = MapProp_CreateGroundHeightmap("assets/heightmaps/verdant_path_island.png",
                                    MAP_WIDTH, MAP_DEPTH, CLIFF_DEPTH, 12.0f,
                                    "assets/textures/grass_ground_diffuse.png",
                                    "assets/textures/grass_ground_diffuse.png",
                                    "assets/textures/dirt_diffuse.png");

    // 3. Stone path — path_blend.fs handles its own lighting/edge, normal/roughness unused (NULL)
    s_path = MapProp_CreateStrip(PATH_LENGTH, PATH_WIDTH, 2.0f,
                                 "assets/textures/stone_path_diffuse.png",
                                 NULL, NULL);

    // 4. Scattered rocks — 1 model, many positions (section 11's "1 model, many draw calls" principle)
    s_rocks = MapProp_CreateRocks("assets/textures/rock_diffuse.png",
                                  "assets/textures/rock_normal.png",
                                  "assets/textures/rock_roughness.png");

    // Shared floating-plain perimeter, independent of biome materials.
    s_cloudSea = MapProp_CreateCloudSea(MAP_WIDTH + 300.0f, MAP_DEPTH + 300.0f, 50.0f);
    MapBoundaryMistStyle rim = {1.2f, 4.0f, 0.5f, 2.0f, 0.35f};
    if (!MapProp_SetCloudSeaGroundBoundary(&s_cloudSea, &s_ground, -0.35f, &rim))
        TraceLog(LOG_WARNING, "Boundary mist bake failed");
    s_sky = MapProp_CreateSkyDome();

    s_ready = true;
}

void DrawVerdantPathMap(void)
{
    if (!s_ready) return;

    PropLit_UpdateLighting(); // required every frame — s_rocks uses prop_lit (all 3 paths)

    MapProp_DrawGround(&s_ground, kMapCenter);
    MapProp_DrawStrip(&s_path, kMapCenter, 0.01f); // small yOffset avoids z-fighting with the ground
    MapProp_DrawRocks(&s_rocks, kRocks, ROCK_COUNT, true);
    MapProp_DrawSkyDome(&s_sky);
    MapProp_DrawCloudSea(&s_cloudSea, kMapCenter, CLOUD_SEA_Y);
}

void DrawTransparentVerdantPathMap(void)
{
    if (s_ready) MapProp_DrawIslandMist(&s_cloudSea, kMapCenter);
}

void UnloadVerdantPathMap(void)
{
    MapProp_UnloadGround(&s_ground);
    MapProp_UnloadStrip(&s_path);
    MapProp_UnloadRocks(&s_rocks);
    MapProp_UnloadCloudSea(&s_cloudSea);
    MapProp_UnloadSkyDome(&s_sky);
    s_ready = false;
}
```

Declare `Unload{Prefix}Map` in the map header so map switching releases its models. `Update{Prefix}Map` is optional for a static map; cloud animation uses engine time.

If a map needs something the toolkit doesn't yet have (bushes, flower carpets, lakes, hilly terrain...) — add a new function to `maps/toolkit/map_props.h`/`.c` following the `Create*`/`Draw*`/`Unload*` pattern above, or fall back to manual code in sections 8-12 below for that specific piece of the map.

---

## 6b. Sample Source Skeleton — Hand-Drawn With `rlgl` (Advanced / Toolkit Not Yet Supported)

Below is a complete example of a `.c` file for a Desert Lava themed map, drawn entirely by hand with `rlgl` — used when you need an effect the toolkit doesn't have yet (a rippling lava lake, a background moon...), not the default way to build ground/paths/rocks (already covered in section 6):

```c
#include "desert_lava.h"
#include "raylib.h"
#include "rlgl.h"
#include "environment/environment_system.h"
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// Stores animation time (rolling water/lava waves)
static float s_lavaTime = 0.0f;

void InitDesertLavaMap(void) {
    // 1. Configure the lava-night atmosphere (deep blue-black ambient, pale gold moonlight)
    Environment_SetAmbientColor((Color){ 15, 10, 20, 255 }); 
    Environment_SetSunColor((Color){ 90, 70, 50, 255 });    
    Environment_SetSunDirection((Vector3){ 0.5f, -0.8f, -0.3f }); 
    Environment_SetShadowColor((Color){ 5, 2, 8, 200 });

    // 2. Set up dark red fog rising from the magma
    EnvFogConfig fog = {0};
    fog.enabled = true;
    fog.color = (Color){ 25, 10, 10, 255 }; 
    fog.start = 700.0f;
    fog.end = 2000.0f;
    fog.density = 1.0f;
    Environment_SetFogConfig(fog);
}

void DrawDesertLavaMap(void) {
    Vector3 center = { 600.0f, 0.0f, 440.0f };
    float radius = 1805.0f;
    float poolRadius = 300.0f;

    // --- STEP 1: DRAW THE FLOATING SAND GROUND ---
    rlDisableBackfaceCulling();
    rlBegin(RL_TRIANGLES);
    Color cSandCenter = GetColor(0x3A2518FF); // Nighttime sand color
    Color cSandEdge = GetColor(0x1A0F0AFF);   // Darkens toward the edge
    int segments = 64;
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        Vector3 p1 = { center.x + cosf(a1) * radius, center.y - 0.1f, center.z + sinf(a1) * radius };
        Vector3 p2 = { center.x + cosf(a2) * radius, center.y - 0.1f, center.z + sinf(a2) * radius };
        
        rlColor4ub(cSandCenter.r, cSandCenter.g, cSandCenter.b, 255);
        rlVertex3f(center.x, center.y - 0.1f, center.z);
        rlColor4ub(cSandEdge.r, cSandEdge.g, cSandEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cSandEdge.r, cSandEdge.g, cSandEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();

    // --- STEP 2: DRAW THE LAVA LAKE IN THE MIDDLE (FULLY OPAQUE, ALPHA = 255) ---
    rlBegin(RL_TRIANGLES);
    Color cLavaCenter = GetColor(0xFF5500FF); // Bright orange at the center
    Color cLavaEdge = GetColor(0x8B0000FF);   // Deep red at the lake's edge
    for (int i = 0; i < segments; i++) {
        float a1 = ((float)i / segments) * 2.0f * PI;
        float a2 = ((float)(i + 1) / segments) * 2.0f * PI;
        // Gentle rippling lava wave animation
        float w1 = sinf(a1 * 6.0f + s_lavaTime) * 8.0f;
        float w2 = sinf(a2 * 6.0f + s_lavaTime) * 8.0f;
        Vector3 p1 = { center.x + cosf(a1) * (poolRadius + w1), center.y, center.z + sinf(a1) * (poolRadius + w1) };
        Vector3 p2 = { center.x + cosf(a2) * (poolRadius + w2), center.y, center.z + sinf(a2) * (poolRadius + w2) };
        
        rlColor4ub(cLavaCenter.r, cLavaCenter.g, cLavaCenter.b, 255);
        rlVertex3f(center.x, center.y, center.z);
        rlColor4ub(cLavaEdge.r, cLavaEdge.g, cLavaEdge.b, 255);
        rlVertex3f(p2.x, p2.y, p2.z);
        rlColor4ub(cLavaEdge.r, cLavaEdge.g, cLavaEdge.b, 255);
        rlVertex3f(p1.x, p1.y, p1.z);
    }
    rlEnd();
    rlEnableBackfaceCulling();

    // --- STEP 3: DRAW STATIC OBJECTS & FAKE SHADOWS ---
    Vector3 pillarPos = { center.x + 350.0f, center.y, center.z - 250.0f };
    float pHeight = 80.0f;
    float pRadius = 20.0f;
    // 1. Draw the fake shadow first
    Environment_DrawSmartShadow(pillarPos, ENV_SHAPE_CYLINDER, pRadius, pHeight);
    // 2. Draw a low-poly octagonal-prism stone pillar (segments = 8)
    DrawCylinder((Vector3){pillarPos.x, pillarPos.y + pHeight * 0.5f, pillarPos.z}, pRadius, pRadius, pHeight, 8, GetColor(0x2D1E18FF));
    DrawCylinderWires((Vector3){pillarPos.x, pillarPos.y + pHeight * 0.5f, pillarPos.z}, pRadius, pRadius, pHeight, 8, GetColor(0x4A352BFF));

    // --- STEP 4: DRAW THE DISTANT MOON (Blood Moon) ---
    Vector3 moonPos = { center.x - 600.0f, 300.0f, center.z - 1200.0f };
    rlDisableLighting();
    DrawSphere(moonPos, 150.0f, GetColor(0xFF4400FF)); // Vivid blood moon
    rlEnableLighting();
}

void UpdateDesertLavaMap(float dt) {
    // Advance the magma's animation time
    s_lavaTime += dt * 1.5f;
}

void UnloadDesertLavaMap(void) {
    // Free resources (if any external textures were loaded)
}
```

---

## 7. Steps To Create & Ship A Map (For AI/Dev)

To add a new map, follow these steps:

1. **Step 1:** Create a new folder matching the map name under `maps/worlds/` (e.g. `maps/worlds/desert_lava/`).
2. **Step 2:** Create `.h` and `.c` files in that folder — prefer the Toolkit skeleton in section 6; only use the manual `rlgl` skeleton in section 6b for whatever the toolkit doesn't support yet.
3. **Step 3:** Run `make` at the project root in a terminal.
   - The CMake build will automatically run `generate_map_registry.py` to detect the new map, generate registration code into `core/maps_generated.h`, and link it into the game.
4. **Verify:** Launch the game (`./wuxing`), press **`K`** to cycle through maps and check how your new map renders!

---

## 8. Loading & Placing 3D Models In A Map

If you have 3D model files (e.g. bamboo `.obj`, a house `.gltf` with textures), bring them into a map with the following process:

### Step 1: Store the model files
Create a storage folder in the project, e.g.:
- Model: `assets/models/bamboo.obj` (or `.gltf`)
- Accompanying texture: `assets/textures/bamboo_diffuse.png`

### Step 2: Declare and load in the C file
Declare a `static Model` variable at the top of your map's `.c` file to hold the VRAM-loaded resource, load it in `Init`, and free it in `Unload`.

```c
#include "desert_lava.h"
#include "raylib.h"
#include "environment/environment_system.h"

static Model s_bambooModel;
static bool s_bambooLoaded = false;

void InitDesertLavaMap(void) {
    // Load the 3D model from assets
    s_bambooModel = LoadModel("assets/models/bamboo.obj");
    
    // If the model uses an external texture, load it and assign it into the model's material
    if (s_bambooModel.meshCount > 0) {
        Texture2D tex = LoadTexture("assets/textures/bamboo_diffuse.png");
        s_bambooModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    }
    s_bambooLoaded = true;
    
    // Other environment configuration...
}

void DrawDesertLavaMap(void) {
    if (s_bambooLoaded) {
        Vector3 treePos = { 500.0f, 0.0f, 300.0f };
        
        // 1. Draw the fake shadow flush to the ground
        Environment_DrawSmartShadow(treePos, ENV_SHAPE_CYLINDER, 15.0f, 60.0f);
        
        // 2. Draw the 3D bamboo model (use DrawModelEx to support rotation/scale)
        Vector3 rotationAxis = { 0.0f, 1.0f, 0.0f }; // Rotate around the vertical Y axis
        float rotationAngle = 45.0f; // Rotate 45 degrees
        Vector3 scale = { 1.5f, 1.5f, 1.5f }; // Scale up
        
        DrawModelEx(s_bambooModel, treePos, rotationAxis, rotationAngle, scale, WHITE);
    }
}

void UnloadDesertLavaMap(void) {
    if (s_bambooLoaded) {
        // Free the texture assigned to the material first
        UnloadTexture(s_bambooModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture);
        // Free the 3D model from VRAM
        UnloadModel(s_bambooModel);
        s_bambooLoaded = false;
    }
}
```

---

## 9. Uneven Terrain (Terrain & Heightmap) Design & Physics Limits

To create hilly terrain, slopes, or pits, you need to keep the distinction between **visual rendering (Graphics)** and **movement physics/collision (Physics/Collision)** of the current Engine in mind:

### A. Current Physics Behavior (Core's limitation)
Currently, the character/monster core physics in `sandbox_core.c` operates under these assumptions:
*   The arena floor is by default perfectly flat at height **`Y = 0.0f`**.
*   Elevated platforms are declared statically via a Pillars array (`pillars`): when a character jumps onto a pillar's top, their standing ground height (`currentGroundY`) is only raised to that pillar's height.
*   *Note:* A map plugin only decides the **graphics**. A map cannot change the Core's collision algorithm on its own unless the Core is updated with a Heightmap API.

### B. Solution 1: Purely Visual Uneven Terrain (Visual-only Hills)
If you only want the player to move on a flat `Y = 0` surface while the visuals show bumpy terrain:
1. Draw dirt mounds by scattering half-buried spheres (`DrawSphere`) or boxes (`DrawCube`).
2. Or load a rough 3D terrain mesh (Heightmap Mesh) drawn overlapping the `Y = 0` elevation.
*Advantage:* Extremely lightweight, easy to draw, no risk of the character getting stuck. The character glides smoothly through/over the gentle bumps.

### C. Solution 2: Creating Pits/Chasms (Holes & Cliffs)
To create sinkholes where a player **falling in gets pulled down by gravity into the abyss and dies**:
1. In the `Draw` function, draw terrain with holes left out (don't draw polygon mesh in that area, exposing dark empty space).
2. Since the Core currently only checks for ring-out when the player exceeds `arenaRadius` (`1800.0f` from the center `600, 440`), if you want lethal holes *in the middle* of a map, you'll need to coordinate adding circular collision-check logic for that hole in the Core's physics file (or ask the Core Agent to set up an additional no-go/lethal zone, `NAT_CLIFF`).

---

## 10. Loading Textures For A Map (Rock, Sand, Water)

> Ground/path/rocks already have `MapProp_CreateGround`/`CreateStrip`/`CreateRocks` (section 1b) handling texture loading+assignment automatically — only read this section when you need a texture for some other prop type (section 8's free-form 3D model) that the toolkit doesn't yet have a dedicated function for.

To apply an image texture (e.g. rock texture `stone.png`, grass/dirt `grass.png`), there are two approaches depending on how you draw the terrain:

### A. Assigning a texture to an externally loaded 3D Model
```c
static Model s_rockModel;
static bool s_rockLoaded = false;

void InitMap(void) {
    s_rockModel = LoadModel("assets/models/rock.obj");
    
    // Load the rock texture from an image file
    Texture2D rockTex = LoadTexture("assets/textures/stone_diffuse.png");
    
    // Assign the texture to material 0's main color channel (Diffuse Map)
    s_rockModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = rockTex;
    s_rockLoaded = true;
}

void DrawMap(void) {
    if (s_rockLoaded) {
        DrawModel(s_rockModel, (Vector3){ 600.0f, 0.0f, 440.0f }, 1.0f, WHITE);
    }
}

void UnloadMap(void) {
    if (s_rockLoaded) {
        // Must unload the texture before unloading the model to avoid a VRAM leak
        UnloadTexture(s_bambooModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture);
        UnloadModel(s_rockModel);
    }
}
```

### B. Applying a texture directly to hand-drawn `rlgl` geometry (e.g. the ground)
If you draw the arena floor with `rlgl`, you need to bind the texture ID and assign UV coordinates (`rlTexCoord2f`) per vertex:
```c
static Texture2D s_groundTex;

void InitMap(void) {
    s_groundTex = LoadTexture("assets/textures/grass_texture.png");
    // Enable texture repeat (Wrap Mode) if you want the texture to tile over a large area
    SetTextureWrap(s_groundTex, TEXTURE_WRAP_REPEAT);
}

void DrawMap(void) {
    rlSetTexture(s_groundTex.id); // Bind the texture
    rlBegin(RL_TRIANGLES);
        // Vertex 1
        rlTexCoord2f(0.0f, 0.0f); // UV coordinate (0,0)
        rlVertex3f(500.0f, 0.0f, 300.0f);
        
        // Vertex 2
        rlTexCoord2f(1.0f, 0.0f); // UV coordinate (1,0)
        rlVertex3f(600.0f, 0.0f, 300.0f);
        
        // Vertex 3
        rlTexCoord2f(0.5f, 1.0f); // UV coordinate (0.5,1)
        rlVertex3f(550.0f, 0.0f, 400.0f);
    rlEnd();
    rlSetTexture(0); // Unbind the texture when done
}
```

---

## 11. Memory Optimization: Drawing An Entire Forest/Flower Carpet From 1 Model

> For scattered rocks, `MapProp_CreateRocks`/`MapProp_DrawRocks` (section 1b) already do this correctly for you — only read the rest of this section when scattering a different prop type (bamboo clumps, flower carpets...) with no dedicated toolkit function yet.

**Absolutely do NOT** call `LoadModel` once per bamboo stalk or per flower. This floods VRAM and crashes the game.
*   **Solution:** Load the model **exactly once** in `Init` to cache it in memory, then in the `Draw` function use a `for` loop to draw that same model at many different positions with randomized scale and rotation to build a forest or flower carpet.

### Sample code building a bamboo forest from 1 model:
```c
#define MAX_BAMBOO_TREES 30

static Model s_bambooModel;
static Vector3 s_bambooPositions[MAX_BAMBOO_TREES];
static float s_bambooRotations[MAX_BAMBOO_TREES];
static float s_bambooScales[MAX_BAMBOO_TREES];
static bool s_modelReady = false;

void InitMap(void) {
    s_bambooModel = LoadModel("assets/models/bamboo.obj");
    s_modelReady = true;

    // Randomly generate the bamboo forest's positions once here (do not generate inside Draw — causes stutter)
    for (int i = 0; i < MAX_BAMBOO_TREES; i++) {
        // Generate coordinates around the map's border
        float angle = ((float)i / MAX_BAMBOO_TREES) * 2.0f * PI;
        float radius = 1000.0f + (float)GetRandomValue(-200, 200); 
        s_bambooPositions[i] = (Vector3){
            600.0f + cosf(angle) * radius,
            0.0f,
            440.0f + sinf(angle) * radius
        };
        
        s_bambooRotations[i] = (float)GetRandomValue(0, 360);
        s_bambooScales[i] = 1.0f + (float)GetRandomValue(-20, 20) / 100.0f; // Scale range 0.8f to 1.2f
    }
}

void DrawMap(void) {
    if (!s_modelReady) return;

    Vector3 rotationAxis = { 0.0f, 1.0f, 0.0f }; // Rotate around the Y axis

    // Loop drawing the whole forest
    for (int i = 0; i < MAX_BAMBOO_TREES; i++) {
        // 1. Draw the shadow matching that tree's position
        Environment_DrawSmartShadow(s_bambooPositions[i], ENV_SHAPE_CYLINDER, 15.0f, 80.0f);
        
        // 2. Draw the bamboo model at that position
        Vector3 scaleVec = { s_bambooScales[i], s_bambooScales[i], s_bambooScales[i] };
        DrawModelEx(s_bambooModel, s_bambooPositions[i], rotationAxis, s_bambooRotations[i], scaleVec, WHITE);
    }
}

void UnloadMap(void) {
    if (s_modelReady) {
        UnloadModel(s_bambooModel);
        s_modelReady = false;
    }
}
```
*Similarly, for a grass or flower carpet, load just one small flower/blade-of-grass model, then loop generating hundreds of nearby random points to cover the arena surface in green.*

---

## 12. Snapping Objects To Uneven Terrain (Snapping to Terrain)

When a map is no longer flat but has hilly terrain (Heightmap), you **cannot** leave a tree's or house's position fixed at `Y = 0.0f`, since they'd float in midair or get buried underground.

### Solution: Query height from the Heightmap image (CPU Height Query)
We use a C helper function that reads a Heightmap image's pixel value (a grayscale image representing height: white is a mountain peak, black is a valley) at coordinate `(X, Z)` and converts it into an actual `Y` height.

#### Height-computing helper function:
```c
// Gets the Y height at a world XZ coordinate based on a Heightmap image
float GetHeightmapHeight(Image heightmap, Vector3 terrainSize, Vector3 terrainCenter, float x, float z) {
    float halfWidth = terrainSize.x / 2.0f;
    float halfLength = terrainSize.z / 2.0f;
    
    // 1. Convert World XZ coordinates to image Pixel (U, V) coordinates
    float normX = (x - (terrainCenter.x - halfWidth)) / terrainSize.x;
    float normZ = (z - (terrainCenter.z - halfLength)) / terrainSize.z;
    
    int pixelX = (int)(normX * heightmap.width);
    int pixelZ = (int)(normZ * heightmap.height);
    
    // 2. Clamp the pixel coordinates within the image bounds
    if (pixelX < 0) pixelX = 0;
    if (pixelX >= heightmap.width) pixelX = heightmap.width - 1;
    if (pixelZ < 0) pixelZ = 0;
    if (pixelZ >= heightmap.height) pixelZ = heightmap.height - 1;
    
    // 3. Read that pixel's color (assuming a grayscale image, take the Red channel value 0-255)
    Color pixel = GetImageColor(heightmap, pixelX, pixelZ);
    
    // 4. Convert the 0-255 value into a Y elevation relative to the terrain's max height (terrainSize.y)
    float height = ((float)pixel.r / 255.0f) * terrainSize.y;
    return height;
}
```

#### How to apply it in a Map's code:
```c
static Model s_terrainModel;
static Model s_treeModel;
static Image s_heightmapImage;
static Vector3 s_terrainSize = { 1800.0f, 150.0f, 1800.0f }; // Width 1800, max height 150, depth 1800
static Vector3 s_center = { 600.0f, 0.0f, 440.0f };

// Random positions for 10 bamboo trees
static Vector3 s_treePositions[10];

void InitMap(void) {
    // 1. Load the terrain mesh from the Heightmap image to render it in 3D
    s_heightmapImage = LoadImage("assets/heightmaps/terrain_height.png");
    Mesh mesh = GenMeshHeightmap(s_heightmapImage, s_terrainSize);
    s_terrainModel = LoadModelFromMesh(mesh);
    s_terrainModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = LoadTexture("assets/textures/grass_diffuse.png");
    
    s_treeModel = LoadModel("assets/models/tree.obj");

    // 2. Scatter trees, automatically snapping them to the hilly terrain
    for (int i = 0; i < 10; i++) {
        float x = s_center.x + (float)GetRandomValue(-600, 600);
        float z = s_center.z + (float)GetRandomValue(-600, 600);
        
        // Query the heightmap image to get the Y value
        float y = GetHeightmapHeight(s_heightmapImage, s_terrainSize, s_center, x, z);
        
        s_treePositions[i] = (Vector3){ x, y, z };
    }
}

void DrawMap(void) {
    // Draw the hilly terrain
    DrawModel(s_terrainModel, (Vector3){ s_center.x - s_terrainSize.x/2.0f, 0.0f, s_center.z - s_terrainSize.z/2.0f }, 1.0f, WHITE);
    
    // Draw the trees, now snapped to the correct height
    for (int i = 0; i < 10; i++) {
        Environment_DrawSmartShadow(s_treePositions[i], ENV_SHAPE_CYLINDER, 15.0f, 50.0f);
        DrawModel(s_treeModel, s_treePositions[i], 1.0f, WHITE);
    }
}

void UnloadMap(void) {
    UnloadImage(s_heightmapImage); // Free the CPU-side image data
    UnloadModel(s_terrainModel);
    UnloadModel(s_treeModel);
}
```

---

## 13. Virtual Trigger Zones (Elemental Zones) — ../../ROADMAP.md Module 2

Map = pure data: each map only declares the **POSITION** of elemental zones; the
modifier rule (Water = -50% cooldown in a River...) lives centrally in
`game/game_rules.h` (see `game/docs/API.md` §3), NOT in the map.

API (part of `core/map_manager.h`, state of the currently active map):

```c
typedef enum { NAT_NONE = 0, NAT_RIVER, NAT_FOREST, NAT_DESERT_ZONE } NatureZoneType;

typedef struct {
    NatureZoneType type;
    Vector3 center;   // flush to the ground, y = 0
    float   radius;   // XZ distance check (same as Entity_GetNearbyTargets)
} MapZone;

#define MAX_MAP_ZONES 16

void MapManager_SetZones(const MapZone *zones, int count); // call in the map's Init
int            Map_GetZoneCount(void);
const MapZone *Map_GetZone(int index);        // NULL if index is invalid
NatureZoneType Map_QueryZoneAt(Vector3 pos);  // NAT_NONE if outside every zone
void MapManager_DebugDrawZones(void);         // debug rings colored by type (inside BeginMode3D)
```

Rules:
- Zones are **automatically cleared when the map changes** (`MapManager_SetActiveIndex`
  clears before calling the new map's `Init`) — a map with no zones doesn't need
  to call anything.
- A map MUST draw a **visual cue** matching each zone's position (No Tutorial —
  players discover it themselves). Example: `DEFAULT_ARENA` draws 3 self-lit
  vertex-color gradient discs (water blue / forest green / sand yellow, alpha 255 —
  see `maps/worlds/default_arena/default_arena.c`; same technique as a floor
  plate, since a lit material would go black in the night arena).
- Adding a new map with a zone = add a `static const MapZone zones[]` array +
  call `MapManager_SetZones` in `Init{Prefix}Map` + draw the visual cue. No
  engine change needed.

Current consumers: `game/game_screen.c` (applies the rule to the player every frame),
`combat/combat.c` (Earth projectiles in `NAT_FOREST` take -50% damage).
Autotest: `map_trigger_zones` in `main.c`.

`MapSkyDome` owns its model and borrows its cached ResourceManager shader. Draw opaque terrain/props first, then sky and the cloud sheet. Clouds retain a flat, two-triangle opaque plane and write their native surface depth; placing the sheet above the cliff bottom covers submerged terrain consistently for depth-based fog. Cloud world coordinates use explicit translation because the engine's `matModel` includes the view transform.

`MapProp_SetCloudSeaBoundaryContours` bakes one indexed, textured mist ribbon from closed map-local loops. Plateau interior must be on the left of each directed edge: outer loops are counterclockwise in XZ, holes clockwise. Concave contours, disjoint islands, holes, and varying contour heights are supported. Repeated closing endpoints are accepted. Input points are copied into geometry, so the caller can free them. Miter lengths are bounded at sharp corners. Style distances are metres; opacity is 0..1. Invalid input/allocation failure retains the previous model. Zero contours clears it. The 16-bit indexed mesh supports at most 32,760 contour points.

`MapProp_SetCloudSeaGroundBoundary` derives outer contours from a horizontal slice of actual terrain triangles, including indexed and duplicated unindexed meshes. `contourHeight` is relative to the map's draw center Y. Endpoint hashing joins the segments; interior depression loops (lakes) are excluded. Derived contours are simplified to a 0.10m tolerance; terrain/collision geometry is unchanged. Flat meshes without a descending rim should supply explicit contours instead. Non-manifold/open slices or more than 8,192 slice endpoints return false. These operations happen once at initialization.

`MapProp_DrawIslandMist` draws the cached ribbon at the same world center as its terrain, in the transparent pass. It uses one 256px baked periodic RGBA strip, one default-shader texture sample, and no simulation, searches, allocations, noise evaluation, or vertex updates per frame. Depth testing remains active; depth writes and culling are disabled with flushed/restored state. `MapProp_UnloadCloudSea` unloads both its models; its textures are borrowed. Regenerate the strip with `/usr/bin/python3 maps/toolkit/generate_cloud_mist.py` (NumPy/Pillow). `MapIslandBoundary` and its rectangle setters remain compatibility descriptors; the cloud setter no longer builds a raised bank. New map boundaries should use the contour API.

`WUXING_NO_CLOUD_SEA=1` suppresses the Verdant Path background sheet for same-binary frame-cost comparisons; it is a measurement switch, not a quality preset.

Cloud noise uses a separate ResourceManager mipmapped trilinear/repeat variant. Terrain, strips, rock materials, and petals request immutable mipmapped variants; anisotropic requests first establish trilinear filtering so unsupported anisotropy still selects mip levels. Allocation failure retains legacy load-time configuration. Draw opaque terrain first, then sky and the cloud sheet. The opaque sheet tests against the foreground and replaces only deeper surfaces with its native cloud-surface depth. The transparent rim mist preserves depth. Sky/cloud/terrain uniforms must be uploaded inside their active shader scopes; uniform locations are cached at initialization. Unload each map model, leaving cached shaders/textures to ResourceManager.

## Patch Log

| Date | Editor (human/AI) | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-10-09 | Codex | Canonical far-blade shape and visible/shadow validation | maps/toolkit/shaders/nature_parametric.glsl; maps/toolkit/map_props_nature.inl; maps/tests/test_meadow_parametric.py; maps/tests/test_meadow_world_space.py | Ground-truth |
| 2026-10-09 | Codex | Shared meadow palette, hemispheric canopy lighting and terrain relief | maps/toolkit/meadow_palette.h; shaders/meadow_palette.glsl; shaders/nature_surface.glsl; scripts/generate_island_heightmap.py; maps/tests/test_verdant_heightmap.py | Ground-truth |
| 2026-10-06 | Codex | Typed rooted receiver channels, material properties and drag ownership | maps/toolkit/map_props_nature.inl; maps/tests/test_nature_local_wind.py | Ground-truth |
| 2026-10-06 | Codex | Rooted Newton-force spring response, ground-relative local wind and periodic diagnostics | maps/toolkit/map_props_nature.inl; maps/tests/test_nature_local_wind.py | Ground-truth |
| 2026-10-05 | Codex | Arbitrary contour mist, terrain extraction, flat background plane | maps/toolkit/map_props_boundary_contour.inl; map_props.h; tests/test_island_boundary.py | Ground-truth |
| 2026-10-05 | Codex | Shared floating-plain perimeter, cached sky shader, background depth policy, baked rim wisps | maps/toolkit/map_props.h; map_props_boundary.inl; map_props_cloud.inl; shaders/cloud_sea.vs | Ground-truth |
| 2026-10-05 | Codex | Cloud minification filtering | maps/toolkit/map_props_cloud.inl; core/resource_manager.h | Ground-truth |
| 2026-10-04 | Codex | Opt-in sky, per-cloud tiling, active water/cloud shader scopes and metric heightmap | `maps/toolkit/map_props.h`, `maps/toolkit/map_props_sky.inl`, `maps/toolkit/map_props_cloud.inl`, `scripts/generate_island_heightmap.py` | Ground-truth |
| 2026-10-03 | Codex | Translation-only parametric meadow world-space contract | `maps/toolkit/map_props_meadow_parametric.inl`, `maps/toolkit/shaders/nature_lit_parametric.vs`, `maps/toolkit/shaders/nature_shadow_parametric.vs`, `maps/tests/test_meadow_world_space.py` | Ground-truth |
| 2026-10-03 | Codex | Prop material coordinate-space and active-camera contract | `maps/toolkit/prop_lit.c`, `maps/toolkit/shaders/prop_lit.fs`, `maps/tests/test_prop_lighting_space.py` | Ground-truth |
| 2026-10-03 | Codex | Exact water wake upload cache and lifecycle | `maps/toolkit/map_props_nature.inl`, `maps/toolkit/map_props.h`, `maps/tests/test_water_wave_upload.py` | Ground-truth |
| 2026-10-02 | Codex | Shared grass atlas, optional compact submissions and ordering, botanical variation, derived terrain relief | `maps/toolkit/map_props_meadow_parametric.inl`, `maps/toolkit/map_props_nature.inl`, `maps/toolkit/shaders/ground_splat.fs` | Ground-truth |
| 2026-10-02 | Codex | Per-tuft visible LOD and narrower distant fog | `maps/toolkit/shaders/nature_parametric.glsl`, `maps/worlds/verdant_path/verdant_path.c` | Ground-truth |
| 2026-09-26 | Codex | Reusable meadow shadow contract | `maps/toolkit/map_props_nature.inl`, `environment/env_shadow.c` | Ground-truth |
| 2026-09-26 | Codex | Radial water depth and shore transparency contract | `maps/toolkit/map_props_nature.inl`, `maps/toolkit/shaders/water_surface.fs` | Ground-truth |
