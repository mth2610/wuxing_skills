# Shading module

Owns **how a shader program is produced** — the text pipeline that turns a
`.vs`/`.fs` on disk into the source string a backend compiles. It does not own
what a material pushes into that program (`core/material/`), nor the `.glsl`
files themselves (`core/shaders/`).

Public entry point: `core/shading/shader_preprocessor.h`.
- `ShaderPreprocessor_Load` — recursive `#include` expansion.
- `ShaderPreprocessor_LoadWithDefines` — the same, plus a defines block injected
  after `#version`. This is the permutation primitive: one source, N programs.

## Why the shader CACHE is not here

`ResourceManager_LoadShaderVariant` stays in `core/resource_manager.c` on
purpose. A cached shader is an owned resource with the same lifetime and the
same `Unload` path as a Texture, Model, Sound or Font; pulling it out would
create a second teardown path for one kind of asset. The layering runs one way
only — resource_manager (lifetime) calls into shading (text → source), never the
reverse — which is also why this module must never grow a dependency on
`core/material/`.

The cache key is the **triple** (vsPath, fsPath, defines). Two variants of one
`.vs`/`.fs` pair are different programs with different uniform locations, so
keying on the paths alone hands a caller the wrong program.

## The trap this module sets

The expander is purely textual and understands **no comments**. An include
directive written inside a `//` comment is still expanded, and the parser takes
the next double-quote *anywhere* after the token as the path — not the next one
on that line. See `ENGINE_LANDMINES.md` #20; guarded by
`core/tests/shader_permutation_test.c`.

Headless test: `core/tests/shader_permutation_test.c`.

## Materials (`materials/*.mat`)

A `.mat` is the ONE list a material's uniforms are described in.
`scripts/gen_materials.py` compiles it at CMake configure time into two outputs,
both of which carry a generated banner and both of which are overwritten on every
configure:

```
core/shading/materials/<name>.mat
        │
        ├── core/shaders/<name>.fs                 the fragment program
        └── core/material/materials.generated.inl  the VfxParamDesc table
```

Build-time, not runtime, for the same reason Filament's `matc` is: the inputs
never change after the build, and a runtime parser would mean allocation and
parse errors on a player's device. Every other generated artefact in this repo
works this way too.

### What it is for

The uniform list used to exist twice — as `uniform` lines in the `.fs`, and as a
hand-written table in `material_system.c` — with nothing comparing them. They
drifted. Authoring the first `.mat` immediately turned up `u_topY`: the C table
had been fetching a location for a uniform **no shader in the tree declares**,
getting -1 and pushing nothing, while the composition using it carefully set its
value with a comment explaining what it was supposed to do. (That first `.mat`
was `aura_shell`; it and its only consumer, ENERGY ORB, were deleted on
26/08/2026, so the example below has been moved to a material that still
exists.)

### Shape

```
material {
    name    : crystal,
    output  : body,              // body | emission | premultiplied
    vertex  : "core/shaders/crystal.vs",
    includes : [ ... ],
    table   : CRYSTAL_PARAMS,            // the C symbol to generate
    struct  : CrystalMaterialParams,     // whose fields the params bind to
    parameters : [
        { uniform: u_bodyColor, kind: color, field: bodyColor },
        { uniform: u_opacity,   kind: float, field: opacity },
        { uniform: u_displaceAmp, kind: float, field: displaceAmp, stage: vertex }
    ]
}
fragment { /* GLSL, no uniform declarations — those are generated */ }
```

`kind` is one of:

| kind | means |
|---|---|
| `float` | a float field; add `a`/`b`/`lo`/`hi` for an affine map `a + b*field` clamped to `[lo,hi]` — crystal's `roughness 0..1 -> u_fresnelPower 8..1` is one |
| `color` | a `Color` field, pushed as a normalized `vec4` |
| `texture` | a `Texture2D` field |
| `texflag` | the same `Texture2D` field pushed as `int(id != 0)`; list it BEFORE the sampler when the shader gates on it |
| `const` | a literal the material re-pushes on every Begin; needs `value`, has no field |
| `texture` + `default` | the asset the loader falls back to when the caller left the field at id 0, so the path lives in the material rather than in C |
| `extern` | declared in GLSL but NOT bound by the table — something sets it by name at runtime (`Material_SetFloat`). Without this the shader would reference an undeclared uniform |

`stage: vertex` means the
uniform is declared in the hand-written `.vs`, so the fragment stage must not
redeclare it — the C table still binds it, because a uniform is program-wide once
linked.

### What the compiler refuses

Each of these fails the build rather than producing a material that renders
something slightly wrong:

- an `output` that is not one of the three
- a parameter with no `field`, or listed twice
- **a parameter the fragment block never reads** — that is the `u_topY` class of
  drift, caught at its source
- **`u_time`** — auto-bound by `SkillManager_BeginShader` and only by it. A
  material pushing its own replaces a clock accumulated from the pinned delta
  with the wall clock, and the effect stops being reproducible under
  `render_vfx_matrix.sh`. See `ENGINE_LANDMINES.md`.

### Presets — the Material Instance idea

A `.mat` may carry a `presets` block: named sets of parameter-struct defaults for
one shader and one table. `Material_Get(&mat, MAT_FIRE)` reads a generated
`EffectMaterialParams` array indexed by the enum instead of the ~70-line `switch`
that used to hold the same numbers in code.

```
presets : [
    { name: MAT_FIRE, baseColor: ELEMENT_COLOR_FIRE, rimStrength: 1.2f },
    { name: MAT_ICE,  baseColor: ((Color){170, 220, 255, 150}),
      texture_path: "assets/textures/tex_ice_crystal.png" }
]
```

Values are C expressions emitted verbatim, so a preset can name the engine's own
colour constants. `texture_path` is separate from the rest because a texture is
LOADED rather than initialised; it becomes a parallel table of paths that
`Material_Get` resolves.

The compiler rejects a preset that sets a key **no parameter binds** — a typo
there would otherwise become a C initializer for a nonexistent field and the
error would point at generated code.

Two parsing details exist because presets broke the naive versions: entries are
split by BRACE depth (a value like `(Color){170, 220, 255, 150}` carries commas
and braces of its own) and the `[...]` after a key is matched by BRACKET depth (a
greedy regex ran to the last `]` in the block and fed `parameters` entries into
the preset parser).

### Why a texture default is a PATH and not a registry role

`assets/TEXTURE_PACKING.md` scopes `vfx_surface_registry` to packed VFX sheets
with a machine-checked channel grammar, and puts conventional albedo/detail maps
explicitly outside it. Every texture a material here wants — `tex_crystal.png`,
`tex_ice_crystal.png`, `tex_rock_albedo.png` — is the second kind. Routing them
through the registry would mean either registering sheets that break its channel
contract, or widening a normative document to fit two call sites.

So a material names its asset directly. The goal was never "use the registry"; it
was "C should not be the thing holding an asset path", and a `default:` in the
`.mat` achieves that without bending something else out of shape.
