# Core Issues / Backlog

Tasks for Core Agent — assign when ready.

## 1. `Cast[Name]Skill` signature inconsistent across CORE_API.md §4 skeleton examples

Found while building the first real skill (Wood "Vine Snare") against the documented pipeline: the skeleton example code in §4 declares `Cast[Name]Skill` as `(Vector3 spawnPos, SkillParams params)`, but the required lifecycle header template (earlier in §4) mandates `(Vector3 startPos, Vector3 target, SkillParams params)`. Skills Agent followed the header template (required for auto-registration via `scripts/generate_registry.py`'s signature matching) and worked around the mismatch correctly this time, but it's a real trap for a less careful future implementation — two parts of the same document disagree on a required signature.

~~Action: audit all four skeleton examples in §4 (Flying, Anchored-Single-Point, Anchored-Along-Path, Entity-Attached) for signature consistency against the header template immediately above them. Fix any that drifted. Use `Edit` with precise `old_string` — `grep -n "^### \|^## " CORE_API.md` first, it's 1200+ lines, never `Write` it wholesale.~~

**RESOLVED 2026-06-30**: Audited all four §4 skeletons. Only Anchored-Single-Point (Ground-Rising) had drifted (`CastExampleRisingSkill(Vector3 spawnPos, SkillParams params)`); fixed to `(Vector3 startPos, Vector3 target, SkillParams params)`, with the rising mesh now spawning at `target` and the cast-windup effect playing at `startPos`. Flying-Projectile, Anchored-Along-Path, and Entity-Attached already matched the template.

## 2. Anchored-Single-Point skeleton's mesh example produces a "robotic" shape — contradicts the document's own Aesthetic Laws

Also found building "Vine Snare": the skeleton drew its rising mesh with a single `DrawCoreCylinder(bottom, top, radiusBottom, radiusTop, slices, color)` call — different top/bottom radii, no segmentation, no jitter. Visually this is a plain truncated cone (frustum), not remotely organic. The Skills Agent followed the skeleton example faithfully and produced exactly this — a straight tapered cylinder for a skill that was supposed to be a vine wrapping around a target.

This directly contradicts §12 "Critical Aesthetic Laws (Anti-Robotic Design)" in the same document:
- §12.2 Perpendicular Jitter — the single-cylinder example has none
- §12.3 Instance Randomization — no randomized scale/yaw/pitch anywhere in the skeleton
- The skeleton is "technically compliant" (uses `DrawCoreCylinder`, an approved procedural mesh function, not a banned raylib primitive) but still reads as robotic because it's used as a single undecorated shape

Impact: any AI agent copying this skeleton verbatim for a new skill — not just Vine Snare — will produce a similarly "robotic" look, even though it follows every individual rule in isolation. The skeleton needs to actually demonstrate the Aesthetic Laws it's sitting next to in the same document, not just satisfy the "no banned primitives" rule.

~~Action: revise the Anchored-Single-Point skeleton's example mesh-drawing code in §4 to show a multi-segment, jittered approach...~~

**RESOLVED 2026-06-30**: `DrawExampleRisingSkill` now stacks `MESH_SEGMENTS` (5) short `DrawCoreCylinder` segments instead of one straight frustum. Each segment is perpendicular-jittered using the §12.2 `perp`/`jitter` pattern (scaled down to ±3.0f for this ~80f-tall mesh vs. the ±12.0f reference value, since the reference is tuned for longer path layouts). Added per-instance `yawOffset` (random jitter axis direction) and `scaleVariance` (85-115%) fields per §12.3, applied to segment height/radius. Added a one-sentence prose note before the code pointing back at §12.2/§12.3. State machine and `SmoothStep01`/`Math_Mix` height animation logic unchanged.

## 3. Extend `core/procedural_mesh_utils.h` with new geometry types (wave / curling wave / low-poly rock)

Design discussion (2026-06-30, prompted by planning a future tsunami-style skill): the existing primitive set (`DrawCoreSphere/Cylinder/Cone/Cube/Torus/Prism/Plane*`) plus `ProceduralMesh_BuildTube` (organic tube-along-Bezier, used by `water_stream`) doesn't cover deformable wave surfaces or naturally-faceted rock. Squishing an existing primitive (non-uniform scale on a sphere/cube) was explicitly rejected for anything prominent — it reproduces the same "looks robotic/fake" failure already hit with the Vine Snare skill's first two attempts (a smooth/regular primitive read as mechanical no matter how it's scaled; irregular geometry is what actually reads as natural).

Add three new functions. Priority order below — if time-constrained, items 1–2 are more immediately useful (wave shapes, no current equivalent at all); item 3 only matters for *prominent/large* rocks, small background rubble should keep using squished `DrawCoreCube`/`DrawCoreSphere` + per-instance random scale/rotation (§12.3) rather than a dedicated function — don't over-build for the small-debris case.

### 1. `ProceduralMesh_BuildWavePlane` — flat-ish rippling water surface

A subdivided grid mesh (moderate segment count — low-poly per the project's mobile/Android performance discipline and `WUXING_ART_DIRECTION`, not a dense smooth mesh) where each vertex's height (Y) is displaced by a wave function — combine a few sine waves at different frequencies/phases plus `fbm2`/`vnoise` (already in `core/shaders/common/noise.glsl`, or computed CPU-side if this needs to be CPU geometry rather than GPU-displaced — your call which is more appropriate given how `water_stream` already splits displacement between CPU mesh build and GPU shader, follow that established split rather than inventing a new one) so the ripple pattern doesn't repeat in an obviously regular way (avoid the same "perfectly regular = robotic" trap as the spiral-helix mistake).

```c
typedef struct {
    float wavelength;
    float amplitude;
    Vector3 direction;     // propagation direction, XZ-plane, will be normalized
    float crestSharpness;  // 0 = smooth sine, higher = sharper peaked crests
} WavePlaneConfig;

void ProceduralMesh_BuildWavePlane(Vector3Mesh /* or whatever output struct fits your existing TubeMeshData-style convention */ *out,
    Vector3 center, float width, float length, int segmentsX, int segmentsZ,
    float time, const WavePlaneConfig *cfg);
void ProceduralMesh_DrawWavePlane(/* matching data struct */ *data);
```
(Adapt the exact struct/function shape to match the existing `TubeMeshData`/`ProceduralMesh_BuildTube`/`DrawTube` convention in the same header — consistency with the established pattern matters more than the exact signature suggested here.)

### 2. `ProceduralMesh_BuildCurlingWave` — cresting/curling wave wall (the actual tsunami silhouette)

Different from #1: instead of just height-displacing a flat grid, this sweeps an **open "C"-shaped cross-section profile** (a curl silhouette — base, rising face, overhanging lip) sideways along the wave's width, reusing the same conceptual technique `ProceduralMesh_BuildTube` already uses to sweep a *closed circular* cross-section along a Bezier path. Don't build an unrelated second system — adapt/share the sweep-along-path machinery `BuildTube` already has, with an open curl profile instead of a closed circle, swept along a straight or gently-curved width axis instead of a long path.

```c
typedef struct {
    float curlAmount;   // 0 = flat wall, higher = more overhang at the top lip
    float height;
    float archWidth;     // how far the wave wall extends sideways
} CurlingWaveConfig;

void ProceduralMesh_BuildCurlingWave(/* data struct */ *out, Vector3 baseCenter,
    Vector3 widthDirection, const CurlingWaveConfig *cfg, int profileSegs, int widthSegs);
void ProceduralMesh_DrawCurlingWave(/* data struct */ *data);
```

### 3. `ProceduralMesh_BuildRock` — low-poly faceted rock (prominent/large rocks only)

Standard low-poly-rock technique: start from a base icosphere (or subdivided cube), randomly displace each vertex's distance from center within a jitter range, producing natural angular facets — NOT a squished smooth primitive.

```c
void ProceduralMesh_BuildRock(/* data struct */ *out, Vector3 center, float radius,
    float jitterAmount, int seed, int subdivisions);
void ProceduralMesh_DrawRock(/* data struct */ *data);
```
`seed` should make repeated calls with the same seed produce the same rock shape (deterministic), so a skill can cache/reuse a rock shape across frames without recomputing every draw call if that's more efficient than rebuilding per-frame — use your judgment on whether build-once-cache-in-instance-struct or rebuild-every-draw is more appropriate given the existing `TubeMeshData` convention's own caching approach (check how skills currently call `ProceduralMesh_BuildTube`/`DrawTube` — once per cast and cached, or every frame — and match that).

### Documentation

~~Update `CORE_API.md`'s Procedural Mesh section (`### Procedural Mesh (core/procedural_mesh_utils.h)`) with all three new functions/configs, matching the existing doc style for `TubeMeshConfig`/`ProceduralMesh_BuildTube`.~~

**RESOLVED 2026-06-30**: All three generators added to `core/procedural_mesh_utils.h`/`.c`, `CORE_API.md` updated.

- `ProceduralMesh_BuildWavePlane`/`DrawWavePlane` — grid mesh (`WAVE_PLANE_MAX_SEGMENTS_X/Z` = 24) with 3 layered sine waves (main directional + secondary cross-direction + slow low-freq) plus a deterministic per-vertex hash noise term, all combined CPU-side at Build time (matches `water_stream`'s split — `BuildTube` itself does CPU geometry, GPU shader handles flow texture animation only, not displacement). `crestSharpness` implemented via `sign(s)*|s|^(1/(1+sharpness))` power-reshaping of the main sine, not a literal `noise.glsl` port, since height needs to be CPU-side geometry. Normals via finite-difference between neighboring displaced verts. Output struct named `WavePlaneMeshData` (not the placeholder `Vector3Mesh` from the issue) to match `TubeMeshData` naming.
- `ProceduralMesh_BuildCurlingWave`/`DrawCurlingWave` — sweeps an open arc profile (start angle -90°, smoothstep-eased sweep up to 90°+90°*curlAmount) along `widthDirection`, reusing the Frenet-style frame construction (`up`/`depth`) from `BuildTube`'s `right`/`up`. `CurlingWaveMeshData` (`CURLING_WAVE_MAX_WIDTH_SEGS`=32, `CURLING_WAVE_MAX_PROFILE_SEGS`=16) stores `verts[width][profile]` + per-vertex normals. Small deterministic jitter on the lip avoids a perfectly smooth cast-mold look.
- `ProceduralMesh_BuildRock`/`DrawRock` — icosahedron (12 verts/20 faces) + recursive subdivide (clamped `subdivisions<=2`, ~162 verts, fits `ROCK_MESH_MAX_VERTS`), then per-vertex radial jitter via a deterministic hash PRNG keyed on `seed`+vertex index (same seed -> same rock). Flat-shaded: `RockMeshData` stores per-face normals (not per-vertex) so facets read as angular — subdivision intentionally doesn't weld shared edges, since post-jitter they diverge anyway and welding would defeat the facet look. Build-once-cache convention documented in the function comment (rocks don't animate shape, unlike water's per-frame `BuildTube`/`DrawTube`).

Deviation from the issue's suggested signatures: all `Draw*` functions take an explicit `Color color` parameter (issue's pseudocode omitted it) to match `DrawCoreCube`/`DrawCoreTorus`-style convention of passing color at draw time — `TubeMeshData`/`DrawTube` is the one exception (color comes from a dedicated shader), but these new shapes are meant to also support plain vertex-color rendering.

---

## 4. No longer deferred (2026-06-30 update) — build `BuildShardCluster`, `BuildVortexFunnel`, `BuildFissure` too

User decision: don't wait for a specific skill to need these — add all three now, in the same spirit as items 1–3 above. **Sequencing note: do this AFTER item #3 is fully resolved and merged** — both touch `core/procedural_mesh_utils.h`/`.c`, running them concurrently risks the same kind of file-edit collision seen earlier this session when two agents built/ran `make` on `entities/` in parallel. Assign as a separate follow-up task once item #3's agent reports done.

~~### `ProceduralMesh_BuildShardCluster` — crystal/shard cluster~~

~~Several elongated angular prisms (reuse `DrawCorePrism`-style faceting, or a dedicated low-poly crystal cross-section) radiating outward from a center point or scattered along a path, irregular lengths/angles/thicknesses per instance (same anti-robotic jitter/randomization discipline as everything else this session — don't make them evenly spaced or identically sized). Use case: Metal sword-qi/shard skills, Water ice-shard skills.~~

~~### `ProceduralMesh_BuildVortexFunnel` — spiral wind funnel~~

~~A tapered, twisting funnel shape (wide at top or bottom, narrowing toward the other end) with visible spiral ridges along its surface — for Phong (wind) skills, tornado/cyclone visuals, the Taiji ultimate. Likely reuses the tube-sweep machinery again (a spiral path swept with a widening/narrowing circular cross-section), similar lineage to `BuildCurlingWave`'s reuse of `BuildTube`'s sweep technique — check if it can share code with that rather than being fully separate.~~

~~### `ProceduralMesh_BuildFissure` — 3D jagged ground crack~~

~~A raised/sunken jagged crack mesh along a path (distinct from the existing flat 2D crack decals — this is real 3D geometry with irregular broken-rock edges), for Earth skills (Địa chấn, Thạch shatter-type effects) where a flat decal isn't enough presence. Likely built from a jittered path (reuse `core/path_spline.h`'s `SamplePath` for the crack's centerline, like the Anchored-Along-Path skill skeleton already does) with irregular angular cross-section geometry along it, similar spirit to `BuildShardCluster`'s angular faceting but following a line instead of radiating from a point.~~

~~### Documentation~~

~~Same as item #3 — update `CORE_API.md`'s Procedural Mesh section with all three, matching existing doc style.~~

**RESOLVED 2026-06-30**: All three generators added to `core/procedural_mesh_utils.h`/`.c`, `CORE_API.md` updated.

- `ProceduralMesh_BuildShardCluster`/`DrawShardCluster` — `ShardClusterMeshData` (`SHARD_CLUSTER_MAX_SHARDS`=16, `SHARD_MAX_SIDES`=6) stores per-shard `baseRing`/`tipRing`/`baseNormal` arrays plus `baseCenter`/`tipCenter`. Each shard is a tapered N-sided prism radiating from `origin`, tilted off `mainDirection` within `cfg->spreadAngle` (cone spread), with length/thickness/cross-section-twist all driven by `ProceduralMesh__Noise2` keyed on `seed` + shard index — same deterministic-hash PRNG `BuildRock` uses for jitter, no new randomization scheme invented. `tipSharpness` controls `tipRadius = baseRadius*(1-tipSharpness)`, so 1.0 gives a near-point tip. New code (no Tube/Rock geometry shared directly, only the PRNG helper).
- `ProceduralMesh_BuildVortexFunnel`/`DrawVortexFunnel` — investigated reusing `BuildTube` directly first, per the issue's suggestion; rejected because `BuildTube` requires 4 Bezier control points and Frenet-frame tangent tracking, which is unnecessary overhead for a fixed vertical-axis path with no curvature. Instead wrote a dedicated build loop, but it deliberately mirrors `BuildTube`'s **data layout and loop structure** (`rings[height][radial]`/`normals[height][radial]`, outer loop over the path direction then inner loop over the radial circumference, precomputed `sinPhi`/`cosPhi` tables) — same conventions, smaller/simpler math since the path is straight. `VortexFunnelMeshData` (`VORTEX_FUNNEL_MAX_HEIGHT_SEGS`=32, `VORTEX_FUNNEL_MAX_RADIAL_SEGS`=24). Spiral ridges via `cos(phi*ridgeCount)` radius bump that follows the twist angle so ridges read as spiraling, not static rings. No end caps (tornado silhouette stays open at both ends), unlike Tube.
- `ProceduralMesh_BuildFissure`/`DrawFissure` — centerline rasterized via `core/path_spline.h`'s `SamplePath` (same function the Anchored-Along-Path skeleton uses), as directed — no hand-rolled path sampling. `FissureMeshData` (`FISSURE_MAX_SEGMENTS`=48, `FISSURE_CROSS_VERTS`=5: left edge/left shoulder/bottom/right shoulder/right edge) builds a jagged "V" cross-section per centerline sample, jittered via the same seed-keyed `ProceduralMesh__Noise2` hash used by Rock/ShardCluster (lateral centerline offset, edge width, shoulder/bottom depth all jittered independently). Negative `depth` produces a raised crack instead of sunken.

Deviations from the issue's suggested signatures: `ProceduralMesh_BuildShardCluster` takes an explicit `mainDirection` + `ShardClusterConfig*` (issue's pseudocode only had `spreadRadius`, no direction/config) — needed a reference direction for the spread-cone and a config struct to match the `TubeMeshConfig`/`WavePlaneConfig`-style precedent (`spreadAngle`, `thicknessMin/Max`, `tipSharpness`, `sides`). `ProceduralMesh_BuildVortexFunnel` adds a `time` parameter (issue's pseudocode omitted it) to support an animated spin independent of `twistAmount`, matching `BuildTube`/`BuildWavePlane`'s `time`-driven animation convention; pass `time=0` for a static/cached funnel. All three `Draw*` functions take an explicit `Color color` parameter, consistent with item #3's same deviation.

---
*Logged from first real skill build (Vine Snare) against the pipeline, 2026-06-30.*
