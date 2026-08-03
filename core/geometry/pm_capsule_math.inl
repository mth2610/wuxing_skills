#ifndef PM_CAPSULE_MATH_INL
#define PM_CAPSULE_MATH_INL

/* ===========================================================================
 * CON NHỘNG — đường bao bán kính của một capsule (stadium tròn xoay)
 *
 * Chỉ chứa TOÁN của hình này; bộ máy quét ở pm_tube.inl (xem ghi chú đầu
 * pm_droplet.inl để biết vì sao không tách bộ máy ra).
 *
 * ── CÔNG THỨC ──────────────────────────────────────────────────────────────
 * Con nhộng theo định nghĩa hình học là một HÌNH TRỤ có hai NỬA CẦU úp vào hai
 * đầu. Không có gì mềm dẻo ở đây — bán kính của thân và bán kính của hai chỏm
 * bằng nhau, nếu không thì mặt bị gãy tại chỗ nối.
 *
 *              ⎧ sqrt(1 - ((c - t)/c)^2)          t < c        (nửa cầu đuôi)
 *      r(t) =  ⎨ 1                                c ≤ t ≤ 1-c  (thân trụ)
 *              ⎩ sqrt(1 - ((t - (1-c))/c)^2)      t > 1-c      (nửa cầu đầu)
 *
 *      với c = capFrac, phần chiều dài mỗi chỏm chiếm
 *
 * Đối xứng, khép kín ở cả hai đầu, và đạo hàm bằng 0 tại hai chỗ nối — nên bề
 * mặt liền, không có gờ. c = 0.5 cho ra hình cầu; c → 0 cho ra ống có hai đầu
 * bịt phẳng.
 *
 * ── NÓ KHÔNG PHẢI ĐƯỜNG BAO CŨ ─────────────────────────────────────────────
 * PM_PROFILE_LEGACY_CAPSULE mang tên "capsule" nhưng là
 *
 *      0.3 + 0.7*sqrt(sin(t*PI))
 *
 * — một hình THẤU KÍNH: phình ở giữa, và dừng ở 0.3 chứ không về 0, nên hai
 * đầu là hai lỗ hổng phải bịt bằng nắp nón. Đó không phải con nhộng: một con
 * nhộng có đoạn thân THẲNG và hai chỏm CẦU. Hồ sơ cũ được giữ nguyên vì beam
 * đang phụ thuộc vào đúng những con số đó, nhưng đừng nhầm hai cái với nhau.
 *
 * ── VÌ SAO KHÔNG CÓ NẮP ────────────────────────────────────────────────────
 * Hai nửa cầu LÀ nắp, và chúng là một phần của đường bao chứ không phải hai
 * quạt tam giác dán thêm. Bịt thêm nắp lên một hình đã khép là dựng hai hình
 * nón bên trong chính nó.
 * ===========================================================================*/

static float PMCapsuleRadius(float t, float capFrac)
{
    float c = (capFrac > 0.0f) ? capFrac : 0.25f;
    if (c > 0.5f) c = 0.5f; /* quá 0.5 thì hai chỏm chồng nhau — đó là hình cầu */

    if (t < c) {
        float u = (c - t) / c; /* 1 tại mút đuôi, 0 tại chỗ nối */
        return sqrtf(fmaxf(0.0f, 1.0f - u * u));
    }
    if (t > 1.0f - c) {
        float u = (t - (1.0f - c)) / c; /* 0 tại chỗ nối, 1 tại mút đầu */
        return sqrtf(fmaxf(0.0f, 1.0f - u * u));
    }
    return 1.0f; /* thân trụ */
}

#endif /* PM_CAPSULE_MATH_INL */
