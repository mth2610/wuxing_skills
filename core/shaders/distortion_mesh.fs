#version 330
#include "core/shaders/common/fs_header.glsl"

// Messiah Engine / Where Winds Meet Style Screen-Space Mesh Distortion Pass
// Allows arbitrary 3D geometry (sword slashes, energy palms, shockwaves)
// to distort the background scene using normal/flow maps.

uniform sampler2D texture0;            // Flow / Normal map texture
uniform sampler2D u_sceneTex;          // Background scene snapshot texture
uniform int       u_hasScene;
uniform float     u_distortionStrength;// Displacement amplitude (e.g. 0.02 - 0.08)
uniform vec2      u_flowSpeed;         // UV scroll rate
uniform vec4      u_tintColor;         // RGB tint, A = tint blend factor
uniform float     u_edgeFade;          // Grazing angle falloff

out vec4 finalColor;

void main() {
    vec2 res = (u_resolution.x > 0.0 && u_resolution.y > 0.0) ? u_resolution : vec2(1920.0, 1080.0);
    vec2 screenUV = gl_FragCoord.xy / res;

    // Procedural ripple wave along blade UV + texture flow
    float ripple = sin((fragTexCoord.x * 14.0 - u_time * 12.0) * 3.14159) * cos(fragTexCoord.y * 6.28);
    vec2 procFlow = vec2(cos(fragTexCoord.x * 6.28), sin(fragTexCoord.y * 6.28)) * ripple;

    // Sample flow map with animated UV if present
    vec2 flowUV = fragTexCoord + u_flowSpeed * u_time;
    vec4 flowSample = texture(texture0, flowUV);

    // Unpack flow direction to [-1, 1] + ripple
    vec2 flowDir = (flowSample.rg * 2.0 - 1.0) * 0.5 + procFlow * 0.8;

    // Alpha mask
    float mask = (flowSample.a > 0.01) ? flowSample.a : 1.0;

    // View direction: fragPosition in view-space has camera at origin
    vec3 V = normalize(-fragPosition);
    vec3 N = normalize(fragNormal);
    float NdotV = clamp(abs(dot(N, V)), 0.0, 1.0);
    float edgeFactor = pow(max(NdotV, 0.1), (u_edgeFade > 0.01 ? u_edgeFade : 1.0));

    float distStrength = (u_distortionStrength > 0.0001) ? u_distortionStrength : 0.055;
    vec2 distortedUV = screenUV + flowDir * (distStrength * mask * edgeFactor);

    vec3 sceneColor;
    if (u_hasScene != 0) {
        sceneColor = texture(u_sceneTex, clamp(distortedUV, vec2(0.001), vec2(0.999))).rgb;
    } else {
        sceneColor = vec3(0.05, 0.08, 0.12);
    }

    // Hot energy blade rim on grazing edges
    float bladeRim = pow(1.0 - NdotV, 2.0) * 1.2;
    // Core slash energy highlight
    float coreGlow = pow(sin(clamp(fragTexCoord.y, 0.0, 1.0) * 3.14159), 2.5) * 0.85;

    vec3 outRgb = mix(sceneColor, u_tintColor.rgb, clamp(u_tintColor.a, 0.0, 1.0)) 
                + u_tintColor.rgb * (bladeRim + coreGlow);
    finalColor = vec4(outRgb, 1.0);
}
