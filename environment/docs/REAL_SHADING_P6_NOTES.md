# Real Shading P6 — Shadow Map Debugging Notes (2026-07-20, session 4 — pixel-readback diagnostics retired as unreliable; direct C-side matrix logging in flight)

## SESSION 4 UPDATE, PART 11 (2026-07-20 — pixel-encoded diagnostics retired; direct Matrix TraceLogs added instead)

Parts 9-10 encoded live shader state (proj.x/y/z) directly into `ground_shadow.fs`'s output color
and read it back via screen pixels at computed world→screen coordinates. The result looked
damning — the shader's own computed `proj.z` for the player's exact feet position (0.847) wildly
disagreed with `EnvShadow_DebugDump`'s CPU ground truth for the SAME point (0.5122) — but scanning
outward, the values didn't change *smoothly* the way a coherent light-space projection should
(jumping around, even reversing direction partway through the scan). That's consistent with the
scan path crossing OTHER rendered geometry (the skill-sandbox screen has several glowing test
dummies/props near the arena center) or HDR bloom bleeding brightness from those into neighboring
pixels before the final `LoadImageFromScreen()` capture — either way, encoding precise numeric
data in post-processed (bloom + ACES tonemap) pixel colors and reading it back is **not
trustworthy at this precision**, so parts 9-10's specific numeric conclusions are retracted (the
qualitative "no shadow renders" finding from part 8's simpler RED/GREEN bucket is unaffected —
binary/large-area color classification is far more bloom-tolerant than continuous per-channel
gradients).

**Replaced with a zero-rendering diagnostic**: `maps/toolkit/ground_shadow.c` and
`environment/env_shadow.c` each now `TraceLog` the exact `Matrix` bytes at the two points that
matter — the moment `env_shadow.c` computes `s_lightVP` (`EnvShadow_BeginCapture`), and the moment
`ground_shadow.c` is about to upload it (`GroundShadow_Begin`, right before
`SetShaderValueMatrix`). Both read via `EnvShadow_GetLightVP()`, so they're the same C struct by
construction — if these two logged matrices match (they should, trivially) but the shader still
computes wrong `proj` values, the bug is confirmed to be inside rlvk's C→GPU uniform upload path
itself (`rlSetUniformMatrix`/`rlvkShaderWriteUniform`/the UBO push), not in application code — at
that point this needs RenderDoc/Xcode GPU capture (`../../third_party/vulkan/docs/HANDOFF.md` §6), not more TraceLogs.
`ground_shadow.c` also logs whether `GetShaderLocation` resolved `u_lightVP` to a valid (non -1)
slot, ruling out the simplest possible cause first. `ground_shadow.fs` is back to real production
logic (no diagnostic output).

**To run**: same `WUXING_SHADOW_TEST=1 ./build/wuxing` command as before. Look for the
`GROUND_SHADOW diag:` and `ENV_SHADOW diag:` lines (both print once, early in the log, well before
the `ENV_SHADOW dump:`/H-dump lines at the end) and diff the two logged matrices' rows directly.

## SESSION 4 UPDATE, PART 7 (2026-07-20 — manual screenshot debugging hit its limit; added a deterministic headless test)

Every manual round-trip (part 5, part 6) kept desyncing because the player moved between the
build that hardcoded a UV/added a diagnostic and the screenshot that tested it — a coordination
problem, not new evidence. Four rlvk visual-test scenarios (`custom_vs_texture`,
`render_then_immediate_sample`, `r32f_immediate_sample`, `nested_rt_sample` — the last one
specifically nests the sampling draw inside a SECOND FBO scope, mirroring `ScreenDistort_Begin()`
wrapping the whole main scene including the ground) all reproduce `ground_shadow`'s exact
technique and all **PASS** in isolation — so whatever's wrong needs the real game's actual frame
structure to reproduce, not a minimal repro.

**Added instead: `WUXING_SHADOW_TEST=1` headless verify mode (`main.c`)**, following the existing
`WUXING_VERIFY=<skill>` pattern (`sandbox/visual_verify.c`) already used for VFX regression
testing. It:
1. Fixes the player at the arena center `(6, 0, 4.4)` — deterministic, always inside
   `default_arena`'s ground and the shadow frustum.
2. Calls `EnvShadow_SetEnabled(true)`.
3. Runs `WUXING_SHADOW_TEST_WARMUP` frames headless (default 20, override via env var).
4. Saves `autotest_output/shadow_test.png`.
5. **Programmatically reads back two screen pixels** — the player's own feet (expected NOT
   shadowed, nothing occludes directly underneath) and a point 1m along the flat sun direction
   (expected shadowed, per every H-dump so far showing a real occluded band near there) — and
   `TraceLog`s both colors, their luminance, and a `SHADOW VISIBLE` / `NO SHADOW DETECTED`
   verdict. This removes the player-position/UV-sync problem entirely: the harness knows exactly
   where the caster and test points are because it put them there itself.

`ground_shadow.fs` is back to its real production logic (`mat*vec` `ShadowFactor()` +
`mix(0.30, 1.0, shadow)` darken) — the diagnostic bucket/hardcoded-UV versions from parts 5–6 are
gone.

**To run** (human-run only, per root `CLAUDE.md`'s build section):
```bash
cmake --build build -j4
WUXING_SHADOW_TEST=1 ./build/wuxing
```
Paste back the `WUXING_SHADOW_TEST:` log line (and optionally the PNG at
`autotest_output/shadow_test.png`, which can be read directly without a manual screenshot). If it
says `NO SHADOW DETECTED`, the readback coordinates and both pixel colors are in the log — enough
to reason about precisely without another screenshot round-trip. If `SHADOW VISIBLE`, P6 works and
the earlier in-game "nothing visible" reports were a false read (wrong position/zoom/timing) —
worth a normal in-game recheck at that point to confirm, but the hard part is done.

## SESSION 4 UPDATE, PART 6 (2026-07-20 — part 5's grayscale-at-computed-UV came back uniform white/cream on a hard diagonal split; 3 synthetic scenarios that mirror the technique all PASS)

Part 5's raw-visualize-at-`proj.xy` result: a hard diagonal split — **MAGENTA** (out-of-[0,1]
bounds) on one side, **near-white/cream, uniform** on the other (the "in bounds" side) — no dark
patch, no gradient, even though the H-dump proves real caster depth data exists in the map. The
bottom-right shadow-map preview box (a plain `DrawTexturePro` of the same texture) still shows
correct content (mostly red/far with the expected dark caster cluster).

To pin down whether this is a texture-binding issue or a live matrix issue, three new rlvk
visual-test scenarios were written (`third_party/vulkan/tests/rlvk_visual_test.c`) to reproduce
`ground_shadow`'s exact technique in isolation:
- `custom_vs_texture` — custom VS (position+color only, no texcoord, matching
  `ground_shadow.vs`) + custom FS sampling `texture0` at a fixed UV, drawn via raw
  `rlBegin(RL_TRIANGLES)`, texture bound via `rlSetTexture()` right before, preceded by an
  unrelated RL_TRIANGLES draw in the same mode (matching the game's draw ordering). **PASS.**
- `render_then_immediate_sample` — same, but the sampled texture is rendered-to (`BeginTextureMode`/
  `EndTextureMode`) earlier in the SAME frame, matching `EnvShadow_BeginCapture/EndCapture` running
  as a pre-pass before the main 3D draw. **PASS.**
- `r32f_immediate_sample` — same, but the texture is built EXACTLY like `s_copyRT` (manual
  `rlLoadFramebuffer`+`rlLoadTexture(..., RL_PIXELFORMAT_UNCOMPRESSED_R32, ...)`, not
  `LoadRenderTexture`, filled via a copy-style shader pass, `POINT`/`CLAMP` set after) and only
  `.r` is sampled. **PASS.**

So the general mechanism (custom-VS immediate-mode draw sampling an R32F texture that was
rendered-to earlier the same frame, via `rlSetTexture`) works correctly under rlvk in isolation —
none of these isolate the real bug. The real game's frame does a lot more between the shadow
map's last write and this read (`ScreenDistort_Begin()`, `MyBeginMode3D`'s own push/pop,
`SurfaceMaterial_UpdateFrame`, multiple other shader/scope switches) that these minimal repros
don't include; reproducing that fully would take substantially more scaffolding.

**New diagnostic (bypasses u_lightVP entirely)**: `ground_shadow.fs`'s `main()` now samples
`texture0` at a UV **hardcoded from the H-dump's own caster texel** (2048-res map, texel
`(1638,1407)` → uv `(0.8003, 0.6873)`, proven by the H-dump to hold real `~0.695` depth data right
now) and outputs it as grayscale, skipping `u_lightVP`/`fragWorldPos`/`proj` completely.
- **Still uniform white** → texture0 itself is not bound for this draw, full stop, independent of
  any matrix — the bug is in the binding/descriptor path specifically for this shader+draw
  combination, in a way the three synthetic scenarios above didn't reproduce. Next step there:
  either extend the synthetic repro to include more of the real frame's structure (ScreenDistort,
  MyBeginMode3D nesting) between write and read, or get a RenderDoc/Xcode capture of the real game
  (HANDOFF §6) — screenshot-guessing has hit its limit for this specific question.
- **Dark/mid-gray** → texture0 IS bound correctly; the bug is specifically in this shader's live
  `u_lightVP`/`fragWorldPos`/`proj` computation (something not caught by the `shadow_ortho`
  scenario, which uses a default VS + `DrawTexturePro`, not this shader's custom VS + raw
  immediate-mode draw) — go there next, not the texture path.

TEMPORARY — revert `ground_shadow.fs` back to `mix(0.30, 1.0, ShadowFactor(fragWorldPos))` once
diagnosed either way.

## SESSION 4 UPDATE, PART 5 (2026-07-20 — RED/GREEN bucket came back SOLID GREEN — narrowing further)

Part 4's RED/GREEN bucket result: **uniform GREEN across the entire visible floor**, no RED
anywhere, not even right at the character's feet where the H-dump proves a real occluded band
exists. The bottom-right "SHADOW MAP DEBUG" preview box (a raw `DrawTexturePro` of the shadow
copy texture, unrelated to `ground_shadow.fs`) still shows the expected content — mostly solid red
(far, R32F depth≈1.0 visualized as pure red since G/B read 0) with a small dark caster cluster —
consistent with the H-dump. So the shadow map itself is fine; the break is specifically in
`ground_shadow.fs`'s live GPU sampling of it.

All-GREEN with zero RED, given `ShadowFactor()`'s logic (`if (out of [0,1] bounds) return 1.0;
else PCF-compare`), collapses to two possible root causes:
1. `u_lightVP` reaching this shader as garbage → every fragment's projected coordinate lands
   outside `[0,1]` → always hits the early "lit" return.
2. `texture0` sampling as the "unbound/white" default (~1.0 = far) for this specific draw — this
   is session 2's original bug #1 class (custom sampler read as unbound under rlvk for
   immediate-mode draws), which the `rlSetTexture(texture0)` route was supposed to have already
   fixed for this exact code path. If it silently regressed or never actually applied to the
   REAL triangle batch (vs. the debug-preview's separate `DrawTexturePro` call), every PCF sample
   would read ~1.0 and `proj.z - bias > 1.0` can never be true → always unoccluded.

Checked (ruled out) as a cause: `shader->usesUbo` for a normal runtime-compiled VS+FS pair is
unconditionally `true` (`rlvk_shader.inl:221`), so the earlier theory "matrix writes silently
no-op because usesUbo is false" doesn't apply here — that's compute-shader-only logic
(`rlvk_shaderc.inl`/`rlvk_shader.inl:377`). `rlGetLocationUniform` is a plain linear name search
with no VS/FS ambiguity risk (unique uniform names). `rlvkShaderWriteUniform` correctly targets
`fsOffset` for FS-only uniforms like `u_lightVP` and the offset+size math checks out against the
`spirv-dis` result from part 1. Nothing found on paper; needs a live signal.

**Diagnostic change**: `ground_shadow.fs`'s `main()` now skips the shadow comparison entirely and
directly visualizes what `texture0` returns at each fragment's projected coordinate:
- **BLUE** = `u_shadowEnabled` not seen as `>0.5`
- **MAGENTA** = projected coordinate out of `[0,1]` bounds (points at cause 1)
- **GRAYSCALE** (black=near/occluder, white=far/clear) = `texture0`'s raw depth at that point — if
  this is uniform white everywhere including right next to the caster, that's definitive proof of
  cause 2 (texture0 not actually bound to the real shadow map here); if it shows a dark patch/
  gradient matching the debug-preview box's shape, `texture0` IS correct and the bug is
  specifically in the occlusion comparison (bias, PCF, or the coordinate itself despite passing
  the bounds check) — cause 1's matrix-reaches-the-shader-but-is-subtly-wrong variant.

**Next step**: press **J**, screenshot the floor near the character again. TEMPORARY — revert to
`mix(0.30, 1.0, shadow)` (and restore the RED/GREEN or plain occlusion logic) once diagnosed.

## SESSION 4 UPDATE, PART 4 (2026-07-20 — near-black darken still invisible, switched to a flat-color bucket)

Part 3's near-black diagnostic (`mix(0.02, 1.0, shadow)`) still showed **no visible shadow at all**
— ruling out "too subtle to see." Since the H-dump proves the capture/copy/matrix data is correct
(part 3), the remaining suspects are downstream of `ShadowFactor()`'s inputs actually reaching this
specific shader live: either `u_shadowEnabled` isn't landing as `>0.5` for `ground_shadow.fs`
specifically (a custom-VS + custom-FS pair driven via raw immediate-mode `rlBegin(RL_TRIANGLES)` —
untested by the `shadow_ortho` synthetic scenario, which used a default VS), or `ShadowFactor()`
evaluates differently live than in the CPU `ProjectLS()` dump.

`ground_shadow.fs`'s `main()` was switched (TEMPORARY, session 2's proven bucketed-debug technique)
to output a flat color instead of darkening the real ground:
- **BLUE** = `u_shadowEnabled` not seen as `>0.5` by this shader (wiring/uniform-upload bug)
- **GREEN** = enabled, but `ShadowFactor()` returned lit — includes both "genuinely not occluded"
  and the out-of-frustum-bounds early return, so GREEN alone doesn't distinguish those
- **RED** = enabled AND occluded — the expected result for the ~1m band near the character per the
  H-dump

**Next step**: press **J**, screenshot the arena floor (whole visible area, plus close to the
character). Whichever color dominates narrows the bug immediately — BLUE points at
`GroundShadow_Begin`'s uniform upload, all-GREEN-no-RED points at `ShadowFactor()`'s live PCF/bias
math disagreeing with the CPU dump for reasons not yet understood, and RED-but-in-the-wrong-place
would resurrect the original position-drift question. Revert to `mix(0.30, 1.0, shadow)` once
diagnosed — do not ship the bucket colors.

## SESSION 4 UPDATE, PART 3 (2026-07-20 — after the env_shadow.c revert: capture data looks correct, but no visible shadow yet)

With `env_shadow.c` back to push/pop (part 2) and `mat*vec` still live (part 1), a fresh **H** dump
(3 presses, same position, 3 zoom levels — all three dumps numerically identical, as expected since
zoom doesn't move the character) shows the diagonal-split bug is gone and the capture data now
looks like a textbook-healthy shadow map:
- `stored depth min=0.4650 max=1.0000 | <0.1:0 0.1-0.9:5274 0.9-0.999:0 >=0.999:4189030` — a small
  cluster of texels (the caster) at a real mid-range depth (~0.45-0.70), everything else correctly
  at the far/clear plane. This is the "healthy green→red gradient" shape session 2 was originally
  looking for, not the all-white or all-near degenerate cases from earlier sessions.
- The caster's own texel: CPU-predicted `proj=(0.801,0.687,z=0.6952)` vs stored `0.6949` — matches
  to 3 decimals, **no Y-flip needed** (`stored[noflip]=0.6949` is the right one, `stored[flip]=
  1.0000` is wrong here — differs from session 3's dump, worth watching if it flips again).
- The line-walk (ground points from the caster's feet outward along the flat sun direction) shows
  **PASS(shadow) at t=0.5, 1.0, 1.5** (a real, correctly-computed occlusion) and `fail` (correctly
  unshadowed) elsewhere — i.e. the numeric ground truth says a real, roughly 1-meter-wide shadow
  band should be visible just past the character's feet, not directly underneath.
- **But the user reports no visible shadow at any position or zoom level.** Given the numeric data
  now looks correct, the leading hypothesis is that the shadow is real but too **subtle/small** to
  read: only ~1m wide, darkened to just 30% (`mix(0.30, 1.0, shadow)` in `ground_shadow.fs`), against
  an already-dark, muted night palette.

**Diagnostic change (TEMPORARY, must be reverted)**: `ground_shadow.fs`'s darken floor was bumped
from `mix(0.30, ...)` to `mix(0.02, ...)` (near-black) so even a small/subtle patch is unmistakable.
**Next step**: retest with **J**, standing still, looking closely at the ~1m band just past the
character's feet (in the direction the shadow should fall, roughly opposite the sun). If a dark
patch now appears there: the math was right all along, this was a contrast/UX issue — revert the
darken back to `0.30` (or pick a better middle value) and consider whether the shadow's small
size/short reach needs a design change (bigger character caster height offset? Lower sun elevation
for a longer shadow throw?) rather than more matrix debugging. If STILL nothing appears even at
0.02: the bug is somewhere between `GroundShadow_Begin`'s wiring and the actual on-screen ground
fragment (wrong ground being drawn/wrapped, a different mesh covering it, or `u_shadowEnabled`
not actually reaching this draw) — go there next, not back to the capture matrices.

## SESSION 4 UPDATE, PART 2 (2026-07-20 — in-game regression, env_shadow.c reverted)

The env_shadow.c push→direct-set change described below (made to fix the §7.25 rlvk state-
lifecycle bug found by the new `shadow_ortho` scenario) **made the real in-game shadow worse, not
better**, per user tier-4 testing: instead of a coherent-but-drifting shadow, the screen split into
a hard-edged dark/light diagonal (following the capture frustum's projected bounds) that got worse
when zoomed in, fully covering the character up close. The **H**-hotkey numeric dump confirmed why:
stored depth went from session 3's "crammed near 1.0 (far) almost everywhere" to **crammed near 0.0
(near) almost everywhere** (`<0.1: 4126587` texels out of 4194304 total, i.e. 98.4%) — inverted, not
fixed. Every line-walk sample read `stored=0.0000`, so `ShadowFactor()`'s `proj.z - bias > stored`
test was true everywhere inside the frustum → the whole in-frustum ground rendered at the shadowed
30% floor, and the frustum's own `if (proj out of [0,1]) return 1.0` boundary is exactly the hard
diagonal the user saw.

**`environment/env_shadow.c` has been reverted** to `rlPushMatrix()`/`rlPopMatrix()` for the
MODELVIEW capture matrix (the pre-session-4 form, matching `main.c`'s `MyBeginMode3D` idiom). The
`shadow_ortho` synthetic test still demonstrates a real corruption from the push/pop form (see
§7.25) — that finding hasn't been retracted, just found NOT to translate into an improvement for
this specific real-game capture path when reverted the other way. **This contradiction is
unresolved**: something about the real game's capture (many more draw calls per capture than the
synthetic scenario — the full re-invoked scene, multiple objects each with their own local
`rlPushMatrix`/`rlTranslatef` nesting) behaves differently from the isolated single-cube repro.
Whoever picks this up next should NOT re-apply the direct-`rlLoadIdentity()` form to env_shadow.c
without a GPU capture (RenderDoc/Xcode) proving what changed in the actual capture output — the
`shadow_ortho` scenario's PASS does not predict the real game's behavior here.

**mat*vec is still believed correct** (the `spirv-dis` ColMajor evidence in part 1 below doesn't
depend on the push/pop question at all — it's about UBO layout, not matrix-stack mechanics) and was
NOT reverted. What's unverified now: whether mat*vec alone (with the capture pass back to its
original, session-3 push/pop form) fixes the position/scale drift in-game. **Next step: rebuild and
retest with J**, now that only the shader fix is live.

## SESSION 4 UPDATE, PART 1 (2026-07-20 — matrix-convention fix, spirv-dis + synthetic test)

Session 4 root-caused and fixed the matrix-convention bug behind session 3's "position/size
drifts with distance from the arena center" symptom, using the Renderer agent's proper tooling
(spirv-dis + a new rlvk visual-test scenario) instead of more in-game screenshot iteration —
exactly what session 3 recommended as the next step. It also found and fixed a SEPARATE, real
rlvk state-lifecycle bug that the new test scenario exposed as a side effect.

**Bug found: `vec*mat` was backwards.** Session 3 switched `ShadowFactor()`'s `posLS` computation
from `u_lightVP * vec4(worldPos,1)` (mat*vec) to `vec4(worldPos,1) * u_lightVP` (vec*mat) based on
a theory that rlvk's auto-generated UBO declares `mat4` uniforms `row_major`. **That theory is
false.** `RLVK_DUMP_SPV` + `spirv-dis` on `ground_shadow.fs`'s exact compiled SPIR-V shows:
```
OpMemberDecorate %gl_DefaultUniformBlock 0 ColMajor
OpMemberDecorate %gl_DefaultUniformBlock 0 MatrixStride 16
OpMemberDecorate %gl_DefaultUniformBlock 0 Offset 0
```
`ColMajor`, matching `rlSetUniformMatrix`'s straight column-major upload of raylib's `Matrix`
(`rlvk/rlvk_shader.inl`) and matching every other matrix uniform in the engine (the automatic
`mvp`). So `mat*vec` was already correct, and `vec*mat` was computing `transpose(u_lightVP) * v`
instead — which only coincidentally agreed with `mat*vec` **at the light's aim point**
(`ARENA_CENTER`, where session 2/3's CPU numeric check `proj=(0.500,0.500,0.499)` "confirmed" it —
that check used a `ProjectLS()` CPU formula that is itself the `mat*vec` convention, so it was
inadvertently checking the correct math against the wrong shader code and only the coincidence at
the aim point hid the mismatch). Off-center, `transpose(M)*v` diverges from `M*v` by the
transpose of the view's rotation block, growing with distance — exactly the reported "drifts
proportionally to distance from arena center, ~2× at mid-arena."

**Fix**: reverted both `ShadowFactor()` implementations (`maps/toolkit/shaders/ground_shadow.fs`,
`core/shaders/surface_lit.fs`) to `u_lightVP * vec4(worldPos, 1.0)` (mat*vec).

**Verification**: rather than more in-game iteration, wrote `shadow_ortho` — a new
`third_party/vulkan/tests/rlvk_visual_test.c` scenario mirroring `env_shadow.c`'s exact pipeline
(manual ortho depth-capture FBO, R32F depth copy, custom `mat4` uniform sample) with an occluder
and two test points placed well off the world origin and off the light's own position (the shape
of scene that actually exposes a transpose bug — a check near the origin/aim-point wouldn't). With
`vec*mat` the scenario fails (point B, in open ground, is falsely shadowed); with `mat*vec` it
passes. This scenario is now permanent regression coverage — see `../../third_party/vulkan/docs/HANDOFF.md` §5's scenario
list.

**Second bug found (while getting the test suite clean): a real rlvk state-lifecycle bug.** The
first version of `shadow_ortho` passed on its own but broke two unrelated later scenarios
(`instanced`, `ui_after_rt`) whenever it ran earlier in the same process. Bisected to: bracketing a
MODELVIEW change with `rlPushMatrix()`/`rlPopMatrix()` while a non-swapchain (manual) framebuffer
is bound corrupts a LATER, unrelated `DrawMesh`/`DrawMeshInstanced` call — reproduced with **zero
draw calls** between the push and the pop. This is the exact pattern `EnvShadow_BeginCapture`/
`EndCapture` used for the light's view matrix. Root cause isn't fully isolated (needs a GPU
capture, see `../../third_party/vulkan/docs/HANDOFF.md` §7.25), but a direct `rlLoadIdentity()`+`rlMultMatrixf()` (no
matrix-stack push at all) reproducibly avoids it, confirmed by the full 15/15 visual suite passing.
Applied the same fix to `environment/env_shadow.c` (both `BeginCapture` and `EndCapture`) — the
PROJECTION stack's push/pop (`rlOrtho`) was unaffected and left as-is. Full case study, bisection
log, and the open question for a future session: `../../third_party/vulkan/docs/HANDOFF.md` §7.25.

**Superseded by part 2 above**: this session could not run the actual game itself (build output is
off-limits to this agent), so verification of the mat*vec fix happened only through the Renderer
agent's tier-1/tier-3 ladder (compile check + `run_rlvk_visual_test.sh`, both green) plus the
raylib/GL convention argument. The user then ran tier 4 and found a regression — but it traces to
the SEPARATE §7.25 push/pop change (now reverted, see part 2), not to mat*vec itself. Still needed:
a retest with only the mat*vec fix live, to see if the position/scale drift is actually gone.

The rest of this file is the session 1–3 history (superseded where it conflicts with the above,
kept for the reasoning trail).

---

## SESSION 3 UPDATE (2026-07-19)

Session 3 built a **numeric instrument** and got the shadow to actually RENDER — a coherent,
character-shaped, caster-following shadow — but with an unresolved position/scale drift. Paused at
the best-so-far state. What's in the tree now:

**The instrument (keep — this is how future sessions should work):**
- `EnvShadow_DebugDump(Vector3)` (env_shadow.c) + **H hotkey** (main.c, works when shadow is ON):
  CPU-reads the shadow map back (`rlReadTexturePixels` — works under rlvk), prints min/max/
  histogram, projects the player + the ground under them with the exact CPU row-vector formula,
  prints the stored depth at those texels in both Y orientations + 5×5 neighborhoods, and walks the
  expected shadow line (t=0..6m along the flat sun dir) printing z/stored/PASS-fail per step.
  One keypress = full numeric ground truth; no more color-guessing.

**Fixed and confirmed in session 3 (all still in the tree):**
1. **`textureSize()` returns 0 under rlvk** → `texel = 1/0 = INF` → every PCF tap coordinate NaN
   (`0*INF`) → silently no shadow. Replaced with a `u_shadowTexel` uniform pushed from C
   (`1.0/map.width`). Same fix hardcoded (1/1024) in surface_lit.fs.
2. **Capture + copy are byte-correct** (proven by readback at BOTH 1024 and 2048): casters land at
   exactly the CPU-predicted texels with depths matching computed values to 3 decimals
   (e.g. stored 0.7172 vs computed 0.7173), no Y flip, far-clear 1.0 elsewhere. The capture
   pipeline (rlOrtho + rlMultMatrixf view + depth shader + copy pass) is NOT the problem.
3. **Empirical GLSL multiply = `vec * mat`** (4-combo color experiment: only `(vec*mat, v-as-is)`
   produced caster silhouettes; `mat*vec` produced far-crammed z). Combined with reading
   `rlvk_shader.inl`'s `rlSetUniformMatrix` (uploads raylib memory order = semantic column-major,
   correct std140), the implication is **rlvk's auto-generated UBO declares mat4s row_major** —
   NOT yet verified in `rlvk_shaderc.inl`; verify with `RLVK_DUMP_SPV` + spirv-dis.

**The best-so-far state (what's committed now):** 2048 map + copy texture + `mat4 u_lightVP` +
`vec*mat` + texture0-via-`rlSetTexture` + `u_shadowTexel` + real darken output. Renders a coherent
character-shaped shadow that follows the caster. **Remaining bug: the shadow's position and size
drift proportionally to the caster's distance from the arena center** (~2× at mid-arena — the
yellow-ball experiment pinned it: a marker drawn at the CPU-predicted shadow spot stays fixed at
the feet while the rendered shadow sits meters away and scales).

**Open contradictions (the next session must resolve these, ideally as an rlvk visual-test
scenario, NOT via more in-game iteration):**
- (a) Same shader code: **1024 map → NO shadow at all; 2048 map → displaced-but-present shadow.**
  Readback proves the copy content correct at both sizes. Something in sampling/binding is
  texture-size-dependent (pool-ring descriptors? batch texture0 path? NPOT-vs-window-size in the
  §7.14-7.17 letterbox handling?).
- (b) Uploading the matrix as **4 plain vec4 rows** (bit-exact CPU formula, no mat4 convention
  ambiguity) → NO shadow at all, even though `rlSetUniform`'s VEC4 path reads correct in source.
  Suggests the auto-UBO offset table and `GetShaderLocation` indices desync for some layouts.
- (c) Sampling the **raw depth texture** (rlvk §7.10 auto-twin) instead of the manual copy →
  shadow appears but with a DISTORTED shape.
- (d) Round C (1024 + sun y=-0.5): user stood inside the rendered patch and the CPU line-walk
  matched exactly. A later 1024 run (sun y=-0.7, otherwise same) → nothing. Possibly sun-angle-
  dependent, possibly a mis-tracked variable across rounds.

**Recommended next steps (Renderer agent, `third_party/vulkan/CLAUDE.md` ladder):**
1. `RLVK_DUMP_SPV=dir` + spirv-dis on ground_shadow.fs → confirm the mat4's RowMajor/ColMajor
   decoration and the UBO offsets of every uniform (settles (b) and the multiply question for good).
2. Write a minimal visual-test scenario (tests ladder step 3): ortho depth capture to an RT,
   second shader samples it with the same VP, assert a known occluder shadows a known point.
   Vary RT size 1024/2048 → reproduces (a) headlessly.
3. Only after both pass, come back to the game.

The rest of this file is the session-1/session-2 history (superseded where it conflicts with the
above, kept for the reasoning trail).

---

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

   **Tried `rlOrtho` (did NOT fix it).** Replaced `rlMultMatrixf(MatrixToFloat(s_lightProj))` with
   `rlOrtho(-halfExtent, halfExtent, -halfExtent, halfExtent, 0.1, distance*2)` (kept in the code —
   it's the proven-good projection pattern regardless). Sampled-d bucket was still all-WHITE.
   **So the cramming is NOT the projection-matrix upload path.**

   **The decisive observation (do NOT lose this):** after the bug-2 fix, the FS's own `proj.z`
   (`vec4(worldPos,1)*u_lightVP`, remapped `*0.5+0.5`) reads a healthy **~0.1–0.9 (GREEN)** across
   the ground — so the FS's light-space depth is correct and NOT crammed. But the depth **SAMPLED
   from the shadow map** at those same texels reads **~0.95–0.999 (WHITE, casters barely below
   1.0)**. So a caster (physically ABOVE the ground, i.e. NEARER an overhead light) is stored with a
   depth (~0.97) that is LARGER than the ground fragment's own computed depth (~0.5). That ordering
   is **inverted** — the near-to-light caster should store a SMALLER depth than the farther ground.
   This is a **depth-convention / range mismatch between what the capture pipeline WRITES and what
   the FS COMPUTES**, specific to rlvk (custom Vulkan rlgl). Candidate causes not yet isolated:
   - rlvk's clip-z epilogue (`gl_Position.z=(z+w)*0.5`, HANDOFF §3.1) interacting with `rlOrtho`'s
     depth range differently than the FS's manual `MatrixOrtho`-based `*0.5+0.5` remap (possible
     double- or half-transform → cram to [0.5,1.0], which fits the observed ~0.97).
   - rlvk's `noSampledDepth` depth→R32F twin (HANDOFF §7.10) storing/scaling depth non-linearly or
     reversed relative to a straight [0,1].

   **Recommended next step (needs the rlvk/Renderer agent or GPU capture, NOT more screenshot
   guessing):** determine rlvk's EXACT written-depth convention for an `rlOrtho` projection + the
   epilogue, then make the FS's `proj.z` replicate that same transform (instead of the textbook
   GL `*0.5+0.5`). Concretely: render a plane at a KNOWN light-space depth into the map and read
   back the stored value to calibrate the transform. Or sample the depth twin directly (skip the
   manual copy pass — `EnvShadow_GetShadowMap` could return `s_depthTex2D` and rely on rlvk's auto
   twin per §7.10) to remove the copy pass as a variable. This is squarely the Renderer agent's
   domain (`third_party/vulkan/CLAUDE.md`); the two application-side bugs (1 & 2) are already fixed.

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
`Makefile.Android`, `core/docs/API.md`, `environment/docs/API.md`, `core/gfx_quality.{h,c}`,
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
- `core/docs/API.md` §18 — documents the P0–P6 API surface including this unresolved status.

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
3. **rlvk's `noSampledDepth` quirk** (`../../third_party/vulkan/docs/HANDOFF.md` §7.10, confirmed active on this dev machine:
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
class of tool `../../third_party/vulkan/docs/HANDOFF.md` §6 ("DEBUGGING METHODOLOGY") assumes for this kind of bug, and
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
- `../../third_party/vulkan/docs/HANDOFF.md` §7.10 describes rlvk's OWN internal automatic depth→sampleable-twin mechanism
  (triggered at `rlDisableFramebuffer()`), which — per that doc — should make the RAW depth texture
  directly sampleable transparently, without needing a manual copy at all. This session's manual
  copy (mirroring `screen_distort.c`) was kept anyway since it's the pattern with actual proven
  precedent in THIS codebase; whether rlvk's automatic twin would work better/worse if the manual
  copy were removed entirely is untested.
