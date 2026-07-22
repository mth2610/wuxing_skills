# Đợt E — Elden-Ring-tier VFX: implementation spec

Owner: **Core Agent**. Companion to `VFX_ARCHITECTURE.md` (which describes the VFX
system as it is); this doc describes **what to build next and exactly how**.
Status/progress for these items lives in `PROGRESS.md`, not here.

Each task below is written to be executed **independently by one agent in one
session**, with no prior exploration: it names the files, the exact API to add,
the wiring points, the traps, and a checkable Definition of Done. Read only the
task you are assigned plus §0.

---

## 0. Read this before any task

### 0.1 Premise (audit, 22/07/2026 — ground truth)

The renderer does **not** need a rewrite. What already exists and works:

| Capability | Where |
|---|---|
| HDR offscreen + ACES tonemap + dual-filter bloom | `core/post_fx.c` (`DUAL_FILTER_LEVELS` chain) |
| Chromatic aberration, vignette, colour grade, split-tone | `core/shaders/post_process.fs` |
| Heat-haze / refraction | `ScreenDistort_Add` (`core/screen_distort.h`) |
| Depth-fade soft particles | `core/shaders/common/soft_particle.glsl` + `ScreenDistort_BindDepthForSoftParticles` |
| Over-lifetime curves, sub-emitters, velocity stretch, collision, per-particle ribbon trails | `core/particle_system.h:30-85` |
| Flipbook atlas playback on particles | `core/sprite_anim.h` (`SpriteAnim_CalculateUV`) |
| Dynamic VFX point lights (spawn/update/query) | `core/vfx_light.h` |
| Mesh dissolve, fresnel rim, triplanar, crystal/plasma materials | `core/shaders/` |
| Hitstop + camera trauma | `TimeFX_Hitstop`, `CameraFX_Shake` |
| 79 `VFX_Compose*` components | `core/composition/visual_composer.h` |

### 0.1b Why smoke / fire / character aura don't look good — root cause

This is the **most important finding in this document**, and it outranks
everything in §0.1's gap list. Diagnosis from reading
`energy_smoke.fs`, `smoke_column.fs`, `ground_aura.fs`, `aura_shell.fs`,
`fire_funnel.fs`, `particles.fs`, and the blend-mode usage across
`core/composition/`.

**Every one of those effects is built the same way: ONE surface (a quad or a
shell mesh) with FBM noise evaluated inside it.** That architecture has a
ceiling, and all three fundamentals are sitting on it. Four concrete causes:

1. **Particles are completely unlit.** `core/shaders/particles.fs` is 14 lines:
   `texture0 * fragColor`, then an alpha discard. There is no light term at all.
   Real-time smoke reads as *volume* almost entirely because of lighting — a
   bright rim facing the light and a dark occluded core. Flat-shaded smoke can
   only ever look like a decal of smoke. **This is the single highest-value fix
   in the whole project.**
2. **Additive dominates**: 42 `BLEND_ADDITIVE` versus 23 `BLEND_ALPHA` across the
   composition layer. Additive output can never be *darker* than its background,
   so anything drawn additively reads as glowing gas, never as smoke, dust or
   ash. Smoke needs alpha blending over a dark base; additive belongs to embers,
   glow and sparks only.
3. **Noise is being used as SHAPE instead of as DETAIL.** FBM is a uniform
   statistical field — it has texture but no *silhouette*. Real smoke's beauty
   lives in its outline: rounded, cauliflower-like billow lobes. Thresholding
   FBM produces a fuzzy blob with a statistically-even edge, which is why the
   results read as "a noise texture" rather than "smoke". Authored flipbooks
   (E4) exist precisely to supply the silhouette that noise cannot.
4. **One surface = no parallax, no depth.** Real smoke and fire are dozens of
   overlapping sprites at different depths, scales, rotations, and lifetimes,
   which is what produces internal parallax as the camera moves. A single quad
   is flat no matter how good the shader inside it is.

Per-element on top of the shared causes:

- **Fire** (`fire_funnel.fs:69-71`): the colour ramp is three `smoothstep` mixes
  `red → orange → white`. It is not a black-body curve, and there is no
  transition to smoke at the tip — real flame darkens and turns to smoke where
  it cools, which is most of what makes fire read as fire.
- **Character aura** (`aura_shell.fs`): fresnel shell + scrolled FBM is the
  textbook "plastic bubble" look. An aura only reads well when it (a) flows
  directionally along the body rather than uniformly, (b) has **discrete**
  elements — motes, wisps, ribbons — breaking the shell's silhouette, and
  (c) actually lights the character (which is E2, currently absent).

**Consequence for the plan:** post-processing polish (E1) applied on top of weak
fundamentals just produces shiny weak fundamentals. The **F-series (Part A)
therefore runs first**, and the E-series becomes polish on top of it.

### 0.1c The gap list

The gap versus the FromSoftware bar is in **three** further places, which this
spec also closes:

1. **Authored textures.** `assets/textures/` holds 68 files and exactly **one**
   flipbook (`smoke_flipbook.png`). Elden Ring VFX is ~70% hand-authored sheets.
   This is why current effects read as "shader-clean" rather than "painted".
   → **E4**
2. **No choreography layer.** Every skill hand-codes its own phase timing, so
   nothing enforces the `anticipation → burst → sustain → dissipate` envelope
   that makes ER effects readable at a glance. → **E3**
3. **Three missing render features**: screen-space radial blur, anamorphic
   streak bloom, and emissive light bleed onto characters (verified absent:
   `surface_lit.fs` consumes only `u_sunToLight` + `u_rimColor` from
   `environment_system` — it never reads the `vfx_light` list). → **E1, E2**

### 0.2 Execution order

```
Part A (fundamentals — do first)
  F0 (triage/purge) → F1 (lit particles + blend law) → F2 (smoke) → F3 (fire) → F4 (aura)
                            └─────────────────┬──────────────────────────────┘
Part B (polish — on top of A)                 │
  E0 (baseline) ─→ E1 → E2 → E3 → E5 → E6 → E7 → E8
                            └→ E4 (textures, asset-gated, parallel from F1; feeds F2/F3/E5/E6)
```

**F0 first** because there is no point porting, relighting or documenting effects
that are about to be deleted — and per the owner's direction the skill layer can
be discarded wholesale, which makes the purge cheap and the survivor set small.
**F1 next** because unlit particles are the ceiling under smoke, fire, dust, ash
and aura simultaneously; fixing it lifts every one of them at once, and F2–F4
are essentially impossible before it.

E0 is captured **after F0** (there is no value in baselining effects that get
deleted) but **before F1**, so the fundamentals rebuild has a yardstick.

Within Part B: E1 is cheap and lifts everything, E3 comes before E5/E6 so new
compositions are authored against the sequencer rather than retrofitted into it.
E4 is the long pole and the only item the engine agents cannot finish alone;
where assets stall, take the shader route (precedent: `VFX_ComposeEnergySmoke`,
a shader-only erosion puff built when asset sourcing stalled). Note E4 now feeds
F2/F3 as well, which pulls it earlier than originally planned.

### 0.3 Rules that bind every task here

- **C99, static pools, no `malloc`/`calloc`/`realloc`/`free`.** Fixed-size
  arrays with a `MAX_*` define; on overflow, recycle the oldest or drop.
- **Meter scale** (1 unit = 1 m). Radii 0.10–0.20, forces 3.0–7.0 (real gravity
  is 9.81), speeds 1.0–3.0. Never write 1cm-scale (×100) numbers.
- **Backend-agnostic draws** — `rlgl`/raylib only, never raw GL or Vulkan calls.
- **Composition layer rule** — colours/gradients/force fields come from
  `VFX_Material(VC_MAT_*)` (`core/presets/vc_material.h`, 12 ids:
  `FIRE, ICE, WATER, LIGHTNING, EARTH, WOOD, METAL, TAIJI, HOLY, VOID, POISON, QI`);
  motion math comes from `core/composition/common/vc_motion.h`
  (`VC_MotionOrbit`, `VC_MotionHelix`, `VC_MotionSpiralIn`, `VC_MotionJitter`,
  `VC_MotionBob`, `VC_DirCone`, `VC_Pulse01`, `VC_Breathe`, `VC_Flicker01`,
  `VC_RingPointXZ`, `VC_TangentXZ`). Hard-coded colours only for a deliberate
  identity break, **with a comment saying so**. New reusable motion formulas go
  into `vc_motion.h`, not inline.
- **Shaders**: load via `ResourceManager_LoadShader()`; never call
  `UnloadShader`/`UnloadTexture` from effect code.
- **Read `ENGINE_LANDMINES.md` before touching any shared shader or any depth
  state.** Especially §1, the Raylib batching hazard: `rlDrawRenderBatchActive()`
  before **and** after every depth-mask/depth-test change.
- **Every new VFX must be wired into `sandbox/vfx_test.c`'s NEW FX tab** — and
  the manifest entry at `scripts/vfx_test_manifest.json` is added **by hand
  first**, then `python3 scripts/sync_vfx_test.py` regenerates the rest. The
  script does not invent entries.
- **A composition that returns a handle needs its own Update/Draw pool wired in**
  (`VFX_Compose_Update` / `VFX_Compose_Draw3D` in
  `core/composition/visual_composer.c:48-49`, called from `main.c:1709` and
  `main.c:1805`) — a manifest `type` of `continuous` is not sufficient.

### 0.4 Manifest entry format (needed by E3, E5, E6)

`scripts/vfx_test_manifest.json` → `entries[]`:

```json
{
  "fn": "VFX_ComposeGlintSparkle",
  "label": "GLINT SPARKLE",
  "category": "metal",
  "type": "continuous",
  "draw_call": "VFX_ComposeGlintSparkle($POS, VC_MAT_METAL, 1.0f, $TIME)"
}
```

Placeholders: `$POS` = spawn origin, `$TIME` = running time, `$PROG` =
`0..1` progress, `$SEED` = position-derived int seed. `category` ∈
`fire, water, wood, metal, earth, taiji, util`. `type` ∈ `continuous`
(re-called every frame) or one-shot. Complex signatures go in `overrides`.

---

# PART A — Purge + fundamentals

Do this part first. See §0.1b for why.

---

## F0. Triage & purge

**Owner:** Core Agent (compositions) + Skills Agent (skills) · **Size:** M
· **Depends on:** nothing · **Runs before everything, including E0.**

79 compositions and 10 skills exist; by the owner's own assessment most are not
good enough, only a few are keepers, and **the skill layer may be deleted
wholesale and rebuilt**. Carrying dead weight through F1–E8 multiplies every
later task's cost, so the purge happens first.

**Files:** `core/composition/**`, `core/composition/visual_composer.h`,
`scripts/vfx_test_manifest.json`, `skills/**`

### The deletion boundary — LOCKED by the owner, 22/07/2026

The purge covers **exactly two tiers**. This is not negotiable by the agent
running F0, and it is not a judgement call:

| Tier | What | Size | Action |
|---|---|---|---|
| **1. Primitives / infrastructure** — `particle_system`, `ribbon_strip`, `trail_system`, `force_field`, `decal_system`, `vfx_light`, `screen_distort`, `post_fx`, `resource_manager`, `sprite_anim`, `color_gradient`, `skill_curve`, `flow_map`, `emitter_system`, `metaball_fx`, `vfx_proc_ray`, `surface_material`, and all of `core/shaders/` | ~8,250 lines | **DO NOT DELETE. Extend only.** |
| **2. Composition layer** — `core/composition/**` | ~8,615 lines | Purge per the rubric below |
| **3. Skills** — `skills/**` | ~2,674 lines | Delete wholesale, rebuild later |

**Why tier 1 is off-limits:** none of §0.1b's four root causes implicate it. All
four describe how the *composition layer uses* the primitives — additive where
alpha belongs, noise as silhouette, one surface instead of many. `particle_system.c`
is not defective; it is missing a light term, and F1 **adds** that behind an
opt-in `lightingStrength` defaulting to 0.0. That is an extension, not a rewrite.

Tier 1 also carries a large amount of hard-won platform knowledge in lines that
look inconsequential and each cost a debugging session to find — `BLANK` rather
than `BLACK` in `GenImageGradientRadial` (Mali draws square edges around every
particle otherwise), the `rlDrawRenderBatchActive()` flush pairs around depth
changes, the bounded-seed `fract()` wrap in `smoke_column.fs`, the `rlFrustum`
near-plane floor, the MoltenVK UBO layout constraint. Rewriting tier 1 means
re-earning every one of them. See `ENGINE_LANDMINES.md`.

**Within tier 2, prefer rewrite over repair.** A single-quad-plus-FBM effect does
not "upgrade" into a layered lit one — that is a rewrite, so delete the file and
author fresh against the F1 foundation. This is why the FIX bucket below is
deliberately narrow.

### The rubric — classify every composition into exactly one bucket

Judge each in the NEW FX tab, one at a time, against these questions:

| Test | Ask |
|---|---|
| **Silhouette** | Cover the colour — is the shape still readable and interesting? Or is it a fuzzy blob? |
| **Depth** | Does it have overlapping layers/parallax, or is it one flat surface? |
| **Motion** | Does it have a clear phase envelope, or does it just scroll/loop uniformly? |
| **Identity** | Could you tell which element it is with the colour removed? |
| **Reuse** | Is it a *primitive* other skills will compose with, or a one-off? |

Buckets:
- **KEEP** — passes silhouette + reuse. Survives untouched.
- **FIX** — good idea, ceilinged by §0.1b's causes (unlit / additive / single
  surface). Will be rebuilt on the F1 foundation. **Only list it here if you can
  name which of the four causes it hits** — otherwise it is DELETE.
- **DELETE** — one-off, redundant with a KEEP, or nothing to salvage. Includes
  the `*Test` scaffolding (`VFX_ComposeParticleUpgradesTest`,
  `VFX_ComposeTrailUpgradesTest`, `VFX_ComposeSpiritWispTest`,
  `VFX_ComposeMeteorCometTest`) unless it is still actively used as a harness.

**Bias the call toward DELETE.** A composition that has to be argued for is a
DELETE. The purpose here is a small, strong primitive set, not preservation.

### Steps — **the owner performs the deletion themselves (decided 22/07/2026)**

The agent does **not** delete anything in F0. It produces the recommendation and
then repairs whatever the owner's deletion left dangling.

1. **Produce the triage table** (fn → bucket → one-line reason) in
   `core/docs/PROGRESS.md`, covering tier 2 and tier 3. That is the whole of the
   agent's F0 output; hand it over and stop.
2. The owner deletes.
3. **Afterwards**, the agent runs the cleanup pass below — a hand deletion
   reliably leaves these dangling, and the build will either break or silently
   keep dead entries.

### Post-deletion cleanup checklist

For every composition removed, all four must go together:

- [ ] the `.inl` body under `core/composition/**`
- [ ] the declaration in `core/composition/visual_composer.h`
- [ ] the `#include` in `core/composition/visual_composer.c` (if it had its own `.inl`)
- [ ] the `entries[]` item in `scripts/vfx_test_manifest.json` — **and any
      `overrides` key with the same fn name** — then re-run
      `python3 scripts/sync_vfx_test.py`

For every skill removed:

- [ ] the directory under `skills/`
- [ ] re-run `python3 scripts/generate_registry.py` (regenerates `skills_generated.h`)
- [ ] its tunables (`*_tunables.inl`) and any `RegisterSkillTunables` call
- [ ] **the default loadout** — the autotest expects a working one; if a deleted
      skill was in it, fix it in the same commit or the suite fails
- [ ] grep the skill name across `game/`, `boss/`, `ai/`, `ui/`, `formations/`,
      `maps/` — those modules trigger skills and compositions by name, and a
      dangling reference there breaks the match flow, not the build

Then:

- [ ] `bash scripts/gen_core_api_index.sh > core/docs/API.md`
- [ ] clean build + autotest green
- [ ] no commented-out corpses left behind

**DoD:** triage table committed and handed to the owner; after their deletion,
survivor set builds and runs; NEW FX tab shows only survivors; API index
regenerated; no dead declarations or orphaned manifest entries.

**Landmines**
- Deleting a skill that `game/`, `boss/`, `ai/` or a loadout references breaks
  the match flow — grep for the skill name across those modules before removing.
- `maps/` and `formations/` also trigger compositions; grep before deleting a
  composition, not just within `core/`.
- The autotest expects a working default loadout; if you delete a skill that is
  in it, fix the loadout in the same commit.

---

## F1. Lit particles + blend discipline — **the keystone**

**Owner:** Core Agent · **Size:** L · **Depends on:** F0, E0
· **This is the highest-value task in the entire document.**

Per §0.1b: `core/shaders/particles.fs` is 14 lines with no lighting whatsoever,
and additive outnumbers alpha 42:23. Smoke, dust, ash, cloud and soft aura can
**never** look good until this changes. F2, F3 and F4 all depend on it.

**Files:** `core/shaders/particles.fs`, `core/shaders/particles.vs`,
`core/particle_system.c`, `core/vfx_config.h` (`VFX_RenderConfig`),
`core/particle_system.h`

### F1a — Spherical-normal billboard lighting

The standard cheap trick, and the one that transforms flat sprites into volume:
derive a fake normal from the billboard's own UV so each particle shades like a
sphere.

```glsl
// particles.fs
vec2 d = fragTexCoord * 2.0 - 1.0;
float r2 = dot(d, d);
if (r2 > 1.0) { /* outside the disc: keep flat/edge behavior */ }
vec3 nView = vec3(d, sqrt(max(0.0, 1.0 - r2)));   // view-space hemisphere normal
vec3 nWorld = normalize(mat3(u_invView) * nView);
```

Then light it with a **half-Lambert wrap** (matching `surface_lit.fs`'s
convention so particles and characters agree):

```glsl
float ndl = dot(nWorld, u_sunToLight);
float wrap = pow(ndl * 0.5 + 0.5, 1.5);            // soft, no hard terminator
vec3  lit  = u_ambient + u_sunColor * wrap;
```

Plus the same VFX point lights E2 adds to characters — smoke lit by the fireball
inside it is exactly the ER look. Share the uniform block layout with E2 so both
shaders upload from one place.

Expose a per-config strength so existing effects can opt in gradually:

```c
// VFX_RenderConfig (core/vfx_config.h)
float lightingStrength;   // 0.0 = today's flat unlit behavior (DEFAULT), 1.0 = fully lit
float scatterStrength;    // 0.0 = off; fakes forward-scatter: brightens where the
                          // view direction aligns with the light (smoke glowing
                          // when backlit — the single most convincing smoke cue)
```

**`lightingStrength` must default to 0.0** so nothing already shipped changes
appearance until it opts in. F2/F3/F4 set it to 1.0.

### F1b — Blend discipline

Add an explicit blend mode to the config instead of relying on callers to wrap
draws:

```c
typedef enum {
    VFX_BLEND_ALPHA = 0,   // DEFAULT — smoke, dust, ash, cloth, anything that occludes
    VFX_BLEND_ADDITIVE,    // embers, sparks, glow, energy ONLY
    VFX_BLEND_PREMULTIPLIED // authored flipbooks (E4) — lets one sheet do both
} VFX_BlendMode;
```

**The law, to be stated in `API_GUIDE.md` and enforced in review:** if the thing
being drawn would *block light* in reality, it is `ALPHA`. If it *emits* light,
it is `ADDITIVE`. Smoke that glows is **two draws** — an alpha body plus an
additive glow — never one additive draw. Most of the 42 additive uses are
violations of this and get corrected as their effects are rebuilt.

`VFX_BLEND_PREMULTIPLIED` is the important one for E4: a premultiplied sheet can
carry both an occluding body and an emissive core in a single texture and a
single draw, which is how commercial engines ship fire.

**DoD**
- E0 captures re-taken: **identical** output with `lightingStrength = 0`
  (proves the opt-in default is truly inert), and a demonstrably volumetric
  result on a test puff with it at 1.0.
- Backlit test: a puff between camera and a bright `VFXLight` visibly glows
  through — this is the acceptance test for `scatterStrength`.
- `API_GUIDE.md` documents the blend law with the two-draw glowing-smoke recipe.
- rlvk headless green; GLES converted and verified.

**Landmines**
- `particles.fs` is `#version 430` — the GLES conversion
  (`scripts/convert_shaders_to_gles.py`) must be re-run and checked, not assumed.
- Alpha-blended particles need **back-to-front sorting** to composite correctly,
  which additive never needed. If the pool does not sort, sorting must be added
  or the smoke will show quad-order popping as the camera orbits. Check before
  assuming, and budget for it — this is the hidden cost of F1b.
- Lighting maths per particle is a real fill-rate cost on Mali. Gate
  `lightingStrength` behind `GfxQuality_Get()` (`GFX_LOW` forces 0).
- Batching hazard: any depth-state change needs `rlDrawRenderBatchActive()`
  either side (`ENGINE_LANDMINES.md` §1).

---

## F2. Smoke — rebuild on layered sprites

**Owner:** Core Agent · **Size:** L · **Depends on:** F1; E4's
`fb_smoke_lit_8x8` strongly preferred (procedural fallback required)

Replaces the single-quad-plus-FBM approach in `energy_smoke.fs` /
`smoke_column.fs`. Those shaders are not "wrong" — they are a good implementation
of an architecture that has a ceiling.

```c
// core/composition/common/vc_smoke_volume.inl
int  VFX_ComposeSmokeVolume(Vector3 origin, VC_MaterialId mat, float scale,
                            float density, float duration);
void VFX_SmokeVolumeSetWind(int handle, Vector3 wind);
```

### START HERE — the first VFX to build after F1 is a single puff

Not a smoke column, not a skill effect. **`VFX_ComposeSmokePuff` — one short-lived
dust/smoke puff** — is the correct first thing to build on the F1 foundation, for
three reasons:

1. It is the **smallest thing that exercises all four root causes at once**
   (lighting, alpha over additive, layered sprites, authored silhouette). If F1
   is wrong, a puff reveals it in one session, before anything is built on top.
2. **Almost everything else consumes it**: impact dust, footstep, landing, the
   base of an explosion, the smoke F3's fire cools into, ground scuffs. A smoke
   column, an explosion and a fire's smoke are all *the same puff* with different
   spawn patterns and wind — build the unit correctly and the rest is
   orchestration.
3. It is **objectively judgeable**. Everyone knows what a dust puff should look
   like; "is this aura good" is not a question that can validate a foundation.

**Acceptance test — run all four in the NEW FX tab:**

| Test | Passes when |
|---|---|
| Strip the colour | The silhouette is still lobed and readable — not a fuzzy blob |
| Orbit the camera | Visible internal parallax between layers |
| Sweep a `VFXLight` around it | Lighting travels across it; it **glows** when the light is behind it |
| Put it against a *bright* background | It still goes **dark** — this is the proof additive is gone |

The third is the one that matters most: backlit smoke lighting up is the single
most convincing volumetric cue the eye has, and it only appears when F1's
`scatterStrength` is right. **If that test fails, stop and fix F1 — do not
proceed to the column, and do not proceed to F3.**

Only once the puff passes all four, scale up to `VFX_ComposeSmokeVolume` below.
The second VFX to build is not the aura but the **impact package** (puff +
debris + light flash + hitstop, see E6) — it is the most-seen effect in the game
and it reuses the unit immediately. The aura (F4) waits because it depends on E2.

### The architecture that actually works

1. **Many sprites, not one.** 20–40 alpha-blended, **lit** (F1) billboards per
   puff, each with its own scale, rotation, rotation speed, lifetime offset and
   spawn jitter. The internal parallax as the camera moves is most of the
   perceived volume.
2. **Size grows, opacity falls, over life.** Smoke expands and thins; use
   `radiusCurve` rising and `alphaCurve` falling (both already in
   `ParticleConfig`). Never a constant-size puff.
3. **Silhouette from the flipbook, detail from noise** — the inversion of what
   the current shaders do. `SpriteAnim` already plays atlases on particles.
   Procedural fallback: erode a soft disc with a *low*-frequency warp so the
   outline stays lobed, rather than a high-frequency wisp.
4. **Rotation variance is not optional.** Identical un-rotated sprites read as
   repeated stamps instantly; per-particle spin kills the pattern.
5. **Dark base colour.** Smoke's body is dark and takes its brightness from
   light (F1), not from its own colour. Do not author bright smoke.
6. **Depth fade on** — `soft_particle.glsl` +
   `ScreenDistort_BindDepthForSoftParticles`, so puffs don't razor-cut the
   ground.

**DoD:** camera orbit shows real internal parallax; puff is visibly lit from one
side and glows when backlit; reads as smoke with colour stripped; ≤40 sprites per
puff; manifest + NEW FX tab; A/B against the E0 `SmokeColumnFX` capture.

---

## F3. Fire — black-body ramp + smoke transition

**Owner:** Core Agent · **Size:** M · **Depends on:** F1, F2 (fire needs its
smoke), E4's `fb_fire_8x8` preferred

```c
int VFX_ComposeFlameVolume(Vector3 origin, VC_MaterialId mat, float scale,
                           float intensity, float duration);
```

Fixes the specific faults in `fire_funnel.fs:69-76`:

1. **Black-body ramp, not three smoothsteps.** Temperature `t ∈ [0,1]` maps
   `dark red → red → orange → yellow → white`, non-linearly weighted so most of
   the flame body sits in the orange band and white appears only at the very
   hottest core. Put the ramp in a shared helper so every fire effect uses the
   same one — a `ColorGradient` preset on `VC_MAT_FIRE` is the natural home.
2. **Cool into smoke at the tip.** This is what the current implementation is
   most obviously missing. As a flame element ages it must lose temperature,
   desaturate, darken, and **hand off to F2's smoke** — fire and its smoke are
   one continuous system, not two effects stacked. Drive it off the same age
   parameter and spawn smoke via `onDeathEmit`.
3. **Additive core, alpha body.** Per F1b: the hot core is additive, the cooler
   outer flame and the smoke are alpha. One additive draw for the whole flame is
   exactly why fire currently reads as glowing gas.
4. **Rise + turbulence + buoyancy.** Elements accelerate upward as they heat and
   decelerate as they cool; `VC_MotionJitter` for turbulence. Constant-velocity
   rise reads as a jet, not fire.
5. **Silhouette licks.** Flame's readable feature is its tongues. From a
   flipbook, or procedurally as a few elongated, stretched, fast-spinning
   elements at the leading edge.

**DoD:** fire visibly transitions to smoke at the tip in one continuous system;
white appears only at the core; lights the environment via `VFXLight_Spawn` and
(with E2) the caster; A/B against the E0 fire capture.

---

## F4. Character aura — break the shell

**Owner:** Core Agent · **Size:** M · **Depends on:** F1, E2 (the light bleed
is half the effect)

`aura_shell.fs` is fresnel + scrolled FBM — the "plastic bubble". A shell alone
can never read as an aura because its silhouette is a smooth ellipsoid no matter
what is painted on it.

```c
int  VFX_ComposeCharacterAura(int agentId, VC_MaterialId mat, float intensity);
void VFX_AuraSetIntensity(int handle, float intensity01);
void VFX_KillCharacterAura(int handle);
```

The aura is **three layers**, and the shell is the least important:

1. **Discrete elements breaking the outline (most important).** Motes, wisps and
   short ribbons orbiting and rising past the body silhouette — `VC_MotionHelix`
   for rise-and-spin, `VC_MotionOrbit` for the band, `VC_Flicker01` for
   per-element life. These are what the eye reads as an aura; they must cross the
   silhouette edge, not stay inside it.
2. **Directional flow on the body**, not uniform scroll. Flow *upward along the
   body axis* (and outward at the shoulders/hands) rather than a uniform noise
   scroll — uniform scroll is what makes it read as a texture on a bubble. Keep
   the fresnel rim, but drive it with a `VC_Breathe` pulse so it lives.
3. **Actual light emission** — `VFXLight_Spawn` tracking the agent, so with E2
   the character is lit by their own aura. Without this an aura always looks
   pasted on. This layer is why F4 depends on E2.

Ground contact matters too: a faint `VFX_ComposeGroundAura` under the feet ties
the character to the floor. An aura floating free of the ground reads as a decal.

**DoD:** silhouette is visibly broken by discrete elements from every camera
angle; character is lit by their own aura (with E2 on); intensity ramps smoothly
via `VFX_AuraSetIntensity` with no popping; pooled per-agent with a hard cap
(`#define VFX_AURA_MAX 8`), oldest recycled with a `TRACELOG`.

**Landmine:** an aura is long-lived and per-agent — it must be killed on agent
death/despawn or the pool leaks. Handle-returning ⇒ needs its own Update/Draw
pool per §0.3.

---

# PART B — Polish on top of the fundamentals

---

## E0. Baseline capture

**Owner:** Core Agent · **Size:** S · **Depends on:** F0 (baseline only the survivors)

Without this there is no way to tell whether E1–E7 actually improved anything,
and the later rounds have no acceptance evidence.

**Files:** `sandbox/auto_test.c` (or a new `scripts/capture_vfx_baseline.sh`)

**Steps**
1. Add an autotest case that walks the NEW FX tab and calls
   `AutoTest_SaveScreenshot(name)` (`sandbox/auto_test.h:38`) at a **fixed**
   camera, **fixed** effect time (drive time manually, do not use wall clock —
   captures must be reproducible) for these 8:
   `Fireball, ThunderOrb/MeshElectricity, Tornado, BlackHole, SlashArc,
   GroundAura, SmokeColumnFX, Impact`.
2. Store under `docs/baseline/E0/<name>.png`.
3. Make the capture re-runnable with one command; note that command in
   `PROGRESS.md`.

**DoD:** 8 PNGs committed; re-running the command reproduces byte-comparable
framing (not necessarily byte-identical pixels); command documented.

**Landmine:** effect time must be deterministic — several compositions advance
on an internal accumulator, so a "same frame index" capture is not automatically
a "same effect phase" capture.

---

## E1. Post-FX — radial blur + anamorphic streak bloom

**Owner:** Core Agent · **Size:** M · **Depends on:** E0

The two highest-payoff-per-line changes in this whole plan. Both apply
retroactively to every effect already shipped.

**Files:** `core/post_fx.h`, `core/post_fx.c`,
`core/shaders/post_process.fs`, `core/shaders/bloom_downsample.fs`,
`core/shaders/bloom_upsample.fs`, new `core/shaders/radial_blur.fs`

**Current structure you are extending** (`core/post_fx.c`): `mainRenderTex` →
`brightShader` bright-pass → `bloomTex` (1/4 res) → dual-filter down
(`dfTex[0..DUAL_FILTER_LEVELS-1]`, `DualFilterPass` at `post_fx.c:153`) → up →
`compositeShader` (`post_process.fs`) does bloom composite + tonemap + grade +
chromatic + vignette. Uniform locations are cached as `static int *Loc` at
`post_fx.c:26-50`; follow that pattern exactly for new uniforms.

### E1a — Radial / directional blur

This is what makes an ER burst read as *violent* rather than merely *bright*.

Add to `PostFXConfig`:

```c
  // Radial blur (Đợt E1) — screen-space smear from a focal point. Drives the
  // "violent burst" read; effects raise strength on a curve and let it decay.
  bool    radialBlurEnabled;
  Vector2 radialBlurCenter;   // screen UV [0..1], normally the effect projected
  float   radialBlurStrength; // 0 = off, ~0.15 strong. Sample offset scale.
  float   radialBlurFalloff;  // radius (UV) where the blur reaches full strength
```

Implementation: a new pass between bloom composite and tonemap, or — cheaper and
preferred — fold it into `post_process.fs` as an 8-tap loop along
`uv - radialBlurCenter`, weighted by
`smoothstep(0.0, radialBlurFalloff, length(uv - center))` so the focal point
stays sharp. Gate the whole loop behind `radialBlurEnabled` so the OFF path
costs one branch.

Add a spawn-side helper so effects don't poke the config directly:

```c
// core/post_fx.h — decays automatically, mirrors CameraFX_Shake's trauma model
void PostFX_RadialBurst(Vector3 worldPos, float strength, float duration);
void PostFX_UpdateTransient(Camera3D cam, float dt); // projects worldPos → UV, decays
```

Call `PostFX_UpdateTransient` from `main.c` next to `VFXLight_Update`
(`main.c:1716`) — **ask the owning agent to make the `main.c` edit**, do not
edit it directly.

### E1b — Anamorphic streak bloom

~20 lines, large stylistic payoff. In the dual-filter downsample
(`bloom_downsample.fs`), add a second, **horizontally-biased** tap set whose
sample offsets are stretched on X (e.g. ×4) and compressed on Y, accumulated
into the same target with its own weight. Expose:

```c
  bool  bloomStreakEnabled;
  float bloomStreakStrength; // 0..1, blend of the streaked tap vs the round one
  float bloomStreakAngle;    // radians, 0 = horizontal (classic anamorphic)
```

**Both features default OFF** in every `PostFXConfig` initialiser, so no existing
map or skill changes appearance until it opts in.

**DoD**
- `PostFXConfig` extended, each field commented in the header.
- E0's 8 captures re-taken with the features ON → visible delta, no regression
  with them OFF (compare against E0 to confirm the OFF path is untouched).
- rlvk headless test green (`scripts/run_rlvk_runtime_test.sh`).
- GLES path converted/verified (`scripts/convert_shaders_to_gles.py`).

**Landmines**
- `PostFX_IsHDR()` may be false on some backends — the blur must not assume a
  float target.
- Shader compile failure draws **WHITE**, not black. If it goes invisible, the
  shader compiled and the maths is wrong (see `ENGINE_LANDMINES.md`).
- GLES needs matching precision qualifiers and `f`-suffixed float literals.

---

## E2. Emissive VFX light bleed onto characters

**Owner:** Core Agent, **coordinated with** Character + Environment Agents
· **Size:** M · **Depends on:** E1 (ordering only, not technically)

ER's signature look is the caster lit by their own spell. **Verified gap:**
`core/shaders/surface_lit.fs` reads only `u_sunToLight`, `u_rimColor` and the
shadow `u_lightVP` from `environment_system` (`surface_lit.fs:24-78`). It never
reads the `vfx_light` list. `vfx_light.h` maintains the data
(`VFXLight_GetActive(out, &count, maxCount)`) but nothing consumes it for
character meshes.

**Files:** `core/shaders/surface_lit.fs`, `core/surface_material.c`
(shader is bound at `surface_material.c:26`), `core/vfx_light.h` (read-only)

**Steps**
1. Add to `surface_lit.fs`:
   ```glsl
   #define MAX_VFX_LIGHTS 4
   uniform int  u_vfxLightCount;
   uniform vec3 u_vfxLightPos[MAX_VFX_LIGHTS];
   uniform vec3 u_vfxLightColor[MAX_VFX_LIGHTS];   // pre-multiplied by intensity
   uniform float u_vfxLightRadius[MAX_VFX_LIGHTS];
   ```
   Accumulate a smooth inverse-square-ish falloff
   (`att = pow(clamp(1.0 - d/radius, 0.0, 1.0), 2.0)`) into the diffuse term
   **after** the half-Lambert sun term, before the fresnel rim. Keep it additive
   and let ACES roll it off — do not clamp to 1.0.
2. In `surface_material.c`'s per-frame update (alongside the existing
   `SurfaceMaterial_UpdateFrame` env upload), call `VFXLight_GetActive` with
   `maxCount = 4`, sort/pick by proximity to the camera target, upload.
3. Gate behind `GfxQuality_Get()` — `GFX_LOW` uploads 0 lights (the loop trips
   once and exits), `GFX_MED` 2, `GFX_HIGH` 4.

**DoD:** a screenshot pair proving the hero is visibly lit by their own fireball;
`GFX_UNLIT`/`GFX_LOW` unchanged versus E0; no per-frame allocation.

**Landmines**
- 4 lights × 3 uniform arrays is a real uniform-block cost on Mali — measure
  before raising `MAX_VFX_LIGHTS`.
- Do **not** edit files under `character/`. If the character path needs a change,
  hand the Character Agent a specific request.
- Loop bounds must be a compile-time constant on GLES; branch on
  `i < u_vfxLightCount` inside a `for (i = 0; i < MAX_VFX_LIGHTS; i++)`.

---

## E3. `VFX_Sequence` — the choreography layer

**Owner:** Core Agent · **Size:** L · **Depends on:** none technically; do
before E5/E6 · **This is the structural keystone of Đợt E.**

ER effects read clearly because particles, light, hitstop, shake, distortion and
sound land on **one shared beat track** with a strict
`anticipation → burst → sustain → dissipate` envelope. Today every skill
hand-codes that timing, so the envelope is accidental and inconsistent.

**Files (new):** `core/composition/vfx_sequence.h`, `core/composition/vfx_sequence.c`
**Files (edited):** `core/composition/visual_composer.c` (call into the pool from
`VFX_Compose_Update` / `VFX_Compose_Draw3D` at lines 48-49 — this keeps the
existing single `main.c` wiring and needs **no `main.c` edit**)

### API

```c
typedef enum {
    VFX_BEAT_COMPOSE = 0, // fire a VFX_Compose* callback
    VFX_BEAT_LIGHT,       // VFXLight_Spawn
    VFX_BEAT_SHAKE,       // CameraFX_Shake
    VFX_BEAT_HITSTOP,     // TimeFX_Hitstop
    VFX_BEAT_DISTORT,     // ScreenDistort_Add
    VFX_BEAT_RADIAL,      // PostFX_RadialBurst (E1)
    VFX_BEAT_DECAL,       // decal_system
    VFX_BEAT_CALLBACK     // user fn(Vector3 origin, float scale, void *ud)
} VFX_BeatKind;

typedef struct {
    VFX_BeatKind kind;
    Vector3      offset;      // relative to the sequence origin (meters)
    float        a, b, c;     // kind-specific scalars — document per kind in the header
    Color        color;       // VC_MAT_* colour if left {0,0,0,0}
    void       (*cb)(Vector3 pos, float scale, void *ud);
    void        *ud;
} VFX_Beat;

typedef struct VFX_Sequence VFX_Sequence;   // opaque; static pool

VFX_Sequence *VFX_SeqBegin(Vector3 origin, VC_MaterialId mat, float scale);
void          VFX_SeqAt(VFX_Sequence *s, float t, VFX_Beat beat); // t in seconds from play
void          VFX_SeqPlay(VFX_Sequence *s);
void          VFX_SeqStop(int handle);
```

### Implementation notes

- Static pools: `#define VFX_SEQ_MAX 16`, `#define VFX_SEQ_MAX_BEATS 24`. On
  overflow, recycle the **oldest playing** sequence and `TRACELOG` once — never
  fail silently (see the `rlvk_shaderc.inl` precedent: a silent skip path cost a
  full debugging session).
- Update: advance each playing sequence's clock, fire every beat whose `t` has
  been crossed this frame, mark it fired. **Fire beats in `t` order** even when
  several land in one frame, and fire all of them — never drop a beat because
  `dt` was large (frame spikes must not eat the hitstop).
- `VFX_SeqAt` before `VFX_SeqPlay` only; asserting/ignoring afterwards is fine,
  but document which.
- Colour default: `{0,0,0,0}` means "take it from `VFX_Material(mat)`".
- Provide **envelope presets** so callers get the ER shape for free:
  ```c
  // Pre-fills a standard 4-phase envelope; caller then overrides individual beats.
  VFX_Sequence *VFX_SeqPreset(Vector3 origin, VC_MaterialId mat, float scale,
                              float anticipation, float burst, float sustain, float dissipate);
  ```

**DoD**
- Pool wired into `VFX_Compose_Update`/`VFX_Compose_Draw3D`; no `main.c` change.
- **One existing skill ported to it as proof** — pick a simple one, keep the old
  path deleted, not commented out.
- Worked example in `core/docs/API_GUIDE.md` (the index `API.md` is generated —
  do not hand-edit it; run `bash scripts/gen_core_api_index.sh > core/docs/API.md`).
- Manifest entry + `sync_vfx_test.py` + visible in the NEW FX tab.

**Landmine:** the sequence clock must use the **post-`TimeFX_Apply`** dt, or a
sequence that itself triggers hitstop will slow its own remaining beats — decide
this deliberately and comment the choice in the header. (Recommended: sequences
run on scaled dt so the whole effect stretches with the hitstop, which is the ER
feel; expose a `bool unscaled` on the sequence for the exceptions.)

---

## E4. Authored texture / flipbook library

**Owner:** user / asset pipeline · **Size:** L · **Runs in parallel from F1**

> **Priority raised:** F2 (smoke) and F3 (fire) consume `fb_smoke_lit_8x8` and
> `fb_fire_8x8`, so this is no longer a late-plan item — it gates the two most
> visible fundamentals. Both have procedural fallbacks, but the flipbook is what
> supplies the silhouette that noise structurally cannot (§0.1b cause 3).

The single biggest quality gap, and **not a code problem**. `SpriteAnim` already
does atlas playback on particles (`core/sprite_anim.h`), so the consumer side
needs no new engine work.

Target set, all under `assets/textures/`:

| Group | Files | Notes |
|---|---|---|
| Flipbooks | `fb_fire_8x8`, `fb_smoke_lit_8x8`, `fb_dust_4x4`, `fb_spark_burst_4x4` | power-of-two grid, premultiplied alpha |
| Glints / streaks | `glint_star_4pt`, `glint_star_6pt`, `streak_aniso` | single-channel, used as a mask |
| Slash masks | `arc_slash_01..04` | crescent with torn/frayed edges |
| Sigils | `rune_glyph_atlas_4x4`, `sigil_ring_01..03` | grayscale, colour comes from `VFX_Material` |
| Distortion | `distort_normal_swirl`, `distort_normal_shock` | tangent-space normal maps for `ScreenDistort` |
| Erosion | `erosion_mask_01..03` | dissolve-specific; distinct from the generic `noise.png` |

**Engine-side work while assets are pending:** each consumer composition (E5/E6)
must fall back to a procedural path when its texture is missing, so the build
never depends on an asset landing.

**DoD:** files present, one `assets/INDEX.md` entry each, one composition
consuming each group.

**Landmine:** Mali dies on `fract(sin(...))` noise hashes at large domains
(invisible aura on device, fine on desktop) — this is exactly what these authored
masks exist to replace. See `ENGINE_LANDMINES.md`.

---

## E5. New compositions, batch 1 — foundation

**Owner:** Core Agent · **Size:** L · **Depends on:** E3 (author them against
the sequencer); E4 optional (procedural fallback required)

Ordered by how many future skills each unlocks. Each is a new `.inl` under
`core/composition/common/` (or the element folder), included from
`visual_composer.c`, declared in `visual_composer.h`.

### 1. `VFX_ComposeGlintSparkle` — highest reuse
```c
void VFX_ComposeGlintSparkle(Vector3 center, VC_MaterialId mat, float scale, float time);
```
Anisotropic star glints over a point cloud (Elden Ring's holy/faith signature).
**Nothing in the current 79 components does this.** Points on a Fibonacci sphere
(reuse the distribution from the thunder-orb work), each drawn as a camera-facing
quad with `glint_star_*` (fallback: two crossed additive quads), each twinkling
on its own phase via `VC_Flicker01(time, seed)` and scaled by `VC_Pulse01`.
Additive blend, depth-test on / depth-write off — **flush the batch either side**.

### 2. `VFX_ComposeRuneCircle`
```c
void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat,
                           float radius, float t01, int ringCount);
```
`ringCount` counter-rotating rings + glyph atlas + expand/collapse on `t01`.
`MagicFilaments` and `GroundAura` exist but there is **no layered sigil**, and
every caster skill wants one. Rings alternate spin direction; glyph cells picked
per-ring from `$SEED`; radius driven by a `SkillCurve` so it snaps open and eases
shut. Fallback without the atlas: procedural tick marks.

### 3. `VFX_ComposeChargeConverge`
```c
void VFX_ComposeChargeConverge(Vector3 center, VC_MaterialId mat, float radius,
                               float t01, int moteCount);
```
The universal cast tell: motes sucked inward via `VC_MotionSpiralIn(center,
radius, turns, phase, t01)`, brightness ramping on an `emissiveCurve`, paired with
a `VFXLight_Spawn` whose radius grows with `t01`. Makes any skill read as
*wound-up* rather than *popped*. Pair with `VFX_SeqPreset`'s anticipation phase.

### 4. `VFX_ComposeDissolveExit`
```c
void VFX_ComposeDissolveExit(Vector3 pos, VC_MaterialId mat, float scale, float t01);
```
A **shared** erosion-out attachable to any effect's death. ER effects never pop
off — they erode with a bright leading edge. `core/shaders/dissolve.fs` already
exists but is not exposed as a composition. Drive the dissolve threshold from
`t01`, tint the edge band from `VFX_Material(mat)->glow`.

**DoD per composition:** built from `VFX_Material` + `vc_motion.h` + primitives
(`vc_common.inl`); no hard-coded colours without a comment; meter-scale;
manifest entry added **by hand** then `sync_vfx_test.py`; visible and correct in
the NEW FX tab; procedural fallback when its E4 texture is absent.

---

## E6. New compositions, batch 2 — signature looks

**Owner:** Core Agent · **Size:** L · **Depends on:** E5, plus as noted

### 5. `VFX_ComposeSweepSlash` — *needs E4*
```c
void VFX_ComposeSweepSlash(Vector3 origin, Vector3 dir, VC_MaterialId mat,
                           float length, float arcRad, float t01);
```
Arc mesh driven by an authored flipbook mask **plus refraction behind it**
(`ScreenDistort_Add` along the arc). The current `SlashArc`/`GustSlash` are purely
procedural, which is precisely why they don't read as ER weapon arts. Build the
strip with `ribbon_strip.h` in `RIBBON_FIXED_NORMAL` mode + `Ribbon_ComputeArcLengthUV`
so the mask doesn't stretch unevenly.

### 6. `VFX_ComposeImpactPackage` — *needs E3*
```c
void VFX_ComposeImpactPackage(Vector3 pos, Vector3 normal, VC_MaterialId mat,
                              float scale, float severity01);
```
One call = particles + decal + light flash + distortion + hitstop + shake, tuned
as a unit and scaled coherently by `severity01`. The successor to
`VFX_ComposeTriggerImpactBurst` with the non-visual beats folded in; implement it
**as a `VFX_SeqPreset`**, which is the proof that E3 earns its keep.

### 7. `VFX_ComposeLightShaft` — *needs E1*
```c
void VFX_ComposeLightShaft(Vector3 from, Vector3 to, VC_MaterialId mat,
                           float width, float intensity);
```
Godrays. Camera-facing tapered quads with soft-particle depth fade, brightness
boosted above the bloom threshold so E1's streak bloom does the heavy lifting.

### 8. `VFX_ComposeSpiritSwarm`
```c
int VFX_ComposeSpiritSwarm(Vector3 from, Vector3 to, VC_MaterialId mat,
                           int count, float spread, float duration);
```
Many small directed wisps with **staggered** arrival (the summon / scarlet-rot
family). Returns a handle → **needs its own Update/Draw pool**, per §0.3.

---

## E7. Retrofit pass — the checkpoint

**Owner:** Skills Agent (Core Agent supports) · **Size:** M · **Depends on:** E5, E6

Rewrite **3** pilot skills — one Fire, one Water/Wood, one Taiji — through
`VFX_Sequence` + the new compositions. This is where the plan is proven or
falsified.

**This is a deliberate stop-gate.** A/B the result against the E0 captures. If
the retrofitted skills do not *clearly* out-read the originals, **stop and
re-scope before touching the remaining ~8 skills.** Do not proceed on the
assumption that more of the same will fix it.

**DoD:** side-by-side captures; a written verdict (continue / re-scope, and why)
in `skills/docs/PROGRESS.md`.

---

## E8. Platform + performance pass

**Owner:** Renderer (rlvk) Agent + Core Agent · **Size:** M · **Depends on:** E7

Radial blur and a second bloom tap are fill-rate hungry; the Samsung A33 / Mali
is the binding constraint.

**Steps**
1. rlvk visual-tier test (`scripts/run_rlvk_visual_test.sh`); then a real GLES 3.0
   device run.
2. Gate the new post passes behind `GfxQuality_Get()` tiers
   (`core/gfx_quality.h`): `GFX_LOW` = no radial blur, no streak bloom;
   `GFX_MED` = streak only; `GFX_HIGH` = both.
3. Profile: post-FX cost per frame at 1080p, and the A33 at its tier.

**DoD:** ≥60 FPS on PC with everything on; A33 stable at its tier; zero new
Vulkan validation errors.

**Known device traps** (all in `ENGINE_LANDMINES.md` — read it first):
- `fract(sin(...))` noise hashes break on Mali at large domains → invisible
  effect on device, fine on desktop.
- Invisible ≠ shader failure: a **failed** shader draws WHITE.
- Android needs `-DOPENGL_VERSION="ES 3.0"`; `-DGRAPHICS` is ignored there.

---

## Deliberately out of scope for Đợt E

Depth-of-field pulse on ultimates; volumetric fog/lighting; a GPU-driven particle
rewrite; and skinned/animated custom VFX meshes. ER leans on that last one
heavily, but this engine is procedural by design and changing that is a separate,
larger initiative — not a line item here.

---

## Patch log

| Date | Editor | Section | Source | Tier |
|---|---|---|---|---|
| 2026-07-22 | Claude | F0: added the locked 3-tier deletion boundary (tier 1 primitives off-limits, extend only; tiers 2+3 purged) | Owner decision this session, plus a line count of each tier | **Ground-truth** — owner's explicit scope call: composition + skills only, `particle_system.c` is an update not a rewrite |
| 2026-07-22 | Claude | Added §0.1b (root-cause diagnosis) + Part A (F0–F4) after owner reported most VFX are not good enough and the skill layer is disposable | Direct read of `shaders/particles.fs` (14 lines, unlit — ground truth), `energy_smoke.fs`, `smoke_column.fs`, `ground_aura.fs`, `aura_shell.fs`, `fire_funnel.fs:69-76`, blend-mode census across `core/composition/` (42 additive / 23 alpha), `vfx_config.h` `VFX_RenderConfig` (no blend or lighting field) | **Ground-truth** for all four root causes and the file/line evidence. **Inferred/proposed** for the F0–F4 designs, proposed signatures and sizes |
| 2026-07-22 | Claude | Whole doc — initial spec | Direct audit this session of `core/post_fx.c/.h`, `particle_system.h`, `composition/visual_composer.h/.c`, `common/vc_motion.h`, `presets/vc_material.h`, `vfx_light.h`, `sprite_anim.h`, `ribbon_strip.h`, `gfx_quality.h`, `shaders/surface_lit.fs`, `scripts/vfx_test_manifest.json`, `main.c` wiring points, `assets/textures/` listing | **Ground-truth** for §0.1's "what exists" table, the E2 gap finding (`surface_lit.fs` does not read `vfx_light`), file/line references, and the manifest format. **Inferred/proposed** for all phase plans, proposed API signatures, effort sizes, and the Elden Ring comparison — none of it is implemented or validated yet |
