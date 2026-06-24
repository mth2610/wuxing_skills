#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform float u_time;

out vec4 finalColor;

// ─────────────────── Noise helpers ───────────────────
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
    for (int i = 0; i < 3; i++) { v += a * noise(p);
    p = rot * p * 2.1; a *= 0.5; }
    return v;
}

void main() {
    float typeB = fragColor.b;
    float alpha = fragColor.a;

    float density = 0.0;
    vec3  col     = vec3(0.0);

    // ═══════════════════════════════════════════════════════════════
    // 1.  RIBBON TRAIL  —  typeB ≈ 0.176  (< 0.20)
    // ═══════════════════════════════════════════════════════════════
    if (typeB < 0.20) {
        float aw = fragTexCoord.x;
        float at = fragTexCoord.y;
        float cx = aw - 0.5;

        // Lõi ánh sáng cực sắc nét
        float coreShape = exp(-cx * cx * 120.0);
        // Vầng glow tỏa mượt mà ra xung quanh
        float wingShape = exp(-cx * cx * 20.0);
        
        // Mờ dần về đuôi (tail)
        float fade = at * at;

        // Tạo gợn sóng năng lượng (Sine wave) chạy dọc theo đuôi
        float energyPulse = sin(at * 20.0 - u_time * 15.0) * 0.5 + 0.5;

        density = alpha * fade * (wingShape * 0.4 + coreShape * 0.9 + wingShape * energyPulse * 0.4);
        density = clamp(density, 0.0, 1.0);

        // BẢNG MÀU KIẾM KHÍ (Cam vàng -> Vàng chói)
        vec3 bodyGold  = vec3(1.00, 0.60, 0.05); // Vàng cam ấm
        vec3 coreWhite = vec3(1.00, 0.95, 0.70); // Trắng vàng lóa

        // Trộn màu: Lõi sáng lóa, viền cam vàng, cộng thêm nhịp đập năng lượng
        col = mix(bodyGold, coreWhite, coreShape + energyPulse * 0.3);
    }
    // ═══════════════════════════════════════════════════════════════
    // 2.  SWORD SPRITE  —  typeB ≈ 0.502  (0.20 … 0.70)
    // ═══════════════════════════════════════════════════════════════
   // ═══════════════════════════════════════════════════════════════
    // 2.  SWORD SPRITE  —  typeB ≈ 0.502  (0.20 … 0.70)
    // ═══════════════════════════════════════════════════════════════
    else if (typeB < 0.70) {
        vec2 uv = fragTexCoord;
        vec4 tex = texture(texture0, uv);
        float mask = tex.a;

        if (mask < 0.01) discard;

        // 1. TẠO KIẾM RỖNG TRONG SUỐT
        float cy = abs((uv.y - 0.5) * 2.0); 
        float hollowAlpha = pow(cy, 1.5) * 0.85 + 0.15; 
        
        // 2. DÒNG CHẢY LINH KHÍ NỀN (Cuộn êm ả)
        float auraNoise = fbm(uv * vec2(10.0, 3.0) - vec2(u_time * 4.0, 0.0));
        float auraGlow = auraNoise * mask * 0.4; 

        // 3. SỢI KIẾM KHÍ CHẠY DỌC (Sắc, mảnh, lướt nhanh)
        // Nhân uv.y với 60.0 để ép dẹp thành các sợi chỉ, uv.x chạy tốc độ cao (20.0)
        float threadNoise = fbm(vec2(uv.x * 12.0 - u_time * 20.0, uv.y * 60.0));
        
        // Dùng smoothstep cắt gắt để lọc lấy các sợi sáng nhất
        float threads = smoothstep(0.65, 0.95, threadNoise) * mask;
        
        // Làm mờ (fade) sợi khí ở 2 đầu mũi/chuôi để nó hòa quyện tự nhiên vào thân kiếm
        float threadFade = smoothstep(0.0, 0.2, uv.x) * smoothstep(1.0, 0.7, uv.x);
        threads *= threadFade;

        // Tổng hợp độ đục: Kiếm rỗng + Khí nền + Sợi khí
        density = (mask * hollowAlpha + auraGlow + threads) * alpha;
        density = clamp(density, 0.0, 1.0);
        if (density < 0.01) discard;
        
        // BẢNG MÀU
        vec3 tintColor = vec3(1.00, 0.80, 0.20); // Vàng kim (màu gốc)
        vec3 auraColor = vec3(1.00, 0.90, 0.40); // Vàng linh khí nền
        vec3 threadColor = vec3(1.00, 0.98, 0.90); // Trắng lóa cho sợi kiếm khí
        
        vec3 baseColor = tex.rgb * tintColor * hollowAlpha * 1.5;
        vec3 finalAura = auraColor * auraGlow * 1.5;
        vec3 finalThreads = threadColor * threads * 3.0; // Kích sáng chói cho sợi chỉ

        // 4. Vệt chớp lướt dọc
        float sweepT = fract(u_time * 1.2) * 1.5 - 0.25;  
        float sweep  = exp(-pow((uv.x - sweepT) * 15.0, 2.0)) * mask;

        // Trộn tổng thể
        col = baseColor + finalAura + finalThreads + (vec3(1.0, 0.98, 0.8) * sweep * 0.5);
    }
    // ═══════════════════════════════════════════════════════════════
    // 3.  PORTAL  —  typeB ≈ 0.863  (≥ 0.70)
    // ═══════════════════════════════════════════════════════════════
    else {
        vec2  uv  = fragTexCoord - vec2(0.5);
        float r   = length(uv);
        float ang = atan(uv.y, uv.x);
        
        // ── Inner void: pitch-black center hole ──
        float innerVoid = smoothstep(0.07, 0.17, r);
        // ── Three concentric chrome rings ──
        float ring0 = exp(-pow((r - 0.11) * 45.0, 2.0));
        float ring1 = exp(-pow((r - 0.28) * 24.0, 2.0));
        float ring2 = exp(-pow((r - 0.43) * 15.0, 2.0));

        // ── Dual counter-rotating spirals ──
        float sp1 = sin(ang * 7.0 - r * 22.0 - u_time * 14.0) * 0.5 + 0.5;
        float sp2 = sin(ang * 5.0 + r * 15.0 + u_time * 9.0) * 0.5 + 0.5;
        float spiralZone = smoothstep(0.08, 0.46, r) * smoothstep(0.50, 0.28, r);
        
        // ── Rotating 6-blade metallic arcs ──
        float blades = pow(max(0.0, sin(ang * 6.0 - u_time * 8.5)), 10.0);
        float bladeMask = smoothstep(0.10, 0.38, r) * smoothstep(0.48, 0.26, r);
        
        // ── Soft outer aura ──
        float aura = exp(-r * r * 7.0) * smoothstep(0.50, 0.18, r);
        
        // ── FBM surface shimmer on main ring ──
        vec2 noiseUV = uv * 6.5 + vec2(cos(u_time * 0.85) * 0.4, sin(u_time * 1.15) * 0.4);
        float shimmer = fbm(noiseUV);

        density = alpha * innerVoid * (
            ring0 * 0.75
          + ring1 * (0.65 + 0.35 * sp1 * shimmer)
          + ring2 * (0.45 + 0.25 * sp2)
          + blades * bladeMask * 0.95
          + aura * 0.45
        );
        density = clamp(density, 0.0, 1.0);

        // BẢNG MÀU CỔNG DỊCH CHUYỂN HỆ KIM
        vec3 voidCol  = vec3(0.12, 0.05, 0.00);  // Hư không màu nâu thẫm
        vec3 platCol  = vec3(1.00, 0.90, 0.40);  // Vòng bạch kim ngả vàng
        vec3 goldCol  = vec3(1.00, 0.65, 0.10);  // Viền ngoài vàng cam
        vec3 bladeCol = vec3(1.00, 0.98, 0.85);  // Cánh quạt sáng lóa
        vec3 auraCol  = vec3(1.00, 0.80, 0.20);  // Vầng sáng vàng

        col  = voidCol;
        col  = mix(col, platCol, ring0 * 0.90);
        col  = mix(col, platCol, ring1 * (0.55 + 0.45 * sp1));
        col  = mix(col, goldCol, ring2 * 0.85);
        col += bladeCol * blades * bladeMask * 1.0;
        col += auraCol  * aura  * 0.75;
        col += platCol  * sp1   * ring1 * 0.35;
    }

    if (density < 0.01) discard;
    finalColor = vec4(col, clamp(density, 0.0, 1.0));
}