#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_time;

out vec4 finalColor;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p); vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i),              hash(i + vec2(1,0)), u.x),
               mix(hash(i + vec2(0,1)), hash(i + vec2(1,1)), u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    mat2 rot = mat2(0.8776, 0.4794, -0.4794, 0.8776);
    for (int i = 0; i < 3; i++) { v += a * noise(p); p = rot * p * 2.1; a *= 0.5; }
    return v;
}


void main() {
    float typeB = fragColor.b;
    float alpha = fragColor.a;

    float density = 0.0;
    vec3  col     = vec3(0.0);

    // Kéo các lệnh texture lookup ra ngoài nhánh if để tránh lỗi compiler trên Android (Implicit LOD in non-uniform control flow)
    vec2 uv = fragTexCoord;
    vec2 offset = vec2(0.015, 0.015);
    vec4 tex = texture(texture0, uv);
    float maskU = texture(texture0, uv + vec2(0.0, offset.y)).a;
    float maskD = texture(texture0, uv - vec2(0.0, offset.y)).a;
    float maskL = texture(texture0, uv - vec2(offset.x, 0.0)).a;
    float maskR = texture(texture0, uv + vec2(offset.x, 0.0)).a;

    // ═══════════════════════════════════════════════════════════════
    // 1.  RIBBON TRAIL & WISPS (Đuôi chính & Các luồng khí mờ)
    // ═══════════════════════════════════════════════════════════════
    if (typeB < 0.20) {
        float aw = fragTexCoord.x;
        float at = fragTexCoord.y;
        float cx = aw - 0.5;
        
        float coreShape = exp(-cx * cx * 120.0);
        float wingShape = exp(-cx * cx * 15.0); 
        float fade = at * at;
        float energyPulse = sin(at * 20.0 - u_time * 15.0) * 0.5 + 0.5;
        
        // Thêm lớp nhiễu FBM phá vỡ các đường mép, làm luồng khí trông bị xé gió, tơi tả
        float trailNoise = fbm(vec2(at * 12.0 - u_time * 15.0, aw * 8.0));
        
        density = alpha * fade * (wingShape * 0.5 + coreShape * 0.9 + wingShape * energyPulse * 0.4);
        density *= mix(0.4, 1.0, trailNoise); // Trộn nhiễu uốn lượn vào mật độ khí
        density = clamp(density, 0.0, 1.0);

        vec3 bodyGold = vec3(1.00, 0.85, 0.15); 
        vec3 coreGold = vec3(1.00, 0.98, 0.70); 

        col = mix(bodyGold, coreGold, coreShape + energyPulse * 0.3);
    }
    // ═══════════════════════════════════════════════════════════════
    // 2.  SWORD SPRITE  —  1 THANH KIẾM VÀNG KIM CUỘN TRÀO
    // ═══════════════════════════════════════════════════════════════
    else if (typeB < 0.70) {
        float mask = tex.a;

        if (mask < 0.01) discard;
        
        float edge = abs(mask - maskU) + abs(mask - maskD) + abs(mask - maskL) + abs(mask - maskR);
        edge = smoothstep(0.1, 0.6, edge) * mask; 
        
        float cy = abs((uv.y - 0.5) * 2.0);
        float hollowAlpha = pow(cy, 1.5) * 0.85 + 0.15; 
        
        float auraNoise = fbm(uv * vec2(10.0, 3.0) - vec2(u_time * 4.0, 0.0));
        float auraGlow = auraNoise * mask * 0.4; 

        float threadNoise = fbm(vec2(uv.x * 12.0 - u_time * 20.0, uv.y * 60.0));
        float threads = smoothstep(0.65, 0.95, threadNoise) * mask;
        float threadFade = smoothstep(0.0, 0.2, uv.x) * smoothstep(1.0, 0.7, uv.x);
        threads *= threadFade;

        float flowT = u_time * 12.0;
        vec2 flowUV = vec2(uv.x * 8.0 - flowT, uv.y * 12.0 + sin(uv.x * 10.0 - flowT * 0.5) * 2.0);
        float energyFlow = fbm(flowUV);
        float surge = smoothstep(0.4, 0.7, energyFlow) * mask;

        density = (mask * hollowAlpha + auraGlow + threads + surge + edge) * alpha;
        density = clamp(density, 0.0, 1.0);
        if (density < 0.01) discard;

        vec3 tintColor   = vec3(1.00, 0.85, 0.20); 
        vec3 auraColor   = vec3(1.00, 0.75, 0.00); 
        vec3 threadColor = vec3(1.00, 0.95, 0.50); 
        vec3 edgeGlow    = vec3(1.00, 0.90, 0.30); 
        vec3 surgeColor  = vec3(1.00, 0.98, 0.80); 
        vec3 silverFlash = vec3(1.00, 1.00, 0.90);
        
        vec3 baseColor    = tex.rgb * tintColor * hollowAlpha * 1.5;
        vec3 finalAura    = auraColor * auraGlow * 1.5;
        vec3 finalThreads = threadColor * threads * 3.0;
        vec3 finalEdge    = edgeGlow * edge * 3.0;
        vec3 finalSurge   = surgeColor * surge * 2.5; 
        
        float sweepT = fract(u_time * 1.2) * 1.5 - 0.25;
        float sweep  = exp(-pow((uv.x - sweepT) * 15.0, 2.0)) * mask;

        col = baseColor + finalAura + finalThreads + finalSurge + finalEdge + (silverFlash * sweep * 0.7);
    }

    if (density < 0.01) discard;
    finalColor = vec4(col, clamp(density, 0.0, 1.0));
}