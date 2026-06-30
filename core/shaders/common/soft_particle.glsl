// ============================================================
// WUXING — Soft Particles (Fragment Shader)
// Include trong .fs của skill khi cần particle/decal tự fade alpha
// thay vì cắt cứng (clipping) khi va chạm rìa hình học khác (đá,
// đài, thực thể...).
//
// Yêu cầu: skill gọi ScreenDistort_BindDepthForSoftParticles(shader, slot)
// (core/screen_distort.h) mỗi frame trước khi Draw, và
// ScreenDistort_UnbindSoftParticleDepth(slot) ngay sau khi vẽ xong. Depth
// trễ 1 frame (xem ghi chú trong screen_distort.h) — chấp nhận được cho
// soft-particle fade.
//
// Cung cấp:
//   SoftParticle_LinearDepth() — tuyến tính hoá giá trị depth [0..1]
//   SoftParticle_Factor()      — [0..1] alpha fade factor, nhân trực
//                                 tiếp vào finalColor.a
//
// Xem CORE_API.md — Screen Distort, mục "Soft Particles" để biết cách dùng.
// ============================================================

uniform sampler2D u_cameraDepthTex;
uniform float u_cameraNear;
uniform float u_cameraFar;

// depthSample: giá trị đọc trực tiếp từ depth texture hoặc gl_FragCoord.z,
// đều ở dạng NDC [0..1] non-linear. Trả về khoảng cách thật (world units)
// tới camera.
float SoftParticle_LinearDepth(float depthSample) {
    float ndc = depthSample * 2.0 - 1.0;
    return (2.0 * u_cameraNear * u_cameraFar) /
           (u_cameraFar + u_cameraNear - ndc * (u_cameraFar - u_cameraNear));
}

// fadeDistance : khoảng cách (world units) mà alpha mờ dần về 0 khi fragment
//                tiến gần bề mặt cảnh phía sau (thường 5 - 30 cho hạt nhỏ).
// Trả về [0..1]: 1.0 = không bị che (alpha gốc giữ nguyên),
//                0.0 = ngay sát/đâm xuyên bề mặt (alpha về 0, không còn vết cắt cứng).
// Nhân trực tiếp vào finalColor.a của fragment.
float SoftParticle_Factor(float fadeDistance) {
    vec2 screenUV = gl_FragCoord.xy / u_resolution;
    // u_cameraDepthTex stores ALREADY-LINEARIZED world-space depth (written by
    // core/shaders/depth_copy.fs into an R32F target) — do NOT linearize it
    // again. Only the fragment's own gl_FragCoord.z needs linearizing.
    float sceneLinear = texture(u_cameraDepthTex, screenUV).r;
    float fragLinear  = SoftParticle_LinearDepth(gl_FragCoord.z);
    float diff = sceneLinear - fragLinear;
    return clamp(diff / max(fadeDistance, 0.0001), 0.0, 1.0);
}
