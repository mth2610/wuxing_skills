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
    // The ground draws this wraps never rlPushMatrix/rlTranslatef — vertices
    // are already authored in world space (arena center ~(6,0,4.4)).
    fragWorldPos = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
