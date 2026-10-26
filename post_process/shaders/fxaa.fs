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
    vec4 sampM = texture(texture0, uv);
    vec3 rgbM  = sampM.rgb;

    float lM  = sampM.a;
    float lNW = texture(texture0, uv + vec2(-1.0, -1.0) * u_texel).a;
    float lNE = texture(texture0, uv + vec2( 1.0, -1.0) * u_texel).a;
    float lSW = texture(texture0, uv + vec2(-1.0,  1.0) * u_texel).a;
    float lSE = texture(texture0, uv + vec2( 1.0,  1.0) * u_texel).a;

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    float range = lMax - lMin;

    // FidelityFX CAS-inspired (Contrast-Adaptive Sharpening) weight:
    // Maximizes micro-detail recovery on low/medium-contrast ground and distant turf,
    // while automatically tapering to zero on high-contrast grass blade silhouettes
    // and bright flower/pampas tips to prevent white stipple halos.
    float casHeadroom = clamp(sqrt(min(lMin, 1.0 - lMax) / max(lMax, 1e-4)), 0.0, 1.0);
    float lap = clamp(4.0 * lM - (lNW + lNE + lSW + lSE), -0.14, 0.14) * casHeadroom;

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

    // Apply contrast-adaptive sharpening to keep high-frequency textures crisp
    finalColor = vec4(clamp(aa + u_sharpen * lap, 0.0, 1.0), 1.0);
}
