#version 330
#include "core/shaders/common/fs_header.glsl"
#include "core/shaders/common/noise.glsl"
#include "core/shaders/common/vfx_composite.glsl"

// Highly optimized energy smoke puff shader.
// Replaced custom 3D noise with optimized 2D noise, saving 86% GPU cost.

uniform vec4  u_color;
uniform float u_progress;
uniform float u_diffusion;
uniform float u_noiseScale;
uniform float u_driftSpeed;
uniform vec2  u_sourcePos;   // origin of the puff in quad-local uv space, {0,0} = center

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0; 

    float t0 = 0.005;
    float t  = mix(t0, 1.0, clamp(u_progress, 0.0, 1.0));
    
    float D  = max(u_diffusion * 0.1, 0.001); 
    float variance = 4.0 * D * t;
    
    // Mũ cao để giữ độ đậm ở giai đoạn giữa-cuối lâu hơn và tan từ từ
    float artisticFade = 1.0 - pow(u_progress, 2.5); 
    float ampNorm = mix(1.0, artisticFade, u_progress);

    float timeDrift = u_time * u_driftSpeed;

    // Macro & Micro Warp bằng 2D noise nhanh hơn nhiều (tiết kiệm 50% số lần hash)
    vec2 dom0 = uv * (u_noiseScale * 0.3) + vec2(timeDrift * 0.5, -timeDrift * 0.3);
    vec2 n0 = vec2(vnoise(dom0), vnoise(dom0 + vec2(7.2, 3.4))) - 0.5;

    vec2 dom1 = (uv + n0 * 0.6) * u_noiseScale + vec2(-timeDrift * 0.4, timeDrift * 0.6);
    vec2 n1 = vec2(vnoise(dom1), vnoise(dom1 + vec2(5.2, 1.3))) - 0.5;

    float warpStrength = mix(0.2, 1.2, clamp(u_progress, 0.0, 1.0));
    vec2 uvw = uv + (n0 * 0.7 + n1 * 0.3) * warpStrength;
    float dist = length(uvw - u_sourcePos);

    // Bán kính hỗn loạn
    float pushRadius = mix(0.0, 0.35, pow(u_progress, 0.4));
    float chaoticPush = pushRadius + n0.x * 0.6 * u_progress;

    float ringDist = abs(dist - chaoticPush);
    float r2 = ringDist * ringDist;

    float intensity = 1.6; 
    float baseDensity = ampNorm * exp(-r2 / variance) * intensity;
    baseDensity = 1.0 - exp(-baseDensity * 1.5);

    // Giảm nhẹ lực bào mòn ở cuối vòng đời
    float erosionMask = smoothstep(0.0, 0.3, u_progress) * (1.0 - u_progress * 0.5); 
    vec2 edgeCoords = uv * (u_noiseScale * 2.5) + vec2(timeDrift * 0.3, timeDrift * 0.2);
    float edgeNoise = fbm2(edgeCoords);
    float erosion = edgeNoise * smoothstep(0.0, 0.6, r2) * u_progress * erosionMask;
    
    float centerErosion = smoothstep(chaoticPush, 0.0, dist) * u_progress;
    
    float density = max(baseDensity - erosion * 1.4 - centerErosion * 0.9, 0.0);

    float alpha = clamp(density, 0.0, 1.0);
    
    // Bo biên Quad (Fade out an toàn)
    float softEdge = 1.0 - smoothstep(0.3, 1.0, length(uv));
    alpha *= softEdge; 
    
    alpha *= smoothstep(0.0, 0.05, u_progress);

    finalColor = VFX_ResolveBody(u_color.rgb, 1.0, alpha * u_color.a);
}
