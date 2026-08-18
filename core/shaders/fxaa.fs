#version 330

// FXAA 3.11, the compact console variant, run on the TONE-MAPPED LDR image.
//
// Why this pass exists at all: the scene is rasterised into an offscreen HDR render
// target, not the swapchain, so FLAG_MSAA_4X_HINT cannot help it — raylib's MSAA hint
// applies to the window's own framebuffer. Every geometric silhouette in the game
// therefore lands with binary coverage. Measured on the ShieldShell: the last background
// pixel reads 188 luma, the shell's first pixel 252, a +64 step in ONE pixel with a
// perfectly smooth bloom ramp on either side of it. That step is the crease the eye
// reads as "the boundary is not smooth", and it is not specific to any effect — the
// map's own geometry staircases identically.
//
// It must run AFTER tone mapping, on perceptual values. FXAA thresholds on luma
// contrast; feeding it linear HDR would make the thresholds meaningless (a 40:1 linear
// range is a modest perceptual one) and it would smear highlights while ignoring
// shadow edges.

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;       // 1/width, 1/height of texture0

// The perceptual weights, not the linear-luminance ones: this runs post tone map.
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

    // Two thresholds, and both matter. The relative one (1/16) keeps the filter off
    // gentle gradients, which is what stops a bloom halo or a sky ramp from being
    // smeared into blotches. The absolute one (1/32) keeps it off near-black, where
    // 8-bit noise would otherwise register as an edge in every frame and shimmer.
    if (range < max(lMax * (1.0 / 16.0), 1.0 / 32.0))
    {
        finalColor = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),
                     ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    // The +-8 texel clamp is the whole reason this is safe on thin bright features: an
    // unclamped search direction on a one-pixel-wide highlight walks off the feature and
    // pulls in unrelated colour, which is how FXAA earns its reputation for eating
    // detail. A shell rim IS a one-pixel-wide bright feature.
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * u_texel;

    vec3 rgbA = 0.5 * (texture(texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(texture0, uv + dir * -0.5).rgb +
                                     texture(texture0, uv + dir *  0.5).rgb);

    // The wider four-tap average is better AA but can overshoot past the local luma
    // range, which shows as a dark or bright fringe hugging the edge. Fall back to the
    // narrow pair whenever it does — this is the guard that keeps a bright rim from
    // growing a halo of its own.
    float lB = fxaaLuma(rgbB);
    finalColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
