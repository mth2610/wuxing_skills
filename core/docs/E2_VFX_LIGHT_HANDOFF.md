# E2 — VFX point lights don't light anything. **RESOLVED 23/07/2026.**

**Root cause:** `matModel` is model×**view**, so every lit surface's
`fragPosition` / `fragWorldPos` is a **view-space** position that the shaders all
label "world space". The lights were uploaded in world space. The two operands
were in different spaces, `length(lightPos - fragPos)` returned roughly the
camera distance instead of the true ~1 m, attenuation clamped to 0, and nothing
was ever lit — on any surface, at any tier, with no error anywhere.

**Fix:** `core/vfx_light.c` now converts light positions into the surfaces' space
with `rlGetMatrixTransform()` before uploading them (`VFXLight_ShaderSpaceMatrix`),
and `main.c` calls `VFXLight_BindAll` **inside** the 3D pass, right after
`MyBeginMode3D`, where that matrix actually holds the view.

Verified in-scene: a 6 m test light at the player casts a real warm pool on the
grass, the paved path, the arena floor plate, the rocks, and the character.

**Consumers (all five lit-surface shaders):** `core/shaders/surface_lit.fs`
(character), `maps/toolkit/shaders/{ground_splat,grass_material,path_blend}.fs`
(terrain), `maps/toolkit/shaders/prop_lit.fs` (rocks/props),
`maps/toolkit/shaders/ground_shadow.fs` (immediate-mode floor plate + zone discs). Regression test:
`core/tests/vfx_light_space_test.c` (suite green, 5/5).

E2's own design — the shared GLSL block, the register-once/bind-all-per-frame
structure, the vec4 packing — was correct as written; only the world-vs-view
space of the upload was wrong. Extending it to the remaining surfaces (rocks,
immediate-mode floor/discs) was then a one-include-plus-one-register job per
shader (see PROGRESS.md, 25/07/2026 follow-up).

---

## 1. What the previous handoff got wrong, and why it cost two sessions

Keeping this section because the *shape* of the mistake is worth more than the
fix. Three of the seven "ELIMINATED — do not re-investigate" items were sound.
The load-bearing one was not.

**§3.4 claimed: "The ground's world position is correct. Debug mode 1 painted
`fract(worldPos)` and produced a clean 1 m colour grid, so `fragPosition` is
genuine world space."**

It does produce a clean 1 m grid. It always will. `fract()` of a *translated*
position is an identical grid — the function is blind to the origin, and the
origin was the entire bug. The one debug view aimed squarely at the fault
reported success, which is why every subsequent session searched everywhere else:
std140 array padding, rlvk's uniform delivery, and finally "maybe Vulkan doesn't
support dynamic lights at all".

A debug view that renders the wrong quantity is worse than no debug view
(`core/CLAUDE.md` §6). This one didn't just fail to help — it actively closed off
the correct line of inquiry, in writing, under a heading that said not to look
there again.

Two smaller corrections to the same document:

- **The vec3→vec4 rewrite fixed nothing.** rlvk's `rlSetUniform` already strides
  array writes by 16 bytes; `vec3[]` uploads were landing correctly all along. A
  headless probe reading the UBO staging back proved it (see §3 below). The
  `vec4` packing is still the better declaration and stays, but it was not a bug
  fix, and `core/docs/LANDMINES.md` has been corrected where it claimed otherwise.
- **The "regression" in §4 — modes 0-4 all stopping at once — was not real.**
  Those observations were made in a scene where the caster stands off the island
  plateau, so the surface filling the frame was the *cloud sea*, not the ground.
  The ground shader was drawing a sliver in one corner the whole time.

## 2. The decisive experiment (§6 of the old doc), and what it actually showed

The old doc proposed a hard-coded light in `ground_splat.fs` with a two-way
reading: orange pool ⇒ uniform delivery is at fault; nothing ⇒ the accumulate
result never reaches the output. The real answer was neither — the uniforms
arrived *and* reached the output, and the values were still wrong. A two-way test
whose two branches are both false is a sign the framing is off; the space of
possible causes was drawn too narrowly.

What worked instead was a ladder of debug modes, each isolating exactly one link,
with the crucial property that **one of them cannot be faked**:

| Mode | Paints | Result | Rules out |
|---|---|---|---|
| 5 | a flat constant | ground + character turn red | the block's return not reaching the output |
| 3 | `u_vfxLightCount` alone | blown white | count ≤ 0 |
| 4 | `fract(pos[0].xyz)` | magenta, matching the logged position exactly | the array upload |
| 6 | `pos[0].w` (radius) | blown white | the packed component |
| 1 | `fract(worldPos)` | clean 1 m grid | **nothing — this is the decoy** |
| 7 | `length(pos[0] - worldPos)` | **no dark spot anywhere** | ← the answer |

Modes 3/4/5/6 all passed while 0/2/7 failed. The only thing those two groups
differ by is whether the light position and the fragment position are compared
against each other — which localised the fault to "these two are not in the same
space" in a single run.

Mode 7 is the one that mattered, and it is the one the old instrument set was
missing: it is the only mode sensitive to the *origin* of `worldPos` rather than
its gradient. Modes 5, 6 and 7 are now permanent in
`core/shaders/common/vfx_lights.glsl`.

## 3. The cheap instrument that should have come first

Most of this bug's cost was spent arguing about what the shader receives. A
~120-line headless probe settles that in about two seconds, with no window and no
frame: compile the real `.vs`/`.fs` through `rlLoadShaderProgram`, then read back
`RLVK.shaderSlots[id].uniforms[loc].fsOffset` and the bytes at that offset in
`fsStage`.

It reported, immediately: all three shaders compile, every `u_vfxLight*` uniform
reflects, and every uploaded value — including each `vec4` array element at its
correct std140 offset — lands exactly where the shader will read it. That killed
the std140 hypothesis, the "rlvk can't deliver uniforms" hypothesis and the
"maybe it isn't compiling" hypothesis at once, before a single screenshot.

Per `core/CLAUDE.md`'s debugging table: classify the question before rebuilding.
"Does the value reach the GPU?" is not a visual question and must never be
answered with a screenshot.

## 4. Instruments now in the tree

`tuning.cfg` (hot-reload, no rebuild):

```
vfx_light_gain        = 3.0   # daylight wants 4-6
vfx_light_debug       = 1     # wire sphere at every light's world position
vfx_light_debug_mode  = 0     # 0 off
                              # 1 fract(worldPos)   -- DECOY, see §1
                              # 2 attenuation of light 0
                              # 3 count alone
                              # 4 light 0 position
                              # 5 flat constant  (does the block reach the output?)
                              # 6 light 0 radius (.w alone)
                              # 7 distance to light 0  (the origin-sensitive one)
vfx_light_test        = 1     # park a controllable light on the player
vfx_light_test_radius = 6.0
```

`vfx_light_test` exists because every round of this bug was really a question
about *where* the light was: a skill's light is typically under 2 m wide and
lives under half a second, which cannot answer "is this surface lit at all".
This one is 6 m, permanent, and follows the player.

With any debug mode on, `VFXLight_BindToShader` also logs the numbers as
uploaded, once a second — post-conversion, so a wrong space shows up directly as
a coordinate that has no business being there.

## 5. Reproducing a lit scene headlessly

```bash
VSDK=~/VulkanSDK/1.3.296.0/macOS
VK_ICD_FILENAMES="$VSDK/share/vulkan/icd.d/MoltenVK_icd.json" \
DYLD_LIBRARY_PATH="$VSDK/lib" \
WUXING_MAP=verdant WUXING_VERIFY=STONE_PRISON ./build/wuxing
```

Screenshots land in `autotest_output/`. Two traps, both paid for:

- **The plain binary aborts with `FATAL: RLVK: instance creation failed`** unless
  those two SDK env vars are set.
- **`WUXING_VERIFY`'s cast position is `(6, 0, 7.4)`**, which on `verdant_path`
  (map centre `(50, 0, 37.5)`) is off the plateau — the frame fills with cloud
  sea and the ground is not on screen at all. Before concluding a ground shader
  has no effect, make it output a flat unmistakable colour and confirm you can
  see *where* it draws.

## 6. Environment

```
Device:  Intel(R) Iris(TM) Graphics 6000
Driver:  MoltenVK 1.2.11        API: 1.2.296
rlvk quirk noSampledDepth active (MoltenVK/Intel)
GfxQuality tier = 3 (HIGH)
```

rlvk was never at fault. Nothing here is Renderer Agent territory.

## 7. Where the lesson lives now

- `ENGINE_LANDMINES.md` §9 — promoted, because any shader doing positional work
  through `matModel` hits it. `maps/toolkit/ground_shadow.c` had already hit and
  solved the same trap from the other direction, and that precedent went unnoticed.
- `core/docs/LANDMINES.md` — full chain, plus the correction to the std140 entry.
- `core/tests/vfx_light_space_test.c` — asserts the conversion, that the view
  transform preserves distance, that `fract()` cannot distinguish the two spaces,
  and that `VFXLight_BindAll` is still called after `MyBeginMode3D`.
