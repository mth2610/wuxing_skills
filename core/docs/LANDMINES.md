# core — Landmines

> Distilled, reusable debugging lessons for the **core** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting traps (batching hazard, depth-test-vs-mask, `rlFrustum near<1.0`, lit-material-dark, emitter collision) live in root `ENGINE_LANDMINES.md` — read that too.
> Long session logs and open backlog are in `PROGRESS.md`, not here.

### An additive sprite cannot retain contrast over a bright destination
- **Symptom:** an orange/yellow emitter looks rich against black but its centre
  becomes pale or almost white over a bright sky, terrain, or area light.
- **Cause:** `VFX_BLEND_ADDITIVE` computes source + destination. It never
  replaces or darkens the already-bright destination, while HDR/bloom compresses
  the resulting high values further toward white. This is a blend-law limit, not
  a Vulkan sampler or lighting defect.
- **Rule:** author a shaped emissive effect as an unlit alpha **core** (to retain
  its element hue) plus a larger, lower-energy additive **halo** (to emit). Do
  not use one high-energy additive quad for both jobs. Submit the core through
  `ScreenDistort_BeginVFXBody()` and the halo through
  `ScreenDistort_BeginVFXEmission()`; `bright_vfx_isolation_test.c` is the
  renderer-independent regression probe for this blend law.

### GPU particle blend state must not inherit the CPU particle pass
- **Symptom:** a GPU VFX appears additive in some frames/scenes but alpha-blended
  in others, especially when CPU particles are present too.
- **Cause:** CPU particle drawing changes blend mode per batch and restores alpha
  afterwards. An outer additive scope is not a stack; it cannot restore GPU
  state after that inner change.
- **Rule:** `ParticleManager_Draw` owns the GPU additive scope explicitly after
  CPU drawing. Never wrap the manager in a presumed blend mode at its call site.

### Custom texture binding must go through `SetShaderValueTexture`
- **Symptom:** `texture(u_myTex, ...)` reads back 0 in a shader no matter which slot you bind, for a custom multi-texture shader.
- **Cause:** manual `rlActiveTextureSlot()`/`rlEnableTexture()` binding silently doesn't reach a raylib `Shader`. Hit independently by `flow_map.c` and the soft-particle depth bind.
- **Rule:** bind custom textures with `SetShaderValueTexture()` and let raylib manage the unit; don't hand-roll `rlActiveTextureSlot`/`rlEnableTexture` for a raylib `Shader`.

### Depth-linearization near/far must match the real projection, not the clip-plane globals
- **Symptom:** every depth sample crushes to near-zero; soft-particle occlusion looks uniformly "no occlusion".
- **Cause:** `rlGetCullDistanceNear/Far()` reflects `rlSetClipPlanes`, but `MyBeginMode3D`'s `rlFrustum(...)` hardcodes a different near (10.0). Two unrelated globals; using the clip-plane one is wrong.
- **Rule:** linearize depth with the near/far the actual projection uses. There is no shared source of truth — if `MyBeginMode3D`'s near/far changes, update the `SOFT_PARTICLE_SCENE_NEAR/_FAR` constants in `core/screen_distort.c` too.

### Prefer a shader-side debug view over a CPU numeric readback
- **Symptom:** a CPU 3-point depth readback gives misleading numbers even though it's "numeric".
- **Cause:** the sampled world points don't correspond to the actual visible front-surface fragment at that pixel from an oblique camera.
- **Rule:** numeric beats screenshot-guessing, but a numeric check is only as good as whether it queries the *actual rendered fragment*. Prefer a shader-side debug view (real per-fragment value) over a CPU approximation. Avoid pre-clamped debug views — they hide narrow signals; flag near-zero explicitly.

### Meter-scale: a correct skill file can still look wrong via shared code
- **Symptom:** a fully-converted skill still renders oversized/displaced effects.
- **Cause:** the shared functions it calls (`CastSkill` offsets, `SpawnImpactEffect` presets, `ProcRay_*Config` thickness, `UpdateSkillManager` enemyRadius) had un-rescaled internals.
- **Rule:** when converting a skill, trace *into* every shared function it calls and check that function's own internals are meter-scaled — don't trust a normal-looking signature. Full checklist in `PROGRESS.md` (Item 34).

### Lightning zigzag needs precomputed geometric waypoints, not physics/noise
- **Symptom:** an electric bolt drawn via a physics/noise trail renders as a straight line or a smooth "silk ribbon" sag, never a sharp zigzag.
- **Cause:** `TRAIL_TYPE_PROJECTILE` homing steer damps deviation back to straight every frame; `TRAIL_TYPE_WISP`'s `ConstrainRibbonSegment` distance-solver low-pass-filters per-node jaggedness into a flowing curve. Both are built to stay smooth by design.
- **Rule:** build a precomputed jagged polyline (perpendicular-offset kinks, no `forceField`) and drive a `TRAIL_TYPE_FOLLOWER` along it — see `SpawnLightningTrail`/`GenerateLightningWaypoints`. Don't try to make physics produce the kink.

### Check `IsKeyPressed` collisions before binding a test key
- **Symptom:** a debug toggle also cycles the map; effect looks position-dependent when it isn't.
- **Cause:** `KEY_K` is already globally bound in `main.c` (cycle maps); raylib gives no key exclusive ownership, so both handlers fire.
- **Rule:** grep `IsKeyPressed(KEY_` across `main.c`/`sandbox/*.c` before binding any new key in a harness.

---

## Tuning_RegisterFloat before Tuning_Init silently keeps the default

**Symptom.** A float registered with `Tuning_RegisterFloat` ignores the value in
`tuning.cfg` on a fresh run. The feature it drives looks dead. Editing and saving
`tuning.cfg` while the game runs makes it spring to life — which reads like a
hot-reload quirk and sends you looking in the wrong place entirely.

**Cause.** `Tuning_RegisterFloat` (`core/tuning.c:64`) only reads the config file
`if (s_configPath[0] != '\0')` — and that path is set by `Tuning_Init`. `main.c`
calls the subsystem inits (e.g. `InitParticleSystem`, :1017) well BEFORE
`Tuning_Init` (:1063), so anything registering from its own init registers before
the path exists, silently keeps `defaultValue`, and only picks the real value up
on the next mtime change.

**Rule.** Do not register tunables from a subsystem's `Init`. Register lazily on
first use (a `static bool` one-shot in the update/draw path), by which time
`Tuning_Init` has certainly run. If a default of 0 means "feature off", this bug
is invisible rather than merely wrong — see `ParticleLighting_Begin` in
`core/particle_system.c` for the shape to copy.

---

## A shader with `#include` MUST be loaded via `ResourceManager_LoadShader`

**Symptom.** A shader that compiled fine suddenly fails the moment a shared block
is `#include`d into it — or, worse, silently renders wrong because the driver
tolerated the line.

**Cause.** GLSL has no `#include`. The directive works in this project only
because `core/shader_preprocessor.c` resolves it, and that runs **only** inside
`ResourceManager_LoadShader`. raylib's plain `LoadShader` hands the file to the
driver verbatim. Hit in `maps/toolkit/map_props_ground.inl`, which used raw
`LoadShader` and broke as soon as `ground_splat.fs` gained a shared include.

**Rule.** Any `.fs`/`.vs` containing `#include` must be loaded with
`ResourceManager_LoadShader`. Adding an include to an existing shader means
checking its load site too — the two live in different files and nothing links
them. Audit with:

```bash
grep -rln '#include' --include=*.fs --include=*.vs . | while read s; do
  grep -rn "LoadShader" --include=*.c --include=*.inl . | grep -v ResourceManager | grep "$(basename $s)"
done
```

Corollary: `ResourceManager_LoadShader` caches by path, so never call
`UnloadShader` on the result.

---

## std140: a `vec3` or `float` ARRAY uniform uploads garbage under rlvk

**Symptom.** A shader's scalar uniforms arrive correctly but an ARRAY uniform is
zero or nonsense. Nothing errors. The feature driven by the array silently does
nothing, and every other diagnostic (the value is computed, the location is
valid, `SetShaderValueV` is called) reports healthy.

**Cause.** rlvk is a real Vulkan backend and packs uniforms into **std140**
blocks. In std140 every array element is padded to a 16-byte boundary — so
`vec3 arr[4]` has a stride of 16 bytes, not 12, and `float arr[4]` a stride of
16, not 4. `SetShaderValueV(shader, loc, data, SHADER_UNIFORM_VEC3, 4)` uploads
12 bytes per element, tightly packed. Element 0 happens to land correctly;
everything after it is read from the wrong offset.

**CORRECTION (23/07/2026) — this entry's original diagnosis was wrong.** It
claimed E2's `u_vfxLightPos[4]` (vec3) and `u_vfxLightRadius[4]` (float) "arrived
corrupt". They did not. `rlSetUniform` in `third_party/vulkan/rlvk/rlvk_shader.inl`
already strides array writes by 16 bytes, so a `vec3[]` upload is handled
correctly; a headless probe that compiled the real shaders and read the UBO
staging back confirmed every element landed at the right offset, before and after
the vec4 rewrite. The lights were dark for a completely different reason — see
"A positional effect lights nothing" below. The vec3→vec4 rewrite was therefore a
change that fixed nothing, and it cost a session because it was believed to have.

The rule below still stands on its own merits (it is correct under desktop GL and
any other std140 consumer, and it removes a class of trap), but do **not** cite
this entry as evidence that an array uniform is your problem: measure the staging
bytes first. The probe that does it is ~120 lines against `rlLoadShaderProgram` +
`rlGetLocationUniform` + `RLVK.shaderSlots[id].fsStage`, and it runs in ~2 s.

**Rule.** Never declare a `vec3[]` or `float[]` uniform array. Use `vec4[]` and
pack the spare component (radius into `.w`, a flag into `.a`). Correct under
std140 and under desktop GL alike, and it removes the trap rather than
documenting around it. A `mat4[]` is already 16-byte aligned and is safe.

**How it was found** (the method matters more than the fact): a debug mode in the
shared GLSL that painted each suspect quantity — world position, then
attenuation, then the count alone, then the array element alone. Position was
correct and the count was correct while the array was not, which localised the
fault in one run after several rounds of guessing had not.

---

## A positional effect lights nothing: `matModel` is model×view, so `fragPosition` is VIEW space

**Symptom.** A point light (or any radial falloff / distance fade) has no effect
whatsoever — not faint, *nothing* — on every lit surface simultaneously:
character, ground, path. The sun and ambient on those same shaders look right.
Every check passes: the pool has lights, all three shaders compile and reflect
every `u_vfxLight*` uniform, the uploaded bytes reach the UBO staging at the
correct std140 offsets, and the debug wire sphere lands exactly on the effect.

**Cause.** raylib's `DrawMesh` builds the matrix it uploads as

```c
matModel = modelTransform * rlGetMatrixTransform();
```

and inside a 3D pass `rlGetMatrixTransform()` returns **the view matrix**, not
identity: `rlPushMatrix()` in `RL_MODELVIEW` mode sets rlgl's `transformRequired`
and redirects the current matrix to `RLGL.State.transform`, so `MyBeginMode3D`'s
`rlLoadIdentity()` + `rlMultMatrixf(matView)` write the view there. Measured, not
inferred: `rlGetMatrixTransform()`'s translation row came back bit-identical to
`MatrixLookAt(camera.position, camera.target, camera.up)`'s.

Consequence: every shader in this project that writes

```glsl
fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));   // labelled "world space"
```

is producing a **view-space** position. The VFX lights were uploaded in world
space, so `length(lightPos - fragPos)` measured roughly the camera distance
(40+ m) instead of the true ~1 m, `clamp(1 - d/radius)` pinned to 0, and nothing
was ever lit — at any quality tier, on any surface, with no error anywhere.
Directional lighting never noticed because it uses no position at all.

**Rule.** `matModel` is not a model matrix here. Put the light and the fragment
in the **same** space and name that space in a comment. Two working precedents:

- `core/vfx_light.c` — `VFXLight_ShaderSpaceMatrix()` transforms light positions
  by `rlGetMatrixTransform()` before upload, meeting the surfaces in view space.
  The view matrix is rigid, so the radius (a length) needs no conversion.
- `maps/toolkit/ground_shadow.c` — folds `MatrixInvert(rlGetMatrixTransform())`
  into its light-VP matrix, going the other way, back to world.

Anything reading that matrix must run **inside** the 3D pass. `VFXLight_BindAll`
was originally called from the update block, where the matrix is identity and the
conversion is a silent no-op; it is now called immediately after `MyBeginMode3D`,
and `VFXLight_ShaderSpaceMatrix` logs a warning if it ever sees identity again.
`core/tests/vfx_light_space_test.c` asserts the call ordering in `main.c` so the
move cannot be quietly undone.

**The decoy — this is the expensive part.** Debug mode 1 painted
`fract(fragPosition)` and produced a clean 1 m colour grid, which was written
down as proof that `fragPosition` was genuine world space. It proves no such
thing: `fract()` of a *translated* position is an identical grid. The one debug
view aimed at the actual fault reported success, and two sessions were then spent
looking everywhere else — at std140 padding, at rlvk's uniform delivery, at
whether Vulkan supported dynamic lights at all.

A debug view that renders the wrong quantity is worse than no debug view
(`core/CLAUDE.md` §6). `fract(pos)` can only show the *gradient*; it is blind to
the *origin*, and the origin was the bug. What settled it in one run was mode 7:
paint `length(lightPos - fragPos)` directly. It has no dark spot anywhere, so the
two positions were nowhere near each other — a fact no amount of staring at an
unlit floor would have produced.

**Method that worked, for next time.** Order the debug modes so each isolates one
link, and always include a mode that *cannot* be faked:
`5` a flat constant (proves the block's return reaches the output at all),
`3` the count alone, `4` the array element alone, `6` the packed `.w` alone,
`7` the distance (the only one sensitive to the origin). Modes 3/4/5/6 all passed
while 0/2/7 failed, which localised the fault to "the two positions disagree" —
the only thing those two groups differ by.

**Also worth knowing (cost ~4 rounds):** the first screenshots were taken in a
scene where the caster stood off the island plateau, so the "ground" filling the
frame was the *cloud sea*, and a hard-coded green in `ground_splat.fs` appeared
only as a sliver in one corner. Before concluding "this shader has no effect on
screen", make the shader output a flat unmistakable colour and confirm you can
see *where* it is drawing at all.

## Ribbon strips could be silently back-face culled (27/07/2026)

**Symptom.** `VFX_ComposeRuneCircle` drew nothing at all. Its geometry logged as
correct — 97 points, unit-length side vectors, alpha 242, sensible world
positions — and a control shape drawn from the same function at the same spot
appeared normally. Swapping the ribbon's mode to `RIBBON_CAMERA_FACING` made it
appear; `RIBBON_FIXED_NORMAL` stayed invisible.

**Cause.** `DrawRibbonStripEx` called `rlDisableBackfaceCulling()` immediately
before `rlBegin`, with no batch flush. A raster-state change that is not flushed
on both sides does not apply to what is submitted next (ENGINE_LANDMINES §1), so
the disable never took effect for the strip's own quads. A camera-facing strip
always presents its front face and looked fine; a strip lying on a fixed plane
presents its back face from half the viewing angles and was culled entirely.

**Rule.** Flush (`rlDrawRenderBatchActive()`) around EVERY raster/depth state
change, not just depth-mask ones — culling counts. Fixed in
`core/ribbon_strip.c`, so every ribbon consumer inherits it.

**And the diagnostic lesson:** "geometry is correct" and "geometry is visible"
are different claims. The log proved the first and said nothing about the second;
what separated them in one run was drawing a control shape (a plain polygon) from
the same code at the same place — it isolated the state from the maths.

## rlDrawRenderBatchActive() resets the bound texture (27/07/2026)

**Symptom.** A cluster of soft round sprites rendered as a hard bright SQUARE —
but only from the second sprite onward; the first was correct.

**Cause.** The loop flushed the batch between sprites (needed, to change a
per-sprite uniform), and `rlDrawRenderBatchActive()` restores rlgl's default 1x1
WHITE texture as the current binding. Every quad after the flush therefore
sampled solid white: alpha 1 across the whole quad, hence the square.

**Rule.** `rlSetTexture(id)` must be re-issued AFTER every batch flush, not once
before the loop. This pairs with the flush rule (ENGINE_LANDMINES §1) — flushing
is mandatory around state changes, and flushing itself changes state.

## A shader with no consumer has never been compiled (27/07/2026)

`core/shaders/dissolve.fs` sat in the tree unused. Its first consumer (E5.4)
found it failing to compile — `'colDiffuse' : undeclared identifier` — and a
failed compile falls back to the DEFAULT shader silently, so the effect drew as
an ordinary quad with no erosion and nothing in the frame said why.

**Rule.** Treat any never-instantiated shader as unverified code, and read the
compile log the first time it is instantiated. Two smaller traps in the same
file: uniform initialisers (`uniform float edgeWidth = 0.04;`) are ignored with
a warning under this backend, so a caller that forgets to set one gets 0, not
the written default.

## A masked ribbon: which side of the strip is u = 0 (28/07/2026)

**Symptom.** An asymmetric mask on a `DrawRibbonStripEx` band comes out mirrored
— the hot edge on the wrong side of the path. It does not look like a bug on
screen; it looks like the effect is "a bit soft", which is the kind of wrongness
that survives a screenshot review indefinitely.

**Cause.** `ribbon_strip.c` emits u = 0 at `center + side*halfWidth` and u = 1 at
`center - side*halfWidth`, with `side = normalize(cross(tangent, primaryNormal))`.
Nothing about the caller's own geometry decides it. The path is the strip's
CENTRE (u = 0.5), not one of its edges — the band extends `halfWidth` to BOTH
sides of it.

**Rule.** Before authoring an asymmetric mask, work out which world direction
`cross(tangent, normal)` actually points for your path and write the derivation
down. For an arc built as `p(a) = origin + axU cos a + axV sin a` with
`axV = cross(n, axU)`, the basis is right-handed (`axU x axV = n`), so
`tangent x n` is the radial OUTWARD direction — i.e. u = 0 is the outer edge.
Worked example and a headless assertion on it: `VFX_ComposeSweepSlash`
(`core/composition/common/vc_sweep_slash.inl`, `core/tests/sweep_slash_test.c`).

## `stretchStrength` is not a 0..1 fraction (28/07/2026)

**Symptom.** Fast particles meant to read as streaks/sparks draw as ROUND DOTS,
and bloom then inflates each one into a bead. Nothing logs anything wrong.

**Cause.** `stretchFactor = 1 + speed * stretchStrength`
(`core/particle_system.c:1003`). At 4 m/s a "reasonable-looking" 0.10 gives
1.4x — visually identical to no stretch. The only prior user in the tree
(`vc_particle_upgrades_test.inl`) sets 0.04, which is where the wrong intuition
comes from: that value is for a much faster population.

**Rule.** Pick the value from the FACTOR you want at the speed you actually
spawn at: `strength = (factor - 1) / speed`. For a 4 m/s spark wanting a 5x
streak, that is 1.0, not 0.1. Also set `stretchMinSpeed` below the slowest
speed in the population, or the slow tail silently opts out of stretching.

## Thickness is a ratio against the thing's OWN length (28/07/2026)

**Symptom.** A new VFX is not broken and not ugly, but reads as the wrong
object: a slash reads as a crescent moon, a swarm's wisps read as shark fins, a
stream reads as a leaf. Tuning colours and brightness never fixes it.

**Cause.** The width was keyed to a parameter that is not the element's own
length. Three cases in one session:
- `SweepSlash`: half-width from the arc's RADIUS, so the aspect ratio moved with
  `arcRad` — 1:10 at a 2.2 rad swing, ~1:3 at a 0.6 rad flick. Right at exactly
  one sweep angle, and nothing said which.
- `SpiritSwarm`: width from the swarm's `spread` while length came from the tail
  fraction. Unrelated quantities; they happened to land at 2.4:1.
- `SpiritStream`: radius from the caller while length came from the path
  segment.

**Rule.** Derive thickness from the length of the SAME thing —
`halfWidth = ownLength * k` — and pick `k` from a target aspect ratio. Working
values from this session: a blade trail ~**1:20** against the arc it travelled;
a comet/wisp ~**1:14**; an energy stream ~**1:10**. Then assert the ratio AND
its invariance in a headless test — the invariance is the assertion that matters,
because the ratio alone passes on the broken formula too, at one parameter value.

**Related:** both ends of a moving element come to a POINT. "Zero at the
endpoint" is not the same property as "comes to a point": `sqrt(s)` is zero at
s=0 and still reaches half width by s=0.25, which draws a blunt club. A lens
(`pow(sin(PI*s), 0.85)`) is the shape; assert symmetry, not the endpoints. A
strip that starts at full width has a flat cap, and a flat cap on a short strip
is the flat base of a triangle.

## The blend law, violated: mist that renders BLACK (28/07/2026)

**Symptom.** A coloured mist/smoke population comes out near-black, on top of a
dark scene. Changing its colour barely helps.

**Cause.** It was `BLEND_ALPHA` with `render.unlit = 0`, i.e. it went through the
lighting multiply — and a night arena has almost no light to multiply by. Both
water skills had this.

**Rule.** `core/particle_system.h` states it: if it would BLOCK light, draw
`BLEND_ALPHA` and light it; if it EMITS light, draw `BLEND_ADDITIVE` and leave it
unlit. Additive output can never be darker than its background, which is exactly
why "energy mist" must be additive. Glowing smoke is TWO draws — an alpha body
plus an additive glow — never one alpha draw turned bright.

**Corollary:** `VFX_ComposeSmokePuff` is *deliberately* dark (its own header says
so) and depends on the lighting pass for brightness. Adding it to an unlit scene
as a "glow" makes a black smudge.

## A sequence called from Draw restarts every frame (28/07/2026)

**Symptom.** After an impact the game appears to enter slow motion for as long as
the effect lasts; lights stack far brighter than authored.

**Cause.** `VFX_ComposeImpactPackage` plays a `VFX_Sequence`. It was called from
a skill's *draw* path, so it re-fired 60 times a second — including its hitstop
beat, which is a time-scale effect.

**Rule.** Know which side of the line a composition sits on before wiring it:
**one-shot** (spawns a population or plays a sequence — call it once, from a
state transition in Update) versus **continuous/immediate-mode** (emits geometry
for the current frame and keeps no state — call it every frame from Draw).
Getting it backwards fails in both directions: a one-shot in Draw stacks, and a
continuous one called once draws a single frame and looks like nothing happened —
which is precisely what made the bench's ICE CRYSTAL entry appear dead when
`sync_vfx_test.py` auto-classified it as one-shot.

**Also:** `VFX_ComposeImpactPackage`'s severity is gated — `>= 0.45` fires
hitstop, `>= 0.35` fires screen distortion. A substitution for a purely visual
burst must stay under 0.45 or every hit in the game develops a stutter.

## Density belongs to the world, not to the array (28/07/2026)

**Symptom.** A row of effects along a path looks right at one range and wrong at
another — separated clumps up close, gaps far away, or the reverse.

**Cause.** Spacing was computed by stepping the path's POINT INDEX. The path had
a fixed point count regardless of length, so the metres between elements changed
with the target distance.

**Rule.** Space things by metres: `count = pathLength / spacingMetres`, clamped.
The array that describes a path is an implementation detail of the path, not a
unit of distance. (Same family as the thickness rule above: derive from the world
quantity, not from whatever number is nearby.)

## An `.inl` is not code until something includes it (28/07/2026)

**Symptom.** A restored/added `.inl` compiles cleanly — and the build fails at
LINK with an undefined symbol for the function it defines.

**Cause.** `core/composition/` is a unit build: `visual_composer.c` includes the
`.inl` files. A file present in the tree but absent from that include list is
compiled into nothing.

**Rule.** Adding or restoring an `.inl` means adding its `#include` in the same
edit. The compiler cannot warn about this, and the link error names the symbol,
not the missing include.

## Inspecting a greyscale sheet: the viewer composites alpha over white (28/07/2026)

**Symptom.** A generated mask looks blown out and nearly white when opened, while
its histogram says it is mostly dark.

**Cause.** The image is RGBA with low alpha; the viewer composites it over a
white background.

**Rule.** To judge a mask, dump RGB with alpha forced to 255 and look at that —
or trust the numbers (mean, percentile coverage) over the picture. Retuning a
sheet against a viewer artifact wastes a full round.

## The bench's play window is 5 seconds, and a bench that cycles state inside it invents symptoms (29/07/2026)

**Symptom.** The owner reported a swept trail "splitting into several strands"
now and then. Nothing in the composition can do that: the two non-FILAMENT
styles spawn exactly one strand, and the arithmetic was green on 57 assertions.

**Cause.** The BENCH, not the effect. `sandbox/vfx_test.c` stops a NEWFX play at
`s_meshTime > 5.0f` (VFXTest_Update). The H1 entry cycled its style on a 4 s
timer inside that window, so a play went BLADE (0-4 s) → RIBBON (4-5 s) → frozen,
and FILAMENT was unreachable. At the switch it called `VFX_KillSweptTrail` and
immediately created the next style — and Kill deliberately does not cut the strip
out of existence, it stops the feed and lets the strip drain. So for ~0.85 s a
fading BLADE and a growing RIBBON were on screen at once: two strands, from a
one-strand effect, once per play.

**Rule.** A bench entry's own timers must fit inside the 5 s play window, and
**state that advances per PLAY belongs on the re-trigger, not on a timer** — the
H1 entry now advances one style per trigger and TraceLogs its name. More
generally: before debugging an effect against what the bench shows, check that
the bench is showing one instance of it. A killed-but-fading effect overlapping
its replacement is the cheapest way to manufacture a symptom the effect does not
have, and the overlap is CORRECT behaviour for the API (a combo's second swing
should start while the first is still fading).

**Corollary — the other way one trail reads as several.** A `TRAIL_TYPE_FOLLOWER`
draws TWO concentric strips, and the inner white core's alpha ignores the alpha
curve entirely (`trail_system.c:961`). Additive plus bloom, that core can read as
a separate bright thread inside the band. `swept_core = 0` in tuning.cfg turns it
off, so the question is answered by looking rather than by rebuilding.

## One axis, two consumers: one needs the SIGN, the other needs CONTINUITY (29/07/2026)

**Symptom.** A FILAMENT swept trail's four strands knotted together near the
head, crossing each other, instead of running parallel as a bundle. Only at some
camera angles and only sometimes — i.e. the shape of report that gets dismissed
as "looks a bit messy".

**Cause.** One vector was serving two purposes. The swing-plane normal
`cross(chord1, chord2)` is used (a) as the ribbon's `fixedNormal`, where its SIGN
is load-bearing because it decides which side of the strip u = 0 lands on, and
(b) as the axis the filament strands are offset along. At an inflection the
curvature genuinely reverses, so the normal flips — correct for (a), and for (b)
it teleports every strand's newest node from +offset to -offset in a single
frame, which is a knot.

**Rule.** When one derived vector feeds two consumers, ask whether each wants the
raw quantity or a *stabilised* one, and keep both. Here: `normal` stays raw for
the ribbon, `lateralAxis` is the same vector flipped to keep `dot(prev, cur) > 0`
and is what the offsets use. Cheap, and the two can never fight again.

**And the second thing that capture showed:** the spread table was
`{-1.0, -0.28, 0.34, 1.0}` — near-uniform gaps, which draws a COMB (four parallel
wires), not a bundle of threads. Regularity is what the eye picks up; no colour or
width change fixes it. The headless test now asserts `minGap/maxGap < 0.85`,
which is what caught the first "irregular" table being 0.861.

## A trail assumes the gap between two samples was SWEPT (29/07/2026)

**Symptom.** A long, perfectly straight, hairline streak crossing the whole
frame, unrelated to the curve the effect was drawing — and "at some spawn
positions it comes out as two strands, at others it doesn't".

**Cause.** `VFX_ComposeSweptTrail` lays nodes along the gap between the previous
frame's tip and this frame's, interpolating sub-frame steps in between. That is
correct for motion and wrong for a **teleport**: dragging the bench's spawn point
(or an agent respawning, or a blink) moves the transform metres in one frame, and
the trail dutifully draws a straight band BRIDGING two places the weapon never
was. The second "strand" is that bridge lying next to the real curve.

**Rule.** Any effect that reconstructs a path from per-frame samples needs a
teleport discriminator, and it cannot be a fixed distance: a real swing covers
0.4 m per frame at 60 fps and 2.5 m during a 100 ms hitch. Scale it with dt and
put a floor under it — `moved > max(45 m/s * dt, 0.75 m)` — then CUT the history
rather than bridging it (a fading bridge is still a bridge). Asserted from both
sides in `core/tests/swept_trail_test.c`: every plausible swing at 30/60/144 fps
and through a hitch must pass, and a 1 m single-frame jump must be caught.

## A mask is authored for a physical WIDTH, and reusing it narrower re-scales every frequency in it (29/07/2026)

**Symptom.** The BLADE swept trail rendered as a DOTTED line — a row of dashes
following the right path — while the same code's RIBBON and FILAMENT strips next
to it were solid.

**Cause.** It borrowed `VFX_ComposeSweepSlash`'s sheet, which H1 was written to
reuse. That sheet carries cross-blade striations at `sin(u*PI*23)` — 11.5 cycles
ACROSS the band — and it was authored for the slash's band, `arcLen*0.044` ≈
0.38 m. A trail band at 1:20 against a 1.2 m tail is 0.06 m: the same texture,
six times narrower on screen, so every frequency in it is six times higher.
`LoadTextureFromImage` produces exactly ONE mip level, so there is nothing to
filter it down with — each pixel samples whichever phase it lands on, and the
strip breaks into dashes.

**Rule.** "Reuse before invent" still holds, but a mask's frequencies are only
valid at the width it was authored for. Before reusing a sheet, compare the two
consumers' widths **in metres**; if they differ by more than ~2x, author a second
sheet at the new width instead of scaling the old one. Keep the SHAPE (hot edge,
smear inward, one slow swell) and drop everything the narrower band cannot
resolve — and widen the rim in proportion, or the edge itself aliases (sigma 0.05
→ 0.16 here). `core/tests/swept_trail_test.c` asserts it by counting direction
changes in the profile: one peak passes, the slash sheet's 12 do not.

**Family:** this is the size half of VFX_PLAN §H6's rule, whose axis half already
says "a sheet authored for a flat beam produces rings when wrapped on a tube".
Both are the same mistake — a sheet carries assumptions its pixels do not state.

## A strip thinner than a pixel renders as DASHES — and the symptom is zoom-dependence (29/07/2026)

**Symptom.** The BLADE swept trail came out dotted. Replacing its mask (see the
entry above) changed nothing. The owner's decisive observation: **it depends on
ZOOM** — solid close in, broken far out, and the TAIL dashed at every zoom.

**Cause.** Geometry, not texture. A strip under ~1 px wide is rasterised where
its centre happens to land inside a pixel and dropped where it does not, which
draws a row of dashes. Two independent instances of it here:
1. Distance. A 6 cm band (1:20 against a 1.2 m tail) is many pixels at 6 m and
   sub-pixel at 90 m — hence the zoom dependence, which is the tell that
   separates this from any texture problem.
2. The tail, at every distance, because the width envelope takes it to ZERO. The
   BLADE alpha curve had 0.48 where the width curve had 0.36: the tail was
   *brighter than it was wide*, so it disintegrated instead of fading.

**Rule, two parts.**
- **Floor the width in SCREEN space and pay for it in alpha.** `minHalf =
  (minPx/2) / pixelsPerMetre`, `alpha *= halfW/minHalf`. The floor alone is worse
  than the dashes — a distant trail becomes a fat opaque worm; conserving
  width x alpha makes it fade instead. `pixelsPerMetre = screenH / (2 d
  tan(fovy/2))`, and `camera` is available via `core/camera_context.h`.
- **A taper's alpha must fall at least as fast as its width.** Anywhere a strip
  narrows to nothing, brightness must lead the narrowing, or the last stretch is
  sub-pixel while still visible. Assert `alpha(s) <= width(s)` over the taper.

**And the layer that fails FIRST is the brightest one:** `TRAIL_TYPE_FOLLOWER`'s
inner core is 0.267x the outer half-width and drawn at full white, so it goes
sub-pixel roughly four times sooner than the band around it. It is gated off
below 5 px of band width — a distance LOD, clamping down only.

**Diagnostic worth reusing:** *ask whether the artefact tracks zoom.* Texture
frequency artefacts change with distance too, but they do not vanish when the
geometry is a handful of pixels wide — sub-pixel geometry does exactly that, and
one sentence from the owner ("zoom cận thì ít đứt") separated the two after a
whole round had been spent on the wrong one.

## A mask decides how wide an effect LOOKS, independently of how wide it IS (29/07/2026)

**Symptom.** One swept-trail style (BLADE) rendered as dashes; the other two,
drawn by the same code on the same paths, were solid. The owner's two decisive
observations: it tracked ZOOM, and it tracked CURVATURE — straight stretches
solid, bends dotted. Two rounds were spent on the wrong causes first (the mask's
frequency content, then a screen-space width floor).

**Cause.** The BLADE sheet put **66% of its alpha in the outer 20% of the band**
and only 9% at the centre (measured, `OuterEnergyFraction` in
`core/tests/swept_trail_test.c`). So a 3-pixel-wide strip displayed a
0.6-pixel-wide bright line, and a sub-pixel line rasterises where its centre
lands inside a pixel and drops where it does not. The geometry was never the
problem: the *feature inside* the geometry was.

**The fact that falsifies every other theory:** FILAMENT has the THINNEST
geometry of the three (1:40 vs the blade's 1:20) and never dashed — because it
uses the centre-weighted global trail sheet (10% in the outer 20%, peak at
u = 0.5). Its visible feature IS its geometry. Any "the strip is sub-pixel"
explanation predicts the opposite ordering, which is how the real cause was
finally cornered.

**Rule.** When authoring a mask for a strip, measure what fraction of its alpha
lies in what fraction of its width, and keep the bright feature a comparable size
to the band. An edge-weighted profile is right for a wide band seen close (the
slash) and is a dashed line on a thin one. Assert it: `OuterEnergyFraction < 0.4`
plus a real value at mid-band.

**And the process lesson, which cost more than the bug.** Each candidate cause
could only be tested by a rebuild, so each round tested exactly one guess. The
fix was to ship the DISCRIMINATORS as tunables — `swept_blade_flat` (swap in the
centre-weighted sheet) and `swept_camfacing` (drop the fixed normal) — so the
next observation settles the question whichever way it falls. When an artefact
survives one confident fix, stop fixing and start instrumenting
(core/CLAUDE.md §5, §6).

## A bench that passes NULL cannot show the one thing the effect exists for (29/07/2026)

**Symptom.** `VFX_ComposeGroundWave` was reported as conforming to the flat parts
of a heightmap map and floating on the slopes — i.e. failing at the single
requirement H2 was written to meet.

**Cause.** Not the composition. The BENCH entry passed `heightFn = NULL`, with a
comment reasoning that "the sandbox floor is flat". NULL means "flat at
center.y", which coincides with the ground exactly where the ground is level and
diverges everywhere else. The effect was never given a terrain sampler, so it
could not conform, and the bench could not have shown it either way.

**Rule, two parts.**
- **A bench entry must exercise the FEATURE, not just the code path.** When an
  effect's reason for existing is a callback, an argument or a mode, the bench
  must pass the real one. A default-argument bench proves the function runs; it
  proves nothing about what it was built for.
- **Ship the obvious adapter with the API.** Every caller of a
  `GroundHeightSampleFn` wants the active map's height, so
  `VFX_GroundHeightFromMap` (over `MapManager_GetGroundHeightAt`) now sits beside
  the composition. An API whose correct use requires each caller to write the
  same five-line shim will be called with NULL.

## A "low-frequency query" called per vertex: 455 raycasts a frame (29/07/2026)

**Symptom.** The grass map ran at **13 fps** while a ground wave was on screen.
The controlled comparison was already in hand: the only change between the fast
run and the slow one was `heightFn` going from NULL to a real sampler.

**Cause.** `MapProp_SampleGroundHeight` is a **ray-triangle test against the
terrain mesh**, and its own header says exactly what it is for: *"slightly more
expensive per call (real ray-triangle test over the mesh) but this is a
low-frequency query (VFX placement, not per-frame-per-particle)"*. The ground
wave sampled it once per vertex — 65 slices x 7 radials = **455 raycasts every
frame**. The map layer was not slow; the caller ignored a contract written in the
line above the function it called.

**Rule.**
- **Read the cost note on any callback before putting it in a per-vertex loop.**
  A `GroundHeightSampleFn` is not a cheap array lookup, and nothing in the type
  says so — the header does.
- **Sample the world at the world's own scale, not the mesh's.** The ground under
  a 5 m ring is described perfectly well by 24 samples; the 455 were describing
  the tessellation, not the terrain. Interpolating between them is EXACT for a
  planar slope, which is what a narrow band sits on.
- **Make the budget independent of the quality tier.** A raycast count derived
  from the slice count grows on exactly the machines the tier gate is protecting.
  Asserted in `core/tests/ground_wave_test.c`.
- Same family as "Density belongs to the world, not to the array" — one is about
  how many things to draw, this is about how often to ask the world a question.

## The halo must not carry the body's texture — again (29/07/2026)

**Symptom.** A swept ribbon's edge came out as a regular SAWTOOTH, long thin
spikes reaching out from an otherwise smooth band. The owner read it as the trail
"cutting itself" into segments. The centre line stayed clean, which is what says
it is the TEXTURE and not the geometry.

**Cause.** All three layers shared one sheet. The fibres inside it wander across
`u` as they travel down `v` — the whole point of them — and near the band's edge
that wander modulates the alpha of the silhouette itself. On the body layer it is
invisible; on the GLOW layer, which is 2.6x wider, the same wander is thrown 2.6x
further out and becomes spikes.

**Rule.** A halo exists to put light BEHIND a shape, not to be a second
silhouette: give it its own sheet with none of the body's structure. Damp interior
detail by the band profile SQUARED so it lives in the middle and is gone before
the edge.

**And the point that costs more than the bug:** this exact lesson was already
written, by this project, in `core/composition/common/vc_sweep_slash.inl` — "WHY
THE HALO MUST NOT CARRY THE RIM" — and repeated here anyway, because it was
recorded as a fact about the slash rather than as a rule about halos. A landmine
filed under one effect's name will be re-stepped-on by the next effect.

## Three additive copies of one pattern sum to something FLAT (29/07/2026)

**Symptom.** A ribbon's flow texture would not visibly move at ANY scroll speed —
1.3, 2.1, 3.6 tiles/sec all read as "the energy just sits there". Five rounds
went on the speed number.

**Cause.** The speed was never the problem. All three layers of the trail drew the
SAME quasi-periodic fibre sheet at three different phases, additively. Averaging
shifted copies of a pattern is how you REMOVE the pattern: the interior structure
cancelled itself into a smooth glow, and a smooth glow cannot be seen to move
however fast it scrolls.

**Rule.** In a multi-pass additive effect, the structure belongs to exactly ONE
layer. The others are lit SHAPES — a halo puts light behind, a core puts a hot
line through the middle — and giving them the body's texture both flattens the
pattern and, on the wider layers, throws its edge artefacts outward (see "The halo
must not carry the body's texture").

**Diagnostic worth reusing:** when a parameter has been swept across an order of
magnitude and nothing on screen changes, the parameter is not connected to the
symptom. Stop tuning and ask what could be cancelling the whole quantity.

## A distance constraint cannot stop node ORDER from reversing (29/07/2026)

**Symptom.** A simulated ribbon developed a fold the owner reported four times as
"it twists itself" — one place along the strip where the band pinched to a wedge
and the shading flipped. Four rounds went on the strip's side vector, its
tangent, and its plane. None of them was the cause.

**Cause.** Arithmetic settled it in one line. The bench swings a 3 m arm at
2.4 rad/s, so the tip runs at 7.2 m/s; the history is sampled at 60 Hz, so nodes
are laid **0.12 m apart**. The cloth sim bounded each node's stray from its laid
position at a flat **0.30 m** — two and a half node spacings. A node was free to
travel clean past its own leader, and the polyline folded back on itself. The
side vector "flipping" was a symptom: it flips because the tangent reverses at a
crossing.

The two rope constraints running underneath could not catch it, and this is the
general point: **distance is a scalar.** A node that has passed THROUGH its
leader is simply "slightly too close" again, so `CONSTRAIN_MIN` pushes them apart
in the reversed order and stabilises the fold instead of undoing it.

**Rule.** In any chain — cloth, rope, ribbon, trail — a per-node displacement
bound must be expressed **relative to the node spacing, not in absolute metres**,
and only along the chain. Split the displacement:

- **along** the chain: bound by a fraction of the spacing, strictly `< 0.5` (both
  ends move, so the gap can close by twice the bound). This is a *correctness*
  bound, and it is cheap — the fraction never needs tuning.
- **across** it: the loose absolute bound. Sag, curl and flutter are almost
  entirely lateral, so the look lives here and the fix costs no motion.

See `SWEPT_ORDER_FRAC` in `core/composition/common/vc_ribbon_trail.inl` and
`Test_NodesCannotCrossTheirNeighbour` in `core/tests/swept_trail_test.c`.

## A scrolling texture made of CONTINUOUS features reads as static (29/07/2026)

**Symptom.** With the sheet-sharing bug above fixed, the ribbon's energy flow
provably scrolled — and the owner still said it looked frozen, then diagnosed it
exactly: *"it is like you are dragging a sine GRAPH along, not a cord oscillating
— this texture is wrong."*

**Cause.** The sheet's fibres were continuous lanes,
`c = lane[f] + wob[f] * sin(2*PI*cyc[f]*v + phase[f])`, each running the full
height of the sheet. Scrolling `v` can then only **translate a rigid pattern**:
every feature is present at every instant, nothing begins and nothing ends. The
eye reads motion far more strongly from things appearing and vanishing than from
a rigid shape sliding, so a rigid pattern at any speed reads as a still image
being moved.

Seamlessness is what forced the mistake. Tiling demands an integer period in `v`,
and the cheapest way to get one is a feature that spans the whole sheet.

**Rule.** Flow is made of **finite streaks**, not lanes: bound each feature in
`v` with a raised-cosine envelope (zero value *and* zero slope at both ends) and
measure `v` with a **circular** distance (`dv -= floorf(dv + 0.5f)`) so the
bounded feature wraps. Then the tiling stays seamless while filaments slide in,
brighten, stretch past and fade — motion the envelope gives for free, independent
of the scroll rate.

**Rule of thumb:** if scrolling a texture faster does not make it look faster,
the pattern has no beginnings or ends in it.

## A scroll built on a MOVING origin is the motion, not a scroll (29/07/2026)

**Symptom.** A trail's flow texture read as frozen. Five rounds went on the
scroll speed; the sheet was proven to be scrolling, and it still looked still.
The owner named the cause without measuring it: *"the UV scroll is in the same
direction as the motion, which is what makes it feel still."*

**Cause.** The flow UV was `arc from the TAIL / tile`. That looks like a
material coordinate and is not one. Once a history ring is full, the tail
retreats at exactly the speed the head advances, so **a fixed piece of the ribbon
sees its own arc value change at the tip speed whether or not anything is
scrolling**. Measured on the bench swing (3 m arm, 2.4 rad/s, 1.10 m tile):

| term | tiles/sec |
|---|---|
| leaked in from the swing | 6.5 |
| the actual scroll | 2.2 |

So **75% of the UV motion was the swing**, with two consequences, both fatal:

- It was *locked to the motion*: the flow accelerated and stopped exactly with
  the blade. Flow that is a function of the movement is read as part of the
  movement — a pattern being dragged, not energy travelling.
- It totalled ~8.8 tiles/sec, about three whole sheet-lengths a second on a band
  a few centimetres wide. **Too fast to track is indistinguishable from not
  moving**, which is why no scroll *speed* ever fixed it.

**Rule.** Scroll against a **material coordinate** — a label stamped on the
geometry when it is created and never revisited (here: metres of emitter path at
the moment the node was laid). Then the scroll term is the *only* thing that
moves the texture, its rate is exactly what you set, and it is independent of how
fast the emitter is driven. Anything measured from a *moving* end of the geometry
— tail, head, first live particle — is not a material coordinate.

**Two diagnostics that generalise:**

- A "the animation looks frozen" bug is one of THREE things, and speed is the
  least likely: it is not moving; it is moving too fast to track; or it is moving
  in lockstep with something else, so the eye cancels it. Measure the rate in the
  frame of the thing being textured before touching the speed.
- An instrument must measure the right frame. The UV log here watched the HEAD's
  `v` — a different node every frame — so it faithfully reported the swing, and
  agreed with the broken code all five rounds.

## A mirror needle must pin the CODE, not the FORMATTING (29/07/2026)

**Symptom.** A formatter reflowed `vc_ribbon_trail.inl` and **seventeen** mirror
assertions failed in one run, with nothing about the behaviour changed.

**Cause.** The needles were written by copying source lines complete with their
column alignment — `#define SWEPT_FLOW_SPEED     2.10f`, five spaces. Alignment
belongs to whatever formats the file, not to the author, so every needle was a
hostage to it.

**Rule.** Collapse whitespace on BOTH sides before matching (`CollapseWS` in
`core/tests/swept_trail_test.c`): any run of spaces, tabs and newlines compares
equal to one space. Multi-line needles then work for free. A test that cries wolf
over whitespace trains people to ignore it, which is worse than not having it.

## Overlapping additive layers clip, and a BUG can hide the clipping (29/07/2026)

**Symptom.** A trail finally moved and folded correctly — and immediately read as
"burnt out, no fine detail left". The flow sheet had been rebuilt twice to get
that detail; none of it was visible.

**Cause.** The three ribbon layers OVERLAP — the core sits inside the body, which
sits inside the halo — and they are additive, so the frame buffer sees their
**sum**:

| | halo | body | core | sum |
|---|---|---|---|---|
| before | 0.16 | 0.85 | 1.00 | **2.01** |
| body+core alone, where they overlap | | 0.85 | 1.00 | **1.85** |

1.00 is already full white. Every texel through the body clipped, so the sheet's
filaments were **mathematically unrecoverable** however good the sheet was. No
amount of authoring can put detail into a clipped region.

**The part worth the entry.** Those numbers were unchanged from the version
everyone had been looking at for days, and nobody called it burnt out — because
the ribbon was also FOLDING, and the fold carved dark notches across the band.
The notches were read as detail. Fixing the fold removed the only structure that
was surviving the clipping, so **"it works now" and "it is blown out" arrived in
the same frame**, and the second looked like a regression caused by the first.

**Rule.** For any stack of additive layers drawn over each other, budget the SUM,
not each layer: keep it **below 1.0 through the body** so texture survives, and
let it go over only at the one point that is supposed to blow out (a trail's
head, an impact's centre). Write the budget down as a test — the numbers are
arithmetic and drift silently otherwise (`Test_AdditiveBudget`).

**And the diagnostic:** when fixing a bug makes something look worse, check
whether the bug was supplying contrast. Dashes, flicker, gaps and folds all read
as detail; removing them can expose a saturation or flatness problem that was
there the whole time.

## A hand-written #include silently defeats the archetype generator (29/07/2026)

**Symptom.** `VFX_ComposeProjectile` was completely invisible — no orb, no
trails, nothing in the log, and a clean build.

**Cause.** A stateful composition declares itself by defining
`VC_<Name>_Update(float)` and `VC_<Name>_Draw3D(Camera3D)`;
`scripts/sync_vfx_test.py` then generates three things into
`visual_composer.c` — the `#include` and the two per-frame dispatch calls.

I added the `#include` **by hand**. The generator's early-out was
`if not dropped and not added: return False`, computed from the include list
alone — so with the include already present it decided there was nothing to do
and never wrote either dispatch call. `VC_Projectile_Update` never ran, the
trails were fed a transform that never moved, the idle gate stopped feeding them,
and the orb was never drawn because `Draw3D` never ran either.

That is precisely the failure the function's own docstring says it exists to
prevent: *"a missed call there is worse: it compiles clean and the VFX simply
never appears, because its pool is never ticked."*

**Rule.** Never hand-edit a `@gen:` block — run the generator. And when a
generator's early-out is computed from its TRIGGER rather than from its OUTPUT,
any hand-edit of the trigger silently disables it. `sync_vfx_test.py` now
verifies the dispatch calls themselves and regenerates when they are missing, so
a hand-written include is merely redundant instead of destructive.

**Generalisable:** an idempotence check must compare against what the tool
PRODUCES, not against what tells it to run.

## A stale range check silently CLAMPS a new enum value (30/07/2026)

**Symptom.** A newly added trail style produced something that looked exactly
like the old default. The owner: *"sao nó có khác gì trail bình thường? có nhầm
lẫn gì ko?"* Three rounds went into tuning its alpha, whitening and texture.

**Cause.** `VFX_TRAIL_HAZE` was added as a fourth `VFX_TrailStyle`.
`VFX_ComposeSweptTrail` validated its argument with

```c
if (style < VFX_TRAIL_BLADE || style > VFX_TRAIL_FILAMENT)   // the OLD last style
    style = VFX_TRAIL_BLADE;
```

so every HAZE request was silently turned into BLADE — the narrowest, sharpest
style in the file. The wide faint energy field was never built. Not once.

**Why it survived so long:** a silent clamp produces a **plausible** result
rather than an absent one. An effect that does not appear is noticed in seconds;
an effect that appears as the wrong thing gets argued about, tuned, and rebuilt.
Every visual "fix" applied on top was being applied to a different style.

**What actually caught it** was one log line printing the style, shape and drawn
radius — after four rounds of reading the code and finding every path correct.
The arithmetic was conclusive the moment it existed: drawn radius 0.126 m over
5.04 m travelled is an aspect of 0.0250, which is BLADE exactly, where HAZE's
0.1600 would have given 0.81 m. **When "it looks unchanged" and "the new path
never ran" are indistinguishable on screen, stop reading and print the value.**

**Rules.**
1. Range-check against a **count sentinel** (`VFX_TRAIL_STYLE_COUNT`) added to
   the enum itself, never against the last member by name. Then adding a member
   cannot leave a check behind.
2. **A clamp must announce itself.** Defaulting an out-of-range value without a
   log converts a caller's bug into a rendering mystery.
3. Grep for every range check on an enum when adding a member — the compiler
   will not, because the value is still a valid `int`.

**And a test trap met while pinning this:** a NEGATIVE `FileHas` assertion
matches comments as well as code, so `!FileHas("style > VFX_TRAIL_FILAMENT")`
failed against the comment explaining the fix — the test broke *because* the bug
had been documented. Give a negative needle punctuation only the code can carry.

## Normalising a near-zero vector collapses geometry SILENTLY (30/07/2026)

**Symptom.** A swept tube rendered as a flat plane. No NaN, no crash, nothing in
the log — just wrong geometry that looked like a deliberate ribbon.

**Cause.** The parallel-transported cross-section frame is re-orthogonalised
against the tangent every slice:

```c
right = Vector3Normalize(Vector3Subtract(
    right, Vector3Scale(tangent, Vector3DotProduct(right, tangent))));
```

When the carried frame drifts parallel to the tangent — a path that doubles back,
or two coincident nodes making the tangent garbage — that subtraction yields
~zero, and normalising ~zero returns garbage. `right` and `up` then span nothing,
so every ring's points fall on a LINE and the tube draws as a plane.

**Rule.** Any `Normalize` whose input can degenerate needs a length check and a
defined fallback. The fallback does not have to be as good as the normal path —
here it is the less-stable reference frame — it only has to be *valid*, because
the alternative is not a slightly worse result but a structurally different one.

**And the reason it cost four rounds:** "the tube renders flat" had FOUR
independent causes in this codebase (a ribbon sheet wrapped around a cylinder, a
fix applied to one layer of two, a missing batch flush around the cull state, and
this). Each fix was correct and none of them changed what was on screen, which
read three times as "the fix didn't work". **When a symptom survives a fix you
have verified is correct, look for a second cause rather than doubting the fix.**

## A shared `onUpdate` callback silently re-installs the OTHER effect's physics (03/08/2026)

**Symptom.** The energy trail's own source comment says "no forceField, no
nodeHomeSpring — the swept path stays the spine". Every value it was asked to
render was correct, every uniform arrived, and the ribbon still wandered and
would not hold a clean shape.

**Cause.** `VFX_ComposeEnergyTrail` set `cfg.onUpdate = SmokeTrail_OnUpdate` to
reuse the smoke trail's live-tuning copy. That callback does not only copy
tunables — it ends with the smoke's cloth block, and `smoketrail_curl_strength`
defaults to `1.0`, so every frame it wrote `forceField = &s_smokeTrailUpdraft`,
`nodeHomeSpring`, `nodeHomeMaxDev` and `nodeOrderFrac` onto the energy trail.
The spawn config's omissions were re-filled with a fire updraft one frame later.

**Rule.** A `TrailConfig.onUpdate` (and any other per-frame "apply live tuning"
callback) is not a pure function of tunables — it is authority over the entity's
whole state. Never share one between two effects. If two effects want the same
knobs, share a helper that copies ONLY those fields and give each effect its own
callback. And when a spawn-time field refuses to stay at the value you set,
suspect a per-frame writer before you suspect the field.

## Two shader modes cannot share one authored sheet if they disagree about where the SHAPE lives

**Symptom.** First seen as a band that snakes across a ribbon correctly in the
middle of the strip and blinks out at the crests of its own wave. The deeper
version of the same conflict: the trail rendered as one smooth glowing ribbon
with a clean edge and a clean tail, no matter how the erosion was tuned.

**Cause.** `energy_wisp.png` was authored for the packed-wisp path
(`u_matMode == 1`), which never reads texture alpha and therefore needs an edge
fade baked into every channel. Mode 2 draws its own silhouette, so that baked
fade multiplied its shape by ~0 exactly where the shape reached the strip edge.
Centring the sample on the band's own frame worked around it — but the sheet was
still isotropic CLOUD noise, and cloud noise can only modulate a shape's
BRIGHTNESS. It cannot split one band into many. No shader change can make a
smooth-edged ribbon read as a bundle of hairs when the asset has no hairs in it.

**Rule.** Decide per mode whether the SHAPE lives in the shader or in the sheet,
and do not let two modes share an asset across that line. If the mode draws its
own silhouette, the texture may only be *detail* and must be sampled in the
silhouette's own frame. If the effect needs discrete filaments, they have to
exist in the asset (`scripts/gen_energy_wisp_texture.py` builds them as narrow
Gaussian ridges wandering along V) — and combine with **max**, never a sum:
summing overlapping hairs fills the gaps between them back in, which is the one
thing that must not happen. The gaps ARE the effect.

## The body/emission split — promoted to `ENGINE_LANDMINES.md`

`BLEND_ALPHA` is not premultiplied, so a shader that emits one colour for both
VFX passes dims twice in the body pass and the effect washes out over bright
backgrounds. Any module drawing through `ScreenDistort_BeginVFXBody()` /
`...Emission()` can hit it — see **"BLEND_ALPHA is NOT premultiplied"** in root
`ENGINE_LANDMINES.md`. The trail system's fix is `ResolvePass` in
`core/trails/shaders/trail_deform.fs` plus `TrailMaterialConfig.bodyOpacity`.

## A shared `onUpdate` is authority over the whole entity — see above

Also worth stating as a general shape: the energy trail read the smoke trail's
`onUpdate` for its tunables and silently inherited the smoke's force field. Any
"apply live tuning" callback owns everything it touches, so two effects must
never share one. Give each its own and factor the shared *copy* into a helper.

## A "trail texture" is a SHAPE; a filament sheet is a MATERIAL. Tiling decides which

- **Symptom:** a trail effect renders as a uniform rope — no head, no tail, no
  silhouette — however the wave, flow, dissolve and colour parameters are tuned.
- **Cause:** the sheet was authored as a repeating material and sampled with a
  tiling coordinate, when the algorithm expects one complete trail shape whose
  head/tail taper is painted into the texture and whose U maps ONCE across the
  whole trail (the RzFX reference's 拖尾紋理 channels). Every downstream stage
  behaves correctly and still cannot produce a silhouette the input does not
  contain. Three sin-offset samples of a rope are three ropes.
- **Rule:** decide per sheet whether it is a SHAPE (stretch once; taper baked in;
  must not tile) or a MATERIAL (tile by metres; uniform by design), record it
  next to the asset, and give the consumer an explicit switch —
  `TrailMaterialConfig.stretchUV`. Then, when an effect's structure refuses to
  appear no matter how the parameters move, stop turning knobs and ask whether
  the input is the KIND of thing the algorithm consumes.

## Two independent safety clamps computed against different radii can still combine into an unsafe result (05/08/2026)

**Symptom.** A swept tube's vertex deform (`core/geometry/pm_tube.inl`), with
BOTH a radius-scale channel and a vertex-offset channel active, kept
producing stair-stepping / self-intersection on a steeply tapered, moving
tube (`vc_smoke_trail.inl`'s funnel, `radiusTailFrac` 0.12) no matter how the
offset clamp's own shape was tuned (hard clamp → soft-knee tanh clamp, both
tried). 4 rounds of visual-only patches, none of which touched the actual
cause.

**Cause.** Two clamps each individually correct in isolation:
- `PM_TUBE_MIN_RADIUS_FRAC` floors the SCALE channel's `deform` at 0.25 of
  nominal.
- The OFFSET channel's clamp capped `|dOffset|` at `0.55 * ringRadius` —
  but `ringRadius` was the ring's **nominal, undeformed** radius, computed
  independently of how far the SCALE channel had already shrunk that exact
  vertex.

Both channels are separate, uncorrelated noise fields sampled continuously
across the whole mesh, so nothing prevents SCALE hitting its floor at a
vertex while OFFSET simultaneously pushes near its own (nominal-radius-based)
cap at the *same* vertex. Measured
(`core/tests/pm_tube_offset_clamp_test.c`): `finalRadius(0.25x) +
offset(-0.55x of NOMINAL, i.e. of 1.0x) ≈ -0.30x` — negative, the
cross-section folds through its own centreline. This ratio is
**scale-invariant**: it exists identically at `radiusTailFrac=1.0` (no
taper at all), a steeply tapered funnel just makes the absolute artifact
size large relative to the (already small) local geometry, and therefore
visible, while a thick column's texture and distance hide the same defect.

**Rule.** When two independent clamps each bound a *different* quantity that
gets summed into one final result (here: a radius floor and an offset cap,
summed into `finalRadius + dOffset`), each clamp must be expressed in terms
of the *other's already-applied output*, not a value computed in parallel
from the same nominal inputs. Concretely: the offset clamp must take the
SCALE-channel's post-floor radius as its basis
(`PMSweptSection_ClampOffset(rawOffset, finalRadius, ...)`,
`core/geometry/procedural_mesh_utils.c`), not the nominal per-ring radius —
that ordering makes `effectiveRadius > localRadius*(1-maxRadiusFrac) > 0`
true **by construction**, not by hoping the two clamps' worst cases never
coincide. Never assume two safety clamps compose safely just because each
one is independently proven safe — prove the *combination* by number
(sweep both channels' full amplitude range together, not each alone).

## Reusing one coordinate for both the ENVELOPE gate and the NOISE-SAMPLING position couples two unrelated concerns (05/08/2026)

**Symptom.** After fixing the offset-clamp landmine above and enabling
`PMTubeConfig.noiseWavelength` on the smoke trail for the first time
(`vc_smoke_trail.inl`), the churn read as visibly flatter/less varied than
the smoke column using the *identical* layer amplitudes — not the
self-intersection/stair-step symptom from the other landmine, a different
one: "biến đổi ít quá" (too little variation), confirmed even once the
trail's real length had reached steady state (ruled out by a temporary
`TraceLog` of the actual `rawSpanLen`/`tubeNoiseSpanLen` values, which
looked perfectly healthy — the bug was NOT in that number).

**Cause.** `core/geometry/pm_tube.inl`'s `PMTubeShapeDeformNoise` (and
`PMTubeAxisScalar`) called `MeshDeform_Evaluate(field, (Vector2){uu, t}, ...)`
where `t` was **the same value** used for two different things a swept-tube
ring computes:
- the true geometric fraction along the body (`i/segments` in the caller),
  which `MeshDeformLayer.env`/`envStart`/`envEnd` read as `surf.y` — the
  ALONG-BODY ENVELOPE gate, and
- `tNoise` — the material/noise coordinate `noiseWavelength` deliberately
  SCALES to real metres, which is `< t` whenever the tube's current real
  extent is shorter than `noiseWavelength` (the normal case: any
  `core/trails/` follower is hard-capped at `TRAIL_HISTORY_COUNT=60` = 1s of
  history regardless of what `lifetime` asks for).

Every existing caller (`noiseWavelength == 0`) has `tNoise == t` always, so
the coupling was invisible until the trail became the first live consumer
to set `noiseWavelength > 0`. Once `tNoise < t`, the envelope's `surf.y`
input shrank too — and the smoke churn's own envelope kind,
`UV_ENV_HEAD_WELD_SQ = smoothstep(start,end,c) * c * c`, keeps growing with
`c²` **past its ramp zone**, not plateauing at 1.0. Feeding it a
globally-shrunk `c` therefore squares the shrink into the envelope's
magnitude across the ENTIRE body, not merely delaying where it opens —
measured (`core/tests/pm_tube_envelope_coordinate_test.c`): at the trail's
front ring with a typical `tNoise/t` ratio of 0.2-0.5 (matching the values
actually logged), the envelope was suppressed to 4-25% of what the same
ring should carry.

**Rule.** `mesh_deform.h`'s own doc already names half of this principle —
"passing surf.y [into mat] is the mistake that makes a churning body read as
a pre-squeezed shape being dragged." The other half, learned here: it goes
wrong in the OPPOSITE direction too — passing the intentionally-rescaled
MATERIAL coordinate into the ENVELOPE's `surf.y` slot silently scales every
envelope-gated layer's magnitude by however much that rescaling shrank the
coordinate, everywhere, not just at the gate's threshold. Whenever a
function computes more than one coordinate meaning from one input (a
geometric position vs. a rescaled/drifted "material" position), give each
meaning its OWN parameter all the way to the `MeshDeform_Evaluate` call —
never let a rename or a refactor collapse them back into one variable just
because they happen to be equal for every caller tested so far.

## One flag that re-anchors ONE of several emitter-relative quantities is a bug with a plausible name (05/08/2026)

**Symptom.** The smoke trail's churn still read as flat/thin next to the smoke
column after BOTH landmines above were found and fixed, on layer numbers that
are a verbatim copy of the column's. Raising the amplitude knob to 3x
(`smoketrail2_noise = 3.0` in `tuning.cfg`, which silently stayed set for
several rounds of visual comparison) did not close the gap either.

**Cause.** `PMTubeConfig` had a flag named `radiusAnchorAtTail`, added earlier
in the same session to give a moving trail a taper that is thin at its
*leading* end — the trail lays its newest node at `t=1` of the swept path,
the opposite of the column's static path, so `r(t)` had to run on `1-t`. The
name was the bug. The flag does not state a fact about the radius; it states
one fact about the caller — **"my emitter is at the other end of this path"** —
and THREE independent quantities in `pm_tube.inl` anchor to the emitter:

1. the radius profile `r(t)` — thin at the source,
2. the deform ENVELOPE (`MeshDeformLayer.env`, i.e. `UV_ENV_HEAD_WELD` =
   "zero excursion at the source"),
3. the centreline weld (`centerlineAmp * t * t`) — the base does not fidget.

Only (1) was re-anchored. After the flip, `t=0` is the trail's FAT back — so
the widest ring of the tube got envelope **exactly 0** (no churn at all, at
any amplitude), while envelope 1 landed at `t=1`, the `0.12x`-radius front
tip where there is nothing to bulge and where both offset clamps are
tightest. The absolute excursion a viewer sees is
`baseRadius * capsuleCurve(t) * envelope(t) * noise`, so `capsuleCurve x
envelope` is the entire shape of the available churn; measured
(`core/tests/pm_tube_envelope_anchor_test.c`) its peak was **0.197 against
the column's 1.000** on the identical layers — 5x on the scale layer, 8x on
the offset layer whose envelope is squared.

**Why the knob could not rescue it, and why that matters diagnostically.**
Amplitude is a uniform factor on that product, so the deficit is a *ratio*,
invariant under the knob — the trail at 3x still peaked below the column at
2x. A whole round of "it looks the same, turn it up" was therefore
guaranteed to be uninformative before it started. When a knob provably
cannot move the quantity you are looking at, sweeping it is not a weak
experiment, it is a null one.

**Rule.** When a flag re-anchors, mirrors, or reverses a parametrisation,
enumerate every quantity derived from that parametrisation **before**
naming the flag, and derive them all from ONE named intermediate
(`float tEnv = cfg->anchorAtTail ? (1.0f - t) : t;`) rather than repeating
the conditional per consumer. Name the flag after the FACT
(`anchorAtTail`), never after the first consumer you happened to need
(`radiusAnchorAtTail`) — a consumer-shaped name makes the other consumers
look like somebody else's concern, and they will silently keep the old
anchor. A test that asserts each consumer reads the shared intermediate
(3 checks, one per consumer) is what stops a later refactor from
un-anchoring one of them again.

**Corollary on tuning.cfg.** Persisted overrides survive across sessions and
are invisible in code review. Every round of visual comparison in this
session ran at 3x the code default without anyone noticing, and the file
also still carried `smoketrail2_freeze`, a key nothing has registered since
the code that read it was deleted. Before any visual A/B, read the whole
`tuning.cfg` block for the effect under test and state the multipliers in
the report — or log the post-multiplication value, as
`VFX_SMOKE_TRAIL`'s spawn line does (`churn 1.02` vs. the code's 0.34 was
what finally exposed it).
