# UV module

Owns UV-space coordinate deformation and layered surface sampling — the two
halves of `mesh + UVDeformField + SurfaceFlow = effect`. Warp the coordinate,
then sample it. Public entry points: `core/uv/uv_deform.h` (the warp),
`core/uv/surface_flow.h` (the sampling), `core/uv/uv_fx.h` (binds both in one
call), and `core/uv/flow_map.h` (the one-layer convenience, kept as-is).

They are one module rather than two because they share the ENVELOPE: the
along-surface gate that weights a wave's amplitude is the same weight that
blends a texture layer, and every consumer uses both together.

`shaders/uv_deform.glsl` and `shaders/surface_flow.glsl` are pure functions
declaring no uniforms — the `flow_map.glsl` contract — so a shader with its own
uniform naming can call them without renaming anything.
`shaders/uv_field.glsl` is the other route: it declares the engine's standard
packed `vec4[]` blocks that `UVFx_Apply()` binds. Include one or the other.

These four `.glsl` files are shared includes despite living here rather than in
`core/shaders/common/`; every one needs its own `configure_file` line in
`CMakeLists.txt` and its own `cp -f` in `Makefile.Android`, because a missing
shader file does not report as a shader problem — the consumer's capability
check just goes false and it renders the old path.

Headless test: `core/tests/uv_deform_test.c`.
