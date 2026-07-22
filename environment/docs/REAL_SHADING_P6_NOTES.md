# Real Shading P6 — Shadow Map Debugging Notes

## SESSION 6 (2026-07-22) — FIXED IN-GAME. Two bugs; the game one is a VIEW-SPACE mismatch (READ FIRST)

The ground shadow now works. Session 5's "MoltenVK drops the second push to binding 0" theory is
**wrong** and retired. Two distinct bugs were found:

**Bug 1 — the actual in-game cause (the fix that makes the character's shadow appear):**
`main.c`'s `MyBeginMode3D` `rlPushMatrix()`es the MODELVIEW and `rlMultMatrixf(view)`s into it, so
the **view matrix lands in rlgl's `transform`** (`transformRequired=true`) for the whole main pass.
Every immediate-mode `rlVertex3f` (the arena floor = the shadow receiver) is therefore CPU-
transformed to **VIEW space** before the vertex shader. `ground_shadow.vs` set
`fragWorldPos = vertexPosition` assuming **world** space, so `u_lightVP * fragWorldPos` projected the
shadow into garbage → **no shadow ever appeared in-game**. The headless `shadow_proj`/`shadow_cast`
passed only because they use raylib's `BeginMode3D`, which keeps the view in MODELVIEW
(`transformRequired=false`, coords stay world-space).
- **Fix**: `maps/toolkit/ground_shadow.c` `GroundShadow_Begin` folds `inverse(rlGetMatrixTransform())`
  into the uploaded `u_lightVP` (`transform` = view in-game, identity otherwise → correct in both
  paths). Verified headlessly (rlvk_visual_test, MyBeginMode3D-style camera → shadow lands).
- **How it was found**: the human noted the game uses `DrawCoreCube` (absolute coords, **no**
  `rlPushMatrix`), not raylib `DrawCube`. Reproducing the game's `MyBeginMode3D` camera in the
  headless test made the shadow vanish; a `fwp`/`u_lightVP` probe showed the projection (not the
  texture) was wrong; the trigger was the view-in-`transform` immediate-mode path.
- **Character self-shadow (`surface_lit.fs`, mesh/`DrawModel`)** is unaffected — the mesh path
  builds `fragWorldPos` from `matModel` (world space), no `transformRequired`.

**Bug 2 — a real but game-irrelevant rlvk bug (found while chasing Bug 1):** raylib's `DrawCube`
uses `rlPushMatrix/rlPopMatrix`; `rlvk`'s `rlPopMatrix` reset `transformRequired` only when the
**shared** `stackCounter==0`, which never fires while `BeginMode3D`'s PROJECTION push is outstanding
→ `transformRequired` leaked into the next custom-UBO draw. Fixed with a MODELVIEW-only depth counter
(`rlvk_matrix.inl` + `rlvk_state.inl`, HANDOFF §7.26). The **game never hits this** (`DrawCoreCube`
has no push/pop), but it's a correct fix and `shadow_pipeline` guards it.

Everything below (Session 5 and earlier) is kept as the reasoning trail but is **superseded** where
it conflicts with the above.

### SESSION 6 — PERFORMANCE PASS + SESSION CLOSE (2026-07-22)

**Status: shadow WORKS and looks good (soft, character-shaped, no banding). Perf is the open issue.**
On the dev machine (Intel Iris + MoltenVK) enabling the real shadow drops **60 → ~30 FPS** (~16 ms).

What was tried, and what it tells us (this is the useful part — a map of the perf space):
1. **Shadow-map resolution 2048 → 1024: NO FPS change.** ⇒ the cost is NOT fill-rate / O(res²). Also
   made the shadow *worse* — at the raking sun angle a lower-res map shows coarse diagonal texel
   STREAKS (projective aliasing). Reverted to 2048. Do not lower it.
2. **Removed the depth→R32F copy pass + the §7.10 depth-sample twin bounce** (capture depth-as-color
   straight into an R32F color attachment, like the `shadow_cast` test): **60→... 20→30 FPS.** This
   is the ONE thing that helped — it removed ~2 render/blit encoders per frame. ⇒ the cost is
   *per-pass / per-encoder fixed overhead*, not fill.
3. **Gated out the non-caster draws in the shadow capture** (HP/mana text bars, fake blob shadows,
   pillar wireframes, circle outlines, lines — via `EnvShadow_IsCapturing()`): **NO FPS change.**
   ⇒ the cost is NOT the extra geometry either.

4. **THE DEBUG PREVIEW WAS INSIDE THE MEASUREMENT (found last, invalidates the above).** `main.c`'s
   "SHADOW MAP DEBUG" box was gated on `EnvShadow_IsEnabled()` — so it only ran **after pressing J**,
   i.e. it was part of every "shadow costs 30 FPS" number in steps 1–3. It minifies the entire
   **2048×2048 R32F (16 MB)** map into a 220 px box with **POINT filtering and no mipmaps**: ~48 k
   scattered, cache-missing texel fetches per frame. Now gated behind `WUXING_SHADOW_DEBUG=1`
   (default OFF). **Steps 1–3 were measured with this cost mixed in and must be re-tested before
   their conclusions are trusted.**
   → **Lesson: never leave a debug visualiser gated on the same switch as the feature you're
   profiling.** It makes the feature look expensive and silently poisons every A/B.

**Conclusion (judgement, PARTLY INVALIDATED by finding 4 — re-measure first):** the remaining ~16 ms
was believed to be the **fixed overhead of the capture render pass itself** on this old Intel/MoltenVK — the FBO bind + the 2048² clear + the
scope-open/close layout barriers + the encoder switch — plus the player model being **fully skinned
twice per frame** (once for the shadow, once for the main view). Neither is reducible by touching
geometry or resolution. Removing *whole passes* is what moved the needle (step 2); nothing else did.

5. **PRIME SUSPECT IDENTIFIED (rlvk §7.27, still OPEN):** `rlvk`'s `Caps.noSampledDepth` depth
   **twin is bounced at every scope close** (`depth-image → buffer → R32F twin`,
   `width*height*4` each way) — **unconditionally**, even though since the depth-as-color change
   **nothing ever samples this twin** (we read the R32F *color* attachment). At 2048² that is
   **~32 MB/frame of pure waste**, and it matches the signature exactly: per-pass cost, unmoved by
   resolution or geometry, only removing whole passes helps.
   Suspect files: `rlvk_renderpass.inl` (the scope-close bounce) + `rlvk_texture.inl` (twin creation).
   **Two fixes were attempted and BOTH regressed validation — documented in `rlvk/docs/LANDMINES.md`
   §7.27 so they are not repeated:** (a) lazy "only bounce twins that were sampled" desyncs the depth
   layout bookkeeping → reintroduces `VUID-…-oldLayout-01211`; (b) honouring `useRenderBuffer=true`
   fails because **raylib's own `LoadRenderTexture` passes `true`**, so it strips the twin from every
   render texture and breaks soft particles. Both were reverted; tree is back to 17/17, VUID 0/1/0.

**Remaining problems / untried levers (for whoever picks this up):**
- **§7.27 is the highest-value open lead** — see above. A correct fix keeps the depth image's layout
  transitions consistent whether or not the bounce runs, and must not key off `useRenderBuffer`.
  Gate it on `VALIDATE=1` for `soft_depth`/`soft_ground`/`depth_rt` (baseline 0/1/0).
- **FIRST: re-measure with the debug preview off** (it is now off by default). If FPS with J is back
  near 60, the whole "shadow is expensive" premise was largely the preview and nothing else is
  needed. If it's still ~30, re-run levers 1 and 3 — their earlier verdicts were confounded.
- **Skin the player model ONCE per frame** and reuse the posed mesh for both the capture and the main
  pass (currently `CharacterModel_Draw` re-skins in each). This is a `character/` module change and is
  the most likely real win if the skinning (not the pass overhead) dominates. **Profile first**:
  `RLVK_GPU_TRACE=1` prints per-section GPU ms — that settles skinning-vs-pass-overhead definitively.
  Every perf guess this session was made without that profile; get it before more work.
- The 2048² **clear** is the one remaining O(res²) item; if the profile shows it dominating, a
  cheaper clear or a tighter (caster-fit, not arena-fit) light frustum at a smaller map could help
  without the grazing-angle streaks — but resolution alone (step 1) already proved fill isn't it.
- Only `DrawSandbox3D` gates non-casters; `GameScreen_Draw3D`/`Boss`/`Formation` (SCREEN_GAME) do
  NOT yet — apply the same `EnvShadow_IsCapturing()` gate there if the real game screen is slow.
- Soft shadows at the raking sun angle are inherently aliasing-prone; the current fix is a contiguous
  7×7 PCF (`GROUND_SHADOW_PCF_*` in `ground_shadow.fs`). Bilinear-filtering the R32F map would help
  but R32F linear-filter support is unreliable on GL3.3/Intel (why it's forced POINT).

**STATE OF THE TREE AT SESSION END (2026-07-22):**
- ✅ **Shadow works and looks good.** Soft, character-shaped, correctly placed, no banding/stripes.
- ❌ **Perf unresolved: ~30 FPS with J on (60 off).** Every lever tried is listed above; the one
  identified-but-unfixed cause is **rlvk §7.27** (the always-on depth-twin bounce).
- **LANDED (working, keep):** `ground_shadow.c` inverse-view fold · `env_shadow.c` depth-as-color
  capture (no copy pass, white "far" clear) · `ground_shadow.fs` contiguous 7×7 PCF ·
  `shadow_depth.fs` emits `gl_FragCoord.z` · rlvk §7.26 `mvStackDepth` matrix fix ·
  `main.c` shadow-map debug preview now behind `WUXING_SHADOW_DEBUG=1` (was ON whenever the shadow
  was, polluting every perf measurement) · `EnvShadow_IsCapturing()` + `DrawSandbox3D` non-caster gate.
- **REVERTED (do not retry as-is):** both §7.27 fix attempts — see `rlvk/docs/LANDMINES.md` §7.27.
- **Verification state:** rlvk suite **17/17**, `VALIDATE=1` baseline **soft_depth 0 / soft_ground 1
  / depth_rt 0**. rlvk working tree clean vs HEAD. Use these exact numbers as the regression gate.
- **Resolution stays 2048** — lowering it does not help FPS and visibly degrades the shadow.

**Top lessons from this multi-session hunt (the meta-lessons):**
1. **The 4-session "rlvk descriptor bug" never existed.** It was diagnosed as "MoltenVK drops the 2nd
   push-descriptor to binding 0." Disproven decisively: the descriptor was byte-correct, and the
   *bound-set path failed identically*. When **two independent binding mechanisms fail the same way,
   stop blaming the binding** — probe what the shader actually *computes*. Here it was `u_lightVP` /
   `fragWorldPos`, not the texture.
2. **The real bugs were mundane and CPU-side:** (a) an rlvk matrix-stack leak (`rlPopMatrix` gating
   its `transformRequired` reset on the shared `stackCounter`, §7.26), and (b) the game's
   `MyBeginMode3D` putting the view matrix into `transform`, making immediate-mode `vertexPosition`
   **view-space**, which the ground receiver assumed was world-space.
3. **Test the game's real path, not a lookalike.** The headless repro used raylib `DrawCube` /
   `BeginMode3D`; the game uses `DrawCoreCube` (no `rlPushMatrix`) / `MyBeginMode3D` (view in
   transform). Those differences were the entire bug. The user catching "we use DrawCoreCube, not
   DrawCube" is what unblocked it.
4. **Perf: measure which axis moves the number.** Resolution (fill) and geometry both did nothing;
   only removing whole passes did. That single fact redirects all future perf work here.

---

## SESSION 5 (2026-07-21) — FULLY ROOT-CAUSED: an rlvk MoltenVK push-descriptor bug (READ FIRST) [SUPERSEDED — see Session 6; the "second push dropped" conclusion was WRONG]

The shadow algorithm is **100% correct** — proven by two passing headless rlvk scenarios
(`shadow_proj`, `shadow_cast`) that reproduce the game's exact capture→copy→sample→`M*v` chain at
game scale and DO render a correct ground shadow (PNGs verified). The reason it fails in-game is a
**renderer bug**, isolated to a third headless scenario, `shadow_pipeline`, which FAILs:

**Root cause:** on the MoltenVK **push-descriptor** path, when the main render pass already has a
prior 3D draw, the immediate-mode ground receiver's **per-draw texture push**
(`rlvkPushTexture` → `CmdPushDescriptorSetKHR` binding 0, in the batch draw loop) **does not take
effect** — the fragment shader samples the **default white texture** (reads 1.0 = "no occluder")
even though: the shadow-map RT is correctly bound (its id shows in the `RLVK_DEBUG_FLUSH` log),
populated (a `rlReadTexturePixels` readback shows 3204 occluder texels), transitioned to
`SHADER_READ_ONLY`, and the shader's **UBO** (`u_lightVP`/`u_on`, pushed via `rlvkBindShaderUbos`
just before) DOES arrive (an in-shader red probe confirmed `u_on`). The discriminator: the UBO push
is the *first* set-0 push in the main pass (the default-shader pollution uses push constants, no UBO
push) → it works; the texture push is the *second* set-0 push to binding 0 (pollution's batch draw
already pushed the white default there) → **MoltenVK drops the second push to the same binding
within one render pass.**

Confirmed NOT the cause (each ruled out with a probe): matrix convention (M*v, `shadow_proj`
Δ0.002), projection (`u_lightVP` delivered — proj gradient sane), capture, copy-flip orientation,
R32F copy format, texture binding value (flush log shows the RT id), the default-texture
substitution branch (`rlvkPushTexture`), image layout, copyRT content (readback), the set-0 cache,
the texture dedup (force-pushed, still fails), push-constant↔push-descriptor interaction (custom-
shader pollution fails identically).

**Repro:** `scripts/run_rlvk_visual_test.sh shadow_pipeline` — FAILs; delete the two "pollution"
`DrawCube`s in `sc_shadow_pipeline` and it PASSes (isolating the trigger to "a prior 3D draw").

**Fix candidates (Renderer agent — a real rlvk fix, not a game workaround):**
1. Route set-0 through the **pool-ring** (bound descriptor sets, one consistent `vkUpdateDescriptorSets`
   per draw) instead of incremental push-descriptor writes when a binding changes mid-pass.
   `RLVK_FORCE_POOL_RING=1` currently **segfaults** on this device — that path has a latent bug that
   must be fixed first (and note Mali/Android already relies on the pool-ring path).
2. Or coalesce the UBO + texture writes into a **single** `CmdPushDescriptorSetKHR` per draw and
   re-issue the full set-0 whenever any binding changes, so no binding depends on a prior push
   surviving.

The game shaders / env_shadow / ground_shadow are all correct as they stand; do not change them for
this. The two green scenarios are permanent guards that the algorithm stays correct.

`ground_shadow.fs` currently has `GROUND_SHADOW_DEBUG_PROJ 1` (floor-projection viz) — set back to 0
after the renderer fix lands.

---

## SESSION 4 (2026-07-21) — CONVENTION SETTLED BY A HEADLESS TEST: use `M*v`

**Bottom line: the receiver shaders MUST use `u_lightVP * vec4(worldPos,1.0)` (M*v).**
Settled by an isolated, self-checking rlvk test — not code-reading, not in-game guessing —
so it must not be re-litigated. Run it: `scripts/run_rlvk_visual_test.sh shadow_proj`.

**The test (`sc_shadow_proj` in `third_party/vulkan/tests/rlvk_visual_test.c`):** builds the same
light VP the game does, uploads it as a custom `uniform mat4` via `SetShaderValueMatrix`, has a
fragment shader OUTPUT the projected UV as a color, reads it back, and compares BOTH multiply
orders to the CPU `ProjectLS` formula (the one proven to match the captured shadow texels).
Result:
```
CPU=(0.702,0.583)   M*v=(0.702,0.584) Δ=0.002   v*M=(0.431,0.529) Δ=0.324
```
`M*v` reproduces the CPU projection; `v*M` is off by 0.324 → it lands the shadow at the wrong UV,
which is exactly the "shadow drifts far from the caster" screenshots. **This makes the earlier
`v*M` conclusion wrong and retires it.**

**Why `v*M` *looked* like it worked and `M*v` looked like "no shadow" in-game (the reconciliation):**
the game always draws a separate FAKE blob shadow (`Environment_DrawSmartShadow`) right under the
feet. With the correct `M*v`, the real shadow also lands at the feet → it overlaps the fake blob →
"no new shadow visible." With the wrong `v*M`, the real shadow drifts away from the fake blob → a
distinct (mis-placed) patch appears → "now I see a shadow." So the presence/absence was the fake
blob overlap, not the real shadow's correctness. To VISUALLY confirm the real shadow, temporarily
disable the fake blob (or look for the feet shadow deepening) — do not judge by "is there a patch
away from the feet."

The H-dump already independently confirms the depth side of `M*v`: captured caster depth 0.6949 vs
CPU `M*v` z 0.6952, and the line-walk PASSes at t=0.5–1.5 — so projection (test) + depth (dump) both
check out for `M*v`.

**What happened:** mid-session I flipped both receivers to `M * v` after reading rlvk's upload
(`rlSetUniformMatrix` writes plain column-major `m0..m15`) and shaderc (never rewrites matrix
majorness) and concluding the UBO mat4 must be ColMajor ⇒ `M*v`. **Live test disproved it: `M*v`
produced ZERO shadow anywhere on the map.** Reverted to `v*M`, which is the state that renders.

**Why `v*M` is right (the decoration is RowMajor for a CUSTOM uniform):**
**Kept improvement:** `surface_lit.fs`'s hardcoded `1.0/1024.0` PCF texel is now a pushed
`u_shadowTexel` uniform (`core/surface_material.c`) tracking the real map size (2048 desktop /
512 Mali), matching what `ground_shadow` already did.

**Coverage / raking note:** only geometry wrapped in `GroundShadow_Begin/End` receives the ground
shadow; the airborne screenshots' far-offset shadow is the *correct* long shadow of a caster metres
up under a ~50° raking sun, returning under the feet on landing.

**Method note for the next session:** the flip-flopping was caused by judging convention from
in-game visuals (confounded by the always-on fake blob). Don't. The `shadow_proj` scenario answers
it in 20 s headless; extend that test (add a real capture→sample→compare) before touching the game
if a NEW shadow bug appears.

---

## SESSION 3 UPDATE (2026-07-19 — superseded by Session 4 above for the drift bug)

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
