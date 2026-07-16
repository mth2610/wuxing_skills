#version 330
#include "core/shaders/common/noise.glsl"

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
    
    // Glowing lava/amber color inside the cracks
    vec3 lavaCol = vec3(1.0, 0.45, 0.05) * (1.5 + 0.5 * sin(u_time * 5.0));
    
    // Core of the cracks glows white-hot
    vec3 col = mix(vec3(0.08, 0.06, 0.05), lavaCol, crackMask);
    col += vec3(1.0, 0.9, 0.7) * pow(crackMask, 4.0) * 1.5;

    // Glowing border at the spreading edge of the fracture
    float border = smoothstep(revealRadius - 0.05, revealRadius, d);
    col += vec3(1.0, 0.35, 0.0) * border * 2.2;

    // Fade out near the outer bounds of the quad
    float alpha = crackMask * smoothstep(0.5, 0.45, d) * fragColor.a;

    finalColor = vec4(col, alpha);
}
