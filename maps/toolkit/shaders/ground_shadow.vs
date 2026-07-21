#version 330

// Real Shading P6 — vertex shader for GroundShadow, the shadow-receiver wrap
// for raw immediate-mode ground draws (rlBegin(RL_TRIANGLES)/rlVertex3f/
// rlColor4ub in default_arena.c/verdant_path.c). Those draws never go
// through a Model/Material, so they can't sample the shadow map without
// this. Matches raylib's default immediate-mode attribute set exactly:
// position + vertex color, no normal/texcoord (never fed by those draws).

in vec3 vertexPosition;
in vec4 vertexColor;

uniform mat4 mvp;

out vec4 fragColor;
out vec3 fragWorldPos;

void main() {
    fragColor = vertexColor;
    // NOTE: `vertexPosition` is NOT world space in-game. main.c's MyBeginMode3D puts the view
    // matrix into rlgl's `transform` (transformRequired), so immediate-mode coords are CPU-
    // transformed to VIEW space before arriving here. The receiver keeps sampling with
    // `u_lightVP * fragWorldPos`, and ground_shadow.c folds inverse(view) into `u_lightVP` so the
    // product equals lightVP * worldPos (identity fold when transformRequired is off). See §7.26.
    fragWorldPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
