#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform sampler2D texture0;
uniform vec4 u_bodyColor;
uniform vec4 u_glowColor;
uniform float u_opacity;
uniform float u_emission;
uniform float u_detailScale;
uniform float u_layerPhase;
uniform int u_hasSmokeTexture;

void main()
{
    float angle = fragTexCoord.x * 6.28318530718;
    float radial = fragTexCoord.y;
    // Pan two procedural noise channels in DISC coordinates, then move the
    // coordinate BEFORE evaluating the mask. This is the Shader Graph
    // `Noise → UV offset → Radial Mask` chain: it deforms the actual boundary,
    // not merely the colour painted inside a perfectly clean ring.
    vec2 baseP = vec2(cos(angle), sin(angle)) * radial;
    vec2 pan = vec2(u_time * 0.08, -u_time * 0.05) +
               vec2(cos(u_layerPhase), sin(u_layerPhase)) * 0.37;
    vec2 uvWarp = vec2(fbm3(vec3(baseP * (2.1 + u_detailScale * 0.12) + pan,
                                 u_time * 0.07 + u_layerPhase)),
                       fbm3(vec3(baseP * (2.1 + u_detailScale * 0.12) +
                                     vec2(7.2, -4.6) - pan,
                                 -u_time * 0.09 - u_layerPhase))) - 0.5;
    vec2 p = baseP + uvWarp * 0.11;
    float warpedRadial = length(p);
    // Disc coordinates give broad cloudy regions and radial tears, rather
    // than the regular tangential neon lines of a scrolling annulus strip.
    float cloudLarge = fbm3(vec3(p * (1.9 + u_detailScale * 0.18),
                                 u_time * 0.10 + u_layerPhase));
    float cloudFine = fbm3(vec3(p * u_detailScale + vec2(4.3, -2.1),
                                -u_time * 0.16 - u_layerPhase));
    float rimNoise = fbm3(vec3(vec2(cos(angle), sin(angle)) * 2.4,
                                u_time * 0.07));

    // The mesh is a full disc. This soft, uneven hole is why the effect reads
    // as a torn pressure membrane around the victim, not a clean donut tube.
    float holeRadius = 0.20 + (rimNoise - 0.5) * 0.22;
    float inner = smoothstep(holeRadius, holeRadius + 0.20, warpedRadial);
    float outer = 1.0 - smoothstep(0.78, 1.00, warpedRadial +
                                    (cloudLarge - 0.5) * 0.12);
    float cloud = smoothstep(0.30, 0.76, cloudLarge * 0.64 + cloudFine * 0.36);
    float wisps = smoothstep(0.58, 0.84, cloudFine) *
                  smoothstep(0.28, 0.92, warpedRadial);
    // The authored thin-smoke strip maps exactly once around polar U.  Its
    // vertical smoke band becomes the radial width of this pressure front;
    // varying each layer's phase misaligns the torn gaps without ever tiling.
    float smokeU = clamp(fragTexCoord.x + sin(u_layerPhase) * 0.045, 0.002, 0.998);
    float bandCenter = 0.61 + sin(u_layerPhase * 1.7) * 0.035;
    float smokeV = clamp(0.5 + (warpedRadial - bandCenter) * 2.2, 0.002, 0.998);
    float authoredSmoke = smoothstep(0.04, 0.48,
                                     texture(texture0, vec2(smokeU, smokeV)).a);
    // Asset-missing fallback keeps the primary readable, but authored alpha is
    // deliberately the dominant silhouette when the registered sheet loaded.
    float smokeShape = mix(mix(0.20, 1.0, cloud), authoredSmoke,
                            float(u_hasSmokeTexture));
    float coverage = inner * outer * smokeShape;
    vec3 color = mix(u_bodyColor.rgb, u_glowColor.rgb,
                     clamp(cloud * 0.38 + wisps * 0.22 + authoredSmoke * 0.72,
                           0.0, 1.0));
    finalColor = vec4(color * u_emission, coverage * u_opacity * u_bodyColor.a);
}
