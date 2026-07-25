#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;   
uniform sampler2D u_bloomTex; 

// Cấu hình
uniform float u_bloomEnabled;
uniform float u_bloomIntensity;

uniform float u_chromaticEnabled;
uniform float u_chromaticStrength;

uniform float u_vignetteEnabled;
uniform float u_vignetteRadius;
uniform float u_vignetteSoftness;

uniform float u_colorGradeEnabled;
uniform float u_contrast;
uniform float u_saturation;
uniform vec3 u_colorTint;

uniform vec3 u_shadowTint;
uniform vec3 u_highlightTint;

uniform float u_tonemapEnabled;
uniform float u_exposure;

// Radial blur (Đợt E1a) — screen-space smear away from a focal point. This is
// what makes a burst read as VIOLENT rather than merely bright: brightness
// alone says "there is light here", a directional smear says "something just
// happened here". Folded into the composite rather than run as its own pass —
// one extra pass costs a full-screen read/write, this costs 8 taps only while
// it is on, and the whole loop sits behind a uniform branch so the OFF path is
// a single branch on a value that is constant across the frame.
uniform float u_radialBlurEnabled;
uniform vec2  u_radialBlurCenter;   // screen UV [0..1] of the focal point
uniform float u_radialBlurStrength; // sample offset scale (~0.15 = strong)
uniform float u_radialBlurFalloff;  // UV radius where the blur reaches full strength

out vec4 finalColor;

vec3 acesFilmic(vec3 x) {
    // Đã in-line các hằng số để tránh khai báo biến const trong hàm
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 toCenter = uv - vec2(0.5);

    // [TỐI ƯU 1]: Chỉ lấy mẫu texture0 MỘT LẦN duy nhất làm gốc.
    vec4 sceneCol = texture(texture0, uv);

    // [TỐI ƯU 2]: Uniform Branching (Rẽ nhánh Uniform).
    // Vì u_chromaticEnabled đồng nhất trên toàn màn hình, lệnh 'if' này có chi phí gần như bằng 0 
    // và cứu GPU khỏi 2 lần fetch texture thừa khi hiệu ứng tắt.
    if (u_chromaticEnabled > 0.5) {
        
        // [TỐI ƯU 3]: Thay length() bằng dot() để triệt tiêu hàm căn bậc 2 (sqrt) đắt đỏ.
        // Falloff bằng bình phương (distSq) sẽ tạo cảm giác méo dồn ra viền màn hình tự nhiên hơn.
        float distSq = dot(toCenter, toCenter);
        vec2 offset = toCenter * (u_chromaticStrength * distSq * 0.05);
        
        sceneCol.r = texture(texture0, uv - offset).r;
        // Kênh .g đã có sẵn trong sceneCol từ lần fetch trên cùng, KHÔNG FETCH LẠI!
        sceneCol.b = texture(texture0, uv + offset).b;
    }

    // 1b. Radial blur — AFTER chromatic (so it smears the aberrated image, not
    // the other way round, which would re-sharpen the fringes) and BEFORE bloom,
    // so the bloom that follows blooms the smear rather than the sharp source.
    if (u_radialBlurEnabled > 0.5) {
        vec2  dir = uv - u_radialBlurCenter;
        // The focal point stays SHARP and the smear grows outward. Blurring
        // uniformly reads as a camera fault; blurring around a fixed point
        // reads as force radiating from that point.
        float w = smoothstep(0.0, u_radialBlurFalloff, length(dir));
        if (w > 0.001) {
            vec3 acc = vec3(0.0);
            for (int i = 1; i <= 8; i++) {
                float s = float(i) / 8.0;
                acc += texture(texture0, uv - dir * (u_radialBlurStrength * s * w)).rgb;
            }
            sceneCol.rgb = mix(sceneCol.rgb, acc * 0.125, w);
        }
    }

    // 2. Bloom
    if (u_bloomEnabled > 0.5) {
        sceneCol.rgb += (texture(u_bloomTex, uv).rgb * u_bloomIntensity);
    }

    // 2b. Tone mapping
    if (u_tonemapEnabled > 0.5) {
        sceneCol.rgb = acesFilmic(sceneCol.rgb * u_exposure);
    }

    // 3. Color Grading
    if (u_colorGradeEnabled > 0.5) {
        vec3 gradedCol = sceneCol.rgb;
        
        // Contrast
        gradedCol = (gradedCol - vec3(0.5)) * u_contrast + vec3(0.5);
        
        // [TỐI ƯU 4]: Tính toán Luminance (độ sáng) ĐÚNG 1 LẦN.
        // Chuyển vec3 magic number thành hằng số (const) để compiler tối ưu register.
        const vec3 LUMA_COEFF = vec3(0.2126, 0.7152, 0.0722);
        float luma = dot(gradedCol, LUMA_COEFF);
        
        // Saturation
        gradedCol = mix(vec3(luma), gradedCol, u_saturation);
        
        // Split-tone
        // [TỐI ƯU 5]: Xóa bỏ hàm clamp(). 
        // Hàm smoothstep nội bộ đã tự động clamp giá trị đầu vào trong khoảng [edge0, edge1] rồi.
        gradedCol *= mix(u_shadowTint, u_highlightTint, smoothstep(0.0, 1.0, luma));
        
        // Color Tint
        sceneCol.rgb = gradedCol * u_colorTint;
    }

    // 4. Vignette
    if (u_vignetteEnabled > 0.5) {
        // [TỐI ƯU 6]: Tái sử dụng vector toCenter đã tính từ bước đầu tiên thay vì tính lại.
        float len = length(toCenter);
        float darkness = smoothstep(u_vignetteRadius - u_vignetteSoftness, u_vignetteRadius, len);
        sceneCol.rgb *= (1.0 - darkness);
    }

    finalColor = sceneCol * fragColor;
}