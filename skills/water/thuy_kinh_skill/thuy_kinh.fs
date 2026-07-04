#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"
#include "core/shaders/common/soft_particle.glsl"

uniform float u_dissolve;
uniform sampler2D flowTex;      // RG = flow direction (water_flow.png)
uniform sampler2D causticsTex;  // caustic pattern    (water_caustics.png)

void main() {
    vec3 viewDir  = normalize(viewPos - fragPosition);
    vec3 lightDir = normalize(u_lightDir);

    // Smooth mesh normal only — no procedural perturbation. All the surface
    // life comes from flow-mapped caustics, so the silhouette stays clean
    // instead of sparkling (the old fbm-gradient normal read as "rough").
    vec3 normal = normalize(fragNormal);

    float fresnel  = calcFresnel(normal, viewDir, 3.0);
    float diffuse  = calcDiffuse(normal, lightDir, 0.22);

    // Caustic layers scrolled by the flow map at different tilings and
    // opposite speeds — classic parallax water-wall look, fully texture-driven.
    vec2 flowDir = texture(flowTex, fragTexCoord).rg * 2.0 - 1.0;
    float caustA = flowBlend(causticsTex, fragTexCoord * vec2(4.0, 2.0), flowDir, 0.55, 0.10, u_time);
    float caustB = flowBlend(causticsTex, fragTexCoord * vec2(7.0, 3.5) + 0.37, flowDir, -0.35, 0.07, u_time);
    float caustic = caustA * 0.7 + caustB * 0.45;
    // Third, finer layer used only for glints: bright spots where the fast
    // pattern peaks, concentrated toward the rim — the "lấp lánh" sparkle.
    float caustC = flowBlend(causticsTex, fragTexCoord * vec2(10.0, 5.0) + 0.71, flowDir, 0.85, 0.05, u_time);
    float glint  = smoothstep(0.72, 1.0, caustC) * mix(0.4, 1.0, fresnel);

    // Specular brightens where caustic crests cross it — wet, glittering water.
    float specular = calcSpecular(normal, lightDir, viewDir, 96.0) * (1.2 + caustic * 1.4);

    // Deep-water core -> cyan edge, faint self-glow floor for the night side.
    vec3 coreColor = vec3(0.03, 0.18, 0.38);
    vec3 edgeColor = vec3(0.22, 0.62, 0.95);
    vec3 baseColor = mix(coreColor, edgeColor, pow(fresnel, 1.4));
    baseColor *= diffuse;
    baseColor += edgeColor * 0.09;

    // Caustics added after base lighting; bright enough for bloom to catch
    // the crests (post-FX bloomThreshold 0.5 picks up what exceeds it).
    baseColor += vec3(0.30, 0.70, 0.95) * smoothstep(0.25, 1.1, caustic) * 0.85;
    baseColor += vec3(0.75, 0.95, 1.0) * glint * 0.9;

    // Rim glow, biased toward the light side (dimmed, not zeroed, behind).
    float rim = fresnel * mix(0.35, 1.0, max(dot(normal, lightDir), 0.0));
    baseColor += vec3(0.45, 0.85, 1.0) * rim * 0.6;

    // Center see-through; caustic bands carry a little extra density so the
    // water reads as substance, not tinted glass.
    float alpha = mix(0.12, 0.62, fresnel) + caustic * 0.10 + glint * 0.15;

    // Dissolve: shield breaks apart into glowing water cells. MUST be gated:
    // dissolveCalc() yields a nonzero edgeFactor on ~8% of fragments even at
    // dissolve == 0 (CORE_API §9b) — unguarded, those cells render as the
    // square/triangle speckle seen all over the live dome.
    if (u_dissolve > 0.0) {
        float n = hash3(floor(fragPosition * 0.12));
        float edgeFactor;
        if (dissolveCalc(n, u_dissolve, 0.10, edgeFactor) >= 1.0) discard;
        baseColor = mix(baseColor, vec3(0.7, 0.95, 1.0), edgeFactor);
    }

    // Apply soft particle fade at intersections with ground/solid geometry
    float softFactor = SoftParticle_Factor(0.3);
    alpha *= softFactor;

    finalColor = vec4(baseColor + vec3(specular), alpha);
}
