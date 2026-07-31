# Trails module

Owns trail pool/lifecycle and trail-specific surface shaders. Public entry point:
`core/trails/trail_system.h`. `trail_glow.fs` remains the startup default loaded
by `main.c`; shared GLSL includes stay in `core/shaders/common/`.
