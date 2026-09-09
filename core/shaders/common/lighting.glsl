// ============================================================
// WUXING — Lighting Utilities
// Include sau fs_header.glsl trong .fs của skill.
//
// Cung cấp:
//   perturbNormal()  — Normal perturbation qua gradient height field
//   calcFresnel()    — one-sided surface Fresnel rim
//   calcTwoSidedFresnel() — winding-independent rim for two-sided sheets
//   calcOpticalDepth*() — volume body/rim terms from |N.V|
//   calcSpecular()   — Specular Blinn-Phong
//   calcDiffuse()    — Lambertian diffuse với ambient floor
//
// Không phụ thuộc vào bất kỳ uniform skill-specific nào.
// lightDir chuẩn của project: normalize(vec3(0.5, 0.8, 0.5))
// ============================================================

// Tính normal bị nhiễu từ gradient của một height field.
//
//   baseNormal  — normal gốc từ mesh (fragNormal đã normalize)
//   heightDelta — vec2(h(u-eps) - h(u+eps),  h(v-eps) - h(v+eps))
//                 tức gradient theo U và V của hàm height skill tự cung cấp
//   strength    — cường độ biến dạng, thường 0.3 – 0.8
//
// Pattern dùng trong main():
//   const float eps = 0.02;
//   vec2 hDelta = vec2(
//       myHeight(fragTexCoord - vec2(eps, 0.0)) - myHeight(fragTexCoord + vec2(eps, 0.0)),
//       myHeight(fragTexCoord - vec2(0.0, eps)) - myHeight(fragTexCoord + vec2(0.0, eps))
//   );
//   vec3 normal = perturbNormal(fragNormal, hDelta, 0.5);
vec3 perturbNormal(vec3 baseNormal, vec2 heightDelta, float strength) {
    vec3 tangent = cross(vec3(0.0, 1.0, 0.0), baseNormal);
    if (length(tangent) < 0.1) tangent = cross(vec3(1.0, 0.0, 0.0), baseNormal);
    tangent    = normalize(tangent);
    vec3 bitangent = cross(baseNormal, tangent);
    return normalize(baseNormal
                     + (tangent * heightDelta.x + bitangent * heightDelta.y) * strength);
}

// One-sided Fresnel rim for closed, outward-facing surfaces. Back faces must
// be culled by the caller; treating them as a rim would light the whole back.
// Trả về [0..1]: 0 = nhìn thẳng mặt, 1 = nhìn từ cạnh.
//   power — cao hơn → viền mỏng & sắc hơn (thường 2.0 – 5.0)
float calcFresnel(vec3 normal, vec3 viewDir, float power) {
    return pow(1.0 - max(dot(normal, viewDir), 0.0), power);
}

// Winding-independent Fresnel for a genuinely two-sided surface. This only
// defines the scalar edge term; it deliberately does NOT decide whether a
// back-facing fragment should be discarded.
float calcTwoSidedFresnel(vec3 normal, vec3 viewDir, float power) {
    float absNdotV = clamp(abs(dot(normal, viewDir)), 0.0, 1.0);
    return pow(1.0 - absNdotV, power);
}

// Optical depth is not Fresnel: a convex volume is thickest face-on and
// thinnest at its silhouette. Callers retain control over gains because body
// density and rim scattering are independent artistic parameters.
float calcOpticalDepthBody(float absNdotV, float power) {
    return pow(clamp(absNdotV, 0.0, 1.0), max(power, 0.001));
}

float calcOpticalDepthRim(float absNdotV, float power) {
    return pow(clamp(1.0 - absNdotV, 0.0, 1.0), max(power, 0.001));
}

float combineOpticalDepth(float body, float rim, float bodyWeight, float rimWeight) {
    return clamp(bodyWeight * body + rimWeight * rim, 0.0, 1.0);
}

// Specular Blinn-Phong.
// Trả về [0..1] trước khi scale bởi intensity.
//   shininess — cao hơn → điểm sáng nhỏ & tập trung hơn (thường 32 – 512)
float calcSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, float shininess) {
    vec3 halfVec = normalize(lightDir + viewDir);
    return pow(max(dot(normal, halfVec), 0.0), shininess);
}

// Lambertian diffuse với ambient floor.
// Trả về [ambient..1.0] — cộng trực tiếp vào baseColor như hệ số nhân sáng.
//   ambient — ánh sáng nền tối thiểu (thường 0.10 – 0.25)
//
// Ví dụ:
//   vec3 lightDir = normalize(vec3(0.5, 0.8, 0.5));   // hướng đèn chuẩn
//   float diff = calcDiffuse(normal, lightDir, 0.15);
//   baseColor *= diff;
float calcDiffuse(vec3 normal, vec3 lightDir, float ambient) {
    return max(dot(normalize(normal), normalize(lightDir)), ambient);
}

// ============================================================
// BỨC XẠ VẬT THỂ ĐEN PLANCK (Ghost of Tsushima / Physically-Based Combustion)
//
// Tính toán phân bố năng lượng bức xạ nhiệt quang học theo nhiệt độ Kelvin.
//   tempKelvin: [800.0 .. 5500.0]
//     - < 1000K: Tàn tro muội than (Soot / Ember) tối đen
//     - 1200K - 1600K: Đỏ sẫm than hồng (Deep Crimson)
//     - 1800K - 2400K: Cam lửa bùng cháy rực rỡ (Fiery Orange)
//     - 2800K - 3400K: Vàng sáng chói (Golden Yellow)
//     - > 3800K: Trắng rực năng lượng cao (> 1.0 kích hoạt Bloom HDR)
// ============================================================
vec3 calcBlackbody(float tempKelvin) {
    float T = clamp(tempKelvin, 800.0, 5500.0);
    
    // Kênh Đỏ (R): bốc nhanh từ 1000K
    float r = smoothstep(800.0, 1400.0, T);
    
    // Kênh Xanh lá (G): xuất hiện từ 1400K tạo sắc cam/vàng rực
    float g = smoothstep(1400.0, 3200.0, T);
    g = pow(g, 1.25);
    
    // Kênh Xanh dương (B): xuất hiện ở nhiệt độ cao (>2600K) tạo sắc trắng rực
    float b = smoothstep(2600.0, 4400.0, T);
    b = pow(b, 1.8);
    
    // Độ bức xạ nhiệt năng lượng cao (Radiant Flux / Intensity)
    // T < 1000K: tàn tro nguội lạnh gần như không bức xạ quang năng (radiance -> 0.0)
    // T > 1400K: bắt đầu phát quang ánh đỏ/cam
    // T > 3500K: quang năng vượt ngưỡng 1.0 kích hoạt Bloom
    float radiance = mix(0.0, 3.2, smoothstep(900.0, 4200.0, T));
    
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
//
// Tính toán ánh sáng có hướng kết hợp tự đổ bóng (Beer-Lambert Self-Shadowing)
// và đa tán xạ ánh sáng môi trường (bầu trời + phản xạ mặt đất).
//
//   baseColor    : màu cơ bản của khói (tint)
//   normal       : pháp tuyến bề mặt / pháp tuyến bán cầu ảo (Hemispherical Normal)
//   sunToLight   : vector hướng từ bề mặt về nguồn sáng mặt trời
//   sunColor     : màu ánh sáng mặt trời
//   skyAmbient   : ánh sáng môi trường bầu trời (bù đắp tán xạ phía trên)
//   groundAmbient: ánh sáng phản xạ từ mặt đất (bù đắp tán xạ phía dưới)
//   density      : mật độ hạt khói hiện tại [0..1]
//   shadowExt    : hệ số suy giảm tự đổ bóng (thường 1.5 - 3.0)
// ============================================================
vec3 calcLitVolume(vec3 baseColor, vec3 normal, vec3 sunToLight, vec3 sunColor,
                   vec3 skyAmbient, vec3 groundAmbient, float density, float shadowExt) {
    vec3 N = normalize(normal);
    vec3 L = normalize(sunToLight);
    
    // 1. Ánh sáng tán xạ bán cầu trực tiếp (Half-Lambert wrap)
    float NdotL = dot(N, L);
    float wrapDiff = max(0.0, NdotL * 0.5 + 0.5);
    
    // 2. Tự đổ bóng xuyên thể tích (Beer-Lambert extinction)
    // Phía khuất sáng của thể tích chịu sự suy giảm quang năng lớn nhất
    float opticalPath = (1.0 - max(NdotL, 0.0)) * density * shadowExt;
    float selfShadow = exp(-opticalPath);
    
    vec3 directLight = sunColor * (wrapDiff * selfShadow);
    
    // 3. Đa tán xạ môi trường bù trừ (Multiple scattering approximation)
    // Phân tầng theo phương Y của pháp tuyến: nửa trên đón skyAmbient, nửa dưới đón groundAmbient
    float hemiY = N.y * 0.5 + 0.5;
    vec3 ambientFill = mix(groundAmbient, skyAmbient, hemiY);
    
    return baseColor * (directLight + ambientFill);
}

// ============================================================
// HỢP NHẤT BỨC XẠ NĂNG LƯỢNG & PLASMA (Unified Radiant Energy Pipeline)
//
// Tính toán quang phổ phát quang đồng bộ cho Lửa, Năng Lượng Ngũ Hành và Hạt phát sáng.
//
//   energy          : mức năng lượng / nhiệt độ [0.0 .. 1.0]
//   elementHue      : sắc thái nhận diện nguyên tố (Đỏ Hỏa, Lam Lôi, Lục Mộc, v.v.)
//   coreHue         : màu lõi năng lượng nóng nhất (thường là vec3(1.0, 0.98, 0.92))
//   blackbodyFactor : 1.0 = Bức xạ nhiệt Planck (Lửa), 0.0 = Năng lượng ma thuật nguyên tố,
//                     (0.0 .. 1.0) = Lửa ma thuật lai tạo (Hắc hỏa, Tiên hỏa, Lam hỏa)
//   hdrGain         : hệ số kích phát HDR đẩy vào Bloom (> 1.0)
// ============================================================
vec3 calcRadiantEnergy(float energy, vec3 elementHue, vec3 coreHue, float blackbodyFactor, float hdrGain) {
    float e = clamp(energy, 0.0, 1.0);
    
    // 1. Phổ bức xạ nhiệt vật thể đen Planck (dành cho Lửa vật lý chuẩn Ghost of Tsushima)
    vec3 bb = calcBlackbodyNormalized(e);
    
    // 2. Phổ năng lượng nguyên tố ma thuật (dành cho Lôi điện, Băng lam, Tiên khí)
    float coreTransition = smoothstep(0.60, 1.0, e);
    vec3 energySpec = mix(elementHue * e, coreHue * (1.0 + coreTransition * 2.0), coreTransition);
    
    // 3. Hòa trộn mượt mà giữa Năng lượng nguyên tố và Lửa vật lý Planck
    vec3 finalSpec = mix(energySpec, bb, clamp(blackbodyFactor, 0.0, 1.0));
    
    return finalSpec * max(hdrGain, 0.0);
}


