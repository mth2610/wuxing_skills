#version 330

// FXAA 3.11 with Subpixel Quality and Contrast-Adaptive Sharpening on LDR image.

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 u_texel;       // 1/width, 1/height of texture0
uniform float u_sharpen;    // Luma unsharp mask strength

float fxaaLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    vec2 uv = fragTexCoord;
    vec4 sampM = texture(texture0, uv);
    vec3 rgbM  = sampM.rgb;
    float lM   = sampM.a;

    vec4 d = u_texel.xyxy * vec4(-1.0, -1.0, 1.0, 1.0);
    float lNW = texture(texture0, uv + d.xy).a;
    float lNE = texture(texture0, uv + d.zy).a;
    float lSW = texture(texture0, uv + d.xw).a;
    float lSE = texture(texture0, uv + d.zw).a;

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    float range = lMax - lMin;

    // FidelityFX CAS-inspired weight: tapers at both dark shadows and bright highlights
    // so sharpening never causes black-crush rings or white specular stipple halos.
    float diagAvg = (lNW + lNE + lSW + lSE) * 0.25;
    float casHeadroom = clamp(sqrt(min(lMin, 1.0 - lMax) / max(lMax, 1e-4)), 0.0, 1.0);
    float sharpDelta = clamp(lM - diagAvg, -0.035, 0.035) * (4.0 * u_sharpen * casHeadroom);

    // Early exit on low-contrast areas
    if (range < max(lMax * (1.0 / 16.0), 1.0 / 32.0))
    {
        finalColor = vec4(clamp(rgbM + sharpDelta, 0.0, 1.0), 1.0);
        return;
    }

    // Subpixel aliasing metric (helps thin grass blades and distant sword silhouettes)
    float subpix = clamp(abs(diagAvg - lM) / max(range, 1e-4), 0.0, 1.0);
    subpix = smoothstep(0.0, 1.0, subpix);
    subpix = subpix * subpix * 0.25;

    // Direct edge direction from Sobel-like luma gradient
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),
                     ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max(diagAvg * (1.0 / 8.0), 1.0 / 128.0);
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
    aa = mix(aa, rgbB, subpix);

    // Giảm làm nét trực tiếp trên đúng cạnh vừa được khử răng cưa để không làm gắt lại răng cưa
    float edgeAtten = 1.0 - clamp(range * 1.5, 0.0, 0.75);
    finalColor = vec4(clamp(aa + sharpDelta * edgeAtten, 0.0, 1.0), 1.0);
}