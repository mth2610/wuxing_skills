#version 330

// Real Shading P6 — depth-only vertex shader for the shadow caster pass.
// CPU skinning already posed the mesh (see surface_lit.vs), so this is a
// plain static transform: only gl_Position matters, driven by the light's
// view/projection (set via rlSetMatrixModelview/rlSetMatrixProjection in
// EnvShadow_BeginCapture, not the camera's).

in vec3 vertexPosition;

uniform mat4 mvp;

void main() {
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
