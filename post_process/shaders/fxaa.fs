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

uniform sampler2D u_lutTex;
uniform float u_lutEnabled;
uniform float u_lutStrength;
uniform vec2  u_lutParams;
uniform float u_lutSize;

uniform float u_radialBlurEnabled;
uniform vec2  u_radialBlurCenter;
uniform float u_radialBlurStrength;
uniform float u_radialBlurFalloff;

uniform float u_hueRestore;
uniform float u_shoulderView;

out vec4 finalColor;

vec3 ApplyColorGradeLut(vec3 col) {
    float size = u_lutSize;
    vec3 c = clamp(col, 0.0, 1.0);

    float b = c.b * (size - 1.0);
    float slice0 = floor(b);
    float slice1 = min(slice0 + 1.0, size - 1.0);
    float blend = b - slice0;

    float uInTile = 0.5 + c.r * (size - 1.0);
    float v = (0.5 + c.g * (size - 1.0)) * u_lutParams.y;

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
    vec3 perChannel = acesFilmic(x);
    if (u_hueRestore <= 0.0) return perChannel;

    float peak = max(x.r, max(x.g, x.b));
    if (peak <= 0.0) return perChannel;

    float w = clamp(u_hueRestore, 0.0, 1.0);
    if (w <= 0.0) return perChannel;

    vec3 hueKept = mix(x / peak, vec3(1.0), smoothstep(5.0, 12.0, peak)) * acesFilmicScalar(peak);
    return mix(perChannel, hueKept, w);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 toCenter = uv - vec2(0.5);

    // 1. Lấy mẫu gốc
    vec4 sceneCol = texture(texture0, uv);

    // 1a. Chromatic Aberration
    if (u_chromaticEnabled > 0.5) {
        float distSq = dot(toCenter, toCenter);
        vec2 offset = toCenter * (u_chromaticStrength * distSq * 0.05);
        sceneCol.r = texture(texture0, uv - offset).r;
        sceneCol.b = texture(texture0, uv + offset).b;
    }

    // 1b. Radial Blur (đã loại bỏ lệnh sqrt/length thứ 2)
    if (u_radialBlurEnabled > 0.5) {
        vec2 dir = uv - u_radialBlurCenter;
        float dirLen = length(dir);
        float w = smoothstep(0.0, u_radialBlurFalloff, dirLen);

        vec2 c = u_radialBlurCenter;
        float off = max(max(-c.x, c.x - 1.0), max(-c.y, c.y - 1.0));
        w *= 1.0 - smoothstep(0.0, 0.35, off);

        if (w > 0.001 && dirLen > 1e-5) {
            float smearScale = min(u_radialBlurStrength * w, 0.08 / dirLen);
            vec2 stepUV = dir * (smearScale * 0.125);
            vec2 sampleUV = uv;
            vec3 acc = vec3(0.0);
            for (int i = 0; i < 8; i++) {
                sampleUV -= stepUV;
                acc += texture(texture0, sampleUV).rgb;
            }
            sceneCol.rgb = mix(sceneCol.rgb, acc * 0.125, w);
        }
    }

    // 2. Bloom (4-tap tent filter, bỏ phép * 4.0 / 16.0 thừa)
    if (u_bloomEnabled > 0.5 && u_bloomIntensity > 0.0) {
        vec4 d = u_bloomTexel.xyxy * vec4(-0.5, 0.5, 0.5, -0.5);
        vec3 bloomSum = texture(u_bloomTex, uv + d.xy).rgb
                      + texture(u_bloomTex, uv + d.zy).rgb
                      + texture(u_bloomTex, uv + d.xw).rgb
                      + texture(u_bloomTex, uv + d.zw).rgb;
        sceneCol.rgb += bloomSum * (0.25 * u_bloomIntensity);
    }

    // 2b. Tone mapping
    float exposure = u_exposure;
    if (u_autoExposure > 0.0) {
        float autoE = texture(u_exposureTex, vec2(0.5)).r;
        if (autoE > 0.0) exposure *= mix(1.0, autoE, u_autoExposure);
    }
    vec3 exposedScene = sceneCol.rgb * exposure;
    if (u_tonemapEnabled > 0.5) {
        sceneCol.rgb = toneMapScene(exposedScene);
    }

    // 3. Color Grading (Bảo vệ chi tiết vùng tối & chống bệt màu cỏ trên cảnh ban ngày)
    if (u_colorGradeEnabled > 0.5) {
        const vec3 LUMA_COEFF = vec3(0.2126, 0.7152, 0.0722);
        vec3 origCol = sceneCol.rgb;

        // Contrast tuyến tính kết hợp bảo vệ vùng tối (Shadow-toe protection) để không
        // đẩy các kênh màu dưới gốc cỏ xuống giá trị âm gây bết đen
        vec3 contrasted = (origCol - vec3(0.5)) * u_contrast + vec3(0.5);
        vec3 softShadow = origCol * (1.0 / max(u_contrast, 1.0));
        float shadowWeight = smoothstep(0.0, 0.22, dot(origCol, LUMA_COEFF));
        vec3 gradedCol = max(mix(softShadow, contrasted, shadowWeight), vec3(0.0));

        float luma = dot(gradedCol, LUMA_COEFF);

        // Giữ nguyên độ rực ở midtone/VFX nhưng giảm nhẹ lực kéo bão hòa ở vùng tối sâu (< 0.12)
        // để bóng râm dưới cỏ không bị ám màu bệt
        float satProtect = (u_saturation > 1.0) ? mix(0.82, 1.0, smoothstep(0.02, 0.16, luma)) : 1.0;
        float effSat = 1.0 + (u_saturation - 1.0) * satProtect;
        gradedCol = max(mix(vec3(luma), gradedCol, effSat), vec3(0.0));

        // Split-tone & Color Tint
        gradedCol *= mix(u_shadowTint, u_highlightTint, smoothstep(0.0, 1.0, luma));
        sceneCol.rgb = gradedCol * u_colorTint;
    }

    // 3b. Colour-grade LUT
    if (u_lutEnabled > 0.5 && u_lutStrength > 0.0) {
        vec3 graded = ApplyColorGradeLut(sceneCol.rgb);
        sceneCol.rgb = mix(sceneCol.rgb, graded, clamp(u_lutStrength, 0.0, 1.0));
    }

    // 4. Vignette
    if (u_vignetteEnabled > 0.5) {
        float len = length(toCenter);
        float innerRadius = max(u_vignetteRadius - u_vignetteSoftness, 0.0);
        float darkness = smoothstep(innerRadius, u_vignetteRadius, len);
        sceneCol.rgb *= (1.0 - darkness);
    }

    // 5. Output dither (Tối ưu: chỉ tính dot() 1 lần duy nhất cho cả d0 và d1)
    float dBase = dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715));
    float d0 = fract(52.9829189 * fract(dBase));
    float d1 = fract(52.9829189 * fract(dBase + 0.24011107));
    sceneCol.rgb = clamp(sceneCol.rgb + vec3((d0 + d1 - 1.0) * (1.0 / 255.0)), 0.0, 1.0);

    // Shoulder diagnostic view
    if (u_shoulderView > 0.5) {
        float peak = max(exposedScene.r, max(exposedScene.g, exposedScene.b));
        float g = dot(sceneCol.rgb, vec3(0.2126, 0.7152, 0.0722)) * 0.30;
        if      (peak >= 9.0) sceneCol.rgb = vec3(0.0, 0.9, 1.0);
        else if (peak >= 1.0) sceneCol.rgb = vec3(1.0, 0.0, 0.8);
        else                  sceneCol.rgb = vec3(g);
    }

    finalColor = vec4(sceneCol.rgb * fragColor.rgb, 1.0);
}