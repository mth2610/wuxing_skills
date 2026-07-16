#version 330
#include "core/shaders/common/vs_header.glsl"

// ============================================================
// Water Splash — Vertex Shader (Crown Splash GLB Mesh)
// ============================================================

uniform float u_customParam1; // Splash progress (0.0 -> 1.0)
uniform float u_customParam2; // Random phase offset (0.0 -> 10.0)

void main() {
    vec3 pos = vertexPosition;
    float t  = u_customParam1;
    
    // ── GIAI ĐOẠN 1: CYLINDER SQUEEZE → BUNG XÒE ──────────────
    // Ép toàn bộ vành ngoài thành hình trụ hẹp lúc t=0,
    // rồi buông dần cho mesh trở về hình dáng gốc (crown).
    float squeezeFactor = 1.0 - smoothstep(0.0, 0.40, t);
    float origR         = length(pos.xz);
    float cylinderR     = 0.25;                                 // bán kính trụ ban đầu
    float targetR       = mix(origR, min(origR, cylinderR), squeezeFactor);
    float radialMul     = targetR / (origR + 0.0001);
    pos.xz *= radialMul;
    
    // ── GIAI ĐOẠN 2: DÂNG LÊN & RƠI XUỐNG ────────────────────
    // Parabol hoàn hảo 0→1→0 đỉnh tại t=0.45 (hơi lệch sớm để
    // pha rơi kéo dài hơn pha dâng — giống trọng lực thật).
    float peakT    = 0.42;
    float riseEase = smoothstep(0.0, peakT, t);
    float fallEase = smoothstep(peakT, 1.0, t);
    float heightMul = riseEase * (1.0 - fallEase * fallEase);   // rơi tăng tốc (quadratic)
    
    // Vành ngoài vọt cao hơn tâm (tâm = 20%, rìa = 140%)
    float normR     = clamp(origR / 2.5, 0.0, 1.0);
    float amplitude = mix(0.20, 1.40, smoothstep(0.15, 0.85, normR));
    pos.y *= heightMul * amplitude;
    
    // Khi rơi, nước bẹp ra thêm một chút (splat)
    float splat = 1.0 + fallEase * 0.35;
    pos.xz *= splat;
    
    // ── RUNG ĐỘNG BỀ MẶT (DUAL-FREQUENCY WOBBLE) ──────────────
    // 2 tần số chồng lớp tạo chuyển động sống động, không lặp.
    vec4 wp = matModel * vec4(pos, 1.0);
    
    float phase   = u_time + u_customParam2;
    float wobble1 = sin(wp.y * 5.0 + wp.x * 3.0 - phase * 8.0) * 0.55;
    float wobble2 = sin(wp.z * 7.0 - wp.y * 2.0 + phase * 5.5) * 0.45;
    float wobble  = wobble1 + wobble2;
    
    // Chỉ rung phần ngọn, gốc bám đất
    float hFactor = clamp(pos.y * 0.8, 0.0, 1.0);
    // Rung dần biến mất khi nước đang rơi xuống
    float wobbleStrength = 0.05 * hFactor * (1.0 - fallEase * 0.7);
    
    // Hướng đẩy: dùng normal nếu hợp lệ, không thì hướng ra ngoài từ tâm
    vec3 pushDir = vertexNormal;
    float nLen   = dot(pushDir, pushDir);
    if (nLen < 0.01) pushDir = normalize(vec3(pos.x, 0.2, pos.z));
    else             pushDir = pushDir / sqrt(nLen);
    
    pos += pushDir * wobble * wobbleStrength;
    
    VS_FinalOutput(pos);
}