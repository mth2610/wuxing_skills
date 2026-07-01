#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"  

// ============================================================
// Water Stream Skill — Fragment Shader
//
// Đặc trưng của skill:
//   - Bảng màu "Xanh Biển Sâu": bottomColor → midColor → topColor
//   - Caustics: vân nước trượt dọc theo u_time
//   - Normal Perturbation: bề mặt nước gồ ghề từ cùng hàm height với VS
//   - Rim glow (Fresnel) màu xanh nhạt + Specular trắng
//   - u_dissolve: fade out êm ái khi hết lifetime
//
// Bugfix so với bản gốc:
//   dU = getIrregularity(uv + vec2(eps,0)) → đúng là vec2(0,eps)
// ============================================================

uniform float u_uvLength; // cùng giá trị với VS, dùng để khớp hàm height
uniform float u_dissolve; // [0..1] — set từ C-side khi skill gần hết thời gian

// Hàm height field của Water Stream — khớp 100% với getDisplacement() trong VS.
// Dùng để tính gradient → perturbNormal(), tạo cảm giác bề mặt nước gồ ghề.
float getIrregularity(vec2 uv) {
    float t   = uv.y / u_uvLength;
    float phi = uv.x * 6.28318;
    float swell = sin(t * 4.0  - u_time * 12.0);
    float bump  = sin(t * 8.0  - u_time * 20.0) * cos(phi * 2.0);
    float irregularity = swell * 0.5 + bump * 0.5;
    float dampen = smoothstep(0.02, 0.15, t) * smoothstep(0.98, 0.85, t);
    return irregularity * dampen * 4.0;
}

void main() {
    // 1. Normal Perturbation — tạo cảm giác bề mặt nước sần sùi, sống động
    const float eps = 0.02;
    vec2 hDelta = vec2(
        getIrregularity(fragTexCoord - vec2(eps, 0.0 )) - getIrregularity(fragTexCoord + vec2(eps, 0.0 )),
        getIrregularity(fragTexCoord - vec2(0.0,  eps)) - getIrregularity(fragTexCoord + vec2(0.0,  eps))
    );
    vec3 normal = perturbNormal(fragNormal, hDelta, 0.5);

    // 2. Lighting vectors & terms (dùng hàm từ lighting.glsl)
    vec3  viewDir  = normalize(viewPos - fragPosition);
    vec3  lightDir = normalize(u_lightDir);
    float fresnel  = calcFresnel(normal, viewDir, 3.0);
    float specular = calcSpecular(normal, lightDir, viewDir, 128.0) * 5.0;

    // 3. Bảng màu Water Stream: Xanh Biển Sâu (Deep Ocean Blue) ─── SKILL-SPECIFIC ───
    float upFactor   = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 topColor    = vec3(0.75, 0.92, 1.00); // bề mặt phía trên: xanh nhạt ánh trắng
    vec3 midColor    = vec3(0.18, 0.55, 0.95); // thân giữa: xanh dương tươi
    vec3 bottomColor = vec3(0.01, 0.08, 0.28); // đáy: xanh tối như đáy biển
    vec3 baseColor   = mix(bottomColor, midColor, pow(upFactor, 1.2));
         baseColor   = mix(baseColor,   topColor, pow(upFactor, 4.0));

    // 4. Caustics: vân nước trượt dọc ─── SKILL-SPECIFIC ───
    vec2  scroll   = fragTexCoord * vec2(1.0, 0.5) - vec2(0.0, u_time * 4.0);
    float caustics = sin(scroll.x * 6.0) * cos(scroll.y * 5.0);
    baseColor += vec3(0.2, 0.5, 0.9) * pow(max(caustics, 0.0), 2.0) * 0.2;

    // 5. Rim glow (Fresnel) màu xanh nhạt
    baseColor += vec3(0.4, 0.8, 1.0) * fresnel * 0.1;

    // 6. Composite + dissolve fade-out
    float alpha = mix(0.3, 0.9, fresnel) * (1.0 - u_dissolve);
    finalColor  = vec4(baseColor + vec3(specular), alpha);
}
