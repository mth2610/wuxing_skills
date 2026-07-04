# Asset Index

Catalog of all runtime assets under `assets/`. Skill authors may only reference
textures listed here (or ship their own PNG inside the skill directory).
See `SKILL_RECIPE.md` for the per-element preset table that cross-references
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
