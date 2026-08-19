# Bright-background VFX rendering specification

> **Document archetype:** implementation plan / progress specification. This is not an API
> contract and does not record completed work.
>
> **Status:** the `bright_vfx` oracle is now a real acceptance chart — six element hues x five
> backgrounds x three exposures, bloom off and on, measured through the SHIPPING ACES curve and
> the SHIPPING bloom shaders (it loads `core/shaders/post_process.fs` and the three
> `bloom_*.fs` rather than re-implementing them). It passes, and building it produced §5.5,
> §5.6 and §12 below, which are measurements, not theory. The shared Core compositor is wired
> into the reference shaders and the bloom prefilter evaluates threshold/weight in exposure
> space while writing raw HDR. Remaining: §12's open items (hue-preserving tone map, RGBA8
> fallback oracle, temporal metrics) and Mali confirmation.
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

### 5.5 Three radiance terms, not two (measured)

> [!NOTE]
> **(project convention):** the 3–9 px corona carries its own **saturated** radiance, separate
> from both the coverage term and the white core.

The first `bright_vfx` fixture was built as coverage + a narrow white core, exactly as §5.2
reads. It failed: a warm body over the bright-cool tile measured `chroma(S) = 0.051`. The cause
is structural, not a tuning error — a translucent body over a **complementary** background mixes
toward neutral by construction, because `bodyColor·a + bg·(1-a)` moves the result toward the
mid-point of two opposed hues. Coverage buys luminance contrast; it cannot buy chroma there.

So the premultiplied form in §5.2 is completed by a third term:

```glsl
vec3 coveredBody   = bodyColor * bodyIntensity * coverage;   // attenuates the background
vec3 coronaLight   = bodyColor * coronaMask * coronaGain;    // SATURATED radiance, 3-9 px
vec3 emittedLight  = emissionColor * coreMask * emissionGain;// whitened radiance, 1-3 px
finalColor = vec4(coveredBody + coronaLight + emittedLight, coverage);
```

`coronaGain` sits well below `emissionGain` (the reference fixture uses 1.6 against 4.0) and
uses the **body** hue, not the emission hue. With it, the same fixture measures `chroma 0.37`
on that tile.

### 5.6 Two laws for the additive halo (measured)

> [!NOTE]
> **(project convention):** the halo is a SURROUND. It must fade out inside the body radius,
> and it must carry the saturated body hue.

Both were found by measurement, and both are the difference between a readable effect and the
"milky film" this document exists to prevent:

1. **The halo is an ANNULUS, not a fill**, and both edges of it were measured. A halo still at
   ~0.8 mask over the body adds back exactly the light the body's coverage had just removed, so
   the body stops attenuating the background at all. But a halo that decays fast enough to clear
   the body on its own (a squared falloff) is down to `rgbDistance 0.009` by r=11 and no longer
   occupies the 8–32 px band §5.4 asks for — it passes "the halo is not the strongest shape"
   vacuously, by not existing. The working profile is zero until just past the body radius, then
   a **linear** decay to the halo radius:

   ```glsl
   float haloMask = (1.0 - smoothstep(0.0, haloRadius, r))
                  * smoothstep(bodyRadius * 0.9, bodyRadius * 2.0, r);
   ```

   Measured with it: on the dark tile the halo reads at `rgbDistance 0.47`, on the white tile at
   `0.017` — strong against darkness, subordinate against brightness, which is what a low-energy
   additive surround is supposed to do. `bright_vfx` asserts a measurement floor on the dark tile
   so the ordering test cannot go vacuous again.
2. **The halo carries the saturated BODY hue, never the whitened EMISSION hue.** An
   emission-coloured halo pumps light back into precisely the channels the body darkened. A blue
   effect over the bright-cool tile measured `rgbDistance 0.06` with an emission-coloured halo and
   `0.21` with a body-coloured one, everything else held constant.

### 5.7 The darkening budget

> [!NOTE]
> **(project convention):** on any background brighter than mid-grey, the effect must pull at
> least one channel BELOW the background. Not by a specific amount — at all.

This is a **structural** rule, not a magnitude one: §8.2's `rgbDistance` already sets the
magnitude. What no other metric checks is the *mechanism*. An effect that is visible purely
because it adds light is riding on a property of the background it was tuned against, and it
will fade out as the scene gets brighter — which is the failure mode that only appears in
daylight, long after the effect was approved in the night arena. Additive output can never
satisfy this rule (§4), which is exactly why the rule is the right one to test.

The reference `ShieldShell` oscillation (`docs/PROGRESS.md`, 2026-08-16) is this failure: scalar
alpha/gain tuning moved it between a faint wall and an opaque sphere because neither end
established a darkening budget.

## 6. Effect recipes

These are defaults, not hard-coded constants. Preserve per-effect authoring controls.

### 6.1 Fire

> [!NOTE]
> **(project convention):** fire uses a premultiplied combined surface by default.

- `coverage`: flame density plus soot density; soft at the silhouette.
- `bodyColor`: deep red/orange at low temperature and neutral/dark soot where smoke dominates.
- `coreMask`: the highest-temperature channel, narrower than flame coverage.
- `emissionColor`: yellow-white core with an orange saturated shoulder.
- Start with `coverage = 0.60..0.80`, `emissionGain = 3..8` in linear HDR. **The old
  `0.35..0.70` was wrong at the bottom**: measured on the §8 chart, the reference fixture
  needs `~0.68` at the saturated-body sample to clear `rgbDistance 0.10` against white at
  EV2. On bright scenery contrast comes from coverage, not from emission (§4).
- Spawn smoke as an alpha/premultiplied body, not an additive gray sprite.
- Bloom comes from the hot channel only. A broad orange body must remain visible with bloom off.

### 6.2 Energy/plasma/magic orb

> [!NOTE]
> **(project convention):** named MAGIC effects are not pure additive by default. Pure `GLOW`
> remains additive; structured magic uses premultiplied coverage.

- Use an alpha-composite shell/body with `coverage = 0.55..0.80` to hold saturated hue.
  **The old `0.20..0.55` was below what §8.2 requires** and is how structured magic ended
  up invisible in daylight: at `0.20` a body cannot attenuate enough to be seen against
  white at any exposure. ENERGY ORB needed a `0.30` base coverage FLOOR on top of its
  noise modulation before its body area on white reached parity with its body area on
  black (0.07% -> 9.89%).
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
  bright-background mechanism even though radiance above 1.0 is unavailable. **This is now
  tested, not asserted:** scenario `bright_vfx_ldr` runs the whole §8 chart through an RGBA8
  scene target and the source-readability metrics pass on coverage alone.
- **On that path bloom is INERT below exposure 1.25.** An RGBA8 scene buffer clips at 1.0, so
  nothing can cross a 1.25 *exposed* threshold until exposure itself does — an effect authored
  at `emissionGain = 8` has its emission clipped away before the prefilter ever sees it.
  Measured by `bright_vfx_ldr`, which scopes its own bloom self-check to the exposures where
  bloom is physically possible.

> [!WARNING]
> **This fallback is NOT "the mobile path", and an earlier version of this section said it
> was.** `ScreenDistort_Init` (`core/screen_distort.c:180`) takes RGBA8 only when
> `WUXING_NO_HDR` is set or the RGBA16F framebuffer fails to complete. Under Vulkan 1.1
> `R16G16B16A16_SFLOAT` is a **mandatory** colour-attachment format, so on any conformant
> Vulkan device — desktop or Android — the probe succeeds and HDR is on. The "GLES2 devices
> without float-renderable color" comment in that file predates the Vulkan backend. What
> `bright_vfx_ldr` actually guards is the `WUXING_NO_HDR` override and a GL/GLES build
> (`WUXING_USE_VULKAN=OFF`) on a device without float-renderable colour — worth keeping, but
> do not plan mobile authoring around it. Which backend the shipping Android build uses is
> not verifiable from here (`android.wuxing_skills/` is off-limits to agents); confirm it
> before drawing any mobile conclusion from this scenario.

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

> [!NOTE]
> **(project convention):** the bloom threshold must sit **above the brightest expected
> background in exposed space**. Below it, the background blooms itself, veils the frame, and
> costs every effect chroma no matter how the effect is authored — measured at ~0.06 chroma on
> the bright tiles at EV2, where a 1.0 background exposes to 2.0 against a 1.25 threshold. This
> is a property of the threshold versus the scene, so `bright_vfx` scopes §8.2's chroma-drop
> limit to the non-self-blooming case and prints the drop otherwise.

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

### 7.3b Output dither (required)

The swapchain is 8-bit and the widest, flattest gradients in the frame are exactly what this
pipeline produces: a 32 px halo and the bloom skirt around it. On a bright background those
quantise into visible concentric rings. One LSB of triangular-PDF noise added at the very end of
`core/shaders/post_process.fs` breaks the ring into dither for the cost of two hashes.

The hash must be **sin-free** — `fract(sin(...))` degenerates on Mali at large domains
(`ENGINE_LANDMINES.md` #4) and `gl_FragCoord` is a large domain.

Clamp **after** the dither, not before. The noise is added on top of an already tone-mapped
value, so without the clamp a channel ACES had pinned at `1.0` comes back at `1.004`. The UNORM
swapchain hides it, but any HDR probe reading this pass then sees post-tone-map values the curve
cannot produce — an "impossible number" that costs a debugging session to attribute.

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

### 7.6 The radiance anchor — what a number in the scene target MEANS

Measured 19/08/2026 with the HDR probe wired into `SceneTargets_End`
(`WUXING_VFX_PROBE_PREFIX` / `_FRAME`, `core/render_target_probe.h`), which dumps the
scene target as float32 RGBA **before** the tone map. Until this existed there was no
answer to "what radiance is this effect actually emitting", so every brightness decision
in the project was made by eye against one background — which is most of why authoring for
bright scenery has cost what it has.

**The probe is trustworthy for levels.** Ground-truthed against known clears: `0x050505`
reads back 0.0197, `0x2E2E2E` 0.1804, `0xFFFFFF` 1.0001, with p99 exactly the clear value
in all three. So the 8-bit background lands in the HDR target **1:1 linear, no sRGB
conversion**. (The header's own "R16F CPU readback is diagnostic only" warning is
over-cautious for level questions; it may still hold for fine HDR *shape*.)

#### The scale

| band | scene-referred | what lives there |
|---|---|---|
| night arena background | **0.02** | the shipping maps |
| mid / overcast | **0.18** | §8.1's mid tile |
| a white surface, full daylight | **1.00** | the ceiling of ordinary scenery |
| bloom threshold (shipping) | **1.25** | deliberately above that ceiling — §7.3 |
| tone map's white-hot band | **5.0 → 12.0** | where `toneMapScene` desaturates a core to white |

Ordinary scenery therefore occupies `[0.02, 1.0]`, and **1.0 is the number every authoring
decision should be judged against** — not 255, not "looks bright".

#### What the shipping effects actually emit (dark background, warmup 90)

| | p99 | p99.9 | max | >1.0 | >4.0 |
|---|---|---|---|---|---|
| FLAME VOLUME | 0.53 | 2.12 | 2.68 | 0.72% | 0% |
| VOLUME TRAIL | 1.25 | 1.74 | 3.34 | 1.93% | 0% |
| ENERGY ORB | 1.29 | 2.49 | 26.72 | 6.42% | 0.05% |

**Two findings fall straight out of this table, and neither was visible before.**

**1. A flame's bright body is DIMMER THAN A WHITE WALL.** FLAME's p99 is 0.53 — half the
radiance of a 1.0 background. It cannot separate from bright scenery by luminance at all;
everything it has left is chroma and the §5.7 darkening budget. That is not an authoring
mistake to be fixed by turning it up — it is the quantitative form of §5.5, and it explains
why `detail` collapses ~3x from dark to white on all three fixtures (§11b baseline) while
`cover%` barely moves.

**2. Two of the three flagship effects never reach the tone map's own white-hot band.**
`toneMapScene` desaturates toward white over `smoothstep(5.0, 12.0, peak)` (§12.1). FLAME
peaks at 2.68 and TRAIL at 3.34 — **neither ever enters it**. Only the ORB (26.72) does.
So the curve was tuned for a range two of the three effects are not authored in, and no
amount of tuning either one in isolation could reveal that.

#### The anchor, as authoring targets

Derived from the measurements above, not invented:

| layer | target | why |
|---|---|---|
| BODY | **0.3 – 1.0** | reads on dark scenery; on bright it must earn its place by coverage and darkening, never by brightness |
| CORONA | **1.5 – 2.5** | clears the 1.25 threshold, so it blooms against **any** background |
| CORE | **5 – 12** | enters the tone map's white-hot band; below 5 a core cannot read as hot no matter how saturated |

An effect with no pixel above 1.25 will not bloom on a white background, and one with no
pixel above 5.0 has no white-hot core — both are now checkable before anyone renders a
frame, which is the whole point of writing the scale down.

#### Library survey, 19/08/2026 — six effects, dark background, warmup 90

| effect | p99 | max | share above the 1.25 threshold |
|---|---|---|---|
| TRAIL BLADE | 0.020 *(= the background)* | 1.52 | 0.002% |
| TRAIL MAIN | 0.020 *(= the background)* | 1.72 | 0.007% |
| TRAIL ENERGY | 0.047 | 3.47 | 0.23% |
| FLAME VOLUME | 0.53 | 2.68 | 0.72% |
| VOLUME TRAIL | 1.25 | 3.34 | 1.93% |
| ENERGY ORB | 1.29 | 26.72 | 6.42% |

**Five of the six peak below 3.5, and exactly one reaches the tone map's white-hot band
(5.5–12).** Two of them — the swept blade and main trails — have a p99 equal to the
background itself and put roughly 20 pixels of a 921,600-pixel frame over the bloom
threshold. The curve's shoulder and the bloom prefilter are, for most of this library,
machinery that almost never engages.

**Where the ceilings came from is NOT one story, and only one of them is a mistake:**

- `volume_trail_test.c` caps the additive budget at 1.0 because **"1.00 is already full
  white"**. That is false here — 1.00 scene-referred tone-maps to 0.80 display, and display
  white is not reached until ~5.5. It is LDR reasoning surviving into an HDR pipeline, and
  it is enforced by a test. Lifting the ENERGY layers 0.16/0.46 → 0.35/1.00 was measured:
  on white, `structure` 0.076 → 0.217, `detail` 0.049 → 0.099, `chroma` 0.609 → 0.875, and
  the dark→white `detail` collapse fell from 62% to 12%. Cost: ~12% less `detail` on dark.
- The swept trail's 0.5 body ceiling and the beam core's 0.35 are **empirical**, measured on
  screen, with sound stated reasons (bloom lifts near-threshold content; additive alpha is
  added light, not opacity). They are not premise errors.
- `flame_volume.inl`'s note that the value interacts with the background — "the test arena's
  sky sits around 0.35, so the same value that reads as fire against a night scene blows out
  here" — is already correct HDR-aware reasoning.

> [!WARNING]
> **A hypothesis that did NOT survive, recorded so it is not retried.** The swept trail's
> ceiling was halved twice on "it burned out on screen", and it was tempting to blame the
> unpinned `tuning.cfg` (`bloom_threshold` 0.9, `bloom_intensity` 0.35 against a shipping
> 1.25 / 0.12). Tested directly with `WUXING_TUNING` pointing at each configuration: TRAIL
> MAIN measures within noise of itself on dark and mid under both, because at its authored
> level it sits far below either threshold. The misconfiguration is real and worth having
> fixed, but it does not explain those ceilings.

> [!NOTE]
> **(project convention):** state an effect's intended band in its composer, and check it
> with the probe rather than by eye. The three rows above are the measured reference, not a
> ceiling — an effect may deliberately sit outside them, but it should say so.

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
> **(project convention):** the chart runs all **six element hues**, not the three originally
> specified. Kim and Thuy are the hard cases and a warm+blue-only chart hides them: a pale
> white-gold Kim body measured `chroma 0.02` / `rgbDistance 0.04` against a white background at
> EV2, i.e. genuinely invisible there whatever the compositor does. Near-neutral emitters have no
> chroma headroom on bright ground — **Kim must be authored as deep gold, not white-gold.**

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

**Recorded metric changes** (per the paragraph below, each with its reason):

- `rgbDistance` is the **max-abs-channel** distance as defined above. The first implementation
  used a Euclidean distance, which silently changed what the `0.10` threshold meant.
- The bloom chroma-drop limit is `max(0.05, 0.15 * chromaWithoutBloom)`, not a flat `0.05`. An
  absolute delta is not scale-free: a hue at `chroma 0.80` losing `0.07` to its own core bloom is
  8% relative and looks correct, while a hue starting at `0.20` losing `0.06` is a third of its
  colour. The limit must clear both an absolute floor and a relative share.
- The transparent-corner tolerance is `4/255`, not `2/255`, because §7.3b's output dither can
  legitimately move two samples by `2/255` between them. A real billboard border is far larger.
- `Kim`'s chroma floor is `0.10`, not `0.12` — see the §8.1 note on near-neutral hues.
- The saturated-body sample `S` is taken at the middle of the §5.4 band, not at its outer
  shoulder. Both edges of that window were measured: too close to the core and the core's own
  bloom desaturates the sample by ~0.07; too close to the taper and the background leaks through
  and costs ~0.02 of `rgbDistance` on white at EV2.

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

## 9b. What `bright_vfx` actually asserts today

Six hues x five backgrounds x three exposures (0.5/1/2), in two passes:

- **Pass A, bloom off — source readability.** Per tile: `rgbDistance(S,B) >= 0.10`, per-hue
  `chroma(S)` floor, `luma(C) >= luma(S)` on the dark and mid tiles, halo weaker than body,
  the §5.7 darkening budget on bright tiles, at most 3 of 9 body-width samples fully white, and
  the transparent-corner check. The additive control row must never darken any channel and must
  lose chroma to the premultiplied row of the same hue on white — it proves the chart still
  detects the problem the document is about.
- **Pass B, bloom on — optical quality.** Chroma retention (scoped per §7.2), halo still weaker
  than body, plus a self-check that enabling bloom changed the halo at all, so a pyramid that
  never reaches the composite cannot make every assertion vacuously true.
- **Blend law at BOTH draw sites.** The 2D batch and `rlvkDrawMesh` build pipelines
  independently; a flat premultiplied source over the five backgrounds pins both to
  `src + dst*(1-a)` within `0.02`.
- **Structural guards.** No NaN/Inf anywhere in the frame; the readback orientation is *resolved*
  from a hue signature rather than assumed.

## 11b. Measuring a REAL effect against the chart (`render_vfx_matrix.sh`)

`bright_vfx` measures a synthetic fixture. To measure a shipping effect the same way:

```bash
scripts/render_vfx_matrix.sh 35 40 90 140     # fixture index, then lifetime frames
```

> [!WARNING]
> **Every baseline recorded below §11b before 19/08/2026 was taken with `tuning.cfg`
> UNPINNED, and none of them recorded its contents.** `Tuning_Init` runs before the
> headless branch in `main.c`, so `--render-vfx` inherits the working copy of that file —
> which persists across sessions. When this was found it held `bloom_threshold = 0.9`
> (below 1.0, so every diffuse surface blooms itself and veils the frame — §7.3 says the
> threshold must sit ABOVE the brightest expected background), `bloom_intensity = 0.35`
> against a shipping 0.12, and `postfx_hue_restore = 0.5` against a shipping 0.6. Treat
> the older numbers as indicative, not reproducible, and do not diff them against a run
> taken after the pin.
>
> The harness now pins to the shipping defaults (`WUXING_TUNING=none`) and writes the pin,
> the binary's timestamp and the git HEAD into `config.txt` beside the captures. Pass
> `WUXING_TUNING=<path>` to measure a specific configuration on purpose.

It renders one fixture at identical camera, resolution and frame across the §8.1
backgrounds via `WUXING_VFX_BG` (which also skips map+skybox — the skybox would paint over
the clear), plus a **background plate per colour** rendered with no fixture, and reports
per background: footprint, body area, darkening fraction (§5.7 on real content), internal
structure, chroma, and distance from the background.

Three methodology notes, all learned by getting them wrong here first:

- **Never compare screenshots taken at different apparent size.** At a smaller size every
  structure merges, so background and scale are confounded and neither can be attributed.
  The whole point of the matrix is that the ONLY variable is the background.
- **The background reference must be a plate, not derived from the frame.** The first
  version took a radial median of the image itself; the background's own bloom lifts the
  frame near the effect, and the mask then reported 39% of the frame as effect where the
  true footprint is 6%.
- **Darkening is measured on LUMA, never per channel.** The colour grade's saturation
  stage, `mix(vec3(luma), col, sat)` with `sat > 1`, is not per-channel monotonic. A
  provably pure-additive effect (ENERGY ORB, `VFX_SURFACE_ADDITIVE` at the draw site)
  measured **97.9% of its footprint "darkened"** against the cool background — R +77,
  G +9, B −5 — because the added warm light raised luma and the saturation operator then
  pushed the already-below-luma blue further down. Nothing attenuated anything. Additive
  can only ever add light, so a drop in *luminance* is the honest coverage test, and the
  saturation operator is luma-preserving by construction. Note this is the SAME assumption
  — per-channel monotonicity — that §12.1's tone-map candidate also breaks.

### PINNED BASELINE — 19/08/2026, `WUXING_TUNING=none`, git 9fce0d0

The first matrix taken with the tuning pin in place, and therefore the first one that can
be re-run and diffed. **This is the reference for the `core/scene_targets` extraction:**
that refactor moves render-target ownership without changing a pixel, so re-running these
three fixtures afterwards must reproduce these numbers exactly. Any drift is the refactor,
not the effect.

Warmup 90, the middle of each fixture's life:

| fixture | bg | cover% | body% | darken% | structure | detail | chroma | \|d\| |
|---|---|---|---|---|---|---|---|---|
| FLAME VOLUME (37) | dark | 1.536 | 1.38 | 0.0 | 0.474 | 0.056 | 0.648 | 0.724 |
| | mid | 1.664 | 1.39 | 2.7 | 0.312 | 0.040 | 0.619 | 0.531 |
| | white | 1.604 | 1.15 | 93.4 | 0.118 | 0.017 | 0.578 | 0.285 |
| | warm | 1.479 | 0.61 | 61.4 | 0.181 | 0.021 | 0.747 | 0.120 |
| | cool | 1.601 | 1.31 | 21.8 | 0.142 | 0.022 | 0.470 | 0.496 |
| VOLUME TRAIL (34) † | dark | 6.368 | 6.06 | 0.0 | 0.243 | 0.113 | 0.871 | 0.873 |
| | mid | 7.712 | 6.21 | 1.9 | 0.228 | 0.091 | 0.853 | 0.565 |
| | white | 6.097 | 6.00 | 99.8 | 0.217 | 0.099 | 0.875 | 0.696 |
| | warm | 6.038 | 5.43 | 99.0 | 0.213 | 0.092 | 0.886 | 0.251 |
| | cool | 6.880 | 6.04 | 71.3 | 0.214 | 0.086 | 0.870 | 0.729 |
| ENERGY ORB (11) | dark | 10.174 | 10.14 | 0.0 | 0.160 | 0.053 | 0.854 | 0.931 |
| | mid | 10.171 | 10.14 | 0.0 | 0.118 | 0.026 | 0.818 | 0.722 |
| | white | 10.160 | 10.10 | 99.6 | 0.068 | 0.030 | 0.684 | 0.465 |
| | warm | 10.124 | 9.93 | 98.8 | 0.067 | 0.020 | 0.845 | 0.216 |
| | cool | 10.105 | 10.09 | 91.2 | 0.070 | 0.015 | 0.630 | 0.673 |

> [!IMPORTANT]
> **Only FLAME VOLUME and VOLUME TRAIL are bit-reproducible. ENERGY ORB is not.**
> Established on 19/08/2026 while using this table as the `core/scene_targets`
> acceptance oracle: FLAME and TRAIL reproduced every digit across the refactor, while
> ENERGY ORB moved slightly. Two further runs **of the same binary** then disagreed with
> each other — cool `darken%` spanning 89.0 / 89.3 / 91.2 and `chroma` 0.675 / 0.676 /
> 0.684 — so the spread is the fixture's own, not the change under test, and it is wider
> than the delta it was being used to detect. The harness's "pinned timestep + RNG seed"
> therefore does not hold all the way down for this fixture; `vc_energy_orb.inl` itself
> contains no clock or RNG call, so the source is further down the stack and is not yet
> identified.
>
> **Use FLAME VOLUME + VOLUME TRAIL for any bit-exact comparison.** ENERGY ORB stays in
> the table as a level reference, but a difference under roughly 0.01 chroma or 2 points
> of `darken%` on it means nothing.

† **VOLUME TRAIL's rows were re-baselined on 19/08/2026** when its ENERGY layers were
raised out of the LDR cap (0.16/0.46 → 0.35/1.00, see the survey below). The pre-change
row read `white 6.026 / 5.98 / 99.9 / 0.076 / 0.049 / 0.609 / 0.439`; keep it in mind only
as the before-picture, never as a target. FLAME VOLUME and ENERGY ORB are untouched, and
FLAME reproduced every digit across that change, which is what proved the edit was scoped
to the one kind it named.

`autotest_output/` is gitignored, so the captures themselves do not survive a clean — these
rows are the durable record, and each run's `config.txt` states the pin, the binary
timestamp and the git HEAD that produced it.

One reading worth carrying forward, visible in all three: **`detail` collapses from dark to
white** (FLAME 0.056 → 0.017, TRAIL 0.129 → 0.049, ORB 0.053 → 0.030) while `cover%` barely
moves. The silhouettes hold; the internal texture is what the bright background eats. That
is §5.5, and it is not an authoring failure in any of the three — it is what §7.6's missing
radiance anchor leaves unresolved.

### First result — VOLUME TRAIL (fixture 35), the largest in-band effect

| background | body% | darken% | structure | chroma |
|---|---:|---:|---:|---:|
| dark | 6.09% | 0.0% | **0.327** | 0.787 |
| mid | 6.11% | 25.0% | 0.267 | 0.734 |
| white | 6.07% | 99.6% | **0.085** | 0.418 |
| warm | 4.69% | 99.0% | 0.107 | 0.736 |
| cool | 6.11% | 36.2% | 0.126 | 0.335 |

- **Silhouette and coverage: PASS.** Body area is flat across backgrounds, and on bright
  ground it darkens 99.6% of its own footprint — this is a real premultiplied body
  (`trail_volume.fs` ends in `VFX_ResolveBody`), not an additive one. It passes §5.7
  comfortably, unlike ShieldShell.
- **Internal structure: FAILS, by 4x.** The filament network's contrast collapses from
  0.327 to 0.085. Verified not to be an artifact of the mean rising: absolute luminance
  std falls 40.9 -> 16.5 over the same ~56,000 core pixels.
- **Chroma halves on white (0.79 -> 0.42) and is worst on the complementary cool
  background (0.34)** — §5.5's finding, reproduced on real content.

**Why, and it is the same law again.** For an alpha body, two pixels of the same colour
differing only in coverage compose to `out1 - out2 = (C - B)(a1 - a2)`: the effect's
INTERNAL contrast is proportional to how far its colour sits from the background. Against
black, an orange filament is far from its gaps; against white it is not, so filament and
gap converge and the volume reads flat. Coverage cannot fix this — it is what coverage
does.

The fix is §5.5's split, arrived at independently by the synthetic chart: the filaments
need their own HDR **emission**, whose brightness does not depend on the background, and
the gaps want their own darkness (soot) rather than borrowing the scene's. VOLUME TRAIL
currently emits nothing — its single output is a BODY.

### ENERGY ORB — a contract mismatch, found by measurement and confirmed in code

| background | body% | darken% | structure | chroma |
|---|---:|---:|---:|---:|
| dark | 10.04% | 0.0% | 0.466 | 0.617 |
| mid | 10.08% | 0.0% | 0.189 | 0.435 |
| white | **0.07%** | 0.0% | **0.003** | 0.092 |
| warm | **1.13%** | 0.0% | 0.034 | 0.407 |
| cool | 10.07% | 0.4% | 0.039 | 0.086 |

It does not merely lose structure like the others — **it loses the body**: 10.04% → 0.07%
on white, a 99.3% collapse, with `darken%` at zero everywhere. The code says why, and the
two agree exactly:

- `core/composition/common/vc_energy_orb.inl:118` opens the draw with
  **`VFX_SURFACE_ADDITIVE`**;
- but its shader `core/shaders/aura_shell.fs:88` returns
  **`VFX_ResolveBody(col, 1.0, alpha)`** — a straight-alpha BODY.

`vfx_composite.glsl`'s own header states the contract: `ResolveBody` must be drawn with
the ALPHA surface. Bound additive instead, the alpha is consumed as a brightness
multiplier and the coverage term is discarded, so the effect can never attenuate anything
— which is precisely what the matrix measured before anyone read the file.

This is also a case §6.2 and Task 3 already legislated ("named MAGIC effects are not pure
additive by default; structured magic uses premultiplied coverage"). The 2026-08-16
appearance migration did not reach it because the surface is hardcoded at the draw site
rather than resolved through `VFXAppearance`.

#### ENERGY ORB fixed — and what each half of the fix was worth

Two changes, both defaulting to the old behaviour for every other consumer, each measured
separately (warmup 90, `body%` = share of the frame above 32/255 from the plate):

| step | body% dark | body% white | darken% white | chroma white |
|---|---:|---:|---:|---:|
| as found (`VFX_SURFACE_ADDITIVE`) | 10.04% | **0.07%** | 0.0% | 0.092 |
| + surface matched to the resolver | 10.06% | 1.04% | 95.2% | 0.348 |
| + cylinder height-fade cancelled | 10.08% | **8.09%** | 98.0% | 0.440 |
| + coverage floor 0.30 | 10.08% | **9.89%** | 97.9% | 0.459 |

**The silhouette now survives daylight at parity with night** — 9.89% against 10.08%,
from 0.07%. Note the attribution: the blend-contract fix was necessary but bought little
on its own; **cancelling the borrowed cylinder fade was the dominant defect**, worth 8x by
itself. `aura_shell.fs` fades alpha to zero toward the top because it was written for an
aura rising off the ground; on a sphere that deletes the upper half's coverage outright.
The coverage floor is a cheap final 22%, costing ~13% of fine detail.

**A metric lesson that changed the reading.** `structure` appeared to fall from 0.466 to
0.155 across this work, which reads as a regression. It is not: the coefficient of
variation counts a broad smooth gradient exactly like fine texture, and most of the
original 0.466 WAS the wrong vertical fade. The analyzer now also reports `detail`
(luminance minus a blurred copy), which only sees short-length-scale contrast; by that
measure the fix costs 0.069 -> 0.060, not 3x. **Do not read `structure` alone after
changing anything that alters a large-scale gradient.**

**Still open on this effect:** `detail` is 0.060 on dark and 0.012 on white, a ~6x
collapse that coverage cannot fix — it is the §5.5 law, and closing it needs the emission
split (the orb's `VFX_ComposeCoreGlow` core plus a saturated corona), not more alpha.

### The three largest in-band effects, measured (warmup 90)

| | VOLUME TRAIL | FLAME VOLUME | PROJECTILE *(deleted)* |
|---|---|---|---|
| darken% on white (§5.7) | **99.6%** ✅ | **91.0%** ✅ | **28.7%** ❌ |
| body% dark → white | 6.09 → 6.07 ✅ | 1.40 → 0.62 ⚠️ −56% | 1.72 → 0.36 ❌ −79% |
| structure dark → white | 0.327 → 0.085 ❌ 4x | 0.519 → 0.063 ❌ 8x | 0.819 → 0.078 ❌ 10x |
| chroma dark → white → cool | 0.79 → 0.42 → 0.34 | 0.55 → 0.46 → 0.29 | 0.42 → 0.29 → 0.16 |

Read down the columns and the ordering is not an accident: **the more of an effect's light
is additive emission rather than covered body, the worse every column gets.** VOLUME TRAIL
is a real body and keeps its silhouette exactly; FLAME VOLUME loses over half its body area
on white; PROJECTILE — the hottest fixture in the game, the only one with content above
exposed peak 9 — loses 79% of its body, barely attenuates anything, and has the lowest
chroma of the three. That is §4 restated as a measurement rather than an argument.

**Internal structure collapses on every one of them**, 4x to 10x, including the effect that
otherwise passes — and the three are drawn by three DIFFERENT paths: VOLUME TRAIL through
the trail system's volume tube (`trail_volume.fs`), FLAME VOLUME through the particle
system (`SpawnParticle`/`VC_FlameEmitter`), PROJECTILE as a score over primitives. Same
collapse, same ordering, three unrelated renderers.

That rules out a shader bug and confirms it is the LAW: with `out = C*a + B*(1-a)`, two
pixels of one colour differing only in coverage compose to `(C - B)(a1 - a2)`, so internal
contrast is proportional to the distance between the effect's colour and the background.
There is no shared file to fix — the lever is per-effect authoring, §5.5's emission split,
because emission is the one term that does not scale with `C - B`.

> [!NOTE]
> **The sign flips for dark-bodied effects, so do not generalise this to smoke.** Smoke is
> a body with a DARK `C`, so `|C - B|` is largest against a bright background: smoke reads
> *better* in daylight and loses its structure against black. Fire and energy have the
> problem in daylight; smoke has it at night. Measuring a smoke effect against this table
> produces the mirror image, not a confirmation — §6.1 already says smoke is an
> alpha/premultiplied body and must not be given emission.

**PROJECTILE was deleted on 17/08/2026 on the strength of this row** — it had no gameplay
consumer, only the sandbox fixture, and it scored worst on every axis. `VFX_ComposeVolumeTrail`
survives as a fixture in its own right. Deleting the projectile did not remove the
structure-collapse defect, but not for the reason first written here: the defect is not resident
in `trail_volume.fs`, it is the law above, and it applies to any bright-bodied effect on any
render path.

One consequence for §12.1: PROJECTILE was the only fixture with content above exposed peak 9, so
**nothing in the game currently exercises the candidate curve's ramp-out** (the part that keeps a
hot core white). It stays as insurance and cannot be gate-4-verified until some effect gets that
hot again.

Two caveats on reading this table. `cover%` (the >8/255 footprint, not shown) swings with
the background because it includes the effect's bloom veil, whose spread depends on whether
the background itself is above the bloom threshold — `body%` is the stable silhouette
measure. And these are three fixtures at three frames, not a survey.

## 12. Measured findings that are still open

### 12.1 Hue-preserving tone map — MONOTONE form shipping since 18/08/2026

> [!IMPORTANT]
> **SUPERSEDED 18/08/2026 — the curve is now Candidate H (constant weight, monotone
> whitening), and the "bump over ACES" framing below no longer describes it.** The
> intensity-dependent weight was the cause of the "rainbow rim": turning the correction ON
> across a rising input pulls the non-peak channels down and then releases them, which is a
> colour band with an edge on each side. Isolated on a plain RECTANGLE through the real
> pipeline (`sandbox/gradient_probe.c`), with no effect in the frame at all, and searched
> across the whole weight family — **it is the LOWER bound that causes it, not the upper
> one**, so "bit-identical below peak 1" and "monotone" cannot coexist.
>
> What replaced the old table:
>
> | exposed peak | behaviour now |
> |---|---|
> | any, ACHROMATIC input | bit-identical, exactly, at every level — hue keeping is `(x/peak)·f(peak)`, which for a grey IS the per-channel result. This is what confines the change to saturated content. |
> | `< 1.0`, saturated | moves; worst case **0.206 at peak 0.98** at `u_hueRestore` 1.0, 0.103 at the shipping strength |
> | rising | strictly monotone per channel — the property the trade bought |
> | high | the hue-kept colour desaturates toward white over `smoothstep(5,12)`, so a hot core still reaches white per §5.4 — monotonically |
>
> Measured on ShieldShell across this section's five backgrounds, chroma is UP on **every**
> plate (dark 0.301 → 0.316, mid 0.230 → 0.255, white 0.316 → 0.345, warm 0.351 → 0.410,
> cool 0.138 → 0.212). Whole-scene cost on a real capture: 0.758 % of the frame moving more
> than 2/255, mean 1.03/255, against a 0.043 % A/A floor.
>
> **Gate 0 (`tonemap_shoulder`) was REWRITTEN, not defeated** — it now asserts the
> achromatic row is bit-identical, the saturated shift stays under a stated ceiling, chroma
> still improves in the shoulder, and no channel goes backwards across a dense rising ramp.
> That last check was confirmed RED on the pre-H shader before being kept. Suite 28/28.
> **Gate 5 (Mali cost and `mediump` behaviour of the `x / peak` rescale) is still
> OUTSTANDING**, and now also covers the added `mix`/`smoothstep`.
>
> Everything below is the record of the superseded bounded form. Keep it: the measurements
> are still the reason the feature exists, and the blind A/B that chose the strength still
> applies to `u_hueRestore` itself.

> [!NOTE]
> **DECIDED 17/08/2026: shipping at `postfx_hue_restore = 0.6`.** Chosen by the owner from a
> blind A/B over 0 / 0.35 / 0.6 / 1.0 (`scripts/set_tonemap.sh blind`), on FLAME VOLUME and
> VOLUME TRAIL — fixtures untouched by that session's effect fixes, so the curve was judged on
> its own. 1.0 was available and not chosen: it pulls a hot core's non-peak channels down hard
> (measured `0.777,0.890,0.937 -> 0.332,0.620,0.937`), which costs the "hot" read. The default
> in `core/post_fx.c` is now `0.6f`. **Gate 5 (Mali cost and `mediump` behaviour of the
> `x / peak` rescale) is still OUTSTANDING** — this ships to desktop verified and to Android
> unverified.

`postfx_hue_restore` (tuning.cfg, **shipping default 0.6**; 0 restores the old curve) selects the
curve in `core/shaders/post_process.fs`. It is a **bump over** the ACES fit, not a replacement, which is
what keeps a tone-mapper change from being a whole-scene change:

| exposed peak | behaviour |
|---|---|
| `< 1.0` | identity — bit-identical to the curve every approved material was authored against |
| `1.0 – 5.0` | hue restoration ramps in (tone map the peak, carry the channel ratios) |
| `> 9.0` | identity again, so a genuinely hot core still reaches white per §5.4 |

**Gate 0 — bounded (rlvk `tonemap_shoulder`).** Measured through the shipping shader:
`d = 0.00000` at peaks 0.2/0.5/0.9 and at 10/14; the shoulder band changes and is required to.
The dither is a deterministic hash of `gl_FragCoord`, so it cancels between runs and
"bit-identical" stays a testable claim rather than an approximate one.

**The chart now runs at the shipping 0.6 by default**, not at 0 — an acceptance oracle that
measures a configuration the game does not use is exactly the drift it exists to prevent.
`BRIGHT_HUEFIX=<0..1>` overrides it, which is how the strength was chosen; keep the default in
sync with `core/post_fx.c`'s `s_hueRestore`.

**Gate 1 — no regression, large gain (`BRIGHT_HUEFIX=<0..1> bright_vfx`).** The full chart passes
at strengths 0.35, 0.6 and 1.0 with every metric still enforced. Chart-wide:

| strength | mean chroma gain | worst `rgbDistance` change, anywhere |
|---|---|---|
| 0.35 | **+0.074** | −0.038 |
| 0.60 | **+0.127** | −0.021 |
| 1.00 | **+0.211** | −0.035 |

At the worst cell — EV2 on bright neutral — chroma goes from `0.19–0.24` to `0.40–0.45`
(strength 0.35) or `0.70–0.79` (strength 1.0). No cell fell below any gate at any strength.

**Two things the chart cannot tell you, and they decide this.**

1. **Strength 1.0 is almost certainly too strong.** Restoration pulls the non-peak channels
   *down*: a bright core measured `(0.777, 0.890, 0.937) → (0.332, 0.620, 0.937)`. More saturated,
   but markedly darker and less "hot". Expect the shipping value to land nearer `0.35–0.6`.
2. **It breaks per-channel monotonicity, and §5.7 depends on that.** Under the candidate an
   *additive* effect bright enough to enter the shoulder can end up **below** the background in a
   channel (measured: control R `0.637 → 0.572` against a `0.613` background). "It darkens,
   therefore it has coverage" stops being sound — if this ships, §5.7's invariant has to be
   re-derived in scene-linear space. `bright_vfx` scopes that assertion to the shipping curve
   rather than silently asserting something false.

**Gate 2 — PASS, and it is the decisive one.** `scripts/run_tonemap_ab.sh --vfx 0 <strength>`,
A/A noise floor **0.000% / max 0** (byte-identical), then:

| strength | pixels changed >2/255 | >32/255 | max delta |
|---|---|---|---|
| 0.35 | 0.494% | 0.009% | 35/255 |
| 0.60 | 0.495% | 0.262% | 61/255 |
| 1.00 | 0.495% | 0.379% | 103/255 |

**Under half a percent of the frame moves at any strength**, and the ×8 difference map shows why:
the change is confined to the emissive beam itself. Ground, sky, the portal ellipse, stars and the
fog gradient are pitch black in the diff — not "small changes", *zero* changes. Visually the beam
goes from washed cream to saturated orange with nothing else touched. This is the evidence that
converts "changing the tone mapper" from a whole-scene re-approval into an emissive-only one.

Note the strength column carefully: the changed pixel COUNT is identical at every strength (it is
determined by which pixels are above the shoulder, not by how hard they move), while the magnitude
scales. That is the bump behaving exactly as designed.

**A capture-path limitation, found by the A/A floor and worth fixing separately.** The whole-scene
path (`WUXING_VERIFY=<skill>`) is **not** reproducible: two identical runs differ on 0.05–0.17% of
pixels, and `>2/255`, `>8/255` and `>32/255` come back at the *same* percentage, i.e. the differing
pixels are entirely different rather than slightly shifted — a sparse-particle or frame-phase leak,
not a global drift. The A/A floor exceeded the A/B signal there, so that path cannot carry a pixel
A/B until it is fixed. `--render-vfx` is byte-identical and carries gate 2 today, at the cost of
covering a VFX fixture rather than a full gameplay frame; the "does skin/ground/sky move" half of
the question is answered by gate 0's identity proof plus this diff map, not by that path.

**To re-run gate 2** (agents do not touch `build/`):

```bash
scripts/run_tonemap_ab.sh --vfx 0 0.6          # deterministic, carries the gate today
scripts/run_tonemap_ab.sh --verify FIRE 0.6    # whole scene — blocked on the floor above
```

It captures the same scene twice, runs **A/A first as a noise floor** — an A/B number without one
is uninterpretable, and here that check is what caught the `--verify` path — then reports what
fraction of the frame moved plus a ×8 difference map. Since gate 0 proves identity below the
shoulder, *every changed pixel is a pixel that was already above it*, so "changed %" **is** the
approval surface.

**Gate 3 — PASS on the current art direction, with one standing condition.**

Answered with a *single capture* instead of a diff, so the `--verify` path's nondeterminism does
not matter: `postfx_hue_restore = -1` switches `post_process.fs` into a **shoulder view** that
paints exposed peak `[1,9)` magenta (the candidate's entire active band), `>= 9` cyan, and
everything else grey. Whatever is not magenta is provably untouched.

| capture | in band | above band |
|---|---|---|
| whole scene, `WUXING_VERIFY=FIRE`, 5 timed shots | **0.0000%** (one single pixel at one shot) | 0.0000% |
| whole scene, `WUXING_VERIFY=TUBE`, 5 timed shots | **0.0000%** (one single pixel at one shot) | 0.0000% |
| VFX fixtures `--render-vfx 0..5` | 0.4964% / 0 / 0.0541% / 0 / 0 / 0.0574% | 0.0000% |

**No character material, ground, sky, fog or star reaches exposed peak 1.0.** The material
regression list this gate was supposed to produce is therefore empty: on the night-arena art
direction the candidate is the identity on every non-emissive surface in the frame, not
approximately but exactly.

The two instruments cross-validate: the shoulder view counts **0.4964%** of fixture 0 in the band
from one capture, and the gate-2 A/B counted **0.495%** of the same fixture changed from two. Two
independent methods agreeing to three decimals is the reason to believe either.

**Standing condition — re-run gate 3 whenever the scene gets brighter.** This result is a property
of the current content, not of the curve: every shipping map is night-time and exposure is fixed at
`1.00` (`main.c`). Raise exposure, add a daylight arena, or land the §7.5 auto-exposure and ground
and sky *will* cross 1.0 — which grows the approval surface into exactly the materials this gate
just cleared. The whole point of this document is that scenes are going to get brighter, so treat
this as a gate that expires.

**Two limits worth stating.** Only `FIRE` and `TUBE` are compiled into the current binary
(`core/skills_config.h` has the other six at `0`), so the whole-scene sample is two skills; and
nothing anywhere reached the `>= 9` band, so the candidate's ramp-*out* — the part that keeps a hot
core white — is currently insurance rather than an exercised path.

The shoulder view is now its own knob, `postfx_shoulder_view` — **kept rather than deleted**,
because this gate expires: it passed only because every map is night-time and exposure is pinned
at 1.00, and one capture with the overlay is how you find out that a new daylight map has regrown
the approval surface.

**Gate 4 — the human call, and it is deliberately a NARROW one.**

Gates 0–3 already settled everything that can be settled by measurement: below the shoulder the
output is bit-identical, under 0.5% of a frame moves, and 0.0000% of non-emissive content is even
eligible to change. So gate 4 is **not** "re-approve the game's look". Ground, characters, sky,
fog, UI and every material are out of scope — they provably do not move. The only open questions
are (a) which strength, and (b) does anything about the *emissive* look get worse.

Judge it **live and in motion**, never from stills. `core/tuning.c` watches `tuning.cfg`'s mtime
and `main.c:1075` calls `Tuning_Update()` every frame, so the knob takes effect on the next frame
with no restart and no rebuild:

```bash
scripts/set_tonemap.sh 0.6        # or 0 / 0.35 / 1.0
scripts/set_tonemap.sh blind      # applies one of 0/0.35/0.6/1.0 without telling you
scripts/set_tonemap.sh reveal
scripts/set_tonemap.sh off        # ALWAYS, when finished - see the landmine below
```

Use `blind` for the actual decision. A visible A/B cannot be judged honestly once you know which
side is the new one; "the new one looks better" is the result that protocol always produces.

**Which VFX to point it at.** Measured, not guessed: the shoulder view was swept over all 46 NEWFX
fixtures and half of them have *no pixels at all* in the candidate's band, so testing on those
shows literally nothing. Sustained in-band area at the default 90-frame warmup:

Cite fixtures by NAME — indices are positional and shift when the manifest is pruned, which it
was on 17/08/2026 (`scripts/vfx_fixture_index.py --list`, or pass the name straight to
`render_vfx_matrix.sh`).

| fixture | in band | note |
|---|---:|---|
| VOLUME TRAIL | 2.54% | largest — use it to see *whether* anything changed at all |
| FLAME VOLUME | 0.76% | warm core: the "does it still read HOT" risk lives here |
| SHIELD SHELL | 0.66% | the project's own reference fixture — but see the warning below |
| LIGHT SHAFT | 0.61% | |
| ENERGY ORB | 0.55% | saturated magic hue |
| BEAM | 0.50% | |
| PROJECTILE | 0.47% | **the only fixture with anything above peak 9** (0.073%), so the only one that exercises the ramp-*out* that keeps a hot core white |
| BLACK HOLE | 0.03% | **not** a useful probe — see the correction below |
| PROJECTILE | *deleted 17/08/2026* | was 0.47%, and the only fixture above peak 9 |

> [!WARNING]
> **This table was wrong once, and the way it was wrong is the lesson.** Its first
> version ranked `BLACK HOLE` first at **7.35%**, three times the next effect. That
> number was the DEFAULT shader: `black_hole_swirl.fs`, `ground_aura.fs` and
> `plasma_shell.fs` all redefined `vnoise3`/`fbm3` that their own `#include` of
> `noise.glsl` already exported, GLSL rejected the duplicate body, and raylib answered
> the failed compile with the default shader and a valid non-zero id. Re-measured after
> the fix, `BLACK HOLE` is **0.03%** — bottom of the table, not top — and `ENERGY ORB`
> moved 0.67% → 0.55%. A measurement taken through the game measures whatever the game
> actually drew, which is not always the thing you named. `scripts/validate_shader_includes.py`
> now fails the build on this collision class (wired into `CMakeLists.txt`).

**That table is a LOWER BOUND — it samples one frame.** Transient effects peak early and the
90-frame capture misses them entirely: `ENERGY BURST` reads 0.00% at warmup 90 and **2.56% at
warmup 40**; `SHOCK RING` 0.00% at 90 and 0.49% at 40; `LIGHTNING ARC` only 0.07%, only at warmup
20. A "0%" in a single-frame sweep means "not in the band *at that frame*", nothing more — which is
also why gate 4 is judged live, where the whole lifetime is visible.

Worth a separate look: `LIGHTNING IMPACT` stayed at 0.00% at all four sampled warmups. If that
holds under a proper time sweep it means the game's most ionised effect never crosses exposed peak
1.0 — and therefore never crosses the 1.25 bloom threshold either, which would be a finding about
the effect's authoring rather than about this curve.

**What to look at** (all of it emissive, all of it in motion at gameplay camera distance):

- does the hot core still read as HOT? This is the known risk — restoration pulls the non-peak
  channels down, and at strength 1.0 a measured core went `(0.777, 0.890, 0.937)` to
  `(0.332, 0.620, 0.937)`: more saturated, markedly darker;
- is the hue the right ELEMENT hue, or has it over-shot into a different one;
- the §8.3 eyeball list that pixels cannot prove: hue reversal, sort popping, bloom crawl on a
  thin moving bolt, a white bloom tube swallowing a beam;
- both a dark arena and the brightest scenery available — the candidate acts on anything above
  exposed peak 1.0, which in a night arena is the VFX and nothing else.

**Landmine:** `tuning.cfg` persists across sessions, so a strength left in it silently becomes the
baseline for every later visual judgement (`ENGINE_LANDMINES.md` #13). Run
`scripts/set_tonemap.sh off` when finished.

**Recording the outcome:** the chosen strength goes here with its reason, and `s_hueRestore`'s
default in `core/post_fx.c` changes from `0.0f` to it — that is a Core edit and a rebuild, and it
is the point at which the shoulder-view diagnostic in `post_process.fs` should be deleted.

Gate 5 (Mali cost + `mediump` behaviour of the `x / peak` rescale) remains.

---

1. **Per-channel ACES is the binding constraint at high exposure, and a hue-preserving tone map
   is the real fix.** At EV2 over a 1.0 white background the tone-mapped background sits at
   `0.915`, so the entire readable range for any effect is what fits below it. Every hue in the
   chart had to be authored with its darkest channel at or under ~0.05 to clear
   `rgbDistance 0.10` there. §7.4 currently rules a tone-mapper change out of scope for phases
   0–4; that is the right call for *sequencing* and the wrong one for *ambition*. Tone mapping on
   max-channel/luminance and rescaling RGB — blending toward the per-channel result only in the
   top stop — is what buys back hue in the highlights. It needs its own whole-scene approval.
2. **§6.1/§6.2's coverage ranges are below what §8.2 requires.** The reference fixture needs
   `coverage ≈ 0.68` to pass on bright ground; §6.2 recommends `0.20..0.55` for magic. On bright
   backgrounds contrast comes from coverage, not from emission. The ranges need re-deriving
   against the chart rather than being quoted as-is.
3. **The RGBA8 fallback has no oracle.** §7.1 keeps the fallback and states that coverage remains
   the mechanism there, but nothing measures it. On that path `emissionGain > 1` clips *before*
   bloom, so the core flattens to white and loses hue — the worst case of the failure this
   document targets, on the path that ships to mobile.
4. **No temporal metric.** §8.3 lists bloom crawl and exposure pumping as eyeball checks only. A
   two-frame sub-pixel-shift capture on a thin bolt, asserting a bound on the per-pixel delta in
   the halo, would make the most visible AAA tell measurable.

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
| 2026-08-17 | Claude (Renderer) | Rebuilt `bright_vfx` into the real §8 chart: shipping ACES + shipping bloom shaders instead of a Reinhard probe and a re-implementation, §5.4 spatial fixture instead of a flat quad, all 8 §8.2 metrics, 6 hues x 5 backgrounds x 3 exposures x bloom off/on, both draw sites | `third_party/vulkan/tests/rlvk_visual_test.c`; `core/shaders/{post_process,bloom_bright,bloom_downsample,bloom_upsample}.fs` | Ground-truth: PASS, suite 25/25 normal and with validation |
| 2026-08-17 | Claude (Renderer) | Added §5.5 (corona radiance), §5.6 (halo laws), §5.7 (darkening budget), §7.2 (bloom threshold vs background), §7.3b (output dither), §8.1 six-hue note, §8.2 recorded metric changes, §9b, §12 | measurements from the rebuilt `bright_vfx` chart | Ground-truth: every number quoted was read back from the chart |
| 2026-08-17 | Claude (Renderer) | Fixed the flush-scoped blend toggle in the game's postFX composite (VFX regions were multiplied by the scene's accumulated alpha and clipped to white) and added the `colorblend_flush` scenario | `core/post_fx.c`; `third_party/vulkan/tests/rlvk_visual_test.c` | Ground-truth: `colorblend_flush` pins both halves; promoted to `ENGINE_LANDMINES.md` #16 |
| 2026-08-17 | Claude (Renderer) | Output dither in `post_process.fs` (sin-free hash) and NaN/Inf containment in the shared resolver | `core/shaders/post_process.fs`; `core/shaders/common/vfx_composite.glsl` | Core suites 68/73, unchanged baseline failures |
| 2026-08-17 | Claude (Renderer) | Halo re-derived as an annulus (§5.6) after the measurement floor showed the old profile passing the ordering test by not existing; dither clamped after the fact | `third_party/vulkan/tests/rlvk_visual_test.c`; `core/shaders/post_process.fs` | Ground-truth: dark tile `dH 0.47`, white tile `0.017` |
| 2026-08-17 | Claude (Renderer) | §11b measured FLAME VOLUME (38) and PROJECTILE (20); wired the harness into the working protocol of root/core/skills/rlvk `CLAUDE.md` | matrix runs; `CLAUDE.md`, `core/CLAUDE.md`, `skills/CLAUDE.md`, `third_party/vulkan/CLAUDE.md` | Ground-truth: structure collapses 4x/8x/10x; PROJECTILE darkens only 28.7% |
| 2026-08-17 | Claude (Renderer) | §11b — `render_vfx_matrix.sh` + `analyze_vfx_matrix.py`: measure a shipping effect across the §8.1 backgrounds at identical framing, plate-referenced; first result is VOLUME TRAIL | `scripts/render_vfx_matrix.sh`, `scripts/analyze_vfx_matrix.py`; `WUXING_VFX_BG` (main.c:1174) | Ground-truth: structure 0.327 -> 0.085 dark to white, std 40.9 -> 16.5 |
| 2026-08-17 | Claude (Renderer) | Fixed duplicate `vnoise3`/`fbm3` in three shaders that were silently compiling to the DEFAULT shader; added a configure-time guard; re-measured the gate-4 table | `core/shaders/{black_hole_swirl,ground_aura,plasma_shell}.fs`; `scripts/validate_shader_includes.py`; `CMakeLists.txt` | Ground-truth: BLACK HOLE 7.35% -> 0.03% after the fix |
| 2026-08-17 | Claude (Renderer) | Surface/resolver contract validator + CMake gate; fixed ENERGY ORB and BLACK HOLE; corrected §6.1/§6.2 coverage ranges to the measured requirement; added the RGBA8 fallback oracle `bright_vfx_ldr` | `scripts/validate_vfx_surface_contract.py`, `CMakeLists.txt`, `core/composition/{common/vc_energy_orb,taiji/vc_black_hole}.inl`, `tests/rlvk_visual_test.c` | Ground-truth: suite 27/27 normal + validation; LDR chart passes on coverage alone; bloom inert below EV1.25 there |
| 2026-08-17 | Claude (Renderer) | §12.1 gate 4 test set — shoulder-view sweep over all 46 NEWFX fixtures + a warmup sweep showing the single-frame ranking is a lower bound | `--render-vfx` captures, `sandbox/vfx_test.c` name table | Ground-truth: 23/46 read zero at warmup 90, but ENERGY BURST is 2.56% at warmup 40 |
| 2026-08-17 | Claude (Renderer) | §12.1 gate 4 procedure — live hot-reload toggle with a blind mode, and the scope narrowed to emissive-only by gates 0-3 | `scripts/set_tonemap.sh`; `core/tuning.c`, `main.c:1075` (hot-reload verified) | Procedure; the call itself is the owner's |
| 2026-08-17 | Claude (Renderer) | §12.1 gate 3 — shoulder-view diagnostic; whole-scene captures show 0.0000% of non-emissive content in the candidate's band, cross-validated against the gate-2 diff | `core/shaders/post_process.fs`; captures via `WUXING_VERIFY` and `--render-vfx` | Ground-truth: two instruments agree to 0.4964% vs 0.495% on the same fixture |
| 2026-08-17 | Claude (Renderer) | §12.1 — hue-preserving candidate behind `postfx_hue_restore` (default 0), gates 0 and 1 executed and PASS, gate 2 tooling written for the human | `core/shaders/post_process.fs`; `core/post_fx.c`; `scripts/run_tonemap_ab.sh`; `scripts/diff_captures.py`; `core/tests/color_grade_lut_test.c` | Ground-truth: `tonemap_shoulder` d=0.00000 outside the shoulder; chart passes at 3 strengths |
