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

out vec4 finalColor;

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

    // 2. Bloom
    vec4 bloomCol = texture(u_bloomTex, uv);
    sceneCol.rgb += (bloomCol.rgb * u_bloomIntensity) * u_bloomEnabled;

    // 3. Color Grading
    vec3 gradedCol = sceneCol.rgb;
    // Contrast
    gradedCol = (gradedCol - vec3(0.5)) * u_contrast + vec3(0.5);
    // Saturation
    float luma = dot(gradedCol, vec3(0.2126, 0.7152, 0.0722));
    gradedCol = mix(vec3(luma), gradedCol, u_saturation);
    // Color Tint
    gradedCol *= u_colorTint;
    
    sceneCol.rgb = mix(sceneCol.rgb, gradedCol, u_colorGradeEnabled);

    // 4. Vignette
    float len = length(uv - vec2(0.5));
    float darkness = smoothstep(u_vignetteRadius - u_vignetteSoftness, u_vignetteRadius, len);
    sceneCol.rgb = mix(sceneCol.rgb, sceneCol.rgb * (1.0 - darkness), u_vignetteEnabled);

    finalColor = sceneCol * fragColor;
}