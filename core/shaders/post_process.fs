#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   
uniform sampler2D u_bloomTex; 

// Cấu hình (Nhận giá trị 0.0 hoặc 1.0 thay cho int)
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

// Split-toning (Đợt G — cinematic color). Multiplicative tints applied by
// luminance: shadows lean one way (cool moonlight), highlights the other
// (warm), giving depth/mood that a single flat tint can't. Set to (1,1,1) to
// disable. ACES desaturates, so this + a saturation lift restore richness.
uniform vec3 u_shadowTint;
uniform vec3 u_highlightTint;

// Tone mapping (Đợt G1 — cinematic base). ACES filmic approximation
// (Narkowicz) — cheap, GLES-friendly, no mat3. Rolls bright bloom off to
// white smoothly instead of clipping, and gives the whole frame a filmic
// contrast/color response. u_exposure scales scene brightness pre-curve.
uniform float u_tonemapEnabled;
uniform float u_exposure;

out vec4 finalColor;

vec3 acesFilmic(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec2 uv = fragTexCoord;
    vec4 sceneCol;

    // 1. Chromatic Aberration (Tách màu bằng mix)
    vec2 toCenter = uv - vec2(0.5);
    float dist = length(toCenter);
    vec2 offset = toCenter * u_chromaticStrength * dist * 0.05;
    
    vec4 chromCol;
    chromCol.r = texture(texture0, uv - offset).r;
    chromCol.g = texture(texture0, uv).g;
    chromCol.b = texture(texture0, uv + offset).b;
    chromCol.a = texture(texture0, uv).a;
    
    sceneCol = mix(texture(texture0, uv), chromCol, u_chromaticEnabled);

    // 2. Bloom (added in a linear-ish HDR range; can exceed 1.0)
    vec4 bloomCol = texture(u_bloomTex, uv);
    sceneCol.rgb += (bloomCol.rgb * u_bloomIntensity) * u_bloomEnabled;

    // 2b. Tone mapping — exposure then ACES filmic. Highlights (esp. bloom)
    // roll off to white instead of clipping; the frame gets a filmic feel.
    vec3 toned = acesFilmic(sceneCol.rgb * u_exposure);
    sceneCol.rgb = mix(sceneCol.rgb, toned, u_tonemapEnabled);

    // 3. Color Grading (on the tone-mapped LDR result)
    vec3 gradedCol = sceneCol.rgb;
    // Contrast
    gradedCol = (gradedCol - vec3(0.5)) * u_contrast + vec3(0.5);
    // Saturation
    float luma = dot(gradedCol, vec3(0.2126, 0.7152, 0.0722));
    gradedCol = mix(vec3(luma), gradedCol, u_saturation);
    // Split-tone: cool shadows ↔ warm highlights by luminance (cinematic mood).
    float toneLuma = clamp(dot(gradedCol, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
    gradedCol *= mix(u_shadowTint, u_highlightTint, smoothstep(0.0, 1.0, toneLuma));
    // Color Tint (overall)
    gradedCol *= u_colorTint;
    
    sceneCol.rgb = mix(sceneCol.rgb, gradedCol, u_colorGradeEnabled);

    // 4. Vignette
    float len = length(uv - vec2(0.5));
    float darkness = smoothstep(u_vignetteRadius - u_vignetteSoftness, u_vignetteRadius, len);
    sceneCol.rgb = mix(sceneCol.rgb, sceneCol.rgb * (1.0 - darkness), u_vignetteEnabled);

    finalColor = sceneCol * fragColor;
}