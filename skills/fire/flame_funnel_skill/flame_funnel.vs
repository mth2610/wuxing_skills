#version 330
#include "core/shaders/common/vs_header.glsl"
#include "core/shaders/common/noise.glsl"

uniform vec3  u_center;      // tâm đáy phễu (world space)
uniform float u_baseY;       // world Y của vòng đáy
uniform float u_height;      // chiều cao phễu
uniform float u_flickerAmp;  // biên độ đẩy ngang (world units)
uniform float u_scrollSpeed; // tốc độ sóng cuộn lên
uniform float u_ridgeFreq;   // số "lưỡi lửa" quanh chu vi
uniform float u_seed;        // lệch pha riêng mỗi lần cast

void main()
{
    vec3 pos = vertexPosition;

    // h = 0 ở đáy, 1 ở đỉnh. Đáy gần như đứng yên (bén rễ xuống đất),
    // toàn bộ chuyển động dồn về phía đỉnh — giống lửa thật lay ở ngọn,
    // gốc thì ổn định.
    float h = clamp((pos.y - u_baseY) / max(u_height, 0.0001), 0.0, 1.0);

    vec2 toCenter = pos.xz - u_center.xz;
    float dist = length(toCenter);
    vec2 radialDir = (dist > 0.0001) ? (toCenter / dist) : vec2(1.0, 0.0);
    float angle = atan(toCenter.y, toCenter.x);

    // Sóng "cuộn từ dưới lên": giữ phase cố định thì h phải tăng khi
    // u_time tăng -> đỉnh sóng leo dần từ đáy lên ngọn mỗi frame, đúng
    // kiểu lửa phập phồng cuộn lên thay vì rung tại chỗ.
    vec2 wavePos = vec2(angle * u_ridgeFreq * 0.5 + u_seed, h * 3.0 - u_time * u_scrollSpeed);
    float wave = fbm2(wavePos) * 2.0 - 1.0;

    // Biên độ tăng dần theo chiều cao (đáy gần như bằng 0) để giữ chân lửa
    // bám đất, chỉ phần thân/ngọn mới "phập phồng".
    float envelope = h * h;
    float radialPush = wave * u_flickerAmp * envelope;

    // Nhịp thở tổng thể — toàn khối phồng/xẹp nhẹ, cộng thêm trên sóng lưỡi lửa.
    float breathe = 0.05 * sin(u_time * 2.4 + u_seed) * envelope;

    pos.x += radialDir.x * radialPush;
    pos.z += radialDir.y * radialPush;
    pos.y += breathe * u_height;

    VS_FinalOutput(pos);
    // Biên độ dịch chuyển nhỏ so với bán kính phễu nên dùng normal gốc
    // (chưa xoay) là đủ — cùng cách xấp xỉ mà DisplaceVertex_Noise dùng,
    // xem SHADER_API.md.
}
