# Decals module

Owns ground/surface decal lifecycle, fixed pool, receiver projection and every
feature-specific decal shader. New decal code belongs here; shared GLSL helpers
remain under `core/shaders/common/` only.

Public entry point: `core/decals/decal_system.h`.

`shaders/decal_flow.fs` is the legacy-compatible flow pass. `shaders/decal_material.fs`
is the P4 conformal material-stamp pass. Both are loaded exclusively through
`ResourceManager_LoadShader()` by `decal_system.c`.
