#ifndef PM_DROPLET_MATH_INL
#define PM_DROPLET_MATH_INL

/* ===========================================================================
 * GIỌT NƯỚC — đường bao bán kính của một giọt đang rơi
 *
 * Chỉ chứa TOÁN của hình này. Bộ máy quét (khung vận chuyển, deform, dựng
 * ring, vẽ) nằm ở pm_tube.inl và dùng chung cho mọi hình — nhân bản nó ra ba
 * file là mời ba bản phân kỳ, và khung vận chuyển là phần đã tốn bốn vòng để
 * làm đúng.
 *
 * ── CÔNG THỨC ──────────────────────────────────────────────────────────────
 * Một giọt nước rơi thật KHÔNG đối xứng: đầu tròn đầy do sức căng bề mặt, đuôi
 * vuốt thành một mũi do lực cản. Đường bao là hai mảnh nối liền tại vai:
 *
 *              ⎧ (t / h)^p                       t < h      (mũi, luật luỹ thừa)
 *      r(t) =  ⎨
 *              ⎩ sqrt(1 - ((t - h)/(1-h))^2)     t ≥ h      (chỏm cầu)
 *
 *      với h = 1 - headFrac  (vị trí vai), p = tailSharp
 *
 * Tại t = h cả hai mảnh đều bằng 1 → đường bao liên tục. Tại t = 0 và t = 1
 * đều bằng 0 → hình TỰ KHÉP ở cả hai đầu.
 *
 * ── VÌ SAO KHÔNG CÓ NẮP ────────────────────────────────────────────────────
 * Vì nó tự khép. Bản cũ dùng một đường bao đối xứng không bao giờ về 0 (nó
 * dừng ở 0.3), nên hai đầu là hai lỗ hổng, và chúng được bịt bằng hai quạt tam
 * giác có ĐỈNH đẩy ra ngoài theo tiếp tuyến — tức hai hình NÓN. Đó là cái đầu
 * bút chì. Nắp không phải một tuỳ chọn cần tắt cho hình này; với một hình tự
 * khép thì nắp là sai.
 *
 * ── THAM SỐ ────────────────────────────────────────────────────────────────
 *   tailSharp  (mặc định 1.6) 1 = mũi thẳng hình nón; lớn hơn = mũi thon hơn,
 *                             vai đầy hơn. Dưới 1 cho ra đuôi phình, không
 *                             phải giọt nước.
 *   headFrac   (mặc định 0.34) phần chiều dài dành cho chỏm cầu. Lớn hơn = đầu
 *                             tròn hơn và chỗ phình lùi về phía đuôi.
 *
 * Chỗ phình nằm ở vai, tức t = 1 - headFrac ≈ 0.66 — SAU điểm giữa, về phía
 * đầu. Đó là dấu hiệu phân biệt với hình thấu kính đối xứng phình đúng giữa.
 * ===========================================================================*/

static float PMDropletRadius(float t, float tailSharp, float headFrac)
{
    float h = (headFrac > 0.0f) ? headFrac : 0.34f;
    if (h > 0.9f) h = 0.9f;
    float p = (tailSharp > 0.0f) ? tailSharp : 1.6f;

    float shoulder = 1.0f - h;
    if (t >= shoulder) {
        /* Chỏm cầu: u = 0 tại vai, 1 tại đỉnh. */
        float u = (t - shoulder) / h;
        return sqrtf(fmaxf(0.0f, 1.0f - u * u));
    }
    /* Mũi: luỹ thừa từ 0 tại đuôi lên 1 tại vai. */
    return powf((shoulder > 1e-5f) ? (t / shoulder) : 1.0f, p);
}

#endif /* PM_DROPLET_MATH_INL */
