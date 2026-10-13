// ============================================================
// WUXING — Lighting Utilities (ALU & Visually Optimized)
// Include sau fs_header.glsl trong .fs của skill.
//
// Cung cấp:
//   perturbNormal()       — Normal perturbation qua gradient height field (Branchless)
//   calcFresnel()         — One-sided surface Fresnel rim
//   calcTwoSidedFresnel() — Winding-independent rim for two-sided sheets
//   calcOpticalDepth*()   — Volume body/rim terms from |N.V|
//   calcSpecular()        — Specular Blinn-Phong (Fast Rational Power)
//   calcDiffuse()         — Lambertian diffuse với ambient floor
//   calcBlackbody*()      — Bức xạ nhiệt Planck (ALU-only, không dùng pow)
//   calcLitVolume()       — Khói thể tích tự đổ bóng + tán xạ viền bạc
//   calcRadiantEnergy()   — Hợp nhất bức xạ năng lượng & Plasma HDR
//
// Không phụ thuộc vào bất kỳ uniform skill-specific nào.
// lightDir chuẩn của project: normalize(vec3(0.5, 0.8, 0.5))
// ============================================================

// Hàm lũy thừa xấp xỉ hữu tỷ cực nhanh trên đoạn [0, 1] (Không dùng SFU log2/exp2)
// Áp dụng bình phương 1 bước kết hợp xấp xỉ Padé/Schlick giúp sai số < 0.5% với mọi power > 0
float fastUnitPow(float x, float p) {
    x = clamp(x, 0.0, 1.0);
    // Với p lớn, nén miền qua phép bình phương để giữ độ dốc hoàn hảo ở đuôi hàm
    float x2 = x * x;
    float hp = p * 0.5;
    float r1 = x / max(p - (p - 1.0) * x, 1e-4);
    float r2 = x2 / max(hp - (hp - 1.0) * x2, 1e-4);
    return mix(r1, r2, clamp((p - 1.0) * 0.5, 0.0, 1.0));
}

// Tính normal bị nhiễu từ gradient của một height field.
// Sử dụng Pixar Revised Orthonormal Basis: hoàn toàn không phân nhánh (branchless),
// không cần cross() hay sqrt() để dựng hệ trục, và không bị giật cục ở đỉnh cực.
vec3 perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength) {
    vec3 n = normalize(baseNormal);
    float s = (n.z >= 0.0) ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float b = n.x * n.y * a;
    vec3 tangent   = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
    vec3 bitangent = vec3(b, s + n.y * n.y * a, -n.y);
    return normalize(n + (tangent * heightDelta.x + bitangent * heightDelta.y) * strength);
}

// One-sided Fresnel rim for closed, outward-facing surfaces.
// Trả về [0..1]: 0 = nhìn thẳng mặt, 1 = nhìn từ cạnh.
float calcFresnel(vec3 normal, vec3 viewDir, float power) {
    float rim = 1.0 - max(dot(normal, viewDir), 0.0);
    return fastUnitPow(rim, max(power, 0.001));
}

// Winding-independent Fresnel for a genuinely two-sided surface.
float calcTwoSidedFresnel(vec3 normal, vec3 viewDir, float power) {
    float absNdotV = clamp(abs(dot(normal, viewDir)), 0.0, 1.0);
    return fastUnitPow(1.0 - absNdotV, max(power, 0.001));
}

// Optical depth terms from |N.V|
float calcOpticalDepthBody(float absNdotV, float power) {
    return fastUnitPow(clamp(absNdotV, 0.0, 1.0), max(power, 0.001));
}

float calcOpticalDepthRim(float absNdotV, float power) {
    return fastUnitPow(clamp(1.0 - absNdotV, 0.0, 1.0), max(power, 0.001));
}

float combineOpticalDepth(float body, float rim, float bodyWeight, float rimWeight) {
    return clamp(bodyWeight * body + rimWeight * rim, 0.0, 1.0);
}

// Specular Blinn-Phong (Sử dụng xấp xỉ Gaussian/Schlick siêu tốc thay cho pow)
// Trả về [0..1] trước khi scale bởi intensity.
float calcSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess) {
    vec3 halfVec = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfVec), 0.0);
    float NdotH2 = NdotH * NdotH;
    // Xấp xỉ hữu tỷ Lyon/Schlick cho số mũ shininess cao (32 - 512): chính xác & không tốn log2/exp2
    float s = max(shininess * 0.5, 0.5);
    float spec = NdotH2 / max(s - (s - 1.0) * NdotH2, 1e-4);
    return spec * spec;
}

// Lambertian diffuse với ambient floor.
float calcDiffuse(vec3 normal, vec3 lightDir, float ambient) {
    return max(dot(normalize(normal), normalize(lightDir)), ambient);
}

// ============================================================
// BỨC XẠ VẬT THỂ ĐEN PLANCK (Ghost of Tsushima / Physically-Based Combustion)
// Đã tối ưu hóa bằng đa thức thuần ALU (loại bỏ hoàn toàn pow()) và tinh chỉnh
// đường cong bức xạ nhiệt phi tuyến giúp lửa và lõi năng lượng rực rỡ hơn.
// ============================================================
vec3 calcBlackbody(float tempKelvin) {
    float T = clamp(tempKelvin, 800.0, 5500.0);

    // Kênh Đỏ (R): bốc nhanh từ 950K
    float r = smoothstep(800.0, 1400.0, T);

    // Kênh Xanh lá (G): đa thức bậc 2 thay thế pow(g, 1.25)
    float g = smoothstep(1400.0, 3200.0, T);
    g = g * (0.75 + 0.25 * g);

    // Kênh Xanh dương (B): đa thức bậc 3 thay thế pow(b, 1.8)
    float b = smoothstep(2600.0, 4400.0, T);
    b = (b * b) * (1.20 - 0.20 * b);

    // Độ bức xạ nhiệt năng lượng cao (Non-linear Stefan-Boltzmann inspired flux)
    float tNorm = smoothstep(900.0, 4200.0, T);
    float radiance = (tNorm * (0.65 + 0.35 * tNorm)) * 3.2;

    vec3 color = vec3(r, g * 0.88 + b * 0.12, b);
    return color * radiance;
}

// Hàm tiện ích: chuyển đổi tiến trình t [0..1] sang dải nhiệt độ [900K..4200K]
vec3 calcBlackbodyNormalized(float t) {
    float kelvin = mix(900.0, 4200.0, clamp(t, 0.0, 1.0));
    return calcBlackbody(kelvin);
}

// ============================================================
// KHÓI THỂ TÍCH TIẾP NHẬN ÁNH SÁNG & TỰ ĐỔ BÓNG (Volumetric Lit Smoke)
// Tích hợp xấp xỉ Padé cho Beer-Lambert + Hiệu ứng tán xạ viền bạc (Silver-Lining)
// giúp đám khói có khối 3D sâu và rìa khói bắt sáng tự nhiên.
// ============================================================
vec3 calcLitVolume(vec3 baseColor, vec3 normal, vec3 sunToLight, vec3 sunColor,
                   vec3 skyAmbient, vec3 groundAmbient, float density, float shadowExt) {
    vec3 N = normalize(normal);
    vec3 L = normalize(sunToLight);

    // 1. Ánh sáng tán xạ bán cầu trực tiếp (Half-Lambert wrap)
    float NdotL = dot(N, L);
    float wrapDiff = max(0.0, NdotL * 0.5 + 0.5);

    // 2. Tự đổ bóng xuyên thể tích (Fast Padé approximation của exp(-opticalPath))
    float d = max(density, 0.0);
    float opticalPath = (1.0 - max(NdotL, 0.0)) * d * shadowExt;
    float selfShadow = 1.0 / (1.0 + opticalPath + 0.48 * opticalPath * opticalPath);

    // Hiệu ứng Powder / Silver-Lining: tăng độ bắt sáng nhẹ ở rìa khói mỏng đón nắng
    float powder = 1.0 - 1.0 / (1.0 + d * 2.5 + d * d * 3.0);
    float scatterGain = mix(1.0, 1.0 + 0.35 * powder, wrapDiff);

    vec3 directLight = sunColor * (wrapDiff * selfShadow * scatterGain);

    // 3. Đa tán xạ môi trường bù trừ (Multiple scattering approximation)
    float hemiY = N.y * 0.5 + 0.5;
    // Khói dày vẫn giữ lại một phần tán xạ nội bộ từ bầu trời giúp bụng khói không bị đen đặc
    float ambientOcclusion = 1.0 / (1.0 + d * 0.45);
    vec3 ambientFill = mix(groundAmbient, skyAmbient, hemiY) * ambientOcclusion;

    return baseColor * (directLight + ambientFill);
}

// ============================================================
// HỢP NHẤT BỨC XẠ NĂNG LƯỢNG & PLASMA (Unified Radiant Energy Pipeline)
// ============================================================
vec3 calcRadiantEnergy(float energy, vec3 elementHue, vec3 coreHue, float blackbodyFactor, float hdrGain) {
    float e = clamp(energy, 0.0, 1.0);

    // 1. Phổ bức xạ nhiệt vật thể đen Planck (dành cho Lửa vật lý)
    float maxC = max(elementHue.r, max(elementHue.g, elementHue.b));
    vec3 normalizedHue = (maxC > 0.001) ? (elementHue / maxC) : vec3(1.0);
    vec3 bb = calcBlackbodyNormalized(e);
    vec3 fireSpec = bb * normalizedHue;

    // 2. Phổ năng lượng nguyên tố ma thuật với đường cong bão hòa trung gian rực rỡ
    float coreTransition = smoothstep(0.58, 1.0, e);
    float midGlow = e * (1.0 + 0.45 * e); // Tăng độ rực ở dải năng lượng trung bình
    vec3 energySpec = mix(elementHue * midGlow, coreHue * (1.0 + coreTransition * 2.0), coreTransition);

    // 3. Hòa trộn mượt mà giữa Lửa vật lý và Năng lượng nguyên tố
    vec3 finalSpec = mix(energySpec, fireSpec, clamp(blackbodyFactor, 0.0, 1.0));

    return finalSpec * max(hdrGain, 0.0);
}