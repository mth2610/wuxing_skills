# Core Engine — Open Issues / Unfinished Work

Tracks work from the "Core API update" task list that is either unfinished
(reverted, needs a fresh approach) or not yet started. See `CORE_API.md` for
the API surface that IS shipped and documented.

---

## Item 3 — Soft Particles (REVERTED, unresolved)

**Status: removed from the codebase.** Multiple debugging passes failed to
get a translucent particle correctly fading at geometry intersections. The
dissolve edge glow and metaballs parts of Item 3 **are** done and kept (see
`CORE_API.md` — Metaballs section, and `fx.glsl`'s `dissolveCalc`).

### What was attempted

A scene-depth-texture system in `core/screen_distort.c` (the module owning
the real 3D-pass render target) to let particle shaders fade their alpha
near intersections instead of hard-clipping:
- `renderTex` rebuilt with a sampleable depth **texture** attachment
  (instead of raylib's default depth renderbuffer).
- A "snapshot" pass each frame copying that depth into a second texture
  (`prevDepthTex`), to avoid sampling a texture that's still bound as the
  active render target (an OpenGL feedback-loop hazard).
- A skill-side helper (`ScreenDistort_BindDepthForSoftParticles`) to bind
  that snapshot to a shader's custom sampler + push near/far/resolution
  uniforms.
- A `soft_particle.glsl` opt-in include with the fade-factor math.

### Why it was reverted

The fade factor (`SoftParticle_Factor`) kept evaluating to exactly `0.0`
(particle invisible) in real on-screen testing, despite several rounds of
fixes that each looked correct in isolation but didn't hold up:

1. **First suspected cause:** stale/uncleared depth buffer in the snapshot
   target failing the depth test during the copy write. Fixed
   (`rlDisableDepthTest()` around the copy) — did not resolve visibility.
2. **Second suspected cause:** `DrawCoreSphere`'s immediate-mode `rlgl`
   batch draw not honoring a manually-bound texture unit. Switched to
   `DrawMesh` + a baked `Mesh` — a CPU-readback test "confirmed" this fixed
   it, but that test was methodologically flawed (it scanned the *whole
   screen*, not the particle's own pixels, so it was dominated by unrelated
   scene geometry and gave a false positive).
3. **Third suspected cause:** `DrawMesh`'s automatic `Material`-map texture
   binding (slots 0–11) clobbering a custom sampler bound to a low texture
   slot. Moved the depth-texture bind to slot 12. A *properly isolated*
   marker-color readback test then showed correct results — but **only at
   one specific world position** (arena center). At the actual player
   position, a real screenshot showed the particle rendering as solid
   black again (factor still `0.0`).
4. **Fourth suspected cause:** 8-bit RGBA precision in the depth snapshot
   crushing all real scene distances to `255` given the project's
   near/far planes (`0.1` / `15000` — a 150000:1 ratio), making
   scene-depth and particle-depth indistinguishable. Switched the snapshot
   target to a single-channel `R32F` float texture with linearized depth
   stored directly. Also turned out `R32F` needs explicit `TEXTURE_FILTER_POINT`
   (linear filtering on float textures is undefined on many GL3.3 drivers) —
   fixed that too. **Still read back black** in the final screenshot test
   before the decision was made to cut losses and revert.

### Honest assessment for whoever picks this up

Four plausible root causes were found and fixed in sequence, and the bug
survived all four. That pattern suggests either (a) there's a fifth, still
unidentified cause, or (b) one of the earlier "fixes" was never actually
verified correctly and the original cause is still present. The debugging
method (CPU pixel-readback scans of screenshots) repeatedly produced false
positives that looked like confirmation — **don't trust a readback test
that isn't scanning the exact pixels of the shape in question, at the
exact world position the bug was reported at.**

Before retrying: get a way to inspect actual GPU state directly (a real
graphics debugger / RenderDoc-style capture) rather than inferring
correctness from rendered pixel colors — that was the main source of the
repeated false "fixed" conclusions here.

### Files affected by the revert
- Deleted: `core/shaders/common/soft_particle.glsl`, `core/shaders/depth_copy.fs`,
  `skills/taiji/core_test/core_test_soft.vs`/`.fs`
- Reverted to pre-Item-3 state: `core/screen_distort.c`/`.h`,
  `core/procedural_mesh_utils.c`/`.h` (the `ProceduralMesh_CreateBaseSphere`
  addition was soft-particle-only and removed with it)
- `main.c`: removed the `ScreenDistort_SnapshotDepth()` call
- `skills/taiji/core_test/`: kept only the dissolve-quad and metaball shapes

---

## Item 4 — Material/Trail/Decal/Bloom upgrades (NOT STARTED)

From the original Core API update request. Assessed as legitimate, real
gaps (not redundant with existing code) but no implementation work has
begun. Four independent sub-items — can be picked up in any order or split
across sessions:

### 4a. Triplanar mapping (Earth/Metal shaders or `lighting.glsl`)
No existing implementation. Goal: texture large procedural rock/metal
surfaces by world-space position + normal instead of UV, so jagged
ProceduralMesh shapes (Rock, ShardCluster, Fissure) don't show
stretching/streaking on faces with extreme UV distortion.

### 4b. Flow-mapped decals (`core/decal_system.h`)
`DecalSystem_Add`/`AddEx`/`AddStreak` exist but have no flow/scroll concept
— decals are static images. Goal: let `DECAL_PRESET_FIRE_LAVA` /
`DECAL_PRESET_WATER_RIPPLE`-style decals sample a flow map and scroll
outward from center over time instead of being a static stamped texture.

### 4c. Dynamic trail attachment (`core/trail_system.h`)
Existing API (`UpdateFollowerPosition`, `SetFollowerAxis`) supports a
trail following a *point*, not a bone/joint on a skinned IQM model. Goal:
`Trail_AttachToTransform(TrailConfig, const Matrix *targetTransform,
Vector3 localOffset)` so a sword-light/afterimage trail can track an
animated bone matrix. **Needs investigation first**: confirm bone matrices
are actually exposed anywhere accessible to Core (entities/skills modules
own the animated models) before committing to this signature.

### 4d. Dual-filtering bloom (`core/post_fx.h`)
Current bloom (`PostFX_Draw`'s bright/blur passes in `core/post_fx.c`) uses
a simple separable Gaussian (`bloom_blur.fs`), not a dual-filter
(downsample/upsample pyramid) approach. Goal: replace with dual-filtering
for a wider, cheaper glow more suitable for mobile — this is a quality/perf
upgrade to an already-working system, lowest priority of the four.

**None of 4a–4d have any code written yet.** Recommend treating each as its
own scoped task with a visual test (sandbox skill or similar) before
merging, given the Item 3 experience above.
