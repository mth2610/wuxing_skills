#version 330
#include "core/shaders/common/vfx_lights.glsl"
#include "maps/toolkit/shaders/map_shadow.glsl"
#include "maps/toolkit/shaders/nature_surface.glsl"

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
in float fragHeight;
in vec2 fragTexCoord;
in vec4 v_lightSpace;
in vec4 v_staticLightSpace;

uniform sampler2D texture0;
uniform int u_useTexture;
uniform float u_alphaCutoff;

out vec4 finalColor;

void main()
{
    vec3 albedo = fragColor.rgb * colDiffuse.rgb;

    if (u_useTexture != 0 && fragTexCoord.x >= 0.0) {
        vec4 texel = texture(texture0, fragTexCoord);
        // Preserve alpha coverage across mipmaps to prevent distant flowers/plumes from thinning out
        float uvGrad = dot(fwidth(fragTexCoord), vec2(0.5));
        float boostedAlpha = texel.a + clamp(uvGrad * 0.65, 0.0, 0.22);
        if (boostedAlpha < u_alphaCutoff) discard;
        albedo *= texel.rgb;
    } else {
        // Subtle center-ridge depth & lush vertical vibrancy gradient for untextured blades/stems
        float centerRidge = 1.0 - 0.05 * (1.0 - abs(clamp(fragTexCoord.x, -1.0, 1.0)));
        vec3 stemLift = mix(vec3(1.04, 1.10, 1.01), vec3(1.14, 1.20, 1.08), clamp(fragHeight, 0.0, 1.0));
        albedo *= stemLift * centerRidge;
    }

    // Re-normalize interpolated normal & handle back-facing foliage gracefully
    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) {
        N = normalize(vec3(-N.x, max(N.y, 0.25), -N.z));
    }

    finalColor = vec4(NatureShade(albedo, fragPosition, N, fragHeight, fragTexCoord.x, v_lightSpace, v_staticLightSpace), 1.0);
}