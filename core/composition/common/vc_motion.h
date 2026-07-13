#ifndef VC_MOTION_H
#define VC_MOTION_H

// Motion Library — Layer "chuyển động" của Visual Composer.
// Toàn bộ là hàm thuần toán học, stateless, static inline: nhận (time, tham số)
// trả về vị trí/hướng/hệ số — không pool, không side effect. Dùng để lắp quỹ đạo
// cho particle/mesh/light trong composition và skill thay vì tự viết sin/cos ad-hoc.
//
// Quy ước: trục đứng là Y (Y=0 = mặt đất), mọi góc tính bằng radian,
// "XZ" = mặt phẳng ngang.

#include "raylib.h"
#include "raymath.h"
#include <math.h>

#ifndef PI
#define PI 3.1415926535f
#endif

// ---------------------------------------------------------------------------
// Vị trí
// ---------------------------------------------------------------------------

// Điểm trên vòng tròn ngang quanh center — khối lắp cơ bản nhất (spawn ring,
// orbit, vành đai). Giữ nguyên center.y.
static inline Vector3 VC_RingPointXZ(Vector3 center, float radius, float angle)
{
    return (Vector3){ center.x + cosf(angle) * radius,
                      center.y,
                      center.z + sinf(angle) * radius };
}

// Quỹ đạo tròn đều quanh center: angle = time*speed + phase. Lệch `phase`
// (vd. i * 2*PI/count) để rải nhiều vật thể quanh vòng.
static inline Vector3 VC_MotionOrbit(Vector3 center, float radius, float speed, float time, float phase)
{
    return VC_RingPointXZ(center, radius, time * speed + phase);
}

// Xoắn ốc quanh trục Y đi lên từ base: xoay spinSpeed rad/s, dâng riseSpeed m/s.
static inline Vector3 VC_MotionHelix(Vector3 base, float radius, float riseSpeed, float spinSpeed, float t, float phase)
{
    Vector3 p = VC_RingPointXZ(base, radius, t * spinSpeed + phase);
    p.y = base.y + t * riseSpeed;
    return p;
}

// Xoáy hút vào tâm: t01 đi 0→1, bán kính co startRadius→0 sau `turns` vòng.
// Dùng cho hiệu ứng "năng lượng bị hút vào" (charge, absorb).
static inline Vector3 VC_MotionSpiralIn(Vector3 center, float startRadius, float turns, float phase, float t01)
{
    float angle = phase + t01 * turns * 2.0f * PI;
    return VC_RingPointXZ(center, startRadius * (1.0f - t01), angle);
}

// Rung lắc quanh pos: sin 3 trục tần số lệch nhau (không đồng pha nên không
// đọc thành dao động tuyến tính). `seed` tách pha giữa các vật thể cùng rung.
static inline Vector3 VC_MotionJitter(Vector3 pos, float amp, float freq, float time, float seed)
{
    return (Vector3){ pos.x + amp * sinf(time * freq + seed),
                      pos.y + amp * 0.6f * sinf(time * freq * 1.17f + seed * 2.3f),
                      pos.z + amp * cosf(time * freq * 0.83f + seed * 4.7f) };
}

// Dập dềnh dọc Y quanh base (vật thể lơ lửng "thở" lên xuống).
static inline Vector3 VC_MotionBob(Vector3 base, float amp, float freq, float time, float phase)
{
    base.y += amp * sinf(time * freq + phase);
    return base;
}

// ---------------------------------------------------------------------------
// Hướng
// ---------------------------------------------------------------------------

// Tiếp tuyến của vòng tròn ngang tại góc `angle` (chiều quay dương) kèm thành
// phần dâng `up` — vận tốc cho hạt bay quanh vành đai. KHÔNG chuẩn hóa,
// call site tự Vector3Scale theo tốc độ muốn có.
static inline Vector3 VC_TangentXZ(float angle, float up)
{
    return (Vector3){ -sinf(angle), up, cosf(angle) };
}

// Hướng ngẫu nhiên trong hình nón quanh dir, góc mở coneRad.
// u1, u2 ∈ [0..1] (đưa Random01() vào) — u1 = phương vị, u2 = độ lệch trục.
static inline Vector3 VC_DirCone(Vector3 dir, float coneRad, float u1, float u2)
{
    Vector3 d = Vector3Normalize(dir);
    Vector3 ref = (fabsf(d.y) < 0.99f) ? (Vector3){ 0.0f, 1.0f, 0.0f }
                                       : (Vector3){ 1.0f, 0.0f, 0.0f };
    Vector3 t1 = Vector3Normalize(Vector3CrossProduct(d, ref));
    Vector3 t2 = Vector3CrossProduct(d, t1);
    float phi = u1 * 2.0f * PI;
    float theta = u2 * coneRad;
    float s = sinf(theta);
    return Vector3Add(Vector3Scale(d, cosf(theta)),
                      Vector3Add(Vector3Scale(t1, s * cosf(phi)),
                                 Vector3Scale(t2, s * sinf(phi))));
}

// ---------------------------------------------------------------------------
// Shaper vô hướng (modifier cho size/alpha/emissive theo thời gian)
// ---------------------------------------------------------------------------

// Sóng sin chuẩn hóa 0..1 — cho alpha/emissive nhấp nhô tuần hoàn.
static inline float VC_Pulse01(float time, float freq)
{
    return 0.5f + 0.5f * sinf(time * freq);
}

// Hệ số "thở" 1±amp — nhân thẳng vào radius/scale (vd. VC_Breathe(t, 2.4f, 0.015f)).
static inline float VC_Breathe(float time, float freq, float amp)
{
    return 1.0f + amp * sinf(time * freq);
}

// Nhiễu nhấp nháy 0..1 kiểu hash — đổi giá trị hỗn loạn theo frame, cho lửa/điện.
// `seed` tách pha giữa các vật thể (vd. lấy từ vị trí spawn) để không chớp đồng bộ.
static inline float VC_Flicker01(float time, float seed)
{
    float f = sinf(time * 12.9898f + seed * 78.233f) * 43758.5453f;
    return f - floorf(f);
}

#endif // VC_MOTION_H
