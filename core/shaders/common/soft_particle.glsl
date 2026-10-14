// ============================================================
// WUXING — Soft Particles (Fragment Shader - ALU & Visually Optimized)
// Include trong .fs của skill khi cần particle/decal tự fade alpha
// thay vì cắt cứng (clipping) khi va chạm rìa hình học khác (đá,
// đài, thực thể...).
//
// Yêu cầu: skill gọi ScreenDistort_BindDepthForSoftParticles(shader, slot)
// (core/screen_distort.h) mỗi frame trước khi Draw, và
// ScreenDistort_UnbindSoftParticleDepth(slot) ngay sau khi vẽ xong.
//
// QUAN TRỌNG: nếu mesh có khả năng nửa-chìm/giao cắt hình học khác,
// skill code PHẢI gọi rlDisableDepthTest() quanh draw call — không chỉ
// rlDisableDepthMask().
//
// Cung cấp:
//   SoftParticle_LinearDepth() — tuyến tính hoá giá trị depth [0..1] (Rút gọn đại số)
//   SoftParticle_Factor()      — [0..1] S-curve alpha fade factor + Near-plane fade
// ============================================================

uniform sampler2D u_cameraDepthTex;
uniform float u_cameraNear;
uniform float u_cameraFar;

// depthSample: giá trị đọc trực tiếp từ depth texture hoặc gl_FragCoord.z,
// đều ở dạng [0..1] non-linear. Trả về khoảng cách thật (world units) tới camera.
// Đã rút gọn đại số từ công thức NDC [-1..1] về [0..1], tiết kiệm 4 phép tính ALU.
float SoftParticle_LinearDepth(float depthSample) {
    return (u_cameraNear * u_cameraFar) /
           max(u_cameraFar - depthSample * (u_cameraFar - u_cameraNear), 1e-5);
}

// fadeDistance : khoảng cách (world units) mà alpha mờ dần về 0 khi fragment
//                tiến gần bề mặt cảnh phía sau (thường 5 - 30 cho hạt nhỏ).
// Trả về [0..1]: 1.0 = không bị che (alpha gốc giữ nguyên),
//                0.0 = ngay sát/đâm xuyên bề mặt (alpha về 0, không còn vết cắt cứng).
// Nhân trực tiếp vào finalColor.a của fragment.
float SoftParticle_Factor(float fadeDistance) {
    vec2 screenUV = gl_FragCoord.xy / max(u_resolution, vec2(1.0));
    
    // Dùng textureLod(..., 0.0) để bỏ qua bước tính đạo hàm mipmap (dFdx/dFdy) không cần thiết
    // trên depth buffer R32F đã được tuyến tính hóa sẵn.
    float sceneLinear = textureLod(u_cameraDepthTex, screenUV, 0.0).r;

    // FAIL-OPEN SAFETY: Nếu depth buffer rỗng/chưa ghi (<= near plane), trả về 1.0
    // để tránh làm tàng hình toàn bộ particle trên màn hình.
    float safeNear = max(u_cameraNear, 0.01);
    if (sceneLinear <= safeNear) return 1.0;

    float fragLinear = SoftParticle_LinearDepth(gl_FragCoord.z);

    // 1. Độ mờ tiếp xúc bề mặt (Scene Geometry Intersection Fade)
    float invFade = 1.0 / max(fadeDistance, 1e-4);
    float surfaceT = clamp((sceneLinear - fragLinear) * invFade, 0.0, 1.0);
    // Đường cong Hermite S-Curve giúp triệt tiêu hiện tượng viền gắt Mach-Band tại rìa cắt
    float surfaceFade = surfaceT * surfaceT * (3.0 - 2.0 * surfaceT);

    // 2. Độ mờ khi áp sát ống kính (Camera Near-Plane Fade)
    // Ngăn chặn hiện tượng particle cắt ngang mặt camera gây chớp hình đột ngột
    float nearFadeSpan = max(fadeDistance * 0.35, 0.25);
    float camT = clamp((fragLinear - safeNear) / nearFadeSpan, 0.0, 1.0);
    float camFade = camT * (2.0 - camT);

    return surfaceFade * camFade;
}