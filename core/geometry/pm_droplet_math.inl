#ifndef PM_DROPLET_MATH_INL
#define PM_DROPLET_MATH_INL

/* ===========================================================================
 * GIỌT NƯỚC — đường bao bán kính, MỘT biểu thức giải tích
 *
 *      r(t) = t^a * sqrt(1 - t^2)        t in [0,1], chuẩn hoá đỉnh về 1
 *
 * ── VÌ SAO MỘT BIỂU THỨC, KHÔNG PHẢI HAI MẢNH ──────────────────────────────
 * Bản đầu ghép một đoạn luỹ thừa vào một chỏm cầu tại "vai". Hai mảnh khớp
 * GIÁ TRỊ ở đó (cùng bằng 1) nhưng không khớp ĐẠO HÀM: đuôi tới với độ dốc
 * p/hs ~ 2.4, chỏm cầu rời đi với độ dốc 0. Bước nhảy đạo hàm là một NẾP GÃY
 * trên mặt, và mắt bắt được ngay — nó đọc ra đúng cái nó là: một bán cầu dán
 * vào một cái đuôi.
 *
 * Đo được, cùng dải [0.003, 0.985]:
 *      ghép hai mảnh : nhảy 2.416 tại t = 0.660   (đúng chỗ nối)
 *      một biểu thức : nhảy 0.495 tại t = 0.985   (tiếp tuyến vòm, hợp lệ)
 *
 * Biểu thức này trơn vô hạn trên (0,1) — không có chỗ nối để mà thô.
 *
 * ── VÌ SAO NÓ ĐÚNG LÀ GIỌT NƯỚC ────────────────────────────────────────────
 *   t -> 0:  r ~ t^a                 => MŨI NHỌN, a quyết định độ thon
 *   t -> 1:  1 - t^2 = (1-t)(1+t) ~ 2(1-t)
 *            nên r ~ sqrt(2(1-t))    => tiếp tuyến THẲNG ĐỨNG = VÒM TRÒN
 *
 * Vòm ở đầu không phải mảnh dán vào: nó rơi ra từ chính căn bậc hai. Đó là
 * hình một giọt rơi thật — sức căng bề mặt bo tròn đầu, lực cản vuốt nhọn đuôi.
 *
 *   đỉnh tại  t* = sqrt(a/(a+1))     LUÔN sau điểm giữa, về phía đầu. Đó là
 *                                    dấu phân biệt với thấu kính đối xứng.
 *
 * ── THAM SỐ a (tailSharp, mặc định 1.6) ────────────────────────────────────
 *   a = 1    đỉnh 0.71, đuôi thẳng như hình nón
 *   a = 1.6  đỉnh 0.78, đuôi hơi lõm — giọt rơi điển hình
 *   a = 3    đỉnh 0.87, đuôi rất dài và mảnh
 *   a < 1    đuôi PHÌNH thay vì thon — không còn là giọt nước
 * ===========================================================================*/

static float PMDropletRadius(float t, float tailSharp)
{
    float a = (tailSharp > 0.0f) ? tailSharp : 1.6f;
    if (t <= 0.0f || t >= 1.0f) return 0.0f;

    /* Chuẩn hoá đỉnh về đúng 1; thiếu nó thì bán kính caller yêu cầu bị co lại
     * theo a một cách vô hình. */
    float peakT = sqrtf(a / (a + 1.0f));
    float norm = powf(peakT, a) * sqrtf(fmaxf(0.0f, 1.0f - peakT * peakT));
    if (norm < 1e-6f) norm = 1e-6f;

    return powf(t, a) * sqrtf(fmaxf(0.0f, 1.0f - t * t)) / norm;
}

#endif /* PM_DROPLET_MATH_INL */
