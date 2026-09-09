#version 330
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/lighting.glsl"

/* Varyings */
in vec2 fragTexCoord;
in vec4 fragColor;

/* Uniforms */
uniform sampler2D texture0;
uniform float     u_progress; // 0.0 to 1.0
uniform float     u_time;

/* Output */
out vec4 finalColor;

void main() {
    vec2 uv = fragTexCoord;
    vec4 tex = texture(texture0, uv);

    // Calculate distance from center
    vec2 p = uv - vec2(0.5);
    float d = length(p);

    // Add noise to the edge of the crack reveal to make it look jagged and organic
    float edgeNoise = vnoise(uv * 8.0) * 0.06;
    float revealRadius = u_progress * 0.5 + edgeNoise;

    // Discard pixels outside the crawling fracture front.
    // NOTE: kept local, not fx.glsl's dissolveCalc() — this is a radial
    // "grow outward" reveal (discards d > revealRadius, glows fading IN as d
    // approaches the boundary from inside), the opposite polarity of
    // dissolveCalc's "discard below threshold, glow fading out past it".
    // Reusing dissolveCalc here would require negating noiseVal/dissolve,
    // which is more fragile/obscure than this direct radial math.
    if (d > revealRadius) {
        discard;
    }

    // Mask crack based on texture alpha
    float crackMask = tex.r;
    
    // Ghost of Tsushima: Molten rock cracks with Planck Blackbody radiation
    float pulse = 0.85 + 0.15 * sin(u_time * 5.0);
    float crackHeat = clamp(crackMask * pulse, 0.0, 1.0);
    vec3 radiantCrack = calcBlackbodyNormalized(crackHeat);

    // Core of the cracks glows white-hot with deep obsidian rock surround
    vec3 rockCol = vec3(0.06, 0.05, 0.04);
    vec3 col = mix(rockCol, radiantCrack, smoothstep(0.04, 0.35, crackMask));

    // Glowing border at the spreading edge of the fracture
    float border = smoothstep(revealRadius - 0.05, revealRadius, d);
    col += calcBlackbodyNormalized(0.75) * border * 1.6;

    // Fade out near the outer bounds of the quad
    float alpha = crackMask * smoothstep(0.5, 0.45, d) * fragColor.a;

    // BODY / ALPHA scope (stone_prison_skill.c).
    finalColor = VFX_ResolveBody(col, 1.0, alpha);
}
