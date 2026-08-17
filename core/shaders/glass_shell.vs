#version 330
#include "core/shaders/common/vs_header.glsl"

out vec3 shieldViewDir;
out float shieldFresnel;
// Raw |N·V|, so the fragment stage can model WALL THICKNESS rather than reuse a
// fresnel band. shieldFresnel stays as-is for the emission/pattern terms.
out float shieldNdotV;

// DrawCoreSphere is immediate-mode geometry. BeginMode3D has already applied
// the model-view transform on the CPU, so both attributes arrive in view space.
// Calling VS_FinalOutput here would apply matModel a second time and skew the
// normal, producing a crescent across one hemisphere instead of a silhouette rim.
void main() {
    fragPosition = vertexPosition;
    fragNormal = normalize(vertexNormal);
    fragTexCoord = vertexTexCoord;
    shieldViewDir = normalize(-vertexPosition);
    // ABS, not clamp-to-zero. The far wall is BACK-FACING: its normal points away from
    // the eye, so the signed dot is negative and clamping pinned it to 0 across the whole
    // rear hemisphere. Everything downstream then degenerated at once — path length
    // saturated (1/0.10), wall density went to 1 everywhere so the rear had no gradient
    // at all, and the rim-hot blend keyed on that density turned the entire rear wall
    // WHITE, which the rear dimming then turned grey. That is the flat colourless haze
    // the shell showed behind its front face.
    //
    // What the shading actually wants is the OBLIQUITY, |N·V| — how edge-on the surface
    // is — which is the same quantity on both walls and is what makes the far wall carry
    // its own grazing gradient inside the near one.
    shieldNdotV = abs(dot(fragNormal, shieldViewDir));
    float fresnelM = 1.0 - shieldNdotV;
    float fresnelX2 = fresnelM * fresnelM;
    shieldFresnel = fresnelX2 * fresnelX2;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
