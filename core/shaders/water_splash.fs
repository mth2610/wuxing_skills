#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/lighting.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/fx.glsl"

// ============================================================
// Water Splash — Fragment Shader (Crown Splash GLB Mesh)
// ============================================================

uniform sampler2D texture0;
uniform vec4  u_baseColor;
uniform float u_translucency;
uniform float u_rimStrength;
uniform float u_fresnelPower;
uniform float u_emissiveIntensity;
uniform float u_customParam1;   // splash progress 0→1
uniform float u_customParam2;   // random phase offset

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);
    vec3 L = normalize(u_lightDir);
    float t = u_customParam1;
    
    // ── 1. CAUSTIC SHIMMER (ÁNH LÓNG LÁNH MẶT NƯỚC) ──────────
    // FBM noise lăn theo world-space XZ tạo vân nước dao động.
    vec2 causticUV = fragPosition.xz * 1.8 + vec2(u_time * 0.3, u_time * 0.2);
    causticUV += vec2(u_customParam2);               // lệch pha ngẫu nhiên mỗi lần bắn
    float caustic = fbm2(causticUV);
    // Boost caustic thành ánh lóe sắc nét
    float shimmer = smoothstep(0.45, 0.70, caustic) * 0.6;
    // Mờ dần khi nước đang tan biến
    shimmer *= (1.0 - smoothstep(0.5, 0.9, t));
    
    // ── 2. DISSOLVE (TAN BIẾN CUỐI ĐỜI) ───────────────────────
    // Noise 3D theo world-space để dissolve không phụ thuộc UV.
    float noiseVal = hash3(floor(fragPosition * 8.0));
    float dissolveT = smoothstep(0.50, 1.0, t);      // bắt đầu tan từ nửa đời
    
    float edgeFactor;
    float discardFlag = dissolveCalc(noiseVal, dissolveT, 0.08, edgeFactor);
    if (discardFlag >= 1.0) discard;
    
    // Viền tan biến phát sáng theo màu nguyên tố
    vec3 dissolveEdgeColor = u_baseColor.rgb * 2.5;
    
    // ── 3. ÁNH SÁNG ───────────────────────────────────────────
    float diffuse = calcDiffuse(N, L, 0.18);
    float fresnel = calcFresnel(N, V, u_fresnelPower);
    
    // Specular kép: highlight nhỏ sắc (256) + highlight to mềm (48)
    float specSharp = calcSpecular(N, L, V, 256.0) * 2.5;
    float specSoft  = calcSpecular(N, L, V, 48.0)  * 0.8;
    float spec      = specSharp + specSoft;
    // Specular mờ đi khi tan biến (tránh đốm sáng bay lơ lửng)
    spec *= (1.0 - dissolveT);
    
    // ── 4. TỔ HỢP MÀU ─────────────────────────────────────────
    vec3 color = u_baseColor.rgb * diffuse;
    
    // Fresnel rim glow
    color += fresnel * u_rimStrength * u_baseColor.rgb;
    
    // Emissive inner glow
    color += u_baseColor.rgb * u_emissiveIntensity;
    
    // Caustic shimmer lấp lánh
    color += vec3(shimmer) * mix(vec3(1.0), u_baseColor.rgb, 0.4);
    
    // Specular highlight
    color += vec3(spec);
    
    // Dissolve edge glow
    color = mix(color, dissolveEdgeColor, edgeFactor * 0.7);
    
    // ── 5. ALPHA ───────────────────────────────────────────────
    // Fade toàn cục êm ái ở 15% cuối đời
    float globalFade = 1.0 - smoothstep(0.85, 1.0, t);
    float alpha = u_baseColor.a * u_translucency * globalFade;
    // Dissolve cũng ăn dần alpha ở vùng gần mép
    alpha *= mix(1.0, 0.4, edgeFactor);
    
    finalColor = vec4(color, alpha);
}