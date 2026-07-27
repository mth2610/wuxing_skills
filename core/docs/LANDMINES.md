# core — Landmines

> Distilled, reusable debugging lessons for the **core** module. Format: Symptom → Cause → Rule (`DOC_ARCHITECTURE.md` §6).
> Cross-cutting traps (batching hazard, depth-test-vs-mask, `rlFrustum near<1.0`, lit-material-dark, emitter collision) live in root `ENGINE_LANDMINES.md` — read that too.
> Long session logs and open backlog are in `PROGRESS.md`, not here.

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
