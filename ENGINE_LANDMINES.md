# ENGINE_LANDMINES.md — Cross-cutting traps every module can hit

> Lessons that are **not** owned by one module — any agent touching GL, shaders, rendering, or the device build can repeat them. Read this before touching GL/shaders or shipping to Android.
>
> Format per entry: **Symptom → Cause → Rule** (see `DOC_ARCHITECTURE.md` §6). Module-local lessons stay in `module/docs/LANDMINES.md`; only promote here when another module could hit it.

## Index

| # | Trap | Bites |
|---|---|---|
| 1 | Raylib batching hazard (depth-state leak) | Anyone drawing with immediate mode + depth state changes |
| 2 | `rlFrustum` / camera near < 1.0 → blank render | Anyone tuning camera clip planes / custom projections |
| 3 | Lit material invisible in the dark night arena | Anyone drawing lit ground/geometry |
| 4 | Mali: `fract(sin(...))` noise hash dies | Any shader using hash noise, on-device only |
| 5 | Mali: top-84px is a mandatory gesture zone | Any on-screen UI/touch target |
| 6 | Android: black screen / crash on device | Anyone changing the Android build flags |
| 7 | `CreateEmitter` needs per-frame target update; name collision | Anyone using the emitter systems |
| 8 | Per-instance uniform changes → stale-UBO scrambling (rlvk) | Any VFX that calls `SetShaderValue` between instances |
| 9 | `matModel` is model×**view** → `fragPosition` is NOT world space | Any shader doing positional lighting/effects |
| 10 | `SetShaderValue` writes to the **active** shader under rlvk | Anyone setting uniforms outside `BeginShaderMode` |

---

## 10. `SetShaderValue` targets the ACTIVE shader under rlvk, not the one you passed

- **Symptom:** a uniform you set on shader A never arrives — or worse, silently
  lands on shader B and corrupts one of ITS uniforms. No error, no warning. The
  feature driven by that uniform behaves as if pinned to whatever was there.
- **Cause:** rlvk's `rlSetUniform` writes into
  `RLVK.shaderSlots[RLVK.State.activeShaderSlot]` — the currently **bound**
  shader. raylib's `SetShaderValue(shader, ...)` looks like it addresses the
  `shader` you hand it, and under desktop GL it effectively does (the location is
  program-scoped). Under rlvk the `shader` argument only supplies the *location*;
  the *destination* is whatever is active. Setting a uniform before its
  `BeginShaderMode` therefore writes it into the previous shader.
- **Rule:** every `SetShaderValue` must sit **between** `BeginShaderMode(sh)` and
  `EndShaderMode()` for that same `sh`. Never hoist uniform sets out of a shader
  scope "to avoid repeating them" — that is exactly the mistake. When a helper
  performs the `BeginShaderMode` for you, pass the values INTO the helper rather
  than setting them at the call site (worked example: `DualFilterPass`'s
  `streakCfg` parameter in `core/post_fx.c`, added for Đợt E1b — the streak
  uniforms would otherwise have landed on the bright-pass shader).
- Same family as §9: an API whose signature implies one target while the backend
  uses another.

---

## 9. `matModel` is model×view — `fragPosition` is view space, not world space

- **Symptom:** a positional effect (point light, radial falloff, distance fade) does nothing at all — not dim, *nothing* — on every surface at once, while directional sun/ambient on the same shaders looks perfectly correct. Everything checkable checks out: the uniforms reflect, the values reach the UBO, the debug marker lands on the effect.
- **Cause:** raylib's `DrawMesh` uploads `matModel = modelTransform * rlGetMatrixTransform()`, and inside a 3D pass `rlGetMatrixTransform()` **is the view matrix** — `rlPushMatrix()` in `RL_MODELVIEW` mode flips rlgl into `transformRequired` and redirects the current matrix into `RLGL.State.transform`, which is where `MyBeginMode3D`'s `rlMultMatrixf(matView)` then lands. So the near-universal line
  ```glsl
  fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));   // "world space"
  ```
  produces a **view-space** position. Compare it against anything genuinely in world space and the distance is roughly the camera distance, so every falloff clamps to zero. Directional lighting never notices, because it uses no position.
- **Rule:** never assume `matModel` gives world space. Put both operands in the *same* space and say which one in a comment. Two working precedents: `core/vfx_light.c` converts light positions with `rlGetMatrixTransform()` before upload (so lights meet the surfaces in view space), and `maps/toolkit/ground_shadow.c` folds `MatrixInvert(rlGetMatrixTransform())` into its light-VP matrix (so the shader gets back to world). Anything reading that matrix must run **inside** the 3D pass — outside it the matrix is identity and the correction silently becomes a no-op.
- **The decoy — do not repeat it:** `fract(fragPosition)` was used to "prove" the coordinates were world space, and it painted a clean 1 m grid. It always will: `fract()` of a *translated* position is an identical grid. A debug view that cannot distinguish the failure from success is worse than none (core/`CLAUDE.md` §6). Paint a quantity that depends on the ORIGIN — the distance to a known point — or assert it numerically. Full chain + regression test: `core/tests/vfx_light_space_test.c`, `core/docs/LANDMINES.md`.

---

## 1. Raylib batching hazard (depth-state leak)

- **Symptom:** ground/environment drawn just before your effect renders with the wrong GL state — depth-write disabled, Z-buffer corrupted, soft particles broken.
- **Cause:** raylib's `rlgl` batches immediate-mode draws. State calls (`rlDisableDepthMask/Test`, `rlEnableDepthMask/Test`) change GL **immediately**, but vertices already queued via `rlBegin/rlEnd` are still un-flushed and get drawn under your new state. (Root-caused in `core/docs/PROGRESS.md` Item 3 / soft particles.)
- **Rule:** always flush the batch with `rlDrawRenderBatchActive()` **before and after** any depth mask/test change:
  ```c
  rlDrawRenderBatchActive();   // flush first
  rlDisableDepthMask(); rlDisableDepthTest();
  // ... your draw ...
  rlDrawRenderBatchActive();   // flush before restoring
  rlEnableDepthMask(); rlEnableDepthTest();
  ```

## 2. `rlFrustum` / camera near < 1.0 → blank render

- **Symptom:** whole scene renders blank/black after a camera or projection change.
- **Cause:** a near clip plane below `1.0` with the custom frustum path produces a broken projection under this engine's scale. (Logged in the meter-rescale work.)
- **Rule:** keep camera near clip ≥ `1.0`. If a change to clip planes / custom projection blanks the scene, suspect near-plane first.

## 3. Lit material invisible in the dark night arena

- **Symptom:** ground/geometry built with `EffectMaterial` (lit) renders black-on-black and disappears in the night arena.
- **Cause:** the lit shader has almost no light to work with in the dark scene, so lit surfaces collapse to black.
- **Rule:** for ground/large geometry in the night arena, use self-shaded vertex-color gradients instead of a lit material. Don't debug it as a "missing geometry" bug.

## 4. Mali: `fract(sin(...))` noise hash dies

- **Symptom:** on-device (Mali GPU, e.g. Samsung A33) only — an aura/effect turns into an invisible black hole or static; fine on desktop.
- **Cause:** `fract(sin(x))` hash noise loses precision at large domains on Mali. Note: **invisible ≠ shader failure** — a failed shader draws WHITE; invisible means the math degenerated.
- **Rule:** use a non-`sin` hash in `noise.glsl`. When something is invisible (not white) only on device, suspect a precision-degenerate hash before anything else.

## 5. Mali: top-84px is a mandatory OS gesture zone

- **Symptom:** finger taps on UI near the top of the screen do nothing on device, yet `adb shell input tap` at the same spot "works".
- **Cause:** the top 84px is a mandatory OS gesture zone that eats touches; `adb` bypasses the OS so it masks the bug.
- **Rule:** keep interactive UI below `y = 90`. Never trust `adb tap` to validate touch reachability.

## 6. Android: black screen / crash on device

- **Symptom (black screen):** app runs but shows only black; **Symptom (crash):** hard crash on launch.
- **Cause:** black screen = raylib 6.0 `CUSTOMIZE_BUILD` landmine (`EndDrawing` never swaps buffers). Crash = `-DGRAPHICS` is ignored on Android; ES2 instancing pointers are NULL on Mali.
- **Rule:** build Android with `-DOPENGL_VERSION="ES 3.0"` (not `-DGRAPHICS`, not ES2). After changing build flags, delete the raylib build cache. (Verified on Samsung A33 / Mali.)
- **STALE-DOC WARNING (2026-07-22):** the rule above belongs to the **GLES fallback** path. Since 2026-07-17 the Android build runs the **Vulkan backend (rlvk)** on real Mali hardware (`third_party/vulkan/docs/PROGRESS.md`, HANDOFF §7.11–7.23). Do not read this entry as "Android = GLES" — it cost one wrong conclusion already. What that means for renderer work: rlvk fixes DO reach Android, **except** anything gated on `Caps.noSampledDepth`, which is detected as *portability-subset AND vendorID 0x8086* (`rlvk_frame.inl:322`) — i.e. MoltenVK-on-Intel only, never Mali. The depth-twin machinery (§7.10/§7.27/§7.29) therefore does not exist on device.

## 7. Emitter systems: per-frame target + name collision

- **Symptom:** an emitter created with `CreateEmitter` doesn't follow its target; or edits to "EmitterSystem" affect the wrong thing.
- **Cause:** `core/emitter_system.h`'s `CreateEmitter` requires per-frame `UpdateEmitterTarget` calls. Separately, `main.c`'s `EmitterSystem_Update(dt)` is an **unrelated same-named system** in `skill_helper.c`.
- **Rule:** call `UpdateEmitterTarget` every frame for managed emitters. Don't assume the two `EmitterSystem` names are the same system — verify which one you're editing.

## 8. Per-instance uniform changes → stale-UBO scrambling (rlvk/Vulkan)

- **Symptom:** an effect renders correctly most of the time, then intermittently comes out as **small rectangles in scrambled positions or wrong colors**. Worse with several instances on screen at once, or while the camera moves; the next frame can look fine.
- **Cause:** changing a uniform between instances forces one batch flush — and one UBO snapshot — per instance. When rlvk's per-frame bump arena filled up, the UBO push was silently **skipped** and the draw inherited the *previous* push: stale `mvp` (wrong transform → scrambled quads) plus stale uniform values. Fixed in the backend (reserve-before-record, `third_party/vulkan/docs/HANDOFF.md` §7.28, guard scenario `ubo_arena`); the pattern is still the one that stresses the arena hardest.
- **Rule:** don't flush per instance when you can avoid it — carry per-instance variation in a **vertex attribute** (color/normal/UV channel) instead of a uniform, and keep uniforms to values shared by the whole batch. If you must change a uniform per instance, keep the instance count bounded. Also: never let a shader's noise domain depend on an unbounded value — wrap it with `fract()` first, or a bad frame degenerates into flat blocks (see `core/shaders/smoke_column.fs`).


## Raster-state flush also covers CULLING (27/07/2026)

§1's flush rule was written about depth state. It applies to back-face culling
just as literally: `core/ribbon_strip.c` disabled culling immediately before
`rlBegin` without a flush, so the disable never reached its own quads and any
ribbon presenting its back face — e.g. a ring lying flat on a plane via
`RIBBON_FIXED_NORMAL` — rendered NOTHING, while camera-facing ribbons looked
fine. Fixed in core; details in `core/docs/LANDMINES.md`.

## rlvk: a SECOND sampler in a shader unbinds the first (28/07/2026)

**Symptom.** Every particle draws as a flat, uniformly bright SQUARE — soft
interior gone, quad edges hard. Anything textured through the affected shader
loses its texture; a 1x1 white texel across a quad is a square.

**Cause.** `core/shaders/particle_lit.fs` gained a second `uniform sampler2D`
(`u_cameraDepthTex`, pulled in by `common/soft_particle.glsl`). rlvk's shaderc
pass rebases binding indices, and going from one sampler to two moved
`texture0`, so nothing was bound to it. The shader COMPILES cleanly — rlvk
reported "shader program compiled (26 uniforms)" — so there is no error to find.

**Rule.** Under the Vulkan backend, treat "how many samplers this shader
declares" as an interface that other things depend on. Before adding a second
sampler to a shader that has one, verify on-device; if the texture goes flat,
that is this. The mere DECLARATION is enough — the squares appear with the new
sampler's feature switched off and never read, which is the cleanest way to tell
this apart from a sampling bug.

**Diagnosis shortcut.** Set the new feature's uniform to 0 so its sampler is
never read. Still square = binding (this landmine). Square only when the feature
is on = the sampling maths.

Related, same session: a shader that FAILS to compile does not come back with
id 0 — raylib hands over the default shader and rlvk logs the GLSL error and
continues, so `shader.id != 0` answers "did something get bound", not "did mine
compile". Check that a uniform you know the shader declares resolves; see
`ParticleLighting_Begin` in `core/particle_system.c`.

## A plane-pinned ribbon dashes where it CURVES (29/07/2026)

- **Symptom:** a `RIBBON_FIXED_NORMAL` / `RIBBON_WORLD_UP` strip renders as a row
  of dashes — but only where the path bends, worse the further the camera is, and
  never in `RIBBON_CAMERA_FACING` on the same path with the same code.
- **Cause:** the width vector `side = cross(tangent, planeNormal)` rotates with
  the tangent, so the fraction of it that survives projection changes ALONG the
  strip. Measured on a real bench path (lemniscate, sandbox camera at 8.4 m):
  **1.00 at one end of the band and 0.08 at the other — 13x within a single
  strip.** The thin end is sub-pixel and dashes; the wide end is solid. A
  straight path holds the tangent constant and therefore the factor, which is why
  the artefact tracks curvature. A camera-facing strip is immune by construction:
  its side vector is built perpendicular to the view, factor 1 everywhere.
- **Rule:** fixed in `core/ribbon_strip.c` for every consumer — below
  `RIBBON_MIN_PROJECTION` (0.35, ~70 degrees of foreshortening) `side` is blended
  toward the camera-facing side vector, reaching it at true edge-on. **Rotate it,
  do not widen it.** The first attempt held the projected width by widening the
  band and paying in alpha (conserving brightness x width, which is the right law
  for a DISTANCE floor) — that converts a thin stretch into a DIM stretch, and a
  dim stretch between two bright ones still reads as a break. Measured worst
  factor over every camera angle and phase: 0.069 raw, 0.342 blended.
- **Diagnostic that cost four rounds to learn.** When one variant of an effect
  breaks and another does not, enumerate what actually DIFFERS between them and
  rule the list out one item at a time — and when a hypothesis cannot be tested
  by eye, MODEL IT NUMERICALLY rather than shipping the next plausible fix. Three
  rounds went to candidates that each looked obvious (mask frequency, sub-pixel
  geometry, mask energy distribution); twenty lines of arithmetic against the
  real path and the real camera settled it and produced the regression test
  (`Test_Foreshortening`, `core/tests/swept_trail_test.c`). The decisive fact was
  available the whole time and came from the owner: the THINNEST geometry of the
  three (a 1:40 filament) never dashed while a 1:20 blade did.

## The anti-bowtie check must run on the side vector you actually DRAW (29/07/2026)

- **Symptom:** a ribbon pinches to nothing somewhere along its length and crosses
  over itself — a dark wedge in the middle of an otherwise clean band.
- **Cause:** `DrawRibbonStripEx` keeps `side` continuous point-to-point by
  flipping it when it opposes the previous point's. That check stored `prevSide`
  **before** the foreshortening blend that runs a few lines later, so continuity
  was enforced on the RAW side vectors while the quads were built from the
  BLENDED ones — and two blended sides are free to point opposite ways even when
  their raw versions agree.
- **Rule:** record continuity as the LAST thing before the geometry is emitted.
  Anything that modifies `side` — a blend, a twist, a per-point rotation — has to
  happen before that store, and the ordering is asserted in
  `core/tests/swept_trail_test.c`.
- **Related, same symptom from the other end:** a cloth/rope solver whose
  constraint is stretch-only lets adjacent nodes collapse onto each other or pass
  THROUGH each other. A zero-length or reversed segment gives a garbage tangent,
  which flips the side vector, which is the same bowtie. Constrain from both
  sides — a floor at ~1/3 of the rest spacing lets the ribbon bunch without ever
  degenerating.

## A boolean that means "exactly" when you read it as "at least" (29/07/2026)

- **Symptom:** a simulated ribbon 6 m long rendered as a short, stiff spindle
  stuck near its emitter — it stopped following the path at all, and the flow
  animating along it had almost no length left to travel.
- **Cause:** `Ribbon_ConstrainSegment(a, b, rest, pinned, stretchOnly)` was called
  twice per segment: once with `true` for the inextensibility CEILING, then once
  with a shorter length and `false`, intended as a FLOOR against nodes collapsing
  into each other. `stretchOnly = false` does not mean "also enforce a minimum" —
  it means "force the distance to be EXACTLY restLen". The second call therefore
  overwrote the first and pinned every segment to a third of its rest spacing.
- **Rule:** when a constraint can be a ceiling, a floor, or an equality, give it
  a **named mode**, never a bool. `RIBBON_CONSTRAIN_MAX / MIN / EXACT` makes the
  wrong call unspellable; `stretchOnly = false` reads like the opposite of what
  it does. More generally: a boolean parameter whose `false` branch is not the
  negation of its `true` branch is a mis-named enum.
- **How it was found:** by pulling one frame out of the owner's screen recording
  with `qlmanage -t` and looking at it. The trail's rendered length against the
  path it was supposed to trace was obvious in a single still — after several
  rounds of reasoning had not found it.

## A ribbon pinches where the path points AT the camera — and a sign flip cannot fix it (29/07/2026)

- **Symptom:** a camera-facing strip closes to a single point somewhere along its
  length and re-opens rotated — two wedges meeting at a vertex. Once or twice per
  loop on a path that curves through the view direction.
- **Cause:** `side = cross(tangent, primaryNormal)` carries no direction when the
  tangent is parallel to `primaryNormal` — for a camera-facing strip, wherever the
  path runs straight at or away from the viewer. `ComputeSideVector` then fell
  back to an unrelated reference vector, so `side` did not flip, it **jumped to a
  different direction entirely**.
- **Rule:** the anti-bowtie continuity check only ever NEGATES `side`, which
  cannot undo a 90-degree jump — do not reach for it here. Detect the degenerate
  cross product by its LENGTH and **parallel-transport** the previous side vector
  instead: re-orthogonalise it against the new tangent. The strip then narrows
  through the degenerate stretch, which is correct (it is being seen end-on),
  rather than tearing. Fixed in `core/ribbon_strip.c` for every consumer.
- **Diagnostic:** a pinch that recurs at the same points of a repeating path is
  geometric degeneracy, not shading and not the mask. Ask what the cross product
  is doing where the path aligns with the reference direction.
