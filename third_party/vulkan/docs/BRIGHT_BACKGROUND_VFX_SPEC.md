# Bright-background VFX rendering specification

> **Document archetype:** implementation plan / progress specification. This is not an API
> contract and does not record completed work.
>
> **Status:** core implementation in progress; Vulkan RGBA16F readback and the `bright_vfx`
> oracle are implemented, the shared Core compositor is wired into reference shaders, and the
> bloom prefilter now evaluates threshold/weight in exposure space while writing raw HDR.
> Full Vulkan execution remains pending the local Vulkan-Headers cache/network.
>
> **Target:** Vulkan 1.1 first, with the existing raylib/rlgl-compatible GL/GLES paths kept
> behaviorally equivalent.

## 1. Required outcome

Fire, energy, plasma, beams, lightning, explosions, and magical trails must retain a readable
silhouette, element hue, compact hot core, and controlled glow over dark, mid-gray, bright-gray,
and bright-colored scenery. A white-hot center is allowed; turning the whole effect into a white
blob is not.

The implementation must not solve this by increasing additive intensity alone. It must separate:

1. **coverage/body** — the part that can attenuate the background and therefore creates local
   contrast;
2. **radiance/core** — HDR energy that can exceed 1.0 and drives the hot center;
3. **optical spread** — bloom, which communicates brightness around the source but does not define
   the source silhouette.

> [!NOTE]
> **(project convention):** the three-part split above is the normative Wuxing authoring model.
> An effect may omit a part (a spark can be radiance-only), but it may not overload one number as
> both coverage and radiance.

## 2. Production evidence and what to copy

| Source | Production technique | Decision for Wuxing |
|---|---|---|
| [Unreal Engine — Material Blend Modes](https://dev.epicgames.com/documentation/en-us/unreal-engine/material-blend-modes-in-unreal-engine) | Epic explicitly documents that additive materials are difficult to see on light backgrounds and recommends AlphaComposite. Its formula is `source + destination * (1 - opacity)`. | Use the existing premultiplied-alpha surface for any energy effect that must keep hue on bright scenery. Reserve pure additive for sparse sparks and low-opacity halos. |
| [Prototype 2 particle rendering](https://www.gamedeveloper.com/programming/fire-blood-explosions-i-prototype-2-i-s-over-the-top-effects-tech) | Prototype 2 used `ONE, INVERSE_SOURCE_ALPHA` so one particle could interpolate from additive energy to alpha-blended smoke, and lit particles against several world zones/times of day. | Keep coverage independent of emission, allow lifetime curves to move an explosion from hot energy toward smoke, and test against several scene luminances. |
| [Hogwarts Legacy — Open World Rendering Techniques](https://media.gdcvault.com/gdc2024/Slides/GDC%2Bslide%2Bpresentations/Hall_Rob_OpenWorldRendering.pdf) | The GDC 2024 VFX section says magic had to look bright at every time of day and used camera exposure so fixed-brightness effects did not become dim by day and excessive by night. | Bloom extraction and VFX calibration must use the same exposure space as the scene. Auto exposure is a later phase, not a substitute for local contrast. |
| [Unreal Engine — Auto Exposure](https://dev.epicgames.com/documentation/en-us/unreal-engine/auto-exposure-in-unreal-engine) | Unreal offers histogram and downsampled exposure, separate adaptation speeds, metering masks, and local exposure. Pre-exposure keeps scene-color values in a practical floating-point range. | Start with exposure-stable bloom and deterministic manual EV tests. Add bounded temporal auto exposure only after compositing passes the bright-background oracle. |
| [Call of Duty: Advanced Warfare post processing](https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/) | Call of Duty used a pyramidal bloom hierarchy for robustness and temporal stability; the published discussion also describes low-strength thresholdless bloom as viable in a high-dynamic-range PBR scene. | Keep the current pyramid. Use a soft knee, preserve thin emitters, and avoid a hard energy clamp. Do not add FFT/convolution bloom to the Vulkan 1.1 mobile path. |
| [Unreal Engine — Bloom](https://dev.epicgames.com/documentation/en-us/unreal-engine/bloom-in-unreal-engine) | Unreal combines several blur radii at progressively lower resolutions; standard bloom is the normal game path, while convolution bloom targets high-end/cinematic use. | The shipping path remains a downsample/upsample pyramid. Wide glow comes from low-resolution levels, not giant additive sprites. |
| [Unreal Engine — Filmic tonemapper](https://dev.epicgames.com/documentation/unreal-engine/color-grading-and-the-filmic-tonemapper-in-unreal-engine?lang=en-US) | Filmic highlight roll-off preserves shape as exposure rises and allows only sufficiently hot emissive regions to approach white. | Author a narrow white-hot core and a broader saturated body. Do not demand that arbitrarily high HDR RGB remain saturated after tone mapping. |
| [Unity — Tonemapping](https://docs.unity3d.com/es/530/Manual/script-Tonemapping.html) | Unity documents that bloom and other effects needing high luminance must run before tone mapping, while HDR values are still available. | Bloom composite is HDR and precedes tone mapping; display-referred grading/LUT remains after tone mapping. |
| [Khronos — blend operation](https://registry.khronos.org/VulkanSC/specs/1.0-extensions/man/html/VkBlendOp.html) | Floating-point attachments are not clamped before blend evaluation. | Keep the scene and bloom chain in RGBA16F on the Vulkan HDR path. |

The common conclusion is not “more bloom.” The source must first remain legible; exposure and
bloom then communicate how bright it is.

## 3. Existing Wuxing ground truth

Do not rebuild systems that already exist.

- `core/post_fx.h` already exposes a six-level bloom pyramid, bloom threshold/intensity/scatter,
  ACES-style tone mapping, and manual exposure. Bloom is documented before tone mapping.
- `core/shaders/bloom_bright.fs` already has a soft knee, thin-feature coverage compensation, a
  monotonic soft energy ceiling, and max-channel brightness so saturated blue/purple emitters are
  not rejected by luminance alone.
- `core/shaders/bloom_downsample.fs` already applies Karis-style firefly suppression on the first
  downsample.
- `core/screen_distort.h` defines the authoritative HDR scene target and reports whether it is
  RGBA16F. It also exposes BODY and EMISSION entry points plus safe scene/depth snapshots.
- `core/vfx_config.h` already defines alpha, additive, and premultiplied surfaces. Its
  premultiplied formula is the required AlphaComposite family.
- `core/vfx_appearance.h` and `core/vfx_contrast.h` already centralize surface, body opacity,
  emission intensity/threshold, edge contrast, and core size.
- `core/trails/trail_recipe.h` already separates `bodyOpacity` from `hdrGain`; this is the reference
  data shape for every other VFX renderer.
- `core/lightning/lightning_stroke.h` already defines body, halo, and core as three semantic
  layers. `core/lightning/shaders/lightning_stroke.fs` is the reference spatial profile for thin
  emitters.
- `third_party/vulkan/rlvk/rlvk_pipeline.inl` already implements
  `RL_BLEND_ALPHA_PREMULTIPLY` as `ONE, ONE_MINUS_SRC_ALPHA` and implements the existing additive
  and separate-custom modes.
- `third_party/vulkan/rlvk/rlvk_format.inl` already maps
  `RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16` to `VK_FORMAT_R16G16B16A16_SFLOAT`.
- `third_party/vulkan/rlvk.h` already exposes format capability queries. The backend documents
  that R16F blend/filter support is mandatory while R32F extras are optional.

The missing work is consistent use, measurable cross-background acceptance, and exposure-stable
calibration—not a new Vulkan blend feature.

## 4. Why additive-only fails

For additive blending:

```text
out = source + destination
```

The source cannot reduce any destination channel. On a bright neutral destination, extra RGB
quickly rolls toward display white; relative contrast shrinks and hue differences compress. More
intensity therefore creates a larger white region instead of a more readable colored effect.

For premultiplied AlphaComposite:

```text
out = sourcePremul + destination * (1 - coverage)
```

Coverage attenuates the background locally while `sourcePremul` can still contain HDR radiance.
The same pixel can therefore preserve a colored body on white scenery and produce a bright core
on dark scenery.

> [!NOTE]
> **(project convention):** an HDR energy effect that occupies more than a few isolated pixels
> must have either non-zero coverage in a premultiplied draw or a separate BODY draw. Pure
> additive is valid only when losing the mark over a white background is acceptable (sparks,
> tiny glints, very faint far halo).

## 5. Normative pixel contract

### 5.1 Inputs

Every migrated VFX fragment shader must derive these semantic values before returning color:

```glsl
float coverage;       // 0..1 geometric/optical coverage; includes lifetime and soft-depth fade
float bodyMask;       // 0..1 mass or hue-support mask
float coreMask;       // 0..1 compact emitter; normally sharper/narrower than bodyMask
vec3 bodyColor;       // linear RGB, scene-referred hue carrier
vec3 emissionColor;   // linear RGB, scene-referred emitter hue
float bodyIntensity;  // normally <= 1
float emissionGain;   // HDR, may exceed 1
```

Coverage must not be computed from `emissionGain`. Increasing HDR energy must not make a sprite
more opaque. Decreasing opacity must not silently remove all radiance.

### 5.2 Premultiplied output

> [!NOTE]
> **(project convention):** use this exact conceptual formula for combined body + energy
> particles. Implement it once in a shared GLSL include; individual shaders supply masks/colors.

```glsl
coverage = clamp(coverage * bodyMask, 0.0, 1.0);

vec3 coveredBody = bodyColor * bodyIntensity * coverage;
vec3 emittedLight = emissionColor * coreMask * emissionGain;

finalColor = vec4(coveredBody + emittedLight, coverage);
```

The draw must use `BLEND_ALPHA_PREMULTIPLY`, which the current Vulkan backend maps to
`ONE, ONE_MINUS_SRC_ALPHA` in `third_party/vulkan/rlvk/rlvk_pipeline.inl`.

Do not multiply `finalColor.rgb` by `finalColor.a` again at the call site. `coveredBody` is already
premultiplied; `emittedLight` is intentionally independent radiance.

### 5.3 Separate BODY + EMISSION output

Use separate passes when the material requires different geometry, sorting, lighting, or masks.

```glsl
// BODY draw with BLEND_ALPHA
finalColor = vec4(bodyColor * bodyIntensity, coverage);

// EMISSION draw with current raylib additive blend (SRC_ALPHA, ONE)
finalColor = vec4(emissionColor * emissionGain, coreMask * authoredAlpha);
```

Do not pre-scale additive RGB by the same mask stored in alpha; the blend state applies alpha and
would square soft edges. `core/trails/shaders/trail_deform.fs` is the local reference for this
rule.

### 5.4 Spatial hierarchy

> [!NOTE]
> **(project convention):** at a normal gameplay camera distance, measure widths in final screen
> pixels after projection.

| Region | Target width | Color/role |
|---|---:|---|
| hot filament/core | 1–3 px | near-white center, allowed to desaturate |
| saturated inner body/corona | 3–9 px | strongest element hue and bright-background readability |
| soft field/halo | 8–32 px | low-energy additive color; never the main silhouette |
| post bloom | scene dependent | optical spread, low opacity, no hard edge |

The saturated region must be wider than the white core. If bloom covers the saturated region
completely, lower halo/core energy or narrow the core; do not counter it with global saturation.

## 6. Effect recipes

These are defaults, not hard-coded constants. Preserve per-effect authoring controls.

### 6.1 Fire

> [!NOTE]
> **(project convention):** fire uses a premultiplied combined surface by default.

- `coverage`: flame density plus soot density; soft at the silhouette.
- `bodyColor`: deep red/orange at low temperature and neutral/dark soot where smoke dominates.
- `coreMask`: the highest-temperature channel, narrower than flame coverage.
- `emissionColor`: yellow-white core with an orange saturated shoulder.
- Start with `coverage = 0.35..0.70`, `emissionGain = 3..8` in linear HDR.
- Spawn smoke as an alpha/premultiplied body, not an additive gray sprite.
- Bloom comes from the hot channel only. A broad orange body must remain visible with bloom off.

### 6.2 Energy/plasma/magic orb

> [!NOTE]
> **(project convention):** named MAGIC effects are not pure additive by default. Pure `GLOW`
> remains additive; structured magic uses premultiplied coverage.

- Use an alpha-composite shell/body with `coverage = 0.20..0.55` to hold saturated hue.
- Use one compact HDR core at `emissionGain = 2.5..6`.
- Use a faint additive halo at `0.10..0.35` of core energy.
- Refraction/distortion is structure, not brightness. It must sample
  `ScreenDistort_GetSceneSnapshotTexture()` after requesting a safe snapshot through
  `core/screen_distort.h`; never sample the bound scene target.
- Use internal dark gaps/noise between filaments. Filling every texel with emission produces a
  flat disc after tone mapping.

### 6.3 Beam

- Carrier/body: premultiplied, saturated, 3–9 px.
- Core: additive or premultiplied HDR, 1–3 px, near-white only at the center.
- Halo: additive, wider, low energy.
- Keep width stable in screen space at distance or clamp world-space width to a minimum projected
  pixel width. A sub-pixel core cannot be repaired reliably by bloom alone.
- Use continuous arc-length UVs and smooth width/taper; do not assemble a beam from disconnected
  alpha quads that reveal sorting seams.

### 6.4 Lightning

`core/lightning/lightning_stroke.h` already has BODY/HALO/CORE and should be the reference rather
than rewritten.

- BODY preserves blue/violet hue on bright scenery.
- CORE communicates ionization and may roll to white.
- HALO remains pale and low energy; it must not become an opaque blue tube.
- Endpoint contacts may temporarily raise core energy, but must not widen the whole bolt.
- The bright-background test must include horizontal, diagonal, and one-pixel projected segments.

### 6.5 Explosion

Follow the Prototype 2 lifetime principle:

- ignition: compact HDR core, low-to-medium coverage;
- expansion: saturated premultiplied flame body plus diminishing core;
- cooling: emission falls faster than coverage;
- aftermath: alpha/premultiplied lit smoke, zero emission except embers.

The transition is authored through independent emission and opacity curves. Do not cross-fade two
whole effect systems whose sort order can pop.

### 6.6 Sparks and embers

- Sparse sparks may remain pure additive.
- Embers larger than a few pixels need a small premultiplied/alpha body or they will disappear on
  bright ground.
- A spark's bloom must be limited by its sub-pixel coverage so a one-pixel sample does not inflate
  into a 16-pixel white blob. `core/shaders/bloom_bright.fs` already contains this compensation.

## 7. HDR, exposure, bloom, and tone-map contract

### 7.1 Render space and format

- Perform lighting and blending in linear HDR.
- Use `RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16` for scene color and bloom targets on the Vulkan
  HDR path; `third_party/vulkan/rlvk_format.inl` maps it to RGBA16F.
- Do not replace any HDR target with R32F to “gain precision.” Blending and linear filtering on
  R32F are optional and guarded by `rlvkFormatSupportsBlend()` /
  `rlvkFormatSupportsLinearFilter()` in `third_party/vulkan/rlvk.h`.
- Preserve the existing RGBA8 fallback. On fallback, premultiplied coverage remains the primary
  bright-background mechanism even though radiance above 1.0 is unavailable.

### 7.2 Exposure ordering

```text
linear scene + VFX (raw scene-referred HDR)
    -> evaluate bloom threshold from (scene * exposure)
    -> write selected bloom in raw scene-referred units
    -> bloom pyramid + composite in raw HDR
    -> multiply the complete scene+bloom by exposure exactly once
    -> filmic tone map
    -> display-referred grade/LUT/vignette
    -> output
```

`core/shaders/post_process.fs` currently composites bloom before ACES tone mapping and applies the
LUT afterward; preserve that ordering.

> [!NOTE]
> **(project convention):** bloom threshold is interpreted in exposed scene space. At exposure
> `E`, prefilter `scene * E`, not raw scene followed by a fixed threshold. A spell must cross the
> bloom threshold according to how bright it appears to the camera, not according to one time-of-
> day-specific raw number.

### 7.3 Bloom prefilter

Keep the existing max-channel brightness rule for saturated magic colors and the existing soft
knee. The required conceptual form is:

```glsl
vec3 exposed = hdrColor * exposure;
float b = max(exposed.r, max(exposed.g, exposed.b));
float soft = clamp(b - threshold + knee, 0.0, 2.0 * knee);
soft = soft * soft / max(4.0 * knee, 1e-5);
float weight = max(soft, b - threshold) / max(b, 1e-5);
// Keep raw scene units. post_process.fs exposes scene+bloom together later.
vec3 bloomSource = hdrColor * weight;
```

Do not hard-clamp `bloomSource`. The existing asymptotic soft ceiling in
`core/shaders/bloom_bright.fs` is allowed because its derivative remains positive.

### 7.4 Tone mapping and color retention

`core/shaders/post_process.fs` already uses an ACES-style curve. Keep it in the first
implementation. Tune source zoning before changing the global tone mapper:

- body/corona stays saturated and lower-energy;
- only the compact core enters the near-white shoulder;
- bloom is colored by the source but low enough not to cover the body;
- color grading must not be used to recover a hue that the HDR source already clipped to white.

Changing the global tone mapper is out of scope for phases 0–4. It affects every material and
requires a separate whole-scene approval.

### 7.5 Auto exposure (phase 5, optional until approved)

> [!NOTE]
> **(project convention):** auto exposure is bounded camera adaptation, not per-spell inverse
> brightness. All scene and VFX radiance receive the same exposure.

If implemented:

- begin with the existing graphics downsample path; do not add storage images to the fixed compute
  set (the MoltenVK storage-image/UBO quirk is documented in
  `third_party/vulkan/docs/LANDMINES.md`);
- meter log luminance before bloom so bloom cannot drive its own exposure feedback loop;
- exclude or strongly down-weight screen edges and UI;
- clamp the accepted range to an art-approved minimum/maximum EV;
- use separate adaptation speeds: faster toward bright scenes, slower toward dark scenes;
- store exposure in a tiny ping-pong target; never perform a blocking CPU readback per frame;
- expose a debug view showing metered luminance, target EV, current EV, and bloom threshold.

Auto exposure is complete only after rapid cuts between dark and bright fixtures show no flashing,
pumping, or spell-caused exposure oscillation.

## 8. Objective acceptance oracle

### 8.1 Test chart

Render the same synthetic plasma/beam fixture over these linear HDR backgrounds:

| Tile | RGB |
|---|---|
| dark neutral | `(0.02, 0.02, 0.02)` |
| middle gray | `(0.18, 0.18, 0.18)` |
| bright neutral | `(1.00, 1.00, 1.00)` |
| bright warm | `(1.00, 0.72, 0.35)` |
| bright cool | `(0.35, 0.72, 1.00)` |

Render at exposure multipliers `0.5`, `1.0`, and `2.0`. Render once with bloom disabled and once
with bloom enabled. The no-bloom image tests source readability; the bloom image tests optical
quality.

> [!NOTE]
> **(project convention):** the first oracle color is electric blue. Once it passes, repeat with
> orange fire and violet magic; do not tune only one primary hue.

### 8.2 Pixel metrics after tone mapping

For each tile, record a background sample `B`, saturated-body sample `S`, core sample `C`, and
halo sample `H` in linearized display RGB.

```text
rgbDistance(a,b) = max(abs(a.r-b.r), abs(a.g-b.g), abs(a.b-b.b))
chroma(a)        = max(a) - min(a)
luma(a)          = dot(a, (0.2126, 0.7152, 0.0722))
```

Required:

- `rgbDistance(S, B) >= 0.10` on every background and exposure;
- `chroma(S) >= 0.12` for the colored body;
- `luma(C) >= luma(S)` on dark and middle-gray tiles;
- the core may be near-white, but no more than 35% of the authored body-width samples may have all
  RGB channels above `0.98`;
- with bloom disabled, `rgbDistance(S, B)` still passes;
- enabling bloom must not reduce `chroma(S)` by more than `0.05`;
- `rgbDistance(H, B) < rgbDistance(S, B)`; the halo cannot become the strongest shape;
- no sample may be NaN/Inf, and no transparent sprite corner may differ from its background by more
  than `2/255`.

These thresholds are regression guards, not final art grading. If the approved art cannot satisfy
one, change the metric deliberately in this document with a captured reason; do not silently loosen
the test.

### 8.3 Visual checks that pixels cannot prove

- no rectangular billboard borders;
- no hard particle/ground intersections;
- no white bloom tube swallowing a thin beam;
- no hue reversal (pale outside, saturated inside is valid; saturated outside with a gray middle is
  not);
- no sort-order popping when body and emission overlap;
- no exposure pumping when a spell enters/leaves the frame;
- no bloom crawl on a sub-pixel moving line;
- VFX behind opaque geometry remains occluded.

## 9. Small-AI implementation plan

Execute one task at a time. Do not combine tasks into a broad refactor. After each task, inspect the
diff and run only the stated verification ladder before proceeding.

### Task 0 — Freeze the baseline

**Owner:** Renderer agent for the rlvk scenario; Core agent for game captures.

**Writes:** screenshots/probes under the test harness's existing temporary output only. No source
change.

1. Capture one current fire, one energy/plasma, and one lightning effect over dark and bright test
   scenery.
2. Record HDR on/off, bloom on/off, exposure, resolution, and backend.
3. Record the four samples from §8.2.
4. If a capture path returns the default shader, stop and fix the fixture path before trusting any
   number; `third_party/vulkan/docs/LANDMINES.md` documents this exact perf-test trap.

**Done when:** the before-images and measurements exist and can be regenerated.

### Task 1 — Add the Vulkan blend oracle first

**Owner:** Renderer agent (`vulkan_backend`).

**Writes:**

- `third_party/vulkan/tests/rlvk_visual_test.c`
- `scripts/run_rlvk_visual_test.sh` only if scenario registration needs script help

**Scenario name:** `bright_vfx`

1. Add a self-contained fragment shader string; do not load a repository-relative shader file.
2. Allocate an RGBA16F render target.
3. Draw the five background tiles from §8.1.
4. Draw one analytic disc/beam using the premultiplied formula from §5.2 and
   `BLEND_ALPHA_PREMULTIPLY`.
5. Draw a second control using pure additive.
6. Read back `B/S/C/H` points before screen presentation.
7. Assert the exact blend equation within half-float tolerance (`0.02` per channel).
8. Assert the premultiplied fixture passes §8.2 while the additive control is allowed to fail the
   bright-tile visibility metric; the control proves the test can detect the original problem.
9. Ensure the scenario runs through both batch and mesh draw sites if both will be used by migrated
   VFX.

**Verification:**

```bash
./scripts/check_rlvk_compile.sh
./scripts/run_rlvk_visual_test.sh bright_vfx
VALIDATE=1 ./scripts/run_rlvk_visual_test.sh bright_vfx
```

Then run the normal visual suite because this touches a draw/blend scenario.

**Stop if:** RGBA16F creation, blending, or readback differs from the existing format capability
contract. Diagnose the backend before changing Core shaders.

### Task 2 — Add one shared GLSL resolver

**Owner:** Core agent, not Renderer agent.

**Writes:** one new shared include under `core/shaders/common/` plus its dedicated source-level
contract test.

1. Add functions for straight-alpha BODY, alpha-weighted ADDITIVE, and combined PREMULTIPLIED
   output using §5.2–§5.3.
2. Parameters must be semantic masks/colors; do not embed fire/energy-specific constants.
3. Guard division by zero and clamp coverage only, never HDR RGB.
4. Add a test proving: zero coverage + zero emission is transparent; emission survives independent
   of coverage; covered body is premultiplied exactly once.

**Verification:**

```bash
./scripts/run_core_tests.sh
```

No renderer change is permitted in this task.

### Task 3 — Make appearances express the policy centrally

**Owner:** Core agent.

**Writes:** `core/vfx_appearance.h`, its owning implementation, and existing appearance tests.

1. Keep `VFX_APPEARANCE_GLOW` pure additive for sparse light-only effects.
2. Keep `VFX_APPEARANCE_FIRE` premultiplied.
3. Change structured `VFX_APPEARANCE_MAGIC` to premultiplied with non-zero body opacity; individual
   spark/glint producers explicitly request GLOW instead.
4. Do not add a new blend enum. `VFX_SURFACE_PREMULTIPLIED` already expresses the needed law.
5. Preserve `INHERIT` as exact identity for legacy effects.

**Required tests:** appearance resolution values, INHERIT identity, BODY/EMISSION enablement, and a
failure if MAGIC regresses to additive-only.

**Verification:** `./scripts/run_core_tests.sh`.

### Task 4 — Migrate one reference effect per archetype

**Owner:** Core agent.

Do these as separate patches in this order:

1. lightning (should mostly be verification; it already has BODY/HALO/CORE);
2. one plasma/energy orb;
3. one beam/trail;
4. one fire volume;
5. one explosion transition.

For each effect:

1. Write down its `coverage`, `bodyMask`, `coreMask`, colors, and gains.
2. Route output through the shared resolver.
3. Select BODY/PREMULTIPLIED/ADDITIVE through `VFXAppearance`; do not call raw blend functions from
   the effect if `core/vfx_render.h` already owns the scope.
4. Verify dark and bright captures with bloom off first.
5. Tune bloom only after the no-bloom oracle passes.
6. Add a narrow source-level contract test and an image/pixel integration fixture.

**Stop if:** the migration requires sampling scene color from the target currently being drawn.
Use the snapshot API in `core/screen_distort.h`; do not create a read/write feedback loop.

### Task 5 — Make bloom exposure-stable

**Owner:** Core agent.

**Writes:** post-FX public config only if a new explicit EV value is necessary; otherwise keep the
existing API. Change only bloom-prefilter uniform plumbing and tests.

1. Prove whether `core/shaders/bloom_bright.fs` receives raw or exposed scene color.
2. If raw, use `rawColor * exposure` only to compute brightness and prefilter weight. Write
   `rawColor * weight` into the bloom pyramid, as specified in §7.3.
3. Keep `core/shaders/post_process.fs` as the single exposure multiplication for the final
   scene+bloom composite. Do not write exposed color into bloom and then expose it again.
4. Keep the existing soft knee, thin-feature coverage, Karis first downsample, six-level pyramid,
   and asymptotic ceiling.
5. Add exposure values `0.5/1/2` to the test chart and assert the source crosses the threshold in
   camera space consistently.

**Verification:** `./scripts/run_core_tests.sh`, then Renderer agent reruns `bright_vfx` and the full
rlvk visual suite if any backend-facing behavior changed.

### Task 6 — Add bounded auto exposure only if requested after Tasks 1–5

**Owner:** Core agent with Renderer review.

This task is optional and must not block the compositing fix.

1. Create a GPU-only downsampled log-luminance chain before bloom.
2. Add center-weighted metering.
3. Add min/max EV and separate brighten/darken speeds.
4. Ping-pong the exposure result; no per-frame CPU readback.
5. Add deterministic freeze/manual mode for tests and screenshots.
6. Add debug overlays and the rapid dark↔bright cut tests from §7.5.

Do not use Vulkan storage images merely for this feature. The shared engine must retain GL/GLES
fallback parity, and `third_party/vulkan/docs/LANDMINES.md` records a compute-layout driver quirk.

### Task 7 — Performance and portability closeout

**Owner:** Renderer agent.

1. Run the entire visual suite normally and with validation.
2. On a device without push descriptors, repeat the chart because Mali takes the pool-ring path;
   `third_party/vulkan/docs/PROGRESS.md` says the forced MoltenVK pool-ring guard is not a valid
   positive oracle on the current Intel host.
3. Measure uncapped frame time, not FPS, following `third_party/vulkan/docs/PROGRESS.md`.
4. Confirm no new full-resolution scene copy occurs unless refraction requests it.
5. Confirm HDR targets are RGBA16F and all filtered/blended formats pass capability checks.
6. Human performs the final full game build; agents do not read or touch `build/`.

**Shipping gate:** §8 passes for the three reference hues on desktop Vulkan and Mali Vulkan, the
full rlvk suite is green, validation adds no new VUIDs, and frame time stays inside the agreed
mobile budget.

## 10. Prohibited shortcuts

- Do not raise every `emissiveBoost` until the effect is visible.
- Do not make the entire sprite white-hot.
- Do not use bloom as the only visible silhouette.
- Do not reuse emission weight as opacity/coverage.
- Do not pre-multiply an additive output that is already multiplied by source alpha in hardware.
- Do not sample the active scene attachment while writing it.
- Do not clear a shared VFX color target in a way that clears scene depth.
- Do not introduce R32F blended/filtered targets without capability checks.
- Do not add a Vulkan-only material path that diverges from GL/GLES semantics.
- Do not tune from a single dark arena screenshot.
- Do not accept a numeric probe without viewing its rendered image.

The corresponding backend hazards and guards live in
`third_party/vulkan/docs/LANDMINES.md`; the cross-module feedback-loop and depth-clear hazards live
in `ENGINE_LANDMINES.md`.

## 11. Definition of done

The project is done with this specification only when all of the following are true:

- source readability passes with bloom disabled;
- bloom adds perceived energy without defining the silhouette;
- colored bodies pass the bright-neutral and bright-colored metrics at three exposures;
- fire, magic/plasma, beam/trail, lightning, and explosion each have one migrated reference;
- the compact core may reach white, while the surrounding body preserves element hue;
- the Vulkan `bright_vfx` scenario and full visual suite pass normally and under validation;
- Mali or another non-push-descriptor Vulkan device passes the same chart;
- no unsafe scene feedback, shared-depth clear, R32F assumption, stale uniform, or new format
  feature dependency is introduced;
- the human approves side-by-side dark/daylight captures at gameplay camera distance.

## Patch Log

| Date | Editor | Section edited | Based on which source | Tier |
|---|---|---|---|---|
| 2026-08-16 | Codex | Initial research and complete implementation specification | Epic Unreal blend/exposure/bloom/tonemapper docs; Unity tonemapping docs; Hogwarts Legacy GDC 2024; Call of Duty: Advanced Warfare SIGGRAPH 2014; Prototype 2 Game Developer article; Khronos blend docs; local headers/shaders/backend files named in §3 | External ground-truth + local ground-truth + marked project conventions |
| 2026-08-16 | Codex | Implemented RGBA16F readback, `bright_vfx` Vulkan oracle, shared compositor include, fire/smoke resolver wiring, and Magic premultiplied appearance policy | `third_party/vulkan/rlvk/rlvk_format.inl`; `third_party/vulkan/tests/rlvk_visual_test.c`; `core/shaders/common/vfx_composite.glsl`; `core/shaders/{fire_funnel,energy_smoke}.fs`; `core/vfx_contrast.c` | Code complete for these slices; compile/visual gates pending dependency cache |
| 2026-08-16 | Codex | Migrated ShieldShell to `VFX_APPEARANCE_MAGIC`; body now uses premultiplied output and emission uses the shared emission resolver | `core/composition/common/vc_shield_shell.inl`; `core/shaders/glass_shell.fs`; `core/tests/shield_shell_test.c`; `core/tests/vfx_render_layers_contract_test.c` | Core ShieldShell contract passes; Vulkan visual capture still pending dependency cache |
| 2026-08-16 | Codex | Made bloom threshold/weight exposure-aware without double-exposing the bloom pyramid | `core/shaders/bloom_bright.fs`; `core/post_fx.c`; bloom contract tests | Core bloom contracts pass; Vulkan visual capture still pending dependency cache |
| 2026-08-16 | Codex | Routed plasma and lightning reference shaders through the shared BODY/EMISSION resolver | `core/shaders/plasma_shell.fs`; `core/lightning/shaders/lightning_stroke.fs`; appearance contract | Reference shader migration complete for these two archetypes |
| 2026-08-16 | Codex | Routed shock-ring and magic-filament reference shaders through the shared BODY resolver; added exposure chart coverage at 0.5/1/2 | `core/shaders/{shock_ring,magic_filaments}.fs`; `core/tests/bloom_pyramid_contract_test.c` | Source contracts added; full runtime visual validation still pending |
| 2026-08-16 | Codex | Routed the beam/trail pass resolver and layered annulus through the shared compositor | `core/trails/shaders/trail_deform.fs`; `core/shaders/vfx_layered_annulus.fs`; appearance contract | Beam/trail and layered dual-pass migration complete at shader level |
| 2026-08-16 | Codex | Re-ran Vulkan gates against the existing local SDK/cache; compile passes, runtime/visual are blocked by MoltenVK `VK_ERROR_INCOMPATIBLE_DRIVER` in the agent environment | `/tmp/rlvk_check_cache_local`; `/tmp/rlvk_visual_cache`; local Vulkan SDK | Environment blocker confirmed; no code failure at compile gate |
| 2026-08-16 | Codex | Re-ran with escalated GPU access: runtime passes, `bright_vfx` passes normally and under validation, full visual suite passes `24/24` in both modes | Local Intel Iris 6000 + MoltenVK 1.2.11 | Validation emits two existing portability warnings for zero-stride instanced probes; no scenario failures |
| 2026-08-16 | Codex | Continued legacy shader audit: routed aura, ground aura, smoke column and black-hole swirl through the shared BODY resolver | `core/shaders/{aura_shell,ground_aura,smoke_column,black_hole_swirl}.fs`; appearance contract | Shader-level migration complete; full game shader compile remains part of final human build |
| 2026-08-16 | Codex | Updated trail contract tests for the shared BODY/EMISSION resolver; Core suite returns to 68/73 with only known baseline failures | `core/trails/shaders/trail_deform.fs`; `core/tests/trail_deform_test.c` | Trail contract passes; remaining failures are unrelated existing suites |
| 2026-08-16 | Codex | Final legacy audit migrated remaining VFX material outputs for decals, GPU/CPU particles, crystal, ground wave, water splash, additive-soft, and trail body | `core/{decals,particles,trails}/shaders`; `core/shaders/{crystal,ground_wave,water_splash,additive_soft}.fs`; appearance contract | Appearance contract passes; remaining direct `vec4` outputs are debug, post-FX, fluid/depth, or data-encoding paths |
| 2026-08-16 | Codex | Completed final material audit for generic effect, metaball, puddle, rim, taiji, trail glow and trail volume outputs | `core/shaders/{effect_material,metaball_threshold,puddle,rim_glow,taiji}.fs`; `core/trails/shaders/{trail_glow,trail_volume}.fs` | Appearance contract passes; remaining direct outputs are intentional debug/data/post-FX paths |
| 2026-08-16 | Codex | Runtime audit found two raw-loaded legacy shaders cannot consume shared includes; reverted only those two include migrations to preserve runtime compilation | `core/shaders/metaball_threshold.fs`; `core/trails/shaders/trail_glow.fs`; appearance contract | Runtime-safe exceptions documented; migrate them only after their loader is upgraded |
| 2026-08-16 | Codex | Fixed aura shader function-name collision exposed by the full-game compile (`noise.glsl` and aura had duplicate `vnoise3/fbm3`) | `core/shaders/aura_shell.fs` | Renamed aura-local noise functions; preserves its tuned 3-octave field while allowing shared includes |
