# Asset Index

Catalog of all runtime assets under `assets/`. Skill authors may only reference
textures listed here (or ship their own PNG inside the skill directory).
See `skills/docs/RECIPE.md` for the per-element preset table that cross-references
these files.

> **Rule:** never invent an asset path. Use the exact filenames below, or add
> a skill-local PNG to the skill directory.

> **Packing:** what each RGBA channel of a VFX sheet may carry is not a matter
> of taste — see **`assets/TEXTURE_PACKING.md`**. It is enforced by
> `scripts/validate_vfx_surface_registry.py`, which runs at CMake configure
> time and fails the build. `smoke_strand.png` is the reference sheet.

---

## Textures — Decals (`assets/textures/decals/`)

Legacy catalog only. New primary work must follow [DECAL_REWORK.md](DECAL_REWORK.md)
and select a semantic surface profile; do not add new filename-based decal use.

| File | DECAL_PRESET constant | Visual description |
|---|---|---|
| `decal_earth_rune.png` | `DECAL_PRESET_EARTH_RUNE` | Circular earth-element glyph, faint carved look |
| `decal_frost_ring.png` | `DECAL_PRESET_ICE` | Concentric frost/ice hexagonal pattern |
| `decal_impact_crater.png` | `DECAL_PRESET_METAL_CRATER` | Radial impact lines + central star, metal shatter |
| `decal_lava_crack.png` | `DECAL_PRESET_FIRE_LAVA` | Radiating crack lines glowing inward |
| `decal_lightning_char.png` | `DECAL_PRESET_TAIJI_LIGHTNING` | Irregular scorch/lightning char pattern |
| `decal_metal_rune.png` | `DECAL_PRESET_METAL_RUNE` | Geometric metal-element inscription |
| `decal_moss_stain.png` | `DECAL_PRESET_WOOD_MOSS` | Soft organic moss/lichen blot |
| `decal_root_mark.png` | `DECAL_PRESET_WOOD_ROOT` | Branching root/vine imprint |
| `decal_slash_mark.png` | `DECAL_PRESET_METAL_SLASH` | Single diagonal blade cut with chip marks |
| `decal_splash_ring.png` | `DECAL_PRESET_WATER_SPLASH` | Circular water splash/ripple burst |
| `decal_stone_shatter.png` | `DECAL_PRESET_EARTH_SHATTER` | Fractured stone shatter radial pattern |
| `decal_taiji_ring.png` | `DECAL_PRESET_TAIJI_RING` | Yin-yang ring with eight-trigram hints |
| `decal_water_ripple.png` | `DECAL_PRESET_WATER_RIPPLE` | Concentric expanding water ripple |
| `decal_wind_groove.png` | `DECAL_PRESET_TAIJI_WIND` | Parallel curved wind-groove streaks |
| `test.png` | — | Development test; do not reference in shipping skills |

---

## Textures — Generic (`assets/textures/generic/`)

Used via `SpawnGroundDecal(DECAL_PRESET_GENERIC_*)`. White/tintable.

| File | DECAL_PRESET constant | Visual description |
|---|---|---|
| `glow_circle.png` | `DECAL_PRESET_GENERIC_GLOW` | Soft radial glow blob, white center fade |
| `impact_ring.png` | `DECAL_PRESET_GENERIC_IMPACT_RING` | Sharp ring with inner fade, impact stamp |
| `shadow_blob.png` | `DECAL_PRESET_GENERIC_SHADOW` | Soft elliptical shadow, fully opaque center |

---

## Textures — Root (`assets/textures/`)

| File | Used by | Visual description | Tint |
|---|---|---|---|
| `crack.png` | `DECAL_PRESET_CRACK` | Simple branching crack, symmetric | white/tintable |
| `scorch_mark.png` | `DECAL_PRESET_BURN` | Circular burn/scorch with charred edges | white/tintable |
| `water_caustics.png` | `DECAL_PRESET_WATER` | Animated caustic light pattern | white/tintable |
| `water_flow.png` | Flow-map shaders (`core/uv/flow_map.h`) | Scrolling direction field for water surface | greyscale vectors |
| `dust_wind.png` | Emitter / particle texture | Soft wispy dust/smoke puff | white/tintable |
| `flare.png` | `VFX_EmberTrail`, VFX light glow | Soft radial white falloff; the EmberTrail core comes from white additive vertex colour | white/additive bloom |
| `noise.png` | Shader noise sampling (`core/shaders/common/noise.glsl`) | Tileable blue-noise / Perlin field | greyscale |
| `energy_volume.png` | Shield tester flow-surface body; `VolumeTrail` energy body | Tileable white energy filaments; use as a display/body sheet, never decode as flow | white/tintable |
| `energy_volume_flow.png` | Shield tester flow-surface map; `VolumeTrail` energy flow | Tileable RG direction field paired with `energy_volume.png`; repeat + bilinear | data, RG direction |
| `energy_flow.png` | `VFX_SurfaceRegistry` EnergyRibbon profile | Legacy filament sheet; crop/rotate/crossfade preprocessing is recorded in the profile before repeat use | white/additive |
| `smoke_ribbon.png` | `VFX_SurfaceRegistry` SmokeRibbon body | Lit smoke body with opacity; repeat only through the semantic profile | pre-lit RGBA |
| `smoke_ribbon_flow.png` | `VFX_SurfaceRegistry` SmokeRibbon flow | RG signed UV direction field paired with smoke ribbon | data, RG direction |
| `smoke_ribbon_mask.png` | `VFX_SurfaceRegistry` SmokeRibbon mask | R-only erosion field; never decode as a flow vector | data, R scalar |
| `energy_wisp.png` | `VFX_SurfaceRegistry` EnergyRibbon body | **STRAND packed sheet** — R/G repeating filament patterns, B distortion, A dissolve; tiles by metres (MATERIAL). Built by `scripts/gen_energy_wisp_texture.py` | data, 4-channel packed |
| `smoke_strand.png` | `VFX_SurfaceRegistry` SmokeStrand body | **STRAND packed sheet, the reference for `assets/TEXTURE_PACKING.md`** — R/G one complete wisp each with taper baked in (SHAPE, stretched once), B distortion + A dissolve seamless. Built by `scripts/gen_smoke_strand_texture.py` | data, 4-channel packed |
| `impact_shockwave_smoke.png` | `VFX_SurfaceRegistry` ImpactSmoke body | **OPAQUE polar strip** — torn thin-smoke silhouette maps exactly once around the free-space impact disc; RGB neutral, A is true density. Built by `scripts/gen_impact_shockwave_texture.py` from the committed authored source. | neutral RGBA / alpha coverage |
| `surfaces/frost_decal_v1.png` | `VFX_SurfaceRegistry` DecalFrost preview body | Neutral ice albedo with true alpha; bright crack lines feed generic emissive extraction; visual-review only | RGBA ice/crack/opacity |
| `volume_surface_smoke.png` | `VFX_SurfaceRegistry` VolumeSmoke body | **OPAQUE MATERIAL** — grey luminance in RGB (tint comes from VFX_Material), real coverage in A; tiles around and along a deformed volume. Built by `scripts/gen_volume_surface.py` | data, 4-channel packed |
| `volume_surface_fire.png` | `VFX_SurfaceRegistry` VolumeFire body | As smoke, tighter and higher contrast — flame tongues | data, 4-channel packed |
| `volume_surface_steam.png` | `VFX_SurfaceRegistry` VolumeSteam body | As smoke, wide and low contrast — near translucent | data, 4-channel packed |
| `volume_noise.png` | `core/deform/mesh_deform.h` displacement (via `TubeMeshConfig.noisePixels`) | **NOISE layout** — four decorrelated scalar fields, pure data, never drawn. Seamless both axes | data, greyscale fields |
| `surfaces/scorch_material_v1.png` | `VFX_SurfaceRegistry` DecalScorch preview body | ImageGen-charcoal scorch material with chroma-key-derived organic alpha; visual-review only | RGBA charcoal/ember/opacity |
| `surfaces/impact_material_v2.png` | `VFX_SurfaceRegistry` DecalImpact preview body | ImageGen element-neutral fracture mark with chroma-key-derived alpha; grayscale plates and near-white fissure cores accept independent runtime body/emissive tinting | RGBA grayscale/cracks/opacity |
| `smoke_volume.png` | `VFX_SurfaceRegistry` SmokeTube preview profile | Seamless smoke tube body; preview-only until P3 approval | pre-lit RGBA |
| `smoke_volume_flow.png` | `VFX_SurfaceRegistry` SmokeTube preview profile | Tileable RG direction field | data, RG direction |
| `fire_volume.png` | `VFX_SurfaceRegistry` FireTube preview profile | Seamless fire tube body; preview-only until P3 approval | emissive RGBA |
| `fire_volume_flow.png` | `VFX_SurfaceRegistry` FireTube preview profile | Tileable RG direction field | data, RG direction |
| `fire_atlas_8x8_flame.png` | FireTongue fallback profile | Legacy 8×8 flame-only column fallback | white flame + alpha |
| `fire_tongue_8x8_flame.png` | `VFX_SurfaceRegistry` FireTongue body | 8×8 directional +Z flame-column flipbook; white emission mask with a soft alpha rim | white flame + alpha |

### P1 semantic-surface migration map

No files are moved in this phase. New or refactored composition code asks for a
semantic profile; the registry owns the runtime path and sampler contract.

| Profile | Primitive | Current consumer | Migration state |
|---|---|---|---|
| SmokeRibbon | ribbon | *(none — consumer deleted 03/08/2026)* | orphaned |
| EnergyRibbon | ribbon | `VFX_ComposeRibbonTrail` | migrated; retains one-time crop/rotate bake |
| EnergyTube | tube | `VFX_ComposeVolumeTrail` | migrated, shipping |
| SmokePuff | puff/card | `VFX_ComposeSmokePuff`, `SmokeEmitter` | migrated |
| FireTongue | alpha tongue | `VFX_FlameEmitter` | migrated |

### P4 decal semantic migration map — visual-owner gate

No decal file is moved or selected by filename in this phase. The canonical
registry has blocked material contracts only; their runtime paths remain empty
until the visual owner approves authored source art.

| Profile | Role / projection | Legacy assessment | State |
|---|---|---|---|
| DecalResidue | conformal mesh stamp, alpha / edge erosion | all legacy decals are rejected globally; wait for newly authored residue material | blocked, no fallback |
| DecalScorch | conformal mesh stamp, alpha charcoal + additive ember ramp | `scorch_material_v1.png` is a replaceable preview primary; all legacy decals are rejected globally | visual primary; no shipping fallback |
| DecalImpact | conformal mesh stamp, alpha / edge erosion | `impact_material_v2.png` is the element-neutral glowing-fracture preview primary; all legacy decals are rejected globally | preview primary; no shipping fallback |
| DecalRune | conformal mesh stamp, alpha / glyph boundary | all legacy decals are rejected globally; wait for a newly authored glyph source | blocked, no fallback |
| SmokeTube / FireTube | tube | `VFX_ComposeVolumeTrail` | registry-only preview; spawn remains blocked pending P3 visual approval |

---

## Models (`assets/models/`)

| File | Used by | Description |
|---|---|---|
| `bamboo.glb` | Wood-element map decoration | Animated bamboo grove segment |

---

## Fonts (`assets/fonts/`)

| File | Used by | Description |
|---|---|---|
| `ui_font.ttf` | `sandbox/ui_panel.c` | UI panel labels and tunable sliders |

---

## Sounds (`assets/sounds/`)

Directory does not yet exist — pending Item 25 (user-sourced .ogg files).
Required files when item is complete:

| File | Preset | Stage |
|---|---|---|
| `water_cast.ogg` | `EFFECT_PRESET_WATER_SPLASH` | Cast windup |
| `water_impact.ogg` | `EFFECT_PRESET_WATER_SPLASH` | Impact |
| `wood_cast.ogg` | `EFFECT_PRESET_WOOD_BLOOM` | Cast windup |
| `wood_impact.ogg` | `EFFECT_PRESET_WOOD_BLOOM` | Impact |
| `fire_cast.ogg` | `EFFECT_PRESET_FIRE_EXPLOSION` | Cast windup |
| `fire_impact.ogg` | `EFFECT_PRESET_FIRE_EXPLOSION` | Impact |
| `earth_cast.ogg` | `EFFECT_PRESET_EARTH_CRACK` | Cast windup |
| `earth_impact.ogg` | `EFFECT_PRESET_EARTH_CRACK` | Impact |
| `metal_cast.ogg` | `EFFECT_PRESET_METAL_SHARD` | Cast windup |
| `metal_impact.ogg` | `EFFECT_PRESET_METAL_SHARD` | Impact |
| `taiji_cast.ogg` | `EFFECT_PRESET_TAIJI_BURST` | Cast windup |
| `taiji_impact.ogg` | `EFFECT_PRESET_TAIJI_BURST` | Impact |

## smoke_puff_01..03.png (Đợt E / F2)
Lobed-silhouette smoke/dust sprites, generated — not hand-painted:
`python3 scripts/generate_smoke_sprite.py assets/textures/smoke_puff_0N.png 256 <seed> 14`
White RGB, shape in ALPHA (the particle system tints per particle). Three
variants because one sprite repeated 28 times reads as stamps however much each
is rotated. Replaces the stock radial gradient, which has no outline at all —
see ELDEN_VFX_SPEC.md §0.1b cause 3.

## fire_puff_8x8.png + fire_puff_8x8_flame.png / _smoke (Đợt E / E4 — flipbook) — IN USE

2048×2048 RGBA, 8×8 = 64 frames of one simulated FIRE puff. Produced by
`scripts/flipbook/` (`ti_sim.py fire_puff --res 112 --frames 64` → `render.py
--density-scale 7 --zoom auto` → `pack.py --split --shape puff`).

Channels in the packed sheet: R flame · G smoke · B self-shadow · A true
opacity. The engine binds the SPLITS, not this file: `_flame` is white RGB +
alpha (fire EMITS, so it is never shadowed and its colour comes from the
black-body ramp at the call site).

Audited: cell coverage 22.3%, lobes 1.14, 0/64 frames clipped, sim wall-shell
mass 0.1%. Re-audit any time with
`python3 scripts/flipbook/pack.py assets/textures/fire_puff_8x8.png --grid 8 --shape puff`.

**Consumed by** `VFX_ComposeFlameVolume` (`core/composition/fire/flame_volume.inl`),
body layer, via `ParticleConfig.spriteAnim`. `tuning.cfg → flame_atlas`:
0 = the F2 static sprites, 1 = this puff (default), 2 = `fire_atlas_8x8` (column).

## fire_puff_8x8_volume.png (Đợt H — packed VOLUME sheet) — IN USE

2048×2048 RGBA, 8×8 = 64 frames. Same simulation as the sheet above, but
**never split**: this is the four-channel `VOLUME` layout delivered to the
engine intact.

The committed texture uses the directionless `fire_volume_puff` preset below.

```bash
python3 scripts/flipbook/ti_sim.py fire_volume_puff --res 96 --frames 64 --name fire_volume_puff
python3 scripts/flipbook/render.py build_cache/fire_volume_puff --cell 256 --supersample 2 --zoom auto --light 1.0 --density-scale 6 --flame-extinction 1.5 --flame-scale 4.5
python3 scripts/flipbook/pack.py build_cache/fire_volume_puff/frames --grid 8 --alpha-from-luma 0 --shape puff --out fire_puff_8x8_volume.png --meta-out core/composition/fire/flame_volume_puff_metadata.inl --meta-symbol s_fvolVolumeFrameMeta
```

R emission · G smoke density · B self-shadow · A true opacity — **no colour in
any channel**. Colour comes from a ramp LUT (`ColorGradient_BakeLUT`) indexed by
R at draw time, which is the point: the same greyscale sheet is orange fire,
purple magic fire or blue ghost-flame with no re-bake, and one sprite carries a
white-hot core AND a dark rim, which the `_flame` split above physically cannot
(its RGB is a flat 255/255/255 mask, so one vertex colour tints the whole quad).

── THE SINGLE SPRITE HAS TO BE RIGHT FIRST ──

Found by setting `flame_body_live = 1` and looking at ONE particle: the sprite
was a hard-edged lumpy cutout, and no number of them stacked can read as gas.

Softness is measured as band area / covered area (0.05 < A < 0.95 over A > 0.05).
NOT as a percentage of the cell — that is not scale-invariant, a bigger puff has
a bigger band at identical softness, and an earlier pass of this work reported a
~3x improvement that was mostly the puff getting bigger.

  viscosity 0.22 (original)   0.421     wall clean
  curl 7.5, viscosity 0.06    0.506     ~6% wall shell  <- rejected
  eddy 58,  viscosity 0.06    0.560     0.0% wall shell <- prior fire_eddy bake
  fire_volume_puff             1.23 lobes, 0/64 clipped       <- current bake

WISPS COME FROM `--eddy`, NOT `--curl`. The script's own --eddy help said so
before any of this started: "curl sets how hard the field stirs, --eddy sets at
what size. Raising curl to get more billows instead just transports the puff
further and runs it into the wall." Raising curl to 7.5 did exactly that — 90th
percentile reach 1.26-1.30 of the domain half-width, boundary clamp creating
mass, and the silhouette partly the box. eddy is a spatial frequency (turbulence
cells across the domain) and transports nothing: reach fell to 0.59.

FOUR MEASURED DEAD ENDS on the wall contact, none of which is the lever:
  --density-scale 9 -> 1.5      band 6.9% -> 9.0% of cell, interior still 0.92
  --radial        8.0 -> 3.0    reach 1.28 -> 1.30
  --fuel-radius   0.05 -> 0.028 reach 1.28 -> 1.26
  --domain        1.0 -> 1.7    reach 1.28 -> 1.23
  --dt            0.9 -> 0.5    coverage 61% -> 50%, but band collapses to 4.9%
The last two exist because of this hunt: --fuel-radius and --domain were added
to ti_sim.py to fix it and did not, which is worth knowing before reaching for
them.

`--density-scale 6` and `--flame-extinction 1.5` keep it TRANSLUCENT rather than
a saturated plate: the default scale drives the
puff's interior into a plateau, which reads as slabs of flat colour. Measured
alpha mean fell 0.77 -> 0.71 and hot texels rose 3.8% -> 4.5% across that change.

Two knobs measured and REJECTED, recorded so they are not retried:
`--fuel-dens` 1.0 -> 0.22 moved the flame/smoke split only 3.5% -> 3.3%, and
dropping the sprite count 90 -> 26 made the effect WORSE, not lighter — each
sprite's own silhouette becomes legible and the fire reads as stacked cards.

What parameters CANNOT reach: `fire_puff` is a radially symmetric BALL by
construction (gravity 0, buoyancy 0, flat 1.0), so no bake setting turns it into
a torch tongue. That needs the `fire` COLUMN preset baked as a second VOLUME
sheet — a separate asset, not a knob.

Measured: R mean 0.068 / sd 0.159 (a real temperature field), G mean 0.149,
B/G 0.643 mean surviving-light fraction. Audited: cell coverage 28.2%, flame
coverage 10.7%, smoke coverage 19.8%, lobes 1.23, 0/64 frames touch the border.

**Consumed by** `VFX_FlameEmitter` (`core/composition/fire/flame_volume.inl`)
when `tuning.cfg → flame_volume = 1` (the default), through
`particle_lit.fs`'s volume branch with `VFX_BLEND_PREMULTIPLIED`.
The same pack command regenerates its per-frame crop metadata: Core samples
only occupied texels while preserving the original cell pivot, reducing
transparent fill-rate without changing the directionless asset.

## smoke_puff_8x8.png + _flame / _smoke (Đợt E / E4 — flipbook) — IN USE

2048×2048 RGBA, 8×8 = 64 frames of one simulated SMOKE puff (`ti_sim.py
smoke_puff`, `render.py --density-scale 3 --light 1.0 --zoom auto`).

The `_smoke` split is what the engine binds, and it is **not** a flat mask: its
RGB carries the volume's own self-shadow (B/G — the fraction of light that
reached each pixel), alpha carries the density. Measured internal value spread
p10..p90 = 0.69. An unshaded mask measures 0.00 and stacks into flat overlapping
cards, which is what "những mảng màu riêng biệt" was — the lighting pass shades
a BILLBOARD and cannot see inside the puff.

Audited: coverage 22.5%, lobes 1.08, 0/64 clipped, wall-shell 0.0%.

**Consumed by** `VFX_ComposeSmokePuff` (`core/composition/common/vc_smoke_puff.inl`).
The call site LIFTS the vertex colour to ~160 so a pre-shaded sheet is only
tinted; feeding a flat mask down that path (or this sheet down the near-black
gradient path used for the static sprites) is a measured 33/255 black smudge.

## dust_puff_4x4.png + _smoke (Đợt E / E4 — flipbook)

Rebuild through the same Taichi smoke/fire pipeline:
`python3 scripts/gen_dust_flipbook.py`. It simulates the full 64-frame event;
the 8×8 master keeps every frame and the 4×4 runtime sheet samples its complete
timeline at a stride of four. `--frames` is a PHYSICS axis, not a coarser
sampling knob.

Current rebuild contract: the script packs the same 64-frame simulation twice.
`dust_puff_8x8.png` is the full temporal master; `dust_puff_4x4.png` samples
frames 1, 5, 9 … 61 across that same arc. A 4×4 runtime sheet must never be
made from the first sixteen near-identical physics frames.

The old sheet is pending replacement. The new bake keeps the parcel isotropic;
impact flattening/fall belong to the particle placement and force field, not to
every card inside the cloud.

No `_flame` split: the channel is spurious for a cold effect and `pack.py` skips
writing it. Audited: coverage 18.1%, lobes 1.02, 0/16 clipped, wall-shell 0.0%,
value spread 0.73.

**Consumed by** nothing yet — intended for `VFX_ComposeImpactPackage` (E6 #6),
footfalls, landings and Earth skills. Falls back to the static smoke sprites
wherever a consumer follows the `vc_smoke_puff.inl` pattern.

**Visual review pending (31/07/2026):** `ImpactDust` currently uses the
occluding alpha contract and a lifted off-white tint. Owner may prefer an
additive stylisation; keep this as an explicit A/B decision, not an accidental
blend change while tuning the atlas.

## smoke_atlas_8x8.png (Đợt E / E4 — flipbook) — SUPERSEDED, kept as fallback only

2048×2048 RGBA, an 8×8 grid of 64 frames: one simulated smoke puff over its full
life (birth → billow → dissipate). Generated by `scripts/gen_smoke_flipbook.py`
(Blender, headless). Rejected by the owner 28/07/2026 and replaced by `smoke_puff_8x8_smoke.png`.
Re-audited, the reasons are measurable: R and G identical at 21.0% (one
greyscale channel tripled — it predates the R/G/B/A layout entirely, so it
cannot express "thick but cool" apart from "hot"), silhouette shredded rather
than billowed (lobes 2.31 against 1.08), framed 1.31x smaller than its cell, and
internal value spread only 0.31. The generating script has been deleted.

**Consumed by** `VFX_ComposeSmokePuff` (`core/composition/common/vc_smoke_puff.inl`)
via `ParticleConfig.spriteAnim`. Falls back to the three static `smoke_puff_0*.png`
silhouettes if the file is missing (and logs that it did). `tuning.cfg →
smokepuff_flipbook = 0` forces the static path for an A/B.

Notes for anyone regenerating it:
- **Straight (non-premultiplied) alpha**, which is correct here — the engine
  draws smoke with `BLEND_ALPHA`. The E4 spec's "premultiplied" line does not
  match this pipeline; do not "fix" it without changing the blend too.
- **Frame 0 is empty and frames 62-63 are empty.** That is harmless: the puff's
  own alpha curve fades in from 0 and out to 0 over the same span, so the blank
  head and tail coincide with fully-transparent particles. It does waste ~5% of
  the sheet.
- **RGB carries baked shading** (mean ~121/255 inside the puff), i.e. it is a
  `*_lit` sheet. The consumer therefore lifts the vertex colour to a near-neutral
  tint instead of the near-black gradient the flat sprites use — multiplying the
  dark gradient by the shaded sheet lands at ~33/255 and reads as a black smudge.
