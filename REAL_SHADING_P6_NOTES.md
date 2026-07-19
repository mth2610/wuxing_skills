# Real Shading P6 — Shadow Map Debugging Notes (2026-07-19, paused unresolved)

Companion to `REAL_SHADING_PLAN.md` / `REAL_SHADING_SPEC.md`. P0–P5 are done and confirmed working
on desktop (user-tested). P6 (real directional shadow map) is **implemented but does not produce a
visible shadow** — extensively debugged in TWO sessions. This doc is the handoff for whoever
resumes it.

## SESSION 2 UPDATE (2026-07-19, later) — three bugs found, ONE remaining

Session 2 got much further than session 1 by using a **flat-color bucketed on-screen debug** on the
ground shader (output distinct pure R/G/B/W by depth range, read off screenshots) instead of blind
iteration. This is the technique to keep using. Three distinct bugs were found; the first two are
**fixed and confirmed**, the third is the remaining blocker:

1. **FIXED — custom sampler binding.** `SetShaderValueTexture(shader, loc, tex)` for a custom
   `uniform sampler2D shadowMap` read as an unbound/white texture (~1.0) under rlvk for the
   immediate-mode ground draws — so every fragment sampled "far" and nothing was ever occluded.
   The reliable path is to bind the shadow map as **`texture0`** via `rlSetTexture(tex.id)` (the
   exact path `DrawTexture`/`DrawTexturePro` uses — which is why the on-screen debug preview of the
   same texture always worked). `maps/toolkit/ground_shadow.{c,fs}` now do this; the custom
   `shadowMap` uniform is gone. **Corollary for the character path (surface_lit.fs):** it still
   binds the shadow map via `SetShaderValueTexture` (a `DrawModel` mesh draw, not immediate mode) —
   UNVERIFIED whether that path works under rlvk; may need the same texture0 treatment or a
   material-map-slot binding.
2. **FIXED — transposed matrix.** `u_lightVP` arrives **transposed** in a custom shader when
   uploaded via `SetShaderValueMatrix` under rlvk (raylib's own internal `mvp` upload transposes;
   `SetShaderValueMatrix` does not, so the matrix reaches GLSL in the opposite convention). Symptom:
   bucketed `proj.z` was uniformly WHITE (≥0.999 — everything crammed at the far plane, a classic
   transposed-projection signature where `w` varies with position). Fix: multiply **`vec * mat`**
   (`vec4(worldPos,1.0) * u_lightVP`, == `transpose(u_lightVP) * v`) instead of `mat * vec`. After
   this, bucketed `proj.z` showed a healthy green→red depth gradient across the ground. Applied to
   BOTH `ground_shadow.fs` and `core/shaders/surface_lit.fs`. (Alternative would be `MatrixTranspose`
   on the C side before `SetShaderValueMatrix`; the shader-side `vec*mat` was chosen since it was
   proven working on the spot.)
3. **REMAINING — capture stores far-crammed depth.** With bugs 1 & 2 fixed, `proj.z` (the ground
   fragment's own light-space depth) is now correct/healthy, but the **SAMPLED depth from the
   shadow map is still ≈1.0 (WHITE bucket) everywhere**, with only very faint caster marks at
   ~0.95–0.99. So the depth written during `EnvShadow_BeginCapture` is crammed against the far
   plane — casters are barely nearer than the far clear, giving no usable depth separation, so the
   `proj.z > sampledDepth` test never fires. **This is the last bug.** The capture sets its
   projection/view via `rlMatrixMode(RL_PROJECTION)` + `rlLoadIdentity` + `rlMultMatrixf(MatrixToFloat(s_lightProj))`
   (and same for the view). Strong suspicion: this manual `rlMultMatrixf(MatrixToFloat(...))` path
   has its own convention/transpose problem (parallel to bug 2) that distorts the capture
   projection and crams the stored depth. Note `main.c`'s working `MyBeginMode3D` uses
   `rlFrustum`/`rlOrtho` (rlgl builds the projection internally) for the projection and only uses
   `rlMultMatrixf(MatrixToFloat(matView))` for the *view* — so the proven-good pattern is
   **rlOrtho for the projection, not rlMultMatrixf of a MatrixOrtho**.

   **Next thing to try:** in `EnvShadow_BeginCapture`, replace
   `rlMultMatrixf(MatrixToFloat(s_lightProj))` with a direct `rlOrtho(-halfExtent, halfExtent,
   -halfExtent, halfExtent, near, far)` call (matching the params used to build `s_lightProj` for
   the FS), keeping the view as `rlMultMatrixf(MatrixToFloat(s_lightView))`. Then re-check with the
   bucketed **sampled-d** debug: if the casters now show up GREEN (~0.3–0.6) instead of faint
   near-white, the capture is fixed and the shadow test should immediately start working. Keep the
   FS's `vec*mat` (bug 2 fix) — it's independent and correct. If `rlOrtho` alone doesn't spread the
   depth, next suspect is the copy pass / rlvk depth-twin scaling.

**Debug scaffolding still in the tree (remove when P6 works or is abandoned):** `main.c`'s
bottom-right "SHADOW MAP DEBUG" preview box; the sun elevation was lowered (`env_shadow`'s callers +
`environment_system.c` default + `verdant_path.c`) to make a raking shadow visible; `env_shadow`'s
`halfExtent` is 45 (was tuned during debugging — should be re-derived to tightly fit the arena once
the depth range is fixed, since a tight frustum gives the best depth precision).

## Current state — safe to leave as-is

`EnvShadow_SetEnabled` defaults to `false` on every platform (`REAL_SHADING_SPEC.md` §7's "do NOT
ship enabled on Mali until profiled" is satisfied trivially — it's not enabled anywhere). The
in-game **J** hotkey is the only way to turn it on, and doing so has **zero visible effect** beyond
the (harmless) extra draw calls — confirmed no crash, no regression to the existing fake blob
shadow (`Environment_DrawSmartShadow`, a completely separate always-on system) or to P0–P5. Safe to
ship / continue other work with this switched off.

## Git boundary

P0–P5 were committed mid-session as `1ac7ea3 "real shading P1-P5"` (touches `CMakeLists.txt`,
`Makefile.Android`, `CORE_API.md`, `ENVIRONMENT_API.md`, `core/gfx_quality.{h,c}`,
`core/shaders/surface_lit.{vs,fs}`, `core/surface_material.{h,c}`,
`environment/environment_system.{h,c}`, `main.c`). Everything below (P6) was written **after** that
commit and is still uncommitted — `git status`/`git diff` from here on only shows the P6 delta on
top of that commit, not the P0–P5 work itself.

## Files touched for P6

- `environment/env_shadow.h` / `environment/env_shadow.c` (new) — depth capture pass + depth→R32F
  copy pass + light-space matrix.
- `environment/shaders/shadow_depth.vs` / `.fs` (new) — depth-only caster shader.
- `environment/shaders/shadow_copy.fs` (new) — copies the depth attachment into a plain R32F color
  texture (see "rlvk depth-sampling quirk" below).
- `core/surface_material.h` / `.c` — `u_lightVP`/`shadowMap`/`u_shadowEnabled` uniforms pushed each
  frame; character self-shadow term in `core/shaders/surface_lit.fs`.
- `maps/toolkit/ground_shadow.h` / `.c` (new) + `maps/toolkit/shaders/ground_shadow.vs` / `.fs`
  (new) — shadow receiver wrap for `default_arena.c`/`verdant_path.c`'s raw immediate-mode floor
  draws (`GroundShadow_Begin()`/`End()` around `rlBegin(RL_TRIANGLES)` blocks).
- `maps/worlds/default_arena/default_arena.c`, `maps/worlds/verdant_path/verdant_path.c` — wrapped
  floor/zone-disc draws with `GroundShadow_Begin`/`End`; lowered sun elevation (was near-vertical,
  now ~-0.5 y) so a working shadow would actually read as a visible raking shadow, not underfoot.
- `environment/environment_system.c` — lowered default sun direction to match.
- `main.c` — `EnvShadow_Init()` call, **J** hotkey, HUD `SHADOW [J]: ON/OFF` text, the shadow
  capture pre-pass block (before `MyBeginMode3D`), `GroundShadow_UpdateFrame()` call, and a
  **debug preview box** (bottom-right corner, `EnvShadow_IsEnabled()`-gated) that draws the raw
  shadow-map texture on screen — useful, kept in.
- `CMakeLists.txt` / `Makefile.Android` — added `environment/env_shadow.c` (maps/toolkit files are
  already covered by the existing glob).
- `CORE_API.md` §18 — documents the P0–P6 API surface including this unresolved status.

## The symptom

With `GfxQuality` at HIGH and shadow enabled, `ShadowFactor()` in both `surface_lit.fs` (character
self-shadow) and `maps/toolkit/shaders/ground_shadow.fs` (ground receiver) reports **zero occlusion
everywhere** — the ground never darkens, regardless of camera angle, character position, or how
aggressively the effect was amplified for visibility (see debugging log below).

## What was ruled out (verified correct, in order)

1. **Depth test/mask state before the capture clear** — forced `rlEnableDepthTest()`/
   `rlEnableDepthMask()` before `rlClearScreenBuffers()` (GL only clears depth when the mask is
   enabled; a previous frame's 2D UI drawing commonly leaves it disabled). Didn't fix it.
2. **Depth-only FBO (`rlActiveDrawBuffers(0)`)** — untested API path anywhere else in this codebase.
   Switched to depth+throwaway-color attachment, matching `core/screen_distort.c`'s
   `LoadRenderTextureWithDepthTexture` (proven working). Didn't fix it, but is the safer form
   regardless — kept.
3. **rlvk's `noSampledDepth` quirk** (`RLVK_HANDOFF.md` §7.10, confirmed active on this dev machine:
   Intel Iris + MoltenVK 1.2.11) — FBO depth-attachment textures aren't reliably sampleable
   directly. Added a depth→R32F copy pass (`shadow_copy.fs` + `BeginTextureMode`/`DrawTextureRec`,
   mirroring `ScreenDistort_SnapshotDepth`). The debug preview confirmed the *copy* itself produces
   recognizable content (see below) — this step works.
4. **`rlSetMatrixModelview`/`rlSetMatrixProjection` direct setters** — used nowhere else in the
   codebase (grepped). Replaced with `rlMatrixMode`+`rlPushMatrix`+`rlLoadIdentity`+`rlMultMatrixf`,
   exactly `main.c`'s `MyBeginMode3D`'s (proven-working) idiom. **This fix visibly changed the
   debug preview from a few stray pixels to a clean, recognizable humanoid silhouette + prop
   shapes** — a real bug, now fixed. Capture is confirmed working after this.
5. **Matrix multiplication order** — checked raylib's own `rmodels.c` `DrawMesh()`:
   `MatrixMultiply(MatrixMultiply(model, view), projection)`. Manual `MatrixMultiply(s_lightView,
   s_lightProj)` matches this exactly (model=identity for immediate-mode draws). Confirmed correct
   via a C-side diagnostic (`TraceLog`) that replicated the shader's exact
   `posLS = u_lightVP * vec4(worldPos,1); proj = posLS.xyz/posLS.w*0.5+0.5` formula for
   `ARENA_CENTER` — printed **`proj=(0.500,0.500,0.499)`, exactly correct** (the light's aim point
   must land at the frustum center). The matrix itself is right.
6. **Frustum too small (debug artifact of shrinking `halfExtent` to 6/20 to zoom the debug preview
   in on the character)** — a raw-value debug view (ground fragment colored blue when
   out-of-frustum-bounds, grayscale otherwise) showed a **hard diagonal line splitting the entire
   visible ground blue/white**, with the character standing on the blue (out-of-bounds) side.
   Widened `halfExtent` from `ARENA_RADIUS+2` (20) to 45 — the diagonal-split debug view then
   showed the whole visible ground as in-bounds (white). **This did not produce any shadow either.**
7. **Bias too large hiding real occlusion** — shrunk from 0.0015 to 0.00005. No change (still zero
   occlusion — ruling out "bias hides a small real signal").
8. **Depth-copy Y-flip** — `ScreenDistort_SnapshotDepth`'s negative-height `DrawTextureRec` flips
   the copy vertically (serves *their* screen-space UV convention). Removed the flip as a test
   (positive height) — no change. Restored the flip afterward (matches proven precedent, and the
   test was inconclusive either way — didn't fix, didn't break further).
9. **Amplified-difference visualization** (`(proj.z - sampledDepth) * 40`, red=occluded/
   green=not-occluded) over the raw grayscale, to catch a signal too small to see on a linear 0..1
   scale — still **100% green, zero red, anywhere**, even directly on/near the character after
   confirming they're within frustum bounds.

## What this pattern suggests, unresolved

The **capture pass** (rasterizing casters from the light's POV) is confirmed working — matrix math
checks out numerically, and the debug preview visibly showed a correctly-shaped character
silhouette after fix #4. But the **ground/character's own read-back** of that same shadow map
(`ShadowFactor()`, comparing `proj.z` against the sampled `pcfDepth`) never detects any occlusion,
even at 40x amplification and near-zero bias. Every individually-testable piece (matrix value,
frustum bounds, bias, Y-flip) checked out correct or made-no-difference when varied — which is
itself suspicious: it suggests the remaining bug is something NOT covered by any of these
targeted tests, e.g.:

- The `shadowMap` sampler binding might not actually be pointing at the freshly-updated copy
  texture by the time the ground draws sample it (a timing/ordering issue between
  `GroundShadow_UpdateFrame()`'s `SetShaderValueTexture` and the actual draw, possibly something
  rlvk-specific about how sampler bindings propagate across separate shader programs — untested
  territory again, since `SetShaderValueTexture` for a *custom*-named sampler like `shadowMap` is
  a less common pattern than the handful of other call sites found in this codebase).
- Something about **how rlvk's per-draw pipeline/descriptor binding interacts with a texture that
  was written via the depth→copy dance this same frame** (a resource-barrier/synchronization gap
  that a CPU-side API trace wouldn't reveal, but a GPU frame capture would show immediately as
  "shader X reads texture Y, which still holds last frame's — or garbage — data").
- A genuine remaining coordinate-space mismatch not caught by the ARENA_CENTER spot-check (that
  check only validates ONE point in the capture's own frame; it doesn't prove the *copied* texture,
  sampled through a *different* shader program at a *different* point in the frame, ends up wired
  to the exact same texture unit/slot the uniform value expects).

## Recommended next step

This needs real GPU introspection (RenderDoc, or Xcode's Metal/GPU frame capture attached to the
MoltenVK process) to see the actual bound resources and pixel values at each pass — exactly the
class of tool `RLVK_HANDOFF.md` §6 ("DEBUGGING METHODOLOGY") assumes for this kind of bug, and
which a remote coding session can't drive. Whoever picks this up next should:
1. Capture a frame with shadow enabled (J) in RenderDoc/Xcode.
2. Inspect the depth-capture pass's output directly (is the character really there, at the
   position/depth expected?).
3. Inspect the copy pass's output (does it match the capture, unflipped/correctly oriented?).
4. Inspect the ground draw's actual bound `shadowMap` sampler binding at the exact draw call (is it
   really the fresh copy texture, or something else/stale?).
5. Only then adjust the C/shader code — this session's blind, screenshot-driven iteration hit its
   limit around step 9 above.

## Also worth knowing

- The two prior (already-fixed) bugs in this list (#2 depth-only FBO, #4 direct matrix setters)
  were both found by grepping for "is this exact API called anywhere else in this codebase" and
  switching to whatever pattern *is* proven elsewhere — a good general debugging heuristic for this
  project given how much of `rlvk` is a from-scratch reimplementation that doesn't cover 100% of
  raylib's surface with equal confidence everywhere.
- `RLVK_HANDOFF.md` §7.10 describes rlvk's OWN internal automatic depth→sampleable-twin mechanism
  (triggered at `rlDisableFramebuffer()`), which — per that doc — should make the RAW depth texture
  directly sampleable transparently, without needing a manual copy at all. This session's manual
  copy (mirroring `screen_distort.c`) was kept anyway since it's the pattern with actual proven
  precedent in THIS codebase; whether rlvk's automatic twin would work better/worse if the manual
  copy were removed entirely is untested.
