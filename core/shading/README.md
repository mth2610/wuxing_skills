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
