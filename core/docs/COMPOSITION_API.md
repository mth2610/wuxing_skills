# Composition API — Visual Composer & Procedural Meshes

> Source headers: `core/composition/visual_composer.h`, `core/geometry/procedural_mesh_utils.h`
> Implementation: `core/composition/visual_composer.c` + modular `.inl` files
> Cross-reference: [`API.md`](API.md) §7 (Particle), §19 stub

> [!IMPORTANT]
> **This document is the mechanical catalog — WHAT exists.** For WHY/HOW to
> combine it into something that actually looks good, **read
> [`WUXING_ART_DIRECTION.md`](WUXING_ART_DIRECTION.md) first** — it's the
> aesthetic authority: Chapter 2 (Universal VFX Language), Chapter 5 (AI
> Design Rules, mandatory), Chapter 6 (VFX Cookbook — reusable layer
> recipes), Chapter 7 (10-step AI workflow + worked examples). Composing
> directly from this catalog without that context reliably produces
> mechanically-correct-but-flat effects (every function called, no
> hierarchy, no silence, no "one idea"). Section 0 below is the bridge
> between the two: it maps every Chapter 6.1 cookbook pattern to the actual
> function calls cataloged in this file, so you can go from "I want a
> Meteor" to a concrete call sequence without reading `.c`/`.inl` source.

---

## 0. Cookbook Pattern → Concrete API (read alongside WUXING_ART_DIRECTION.md §6.1)

Each row translates one of Chapter 6.1's abstract layer recipes into the
actual functions this file (and `API.md`'s primitive sections) already
document. Layer order in the recipe = call/draw order. Not every layer is
mandatory every time — see WUXING_ART_DIRECTION.md §5.4/§6.4 ("one purpose
per layer", "remove lowest layers first, never remove Core Shape or
Motion").

| Cookbook pattern (§6.1) | Recipe | Concrete calls |
|---|---|---|
| **Projectile** | Projectile → Trail → Emitter → Light → Impact → Smoke | `VFX_ComposeProjectile(VC_MaterialId,...)` (bundles core+trail+light+spin) or `SpawnProjectileTrail`/`VFX_ComposeProjectileTrail` alone → `VFX_ComposeImpact`/`VFX_TriggerExplosion` on hit → `VFX_ComposeSmokePuff`/`SmokeTrail` |
| **Beam** | Beam Core + Flow Map + Glow + Edge Sparks + Impact | `VFX_ComposeBeam(VC_MaterialId,...)` (core+glow+jitter already bundled) + `VFX_ComposeGlintBurst` (edge sparks) + `VFX_ComposeImpact` at the endpoint |
| **Sword Slash** | Motion → Trail → Ribbon → Spark → Flash → Decal | Character motion (skill state machine) + `Afterimage_Spawn` (API.md §14, ghost trail) + `VFX_ComposeSlashArc(VC_MaterialId,...)` (ribbon core) + `VFX_ComposeGlintBurst` + `VFX_ComposeStreakFlare` + `DecalSystem_Add` (§8) |
| **Explosion** | Flash → Shockwave → Explosion → Debris → Smoke → Residual Light | `VFX_TriggerExplosion(VC_MaterialId,...)` is already the full standard formula (flash/distort/light/particles/decal) — add `VFX_ComposeShockwaveRing` for an explicit ring and `VFX_ComposeEmberDrift` for lingering residue |
| **Portal** | Circle → Flow Map → Ribbon → Orbit Particles → Distortion → Glow | `VFX_SummonCircle` (2-layer counter-rotating circle + inward particle pull) + `VFX_ComposeMagicPuddle`/`VFX_ComposeGroundAura` (flow-map glow) + `ScreenDistort_Add` (§8) |
| **Aura** | Emitter → Orbit → Curl Noise → Ribbon → Soft Glow | `VFX_ComposeAura(VC_MaterialId,...)` (generic) — or `VFX_ComposeCylinderAura`/`VFX_ComposeQiAura` for a body-hugging column shape instead of a ground ring |
| **Dragon** | Head → Body Motion → Ribbon Spine → Emitter → Glow → Trail → Impact | No dedicated primitive — compose by hand: `VFX_ComposeBeam` or a mesh head as the core, Ribbon Strip (§7) as the spine, `ForceField` orbit/curl (§5) for body motion, particle emitters (§6) along the spine. Follow §6.1's stated order; this is the one pattern still requiring original composition, not a single ready-made call. |
| **Meteor** | Meteor → Long Trail → Smoke → Light → Impact → Debris → Shockwave → Dust | `VFX_ComposeProjectile`/`SpawnProjectileTrail` (long trail variant) + `VFX_ComposeSmokeStrandTrail` + `VFX_TriggerExplosion` on impact + `VFX_ComposeShockwaveRing` |
| **Tornado** | Vortex Motion → Ribbon Spiral → Particles → Debris → Mist → Leaves | Element-specific ready-made: `VFX_ComposeCyclone` (Taiji), `VFX_ComposeFireWhirl` (Fire). Other elements: compose from `ForceField` VORTEX+UPDRAFT (§5) + `VC_MotionHelix`/`VC_MotionSpiralIn` (Group 2c motion library below) + `VFX_ComposeMistVeil` (Water) as reference |
| **Summon** | Portal → Materialize → Glow → Shape Reveal → Idle Aura | `VFX_SummonCircle` (portal) → dissolve-in via `u_dissolve` (API.md §12.4 — animate 1.0→0.0, never pop) → `VFX_ComposeAura` (idle) |
| **Shield** | Core Shape → Flow Map → Energy Edge → Ripple → Light Pulse | `VFX_ComposeShield(VC_MaterialId, pos, radius, progress, time)` — already the full bundled pattern |
| **Chain Lightning** | Charge → Main Arc → Secondary Arcs → Ground Sparks → Residual | No dedicated charge primitive (`VFX_ComposeChargeUp` was removed 2026-07-10, `vc_charge.inl` deleted) — compose the charge beat by hand. Arcs: `VFX_ChainLightning(points, count, scale, hopDelay)` (`vc_archetype.inl`, staggered hop chain — `VFX_ComposeChain`/`vc_chain.inl` was also removed 2026-07-10, this is the only chain-arc primitive left). Ground sparks: `VFX_ComposeGlintBurst`. Residual: `VFX_ComposeStaticField` |
| **Healing** | Soft Glow → Particles Rise → Leaves/Light → Pulse → Fade | `VFX_ComposeAura` (HOLY/WOOD material, soft glow + rising particles) + `VC_Pulse01` (motion library, §2c below) driving a slow emissive pulse + dissolve fade-out, never a burst |
| **Charge (pre-ultimate)** | Weak Glow → Gather → Rotation → Compression → Flash → Release | No dedicated primitive (`VFX_ComposeChargeUp` removed 2026-07-10) — compose by hand from `VC_MotionSpiralIn` (converging particles) + a growing core sphere (`EffectMaterial`) + `VFX_ComposeGlintBurst` near release |
| **Dash** | Motion → Afterimage → Trail → Dust → Arrival Flash | `Afterimage_Spawn` every 0.04s while dashing (§14) + a trail (Ribbon Strip §7 or particle trail) + `VFX_ComposeStreakFlare` on arrival |
| **Impact** | Hit → Flash → Light → Sparks → Shake → Distortion → Smoke | `VFX_ComposeImpact`/`VFX_TriggerImpactBurst` (already bundles flash/decal/light/particles/distort — `ImpactBurstConfig`'s 4 steps below) + `CameraFX_Shake` (§8) — not every impact needs every layer, see §6.5 |

---

## Impact Burst (`core/composition/visual_composer.h`)
```c
typedef struct {
    /* --- Step 1: screen distortion --- */
    bool  distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;
    // distortRadius multiplied by sizeScale inside TriggerImpactBurst

    /* --- Step 2: ground decal --- */
    bool      decalEnabled;
    Texture2D decalTex;
    float     decalScale;            /* multiplied by sizeScale at call time */
    float     decalLife;
    Color     decalTint;
    bool      decalRandomRotation;   /* true = GetRandomValue(0,360), false = decalFixedRotation */
    float     decalFixedRotation;

    /* --- Step 3: point light flash --- */
    bool  lightEnabled;
    Color lightColor;
    float lightRadius;  /* multiplied by sizeScale at call time */
    float lightLife;

    /* --- Step 4: particle burst --- */
    bool particlesEnabled;
    ParticleRadialBurstConfig particles;
    // speedMin/speedMax are DIRECT m/s — no internal throttle factor applied.
    // colorStart is auto-resolved from gradient at t=0 if gradient is set,
    // so gradient-only presets (colorStart.a==0) work correctly.
} ImpactBurstConfig;

void VFX_TriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);
// alias: #define VFX_ComposeTriggerImpactBurst VFX_TriggerImpactBurst (both names valid)
```

**`VFX_ImpactPreset` (in `core/presets/vfx_presets.h`)** — used by `VFX_ComposeImpact`:
```c
typedef struct {
    bool  distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;

    bool            decalEnabled;
    DecalPresetType decalPreset;
    float           decalScale, decalLife;
    Color           decalTint;  // {0,0,0,0} defaults to WHITE inside VFX_ComposeImpact

    bool  lightEnabled;
    Color lightColor;
    float lightRadius, lightLife;

    bool                      particlesEnabled;
    ParticleRadialBurstConfig particles;
    // particles.speedMin/Max: direct m/s (1m-scale). No throttle factor.
    // particles.gradient: auto-drives colorStart; set colorStart.a>0 OR set gradient.
} VFX_ImpactPreset;
```

> **Scale note (1m-scale presets):** `particles.speedMin/speedMax` are used as-is (m/s).
> The old 0.3×/0.4× throttle factors have been removed — calibrate presets directly.
> `lightRadius` should be ≤0.4m for a standard hit so it doesn't bleach the particle cloud.

---

## Group 1: Static Mesh & Shapes (`core/geometry/procedural_mesh_utils.h`, called in Draw)
A set of raw geometry-drawing functions — they don't assign a material or blend mode themselves:
- `ProceduralMesh_DrawOrganicStonePillar`: draws a rough stone pillar with a flat cap, a flat-topped octagonal prism.
- `ProceduralMesh_DrawOrganicPuddle`: draws a rough flat water puddle as an organic, uneven polygon.
- `ProceduralMesh_DrawRock`: draws a jagged rock following a `RockMeshData` struct.
- `ProceduralMesh_DrawShardCluster`: draws a cluster of jagged pointed octahedral crystal shards following a `ShardClusterMeshData` struct.

---

## Group 2: Complete Composed Effect Sets (`core/composition/visual_composer.h`)
Automatically assign shader, texture, material, and manage the appropriate blend mode / Z-buffer state to produce a complete effect:
- `VFX_ComposeStonePillar`: raises a stone pillar according to `progress`, using the `MAT_ROCK` material.
- `VFX_ComposeBoulder`: builds a jagged boulder combined with a smooth center core, using the `MAT_ROCK` material.
- `VFX_ComposeIceCrystal`: builds a clustered translucent prismatic ice crystal, glowing, using the `MAT_ICE` material (alpha blend + depth-write off).
- `VFX_ComposeMagicPuddle`: builds a rippling magical puddle with dynamic flow via the `puddle.fs` shader, sampling a multi-config slot for `water_caustics.png` (slot 0) and `water_flow.png` (slot 1) in `REPEAT` mode.
- `VFX_ComposeFireball`: builds a two-layer fireball (a bright emissive core plus a deforming flickering shell), drawn with `BLEND_ADDITIVE` color blending.
- `VFX_ComposeSmokePuff`: bursts a puff of dense smoke at a point via `ParticleSystem_SpawnRadialBurst`.
- `VFX_ComposeStrandTrail`: the sin-wave strand trail. `VFX_STRAND_ENERGY` = bright filaments, `VFX_STRAND_SMOKE` = heavy occluding smoke (this replaced `VFX_ComposeSmokeTrail`).
- `VFX_ComposeFissureStreak(start, end, width, progress, time)` (`vc_earth.inl`, rewritten 2026-07-10, fixed a 2nd time the same day): uses `ProceduralMesh_BuildFissure` (`pm_magic_effects.inl`) to build a 5-vertex cross-section (edge–shoulder–bottom–shoulder–edge) along the centerline, jittered by a noise seed derived from `start`/`end` (stable across frames, not re-randomized every call) — a built-in light "midpoint displacement" style. 3 layers: ① structural mesh drawn with `ProceduralMesh_DrawFissureShaded` (dedicated geometry for a crack shape — see note below), ② an "ember seam" — a wide quad strip (`width*0.55`) running along the crack's bottom, `BLEND_ADDITIVE`, alpha pulsing with `time` (kept dim — earth is the least-glowing element, not lava); ③ sparse dust falling near the leading edge of the spreading crack (probability-gated per frame, not scattered along the whole length). `progress` (0..1) controls how many cross-section slices are drawn → the crack "runs" A→B; `time` is used only for the ember seam's pulse.
  > [!NOTE]
  > **First fix** (`ProceduralMesh_DrawFissurePartial` + a lit `EffectMaterial(MAT_ROCK)`) was nearly invisible in the dark scene — the lit mesh sank into black-on-black, leaving only a faint ember strip visible (per a user screenshot: a thin brown-orange line). Root cause: `EffectMaterial` depends on real scene lighting, and the test map has almost no light source hitting the crack. Fix: split off a dedicated `ProceduralMesh_DrawFissureShaded(data, crossColors[5], maxSegments)` (new, in `pm_magic_effects.inl`/`procedural_mesh_utils.h`) — the crack geometry now carries its own cross-section color gradient (warm bright edge → dark brown shoulder → near-black bottom), independent of lighting. This is the "dedicated geometry for a crack shape" the user asked to have split out, instead of sharing the lit EffectMaterial used by regular rock. `DrawFissurePartial` (single solid color, lighting-dependent) is still available for cases genuinely needing to share a real lit Material.
  >
  > **3rd fix, same day:** after fix #2, the user reported that on cast they only saw "a small faint straight line," and only dust otherwise. The real root cause was inside `ProceduralMesh_BuildFissure` (not specific to Fissure Streak): the spacing between cross-section slices had a hard floor at `fmaxf(width*0.5f, 1.0f)` — a minimum of 1.0m regardless of width, so a 3m-long/0.4m-wide test crack only got ~3 slices. `progress` (A→B reveal, ramping 0→1 over 1s in the test harness) at the start only drew 1 short, near-straight slice ("small faint straight line"); if fired again before the ramp finished, only the leading dust particle was visible. Fix: spacing is now computed from total path length / `FISSURE_MAX_SEGMENTS`, floored at 0.15m instead of 1.0m → a 3m crack now gets ~15-20 slices, enough density for a smooth reveal with jaggedness visible from the very first segment.
  >
  > **4th fix, same day:** still "one faint line, sometimes nothing visible" after fix #3. The real root cause: `ProceduralMesh_DrawFissureShaded` (layer ①, the structural mesh) was called WITHOUT `rlDisableBackfaceCulling()`. The V-shaped groove has two slanted left/right walls (not a simple horizontal plane) — with backface culling on (the default state entering this function), whichever wall faces away from the camera gets fully culled, so depending on camera angle/crack direction only one wall (or almost no quads at all) survives → exactly matching the "faint/sometimes invisible" symptom. Fix: wrap `ProceduralMesh_DrawFissureShaded` in `rlDisableBackfaceCulling()`/`rlEnableBackfaceCulling()` (two-sided), the same way the ember quad in layer ② already did.
- `VFX_SpawnAuraRing(center, element, radius, duration)` (`vc_archetype.inl`, bug fixed 2026-07-10): a ring of 8 particle emitters around `center` + 1 `VFXLight`. Bug: `CreateEmitter` (`core/emitter_system.h`) only creates the emitter — the actual particle-spawning logic per `spawnRate` lives ENTIRELY inside `UpdateEmitterTarget`, which is not called by any batch Update function. `VFX_SpawnAuraRing` called `CreateEmitter` once and then never touched it again — nothing called `UpdateEmitterTarget` for those 8 emitters → `timeAccumulator` never advanced → zero particles were ever spawned, leaving only the point light visible (read as "nothing showing"). **Name-collision trap:** `main.c:440` does call `EmitterSystem_Update(dt)` every frame, but that is a COMPLETELY DIFFERENT system (defined in `skill_helper.c`, pool `s_emitters`/`EMITTER_FIRE`/`EMITTER_SNOW`...) — same name as `core/emitter_system.h` but unrelated, and it does not rescue AuraRing's emitters. Fix: added a loop in `VC_Archetype_Update` calling `UpdateEmitterTarget(emitterIds[k], p, dt)` for each point on the ring, every frame while the aura is active.
- `VFX_ComposeLightningBolt(start, end, scale)` (`vc_archetype.inl`, not a simple fire-and-forget — moved from `vc_neutral.inl` 2026-07-10): registers a slot in the `Arch_Bolt` pool (8 slots), then is automatically driven by `VC_Archetype_Update`/`Draw3D` calling `ProcBolt_Update`/`Draw` every frame for 0.5s (leader flash brightness 1.9 fading down to 0.3 — same decay formula as thunder_orb_skill's rain bolt), auto-killed via `ProcBolt_Kill` when done. Returns the slot index for reference — no need to call Kill manually (it self-expires). Previously buggy: `SpawnProcBolt` was called and then left alone, with no follow-up Update/Draw, so the bolt existed in the pool but was invisible.
- `VFX_ComposeEnergyFlow(from, to, scale, duration)` (`vc_archetype.inl`, new 2026-07-10, fixed a 2nd time the same day): an energy stream A→B — uses the `Arch_Flow` pool (8 slots), same pattern as `Arch_Bolt`, auto `EnergyFlow_Kill` when `duration` elapses. Mechanism lives in `core/vfx_proc_ray.c` (`SpawnEnergyFlow`/`EnergyFlow_Update`/`EnergyFlow_Draw`/`EnergyFlow_Kill`). Visual characteristics:
  - **Organic rolling wave running along the body** (`GenerateFlowWaypoints`): multi-harmonic across 2 perpendicular axes, both ends pinned (envelope `sin(t·π)`), the pattern MOVES over time because `wavePhase` advances with `ProcRayConfig.waveSpeed` — this is what differs from the first version (the first version was just a static curved arc → read as a "stick," and got criticized). `FLOW_WAYPOINT_CNT=17` control points, resampled via Catmull-Rom up to `FLOW_RIBBON_PTS=48` for smoothness.
  - **Width bulges in the middle, tapers at both ends** (`powf(sin(f·π), 0.55)` — "tapered width" in the reference) — an envelope array shared by all 3 layers.
  - **2nd fix (same day):** rebuilt via `DrawRibbonEnergyField` (`core/ribbon_strip.h`, see the Primitive section — actually lives in core, not composition, since both EnergyFlow and Beam need it) instead of the old flat single camera-facing `DrawRibbonStrip` — same reason as Beam's just-completed migration: a single camera-facing ribbon looks flattened/squashed when viewed at an angle ("why does it look so weird?" — user reported). Now uses a real "+" cross-section with 2 planes (`RIBBON_WORLD_UP`) like Beam, reading as solid/volumetric from any camera angle. 3 layers: a wide textureless soft glow / a body layer with its own texture-scroll speed (`ProcRayConfig.flowScrollSpeed`) / a thin hot-white core scrolling faster (×1.3) + `vFlip`. The lengthwise color gradient (`FlowLerpColor`) was dropped in favor of 3 fixed per-layer colors (simpler, matches the Beam pattern) — no longer tracks a separate `scrollOffset` pre-multiplied by `flowScrollSpeed`, switched to raw `elapsedTime` so each layer multiplies its own scroll speed.
  - **3rd fix (same day):** still looked "off" — a blurry band with no visible texture streak. Root cause: it was mistakenly using `water_flow.png` (a flow-DIRECTION map texture for `VFX_ComposeMagicPuddle`, not a visually-streaky texture) instead of `energy_flow.png`, which Beam uses — switched to `energy_flow.png` for consistency. Also changed `glowWidthMult` 2.6→1.4: the textureless glow layer was 2.6x wider than the textured body layer, completely overwhelming the texture detail, unlike Beam's ~1.3x ratio (outer 0.65 / inner 0.50) — reduced to 1.4 so the textured body layer isn't swallowed by the glow.
  - **4th fix (same day):** still no visible "swelling rolling energy" — the user correctly identified the remaining cause: the wide glow layer + `BLEND_ADDITIVE` adds a UNIFORM amount of color across the entire width, lowering the contrast of the texture underneath (the texture needs a dark background for its bright streaks to read clearly; adding flat background color on top reduces the light/dark difference). Fix: `thickness` 0.06→0.025 (thinned significantly per the "core needs to be truly thin" requirement), `glowWidthMult` 1.4→1.15 (close to the body layer, now just a thin hazy edge), `colorGlow.a` 150→90 (reduces the additive background level), hot-core layer `widthRatio` 0.4→0.22 (a thinner bright stream).
  - **5th fix (same day):** fix #4 overcorrected — shrinking `thickness` overall made EVERYTHING (glow and body alike) too thin; user feedback was "the energy layer is too thin now, can't see it, the energy layer needs to be wide and soft." Clarified two distinct roles: "core" = the innermost bright stream (hot-core layer, `widthRatio=0.22`) — keep THIN; "energy"/glow = the outermost textureless layer — needs to be WIDE and SOFT (a difference in width, not thinness). Fix: `thickness` 0.025→0.05 (shared base for all layers — whichever layer needs to be thin already has its own small `widthRatio`, no need to squeeze the shared base thin); `glowWidthMult` 1.15→2.8 (properly wide, matching its role as a soft surrounding halo); `colorGlow.a` 90→110 (visible enough given the larger area, still lower than body/core so it doesn't swallow texture detail).
  - **6th fix (same day) — dropped the glow layer entirely:** the user decided to remove the "bloom" layer (the textureless glow layer) instead of continuing to tweak width/alpha — after 3 rounds of adjustment (2.6→1.4→1.15→2.8) still no good balance was found between "wide enough to see" and "not swallowing the body layer's texture detail." `DrawFlowChannel` now has only 2 layers: the body using `energy_flow.png` (widthRatio=1.0) + a thin hot-white core (widthRatio=0.22, vFlip, scrolling ×1.3 faster). A soft halo around the energy stream (if needed) is left to the game's post-process bloom system (`core/post_fx.c`) instead of being faked with a flat additive quad. `cfg->colorGlow`/`cfg->glowWidthMult` are no longer used in this function (the fields remain in `ProcRayConfig`, still used by ProcRay/ProcBolt).
  - **7th fix (same day):** request to "make the energy stream even wider" — increased `thickness` 0.05→0.10 (shared base; the core remains relatively thin thanks to its own `widthRatio=0.22`).
- `VFX_ComposeGhostTendrils` — **removed** (2026-07-10, same day it was added). Used to be N=5 `EnergyFlow` strands independently cycling hidden/visible (`Arch_TendrilGroup` pool in `vc_archetype.inl`), using the `ProcRay_GhostFlowConfig` preset (removed along with it). Criticized as "ugly" after two rounds of adjustment (thickness, alpha) — removed outright per the user's request instead of continuing to patch it. If the concept of "multiple intermittent energy streams from a source to a target" is needed again later, the old approach (a multi-strand timing layer stacked on top of `EnergyFlow`) is a starting-point reference, but the shape/material needs to be redesigned — it's still unclear whether the root problem was EnergyFlow's ribbon being inherently too thin, or the color/preset mix being wrong.
  > [!NOTE]
  > Background: the user proposed a separate `RibbonMesh`/`MeshBuilder` architecture (points → mesh) for all ribbon-shaped VFX. Investigation showed `core/ribbon_strip.h`'s `RibbonPoint`/`DrawRibbonStrip` already IS that module (already shared by Beam/Bolt/Trail) — avoiding creating a parallel module ([[feedback_vfx_reuse_before_invent]]). Instead, `ribbon_strip.h` was extended: added `RibbonMode` (`RIBBON_CAMERA_FACING`/`RIBBON_WORLD_UP`/`RIBBON_FIXED_NORMAL`) via `DrawRibbonStripEx`, and `Ribbon_ComputeArcLengthUV` (fixed a UV bug where UV = index/count instead of real arc length, affecting `DrawChannel` shared by ProcRay/ProcBolt too). `EnergyFlow` is the first real use case that actually needs texture-scroll over a ribbon (until now every ribbon in the project was drawn textureless, `(Texture2D){0}`).
  > Not done yet: a persistent VBO (`Ribbon_Create/Update/Draw/Destroy`) — no measured bottleneck justifies it, and it would deviate from the consistent "rebuild every frame" pattern used throughout `core/geometry`.
- `VFX_ComposeShardDebris(pos, count, speed, matId)` (`vc_archetype.inl`, new): launches a cluster of `count` 3D geometric debris shards, shaped as irregular boxes (6 faces / 12 triangles), flying outward in a hemispherical cone from `pos`.
  - **Physics & Collision Model**: Each shard has its own fully independent kinematic trajectory, affected by gravity (Y = -9.81 m/s) and air resistance (viscosity drag), while tumbling randomly around its own axis.
  - **Elastic Bounce**: On hitting the ground plane (Y = 0), shards automatically bounce elastically (damped vertical bounce), friction gradually slows their sliding on the ground, and their tumble rotation gradually decays. Once energy drops below a static threshold, the shard stops moving entirely.
  - **Dust / Sparkle (Trail & Impact Particles)**: While airborne, each shard emits a trailing streak of dust/sparkle particles. In particular, the Ice/Metal element auto-generates extremely bright additive white-blue sparkle particles. On a bounce impact, a small burst of particles fires from the contact point to sell the sense of hard impact.
  - **Erosion Dissolve**: Near the end of its `lifetime`, a shard gradually dissolves by ramping the `u_dissolve` uniform sent into the `effect_material.fs` shader, so its glowing burn edge disintegrates naturally. Thanks to the batching mechanism, this uniform is only activated during the last 20% of its lifetime, maximizing Draw Call efficiency.
  - **Elemental Variety & Randomized Geometry**: Automatically maps `matId` to the corresponding `MaterialPreset` (`MAT_FIRE` molten rock, `MAT_ICE` frost, `MAT_METAL` metal, `MAT_ROCK` earth/stone). Each shard computes a deterministic random offset on the 8 corners of a cube based on its own `seed` (no cache cost, no extra GPU upload) to produce unique flattened/irregular broken-rock shapes. The 6-face (12-triangle) polygon count hits an ideal balance between visual read (clearly reads as a prism chunk) and performance.
  >
  > **2nd migration, same day:** `VFX_ComposeBeam` (`vc_beam.inl`) migrated to the ribbon module — the old 2 intersecting planes (raw `rlBegin(RL_QUADS)`) were replaced by 2 calls to `DrawRibbonStripEx(..., RIBBON_FIXED_NORMAL, fixedNormal)`. This also surfaced 1 latent bug in `DrawRibbonStripEx`: the function didn't call `rlSetTexture(0)` after drawing — harmless for the old callers (which always passed `(Texture2D){0}`) but leaked texture state onto the first real caller with a texture (`EnergyFlow`'s core pass, `VFX_ComposeBeam`) — fixed by adding `rlSetTexture(0)` at the end of `DrawRibbonStripEx` (`core/ribbon_strip.c`).
  >
  > **The user then rewrote `vc_beam.inl` twice by hand** (a twisted multi-layer ribbon, then a 3-layer crossed-plane version with dual-scroll + V-flip + pulsing width) — bypassing `DrawRibbonStripEx` and going back to raw `rlBegin`. A technical review of the final version found: (a) the width pulsing (`sinf(time*25)*0.1f`) exactly duplicated `VC_Breathe(time,freq,amp)`, already present in `vc_motion.h` but unused; (b) `perp1`/`perp2` (a cross-basis derived from `dir`) only works for a STRAIGHT 2-point path, not general enough for future trails/spirals. The user agreed to split it out and wanted the layer count configurable → added `DrawRibbonEnergyField` + `Ribbon_ComputeCrossFrame` to `core/ribbon_strip.h` (initially tried placing it in `vc_common.inl` but moved down to core later the same day since `EnergyFlow` needs it too — see the note below) — `VFX_ComposeBeam` is now only ~35 lines: it builds color/layer config and calls `DrawRibbonEnergyField(points={start,end}, count=2, ...)`.
  >
  > **Moved down to core + applied to EnergyFlow (same day):** the user asked "update energy flow in taiji too" after seeing how much better Beam looked — which is exactly when a layering issue surfaced: `VC_DrawEnergyField` was at the time still in `vc_common.inl` (composition-only, `static`), but `EnergyFlow` lives in `core/vfx_proc_ray.c` (below composition), so it COULDN'T call it. Moved the whole struct + function down to `core/ribbon_strip.h`/`.c`, renamed `VC_EnergyFieldLayer`→`RibbonEnergyFieldLayer`, `VC_DrawEnergyField`→`DrawRibbonEnergyField`. `VC_Breathe` (only in `vc_motion.h`, composition-layer) was re-inlined as `RibbonBreathe` directly in `ribbon_strip.c` — accepting a 1-line formula duplication so core doesn't have to include a composition header. Added a `widthEnvelope` parameter (an array multiplying width per point, NULL = uniform) to preserve the "tapered width" (`powf(sin(f·π),0.55)`) that Beam's original cross-plane version didn't need but EnergyFlow does (the energy stream bulges in the middle, tapers at both ends).
- `VFX_ComposeImpact`: generates an impact effect based on ElementPresetType.
- `VFX_ComposeCast`: generates a gathering-energy cast effect based on ElementPresetType.
- `VFX_ComposeProjectileTrail`: generates a projectile trail based on ElementPresetType.
- `VFX_ComposeWaterStream`: builds a rolling, surging water stream as a soft, winding Bezier tube using the `tube.fs` shader and `water_caustics.png` texture in `BLEND_ALPHA` mode.
- `VFX_BeginWaterStreams` / `VFX_DrawWaterStreamOnPath` / `VFX_EndWaterStreams`: a trio of functions that draw a winding water stream following the turns of an arbitrary path. Supports batching all water streams drawn into a single GPU Draw Call (Single-Pass Batching), and supports an independent phase offset (`phaseOffset`) per stream so multiple streams move naturally without synchronizing.
- `VFX_ComposeWaterStreamOnPath`: a convenience function that auto-wraps the Begin → Draw → End call sequence for a single water stream following a path.
- `VFX_ComposeGlowingVine`: builds a glowing jade vine that automatically crawls and spirals to wrap tightly around a target. Drawn in 2 passes (pass 1: translucent jade with a Fresnel edge glow via `Material_LoadCustom`; pass 2: an enhanced bright-white core using `BLEND_ADDITIVE`).
- `VFX_ComposeProjectile(VC_MaterialId, ...)`: draws an elemental flying projectile with a full set of integrated effects (core sphere, particle trail, radiating light, self-spin). 6 materials have their own dedicated structural variants (FIRE = fireball, ICE = spinning ice shard, LIGHTNING = lightning bolt, WOOD = seed particle, EARTH = spinning rock, TAIJI = yin-yang); other materials fall back to a generic orb (`soft` core + `body` shell + particles from `grad`).
- `VFX_GroundPattern`: creates a magic-circle-style ground pattern as a horizontal quad with culling disabled (cracked earth, spinning magic ring, bubbling lava, frost mist, thorns growing, ancient runes).
- `VFX_ComposeBeam(VC_MaterialId, start, end, width, progress, time)`: a laser/energy beam A→B — built via `DrawRibbonEnergyField` (`core/ribbon_strip.h`, see `API.md`), 3 pre-configured layers (slow-scrolling outer shell / fast-scrolling inner electricity + V-flip for a crossed-weave feel / textureless hot-white core), width pulsing via an internal breathe formula (equivalent to `VC_Breathe` in `vc_motion.h` but inlined in `ribbon_strip.c` so core doesn't depend on composition). Blend + color taken from the material (ADDITIVE→`glow`, ALPHA→`body`), width scales up over the first 10% of progress.
- `VFX_PathWave`: generates a wave of effects growing in sequence along a list of points (rising stone pillars, growing ice spikes, crawling wood thorns, jets of fire, chained lightning), suited to skills drawn by dragging a path with the mouse.
- `VFX_SummonCircle`: creates a summoning circle with two counter-rotating magic-circle layers, pulling streams of energy particles toward the center.
- `VFX_TriggerExplosion(VC_MaterialId, ...)`: triggers an explosion following the standard formula — works for every element; gradient/force field/glow color come from the material, cracked decals for brittle elements (ICE/LIGHTNING/EARTH/METAL) and scorch decals for the rest, along with Screen Distortion, a Point Light flash, a radial particle burst, and optional camera shake.
- `VFX_ComposeAura(VC_MaterialId, pos, radius, time)`: creates a hovering aura/buff ring around the feet, radiating energy particles upward — works for every element, color = `glow` (LIGHTNING alone uses `body`, ambient purple); pure Qi uses `VC_MAT_QI`.
- `VFX_ComposeQiAura` / `VFX_AttachQiAura` / `VFX_DetachQiAura` / `VFX_UpdateQiAuras`: a Qigong-style aura wrapped around a character, keyed by `casterAgentId` (a randomly swirling column of rising qi, scattered sparkle) — `Attach` attaches/initializes per agent, `Update` runs every frame for the whole pool, `Detach` removes it at the end.
- `VFX_ComposeCylinderAura(VC_MaterialId, pos, radius, progress, time)` (`vc_cylinder_aura.inl`): a capless cylindrical energy-membrane column — suited for armor/body-protection buffs. 4 layers: (1) a `VortexFunnel` mesh with the `AuraShellMaterial` shader (`aura_shell.vs/.fs`) — FBM filaments scrolling up the Y axis + horizontal scanline rings + a Fresnel rim boost; (2) wisp curl around the column body (ForceField: NOISE_CURL + VISCOSITY + upward GRAVITY_DIR); (3) a dual rotating rune on the ground (`mat->runeDecal`, outer 18°/s + inner counter-rotating 32°/s); (4) small ember particles jetting straight up inside the column (uniformly distributed via `sqrtf` random within the disc). `progress` drives the scale-in (0..0.2) + an early-exit guard. Body/glow colors updated every frame from `VFX_Material(matId)`.
- `VFX_ComposeGroundAura(VC_MaterialId, pos, radius, scrollSpeed, time)` (`vc_ground_aura.inl`): a glowing energy disc on the ground — the `ground_aura.vs/.fs` shader draws a UV-mapped quad, the FS computes a radial mask (edge fade 0.6→1.0, center hole 0→0.3) + FBM wisps in polar coordinates radiating from the center. `scrollSpeed > 0` = energy radiating outward; `scrollSpeed < 0` = energy pulling inward. 3 layers: (1) ground disc shader + BLEND_ADDITIVE; (2) small edge sparks at the outer rim; (3) an ambient light pulse at the center. Color from `mat->body/glow`.
- `VFX_ComposeEnergySmoke(pos, scale, progress, time, sourceUV)` (`vc_smoke_energy.inl`, new — replaces the flipbook/video direction after finding/creating an atlas asset proved too hard): **a single puff of smoke** that slowly expands and then dissolves. Drawn on a **camera-facing billboard quad** (`DrawCoreBillboardQuad`, new — `core/geometry/pm_core_shapes.inl`) instead of a sphere mesh. Much cheaper than a sphere (1 quad = 2 triangles vs. a 20×20 sphere = 800 triangles).
  > [!NOTE]
  > **2nd shader rewrite (same day):** the FBM erosion-dissolve version (radial bias) wasn't good-looking — tried a 50-step raymarch turbulence modeled on the "Extinguish" shader (@XorDev) — still "extremely blurry" because the FOV was set backwards (wide right from birth instead of near dissolution) plus the accumulated intensity was too low before `tanh()`.
  > **3rd shader rewrite (same day) — analytic diffusion-equation solution:** used the **closed-form analytic solution** for a 2D point/disc source: `C(r,t) = C₀·exp(-r²/4Dt)/(4πDt)` — genuinely a Gaussian whose variance grows with `t` (spreads out) and whose peak amplitude decays as `1/t` (fades, automatically conserving "mass"). `u_progress` maps directly to `t`, `u_diffusion` = the `D` coefficient. Added FBM domain-warp to the sample coordinates before computing distance from center.
  > **4th fix (same day):** reduced `t₀` to `0.005` + recalibrated `D` (composer: 0.35→0.18) so the half-width goes from ~0.05 (a small point) at birth to ~0.7 (covering most of the quad) at dissolution.
  > **Phase 6 optimization (new):** converted the entire shader algorithm from 3D FBM to 2D FBM, cutting math load by up to 86%. Supports batching via a matched begin/end function pair, `VFX_BeginEnergySmokeBatch` and `VFX_EndEnergySmokeBatch`, eliminating render-state flushing when drawing many smoke puffs at once.
- `VFX_ComposeMagicFilaments(pos, scale, progress, color, thickness, frequency, speed, sourceUV)` (`vc_smoke_energy.inl`, new): thin sparkling energy filaments, diffusing outward from a point source `sourceUV` (UV-local) and gradually dissolving.
  - **Physics & Shape Mechanism**: Inherits `energy_smoke`'s analytic gas-diffusion model to simulate expansion and dissolution, combined with ridged FBM to generate thin energy fibers and a gaseous Fresnel glow at the edges. Sparkle peaks running along each filament are randomly generated from high-frequency noise.
  - **Extreme optimization (GPU Fill-rate)**: Uses 1-octave noise for domain warping and 2-octave ridged FBM for the fiber strands, cutting the shader's total noise queries per pixel from 10 down to 5 (a 50% GPU load saving, ensuring overlapping particles still hold 60 FPS).
  - **CPU Batching**: Includes batching functions `VFX_BeginMagicFilamentsBatch()` and `VFX_EndMagicFilamentsBatch()` to send all quads to the GPU in a single Draw Call when parameters are shared.
- `VFX_ComposeMagicFilamentsOnPlane(center, normal, scale, progress, color, thickness, frequency, speed, sourceUV)` (`vc_smoke_energy.inl`, new): a variant that draws directional sparkling filaments lying flat on a plane defined by normal vector `normal` (uses `DrawCoreOrientedQuad` to lie flat instead of facing the camera). Automatically offsets the quad 0.03m along the normal to eliminate Z-fighting.
- `VFX_ComposeBlackHole(VC_MaterialId, pos, radius, time)` (`vc_black_hole.inl`): a gravitational black hole/singularity **hovering high above the ground** (used with `pos.y` > 0 — intentionally pulling matter up from the ground, not a ground-placed effect). Uses a "sphere + swirl shader" pipeline (cheaper than true raymarching, good enough for an MMORPG running many simultaneous effects — see the cost/quality tradeoff comment at the top of the file). 7 layers: (1) an event-horizon sphere (`DrawCoreSphere` + `EffectMaterial` with an almost-absolute-black body, `translucency=0`, only a thin glowing Fresnel rim); (2) **swirl shells** — 3 concentric spheres with increasing radius, sharing the `black_hole_swirl.fs` shader (new): the shader converts `DrawCoreSphere`'s existing longitude/latitude UVs into polar coordinates (angle=longitude, radius=distance from the equator), swirls the angle by radius + time (`angle += radius*10; angle -= time*speed`), samples FBM in the swirled domain, then applies `density *= exp(-radius/bandWidth)` so energy concentrates into a band around the equator instead of lighting the whole sphere evenly — drawn directly onto the sphere's UV surface, not a cross-section plane. Each shell has a different rotation speed/direction/noiseScale/bandWidth (inner shell spins fast counter-rotating, middle spins slow with the flow, outer fades out) — the "several thin layered shells = fake volume" technique instead of true raymarching; (3) matter particles falling into the center at close range — spawned on the sphere's surface (uniform sphere sampling) around the black hole with velocity aimed straight at the center, the same technique `VFX_SummonCircle` uses for its "pull particles toward the center" step; (4) **ground drain** — the main layer that sells "sucking stuff up off the ground": particles spawn scattered on the actual ground (Y=0) right beneath the black hole and fly straight up into the center, with `lifetime = distance/speed` (clamped 0.3–3.0s) so particles genuinely "arrive" instead of dying mid-flight or overshooting; (5) a rotating rune on the ground marking the pull point (`mat->runeDecal`, the same `VC_DrawGroundRune` primitive `VFX_ComposeShield` uses), radius scaled to the black hole's `radius`; (6) `ScreenDistort_Add` triggered probabilistically each frame (space-bending, no static timer, to avoid shared-state bugs across multiple simultaneous instances); (7) a dim ambient light at the center (a black hole shouldn't be blinding). Color comes from `mat->body/glow` for the particle/rune layers; the swirl shells use their own deep purple/purple-white palette (not read from the material table — the black hole is always cosmic purple regardless of the `matId` passed in, with a comment noting this at the definition site). Default suggested material is `VC_MAT_VOID`.

---

## Group 2a (New): Batch Render Boundaries
Functions that manage OpenGL state, assign a shader, and set up blend/depth-mask once to draw a large batch of VFX entities efficiently, eliminating Raylib Batching Hazards:
- `VFX_BeginEnergySmokeBatch()` / `VFX_EndEnergySmokeBatch()`
- `VFX_BeginMagicFilamentsBatch()` / `VFX_EndMagicFilamentsBatch()`
- `VFX_BeginSmokeColumnBatch()` / `VFX_EndSmokeColumnBatch()`

> [!TIP]
> Wrap these pairs around loops that draw many particles/smoke of the same kind to multiply FPS by minimizing shader uniform changes and GPU draw-call splitting.

---

## Group 2b: Element Material Table (`core/presets/vc_material.h`) — source of truth for elemental color/material
`VC_MaterialId` is **the elemental axis of every archetype** — the old style enums (`BeamStyle`, `AuraStyle`, `ShieldStyle`, `ChainStyle`, `ZoneStyle`, `SlashStyle`, `ChargeStyle`, `QiStyle`, `ProjectileStyle`, `ExplosionStyle`) have been removed; only `GroundPatternStyle`/`PathStyle` remain (the shape axis). The enum/struct/`VFX_Material()` live in `vc_material.h` (a minimal header, includable from `visual_composer.h` without pulling in a circular include through `skill_helper.h`); `VFX_MaterialFromPreset()` lives in `vfx_presets.h`.
```c
typedef enum { VC_MAT_FIRE, VC_MAT_ICE, VC_MAT_WATER, VC_MAT_LIGHTNING,
               VC_MAT_EARTH, VC_MAT_WOOD, VC_MAT_METAL, VC_MAT_TAIJI,
               VC_MAT_HOLY, VC_MAT_VOID, VC_MAT_POISON, VC_MAT_QI, VC_MAT_COUNT } VC_MaterialId;
typedef struct {
    Color body;                   // the element's identity color (shell, ribbon, rune)
    Color glow;                   // hot-spot glow color (beam, ember)
    Color soft;                   // light pastel for aura/VFXLight/glint (the old qi-aura palette)
    int   blendMode;              // recommended blend for sheet/beam layers (ICE/VOID = BLEND_ALPHA, rest = ADDITIVE)
    const ColorGradient *grad;    // standard particle gradient
    const ColorGradient *hotGrad; // a brighter variant (only LIGHTNING differs from grad); never NULL
    const ForceField *fld;        // standard force field
    const char *runeDecal;        // ground rune-ring texture (shield/charge)
} VFX_ElementMaterial;
const VFX_ElementMaterial* VFX_Material(VC_MaterialId id);       // always returns a valid entry (bad id → TAIJI)
VC_MaterialId VFX_MaterialFromPreset(EffectPresetType preset);   // maps 8 element presets → material
static inline Color VC_WithAlpha(Color c, unsigned char a);      // attaches alpha at the call site
```
The entire composition layer (`vc_beam/aura/shield/chain/zone/slash/charge/ground/explosion/summon.inl` + the VFXLight/glint/ember spots in `vc_fire/metal/water/earth/wood/path.inl`) now looks up this table instead of hard-coding colors — **changing an element's look = editing one entry in `vfx_presets.c`**. 3-slot convention: `body` = identity color (fire crimson, metal silver...), `glow` = hot spot (fire orange, lightning cyan...), `soft` = pastel for aura/light/glint (light orange for fire, silver-white for metal...). Two deliberately-kept quirks: LIGHTNING's glow is cyan (the arc) vs. body purple (ambient, matching `s_lightningGrad`); TAIJI's body is purple vs. glow gold (aura). `VFX_TriggerExplosion`'s POISON/HOLY/VOID styles now use their own dedicated gradients (`s_poisonGrad/s_holyGrad/s_voidGrad`) instead of borrowing from wood/taiji. New components **must** pull color/gradient/force field from this table, hard-coding only when deliberately breaking element identity (with a comment) — current exceptions: `GROUND_CRACK_*` (neutral earth brown), `GROUND_THORNS` (dark thorn green), `GROUND_RUNE` (bright arcane purple), `vc_plasma`/`vc_taiji`'s own palettes (cyan-pink plasma, yin-yang duotone), and per-component local multi-stop shading gradients (`s_fireBodyGrad`, `s_dropGrad`, `s_steelGrad`...).

---

## Group 2c: Motion Library (`core/composition/vc_motion.h`) — pure math trajectories & shapers
All `static inline`, stateless, no pool, no side effects — already included via `visual_composer.h`. Convention: Y is up, angles in radians. Use these instead of ad-hoc sin/cos when wiring up motion:
```c
// Position
Vector3 VC_RingPointXZ(Vector3 center, float radius, float angle);   // point on a horizontal ring (spawn ring/orbit — the most basic building block)
Vector3 VC_MotionOrbit(Vector3 center, float radius, float speed, float time, float phase); // uniform circular orbit
Vector3 VC_MotionHelix(Vector3 base, float radius, float riseSpeed, float spinSpeed, float t, float phase); // helix rising along the Y axis
Vector3 VC_MotionSpiralIn(Vector3 center, float startRadius, float turns, float phase, float t01); // spiraling inward to the center (charge/absorb)
Vector3 VC_MotionJitter(Vector3 pos, float amp, float freq, float time, float seed); // 3-axis phase-offset trembling shake
Vector3 VC_MotionBob(Vector3 base, float amp, float freq, float time, float phase);  // vertical bobbing along Y (floating/hovering)
// Direction
Vector3 VC_TangentXZ(float angle, float up);                        // horizontal-ring tangent (NOT normalized) — velocity for particles orbiting a rim
Vector3 VC_DirCone(Vector3 dir, float coneRad, float u1, float u2); // random direction within a cone around dir (u1,u2 = Random01())
// Scalar shapers
float VC_Pulse01(float time, float freq);            // normalized sin 0..1 (periodic alpha/emissive)
float VC_Breathe(float time, float freq, float amp); // a 1±amp factor (multiply into radius/scale)
float VC_Flicker01(float time, float seed);          // hash noise 0..1 changing per frame (fire/electricity; seed decorrelates phase to avoid synchronized flicker)
```
The archetypes (`vc_charge/shield/aura/zone/slash.inl`) already use these functions for orbiting motes, tremble, breathe, spawn rings, tangent sparks. New components/skills should assemble trajectories from here first, and only write a custom formula when the motion library can't express it (and consider adding a new function to `vc_motion.h` rather than inlining it in one place).

---

## Group 3: Beauty Primitives (`core/composition/vc_beauty.inl`) — reusable decorative touches
Pure particle/decal/light, **doesn't touch the post-process pipeline** (see `PROGRESS.md` Item 35 — editing the shared bloom/streak pipeline is very fragile on older GPUs; concentrating "sparkle" into small, short-lived primitives like the ones below is the safe approach instead):
- `VFX_ComposeShockwaveRing(pos, radius, life, tint)`: a ground shockwave ring — an expanding ring decal (`assets/textures/generic/impact_ring.png`, `BLEND_ADDITIVE`) + a flash light.
- `VFX_ComposeGlintBurst(pos, count, spread, tint)`: a small burst of sparkling streaks that expand quickly then fade (~0.12-0.22s/particle) — used as a "sparkle" accent for any effect, including attaching it to other archetypes.
- `VFX_ComposeEmberDrift(pos, radius, count, tint)`: drifting ember/dust particles (noise-curl + light upward gravity), long lifetime (~1.2-2.2s) — used for continuous auras/ambience.
- `VFX_ComposeStreakFlare(pos, scale, tint)`: a bright flash burst at a point (an extremely short-lived round particle + a flash light) — reads as a "flash," not a star shape (the particle system only uses a single shared global texture, see `core/particles/particle_system.h`).

Internal primitives (static within the `visual_composer.c` translation unit, not public — callable from every `.inl`):
- `VC_DrawGroundQuadXZ(tex, halfX, halfZ, tint)`: a horizontal textured quad centered at the current transform origin (called inside the caller's push/translate/rotate). The caller manages blend/depth. The foundation of every `GROUND_*` pattern.
- `VC_DrawGroundRune(tex, pos, radius, angleDeg, tint)`: a rotating rune/glow ring around the Y axis at `pos`, auto push/pop matrix — used by shield (rune + glow ring at the dome's base) and charge (foot-level magic circle).
- `DrawRibbonEnergyField(...)` — **moved to `core/ribbon_strip.h`** (no longer in `vc_common.inl`), see `API.md` §"Ribbon Strip". Reason for the move: both `core/vfx_proc_ray.c`'s EnergyFlow and `vc_beam.inl`'s `VFX_ComposeBeam` need it — composition is allowed to depend on core, not the reverse, so a primitive shared by both must live in core. Both current callers (Beam's 2-point straight line, EnergyFlow's ~48-point Catmull-Rom) read as a genuine 3D energy field (2 cross planes), not a flat ribbon that looks squashed at an angle.

---

## Group 4: Element Parity Additions (Phase 1 — `vc_metal.inl` / `vc_fire.inl`)
- `VFX_ComposeMetalShardCluster(basePos, seed)`: a cluster of sharp metal shards sharing the same crystal-mesh system as ice but opaque/glossy/non-refractive (`CrystalMaterialParams`: `refraction=0`, `crack=0`, high `sparkle`).
- `VFX_ComposeBladeRing(pos, radius, bladeCount, rotationDeg)`: a ring of metal blades pointing outward around a center, using the existing `MAT_METAL` material.
- `VFX_ComposeFlameWisp(pos, time)`: a small flickering flame wisp, phase-offset by spawn position so multiple wisps don't flicker in sync.
- `VFX_ComposeFirePillar(basePos, progress)`: a fire pillar rising with `progress`, using the same smoothstep-rise formula as `VFX_ComposeStonePillar`.

---

## Group 4b: Element Skill Sets — per-element one-shot & continuous pieces
Each element has its own cluster of functions in `vc_<element>.inl`. Shared convention: a function with a `time` parameter = **continuous** (called every frame with an accumulating time, self-gates decal/light via probability — doesn't stack); a function with only a `scale` parameter = **one-shot** (called exactly once). Every function manages its own blend mode/depth mask.
- `VFX_ComposePlasmaOrb(pos, radius, time)` (`vc_plasma.inl`): a plasma energy orb — 1 cyan bloom sphere core (EffectMaterial translucency), 2 wispy noise membrane layers (PlasmaMaterial, see the Shader Material System) counter-scrolling + backface, and pink wisp trails (head particle + onLiveEmit tail, strong curl-noise) writhing inside. Continuous. dt-based spawn rate (~27 heads/s, tail 80/s × 0.25s) — pool of 2000, don't double the rate/lifetime without recomputing the budget.
- `VFX_ComposeLeafSwirl(pos, radius, time)` (`vc_wood.inl`): a whirl of leaves swirling around a point (vortex + updraft + curl), leaves = particles with size/emissive flickering along a "flutter" curve, plus pollen + moss decal + light. Continuous.
- `VFX_ComposeBloomBurst(pos, scale)` (`vc_wood.inl`): a one-shot bloom burst — a ring of petals bursting open and drifting down, pollen rising, a soft central flash, a root ring decal, a light. Call once.
- `VFX_ComposeLeafFall(pos, radius, time)` (`vc_wood.inl`): leaves drifting down within an area (light gravity + strong curl + viscosity to prevent straight falling), floating spores, moss decal. Continuous.
- `VFX_ComposeBladeStorm(pos, radius, time)` (`vc_metal.inl`): 7 metal blades (cone `MAT_METAL`) rotating around the caster on 2 counter-rotating rings, each blade with its own radius/height/rake offset via hash; silver streaks flying off the blade tips + catch-light glints. Continuous.
- `VFX_ComposeShrapnelBurst(pos, scale)` (`vc_metal.inl`): a one-shot metal shrapnel explosion — a fan of shards flying low near the ground (pitch -5°..45°, gravity 7), hero streaks trailing tails, a flash + glint at the blast center, a crater decal (only when `pos.y` is near the ground), distort + a cold-toned light. Call once.
- `VFX_ComposeRicochetSpark(pos, dir, scale)` (`vc_metal.inl`): a one-shot fan of parry/deflect sparks fired along `dir` (~35° cone, dies within ~0.3s) + a micro-flash "ping." Call once when blocking a hit/deflecting a projectile.
- `VFX_ComposeSplashBurst(pos, scale)` (`vc_water.inl`): a one-shot water crown — a ring of droplets shot at 55°-75° falling back under real 9.8 gravity, a central jet, a mist puff, 2 expanding ring decals at different speeds (fast splash + slow ripple), a light + light distort. Call once when a water projectile hits/lands on the ground.
- `VFX_ComposeBubbleStream(pos, radius, time)` (`vc_water.inl`): rising bubbles (buoyancy + curl wobble + viscosity), each bubble dies into 3 micro-droplets (`onDeathEmit`), a caustic light shimmer + a ripple ground decal. Continuous.
- `VFX_ComposeMistVeil(pos, radius, time)` (`vc_water.inl`): a low mist veil crawling around an area (slow vortex + curl + strong viscosity — mist crawls rather than blows), plus sparse falling condensation droplets + a cold moonlit sheen. Continuous.
- `VFX_ComposeGustSlash(pos, dir, scale)` (`vc_taiji.inl`): a one-shot wind-blade slash along `dir` — a flat ~80° streak fan (blade shape, not a cone), dust kicked sideways, a wind-groove decal rotated to match the slash direction, distort tearing the air. Call once.
- `VFX_ComposeCyclone(pos, radius, time)` (`vc_taiji.inl`): a tornado column — spawn arms rotating around the base to form a spiraling funnel band (the FirePillar trick), vortex 6.0 + updraft + a gravity-point holding the funnel tight, a gray dust skirt + debris flying high + a distort column. Continuous.
- `VFX_ComposeStaticField(pos, radius, time)` (`vc_taiji.inl`): a static-electricity field — 3-4 purple/white micro-arcs crawling over a sphere of radius `radius`, re-seeded every ~0.09s (a crackle rhythm, not per-frame white noise), sparks firing off anchor points + a jittery purple light. Continuous. Uses `DrawLightningBoltEx` + the global `camera`.
- `VFX_ComposeYinYangOrbit(pos, radius, time)` (`vc_taiji.inl`): a pair of yin-yang orbs chasing each other on a tilted, antiphase-bobbing ring — yang is additive glowing white, yin is BLEND_ALPHA black with a purple Fresnel edge (additive can't draw darkness), crossing trails of motes chasing each other, a rotating taiji mandala ring on the ground. Continuous.
- `VFX_ComposeRockBurst(pos, scale)` (`vc_earth.inl`): a one-shot burst of rock debris — brown chunks flying at 20°-70° falling under real 9.8 gravity, bright sand particles catching light, a dust cloud that expands slowly and outlives the chunks, a 3.5s stone-shatter decal, `CameraFX_Shake` + distort (earth = felt, not glowing — the light is very dim). Call once.
- `VFX_ComposeFloatingStones(pos, radius, time)` (`vc_earth.inl`): 5 real rock meshes (`MeshCache_GetRock`, fixed seed) hovering around the caster, slow orbit/bob/tumble (mass conveyed through inertia), sand dust flying UPWARD beneath each stone (an anti-gravity cue) + an occasional pebble falling straight down. Continuous.
- `VFX_ComposeQuakeRumble(pos, radius, time)` (`vc_earth.inl`): a seismic zone — gravel hopping off the ground and falling back, dust randomly puffing up, new cracks stamping in gradually as the ground shakes, `CameraFX_Shake(0.05)` continuously at low probability. Continuous.
- `VFX_ComposeFlameBreath(pos, dir, scale, time)` (`vc_fire.inl`): a continuous fire breath jetting along `dir` — FireFlow packets fired into a ~14° cone with a surging rhythm (not a steady jet), buoyancy already built into `s_flameFld` naturally curves the stream's tail upward, smoke at the tip where it fades out, shimmer + a muzzle light.
- `VFX_ComposeBurningGround(pos, radius, time)` (`vc_fire.inl`): a continuous patch of burning ground — flame tongues growing at random points weighted toward the center, a pulsing lava-crack decal beneath the fire + accumulating scorch, rising embers + smoke, a flickering firelight.
- `VFX_ComposeFireWhirl(pos, radius, time)` (`vc_fire.inl`): a continuous fire tornado — vortex 7.0 + updraft 3.0, spawn arms rotating to form a spiraling flame band climbing the funnel, a white-hot jet along the central axis, embers flying outward centrifugally, a smoke crown at the top, strong distort + light (ultimate-tier).
- `VFX_ComposeElementalMist(VC_MaterialId, pos, radius, time)` (`vc_elemental_mist.inl`): dry-ice-style mist continuously radiating from a point — 3 layers: fog body (large particles 0.08–0.15m, mat→soft alpha 50, hugging the ground Y<0.05m), wisp tendrils (curl noise, concentrated near the center), sublimation breath (small vapor right at the source point). Force field rebuilt every frame: radial outward push (negative GRAVITY_POINT) + curl noise + a light downward gravity + heavy viscosity (fog creeps rather than floats). Color/glow taken from `VFX_Material(matId)->soft/glow`; changing the element only changes the palette, the shape stays the same.

---

## Group 5: Phase 3 Archetypes — shield/zone/slash
Uses the same `(style, pos, ..., progress, time)` parameter convention as the Group 2 archetypes (`VFX_ComposeBeam`, `VFX_GroundPattern`...), so an AI building a new skill can predict a function's signature:
- `VFX_ComposeShield(VC_MaterialId, pos, radius, progress, time)` (`vc_shield.inl`): a barrier/dome — scale-in 0..0.3, hold, fade-out at 0.85..1.0 (call continuously every frame while the shield exists). A sphere sunk halfway into the ground creates a dome effect without needing a separate hemisphere mesh, plus a rotating rune ring at the base (the material's `runeDecal`) + random surface glints.
- `VFX_ComposeZone(VC_MaterialId, pos, radius, progress, time)` (`vc_zone.inl`): a long-lived AoE zone — reuses `VFX_GroundPattern` for the ground (pattern chosen by material: FIRE→LAVA, ICE→FROST, WOOD→THORNS, VOID→RUNE, EARTH/METAL/POISON→CRACK, everything else→MAGIC_CIRCLE) + particles/light scattered probabilistically each call, called every frame for the zone's whole active lifetime.
- `VFX_ComposeSlashArc(VC_MaterialId, pos, dir, radius, arcDegrees, progress, time)` (`vc_slash.inl`): a melee slash streak as a curved ribbon, thin at the tail/thick at the sweep's peak, only showing the arc portion swept up to `progress`; a glint at the actively-slashing edge. Color = `body`.

> **Removed 2026-07-10** (per user request, `vc_chain.inl`/`vc_charge.inl` deleted from disk, not a bug — real files, all related includes/declarations/manifest entries cleaned up): `VFX_ComposeChain` (the chain/bounce archetype — `VFX_ChainLightning` in `vc_archetype.inl` replaces it for chain lightning; no direct replacement for chaining other elements) and `VFX_ComposeChargeUp` (the charge/channel archetype — no replacement primitive; compose it by hand from `VC_MotionSpiralIn` + a growing core sphere + `VFX_ComposeGlintBurst`, see §0 "Charge (pre-ultimate)" above).

All of Groups 3-5 are already wired into the **"NEW FX"** tab in `sandbox/vfx_test.c` for visual preview — no need to write an actual skill to see them. Run `python3 scripts/sync_vfx_test.py` after adding/removing a `VFX_Compose*` to keep the tab in sync.

---

## `.inl` include order (in `visual_composer.c`)
```
vc_common.inl     — render primitives (VC_DrawGroundQuadXZ, VC_DrawGroundRune)
vc_beauty.inl     — beauty primitives — MUST precede element .inl (vc_zone calls GlintBurst)
vc_preset.inl     — preset-driven: SmokePuff, SmokeTrail, LightningBolt, Impact, Cast, ProjectileTrail
vc_metal.inl / vc_wood.inl / vc_water.inl / vc_fire.inl / vc_earth.inl / vc_plasma.inl / vc_taiji.inl
vc_projectile.inl / vc_ground.inl / vc_beam.inl / vc_path.inl / vc_summon.inl / vc_explosion.inl / vc_aura.inl / vc_cylinder_aura.inl
vc_shield.inl / vc_zone.inl / vc_slash.inl
vc_elemental_mist.inl
vc_ground_aura.inl
vc_black_hole.inl
```

## Sync script
`scripts/vfx_test_manifest.json` is the source of truth for the NEWFX tab.
Run `python3 scripts/sync_vfx_test.py` after every `VFX_Compose*` add/remove.
`--check` flag for dry-run (no writes).
