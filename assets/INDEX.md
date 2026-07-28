# Asset Index

Catalog of all runtime assets under `assets/`. Skill authors may only reference
textures listed here (or ship their own PNG inside the skill directory).
See `skills/docs/RECIPE.md` for the per-element preset table that cross-references
these files.

> **Rule:** never invent an asset path. Use the exact filenames below, or add
> a skill-local PNG to the skill directory.

---

## Textures — Decals (`assets/textures/decals/`)

Used via `SpawnGroundDecal(DECAL_PRESET_*)` / `DecalSystem_Add*`. All are
white/greyscale with alpha so `Color tint` fully controls hue.

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
| `water_flow.png` | Flow-map shaders (`core/flow_map.h`) | Scrolling direction field for water surface | greyscale vectors |
| `dust_wind.png` | Emitter / particle texture | Soft wispy dust/smoke puff | white/tintable |
| `flare.png` | VFX lens-flare, light glow | Star-burst flare with halo ring | white/tintable |
| `noise.png` | Shader noise sampling (`core/shaders/common/noise.glsl`) | Tileable blue-noise / Perlin field | greyscale |

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

## fire_puff_8x8.png + _flame / _smoke (Đợt E / E4 — flipbook) — IN USE

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

1024×1024 RGBA, 4×4 = 16 frames. Simulated at the full 64 frames and subsampled
with `pack.py --stride 4`: `--frames` is a PHYSICS axis in this solver (dt is
per frame), so simulating 16 frames would have produced an earlier MOMENT of the
event, not a coarser sampling of it.

Impact dust, so two terms no other preset uses: the radial push is squashed
vertically (`flat 0.20` → measured height/width **0.87**, wider than tall, the
shape that reads as "something hit the floor") and a small `gravity` on density.
Gravity is deliberately small — at 4.0 the dust piled onto the domain floor and
the boundary clamp manufactured 16% of the mass there; the FALL belongs to the
particle, the sheet only carries the internal asymmetry.

No `_flame` split: the channel is spurious for a cold effect and `pack.py` skips
writing it. Audited: coverage 18.1%, lobes 1.02, 0/16 clipped, wall-shell 0.0%,
value spread 0.73.

**Consumed by** nothing yet — intended for `VFX_ComposeImpactPackage` (E6 #6),
footfalls, landings and Earth skills. Falls back to the static smoke sprites
wherever a consumer follows the `vc_smoke_puff.inl` pattern.

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
