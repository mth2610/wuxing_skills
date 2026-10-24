#version 330

// FXAA 3.11 with Subpixel Quality and Adaptive Sharpening on LDR image.
//
// Runs on the tone-mapped LDR buffer. Antialiases high-contrast geometric silhouettes
// (grass blade edges, character boundaries, rock ridges) that otherwise staircase
// with binary coverage on offscreen render targets.

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;       // 1/width, 1/height of texture0
uniform float u_sharpen;    // Luma unsharp mask strength

// Perceptual luma weights for post-tonemapped image
float fxaaLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    vec2 uv = fragTexCoord;
    vec3 rgbM = texture(texture0, uv).rgb;

    float lM  = fxaaLuma(rgbM);
    float lNW = fxaaLuma(texture(texture0, uv + vec2(-1.0, -1.0) * u_texel).rgb);
    float lNE = fxaaLuma(texture(texture0, uv + vec2( 1.0, -1.0) * u_texel).rgb);
    float lSW = fxaaLuma(texture(texture0, uv + vec2(-1.0,  1.0) * u_texel).rgb);
    float lSE = fxaaLuma(texture(texture0, uv + vec2( 1.0,  1.0) * u_texel).rgb);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    float range = lMax - lMin;

    // Laplacian edge mask for zero-cost unsharp sharpening
    float lap = 4.0 * lM - (lNW + lNE + lSW + lSE);

    // Early exit on low-contrast areas (avoids smearing smooth bloom halos, skies, and gradients)
    if (range < max(lMax * (1.0 / 16.0), 1.0 / 32.0))
    {
        finalColor = vec4(clamp(rgbM + u_sharpen * lap, 0.0, 1.0), 1.0);
        return;
    }

    // Direct edge direction from Sobel-like luma gradient
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),
                     ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * u_texel;

    // 2-tap blur along edge
    vec3 rgbA = 0.5 * (texture(texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    // 4-tap blur along edge
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(texture0, uv + dir * -0.5).rgb +
                                     texture(texture0, uv + dir *  0.5).rgb);

    // Guard against luminance overshoot on thin bright features
    float lB = fxaaLuma(rgbB);
    vec3 aa = (lB < lMin || lB > lMax) ? rgbA : rgbB;

    // Apply adaptive sharpening to keep high-frequency textures crisp
    finalColor = vec4(clamp(aa + u_sharpen * lap, 0.0, 1.0), 1.0);
}
