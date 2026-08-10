# Texture packing — the hard rules

Normative. A VFX sheet that does not conform is rejected by
`scripts/validate_vfx_surface_registry.py`, which runs at CMake configure time
and fails the build. Prose here explains *why*; the machine-checkable part is
the channel grammar in §2.

Scope: every texture registered in `assets/vfx_surface_profiles.json`. Map/PBR
material textures (`rock_*`, `stone_path_*`, `grass_ground_*`) are outside it —
they are conventional albedo/normal/roughness sets and belong to the Map Agent.

## 1. Why pack at all

One RGBA sheet is one bind, one asset, one cache line. The alternative — body +
flow map + mask as three files — costs three binds per surface and lets the
three drift out of sync with each other. `smoke_strand.png` is the reference:
it replaced a body/flow/mask trio with a single 4-channel file.

Packing is not free of consequence, and the rest of this document is the price.

## 2. The channel grammar (machine-checked)

Every `assets.<role>.channels` string in the manifest MUST begin with:

```
<LAYOUT> | R:<slot>/<mode> | G:<slot>/<mode> | B:<slot>/<mode> | A:<slot>/<mode>
```

optionally followed by ` — <free prose>`. All four channels must be declared,
in R G B A order, even when a channel is unused.

### Layouts and their permitted slots

| LAYOUT | R | G | B | A |
|---|---|---|---|---|
| `STRAND` | `pattern1` | `pattern2` | `distort` | `dissolve` |
| `FLOW` | `body` | `mask` or `dissolve` | `flowx` | `flowy` |
| `OPAQUE` | `color` | `color` | `color` | `opacity` |
| `FLIPBOOK` | `color` | `color` | `color` | `opacity` |
| `VOLUME` | `emission` | `density` | `shadow` | `opacity` |
| `NOISE` | `field` | `field` | `field` | `field` |

`STRAND` is the trail sheet read by `core/trails/shaders/trail_deform.fs` mode 2.
`FLOW` is a body that carries its own flow vector — the layout that collapses a
body+flow(+mask) group into one file. `OPAQUE` is a conventional colour sheet.
`FLIPBOOK` is `OPAQUE` with cells, and every channel must be `CLAMP`.

`VOLUME` is the ray-marched sheet from `scripts/flipbook/` and it is the one
layout that carries **no colour at all** — four scalar fields describing a gas:
`emission` is how much light the texel radiates, `density` how much soot it
holds, `shadow` the same density weighted by the light that reached it (so
`shadow/density` is the surviving light fraction), and `opacity` the true
`1 - transmittance`. Colour arrives at draw time from a ramp LUT indexed by
`emission`, which is what lets ONE sheet be orange fire, purple magic fire or
blue ghost-flame without a re-bake — and what lets a single sprite hold a
white-hot core and a dark rim at once, which a `FLIPBOOK` tinted by one vertex
colour cannot. Read by `core/particles/shaders/particle_lit.fs` (volume branch);
its output is premultiplied, so the consumer must use `VFX_BLEND_PREMULTIPLIED`.

`NOISE`
is a pure DATA sheet: four independent scalar fields, decorrelated by
construction, so one fetch feeds several layers of a displacement or a warp.
It carries no colour and no coverage — never draw it.

### Modes

| Mode | Meaning | Authoring requirement |
|---|---|---|
| `TILE` | the consumer pans and wraps this channel | seamless on **both** axes |
| `STRETCH` | maps across the surface exactly once | must fade to 0 at **all four** edges, and must never be tiled |
| `CLAMP` | flipbook cell or decal | never wrapped |

## 3. The rules

### R1 — `STRETCH` and `TILE` cannot be the same channel

A channel with head/tail taper painted into it is a SHAPE and maps once. A
channel that is panned every frame is a MATERIAL and must be seamless. The two
requirements are mutually exclusive: a seamless channel by definition has no
ends, and a tapered one cannot tile. Tiling a shape channel gives a rope of
identical segments with no head, no tail and no silhouette.

### R2 — A sheet MAY mix modes across channels, and the consumer must honour each

`smoke_strand.png` is `STRAND | R:pattern1/STRETCH | G:pattern2/STRETCH |
B:distort/TILE | A:dissolve/TILE`. Its R/G are one complete wisp each; its B/A
are panned noise. `trail_deform.fs` therefore samples R/G at the stretched
coordinate and A at the tiled one **even when the sheet is in stretch mode** —
that asymmetry is correct by design, not a bug, and it is why the `stretch`
argument is passed explicitly at every `SurfaceFlow_AlongV` call site.

A consumer that samples every channel at one coordinate cannot use a mixed
sheet. Declare that in the profile's `consumers`.

### R3 — Signed channels use 128 as neutral

Slots `distort`, `flowx`, `flowy` are signed: encode `128 + 127*v`, decode
`c * 2.0 - 1.0`. Every other slot is unsigned linear 0..1.

A signed channel survives bilinear filtering safely — the average of two
opposite vectors is 128, i.e. no flow — which an unsigned encoding would not.

### R4 — In `STRAND` and `FLOW`, A is DATA, not coverage

Such a sheet must never be handed to `BLEND_ALPHA` as-is; the shader computes
coverage from the channels and emits it. Only `OPAQUE`, `FLIPBOOK` and `VOLUME`
carry a true `opacity` in A.

`VOLUME`'s A is a real opacity, but it is still not the drawn alpha: only the
`density` fraction of it occludes, because the `emission` fraction is light the
texel adds rather than blocks. The shader computes the final alpha and emits
premultiplied colour — handing a `VOLUME` sheet to `BLEND_ALPHA` would make the
flame occlude in proportion to its brightness, which is backwards.

This is load-bearing: raylib's `BLEND_ALPHA` is `glBlendFunc(SRC_ALPHA,
ONE_MINUS_SRC_ALPHA)` and the hardware multiplies RGB by A itself. A sheet
whose A is a dissolve field would be silently used as an opacity mask.

### R5 — There are no mipmaps

`ResourceManager_LoadTexture` calls raylib `LoadTexture`, and
`core/vfx_surface_registry.c:19-25` sets only filter and wrap. Every sheet has
exactly ONE mip level.

Consequence: a channel's frequency content is only valid at the physical width
it is drawn at. Reusing a sheet on a surface more than ~2x narrower makes every
frequency in it that much higher with nothing to filter it down, and fine
detail breaks into dashes. Author a second sheet instead of scaling an existing
one, and record the intended width in `provenance`.

### R6 — No channel may be constant

A packed sheet exists to use all four. `unused` is not a legal slot. If a
layout leaves a channel with nothing to carry, the sheet does not belong in
that layout.

This rule has teeth: `scripts/gen_flow_maps.py` writes `RG` = flow, `B` = 0,
`A` = 255 — half the file is waste, and it is exactly the waste the `FLOW`
layout exists to reclaim.

### R7 — Every packed sheet is generated, never hand-painted

A committed `scripts/gen_*.py` must produce it, and `provenance` must name that
script. A binary asset nobody can regenerate is one nobody can adjust.

### R8 — A packed profile is exactly one file

A profile whose body is `STRAND` or `FLOW` must register exactly one asset and
declare `budget.max_textures: 1`. This is what stops a packed profile quietly
regrowing the second and third file it was created to eliminate.

Elsewhere `max_textures` stays what it always was — a ceiling, not a count.

### R9 — A profile with no consumer is deleted, not kept

An orphan is either given a consumer or removed, along with its assets. If
removal is deferred, the profile must carry `"orphaned": {"since": "<date>",
"reason": "..."}` and the validator will keep reporting it. Nothing sits in the
registry unexplained.

## 4. Choosing a layout

- A trail sheet with two overlapping strand patterns → `STRAND`.
- A body that needs directional flow (two-phase, advection) → `FLOW`.
- Anything with genuine per-texel opacity — puffs, decals, flipbooks →
  `OPAQUE` / `FLIPBOOK`.
- Two patterns AND a flow *vector* do not fit: that is five channels. Choose
  which the effect actually needs. `STRAND`'s `distort` is a scalar for exactly
  this reason.

## 5. Adding or changing a sheet

1. Pick a layout from §4 and write the `channels` string per §2.
2. Write or extend the `scripts/gen_*.py` that produces it (R7).
3. Add a row to `assets/INDEX.md` — the validator requires every registered
   asset to be catalogued there.
4. Run `python3 scripts/validate_vfx_surface_registry.py`; it also runs at
   CMake configure time and fails the build.
5. Regenerate: `python3 scripts/gen_vfx_surface_registry.py`.
6. `./scripts/run_core_tests.sh texture_packing`.

## Patch log

| Date | Editor | Section | Source | Tier |
|---|---|---|---|---|
| 2026-08-03 | AI (Core Agent) | all | `assets/vfx_surface_profiles.json`, `scripts/gen_smoke_strand_texture.py`, `scripts/gen_flow_maps.py`, `core/vfx_surface_registry.c`, `core/trails/shaders/trail_deform.fs` | 1 ground-truth; §2 layouts `FLOW`/`OPAQUE`/`FLIPBOOK` are 3 project convention, newly decided |
