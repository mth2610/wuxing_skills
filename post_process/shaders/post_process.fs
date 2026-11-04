#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D u_bloomTex;
uniform vec2 u_bloomTexel;

// Cấu hình
uniform float u_bloomEnabled;
uniform float u_bloomIntensity;

uniform float u_chromaticEnabled;
uniform float u_chromaticStrength;

uniform float u_vignetteEnabled;
uniform float u_vignetteRadius;
uniform float u_vignetteSoftness;

uniform float u_colorGradeEnabled;
uniform float u_contrast;
uniform float u_saturation;
uniform vec3 u_colorTint;

uniform vec3 u_shadowTint;
uniform vec3 u_highlightTint;

uniform float u_tonemapEnabled;
uniform float u_exposure;

uniform sampler2D u_exposureTex;
uniform float u_autoExposure;

// ── Colour-grade LUT (Đợt G5) ────────────────────────────────────────────────
uniform sampler2D u_lutTex;
uniform float u_lutEnabled;
uniform float u_lutStrength;  // 0..1 blend toward the graded result
uniform vec2  u_lutParams;    // (1/stripWidth, 1/stripHeight)
uniform float u_lutSize;      // slices per axis (strip is size*size by size)

// Radial blur (Đợt E1a)
uniform float u_radialBlurEnabled;
uniform vec2  u_radialBlurCenter;   // screen UV [0..1] of the focal point
uniform float u_radialBlurStrength; // sample offset scale (~0.15 = strong)
uniform float u_radialBlurFalloff;  // UV radius where the blur reaches full strength

uniform float u_hueRestore;    // shipping default 0.6 (§12.1); 0 = the old per-channel curve
uniform float u_shoulderView;  // diagnostic, 0 = off

out vec4 finalColor;

vec3 ApplyColorGradeLut(vec3 col) {
    float size = max(u_lutSize, 2.0);
    float maxSlice = size - 1.0;
    vec3 c = clamp(col, 0.0, 1.0);

    float b = c.b * maxSlice;
    float slice0 = floor(b);
    float slice1 = min(slice0 + 1.0, maxSlice);
    float blend = b - slice0;

    float uInTile = 0.5 + c.r * maxSlice;
    float v = (0.5 + c.g * maxSlice) * u_lutParams.y;

    vec3 s0 = texture(u_lutTex, vec2((slice0 * size + uInTile) * u_lutParams.x, v)).rgb;
    vec3 s1 = texture(u_lutTex, vec2((slice1 * size + uInTile) * u_lutParams.x, v)).rgb;
    return mix(s0, s1, blend);
}

vec3 acesFilmic(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float acesFilmicScalar(float x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 toneMapScene(vec3 x) {
    x = max(x, vec3(0.0));
    vec3 perChannel = acesFilmic(x);
    if (u_hueRestore <= 0.0) return perChannel;

    float peak = max(x.r, max(x.g, x.b));
    if (peak <= 0.0) return perChannel;

    float w = clamp(u_hueRestore, 0.0, 1.0);
    vec3 hueKept = mix(x / peak, vec3(1.0), smoothstep(5.0, 12.0, peak)) * acesFilmicScalar(peak);
    return mix(perChannel, hueKept, w);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 toCenter = uv - vec2(0.5);
    float distSq = dot(toCenter, toCenter);

    // Jimenez sin-free Interleaved Gradient Noise (dùng chung cho cả Radial Blur jitter và Output Dither)
    float d0 = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float d1 = fract(52.9829189 * fract(dot(gl_FragCoord.xy + 17.0, vec2(0.06711056, 0.00583715))));

    // 1. Fetch gốc + Chromatic Aberration
    vec4 sceneCol = texture(texture0, uv);
    vec2 chromaOffset = vec2(0.0);
    if (u_chromaticEnabled > 0.5) {
        chromaOffset = toCenter * (u_chromaticStrength * distSq * 0.05);
        sceneCol.r = texture(texture0, uv - chromaOffset).r;
        sceneCol.b = texture(texture0, uv + chromaOffset).b;
    }

    // 1b. Radial blur
    if (u_radialBlurEnabled > 0.5) {
        vec2  dir = uv - u_radialBlurCenter;
        float w = smoothstep(0.0, max(u_radialBlurFalloff, 0.001), length(dir));

        vec2  c   = u_radialBlurCenter;
        float off = max(max(-c.x, c.x - 1.0), max(-c.y, c.y - 1.0));
        w *= 1.0 - smoothstep(0.0, 0.35, off);

        if (w > 0.001) {
            vec2 smear = dir * (u_radialBlurStrength * w);
            float smearLen = length(smear);
            if (smearLen > 0.08) smear *= (0.08 / smearLen);

            // Bắt đầu tích lũy từ mẫu gốc (đã có chromatic aberration) và jitter nhẹ vị trí tap
            // bằng d0 để xóa hiện tượng phân bậc (ghosting steps) khi blur mạnh.
            vec3 acc = sceneCol.rgb;
            for (int i = 1; i <= 7; i++) {
                float t = (float(i) - 0.5 + (d0 - 0.5) * 0.75) * (1.0 / 7.0);
                vec2 tapUv = clamp(uv - smear * t, vec2(0.001), vec2(0.999));
                acc += texture(texture0, tapUv).rgb;
            }
            sceneCol.rgb = mix(sceneCol.rgb, acc * 0.125, w);
        }
    }

    // 2. Bloom (4 bilinear taps = 3x3 tent filter)
    if (u_bloomEnabled > 0.5) {
        vec4 d = u_bloomTexel.xyxy * vec4(-0.5, 0.5, 0.5, -0.5);
        vec3 bloomAvg = (texture(u_bloomTex, uv + d.xy).rgb
                       + texture(u_bloomTex, uv + d.zy).rgb
                       + texture(u_bloomTex, uv + d.xw).rgb
                       + texture(u_bloomTex, uv + d.zw).rgb) * 0.25;
        sceneCol.rgb += bloomAvg * u_bloomIntensity;
    }

    // 2b. Tone mapping
    float exposure = u_exposure;
    if (u_autoExposure > 0.0) {
        float autoE = texture(u_exposureTex, vec2(0.5)).r;
        if (autoE > 0.0) exposure *= mix(1.0, autoE, u_autoExposure);
    }
    vec3 exposedScene = max(sceneCol.rgb, vec3(0.0)) * exposure;
    if (u_tonemapEnabled > 0.5) {
        sceneCol.rgb = toneMapScene(exposedScene);
    }

    // 3. Color Grading
    if (u_colorGradeEnabled > 0.5) {
        vec3 gradedCol = clamp(sceneCol.rgb, 0.0, 1.0);

        vec3 sCurve = gradedCol * gradedCol * (3.0 - 2.0 * gradedCol);
        float sBlend = clamp((u_contrast - 1.0) * 0.55, -0.25, 0.35);
        gradedCol = mix(gradedCol, sCurve, sBlend);
        float linContrast = 1.0 + (u_contrast - 1.0) * 0.45;
        gradedCol = clamp((gradedCol - vec3(0.5)) * linContrast + vec3(0.5), 0.0, 1.0);

        const vec3 LUMA_COEFF = vec3(0.2126, 0.7152, 0.0722);
        float luma = dot(gradedCol, LUMA_COEFF);

        float satProtection = 1.0 - 0.32 * smoothstep(0.58, 0.96, luma);
        float effSat = mix(1.0, u_saturation, satProtection);
        gradedCol = mix(vec3(luma), gradedCol, effSat);

        gradedCol *= mix(u_shadowTint, u_highlightTint, smoothstep(0.0, 1.0, luma));
        sceneCol.rgb = clamp(gradedCol * u_colorTint, 0.0, 1.0);
    }

    // 3b. Colour-grade LUT
    if (u_lutEnabled > 0.5 && u_lutStrength > 0.0) {
        vec3 graded = ApplyColorGradeLut(sceneCol.rgb);
        sceneCol.rgb = mix(sceneCol.rgb, graded, clamp(u_lutStrength, 0.0, 1.0));
    }

    // 4. Vignette
    if (u_vignetteEnabled > 0.5) {
        float len = sqrt(distSq);
        float innerRadius = max(u_vignetteRadius - u_vignetteSoftness, 0.0);
        float darkness = smoothstep(innerRadius, max(u_vignetteRadius, innerRadius + 0.001), len);
        sceneCol.rgb *= (1.0 - darkness);
    }

    // Shoulder view diagnostic
    if (u_shoulderView > 0.5) {
        float peak = max(exposedScene.r, max(exposedScene.g, exposedScene.b));
        float g = dot(sceneCol.rgb, vec3(0.2126, 0.7152, 0.0722)) * 0.30;
        if      (peak >= 9.0) sceneCol.rgb = vec3(0.0, 0.9, 1.0);
        else if (peak >= 1.0) sceneCol.rgb = vec3(1.0, 0.0, 0.8);
        else                  sceneCol.rgb = vec3(g);
    }

    // Tính perceptual luma cho FXAA TRƯỚC khi cộng dither 8-bit để CAS sharpening
    // trong fxaa.fs không khuếch đại hạt nhiễu dither.
    vec3 cleanRgb = clamp(sceneCol.rgb * fragColor.rgb, 0.0, 1.0);
    float outLuma = dot(cleanRgb, vec3(0.299, 0.587, 0.114));

    // 5. Output dither (Triangular-PDF 1-LSB noise chống bệt màu trên swapchain 8-bit)
    vec3 outRgb = clamp(cleanRgb + vec3((d0 + d1 - 1.0) * (1.0 / 255.0)), 0.0, 1.0);
    finalColor = vec4(outRgb, outLuma);
}