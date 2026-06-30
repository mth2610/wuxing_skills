#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/fx.glsl"

uniform float u_dissolve;   // 0 alive -> 1 dried, drives glow fade + dissolve
uniform float u_uvLength;   // C-side arc length, scales vein pulse along body
uniform vec3  u_woodColor;  // ELEMENT_COLOR_WOOD as linear-ish vec3

// MUST match living_vine.vs exactly for correct perturbed normals.
float barkHeight(vec2 uv) {
    float ridges = fbm2N(vec2(uv.x * 4.0, uv.y * 2.0), 2);
    return ridges;
}

void main() {
    vec2 uv = fragTexCoord;
    vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));
    vec3 viewDir  = normalize(viewPos - fragPosition);

    // Re-derive the bark normal from the same height field the VS displaced with.
    float eps = 0.02;
    vec2 hd = vec2(barkHeight(uv - vec2(eps, 0.0)) - barkHeight(uv + vec2(eps, 0.0)),
                   barkHeight(uv - vec2(0.0, eps)) - barkHeight(uv + vec2(0.0, eps)));
    vec3 N = perturbNormal(normalize(fragNormal), hd, 0.35);

    // --- Dark diffuse bark base (preserve 3D volume, limited emissive) ---
    float diff = calcDiffuse(N, lightDir, 0.18);
    vec3 barkDark = vec3(0.05, 0.07, 0.045);
    vec3 base = barkDark * diff;

    // --- One clean glowing vein running straight down the vine's spine ---
    float spine = 1.0 - smoothstep(0.0, 0.16, abs(uv.x - 0.5));
    float pulse = 0.6 + 0.4 * sin(u_time * 2.5 + uv.y * u_uvLength * 0.2);
    vec3 emissive = u_woodColor * spine * pulse * 1.5;

    // Strong Fresnel rim so edges catch the magical green light.
    float fres = calcFresnel(N, viewDir, 3.0);
    emissive += u_woodColor * fres * 0.35;

    // Glow disappears as the vine dries.
    emissive *= (1.0 - u_dissolve);

    vec3 color = base + emissive;

    // --- Animated dissolve only in the final phase ---
    float edge;
    float dn = fbm2N(uv * 6.0, 4);
    float d = dissolveCalc(dn, u_dissolve, 0.09, edge);
    if (d > 0.5) discard;
    // Burning-edge ember as bark crumbles (also fades out near full dissolve).
    color += u_woodColor * edge * 2.2 * (1.0 - u_dissolve);

    finalColor = vec4(color, 1.0);
}
