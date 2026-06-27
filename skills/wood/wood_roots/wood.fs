#version 330

// Nhận trực tiếp dữ liệu hình học nội suy từ dải Ribbon 3D
in vec2 fragTexCoord;
in vec4 fragColor;

uniform float u_time;
uniform float u_glowPass; // 0 = pass diffuse, 1 = pass bloom cộng sáng

out vec4 finalColor;

// 2D Pseudo-Random Noise
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// 2D Value Noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i + vec2(0.0,0.0)), hash(i + vec2(1.0,0.0)), u.x),
               mix(hash(i + vec2(0.0,1.0)), hash(i + vec2(1.0,1.0)), u.x), u.y);
}

// 3-Octave FBM tạo vân gió đung đưa hữu cơ
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (int i = 0; i < 3; ++i) {
        v += a * noise(p);
        p = rot * p * 2.0;
        a *= 0.5;
    }
    return v;
}

void main() {
    // TỐI ƯU 3D: Trích xuất trực tiếp thông số đa kênh từ dữ liệu đỉnh Ribbon C cung cấp
    float localU      = fragColor.r; // Chạy từ 0.0 -> 1.0 dọc chiều dài rễ
    float localV      = fragTexCoord.x; // Trục hoành của Ribbon (0.0 ở biên trái, 0.5 ở tâm, 1.0 ở biên phải)
    float elementType = fragColor.b; // Định danh hệ mộc (Rễ cây, lá, hoa)
    float baseAlpha   = fragColor.a; // Tiến trình phai màu / rã xác theo thời gian

    // 1. UV warping tạo hiệu ứng rễ cây lay động trước gió trong không gian 3D
    float warpAmount = mix(0.003, 0.009, smoothstep(0.3, 1.0, baseAlpha));
    vec2 warpUV = vec2(localU, localV);
    vec2 flow = vec2(u_time * 0.4, -u_time * 0.6);
    float nDistort = fbm(vec2(localU, localV) * 8.0 + flow);
    warpUV.x += (nDistort - 0.5) * warpAmount;
    warpUV.y += (nDistort - 0.5) * warpAmount;

    // Cập nhật lại tọa độ sau khi bị gió uốn xoắn
    localU = warpUV.x;
    localV = warpUV.y;

    vec3 finalRGB = vec3(0.0);
    float finalAlpha = 0.0;

    vec3 glowRGB = vec3(0.0);
    float glowAlpha = 0.0;

    // --- CHỈ XỬ LÝ PHÂN ĐOẠN RỄ CÂY VỎ GỖ (elementType < 0.2) ---
    if (elementType < 0.2) {
        // Thuật toán hủy diệt / rã rụng vỏ cây khi rễ cây hết tuổi thọ
        if (baseAlpha < 0.99) {
            float barkNoise = noise(vec2(localU * 40.0, localV * 50.0));
            if (barkNoise * 0.95 > baseAlpha) {
                discard;
            }
        }

        float sapStrength = smoothstep(0.0, 0.3, baseAlpha);

        // Bảng màu vỏ cây tiêu chuẩn của bạn
        vec3 darkBark  = vec3(0.12, 0.07, 0.03);
        vec3 midBark   = vec3(0.28, 0.17, 0.08);
        vec3 mossGreen = vec3(0.15, 0.32, 0.12);

        // Giả lập khối hình trụ 3D cylinder normal shading (Đỉnh khối nằm ở tâm dải V = 0.5)
        float normal = sin(localV * 3.14159265);
        float shading = 0.35 + 0.65 * normal;

        vec3 barkColor = mix(darkBark, midBark, normal);
        // Đắp các mảng rêu dựa trên nhiễu rải rác
        barkColor = mix(barkColor, mossGreen, noise(vec2(localU * 6.0, localV * 3.0)) * 0.35);

        // Cơ chế hóa than / mục nát khi rễ cây biến mất
        vec3 dryWoodColor = vec3(0.09, 0.07, 0.05);
        barkColor = mix(dryWoodColor, barkColor, smoothstep(0.4, 0.9, baseAlpha));

        // Tạo vân thớ gỗ dọc uốn lượn theo dải 3D Ribbon
        vec2 barkUV = vec2(localU * 12.0, localV * 2.5);
        float barkTexture = noise(barkUV + noise(barkUV * 1.5) * 0.3) * 0.22;
        barkColor += vec3(barkTexture * (1.0 - normal * 0.3));

        // Vân rạn nứt vàng kim khảm sơn mài ma thuật
        float crackNoise = noise(vec2(localU * 18.0 + 4.0, localV * 9.0 + 9.0));
        float crackLine = smoothstep(0.965, 0.985, crackNoise) - smoothstep(0.985, 1.0, crackNoise);
        vec3 goldCrack = vec3(0.95, 0.75, 0.2) * crackLine * sapStrength * 0.85;
        barkColor += goldCrack;

        // Dòng nhựa ma thuật phát sáng chạy dọc tâm rễ (localV = 0.5)
        float distToCenter = abs(localV - 0.5) * 2.0;
        float sapLineCore = smoothstep(0.16, 0.0, distToCenter);
        float sapHalo = smoothstep(0.55, 0.0, distToCenter) * 0.5;
        float sapPulse = sin(localU * 22.0 - u_time * 6.0) * 0.5 + 0.5;

        vec3 sapCoreColor = vec3(0.55, 1.0, 0.55) * sapLineCore * sapPulse * 0.9 * sapStrength;
        vec3 sapHaloColor = vec3(0.15, 0.55, 0.35) * sapHalo * sapStrength;

        barkColor += sapCoreColor;
        barkColor += sapHaloColor;

        // Điểm phản xạ Specular dọc dải trụ 3D
        vec3 specHighlight = vec3(0.15, 0.25, 0.15) * pow(normal, 6.0) * sapStrength;
        barkColor += specHighlight;

        finalRGB = barkColor * shading;
        finalAlpha = baseAlpha;

        // Tách riêng lõi nhựa phát sáng để nạp cho Pass cộng sáng (Bloom) ở phía mã C
        glowRGB = sapCoreColor + sapHaloColor + goldCrack + specHighlight;
        glowAlpha = clamp(sapLineCore + sapHalo * 0.6 + crackLine, 0.0, 1.0) * baseAlpha;
    }

    // Ép biên an toàn và kết xuất màu dựa trên cấu hình Pass đồ họa hiện tại
    if (u_glowPass > 0.5) {
        finalColor = vec4(glowRGB, clamp(glowAlpha, 0.0, 1.0));
    } else {
        finalColor = vec4(finalRGB, clamp(finalAlpha, 0.0, 1.0));
    }
}