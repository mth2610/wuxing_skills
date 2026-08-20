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
getting -1 and pushing nothing, while `vc_energy_orb.inl` carefully set its value
with a comment explaining what it was supposed to do.

### Shape

```
material {
    name    : aura_shell,
    output  : body,              // body | emission | premultiplied
    vertex  : "core/shaders/aura_shell.vs",
    includes : [ ... ],
    table   : AURA_PARAMS,               // the C symbol to generate
    struct  : AuraShellMaterialParams,   // whose fields the params bind to
    parameters : [
        { uniform: u_bodyColor, kind: color, field: bodyColor },
        { uniform: u_opacity,   kind: float, field: opacity },
        { uniform: u_displaceAmp, kind: float, field: displaceAmp, stage: vertex }
    ]
}
fragment { /* GLSL, no uniform declarations — those are generated */ }
```

`kind` is one of `float`, `color`, `texture`, `texflag`. `stage: vertex` means the
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
