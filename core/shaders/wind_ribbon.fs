#version 330 core
#include "core/shaders/common/vfx_composite.glsl"
#include "core/shaders/common/noise.glsl"

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

    // 1. CUỘN KHÓI TRÔI & BIẾN DẠNG DÒNG CHẤT LƯU (Fluid Smoke Curl & Drift)
    // Tần số kéo dài dọc theo luồng gió (y * 1.5) tạo dải khói dài khí động học, không đứt vụn thành khúc nhỏ
    vec2 smokeUV = vec2(x * 2.2, y * 1.5 - u_time * 1.6);
    float smokeNoise = fbm2(smokeUV);

    // Xoáy vi mô làm rách tưa và uốn lượn sợi khói
    float curlX = (vnoise(smokeUV) - 0.5) * 0.022;
    float driftY = (vnoise(smokeUV + vec2(3.7, 6.1)) - 0.5) * 0.035;

    // 2. LẤY MẪU 2 TẦNG CUỘN CHẢY VI PHÂN (Differential smoke shear)
    // 2 tầng trôi lệch nhẹ vận tốc và cuộn xé vi sai (shear), tạo hiệu ứng tưa rách sợi tự nhiên
    vec2 uv1 = vec2(x + curlX, y - u_time * 1.8 + driftY);
    vec2 uv2 = vec2(x - curlX * 0.75, y * 1.02 - u_time * 2.5 - driftY * 0.5);

    float s1 = texture(texture0, uv1).a;
    float s2 = texture(texture0, uv2).a;

    // Giao thoa 2 tầng lụa khí vi phân
    float silk = mix(s1, s2, 0.48);

    // 3. TOÁN TỬ XÓI MÒN DẠNG KHÓI TRÔI ĐỨT QUÃNG & XÉ RÁCH SỢI (Smoke Erosion & Tears)
    // soft = 0.28 giúp các vết rách tưa mềm mại như mây khói, triệt tiêu hoàn toàn hiện tượng đứt khúc nhỏ/xúc xích
    float soft = 0.28;
    float baseThreshold = mix(0.18, 0.06, y) + u_erosionAmount * 0.18;
    float threshold = baseThreshold + (smokeNoise - 0.5) * 0.15;
    float alphaErode = smoothstep(threshold - soft, threshold + soft, silk);

    // 4. TRIỆT TIÊU MÉP LƯỚI HÌNH HỌC (Ghost of Tsushima Edge Attenuation: 4.0 * V * (1.0 - V))
    float edgeMask = clamp(4.0 * x * (1.0 - x), 0.0, 1.0);
    edgeMask = pow(edgeMask, 1.25);

    // 5. VUỐT NHỌN MŨI KIM Ở ĐẦU VÀ TIÊU TAN Ở ĐUÔI
    float headFade = smoothstep(1.0, 0.92, y);
    float tailFade = smoothstep(0.0, 0.18, y);
    float lengthFade = headFade * tailFade;

    // 6. PHÂN CẤP ÁNH SÁNG (Sợi giữa hơi đậm hơn với lõi trắng, 2 sợi cạnh mờ khói sương)
    vec4 baseColor = (u_windColor.a > 0.01) ? u_windColor : vec4(0.90, 0.95, 1.0, 0.55);
    vec3 mistColor = baseColor.rgb * colDiffuse.rgb * fragColor.rgb;
    vec3 coreWhite = vec3(0.98, 1.0, 1.0);

    // Sợi giữa có silk cao chuyển dần sang ánh sáng trắng thanh khiết
    float centerGlow = smoothstep(0.40, 0.82, silk);
    vec3 color = mix(mistColor, coreWhite, centerGlow * 0.65);

    // 7. TỔNG HỢP ALPHA KHÓI TRÔI (Sợi giữa đậm nét hơn, 2 sợi cạnh mờ thanh thoát)
    float strandAlpha = alphaErode * (0.40 + centerGlow * 0.45);
    float totalAlpha = clamp(strandAlpha * edgeMask * lengthFade * baseColor.a * colDiffuse.a * fragColor.a * 1.4, 0.0, 1.0);

    if (totalAlpha < 0.003) {
        discard;
    }

    finalColor = VFX_ResolveBody(color, 1.0, totalAlpha);
}
