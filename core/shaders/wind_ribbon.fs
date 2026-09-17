#version 330 core
#include "core/shaders/common/vfx_composite.glsl"

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform sampler2D noiseTex;
uniform vec4 colDiffuse;

uniform float u_time;
uniform float u_noiseScale;     // Tần số cuộn dải
uniform float u_speed1;         // Tốc độ trôi dải 1
uniform float u_speed2;         // Tốc độ trôi dải 2
uniform float u_erosionAmount;  // Tiến trình xói mòn [0.0 .. 1.0]
uniform float u_softness;       // Biên độ mềm
uniform vec4  u_windColor;      // Màu luồng gió

out vec4 finalColor;

void main() {
    float x = fragTexCoord.x;
    float y = fragTexCoord.y;

    // 1. CUỘN VÂN LỤA MƯỢT MÀ THUẬN CHIỀU GIÓ (Smooth Silky Streamlines)
    // Panning scrolls along length (y) in the direction of wind travel (towards head at y=1)
    vec2 uv1 = vec2(x, y * 1.6 - u_time * 2.2);
    vec2 uv2 = vec2(1.0 - x, y * 2.4 - u_time * 3.4);

    float s1 = texture(texture0, uv1).a;
    float s2 = texture(texture0, uv2).a;

    // Giao thoa 2 lớp lụa khí trượt lướt vi phân
    float silk = mix(s1, s2, 0.48);

    // 2. TOÁN TỬ XÓI MÒN MỀM MẠI DẠNG NÉT BÚT THỦY MẶC (Painterly Alpha Erosion)
    // y = 1.0 is HEAD (leading edge: crisp, needle-sharp, low threshold)
    // y = 0.0 is TAIL (trailing filaments: higher threshold, dissolves softly)
    float soft = 0.26;
    float threshold = mix(0.62, 0.08, y) + u_erosionAmount * 0.35;
    float alphaErode = smoothstep(threshold - soft, threshold + soft, silk);

    // 3. BO BIÊN NGANG MỀM (Cross-Ribbon Edge Fade)
    float edgeMask = sin(clamp(x, 0.0, 1.0) * 3.14159265);
    edgeMask = pow(edgeMask, 1.3);

    // 4. VUỐT NHỌN MŨI KIM Ở ĐẦU VÀ TIÊU TAN Ở ĐUÔI
    // y -> 1.0 (head): needle tip tapering
    // y -> 0.0 (tail): soft trail fade
    float headFade = smoothstep(1.0, 0.93, y);
    float tailFade = smoothstep(0.0, 0.28, y);
    float lengthFade = headFade * tailFade;

    // 5. MÀU SẮC LỤA TRẮNG BẠCH TRONG TRẺO (Ethereal Translucent Wind)
    vec4 baseColor = (u_windColor.a > 0.01) ? u_windColor : vec4(0.92, 0.96, 1.0, 0.52);
    vec3 color = baseColor.rgb * colDiffuse.rgb * fragColor.rgb;

    // Điểm nhấn vi sợi phát sáng nhẹ
    float filamentLuminance = pow(silk, 1.6) * 0.35;
    color = mix(color, vec3(1.0), filamentLuminance);

    // 6. TỔNG HỢP ALPHA TRONG SUỐT THANH THOÁT
    float totalAlpha = alphaErode * edgeMask * lengthFade * baseColor.a * colDiffuse.a * fragColor.a;

    if (totalAlpha < 0.003) {
        discard;
    }

    finalColor = VFX_ResolveBody(color, 1.0, totalAlpha);
}
