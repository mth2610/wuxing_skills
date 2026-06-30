#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/soft_particle.glsl"

// Test: soft particles (core/screen_distort.h's depth snapshot +
// soft_particle.glsl). Sphere sits low, overlapping the ground plane —
// without this, the bottom edge would hard-clip into the floor.

uniform vec4 u_color;
uniform float u_fadeDistance;

void main() {
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));

    float diff = calcDiffuse(fragNormal, lightDir, 0.4);
    float fres = calcFresnel(fragNormal, viewDir, 2.0);

    float soft = SoftParticle_Factor(u_fadeDistance);

    // TEMP DEBUG: R=sceneLinear/1500, G=fragLinear/1500. Green sphere => scene
    // sample reads 0 (texture broken). Yellow => values close (real geometry).
    vec2 screenUV = gl_FragCoord.xy / u_resolution;
    float sceneLinear = texture(u_cameraDepthTex, screenUV).r;
    float fragLinear  = SoftParticle_LinearDepth(gl_FragCoord.z);
    finalColor = vec4(sceneLinear / 1500.0, fragLinear / 1500.0, 0.0, 1.0);
    return;

    vec3 color = u_color.rgb * diff + u_color.rgb * fres * 0.8;
    finalColor = vec4(color, u_color.a * soft);
}
